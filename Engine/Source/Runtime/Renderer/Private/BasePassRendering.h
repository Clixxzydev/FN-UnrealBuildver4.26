// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	BasePassRendering.h: Base pass rendering definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "HitProxies.h"
#include "RHIStaticStates.h"
#include "SceneManagement.h"
#include "Materials/Material.h"
#include "PostProcess/SceneRenderTargets.h"
#include "LightMapRendering.h"
#include "VelocityRendering.h"
#include "MeshMaterialShaderType.h"
#include "MeshMaterialShader.h"
#include "ShaderBaseClasses.h"
#include "FogRendering.h"
#include "PlanarReflectionRendering.h"
#include "UnrealEngine.h"
#include "ReflectionEnvironment.h"

class FScene;

template<typename TBufferStruct> class TUniformBufferRef;

class FViewInfo;
class UMaterialExpressionSingleLayerWaterMaterialOutput;

/** Whether to allow the indirect lighting cache to be applied to dynamic objects. */
extern int32 GIndirectLightingCache;

class FForwardLocalLightData
{
public:
	FVector4 LightPositionAndInvRadius;
	FVector4 LightColorAndFalloffExponent;
	FVector4 LightDirectionAndShadowMapChannelMask;
	FVector4 SpotAnglesAndSourceRadiusPacked;
	FVector4 LightTangentAndSoftSourceRadius;
	FVector4 RectBarnDoor;
};

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSharedBasePassUniformParameters,)
	SHADER_PARAMETER_STRUCT(FForwardLightData, Forward)
	SHADER_PARAMETER_STRUCT(FForwardLightData, ForwardISR)
	SHADER_PARAMETER_STRUCT(FReflectionUniformParameters, Reflection)
	SHADER_PARAMETER_STRUCT(FPlanarReflectionUniformParameters, PlanarReflection) // Single global planar reflection for the forward pass.
	SHADER_PARAMETER_STRUCT(FFogUniformParameters, Fog)
	SHADER_PARAMETER_STRUCT(FFogUniformParameters, FogISR)
	SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FOpaqueBasePassUniformParameters,)
	SHADER_PARAMETER_STRUCT(FSharedBasePassUniformParameters, Shared)
	// Forward shading 
	SHADER_PARAMETER(int32, UseForwardScreenSpaceShadowMask)
	SHADER_PARAMETER_TEXTURE(Texture2D, ForwardScreenSpaceShadowMaskTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, IndirectOcclusionTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, ResolvedSceneDepthTexture)
	// DBuffer decals
	SHADER_PARAMETER_TEXTURE(Texture2D, DBufferATexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, DBufferATextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, DBufferBTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, DBufferBTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, DBufferCTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, DBufferCTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint>, DBufferRenderMask)
	// Single Layer Water
	SHADER_PARAMETER_TEXTURE(Texture2D, SceneColorWithoutSingleLayerWaterTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorWithoutSingleLayerWaterSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, SceneDepthWithoutSingleLayerWaterTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthWithoutSingleLayerWaterSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGFTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)
	SHADER_PARAMETER(FVector4, SceneWithoutSingleLayerWaterMinMaxUV)
	SHADER_PARAMETER(FVector4, DistortionParams)
	// Misc
	SHADER_PARAMETER_TEXTURE(Texture2D, EyeAdaptation)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FTranslucentBasePassUniformParameters,)
	SHADER_PARAMETER_STRUCT(FSharedBasePassUniformParameters, Shared)
	SHADER_PARAMETER_STRUCT(FSceneTexturesUniformParameters, SceneTextures)
	// Material SSR
	SHADER_PARAMETER(FVector4, HZBUvFactorAndInvFactor)
	SHADER_PARAMETER(FVector4, PrevScreenPositionScaleBias)
	SHADER_PARAMETER(float, PrevSceneColorPreExposureInv)
	SHADER_PARAMETER_TEXTURE(Texture2D, HZBTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, PrevSceneColor)
	SHADER_PARAMETER_SAMPLER(SamplerState, PrevSceneColorSampler)
	// Translucency Lighting Volume
	SHADER_PARAMETER_TEXTURE(Texture3D, TranslucencyLightingVolumeAmbientInner)
	SHADER_PARAMETER_SAMPLER(SamplerState, TranslucencyLightingVolumeAmbientInnerSampler)
	SHADER_PARAMETER_TEXTURE(Texture3D, TranslucencyLightingVolumeAmbientOuter)
	SHADER_PARAMETER_SAMPLER(SamplerState, TranslucencyLightingVolumeAmbientOuterSampler)
	SHADER_PARAMETER_TEXTURE(Texture3D, TranslucencyLightingVolumeDirectionalInner)
	SHADER_PARAMETER_SAMPLER(SamplerState, TranslucencyLightingVolumeDirectionalInnerSampler)
	SHADER_PARAMETER_TEXTURE(Texture3D, TranslucencyLightingVolumeDirectionalOuter)
	SHADER_PARAMETER_SAMPLER(SamplerState, TranslucencyLightingVolumeDirectionalOuterSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()


DECLARE_GPU_DRAWCALL_STAT_EXTERN(Basepass);

extern FTextureRHIRef& GetEyeAdaptation(const FViewInfo& View);

extern void SetupSharedBasePassParameters(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FSceneRenderTargets& SceneRenderTargets,
	FSharedBasePassUniformParameters& BasePassParameters);

extern void CreateOpaqueBasePassUniformBuffer(
	FRHICommandListImmediate& RHICmdList, 
	const FViewInfo& View,
	IPooledRenderTarget* ForwardScreenSpaceShadowMask,
	FVector4* SceneWithoutSingleLayerWaterMinMaxUV,
	IPooledRenderTarget* SceneColorWithoutSingleLayerWater,
	IPooledRenderTarget* SceneDepthWithoutSingleLayerWater,
	TUniformBufferRef<FOpaqueBasePassUniformParameters>& BasePassUniformBuffer);

extern void CreateTranslucentBasePassUniformBuffer(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	IPooledRenderTarget* SceneColorCopy,
	ESceneTextureSetupMode SceneTextureSetupMode,
	TUniformBufferRef<class FTranslucentBasePassUniformParameters>& BasePassUniformBuffer,
	const int32 ViewIndex);

/** Parameters for computing forward lighting. */
class FForwardLightingParameters
{
public:

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("LOCAL_LIGHT_DATA_STRIDE"), FMath::DivideAndRoundUp<int32>(sizeof(FForwardLocalLightData), sizeof(FVector4)));
		extern int32 NumCulledLightsGridStride;
		OutEnvironment.SetDefine(TEXT("NUM_CULLED_LIGHTS_GRID_STRIDE"), NumCulledLightsGridStride);
		extern int32 NumCulledGridPrimitiveTypes;
		OutEnvironment.SetDefine(TEXT("NUM_CULLED_GRID_PRIMITIVE_TYPES"), NumCulledGridPrimitiveTypes);
	}
};

inline void BindBasePassUniformBuffer(const FShaderParameterMap& ParameterMap, FShaderUniformBufferParameter& BasePassUniformBuffer)
{
	TArray<const FShaderParametersMetadata*> NestedStructs;
	FOpaqueBasePassUniformParameters::StaticStructMetadata.GetNestedStructs(NestedStructs);
	FTranslucentBasePassUniformParameters::StaticStructMetadata.GetNestedStructs(NestedStructs);

	for (int32 StructIndex = 0; StructIndex < NestedStructs.Num(); StructIndex++)
	{
		const TCHAR* StructVariableName = NestedStructs[StructIndex]->GetShaderVariableName();
		checkfSlow(!ParameterMap.ContainsParameterAllocation(StructVariableName), TEXT("%s found bound in the base pass.  Base Pass uniform buffer nested structs should not be bound separately"), StructVariableName);
	}
	
	const bool bNeedsOpaqueBasePass = ParameterMap.ContainsParameterAllocation(FOpaqueBasePassUniformParameters::StaticStructMetadata.GetShaderVariableName());
	const bool bNeedsTransparentBasePass = ParameterMap.ContainsParameterAllocation(FTranslucentBasePassUniformParameters::StaticStructMetadata.GetShaderVariableName());

	checkSlow(!(bNeedsOpaqueBasePass && bNeedsTransparentBasePass));

	BasePassUniformBuffer.Bind(ParameterMap, FOpaqueBasePassUniformParameters::StaticStructMetadata.GetShaderVariableName());

	if (!BasePassUniformBuffer.IsBound())
	{
		BasePassUniformBuffer.Bind(ParameterMap, FTranslucentBasePassUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}
}

template<typename LightMapPolicyType>
class TBasePassShaderElementData : public FMeshMaterialShaderElementData
{
public:
	TBasePassShaderElementData(const typename LightMapPolicyType::ElementDataType& InLightMapPolicyElementData) :
		LightMapPolicyElementData(InLightMapPolicyElementData)
	{}

	typename LightMapPolicyType::ElementDataType LightMapPolicyElementData;
};

/**
 * The base shader type for vertex shaders that render the emissive color, and light-mapped/ambient lighting of a mesh.
 * The base type is shared between the versions with and without atmospheric fog.
 */
template<typename LightMapPolicyType>
class TBasePassVertexShaderPolicyParamType : public FMeshMaterialShader, public LightMapPolicyType::VertexParametersType
{
	DECLARE_INLINE_TYPE_LAYOUT_EXPLICIT_BASES(TBasePassVertexShaderPolicyParamType, NonVirtual, FMeshMaterialShader, typename LightMapPolicyType::VertexParametersType);
protected:

	TBasePassVertexShaderPolicyParamType() {}
	TBasePassVertexShaderPolicyParamType(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer):
		FMeshMaterialShader(Initializer)
	{
		LightMapPolicyType::VertexParametersType::Bind(Initializer.ParameterMap);
		BindBasePassUniformBuffer(Initializer.ParameterMap, PassUniformBuffer);
		ReflectionCaptureBuffer.Bind(Initializer.ParameterMap, TEXT("ReflectionCapture"));
	}

public:

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const TBasePassShaderElementData<LightMapPolicyType>& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const;

	void GetElementShaderBindings(
		const FShaderMapPointerTable& PointerTable,
		const FScene* Scene,
		const FSceneView* ViewIfDynamicMeshCommand,
		const FVertexFactory* VertexFactory,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMeshBatch& MeshBatch,
		const FMeshBatchElement& BatchElement,
		const TBasePassShaderElementData<LightMapPolicyType>& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const;

	LAYOUT_FIELD(FShaderUniformBufferParameter, ReflectionCaptureBuffer);
};




/**
 * The base shader type for vertex shaders that render the emissive color, and light-mapped/ambient lighting of a mesh.
 * The base type is shared between the versions with and without atmospheric fog.
 */

template<typename LightMapPolicyType>
class TBasePassVertexShaderBaseType : public TBasePassVertexShaderPolicyParamType<LightMapPolicyType>
{
	typedef TBasePassVertexShaderPolicyParamType<LightMapPolicyType> Super;
	DECLARE_INLINE_TYPE_LAYOUT(TBasePassVertexShaderBaseType, NonVirtual);
protected:
	TBasePassVertexShaderBaseType(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer) : Super(Initializer) {}

	TBasePassVertexShaderBaseType() {}

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return LightMapPolicyType::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		LightMapPolicyType::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	
	
};

template<typename LightMapPolicyType, bool bEnableAtmosphericFog>
class TBasePassVS : public TBasePassVertexShaderBaseType<LightMapPolicyType>
{
	DECLARE_SHADER_TYPE(TBasePassVS,MeshMaterial);
	typedef TBasePassVertexShaderBaseType<LightMapPolicyType> Super;

protected:

	TBasePassVS() {}
	TBasePassVS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer):
		Super(Initializer)
	{
	}

public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		static const auto SupportAtmosphericFog = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportAtmosphericFog"));
		static const auto SupportAllShaderPermutations = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportAllShaderPermutations"));
		const bool bForceAllPermutations = SupportAllShaderPermutations && SupportAllShaderPermutations->GetValueOnAnyThread() != 0;

		const bool bProjectAllowsAtmosphericFog = !SupportAtmosphericFog || SupportAtmosphericFog->GetValueOnAnyThread() != 0 || bForceAllPermutations;

		bool bShouldCache = Super::ShouldCompilePermutation(Parameters);
		bShouldCache &= (bEnableAtmosphericFog && bProjectAllowsAtmosphericFog && IsTranslucentBlendMode(Parameters.MaterialParameters.BlendMode)) || !bEnableAtmosphericFog;

		return bShouldCache
			&& (IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5));
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		// @todo MetalMRT: Remove this hack and implement proper atmospheric-fog solution for Metal MRT...
		OutEnvironment.SetDefine(TEXT("BASEPASS_ATMOSPHERIC_FOG"), !IsMetalMRTPlatform(Parameters.Platform) ? bEnableAtmosphericFog : 0);
	}
};

/**
 * The base shader type for hull shaders.
 */
template<typename LightMapPolicyType, bool bEnableAtmosphericFog>
class TBasePassHS : public FBaseHS
{
	DECLARE_SHADER_TYPE(TBasePassHS,MeshMaterial);

protected:

	TBasePassHS() {}

	TBasePassHS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer):
		FBaseHS(Initializer)
	{
		BindBasePassUniformBuffer(Initializer.ParameterMap, PassUniformBuffer);
	}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// Re-use vertex shader gating
		// Metal requires matching permutations, but no other platform should worry about this complication.
		return (bEnableAtmosphericFog == false || IsMetalPlatform(Parameters.Platform))
			&& FBaseHS::ShouldCompilePermutation(Parameters)
			&& TBasePassVS<LightMapPolicyType,bEnableAtmosphericFog>::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		// Re-use vertex shader compilation environment
		TBasePassVS<LightMapPolicyType,bEnableAtmosphericFog>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

/**
 * The base shader type for Domain shaders.
 */
template<typename LightMapPolicyType>
class TBasePassDS : public FBaseDS
{
	DECLARE_SHADER_TYPE(TBasePassDS,MeshMaterial);

protected:

	TBasePassDS() {}

	TBasePassDS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer):
		FBaseDS(Initializer)
	{
		BindBasePassUniformBuffer(Initializer.ParameterMap, PassUniformBuffer);
	}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// Re-use vertex shader gating
		return FBaseDS::ShouldCompilePermutation(Parameters)
			&& TBasePassVS<LightMapPolicyType,false>::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		// Re-use vertex shader compilation environment
		TBasePassVS<LightMapPolicyType,false>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

/**
 * The base type for pixel shaders that render the emissive color, and light-mapped/ambient lighting of a mesh.
 * The base type is shared between the versions with and without sky light.
 */
template<typename LightMapPolicyType>
class TBasePassPixelShaderPolicyParamType : public FMeshMaterialShader, public LightMapPolicyType::PixelParametersType
{
	DECLARE_INLINE_TYPE_LAYOUT_EXPLICIT_BASES(TBasePassPixelShaderPolicyParamType, NonVirtual, FMeshMaterialShader, typename LightMapPolicyType::PixelParametersType);
public:

	// static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		const bool bOutputVelocity = FVelocityRendering::BasePassCanOutputVelocity(Parameters.Platform);
		if (bOutputVelocity)
		{
			const int32 VelocityIndex = IsForwardShadingEnabled(Parameters.Platform) ? 1 : 4; // As defined in BasePassPixelShader.usf
			OutEnvironment.SetRenderTargetOutputFormat(VelocityIndex, PF_G16R16);
		}

		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
	}

	static bool ValidateCompiledResult(EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutError)
	{
		if (ParameterMap.ContainsParameterAllocation(FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName()))
		{
			OutError.Add(TEXT("Base pass shaders cannot read from the SceneTexturesStruct."));
			return false;
		}

		return true;
	}

	/** Initialization constructor. */
	TBasePassPixelShaderPolicyParamType(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer):
		FMeshMaterialShader(Initializer)
	{
		LightMapPolicyType::PixelParametersType::Bind(Initializer.ParameterMap);
		BindBasePassUniformBuffer(Initializer.ParameterMap, PassUniformBuffer);
		ReflectionCaptureBuffer.Bind(Initializer.ParameterMap, TEXT("ReflectionCapture"));

		// These parameters should only be used nested in the base pass uniform buffer
		check(!Initializer.ParameterMap.ContainsParameterAllocation(FFogUniformParameters::StaticStructMetadata.GetShaderVariableName()));
		check(!Initializer.ParameterMap.ContainsParameterAllocation(FReflectionUniformParameters::StaticStructMetadata.GetShaderVariableName()));
		check(!Initializer.ParameterMap.ContainsParameterAllocation(FPlanarReflectionUniformParameters::StaticStructMetadata.GetShaderVariableName()));
	}
	TBasePassPixelShaderPolicyParamType() {}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const TBasePassShaderElementData<LightMapPolicyType>& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const;

private:
	LAYOUT_FIELD(FShaderUniformBufferParameter, ReflectionCaptureBuffer);
};

/**
 * The base type for pixel shaders that render the emissive color, and light-mapped/ambient lighting of a mesh.
 * The base type is shared between the versions with and without sky light.
 */
template<typename LightMapPolicyType>
class TBasePassPixelShaderBaseType : public TBasePassPixelShaderPolicyParamType<LightMapPolicyType>
{
	typedef TBasePassPixelShaderPolicyParamType<LightMapPolicyType> Super;
	DECLARE_INLINE_TYPE_LAYOUT(TBasePassPixelShaderBaseType, NonVirtual);
public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return LightMapPolicyType::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		LightMapPolicyType::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		Super::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	/** Initialization constructor. */
	TBasePassPixelShaderBaseType(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer) : Super(Initializer) {}

	TBasePassPixelShaderBaseType() {}
};

/** The concrete base pass pixel shader type. */
template<typename LightMapPolicyType, bool bEnableSkyLight>
class TBasePassPS : public TBasePassPixelShaderBaseType<LightMapPolicyType>
{
	DECLARE_SHADER_TYPE(TBasePassPS,MeshMaterial);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// Only compile skylight version for lit materials, and if the project allows them.
		static const auto SupportStationarySkylight = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportStationarySkylight"));
		static const auto SupportAllShaderPermutations = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportAllShaderPermutations"));

		const bool IsSingleLayerWater = Parameters.MaterialParameters.ShadingModels.HasShadingModel(MSM_SingleLayerWater);

		const bool bTranslucent = IsTranslucentBlendMode(Parameters.MaterialParameters.BlendMode);
		const bool bForceAllPermutations = SupportAllShaderPermutations && SupportAllShaderPermutations->GetValueOnAnyThread() != 0;
		const bool bProjectSupportsStationarySkylight = !SupportStationarySkylight || SupportStationarySkylight->GetValueOnAnyThread() != 0 || bForceAllPermutations;

		const bool bCacheShaders = !bEnableSkyLight
			//translucent materials need to compile skylight support to support MOVABLE skylights also.
			|| bTranslucent
			// Some lightmap policies (eg Simple Forward) always require skylight support
			|| IsSingleLayerWater
			|| LightMapPolicyType::RequiresSkylight()
			|| ((bProjectSupportsStationarySkylight || IsForwardShadingEnabled(Parameters.Platform)) && Parameters.MaterialParameters.ShadingModels.IsLit());
		return bCacheShaders
			&& (IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5))
			&& TBasePassPixelShaderBaseType<LightMapPolicyType>::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		// For deferred decals, the shader class used is FDeferredDecalPS. the TBasePassPS is only used in the material editor and will read wrong values.
		OutEnvironment.SetDefine(TEXT("SCENE_TEXTURES_DISABLED"), Parameters.MaterialParameters.MaterialDomain != MD_Surface);
		OutEnvironment.SetDefine(TEXT("COMPILE_BASEPASS_PIXEL_VOLUMETRIC_FOGGING"), DoesPlatformSupportVolumetricFog(Parameters.Platform));
		OutEnvironment.SetDefine(TEXT("ENABLE_SKY_LIGHT"), bEnableSkyLight);
		OutEnvironment.SetDefine(TEXT("PLATFORM_FORCE_SIMPLE_SKY_DIFFUSE"), ForceSimpleSkyDiffuse(Parameters.Platform));

		TBasePassPixelShaderBaseType<LightMapPolicyType>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
	
	/** Initialization constructor. */
	TBasePassPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		TBasePassPixelShaderBaseType<LightMapPolicyType>(Initializer)
	{}

	/** Default constructor. */
	TBasePassPS() {}
};

//Alternative base pass PS for 128 bit canvas render targets that need to be set at shader compilation time.
class F128BitRTBasePassPS : public TBasePassPS<TUniformLightMapPolicy<LMP_NO_LIGHTMAP>, false>
{
	DECLARE_SHADER_TYPE(F128BitRTBasePassPS, MeshMaterial);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FDataDrivenShaderPlatformInfo::GetRequiresExplicit128bitRT(Parameters.Platform);		
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_A32B32G32R32F);
		TBasePassPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	/** Initialization constructor. */
	F128BitRTBasePassPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer): 
		TBasePassPS<TUniformLightMapPolicy<LMP_NO_LIGHTMAP>, false>(Initializer)
	{}

	/** Default constructor. */
	F128BitRTBasePassPS() {}
};

/**
 * Get shader templates allowing to redirect between compatible shaders.
 */

template <typename LightMapPolicyType>
void GetBasePassShaders(
	const FMaterial& Material, 
	FVertexFactoryType* VertexFactoryType, 
	LightMapPolicyType LightMapPolicy, 
	ERHIFeatureLevel::Type FeatureLevel,
	bool bEnableAtmosphericFog,
	bool bEnableSkyLight,
	bool bUse128bitRT,
	TShaderRef<FBaseHS>& HullShader,
	TShaderRef<FBaseDS>& DomainShader,
	TShaderRef<TBasePassVertexShaderPolicyParamType<LightMapPolicyType>>& VertexShader,
	TShaderRef<TBasePassPixelShaderPolicyParamType<LightMapPolicyType>>& PixelShader
	)
{
	const EMaterialTessellationMode MaterialTessellationMode = Material.GetTessellationMode();

	const bool bNeedsHSDS = RHISupportsTessellation(GShaderPlatformForFeatureLevel[FeatureLevel])
		&& VertexFactoryType->SupportsTessellationShaders() 
		&& MaterialTessellationMode != MTM_NoTessellation;

	if (bNeedsHSDS)
	{
		DomainShader = Material.GetShader<TBasePassDS<LightMapPolicyType > >(VertexFactoryType);
		
		// Metal requires matching permutations, but no other platform should worry about this complication.
		if (bEnableAtmosphericFog && DomainShader.IsValid() && IsMetalPlatform(EShaderPlatform(DomainShader->GetTarget().Platform)))
		{
			HullShader = Material.GetShader<TBasePassHS<LightMapPolicyType, true > >(VertexFactoryType);
		}
		else
		{
			HullShader = Material.GetShader<TBasePassHS<LightMapPolicyType, false > >(VertexFactoryType);
		}
	}

	if (bEnableAtmosphericFog)
	{
		VertexShader = Material.GetShader<TBasePassVS<LightMapPolicyType, true> >(VertexFactoryType);
	}
	else
	{
		VertexShader = Material.GetShader<TBasePassVS<LightMapPolicyType, false> >(VertexFactoryType);
	}
	if (bEnableSkyLight)
	{
		PixelShader = Material.GetShader<TBasePassPS<LightMapPolicyType, true> >(VertexFactoryType);
	}
	else
	{
		PixelShader = Material.GetShader<TBasePassPS<LightMapPolicyType, false> >(VertexFactoryType);
	}
}

template <>
void GetBasePassShaders<FUniformLightMapPolicy>(
	const FMaterial& Material, 
	FVertexFactoryType* VertexFactoryType, 
	FUniformLightMapPolicy LightMapPolicy, 
	ERHIFeatureLevel::Type FeatureLevel,
	bool bEnableAtmosphericFog,
	bool bEnableSkyLight,
	bool bUse128bitRT,
	TShaderRef<FBaseHS>& HullShader,
	TShaderRef<FBaseDS>& DomainShader,
	TShaderRef<TBasePassVertexShaderPolicyParamType<FUniformLightMapPolicy>>& VertexShader,
	TShaderRef<TBasePassPixelShaderPolicyParamType<FUniformLightMapPolicy>>& PixelShader
	);

void SetupBasePassView(FRHICommandList& RHICmdList, const FViewInfo& View, const FSceneRenderer* SceneRenderer, const bool bIsEditorPrimitivePass = false);
	
class FBasePassMeshProcessor : public FMeshPassProcessor
{
public:
	enum class EFlags
	{
		None = 0,

		// Informs the processor whether a depth-stencil target is bound when processed draw commands are issued.
		CanUseDepthStencil = (1 << 0),
		bRequires128bitRT = (1 << 1)
	};

	FBasePassMeshProcessor(
		const FScene* InScene,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InDrawRenderState,
		FMeshPassDrawListContext* InDrawListContext,
		EFlags Flags,
		ETranslucencyPass::Type InTranslucencyPassType = ETranslucencyPass::TPT_MAX);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

	FMeshPassProcessorRenderState PassDrawRenderState;

	FORCEINLINE_DEBUGGABLE void Set128BitRequirement(const bool Required)
	{
		bRequiresExplicit128bitRT = Required;
	}

	FORCEINLINE_DEBUGGABLE bool Get128BitRequirement() const
	{
		return bRequiresExplicit128bitRT;
	}

private:

	void AddMeshBatchForSimpleForwardShading(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FLightMapInteraction& LightMapInteraction,
		bool bIsLitMaterial,
		bool bAllowStaticLighting,
		bool bUseVolumetricLightmap,
		bool bAllowIndirectLightingCache,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

	template<typename LightMapPolicyType>
	void Process(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		EBlendMode BlendMode,
		FMaterialShadingModelField ShadingModels,
		const LightMapPolicyType& RESTRICT LightMapPolicy,
		const typename LightMapPolicyType::ElementDataType& RESTRICT LightMapElementData,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

	const ETranslucencyPass::Type TranslucencyPassType;
	const bool bTranslucentBasePass;
	const bool bEnableReceiveDecalOutput;
	EDepthDrawingMode EarlyZPassMode;
	bool bRequiresExplicit128bitRT;
};

ENUM_CLASS_FLAGS(FBasePassMeshProcessor::EFlags);

extern void SetupBasePassState(FExclusiveDepthStencil::Type BasePassDepthStencilAccess, const bool bShaderComplexity, FMeshPassProcessorRenderState& DrawRenderState);
extern FMeshDrawCommandSortKey CalculateTranslucentMeshStaticSortKey(const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, uint16 MeshIdInPrimitive);

