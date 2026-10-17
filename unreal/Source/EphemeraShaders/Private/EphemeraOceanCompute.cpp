#include "EphemeraOceanCompute.h"

#include "GenerateMips.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterStruct.h"

// The passes of Shaders/EphemeraOcean.usf (see there for what each does and the conventions).
namespace
{
	class FOceanSpectrumCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FOceanSpectrumCS);
		SHADER_USE_PARAMETER_STRUCT(FOceanSpectrumCS, FGlobalShader);
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, H0)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SpecA)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SpecB)
			SHADER_PARAMETER(float, TimeS)
			SHADER_PARAMETER(float, PatchM)
			SHADER_PARAMETER(float, Choppiness)
			SHADER_PARAMETER(float, Gravity)
		END_SHADER_PARAMETER_STRUCT()
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
		// The passes read back what they wrote (the spectra in place, the foam from frame to frame) from half and
		// full float textures: typed UAV loads, which every DirectX 12 card the game runs on has (D3D11 does not
		// promise them, and the cook refuses them unasked).
		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FOceanSpectrumCS, "/EphemeraShaders/EphemeraOcean.usf", "SpectrumCS", SF_Compute);

	class FOceanFftCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FOceanFftCS);
		SHADER_USE_PARAMETER_STRUCT(FOceanFftCS, FGlobalShader);
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SpecA)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SpecB)
			SHADER_PARAMETER(uint32, Horizontal)
		END_SHADER_PARAMETER_STRUCT()
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
		// The passes read back what they wrote (the spectra in place, the foam from frame to frame) from half and
		// full float textures: typed UAV loads, which every DirectX 12 card the game runs on has (D3D11 does not
		// promise them, and the cook refuses them unasked).
		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FOceanFftCS, "/EphemeraShaders/EphemeraOcean.usf", "FftCS", SF_Compute);

	class FOceanAssembleCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FOceanAssembleCS);
		SHADER_USE_PARAMETER_STRUCT(FOceanAssembleCS, FGlobalShader);
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, FieldA)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, FieldB)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, MoveOut)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SlopeOut)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, FoamState)
			SHADER_PARAMETER(float, DeltaS)
			SHADER_PARAMETER(float, FoamStart)
			SHADER_PARAMETER(float, FoamFull)
			SHADER_PARAMETER(float, FoamSeconds)
		END_SHADER_PARAMETER_STRUCT()
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
		// The passes read back what they wrote (the spectra in place, the foam from frame to frame) from half and
		// full float textures: typed UAV loads, which every DirectX 12 card the game runs on has (D3D11 does not
		// promise them, and the cook refuses them unasked).
		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(FOceanAssembleCS, "/EphemeraShaders/EphemeraOcean.usf", "AssembleCS", SF_Compute);
}

void EphemeraRenderOcean(FRHICommandListImmediate& RHICmdList, const ERHIFeatureLevel::Type FeatureLevel,
	const TArray<FEphemeraOceanCascadeRHI>& Cascades, const FEphemeraOceanFrame& Frame)
{
	check(IsInRenderingThread());
	if (Cascades.IsEmpty())
	{
		return;
	}
	FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("EphemeraOcean"));
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FOceanSpectrumCS> Spectrum(ShaderMap);
	TShaderMapRef<FOceanFftCS> Fft(ShaderMap);
	TShaderMapRef<FOceanAssembleCS> Assemble(ShaderMap);
	const int32 N = EphemeraOceanTexels;
	const FIntVector Tiles(N / 8, N / 8, 1);
	const FRDGTextureDesc WorkDesc = FRDGTextureDesc::Create2D(FIntPoint(N, N), PF_A32B32G32R32F, FClearValueBinding::Black,
		TexCreate_ShaderResource | TexCreate_UAV);

	for (int32 Index = 0; Index < Cascades.Num(); ++Index)
	{
		const FEphemeraOceanCascadeRHI& Cascade = Cascades[Index];
		if (!Cascade.Spectrum || !Cascade.Move || !Cascade.Slope || !Cascade.Foam)
		{
			continue;
		}
		FRDGTextureRef H0 = RegisterExternalTexture(GraphBuilder, Cascade.Spectrum, TEXT("EphemeraOcean.H0"));
		FRDGTextureRef Move = RegisterExternalTexture(GraphBuilder, Cascade.Move, TEXT("EphemeraOcean.Move"));
		FRDGTextureRef Slope = RegisterExternalTexture(GraphBuilder, Cascade.Slope, TEXT("EphemeraOcean.Slope"));
		FRDGTextureRef Foam = RegisterExternalTexture(GraphBuilder, Cascade.Foam, TEXT("EphemeraOcean.Foam"));
		FRDGTextureRef SpecA = GraphBuilder.CreateTexture(WorkDesc, TEXT("EphemeraOcean.SpecA"));
		FRDGTextureRef SpecB = GraphBuilder.CreateTexture(WorkDesc, TEXT("EphemeraOcean.SpecB"));

		{
			FOceanSpectrumCS::FParameters* P = GraphBuilder.AllocParameters<FOceanSpectrumCS::FParameters>();
			P->H0 = H0;
			P->SpecA = GraphBuilder.CreateUAV(SpecA);
			P->SpecB = GraphBuilder.CreateUAV(SpecB);
			P->TimeS = Frame.TimeS;
			P->PatchM = Cascade.PatchM;
			P->Choppiness = Cascade.Choppiness;
			P->Gravity = Frame.Gravity;
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OceanSpectrum %d", Index), Spectrum, P, Tiles);
		}
		for (const uint32 Horizontal : {1u, 0u})
		{
			FOceanFftCS::FParameters* P = GraphBuilder.AllocParameters<FOceanFftCS::FParameters>();
			P->SpecA = GraphBuilder.CreateUAV(SpecA);
			P->SpecB = GraphBuilder.CreateUAV(SpecB);
			P->Horizontal = Horizontal;
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OceanFft %d %s", Index, Horizontal ? TEXT("rows") : TEXT("columns")),
				Fft, P, FIntVector(N, 1, 1));
		}
		{
			FOceanAssembleCS::FParameters* P = GraphBuilder.AllocParameters<FOceanAssembleCS::FParameters>();
			P->FieldA = SpecA;
			P->FieldB = SpecB;
			P->MoveOut = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Move, 0));
			P->SlopeOut = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Slope, 0));
			P->FoamState = GraphBuilder.CreateUAV(Foam);
			P->DeltaS = Frame.DeltaS;
			P->FoamStart = Frame.FoamStart;
			P->FoamFull = Frame.FoamFull;
			P->FoamSeconds = Frame.FoamSeconds;
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OceanAssemble %d", Index), Assemble, P, Tiles);
		}
		// Every mip of the slopes holds the mean of slope and of slope squared over its texels: what a pixel
		// far off cannot show as a wave it shows as roughness (their difference), so distant water neither
		// shimmers nor repeats.
		FGenerateMipsParams Mips;
		Mips.Filter = SF_Bilinear;
		Mips.AddressU = AM_Wrap;
		Mips.AddressV = AM_Wrap;
		if (Move->Desc.NumMips > 1)
		{
			FGenerateMips::Execute(GraphBuilder, FeatureLevel, Move, Mips);
		}
		if (Slope->Desc.NumMips > 1)
		{
			FGenerateMips::Execute(GraphBuilder, FeatureLevel, Slope, Mips);
		}
		GraphBuilder.SetTextureAccessFinal(Move, ERHIAccess::SRVMask);
		GraphBuilder.SetTextureAccessFinal(Slope, ERHIAccess::SRVMask);
	}
	GraphBuilder.Execute();
}
