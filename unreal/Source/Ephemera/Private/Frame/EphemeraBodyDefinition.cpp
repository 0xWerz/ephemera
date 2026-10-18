#include "Frame/EphemeraBodyDefinition.h"
#include "Frame/EphemeraGroundSurface.h"

#include "HAL/IConsoleManager.h"

#include "Engine/StaticMesh.h"
#include "Frame/EphemeraHeightField.h"

namespace
{
	// Perlin fBm on the unit sphere. Frequency is cycles around the planet: the sample point is the
	// direction scaled so one noise cell spans 1/Frequency of the circumference.
	double Fbm(const FVector& Direction, const FEphemeraNoiseLayer& Layer, double SeedOffset)
	{
		double Sum = 0.0, Amplitude = 1.0, Norm = 0.0;
		double Frequency = Layer.Frequency;
		for (int32 Octave = 0; Octave < FMath::Max(Layer.Octaves, 1); ++Octave)
		{
			const FVector P = Direction * Frequency + FVector(SeedOffset + Octave * 17.13, SeedOffset * 0.37 - Octave * 5.71, SeedOffset * 1.91 + Octave * 9.4);
			double N = FMath::PerlinNoise3D(P);                       // [-1, 1]
			if (Layer.Ridged > 0.0)
			{
				const double Ridge = 1.0 - 2.0 * FMath::Abs(N);       // sharp crest at N = 0, still [-1, 1]
				N = FMath::Lerp(N, Ridge, FMath::Clamp(Layer.Ridged, 0.0, 1.0));
			}
			Sum += N * Amplitude;
			Norm += Amplitude;
			Amplitude *= 0.5;
			Frequency *= 2.03;
		}
		return Sum / Norm;
	}

	double CellValue(const int32 X, const int32 Y, const int32 Z, const int32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 73856093u ^ static_cast<uint32>(Y) * 19349663u ^ static_cast<uint32>(Z) * 83492791u ^ static_cast<uint32>(Seed) * 0x9e3779b9u;
		H ^= H >> 16;
		H *= 0x7feb352du;
		H ^= H >> 15;
		H *= 0x846ca68bu;
		H ^= H >> 16;
		return static_cast<double>(H & 0xffffffu) / 16777216.0;
	}

	/** Value noise in 0..1 with its exact gradient (quintic interpolation, so the gradient is smooth too). */
	double NoiseWithGradient(const FVector& P, const int32 Seed, FVector& OutGradient)
	{
		const int32 X = FMath::FloorToInt32(P.X), Y = FMath::FloorToInt32(P.Y), Z = FMath::FloorToInt32(P.Z);
		const FVector W(P.X - X, P.Y - Y, P.Z - Z);
		const FVector U = W * W * W * (W * (W * 6.0 - FVector(15.0)) + FVector(10.0));
		const FVector Du = W * W * (W * (W - FVector(2.0)) + FVector(1.0)) * 30.0;
		const double A = CellValue(X, Y, Z, Seed), B = CellValue(X + 1, Y, Z, Seed), C = CellValue(X, Y + 1, Z, Seed), D = CellValue(X + 1, Y + 1, Z, Seed);
		const double E = CellValue(X, Y, Z + 1, Seed), F = CellValue(X + 1, Y, Z + 1, Seed), G = CellValue(X, Y + 1, Z + 1, Seed), H = CellValue(X + 1, Y + 1, Z + 1, Seed);
		const double K0 = A, K1 = B - A, K2 = C - A, K3 = E - A, K4 = A - B - C + D, K5 = A - C - E + G, K6 = A - B - E + F, K7 = -A + B + C - D + E - F - G + H;
		OutGradient = Du * FVector(K1 + K4 * U.Y + K6 * U.Z + K7 * U.Y * U.Z, K2 + K5 * U.Z + K4 * U.X + K7 * U.Z * U.X, K3 + K6 * U.X + K5 * U.Y + K7 * U.X * U.Y);
		return K0 + K1 * U.X + K2 * U.Y + K3 * U.Z + K4 * U.X * U.Y + K5 * U.Y * U.Z + K6 * U.Z * U.X + K7 * U.X * U.Y * U.Z;
	}

	/**
	 * Derivative-damped fBm (Inigo Quilez, iquilezles.org/articles/morenoise): the slopes of the
	 * octaves so far are summed, and each octave is divided by one plus that sum squared. About
	 * 0..1, lowest and smoothest in the valleys.
	 */
	double ErodedFbm(const FVector& Direction, const double Frequency, const int32 Octaves, const double Erosion, const int32 Seed)
	{
		FVector P = Direction * Frequency + FVector(Seed * 13.1 + 3.7, Seed * 7.7 - 1.9, Seed * 3.3 + 8.2);
		FVector Slope = FVector::ZeroVector;
		double Sum = 0.0, Amplitude = 1.0, Norm = 0.0;
		for (int32 Octave = 0; Octave < FMath::Max(Octaves, 1); ++Octave)
		{
			FVector Gradient;
			const double Value = NoiseWithGradient(P, Seed + Octave * 31, Gradient);
			Slope += Gradient;
			Sum += Amplitude * Value / (1.0 + Erosion * Slope.SizeSquared());
			Norm += Amplitude;
			Amplitude *= 0.5;
			// Twice as fine, and turned, so the octaves' lattices never line up.
			P = FVector(0.80 * P.Y + 0.60 * P.Z, -0.80 * P.X + 0.36 * P.Y - 0.48 * P.Z, -0.60 * P.X - 0.48 * P.Y + 0.64 * P.Z) * 2.0;
		}
		return Sum / Norm;
	}

	double SmoothStep(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / FMath::Max(B - A, UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** 0 highland .. 1 mare. */
	double MariaMask(const FEphemeraTerrainSettings& Settings, const FVector& Direction)
	{
		const FEphemeraCraterSettings& C = Settings.Craters;
		if (!C.bEnabled || C.MariaFraction <= 0.0)
		{
			return 0.0;
		}
		FEphemeraNoiseLayer Slow;
		Slow.Frequency = 1.1;
		Slow.Octaves = 4;
		const double N = Fbm(Direction, Slow, Settings.Seed * 2.9 + 12.0);
		// The noise is roughly bell-shaped about 0 with a spread near 0.25: the edge is placed so
		// that about MariaFraction of the surface lies above it.
		const double Edge = FMath::Lerp(0.42, -0.10, FMath::Clamp(C.MariaFraction / 0.5, 0.0, 1.0));
		return SmoothStep(Edge - 0.04, Edge + 0.05, N);
	}

	struct FCraterSample
	{
		double HeightM = 0.0;
		double Fresh = 0.0;
	};

	/**
	 * The summed craters at a point. Each size class (diameters D/2..D, halving from the largest)
	 * has a grid of cells 2.2 D across, wide enough that a crater and its ejecta never reach past
	 * the neighbouring cell, so only the 27 cells around the point need looking at.
	 *
	 * Counts: 1.54 f D^-2 craters per km2 larger than D km (f = Saturation) puts
	 * 4.62 f / D^2 per m2 in the class D/2..D, which is 4.62 f * 2.2^2 = 22.4 f per cell, whatever
	 * the size: every class crowds the ground equally. A cell offers six candidates, each real
	 * with the probability that gives that average.
	 *
	 * Shapes (per crater, x = distance from the centre in crater radii): a bowl
	 * (depth + rim) x^2 - depth inside, a rim falling away as x^-3 outside (the measured thinning
	 * of ejecta blankets). Small fresh craters are a fifth as deep as they are wide; the largest
	 * are shallower, with level floors. Age lowers rim and depth.
	 */
	FCraterSample CraterField(const FEphemeraTerrainSettings& Settings, const double RadiusM, const FVector& Direction)
	{
		FCraterSample Out;
		const FEphemeraCraterSettings& C = Settings.Craters;
		const FVector P = Direction * RadiusM;
		constexpr int32 Slots = 6;
		constexpr double CellPerDiameter = 2.2;
		const double Chance = FMath::Min(4.62 * C.Saturation * CellPerDiameter * CellPerDiameter / Slots, 1.0);
		int32 Class = 0;
		for (double Largest = C.LargestM; Largest * 0.5 >= C.SmallestM * 0.999 && Class < 12; Largest *= 0.5, ++Class)
		{
			const double Smallest = Largest * 0.5;
			const double Cell = CellPerDiameter * Largest;
			const int32 X0 = FMath::FloorToInt32(P.X / Cell), Y0 = FMath::FloorToInt32(P.Y / Cell), Z0 = FMath::FloorToInt32(P.Z / Cell);
			for (int32 Dz = -1; Dz <= 1; ++Dz)
			{
				for (int32 Dy = -1; Dy <= 1; ++Dy)
				{
					for (int32 Dx = -1; Dx <= 1; ++Dx)
					{
						const int32 Cx = X0 + Dx, Cy = Y0 + Dy, Cz = Z0 + Dz;
						for (int32 Slot = 0; Slot < Slots; ++Slot)
						{
							const int32 Key = Settings.Seed * 131 + Class * 977 + Slot * 53;
							if (CellValue(Cx, Cy, Cz, Key) >= Chance)
							{
								continue;
							}
							const FVector Candidate = FVector(Cx + CellValue(Cx, Cy, Cz, Key + 1), Cy + CellValue(Cx, Cy, Cz, Key + 2), Cz + CellValue(Cx, Cy, Cz, Key + 3)) * Cell;
							const double CandidateRadius = Candidate.Size();
							// Only candidates within half a cell of the surface become craters (dropped onto it).
							if (FMath::Abs(CandidateRadius - RadiusM) > Cell * 0.5 || CandidateRadius < 1.0)
							{
								continue;
							}
							const FVector CentreDir = Candidate / CandidateRadius;
							const FVector Centre = CentreDir * RadiusM;
							// More small craters than large within the class, by the same law.
							const double Diameter = Smallest / FMath::Sqrt(1.0 - 0.75 * CellValue(Cx, Cy, Cz, Key + 4));
							const double Radius = Diameter * 0.5;
							const double X = FVector::Dist(P, Centre) / Radius;
							if (X > 2.2)
							{
								continue;
							}
							if (CellValue(Cx, Cy, Cz, Key + 5) > FMath::Lerp(1.0, C.MariaSaturation, MariaMask(Settings, CentreDir)))
							{
								continue;
							}
							const double Age = CellValue(Cx, Cy, Cz, Key + 6);
							// Age wears a crater down: shallower, its rim lower and rounder.
							const double Depth = Diameter * FMath::Lerp(0.20, 0.11, SmoothStep(150.0, 450.0, Diameter)) * (1.0 - 0.7 * Age);
							const double Rim = Diameter * 0.045 * (1.0 - 0.85 * Age);
							// The bowl inside and the blanket outside are two curves that cross at the rim.
							// Taking the lower of the two with a soft join gives a rounded crest in place
							// of a knife edge (which a mesh a few metres coarse draws as a saw).
							double Bowl = (Depth + Rim) * X * X - Depth;
							// The largest have level floors.
							const double Floor = -Depth * FMath::Lerp(1.0, 0.6, SmoothStep(200.0, 450.0, Diameter));
							const double FloorBlend = FMath::Max(Depth * 0.12, 0.01);
							const double FloorT = FMath::Clamp(0.5 + 0.5 * (Bowl - Floor) / FloorBlend, 0.0, 1.0);
							Bowl = FMath::Lerp(Floor, Bowl, FloorT) + FloorBlend * FloorT * (1.0 - FloorT);
							const double Blanket = Rim * FMath::Pow(FMath::Max(X, 0.6), -3.0) * (1.0 - SmoothStep(1.7, 2.2, X));
							const double Join = FMath::Max(Rim * FMath::Lerp(0.5, 1.4, Age), Diameter * 0.004);
							const double JoinT = FMath::Max(Join - FMath::Abs(Bowl - Blanket), 0.0) / Join;
							const double Height = FMath::Min(Bowl, Blanket) - JoinT * JoinT * Join * 0.25;
							Out.HeightM += Height;
							// The youngest tenth still have their pale blanket and rays.
							if (Age < 0.10 && Diameter > 45.0)
							{
								double Bright = X < 1.0 ? 0.55 : FMath::Pow(X, -2.0) * (1.0 - SmoothStep(1.6, 2.2, X));
								if (X >= 1.0)
								{
									const FVector East = FVector::CrossProduct(CentreDir, FMath::Abs(CentreDir.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
									const FVector North = FVector::CrossProduct(CentreDir, East);
									const FVector Offset = P - Centre;
									const double Bearing = FMath::Atan2(FVector::DotProduct(Offset, North), FVector::DotProduct(Offset, East));
									const double Streaks = 7.0 + FMath::FloorToDouble(CellValue(Cx, Cy, Cz, Key + 7) * 6.0);
									const double Ray = FMath::Pow(0.5 + 0.5 * FMath::Sin(Bearing * Streaks + CellValue(Cx, Cy, Cz, Key + 8) * 6.283), 3.0);
									Bright *= 0.35 + 0.65 * Ray;
								}
								Out.Fresh = FMath::Max(Out.Fresh, Bright * (1.0 - Age / 0.10));
							}
						}
					}
				}
			}
		}
		Out.Fresh = FMath::Clamp(Out.Fresh, 0.0, 1.0);
		return Out;
	}
}

double FEphemeraTerrain::Continentalness(const FEphemeraTerrainSettings& Settings, const FVector& UnitDirection)
{
	if (Settings.HeightField && Settings.HeightField->IsUsable())
	{
		return FMath::Clamp(Settings.HeightField->HeightM(UnitDirection) / 120.0, -1.0, 1.0);
	}
	if (!Settings.bContinents)
	{
		return 0.0;
	}
	// The sample point is pushed around by a second slow field so coastlines wander into bays,
	// headlands and fjords instead of following smooth noise contours.
	const FVector Seed(Settings.Seed * 5.3 + 2.1, Settings.Seed * 1.7 - 4.4, Settings.Seed * 3.9 + 7.7);
	const FVector SeedB(Seed.Y, Seed.Z, Seed.X), SeedC(Seed.Z, Seed.X, Seed.Y);
	const FVector Warp(FMath::PerlinNoise3D(UnitDirection * 2.3 + Seed), FMath::PerlinNoise3D(UnitDirection * 2.3 + SeedB),
		FMath::PerlinNoise3D(UnitDirection * 2.3 - SeedC));
	FEphemeraNoiseLayer Shape;
	Shape.Frequency = Settings.ContinentFrequency;
	Shape.Octaves = 6;
	return FMath::Clamp(Fbm(UnitDirection + Warp * 0.35, Shape, Settings.Seed * 11.3 + 40.0) * 2.1 - Settings.SeaBias, -1.0, 1.0);
}

double FEphemeraTerrain::HeightM(const FEphemeraTerrainSettings& Settings, double RadiusM, const FVector& UnitDirection)
{
	if (Settings.HeightField && Settings.HeightField->IsUsable())
	{
		return Settings.HeightField->HeightM(UnitDirection);
	}
	double Height = 0.0;
	double Land = 1.0;
	if (Settings.bContinents)
	{
		const double C = Continentalness(Settings, UnitDirection);
		Land = SmoothStep(-0.02, 0.10, C);
		// Sea floor drops away from the shore; land climbs from a coastal plain to inland plateaus.
		const double Shelf = C < 0.0 ? -Settings.SeaDepthM * SmoothStep(0.0, 0.55, -C) : Settings.ContinentHeightM * FMath::Pow(SmoothStep(0.0, 0.8, C), 1.4);
		// Mountains: sharp ridges, only well inland, in ranges rather than everywhere.
		FEphemeraNoiseLayer Ridges;
		Ridges.Frequency = Settings.MountainFrequency;
		Ridges.Octaves = 6;
		Ridges.Ridged = 1.0;
		FEphemeraNoiseLayer Ranges;
		Ranges.Frequency = Settings.MountainFrequency * 0.35;
		Ranges.Octaves = 3;
		const double Range = SmoothStep(-0.05, 0.45, Fbm(UnitDirection, Ranges, Settings.Seed * 3.1 + 90.0));
		const double Ridge = FMath::Pow(FMath::Clamp(Fbm(UnitDirection, Ridges, Settings.Seed * 7.7 + 60.0) * 0.5 + 0.5, 0.0, 1.0), 2.2);
		Height = Shelf + Ridge * Range * SmoothStep(0.12, 0.6, C) * Settings.MountainHeightM;
	}
	if (Settings.ReliefHeightM > 0.0)
	{
		const double Inland = Settings.bContinents ? SmoothStep(0.0, 0.5, Continentalness(Settings, UnitDirection)) : 1.0;
		const double Eroded = ErodedFbm(UnitDirection, Settings.ReliefFrequency, Settings.ReliefOctaves, Settings.ReliefErosion, Settings.Seed);
		Height += (Eroded - 0.3) * Settings.ReliefHeightM * FMath::Lerp(0.3, 1.0, Inland) * FMath::Lerp(0.3, 1.0, Land);
	}
	for (int32 Index = 0; Index < Settings.Layers.Num(); ++Index)
	{
		// Detail layers fade out under the sea: the sea floor is smooth silt, not hills.
		Height += Fbm(UnitDirection, Settings.Layers[Index], Settings.Seed * 31.7 + Index * 101.3) * Settings.Layers[Index].AmplitudeM * FMath::Lerp(0.25, 1.0, Land);
	}
	if (Settings.Craters.bEnabled)
	{
		// Maria first: the highlands' gentle swell gives way to low, nearly level plains.
		Height = FMath::Lerp(Height, -Settings.Craters.MariaDepthM, MariaMask(Settings, UnitDirection) * 0.85);
		Height += CraterField(Settings, RadiusM, UnitDirection).HeightM;
	}
	// Features are sized in metres along the ground: the angle from the feature's direction times the radius.
	for (const FEphemeraTerrainFeatureDef& Feature : Settings.Features)
	{
		const FVector Dir = Feature.Direction.GetSafeNormal();
		const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(UnitDirection, Dir), -1.0, 1.0));
		const double AngularRadius = Feature.RadiusM / FMath::Max(RadiusM, 1.0);
		if (AngularRadius <= 0.0 || Angle > AngularRadius * (1.0 + Feature.Falloff))
		{
			continue;
		}
		const double Inside = 1.0 - SmoothStep(AngularRadius, AngularRadius * (1.0 + Feature.Falloff), Angle);
		if (Feature.Type == EEphemeraTerrainFeature::Plateau)
		{
			Height = FMath::Lerp(Height, Feature.HeightM, Inside);
		}
		else
		{
			const double T = FMath::Clamp(Angle / AngularRadius, 0.0, 1.0);
			const double Bowl = -Feature.HeightM * (1.0 - T * T);                               // paraboloid bowl
			const double Rim = Feature.HeightM * 0.22 * FMath::Exp(-FMath::Square((T - 1.0) / 0.18));  // raised lip
			Height = FMath::Lerp(Height, Height * 0.25 + Bowl, SmoothStep(1.0, 0.75, T)) + Rim * Inside;
		}
	}
	return Height;
}

FVector FEphemeraTerrain::Normal(const FEphemeraTerrainSettings& Settings, double RadiusM, const FVector& UnitDirection)
{
	// Central differences along two tangents, about half a metre apart on the ground; for a baked
	// terrain, one grid spacing apart (closer than that the surface is flat facets between samples).
	FVector TangentA, TangentB;
	UnitDirection.FindBestAxisVectors(TangentA, TangentB);
	const bool bBaked = Settings.HeightField && Settings.HeightField->IsUsable();
	const double Step = (bBaked ? Settings.HeightField->SpacingM(RadiusM) : 0.5) / FMath::Max(RadiusM, 1.0);
	auto Point = [&](const FVector& Offset)
	{
		const FVector Dir = (UnitDirection + Offset).GetSafeNormal();
		return Dir * (RadiusM + HeightM(Settings, RadiusM, Dir));
	};
	const FVector DA = Point(TangentA * Step) - Point(-TangentA * Step);
	const FVector DB = Point(TangentB * Step) - Point(-TangentB * Step);
	FVector N = FVector::CrossProduct(DA, DB).GetSafeNormal();
	return FVector::DotProduct(N, UnitDirection) < 0.0 ? -N : N;
}

FVector FEphemeraTerrain::SurfacePointCm(const UEphemeraBodyDefinition& Definition, const FVector& UnitDirection, const bool bWithRelief)
{
	const FVector Dir = UnitDirection.GetSafeNormal();
	const double Relief = bWithRelief ? MicroReliefM(Definition, Dir) : 0.0;
	return Dir * ((Definition.RadiusM + HeightM(Definition.Terrain, Definition.RadiusM, Dir) + Relief) * 100.0);
}

namespace
{
	// The planet material's whole-number hash (FCells::Hash3(c).x) and the value noise built on it.
	double ReliefHash(const int32 X, const int32 Y, const int32 Z)
	{
		const uint32 UX = static_cast<uint32>(X + 1048576) * 1597334673u;
		const uint32 UY = static_cast<uint32>(Y + 1048576) * 3812015801u;
		const uint32 UZ = static_cast<uint32>(Z + 1048576) * 2798796415u;
		const uint32 A = (UX ^ UY ^ UZ) * 1597334673u;
		return static_cast<double>(A >> 8) * (1.0 / 16777216.0);
	}

	double ReliefNoise(const FVector& Q)
	{
		const double FX = FMath::FloorToDouble(Q.X), FY = FMath::FloorToDouble(Q.Y), FZ = FMath::FloorToDouble(Q.Z);
		const int32 X = static_cast<int32>(FX), Y = static_cast<int32>(FY), Z = static_cast<int32>(FZ);
		auto Ease = [](const double T) { return T * T * (3.0 - 2.0 * T); };
		const double A = Ease(Q.X - FX), B = Ease(Q.Y - FY), C = Ease(Q.Z - FZ);
		const double Low = FMath::Lerp(FMath::Lerp(ReliefHash(X, Y, Z), ReliefHash(X + 1, Y, Z), A),
			FMath::Lerp(ReliefHash(X, Y + 1, Z), ReliefHash(X + 1, Y + 1, Z), A), B);
		const double High = FMath::Lerp(FMath::Lerp(ReliefHash(X, Y, Z + 1), ReliefHash(X + 1, Y, Z + 1), A),
			FMath::Lerp(ReliefHash(X, Y + 1, Z + 1), ReliefHash(X + 1, Y + 1, Z + 1), A), B);
		return FMath::Lerp(Low, High, C);
	}
}

double FEphemeraTerrain::MicroReliefM(const FVector& PositionM, const double Cover, const double Ice, const double AltitudeM, const double Flow)
{
	// Sand flats, beaches, glacier ice and stream beds are smooth; everything else rolls and is hummocked.
	const double Open = FMath::SmoothStep(-0.55, -0.05, Cover) * (1.0 - FMath::SmoothStep(0.2, 0.6, Ice)) * FMath::SmoothStep(1.0, 6.0, AltitudeM)
		* (1.0 - FMath::SmoothStep(0.52, 0.80, Flow));
	if (Open <= 0.0)
	{
		return 0.0;
	}
	const double Swell = ReliefNoise(PositionM / 17.0 + FVector(3.0));
	const double Roll = ReliefNoise(PositionM / 6.3 + FVector(11.0));
	const double Hummock = FMath::Abs(ReliefNoise(PositionM / 2.1 + FVector(23.0)) - 0.5);
	return (Swell * 0.9 + Roll * 0.45 + Hummock * 0.44) * Open;
}

double FEphemeraTerrain::FlowAsShaded(const FEphemeraTerrainSettings& Settings, const FVector& UnitDirection)
{
	const double Linear = FMath::Clamp(Moisture(Settings, UnitDirection), 0.0, 1.0);
	return Linear <= 0.0031308 ? Linear * 12.92 : 1.055 * FMath::Pow(Linear, 1.0 / 2.4) - 0.055;
}

bool FEphemeraTerrain::ReliefIsGeometry()
{
	static const IConsoleVariable* Tessellation = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Nanite.Tessellation"));
	return Tessellation && Tessellation->GetInt() != 0;
}

bool FEphemeraTerrain::ReliefStandsUp(const UEphemeraBodyDefinition& Definition)
{
	return ReliefIsGeometry() || (UEphemeraGroundSurfaceComponent::Wanted(&Definition) && !Definition.TerrainChunks.IsEmpty());
}

double FEphemeraTerrain::MicroReliefM(const UEphemeraBodyDefinition& Definition, const FVector& UnitDirection)
{
	if (Definition.MicroRelief <= 0.f || !ReliefStandsUp(Definition))
	{
		return 0.0;
	}
	const double AltitudeM = HeightM(Definition.Terrain, Definition.RadiusM, UnitDirection);
	return Definition.MicroRelief * MicroReliefM(UnitDirection * (Definition.RadiusM + AltitudeM), Cover(Definition.Terrain, UnitDirection),
		Ice(Definition.Terrain, UnitDirection), AltitudeM, FlowAsShaded(Definition.Terrain, UnitDirection));
}

double FEphemeraTerrain::Ice(const FEphemeraTerrainSettings& Settings, const FVector& UnitDirection)
{
	return Settings.HeightField && Settings.HeightField->IsUsable() ? Settings.HeightField->Sample(Settings.HeightField->Ice, UnitDirection) : 0.0;
}

double FEphemeraTerrain::Cover(const FEphemeraTerrainSettings& Settings, const FVector& UnitDirection)
{
	return Settings.HeightField && Settings.HeightField->IsUsable() ? Settings.HeightField->Sample(Settings.HeightField->Cover, UnitDirection) * 2.0 - 1.0 : 0.0;
}

double FEphemeraTerrain::FreshEjecta(const FEphemeraTerrainSettings& Settings, double RadiusM, const FVector& UnitDirection)
{
	if (Settings.HeightField && Settings.HeightField->IsUsable())
	{
		return Settings.HeightField->Sample(Settings.HeightField->Young, UnitDirection);
	}
	return Settings.Craters.bEnabled ? CraterField(Settings, RadiusM, UnitDirection).Fresh : 0.0;
}

double FEphemeraTerrain::Moisture(const FEphemeraTerrainSettings& Settings, const FVector& UnitDirection)
{
	if (Settings.HeightField && Settings.HeightField->IsUsable())
	{
		// On a baked terrain "moisture" is where the water actually runs.
		return Settings.HeightField->Sample(Settings.HeightField->Flow, UnitDirection);
	}
	if (Settings.Craters.bEnabled)
	{
		return MariaMask(Settings, UnitDirection);
	}
	return 0.5 + 0.5 * FMath::PerlinNoise3D(UnitDirection * 4.3 + FVector(11.7 + Settings.Seed, 3.1, 8.9));
}

double FEphemeraTerrain::Patch(const FEphemeraTerrainSettings& Settings, double Frequency, int32 Salt, const FVector& UnitDirection)
{
	const FVector Offset(Settings.Seed * 3.7 + Salt * 13.1, Salt * 7.3 - 2.9, Settings.Seed * 1.3 + Salt * 5.7);
	const double N = FMath::PerlinNoise3D(UnitDirection * Frequency + Offset) * 0.65 + FMath::PerlinNoise3D(UnitDirection * Frequency * 2.3 - Offset) * 0.35;
	return FMath::Clamp(0.5 + 0.5 * N * 1.6, 0.0, 1.0);
}

bool FEphemeraTerrain::InsidePlateau(const FEphemeraTerrainSettings& Settings, double RadiusM, const FVector& UnitDirection, double Margin)
{
	for (const FEphemeraTerrainFeatureDef& Feature : Settings.Features)
	{
		if (Feature.Type != EEphemeraTerrainFeature::Plateau)
		{
			continue;
		}
		const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(UnitDirection, Feature.Direction.GetSafeNormal()), -1.0, 1.0));
		if (Angle * RadiusM < Feature.RadiusM * Margin)
		{
			return true;
		}
	}
	return false;
}

FTransform FEphemeraTerrain::Seat(const UStaticMesh* Mesh, const FQuat& Rotation, const FVector& GroundPoint, double Scale)
{
	FVector Foot = FVector::ZeroVector;
	if (Mesh)
	{
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		Foot = FVector(Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z - Bounds.BoxExtent.Z);
	}
	return FTransform(Rotation, GroundPoint - Rotation.RotateVector(Foot * Scale), FVector(Scale));
}
