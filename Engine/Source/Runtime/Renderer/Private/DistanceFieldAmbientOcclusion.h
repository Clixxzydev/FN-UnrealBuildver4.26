// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldAmbientOcclusion.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "UniformBuffer.h"
#include "RHIStaticStates.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "PostProcess/SceneRenderTargets.h"
#include "ScenePrivate.h"

const static int32 GAOMaxSupportedLevel = 6;
/** Number of cone traced directions. */
const int32 NumConeSampleDirections = 9;

/** Base downsample factor that all distance field AO operations are done at. */
const int32 GAODownsampleFactor = 2;

extern const uint32 UpdateObjectsGroupSize;

extern FIntPoint GetBufferSizeForAO();

class FDistanceFieldAOParameters
{
public:
	float GlobalMaxOcclusionDistance;
	float ObjectMaxOcclusionDistance;
	float Contrast;

	FDistanceFieldAOParameters(float InOcclusionMaxDistance, float InContrast = 0);
};

/**  */
class FTileIntersectionResources : public FRenderResource
{
public:

	FRWBuffer TileConeAxisAndCos;
	FRWBuffer TileConeDepthRanges;

	FRWBuffer NumCulledTilesArray;
	FRWBuffer CulledTilesStartOffsetArray;
	FRWBuffer CulledTileDataArray;
	FRWBuffer ObjectTilesIndirectArguments;

	FIntPoint TileDimensions;
	int32 MaxSceneObjects;
	bool bAllow16BitIndices;

	FTileIntersectionResources(bool bInAllow16BitIndices) :
		MaxSceneObjects(0), bAllow16BitIndices(bInAllow16BitIndices)
	{}

	bool HasAllocatedEnoughFor(FIntPoint TestTileDimensions, int32 TestMaxSceneObjects) const
	{
		return TestTileDimensions == TileDimensions && TestMaxSceneObjects <= MaxSceneObjects;
	}

	void SetupParameters(FIntPoint InTileDimensions, int32 InMaxSceneObjects)
	{
		TileDimensions = InTileDimensions;
		MaxSceneObjects = InMaxSceneObjects;
	}

	virtual void InitDynamicRHI() override;

	

	virtual void ReleaseDynamicRHI() override
	{
		TileConeAxisAndCos.Release();
		TileConeDepthRanges.Release();

		NumCulledTilesArray.Release();
		CulledTilesStartOffsetArray.Release();
		CulledTileDataArray.Release();
		ObjectTilesIndirectArguments.Release();
	}

	void AcquireTransientResource()
	{
		TileConeAxisAndCos.AcquireTransientResource();
		TileConeDepthRanges.AcquireTransientResource();
		NumCulledTilesArray.AcquireTransientResource();
		CulledTilesStartOffsetArray.AcquireTransientResource();
		CulledTileDataArray.AcquireTransientResource();
	}

	void DiscardTransientResource()
	{
		TileConeAxisAndCos.DiscardTransientResource();
		TileConeDepthRanges.DiscardTransientResource();
		NumCulledTilesArray.DiscardTransientResource();
		CulledTilesStartOffsetArray.DiscardTransientResource();
		CulledTileDataArray.DiscardTransientResource();
	}

	size_t GetSizeBytes() const
	{
		return TileConeAxisAndCos.NumBytes + TileConeDepthRanges.NumBytes
			+ NumCulledTilesArray.NumBytes + CulledTilesStartOffsetArray.NumBytes + CulledTileDataArray.NumBytes + ObjectTilesIndirectArguments.NumBytes;
	}
};

static int32 CulledTileDataStride = 2;
static int32 ConeTraceObjectsThreadGroupSize = 64;

class FTileIntersectionParameters
{
	DECLARE_TYPE_LAYOUT(FTileIntersectionParameters, NonVirtual);
public:

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("CULLED_TILE_DATA_STRIDE"), CulledTileDataStride);
		extern int32 GDistanceFieldAOTileSizeX;
		OutEnvironment.SetDefine(TEXT("CULLED_TILE_SIZEX"), GDistanceFieldAOTileSizeX);
		extern int32 GConeTraceDownsampleFactor;
		OutEnvironment.SetDefine(TEXT("TRACE_DOWNSAMPLE_FACTOR"), GConeTraceDownsampleFactor);
		OutEnvironment.SetDefine(TEXT("CONE_TRACE_OBJECTS_THREADGROUP_SIZE"), ConeTraceObjectsThreadGroupSize);
	}

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		TileListGroupSize.Bind(ParameterMap, TEXT("TileListGroupSize"));
		NumCulledTilesArray.Bind(ParameterMap, TEXT("NumCulledTilesArray"));
		CulledTilesStartOffsetArray.Bind(ParameterMap, TEXT("CulledTilesStartOffsetArray"));
		CulledTileDataArray.Bind(ParameterMap, TEXT("CulledTileDataArray"));
		ObjectTilesIndirectArguments.Bind(ParameterMap, TEXT("ObjectTilesIndirectArguments"));
	}

	template<typename TParamRef>
	void Set(FRHICommandList& RHICmdList, const TParamRef& ShaderRHI, const FTileIntersectionResources& TileIntersectionResources)
	{
		SetShaderValue(RHICmdList, ShaderRHI, TileListGroupSize, TileIntersectionResources.TileDimensions);

		NumCulledTilesArray.SetBuffer(RHICmdList, ShaderRHI, TileIntersectionResources.NumCulledTilesArray);
		CulledTilesStartOffsetArray.SetBuffer(RHICmdList, ShaderRHI, TileIntersectionResources.CulledTilesStartOffsetArray);
		CulledTileDataArray.SetBuffer(RHICmdList, ShaderRHI, TileIntersectionResources.CulledTileDataArray);
		ObjectTilesIndirectArguments.SetBuffer(RHICmdList, ShaderRHI, TileIntersectionResources.ObjectTilesIndirectArguments);
	}

	void GetUAVs(FTileIntersectionResources& TileIntersectionResources, TArray<FRHIUnorderedAccessView*>& UAVs)
	{
		uint32 MaxIndex = 0;
		
		MaxIndex = FMath::Max(MaxIndex, NumCulledTilesArray.GetUAVIndex());
		MaxIndex = FMath::Max(MaxIndex, CulledTilesStartOffsetArray.GetUAVIndex());
		MaxIndex = FMath::Max(MaxIndex, CulledTileDataArray.GetUAVIndex());
		MaxIndex = FMath::Max(MaxIndex, ObjectTilesIndirectArguments.GetUAVIndex());

		UAVs.AddZeroed(MaxIndex + 1);

		if (NumCulledTilesArray.IsUAVBound())
		{
			UAVs[NumCulledTilesArray.GetUAVIndex()] = TileIntersectionResources.NumCulledTilesArray.UAV;
		}

		if (CulledTilesStartOffsetArray.IsUAVBound())
		{
			UAVs[CulledTilesStartOffsetArray.GetUAVIndex()] = TileIntersectionResources.CulledTilesStartOffsetArray.UAV;
		}

		if (CulledTileDataArray.IsUAVBound())
		{
			UAVs[CulledTileDataArray.GetUAVIndex()] = TileIntersectionResources.CulledTileDataArray.UAV;
		}

		if (ObjectTilesIndirectArguments.IsUAVBound())
		{
			UAVs[ObjectTilesIndirectArguments.GetUAVIndex()] = TileIntersectionResources.ObjectTilesIndirectArguments.UAV;
		}

		check(UAVs.Num() > 0);
	}

	template<typename TParamRef>
	void UnsetParameters(FRHICommandList& RHICmdList, const TParamRef& ShaderRHI)
	{
		NumCulledTilesArray.UnsetUAV(RHICmdList, ShaderRHI);
		CulledTilesStartOffsetArray.UnsetUAV(RHICmdList, ShaderRHI);
		CulledTileDataArray.UnsetUAV(RHICmdList, ShaderRHI);
		ObjectTilesIndirectArguments.UnsetUAV(RHICmdList, ShaderRHI);
	}

	friend FArchive& operator<<(FArchive& Ar, FTileIntersectionParameters& P)
	{
		Ar << P.TileListGroupSize;
		Ar << P.NumCulledTilesArray;
		Ar << P.CulledTilesStartOffsetArray;
		Ar << P.CulledTileDataArray;
		Ar << P.ObjectTilesIndirectArguments;
		return Ar;
	}

private:
	
		LAYOUT_FIELD(FShaderParameter, TileListGroupSize)

		LAYOUT_FIELD(FRWShaderParameter, NumCulledTilesArray)
		LAYOUT_FIELD(FRWShaderParameter, CulledTilesStartOffsetArray)
		LAYOUT_FIELD(FRWShaderParameter, CulledTileDataArray)
		LAYOUT_FIELD(FRWShaderParameter, ObjectTilesIndirectArguments)
	
};

class FAOScreenGridResources : public FRenderResource
{
public:

	FAOScreenGridResources()
	{}

	virtual void InitDynamicRHI() override;

	virtual void ReleaseDynamicRHI() override
	{
		ScreenGridConeVisibility.Release();
		ConeDepthVisibilityFunction.Release();
	}

	void AcquireTransientResource()
	{
		ScreenGridConeVisibility.AcquireTransientResource();
	}

	void DiscardTransientResource()
	{
		ScreenGridConeVisibility.DiscardTransientResource();
	}

	FIntPoint ScreenGridDimensions;

	FRWBuffer ScreenGridConeVisibility;
	FRWBuffer ConeDepthVisibilityFunction;

	size_t GetSizeBytesForAO() const
	{
		return ScreenGridConeVisibility.NumBytes;
	}
};

extern void GetSpacedVectors(uint32 FrameNumber, TArray<FVector, TInlineAllocator<9> >& OutVectors);

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FAOSampleData2,)
	SHADER_PARAMETER_ARRAY(FVector4,SampleDirections,[NumConeSampleDirections])
END_GLOBAL_SHADER_PARAMETER_STRUCT()

inline float GetMaxAOViewDistance()
{
	extern float GAOMaxViewDistance;
	// Scene depth stored in fp16 alpha, must fade out before it runs out of range
	// The fade extends past GAOMaxViewDistance a bit
	return FMath::Min(GAOMaxViewDistance, 65000.0f);
}

class FAOParameters
{
	DECLARE_TYPE_LAYOUT(FAOParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		AOObjectMaxDistance.Bind(ParameterMap,TEXT("AOObjectMaxDistance"));
		AOStepScale.Bind(ParameterMap,TEXT("AOStepScale"));
		AOStepExponentScale.Bind(ParameterMap,TEXT("AOStepExponentScale"));
		AOMaxViewDistance.Bind(ParameterMap,TEXT("AOMaxViewDistance"));
		AOGlobalMaxOcclusionDistance.Bind(ParameterMap,TEXT("AOGlobalMaxOcclusionDistance"));
	}

	friend FArchive& operator<<(FArchive& Ar,FAOParameters& Parameters)
	{
		Ar << Parameters.AOObjectMaxDistance;
		Ar << Parameters.AOStepScale;
		Ar << Parameters.AOStepExponentScale;
		Ar << Parameters.AOMaxViewDistance;
		Ar << Parameters.AOGlobalMaxOcclusionDistance;
		return Ar;
	}

	template<typename ShaderRHIParamRef>
	void Set(FRHICommandList& RHICmdList, const ShaderRHIParamRef ShaderRHI, const FDistanceFieldAOParameters& Parameters)
	{
		SetShaderValue(RHICmdList, ShaderRHI, AOObjectMaxDistance, Parameters.ObjectMaxOcclusionDistance);

		extern float GAOConeHalfAngle;
		const float AOLargestSampleOffset = Parameters.ObjectMaxOcclusionDistance / (1 + FMath::Tan(GAOConeHalfAngle));

		extern float GAOStepExponentScale;
		extern uint32 GAONumConeSteps;
		float AOStepScaleValue = AOLargestSampleOffset / FMath::Pow(2.0f, GAOStepExponentScale * (GAONumConeSteps - 1));
		SetShaderValue(RHICmdList, ShaderRHI, AOStepScale, AOStepScaleValue);

		SetShaderValue(RHICmdList, ShaderRHI, AOStepExponentScale, GAOStepExponentScale);

		SetShaderValue(RHICmdList, ShaderRHI, AOMaxViewDistance, GetMaxAOViewDistance());

		const float GlobalMaxOcclusionDistance = Parameters.GlobalMaxOcclusionDistance;
		SetShaderValue(RHICmdList, ShaderRHI, AOGlobalMaxOcclusionDistance, GlobalMaxOcclusionDistance);
	}

private:
	
		LAYOUT_FIELD(FShaderParameter, AOObjectMaxDistance)
		LAYOUT_FIELD(FShaderParameter, AOStepScale)
		LAYOUT_FIELD(FShaderParameter, AOStepExponentScale)
		LAYOUT_FIELD(FShaderParameter, AOMaxViewDistance)
		LAYOUT_FIELD(FShaderParameter, AOGlobalMaxOcclusionDistance)
	
};

class FDFAOUpsampleParameters
{
	DECLARE_TYPE_LAYOUT(FDFAOUpsampleParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		BentNormalAOTexture.Bind(ParameterMap, TEXT("BentNormalAOTexture"));
		BentNormalAOSampler.Bind(ParameterMap, TEXT("BentNormalAOSampler"));
		AOBufferBilinearUVMax.Bind(ParameterMap, TEXT("AOBufferBilinearUVMax"));
		DistanceFadeScale.Bind(ParameterMap, TEXT("DistanceFadeScale"));
		AOMaxViewDistance.Bind(ParameterMap, TEXT("AOMaxViewDistance"));
	}

	void Set(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, const FViewInfo& View, const TRefCountPtr<IPooledRenderTarget>& DistanceFieldAOBentNormal)
	{
		FRHITexture* BentNormalAO = DistanceFieldAOBentNormal ? DistanceFieldAOBentNormal->GetRenderTargetItem().ShaderResourceTexture : GWhiteTexture->TextureRHI;
		SetTextureParameter(RHICmdList, ShaderRHI, BentNormalAOTexture, BentNormalAOSampler, TStaticSamplerState<SF_Bilinear>::GetRHI(), BentNormalAO);

		FIntPoint const AOBufferSize = GetBufferSizeForAO();
		FVector2D const UVMax(
			(View.ViewRect.Width() / GAODownsampleFactor - 0.51f) / AOBufferSize.X, // 0.51 - so bilateral gather4 won't sample invalid texels
			(View.ViewRect.Height() / GAODownsampleFactor - 0.51f) / AOBufferSize.Y);
		SetShaderValue(RHICmdList, ShaderRHI, AOBufferBilinearUVMax, UVMax);

		SetShaderValue(RHICmdList, ShaderRHI, AOMaxViewDistance, GetMaxAOViewDistance());

		extern float GAOViewFadeDistanceScale;
		const float DistanceFadeScaleValue = 1.0f / ((1.0f - GAOViewFadeDistanceScale) * GetMaxAOViewDistance());
		SetShaderValue(RHICmdList, ShaderRHI, DistanceFadeScale, DistanceFadeScaleValue);
	}

	/** Serializer. */
	friend FArchive& operator<<(FArchive& Ar, FDFAOUpsampleParameters& P)
	{
		Ar << P.BentNormalAOTexture;
		Ar << P.BentNormalAOSampler;
		Ar << P.AOBufferBilinearUVMax;
		Ar << P.DistanceFadeScale;
		Ar << P.AOMaxViewDistance;

		return Ar;
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, BentNormalAOTexture);
	LAYOUT_FIELD(FShaderResourceParameter, BentNormalAOSampler);
	LAYOUT_FIELD(FShaderParameter, AOBufferBilinearUVMax);
	LAYOUT_FIELD(FShaderParameter, DistanceFadeScale);
	LAYOUT_FIELD(FShaderParameter, AOMaxViewDistance);
};

class FMaxSizedRWBuffers : public FRenderResource
{
public:
	FMaxSizedRWBuffers()
	{
		MaxSize = 0;
	}

	virtual void InitDynamicRHI()
	{
		check(0);
	}

	virtual void ReleaseDynamicRHI()
	{
		check(0);
	}

	void AllocateFor(int32 InMaxSize)
	{
		bool bReallocate = false;

		if (InMaxSize > MaxSize)
		{
			MaxSize = InMaxSize;
			bReallocate = true;
		}

		if (!IsInitialized())
		{
			InitResource();
		}
		else if (bReallocate)
		{
			UpdateRHI();
		}
	}

	int32 GetMaxSize() const { return MaxSize; }

protected:
	int32 MaxSize;
};

class FScreenGridParameters
{
	DECLARE_TYPE_LAYOUT(FScreenGridParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		BaseLevelTexelSize.Bind(ParameterMap, TEXT("BaseLevelTexelSize"));
		JitterOffset.Bind(ParameterMap, TEXT("JitterOffset"));
		ScreenGridConeVisibilitySize.Bind(ParameterMap, TEXT("ScreenGridConeVisibilitySize"));
		DistanceFieldNormalTexture.Bind(ParameterMap, TEXT("DistanceFieldNormalTexture"));
		DistanceFieldNormalSampler.Bind(ParameterMap, TEXT("DistanceFieldNormalSampler"));
	}

	template<typename TParamRef>
	void Set(FRHICommandList& RHICmdList, const TParamRef& ShaderRHI, const FViewInfo& View, FSceneRenderTargetItem& DistanceFieldNormal)
	{
		const FIntPoint DownsampledBufferSize = GetBufferSizeForAO();
		const FVector2D BaseLevelTexelSizeValue(1.0f / DownsampledBufferSize.X, 1.0f / DownsampledBufferSize.Y);
		SetShaderValue(RHICmdList, ShaderRHI, BaseLevelTexelSize, BaseLevelTexelSizeValue);

		extern FVector2D GetJitterOffset(int32 SampleIndex);
		SetShaderValue(RHICmdList, ShaderRHI, JitterOffset, GetJitterOffset(View.ViewState->GetDistanceFieldTemporalSampleIndex()));

		FAOScreenGridResources* ScreenGridResources = View.ViewState->AOScreenGridResources;

		SetShaderValue(RHICmdList, ShaderRHI, ScreenGridConeVisibilitySize, ScreenGridResources->ScreenGridDimensions);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			DistanceFieldNormalTexture,
			DistanceFieldNormalSampler,
			TStaticSamplerState<SF_Point,AM_Wrap,AM_Wrap,AM_Wrap>::GetRHI(),
			DistanceFieldNormal.ShaderResourceTexture
			);
	}

	friend FArchive& operator<<(FArchive& Ar,FScreenGridParameters& P)
	{
		Ar << P.BaseLevelTexelSize << P.JitterOffset << P.ScreenGridConeVisibilitySize << P.DistanceFieldNormalTexture << P.DistanceFieldNormalSampler;
		return Ar;
	}

private:
	
		LAYOUT_FIELD(FShaderParameter, BaseLevelTexelSize)
		LAYOUT_FIELD(FShaderParameter, JitterOffset)
		LAYOUT_FIELD(FShaderParameter, ScreenGridConeVisibilitySize)
		LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldNormalTexture)
		LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldNormalSampler)
	
};

extern void TrackGPUProgress(FRHICommandListImmediate& RHICmdList, uint32 DebugId);

extern bool ShouldRenderDeferredDynamicSkyLight(const FScene* Scene, const FSceneViewFamily& ViewFamily);

extern void CullObjectsToView(FRHICommandListImmediate& RHICmdList, FScene* Scene, const FViewInfo& View, const FDistanceFieldAOParameters& Parameters, FDistanceFieldObjectBufferResource& CulledObjectBuffers);
extern void BuildTileObjectLists(FRHICommandListImmediate& RHICmdList, FScene* Scene, TArray<FViewInfo>& Views, FSceneRenderTargetItem& DistanceFieldNormal, const FDistanceFieldAOParameters& Parameters);
extern FIntPoint GetTileListGroupSizeForView(const FViewInfo& View);
