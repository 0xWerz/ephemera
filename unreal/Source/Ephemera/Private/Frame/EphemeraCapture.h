#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/UObjectIterator.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Frame/EphemeraBody.h"
#include "Frame/EphemeraBodyDefinition.h"
#include "Frame/EphemeraFrameSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Player/EphemeraPlayer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "RHI.h"
#include "ShaderCompiler.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif
#include "Subsystems/WorldSubsystem.h"
#include "UnrealClient.h"
#include "EphemeraCapture.generated.h"

/**
 * Test-only: `-EphemeraShot=<file.png>` saves one screenshot of the running game and quits
 * (`-EphemeraShotUI` includes what is drawn over the world: the loading screen, the readouts).
 * It waits for shaders to finish compiling and then for `-EphemeraShotDelay=<seconds>` (default 12)
 * so Lumen, virtual shadow maps and eye adaptation have settled. Optional:
 *   -EphemeraTime=<seconds>   start the solar system at this time of day
 *   -EphemeraLook=<pitch>,<yaw>   turn the player's view (degrees) before the shot
 *   -EphemeraPhoto=<body>|<x,y,z>|<height m>|<target>|<fov>|<yaw,pitch>|<roll>
 *       A free camera: above <body> in planet-local direction x,y,z, <height> metres over the
 *       ground, aimed at <target> (a body id, or "at:x,y,z,h" for a point over the same body's
 *       ground), with a horizontal field of view in degrees, then nudged by yaw,pitch for
 *       framing and rolled. Example: dawnhollow|1,0.1,0.12|1.6|hush|24|-3,2|0
 *   -EphemeraPhotoList=<file> -EphemeraShotDir=<folder> [-EphemeraShotSettle=<seconds>]
 *       Many shots from one launch (starting the game is most of a single shot's cost). Each line of
 *       the file is name|time_seconds|photo spec; the camera moves, lighting settles, <name>.png is saved.
 * Used by `unreal/Tools/unreal.py shot` and `shots`, which keep the window invisible on macOS.
 */
UCLASS()
class UEphemeraCaptureSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override
	{
		FString Unused;
		return (FParse::Value(FCommandLine::Get(), TEXT("EphemeraShot="), Unused) || FParse::Value(FCommandLine::Get(), TEXT("EphemeraPhotoList="), Unused))
			&& Super::ShouldCreateSubsystem(Outer);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UEphemeraCaptureSubsystem, STATGROUP_Tickables); }

	virtual void Tick(float DeltaTime) override
	{
		if (!bStarted)
		{
			bStarted = true;
			FParse::Value(FCommandLine::Get(), TEXT("EphemeraShot="), File);
			FParse::Value(FCommandLine::Get(), TEXT("EphemeraShotDelay="), Delay);
			FParse::Value(FCommandLine::Get(), TEXT("EphemeraShotSettle="), Settle);
			FParse::Value(FCommandLine::Get(), TEXT("EphemeraShotDir="), ShotDir);
			FString ListFile;
			if (FParse::Value(FCommandLine::Get(), TEXT("EphemeraPhotoList="), ListFile))
			{
				TArray<FString> Lines;
				FFileHelper::LoadFileToStringArray(Lines, *ListFile);
				for (const FString& Line : Lines)
				{
					const FString Trimmed = Line.TrimStartAndEnd();
					FString Name, Rest, TimeText, Spec;
					if (!Trimmed.IsEmpty() && !Trimmed.StartsWith(TEXT("#")) && Trimmed.Split(TEXT("|"), &Name, &Rest) && Rest.Split(TEXT("|"), &TimeText, &Spec))
					{
						Shots.Add({Name, FCString::Atod(*TimeText), Spec});
					}
				}
				bList = true;
			}
		}
		if (!bTimeSet && ++Frame > 12)
		{
			bTimeSet = true;
			double StartTime = 0.0;
			if (FParse::Value(FCommandLine::Get(), TEXT("EphemeraTime="), StartTime))
			{
				if (UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>())
				{
					Frames->SetTime(StartTime);
				}
			}
		}
		// Shader compilation can take minutes on a cold cache, and a texture or mesh that has just
		// been rebuilt is drawn as a placeholder until its data is ready (a freshly baked depth map
		// reads as flat grey, for one): the clock only runs once all of that is done.
		bool bPreparing = GShaderCompilingManager && GShaderCompilingManager->IsCompiling();
#if WITH_EDITOR
		bPreparing = bPreparing || FAssetCompilingManager::Get().GetNumRemainingAssets() > 0;
#endif
		if (bPreparing)
		{
			// The game's clock keeps running while the engine prepares; on a cold cache that is
			// minutes, enough to turn the afternoon asked for into night. The time is set again
			// once everything is ready.
			bTimeSet = false;
			Frame = 0;
			Waited += DeltaTime;
			if (Waited > 10800.f)
			{
				UE_LOG(LogTemp, Error, TEXT("EphemeraShot: shaders or assets still being prepared after three hours."));
				FPlatformMisc::RequestExitWithStatus(false, 2);
			}
			return;
		}
		Elapsed += DeltaTime;
		if (bList)
		{
			TickList(DeltaTime);
			return;
		}
		UpdatePhotoCamera();
		if (!bLooked && Elapsed > 2.f)
		{
			bLooked = true;
			FString Look;
			if (FParse::Value(FCommandLine::Get(), TEXT("EphemeraLook="), Look))
			{
				FString PitchText, YawText;
				if (Look.Split(TEXT(","), &PitchText, &YawText))
				{
					LookPitch = FCString::Atof(*PitchText);
					LookYaw = FCString::Atof(*YawText);
					bHasLook = true;
				}
			}
		}
		if (!bMenuShown && Elapsed > 4.f && FParse::Param(FCommandLine::Get(), TEXT("EphemeraMenu")))
		{
			bMenuShown = true;
			if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				if (AEphemeraPlayer* Player = Cast<AEphemeraPlayer>(PC->GetPawn()))
				{
					Player->ToggleMenu();
				}
			}
		}
		if (bHasLook && !bLookApplied && Elapsed > 3.f)
		{
			bLookApplied = true;
			if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					Pawn->AddActorLocalRotation(FRotator(0.f, LookYaw, 0.f));
					if (USceneComponent* Camera = Cast<USceneComponent>(Pawn->GetDefaultSubobjectByName(TEXT("Camera"))))
					{
						Camera->SetRelativeRotation(FRotator(LookPitch, 0.f, 0.f));
					}
				}
			}
		}
		if (Elapsed > Delay - 4.f && Elapsed <= Delay)
		{
			FrameSeconds += DeltaTime;
			GameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
			RenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
			GpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
			++Samples;
		}
		if (!bProfiled && Elapsed > Delay - 1.f && FParse::Param(FCommandLine::Get(), TEXT("EphemeraProfile")))
		{
			bProfiled = true;
			GEngine->Exec(GetWorld(), TEXT("r.ProfileGPU.ShowUI 0"));
			GEngine->Exec(GetWorld(), TEXT("ProfileGPU"));
		}
		if (!bRequested && Elapsed > Delay)
		{
			bRequested = true;
			if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				const UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
				if (const APawn* Pawn = PC->GetPawn(); Pawn && Frames)
				{
					const double Elevation = FMath::RadiansToDegrees(FMath::Asin(FVector::DotProduct(Frames->DirectionToStar(Pawn->GetActorLocation()), Pawn->GetActorUpVector())));
					UE_LOG(LogTemp, Display, TEXT("EphemeraShot: time %.0f s, sun elevation %.1f deg, player at %s, up %s"),
						Frames->GetTime(), Elevation, *Pawn->GetActorLocation().ToCompactString(), *Pawn->GetActorUpVector().ToCompactString());
					if (const AEphemeraBody* Under = Frames->DominantBody(Pawn->GetActorLocation()); Under && Under->Definition)
					{
						// Is the player standing where a player should be? Height over the ground itself, and speed.
						const FVector Local = Under->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
						const double GroundCm = FEphemeraTerrain::SurfacePointCm(*Under->Definition, Local.GetSafeNormal()).Size();
						UE_LOG(LogTemp, Display, TEXT("EphemeraShot: player %.1f m above the ground of %s, moving at %.1f m/s"),
							(Local.Size() - GroundCm) / 100.0, *Under->BodyId().ToString(), Pawn->GetVelocity().Size() / 100.0);
					}
					const float N = FMath::Max(Samples, 1);
					UE_LOG(LogTemp, Display, TEXT("EphemeraShot: %.1f ms per frame (%.0f fps) over the last 4 s | game thread %.1f ms, render thread %.1f ms, GPU %.1f ms"),
						FrameSeconds / N * 1000.f, FrameSeconds > 0.f ? Samples / FrameSeconds : 0.f, GameMs / N, RenderMs / N, GpuMs / N);
				}
			}
			// -EphemeraMenu: a picture of the screen as the player sees it, overlay and settings menu included.
			FScreenshotRequest::RequestScreenshot(File, FParse::Param(FCommandLine::Get(), TEXT("EphemeraMenu")) || FParse::Param(FCommandLine::Get(), TEXT("EphemeraShotUI")),
				false, false, FIntRect(), true);
		}
		// -EphemeraShotPair=<frames>: a second picture that many frames after the first, from the same still
		// camera (<file>_b.png). What differs between the two and is not something moving is flicker: the
		// measure of how steady the lighting and the anti-aliasing hold a still view.
		if (bRequested && !bPairTaken)
		{
			int32 PairFrames = 0;
			if (FParse::Value(FCommandLine::Get(), TEXT("EphemeraShotPair="), PairFrames) && PairFrames > 0)
			{
				if (++FramesSinceShot >= PairFrames + 1)
				{
					bPairTaken = true;
					FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(File, TEXT("")) + TEXT("_b.png"), false, false, false, FIntRect(), true);
				}
			}
			else
			{
				bPairTaken = true;
			}
		}
		if (bRequested && Elapsed > Delay + 2.f)
		{
			UE_LOG(LogTemp, Display, TEXT("EphemeraShot: saved %s"), *File);
			FPlatformMisc::RequestExitWithStatus(true, 0);     // forced: a normal shutdown can hang for minutes on macOS
		}
	}

private:
	static FVector ParseVector(const FString& Text)
	{
		TArray<FString> P;
		Text.ParseIntoArray(P, TEXT(","));
		return P.Num() >= 3 ? FVector(FCString::Atod(*P[0]), FCString::Atod(*P[1]), FCString::Atod(*P[2])) : FVector::ForwardVector;
	}

	/** The photo camera is re-placed every frame from its body's current pose, so it rides any frame hand-over. */
	void UpdatePhotoCamera()
	{
		if (!bTimeSet)
		{
			return;
		}
		if (!bPhotoParsed)
		{
			bPhotoParsed = true;
			FString Spec;
			if (FParse::Value(FCommandLine::Get(), TEXT("EphemeraPhoto="), Spec, false))
			{
				Spec.ParseIntoArray(Photo, TEXT("|"), false);
			}
		}
		UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
		if (Photo.Num() < 5 || !Frames)
		{
			return;
		}
		const AEphemeraBody* Body = Frames->FindBody(FName(*Photo[0]));
		if (!Body || !Body->Definition)
		{
			return;
		}
		auto PointOver = [&](const FVector& LocalDirection, double HeightM)
		{
			const FVector Dir = LocalDirection.GetSafeNormal();
			const FVector Local = FEphemeraTerrain::SurfacePointCm(*Body->Definition, Dir) + Dir * HeightM * 100.0;
			return Body->GetActorTransform().TransformPosition(Local);
		};
		// The standing point: a planet-local direction, or "toward:<body>[:<degrees>]" for the point
		// facing another body, optionally walked that many degrees away so the body sits lower in the sky.
		FVector LocalDir;
		if (Photo[1].StartsWith(TEXT("toward:")))
		{
			TArray<FString> P;
			Photo[1].RightChop(7).ParseIntoArray(P, TEXT(":"));
			const AEphemeraBody* Other = P.Num() > 0 ? Frames->FindBody(FName(*P[0])) : nullptr;
			LocalDir = Other ? Body->GetActorTransform().InverseTransformPositionNoScale(Other->GetActorLocation()).GetSafeNormal() : FVector::UpVector;
			if (P.Num() > 1)
			{
				FVector Axis = FVector::CrossProduct(LocalDir, FVector::UpVector).GetSafeNormal();
				if (Axis.IsNearlyZero())
				{
					Axis = FVector::ForwardVector;
				}
				LocalDir = FQuat(Axis, FMath::DegreesToRadians(FCString::Atof(*P[1]))).RotateVector(LocalDir);
			}
		}
		else
		{
			LocalDir = ParseVector(Photo[1]).GetSafeNormal();
		}
		const FVector Eye = PointOver(LocalDir, FCString::Atod(*Photo[2]));
		FVector Target = Eye + FVector::ForwardVector;
		if (Photo[3].StartsWith(TEXT("at:")))
		{
			TArray<FString> P;
			Photo[3].RightChop(3).ParseIntoArray(P, TEXT(","));
			if (P.Num() >= 4)
			{
				Target = PointOver(FVector(FCString::Atod(*P[0]), FCString::Atod(*P[1]), FCString::Atod(*P[2])), FCString::Atod(*P[3]));
			}
		}
		else if (const AEphemeraBody* Other = Frames->FindBody(FName(*Photo[3])))
		{
			Target = Other->GetActorLocation();
		}
		const FVector Forward = (Target - Eye).GetSafeNormal();
		FVector Up = Body->GetActorTransform().TransformVectorNoScale(LocalDir);
		if (FMath::Abs(FVector::DotProduct(Up, Forward)) > 0.985)
		{
			Up = FVector::UpVector;
		}
		FQuat Rotation = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
		if (Photo.Num() > 5)
		{
			FString YawText, PitchText;
			if (Photo[5].Split(TEXT(","), &YawText, &PitchText))
			{
				Rotation = Rotation * FRotator(FCString::Atof(*PitchText), FCString::Atof(*YawText), 0.f).Quaternion();
			}
		}
		if (Photo.Num() > 6)
		{
			Rotation = Rotation * FRotator(0.f, 0.f, FCString::Atof(*Photo[6])).Quaternion();
		}
		if (!PhotoCamera)
		{
			PhotoCamera = GetWorld()->SpawnActor<ACameraActor>(Eye, Rotation.Rotator());
			PhotoCamera->GetCameraComponent()->bConstrainAspectRatio = false;
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				PC->SetViewTarget(PhotoCamera);
			}
			Frames->SetObserver(PhotoCamera);
		}
		PhotoCamera->GetCameraComponent()->SetFieldOfView(FCString::Atof(*Photo[4]));
		PhotoCamera->SetActorLocationAndRotation(Eye, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	}

	/**
	 * A photographer steps round a boulder; this camera is put down where it is told. Anything
	 * sizeable standing at its feet or squarely in front of the lens is taken away for the shot.
	 */
	void ClearFrame()
	{
		if (!PhotoCamera)
		{
			return;
		}
		const FVector Eye = PhotoCamera->GetActorLocation();
		const FVector Forward = PhotoCamera->GetActorForwardVector();
		for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It)
		{
			UInstancedStaticMeshComponent* Component = *It;
			if (Component->GetWorld() != GetWorld() || !Component->GetStaticMesh() || !Component->IsVisible())
			{
				continue;
			}
			const double MeshRadius = Component->GetStaticMesh()->GetBounds().SphereRadius;
			for (int32 Index = Component->GetInstanceCount() - 1; Index >= 0; --Index)
			{
				FTransform Instance;
				Component->GetInstanceTransform(Index, Instance, true);
				const double Radius = MeshRadius * Instance.GetScale3D().GetMax();
				if (Radius < 30.0)
				{
					continue;       // grass and small stones stay where they are
				}
				const FVector To = Instance.GetLocation() - Eye;
				const double Along = FVector::DotProduct(To, Forward);
				const double Aside = (To - Forward * Along).Size();
				const bool bAtFeet = To.Size() < Radius + 300.0;
				const bool bInTheWay = Along > 0.0 && Along < 3000.0 && Aside < Radius + 80.0 + Along * 0.10;
				if (bAtFeet || bInTheWay)
				{
					Component->RemoveInstance(Index);
				}
			}
		}
	}

	/** One launch, many shots: move the camera, let lighting settle, save, repeat. */
	void TickList(float DeltaTime)
	{
		UEphemeraFrameSubsystem* Frames = GetWorld()->GetSubsystem<UEphemeraFrameSubsystem>();
		if (!Frames || ++Frame <= 12)
		{
			return;     // the player and bodies need a few frames to be placed
		}
		if (ShotIndex >= Shots.Num())
		{
			ShotClock += DeltaTime;
			if (ShotClock > 1.5f)
			{
				UE_LOG(LogTemp, Display, TEXT("EphemeraShot: list finished (%d shots)"), Shots.Num());
				FPlatformMisc::RequestExitWithStatus(true, 0);     // forced: a normal shutdown can hang for minutes on macOS
			}
			return;
		}
		const FShot& Shot = Shots[ShotIndex];
		if (!bShotStarted)
		{
			bShotStarted = true;
			bShotRequested = false;
			ShotClock = 0.f;
			Frames->SetTime(Shot.Time);
			Shot.Spec.ParseIntoArray(Photo, TEXT("|"), false);
			bPhotoParsed = true;
			bTimeSet = true;
			// An eighth field: console commands for this shot and those after it, separated by ";" (to take the
			// lighting apart: ShowFlag.*, r.*). They stay until a later line sets them back.
			if (Photo.Num() >= 8)
			{
				TArray<FString> Commands;
				Photo[7].ParseIntoArray(Commands, TEXT(";"));
				for (const FString& Command : Commands)
				{
					GEngine->Exec(GetWorld(), *Command.TrimStartAndEnd());
					UE_LOG(LogTemp, Display, TEXT("EphemeraShot: %s: %s"), *Shot.Name, *Command.TrimStartAndEnd());
				}
			}
		}
		UpdatePhotoCamera();
		ClearFrame();
		ShotClock += DeltaTime;
		// The first shot waits longer: the world is streaming in and the sky is being captured.
		const float Wait = ShotIndex == 0 ? Delay : Settle;
		if (!bShotRequested && ShotClock > Wait)
		{
			bShotRequested = true;
			const FString Path = FPaths::Combine(ShotDir, Shot.Name + TEXT(".png"));
			UE_LOG(LogTemp, Display, TEXT("EphemeraShot: %s at time %.0f s -> %s"), *Shot.Name, Frames->GetTime(), *Path);
			if (const APlayerCameraManager* View = UGameplayStatics::GetPlayerCameraManager(this, 0))
			{
				// Where the sun stands for this shot, as seen from the ground under the camera.
				const FVector Eye = View->GetCameraLocation();
				const AEphemeraBody* Near = Frames->DominantBody(Eye);
				const FVector Up = Near ? (Eye - Near->GetActorLocation()).GetSafeNormal() : FVector::UpVector;
				const double Elevation = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(Frames->DirectionToStar(Eye), Up), -1.0, 1.0)));
				UE_LOG(LogTemp, Display, TEXT("EphemeraShot: %s at time %.0f s, sun elevation %.1f deg, tide %+.1f m"), *Shot.Name, Shot.Time, Elevation,
					Near ? Near->TideM(Eye) : 0.0);
			}
			FScreenshotRequest::RequestScreenshot(Path, false, false, false, FIntRect(), true);
		}
		if (bShotRequested && ShotClock > Wait + 0.6f)
		{
			++ShotIndex;
			bShotStarted = false;
			ShotClock = 0.f;
		}
	}

	struct FShot { FString Name; double Time; FString Spec; };
	TArray<FShot> Shots;
	FString ShotDir;
	float Settle = 7.f;
	float ShotClock = 0.f;
	int32 ShotIndex = 0;
	bool bList = false;
	bool bShotStarted = false;
	bool bShotRequested = false;

	UPROPERTY() TObjectPtr<ACameraActor> PhotoCamera;
	TArray<FString> Photo;
	bool bPhotoParsed = false;
	FString File;
	float Delay = 12.f;
	float Elapsed = 0.f;
	float Waited = 0.f;
	float LookPitch = 0.f;
	float LookYaw = 0.f;
	bool bStarted = false;
	bool bProfiled = false;
	float FrameSeconds = 0.f;
	double GameMs = 0.0, RenderMs = 0.0, GpuMs = 0.0;
	int32 Samples = 0;
	bool bTimeSet = false;
	int32 Frame = 0;
	bool bMenuShown = false;
	bool bLooked = false;
	bool bHasLook = false;
	bool bLookApplied = false;
	bool bRequested = false;
	bool bPairTaken = false;
	int32 FramesSinceShot = 0;
};
