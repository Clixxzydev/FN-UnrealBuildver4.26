// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneRenderTargetParameters.h: Shader base classes
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "SceneInterface.h"
#include "MaterialShared.h"

class FSceneView;
class FShaderParameterMap;

// Convenience parameters used by the material graph or many global shaders
//@todo - replace with rendergraph inputs and outputs, whose lifetimes can be validated (eg GBuffers not available in BasePass)
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSceneTexturesUniformParameters, RENDERER_API)
	// Scene Color / Depth
	SHADER_PARAMETER_TEXTURE(Texture2D, SceneColorTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, SceneDepthTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D<float>, SceneDepthTextureNonMS)

	// GBuffer
	SHADER_PARAMETER_TEXTURE(Texture2D, GBufferATexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, GBufferBTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, GBufferCTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, GBufferDTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, GBufferETexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, GBufferFTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, GBufferVelocityTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, GBufferATextureNonMS)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, GBufferBTextureNonMS)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, GBufferCTextureNonMS)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, GBufferDTextureNonMS)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, GBufferETextureNonMS)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, GBufferFTextureNonMS)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, GBufferVelocityTextureNonMS)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferATextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferBTextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferCTextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferDTextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferETextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferFTextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferVelocityTextureSampler)

	// SSAO
	SHADER_PARAMETER_TEXTURE(Texture2D, ScreenSpaceAOTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, ScreenSpaceAOTextureSampler)

	// Custom Depth / Stencil
	SHADER_PARAMETER_TEXTURE(Texture2D<float>, CustomDepthTextureNonMS)
	SHADER_PARAMETER_TEXTURE(Texture2D, CustomDepthTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, CustomDepthTextureSampler)
	SHADER_PARAMETER_SRV(Texture2D<uint2>, CustomStencilTexture)
	SHADER_PARAMETER_SRV(Texture2D<uint2>, SceneStencilTexture)

	// Misc
	SHADER_PARAMETER_TEXTURE(Texture2D, EyeAdaptation)
	SHADER_PARAMETER_TEXTURE(Texture2D, SceneColorCopyTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorCopyTextureSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

enum class ESceneTextureSetupMode : uint32
{
	None = 0,
	SceneDepth = 1,
	GBuffers = 2,
	SSAO = 4,
	CustomDepth = 8,
	All = SceneDepth | GBuffers | SSAO | CustomDepth
};

inline ESceneTextureSetupMode operator |(ESceneTextureSetupMode lhs, ESceneTextureSetupMode rhs)  
{
	return static_cast<ESceneTextureSetupMode> (
		static_cast<uint32>(lhs) |
		static_cast<uint32>(rhs)
	);
} 

inline ESceneTextureSetupMode operator &(ESceneTextureSetupMode lhs, ESceneTextureSetupMode rhs)  
{
	return static_cast<ESceneTextureSetupMode> (
		static_cast<uint32>(lhs) &
		static_cast<uint32>(rhs)
	);
} 

extern RENDERER_API void SetupSceneTextureUniformParameters(
	FSceneRenderTargets& SceneContext,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode,
	FSceneTexturesUniformParameters& OutParameters);

extern RENDERER_API TUniformBufferRef<FSceneTexturesUniformParameters> CreateSceneTextureUniformBuffer(
	FSceneRenderTargets& SceneContext,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode,
	EUniformBufferUsage Usage);

extern RENDERER_API TRefCountPtr<FRHIUniformBuffer> CreateSceneTextureUniformBufferDependentOnShadingPath(
	FSceneRenderTargets& SceneContext,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode,
	EUniformBufferUsage Usage);

extern RENDERER_API bool IsSceneTexturesValid(FRHICommandListImmediate& RHICmdList);

template <typename TRHICmdList>
RENDERER_API TRefCountPtr<FRHIUniformBuffer> CreateSceneTextureUniformBufferDependentOnShadingPath(
	TRHICmdList& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode,
	EUniformBufferUsage Usage);

template< typename TRHICmdList >
RENDERER_API TUniformBufferRef<FSceneTexturesUniformParameters> CreateSceneTextureUniformBufferSingleDraw(TRHICmdList& RHICmdList, ESceneTextureSetupMode SceneTextureSetupMode, ERHIFeatureLevel::Type FeatureLevel);

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileSceneTextureUniformParameters, RENDERER_API)
	SHADER_PARAMETER_TEXTURE(Texture2D, SceneColorTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, SceneDepthTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, CustomDepthTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, CustomDepthTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, MobileCustomStencilTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, MobileCustomStencilTextureSampler)
	SHADER_PARAMETER_UAV(RWBuffer<uint>, VirtualTextureFeedbackUAV)
	SHADER_PARAMETER_SRV(Buffer<float4>, EyeAdaptationBuffer)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

extern void SetupMobileSceneTextureUniformParameters(
	FSceneRenderTargets& SceneContext,
	ERHIFeatureLevel::Type FeatureLevel,
	bool bSceneTexturesValid,
	bool bCustomDepthIsValid,
	FMobileSceneTextureUniformParameters& SceneTextureParameters);

template< typename TRHICmdList >
RENDERER_API TUniformBufferRef<FMobileSceneTextureUniformParameters> CreateMobileSceneTextureUniformBufferSingleDraw(TRHICmdList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel);

extern RENDERER_API void BindSceneTextureUniformBufferDependentOnShadingPath(
	const FShader::CompiledShaderInitializerType& Initializer,
	FShaderUniformBufferParameter& SceneTexturesUniformBuffer);

BEGIN_SHADER_PARAMETER_STRUCT(FSceneTextureShaderParameters, RENDERER_API)
	SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneTextures)
	SHADER_PARAMETER_STRUCT_REF(FMobileSceneTextureUniformParameters, MobileSceneTextures)
END_SHADER_PARAMETER_STRUCT()

extern RENDERER_API FSceneTextureShaderParameters CreateSceneTextureShaderParameters(
	FSceneRenderTargets& SceneContext,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode,
	EUniformBufferUsage Usage);