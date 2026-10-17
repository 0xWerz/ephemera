#pragma once

#include "CoreMinimal.h"
#include "ContentStreaming.h"
#include "Engine/Engine.h"
#include "ShaderCompiler.h"
#include "Subsystems/WorldSubsystem.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif
#include "EphemeraReadiness.generated.h"

/**
 * Tells the player whether what they are looking at is finished. In a development run Unreal
 * compiles shaders and processes meshes and textures in the background after the world appears,
 * so the first minutes can show grey materials, blurry textures and missing shadows. This puts a
 * line in the top-left corner while that work is going on, and "World ready" when it is done.
 * A packaged game ships with all of this precomputed and never shows the line.
 */
UCLASS()
class UEphemeraReadinessSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UEphemeraReadinessSubsystem, STATGROUP_Tickables); }

	virtual void Tick(float DeltaTime) override
	{
		if (!GEngine)
		{
			return;
		}
		const int32 Shaders = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
		int32 Assets = 0;
#if WITH_EDITOR
		Assets = FAssetCompilingManager::Get().GetNumRemainingAssets();
#endif
		const int32 Textures = IStreamingManager::Get().GetTextureStreamingManager().GetNumWantingResources();
		const bool bBusy = Shaders > 0 || Assets > 0 || Textures > 8;
		if (bBusy)
		{
			QuietSeconds = 0.f;
			bEverBusy = true;
			TArray<FString> Parts;
			if (Shaders > 0) { Parts.Add(FString::Printf(TEXT("%s shaders compiling"), *FText::AsNumber(Shaders).ToString())); }
			if (Assets > 0) { Parts.Add(FString::Printf(TEXT("%s meshes and textures processing"), *FText::AsNumber(Assets).ToString())); }
			if (Textures > 8) { Parts.Add(FString::Printf(TEXT("%s textures loading"), *FText::AsNumber(Textures).ToString())); }
			GEngine->AddOnScreenDebugMessage(MessageKey, 0.6f, FColor(255, 200, 60),
				FString::Printf(TEXT("Preparing the world: %s. What you see is not final yet."), *FString::Join(Parts, TEXT(", "))));
			return;
		}
		QuietSeconds += DeltaTime;
		// A short pause between batches of work is not "done": wait until it has been quiet for a while.
		if (QuietSeconds > 3.f && QuietSeconds < 11.f)
		{
			GEngine->AddOnScreenDebugMessage(MessageKey, 0.6f, FColor(120, 230, 140),
				bEverBusy ? TEXT("World ready: everything is loaded and compiled.") : TEXT("World ready."));
		}
	}

private:
	static constexpr uint64 MessageKey = 0xE9A3001;
	float QuietSeconds = 0.f;
	bool bEverBusy = false;
};
