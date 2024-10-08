// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightmapPreviewVirtualTexture.h"
#include "GPULightmassCommon.h"
#include "EngineModule.h"
#include "Scene/Lights.h"
#include "LightmapRenderer.h"

namespace GPULightmass
{

FLightmapPreviewVirtualTexture::FLightmapPreviewVirtualTexture(FLightmapRenderStateRef LightmapRenderState, FLightmapRenderer* Renderer)
	: LightmapRenderState(LightmapRenderState)
	, LightmapRenderer(Renderer)
{
	FIntPoint SizeInTiles = LightmapRenderState->GetPaddedSizeInTiles();

	FVTProducerDescription ProducerDesc;

	{
		ProducerDesc.bPersistentHighestMip = false;
		ProducerDesc.bContinuousUpdate = true;
		ProducerDesc.Dimensions = 2;
		ProducerDesc.TileSize = GPreviewLightmapVirtualTileSize;
		ProducerDesc.TileBorderSize = GPreviewLightmapTileBorderSize;
		ProducerDesc.BlockWidthInTiles = SizeInTiles.X;
		ProducerDesc.BlockHeightInTiles = SizeInTiles.Y;
		ProducerDesc.DepthInTiles = 1;
		ProducerDesc.NumTextureLayers = 3; // LightMapVirtualTexture->TypeToLayer.Num();
		ProducerDesc.NumPhysicalGroups = 3;
		ProducerDesc.LayerFormat[0] = EPixelFormat::PF_A32B32G32R32F;
		ProducerDesc.LayerFormat[1] = EPixelFormat::PF_A32B32G32R32F;
		ProducerDesc.LayerFormat[2] = EPixelFormat::PF_A32B32G32R32F;
		ProducerDesc.PhysicalGroupIndex[0] = 0;
		ProducerDesc.PhysicalGroupIndex[1] = 1;
		ProducerDesc.PhysicalGroupIndex[2] = 2;
		ProducerDesc.MaxLevel = FMath::Min((int32)FMath::CeilLogTwo((uint32)FMath::Min(SizeInTiles.X, SizeInTiles.Y)), GPreviewLightmapMipmapMaxLevel);

		ProducerHandle = GetRendererModule().RegisterVirtualTextureProducer(ProducerDesc, this);
	}

	{
		FAllocatedVTDescription Desc;

		Desc.Dimensions = ProducerDesc.Dimensions;
		Desc.TileSize = ProducerDesc.TileSize;
		Desc.TileBorderSize = ProducerDesc.TileBorderSize;
		Desc.NumTextureLayers = ProducerDesc.NumTextureLayers;

		for (uint32 LayerIndex = 0u; LayerIndex < Desc.NumTextureLayers; ++LayerIndex)
		{
			Desc.ProducerHandle[LayerIndex] = ProducerHandle;
			Desc.ProducerLayerIndex[LayerIndex] = LayerIndex;
		}

		AllocatedVT = GetRendererModule().AllocateVirtualTexture(Desc);
		ensure(AllocatedVT->GetVirtualAddress() != ~0u);
	}
}

FVTRequestPageResult FLightmapPreviewVirtualTexture::RequestPageData(
	const FVirtualTextureProducerHandle& InProducerHandle,
	uint8 LayerMask,
	uint8 vLevel,
	uint32 vAddress,
	EVTRequestPagePriority Priority)
{
	return FVTRequestPageResult(EVTRequestPageStatus::Available, 0);
}

IVirtualTextureFinalizer* FLightmapPreviewVirtualTexture::ProducePageData(
	FRHICommandListImmediate& RHICmdList,
	ERHIFeatureLevel::Type FeatureLevel,
	EVTProducePageFlags Flags,
	const FVirtualTextureProducerHandle& InProducerHandle,
	uint8 LayerMask,
	uint8 vLevel,
	uint32 vAddress,
	uint64 RequestHandle,
	const FVTProduceTargetLayer* TargetLayers)
{
	FLightmapTileRequest TileRequest(LightmapRenderState, FTileVirtualCoordinates(vAddress, vLevel));

	if (!LightmapRenderState->IsTileCoordinatesValid(TileRequest.VirtualCoordinates))
	{
		return nullptr;
	}

	for (int32 LayerIndex = 0; LayerIndex < 8; LayerIndex++)
	{
		if (LayerMask & (1u << LayerIndex))
		{
			TileRequest.OutputPhysicalCoordinates[LayerIndex] = FIntPoint(TargetLayers[LayerIndex].pPageLocation.X, TargetLayers[LayerIndex].pPageLocation.Y);
			TileRequest.OutputRenderTargets[LayerIndex] = TargetLayers[LayerIndex].PooledRenderTarget;
		}
	}

	check(TileRequest.OutputRenderTargets[0] != nullptr || TileRequest.OutputRenderTargets[1] != nullptr || TileRequest.OutputRenderTargets[2] != nullptr);

	LightmapRenderer->AddRequest(TileRequest);

	return LightmapRenderer;
}

}
