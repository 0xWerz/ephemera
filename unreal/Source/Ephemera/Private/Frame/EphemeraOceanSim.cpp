#include "Frame/EphemeraOceanSim.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EphemeraOceanCompute.h"
#include "Frame/EphemeraBodyDefinition.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"
#include "RenderingThread.h"
#include "TextureResource.h"

namespace
{
	// The sea, as two systems of waves (docs/research/ocean_and_underwater.md 3.2). Hs: significant height
	// (four times the standard deviation of the surface), metres. Heading: the way they run, radians from a
	// chart's first axis. Spread: s in the cos^2s(angle / 2) spreading of their directions.
	struct FWaveSystem
	{
		double Hs, PeakPeriodS, WindMs, FetchM, Gamma, Spread, Heading;
	};
	// A long swell come from far off (this world is too small for its own wind to raise one) and the sea its
	// own wind raises over 8 km of water at 10 m/s (fetch-limited JONSWAP).
	constexpr FWaveSystem Swell{1.2, 7.7, 0.0, 0.0, 6.0, 30.0, 0.0};
	constexpr FWaveSystem Wind{0.44, 0.0, 10.0, 8000.0, 3.3, 8.0, 0.44};
	constexpr uint32 Seed = 29;
	// Each cascade holds no wave longer than this fraction of the next smaller patch...
	constexpr double PatchesPerWave = 6.0;
	// ... and the smallest none shorter than 2.5 of its texels.
	constexpr double FinestOfNyquist = 0.8;
	// The game's own copy of each band: its strongest waves, until they hold this much of its variance.
	constexpr double CpuShare = 0.996;
	constexpr int32 CpuMost = 900;

	double PeakOmega(const FWaveSystem& System, const double G)
	{
		if (System.PeakPeriodS > 0.0)
		{
			return 2.0 * PI / System.PeakPeriodS;
		}
		// Fetch-limited peak (JONSWAP): omega_p = 22 (g^2 / (U F))^(1/3).
		return 22.0 * FMath::Pow(G * G / (System.WindMs * System.FetchM), 1.0 / 3.0);
	}

	double Jonswap(const double Omega, const double Peak, const double Gamma)
	{
		if (Omega <= 0.0)
		{
			return 0.0;
		}
		const double Sigma = Omega <= Peak ? 0.07 : 0.09;
		const double R = FMath::Exp(-FMath::Square(Omega - Peak) / (2.0 * Sigma * Sigma * Peak * Peak));
		return FMath::Pow(Omega, -5.0) * FMath::Exp(-1.25 * FMath::Pow(Peak / Omega, 4.0)) * FMath::Pow(Gamma, R);
	}

	/** cos^2s of half the angle from the heading, normalised over the circle. */
	double Spreading(const double Theta, const double Heading, const double S, const double Norm)
	{
		const double Off = FMath::Atan2(FMath::Sin(Theta - Heading), FMath::Cos(Theta - Heading));
		return FMath::Pow(FMath::Cos(Off / 2.0), 2.0 * S) / Norm;
	}

	double SpreadingNorm(const double S)
	{
		constexpr int32 Steps = 8192;
		double Sum = 0.0;
		for (int32 I = 0; I < Steps; ++I)
		{
			Sum += FMath::Pow(FMath::Cos((-PI + 2.0 * PI * I / Steps) / 2.0), 2.0 * S);
		}
		return Sum * 2.0 * PI / Steps;
	}

	/** Variance per unit area of wavenumber space: E(k, theta) = S(omega) (d omega / dk) D(theta) / k (unscaled). */
	double Density(const FWaveSystem& System, const double Peak, const double Norm, const double Kx, const double Kz, const double G)
	{
		const double K = FMath::Sqrt(Kx * Kx + Kz * Kz);
		if (K <= 0.0)
		{
			return 0.0;
		}
		const double Omega = FMath::Sqrt(G * K);
		return Jonswap(Omega, Peak, System.Gamma) * (G / (2.0 * Omega)) / K * Spreading(FMath::Atan2(Kz, Kx), System.Heading, System.Spread, Norm);
	}

	UTextureRenderTarget2D* MakeTarget(UObject* Outer, const bool bMips)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Outer);
		Target->RenderTargetFormat = RTF_RGBA16f;
		Target->bAutoGenerateMips = bMips;
		Target->bCanCreateUAV = true;
		Target->AddressX = TA_Wrap;
		Target->AddressY = TA_Wrap;
		Target->ClearColor = FLinearColor::Black;
		// Sampled as world textures are: trilinear, with the world's anisotropic filtering, so grazing water
		// keeps its waves instead of smearing them.
		Target->LODGroup = TEXTUREGROUP_World;
		Target->InitCustomFormat(EphemeraOceanTexels, EphemeraOceanTexels, PF_FloatRGBA, true);
		Target->UpdateResourceImmediate(true);
		return Target;
	}
}

void UEphemeraOceanSim::Build(TArray<FEphemeraSeaBand>& Bands, const double Gravity)
{
	Cascades.Reset();
	Spectra.Reset();
	Moves.Reset();
	Slopes.Reset();
	Foams.Reset();
	GravityMs2 = Gravity;
	const int32 Count = FMath::Min(Bands.Num(), MaxCascades);
	if (Count == 0)
	{
		return;
	}
	const int32 N = EphemeraOceanTexels;
	const double G = Gravity;

	// The wavenumbers each cascade holds: from where the coarser one stops to a sixth of the next patch.
	TArray<double> Lower, Upper;
	for (int32 C = 0; C < Count; ++C)
	{
		Lower.Add(C == 0 ? 0.0 : Upper[C - 1]);
		Upper.Add(C + 1 < Count ? 2.0 * PI * PatchesPerWave / Bands[C + 1].PatchM : FinestOfNyquist * PI * N / Bands[C].PatchM);
	}

	// Each system scaled to its height over the bins of every cascade together (each bin belongs to one).
	const FWaveSystem Systems[2] = {Swell, Wind};
	double Peak[2], Norm[2], Total[2] = {0.0, 0.0};
	for (int32 S = 0; S < 2; ++S)
	{
		Peak[S] = PeakOmega(Systems[S], G);
		Norm[S] = SpreadingNorm(Systems[S].Spread);
	}
	auto Index = [N](const int32 T) { return T < N / 2 ? T : T - N; };
	auto Inside = [&](const int32 C, const int32 Nx, const int32 Nz, double& Kx, double& Kz)
	{
		const double Dk = 2.0 * PI / Bands[C].PatchM;
		Kx = Nx * Dk;
		Kz = Nz * Dk;
		const double K = FMath::Sqrt(Kx * Kx + Kz * Kz);
		// The Nyquist row and column carry no waves (EphemeraOcean.usf).
		return FMath::Abs(Nx) < N / 2 && FMath::Abs(Nz) < N / 2 && K > Lower[C] && K <= Upper[C];
	};
	for (int32 C = 0; C < Count; ++C)
	{
		const double Dk2 = FMath::Square(2.0 * PI / Bands[C].PatchM);
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				double Kx, Kz;
				if (Inside(C, Index(X), Index(Y), Kx, Kz))
				{
					for (int32 S = 0; S < 2; ++S)
					{
						Total[S] += Density(Systems[S], Peak[S], Norm[S], Kx, Kz, G) * Dk2;
					}
				}
			}
		}
	}
	double Scale[2];
	for (int32 S = 0; S < 2; ++S)
	{
		Scale[S] = Total[S] > 0.0 ? FMath::Square(Systems[S].Hs / 4.0) / Total[S] : 0.0;
	}

	FRandomStream Random(Seed);
	auto Gaussian = [&Random]()
	{
		const double U1 = FMath::Max(Random.GetFraction(), 1e-12), U2 = Random.GetFraction();
		return FMath::Sqrt(-2.0 * FMath::Loge(U1)) * FMath::Cos(2.0 * PI * U2);
	};
	for (int32 C = 0; C < Count; ++C)
	{
		FEphemeraSeaBand& Band = Bands[C];
		const double Dk2 = FMath::Square(2.0 * PI / Band.PatchM);
		// One complex amplitude per wave (metres): the surface is the sum of Re(A exp(i (k.x - omega t))).
		TArray<FVector2D> Amp;
		Amp.SetNumZeroed(N * N);
		double Variance = 0.0, Mss = 0.0;
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const double Re = Gaussian(), Im = Gaussian();
				double Kx, Kz;
				if (!Inside(C, Index(X), Index(Y), Kx, Kz))
				{
					continue;
				}
				double V = 0.0;
				for (int32 S = 0; S < 2; ++S)
				{
					V += Density(Systems[S], Peak[S], Norm[S], Kx, Kz, G) * Dk2 * Scale[S];
				}
				const double A = FMath::Sqrt(V);
				Amp[Y * N + X] = FVector2D(A * Re, A * Im);
				Variance += V;
				Mss += V * (Kx * Kx + Kz * Kz);
			}
		}

		// A(k) and A(-k) side by side, as the GPU wants them.
		UTexture2D* Spectrum = UTexture2D::CreateTransient(N, N, PF_A32B32G32R32F);
		Spectrum->SRGB = false;
		Spectrum->Filter = TF_Nearest;
		Spectrum->NeverStream = true;
		{
			FTexture2DMipMap& Mip = Spectrum->GetPlatformData()->Mips[0];
			float* Texels = static_cast<float*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
			for (int32 Y = 0; Y < N; ++Y)
			{
				for (int32 X = 0; X < N; ++X)
				{
					const FVector2D& A = Amp[Y * N + X];
					const FVector2D& Opposite = Amp[((N - Y) % N) * N + (N - X) % N];
					float* T = Texels + 4 * (Y * N + X);
					T[0] = float(A.X);
					T[1] = float(A.Y);
					T[2] = float(Opposite.X);
					T[3] = float(Opposite.Y);
				}
			}
			Mip.BulkData.Unlock();
		}
		Spectrum->UpdateResource();
		Spectra.Add(Spectrum);
		Moves.Add(MakeTarget(this, true));
		Slopes.Add(MakeTarget(this, true));
		Foams.Add(MakeTarget(this, false));
		Cascades.Add({Band.PatchM, Band.Choppiness});

		// The game's copy: the strongest waves, as (kx, kz, omega, Re A, Im A).
		TArray<int32> Order;
		for (int32 T = 0; T < N * N; ++T)
		{
			if (!Amp[T].IsZero())
			{
				Order.Add(T);
			}
		}
		Order.Sort([&Amp](const int32 A, const int32 B) { return Amp[A].SizeSquared() > Amp[B].SizeSquared(); });
		double Power = 0.0, Kept = 0.0;
		for (const int32 T : Order)
		{
			Power += Amp[T].SizeSquared();
		}
		Band.Waves.Reset();
		for (const int32 T : Order)
		{
			if (Band.Waves.Num() / 5 >= CpuMost || (Power > 0.0 && Kept / Power >= CpuShare))
			{
				break;
			}
			double Kx, Kz;
			Inside(C, Index(T % N), Index(T / N), Kx, Kz);
			Band.Waves.Append({float(Kx), float(Kz), float(FMath::Sqrt(G * FMath::Sqrt(Kx * Kx + Kz * Kz))), float(Amp[T].X), float(Amp[T].Y)});
			Kept += Amp[T].SizeSquared();
		}
		UE_LOG(LogTemp, Display, TEXT("Ephemera sea: cascade %d, %.1f m patch, waves %.2f-%.1f m: Hs %.3f m, mean square slope %.4f; game copy %d waves (%.1f%% of the variance)"),
			C, Band.PatchM, 2.0 * PI / Upper[C], Lower[C] > 0.0 ? 2.0 * PI / Lower[C] : 0.0, 4.0 * FMath::Sqrt(Variance), Mss,
			Band.Waves.Num() / 5, Power > 0.0 ? 100.0 * Kept / Power : 0.0);
	}
}

void UEphemeraOceanSim::Bind(UMaterialInstanceDynamic* Material) const
{
	if (!Material)
	{
		return;
	}
	for (int32 C = 0; C < Cascades.Num(); ++C)
	{
		Material->SetTextureParameterValue(*FString::Printf(TEXT("Casc%dMove"), C), Moves[C]);
		Material->SetTextureParameterValue(*FString::Printf(TEXT("Casc%dSlope"), C), Slopes[C]);
	}
}

void UEphemeraOceanSim::Simulate(const double TimeS, const float DeltaS)
{
	if (Cascades.IsEmpty())
	{
		return;
	}
	struct FJob
	{
		FTextureResource* Spectrum;
		FTextureResource* Move;
		FTextureResource* Slope;
		FTextureResource* Foam;
		float PatchM, Choppiness;
	};
	TArray<FJob> Jobs;
	for (int32 C = 0; C < Cascades.Num(); ++C)
	{
		FTextureResource* Spectrum = Spectra[C] ? Spectra[C]->GetResource() : nullptr;
		FTextureResource* Move = Moves[C] ? Moves[C]->GetResource() : nullptr;
		FTextureResource* Slope = Slopes[C] ? Slopes[C]->GetResource() : nullptr;
		FTextureResource* Foam = Foams[C] ? Foams[C]->GetResource() : nullptr;
		if (Spectrum && Move && Slope && Foam)
		{
			Jobs.Add({Spectrum, Move, Slope, Foam, Cascades[C].PatchM, Cascades[C].Choppiness});
		}
	}
	FEphemeraOceanFrame Frame;
	Frame.TimeS = float(TimeS);
	Frame.DeltaS = DeltaS;
	Frame.Gravity = float(GravityMs2);
	const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
	ENQUEUE_RENDER_COMMAND(EphemeraOcean)([Jobs = MoveTemp(Jobs), Frame, FeatureLevel](FRHICommandListImmediate& RHICmdList)
	{
		TArray<FEphemeraOceanCascadeRHI> Cascades;
		for (const FJob& Job : Jobs)
		{
			FEphemeraOceanCascadeRHI Cascade;
			Cascade.Spectrum = Job.Spectrum->GetTextureRHI();
			Cascade.Move = Job.Move->GetTextureRHI();
			Cascade.Slope = Job.Slope->GetTextureRHI();
			Cascade.Foam = Job.Foam->GetTextureRHI();
			Cascade.PatchM = Job.PatchM;
			Cascade.Choppiness = Job.Choppiness;
			if (Cascade.Spectrum && Cascade.Move && Cascade.Slope && Cascade.Foam)
			{
				Cascades.Add(Cascade);
			}
		}
		EphemeraRenderOcean(RHICmdList, FeatureLevel, Cascades, Frame);
	});
}
