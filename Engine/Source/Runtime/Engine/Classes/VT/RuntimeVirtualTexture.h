// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureDefines.h"
#include "UObject/ObjectMacros.h"
#include "VirtualTexturing.h"
#include "VT/RuntimeVirtualTextureEnum.h"
#include "RuntimeVirtualTexture.generated.h"

/** Runtime virtual texture UObject */
UCLASS(ClassGroup = Rendering, BlueprintType)
class ENGINE_API URuntimeVirtualTexture : public UObject
{
	GENERATED_UCLASS_BODY()
	~URuntimeVirtualTexture();

protected:
	/** Contents of virtual texture. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Layout, meta = (DisplayName = "Virtual texture content"), AssetRegistrySearchable)
	ERuntimeVirtualTextureMaterialType MaterialType = ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular;

	/** Enable storing the virtual texture in GPU supported compression formats. Using uncompressed is only recommended for debugging and quality comparisons. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Layout, meta = (DisplayName = "Enable BC texture compression"))
	bool bCompressTextures = true;

	/** Enable usage of the virtual texture. When disabled there is no rendering into the virtual texture, and sampling will return zero values. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Layout, meta = (DisplayName = "Enable virtual texture"))
	bool bEnable = true;

	/** Enable clear before rendering a page of the virtual texture. Disabling this can be an optimization if you know that the texture will always be fully covered by rendering.  */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Layout, meta = (DisplayName = "Enable clear before render"))
	bool bClearTextures = true;

	/** Enable continuous update of the virtual texture pages. This round-robin updates already mapped pages and can help fix pages that are mapped before dependent textures are fully streamed in.  */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Layout, meta = (DisplayName = "Enable continuous page updates"))
	bool bContinuousUpdate = false;

	/** Enable page table channel packing. This reduces page table memory and update cost but can reduce the ability to share physical memory with other virtual textures.  */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Layout, meta = (DisplayName = "Enable packed page table"))
	bool bSinglePhysicalSpace = true;

	/** Enable private page table allocation. This can reduce total page table memory allocation but can also reduce the total number of virtual textures supported. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Layout, meta = (DisplayName = "Enable private page table"))
	bool bPrivateSpace = true;

	/** Number of low mips to cut from the virtual texture. This can reduce peak virtual texture update cost but will also increase the probability of mip shimmering. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = Layout, meta = (UIMin = "0", UIMax = "6", DisplayName = "Number of low mips to remove from the virtual texture"))
	int32 RemoveLowMips = 0;

	/** Size of virtual texture along the largest axis. (Actual values increase in powers of 2) */
	UPROPERTY()
	int32 Size_DEPRECATED = -1;

	/** 
	 * Size of virtual texture in tiles. (Actual values increase in powers of 2).
	 * This replaces the deprecated Size property.
	 * This is applied to the largest axis in world space and the size for any shorter axis is chosen to maintain aspect ratio.  
	 */
	UPROPERTY(EditAnywhere, BluePrintGetter = GetTileCount, Category = Size, meta = (UIMin = "0", UIMax = "12", DisplayName = "Size of the virtual texture in tiles"))
	int32 TileCount = 8; // 256

	/** Page tile size. (Actual values increase in powers of 2) */
	UPROPERTY(EditAnywhere, BluePrintGetter = GetTileSize, Category = Size, meta = (UIMin = "0", UIMax = "4", DisplayName = "Size of each virtual texture tile"))
	int32 TileSize = 2; // 256

	/** Page tile border size divided by 2 (Actual values increase in multiples of 2). Higher values trigger a higher anisotropic sampling level. */
	UPROPERTY(EditAnywhere, BluePrintGetter = GetTileBorderSize, Category = Size, meta = (UIMin = "0", UIMax = "4", DisplayName = "Border padding for each virtual texture tile"))
	int32 TileBorderSize = 2; // 4

	/** Texture group this texture belongs to */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = LevelOfDetail, meta = (DisplayName = "Texture Group"), AssetRegistrySearchable)
	TEnumAsByte<enum TextureGroup> LODGroup;

	/** Deprecated texture object containing streamed low mips. */
	UPROPERTY()
	class URuntimeVirtualTextureStreamingProxy* StreamingTexture_DEPRECATED = nullptr;

public:
	/** Public getter for enabled status */
	bool GetEnabled() { return bEnable; }

	/** Get the material set that this virtual texture stores. */
	ERuntimeVirtualTextureMaterialType GetMaterialType() const { return MaterialType; }

	/** Public getter for virtual texture size */
	UFUNCTION(BlueprintPure, Category = Size)
	int32 GetSize() const { return GetTileCount() * GetTileSize(); }
	/** Public getter for virtual texture tile count */
	UFUNCTION(BlueprintGetter)
	int32 GetTileCount() const { return GetTileCount(TileCount); }
	static int32 GetTileCount(int32 InTileCount) { return 1 << FMath::Clamp(InTileCount, 0, 12); }
	/** Public getter for virtual texture tile size */
	UFUNCTION(BlueprintGetter)
	int32 GetTileSize() const { return GetTileSize(TileSize); }
	static int32 GetTileSize(int32 InTileSize) { return 1 << FMath::Clamp(InTileSize + 6, 6, 10); }
	/** Public getter for virtual texture tile border size */
	UFUNCTION(BlueprintGetter)
	int32 GetTileBorderSize() const { return 2 * FMath::Clamp(TileBorderSize, 0, 4); }
	
	/** Public getter for texture LOD Group */
	TEnumAsByte<enum TextureGroup> GetLODGroup() const { return LODGroup; }

	/** Get if this virtual texture uses compressed texture formats. */
	bool GetCompressTextures() const { return bCompressTextures; }
	/** Public getter for virtual texture removed low mips */
	int32 GetRemoveLowMips() const { return FMath::Clamp(RemoveLowMips, 0, 5); }
	/** Public getter for virtual texture using single physical space flag. */
	bool GetSinglePhysicalSpace() const { return bSinglePhysicalSpace; }

#if WITH_EDITOR
	/** Returns an approximate estimated value for the memory used by the page table texture. */
	int32 GetEstimatedPageTableTextureMemoryKb() const;
	/** Returns an approximate estimated value for the memory used by the physical texture. */
	int32 GetEstimatedPhysicalTextureMemoryKb() const;
#endif

	/** Get virtual texture description based on the properties of this object and the passed in volume transform. */
	void GetProducerDescription(FVTProducerDescription& OutDesc, FTransform const& VolumeToWorld) const;

	/** Returns number of texture layers in the virtual texture */
	int32 GetLayerCount() const;
	/** Returns number of texture layers in the virtual texture of a given material type */
	static int32 GetLayerCount(ERuntimeVirtualTextureMaterialType InMaterialType);
	/** Returns the texture format for the virtual texture layer */
	EPixelFormat GetLayerFormat(int32 LayerIndex) const;
	/** Return true if the virtual texture layer should be sampled as sRGB */
	bool IsLayerSRGB(int32 LayerIndex) const;
	/** Return true if the virtual texture layer should be sampled as YCoCg */
	bool IsLayerYCoCg(int32 LayerIndex) const;
	/** Returns true if texture pages should be cleared before render */
	bool GetClearTextures() const { return bClearTextures; }

	/** (Re)Initialize this object. Call this whenever we modify the producer or transform. */
	void Initialize(IVirtualTexture* InProducer, FTransform const& VolumeToWorld, FBox const& WorldBounds);

	/** Release the resources for this object This will need to be called if our producer becomes stale and we aren't doing a full reinit with a new producer. */
	void Release();

	/** Getter for the associated virtual texture producer. Call on render thread only. */
	FVirtualTextureProducerHandle GetProducerHandle() const;
	/** Getter for the associated virtual texture allocation. Call on render thread only. */
	IAllocatedVirtualTexture* GetAllocatedVirtualTexture() const;

	/** Getter for the shader uniform parameters. */
	FVector4 GetUniformParameter(int32 Index) const;

protected:
	/** Initialize the render resources. This kicks off render thread work. */
	void InitResource(IVirtualTexture* InProducer, FTransform const& VolumeToWorld);
	/** Initialize the render resources with a null producer. This kicks off render thread work. */
	void InitNullResource();

	//~ Begin UObject Interface.
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface.

private:
	/** Render thread resource container. */
	class FRuntimeVirtualTextureRenderResource* Resource;

	/** Material uniform parameters to support transform from world to UV coordinates. */
	FVector4 WorldToUVTransformParameters[3];
	/** Material uniform parameter used to pack world height. */
	FVector4 WorldHeightUnpackParameter;
};

class UVirtualTexture2D;

namespace RuntimeVirtualTexture
{
	/** Helper function to wrap a runtime virtual texture producer with a streaming producer. */
	ENGINE_API IVirtualTexture* CreateStreamingTextureProducer(
		IVirtualTexture* InProducer,
		FVTProducerDescription const& InProducerDesc,
		UVirtualTexture2D* InStreamingTexture,
		int32 InMaxLevel,
		int32& OutTransitionLevel);
}
