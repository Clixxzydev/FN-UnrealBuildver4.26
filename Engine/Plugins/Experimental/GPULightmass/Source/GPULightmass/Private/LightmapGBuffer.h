// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GPULightmassCommon.h"
#include "PrimitiveSceneProxy.h"
#include "MeshPassProcessor.h"
#include "MeshPassProcessor.inl"
#include "MeshMaterialShader.h"
#include "LightMapRendering.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLightmapGBufferParams, )
	SHADER_PARAMETER(FVector4, VirtualTexturePhysicalTileCoordinateScaleAndBias)
	SHADER_PARAMETER(FIntPoint, ScratchTilePoolOffset)
	SHADER_PARAMETER(int, RenderPassIndex)
	SHADER_PARAMETER_UAV(RWTexture2D<float4>, ScratchTilePoolLayer0)
	SHADER_PARAMETER_UAV(RWTexture2D<float4>, ScratchTilePoolLayer1)
	SHADER_PARAMETER_UAV(RWTexture2D<float4>, ScratchTilePoolLayer2)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

typedef TUniformBufferRef<FLightmapGBufferParams> FLightmapGBufferUniformBufferRef;

struct FLightmapElementData : public FMeshMaterialShaderElementData
{
	const FLightCacheInterface* LCI;

	FLightmapElementData(const FLightCacheInterface* LCI) : LCI(LCI) {}
};

class FLightmapGBufferVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FLightmapGBufferVS, MeshMaterial);

	LAYOUT_FIELD(FShaderUniformBufferParameter, PrecomputedLightingBufferParameter);
protected:

	FLightmapGBufferVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FLightmapGBufferParams::StaticStructMetadata.GetShaderVariableName());
		PrecomputedLightingBufferParameter.Bind(Initializer.ParameterMap, TEXT("PrecomputedLightingBuffer"));
	}

	FLightmapGBufferVS() = default;

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("RANDOM_SAMPLER"), (int)2);
		OutEnvironment.SetDefine(TEXT("NEEDS_LIGHTMAP_COORDINATE"), 1);
		OutEnvironment.SetDefine(TEXT("SCENE_TEXTURES_DISABLED"), 1);
		OutEnvironment.SetDefine(TEXT("GPreviewLightmapPhysicalTileSize"), GPreviewLightmapPhysicalTileSize);
	}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		if (IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
			&& IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"))->GetValueOnAnyThread() != 0
			&& Parameters.VertexFactoryType->SupportsStaticLighting())
		{
			return true;
		}
		else
		{
			return false;
		}
	}

public:
	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FLightmapElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		if (PrecomputedLightingBufferParameter.IsBound())
		{
			if (!ShaderElementData.LCI || !ShaderElementData.LCI->GetPrecomputedLightingBuffer())
			{
				ShaderBindings.Add(PrecomputedLightingBufferParameter, GEmptyPrecomputedLightingUniformBuffer.GetUniformBufferRHI());
			}
			else
			{
				ShaderBindings.Add(PrecomputedLightingBufferParameter, ShaderElementData.LCI->GetPrecomputedLightingBuffer());
			}
		}
	}
};

class FLightmapGBufferPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FLightmapGBufferPS, MeshMaterial);
public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		if (IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
			&& IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"))->GetValueOnAnyThread() != 0
			&& Parameters.VertexFactoryType->SupportsStaticLighting())
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_A32B32G32R32F);
		OutEnvironment.SetDefine(TEXT("NEEDS_LIGHTMAP_COORDINATE"), 1);
		OutEnvironment.SetDefine(TEXT("SCENE_TEXTURES_DISABLED"), 1);
		OutEnvironment.SetDefine(TEXT("GPreviewLightmapPhysicalTileSize"), GPreviewLightmapPhysicalTileSize);
	}

	FLightmapGBufferPS() = default;

	FLightmapGBufferPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FLightmapGBufferParams::StaticStructMetadata.GetShaderVariableName());
	}
};

class FLightmapGBufferMeshProcessor : public FMeshPassProcessor
{
public:
	FLightmapGBufferMeshProcessor(const FScene* InScene, const FSceneView* InView, FMeshPassDrawListContext* InDrawListContext, FRHIUniformBuffer* InPassUniformBuffer)
		: FMeshPassProcessor(InScene, InView->GetFeatureLevel(), InView, InDrawListContext)
		, DrawRenderState(*InView, InPassUniformBuffer)
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	}

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
	{
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

		if (MeshBatch.bUseForMaterial
			&& (!PrimitiveSceneProxy || PrimitiveSceneProxy->ShouldRenderInMainPass()))
		{
			Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, MaterialRenderProxy, Material);
		}
	}

private:
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource)
	{
		const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

		TMeshProcessorShaders<
			FLightmapGBufferVS,
			FMeshMaterialShader,
			FMeshMaterialShader,
			FLightmapGBufferPS> Shaders;

		Shaders.VertexShader = MaterialResource.GetShader<FLightmapGBufferVS>(VertexFactory->GetType());
		Shaders.PixelShader = MaterialResource.GetShader<FLightmapGBufferPS>(VertexFactory->GetType());

		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, MaterialResource, OverrideSettings);
		ERasterizerCullMode MeshCullMode = CM_None;

		FLightmapElementData ShaderElementData(MeshBatch.LCI);
		ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

		FMeshDrawCommandSortKey SortKey {};

		BuildMeshDrawCommands(
			MeshBatch,
			BatchElementMask,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			MaterialResource,
			DrawRenderState,
			Shaders,
			MeshFillMode,
			MeshCullMode,
			SortKey,
			EMeshPassFeatures::Default,
			ShaderElementData);
	}

private:
	FMeshPassProcessorRenderState DrawRenderState;
};
