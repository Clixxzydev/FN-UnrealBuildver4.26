// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkyAtmosphereRendering.cpp
=============================================================================*/

#include "SkyAtmosphereRendering.h"
#include "CanvasTypes.h"
#include "Components/SkyAtmosphereComponent.h"
#include "DeferredShadingRenderer.h"
#include "LightSceneInfo.h"
#include "PixelShaderUtils.h"
#include "RenderTargetTemp.h"
#include "Rendering/SkyAtmosphereCommonData.h"
#include "ScenePrivate.h"
#include "SceneRenderTargetParameters.h"
#include "VolumeLighting.h"
#include "VolumetricCloudRendering.h"


//PRAGMA_DISABLE_OPTIMIZATION


// The runtime ON/OFF toggle
static TAutoConsoleVariable<int32> CVarSkyAtmosphere(
	TEXT("r.SkyAtmosphere"), 1,
	TEXT("SkyAtmosphere components are rendered when this is not 0, otherwise ignored.\n"),
	ECVF_RenderThreadSafe);

// The project setting (disable runtime and shader code)
static TAutoConsoleVariable<int32> CVarSupportSkyAtmosphere(
	TEXT("r.SupportSkyAtmosphere"),
	1,
	TEXT("Enables SkyAtmosphere rendering and shader code."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

// The project setting for the sky atmosphere component to affect the height fog (disable runtime and shader code)
static TAutoConsoleVariable<int32> CVarSupportSkyAtmosphereAffectsHeightFog(
	TEXT("r.SupportSkyAtmosphereAffectsHeightFog"),
	0,
	TEXT("Enables SkyAtmosphere affecting height fog. It requires r.SupportSkyAtmosphere to be true."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

////////////////////////////////////////////////////////////////////////// Regular sky 

static TAutoConsoleVariable<float> CVarSkyAtmosphereSampleCountMin(
	TEXT("r.SkyAtmosphere.SampleCountMin"), 2.0f,
	TEXT("The minimum sample count used to compute sky/atmosphere scattering and transmittance.\n")
	TEXT("The minimal value will be clamped to 1.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereSampleCountMax(
	TEXT("r.SkyAtmosphere.SampleCountMax"), 32.0f,
	TEXT("The maximum sample count used to compute sky/atmosphere scattering and transmittance The effective sample count is usually lower and depends on distance and SampleCountScale on the component, as well as .ini files.\n")
	TEXT("The minimal value will be clamped to r.SkyAtmosphere.SampleCountMin + 1.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereDistanceToSampleCountMax(
	TEXT("r.SkyAtmosphere.DistanceToSampleCountMax"), 150.0f,
	TEXT("The distance in kilometer after which SampleCountMax samples will be used to ray march the atmosphere."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarSkyAtmosphereSampleLightShadowmap(
	TEXT("r.SkyAtmosphere.SampleLightShadowmap"), 1,
	TEXT("Enable the sampling of atmospheric lights shadow map in order to produce volumetric shadows."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Fast sky

static TAutoConsoleVariable<int32> CVarSkyAtmosphereFastSkyLUT(
	TEXT("r.SkyAtmosphere.FastSkyLUT"), 0,
	TEXT("When enabled, a look up texture is used to render the sky.\n")
	TEXT("It is faster but can result in visual artefacts if there are some high frequency details\n")
	TEXT("in the sky such as earth shadow or scattering lob."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereFastSkyLUTSampleCountMin(
	TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMin"), 4.0f,
	TEXT("Fast sky minimum sample count used to compute sky/atmosphere scattering and transmittance.\n")
	TEXT("The minimal value will be clamped to 1.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereFastSkyLUTSampleCountMax(
	TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMax"), 32.0f,
	TEXT("Fast sky maximum sample count used to compute sky/atmosphere scattering and transmittance.\n")
	TEXT("The maximum sample count used to compute FastSkyLUT scattering. The effective sample count is usually lower and depends on distance and SampleCountScale on the component, as well as .ini files.\n")
	TEXT("The minimal value will be clamped to r.SkyAtmosphere.FastSkyLUT.SampleCountMin + 1.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereFastSkyLUTDistanceToSampleCountMax(
	TEXT("r.SkyAtmosphere.FastSkyLUT.DistanceToSampleCountMax"), 150.0f,
	TEXT("Fast sky distance in kilometer after which at which SampleCountMax samples will be used to ray march the atmosphere."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereFastSkyLUTWidth(
	TEXT("r.SkyAtmosphere.FastSkyLUT.Width"), 192,
	TEXT(""),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereFastSkyLUTHeight(
	TEXT("r.SkyAtmosphere.FastSkyLUT.Height"), 104,
	TEXT(""),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Aerial perspective

static TAutoConsoleVariable<int32> CVarSkyAtmosphereAerialPerspectiveDepthTest(
	TEXT("r.SkyAtmosphere.AerialPerspective.DepthTest"), 1,
	TEXT("When enabled, a depth test will be used to not write pixel closer to the camera than StartDepth, effectively improving performance."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Aerial perspective LUT

static TAutoConsoleVariable<float> CVarSkyAtmosphereAerialPerspectiveLUTDepthResolution(
	TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.DepthResolution"), 16.0f,
	TEXT("The number of depth slice to use for the aerial perspective volume texture."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereAerialPerspectiveLUTDepth(
	TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.Depth"), 96.0f,
	TEXT("The length of the LUT in kilometers (default = 96km to get nice cloud/atmosphere interactions in the distance for default sky). Further than this distance, the last slice is used."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereAerialPerspectiveLUTSampleCountMaxPerSlice(
	TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.SampleCountMaxPerSlice"), 2.0f,
	TEXT("The sample count used per slice to evaluate aerial perspective. The effective sample count is usually lower and depends on SampleCountScale on the component as well as .ini files.\n")
	TEXT("scattering and transmittance in camera frustum space froxel.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereAerialPerspectiveLUTWidth(
	TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.Width"), 32,
	TEXT(""),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarSkyAtmosphereAerialPerspectiveApplyOnOpaque(
	TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.FastApplyOnOpaque"), 0,
	TEXT("When enabled, the low resolution camera frustum/froxel volume containing atmospheric fog\n")
	TEXT(", usually used for fog on translucent surface, is used to render fog on opaque.\n")
	TEXT("It is faster but can result in visual artefacts if there are some high frequency details\n")
	TEXT("such as earth shadow or scattering lob."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Transmittance LUT

static TAutoConsoleVariable<float> CVarSkyAtmosphereTransmittanceLUTSampleCount(
	TEXT("r.SkyAtmosphere.TransmittanceLUT.SampleCount"), 10.0f,
	TEXT("The sample count used to evaluate transmittance."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarSkyAtmosphereTransmittanceLUTUseSmallFormat(
	TEXT("r.SkyAtmosphere.TransmittanceLUT.UseSmallFormat"), 0,
	TEXT("If true, the transmittance LUT will use a small R8BG8B8A8 format to store data at lower quality."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereTransmittanceLUTWidth(
	TEXT("r.SkyAtmosphere.TransmittanceLUT.Width"), 256,
	TEXT(""),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereTransmittanceLUTHeight(
	TEXT("r.SkyAtmosphere.TransmittanceLUT.Height"), 64,
	TEXT(""),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Multi-scattering LUT

static TAutoConsoleVariable<float> CVarSkyAtmosphereMultiScatteringLUTSampleCount(
	TEXT("r.SkyAtmosphere.MultiScatteringLUT.SampleCount"), 15.0f,
	TEXT("The sample count used to evaluate multi-scattering.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereMultiScatteringLUTHighQuality(
	TEXT("r.SkyAtmosphere.MultiScatteringLUT.HighQuality"), 0.0f,
	TEXT("The when enabled, 64 samples are used instead of 2, resulting in a more accurate multi scattering approximation (but also more expenssive).\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereMultiScatteringLUTWidth(
	TEXT("r.SkyAtmosphere.MultiScatteringLUT.Width"), 32,
	TEXT(""),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereMultiScatteringLUTHeight(
	TEXT("r.SkyAtmosphere.MultiScatteringLUT.Height"), 32,
	TEXT(""),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Distant Sky Light LUT

static TAutoConsoleVariable<int32> CVarSkyAtmosphereDistantSkyLightLUT(
	TEXT("r.SkyAtmosphere.DistantSkyLightLUT"), 1,
	TEXT("Enable the generation the sky ambient lighting value.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereDistantSkyLightLUTAltitude(
	TEXT("r.SkyAtmosphere.DistantSkyLightLUT.Altitude"), 6.0f,
	TEXT("The altitude at which the sky samples are taken to integrate the sky lighting. Default to 6km, typicaly cirrus clouds altitude.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Debug / Visualization

static TAutoConsoleVariable<int32> CVarSkyAtmosphereLUT32(
	TEXT("r.SkyAtmosphere.LUT32"), 0,
	TEXT("Use full 32bit per-channel precision for all sky LUTs.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

DECLARE_GPU_STAT(SkyAtmosphereLUTs);
DECLARE_GPU_STAT(SkyAtmosphere);
DECLARE_GPU_STAT(SkyAtmosphereEditor);
DECLARE_GPU_STAT(SkyAtmosphereDebugVisualize);

// Extra internal constants shared between all passes. It is used to render the sky itself (not shared with material)
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSkyAtmosphereInternalCommonParameters, )
	SHADER_PARAMETER(float, SampleCountMin)
	SHADER_PARAMETER(float, SampleCountMax)
	SHADER_PARAMETER(float, DistanceToSampleCountMaxInv)

	SHADER_PARAMETER(float, FastSkySampleCountMin)
	SHADER_PARAMETER(float, FastSkySampleCountMax)
	SHADER_PARAMETER(float, FastSkyDistanceToSampleCountMaxInv)

	SHADER_PARAMETER(FVector4, CameraAerialPerspectiveVolumeSizeAndInvSize)
	SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthResolution)		// Also on View UB
	SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthResolutionInv)	// Also on View UB
	SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthSliceLengthKm)	// Also on View UB
	SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthSliceLengthKmInv)	// Also on View UB
	SHADER_PARAMETER(float, CameraAerialPerspectiveSampleCountPerSlice)

	SHADER_PARAMETER(FVector4, TransmittanceLutSizeAndInvSize)
	SHADER_PARAMETER(FVector4, MultiScatteredLuminanceLutSizeAndInvSize)
	SHADER_PARAMETER(FVector4, SkyViewLutSizeAndInvSize)						// Also on View UB

	SHADER_PARAMETER(float, TransmittanceSampleCount)
	SHADER_PARAMETER(float, MultiScatteringSampleCount)
	SHADER_PARAMETER(float, AerialPespectiveViewDistanceScale)
	SHADER_PARAMETER(float, FogShowFlagFactor)

	SHADER_PARAMETER(FVector, SkyLuminanceFactor)

END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FAtmosphereUniformShaderParameters, "Atmosphere");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FSkyAtmosphereInternalCommonParameters, "SkyAtmosphere");


#define GET_VALID_DATA_FROM_CVAR \
	auto ValidateLUTResolution = [](int32 Value) \
	{ \
		return Value < 4 ? 4 : Value; \
	}; \
	int32 TransmittanceLutWidth = ValidateLUTResolution(CVarSkyAtmosphereTransmittanceLUTWidth.GetValueOnRenderThread()); \
	int32 TransmittanceLutHeight = ValidateLUTResolution(CVarSkyAtmosphereTransmittanceLUTHeight.GetValueOnRenderThread()); \
	int32 MultiScatteredLuminanceLutWidth = ValidateLUTResolution(CVarSkyAtmosphereMultiScatteringLUTWidth.GetValueOnRenderThread()); \
	int32 MultiScatteredLuminanceLutHeight = ValidateLUTResolution(CVarSkyAtmosphereMultiScatteringLUTHeight.GetValueOnRenderThread()); \
	int32 SkyViewLutWidth = ValidateLUTResolution(CVarSkyAtmosphereFastSkyLUTWidth.GetValueOnRenderThread()); \
	int32 SkyViewLutHeight = ValidateLUTResolution(CVarSkyAtmosphereFastSkyLUTHeight.GetValueOnRenderThread()); \
	int32 CameraAerialPerspectiveVolumeScreenResolution = ValidateLUTResolution(CVarSkyAtmosphereAerialPerspectiveLUTWidth.GetValueOnRenderThread()); \
	int32 CameraAerialPerspectiveVolumeDepthResolution = ValidateLUTResolution(CVarSkyAtmosphereAerialPerspectiveLUTDepthResolution.GetValueOnRenderThread()); \
	float CameraAerialPerspectiveVolumeDepthKm = CVarSkyAtmosphereAerialPerspectiveLUTDepth.GetValueOnRenderThread(); \
	CameraAerialPerspectiveVolumeDepthKm = CameraAerialPerspectiveVolumeDepthKm < 1.0f ? 1.0f : CameraAerialPerspectiveVolumeDepthKm;	/* 1 kilometer minimum */ \
	float CameraAerialPerspectiveVolumeDepthSliceLengthKm = CameraAerialPerspectiveVolumeDepthKm / CameraAerialPerspectiveVolumeDepthResolution;

#define KM_TO_CM  100000.0f
#define CM_TO_KM  (1.0f / KM_TO_CM)

float GetValidAerialPerspectiveStartDepthInCm(const FViewInfo& View, const FSkyAtmosphereSceneProxy& SkyAtmosphereProxy)
{
	float AerialPerspectiveStartDepthKm = SkyAtmosphereProxy.GetAerialPerspectiveStartDepthKm();
	AerialPerspectiveStartDepthKm = AerialPerspectiveStartDepthKm < 0.0f ? 0.0f : AerialPerspectiveStartDepthKm;
	// For sky reflection capture, the start depth can be super large. So we max it to make sure the triangle is never in front the NearClippingDistance.
	const float StartDepthInCm = FMath::Max(AerialPerspectiveStartDepthKm * KM_TO_CM, View.NearClippingDistance);
	return StartDepthInCm;
}

static bool ShouldPipelineCompileSkyAtmosphereShader(EShaderPlatform ShaderPlatform)
{
	// Requires SM5 or ES3_1 (GL/Vulkan) for compute shaders and volume textures support.
	return RHISupportsComputeShaders(ShaderPlatform);
}



bool ShouldSkySampleAtmosphereLightsOpaqueShadow(const FScene& Scene, const TArray<FVisibleLightInfo, SceneRenderingAllocator>& VisibleLightInfos, SkyAtmosphereLightShadowData& LightShadowData)
{
	LightShadowData.LightVolumetricShadowSceneinfo0 = Scene.AtmosphereLights[0];
	LightShadowData.LightVolumetricShadowSceneinfo1 = Scene.AtmosphereLights[1];
	
	if (LightShadowData.LightVolumetricShadowSceneinfo0 && LightShadowData.LightVolumetricShadowSceneinfo0->Proxy && LightShadowData.LightVolumetricShadowSceneinfo0->Proxy->GetCastShadowsOnAtmosphere())
	{
		LightShadowData.ProjectedShadowInfo0 = GetLastCascadeShadowInfo(LightShadowData.LightVolumetricShadowSceneinfo0->Proxy, VisibleLightInfos[LightShadowData.LightVolumetricShadowSceneinfo0->Id]);
	}
	if (LightShadowData.LightVolumetricShadowSceneinfo1 && LightShadowData.LightVolumetricShadowSceneinfo1->Proxy && LightShadowData.LightVolumetricShadowSceneinfo1->Proxy->GetCastShadowsOnAtmosphere())
	{
		LightShadowData.ProjectedShadowInfo1 = GetLastCascadeShadowInfo(LightShadowData.LightVolumetricShadowSceneinfo1->Proxy, VisibleLightInfos[LightShadowData.LightVolumetricShadowSceneinfo1->Id]);
	}

	return CVarSkyAtmosphereSampleLightShadowmap.GetValueOnRenderThread() > 0 &&
		(LightShadowData.ProjectedShadowInfo0 || LightShadowData.ProjectedShadowInfo1);
}
void GetSkyAtmosphereLightsUniformBuffers(
	TUniformBufferRef<FVolumeShadowingShaderParametersGlobal0>& OutLightShadowShaderParams0UniformBuffer,
	TUniformBufferRef<FVolumeShadowingShaderParametersGlobal1>& OutLightShadowShaderParams1UniformBuffer,
	const SkyAtmosphereLightShadowData& LightShadowData,
	const FViewInfo& ViewInfo,
	const bool bShouldSampleOpaqueShadow,
	const EUniformBufferUsage UniformBufferUsage
)
{
	FVolumeShadowingShaderParametersGlobal0 LightShadowShaderParams0;
	FVolumeShadowingShaderParametersGlobal1 LightShadowShaderParams1;
	if (bShouldSampleOpaqueShadow && LightShadowData.LightVolumetricShadowSceneinfo0)
	{
		SetVolumeShadowingShaderParameters(LightShadowShaderParams0, ViewInfo,
			LightShadowData.LightVolumetricShadowSceneinfo0, LightShadowData.ProjectedShadowInfo0, INDEX_NONE);
	}
	else
	{
		SetVolumeShadowingDefaultShaderParameters(LightShadowShaderParams0);
	}
	if (bShouldSampleOpaqueShadow && LightShadowData.LightVolumetricShadowSceneinfo1)
	{
		SetVolumeShadowingShaderParameters(LightShadowShaderParams1, ViewInfo,
			LightShadowData.LightVolumetricShadowSceneinfo1, LightShadowData.ProjectedShadowInfo1, INDEX_NONE);
	}
	else
	{
		SetVolumeShadowingDefaultShaderParameters(LightShadowShaderParams1);
	}
	OutLightShadowShaderParams0UniformBuffer = TUniformBufferRef<FVolumeShadowingShaderParametersGlobal0>::CreateUniformBufferImmediate(LightShadowShaderParams0, UniformBufferUsage);
	OutLightShadowShaderParams1UniformBuffer = TUniformBufferRef<FVolumeShadowingShaderParametersGlobal1>::CreateUniformBufferImmediate(LightShadowShaderParams1, UniformBufferUsage);
}

bool ShouldRenderSkyAtmosphere(const FScene* Scene, const FEngineShowFlags& EngineShowFlags)
{
	if (Scene && Scene->HasSkyAtmosphere() && EngineShowFlags.Atmosphere)
	{
		EShaderPlatform ShaderPlatform = Scene->GetShaderPlatform();
		const FSkyAtmosphereRenderSceneInfo* SkyAtmosphere = Scene->GetSkyAtmosphereSceneInfo();
		check(SkyAtmosphere);

		const bool ShadersCompiled = ShouldPipelineCompileSkyAtmosphereShader(ShaderPlatform);
		return FReadOnlyCVARCache::Get().bSupportSkyAtmosphere && ShadersCompiled && CVarSkyAtmosphere.GetValueOnRenderThread() > 0;
	}
	return false;
}

static auto GetSizeAndInvSize = [](int32 Width, int32 Height)
{
	float FWidth = float(Width);
	float FHeight = float(Height);
	return FVector4(FWidth, FHeight, 1.0f / FWidth, 1.0f / FHeight);
};

void SetupSkyAtmosphereViewSharedUniformShaderParameters(const FViewInfo& View, const FSkyAtmosphereSceneProxy& SkyAtmosphereProxy, FSkyAtmosphereViewSharedUniformShaderParameters& OutParameters)
{
	GET_VALID_DATA_FROM_CVAR;

	FRHITexture* SkyAtmosphereCameraAerialPerspectiveVolume = nullptr;
	if (View.SkyAtmosphereCameraAerialPerspectiveVolume)
	{
		SkyAtmosphereCameraAerialPerspectiveVolume = View.SkyAtmosphereCameraAerialPerspectiveVolume->GetRenderTargetItem().ShaderResourceTexture;
	}

	OutParameters.CameraAerialPerspectiveVolumeSizeAndInvSize = GetSizeAndInvSize(CameraAerialPerspectiveVolumeScreenResolution, CameraAerialPerspectiveVolumeScreenResolution);
	OutParameters.ApplyCameraAerialPerspectiveVolume = View.SkyAtmosphereCameraAerialPerspectiveVolume == nullptr ? 0.0f : 1.0f;
	OutParameters.CameraAerialPerspectiveVolumeDepthResolution = float(CameraAerialPerspectiveVolumeDepthResolution);
	OutParameters.CameraAerialPerspectiveVolumeDepthResolutionInv = 1.0f / OutParameters.CameraAerialPerspectiveVolumeDepthResolution;
	OutParameters.CameraAerialPerspectiveVolumeDepthSliceLengthKm = CameraAerialPerspectiveVolumeDepthSliceLengthKm;
	OutParameters.CameraAerialPerspectiveVolumeDepthSliceLengthKmInv = 1.0f / OutParameters.CameraAerialPerspectiveVolumeDepthSliceLengthKm;

	OutParameters.AerialPerspectiveStartDepthKm = GetValidAerialPerspectiveStartDepthInCm(View, SkyAtmosphereProxy) * CM_TO_KM;

	SetBlackAlpha13DIfNull(SkyAtmosphereCameraAerialPerspectiveVolume); // Needs to be after we set ApplyCameraAerialPerspectiveVolume
}

static void CopyAtmosphereSetupToUniformShaderParameters(FAtmosphereUniformShaderParameters& out, const FAtmosphereSetup& Atmosphere)
{
#define COPYMACRO(MemberName) out.MemberName = Atmosphere.MemberName 
	COPYMACRO(MultiScatteringFactor);
	COPYMACRO(BottomRadiusKm);
	COPYMACRO(TopRadiusKm);
	COPYMACRO(RayleighDensityExpScale);
	COPYMACRO(RayleighScattering);
	COPYMACRO(MieScattering);
	COPYMACRO(MieDensityExpScale);
	COPYMACRO(MieExtinction);
	COPYMACRO(MiePhaseG);
	COPYMACRO(MieAbsorption);
	COPYMACRO(AbsorptionDensity0LayerWidth);
	COPYMACRO(AbsorptionDensity0ConstantTerm);
	COPYMACRO(AbsorptionDensity0LinearTerm);
	COPYMACRO(AbsorptionDensity1ConstantTerm);
	COPYMACRO(AbsorptionDensity1LinearTerm);
	COPYMACRO(AbsorptionExtinction);
	COPYMACRO(GroundAlbedo);
#undef COPYMACRO
}

static FLinearColor GetLightDiskLuminance(FLightSceneInfo& Light, FLinearColor LightIlluminance)
{
	const float SunSolidAngle = 2.0f * PI * (1.0f - FMath::Cos(Light.Proxy->GetSunLightHalfApexAngleRadian())); // Solid angle from aperture https://en.wikipedia.org/wiki/Solid_angle 
	return  Light.Proxy->GetAtmosphereSunDiskColorScale() * LightIlluminance / SunSolidAngle; // approximation
}

void PrepareSunLightProxy(const FSkyAtmosphereRenderSceneInfo& SkyAtmosphere, uint32 AtmosphereLightIndex, FLightSceneInfo& AtmosphereLight)
{
	// See explanation in "Physically Based Sky, Atmosphere	and Cloud Rendering in Frostbite" page 26
	const bool bAtmosphereAffectsSunIlluminance = true;
	const FSkyAtmosphereSceneProxy& SkyAtmosphereProxy = SkyAtmosphere.GetSkyAtmosphereSceneProxy();
	const FVector AtmosphereLightDirection = SkyAtmosphereProxy.GetAtmosphereLightDirection(AtmosphereLightIndex, -AtmosphereLight.Proxy->GetDirection());
	FLinearColor TransmittanceTowardSun = bAtmosphereAffectsSunIlluminance ? SkyAtmosphereProxy.GetAtmosphereSetup().GetTransmittanceAtGroundLevel(AtmosphereLightDirection) : FLinearColor(FLinearColor::White);
	FLinearColor TransmittanceAtZenithFinal = bAtmosphereAffectsSunIlluminance ? SkyAtmosphereProxy.GetTransmittanceAtZenith() : FLinearColor(FLinearColor::White);

	FLinearColor SunZenithIlluminance = AtmosphereLight.Proxy->GetColor();
	FLinearColor SunOuterSpaceIlluminance = SunZenithIlluminance / TransmittanceAtZenithFinal;
	FLinearColor SunDiskOuterSpaceLuminance = GetLightDiskLuminance(AtmosphereLight, SunOuterSpaceIlluminance);

	// We always set the transmittance on the proxy. Shaders using atmospheric light color then have to decide which sun illuminance to use (without or with transmittance).
	// We also set whether or not the light should apply the simple transmittance computed on CPU during lighting pass. If per pixel transmittance is enabled, it should not be done.
	const bool bApplyAtmosphereTransmittanceToLightShaderParam = !AtmosphereLight.Proxy->GetUsePerPixelAtmosphereTransmittance();
	AtmosphereLight.Proxy->SetAtmosphereRelatedProperties(TransmittanceTowardSun / TransmittanceAtZenithFinal, SunDiskOuterSpaceLuminance, bApplyAtmosphereTransmittanceToLightShaderParam);
}



/*=============================================================================
	FSkyAtmosphereRenderSceneInfo implementation.
=============================================================================*/


FSkyAtmosphereRenderSceneInfo::FSkyAtmosphereRenderSceneInfo(FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxyIn)
	:SkyAtmosphereSceneProxy(SkyAtmosphereSceneProxyIn)
{
	// Create a multiframe uniform buffer. A render command is used because FSkyAtmosphereRenderSceneInfo ctor is called on the Game thread.
	TUniformBufferRef<FAtmosphereUniformShaderParameters>* AtmosphereUniformBufferPtr = &AtmosphereUniformBuffer;
	FAtmosphereUniformShaderParameters* AtmosphereUniformShaderParametersPtr = &AtmosphereUniformShaderParameters;
	CopyAtmosphereSetupToUniformShaderParameters(AtmosphereUniformShaderParameters, SkyAtmosphereSceneProxy.GetAtmosphereSetup());
	ENQUEUE_RENDER_COMMAND(FCreateUniformBuffer)(
		[AtmosphereUniformBufferPtr, AtmosphereUniformShaderParametersPtr](FRHICommandListImmediate& RHICmdList)
	{
		*AtmosphereUniformBufferPtr = TUniformBufferRef<FAtmosphereUniformShaderParameters>::CreateUniformBufferImmediate(*AtmosphereUniformShaderParametersPtr, UniformBuffer_MultiFrame);
	});
}

FSkyAtmosphereRenderSceneInfo::~FSkyAtmosphereRenderSceneInfo()
{
}

TRefCountPtr<IPooledRenderTarget>& FSkyAtmosphereRenderSceneInfo::GetDistantSkyLightLutTexture()
{
	if (CVarSkyAtmosphereDistantSkyLightLUT.GetValueOnRenderThread() > 0)
	{
		return DistantSkyLightLutTexture;
	}
	return GSystemTextures.BlackDummy;
}



/*=============================================================================
	FScene functions
=============================================================================*/



void FScene::AddSkyAtmosphere(FSkyAtmosphereSceneProxy* SkyAtmosphereSceneProxy, bool bStaticLightingBuilt)
{
	check(SkyAtmosphereSceneProxy);
	FScene* Scene = this;

	ENQUEUE_RENDER_COMMAND(FAddSkyAtmosphereCommand)(
		[Scene, SkyAtmosphereSceneProxy, bStaticLightingBuilt](FRHICommandListImmediate& RHICmdList)
		{
			check(!Scene->SkyAtmosphereStack.Contains(SkyAtmosphereSceneProxy));
			Scene->SkyAtmosphereStack.Push(SkyAtmosphereSceneProxy);

			SkyAtmosphereSceneProxy->RenderSceneInfo = new FSkyAtmosphereRenderSceneInfo(*SkyAtmosphereSceneProxy);

			// Use the most recently enabled SkyAtmosphere
			Scene->SkyAtmosphere = SkyAtmosphereSceneProxy->RenderSceneInfo;
			SkyAtmosphereSceneProxy->bStaticLightingBuilt = bStaticLightingBuilt;
			if (!SkyAtmosphereSceneProxy->bStaticLightingBuilt)
			{
				FPlatformAtomics::InterlockedIncrement(&Scene->NumUncachedStaticLightingInteractions);
			}
		} );
}

void FScene::RemoveSkyAtmosphere(FSkyAtmosphereSceneProxy* SkyAtmosphereSceneProxy)
{
	check(SkyAtmosphereSceneProxy);
	FScene* Scene = this;

	ENQUEUE_RENDER_COMMAND(FRemoveSkyAtmosphereCommand)(
		[Scene, SkyAtmosphereSceneProxy](FRHICommandListImmediate& RHICmdList)
		{
			if (!SkyAtmosphereSceneProxy->bStaticLightingBuilt)
			{
				FPlatformAtomics::InterlockedDecrement(&Scene->NumUncachedStaticLightingInteractions);
			}
			delete SkyAtmosphereSceneProxy->RenderSceneInfo;
			Scene->SkyAtmosphereStack.RemoveSingle(SkyAtmosphereSceneProxy);

			if (Scene->SkyAtmosphereStack.Num() > 0)
			{
				// Use the most recently enabled SkyAtmosphere
				Scene->SkyAtmosphere = Scene->SkyAtmosphereStack.Last()->RenderSceneInfo;
			}
			else
			{
				Scene->SkyAtmosphere = nullptr;
			}
		} );
}

void FScene::ResetAtmosphereLightsProperties()
{
	// Also rest the current atmospheric light to default atmosphere
	for (int32 LightIndex = 0; LightIndex < NUM_ATMOSPHERE_LIGHTS; ++LightIndex)
	{
		FLightSceneInfo* Light = AtmosphereLights[LightIndex];
		if (Light)
		{
			FLinearColor LightZenithIlluminance = Light->Proxy->GetColor();
			const bool bApplyAtmosphereTransmittanceToLightShaderParam = true;
			Light->Proxy->SetAtmosphereRelatedProperties(FLinearColor::White, GetLightDiskLuminance(*Light, LightZenithIlluminance), bApplyAtmosphereTransmittanceToLightShaderParam);
		}
	}
}



/*=============================================================================
	Sky/Atmosphere rendering functions
=============================================================================*/



namespace
{

class FHighQualityMultiScatteringApprox : SHADER_PERMUTATION_BOOL("HIGHQUALITY_MULTISCATTERING_APPROX_ENABLED");
class FFastSky : SHADER_PERMUTATION_BOOL("FASTSKY_ENABLED");
class FFastAerialPespective : SHADER_PERMUTATION_BOOL("FASTAERIALPERSPECTIVE_ENABLED");
class FSecondAtmosphereLight : SHADER_PERMUTATION_BOOL("SECOND_ATMOSPHERE_LIGHT_ENABLED");
class FRenderSky : SHADER_PERMUTATION_BOOL("RENDERSKY_ENABLED");
class FSampleOpaqueShadow : SHADER_PERMUTATION_BOOL("SAMPLE_OPAQUE_SHADOW");
class FSampleCloudShadow : SHADER_PERMUTATION_BOOL("SAMPLE_CLOUD_SHADOW");
class FSampleCloudSkyAO : SHADER_PERMUTATION_BOOL("SAMPLE_CLOUD_SKYAO");
class FAtmosphereOnClouds : SHADER_PERMUTATION_BOOL("SAMPLE_ATMOSPHERE_ON_CLOUDS");

}

//////////////////////////////////////////////////////////////////////////

class FRenderSkyAtmosphereVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderSkyAtmosphereVS);
	SHADER_USE_PARAMETER_STRUCT(FRenderSkyAtmosphereVS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, StartDepthZ)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderSkyAtmosphereVS, "/Engine/Private/SkyAtmosphere.usf", "SkyAtmosphereVS", SF_Vertex);

class FRenderSkyAtmospherePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderSkyAtmospherePS);
	SHADER_USE_PARAMETER_STRUCT(FRenderSkyAtmospherePS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FSampleCloudSkyAO, FFastSky, FFastAerialPespective, FSecondAtmosphereLight, FRenderSky, FSampleOpaqueShadow, FSampleCloudShadow, FAtmosphereOnClouds>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
		SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, TransmittanceLutTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, MultiScatteredLuminanceLutTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, SkyViewLutTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, CameraAerialPerspectiveVolumeTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VolumetricCloudShadowMapTexture0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VolumetricCloudShadowMapTexture1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VolumetricCloudSkyAOTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float> , VolumetricCloudDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputCloudLuminanceTransmittanceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, MultiScatteredLuminanceLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkyViewLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, CameraAerialPerspectiveVolumeTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricCloudShadowMapTexture0Sampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricCloudShadowMapTexture1Sampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricCloudSkyAOTextureSampler)
		SHADER_PARAMETER(float, AerialPerspectiveStartDepthKm)
		SHADER_PARAMETER(uint32, SourceDiskEnabled)
		SHADER_PARAMETER(uint32, DepthReadDisabled)
		SHADER_PARAMETER_STRUCT_REF(FVolumeShadowingShaderParametersGlobal0, Light0Shadow)
		SHADER_PARAMETER_STRUCT_REF(FVolumeShadowingShaderParametersGlobal1, Light1Shadow)
		SHADER_PARAMETER_STRUCT_REF(FVolumetricCloudCommonGlobalShaderParameters, VolumetricCloudCommonGlobalParams)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		// If not rendering the sky, ignore the fastsky and sundisk permutations
		if (PermutationVector.Get<FRenderSky>() == false)
		{
			PermutationVector.Set<FFastSky>(false);
		}

		if (PermutationVector.Get<FAtmosphereOnClouds>() == true)
		{
			PermutationVector.Set<FFastSky>(false);
			PermutationVector.Set<FFastAerialPespective>(false);
			PermutationVector.Set<FRenderSky>(false);
		}

		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		// If not rendering the sky, ignore the FFastSky permutation
		if (PermutationVector.Get<FRenderSky>() == false && PermutationVector.Get<FFastSky>())
		{
			return false;
		}

		if (PermutationVector.Get<FAtmosphereOnClouds>() == true)
		{
			// FSampleCloudSkyAO, FFastSky, FFastAerialPespective, FSecondAtmosphereLight, FRenderSky, FSampleOpaqueShadow, FSampleCloudShadow
			// When tracing atmosphere on clouds, this is because we want crisp light shaft on them.
			if (PermutationVector.Get<FFastSky>() || PermutationVector.Get<FFastAerialPespective>() || PermutationVector.Get<FRenderSky>())
			{
				return false;
			}
		}

		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("PER_PIXEL_NOISE"), 1);
		OutEnvironment.SetDefine(TEXT("MULTISCATTERING_APPROX_SAMPLING_ENABLED"), 1);
		OutEnvironment.SetDefine(TEXT("SOURCE_DISK_ENABLED"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderSkyAtmospherePS, "/Engine/Private/SkyAtmosphere.usf", "RenderSkyAtmosphereRayMarchingPS", SF_Pixel);

//////////////////////////////////////////////////////////////////////////

class FRenderTransmittanceLutCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderTransmittanceLutCS);
	SHADER_USE_PARAMETER_STRUCT(FRenderTransmittanceLutCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

public:
	const static uint32 GroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
		SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, TransmittanceLutUAV)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize);
		OutEnvironment.SetDefine(TEXT("WHITE_TRANSMITTANCE"),1); // Workaround for some compiler not culling enough unused code (e.g. when computing TransmittanceLUT, Transmittance texture is still requested but we are computing it) 
		OutEnvironment.SetDefine(TEXT("TRANSMITTANCE_PASS"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderTransmittanceLutCS, "/Engine/Private/SkyAtmosphere.usf", "RenderTransmittanceLutCS", SF_Compute);

//////////////////////////////////////////////////////////////////////////

class FRenderMultiScatteredLuminanceLutCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderMultiScatteredLuminanceLutCS);
	SHADER_USE_PARAMETER_STRUCT(FRenderMultiScatteredLuminanceLutCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FHighQualityMultiScatteringApprox>;

public:
	const static uint32 GroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
		SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, MultiScatteredLuminanceLutUAV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, TransmittanceLutTexture)
		SHADER_PARAMETER_SRV(Buffer<float4>, UniformSphereSamplesBuffer)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutTextureSampler)
		SHADER_PARAMETER(uint32, UniformSphereSamplesBufferSampleCount)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize);
		OutEnvironment.SetDefine(TEXT("MULTISCATT_PASS"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderMultiScatteredLuminanceLutCS, "/Engine/Private/SkyAtmosphere.usf", "RenderMultiScatteredLuminanceLutCS", SF_Compute);

//////////////////////////////////////////////////////////////////////////

class FRenderDistantSkyLightLutCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderDistantSkyLightLutCS);
	SHADER_USE_PARAMETER_STRUCT(FRenderDistantSkyLightLutCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FSecondAtmosphereLight>;

public:
	const static uint32 GroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
		SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, DistantSkyLightLutUAV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, TransmittanceLutTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, MultiScatteredLuminanceLutTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, MultiScatteredLuminanceLutTextureSampler)
		SHADER_PARAMETER_SRV(Buffer<float4>, UniformSphereSamplesBuffer)
		SHADER_PARAMETER(FVector4, AtmosphereLightDirection0)
		SHADER_PARAMETER(FVector4, AtmosphereLightDirection1)
		SHADER_PARAMETER(FLinearColor, AtmosphereLightColor0)
		SHADER_PARAMETER(FLinearColor, AtmosphereLightColor1)
		SHADER_PARAMETER(float, DistantSkyLightSampleAltitude)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize);
		OutEnvironment.SetDefine(TEXT("SKYLIGHT_PASS"), 1);
		OutEnvironment.SetDefine(TEXT("MULTISCATTERING_APPROX_SAMPLING_ENABLED"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderDistantSkyLightLutCS, "/Engine/Private/SkyAtmosphere.usf", "RenderDistantSkyLightLutCS", SF_Compute);

//////////////////////////////////////////////////////////////////////////

class FRenderSkyViewLutCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderSkyViewLutCS);
	SHADER_USE_PARAMETER_STRUCT(FRenderSkyViewLutCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FSampleCloudSkyAO, FSecondAtmosphereLight, FSampleOpaqueShadow, FSampleCloudShadow>;

public:
	const static uint32 GroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
		SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, SkyViewLutUAV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, TransmittanceLutTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, MultiScatteredLuminanceLutTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VolumetricCloudShadowMapTexture0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VolumetricCloudShadowMapTexture1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VolumetricCloudSkyAOTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, MultiScatteredLuminanceLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricCloudShadowMapTexture0Sampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricCloudShadowMapTexture1Sampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricCloudSkyAOTextureSampler)
		SHADER_PARAMETER_STRUCT_REF(FVolumeShadowingShaderParametersGlobal0, Light0Shadow)
		SHADER_PARAMETER_STRUCT_REF(FVolumeShadowingShaderParametersGlobal1, Light1Shadow)
		SHADER_PARAMETER_STRUCT_REF(FVolumetricCloudCommonGlobalShaderParameters, VolumetricCloudCommonGlobalParams)
		SHADER_PARAMETER(uint32, SourceDiskEnabled)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize);
		OutEnvironment.SetDefine(TEXT("SKYVIEWLUT_PASS"), 1);
		OutEnvironment.SetDefine(TEXT("MULTISCATTERING_APPROX_SAMPLING_ENABLED"), 1);
		OutEnvironment.SetDefine(TEXT("SOURCE_DISK_ENABLED"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderSkyViewLutCS, "/Engine/Private/SkyAtmosphere.usf", "RenderSkyViewLutCS", SF_Compute);

//////////////////////////////////////////////////////////////////////////

class FRenderCameraAerialPerspectiveVolumeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderCameraAerialPerspectiveVolumeCS);
	SHADER_USE_PARAMETER_STRUCT(FRenderCameraAerialPerspectiveVolumeCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FSampleCloudSkyAO, FSecondAtmosphereLight, FSampleOpaqueShadow, FSampleCloudShadow>;

public:
	const static uint32 GroupSize = 4;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
		SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, CameraAerialPerspectiveVolumeUAV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, TransmittanceLutTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, MultiScatteredLuminanceLutTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VolumetricCloudShadowMapTexture0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VolumetricCloudShadowMapTexture1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, VolumetricCloudSkyAOTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, MultiScatteredLuminanceLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricCloudShadowMapTexture0Sampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricCloudShadowMapTexture1Sampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, VolumetricCloudSkyAOTextureSampler)
		SHADER_PARAMETER(float, AerialPerspectiveStartDepthKm)
		SHADER_PARAMETER(float, RealTimeReflection360Mode)
		SHADER_PARAMETER_STRUCT_REF(FVolumeShadowingShaderParametersGlobal0, Light0Shadow)
		SHADER_PARAMETER_STRUCT_REF(FVolumeShadowingShaderParametersGlobal1, Light1Shadow)
		SHADER_PARAMETER_STRUCT_REF(FVolumetricCloudCommonGlobalShaderParameters, VolumetricCloudCommonGlobalParams)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize);
		OutEnvironment.SetDefine(TEXT("MULTISCATTERING_APPROX_SAMPLING_ENABLED"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderCameraAerialPerspectiveVolumeCS, "/Engine/Private/SkyAtmosphere.usf", "RenderCameraAerialPerspectiveVolumeCS", SF_Compute);

//////////////////////////////////////////////////////////////////////////

class FRenderDebugSkyAtmospherePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderDebugSkyAtmospherePS);
	SHADER_USE_PARAMETER_STRUCT(FRenderDebugSkyAtmospherePS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
		SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, TransmittanceLutTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, MultiScatteredLuminanceLutTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, MultiScatteredLuminanceLutTextureSampler)
		SHADER_PARAMETER(float, ViewPortWidth)
		SHADER_PARAMETER(float, ViewPortHeight)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// TODO: Exclude when shipping.
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MULTISCATTERING_APPROX_SAMPLING_ENABLED"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderDebugSkyAtmospherePS, "/Engine/Private/SkyAtmosphere.usf", "RenderSkyAtmosphereDebugPS", SF_Pixel);

//////////////////////////////////////////////////////////////////////////

class RenderSkyAtmosphereEditorHudPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(RenderSkyAtmosphereEditorHudPS);
	SHADER_USE_PARAMETER_STRUCT(RenderSkyAtmosphereEditorHudPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_TEXTURE(Texture2D, MiniFontTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// TODO: Exclude when shipping.
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_EDITOR_HUD"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(RenderSkyAtmosphereEditorHudPS, "/Engine/Private/SkyAtmosphere.usf", "RenderSkyAtmosphereEditorHudPS", SF_Pixel);


/*=============================================================================
	FUniformSphereSamplesBuffer
=============================================================================*/

class FUniformSphereSamplesBuffer : public FRenderResource
{
public:
	FReadBuffer UniformSphereSamplesBuffer;

	uint32 GetSampletCount()
	{
		return FRenderDistantSkyLightLutCS::GroupSize;
	}

	virtual void InitRHI() override
	{
		if ( ! RHISupportsComputeShaders(GMaxRHIShaderPlatform) )
		{
			return;
		}
		const uint32 GroupSize = GetSampletCount();
		const float GroupSizeInv = 1.0f / float(GroupSize);

		UniformSphereSamplesBuffer.Initialize(sizeof(FVector4), GroupSize * GroupSize, EPixelFormat::PF_A32B32G32R32F, BUF_Static);
		FVector4* Dest = (FVector4*)RHILockVertexBuffer(UniformSphereSamplesBuffer.Buffer, 0, sizeof(FVector4)*GroupSize*GroupSize, RLM_WriteOnly);

		FMath::SRandInit(0xDE4DC0DE);
		for (uint32 i = 0; i < GroupSize; ++i)
		{
			for (uint32 j = 0; j < GroupSize; ++j)
			{
				const float u0 = (float(i) + FMath::SRand()) * GroupSizeInv;
				const float u1 = (float(j) + FMath::SRand()) * GroupSizeInv;

				const float a = 1.0f - 2.0f * u0;
				const float b = FMath::Sqrt(1.0f - a*a);
				const float phi = 2 * PI * u1;

				uint32 idx = j * GroupSize + i;
				Dest[idx].X = b * FMath::Cos(phi);
				Dest[idx].Y = b * FMath::Sin(phi);
				Dest[idx].Z = a;
				Dest[idx].W = 0.0f;
			}
		}

		RHIUnlockVertexBuffer(UniformSphereSamplesBuffer.Buffer);
	}

	virtual void ReleaseRHI()
	{
		if ( RHISupportsComputeShaders(GMaxRHIShaderPlatform) )
		{
			UniformSphereSamplesBuffer.Release();
		}
	}
};
TGlobalResource<FUniformSphereSamplesBuffer> GUniformSphereSamplesBuffer;



/*=============================================================================
	FSceneRenderer functions
=============================================================================*/

void FSceneRenderer::InitSkyAtmosphereForViews(FRHICommandListImmediate& RHICmdList)
{
	InitSkyAtmosphereForScene(RHICmdList, Scene);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		InitSkyAtmosphereForView(RHICmdList, Scene, View);
	}
}

static EPixelFormat GetSkyLutTextureFormat(ERHIFeatureLevel::Type FeatureLevel)
{
	EPixelFormat TextureLUTFormat = PF_FloatRGB;
	if (FeatureLevel <= ERHIFeatureLevel::ES3_1)
	{
		// OpenGL ES3.1 does not support storing into 3-component images
		// TODO: check if need this for Metal, Vulkan
		TextureLUTFormat = PF_FloatRGBA;
	}
	
	if (CVarSkyAtmosphereLUT32.GetValueOnAnyThread() != 0)
	{
		TextureLUTFormat = PF_A32B32G32R32F;
	}

	return TextureLUTFormat;
}
static EPixelFormat GetSkyLutSmallTextureFormat()
{
	if (CVarSkyAtmosphereLUT32.GetValueOnAnyThread() != 0)
	{
		return PF_A32B32G32R32F;
	}
	return PF_R8G8B8A8;
}

void InitSkyAtmosphereForScene(FRHICommandListImmediate& RHICmdList, FScene* Scene)
{
	if (Scene)
	{
		GET_VALID_DATA_FROM_CVAR;

		FPooledRenderTargetDesc Desc;
		check(Scene->GetSkyAtmosphereSceneInfo());
		FSkyAtmosphereRenderSceneInfo& SkyInfo = *Scene->GetSkyAtmosphereSceneInfo();

		EPixelFormat TextureLUTFormat = GetSkyLutTextureFormat(Scene->GetFeatureLevel());
		EPixelFormat TextureLUTSmallFormat = GetSkyLutSmallTextureFormat();

		//
		// Initialise per scene/atmosphere resources
		//
		const bool TranstmittanceLUTUseSmallFormat = CVarSkyAtmosphereTransmittanceLUTUseSmallFormat.GetValueOnRenderThread() > 0;

		TRefCountPtr<IPooledRenderTarget>& TransmittanceLutTexture = SkyInfo.GetTransmittanceLutTexture();
		Desc = FPooledRenderTargetDesc::Create2DDesc(
			FIntPoint(TransmittanceLutWidth, TransmittanceLutHeight),
			TranstmittanceLUTUseSmallFormat ? TextureLUTSmallFormat : TextureLUTFormat, FClearValueBinding::None, TexCreate_HideInVisualizeTexture, TexCreate_ShaderResource | TexCreate_UAV, false);
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, TransmittanceLutTexture, TEXT("TransmittanceLutTexture"), true, ERenderTargetTransience::Transient);

		TRefCountPtr<IPooledRenderTarget>& MultiScatteredLuminanceLutTexture = SkyInfo.GetMultiScatteredLuminanceLutTexture();
		Desc = FPooledRenderTargetDesc::Create2DDesc(
			FIntPoint(MultiScatteredLuminanceLutWidth, MultiScatteredLuminanceLutHeight),
			TextureLUTFormat, FClearValueBinding::None, TexCreate_HideInVisualizeTexture, TexCreate_ShaderResource | TexCreate_UAV, false);
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, MultiScatteredLuminanceLutTexture, TEXT("MultiScatteredLuminanceLutTexture"), true, ERenderTargetTransience::Transient);

		if (CVarSkyAtmosphereDistantSkyLightLUT.GetValueOnRenderThread() > 0)
		{
			TRefCountPtr<IPooledRenderTarget>& DistantSkyLightLutTexture = SkyInfo.GetDistantSkyLightLutTexture();
			Desc = FPooledRenderTargetDesc::Create2DDesc(
				FIntPoint(1, 1),
				TextureLUTFormat, FClearValueBinding::None, TexCreate_HideInVisualizeTexture, TexCreate_ShaderResource | TexCreate_UAV, false);
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, DistantSkyLightLutTexture, TEXT("DistantSkyLightLutTexture"), true, ERenderTargetTransience::Transient);
		}
	}
}

void InitSkyAtmosphereForView(FRHICommandListImmediate& RHICmdList, const FScene* Scene, FViewInfo& View)
{
	if (Scene)
	{
		GET_VALID_DATA_FROM_CVAR;

		check(ShouldRenderSkyAtmosphere(Scene, View.Family->EngineShowFlags)); // This should not be called if we should not render SkyAtmosphere
		FPooledRenderTargetDesc Desc;
		check(Scene->GetSkyAtmosphereSceneInfo());
		const FSkyAtmosphereRenderSceneInfo& SkyInfo = *Scene->GetSkyAtmosphereSceneInfo();

		EPixelFormat TextureLUTFormat = GetSkyLutTextureFormat(Scene->GetFeatureLevel());
		EPixelFormat TextureLUTSmallFormat = GetSkyLutSmallTextureFormat();
		EPixelFormat TextureAerialLUTFormat = (CVarSkyAtmosphereLUT32.GetValueOnAnyThread() != 0) ? PF_A32B32G32R32F : PF_FloatRGBA;

		//
		// Initialise transient per view resources.
		//

		FPooledRenderTargetDesc SkyAtmosphereViewLutTextureDesc = FPooledRenderTargetDesc::Create2DDesc(
			FIntPoint(SkyViewLutWidth, SkyViewLutHeight),
			TextureLUTFormat, FClearValueBinding::None, TexCreate_HideInVisualizeTexture, TexCreate_ShaderResource | TexCreate_UAV, false);

		FPooledRenderTargetDesc SkyAtmosphereCameraAerialPerspectiveVolumeDesc = FPooledRenderTargetDesc::CreateVolumeDesc(
			CameraAerialPerspectiveVolumeScreenResolution, CameraAerialPerspectiveVolumeScreenResolution, CameraAerialPerspectiveVolumeDepthResolution,
			TextureAerialLUTFormat, FClearValueBinding::None, TexCreate_HideInVisualizeTexture, TexCreate_ShaderResource | TexCreate_UAV, false);

		// Set textures and data that will be needed later on the view.
		View.SkyAtmosphereUniformShaderParameters = SkyInfo.GetAtmosphereShaderParameters();
		GRenderTargetPool.FindFreeElement(RHICmdList, SkyAtmosphereViewLutTextureDesc, View.SkyAtmosphereViewLutTexture, TEXT("View.SkyAtmosphereViewLutTexture"), true, ERenderTargetTransience::Transient);
		GRenderTargetPool.FindFreeElement(RHICmdList, SkyAtmosphereCameraAerialPerspectiveVolumeDesc, View.SkyAtmosphereCameraAerialPerspectiveVolume, TEXT("View.SkyAtmosphereCameraAerialPerspectiveVolume"), true, ERenderTargetTransience::Transient);
	}
}

static void SetupSkyAtmosphereInternalCommonParameters(
	FSkyAtmosphereInternalCommonParameters& InternalCommonParameters, 
	const FScene& Scene,
	const FSceneViewFamily& ViewFamily,
	const FSkyAtmosphereRenderSceneInfo& SkyInfo)
{
	GET_VALID_DATA_FROM_CVAR;

	InternalCommonParameters.TransmittanceLutSizeAndInvSize = GetSizeAndInvSize(TransmittanceLutWidth, TransmittanceLutHeight);
	InternalCommonParameters.MultiScatteredLuminanceLutSizeAndInvSize = GetSizeAndInvSize(MultiScatteredLuminanceLutWidth, MultiScatteredLuminanceLutHeight);
	InternalCommonParameters.SkyViewLutSizeAndInvSize = GetSizeAndInvSize(SkyViewLutWidth, SkyViewLutHeight);

	const float SkyAtmosphereBaseSampleCount = 32.0f;
	const float AerialPerspectiveBaseSampleCountPerSlice = 1.0f;

	InternalCommonParameters.SampleCountMin = CVarSkyAtmosphereSampleCountMin.GetValueOnRenderThread();
	InternalCommonParameters.SampleCountMax = FMath::Min(SkyAtmosphereBaseSampleCount * SkyInfo.GetSkyAtmosphereSceneProxy().GetTraceSampleCountScale(), float(CVarSkyAtmosphereSampleCountMax.GetValueOnRenderThread()));
	float DistanceToSampleCountMaxInv = CVarSkyAtmosphereDistanceToSampleCountMax.GetValueOnRenderThread();

	InternalCommonParameters.FastSkySampleCountMin = CVarSkyAtmosphereFastSkyLUTSampleCountMin.GetValueOnRenderThread();
	InternalCommonParameters.FastSkySampleCountMax = FMath::Min(SkyAtmosphereBaseSampleCount * SkyInfo.GetSkyAtmosphereSceneProxy().GetTraceSampleCountScale(), float(CVarSkyAtmosphereFastSkyLUTSampleCountMax.GetValueOnRenderThread()));
	float FastSkyDistanceToSampleCountMaxInv = CVarSkyAtmosphereFastSkyLUTDistanceToSampleCountMax.GetValueOnRenderThread();

	InternalCommonParameters.CameraAerialPerspectiveVolumeDepthResolution = float(CameraAerialPerspectiveVolumeDepthResolution);
	InternalCommonParameters.CameraAerialPerspectiveVolumeDepthResolutionInv = 1.0f / InternalCommonParameters.CameraAerialPerspectiveVolumeDepthResolution;
	InternalCommonParameters.CameraAerialPerspectiveVolumeDepthSliceLengthKm = CameraAerialPerspectiveVolumeDepthSliceLengthKm;
	InternalCommonParameters.CameraAerialPerspectiveVolumeDepthSliceLengthKmInv = 1.0f / CameraAerialPerspectiveVolumeDepthSliceLengthKm;
	InternalCommonParameters.CameraAerialPerspectiveSampleCountPerSlice = FMath::Max(AerialPerspectiveBaseSampleCountPerSlice, FMath::Min(2.0f * SkyInfo.GetSkyAtmosphereSceneProxy().GetTraceSampleCountScale(), float(CVarSkyAtmosphereAerialPerspectiveLUTSampleCountMaxPerSlice.GetValueOnRenderThread())));

	InternalCommonParameters.TransmittanceSampleCount = CVarSkyAtmosphereTransmittanceLUTSampleCount.GetValueOnRenderThread();
	InternalCommonParameters.MultiScatteringSampleCount = CVarSkyAtmosphereMultiScatteringLUTSampleCount.GetValueOnRenderThread();

	const FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy = SkyInfo.GetSkyAtmosphereSceneProxy();
	InternalCommonParameters.SkyLuminanceFactor = FVector(SkyAtmosphereSceneProxy.GetSkyLuminanceFactor());
	InternalCommonParameters.AerialPespectiveViewDistanceScale = SkyAtmosphereSceneProxy.GetAerialPespectiveViewDistanceScale();
	InternalCommonParameters.FogShowFlagFactor = ViewFamily.EngineShowFlags.Fog > 0 ? 1.0f : 0.0f;

	auto ValidateDistanceValue = [](float& Value)
	{
		Value = Value < KINDA_SMALL_NUMBER ? KINDA_SMALL_NUMBER : Value;
	};
	auto ValidateSampleCountValue = [](float& Value)
	{
		Value = Value < 1.0f ? 1.0f : Value;
	};
	auto ValidateMaxSampleCountValue = [](float& Value, float& MinValue)
	{
		Value = Value < MinValue ? MinValue : Value;
	};
	ValidateSampleCountValue(InternalCommonParameters.SampleCountMin);
	ValidateMaxSampleCountValue(InternalCommonParameters.SampleCountMax, InternalCommonParameters.SampleCountMin);
	ValidateSampleCountValue(InternalCommonParameters.FastSkySampleCountMin);
	ValidateMaxSampleCountValue(InternalCommonParameters.FastSkySampleCountMax, InternalCommonParameters.FastSkySampleCountMin);
	ValidateSampleCountValue(InternalCommonParameters.CameraAerialPerspectiveSampleCountPerSlice);
	ValidateSampleCountValue(InternalCommonParameters.TransmittanceSampleCount);
	ValidateSampleCountValue(InternalCommonParameters.MultiScatteringSampleCount);
	ValidateDistanceValue(DistanceToSampleCountMaxInv);
	ValidateDistanceValue(FastSkyDistanceToSampleCountMaxInv);

	// Derived values post validation
	InternalCommonParameters.DistanceToSampleCountMaxInv = 1.0f / DistanceToSampleCountMaxInv;
	InternalCommonParameters.FastSkyDistanceToSampleCountMaxInv = 1.0f / FastSkyDistanceToSampleCountMaxInv;
	InternalCommonParameters.CameraAerialPerspectiveVolumeSizeAndInvSize = GetSizeAndInvSize(CameraAerialPerspectiveVolumeScreenResolution, CameraAerialPerspectiveVolumeScreenResolution);
}

void FSceneRenderer::RenderSkyAtmosphereLookUpTables(FRHICommandListImmediate& RHICmdList)
{
	check(ShouldRenderSkyAtmosphere(Scene, ViewFamily.EngineShowFlags)); // This should not be called if we should not render SkyAtmosphere

	SCOPED_DRAW_EVENT(RHICmdList, SkyAtmosphereLUTs);
	SCOPED_GPU_STAT(RHICmdList, SkyAtmosphereLUTs);

	FSkyAtmosphereRenderSceneInfo& SkyInfo = *Scene->GetSkyAtmosphereSceneInfo();
	const FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy = SkyInfo.GetSkyAtmosphereSceneProxy();

	const bool bHighQualityMultiScattering = CVarSkyAtmosphereMultiScatteringLUTHighQuality.GetValueOnRenderThread() > 0;
	const bool bFastSky = CVarSkyAtmosphereFastSkyLUT.GetValueOnRenderThread() > 0;
	const bool bFastAerialPespective = CVarSkyAtmosphereAerialPerspectiveApplyOnOpaque.GetValueOnRenderThread() > 0;
	const bool bSecondAtmosphereLightEnabled = Scene->IsSecondAtmosphereLightEnabled();

	FRHISamplerState* SamplerLinearClamp = TStaticSamplerState<SF_Trilinear>::GetRHI();

	// Initialise common internal parameters on the sky info for this frame
	FSkyAtmosphereInternalCommonParameters InternalCommonParameters;
	SetupSkyAtmosphereInternalCommonParameters(InternalCommonParameters, *Scene, ViewFamily, SkyInfo);
	SkyInfo.GetInternalCommonParametersUniformBuffer() = TUniformBufferRef<FSkyAtmosphereInternalCommonParameters>::CreateUniformBufferImmediate(InternalCommonParameters, UniformBuffer_SingleFrame);

	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef TransmittanceLut = GraphBuilder.RegisterExternalTexture(SkyInfo.GetTransmittanceLutTexture());
	FRDGTextureUAVRef TransmittanceLutUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(TransmittanceLut, 0));
	FRDGTextureRef MultiScatteredLuminanceLut = GraphBuilder.RegisterExternalTexture(SkyInfo.GetMultiScatteredLuminanceLutTexture());
	FRDGTextureUAVRef MultiScatteredLuminanceLutUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(MultiScatteredLuminanceLut, 0));

	// Transmittance LUT
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
	{
		TShaderMapRef<FRenderTransmittanceLutCS> ComputeShader(GlobalShaderMap);

		FRenderTransmittanceLutCS::FParameters * PassParameters = GraphBuilder.AllocParameters<FRenderTransmittanceLutCS::FParameters>();
		PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
		PassParameters->SkyAtmosphere = SkyInfo.GetInternalCommonParametersUniformBuffer();
		PassParameters->TransmittanceLutUAV = TransmittanceLutUAV;

		FIntVector TextureSize = TransmittanceLut->Desc.GetSize();
		TextureSize.Z = 1;
		const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize, FRenderTransmittanceLutCS::GroupSize);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TransmittanceLut"), ComputeShader, PassParameters, NumGroups);

		GraphBuilder.QueueTextureExtraction(TransmittanceLut, &SkyInfo.GetTransmittanceLutTexture(), true);
	}
	
	// Multi-Scattering LUT
	{
		FRenderMultiScatteredLuminanceLutCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FHighQualityMultiScatteringApprox>(bHighQualityMultiScattering);
		TShaderMapRef<FRenderMultiScatteredLuminanceLutCS> ComputeShader(GlobalShaderMap, PermutationVector);

		FRenderMultiScatteredLuminanceLutCS::FParameters * PassParameters = GraphBuilder.AllocParameters<FRenderMultiScatteredLuminanceLutCS::FParameters>();
		PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
		PassParameters->SkyAtmosphere = SkyInfo.GetInternalCommonParametersUniformBuffer();
		PassParameters->TransmittanceLutTextureSampler = SamplerLinearClamp;
		PassParameters->TransmittanceLutTexture = TransmittanceLut;
		PassParameters->UniformSphereSamplesBuffer = GUniformSphereSamplesBuffer.UniformSphereSamplesBuffer.SRV;
		PassParameters->UniformSphereSamplesBufferSampleCount = GUniformSphereSamplesBuffer.GetSampletCount();
		PassParameters->MultiScatteredLuminanceLutUAV = MultiScatteredLuminanceLutUAV;

		FIntVector TextureSize = MultiScatteredLuminanceLut->Desc.GetSize();
		TextureSize.Z = 1;
		const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize, FRenderMultiScatteredLuminanceLutCS::GroupSize);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("MultiScatteringLut"), ComputeShader, PassParameters, NumGroups);

		GraphBuilder.QueueTextureExtraction(MultiScatteredLuminanceLut, &SkyInfo.GetMultiScatteredLuminanceLutTexture(), true);
	}

	// Distant Sky Light LUT
	if(CVarSkyAtmosphereDistantSkyLightLUT.GetValueOnRenderThread() > 0)
	{
		FRDGTextureRef DistantSkyLightLut = GraphBuilder.RegisterExternalTexture(SkyInfo.GetDistantSkyLightLutTexture());
		FRDGTextureUAVRef DistantSkyLightLutUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DistantSkyLightLut, 0));

		FRenderDistantSkyLightLutCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FSecondAtmosphereLight>(bSecondAtmosphereLightEnabled);
		PermutationVector.Set<FSecondAtmosphereLight>(bSecondAtmosphereLightEnabled);
		TShaderMapRef<FRenderDistantSkyLightLutCS> ComputeShader(GlobalShaderMap, PermutationVector);

		FRenderDistantSkyLightLutCS::FParameters * PassParameters = GraphBuilder.AllocParameters<FRenderDistantSkyLightLutCS::FParameters>();
		PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
		PassParameters->SkyAtmosphere = SkyInfo.GetInternalCommonParametersUniformBuffer();
		PassParameters->TransmittanceLutTextureSampler = SamplerLinearClamp;
		PassParameters->MultiScatteredLuminanceLutTextureSampler = SamplerLinearClamp;
		PassParameters->TransmittanceLutTexture = TransmittanceLut;
		PassParameters->MultiScatteredLuminanceLutTexture = MultiScatteredLuminanceLut;
		PassParameters->UniformSphereSamplesBuffer = GUniformSphereSamplesBuffer.UniformSphereSamplesBuffer.SRV;
		PassParameters->DistantSkyLightLutUAV = DistantSkyLightLutUAV;

		FLightSceneInfo* Light0 = Scene->AtmosphereLights[0];
		FLightSceneInfo* Light1 = Scene->AtmosphereLights[1];
		if (Light0)
		{
			PassParameters->AtmosphereLightDirection0 = -Light0->Proxy->GetDirection();
			PassParameters->AtmosphereLightColor0 = Light0->Proxy->GetColor();
		}
		if (Light1)
		{
			PassParameters->AtmosphereLightDirection1 = -Light1->Proxy->GetDirection();
			PassParameters->AtmosphereLightColor1 = Light1->Proxy->GetColor();
		}
		PassParameters->DistantSkyLightSampleAltitude = CVarSkyAtmosphereDistantSkyLightLUTAltitude.GetValueOnAnyThread();

		FIntVector TextureSize = FIntVector(1, 1, 1);
		const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize, FRenderDistantSkyLightLutCS::GroupSize);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DistantSkyLightLut"), ComputeShader, PassParameters, NumGroups);

		GraphBuilder.QueueTextureExtraction(DistantSkyLightLut, &SkyInfo.GetDistantSkyLightLutTexture(), true);
	}

	SkyAtmosphereLightShadowData LightShadowData;
	const bool bShouldSampleOpaqueShadow = ShouldSkySampleAtmosphereLightsOpaqueShadow(*Scene, VisibleLightInfos, LightShadowData);

	// SkyViewLUT texture is required if there are any sky dome material that could potentially sample it.
	// This texture is sampled on skydome mesh with a sky material when rendered into a cubemap real time capture.
	const bool bRealTimeReflectionCaptureSkyAtmosphereViewLutTexture = Views.Num() > 0 && Scene->SkyLight && Scene->SkyLight->bRealTimeCaptureEnabled;
	// CameraAP volume is required if there is a skydome or a volumetric cloud component rendered in a cubemap real time capture.
	const bool bRealTimeReflectionCapture360APLutTexture = Views.Num() > 0 && Scene->SkyLight && Scene->SkyLight->bRealTimeCaptureEnabled && (Views[0].bSceneHasSkyMaterial || Scene->HasVolumetricCloud());
	if (bRealTimeReflectionCaptureSkyAtmosphereViewLutTexture || bRealTimeReflectionCapture360APLutTexture)
	{
		FViewInfo& View = Views[0];
		const float AerialPerspectiveStartDepthInCm = GetValidAerialPerspectiveStartDepthInCm(View, SkyAtmosphereSceneProxy);

		FViewUniformShaderParameters ReflectionViewParameters = *View.CachedViewUniformShaderParameters;
		FViewMatrices ViewMatrices = View.ViewMatrices;
		ViewMatrices.HackRemoveTemporalAAProjectionJitter();
		ViewMatrices.UpdateViewMatrix(Scene->SkyLight->CapturePosition, FRotator());
		View.SetupCommonViewUniformBufferParameters(
			ReflectionViewParameters,
			View.ViewRect.Size(), 1,
			View.ViewRect,
			ViewMatrices,
			ViewMatrices
		);

		// LUTs still need to be pre-exposed as usual so we set reflection to 0.
		ReflectionViewParameters.RealTimeReflectionCapture = 0.0f;

		// Setup a constant referential for each of the faces of the dynamic reflection capture.
		const FAtmosphereSetup& AtmosphereSetup = SkyAtmosphereSceneProxy.GetAtmosphereSetup();
		const FVector SkyViewLutReferentialForward = FVector(1.0f, 0.0f, 0.0f);
		const FVector SkyViewLutReferentialRight = FVector(0.0f, 1.0f, 0.0f);
		AtmosphereSetup.ComputeViewData(Scene->SkyLight->CapturePosition, SkyViewLutReferentialForward, SkyViewLutReferentialRight,
			ReflectionViewParameters.SkyWorldCameraOrigin, ReflectionViewParameters.SkyPlanetCenterAndViewHeight, ReflectionViewParameters.SkyViewLutReferential);

		FVolumetricCloudRenderSceneInfo* CloudInfo = Scene->GetVolumetricCloudSceneInfo();
		FCloudShadowAOData CloudShadowAOData;
		GetCloudShadowAOData(CloudInfo, View, GraphBuilder, CloudShadowAOData);

		TUniformBufferRef<FViewUniformShaderParameters> ReflectionViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(ReflectionViewParameters, UniformBuffer_SingleFrame);

		if (bRealTimeReflectionCaptureSkyAtmosphereViewLutTexture)
		{
			FIntVector SkyViewLutSize = View.SkyAtmosphereViewLutTexture->GetDesc().GetSize();
			FRDGTextureRef RealTimeReflectionCaptureSkyAtmosphereViewLutTexture = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2DDesc(FIntPoint(SkyViewLutSize.X, SkyViewLutSize.Y), GetSkyLutTextureFormat(Scene->GetFeatureLevel()), FClearValueBinding::None,
					TexCreate_None, TexCreate_ShaderResource | TexCreate_UAV, false, 1), TEXT("RealTimeReflectionSkyViewLutTexture"));
			FRDGTextureUAVRef RealTimeReflectionCaptureSkyAtmosphereViewLutTextureUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RealTimeReflectionCaptureSkyAtmosphereViewLutTexture, 0));


			FRenderSkyViewLutCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FSampleCloudSkyAO>(CloudShadowAOData.bShouldSampleCloudSkyAO);
			PermutationVector.Set<FSecondAtmosphereLight>(bSecondAtmosphereLightEnabled);
			PermutationVector.Set<FSampleOpaqueShadow>(false); // bShouldSampleOpaqueShadow); Off for now to not have to generate Light0Shadow and Light1Shadow
			PermutationVector.Set<FSampleCloudShadow>(CloudShadowAOData.bShouldSampleCloudShadow);
			TShaderMapRef<FRenderSkyViewLutCS> ComputeShader(View.ShaderMap, PermutationVector);

			FRenderSkyViewLutCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRenderSkyViewLutCS::FParameters>();
			PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
			PassParameters->SkyAtmosphere = SkyInfo.GetInternalCommonParametersUniformBuffer();
			PassParameters->ViewUniformBuffer = ReflectionViewUniformBuffer;
			PassParameters->TransmittanceLutTextureSampler = SamplerLinearClamp;
			PassParameters->MultiScatteredLuminanceLutTextureSampler = SamplerLinearClamp;
			PassParameters->VolumetricCloudShadowMapTexture0Sampler = SamplerLinearClamp;
			PassParameters->VolumetricCloudShadowMapTexture1Sampler = SamplerLinearClamp;
			PassParameters->VolumetricCloudSkyAOTextureSampler = SamplerLinearClamp;
			PassParameters->TransmittanceLutTexture = TransmittanceLut;
			PassParameters->MultiScatteredLuminanceLutTexture = MultiScatteredLuminanceLut;
			PassParameters->VolumetricCloudShadowMapTexture0 = CloudShadowAOData.VolumetricCloudShadowMap[0];
			PassParameters->VolumetricCloudShadowMapTexture1 = CloudShadowAOData.VolumetricCloudShadowMap[1];
			PassParameters->VolumetricCloudSkyAOTexture = CloudShadowAOData.VolumetricCloudSkyAO;
			PassParameters->SkyViewLutUAV = RealTimeReflectionCaptureSkyAtmosphereViewLutTextureUAV;
			//PassParameters->Light0Shadow = nullptr;
			//PassParameters->Light1Shadow = nullptr;
			if (CloudShadowAOData.bShouldSampleCloudShadow || CloudShadowAOData.bShouldSampleCloudSkyAO)
			{
				PassParameters->VolumetricCloudCommonGlobalParams = CloudInfo->GetVolumetricCloudCommonShaderParametersUB();
			}
			PassParameters->SourceDiskEnabled = 0;

			FIntVector TextureSize = RealTimeReflectionCaptureSkyAtmosphereViewLutTexture->Desc.GetSize();
			TextureSize.Z = 1;
			const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize, FRenderSkyViewLutCS::GroupSize);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RealTimeCaptureSkyViewLut"), ComputeShader, PassParameters, NumGroups);

			GraphBuilder.QueueTextureExtraction(RealTimeReflectionCaptureSkyAtmosphereViewLutTexture, &Scene->RealTimeReflectionCaptureSkyAtmosphereViewLutTexture);
		}
		else
		{
			Scene->RealTimeReflectionCaptureSkyAtmosphereViewLutTexture = nullptr;
		}

		if (bRealTimeReflectionCapture360APLutTexture)
		{
			FIntVector CameraAPLutSize = View.SkyAtmosphereCameraAerialPerspectiveVolume->GetDesc().GetSize();
			EPixelFormat TextureAerialLUTFormat = (CVarSkyAtmosphereLUT32.GetValueOnAnyThread() != 0) ? PF_A32B32G32R32F : PF_FloatRGBA;
			FRDGTextureRef RealTimeReflectionCaptureCamera360APLutTexture = GraphBuilder.CreateTexture(
				FRDGTextureDesc::CreateVolumeDesc(CameraAPLutSize.X, CameraAPLutSize.Y, CameraAPLutSize.Z, TextureAerialLUTFormat, FClearValueBinding::None, 
					TexCreate_HideInVisualizeTexture, TexCreate_ShaderResource | TexCreate_UAV, false), TEXT("RealTimeReflectionCamera360APLutTexture"));
			FRDGTextureUAVRef RealTimeReflectionCaptureSkyAtmosphereViewLutTextureUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RealTimeReflectionCaptureCamera360APLutTexture, 0));

			FRenderCameraAerialPerspectiveVolumeCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FSampleCloudSkyAO>(CloudShadowAOData.bShouldSampleCloudSkyAO);
			PermutationVector.Set<FSecondAtmosphereLight>(bSecondAtmosphereLightEnabled);
			PermutationVector.Set<FSampleOpaqueShadow>(false); // bShouldSampleOpaqueShadow); Off for now to not have to generate Light0Shadow and Light1Shadow
			PermutationVector.Set<FSampleCloudShadow>(CloudShadowAOData.bShouldSampleCloudShadow);
			TShaderMapRef<FRenderCameraAerialPerspectiveVolumeCS> ComputeShader(View.ShaderMap, PermutationVector);

			FRenderCameraAerialPerspectiveVolumeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRenderCameraAerialPerspectiveVolumeCS::FParameters>();
			PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
			PassParameters->SkyAtmosphere = SkyInfo.GetInternalCommonParametersUniformBuffer();
			PassParameters->ViewUniformBuffer = ReflectionViewUniformBuffer;
			PassParameters->TransmittanceLutTextureSampler = SamplerLinearClamp;
			PassParameters->MultiScatteredLuminanceLutTextureSampler = SamplerLinearClamp;
			PassParameters->VolumetricCloudShadowMapTexture0Sampler = SamplerLinearClamp;
			PassParameters->VolumetricCloudShadowMapTexture1Sampler = SamplerLinearClamp;
			PassParameters->VolumetricCloudSkyAOTextureSampler = SamplerLinearClamp;
			PassParameters->TransmittanceLutTexture = TransmittanceLut;
			PassParameters->MultiScatteredLuminanceLutTexture = MultiScatteredLuminanceLut;
			PassParameters->VolumetricCloudShadowMapTexture0 = CloudShadowAOData.VolumetricCloudShadowMap[0];
			PassParameters->VolumetricCloudShadowMapTexture1 = CloudShadowAOData.VolumetricCloudShadowMap[1];
			PassParameters->VolumetricCloudSkyAOTexture = CloudShadowAOData.VolumetricCloudSkyAO;
			PassParameters->CameraAerialPerspectiveVolumeUAV = RealTimeReflectionCaptureSkyAtmosphereViewLutTextureUAV;
			PassParameters->AerialPerspectiveStartDepthKm = AerialPerspectiveStartDepthInCm * CM_TO_KM;
			PassParameters->RealTimeReflection360Mode = 1.0f;
			//PassParameters->Light0Shadow = nullptr;
			//PassParameters->Light1Shadow = nullptr;
			if (CloudShadowAOData.bShouldSampleCloudShadow || CloudShadowAOData.bShouldSampleCloudSkyAO)
			{
				PassParameters->VolumetricCloudCommonGlobalParams = CloudInfo->GetVolumetricCloudCommonShaderParametersUB();
			}

			FIntVector TextureSize = RealTimeReflectionCaptureCamera360APLutTexture->Desc.GetSize();
			const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize, FRenderCameraAerialPerspectiveVolumeCS::GroupSize);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RealTimeCaptureCamera360VolumeLut"), ComputeShader, PassParameters, NumGroups);

			GraphBuilder.QueueTextureExtraction(RealTimeReflectionCaptureCamera360APLutTexture, &Scene->RealTimeReflectionCaptureCamera360APLutTexture);
		}
		else
		{
			Scene->RealTimeReflectionCaptureCamera360APLutTexture = nullptr;
		}
	}
	else
	{
		Scene->RealTimeReflectionCaptureSkyAtmosphereViewLutTexture = nullptr;
		Scene->RealTimeReflectionCaptureCamera360APLutTexture = nullptr;
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		const float AerialPerspectiveStartDepthInCm = GetValidAerialPerspectiveStartDepthInCm(View, SkyAtmosphereSceneProxy);
		const bool bLightDiskEnabled = !View.bIsReflectionCapture;

		FRDGTextureRef SkyAtmosphereViewLutTexture = GraphBuilder.RegisterExternalTexture(View.SkyAtmosphereViewLutTexture);
		FRDGTextureUAVRef SkyAtmosphereViewLutTextureUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SkyAtmosphereViewLutTexture, 0));
		FRDGTextureRef SkyAtmosphereCameraAerialPerspectiveVolume = GraphBuilder.RegisterExternalTexture(View.SkyAtmosphereCameraAerialPerspectiveVolume);
		FRDGTextureUAVRef SkyAtmosphereCameraAerialPerspectiveVolumeUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SkyAtmosphereCameraAerialPerspectiveVolume, 0));

		TUniformBufferRef<FVolumeShadowingShaderParametersGlobal0> LightShadowShaderParams0UniformBuffer;
		TUniformBufferRef<FVolumeShadowingShaderParametersGlobal1> LightShadowShaderParams1UniformBuffer;
		GetSkyAtmosphereLightsUniformBuffers(LightShadowShaderParams0UniformBuffer, LightShadowShaderParams1UniformBuffer,
			LightShadowData, View, bShouldSampleOpaqueShadow, UniformBuffer_SingleFrame);

		FVolumetricCloudRenderSceneInfo* CloudInfo = Scene->GetVolumetricCloudSceneInfo();
		FCloudShadowAOData CloudShadowAOData;
		GetCloudShadowAOData(CloudInfo, View, GraphBuilder, CloudShadowAOData);

		// Sky View LUT
		{
			FRenderSkyViewLutCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FSampleCloudSkyAO>(CloudShadowAOData.bShouldSampleCloudSkyAO);
			PermutationVector.Set<FSecondAtmosphereLight>(bSecondAtmosphereLightEnabled);
			PermutationVector.Set<FSampleOpaqueShadow>(bShouldSampleOpaqueShadow);
			PermutationVector.Set<FSampleCloudShadow>(CloudShadowAOData.bShouldSampleCloudShadow);
			TShaderMapRef<FRenderSkyViewLutCS> ComputeShader(View.ShaderMap, PermutationVector);

			FRenderSkyViewLutCS::FParameters * PassParameters = GraphBuilder.AllocParameters<FRenderSkyViewLutCS::FParameters>();
			PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
			PassParameters->SkyAtmosphere = SkyInfo.GetInternalCommonParametersUniformBuffer();
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->TransmittanceLutTextureSampler = SamplerLinearClamp;
			PassParameters->MultiScatteredLuminanceLutTextureSampler = SamplerLinearClamp;
			PassParameters->VolumetricCloudShadowMapTexture0Sampler = SamplerLinearClamp;
			PassParameters->VolumetricCloudShadowMapTexture1Sampler = SamplerLinearClamp;
			PassParameters->VolumetricCloudSkyAOTextureSampler = SamplerLinearClamp;
			PassParameters->TransmittanceLutTexture = TransmittanceLut;
			PassParameters->MultiScatteredLuminanceLutTexture = MultiScatteredLuminanceLut;
			PassParameters->VolumetricCloudShadowMapTexture0 = CloudShadowAOData.VolumetricCloudShadowMap[0];
			PassParameters->VolumetricCloudShadowMapTexture1 = CloudShadowAOData.VolumetricCloudShadowMap[1];
			PassParameters->VolumetricCloudSkyAOTexture = CloudShadowAOData.VolumetricCloudSkyAO;
			PassParameters->SkyViewLutUAV = SkyAtmosphereViewLutTextureUAV;
			PassParameters->Light0Shadow = LightShadowShaderParams0UniformBuffer;
			PassParameters->Light1Shadow = LightShadowShaderParams1UniformBuffer;
			if (CloudShadowAOData.bShouldSampleCloudShadow || CloudShadowAOData.bShouldSampleCloudSkyAO)
			{
				PassParameters->VolumetricCloudCommonGlobalParams = CloudInfo->GetVolumetricCloudCommonShaderParametersUB();
			}
			PassParameters->SourceDiskEnabled = bLightDiskEnabled ? 1 : 0;

			FIntVector TextureSize = SkyAtmosphereViewLutTexture->Desc.GetSize();
			TextureSize.Z = 1;
			const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize, FRenderSkyViewLutCS::GroupSize);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SkyViewLut"), ComputeShader, PassParameters, NumGroups);
		}

		// Camera Atmosphere Volume
		{
			FRenderCameraAerialPerspectiveVolumeCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FSampleCloudSkyAO>(CloudShadowAOData.bShouldSampleCloudSkyAO);
			PermutationVector.Set<FSecondAtmosphereLight>(bSecondAtmosphereLightEnabled);
			PermutationVector.Set<FSampleOpaqueShadow>(bShouldSampleOpaqueShadow);
			PermutationVector.Set<FSampleCloudShadow>(CloudShadowAOData.bShouldSampleCloudShadow);
			TShaderMapRef<FRenderCameraAerialPerspectiveVolumeCS> ComputeShader(View.ShaderMap, PermutationVector);

			FRenderCameraAerialPerspectiveVolumeCS::FParameters * PassParameters = GraphBuilder.AllocParameters<FRenderCameraAerialPerspectiveVolumeCS::FParameters>();
			PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
			PassParameters->SkyAtmosphere = SkyInfo.GetInternalCommonParametersUniformBuffer();
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->TransmittanceLutTextureSampler = SamplerLinearClamp;
			PassParameters->MultiScatteredLuminanceLutTextureSampler = SamplerLinearClamp;
			PassParameters->VolumetricCloudShadowMapTexture0Sampler = SamplerLinearClamp;
			PassParameters->VolumetricCloudShadowMapTexture1Sampler = SamplerLinearClamp;
			PassParameters->VolumetricCloudSkyAOTextureSampler = SamplerLinearClamp;
			PassParameters->TransmittanceLutTexture = TransmittanceLut;
			PassParameters->MultiScatteredLuminanceLutTexture = MultiScatteredLuminanceLut;
			PassParameters->VolumetricCloudShadowMapTexture0 = CloudShadowAOData.VolumetricCloudShadowMap[0];
			PassParameters->VolumetricCloudShadowMapTexture1 = CloudShadowAOData.VolumetricCloudShadowMap[1];
			PassParameters->VolumetricCloudSkyAOTexture = CloudShadowAOData.VolumetricCloudSkyAO;
			PassParameters->CameraAerialPerspectiveVolumeUAV = SkyAtmosphereCameraAerialPerspectiveVolumeUAV;
			PassParameters->AerialPerspectiveStartDepthKm = AerialPerspectiveStartDepthInCm * CM_TO_KM;
			PassParameters->RealTimeReflection360Mode = 0.0f;
			PassParameters->Light0Shadow = LightShadowShaderParams0UniformBuffer;
			PassParameters->Light1Shadow = LightShadowShaderParams1UniformBuffer;
			if (CloudShadowAOData.bShouldSampleCloudShadow || CloudShadowAOData.bShouldSampleCloudSkyAO)
			{
				PassParameters->VolumetricCloudCommonGlobalParams = CloudInfo->GetVolumetricCloudCommonShaderParametersUB();
			}

			FIntVector TextureSize = SkyAtmosphereCameraAerialPerspectiveVolume->Desc.GetSize();
			const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize, FRenderCameraAerialPerspectiveVolumeCS::GroupSize);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CameraVolumeLut"), ComputeShader, PassParameters, NumGroups);
		}

		GraphBuilder.QueueTextureExtraction(SkyAtmosphereViewLutTexture, &View.SkyAtmosphereViewLutTexture, true);
		GraphBuilder.QueueTextureExtraction(SkyAtmosphereCameraAerialPerspectiveVolume, &View.SkyAtmosphereCameraAerialPerspectiveVolume, true);
	}
	
	GraphBuilder.Execute();
	// TODO have RDG execute those above passes with compute overlap similarly to using AutomaticCacheFlushAfterComputeShader(true);
}

FSkyAtmosphereRenderContext::FSkyAtmosphereRenderContext()
{
	bAPOnCloudMode = false;
	VolumetricCloudDepthTexture = nullptr;
	InputCloudLuminanceTransmittanceTexture = nullptr;
}

void FSceneRenderer::RenderSkyAtmosphereInternal(FRDGBuilder& GraphBuilder, FSkyAtmosphereRenderContext& SkyRC)
{
	check(Scene->HasSkyAtmosphere());

	FSkyAtmosphereRenderSceneInfo& SkyInfo = *Scene->GetSkyAtmosphereSceneInfo();
	const FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy = SkyInfo.GetSkyAtmosphereSceneProxy();
	const FAtmosphereSetup& Atmosphere = SkyAtmosphereSceneProxy.GetAtmosphereSetup();

	const FViewMatrices& ViewMatrices = *SkyRC.ViewMatrices;

	FRHISamplerState* SamplerLinearClamp = TStaticSamplerState<SF_Trilinear>::GetRHI();
	const float AerialPerspectiveStartDepthInCm = SkyRC.AerialPerspectiveStartDepthInCm;

	const FVector ViewOrigin = ViewMatrices.GetViewOrigin();
	const FVector PlanetCenter = Atmosphere.PlanetCenterKm * KM_TO_CM;
	const float TopOfAtmosphere = Atmosphere.TopRadiusKm * KM_TO_CM;
	const float SafeEdge = 1000.0f;	// 10 meters
	const bool ForceRayMarching = SkyRC.bForceRayMarching || (FVector::Distance(ViewOrigin, PlanetCenter) - TopOfAtmosphere - SafeEdge) > 0.0f;
	const bool bDisableBlending = SkyRC.bDisableBlending;

	// Render the sky, and optionally the atmosphere aerial perspective, on the scene luminance buffer
	{
		FRenderSkyAtmospherePS::FPermutationDomain PsPermutationVector;
		PsPermutationVector.Set<FSampleCloudSkyAO>(SkyRC.bShouldSampleCloudSkyAO);
		PsPermutationVector.Set<FFastSky>(SkyRC.bFastSky && !ForceRayMarching);
		PsPermutationVector.Set<FFastAerialPespective>(SkyRC.bFastAerialPerspective && !ForceRayMarching);
		PsPermutationVector.Set<FSecondAtmosphereLight>(SkyRC.bSecondAtmosphereLightEnabled);
		PsPermutationVector.Set<FRenderSky>(SkyRC.bRenderSkyPixel);
		PsPermutationVector.Set<FSampleOpaqueShadow>(SkyRC.bShouldSampleOpaqueShadow);
		PsPermutationVector.Set<FSampleCloudShadow>(SkyRC.bShouldSampleCloudShadow);
		PsPermutationVector.Set<FAtmosphereOnClouds>(SkyRC.bAPOnCloudMode);
		PsPermutationVector = FRenderSkyAtmospherePS::RemapPermutation(PsPermutationVector);
		TShaderMapRef<FRenderSkyAtmospherePS> PixelShader(GetGlobalShaderMap(SkyRC.FeatureLevel), PsPermutationVector);

		FRenderSkyAtmosphereVS::FPermutationDomain VsPermutationVector;
		TShaderMapRef<FRenderSkyAtmosphereVS> VertexShader(GetGlobalShaderMap(SkyRC.FeatureLevel), VsPermutationVector);

		FRenderSkyAtmospherePS::FParameters* PsPassParameters = GraphBuilder.AllocParameters<FRenderSkyAtmospherePS::FParameters>();
		PsPassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
		PsPassParameters->SkyAtmosphere = SkyInfo.GetInternalCommonParametersUniformBuffer();
		PsPassParameters->ViewUniformBuffer = SkyRC.ViewUniformBuffer;
		PsPassParameters->RenderTargets = SkyRC.RenderTargets;
		PsPassParameters->SceneTextures = CreateSceneTextureUniformBufferSingleDraw(GraphBuilder.RHICmdList, SkyRC.bDepthReadDisabled ? ESceneTextureSetupMode::None : ESceneTextureSetupMode::All, SkyRC.FeatureLevel);
		PsPassParameters->TransmittanceLutTextureSampler = SamplerLinearClamp;
		PsPassParameters->MultiScatteredLuminanceLutTextureSampler = SamplerLinearClamp;
		PsPassParameters->SkyViewLutTextureSampler = SamplerLinearClamp;
		PsPassParameters->CameraAerialPerspectiveVolumeTextureSampler = SamplerLinearClamp;
		PsPassParameters->VolumetricCloudShadowMapTexture0Sampler = SamplerLinearClamp;
		PsPassParameters->VolumetricCloudShadowMapTexture1Sampler = SamplerLinearClamp;
		PsPassParameters->VolumetricCloudSkyAOTextureSampler = SamplerLinearClamp;
		PsPassParameters->TransmittanceLutTexture = SkyRC.TransmittanceLut;
		PsPassParameters->MultiScatteredLuminanceLutTexture = SkyRC.MultiScatteredLuminanceLut;
		PsPassParameters->SkyViewLutTexture = SkyRC.SkyAtmosphereViewLutTexture;
		PsPassParameters->CameraAerialPerspectiveVolumeTexture = SkyRC.SkyAtmosphereCameraAerialPerspectiveVolume;
		PsPassParameters->VolumetricCloudShadowMapTexture0 = SkyRC.VolumetricCloudShadowMap[0];
		PsPassParameters->VolumetricCloudShadowMapTexture1 = SkyRC.VolumetricCloudShadowMap[1];
		PsPassParameters->VolumetricCloudSkyAOTexture = SkyRC.VolumetricCloudSkyAO;
		PsPassParameters->VolumetricCloudDepthTexture = SkyRC.VolumetricCloudDepthTexture;
		PsPassParameters->InputCloudLuminanceTransmittanceTexture = SkyRC.InputCloudLuminanceTransmittanceTexture;
		PsPassParameters->AerialPerspectiveStartDepthKm = AerialPerspectiveStartDepthInCm * CM_TO_KM;
		PsPassParameters->SourceDiskEnabled = SkyRC.bLightDiskEnabled ? 1 : 0;
		PsPassParameters->DepthReadDisabled = SkyRC.bDepthReadDisabled ? 1 : 0;
		if (SkyRC.bShouldSampleCloudShadow || SkyRC.bShouldSampleCloudSkyAO)
		{
			PsPassParameters->VolumetricCloudCommonGlobalParams = Scene->GetVolumetricCloudSceneInfo()->GetVolumetricCloudCommonShaderParametersUB();
		}

		PsPassParameters->Light0Shadow = SkyRC.LightShadowShaderParams0UniformBuffer;
		PsPassParameters->Light1Shadow = SkyRC.LightShadowShaderParams1UniformBuffer;

		ClearUnusedGraphResources(PixelShader, PsPassParameters);

		float StartDepthZ = 0.1f;
		if (SkyRC.bFastAerialPerspectiveDepthTest)
		{
			const FMatrix ProjectionMatrix = ViewMatrices.GetProjectionMatrix();
			float HalfHorizontalFOV = FMath::Atan(1.0f / ProjectionMatrix.M[0][0]);
			float HalfVerticalFOV = FMath::Atan(1.0f / ProjectionMatrix.M[1][1]);
			float StartDepthViewCm = FMath::Cos(FMath::Max(HalfHorizontalFOV, HalfVerticalFOV)) * AerialPerspectiveStartDepthInCm;
			StartDepthViewCm = FMath::Max(StartDepthViewCm, SkyRC.NearClippingDistance); // In any case, we need to limit the distance to frustum near plane to not be clipped away.
			const FVector4 Projected = ProjectionMatrix.TransformFVector4(FVector4(0.0f, 0.0f, StartDepthViewCm, 1.0f));
			StartDepthZ = Projected.Z / Projected.W;
		}

		const bool bFastAerialPerspectiveDepthTest = SkyRC.bFastAerialPerspectiveDepthTest;
		const bool bRenderSkyPixel = SkyRC.bRenderSkyPixel;
		FIntRect Viewport = SkyRC.Viewport;
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SkyAtmosphereDraw"),
			PsPassParameters,
			ERDGPassFlags::Raster,
			[PsPassParameters, VertexShader, PixelShader, Viewport, bFastAerialPerspectiveDepthTest, bRenderSkyPixel, bDisableBlending, StartDepthZ](FRHICommandList& RHICmdListLambda)
		{
			RHICmdListLambda.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdListLambda.ApplyCachedRenderTargets(GraphicsPSOInit);

			if (bDisableBlending)
			{
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			}
			else
			{
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
			}
			if (bFastAerialPerspectiveDepthTest)
			{
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
			}
			else
			{
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			}
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			if (!bRenderSkyPixel && GSupportsDepthBoundsTest)
			{
				// When we do not render the sky in the sky pass and depth bound test is supported, we take advantage of it in order to skip the processing of sky pixels.
				GraphicsPSOInit.bDepthBounds = true;
				if (bool(ERHIZBuffer::IsInverted))
				{
					//const float SmallestFloatAbove0 = 1.1754943508e-38;		// 32bit float depth
					const float SmallestFloatAbove0 = 1.0f / 16777215.0f;		// 24bit norm depth
					RHICmdListLambda.SetDepthBounds(SmallestFloatAbove0, 1.0f);	// Tested on dx12 PC
				}
				else
				{
					//const float SmallestFloatBelow1 = 0.9999999404;			// 32bit float depth
					const float SmallestFloatBelow1 = 16777214.0f / 16777215.0f;// 24bit norm depth
					RHICmdListLambda.SetDepthBounds(0.0f, SmallestFloatBelow1);	// Untested
				}
			}

			SetGraphicsPipelineState(RHICmdListLambda, GraphicsPSOInit);

			SetShaderParameters(RHICmdListLambda, PixelShader, PixelShader.GetPixelShader(), *PsPassParameters);

			FRenderSkyAtmosphereVS::FParameters VsPassParameters;
			VsPassParameters.StartDepthZ = StartDepthZ;
			SetShaderParameters(RHICmdListLambda, VertexShader, VertexShader.GetVertexShader(), VsPassParameters);

			RHICmdListLambda.DrawPrimitive(0, 1, 1);
		});
	}
}



void FSceneRenderer::RenderSkyAtmosphere(FRHICommandListImmediate& RHICmdList)
{
	check(!IsMobilePlatform(Scene->GetShaderPlatform()));

	check(ShouldRenderSkyAtmosphere(Scene, ViewFamily.EngineShowFlags)); // This should not be called if we should not render SkyAtmosphere

	SCOPED_DRAW_EVENT(RHICmdList, SkyAtmosphere);
	SCOPED_GPU_STAT(RHICmdList, SkyAtmosphere);

	FSkyAtmosphereRenderSceneInfo& SkyInfo = *Scene->GetSkyAtmosphereSceneInfo();
	const FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy = SkyInfo.GetSkyAtmosphereSceneProxy();
	FVolumetricCloudRenderSceneInfo* CloudInfo = Scene->GetVolumetricCloudSceneInfo();

	FSkyAtmosphereRenderContext SkyRC;
	SkyRC.ViewMatrices = nullptr;

	const FAtmosphereSetup& Atmosphere = SkyAtmosphereSceneProxy.GetAtmosphereSetup();
	SkyRC.bFastSky = CVarSkyAtmosphereFastSkyLUT.GetValueOnRenderThread() > 0;
	SkyRC.bFastAerialPerspective = CVarSkyAtmosphereAerialPerspectiveApplyOnOpaque.GetValueOnRenderThread() > 0;
	SkyRC.bFastAerialPerspectiveDepthTest = CVarSkyAtmosphereAerialPerspectiveDepthTest.GetValueOnRenderThread() > 0;
	SkyRC.bSecondAtmosphereLightEnabled = Scene->IsSecondAtmosphereLightEnabled();

	SkyAtmosphereLightShadowData LightShadowData;
	SkyRC.bShouldSampleOpaqueShadow = ShouldSkySampleAtmosphereLightsOpaqueShadow(*Scene, VisibleLightInfos, LightShadowData);
	SkyRC.bUseDepthBoundTestIfPossible = true;
	SkyRC.bForceRayMarching = false;
	SkyRC.bDepthReadDisabled = false;
	SkyRC.bDisableBlending = false;

	FRDGBuilder GraphBuilder(RHICmdList);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FRDGTextureRef SceneColor = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor(), TEXT("SceneColor"));
	FRDGTextureRef SceneDepth = GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ, TEXT("SceneDepth"));

	SkyRC.TransmittanceLut = GraphBuilder.RegisterExternalTexture(SkyInfo.GetTransmittanceLutTexture());
	SkyRC.MultiScatteredLuminanceLut = GraphBuilder.RegisterExternalTexture(SkyInfo.GetMultiScatteredLuminanceLutTexture());

	SkyRC.RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
	SkyRC.RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthRead_StencilNop);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		SkyRC.ViewMatrices = &View.ViewMatrices;
		SkyRC.ViewUniformBuffer = View.ViewUniformBuffer;

		SkyRC.Viewport = View.ViewRect;
		SkyRC.bLightDiskEnabled = !View.bIsReflectionCapture;
		SkyRC.AerialPerspectiveStartDepthInCm = GetValidAerialPerspectiveStartDepthInCm(View, SkyAtmosphereSceneProxy);
		SkyRC.NearClippingDistance = View.NearClippingDistance;
		SkyRC.FeatureLevel = View.FeatureLevel;

		// If the scene contains Sky material then it is likely rendering the sky using a sky dome mesh.
		// In this case we can use a simpler shader during this pass to only render aerial perspective
		// and sky pixels can likely be optimised out.
		SkyRC.bRenderSkyPixel = !View.bSceneHasSkyMaterial;

		SkyRC.SkyAtmosphereViewLutTexture = GraphBuilder.RegisterExternalTexture(View.SkyAtmosphereViewLutTexture);
		SkyRC.SkyAtmosphereCameraAerialPerspectiveVolume = GraphBuilder.RegisterExternalTexture(View.SkyAtmosphereCameraAerialPerspectiveVolume);

		GetSkyAtmosphereLightsUniformBuffers(SkyRC.LightShadowShaderParams0UniformBuffer, SkyRC.LightShadowShaderParams1UniformBuffer,
			LightShadowData, View, SkyRC.bShouldSampleOpaqueShadow, UniformBuffer_SingleDraw);

		FCloudShadowAOData CloudShadowAOData;
		GetCloudShadowAOData(CloudInfo, View, GraphBuilder, CloudShadowAOData);
		SkyRC.bShouldSampleCloudShadow = CloudShadowAOData.bShouldSampleCloudShadow;
		SkyRC.VolumetricCloudShadowMap[0] = CloudShadowAOData.VolumetricCloudShadowMap[0];
		SkyRC.VolumetricCloudShadowMap[1] = CloudShadowAOData.VolumetricCloudShadowMap[1];
		SkyRC.bShouldSampleCloudSkyAO = CloudShadowAOData.bShouldSampleCloudSkyAO;
		SkyRC.VolumetricCloudSkyAO = CloudShadowAOData.VolumetricCloudSkyAO;

		RenderSkyAtmosphereInternal(GraphBuilder, SkyRC);
	}

	GraphBuilder.Execute();

#if WITH_EDITOR
	if (CVarSkyAtmosphereFastSkyLUT.GetValueOnAnyThread() == 0 && CVarSkyAtmosphereAerialPerspectiveApplyOnOpaque.GetValueOnAnyThread() > 0)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];
			const float ViewPortWidth = float(View.ViewRect.Width());
			const float ViewPortHeight = float(View.ViewRect.Height());

			FRenderTargetTemp TempRenderTarget(View, (const FTexture2DRHIRef&)SceneContext.GetSceneColor()->GetRenderTargetItem().TargetableTexture);
			FCanvas Canvas(&TempRenderTarget, NULL, View.Family->CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, View.GetFeatureLevel());

			FLinearColor TextColor(1.0f, 0.5f, 0.0f);
			FString Text = TEXT("You are using a FastAerialPespective without FastSky, visuals might look wrong.");
			Canvas.DrawShadowedString(ViewPortWidth*0.5f - Text.Len()*7.0f, ViewPortHeight*0.4f, *Text, GetStatsFont(), TextColor);

			Canvas.Flush_RenderThread(RHICmdList);
		}
	}
#endif
}

bool FSceneRenderer::ShouldRenderSkyAtmosphereEditorNotifications()
{
#if WITH_EDITOR
	bool bAnyViewHasSkyMaterial = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		bAnyViewHasSkyMaterial |= Views[ViewIndex].bSceneHasSkyMaterial;
	}
	return bAnyViewHasSkyMaterial;
#endif
	return false;
}

void FSceneRenderer::RenderSkyAtmosphereEditorNotifications(FRHICommandListImmediate& RHICmdList)
{
#if WITH_EDITOR
	SCOPED_DRAW_EVENT(RHICmdList, SkyAtmosphereEditor);
	SCOPED_GPU_STAT(RHICmdList, SkyAtmosphereEditor);

	FRDGBuilder GraphBuilder(RHICmdList);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FTexture2DRHIRef& SceneColor = (FTexture2DRHIRef&)SceneContext.GetSceneColor()->GetRenderTargetItem().TargetableTexture;
	FRDGTextureRef RdgSceneColor = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor(), TEXT("SceneColor"));

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		if (View.bSceneHasSkyMaterial && View.Family->EngineShowFlags.Atmosphere)
		{
			RenderSkyAtmosphereEditorHudPS::FPermutationDomain PermutationVector;
			TShaderMapRef<RenderSkyAtmosphereEditorHudPS> PixelShader(View.ShaderMap, PermutationVector);

			RenderSkyAtmosphereEditorHudPS::FParameters* PassParameters = GraphBuilder.AllocParameters<RenderSkyAtmosphereEditorHudPS::FParameters>();
			PassParameters->RenderTargets[0] = FRenderTargetBinding(RdgSceneColor, ERenderTargetLoadAction::ELoad);
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->MiniFontTexture = GEngine->MiniFontTexture ? GEngine->MiniFontTexture->Resource->TextureRHI : GSystemTextures.WhiteDummy->GetRenderTargetItem().TargetableTexture;

			FPixelShaderUtils::AddFullscreenPass<RenderSkyAtmosphereEditorHudPS>(GraphBuilder, View.ShaderMap, RDG_EVENT_NAME("SkyAtmosphereEditor"), PixelShader, PassParameters, View.ViewRect);
		}
	}

	GraphBuilder.Execute();
#endif
}



/*=============================================================================
	FDeferredShadingSceneRenderer functions
=============================================================================*/



void FDeferredShadingSceneRenderer::RenderDebugSkyAtmosphere(FRHICommandListImmediate& RHICmdList)
{
#if WITH_EDITOR
	check(ShouldRenderSkyAtmosphere(Scene, ViewFamily.EngineShowFlags)); // This should not be called if we should not render SkyAtmosphere

	//if (!RHISupportsComputeShaders()) return;	// TODO cannot render, add a ShouldRender function. Also should PipelineShouldCook ?

	SCOPED_DRAW_EVENT(RHICmdList, SkyAtmosphereDebugVisualize);
	SCOPED_GPU_STAT(RHICmdList, SkyAtmosphereDebugVisualize);

	const bool bSkyAtmosphereVisualizeShowFlag = ViewFamily.EngineShowFlags.VisualizeSkyAtmosphere;
	FSkyAtmosphereRenderSceneInfo& SkyInfo = *Scene->GetSkyAtmosphereSceneInfo();
	const FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy = SkyInfo.GetSkyAtmosphereSceneProxy();

	const FAtmosphereSetup& Atmosphere = SkyAtmosphereSceneProxy.GetAtmosphereSetup();
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	if (bSkyAtmosphereVisualizeShowFlag)
	{
		const bool bFastSky = CVarSkyAtmosphereFastSkyLUT.GetValueOnRenderThread() > 0;
		const bool bFastAerialPespective = CVarSkyAtmosphereAerialPerspectiveApplyOnOpaque.GetValueOnRenderThread() > 0;

		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef SceneColor = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor(), TEXT("SceneColor"));
		FRDGTextureRef SceneDepth = GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ, TEXT("SceneDepth"));

		FRDGTextureRef TransmittanceLut = GraphBuilder.RegisterExternalTexture(SkyInfo.GetTransmittanceLutTexture());
		FRDGTextureRef MultiScatteredLuminanceLut = GraphBuilder.RegisterExternalTexture(SkyInfo.GetMultiScatteredLuminanceLutTexture());

		FRHIBlendState* PreMultipliedColorTransmittanceBlend = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
		FRHIDepthStencilState* DepthStencilStateWrite = TStaticDepthStencilState<true, CF_Always>::GetRHI();
		FRHISamplerState* SamplerLinearClamp = TStaticSamplerState<SF_Trilinear>::GetRHI();

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];

			// Render the sky and atmosphere on the scene luminance buffer
			{
				FRenderDebugSkyAtmospherePS::FPermutationDomain PermutationVector;
				TShaderMapRef<FRenderDebugSkyAtmospherePS> PixelShader(View.ShaderMap, PermutationVector);

				FRenderDebugSkyAtmospherePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRenderDebugSkyAtmospherePS::FParameters>();
				PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
				PassParameters->SkyAtmosphere = SkyInfo.GetInternalCommonParametersUniformBuffer();
				PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
				PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
				PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthWrite_StencilNop);
				PassParameters->TransmittanceLutTextureSampler = SamplerLinearClamp;
				PassParameters->MultiScatteredLuminanceLutTextureSampler = SamplerLinearClamp;
				PassParameters->TransmittanceLutTexture = TransmittanceLut;
				PassParameters->MultiScatteredLuminanceLutTexture = MultiScatteredLuminanceLut;
				PassParameters->ViewPortWidth = float(View.ViewRect.Width());
				PassParameters->ViewPortHeight = float(View.ViewRect.Height());

				FPixelShaderUtils::AddFullscreenPass<FRenderDebugSkyAtmospherePS>(GraphBuilder, View.ShaderMap, RDG_EVENT_NAME("SkyAtmosphere"), PixelShader, PassParameters,
					View.ViewRect, PreMultipliedColorTransmittanceBlend, nullptr, DepthStencilStateWrite);
			}
		}
		GraphBuilder.Execute();
	}

	// Now debug print
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		const float ViewPortWidth = float(View.ViewRect.Width());
		const float ViewPortHeight = float(View.ViewRect.Height());

		FRenderTargetTemp TempRenderTarget(View, (const FTexture2DRHIRef&)SceneContext.GetSceneColor()->GetRenderTargetItem().TargetableTexture);
		FCanvas Canvas(&TempRenderTarget, NULL, View.Family->CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, View.GetFeatureLevel());

		FLinearColor TextColor(FLinearColor::White);
		FLinearColor GrayTextColor(FLinearColor::Gray);
		FLinearColor WarningColor(1.0f, 0.5f, 0.0f);
		FString Text;

		if (bSkyAtmosphereVisualizeShowFlag)
		{
			const float ViewPlanetAltitude = (View.ViewLocation*FAtmosphereSetup::CmToSkyUnit - Atmosphere.PlanetCenterKm).Size() - Atmosphere.BottomRadiusKm;
			const bool bViewUnderGroundLevel = ViewPlanetAltitude < 0.0f;
			if (bViewUnderGroundLevel)
			{
				Text = FString::Printf(TEXT("SkyAtmosphere: View is %.3f km under the planet ground level!"), -ViewPlanetAltitude);
				Canvas.DrawShadowedString(ViewPortWidth*0.5 - 250.0f, ViewPortHeight*0.5f, *Text, GetStatsFont(), WarningColor);
			}

			// This needs to stay in sync with RenderSkyAtmosphereDebugPS.
			const float DensityViewTop = ViewPortHeight * 0.1f;
			const float DensityViewBottom = ViewPortHeight * 0.8f;
			const float DensityViewLeft = ViewPortWidth * 0.8f;
			const float Margin = 2.0f;
			const float TimeOfDayViewHeight = 64.0f;
			const float TimeOfDayViewTop = ViewPortHeight - (TimeOfDayViewHeight + Margin * 2.0);
			const float HemiViewHeight = ViewPortWidth * 0.25f;
			const float HemiViewTop = ViewPortHeight - HemiViewHeight - TimeOfDayViewHeight - Margin * 2.0;

			Text = FString::Printf(TEXT("Atmosphere top = %.1f km"), Atmosphere.TopRadiusKm - Atmosphere.BottomRadiusKm);
			Canvas.DrawShadowedString(DensityViewLeft, DensityViewTop, *Text, GetStatsFont(), TextColor);
			Text = FString::Printf(TEXT("Rayleigh extinction"));
			Canvas.DrawShadowedString(DensityViewLeft + 60.0f, DensityViewTop + 30.0f, *Text, GetStatsFont(), FLinearColor(FLinearColor::Red));
			Text = FString::Printf(TEXT("Mie extinction"));
			Canvas.DrawShadowedString(DensityViewLeft + 60.0f, DensityViewTop + 45.0f, *Text, GetStatsFont(), FLinearColor(FLinearColor::Green));
			Text = FString::Printf(TEXT("Absorption"));
			Canvas.DrawShadowedString(DensityViewLeft + 60.0f, DensityViewTop + 60.0f, *Text, GetStatsFont(), FLinearColor(FLinearColor::Blue));
			Text = FString::Printf(TEXT("<=== Low visual contribution"));
			Canvas.DrawShadowedString(DensityViewLeft + 2.0, DensityViewTop + 150.0f, *Text, GetStatsFont(), GrayTextColor);
			Text = FString::Printf(TEXT("High visual contribution ===>"));
			Canvas.DrawShadowedString(ViewPortWidth - 170.0f, DensityViewTop + 166.0f, *Text, GetStatsFont(), GrayTextColor);
			Text = FString::Printf(TEXT("Ground level"));
			Canvas.DrawShadowedString(DensityViewLeft, DensityViewBottom, *Text, GetStatsFont(), TextColor);

			Text = FString::Printf(TEXT("Time-of-day preview"));
			Canvas.DrawShadowedString(ViewPortWidth*0.5f - 80.0f, TimeOfDayViewTop, *Text, GetStatsFont(), TextColor);

			Text = FString::Printf(TEXT("Hemisphere view"));
			Canvas.DrawShadowedString(Margin, HemiViewTop, *Text, GetStatsFont(), TextColor);

			Canvas.Flush_RenderThread(RHICmdList);
		}
	}

#endif
}


