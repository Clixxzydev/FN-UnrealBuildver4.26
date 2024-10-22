// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VT/RuntimeVirtualTextureEnum.h"

class FRHICommandListImmediate;
class FRHITexture2D;
class FRHIUnorderedAccessView;
class FRDGBuilder;
class FRDGTexture;
class FRDGTextureUAV;
class FScene;
class URuntimeVirtualTextureComponent;

namespace RuntimeVirtualTexture
{
#if WITH_EDITOR
	/**
	 * Get the scene index of the FRuntimeVirtualTextureSceneProxy associated with a URuntimeVirtualTextureComponent.
	 * This is needed when rendering runtime virtual texture pages in alternative contexts such as when building previews etc.
	 * This function is slow because it needs to flush render commands.
	 */
	RENDERER_API uint32 GetRuntimeVirtualTextureSceneIndex_GameThread(URuntimeVirtualTextureComponent* InComponent);
#endif

	/** Enum for our maximum RenderPages() batch size. */
	enum { EMaxRenderPageBatch = 8 };

	/** Structure containing a texture layer target description for a call for RenderPages(). */
	struct FRenderPageTarget
	{
		/** Physical texture to render to. */
		FRHITexture2D* Texture = nullptr;
		/** Unordered access view of physical texture to render to. If this exists we can render to it directly instead of using RHICopyTexture(). */
		FRHIUnorderedAccessView* UAV = nullptr;
	};

	/** A single page description. Multiple of these can be placed in a single FRenderPageBatchDesc batch description. */
	struct FRenderPageDesc
	{
		/** vLevel to render at. */
		uint8 vLevel;
		/** UV range to render in virtual texture space. */
		FBox2D UVRange;
		/** Destination box to render in texel space of the target physical texture. */
		FBox2D DestBox[RuntimeVirtualTexture::MaxTextureLayers];
	};

	/** A description of a batch of pages to be rendered with a single call to RenderPages(). */
	struct FRenderPageBatchDesc
	{
		/** Scene to use when rendering the batch. */
		FScene* Scene;
		/** Mask created from the target runtime virtual texture's index within the scene. */
		uint32 RuntimeVirtualTextureMask;
		/** Virtual texture UV space to world space transform. */
		FTransform UVToWorld;
		/** Virtual texture world space bounds. */
		FBox WorldBounds;
		/** Material type of the runtime virtual texture that we are rendering. */
		ERuntimeVirtualTextureMaterialType MaterialType;
		/** Max mip level of the runtime virtual texture that we are rendering. */
		uint8 MaxLevel;
		/** Set to true to clear before rendering. */
		bool bClearTextures;
		/** Set to true for thumbnail rendering. */
		bool bIsThumbnails;
		/** Debug visualization to render with. */
		ERuntimeVirtualTextureDebugType DebugType;

		/** Physical texture targets to render to. */
		FRenderPageTarget Targets[RuntimeVirtualTexture::MaxTextureLayers];
		
		/** Number of pages to render. */
		int32 NumPageDescs;
		/** Page descriptions for each page in the batch. */
		FRenderPageDesc PageDescs[EMaxRenderPageBatch];
	};

	/** Returns true if the FScene is initialized for rendering to runtime virtual textures. Always check this before calling RenderPages(). */
	RENDERER_API bool IsSceneReadyToRender(FScene* Scene);

	/** Render a batch of pages for a runtime virtual texture. */
	RENDERER_API void RenderPages(FRHICommandListImmediate& RHICmdList, FRenderPageBatchDesc const& InDesc);

	/**
	 * Utility funtion to downsample a height texture and then pack and write the MinMax values to a texel in the destination texture.
	 * SrcTexture is expected to by G16 and DstTexture is expected to be RGBA8 packed as 16 bit min and max split across the 8 bit channels.
	 */
	RENDERER_API void DownsampleMinMaxAndCopy(FRDGBuilder& GraphBuilder, FRDGTexture* SrcTexture, FIntPoint SrcSize, FRDGTextureUAV* DstTexture, FIntPoint DstCoord);
	/** Utility function to generate all additional mips from mip0 for a MinMax height texture already packed in RGBA8. */
	RENDERER_API void GenerateMinMaxTextureMips(FRDGBuilder& GraphBuilder, FRDGTexture* Texture, FIntPoint SrcSize, int32 NumMips);
}
