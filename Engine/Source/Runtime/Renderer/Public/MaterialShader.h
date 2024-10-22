// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialShader.h: Shader base classes
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "ShaderParameters.h"
#include "SceneView.h"
#include "Shader.h"
#include "MaterialShared.h"
#include "GlobalShader.h"
#include "MaterialShaderType.h"
#include "SceneRenderTargetParameters.h"
#include "ShaderParameterUtils.h"

template<typename TBufferStruct> class TUniformBufferRef;

/**
 * Debug information related to uniform expression sets.
 */
class FDebugUniformExpressionSet
{
	DECLARE_TYPE_LAYOUT(FDebugUniformExpressionSet, NonVirtual);
public:
	FDebugUniformExpressionSet()
		: NumVectorExpressions(0)
		, NumScalarExpressions(0)
	{
		FMemory::Memzero(NumTextureExpressions);
	}

	explicit FDebugUniformExpressionSet(const FUniformExpressionSet& InUniformExpressionSet)
	{
		InitFromExpressionSet(InUniformExpressionSet);
	}

	/** Initialize from a uniform expression set. */
	void InitFromExpressionSet(const FUniformExpressionSet& InUniformExpressionSet)
	{
		NumVectorExpressions = InUniformExpressionSet.UniformVectorPreshaders.Num();
		NumScalarExpressions = InUniformExpressionSet.UniformScalarPreshaders.Num();
		for (uint32 TypeIndex = 0u; TypeIndex < NumMaterialTextureParameterTypes; ++TypeIndex)
		{
			NumTextureExpressions[TypeIndex] = InUniformExpressionSet.UniformTextureParameters[TypeIndex].Num();
		}
	}

	/** Returns true if the number of uniform expressions matches those with which the debug set was initialized. */
	bool Matches(const FUniformExpressionSet& InUniformExpressionSet) const
	{
		for (uint32 TypeIndex = 0u; TypeIndex < NumMaterialTextureParameterTypes; ++TypeIndex)
		{
			if (NumTextureExpressions[TypeIndex] != InUniformExpressionSet.UniformTextureParameters[TypeIndex].Num())
			{
				return false;
			}
		}
		return NumVectorExpressions == InUniformExpressionSet.UniformVectorPreshaders.Num() && NumScalarExpressions == InUniformExpressionSet.UniformScalarPreshaders.Num();
	}
	
	/** The number of each type of expression contained in the set. */
	LAYOUT_FIELD(int32, NumVectorExpressions);
	LAYOUT_FIELD(int32, NumScalarExpressions);
	LAYOUT_ARRAY(int32, NumTextureExpressions, NumMaterialTextureParameterTypes);
};

struct FMaterialShaderPermutationParameters : public FShaderPermutationParameters
{
	FMaterialShaderParameters MaterialParameters;

	FMaterialShaderPermutationParameters(EShaderPlatform InPlatform, const FMaterialShaderParameters& InMaterialParameters, int32 InPermutationId)
		: FShaderPermutationParameters(InPlatform, InPermutationId)
		, MaterialParameters(InMaterialParameters)
	{}
};

/** Base class of all shaders that need material parameters. */
class RENDERER_API FMaterialShader : public FShader
{
	DECLARE_TYPE_LAYOUT(FMaterialShader, NonVirtual);
public:
	using FPermutationParameters = FMaterialShaderPermutationParameters;
	using ShaderMetaType = FMaterialShaderType;

	static FName UniformBufferLayoutName;

	FMaterialShader()
#if WITH_EDITORONLY_DATA
		: DebugUniformExpressionUBLayout(FRHIUniformBufferLayout::Zero)
#endif
	{}

	FMaterialShader(const FMaterialShaderType::CompiledShaderInitializerType& Initializer);

	FRHIUniformBuffer* GetParameterCollectionBuffer(const FGuid& Id, const FSceneInterface* SceneInterface) const;

	template<typename ShaderRHIParamRef>
	FORCEINLINE_DEBUGGABLE void SetViewParameters(FRHICommandList& RHICmdList, const ShaderRHIParamRef ShaderRHI, const FSceneView& View, const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer)
	{
		const auto& ViewUniformBufferParameter = GetUniformBufferParameter<FViewUniformShaderParameters>();
		SetUniformBufferParameter(RHICmdList, ShaderRHI, ViewUniformBufferParameter, ViewUniformBuffer);

		if (View.bShouldBindInstancedViewUB && View.Family->Views.Num() > 0)
		{
			// When drawing the left eye in a stereo scene, copy the right eye view values into the instanced view uniform buffer.
			const EStereoscopicPass StereoPassIndex = IStereoRendering::IsStereoEyeView(View) ? eSSP_RIGHT_EYE : eSSP_FULL;

			const FSceneView& InstancedView = View.Family->GetStereoEyeView(StereoPassIndex);
			const auto& InstancedViewUniformBufferParameter = GetUniformBufferParameter<FInstancedViewUniformShaderParameters>();
			SetUniformBufferParameter(RHICmdList, ShaderRHI, InstancedViewUniformBufferParameter, InstancedView.ViewUniformBuffer);
		}
	}

	/** Sets pixel parameters that are material specific but not FMeshBatch specific. */
	template< typename TRHIShader >
	void SetParameters(
		FRHICommandList& RHICmdList,
		TRHIShader* ShaderRHI,
		const FMaterialRenderProxy* MaterialRenderProxy, 
		const FMaterial& Material,
		const FSceneView& View);

	void GetShaderBindings(
		const FScene* Scene,
		const FStaticFeatureLevel FeatureLevel,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		FMeshDrawSingleShaderBindings& ShaderBindings) const;

private:
	/** If true, cached uniform expressions are allowed. */
	static int32 bAllowCachedUniformExpressions;
	/** Console variable ref to toggle cached uniform expressions. */
	static FAutoConsoleVariableRef CVarAllowCachedUniformExpressions;

#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING || !WITH_EDITOR)
	void VerifyExpressionAndShaderMaps(const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial& Material, const FUniformExpressionCache* UniformExpressionCache) const;
#endif

	LAYOUT_FIELD(TMemoryImageArray<FShaderUniformBufferParameter>, ParameterCollectionUniformBuffers);

	LAYOUT_FIELD(FShaderUniformBufferParameter, MaterialUniformBuffer);

	// Only needed to avoid unbound parameter error
	// This texture is bound as an UAV (RWTexture) and so it must be bound together with any RT. So it actually bound but not as part of the material
	LAYOUT_FIELD(FShaderResourceParameter, VTFeedbackBuffer);

protected:
	LAYOUT_FIELD_EDITORONLY(FDebugUniformExpressionSet, DebugUniformExpressionSet);
	LAYOUT_FIELD_EDITORONLY(FRHIUniformBufferLayout, DebugUniformExpressionUBLayout);
	LAYOUT_FIELD_EDITORONLY(FMemoryImageString, DebugDescription);
};
