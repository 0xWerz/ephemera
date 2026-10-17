#include "EphemeraCloudBuilder.h"

#include "Algo/BinarySearch.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/ParallelFor.h"
#include "Engine/VolumeTexture.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	uint32 Hash(const int32 X, const int32 Y, const int32 Z, const uint32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 73856093u ^ static_cast<uint32>(Y) * 19349663u ^ static_cast<uint32>(Z) * 83492791u ^ Seed * 0x9e3779b9u;
		H ^= H >> 16;
		H *= 0x7feb352du;
		H ^= H >> 15;
		H *= 0x846ca68bu;
		H ^= H >> 16;
		return H;
	}

	float Unit(const uint32 H)
	{
		return static_cast<float>(H & 0xffffffu) / 16777216.f;
	}

	/** Period 0 means the noise does not repeat. */
	int32 Wrap(const int32 V, const int32 Period)
	{
		return Period > 0 ? ((V % Period) + Period) % Period : V;
	}

	float Gradient(const uint32 H, const float X, const float Y, const float Z)
	{
		switch (H % 12u)
		{
		case 0: return X + Y;
		case 1: return -X + Y;
		case 2: return X - Y;
		case 3: return -X - Y;
		case 4: return X + Z;
		case 5: return -X + Z;
		case 6: return X - Z;
		case 7: return -X - Z;
		case 8: return Y + Z;
		case 9: return -Y + Z;
		case 10: return Y - Z;
		default: return -Y - Z;
		}
	}

	/** Gradient noise, about -1..1, repeating every Period lattice cells. */
	float Perlin(const FVector3f& P, const int32 Period, const uint32 Seed)
	{
		const int32 X0 = FMath::FloorToInt32(P.X);
		const int32 Y0 = FMath::FloorToInt32(P.Y);
		const int32 Z0 = FMath::FloorToInt32(P.Z);
		const float Fx = P.X - X0;
		const float Fy = P.Y - Y0;
		const float Fz = P.Z - Z0;
		const auto Fade = [](const float T) { return T * T * T * (T * (T * 6.f - 15.f) + 10.f); };
		const float U = Fade(Fx);
		const float V = Fade(Fy);
		const float W = Fade(Fz);
		float Result = 0.f;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const int32 Dx = Corner & 1;
			const int32 Dy = (Corner >> 1) & 1;
			const int32 Dz = (Corner >> 2) & 1;
			const float Weight = (Dx ? U : 1.f - U) * (Dy ? V : 1.f - V) * (Dz ? W : 1.f - W);
			const uint32 H = Hash(Wrap(X0 + Dx, Period), Wrap(Y0 + Dy, Period), Wrap(Z0 + Dz, Period), Seed);
			Result += Weight * Gradient(H, Fx - Dx, Fy - Dy, Fz - Dz);
		}
		return Result;
	}

	/** Octaves of gradient noise, each twice as fine and half as strong; about -1..1. */
	float PerlinFbm(const FVector3f& P, const int32 Octaves, const int32 Period, const uint32 Seed)
	{
		float Sum = 0.f;
		float Amplitude = 1.f;
		float Total = 0.f;
		int32 Scale = 1;
		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			Sum += Amplitude * Perlin(P * static_cast<float>(Scale), Period * Scale, Seed + Octave * 101u);
			Total += Amplitude;
			Amplitude *= 0.5f;
			Scale *= 2;
		}
		return Sum / Total;
	}

	/** Cellular noise: 1 at a cell's feature point, falling to 0 a cell away. Repeats every Period cells. */
	float Worley(const FVector3f& P, const int32 Period, const uint32 Seed)
	{
		const int32 X0 = FMath::FloorToInt32(P.X);
		const int32 Y0 = FMath::FloorToInt32(P.Y);
		const int32 Z0 = FMath::FloorToInt32(P.Z);
		float Nearest = 4.f;
		for (int32 Dz = -1; Dz <= 1; ++Dz)
		{
			for (int32 Dy = -1; Dy <= 1; ++Dy)
			{
				for (int32 Dx = -1; Dx <= 1; ++Dx)
				{
					const int32 Cx = X0 + Dx;
					const int32 Cy = Y0 + Dy;
					const int32 Cz = Z0 + Dz;
					const uint32 H = Hash(Wrap(Cx, Period), Wrap(Cy, Period), Wrap(Cz, Period), Seed);
					const FVector3f Feature(Cx + Unit(H), Cy + Unit(Hash(static_cast<int32>(H), 1, 2, Seed)), Cz + Unit(Hash(static_cast<int32>(H), 3, 4, Seed)));
					Nearest = FMath::Min(Nearest, FVector3f::DistSquared(P, Feature));
				}
			}
		}
		return 1.f - FMath::Min(FMath::Sqrt(Nearest), 1.f);
	}

	float WorleyFbm(const FVector3f& Uvw, const int32 Cells, const uint32 Seed)
	{
		return Worley(Uvw * static_cast<float>(Cells), Cells, Seed) * 0.625f
			+ Worley(Uvw * static_cast<float>(Cells * 2), Cells * 2, Seed + 7u) * 0.25f
			+ Worley(Uvw * static_cast<float>(Cells * 4), Cells * 4, Seed + 13u) * 0.125f;
	}

	/**
	 * Replaces each value by its rank among Reference (sorted), as a byte: afterwards the values
	 * are spread evenly over 0..255.
	 */
	TArray<uint8> Equalise(const TArray<float>& Values, const TArray<float>& Reference)
	{
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Values.Num());
		const float Scale = 255.f / FMath::Max(Reference.Num() - 1, 1);
		ParallelFor(Values.Num(), [&](const int32 Index)
		{
			const int32 Rank = Algo::LowerBound(Reference, Values[Index]);
			Bytes[Index] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Rank * Scale), 0, 255));
		});
		return Bytes;
	}

	TArray<float> Sorted(TArray<float> Values)
	{
		Values.Sort();
		return Values;
	}

	bool SaveVolume(const FString& Folder, const FString& Name, const int32 Size, const TArray<uint8>& Bytes, const bool bRepeat)
	{
		// A picture of the middle slice (Saved/CloudTextures/<name>.pgm), to check a texture by eye.
		{
			const FString Header = FString::Printf(TEXT("P5\n%d %d\n255\n"), Size, Size);
			TArray<uint8> File;
			File.Append(reinterpret_cast<const uint8*>(TCHAR_TO_ANSI(*Header)), Header.Len());
			File.Append(Bytes.GetData() + (Size / 2) * Size * Size, Size * Size);
			FFileHelper::SaveArrayToFile(File, *(FPaths::ProjectSavedDir() / TEXT("CloudTextures") / Name + TEXT(".pgm")));
		}
		const FString PackageName = Folder / Name;
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		UVolumeTexture* Texture = FindObject<UVolumeTexture>(Package, *Name);
		if (!Texture)
		{
			Texture = NewObject<UVolumeTexture>(Package, *Name, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Texture);
		}
		Texture->Modify();
		Texture->Source.Init(Size, Size, Size, 1, TSF_G8, Bytes.GetData());
		Texture->SRGB = false;
		// One uncompressed byte per voxel: block compression would put steps in the soft gradients
		// the clouds are thresholded on.
		Texture->CompressionSettings = TC_Grayscale;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->Filter = TF_Bilinear;
		Texture->AddressMode = bRepeat ? TA_Wrap : TA_Clamp;
		Texture->NeverStream = true;
		Texture->PostEditChange();
		Package->MarkPackageDirty();
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		if (!UPackage::SavePackage(Package, Texture, *Filename, SaveArgs))
		{
			UE_LOG(LogTemp, Error, TEXT("Ephemera cloud builder: could not save %s"), *Filename);
			return false;
		}
		return true;
	}

	struct FStorm
	{
		FVector3f Centre;
		FVector3f East;
		FVector3f North;
		float RadiusRad;
		float Hand;
		float Phase;
	};

	/** How cloudy the weather wants a direction on the planet to be (unbounded; equalised afterwards). */
	float Weather(const FVector3f& Dir, const TArray<FStorm>& Storms, const uint32 Seed)
	{
		// Curl the lookup so fronts bend and trail instead of sitting as round blobs.
		const FVector3f Bend(Perlin(Dir * 1.7f + FVector3f(3.1f, 7.7f, 1.3f), 0, Seed + 31u),
			Perlin(Dir * 1.7f + FVector3f(9.2f, 2.6f, 5.9f), 0, Seed + 37u),
			Perlin(Dir * 1.7f + FVector3f(4.4f, 8.1f, 6.6f), 0, Seed + 41u));
		const FVector3f P = Dir + Bend * 0.5f;
		const float Fronts = PerlinFbm(P * 2.3f + FVector3f(11.f, 5.f, 17.f), 5, 0, Seed + 43u);
		// Latitude belts, as on any spinning world with a sea: rising wet air on the equator, sinking
		// dry air some thirty degrees either side, storm tracks beyond.
		const float Latitude = FMath::Asin(FMath::Clamp(P.GetSafeNormal().Z, -1.f, 1.f));
		const float Polar = FMath::Abs(Latitude);
		const float Belts = 0.30f * FMath::Exp(-FMath::Square(Latitude / 0.16f))
			- 0.30f * FMath::Exp(-FMath::Square((Polar - 0.50f) / 0.17f))
			+ 0.20f * FMath::Exp(-FMath::Square((Polar - 0.98f) / 0.22f));
		float Cyclones = 0.f;
		for (const FStorm& Storm : Storms)
		{
			const float Angle = FMath::Acos(FMath::Clamp(FVector3f::DotProduct(Dir, Storm.Centre), -1.f, 1.f));
			const float T = Angle / Storm.RadiusRad;
			if (T > 1.35f)
			{
				continue;
			}
			// Two arms winding into a clear eye.
			const float Bearing = FMath::Atan2(FVector3f::DotProduct(Dir, Storm.North), FVector3f::DotProduct(Dir, Storm.East));
			const float Spiral = FMath::Cos(2.f * Bearing * Storm.Hand - 6.0f * FMath::Loge(T + 0.07f) + Storm.Phase);
			const float Arm = FMath::Pow(0.5f + 0.5f * Spiral, 1.6f);
			const float Envelope = FMath::SmoothStep(1.35f, 0.3f, T) * FMath::SmoothStep(0.035f, 0.13f, T);
			Cyclones += Envelope * (0.25f + 0.75f * Arm) * 1.0f - Envelope * 0.25f;
		}
		return 0.6f * Fronts + Belts + Cyclones;
	}
}

bool UEphemeraCloudBuilder::BuildCloudTextures(const FString& Folder, const int32 Seed)
{
	const uint32 S = static_cast<uint32>(Seed);

	// ---- Billows: Perlin-Worley, 4 cells across the texture at the coarsest.
	{
		const int32 N = 128;
		TArray<float> Values;
		Values.SetNumUninitialized(N * N * N);
		ParallelFor(N, [&](const int32 Z)
		{
			for (int32 Y = 0; Y < N; ++Y)
			{
				for (int32 X = 0; X < N; ++X)
				{
					const FVector3f Uvw((X + 0.5f) / N, (Y + 0.5f) / N, (Z + 0.5f) / N);
					const float Smooth = FMath::Clamp(PerlinFbm(Uvw * 4.f, 3, 4, S + 1u) * 0.7f + 0.5f, 0.f, 1.f);
					const float Coarse = WorleyFbm(Uvw, 4, S + 3u);
					const float Fine = WorleyFbm(Uvw, 8, S + 5u);
					// Smooth noise puffed up by cells, then with finer cells carved out of it.
					const float Puffed = Coarse + Smooth * (1.f - Coarse);
					Values[(Z * N + Y) * N + X] = (Puffed - (Fine - 1.f)) / (2.f - Fine);
				}
			}
		});
		if (!SaveVolume(Folder, TEXT("T_CloudShape"), N, Equalise(Values, Sorted(Values)), true))
		{
			return false;
		}
	}

	// ---- Wisps: Worley only, finer.
	{
		const int32 N = 64;
		TArray<float> Values;
		Values.SetNumUninitialized(N * N * N);
		ParallelFor(N, [&](const int32 Z)
		{
			for (int32 Y = 0; Y < N; ++Y)
			{
				for (int32 X = 0; X < N; ++X)
				{
					const FVector3f Uvw((X + 0.5f) / N, (Y + 0.5f) / N, (Z + 0.5f) / N);
					Values[(Z * N + Y) * N + X] = WorleyFbm(Uvw, 3, S + 11u);
				}
			}
		});
		if (!SaveVolume(Folder, TEXT("T_CloudDetail"), N, Equalise(Values, Sorted(Values)), true))
		{
			return false;
		}
	}

	// ---- Weather, by direction from the planet's centre.
	{
		FRandomStream Random(Seed);
		TArray<FStorm> Storms;
		for (int32 Index = 0; Index < 5; ++Index)
		{
			const float Hemisphere = (Index % 2 == 0) ? 1.f : -1.f;
			const float Latitude = Hemisphere * Random.FRandRange(0.62f, 1.02f);
			const float Longitude = Random.FRandRange(0.f, 2.f * PI);
			FStorm Storm;
			Storm.Centre = FVector3f(FMath::Cos(Latitude) * FMath::Cos(Longitude), FMath::Cos(Latitude) * FMath::Sin(Longitude), FMath::Sin(Latitude));
			Storm.East = FVector3f::CrossProduct(FVector3f::UpVector, Storm.Centre).GetSafeNormal();
			Storm.North = FVector3f::CrossProduct(Storm.Centre, Storm.East);
			Storm.RadiusRad = Random.FRandRange(0.30f, 0.46f);
			// Storms turn opposite ways either side of the equator.
			Storm.Hand = Hemisphere;
			Storm.Phase = Random.FRandRange(0.f, 2.f * PI);
			Storms.Add(Storm);
		}
		const int32 N = 128;
		TArray<float> Values;
		Values.SetNumUninitialized(N * N * N);
		ParallelFor(N, [&](const int32 Z)
		{
			for (int32 Y = 0; Y < N; ++Y)
			{
				for (int32 X = 0; X < N; ++X)
				{
					const FVector3f V((X + 0.5f) / N * 2.f - 1.f, (Y + 0.5f) / N * 2.f - 1.f, (Z + 0.5f) / N * 2.f - 1.f);
					Values[(Z * N + Y) * N + X] = Weather(V.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::ForwardVector), Storms, S + 17u);
				}
			}
		});
		// Rank against the voxels the material actually reads (a shell near the unit sphere), so
		// that the ranks are fractions of the planet's surface.
		TArray<float> Shell;
		for (int32 Z = 0; Z < N; ++Z)
		{
			for (int32 Y = 0; Y < N; ++Y)
			{
				for (int32 X = 0; X < N; ++X)
				{
					const FVector3f V((X + 0.5f) / N * 2.f - 1.f, (Y + 0.5f) / N * 2.f - 1.f, (Z + 0.5f) / N * 2.f - 1.f);
					const float R = V.Size();
					if (R > 0.86f && R < 0.98f)
					{
						Shell.Add(Values[(Z * N + Y) * N + X]);
					}
				}
			}
		}
		Shell.Sort();
		if (!SaveVolume(Folder, TEXT("T_CloudWeather"), N, Equalise(Values, Shell), false))
		{
			return false;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("Ephemera cloud builder: weather, shape and detail textures written to %s"), *Folder);
	return true;
}

UVolumeTexture* UEphemeraCloudBuilder::ImportWaveVolume(const FString& Folder, const FString& Name, const FString& RawFile, const int32 Size, const int32 Frames)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *RawFile) || Bytes.Num() != Size * Size * Frames * 4)
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera wave volume: %s is missing or not %d x %d x %d x 4 bytes"), *RawFile, Size, Size, Frames);
		return nullptr;
	}
	const FString PackageName = Folder / Name;
	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();
	UVolumeTexture* Texture = FindObject<UVolumeTexture>(Package, *Name);
	if (!Texture)
	{
		Texture = NewObject<UVolumeTexture>(Package, *Name, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Texture);
	}
	Texture->Modify();
	Texture->Source.Init(Size, Size, Frames, 1, TSF_BGRA8, Bytes.GetData());
	Texture->SRGB = false;
	// Uncompressed: block compression puts steps in slopes, and steps in slopes are glints that crawl.
	// With mips, which is what lets the sea far off be drawn from the slopes' averages (see OCEAN_CODE).
	Texture->CompressionSettings = TC_VectorDisplacementmap;
	Texture->MipGenSettings = TMGS_FromTextureGroup;
	Texture->Filter = TF_Trilinear;
	Texture->AddressMode = TA_Wrap;
	Texture->NeverStream = true;
	Texture->PostEditChange();
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	if (!UPackage::SavePackage(Package, Texture, *Filename, SaveArgs))
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera wave volume: could not save %s"), *Filename);
		return nullptr;
	}
	return Texture;
}

UVolumeTexture* UEphemeraCloudBuilder::ImportWaveVolumeHalf(const FString& Folder, const FString& Name, const FString& RawFile, const int32 Size, const int32 Frames)
{
	TArray<uint8> Bytes;
	// Four channels of two bytes each.
	if (!FFileHelper::LoadFileToArray(Bytes, *RawFile) || Bytes.Num() != Size * Size * Frames * 8)
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera wave volume: %s is missing or not %d x %d x %d x 8 bytes"), *RawFile, Size, Size, Frames);
		return nullptr;
	}
	const FString PackageName = Folder / Name;
	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();
	UVolumeTexture* Texture = FindObject<UVolumeTexture>(Package, *Name);
	if (!Texture)
	{
		Texture = NewObject<UVolumeTexture>(Package, *Name, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Texture);
	}
	Texture->Modify();
	Texture->Source.Init(Size, Size, Frames, 1, TSF_RGBA16F, Bytes.GetData());
	Texture->SRGB = false;
	// TC_HDR keeps the half floats as they are (RGBA16F, uncompressed).
	Texture->CompressionSettings = TC_HDR;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	// Linear between texels and, along the third coordinate, between moments: the material reads it
	// with the view's shared wrapping sampler, so this is only what the editor shows.
	Texture->Filter = TF_Bilinear;
	Texture->AddressMode = TA_Wrap;
	Texture->NeverStream = true;
	Texture->PostEditChange();
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	if (!UPackage::SavePackage(Package, Texture, *Filename, SaveArgs))
	{
		UE_LOG(LogTemp, Error, TEXT("Ephemera wave volume: could not save %s"), *Filename);
		return nullptr;
	}
	return Texture;
}
