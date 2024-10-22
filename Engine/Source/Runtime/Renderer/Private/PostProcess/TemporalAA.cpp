// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/TemporalAA.h"
#include "PostProcess/PostProcessTonemap.h"
#include "PostProcess/PostProcessMitchellNetravali.h"
#include "PostProcess/PostProcessing.h"
#include "ClearQuad.h"
#include "PostProcessing.h"
#include "SceneTextureParameters.h"
#include "PixelShaderUtils.h"

namespace
{

const int32 GTemporalAATileSizeX = 8;
const int32 GTemporalAATileSizeY = 8;

constexpr int32 kHistoryTextures = 3;

TAutoConsoleVariable<int32> CVarTAAAlgorithm(
	TEXT("r.TemporalAA.Algorithm"), 0,
	TEXT("Algorithm to use for Temporal AA\n")
	TEXT(" 0: Gen 4 TAAU (default)\n")
	TEXT(" 1: Gen 5 TAAU (experimental)"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarTemporalAAFilterSize(
	TEXT("r.TemporalAAFilterSize"),
	1.0f,
	TEXT("Size of the filter kernel. (1.0 = smoother, 0.0 = sharper but aliased)."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTemporalAACatmullRom(
	TEXT("r.TemporalAACatmullRom"),
	0,
	TEXT("Whether to use a Catmull-Rom filter kernel. Should be a bit sharper than Gaussian."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTemporalAAPauseCorrect(
	TEXT("r.TemporalAAPauseCorrect"),
	1,
	TEXT("Correct temporal AA in pause. This holds onto render targets longer preventing reuse and consumes more memory."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarTemporalAACurrentFrameWeight(
	TEXT("r.TemporalAACurrentFrameWeight"),
	.04f,
	TEXT("Weight of current frame's contribution to the history.  Low values cause blurriness and ghosting, high values fail to hide jittering."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTemporalAAUpsampleFiltered(
	TEXT("r.TemporalAAUpsampleFiltered"),
	1,
	TEXT("Use filtering to fetch color history during TamporalAA upsampling (see AA_FILTERED define in TAA shader). Disabling this makes TAAU faster, but lower quality. "),
	ECVF_Scalability | ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarTemporalAAHistorySP(
	TEXT("r.TemporalAA.HistoryScreenPercentage"),
	100.0f,
	TEXT("Size of temporal AA's history."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTemporalAAAllowDownsampling(
	TEXT("r.TemporalAA.AllowDownsampling"),
	1,
	TEXT("Allows half-resolution color buffer to be produced during TAA. Only possible when motion blur is off and when using compute shaders for post processing."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarUseTemporalAAUpscaler(
	TEXT("r.TemporalAA.Upscaler"),
	1,
	TEXT("Choose the upscaling algorithm.\n")
	TEXT(" 0: Forces the default temporal upscaler of the renderer;\n")
	TEXT(" 1: GTemporalUpscaler which may be overridden by a third party plugin (default)."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTAAR11G11B10History(
	TEXT("r.TemporalAA.R11G11B10History"), 0,
	TEXT("Select the bitdepth of the history."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarTAANyquistHistory(
	TEXT("r.TemporalAA.NyquistHistory"), 0,
	TEXT(""),
	ECVF_RenderThreadSafe);

inline bool DoesPlatformSupportTemporalHistoryUpscale(EShaderPlatform Platform)
{
	return (IsPCPlatform(Platform) || FDataDrivenShaderPlatformInfo::GetSupportsTemporalHistoryUpscale(Platform))
		&& IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
}

inline bool DoesPlatformSupportGen5TAA(EShaderPlatform Platform)
{
	return Platform == SP_PCD3D_SM5;
	//return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
}

BEGIN_SHADER_PARAMETER_STRUCT(FTAA2CommonParameters, )
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, InputInfo)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, LowFrequencyInfo)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, RejectionInfo)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, OutputInfo)
	SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, HistoryInfo)

	SHADER_PARAMETER(FVector2D, InputJitter)

	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FTAA2HistoryTextures, )
	SHADER_PARAMETER_RDG_TEXTURE_ARRAY(Texture2D, Textures, [kHistoryTextures])
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FTAA2HistoryUAVs, )
	SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(RWTexture2D, Textures, [kHistoryTextures])
END_SHADER_PARAMETER_STRUCT()

FTAA2HistoryUAVs CreateUAVs(FRDGBuilder& GraphBuilder, const FTAA2HistoryTextures& Textures)
{
	FTAA2HistoryUAVs UAVs;
	for (int32 i = 0; i < kHistoryTextures; i++)
	{
		UAVs.Textures[i] = GraphBuilder.CreateUAV(Textures.Textures[i]);
	}
	return UAVs;
}

class FTAAGen5Shader : public FGlobalShader
{
public:
	FTAAGen5Shader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{ }

	FTAAGen5Shader()
	{ }

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportGen5TAA(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		//OutEnvironment.CompilerFlags.Add(CFLAG_AllowRealTypes);
	}
}; // class FTAA2Shader

class FTAAStandaloneCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTAAStandaloneCS);
	SHADER_USE_PARAMETER_STRUCT(FTAAStandaloneCS, FGlobalShader);

	class FTAAPassConfigDim : SHADER_PERMUTATION_ENUM_CLASS("TAA_PASS_CONFIG", ETAAPassConfig);
	class FTAAFastDim : SHADER_PERMUTATION_BOOL("TAA_FAST");
	class FTAAResponsiveDim : SHADER_PERMUTATION_BOOL("TAA_RESPONSIVE");
	class FTAAScreenPercentageDim : SHADER_PERMUTATION_INT("TAA_SCREEN_PERCENTAGE_RANGE", 4);
	class FTAAUpsampleFilteredDim : SHADER_PERMUTATION_BOOL("TAA_UPSAMPLE_FILTERED");
	class FTAADownsampleDim : SHADER_PERMUTATION_BOOL("TAA_DOWNSAMPLE");

	using FPermutationDomain = TShaderPermutationDomain<
		FTAAPassConfigDim,
		FTAAFastDim,
		FTAAScreenPercentageDim,
		FTAAUpsampleFilteredDim,
		FTAADownsampleDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4, ViewportUVToInputBufferUV)
		SHADER_PARAMETER(FVector4, MaxViewportUVAndSvPositionToViewportUV)
		SHADER_PARAMETER(FVector2D, ScreenPosAbsMax)
		SHADER_PARAMETER(float, HistoryPreExposureCorrection)
		SHADER_PARAMETER(float, CurrentFrameWeight)
		SHADER_PARAMETER(int32, bCameraCut)

		SHADER_PARAMETER_ARRAY(float, SampleWeights, [9])
		SHADER_PARAMETER_ARRAY(float, PlusWeights, [5])

		SHADER_PARAMETER(FVector4, InputSceneColorSize)
		SHADER_PARAMETER(FIntPoint, InputMinPixelCoord)
		SHADER_PARAMETER(FIntPoint, InputMaxPixelCoord)
		SHADER_PARAMETER(FVector4, OutputViewportSize)
		SHADER_PARAMETER(FVector4, OutputViewportRect)

		// History parameters
		SHADER_PARAMETER(FVector4, HistoryBufferSize)
		SHADER_PARAMETER(FVector4, HistoryBufferUVMinMax)
		SHADER_PARAMETER(FVector4, ScreenPosToHistoryBufferUV)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptation)

		// Inputs
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSceneColorSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneMetadata)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSceneMetadataSampler)

		// History resources
		SHADER_PARAMETER_RDG_TEXTURE_ARRAY(Texture2D, HistoryBuffer, [FTemporalAAHistory::kRenderTargetCount])
		SHADER_PARAMETER_SAMPLER_ARRAY(SamplerState, HistoryBufferSampler, [FTemporalAAHistory::kRenderTargetCount])

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthBuffer)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthBufferSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneVelocityBuffer)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneVelocityBufferSampler)

		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, StencilTexture)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		
		// Temporal upsample specific parameters.
		SHADER_PARAMETER(FVector4, InputViewSize)
		SHADER_PARAMETER(FVector2D, InputViewMin)
		SHADER_PARAMETER(FVector2D, TemporalJitterPixels)
		SHADER_PARAMETER(float, ScreenPercentage)
		SHADER_PARAMETER(float, UpscaleFactor)

		SHADER_PARAMETER_RDG_TEXTURE_UAV_ARRAY(Texture2D, OutComputeTex, [FTemporalAAHistory::kRenderTargetCount])
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture2D, OutComputeTexDownsampled)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		// Screen percentage dimension is only for upsampling permutation.
		if (!IsTAAUpsamplingConfig(PermutationVector.Get<FTAAPassConfigDim>()) &&
			PermutationVector.Get<FTAAScreenPercentageDim>() != 0)
		{
			return false;
		}

		if (PermutationVector.Get<FTAAPassConfigDim>() == ETAAPassConfig::MainSuperSampling)
		{
			// Super sampling is only available in certain configurations.
			if (!DoesPlatformSupportTemporalHistoryUpscale(Parameters.Platform))
			{
				return false;
			}

			// No point disabling filtering.
			if (!PermutationVector.Get<FTAAUpsampleFilteredDim>())
			{
				return false;
			}

			// No point doing a fast permutation since it is PC only.
			if (PermutationVector.Get<FTAAFastDim>())
			{
				return false;
			}
		}

		// No point disabling filtering if not using the fast permutation already.
		if (!PermutationVector.Get<FTAAUpsampleFilteredDim>() &&
			!PermutationVector.Get<FTAAFastDim>())
		{
			return false;
		}

		// No point downsampling if not using the fast permutation already.
		if (PermutationVector.Get<FTAADownsampleDim>() &&
			!PermutationVector.Get<FTAAFastDim>())
		{
			return false;
		}

		// Screen percentage range 3 is only for super sampling.
		if (PermutationVector.Get<FTAAPassConfigDim>() != ETAAPassConfig::MainSuperSampling &&
			PermutationVector.Get<FTAAScreenPercentageDim>() == 3)
		{
			return false;
		}

		// Fast dimensions is only for Main and Diaphragm DOF.
		if (PermutationVector.Get<FTAAFastDim>() &&
			!IsMainTAAConfig(PermutationVector.Get<FTAAPassConfigDim>()) &&
			!IsDOFTAAConfig(PermutationVector.Get<FTAAPassConfigDim>()))
		{
			return false;
		}
		
		// Non filtering option is only for upsampling.
		if (!PermutationVector.Get<FTAAUpsampleFilteredDim>() &&
			PermutationVector.Get<FTAAPassConfigDim>() != ETAAPassConfig::MainUpsampling)
		{
			return false;
		}

		// TAA_DOWNSAMPLE is only only for Main and MainUpsampling configs.
		if (PermutationVector.Get<FTAADownsampleDim>() &&
			!IsMainTAAConfig(PermutationVector.Get<FTAAPassConfigDim>()))
		{
			return false;
		}

		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GTemporalAATileSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GTemporalAATileSizeY);
	}
}; // class FTAAStandaloneCS

class FTAA2DilateVelocityCS : public FTAAGen5Shader
{
	DECLARE_GLOBAL_SHADER(FTAA2DilateVelocityCS);
	SHADER_USE_PARAMETER_STRUCT(FTAA2DilateVelocityCS, FTAAGen5Shader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTAA2CommonParameters, CommonParameters)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneVelocityTexture)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, DilatedVelocityOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, ClosestDepthOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, PrevUseCountOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, PrevClosestDepthOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTAA2DilateVelocityCS

class FTAA2BuildParallaxMaskCS : public FTAAGen5Shader
{
	DECLARE_GLOBAL_SHADER(FTAA2BuildParallaxMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FTAA2BuildParallaxMaskCS, FTAAGen5Shader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTAA2CommonParameters, CommonParameters)
		SHADER_PARAMETER(float, WorldDepthToPixelWorldRadius)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DilatedVelocityTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ClosestDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrevUseCountTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrevClosestDepthTexture)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, ParallaxRejectionMaskOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTAA2BuildParallaxMaskCS

class FTAA2DecimateHistoryCS : public FTAAGen5Shader
{
	DECLARE_GLOBAL_SHADER(FTAA2DecimateHistoryCS);
	SHADER_USE_PARAMETER_STRUCT(FTAA2DecimateHistoryCS, FTAAGen5Shader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTAA2CommonParameters, CommonParameters)
		SHADER_PARAMETER(FVector, OutputQuantizationError)
		SHADER_PARAMETER(float, HistoryPreExposureCorrection)
		SHADER_PARAMETER(int32, bCameraCut)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DilatedVelocityTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ParallaxRejectionMaskTexture)

		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, PrevHistoryInfo)
		SHADER_PARAMETER_STRUCT(FTAA2HistoryTextures, PrevHistory)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, PredictionSceneColorOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, PredictionInfoOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTAA2DecimateHistoryCS

class FTAA2FilterFrequenciesCS : public FTAAGen5Shader
{
	DECLARE_GLOBAL_SHADER(FTAA2FilterFrequenciesCS);
	SHADER_USE_PARAMETER_STRUCT(FTAA2FilterFrequenciesCS, FTAAGen5Shader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTAA2CommonParameters, CommonParameters)
		SHADER_PARAMETER(FVector, OutputQuantizationError)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PredictionSceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PredictionInfoTexture)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, FilteredInputOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, FilteredPredictionSceneColorOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTAA2FilterFrequenciesCS

class FTAA2CompareHistoryCS : public FTAAGen5Shader
{
	DECLARE_GLOBAL_SHADER(FTAA2CompareHistoryCS);
	SHADER_USE_PARAMETER_STRUCT(FTAA2CompareHistoryCS, FTAAGen5Shader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTAA2CommonParameters, CommonParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ParallaxRejectionMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FilteredInputTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FilteredPredictionSceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, HistoryRejectionOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTAA2CompareHistoryCS

class FTAA2DilateRejectionCS : public FTAAGen5Shader
{
	DECLARE_GLOBAL_SHADER(FTAA2DilateRejectionCS);
	SHADER_USE_PARAMETER_STRUCT(FTAA2DilateRejectionCS, FTAAGen5Shader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTAA2CommonParameters, CommonParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryRejectionTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, DilatedHistoryRejectionOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTAA2DilateRejectionCS

class FTAA2UpdateHistoryCS : public FTAAGen5Shader
{
	DECLARE_GLOBAL_SHADER(FTAA2UpdateHistoryCS);
	SHADER_USE_PARAMETER_STRUCT(FTAA2UpdateHistoryCS, FTAAGen5Shader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FTAA2CommonParameters, CommonParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputSceneStencilTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HistoryRejectionTexture)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DilatedVelocityTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ParallaxRejectionMaskTexture)

		SHADER_PARAMETER(FVector, HistoryQuantizationError)
		SHADER_PARAMETER(float, HistoryPreExposureCorrection)
		SHADER_PARAMETER(int32, bCameraCut)

		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, PrevHistoryInfo)
		SHADER_PARAMETER_STRUCT(FTAA2HistoryTextures, PrevHistory)

		SHADER_PARAMETER_STRUCT(FTAA2HistoryUAVs, HistoryOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, SceneColorOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()
}; // class FTAA2UpdateHistoryCS

IMPLEMENT_GLOBAL_SHADER(FTAAStandaloneCS, "/Engine/Private/TemporalAA/TAAStandalone.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTAA2DilateVelocityCS, "/Engine/Private/TemporalAA/TAADilateVelocity.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTAA2BuildParallaxMaskCS, "/Engine/Private/TemporalAA/TAABuildParallaxMask.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTAA2DecimateHistoryCS, "/Engine/Private/TemporalAA/TAADecimateHistory.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTAA2FilterFrequenciesCS, "/Engine/Private/TemporalAA/TAAFilterFrequencies.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTAA2CompareHistoryCS, "/Engine/Private/TemporalAA/TAACompareHistory.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTAA2DilateRejectionCS, "/Engine/Private/TemporalAA/TAADilateRejection.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTAA2UpdateHistoryCS, "/Engine/Private/TemporalAA/TAAUpdateHistory.usf", "MainCS", SF_Compute);

float CatmullRom(float x)
{
	float ax = FMath::Abs(x);
	if (ax > 1.0f)
		return ((-0.5f * ax + 2.5f) * ax - 4.0f) *ax + 2.0f;
	else
		return (1.5f * ax - 2.5f) * ax*ax + 1.0f;
}

FVector ComputePixelFormatQuantizationError(EPixelFormat PixelFormat)
{
	FVector Error;
	if (PixelFormat == PF_FloatRGBA || PixelFormat == PF_FloatR11G11B10)
	{
		FIntVector HistoryColorMantissaBits = PixelFormat == PF_FloatR11G11B10 ? FIntVector(6, 6, 5) : FIntVector(10, 10, 10);

		Error.X = FMath::Pow(0.5f, HistoryColorMantissaBits.X);
		Error.Y = FMath::Pow(0.5f, HistoryColorMantissaBits.Y);
		Error.Z = FMath::Pow(0.5f, HistoryColorMantissaBits.Z);
	}
	else
	{
		check(0);
	}

	return Error;
}

void SetupSampleWeightParameters(FTAAStandaloneCS::FParameters* OutTAAParameters, const FTAAPassParameters& PassParameters, FVector2D TemporalJitterPixels)
{
	float JitterX = TemporalJitterPixels.X;
	float JitterY = TemporalJitterPixels.Y;
	float ResDivisorInv = 1.0f / float(PassParameters.ResolutionDivisor);

	static const float SampleOffsets[9][2] =
	{
		{ -1.0f, -1.0f },
		{  0.0f, -1.0f },
		{  1.0f, -1.0f },
		{ -1.0f,  0.0f },
		{  0.0f,  0.0f },
		{  1.0f,  0.0f },
		{ -1.0f,  1.0f },
		{  0.0f,  1.0f },
		{  1.0f,  1.0f },
	};

	float FilterSize = CVarTemporalAAFilterSize.GetValueOnRenderThread();
	int32 bCatmullRom = CVarTemporalAACatmullRom.GetValueOnRenderThread();

	// Compute 3x3 weights
	{
		float TotalWeight = 0.0f;
		for (int32 i = 0; i < 9; i++)
		{
			float PixelOffsetX = SampleOffsets[i][0] - JitterX * ResDivisorInv;
			float PixelOffsetY = SampleOffsets[i][1] - JitterY * ResDivisorInv;

			PixelOffsetX /= FilterSize;
			PixelOffsetY /= FilterSize;

			if (bCatmullRom)
			{
				OutTAAParameters->SampleWeights[i] = CatmullRom(PixelOffsetX) * CatmullRom(PixelOffsetY);
				TotalWeight += OutTAAParameters->SampleWeights[i];
			}
			else
			{
				// Normal distribution, Sigma = 0.47
				OutTAAParameters->SampleWeights[i] = FMath::Exp(-2.29f * (PixelOffsetX * PixelOffsetX + PixelOffsetY * PixelOffsetY));
				TotalWeight += OutTAAParameters->SampleWeights[i];
			}
		}
	
		for (int32 i = 0; i < 9; i++)
			OutTAAParameters->SampleWeights[i] /= TotalWeight;
	}

	// Compute 3x3 + weights.
	{
		OutTAAParameters->PlusWeights[0] = OutTAAParameters->SampleWeights[1];
		OutTAAParameters->PlusWeights[1] = OutTAAParameters->SampleWeights[3];
		OutTAAParameters->PlusWeights[2] = OutTAAParameters->SampleWeights[4];
		OutTAAParameters->PlusWeights[3] = OutTAAParameters->SampleWeights[5];
		OutTAAParameters->PlusWeights[4] = OutTAAParameters->SampleWeights[7];
		float TotalWeightPlus = (
			OutTAAParameters->SampleWeights[1] +
			OutTAAParameters->SampleWeights[3] +
			OutTAAParameters->SampleWeights[4] +
			OutTAAParameters->SampleWeights[5] +
			OutTAAParameters->SampleWeights[7]);
	
		for (int32 i = 0; i < 5; i++)
			OutTAAParameters->PlusWeights[i] /= TotalWeightPlus;
	}
}

DECLARE_GPU_STAT(TAA)

const TCHAR* const kTAAOutputNames[] = {
	TEXT("TemporalAA"),
	TEXT("TemporalAA"),
	TEXT("TemporalAA"),
	TEXT("SSRTemporalAA"),
	TEXT("LightShaftTemporalAA"),
	TEXT("DOFTemporalAA"),
	TEXT("DOFTemporalAA"),
};

const TCHAR* const kTAAPassNames[] = {
	TEXT("Main"),
	TEXT("MainUpsampling"),
	TEXT("MainSuperSampling"),
	TEXT("ScreenSpaceReflections"),
	TEXT("LightShaft"),
	TEXT("DOF"),
	TEXT("DOFUpsampling"),
};

static_assert(UE_ARRAY_COUNT(kTAAOutputNames) == int32(ETAAPassConfig::MAX), "Missing TAA output name.");
static_assert(UE_ARRAY_COUNT(kTAAPassNames) == int32(ETAAPassConfig::MAX), "Missing TAA pass name.");
} //! namespace

bool IsTemporalAASceneDownsampleAllowed(const FViewInfo& View)
{
	return CVarTemporalAAAllowDownsampling.GetValueOnRenderThread() != 0;
}

float GetTemporalAAHistoryUpscaleFactor(const FViewInfo& View)
{
	float UpscaleFactor = 1.0f;

	// We only support history upscale in certain configurations.
	if (DoesPlatformSupportTemporalHistoryUpscale(View.GetShaderPlatform()))
	{
		UpscaleFactor = FMath::Clamp(CVarTemporalAAHistorySP.GetValueOnRenderThread() / 100.0f, 1.0f, 2.0f);
	}

	return UpscaleFactor;
}

FIntPoint FTAAPassParameters::GetOutputExtent() const
{
	check(Validate());
	check(SceneColorInput);

	FIntPoint InputExtent = SceneColorInput->Desc.Extent;

	if (!IsTAAUpsamplingConfig(Pass))
		return InputExtent;

	check(OutputViewRect.Min == FIntPoint::ZeroValue);
	FIntPoint PrimaryUpscaleViewSize = FIntPoint::DivideAndRoundUp(OutputViewRect.Size(), ResolutionDivisor);
	FIntPoint QuantizedPrimaryUpscaleViewSize;
	QuantizeSceneBufferSize(PrimaryUpscaleViewSize, QuantizedPrimaryUpscaleViewSize);

	return FIntPoint(
		FMath::Max(InputExtent.X, QuantizedPrimaryUpscaleViewSize.X),
		FMath::Max(InputExtent.Y, QuantizedPrimaryUpscaleViewSize.Y));
}

bool FTAAPassParameters::Validate() const
{
	if (IsTAAUpsamplingConfig(Pass))
	{
		check(OutputViewRect.Min == FIntPoint::ZeroValue);
	}
	else
	{
		check(InputViewRect == OutputViewRect);
	}
	return true;
}

FTAAOutputs AddTemporalAAPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FTAAPassParameters& Inputs,
	const FTemporalAAHistory& InputHistory,
	FTemporalAAHistory* OutputHistory)
{
	check(Inputs.Validate());

	// Number of render target in TAA history.
	const int32 IntputTextureCount = IsDOFTAAConfig(Inputs.Pass) && IsPostProcessingWithAlphaChannelSupported() ? 2 : 1;

	// Whether this is main TAA pass;
	const bool bIsMainPass = IsMainTAAConfig(Inputs.Pass);
	
	// Whether to use camera cut shader permutation or not.
	const bool bCameraCut = !InputHistory.IsValid() || View.bCameraCut;

	const FIntPoint OutputExtent = Inputs.GetOutputExtent();

	// Src rectangle.
	const FIntRect SrcRect = Inputs.InputViewRect;
	const FIntRect DestRect = Inputs.OutputViewRect;
	const FIntRect PracticableSrcRect = FIntRect::DivideAndRoundUp(SrcRect, Inputs.ResolutionDivisor);
	const FIntRect PracticableDestRect = FIntRect::DivideAndRoundUp(DestRect, Inputs.ResolutionDivisor);

	const uint32 PassIndex = static_cast<uint32>(Inputs.Pass);

	// Name of the pass.
	const TCHAR* PassName = kTAAPassNames[PassIndex];

	// Create outputs
	FTAAOutputs Outputs;

	TStaticArray<FRDGTextureRef, FTemporalAAHistory::kRenderTargetCount> NewHistoryTexture;

	{
		FRDGTextureDesc SceneColorDesc = FRDGTextureDesc::Create2DDesc(
			OutputExtent,
			PF_FloatRGBA,
			FClearValueBinding::Black,
			/* InFlags = */ TexCreate_None,
			/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_UAV,
			/* bInForceSeparateTargetAndShaderResource = */ false);

		if (Inputs.bOutputRenderTargetable)
		{
			SceneColorDesc.TargetableFlags |= TexCreate_RenderTargetable;
		}

		const TCHAR* OutputName = kTAAOutputNames[PassIndex];

		for (int32 i = 0; i < FTemporalAAHistory::kRenderTargetCount; i++)
		{
			NewHistoryTexture[i] = GraphBuilder.CreateTexture(
				SceneColorDesc,
				OutputName,
				ERDGResourceFlags::MultiFrame);
		}

		NewHistoryTexture[0] = Outputs.SceneColor = NewHistoryTexture[0];

		if (IntputTextureCount == 2)
		{
			Outputs.SceneMetadata = NewHistoryTexture[1];
		}

		if (Inputs.bDownsample)
		{
			const FRDGTextureDesc HalfResSceneColorDesc = FRDGTextureDesc::Create2DDesc(
				SceneColorDesc.Extent / 2,
				Inputs.DownsampleOverrideFormat != PF_Unknown ? Inputs.DownsampleOverrideFormat : Inputs.SceneColorInput->Desc.Format,
				FClearValueBinding::Black,
				/* InFlags = */ TexCreate_None,
				/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_Transient | TexCreate_UAV,
				/* bInForceSeparateTargetAndShaderResource = */ false);

			Outputs.DownsampledSceneColor = GraphBuilder.CreateTexture(HalfResSceneColorDesc, TEXT("SceneColorHalfRes"));
		}
	}

	RDG_GPU_STAT_SCOPE(GraphBuilder, TAA);

	TStaticArray<bool, FTemporalAAHistory::kRenderTargetCount> bUseHistoryTexture;

	{
		FTAAStandaloneCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTAAStandaloneCS::FTAAPassConfigDim>(Inputs.Pass);
		PermutationVector.Set<FTAAStandaloneCS::FTAAFastDim>(Inputs.bUseFast);
		PermutationVector.Set<FTAAStandaloneCS::FTAADownsampleDim>(Inputs.bDownsample);
		PermutationVector.Set<FTAAStandaloneCS::FTAAUpsampleFilteredDim>(true);

		if (IsTAAUpsamplingConfig(Inputs.Pass))
		{
			const bool bUpsampleFiltered = CVarTemporalAAUpsampleFiltered.GetValueOnRenderThread() != 0 || Inputs.Pass != ETAAPassConfig::MainUpsampling;
			PermutationVector.Set<FTAAStandaloneCS::FTAAUpsampleFilteredDim>(bUpsampleFiltered);

			// If screen percentage > 100% on X or Y axes, then use screen percentage range = 2 shader permutation to disable LDS caching.
			if (SrcRect.Width() > DestRect.Width() ||
				SrcRect.Height() > DestRect.Height())
			{
				PermutationVector.Set<FTAAStandaloneCS::FTAAScreenPercentageDim>(2);
			}
			// If screen percentage < 50% on X and Y axes, then use screen percentage range = 3 shader permutation.
			else if (SrcRect.Width() * 100 < 50 * DestRect.Width() &&
				SrcRect.Height() * 100 < 50 * DestRect.Height() &&
				Inputs.Pass == ETAAPassConfig::MainSuperSampling)
			{
				PermutationVector.Set<FTAAStandaloneCS::FTAAScreenPercentageDim>(3);
			}
			// If screen percentage < 71% on X and Y axes, then use screen percentage range = 1 shader permutation to have smaller LDS caching.
			else if (SrcRect.Width() * 100 < 71 * DestRect.Width() &&
				SrcRect.Height() * 100 < 71 * DestRect.Height())
			{
				PermutationVector.Set<FTAAStandaloneCS::FTAAScreenPercentageDim>(1);
			}
		}

		FTAAStandaloneCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTAAStandaloneCS::FParameters>();

		// Setups common shader parameters
		const FIntPoint InputExtent = Inputs.SceneColorInput->Desc.Extent;
		const FIntRect InputViewRect = Inputs.InputViewRect;
		const FIntRect OutputViewRect = Inputs.OutputViewRect;

		if (!IsTAAUpsamplingConfig(Inputs.Pass))
		{
			SetupSampleWeightParameters(PassParameters, Inputs, View.TemporalJitterPixels);
		}

		const float ResDivisor = Inputs.ResolutionDivisor;
		const float ResDivisorInv = 1.0f / ResDivisor;

		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->CurrentFrameWeight = CVarTemporalAACurrentFrameWeight.GetValueOnRenderThread();
		PassParameters->bCameraCut = bCameraCut;

		PassParameters->SceneDepthBuffer = Inputs.SceneDepthTexture;
		PassParameters->SceneVelocityBuffer = Inputs.SceneVelocityTexture;

		PassParameters->SceneDepthBufferSampler = TStaticSamplerState<SF_Point>::GetRHI();
		PassParameters->SceneVelocityBufferSampler = TStaticSamplerState<SF_Point>::GetRHI();

		PassParameters->StencilTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(Inputs.SceneDepthTexture, PF_X24_G8));

		// We need a valid velocity buffer texture. Use black (no velocity) if none exists.
		if (!PassParameters->SceneVelocityBuffer)
		{
			PassParameters->SceneVelocityBuffer = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);;
		}

		// Input buffer shader parameters
		{
			PassParameters->InputSceneColorSize = FVector4(
				InputExtent.X,
				InputExtent.Y,
				1.0f / float(InputExtent.X),
				1.0f / float(InputExtent.Y));
			PassParameters->InputMinPixelCoord = PracticableSrcRect.Min;
			PassParameters->InputMaxPixelCoord = PracticableSrcRect.Max - FIntPoint(1, 1);
			PassParameters->InputSceneColor = Inputs.SceneColorInput;
			PassParameters->InputSceneColorSampler = TStaticSamplerState<SF_Point>::GetRHI();
			PassParameters->InputSceneMetadata = Inputs.SceneMetadataInput;
			PassParameters->InputSceneMetadataSampler = TStaticSamplerState<SF_Point>::GetRHI();
		}

		PassParameters->OutputViewportSize = FVector4(
			PracticableDestRect.Width(), PracticableDestRect.Height(), 1.0f / float(PracticableDestRect.Width()), 1.0f / float(PracticableDestRect.Height()));
		PassParameters->OutputViewportRect = FVector4(PracticableDestRect.Min.X, PracticableDestRect.Min.Y, PracticableDestRect.Max.X, PracticableDestRect.Max.Y);

		// Set history shader parameters.
		{
			FRDGTextureRef BlackDummy = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);

			if (bCameraCut)
			{
				PassParameters->ScreenPosToHistoryBufferUV = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
				PassParameters->ScreenPosAbsMax = FVector2D(0.0f, 0.0f);
				PassParameters->HistoryBufferUVMinMax = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
				PassParameters->HistoryBufferSize = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

				for (int32 i = 0; i < FTemporalAAHistory::kRenderTargetCount; i++)
				{
					PassParameters->HistoryBuffer[i] = BlackDummy;
				}

				// Remove dependency of the velocity buffer on camera cut, given it's going to be ignored by the shader.
				PassParameters->SceneVelocityBuffer = BlackDummy;
			}
			else
			{
				FIntPoint ReferenceViewportOffset = InputHistory.ViewportRect.Min;
				FIntPoint ReferenceViewportExtent = InputHistory.ViewportRect.Size();
				FIntPoint ReferenceBufferSize = InputHistory.ReferenceBufferSize;

				float InvReferenceBufferSizeX = 1.f / float(InputHistory.ReferenceBufferSize.X);
				float InvReferenceBufferSizeY = 1.f / float(InputHistory.ReferenceBufferSize.Y);

				PassParameters->ScreenPosToHistoryBufferUV = FVector4(
					ReferenceViewportExtent.X * 0.5f * InvReferenceBufferSizeX,
					-ReferenceViewportExtent.Y * 0.5f * InvReferenceBufferSizeY,
					(ReferenceViewportExtent.X * 0.5f + ReferenceViewportOffset.X) * InvReferenceBufferSizeX,
					(ReferenceViewportExtent.Y * 0.5f + ReferenceViewportOffset.Y) * InvReferenceBufferSizeY);

				FIntPoint ViewportOffset = ReferenceViewportOffset / Inputs.ResolutionDivisor;
				FIntPoint ViewportExtent = FIntPoint::DivideAndRoundUp(ReferenceViewportExtent, Inputs.ResolutionDivisor);
				FIntPoint BufferSize = ReferenceBufferSize / Inputs.ResolutionDivisor;

				PassParameters->ScreenPosAbsMax = FVector2D(1.0f - 1.0f / float(ViewportExtent.X), 1.0f - 1.0f / float(ViewportExtent.Y));

				float InvBufferSizeX = 1.f / float(BufferSize.X);
				float InvBufferSizeY = 1.f / float(BufferSize.Y);

				PassParameters->HistoryBufferUVMinMax = FVector4(
					(ViewportOffset.X + 0.5f) * InvBufferSizeX,
					(ViewportOffset.Y + 0.5f) * InvBufferSizeY,
					(ViewportOffset.X + ViewportExtent.X - 0.5f) * InvBufferSizeX,
					(ViewportOffset.Y + ViewportExtent.Y - 0.5f) * InvBufferSizeY);

				PassParameters->HistoryBufferSize = FVector4(BufferSize.X, BufferSize.Y, InvBufferSizeX, InvBufferSizeY);

				for (int32 i = 0; i < FTemporalAAHistory::kRenderTargetCount; i++)
				{
					if (InputHistory.RT[i].IsValid())
					{
						PassParameters->HistoryBuffer[i] = GraphBuilder.RegisterExternalTexture(InputHistory.RT[i]);
					}
					else
					{
						PassParameters->HistoryBuffer[i] = BlackDummy;
					}
				}
			}

			for (int32 i = 0; i < FTemporalAAHistory::kRenderTargetCount; i++)
			{
				PassParameters->HistoryBufferSampler[i] = TStaticSamplerState<SF_Bilinear>::GetRHI();
			}
		}

		PassParameters->MaxViewportUVAndSvPositionToViewportUV = FVector4(
			(PracticableDestRect.Width() - 0.5f * ResDivisor) / float(PracticableDestRect.Width()),
			(PracticableDestRect.Height() - 0.5f * ResDivisor) / float(PracticableDestRect.Height()),
			ResDivisor / float(DestRect.Width()),
			ResDivisor / float(DestRect.Height()));

		PassParameters->HistoryPreExposureCorrection = View.PreExposure / View.PrevViewInfo.SceneColorPreExposure;

		{
			float InvSizeX = 1.0f / float(InputExtent.X);
			float InvSizeY = 1.0f / float(InputExtent.Y);
			PassParameters->ViewportUVToInputBufferUV = FVector4(
				ResDivisorInv * InputViewRect.Width() * InvSizeX,
				ResDivisorInv * InputViewRect.Height() * InvSizeY,
				ResDivisorInv * InputViewRect.Min.X * InvSizeX,
				ResDivisorInv * InputViewRect.Min.Y * InvSizeY);
		}

		PassParameters->EyeAdaptation = GetEyeAdaptationTexture(GraphBuilder, View);

		// Temporal upsample specific shader parameters.
		{
			// Temporal AA upscale specific params.
			float InputViewSizeInvScale = Inputs.ResolutionDivisor;
			float InputViewSizeScale = 1.0f / InputViewSizeInvScale;

			PassParameters->TemporalJitterPixels = InputViewSizeScale * View.TemporalJitterPixels;
			PassParameters->ScreenPercentage = float(InputViewRect.Width()) / float(OutputViewRect.Width());
			PassParameters->UpscaleFactor = float(OutputViewRect.Width()) / float(InputViewRect.Width());
			PassParameters->InputViewMin = InputViewSizeScale * FVector2D(InputViewRect.Min.X, InputViewRect.Min.Y);
			PassParameters->InputViewSize = FVector4(
				InputViewSizeScale * InputViewRect.Width(), InputViewSizeScale * InputViewRect.Height(),
				InputViewSizeInvScale / InputViewRect.Width(), InputViewSizeInvScale / InputViewRect.Height());
		}

		// UAVs
		{
			for (int32 i = 0; i < FTemporalAAHistory::kRenderTargetCount; i++)
			{
				PassParameters->OutComputeTex[i] = GraphBuilder.CreateUAV(NewHistoryTexture[i]);
			}

			if (Outputs.DownsampledSceneColor)
			{
				PassParameters->OutComputeTexDownsampled = GraphBuilder.CreateUAV(Outputs.DownsampledSceneColor);
			}
		}

		// Debug UAVs
		{
			FRDGTextureDesc DebugDesc = FRDGTextureDesc::Create2DDesc(
				OutputExtent,
				PF_FloatRGBA,
				FClearValueBinding::None,
				/* InFlags = */ TexCreate_None,
				/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_UAV,
				/* bInForceSeparateTargetAndShaderResource = */ false);

			FRDGTextureRef DebugTexture = GraphBuilder.CreateTexture(DebugDesc, TEXT("Debug.TAA"));
			PassParameters->DebugOutput = GraphBuilder.CreateUAV(DebugTexture);
		}

		TShaderMapRef<FTAAStandaloneCS> ComputeShader(View.ShaderMap, PermutationVector);

		ClearUnusedGraphResources(ComputeShader, PassParameters);
		for (int32 i = 0; i < FTemporalAAHistory::kRenderTargetCount; i++)
		{
			bUseHistoryTexture[i] = PassParameters->HistoryBuffer[i] != nullptr;
		}

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TAA %s%s %dx%d -> %dx%d",
				PassName, Inputs.bUseFast ? TEXT(" Fast") : TEXT(""),
				PracticableSrcRect.Width(), PracticableSrcRect.Height(),
				PracticableDestRect.Width(), PracticableDestRect.Height()),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(PracticableDestRect.Size(), GTemporalAATileSizeX));
	}
	
	if (!View.bStatePrevViewInfoIsReadOnly)
	{
		OutputHistory->SafeRelease();

		for (int32 i = 0; i < FTemporalAAHistory::kRenderTargetCount; i++)
		{
			if (bUseHistoryTexture[i])
			{
				GraphBuilder.QueueTextureExtraction(NewHistoryTexture[i], &OutputHistory->RT[i]);
			}
		}

		OutputHistory->ViewportRect = DestRect;
		OutputHistory->ReferenceBufferSize = OutputExtent * Inputs.ResolutionDivisor;
	}

	return Outputs;
} // AddTemporalAAPass()

static void AddGen5MainTemporalAAPasses(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const ITemporalUpscaler::FPassInputs& PassInputs,
	FRDGTextureRef* OutSceneColorTexture,
	FIntRect* OutSceneColorViewRect)
{
	const FTemporalAAHistory& InputHistory = View.PrevViewInfo.TemporalAAHistory;
	FTemporalAAHistory* OutputHistory = &View.ViewState->PrevFrameViewInfo.TemporalAAHistory;

	// Whether to use camera cut shader permutation or not.
	bool bCameraCut = !InputHistory.IsValid() || View.bCameraCut;

	FIntPoint InputExtent = PassInputs.SceneColorTexture->Desc.Extent;
	FIntRect InputRect = View.ViewRect;

	FIntPoint LowFrequencyExtent = InputExtent;
	FIntRect LowFrequencyRect = FIntRect(FIntPoint(0, 0), InputRect.Size());

	FIntPoint RejectionExtent = LowFrequencyExtent / 2;
	FIntRect RejectionRect = FIntRect(FIntPoint(0, 0), FIntPoint::DivideAndRoundUp(LowFrequencyRect.Size(), 2));

	FIntPoint OutputExtent;
	FIntRect OutputRect;
	{
		OutputRect.Min = FIntPoint(0, 0);
		OutputRect.Max = View.GetSecondaryViewRectSize();

		FIntPoint QuantizedPrimaryUpscaleViewSize;
		QuantizeSceneBufferSize(OutputRect.Max, QuantizedPrimaryUpscaleViewSize);

		OutputExtent = FIntPoint(
			FMath::Max(InputExtent.X, QuantizedPrimaryUpscaleViewSize.X),
			FMath::Max(InputExtent.Y, QuantizedPrimaryUpscaleViewSize.Y));
	}

	FIntPoint HistoryExtent;
	FIntPoint HistorySize;
	{
		int32 OutputHistoryResulitionMultiplier = CVarTAANyquistHistory.GetValueOnRenderThread() ? 2 : 1;

		HistoryExtent = OutputExtent * OutputHistoryResulitionMultiplier;
		HistorySize = OutputRect.Size() * OutputHistoryResulitionMultiplier;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "TAAU %dx%d -> %dx%d", InputRect.Width(), InputRect.Height(), OutputRect.Width(), OutputRect.Height());
	RDG_GPU_STAT_SCOPE(GraphBuilder, TAA);

	FRDGTextureRef BlackDummy = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);

	FTAA2CommonParameters CommonParameters;
	{
		CommonParameters.InputInfo = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(
			InputExtent, InputRect));

		CommonParameters.LowFrequencyInfo = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(
			LowFrequencyExtent, LowFrequencyRect));

		CommonParameters.RejectionInfo = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(
			RejectionExtent, RejectionRect));

		CommonParameters.OutputInfo = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(
			OutputExtent, OutputRect));

		CommonParameters.HistoryInfo = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(
			HistoryExtent, FIntRect(FIntPoint(0, 0), HistorySize)));

		CommonParameters.InputJitter = View.TemporalJitterPixels;
		CommonParameters.ViewUniformBuffer = View.ViewUniformBuffer;
	}

	auto CreateDebugUAV = [&](const FIntPoint& Extent, const TCHAR* DebugName)
	{
		FRDGTextureDesc DebugDesc = FRDGTextureDesc::Create2DDesc(
			Extent,
			PF_FloatRGBA,
			FClearValueBinding::None,
			/* InFlags = */ TexCreate_None,
			/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_UAV,
			/* bInForceSeparateTargetAndShaderResource = */ false);

		FRDGTextureRef DebugTexture = GraphBuilder.CreateTexture(DebugDesc, DebugName);

		return GraphBuilder.CreateUAV(DebugTexture);
	};

	// Dilate the velocity texture & build the parallax rejection mask
	FRDGTextureRef DilatedVelocityTexture;
	FRDGTextureRef ParallaxRejectionMaskTexture;
	{
		FRDGTextureRef ClosestDepthTexture;
		FRDGTextureRef PrevUseCountTexture;
		FRDGTextureRef PrevClosestDepthTexture;
		{
			{
				FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
					InputExtent,
					PF_G16R16,
					FClearValueBinding::None,
					/* InFlags = */ TexCreate_None,
					/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_UAV,
					/* bInForceSeparateTargetAndShaderResource = */ false);

				DilatedVelocityTexture = GraphBuilder.CreateTexture(Desc, TEXT("TAA.DilatedVelocity"));

				Desc.Format = PF_R16F;
				ClosestDepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("TAA.ClosestDepthTexture"));

				Desc.Format = PF_R32_UINT;
				PrevUseCountTexture = GraphBuilder.CreateTexture(Desc, TEXT("TAA.PrevUseCountTexture"));
				PrevClosestDepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("TAA.PrevClosestDepthTexture"));
			}

			FTAA2DilateVelocityCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTAA2DilateVelocityCS::FParameters>();
			PassParameters->CommonParameters = CommonParameters;
			PassParameters->SceneDepthTexture = PassInputs.SceneDepthTexture;
			PassParameters->SceneVelocityTexture = PassInputs.SceneVelocityTexture;
			PassParameters->DilatedVelocityOutput = GraphBuilder.CreateUAV(DilatedVelocityTexture);
			PassParameters->ClosestDepthOutput = GraphBuilder.CreateUAV(ClosestDepthTexture);
			PassParameters->PrevUseCountOutput = GraphBuilder.CreateUAV(PrevUseCountTexture);
			PassParameters->PrevClosestDepthOutput = GraphBuilder.CreateUAV(PrevClosestDepthTexture);
			PassParameters->DebugOutput = CreateDebugUAV(InputExtent, TEXT("Debug.TAA.DilateVelocity"));

			uint32 ClearValues[4] = { 0, 0, 0, 0 };
			AddClearUAVPass(GraphBuilder, PassParameters->PrevUseCountOutput, ClearValues);
			AddClearUAVPass(GraphBuilder, PassParameters->PrevClosestDepthOutput, ClearValues);

			TShaderMapRef<FTAA2DilateVelocityCS> ComputeShader(View.ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TAA DilateVelocity %dx%d", InputRect.Width(), InputRect.Height()),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(InputRect.Size(), 8));
		}

		{
			{
				FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
					InputExtent,
					PF_R8,
					FClearValueBinding::None,
					/* InFlags = */ TexCreate_None,
					/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_UAV,
					/* bInForceSeparateTargetAndShaderResource = */ false);

				ParallaxRejectionMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("TAA.ParallaxRejectionMask"));
			}

			FTAA2BuildParallaxMaskCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTAA2BuildParallaxMaskCS::FParameters>();
			PassParameters->CommonParameters = CommonParameters;
			{
				float TanHalfFieldOfView = View.ViewMatrices.GetInvProjectionMatrix().M[0][0];

				// Should be multiplied 0.5* for the diameter to radius, and by 2.0 because GetTanHalfFieldOfView() cover only half of the pixels.
				PassParameters->WorldDepthToPixelWorldRadius = TanHalfFieldOfView / float(View.ViewRect.Width());
			}
			PassParameters->DilatedVelocityTexture = DilatedVelocityTexture;
			PassParameters->ClosestDepthTexture = ClosestDepthTexture;
			PassParameters->PrevUseCountTexture = PrevUseCountTexture;
			PassParameters->PrevClosestDepthTexture = PrevClosestDepthTexture;
			PassParameters->ParallaxRejectionMaskOutput = GraphBuilder.CreateUAV(ParallaxRejectionMaskTexture);
			PassParameters->DebugOutput = CreateDebugUAV(InputExtent, TEXT("Debug.TAA.BuildParallaxMask"));

			TShaderMapRef<FTAA2BuildParallaxMaskCS> ComputeShader(View.ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TAA BuildParallaxMask %dx%d", InputRect.Width(), InputRect.Height()),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(InputRect.Size(), 8));
		}
	}

	// Setup the previous frame history
	FScreenPassTextureViewportParameters PrevHistoryInfo;
	FTAA2HistoryTextures PrevHistory;
	if (bCameraCut)
	{
		PrevHistoryInfo = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(
			FIntPoint(1, 1), FIntRect(FIntPoint(0, 0), FIntPoint(1, 1))));

		for (int32 i = 0; i < kHistoryTextures; i++)
		{
			PrevHistory.Textures[i] = BlackDummy;
		}
	}
	else
	{
		bool bIsNyquistInputHistory = InputHistory.RT[0]->GetDesc().Extent.X > InputHistory.ReferenceBufferSize.X;

		int32 ResolutionMultiplier = bIsNyquistInputHistory ? 2 : 1;

		PrevHistoryInfo = GetScreenPassTextureViewportParameters(FScreenPassTextureViewport(
			InputHistory.ReferenceBufferSize * ResolutionMultiplier,
			FIntRect(FIntPoint(0, 0), InputHistory.ViewportRect.Size() * ResolutionMultiplier)));

		for (int32 i = 0; i < kHistoryTextures; i++)
		{
			if (InputHistory.RT[i].IsValid())
			{
				PrevHistory.Textures[i] = GraphBuilder.RegisterExternalTexture(InputHistory.RT[i]);
			}
			else
			{
				PrevHistory.Textures[i] = BlackDummy;
			}
		}

		// InputHistory.SafeRelease(); TODO
	}

	// Allocate a new history
	bool bR11G11B10History = CVarTAAR11G11B10History.GetValueOnRenderThread() != 0;
	FTAA2HistoryTextures History;
	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
			HistoryExtent,
			bR11G11B10History ? PF_FloatR11G11B10 : PF_FloatRGBA,
			FClearValueBinding::None,
			/* InFlags = */ TexCreate_None,
			/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_UAV,
			/* bInForceSeparateTargetAndShaderResource = */ false);

		History.Textures[0] = GraphBuilder.CreateTexture(Desc, TEXT("TAA.History.LowFrequencies"));
		History.Textures[1] = GraphBuilder.CreateTexture(Desc, TEXT("TAA.History.HighFrequencies"));

		Desc.Format = PF_R8G8;
		History.Textures[2] = GraphBuilder.CreateTexture(Desc, TEXT("TAA.History.Metadata"));
	}

	// Decimate input to flicker at same frequency as input.
	FRDGTextureRef PredictionSceneColorTexture;
	FRDGTextureRef PredictionInfoTexture;
	{
		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
				LowFrequencyExtent,
				PF_FloatR11G11B10,
				FClearValueBinding::None,
				/* InFlags = */ TexCreate_None,
				/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_UAV,
				/* bInForceSeparateTargetAndShaderResource = */ false);

			PredictionSceneColorTexture = GraphBuilder.CreateTexture(Desc, TEXT("TAA.Decimated.SceneColor"));

			Desc.Format = PF_R8;
			PredictionInfoTexture = GraphBuilder.CreateTexture(Desc, TEXT("TAA.Decimated.Completeness"));
		}

		FTAA2DecimateHistoryCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTAA2DecimateHistoryCS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;
		PassParameters->OutputQuantizationError = ComputePixelFormatQuantizationError(PredictionSceneColorTexture->Desc.Format);
		PassParameters->HistoryPreExposureCorrection = View.PreExposure / View.PrevViewInfo.SceneColorPreExposure;
		PassParameters->bCameraCut = bCameraCut;

		PassParameters->DilatedVelocityTexture = DilatedVelocityTexture;
		PassParameters->ParallaxRejectionMaskTexture = ParallaxRejectionMaskTexture;\

		PassParameters->PrevHistoryInfo = PrevHistoryInfo;
		PassParameters->PrevHistory = PrevHistory;

		PassParameters->PredictionSceneColorOutput = GraphBuilder.CreateUAV(PredictionSceneColorTexture);
		PassParameters->PredictionInfoOutput = GraphBuilder.CreateUAV(PredictionInfoTexture);
		PassParameters->DebugOutput = CreateDebugUAV(LowFrequencyExtent, TEXT("Debug.TAA.DecimateHistory"));

		TShaderMapRef<FTAA2DecimateHistoryCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TAA DecimateHistory %dx%d", InputRect.Width(), InputRect.Height()),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(InputRect.Size(), 8));
	}

	// Reject the history with frequency decomposition.
	FRDGTextureRef HistoryRejectionTexture;
	{
		// Filter out the high frquencies
		FRDGTextureRef FilteredInputTexture;
		FRDGTextureRef FilteredPredictionSceneColorTexture;
		{
			{
				FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
					LowFrequencyExtent,
					PF_FloatR11G11B10,
					FClearValueBinding::None,
					/* InFlags = */ TexCreate_None,
					/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_UAV,
					/* bInForceSeparateTargetAndShaderResource = */ false);

				FilteredInputTexture = GraphBuilder.CreateTexture(Desc, TEXT("TAA.Filtered.SceneColor"));
				FilteredPredictionSceneColorTexture = GraphBuilder.CreateTexture(Desc, TEXT("TAA.Filtered.Prediction.SceneColor"));
			}

			FTAA2FilterFrequenciesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTAA2FilterFrequenciesCS::FParameters>();
			PassParameters->CommonParameters = CommonParameters;
			PassParameters->OutputQuantizationError = ComputePixelFormatQuantizationError(FilteredInputTexture->Desc.Format);

			PassParameters->InputTexture = PassInputs.SceneColorTexture;
			PassParameters->PredictionSceneColorTexture = PredictionSceneColorTexture;
			PassParameters->PredictionInfoTexture = PredictionInfoTexture;

			PassParameters->FilteredInputOutput = GraphBuilder.CreateUAV(FilteredInputTexture);
			PassParameters->FilteredPredictionSceneColorOutput = GraphBuilder.CreateUAV(FilteredPredictionSceneColorTexture);
			PassParameters->DebugOutput = CreateDebugUAV(LowFrequencyExtent, TEXT("Debug.TAA.FilterFrequencies"));

			TShaderMapRef<FTAA2FilterFrequenciesCS> ComputeShader(View.ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TAA FilterFrequencies %dx%d", LowFrequencyRect.Width(), LowFrequencyRect.Height()),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(LowFrequencyRect.Size(), 8));
		}

		// Compare the low frequencies
		{
			{
				FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
					RejectionExtent,
					PF_R8,
					FClearValueBinding::None,
					/* InFlags = */ TexCreate_None,
					/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_UAV,
					/* bInForceSeparateTargetAndShaderResource = */ false);

				HistoryRejectionTexture = GraphBuilder.CreateTexture(Desc, TEXT("TAA.HistoryRejection"));
			}

			FTAA2CompareHistoryCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTAA2CompareHistoryCS::FParameters>();
			PassParameters->CommonParameters = CommonParameters;
			PassParameters->ParallaxRejectionMaskTexture = ParallaxRejectionMaskTexture;
			PassParameters->FilteredInputTexture = FilteredInputTexture;
			PassParameters->FilteredPredictionSceneColorTexture = FilteredPredictionSceneColorTexture;

			PassParameters->HistoryRejectionOutput = GraphBuilder.CreateUAV(HistoryRejectionTexture);
			PassParameters->DebugOutput = CreateDebugUAV(LowFrequencyExtent, TEXT("Debug.TAA.CompareHistory"));

			TShaderMapRef<FTAA2CompareHistoryCS> ComputeShader(View.ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TAA CompareHistory %dx%d", LowFrequencyRect.Width(), LowFrequencyRect.Height()),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(LowFrequencyRect.Size(), 8));
		}
	}

	// Dilate the rejection.
	FRDGTextureRef DilatedHistoryRejectionTexture;
	{
		DilatedHistoryRejectionTexture = GraphBuilder.CreateTexture(HistoryRejectionTexture->Desc, TEXT("TAA.DilatedHistoryRejection"));

		FTAA2DilateRejectionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTAA2DilateRejectionCS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;
		PassParameters->HistoryRejectionTexture = HistoryRejectionTexture;
		PassParameters->DilatedHistoryRejectionOutput = GraphBuilder.CreateUAV(DilatedHistoryRejectionTexture);
		PassParameters->DebugOutput = CreateDebugUAV(RejectionExtent, TEXT("Debug.TAA.DilateRejection"));

		TShaderMapRef<FTAA2DilateRejectionCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TAA DilateRejection %dx%d", RejectionRect.Width(), RejectionRect.Height()),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(RejectionRect.Size(), 8));
	}

	TStaticArray<bool, kHistoryTextures> ExtractHistory;
	FRDGTextureRef SceneColorOutputTexture;
	{
		// Allocate output
		{
			FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
				OutputExtent,
				PF_FloatR11G11B10,
				FClearValueBinding::None,
				/* InFlags = */ TexCreate_None,
				/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_UAV,
				/* bInForceSeparateTargetAndShaderResource = */ false);

			SceneColorOutputTexture = GraphBuilder.CreateTexture(Desc, TEXT("TAA.Output"));
		}

		FTAA2UpdateHistoryCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTAA2UpdateHistoryCS::FParameters>();
		PassParameters->CommonParameters = CommonParameters;
		PassParameters->InputSceneColorTexture = PassInputs.SceneColorTexture;
		PassParameters->InputSceneStencilTexture = GraphBuilder.CreateSRV(
			FRDGTextureSRVDesc::CreateWithPixelFormat(PassInputs.SceneDepthTexture, PF_X24_G8));
		PassParameters->HistoryRejectionTexture = DilatedHistoryRejectionTexture;
		PassParameters->DilatedVelocityTexture = DilatedVelocityTexture;
		PassParameters->ParallaxRejectionMaskTexture = ParallaxRejectionMaskTexture;

		PassParameters->HistoryQuantizationError = ComputePixelFormatQuantizationError(History.Textures[0]->Desc.Format);
		PassParameters->HistoryPreExposureCorrection = View.PreExposure / View.PrevViewInfo.SceneColorPreExposure;
		PassParameters->bCameraCut = bCameraCut;

		PassParameters->PrevHistoryInfo = PrevHistoryInfo;
		PassParameters->PrevHistory = PrevHistory;

		PassParameters->HistoryOutput = CreateUAVs(GraphBuilder, History);
		PassParameters->SceneColorOutput = GraphBuilder.CreateUAV(SceneColorOutputTexture);
		PassParameters->DebugOutput = CreateDebugUAV(HistoryExtent, TEXT("Debug.TAA.UpdateHistory"));

		TShaderMapRef<FTAA2UpdateHistoryCS> ComputeShader(View.ShaderMap);
		ClearUnusedGraphResources(ComputeShader, PassParameters);

		for (int32 i = 0; i < kHistoryTextures; i++)
		{
			bool bNeedsExtractForNextFrame = PassParameters->PrevHistory.Textures[i] != nullptr;
			bool bPrevFrameIsntAvailable = PassParameters->PrevHistory.Textures[i] == BlackDummy;
			bool bOutputHistory = PassParameters->HistoryOutput.Textures[i] != nullptr;

			ExtractHistory[i] = bNeedsExtractForNextFrame;

			if (bPrevFrameIsntAvailable && !PassParameters->bCameraCut)
			{
				//ensureMsgf(false, TEXT("Shaders read PrevHistory[%d] but doesn't write HistoryOutput[%d]"), i, i);
				PassParameters->bCameraCut = true;
			}

			if (bOutputHistory && !bNeedsExtractForNextFrame)
			{
				ensureMsgf(false, TEXT("Shaders write HistoryOutput[%d] but doesn't read PrevHistory[%d]"), i, i);
			}
		}

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TAA UpdateHistory %dx%d", HistorySize.X, HistorySize.Y),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(HistorySize, 8));
	}

	if (!View.bStatePrevViewInfoIsReadOnly)
	{
		OutputHistory->SafeRelease();

		for (int32 i = 0; i < kHistoryTextures; i++)
		{
			if (ExtractHistory[i])
			{
				GraphBuilder.QueueTextureExtraction(History.Textures[i], &OutputHistory->RT[i]);
			}
		}

		OutputHistory->ViewportRect = OutputRect;
		OutputHistory->ReferenceBufferSize = OutputExtent;
	}

	*OutSceneColorTexture = SceneColorOutputTexture;
	*OutSceneColorViewRect = OutputRect;
} // AddGen5MainTemporalAAPasses()

static void AddGen4MainTemporalAAPasses(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const ITemporalUpscaler::FPassInputs& PassInputs,
	FRDGTextureRef* OutSceneColorTexture,
	FIntRect* OutSceneColorViewRect,
	FRDGTextureRef* OutSceneColorHalfResTexture,
	FIntRect* OutSceneColorHalfResViewRect)
{
	check(View.AntiAliasingMethod == AAM_TemporalAA && View.ViewState);

	FTAAPassParameters TAAParameters(View);

	TAAParameters.Pass = View.PrimaryScreenPercentageMethod == EPrimaryScreenPercentageMethod::TemporalUpscale
		? ETAAPassConfig::MainUpsampling
		: ETAAPassConfig::Main;

	TAAParameters.SetupViewRect(View);

	const EPostProcessAAQuality LowQualityTemporalAA = EPostProcessAAQuality::Medium;

	TAAParameters.bUseFast = GetPostProcessAAQuality() == LowQualityTemporalAA;

	const FIntRect SecondaryViewRect = TAAParameters.OutputViewRect;

	const float HistoryUpscaleFactor = GetTemporalAAHistoryUpscaleFactor(View);

	// Configures TAA to upscale the history buffer; this is in addition to the secondary screen percentage upscale.
	// We end up with a scene color that is larger than the secondary screen percentage. We immediately downscale
	// afterwards using a Mitchel-Netravali filter.
	if (HistoryUpscaleFactor > 1.0f)
	{
		const FIntPoint HistoryViewSize(
			TAAParameters.OutputViewRect.Width() * HistoryUpscaleFactor,
			TAAParameters.OutputViewRect.Height() * HistoryUpscaleFactor);

		TAAParameters.Pass = ETAAPassConfig::MainSuperSampling;
		TAAParameters.bUseFast = false;

		TAAParameters.OutputViewRect.Min.X = 0;
		TAAParameters.OutputViewRect.Min.Y = 0;
		TAAParameters.OutputViewRect.Max = HistoryViewSize;
	}

	TAAParameters.DownsampleOverrideFormat = PassInputs.DownsampleOverrideFormat;

	TAAParameters.bDownsample = PassInputs.bAllowDownsampleSceneColor && TAAParameters.bUseFast;

	TAAParameters.SceneDepthTexture = PassInputs.SceneDepthTexture;
	TAAParameters.SceneVelocityTexture = PassInputs.SceneVelocityTexture;
	TAAParameters.SceneColorInput = PassInputs.SceneColorTexture;

	const FTemporalAAHistory& InputHistory = View.PrevViewInfo.TemporalAAHistory;

	FTemporalAAHistory& OutputHistory = View.ViewState->PrevFrameViewInfo.TemporalAAHistory;


	const FTAAOutputs TAAOutputs = ::AddTemporalAAPass(
		GraphBuilder,
		View,
		TAAParameters,
		InputHistory,
		&OutputHistory);

	FRDGTextureRef SceneColorTexture = TAAOutputs.SceneColor;

	// If we upscaled the history buffer, downsize back to the secondary screen percentage size.
	if (HistoryUpscaleFactor > 1.0f)
	{
		const FIntRect InputViewport = TAAParameters.OutputViewRect;

		FIntPoint QuantizedOutputSize;
		QuantizeSceneBufferSize(SecondaryViewRect.Size(), QuantizedOutputSize);

		FScreenPassTextureViewport OutputViewport;
		OutputViewport.Rect = SecondaryViewRect;
		OutputViewport.Extent.X = FMath::Max(PassInputs.SceneColorTexture->Desc.Extent.X, QuantizedOutputSize.X);
		OutputViewport.Extent.Y = FMath::Max(PassInputs.SceneColorTexture->Desc.Extent.Y, QuantizedOutputSize.Y);

		SceneColorTexture = ComputeMitchellNetravaliDownsample(GraphBuilder, View, FScreenPassTexture(SceneColorTexture, InputViewport), OutputViewport);
	}

	*OutSceneColorTexture = SceneColorTexture;
	*OutSceneColorViewRect = SecondaryViewRect;
	*OutSceneColorHalfResTexture = TAAOutputs.DownsampledSceneColor;
	*OutSceneColorHalfResViewRect = FIntRect::DivideAndRoundUp(SecondaryViewRect, 2);
} // AddGen4MainTemporalAAPasses()

const ITemporalUpscaler* GTemporalUpscaler = nullptr;

class FDefaultTemporalUpscaler : public ITemporalUpscaler
{ 
public:

	virtual const TCHAR* GetDebugName() const
	{
		return TEXT("FDefaultTemporalUpscaler");
	}

	virtual void AddPasses(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const FPassInputs& PassInputs,
		FRDGTextureRef* OutSceneColorTexture,
		FIntRect* OutSceneColorViewRect,
		FRDGTextureRef* OutSceneColorHalfResTexture,
		FIntRect* OutSceneColorHalfResViewRect) const final
	{
		if (CVarTAAAlgorithm.GetValueOnRenderThread())
		{
			*OutSceneColorHalfResTexture = nullptr;
			//*OutSceneColorHalfResViewRect; // TODO.

			return AddGen5MainTemporalAAPasses(
				GraphBuilder,
				View,
				PassInputs,
				OutSceneColorTexture,
				OutSceneColorViewRect);
		}
		else
		{
			return AddGen4MainTemporalAAPasses(
				GraphBuilder,
				View,
				PassInputs,
				OutSceneColorTexture,
				OutSceneColorViewRect,
				OutSceneColorHalfResTexture,
				OutSceneColorHalfResViewRect);
		}
	}
};

// static
const ITemporalUpscaler* ITemporalUpscaler::GetDefaultTemporalUpscaler()
{
	static FDefaultTemporalUpscaler DefaultTemporalUpscaler;
	return &DefaultTemporalUpscaler;
}

int ITemporalUpscaler::GetTemporalUpscalerMode()
{
	return CVarUseTemporalAAUpscaler.GetValueOnRenderThread();
}
