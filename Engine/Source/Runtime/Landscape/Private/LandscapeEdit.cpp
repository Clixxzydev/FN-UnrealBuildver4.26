// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LandscapeEdit.cpp: Landscape editing
=============================================================================*/

#include "LandscapeEdit.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/FeedbackContext.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Landscape.h"
#include "LandscapeStreamingProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionLandscapeVisibilityMask.h"
#include "Materials/MaterialExpressionLandscapeLayerWeight.h"
#include "Materials/MaterialExpressionLandscapeLayerSample.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionLandscapeLayerSwitch.h"
#include "LandscapeDataAccess.h"
#include "LandscapeRender.h"
#include "LandscapeRenderMobile.h"
#include "Materials/MaterialInstanceConstant.h"
#include "LandscapeMaterialInstanceConstant.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeMeshCollisionComponent.h"
#include "LandscapeGizmoActiveActor.h"
#include "InstancedFoliageActor.h"
#include "LevelUtils.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Misc/MapErrors.h"
#include "LandscapeSplinesComponent.h"
#include "Serialization/MemoryWriter.h"
#if WITH_EDITOR
#include "StaticMeshAttributes.h"
#include "MeshUtilitiesCommon.h"

#include "EngineModule.h"
#include "EngineUtils.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "LandscapeEditorModule.h"
#include "LandscapeFileFormatInterface.h"
#include "ComponentRecreateRenderStateContext.h"
#include "ComponentReregisterContext.h"
#include "Interfaces/ITargetPlatform.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#endif
#include "Algo/Count.h"
#include "Serialization/MemoryWriter.h"
#include "Engine/Canvas.h"

DEFINE_LOG_CATEGORY(LogLandscape);
DEFINE_LOG_CATEGORY(LogLandscapeBP);

#define LOCTEXT_NAMESPACE "Landscape"

int32 GMobileCompressLandscapeWeightMaps = 0;
FAutoConsoleVariableRef CVarMobileCompressLanscapeWeightMaps(
    TEXT("r.Mobile.CompressLandscapeWeightMaps"),
    GMobileCompressLandscapeWeightMaps,
    TEXT("Whether to compress the terrain weight maps for mobile."),
    ECVF_ReadOnly
);

#if WITH_EDITOR

// Used to temporarily disable material instance updates (typically used for cases where multiple updates are called on sample component)
// Instead, one call per component is done at the end
LANDSCAPE_API bool GDisableUpdateLandscapeMaterialInstances = false;

// Channel remapping
extern const size_t ChannelOffsets[4];

ULandscapeLayerInfoObject* ALandscapeProxy::VisibilityLayer = nullptr;

void ULandscapeComponent::Init(int32 InBaseX, int32 InBaseY, int32 InComponentSizeQuads, int32 InNumSubsections, int32 InSubsectionSizeQuads)
{
	SetSectionBase(FIntPoint(InBaseX, InBaseY));
	SetRelativeLocation(FVector(GetSectionBase() - GetLandscapeProxy()->LandscapeSectionOffset));
	ComponentSizeQuads = InComponentSizeQuads;
	NumSubsections = InNumSubsections;
	SubsectionSizeQuads = InSubsectionSizeQuads;
	check(NumSubsections * SubsectionSizeQuads == ComponentSizeQuads);
	ULandscapeInfo* Info = GetLandscapeInfo();
}

void ULandscapeComponent::UpdateCachedBounds(bool bInApproximateBounds)
{
	// Update local-space bounding box
	CachedLocalBox.Init();
	if (bInApproximateBounds && GetLandscapeProxy()->HasLayersContent())
	{
		FVector MinBox(0, 0, LandscapeDataAccess::GetLocalHeight(0));
		FVector MaxBox(ComponentSizeQuads + 1, ComponentSizeQuads + 1, LandscapeDataAccess::GetLocalHeight(UINT16_MAX));
		CachedLocalBox = FBox(MinBox, MaxBox);
	}
	else
	{
		const int32 MipLevel = 0;
		const bool bWorkOnEditingLayer = false; // We never want to compute bounds based on anything else that final landscape layer's height data
		FLandscapeComponentDataInterface CDI(this, MipLevel, bWorkOnEditingLayer);

		for (int32 y = 0; y < ComponentSizeQuads + 1; y++)
		{
			for (int32 x = 0; x < ComponentSizeQuads + 1; x++)
			{
				CachedLocalBox += CDI.GetLocalVertex(x, y);
			}
		}
	}
	if (CachedLocalBox.GetExtent().Z == 0)
	{
		// expand bounds to avoid flickering issues with zero-size bounds
		CachedLocalBox.ExpandBy(FVector(0, 0, 1));
	}

	// Update collision component bounds
	ULandscapeHeightfieldCollisionComponent* HFCollisionComponent = CollisionComponent.Get();
	if (HFCollisionComponent)
	{
        // In Landscape Layers the Collision Component is slave and doesn't need to be transacted
		if (!GetLandscapeProxy()->HasLayersContent())
		{
			HFCollisionComponent->Modify();
		}
		HFCollisionComponent->CachedLocalBox = CachedLocalBox;
		HFCollisionComponent->UpdateComponentToWorld();
	}
}

void ULandscapeComponent::UpdateNavigationRelevance()
{
	ALandscapeProxy* Proxy = GetLandscapeProxy();
	if (CollisionComponent && Proxy)
	{
		CollisionComponent->SetCanEverAffectNavigation(Proxy->bUsedForNavigation);
	}
}

void ULandscapeComponent::UpdateRejectNavmeshUnderneath()
{
	ALandscapeProxy* Proxy = GetLandscapeProxy();
	if (CollisionComponent && Proxy)
	{
		CollisionComponent->bFillCollisionUnderneathForNavmesh = Proxy->bFillCollisionUnderLandscapeForNavmesh;
	}
}

ULandscapeMaterialInstanceConstant* ALandscapeProxy::GetLayerThumbnailMIC(UMaterialInterface* LandscapeMaterial, FName LayerName, UTexture2D* ThumbnailWeightmap, UTexture2D* ThumbnailHeightmap, ALandscapeProxy* Proxy)
{
	if (!LandscapeMaterial)
	{
		LandscapeMaterial = Proxy ? Proxy->GetLandscapeMaterial() : UMaterial::GetDefaultMaterial(MD_Surface);
	}

	FlushRenderingCommands();

	ULandscapeMaterialInstanceConstant* MaterialInstance = NewObject<ULandscapeMaterialInstanceConstant>(GetTransientPackage());
	MaterialInstance->bIsLayerThumbnail = true;
	MaterialInstance->bMobile = false;
	MaterialInstance->SetParentEditorOnly(LandscapeMaterial, false);

	FStaticParameterSet StaticParameters;
	MaterialInstance->GetStaticParameterValues(StaticParameters);

	for (int32 LayerParameterIdx = 0; LayerParameterIdx < StaticParameters.TerrainLayerWeightParameters.Num(); ++LayerParameterIdx)
	{
		FStaticTerrainLayerWeightParameter& LayerParameter = StaticParameters.TerrainLayerWeightParameters[LayerParameterIdx];
		if (LayerParameter.ParameterInfo.Name == LayerName)
		{
			LayerParameter.WeightmapIndex = 0;
			LayerParameter.bOverride = true;
		}
		else
		{
			LayerParameter.WeightmapIndex = INDEX_NONE;
		}
	}

	// Don't recreate the render state of everything, only update the materials context
	{
		FMaterialUpdateContext MaterialUpdateContext(FMaterialUpdateContext::EOptions::Default & ~FMaterialUpdateContext::EOptions::RecreateRenderStates);
		MaterialInstance->UpdateStaticPermutation(StaticParameters, &MaterialUpdateContext);
	}
	
	FLinearColor Mask(1.0f, 0.0f, 0.0f, 0.0f);
	MaterialInstance->SetVectorParameterValueEditorOnly(FName(*FString::Printf(TEXT("LayerMask_%s"), *LayerName.ToString())), Mask);
	MaterialInstance->SetTextureParameterValueEditorOnly(FName(TEXT("Weightmap0")), ThumbnailWeightmap);
	MaterialInstance->SetTextureParameterValueEditorOnly(FName(TEXT("Heightmap")), ThumbnailHeightmap);

	MaterialInstance->PostEditChange();

	return MaterialInstance;
}

/**
* Generate a key for this component's layer allocations to use with MaterialInstanceConstantMap.
*/
FString ULandscapeComponent::GetLayerAllocationKey(const TArray<FWeightmapLayerAllocationInfo>& Allocations, UMaterialInterface* LandscapeMaterial, bool bMobile /*= false*/)
{
	if (!LandscapeMaterial)
	{
		return FString();
	}

	FString Result = LandscapeMaterial->GetPathName();

	// Generate a string to describe each allocation
	TArray<FString> LayerStrings;
	for (int32 LayerIdx = 0; LayerIdx < Allocations.Num(); LayerIdx++)
	{
		const bool bNoWeightBlend = Allocations[LayerIdx].LayerInfo && Allocations[LayerIdx].LayerInfo->bNoWeightBlend;
		LayerStrings.Add(FString::Printf(TEXT("_%s_%s%d"), *Allocations[LayerIdx].GetLayerName().ToString(), bNoWeightBlend ? TEXT("n") : TEXT("w"), Allocations[LayerIdx].WeightmapTextureIndex));
	}
	// Sort them alphabetically so we can share across components even if the order is different
	LayerStrings.Sort(TGreater<FString>());

	for (int32 LayerIdx = 0; LayerIdx < LayerStrings.Num(); LayerIdx++)
	{
		Result += LayerStrings[LayerIdx];
	}

	if (bMobile)
	{
		Result += TEXT("M");
	}

	return Result;
}

UMaterialInstanceConstant* ULandscapeComponent::GetCombinationMaterial(FMaterialUpdateContext* InMaterialUpdateContext, const TArray<FWeightmapLayerAllocationInfo>& Allocations, int8 InLODIndex, bool bMobile /*= false*/) const
{
	check(GIsEditor);

	const bool bComponentHasHoles = ComponentHasVisibilityPainted();
	UMaterialInterface* const LandscapeMaterial = GetLandscapeMaterial(InLODIndex);
	UMaterialInterface* const HoleMaterial = bComponentHasHoles ? GetLandscapeHoleMaterial() : nullptr;
	UMaterialInterface* const MaterialToUse = bComponentHasHoles && HoleMaterial ? HoleMaterial : LandscapeMaterial;
	bool bOverrideBlendMode = bComponentHasHoles && !HoleMaterial && LandscapeMaterial->GetBlendMode() == BLEND_Opaque;

	if (bOverrideBlendMode)
	{
		UMaterial* Material = LandscapeMaterial->GetMaterial();
		if (Material && Material->bUsedAsSpecialEngineMaterial)
		{
			bOverrideBlendMode = false;
#if WITH_EDITOR
			static TWeakPtr<SNotificationItem> ExistingNotification;
			if (!ExistingNotification.IsValid())
			{
				// let the user know why they are not seeing holes
				FNotificationInfo Info(LOCTEXT("AssignLandscapeMaterial", "You must assign a regular, non-engine material to your landscape in order to see holes created with the visibility tool."));
				Info.ExpireDuration = 5.0f;
				Info.bUseSuccessFailIcons = true;
				ExistingNotification = TWeakPtr<SNotificationItem>(FSlateNotificationManager::Get().AddNotification(Info));
			}
#endif
			return nullptr;
		}
	}

	if (ensure(MaterialToUse != nullptr))
	{
		ALandscapeProxy* Proxy = GetLandscapeProxy();
		FString LayerKey = GetLayerAllocationKey(Allocations, MaterialToUse, bMobile);

		// Find or set a matching MIC in the Landscape's map.
		UMaterialInstanceConstant* CombinationMaterialInstance = Proxy->MaterialInstanceConstantMap.FindRef(*LayerKey);
		if (CombinationMaterialInstance == nullptr || CombinationMaterialInstance->Parent != MaterialToUse || GetOuter() != CombinationMaterialInstance->GetOuter())
		{
			FlushRenderingCommands();

			ULandscapeMaterialInstanceConstant* LandscapeCombinationMaterialInstance = NewObject<ULandscapeMaterialInstanceConstant>(GetOuter());
			LandscapeCombinationMaterialInstance->bMobile = bMobile;
			CombinationMaterialInstance = LandscapeCombinationMaterialInstance;
			UE_LOG(LogLandscape, Log, TEXT("Looking for key %s, making new combination %s"), *LayerKey, *CombinationMaterialInstance->GetName());
			Proxy->MaterialInstanceConstantMap.Add(*LayerKey, CombinationMaterialInstance);
			CombinationMaterialInstance->SetParentEditorOnly(MaterialToUse, false);

			CombinationMaterialInstance->BasePropertyOverrides.bOverride_BlendMode = bOverrideBlendMode;
			if (bOverrideBlendMode)
			{
				CombinationMaterialInstance->BasePropertyOverrides.BlendMode = bComponentHasHoles ? BLEND_Masked : BLEND_Opaque;
			}

			FStaticParameterSet StaticParameters;
			for (const FWeightmapLayerAllocationInfo& Allocation : Allocations)
			{
				if (Allocation.LayerInfo)
				{
					const FName LayerParameter = (Allocation.LayerInfo == ALandscapeProxy::VisibilityLayer) ? UMaterialExpressionLandscapeVisibilityMask::ParameterName : Allocation.LayerInfo->LayerName;
					StaticParameters.TerrainLayerWeightParameters.Add(FStaticTerrainLayerWeightParameter(LayerParameter, Allocation.WeightmapTextureIndex, true, FGuid(), !Allocation.LayerInfo->bNoWeightBlend));
				}
			}
			CombinationMaterialInstance->UpdateStaticPermutation(StaticParameters, InMaterialUpdateContext);

			CombinationMaterialInstance->PostEditChange();
		}

		return CombinationMaterialInstance;
	}
	return nullptr;
}

void ULandscapeComponent::UpdateMaterialInstances_Internal(FMaterialUpdateContext& Context)
{
	check(GIsEditor);

	int32 MaxLOD = FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1;
	TMap<UMaterialInterface*, int8> NewMaterialPerLOD;
	LODIndexToMaterialIndex.SetNumUninitialized(MaxLOD+1);
	int8 LastLODIndex = INDEX_NONE;

	UMaterialInterface* BaseMaterial = GetLandscapeMaterial();
	UMaterialInterface* LOD0Material = GetLandscapeMaterial(0);

	for (int32 LODIndex = 0; LODIndex <= MaxLOD; ++LODIndex)
	{
		UMaterialInterface* CurrentMaterial = GetLandscapeMaterial(LODIndex);

		// if we have a LOD0 override, do not let the base material override it, it should override everything!
		if (CurrentMaterial == BaseMaterial && BaseMaterial != LOD0Material)
		{
			CurrentMaterial = LOD0Material;
		}

		const int8* MaterialLOD = NewMaterialPerLOD.Find(CurrentMaterial);

		if (MaterialLOD != nullptr)
		{
			LODIndexToMaterialIndex[LODIndex] = *MaterialLOD > LastLODIndex ? *MaterialLOD : LastLODIndex;
		}
		else
		{
			int32 AddedIndex = NewMaterialPerLOD.Num();
			NewMaterialPerLOD.Add(CurrentMaterial, LODIndex);
			LODIndexToMaterialIndex[LODIndex] = AddedIndex;
			LastLODIndex = AddedIndex;
		}
	}

	MaterialPerLOD = NewMaterialPerLOD;

	MaterialInstances.SetNumZeroed(MaterialPerLOD.Num() * 2); // over allocate in case we are using tessellation
	MaterialIndexToDisabledTessellationMaterial.Init(INDEX_NONE, MaxLOD + 1);
	int8 TessellatedMaterialCount = 0;
	int8 MaterialIndex = 0;

	TArray<FWeightmapLayerAllocationInfo>& WeightmapBaseLayerAllocation = GetWeightmapLayerAllocations();
	TArray<UTexture2D*>& WeightmapBaseTexture = GetWeightmapTextures();
	UTexture2D* BaseHeightmap = GetHeightmap();

	for (auto It = MaterialPerLOD.CreateConstIterator(); It; ++It)
	{
		const int8 MaterialLOD = It.Value();

		// Find or set a matching MIC in the Landscape's map.
		UMaterialInstanceConstant* CombinationMaterialInstance = GetCombinationMaterial(&Context, WeightmapBaseLayerAllocation, MaterialLOD, false);

		if (CombinationMaterialInstance != nullptr)
		{
			// Create the instance for this component, that will use the layer combination instance.
			UMaterialInstanceConstant* MaterialInstance = NewObject<ULandscapeMaterialInstanceConstant>(GetOuter());
			MaterialInstances[MaterialIndex] = MaterialInstance;

			// Material Instances don't support Undo/Redo (the shader map goes out of sync and crashes happen)
			// so we call UpdateMaterialInstances() from ULandscapeComponent::PostEditUndo instead
			//MaterialInstance->SetFlags(RF_Transactional);
			//MaterialInstance->Modify();

			MaterialInstance->SetParentEditorOnly(CombinationMaterialInstance);
			MaterialInstance->ClearParameterValuesEditorOnly();
			Context.AddMaterialInstance(MaterialInstance); // must be done after SetParent

			FLinearColor Masks[4] = { FLinearColor(1.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 1.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 1.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 1.0f) };

			// Set the layer mask
			for (int32 AllocIdx = 0; AllocIdx < WeightmapBaseLayerAllocation.Num(); AllocIdx++)
			{
				FWeightmapLayerAllocationInfo& Allocation = WeightmapBaseLayerAllocation[AllocIdx];

				FName LayerName = Allocation.LayerInfo == ALandscapeProxy::VisibilityLayer ? UMaterialExpressionLandscapeVisibilityMask::ParameterName : Allocation.LayerInfo ? Allocation.LayerInfo->LayerName : NAME_None;
				MaterialInstance->SetVectorParameterValueEditorOnly(FName(*FString::Printf(TEXT("LayerMask_%s"), *LayerName.ToString())), Masks[Allocation.WeightmapTextureChannel]);
			}

			// Set the weightmaps
			for (int32 i = 0; i < WeightmapBaseTexture.Num(); i++)
			{
				MaterialInstance->SetTextureParameterValueEditorOnly(FName(*FString::Printf(TEXT("Weightmap%d"), i)), WeightmapBaseTexture[i]);
			}

			// Set the heightmap, if needed.
			if (BaseHeightmap)
			{
				MaterialInstance->SetTextureParameterValueEditorOnly(FName(TEXT("Heightmap")), BaseHeightmap);
			}
			MaterialInstance->PostEditChange();

			// Setup material instance with disabled tessellation
			if (CombinationMaterialInstance->GetMaterial()->D3D11TessellationMode != EMaterialTessellationMode::MTM_NoTessellation)
			{
				ULandscapeMaterialInstanceConstant* TessellationMaterialInstance = NewObject<ULandscapeMaterialInstanceConstant>(this);
				int32 TessellatedMaterialIndex = MaterialPerLOD.Num() + TessellatedMaterialCount++;
				MaterialInstances[TessellatedMaterialIndex] = TessellationMaterialInstance;
				MaterialIndexToDisabledTessellationMaterial[MaterialIndex] = TessellatedMaterialIndex;

				TessellationMaterialInstance->SetParentEditorOnly(MaterialInstance);
				Context.AddMaterialInstance(TessellationMaterialInstance); // must be done after SetParent
				TessellationMaterialInstance->bDisableTessellation = true;
				TessellationMaterialInstance->PostEditChange();
			}
		}

		++MaterialIndex;
	}

	MaterialInstances.Remove(nullptr);
	MaterialInstances.Shrink();

	if (MaterialPerLOD.Num() == 0)
	{
		MaterialInstances.Empty(1);
		MaterialInstances.Add(nullptr);
		LODIndexToMaterialIndex.Empty(1);
		LODIndexToMaterialIndex.Add(0);
	}

	// Update mobile combination material
	{
		GenerateMobileWeightmapLayerAllocations();

		MobileCombinationMaterialInstances.SetNumZeroed(MaterialPerLOD.Num());
		int8 MobileMaterialIndex = 0;

		for (auto It = MaterialPerLOD.CreateConstIterator(); It; ++It)
		{
			const int8 MaterialLOD = It.Value();

			UMaterialInstanceConstant* MobileCombinationMaterialInstance = GetCombinationMaterial(&Context, MobileWeightmapLayerAllocations, MaterialLOD, true);
			MobileCombinationMaterialInstances[MobileMaterialIndex] = MobileCombinationMaterialInstance;

			if (MobileCombinationMaterialInstance != nullptr)
			{
				Context.AddMaterialInstance(MobileCombinationMaterialInstance);
			}
						
			++MobileMaterialIndex;
		}
	}
}

void ULandscapeComponent::UpdateMaterialInstances()
{
	if (GDisableUpdateLandscapeMaterialInstances)
		return;

	// we're not having the material update context recreate the render state because we will manually do it for only this component
	TOptional<FComponentRecreateRenderStateContext> RecreateRenderStateContext;
	RecreateRenderStateContext.Emplace(this);
	TOptional<FMaterialUpdateContext> MaterialUpdateContext;
	MaterialUpdateContext.Emplace(FMaterialUpdateContext::EOptions::Default & ~FMaterialUpdateContext::EOptions::RecreateRenderStates);

	UpdateMaterialInstances_Internal(MaterialUpdateContext.GetValue());

	// End material update
	MaterialUpdateContext.Reset();

	// Recreate the render state for this component, needed to update the static drawlist which has cached the MaterialRenderProxies
	// Must be after the FMaterialUpdateContext is destroyed
	RecreateRenderStateContext.Reset();
}

void ULandscapeComponent::UpdateMaterialInstances(FMaterialUpdateContext& InOutMaterialContext, TArray<FComponentRecreateRenderStateContext>& InOutRecreateRenderStateContext)
{
	InOutRecreateRenderStateContext.Add(this);
	UpdateMaterialInstances_Internal(InOutMaterialContext);
}

void ALandscapeProxy::UpdateAllComponentMaterialInstances(FMaterialUpdateContext& InOutMaterialContext, TArray<FComponentRecreateRenderStateContext>& InOutRecreateRenderStateContext)
{
	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		Component->UpdateMaterialInstances(InOutMaterialContext, InOutRecreateRenderStateContext);
	}

}

void ALandscapeProxy::UpdateAllComponentMaterialInstances()
{
	// we're not having the material update context recreate render states because we will manually do it for only our components
	TArray<FComponentRecreateRenderStateContext> RecreateRenderStateContexts;
	RecreateRenderStateContexts.Reserve(LandscapeComponents.Num());

	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		RecreateRenderStateContexts.Emplace(Component);
	}
	TOptional<FMaterialUpdateContext> MaterialUpdateContext;
	MaterialUpdateContext.Emplace(FMaterialUpdateContext::EOptions::Default & ~FMaterialUpdateContext::EOptions::RecreateRenderStates);

	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		Component->UpdateMaterialInstances_Internal(MaterialUpdateContext.GetValue());
	}

	// End material update
	MaterialUpdateContext.Reset();

	// Recreate the render state for our components, needed to update the static drawlist which has cached the MaterialRenderProxies
	// Must be after the FMaterialUpdateContext is destroyed
	RecreateRenderStateContexts.Empty();
}

int32 ULandscapeComponent::GetNumMaterials() const
{
	return 1;
}

class UMaterialInterface* ULandscapeComponent::GetMaterial(int32 ElementIndex) const
{
	if (ensure(ElementIndex == 0))
	{
		return GetLandscapeMaterial(ElementIndex);
	}

	return nullptr;
}

void ULandscapeComponent::SetMaterial(int32 ElementIndex, class UMaterialInterface* Material)
{
	if (ensure(ElementIndex == 0))
	{
		GetLandscapeProxy()->LandscapeMaterial = Material;
	}
}

bool ULandscapeComponent::ComponentIsTouchingSelectionBox(const FBox& InSelBBox, const FEngineShowFlags& ShowFlags, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const
{
	if (ShowFlags.Landscape)
	{
		return Super::ComponentIsTouchingSelectionBox(InSelBBox, ShowFlags, bConsiderOnlyBSP, bMustEncompassEntireComponent);
	}

	return false;
}

bool ULandscapeComponent::ComponentIsTouchingSelectionFrustum(const FConvexVolume& InFrustum, const FEngineShowFlags& ShowFlags, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const
{
	if (ShowFlags.Landscape)
	{
		return Super::ComponentIsTouchingSelectionFrustum(InFrustum, ShowFlags, bConsiderOnlyBSP, bMustEncompassEntireComponent);
	}

	return false;
}

void ULandscapeComponent::PreFeatureLevelChange(ERHIFeatureLevel::Type PendingFeatureLevel)
{
	Super::PreFeatureLevelChange(PendingFeatureLevel);

	if (PendingFeatureLevel <= ERHIFeatureLevel::ES3_1)
	{
		// See if we need to cook platform data for ES2 preview in editor
		CheckGenerateLandscapePlatformData(false, nullptr);
	}
}

void ULandscapeComponent::PostEditUndo()
{
	if (!IsPendingKill())
	{
		if (!GetLandscapeProxy()->HasLayersContent())
		{
			UpdateMaterialInstances();
		}
	}

	Super::PostEditUndo();

	if (!IsPendingKill())
	{
		EditToolRenderData.UpdateSelectionMaterial(EditToolRenderData.SelectedType, this);
		if (!GetLandscapeProxy()->HasLayersContent())
		{
			EditToolRenderData.UpdateDebugColorMaterial(this);
            UpdateEditToolRenderData();
		}	
	}
		
	if (GetLandscapeProxy()->HasLayersContent())
	{
		const bool bUpdateAll = true;
		RequestHeightmapUpdate(bUpdateAll);
		RequestWeightmapUpdate(bUpdateAll);

		// Clear Cached Editing Data
		CachedEditingLayer.Invalidate();
		CachedEditingLayerData = nullptr;
	}
	else
	{
		TSet<ULandscapeComponent*> Components;
		Components.Add(this);
		GetLandscapeProxy()->FlushGrassComponents(&Components);
	}
}

void ALandscapeProxy::FixupWeightmaps()
{
	WeightmapUsageMap.Empty();

	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		Component->FixupWeightmaps();
	}
}

void ULandscapeComponent::FixupWeightmaps()
{
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		ULandscapeInfo* Info = GetLandscapeInfo();
		ALandscapeProxy* Proxy = GetLandscapeProxy();

		if (Info)
		{
			WeightmapTexturesUsage.Empty();
			WeightmapTexturesUsage.AddDefaulted(WeightmapTextures.Num());

			TArray<ULandscapeLayerInfoObject*> LayersToDelete;
			bool bFixedLayerDeletion = false;

			// make sure the weightmap textures are fully loaded or deleting layers from them will crash! :)
			for (UTexture* WeightmapTexture : WeightmapTextures)
			{
				WeightmapTexture->ConditionalPostLoad();
			}

			// LayerInfo Validation check...
			for (const auto& Allocation : WeightmapLayerAllocations)
			{
				if (!Allocation.LayerInfo
					|| (Allocation.LayerInfo != ALandscapeProxy::VisibilityLayer && Info->GetLayerInfoIndex(Allocation.LayerInfo) == INDEX_NONE))
				{
					if (!bFixedLayerDeletion)
					{
						FFormatNamedArguments Arguments;
						Arguments.Add(TEXT("LandscapeName"), FText::FromString(GetPathName()));
						FMessageLog("MapCheck").Warning()
							->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_FixedUpDeletedLayerWeightmap", "{LandscapeName} : Fixed up deleted layer weightmap"), Arguments)))
							->AddToken(FMapErrorToken::Create(FMapErrors::FixedUpDeletedLayerWeightmap));
					}

					bFixedLayerDeletion = true;
					LayersToDelete.Add(Allocation.LayerInfo);
				}
			}

			if (bFixedLayerDeletion)
			{
				{
					FLandscapeEditDataInterface LandscapeEdit(Info);
					for (int32 Idx = 0; Idx < LayersToDelete.Num(); ++Idx)
					{
						DeleteLayer(LayersToDelete[Idx], LandscapeEdit);
					}
				}

				ForEachLayer([&](const FGuid& LayerGuid, FLandscapeLayerComponentData& LayerData)
				{
					SetEditingLayer(LayerGuid);
					FLandscapeEditDataInterface LandscapeEdit(Info);
					for (int32 Idx = 0; Idx < LayersToDelete.Num(); ++Idx)
					{
						DeleteLayer(LayersToDelete[Idx], LandscapeEdit);
					}
				});
								
				// Make sure to clear editing layer and cache
				SetEditingLayer(FGuid());
				CachedEditingLayer.Invalidate();
				CachedEditingLayerData = nullptr;
			}

			bool bFixedWeightmapTextureIndex = false;

			// Store the weightmap allocations in WeightmapUsageMap
			for (int32 LayerIdx = 0; LayerIdx < WeightmapLayerAllocations.Num();)
			{
				FWeightmapLayerAllocationInfo& Allocation = WeightmapLayerAllocations[LayerIdx];
				if (!Allocation.IsAllocated())
				{
					WeightmapLayerAllocations.RemoveAt(LayerIdx);
					continue;
				}

				// Fix up any problems caused by the layer deletion bug.
				if (Allocation.WeightmapTextureIndex >= WeightmapTextures.Num())
				{
					Allocation.WeightmapTextureIndex = WeightmapTextures.Num() - 1;
					if (!bFixedWeightmapTextureIndex)
					{
						FFormatNamedArguments Arguments;
						Arguments.Add(TEXT("LandscapeName"), FText::FromString(GetName()));
						FMessageLog("MapCheck").Warning()
							->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_FixedUpIncorrectLayerWeightmap", "{LandscapeName} : Fixed up incorrect layer weightmap texture index"), Arguments)))
							->AddToken(FMapErrorToken::Create(FMapErrors::FixedUpIncorrectLayerWeightmap));
					}
					bFixedWeightmapTextureIndex = true;
				}

				UTexture2D* WeightmapTexture = WeightmapTextures[Allocation.WeightmapTextureIndex];

				ULandscapeWeightmapUsage** TempUsage = Proxy->WeightmapUsageMap.Find(WeightmapTexture);

				if (TempUsage == nullptr)
				{
					TempUsage = &Proxy->WeightmapUsageMap.Add(WeightmapTexture, GetLandscapeProxy()->CreateWeightmapUsage());
					(*TempUsage)->LayerGuid.Invalidate();
				}

				ULandscapeWeightmapUsage* Usage = *TempUsage;
				WeightmapTexturesUsage[Allocation.WeightmapTextureIndex] = Usage; // Keep a ref to it for faster access

				// Detect a shared layer allocation, caused by a previous undo or layer deletion bugs
				if (Usage->ChannelUsage[Allocation.WeightmapTextureChannel] != nullptr &&
					Usage->ChannelUsage[Allocation.WeightmapTextureChannel] != this)
				{
					FFormatNamedArguments Arguments;
					Arguments.Add(TEXT("LayerName"), FText::FromString(Allocation.GetLayerName().ToString()));
					Arguments.Add(TEXT("LandscapeName"), FText::FromString(GetName()));
					Arguments.Add(TEXT("ChannelName"), FText::FromString(Usage->ChannelUsage[Allocation.WeightmapTextureChannel]->GetName()));
					FMessageLog("MapCheck").Warning()
						->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_FixedUpSharedLayerWeightmap", "Fixed up shared weightmap texture for layer {LayerName} in component '{LandscapeName}' (shares with '{ChannelName}')"), Arguments)))
						->AddToken(FMapErrorToken::Create(FMapErrors::FixedUpSharedLayerWeightmap));
					WeightmapLayerAllocations.RemoveAt(LayerIdx);
					continue;
				}
				else
				{
					Usage->ChannelUsage[Allocation.WeightmapTextureChannel] = this;
				}
				++LayerIdx;
			}

			RemoveInvalidWeightmaps();
		}
	}
}

void ULandscapeComponent::UpdateLayerWhitelistFromPaintedLayers()
{
	TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = GetWeightmapLayerAllocations();

	for (const auto& Allocation : ComponentWeightmapLayerAllocations)
	{
		LayerWhitelist.AddUnique(Allocation.LayerInfo);
	}
}

//
// LandscapeComponentAlphaInfo
//
struct FLandscapeComponentAlphaInfo
{
	int32 LayerIndex;
	TArray<uint8> AlphaValues;

	// tor
	FLandscapeComponentAlphaInfo(ULandscapeComponent* InOwner, int32 InLayerIndex)
		: LayerIndex(InLayerIndex)
	{
		AlphaValues.Empty(FMath::Square(InOwner->ComponentSizeQuads + 1));
		AlphaValues.AddZeroed(FMath::Square(InOwner->ComponentSizeQuads + 1));
	}

	bool IsLayerAllZero() const
	{
		for (int32 Index = 0; Index < AlphaValues.Num(); Index++)
		{
			if (AlphaValues[Index] != 0)
			{
				return false;
			}
		}
		return true;
	}
};

struct FCollisionSize
{
public:
	static FCollisionSize CreateSimple(bool bUseSimpleCollision, int32 InNumSubSections, int32 InSubsectionSizeQuads, int32 InMipLevel)
	{
		return bUseSimpleCollision ? Create(InNumSubSections, InSubsectionSizeQuads, InMipLevel) : FCollisionSize();
	}

	static FCollisionSize Create(int32 InNumSubSections, int32 InSubsectionSizeQuads, int32 InMipLevel)
	{
		return FCollisionSize(InNumSubSections, InSubsectionSizeQuads, InMipLevel);
	}

	FCollisionSize(FCollisionSize&& Other) = default;
	FCollisionSize& operator=(FCollisionSize&& Other) = default;
private:
	FCollisionSize(int32 InNumSubsections, int32 InSubsectionSizeQuads, int32 InMipLevel)
	{
		SubsectionSizeVerts = ((InSubsectionSizeQuads + 1) >> InMipLevel);
		SubsectionSizeQuads = SubsectionSizeVerts - 1;
		SizeVerts = InNumSubsections * SubsectionSizeQuads + 1;
		SizeVertsSquare = FMath::Square(SizeVerts);
	}

	FCollisionSize()
	{
	}

public:
	int32 SubsectionSizeVerts = 0;
	int32 SubsectionSizeQuads = 0;
	int32 SizeVerts = 0;
	int32 SizeVertsSquare = 0;
};

void ULandscapeComponent::UpdateDirtyCollisionHeightData(FIntRect Region)
{
	// Take first value as is
	if (LayerDirtyCollisionHeightData.IsEmpty())
	{
		LayerDirtyCollisionHeightData = Region;
	}
	else
	{
		// Merge min/max region
		LayerDirtyCollisionHeightData.Include(Region.Min);
		LayerDirtyCollisionHeightData.Include(Region.Max);
	}
}

void ULandscapeComponent::ClearDirtyCollisionHeightData()
{
	LayerDirtyCollisionHeightData = FIntRect();
}

void ULandscapeComponent::UpdateCollisionHeightData(const FColor* const HeightmapTextureMipData, const FColor* const SimpleCollisionHeightmapTextureData, int32 ComponentX1/*=0*/, int32 ComponentY1/*=0*/, int32 ComponentX2/*=MAX_int32*/, int32 ComponentY2/*=MAX_int32*/, bool bUpdateBounds/*=false*/, const FColor* XYOffsetTextureMipData/*=nullptr*/, bool bInUpdateHeightfieldRegion/*=true*/)
{
	ULandscapeInfo* Info = GetLandscapeInfo();
	ALandscapeProxy* Proxy = GetLandscapeProxy();
	FIntPoint ComponentKey = GetSectionBase() / ComponentSizeQuads;
	ULandscapeHeightfieldCollisionComponent* CollisionComp = CollisionComponent.Get();
	ULandscapeMeshCollisionComponent* MeshCollisionComponent = Cast<ULandscapeMeshCollisionComponent>(CollisionComp);
	ULandscapeHeightfieldCollisionComponent* OldCollisionComponent = CollisionComp;

	// Simple collision is not currently supported with mesh collision components
	const bool bUsingSimpleCollision = (SimpleCollisionMipLevel > CollisionMipLevel && SimpleCollisionHeightmapTextureData && !XYOffsetmapTexture);
	
	FCollisionSize CollisionSize = FCollisionSize::Create(NumSubsections, SubsectionSizeQuads, CollisionMipLevel);
	FCollisionSize SimpleCollisionSize = FCollisionSize::CreateSimple(bUsingSimpleCollision, NumSubsections, SubsectionSizeQuads, SimpleCollisionMipLevel);
		
	const int32 TotalCollisionSize = CollisionSize.SizeVertsSquare + SimpleCollisionSize.SizeVertsSquare;

	uint16* CollisionHeightData = nullptr;
	uint16* CollisionXYOffsetData = nullptr;
	bool CreatedNew = false;
	bool ChangeType = false;

    // In Landscape Layers the Collision Component is slave and doesn't need to be transacted
	if (!Proxy->HasLayersContent())
	{
		if (CollisionComp)
		{
			CollisionComp->Modify();
		}
	}
	else
	{
		// In Landscape Layers, only update dirtied collision height data
		if (bInUpdateHeightfieldRegion && ComponentX1 == 0 && ComponentY1 == 0 && ComponentX2 == MAX_int32 && ComponentY2 == MAX_int32 && !LayerDirtyCollisionHeightData.IsEmpty())
		{
			ComponentX1 = LayerDirtyCollisionHeightData.Min.X;
			ComponentY1 = LayerDirtyCollisionHeightData.Min.Y;
			ComponentX2 = LayerDirtyCollisionHeightData.Max.X;
			ComponentY2 = LayerDirtyCollisionHeightData.Max.Y;
		}
		ClearDirtyCollisionHeightData();
	}

	// Existing collision component is same type with collision
	if (CollisionComp && ((XYOffsetmapTexture == nullptr) == (MeshCollisionComponent == nullptr)))
	{
		ComponentX1 = FMath::Clamp(ComponentX1, 0, ComponentSizeQuads);
		ComponentY1 = FMath::Clamp(ComponentY1, 0, ComponentSizeQuads);
		ComponentX2 = FMath::Clamp(ComponentX2, 0, ComponentSizeQuads);
		ComponentY2 = FMath::Clamp(ComponentY2, 0, ComponentSizeQuads);

		if (ComponentX2 < ComponentX1 || ComponentY2 < ComponentY1)
		{
			// nothing to do
			return;
		}

		if (bUpdateBounds)
		{
			CollisionComp->CachedLocalBox = CachedLocalBox;
			CollisionComp->UpdateComponentToWorld();
		}
	}
	else
	{
		CreatedNew = true;
		ChangeType = CollisionComp != nullptr;
		ComponentX1 = 0;
		ComponentY1 = 0;
		ComponentX2 = ComponentSizeQuads;
		ComponentY2 = ComponentSizeQuads;

		RecreateCollisionComponent(bUsingSimpleCollision);
		CollisionComp = CollisionComponent.Get();
        MeshCollisionComponent = Cast<ULandscapeMeshCollisionComponent>(CollisionComp);
	}

	CollisionHeightData = (uint16*)CollisionComp->CollisionHeightData.Lock(LOCK_READ_WRITE);
	
	if (XYOffsetmapTexture && MeshCollisionComponent)
	{
		CollisionXYOffsetData = (uint16*)MeshCollisionComponent->CollisionXYOffsetData.Lock(LOCK_READ_WRITE);
	}

	const int32 HeightmapSizeU = GetHeightmap()->Source.GetSizeX();
	const int32 HeightmapSizeV = GetHeightmap()->Source.GetSizeY();

	// Handle Material WPO baked into heightfield collision
	// Material WPO is not currently supported for mesh collision components
	const bool bUsingGrassMapHeights = Proxy->bBakeMaterialPositionOffsetIntoCollision && !MeshCollisionComponent && GrassData->HasData() && !IsGrassMapOutdated();
	uint16* GrassHeights = nullptr;
	if (bUsingGrassMapHeights)
	{
		if (CollisionMipLevel == 0)
		{
			GrassHeights = GrassData->HeightData.GetData();
		}
		else
		{
			if (GrassData->HeightMipData.Contains(CollisionMipLevel))
			{
				GrassHeights = GrassData->HeightMipData[CollisionMipLevel].GetData();
			}
		}
	}

	UpdateCollisionHeightBuffer(ComponentX1, ComponentY1, ComponentX2, ComponentY2, CollisionMipLevel, HeightmapSizeU, HeightmapSizeV, HeightmapTextureMipData, CollisionHeightData, GrassHeights, XYOffsetTextureMipData, CollisionXYOffsetData);
		
	if (bUsingSimpleCollision)
	{
		uint16* SimpleCollisionGrassHeights = bUsingGrassMapHeights && GrassData->HeightMipData.Contains(SimpleCollisionMipLevel) ? GrassData->HeightMipData[SimpleCollisionMipLevel].GetData() : nullptr;
		uint16* const SimpleCollisionHeightData = CollisionHeightData + CollisionSize.SizeVertsSquare;
		UpdateCollisionHeightBuffer(ComponentX1, ComponentY1, ComponentX2, ComponentY2, SimpleCollisionMipLevel, HeightmapSizeU, HeightmapSizeV, SimpleCollisionHeightmapTextureData, SimpleCollisionHeightData, SimpleCollisionGrassHeights, nullptr, nullptr);
	}

	CollisionComp->CollisionHeightData.Unlock();

	if (XYOffsetmapTexture && MeshCollisionComponent)
	{
		MeshCollisionComponent->CollisionXYOffsetData.Unlock();
	}

	// If we updated an existing component, we need to update the phys x heightfield edit data
	if (!CreatedNew && bInUpdateHeightfieldRegion)
	{
		if (MeshCollisionComponent)
		{
			// Will be done once for XY Offset data update in FXYOffsetmapAccessor() destructor with UpdateCachedBounds()
			//MeshCollisionComponent->RecreateCollision();
		}
		else if (CollisionMipLevel == 0)
		{
			CollisionComp->UpdateHeightfieldRegion(ComponentX1, ComponentY1, ComponentX2, ComponentY2);
		}
		else
		{
			// Ratio to convert update region coordinate to collision mip coordinates
			const float CollisionQuadRatio = (float)CollisionSize.SubsectionSizeQuads / (float)SubsectionSizeQuads;
			const int32 CollisionCompX1 = FMath::FloorToInt((float)ComponentX1 * CollisionQuadRatio);
			const int32 CollisionCompY1 = FMath::FloorToInt((float)ComponentY1 * CollisionQuadRatio);
			const int32 CollisionCompX2 = FMath::CeilToInt( (float)ComponentX2 * CollisionQuadRatio);
			const int32 CollisionCompY2 = FMath::CeilToInt( (float)ComponentY2 * CollisionQuadRatio);
			CollisionComp->UpdateHeightfieldRegion(CollisionCompX1, CollisionCompY1, CollisionCompX2, CollisionCompY2);
		}
	}

	{
		// set relevancy for navigation system
		ALandscapeProxy* LandscapeProxy = CollisionComp->GetLandscapeProxy();
		CollisionComp->SetCanEverAffectNavigation(LandscapeProxy ? LandscapeProxy->bUsedForNavigation : false);
	}

	// Move any foliage instances if we created a new collision component.
	if (OldCollisionComponent && OldCollisionComponent != CollisionComp)
	{
		AInstancedFoliageActor::MoveInstancesToNewComponent(Proxy->GetWorld(), OldCollisionComponent, CollisionComp);
	}
	
	if (CreatedNew && !ChangeType)
	{
		UpdateCollisionLayerData();
	}

	if (CreatedNew && Proxy->GetRootComponent()->IsRegistered())
	{
		CollisionComp->RegisterComponent();
	}

	// Invalidate rendered physical materials
	// These are updated in UpdatePhysicalMaterialTasks()
 	PhysicalMaterialHash = 0;
}

void ULandscapeComponent::DestroyCollisionData()
{
	ULandscapeHeightfieldCollisionComponent* CollisionComp = CollisionComponent.Get();
	if (CollisionComp)
	{
		CollisionComp->DestroyComponent();
		CollisionComponent = CollisionComp = nullptr;
	}
}

void ULandscapeComponent::UpdateCollisionData(bool bInUpdateHeightfieldRegion)
{
	TArray64<uint8> CollisionMipData;
	TArray64<uint8> SimpleCollisionMipData;
	TArray64<uint8> XYOffsetMipData;

	GetHeightmap()->Source.GetMipData(CollisionMipData, CollisionMipLevel);
	if (SimpleCollisionMipLevel > CollisionMipLevel)
	{
		GetHeightmap()->Source.GetMipData(SimpleCollisionMipData, SimpleCollisionMipLevel);
	}
	if (XYOffsetmapTexture)
	{
		XYOffsetmapTexture->Source.GetMipData(XYOffsetMipData, CollisionMipLevel);
	}

	UpdateCollisionHeightData(
		(FColor*)CollisionMipData.GetData(),
		SimpleCollisionMipLevel > CollisionMipLevel ? (FColor*)SimpleCollisionMipData.GetData() : nullptr,
		0, 0, MAX_int32, MAX_int32, true,
		XYOffsetmapTexture ? (FColor*)XYOffsetMipData.GetData() : nullptr, bInUpdateHeightfieldRegion);
}

void ULandscapeComponent::RecreateCollisionComponent(bool bUseSimpleCollision)
{
	ULandscapeHeightfieldCollisionComponent* CollisionComp = CollisionComponent.Get();
	ULandscapeMeshCollisionComponent* MeshCollisionComponent = nullptr;
	TArray<uint8> DominantLayerData;
	TArray<ULandscapeLayerInfoObject*> LayerInfos;
	ALandscapeProxy* Proxy = GetLandscapeProxy();
	ULandscapeInfo* Info = GetLandscapeInfo();
	const FCollisionSize CollisionSize = FCollisionSize::Create(NumSubsections, SubsectionSizeQuads, CollisionMipLevel);
	const FCollisionSize SimpleCollisionSize = FCollisionSize::CreateSimple(bUseSimpleCollision, NumSubsections, SubsectionSizeQuads, SimpleCollisionMipLevel);
	const int32 TotalCollisionSize = CollisionSize.SizeVertsSquare + SimpleCollisionSize.SizeVertsSquare;

	if (CollisionComp) // remove old component before changing to other type collision...
	{
		if (CollisionComp->DominantLayerData.GetElementCount())
		{
			check(CollisionComp->DominantLayerData.GetElementCount() >= TotalCollisionSize);
			DominantLayerData.AddUninitialized(TotalCollisionSize);

			const uint8* SrcDominantLayerData = (uint8*)CollisionComp->DominantLayerData.Lock(LOCK_READ_ONLY);
			FMemory::Memcpy(DominantLayerData.GetData(), SrcDominantLayerData, TotalCollisionSize * CollisionComp->DominantLayerData.GetElementSize());
			CollisionComp->DominantLayerData.Unlock();
		}

		if (CollisionComp->ComponentLayerInfos.Num())
		{
			LayerInfos = CollisionComp->ComponentLayerInfos;
		}

		if (Info)
		{
			Info->Modify();
		}
		Proxy->Modify();
		CollisionComp->DestroyComponent();
		CollisionComp = nullptr;
	}

	if (XYOffsetmapTexture)
	{
		MeshCollisionComponent = NewObject<ULandscapeMeshCollisionComponent>(Proxy, NAME_None, RF_Transactional);
		CollisionComp = MeshCollisionComponent;
	}
	else
	{
		MeshCollisionComponent = nullptr;
		CollisionComp = NewObject<ULandscapeHeightfieldCollisionComponent>(Proxy, NAME_None, RF_Transactional);
	}

	CollisionComp->SetRelativeLocation(GetRelativeLocation());
	CollisionComp->SetupAttachment(Proxy->GetRootComponent(), NAME_None);
	Proxy->CollisionComponents.Add(CollisionComp);

	CollisionComp->RenderComponent = this;
	CollisionComp->SetSectionBase(GetSectionBase());
	CollisionComp->CollisionSizeQuads = CollisionSize.SubsectionSizeQuads * NumSubsections;
	CollisionComp->CollisionScale = (float)(ComponentSizeQuads) / (float)(CollisionComp->CollisionSizeQuads);
	CollisionComp->SimpleCollisionSizeQuads = SimpleCollisionSize.SubsectionSizeQuads * NumSubsections;
	CollisionComp->CachedLocalBox = CachedLocalBox;
	CollisionComp->SetGenerateOverlapEvents(Proxy->bGenerateOverlapEvents);

	// Reallocate raw collision data
	CollisionComp->CollisionHeightData.Lock(LOCK_READ_WRITE);
	uint16* CollisionHeightData = (uint16*)CollisionComp->CollisionHeightData.Realloc(TotalCollisionSize);
	FMemory::Memzero(CollisionHeightData, TotalCollisionSize * CollisionComp->CollisionHeightData.GetElementSize());
	CollisionComp->CollisionHeightData.Unlock();
	
	if (XYOffsetmapTexture && MeshCollisionComponent)
	{
		// Need XYOffsetData for Collision Component
		MeshCollisionComponent->CollisionXYOffsetData.Lock(LOCK_READ_WRITE);
		uint16* CollisionXYOffsetData = (uint16*)MeshCollisionComponent->CollisionXYOffsetData.Realloc(TotalCollisionSize * 2);
		FMemory::Memzero(CollisionXYOffsetData, TotalCollisionSize * 2 * MeshCollisionComponent->CollisionXYOffsetData.GetElementSize());
		MeshCollisionComponent->CollisionXYOffsetData.Unlock();
	}

	if (DominantLayerData.Num())
	{
		CollisionComp->DominantLayerData.Lock(LOCK_READ_WRITE);
		uint8* DestDominantLayerData = (uint8*)CollisionComp->DominantLayerData.Realloc(TotalCollisionSize);
		FMemory::Memcpy(DestDominantLayerData, DominantLayerData.GetData(), TotalCollisionSize * CollisionComp->DominantLayerData.GetElementSize());
		CollisionComp->DominantLayerData.Unlock();
	}

	if (LayerInfos.Num())
	{
		CollisionComp->ComponentLayerInfos = MoveTemp(LayerInfos);
	}
	CollisionComponent = CollisionComp;
}

void ULandscapeComponent::UpdateCollisionHeightBuffer(	int32 InComponentX1, int32 InComponentY1, int32 InComponentX2, int32 InComponentY2, int32 InCollisionMipLevel, int32 InHeightmapSizeU, int32 InHeightmapSizeV,
														const FColor* const InHeightmapTextureMipData, uint16* OutCollisionHeightData, uint16* InGrassHeightData,
														const FColor* const InXYOffsetTextureMipData, uint16* OutCollisionXYOffsetData)
{
	FCollisionSize CollisionSize = FCollisionSize::Create(NumSubsections, SubsectionSizeQuads, InCollisionMipLevel);

	// Ratio to convert update region coordinate to collision mip coordinates
	const float CollisionQuadRatio = (float)CollisionSize.SubsectionSizeQuads / (float)SubsectionSizeQuads;

	const int32 SubSectionX1 = FMath::Max(0, FMath::DivideAndRoundDown(InComponentX1 - 1, SubsectionSizeQuads));
	const int32 SubSectionY1 = FMath::Max(0, FMath::DivideAndRoundDown(InComponentY1 - 1, SubsectionSizeQuads));
	const int32 SubSectionX2 = FMath::Min(FMath::DivideAndRoundUp(InComponentX2 + 1, SubsectionSizeQuads), NumSubsections);
	const int32 SubSectionY2 = FMath::Min(FMath::DivideAndRoundUp(InComponentY2 + 1, SubsectionSizeQuads), NumSubsections);

	const int32 MipSizeU = InHeightmapSizeU >> InCollisionMipLevel;
	const int32 MipSizeV = InHeightmapSizeV >> InCollisionMipLevel;

	const int32 HeightmapOffsetX = FMath::RoundToInt(HeightmapScaleBias.Z * (float)InHeightmapSizeU) >> InCollisionMipLevel;
	const int32 HeightmapOffsetY = FMath::RoundToInt(HeightmapScaleBias.W * (float)InHeightmapSizeV) >> InCollisionMipLevel;

	const int32 XYMipSizeU = XYOffsetmapTexture ? XYOffsetmapTexture->Source.GetSizeX() >> InCollisionMipLevel : 0;

	for (int32 SubsectionY = SubSectionY1; SubsectionY < SubSectionY2; ++SubsectionY)
	{
		for (int32 SubsectionX = SubSectionX1; SubsectionX < SubSectionX2; ++SubsectionX)
		{
			// Area to update in subsection coordinates
			const int32 SubX1 = InComponentX1 - SubsectionSizeQuads * SubsectionX;
			const int32 SubY1 = InComponentY1 - SubsectionSizeQuads * SubsectionY;
			const int32 SubX2 = InComponentX2 - SubsectionSizeQuads * SubsectionX;
			const int32 SubY2 = InComponentY2 - SubsectionSizeQuads * SubsectionY;

			// Area to update in collision mip level coords
			const int32 CollisionSubX1 = FMath::FloorToInt((float)SubX1 * CollisionQuadRatio);
			const int32 CollisionSubY1 = FMath::FloorToInt((float)SubY1 * CollisionQuadRatio);
			const int32 CollisionSubX2 = FMath::CeilToInt((float)SubX2 * CollisionQuadRatio);
			const int32 CollisionSubY2 = FMath::CeilToInt((float)SubY2 * CollisionQuadRatio);

			// Clamp area to update
			const int32 VertX1 = FMath::Clamp<int32>(CollisionSubX1, 0, CollisionSize.SubsectionSizeQuads);
			const int32 VertY1 = FMath::Clamp<int32>(CollisionSubY1, 0, CollisionSize.SubsectionSizeQuads);
			const int32 VertX2 = FMath::Clamp<int32>(CollisionSubX2, 0, CollisionSize.SubsectionSizeQuads);
			const int32 VertY2 = FMath::Clamp<int32>(CollisionSubY2, 0, CollisionSize.SubsectionSizeQuads);

			for (int32 VertY = VertY1; VertY <= VertY2; VertY++)
			{
				for (int32 VertX = VertX1; VertX <= VertX2; VertX++)
				{
					// this uses Quads as we don't want the duplicated vertices
					const int32 CompVertX = CollisionSize.SubsectionSizeQuads * SubsectionX + VertX;
					const int32 CompVertY = CollisionSize.SubsectionSizeQuads * SubsectionY + VertY;

					if (InGrassHeightData)
					{
						uint16& CollisionHeight = OutCollisionHeightData[CompVertX + CompVertY * CollisionSize.SizeVerts];
						const uint16& NewHeight = InGrassHeightData[CompVertX + CompVertY * CollisionSize.SizeVerts];
						CollisionHeight = NewHeight;
					}
					else
					{
						// X/Y of the vertex we're looking indexed into the texture data
						const int32 TexX = HeightmapOffsetX + CollisionSize.SubsectionSizeVerts * SubsectionX + VertX;
						const int32 TexY = HeightmapOffsetY + CollisionSize.SubsectionSizeVerts * SubsectionY + VertY;
						const FColor& TexData = InHeightmapTextureMipData[TexX + TexY * MipSizeU];

						// Copy collision data
						uint16& CollisionHeight = OutCollisionHeightData[CompVertX + CompVertY * CollisionSize.SizeVerts];
						const uint16 NewHeight = TexData.R << 8 | TexData.G;

						CollisionHeight = NewHeight;
					}

					if (XYOffsetmapTexture && InXYOffsetTextureMipData && OutCollisionXYOffsetData)
					{
						const int32 TexX = CollisionSize.SubsectionSizeVerts * SubsectionX + VertX;
						const int32 TexY = CollisionSize.SubsectionSizeVerts * SubsectionY + VertY;
						const FColor& TexData = InXYOffsetTextureMipData[TexX + TexY * XYMipSizeU];

						// Copy collision data
						const uint16 NewXOffset = TexData.R << 8 | TexData.G;
						const uint16 NewYOffset = TexData.B << 8 | TexData.A;

						const int32 XYIndex = CompVertX + CompVertY * CollisionSize.SizeVerts;
						OutCollisionXYOffsetData[XYIndex * 2] = NewXOffset;
						OutCollisionXYOffsetData[XYIndex * 2 + 1] = NewYOffset;
					}
				}
			}
		}
	}
}

void ULandscapeComponent::UpdateDominantLayerBuffer(int32 InComponentX1, int32 InComponentY1, int32 InComponentX2, int32 InComponentY2, int32 InCollisionMipLevel, int32 InWeightmapSizeU, int32 InDataLayerIdx, const TArray<uint8*>& InCollisionDataPtrs, const TArray<ULandscapeLayerInfoObject*>& InLayerInfos, uint8* OutDominantLayerData)
{
	const int32 MipSizeU = InWeightmapSizeU >> InCollisionMipLevel;

	FCollisionSize CollisionSize = FCollisionSize::Create(NumSubsections, SubsectionSizeQuads, InCollisionMipLevel);
	
	// Ratio to convert update region coordinate to collision mip coordinates
	const float CollisionQuadRatio = (float)CollisionSize.SubsectionSizeQuads / (float)SubsectionSizeQuads;

	const int32 SubSectionX1 = FMath::Max(0, FMath::DivideAndRoundDown(InComponentX1 - 1, SubsectionSizeQuads));
	const int32 SubSectionY1 = FMath::Max(0, FMath::DivideAndRoundDown(InComponentY1 - 1, SubsectionSizeQuads));
	const int32 SubSectionX2 = FMath::Min(FMath::DivideAndRoundUp(InComponentX2 + 1, SubsectionSizeQuads), NumSubsections);
	const int32 SubSectionY2 = FMath::Min(FMath::DivideAndRoundUp(InComponentY2 + 1, SubsectionSizeQuads), NumSubsections);
	for (int32 SubsectionY = SubSectionY1; SubsectionY < SubSectionY2; ++SubsectionY)
	{
		for (int32 SubsectionX = SubSectionX1; SubsectionX < SubSectionX2; ++SubsectionX)
		{
			// Area to update in subsection coordinates
			const int32 SubX1 = InComponentX1 - SubsectionSizeQuads * SubsectionX;
			const int32 SubY1 = InComponentY1 - SubsectionSizeQuads * SubsectionY;
			const int32 SubX2 = InComponentX2 - SubsectionSizeQuads * SubsectionX;
			const int32 SubY2 = InComponentY2 - SubsectionSizeQuads * SubsectionY;

			// Area to update in collision mip level coords
			const int32 CollisionSubX1 = FMath::FloorToInt((float)SubX1 * CollisionQuadRatio);
			const int32 CollisionSubY1 = FMath::FloorToInt((float)SubY1 * CollisionQuadRatio);
			const int32 CollisionSubX2 = FMath::CeilToInt((float)SubX2 * CollisionQuadRatio);
			const int32 CollisionSubY2 = FMath::CeilToInt((float)SubY2 * CollisionQuadRatio);

			// Clamp area to update
			const int32 VertX1 = FMath::Clamp<int32>(CollisionSubX1, 0, CollisionSize.SubsectionSizeQuads);
			const int32 VertY1 = FMath::Clamp<int32>(CollisionSubY1, 0, CollisionSize.SubsectionSizeQuads);
			const int32 VertX2 = FMath::Clamp<int32>(CollisionSubX2, 0, CollisionSize.SubsectionSizeQuads);
			const int32 VertY2 = FMath::Clamp<int32>(CollisionSubY2, 0, CollisionSize.SubsectionSizeQuads);

			for (int32 VertY = VertY1; VertY <= VertY2; VertY++)
			{
				for (int32 VertX = VertX1; VertX <= VertX2; VertX++)
				{
					// X/Y of the vertex we're looking indexed into the texture data
					const int32 TexX = CollisionSize.SubsectionSizeVerts * SubsectionX + VertX;
					const int32 TexY = CollisionSize.SubsectionSizeVerts * SubsectionY + VertY;
					const int32 DataOffset = (TexX + TexY * MipSizeU) * sizeof(FColor);

					uint8 DominantLayer = 255; // 255 as invalid value
					int32 DominantWeight = 0;
					for (int32 LayerIdx = 0; LayerIdx < InCollisionDataPtrs.Num(); LayerIdx++)
					{
						const uint8 LayerWeight = InCollisionDataPtrs[LayerIdx][DataOffset];
						const uint8 LayerMinimumWeight = InLayerInfos[LayerIdx] ? (uint8)(InLayerInfos[LayerIdx]->MinimumCollisionRelevanceWeight * 255) :  0;

						if (LayerIdx == InDataLayerIdx) // Override value for hole
						{
							if (LayerWeight > 170) // 255 * 0.66...
							{
								DominantLayer = LayerIdx;
								DominantWeight = INT_MAX;
							}
						}
						else if (LayerWeight > DominantWeight && LayerWeight >= LayerMinimumWeight)
						{
							DominantLayer = LayerIdx;
							DominantWeight = LayerWeight;
						}
					}

					// this uses Quads as we don't want the duplicated vertices
					const int32 CompVertX = CollisionSize.SubsectionSizeQuads * SubsectionX + VertX;
					const int32 CompVertY = CollisionSize.SubsectionSizeQuads * SubsectionY + VertY;

					// Set collision data
					OutDominantLayerData[CompVertX + CompVertY * CollisionSize.SizeVerts] = DominantLayer;
				}
			}
		}
	}
}

void ULandscapeComponent::UpdateCollisionLayerData(const FColor* const* const WeightmapTextureMipData, const FColor* const* const SimpleCollisionWeightmapTextureMipData, int32 ComponentX1, int32 ComponentY1, int32 ComponentX2, int32 ComponentY2)
{
	ULandscapeInfo* Info = GetLandscapeInfo();
	ALandscapeProxy* Proxy = GetLandscapeProxy();
	FIntPoint ComponentKey = GetSectionBase() / ComponentSizeQuads;

	ULandscapeHeightfieldCollisionComponent* CollisionComp = CollisionComponent.Get();

	if (CollisionComp)
	{
		if (!Proxy->HasLayersContent())
		{
			CollisionComp->Modify();
		}

		// Simple collision is not currently supported with mesh collision components
		const bool bUsingSimpleCollision = (SimpleCollisionMipLevel > CollisionMipLevel && SimpleCollisionWeightmapTextureMipData && !XYOffsetmapTexture);

		TArray<ULandscapeLayerInfoObject*> CandidateLayers;
		TArray<uint8*> CandidateDataPtrs;
		TArray<uint8*> SimpleCollisionDataPtrs;

		bool bExistingLayerMismatch = false;
		int32 DataLayerIdx = INDEX_NONE;

		TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = GetWeightmapLayerAllocations(false);
		TArray<UTexture2D*>& ComponentWeightmapsTexture = GetWeightmapTextures(false);

		// Find the layers we're interested in
		for (int32 AllocIdx = 0; AllocIdx < ComponentWeightmapLayerAllocations.Num(); AllocIdx++)
		{
			FWeightmapLayerAllocationInfo& AllocInfo = ComponentWeightmapLayerAllocations[AllocIdx];
			ULandscapeLayerInfoObject* LayerInfo = AllocInfo.LayerInfo;
			if (LayerInfo == ALandscapeProxy::VisibilityLayer || LayerInfo != nullptr)
			{
				int32 Idx = CandidateLayers.Add(LayerInfo);
				CandidateDataPtrs.Add(((uint8*)WeightmapTextureMipData[AllocInfo.WeightmapTextureIndex]) + ChannelOffsets[AllocInfo.WeightmapTextureChannel]);

				if (bUsingSimpleCollision)
				{
					SimpleCollisionDataPtrs.Add(((uint8*)SimpleCollisionWeightmapTextureMipData[AllocInfo.WeightmapTextureIndex]) + ChannelOffsets[AllocInfo.WeightmapTextureChannel]);
				}

				// Check if we still match the collision component.
				if (!CollisionComp->ComponentLayerInfos.IsValidIndex(Idx) || CollisionComp->ComponentLayerInfos[Idx] != LayerInfo)
				{
					bExistingLayerMismatch = true;
				}

				if (LayerInfo == ALandscapeProxy::VisibilityLayer)
				{
					DataLayerIdx = Idx;
					bExistingLayerMismatch = true; // always rebuild whole component for hole
				}
			}
		}

		if (CandidateLayers.Num() == 0)
		{
			// No layers, so don't update any weights
			CollisionComp->DominantLayerData.RemoveBulkData();
			CollisionComp->ComponentLayerInfos.Empty();
		}
		else
		{
			uint8* DominantLayerData = (uint8*)CollisionComp->DominantLayerData.Lock(LOCK_READ_WRITE);
			FCollisionSize CollisionSize = FCollisionSize::Create(NumSubsections, SubsectionSizeQuads, CollisionMipLevel);
			FCollisionSize SimpleCollisionSize = FCollisionSize::CreateSimple(bUsingSimpleCollision, NumSubsections, SubsectionSizeQuads, SimpleCollisionMipLevel);
		
				
			// If there's no existing data, or the layer allocations have changed, we need to update the data for the whole component.
			if (bExistingLayerMismatch || CollisionComp->DominantLayerData.GetElementCount() == 0)
			{
				ComponentX1 = 0;
				ComponentY1 = 0;
				ComponentX2 = ComponentSizeQuads;
				ComponentY2 = ComponentSizeQuads;
											
				const int32 TotalCollisionSize = CollisionSize.SizeVertsSquare + SimpleCollisionSize.SizeVertsSquare;

				
				DominantLayerData = (uint8*)CollisionComp->DominantLayerData.Realloc(TotalCollisionSize);
				FMemory::Memzero(DominantLayerData, TotalCollisionSize);
				CollisionComp->ComponentLayerInfos = CandidateLayers;
			}
			else
			{
				ComponentX1 = FMath::Clamp(ComponentX1, 0, ComponentSizeQuads);
				ComponentY1 = FMath::Clamp(ComponentY1, 0, ComponentSizeQuads);
				ComponentX2 = FMath::Clamp(ComponentX2, 0, ComponentSizeQuads);
				ComponentY2 = FMath::Clamp(ComponentY2, 0, ComponentSizeQuads);
			}

			const int32 WeightmapSizeU = ComponentWeightmapsTexture[0]->Source.GetSizeX();
						
			// gmartin: WeightmapScaleBias not handled?			
			UpdateDominantLayerBuffer(ComponentX1, ComponentY1, ComponentX2, ComponentY2, CollisionMipLevel, WeightmapSizeU, DataLayerIdx, CandidateDataPtrs, CollisionComp->ComponentLayerInfos, DominantLayerData);

			if (bUsingSimpleCollision)
			{
				uint8* const SimpleCollisionHeightData = DominantLayerData + CollisionSize.SizeVertsSquare;
				UpdateDominantLayerBuffer(ComponentX1, ComponentY1, ComponentX2, ComponentY2, SimpleCollisionMipLevel, WeightmapSizeU, DataLayerIdx, SimpleCollisionDataPtrs, CollisionComp->ComponentLayerInfos, SimpleCollisionHeightData);
			}

			CollisionComp->DominantLayerData.Unlock();
		}

		// Invalidate rendered physical materials
		// These are updated in UpdatePhysicalMaterialTasks()
 		PhysicalMaterialHash = 0;

		// We do not force an update of the physics data here. We don't need the layer information in the editor and it
		// causes problems if we update it multiple times in a single frame.
	}
}


void ULandscapeComponent::UpdateCollisionLayerData()
{
	TArray<UTexture2D*>& ComponentWeightmapsTexture = GetWeightmapTextures();

	// Generate the dominant layer data
	TArray<TArray64<uint8>> WeightmapTextureMipData;
	TArray<FColor*> WeightmapTextureMipDataParam;
	WeightmapTextureMipData.Reserve(ComponentWeightmapsTexture.Num());
	WeightmapTextureMipDataParam.Reserve(ComponentWeightmapsTexture.Num());
	for (int32 WeightmapIdx = 0; WeightmapIdx < ComponentWeightmapsTexture.Num(); ++WeightmapIdx)
	{
		TArray64<uint8>& MipData = WeightmapTextureMipData.AddDefaulted_GetRef();
		ComponentWeightmapsTexture[WeightmapIdx]->Source.GetMipData(MipData, CollisionMipLevel);
		WeightmapTextureMipDataParam.Add((FColor*)MipData.GetData());
	}

	TArray<TArray64<uint8>> SimpleCollisionWeightmapMipData;
	TArray<FColor*> SimpleCollisionWeightmapMipDataParam;
	if (SimpleCollisionMipLevel > CollisionMipLevel)
	{
		SimpleCollisionWeightmapMipData.Reserve(ComponentWeightmapsTexture.Num());
		SimpleCollisionWeightmapMipDataParam.Reserve(ComponentWeightmapsTexture.Num());
		for (int32 WeightmapIdx = 0; WeightmapIdx < ComponentWeightmapsTexture.Num(); ++WeightmapIdx)
		{
			TArray64<uint8>& MipData = SimpleCollisionWeightmapMipData.AddDefaulted_GetRef();
			ComponentWeightmapsTexture[WeightmapIdx]->Source.GetMipData(MipData, SimpleCollisionMipLevel);
			SimpleCollisionWeightmapMipDataParam.Add((FColor*)MipData.GetData());
		}
	}

	UpdateCollisionLayerData(WeightmapTextureMipDataParam.GetData(), SimpleCollisionWeightmapMipDataParam.GetData());
}

uint32 ULandscapeComponent::CalculatePhysicalMaterialTaskHash() const
{
	uint32 Hash = 0;
	
	// Take into account any material changes.
	UMaterialInterface* Material = GetLandscapeMaterial();
	for (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Material); MIC; MIC = Cast<UMaterialInstanceConstant>(Material))
	{
		Hash = FCrc::TypeCrc32(MIC->ParameterStateId, Hash);
		Material = MIC->Parent;
	}
	UMaterial* MaterialBase = Cast<UMaterial>(Material);
	if (MaterialBase != nullptr)
	{
		Hash = FCrc::TypeCrc32(MaterialBase->StateId, Hash);
	}

	// We could take into account heightmap and weightmap changes here by adding to the hash.
	// Instead we are resetting the stored hash in UpdateCollisionHeightData() and UpdateCollisionLayerData().

	return Hash;
}

void ULandscapeComponent::UpdatePhysicalMaterialTasks()
{
	uint32 Hash = CalculatePhysicalMaterialTaskHash();
	if (PhysicalMaterialHash != Hash)
	{
		PhysicalMaterialTask.Init(this);
		PhysicalMaterialHash = Hash;
	}

	if (PhysicalMaterialTask.IsValid())
	{
		if (PhysicalMaterialTask.IsComplete())
		{
			UpdateCollisionPhysicalMaterialData(PhysicalMaterialTask.GetResultMaterials(), PhysicalMaterialTask.GetResultIds());

			PhysicalMaterialTask.Release();

			// We do not force an update of the physics data here. 
			// We don't need the information immediately in the editor and update will happen on cook or PIE.
		}
		else
		{
			PhysicalMaterialTask.Tick();
		}
	}
}

void ULandscapeComponent::UpdateCollisionPhysicalMaterialData(TArray<UPhysicalMaterial*> const& InPhysicalMaterials, TArray<uint8> const& InMaterialIds)
{
	// Copy the physical material array
	CollisionComponent->PhysicalMaterialRenderObjects = InPhysicalMaterials;

	// Copy the physical material IDs for both the full and (optional) simple collision.
	const int32 SizeVerts = SubsectionSizeQuads * NumSubsections + 1;
	check(InMaterialIds.Num() == SizeVerts * SizeVerts);
	const int32 FullCollisionSizeVerts = CollisionComponent->CollisionSizeQuads + 1;
	const int32 SimpleCollisionSizeVerts = CollisionComponent->SimpleCollisionSizeQuads > 0 ? CollisionComponent->SimpleCollisionSizeQuads + 1 : 0;
	const int32 BulkDataSize = FullCollisionSizeVerts * FullCollisionSizeVerts + SimpleCollisionSizeVerts * SimpleCollisionSizeVerts;

	void* Data = CollisionComponent->PhysicalMaterialRenderData.Lock(LOCK_READ_WRITE);
	Data = CollisionComponent->PhysicalMaterialRenderData.Realloc(BulkDataSize);
	uint8* WritePtr = (uint8*)Data;

	const int32 CollisionSizes[2] = { FullCollisionSizeVerts, SimpleCollisionSizeVerts };
	for (int32 i = 0; i < 2; ++i)
	{
		const int32 CollisionSizeVerts = CollisionSizes[i];
		if (CollisionSizeVerts == SizeVerts)
		{
			FMemory::Memcpy(WritePtr, InMaterialIds.GetData(), SizeVerts * SizeVerts);
			WritePtr += SizeVerts * SizeVerts;
		}
		else if (CollisionSizeVerts > 0)
		{
			const int32 StepSize = SizeVerts / CollisionSizeVerts;
			check(CollisionSizeVerts * StepSize == SizeVerts);
			for (int32 y = 0; y < SizeVerts; y += StepSize)
			{
				for (int32 x = 0; x < SizeVerts; x += StepSize)
				{
					*WritePtr++ = InMaterialIds[y * SizeVerts + x];
				}
			}
		}
	}

	check(WritePtr - (uint8*)Data == BulkDataSize);
	CollisionComponent->PhysicalMaterialRenderData.Unlock();
}

void ULandscapeComponent::GenerateHeightmapMips(TArray<FColor*>& HeightmapTextureMipData, int32 ComponentX1/*=0*/, int32 ComponentY1/*=0*/, int32 ComponentX2/*=MAX_int32*/, int32 ComponentY2/*=MAX_int32*/, FLandscapeTextureDataInfo* TextureDataInfo/*=nullptr*/)
{
	bool EndX = false;
	bool EndY = false;

	if (ComponentX1 == MAX_int32)
	{
		EndX = true;
		ComponentX1 = 0;
	}

	if (ComponentY1 == MAX_int32)
	{
		EndY = true;
		ComponentY1 = 0;
	}

	if (ComponentX2 == MAX_int32)
	{
		ComponentX2 = ComponentSizeQuads;
	}
	if (ComponentY2 == MAX_int32)
	{
		ComponentY2 = ComponentSizeQuads;
	}

	int32 HeightmapSizeU = GetHeightmap()->Source.GetSizeX();
	int32 HeightmapSizeV = GetHeightmap()->Source.GetSizeY();

	int32 HeightmapOffsetX = FMath::RoundToInt(HeightmapScaleBias.Z * (float)HeightmapSizeU);
	int32 HeightmapOffsetY = FMath::RoundToInt(HeightmapScaleBias.W * (float)HeightmapSizeV);

	for (int32 SubsectionY = 0; SubsectionY < NumSubsections; SubsectionY++)
	{
		// Check if subsection is fully above or below the area we are interested in
		if ((ComponentY2 < SubsectionSizeQuads*SubsectionY) ||		// above
			(ComponentY1 > SubsectionSizeQuads*(SubsectionY + 1)))	// below
		{
			continue;
		}

		for (int32 SubsectionX = 0; SubsectionX < NumSubsections; SubsectionX++)
		{
			// Check if subsection is fully to the left or right of the area we are interested in
			if ((ComponentX2 < SubsectionSizeQuads*SubsectionX) ||		// left
				(ComponentX1 > SubsectionSizeQuads*(SubsectionX + 1)))	// right
			{
				continue;
			}

			// Area to update in previous mip level coords
			int32 PrevMipSubX1 = ComponentX1 - SubsectionSizeQuads*SubsectionX;
			int32 PrevMipSubY1 = ComponentY1 - SubsectionSizeQuads*SubsectionY;
			int32 PrevMipSubX2 = ComponentX2 - SubsectionSizeQuads*SubsectionX;
			int32 PrevMipSubY2 = ComponentY2 - SubsectionSizeQuads*SubsectionY;

			int32 PrevMipSubsectionSizeQuads = SubsectionSizeQuads;
			float InvPrevMipSubsectionSizeQuads = 1.0f / (float)SubsectionSizeQuads;

			int32 PrevMipSizeU = HeightmapSizeU;
			int32 PrevMipSizeV = HeightmapSizeV;

			int32 PrevMipHeightmapOffsetX = HeightmapOffsetX;
			int32 PrevMipHeightmapOffsetY = HeightmapOffsetY;

			for (int32 Mip = 1; Mip < HeightmapTextureMipData.Num(); Mip++)
			{
				int32 MipSizeU = HeightmapSizeU >> Mip;
				int32 MipSizeV = HeightmapSizeV >> Mip;

				int32 MipSubsectionSizeQuads = ((SubsectionSizeQuads + 1) >> Mip) - 1;
				float InvMipSubsectionSizeQuads = 1.0f / (float)MipSubsectionSizeQuads;

				int32 MipHeightmapOffsetX = HeightmapOffsetX >> Mip;
				int32 MipHeightmapOffsetY = HeightmapOffsetY >> Mip;

				// Area to update in current mip level coords
				int32 MipSubX1 = FMath::FloorToInt((float)MipSubsectionSizeQuads * (float)PrevMipSubX1 * InvPrevMipSubsectionSizeQuads);
				int32 MipSubY1 = FMath::FloorToInt((float)MipSubsectionSizeQuads * (float)PrevMipSubY1 * InvPrevMipSubsectionSizeQuads);
				int32 MipSubX2 = FMath::CeilToInt((float)MipSubsectionSizeQuads * (float)PrevMipSubX2 * InvPrevMipSubsectionSizeQuads);
				int32 MipSubY2 = FMath::CeilToInt((float)MipSubsectionSizeQuads * (float)PrevMipSubY2 * InvPrevMipSubsectionSizeQuads);

				// Clamp area to update
				int32 VertX1 = FMath::Clamp<int32>(MipSubX1, 0, MipSubsectionSizeQuads);
				int32 VertY1 = FMath::Clamp<int32>(MipSubY1, 0, MipSubsectionSizeQuads);
				int32 VertX2 = FMath::Clamp<int32>(MipSubX2, 0, MipSubsectionSizeQuads);
				int32 VertY2 = FMath::Clamp<int32>(MipSubY2, 0, MipSubsectionSizeQuads);

				for (int32 VertY = VertY1; VertY <= VertY2; VertY++)
				{
					for (int32 VertX = VertX1; VertX <= VertX2; VertX++)
					{
						// Convert VertX/Y into previous mip's coords
						float PrevMipVertX = (float)PrevMipSubsectionSizeQuads * (float)VertX * InvMipSubsectionSizeQuads;
						float PrevMipVertY = (float)PrevMipSubsectionSizeQuads * (float)VertY * InvMipSubsectionSizeQuads;

#if 0
						// Validate that the vertex we skip wouldn't use the updated data in the parent mip.
						// Note this validation is doesn't do anything unless you change the VertY/VertX loops 
						// above to process all verts from 0 .. MipSubsectionSizeQuads.
						if (VertX < VertX1 || VertX > VertX2)
						{
							check(FMath::CeilToInt(PrevMipVertX) < PrevMipSubX1 || FMath::FloorToInt(PrevMipVertX) > PrevMipSubX2);
							continue;
						}

						if (VertY < VertY1 || VertY > VertY2)
						{
							check(FMath::CeilToInt(PrevMipVertY) < PrevMipSubY1 || FMath::FloorToInt(PrevMipVertY) > PrevMipSubY2);
							continue;
						}
#endif

						// X/Y of the vertex we're looking indexed into the texture data
						int32 TexX = (MipHeightmapOffsetX)+(MipSubsectionSizeQuads + 1) * SubsectionX + VertX;
						int32 TexY = (MipHeightmapOffsetY)+(MipSubsectionSizeQuads + 1) * SubsectionY + VertY;

						float fPrevMipTexX = (float)(PrevMipHeightmapOffsetX)+(float)((PrevMipSubsectionSizeQuads + 1) * SubsectionX) + PrevMipVertX;
						float fPrevMipTexY = (float)(PrevMipHeightmapOffsetY)+(float)((PrevMipSubsectionSizeQuads + 1) * SubsectionY) + PrevMipVertY;

						int32 PrevMipTexX = FMath::FloorToInt(fPrevMipTexX);
						float fPrevMipTexFracX = FMath::Fractional(fPrevMipTexX);
						int32 PrevMipTexY = FMath::FloorToInt(fPrevMipTexY);
						float fPrevMipTexFracY = FMath::Fractional(fPrevMipTexY);

						checkSlow(TexX >= 0 && TexX < MipSizeU);
						checkSlow(TexY >= 0 && TexY < MipSizeV);
						checkSlow(PrevMipTexX >= 0 && PrevMipTexX < PrevMipSizeU);
						checkSlow(PrevMipTexY >= 0 && PrevMipTexY < PrevMipSizeV);

						int32 PrevMipTexX1 = FMath::Min<int32>(PrevMipTexX + 1, PrevMipSizeU - 1);
						int32 PrevMipTexY1 = FMath::Min<int32>(PrevMipTexY + 1, PrevMipSizeV - 1);

						// Padding for missing data for MIP 0
						if (Mip == 1)
						{
							if (EndX && SubsectionX == NumSubsections - 1 && VertX == VertX2)
							{
								for (int32 PaddingIdx = PrevMipTexX + PrevMipTexY * PrevMipSizeU; PaddingIdx + 1 < PrevMipTexY1 * PrevMipSizeU; ++PaddingIdx)
								{
									HeightmapTextureMipData[Mip - 1][PaddingIdx + 1] = HeightmapTextureMipData[Mip - 1][PaddingIdx];
								}
							}

							if (EndY && SubsectionX == NumSubsections - 1 && SubsectionY == NumSubsections - 1 && VertY == VertY2 && VertX == VertX2)
							{
								for (int32 PaddingYIdx = PrevMipTexY; PaddingYIdx + 1 < PrevMipSizeV; ++PaddingYIdx)
								{
									for (int32 PaddingXIdx = 0; PaddingXIdx < PrevMipSizeU; ++PaddingXIdx)
									{
										HeightmapTextureMipData[Mip - 1][PaddingXIdx + (PaddingYIdx + 1) * PrevMipSizeU] = HeightmapTextureMipData[Mip - 1][PaddingXIdx + PaddingYIdx * PrevMipSizeU];
									}
								}
							}
						}

						FColor* TexData = &(HeightmapTextureMipData[Mip])[TexX + TexY * MipSizeU];
						FColor *PreMipTexData00 = &(HeightmapTextureMipData[Mip - 1])[PrevMipTexX + PrevMipTexY * PrevMipSizeU];
						FColor *PreMipTexData01 = &(HeightmapTextureMipData[Mip - 1])[PrevMipTexX + PrevMipTexY1 * PrevMipSizeU];
						FColor *PreMipTexData10 = &(HeightmapTextureMipData[Mip - 1])[PrevMipTexX1 + PrevMipTexY * PrevMipSizeU];
						FColor *PreMipTexData11 = &(HeightmapTextureMipData[Mip - 1])[PrevMipTexX1 + PrevMipTexY1 * PrevMipSizeU];

						// Lerp height values
						uint16 PrevMipHeightValue00 = PreMipTexData00->R << 8 | PreMipTexData00->G;
						uint16 PrevMipHeightValue01 = PreMipTexData01->R << 8 | PreMipTexData01->G;
						uint16 PrevMipHeightValue10 = PreMipTexData10->R << 8 | PreMipTexData10->G;
						uint16 PrevMipHeightValue11 = PreMipTexData11->R << 8 | PreMipTexData11->G;
						uint16 HeightValue = FMath::RoundToInt(
							FMath::Lerp(
							FMath::Lerp((float)PrevMipHeightValue00, (float)PrevMipHeightValue10, fPrevMipTexFracX),
							FMath::Lerp((float)PrevMipHeightValue01, (float)PrevMipHeightValue11, fPrevMipTexFracX),
							fPrevMipTexFracY));

						TexData->R = HeightValue >> 8;
						TexData->G = HeightValue & 255;

						// Lerp tangents
						TexData->B = FMath::RoundToInt(
							FMath::Lerp(
							FMath::Lerp((float)PreMipTexData00->B, (float)PreMipTexData10->B, fPrevMipTexFracX),
							FMath::Lerp((float)PreMipTexData01->B, (float)PreMipTexData11->B, fPrevMipTexFracX),
							fPrevMipTexFracY));

						TexData->A = FMath::RoundToInt(
							FMath::Lerp(
							FMath::Lerp((float)PreMipTexData00->A, (float)PreMipTexData10->A, fPrevMipTexFracX),
							FMath::Lerp((float)PreMipTexData01->A, (float)PreMipTexData11->A, fPrevMipTexFracX),
							fPrevMipTexFracY));

						// Padding for missing data
						if (EndX && SubsectionX == NumSubsections - 1 && VertX == VertX2)
						{
							for (int32 PaddingIdx = TexX + TexY * MipSizeU; PaddingIdx + 1 < (TexY + 1) * MipSizeU; ++PaddingIdx)
							{
								HeightmapTextureMipData[Mip][PaddingIdx + 1] = HeightmapTextureMipData[Mip][PaddingIdx];
							}
						}

						if (EndY && SubsectionX == NumSubsections - 1 && SubsectionY == NumSubsections - 1 && VertY == VertY2 && VertX == VertX2)
						{
							for (int32 PaddingYIdx = TexY; PaddingYIdx + 1 < MipSizeV; ++PaddingYIdx)
							{
								for (int32 PaddingXIdx = 0; PaddingXIdx < MipSizeU; ++PaddingXIdx)
								{
									HeightmapTextureMipData[Mip][PaddingXIdx + (PaddingYIdx + 1) * MipSizeU] = HeightmapTextureMipData[Mip][PaddingXIdx + PaddingYIdx * MipSizeU];
								}
							}
						}

					}
				}

				// Record the areas we updated
				if (TextureDataInfo)
				{
					int32 TexX1 = (MipHeightmapOffsetX)+(MipSubsectionSizeQuads + 1) * SubsectionX + VertX1;
					int32 TexY1 = (MipHeightmapOffsetY)+(MipSubsectionSizeQuads + 1) * SubsectionY + VertY1;
					int32 TexX2 = (MipHeightmapOffsetX)+(MipSubsectionSizeQuads + 1) * SubsectionX + VertX2;
					int32 TexY2 = (MipHeightmapOffsetY)+(MipSubsectionSizeQuads + 1) * SubsectionY + VertY2;
					TextureDataInfo->AddMipUpdateRegion(Mip, TexX1, TexY1, TexX2, TexY2);
				}

				// Copy current mip values to prev as we move to the next mip.
				PrevMipSubsectionSizeQuads = MipSubsectionSizeQuads;
				InvPrevMipSubsectionSizeQuads = InvMipSubsectionSizeQuads;

				PrevMipSizeU = MipSizeU;
				PrevMipSizeV = MipSizeV;

				PrevMipHeightmapOffsetX = MipHeightmapOffsetX;
				PrevMipHeightmapOffsetY = MipHeightmapOffsetY;

				// Use this mip's area as we move to the next mip
				PrevMipSubX1 = MipSubX1;
				PrevMipSubY1 = MipSubY1;
				PrevMipSubX2 = MipSubX2;
				PrevMipSubY2 = MipSubY2;
			}
		}
	}
}

void ULandscapeComponent::CreateEmptyTextureMips(UTexture2D* Texture, bool bClear /*= false*/)
{
	ETextureSourceFormat Format = Texture->Source.GetFormat();
	int32 SizeU = Texture->Source.GetSizeX();
	int32 SizeV = Texture->Source.GetSizeY();

	if (bClear)
	{
		Texture->Source.Init2DWithMipChain(SizeU, SizeV, Format);
		int32 NumMips = Texture->Source.GetNumMips();
		for (int32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
		{
			uint8* MipData = Texture->Source.LockMip(MipIndex);
			FMemory::Memzero(MipData, Texture->Source.CalcMipSize(MipIndex));
			Texture->Source.UnlockMip(MipIndex);
		}
	}
	else
	{
		TArray64<uint8> TopMipData;
		Texture->Source.GetMipData(TopMipData, 0);
		Texture->Source.Init2DWithMipChain(SizeU, SizeV, Format);
		int32 NumMips = Texture->Source.GetNumMips();
		uint8* MipData = Texture->Source.LockMip(0);
		FMemory::Memcpy(MipData, TopMipData.GetData(), TopMipData.Num());
		Texture->Source.UnlockMip(0);
	}
}

template<typename DataType>
void ULandscapeComponent::GenerateMipsTempl(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* Texture, DataType* BaseMipData)
{
	// Stores pointers to the locked mip data
	TArray<DataType*> MipData;
	MipData.Add(BaseMipData);
	for (int32 MipIndex = 1; MipIndex < Texture->Source.GetNumMips(); ++MipIndex)
	{
		MipData.Add((DataType*)Texture->Source.LockMip(MipIndex));
	}

	// Update the newly created mips
	UpdateMipsTempl<DataType>(InNumSubsections, InSubsectionSizeQuads, Texture, MipData);

	// Unlock all the new mips, but not the base mip's data
	for (int32 i = 1; i < MipData.Num(); i++)
	{
		Texture->Source.UnlockMip(i);
	}
}

void ULandscapeComponent::GenerateWeightmapMips(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* WeightmapTexture, FColor* BaseMipData)
{
	GenerateMipsTempl<FColor>(InNumSubsections, InSubsectionSizeQuads, WeightmapTexture, BaseMipData);
}

namespace
{
	template<typename DataType>
	void BiLerpTextureData(DataType* Output, const DataType* Data00, const DataType* Data10, const DataType* Data01, const DataType* Data11, float FracX, float FracY)
	{
		*Output = FMath::RoundToInt(
			FMath::Lerp(
			FMath::Lerp((float)*Data00, (float)*Data10, FracX),
			FMath::Lerp((float)*Data01, (float)*Data11, FracX),
			FracY));
	}

	template<>
	void BiLerpTextureData(FColor* Output, const FColor* Data00, const FColor* Data10, const FColor* Data01, const FColor* Data11, float FracX, float FracY)
	{
		Output->R = FMath::RoundToInt(
			FMath::Lerp(
			FMath::Lerp((float)Data00->R, (float)Data10->R, FracX),
			FMath::Lerp((float)Data01->R, (float)Data11->R, FracX),
			FracY));
		Output->G = FMath::RoundToInt(
			FMath::Lerp(
			FMath::Lerp((float)Data00->G, (float)Data10->G, FracX),
			FMath::Lerp((float)Data01->G, (float)Data11->G, FracX),
			FracY));
		Output->B = FMath::RoundToInt(
			FMath::Lerp(
			FMath::Lerp((float)Data00->B, (float)Data10->B, FracX),
			FMath::Lerp((float)Data01->B, (float)Data11->B, FracX),
			FracY));
		Output->A = FMath::RoundToInt(
			FMath::Lerp(
			FMath::Lerp((float)Data00->A, (float)Data10->A, FracX),
			FMath::Lerp((float)Data01->A, (float)Data11->A, FracX),
			FracY));
	}

	template<typename DataType>
	void AverageTexData(DataType* Output, const DataType* Data00, const DataType* Data10, const DataType* Data01, const DataType* Data11)
	{
		*Output = (((int32)(*Data00) + (int32)(*Data10) + (int32)(*Data01) + (int32)(*Data11)) >> 2);
	}

	template<>
	void AverageTexData(FColor* Output, const FColor* Data00, const FColor* Data10, const FColor* Data01, const FColor* Data11)
	{
		Output->R = (((int32)Data00->R + (int32)Data10->R + (int32)Data01->R + (int32)Data11->R) >> 2);
		Output->G = (((int32)Data00->G + (int32)Data10->G + (int32)Data01->G + (int32)Data11->G) >> 2);
		Output->B = (((int32)Data00->B + (int32)Data10->B + (int32)Data01->B + (int32)Data11->B) >> 2);
		Output->A = (((int32)Data00->A + (int32)Data10->A + (int32)Data01->A + (int32)Data11->A) >> 2);
	}

};

template<typename DataType>
void ULandscapeComponent::UpdateMipsTempl(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* Texture, TArray<DataType*>& TextureMipData, int32 ComponentX1/*=0*/, int32 ComponentY1/*=0*/, int32 ComponentX2/*=MAX_int32*/, int32 ComponentY2/*=MAX_int32*/, struct FLandscapeTextureDataInfo* TextureDataInfo/*=nullptr*/)
{
	int32 WeightmapSizeU = Texture->Source.GetSizeX();
	int32 WeightmapSizeV = Texture->Source.GetSizeY();

	// Find the maximum mip where each texel's data comes from just one subsection.
	int32 MaxWholeSubsectionMip = FMath::FloorLog2(InSubsectionSizeQuads + 1) - 1;

	// Update the mip where each texel's data comes from just one subsection.
	for (int32 SubsectionY = 0; SubsectionY < InNumSubsections; SubsectionY++)
	{
		// Check if subsection is fully above or below the area we are interested in
		if ((ComponentY2 < InSubsectionSizeQuads*SubsectionY) ||	// above
			(ComponentY1 > InSubsectionSizeQuads*(SubsectionY + 1)))	// below
		{
			continue;
		}

		for (int32 SubsectionX = 0; SubsectionX < InNumSubsections; SubsectionX++)
		{
			// Check if subsection is fully to the left or right of the area we are interested in
			if ((ComponentX2 < InSubsectionSizeQuads*SubsectionX) ||	// left
				(ComponentX1 > InSubsectionSizeQuads*(SubsectionX + 1)))	// right
			{
				continue;
			}

			// Area to update in previous mip level coords
			int32 PrevMipSubX1 = ComponentX1 - InSubsectionSizeQuads*SubsectionX;
			int32 PrevMipSubY1 = ComponentY1 - InSubsectionSizeQuads*SubsectionY;
			int32 PrevMipSubX2 = ComponentX2 - InSubsectionSizeQuads*SubsectionX;
			int32 PrevMipSubY2 = ComponentY2 - InSubsectionSizeQuads*SubsectionY;

			int32 PrevMipSubsectionSizeQuads = InSubsectionSizeQuads;
			float InvPrevMipSubsectionSizeQuads = 1.0f / (float)InSubsectionSizeQuads;

			int32 PrevMipSizeU = WeightmapSizeU;
			int32 PrevMipSizeV = WeightmapSizeV;

			for (int32 Mip = 1; Mip <= MaxWholeSubsectionMip; Mip++)
			{
				int32 MipSizeU = WeightmapSizeU >> Mip;
				int32 MipSizeV = WeightmapSizeV >> Mip;

				int32 MipSubsectionSizeQuads = ((InSubsectionSizeQuads + 1) >> Mip) - 1;
				float InvMipSubsectionSizeQuads = 1.0f / (float)MipSubsectionSizeQuads;

				// Area to update in current mip level coords
				int32 MipSubX1 = FMath::FloorToInt((float)MipSubsectionSizeQuads * (float)PrevMipSubX1 * InvPrevMipSubsectionSizeQuads);
				int32 MipSubY1 = FMath::FloorToInt((float)MipSubsectionSizeQuads * (float)PrevMipSubY1 * InvPrevMipSubsectionSizeQuads);
				int32 MipSubX2 = FMath::CeilToInt((float)MipSubsectionSizeQuads * (float)PrevMipSubX2 * InvPrevMipSubsectionSizeQuads);
				int32 MipSubY2 = FMath::CeilToInt((float)MipSubsectionSizeQuads * (float)PrevMipSubY2 * InvPrevMipSubsectionSizeQuads);

				// Clamp area to update
				int32 VertX1 = FMath::Clamp<int32>(MipSubX1, 0, MipSubsectionSizeQuads);
				int32 VertY1 = FMath::Clamp<int32>(MipSubY1, 0, MipSubsectionSizeQuads);
				int32 VertX2 = FMath::Clamp<int32>(MipSubX2, 0, MipSubsectionSizeQuads);
				int32 VertY2 = FMath::Clamp<int32>(MipSubY2, 0, MipSubsectionSizeQuads);

				for (int32 VertY = VertY1; VertY <= VertY2; VertY++)
				{
					for (int32 VertX = VertX1; VertX <= VertX2; VertX++)
					{
						// Convert VertX/Y into previous mip's coords
						float PrevMipVertX = (float)PrevMipSubsectionSizeQuads * (float)VertX * InvMipSubsectionSizeQuads;
						float PrevMipVertY = (float)PrevMipSubsectionSizeQuads * (float)VertY * InvMipSubsectionSizeQuads;

						// X/Y of the vertex we're looking indexed into the texture data
						int32 TexX = (MipSubsectionSizeQuads + 1) * SubsectionX + VertX;
						int32 TexY = (MipSubsectionSizeQuads + 1) * SubsectionY + VertY;

						float fPrevMipTexX = (float)((PrevMipSubsectionSizeQuads + 1) * SubsectionX) + PrevMipVertX;
						float fPrevMipTexY = (float)((PrevMipSubsectionSizeQuads + 1) * SubsectionY) + PrevMipVertY;

						int32 PrevMipTexX = FMath::FloorToInt(fPrevMipTexX);
						float fPrevMipTexFracX = FMath::Fractional(fPrevMipTexX);
						int32 PrevMipTexY = FMath::FloorToInt(fPrevMipTexY);
						float fPrevMipTexFracY = FMath::Fractional(fPrevMipTexY);

						check(TexX >= 0 && TexX < MipSizeU);
						check(TexY >= 0 && TexY < MipSizeV);
						check(PrevMipTexX >= 0 && PrevMipTexX < PrevMipSizeU);
						check(PrevMipTexY >= 0 && PrevMipTexY < PrevMipSizeV);

						int32 PrevMipTexX1 = FMath::Min<int32>(PrevMipTexX + 1, PrevMipSizeU - 1);
						int32 PrevMipTexY1 = FMath::Min<int32>(PrevMipTexY + 1, PrevMipSizeV - 1);

						DataType* TexData = &(TextureMipData[Mip])[TexX + TexY * MipSizeU];
						DataType *PreMipTexData00 = &(TextureMipData[Mip - 1])[PrevMipTexX + PrevMipTexY * PrevMipSizeU];
						DataType *PreMipTexData01 = &(TextureMipData[Mip - 1])[PrevMipTexX + PrevMipTexY1 * PrevMipSizeU];
						DataType *PreMipTexData10 = &(TextureMipData[Mip - 1])[PrevMipTexX1 + PrevMipTexY * PrevMipSizeU];
						DataType *PreMipTexData11 = &(TextureMipData[Mip - 1])[PrevMipTexX1 + PrevMipTexY1 * PrevMipSizeU];

						// Lerp weightmap data
						BiLerpTextureData<DataType>(TexData, PreMipTexData00, PreMipTexData10, PreMipTexData01, PreMipTexData11, fPrevMipTexFracX, fPrevMipTexFracY);
					}
				}

				// Record the areas we updated
				if (TextureDataInfo)
				{
					int32 TexX1 = (MipSubsectionSizeQuads + 1) * SubsectionX + VertX1;
					int32 TexY1 = (MipSubsectionSizeQuads + 1) * SubsectionY + VertY1;
					int32 TexX2 = (MipSubsectionSizeQuads + 1) * SubsectionX + VertX2;
					int32 TexY2 = (MipSubsectionSizeQuads + 1) * SubsectionY + VertY2;
					TextureDataInfo->AddMipUpdateRegion(Mip, TexX1, TexY1, TexX2, TexY2);
				}

				// Copy current mip values to prev as we move to the next mip.
				PrevMipSubsectionSizeQuads = MipSubsectionSizeQuads;
				InvPrevMipSubsectionSizeQuads = InvMipSubsectionSizeQuads;

				PrevMipSizeU = MipSizeU;
				PrevMipSizeV = MipSizeV;

				// Use this mip's area as we move to the next mip
				PrevMipSubX1 = MipSubX1;
				PrevMipSubY1 = MipSubY1;
				PrevMipSubX2 = MipSubX2;
				PrevMipSubY2 = MipSubY2;
			}
		}
	}

	// Handle mips that have texels from multiple subsections
	// not valid weight data, so just average the texels of the previous mip.
	for (int32 Mip = MaxWholeSubsectionMip + 1;; ++Mip)
	{
		int32 MipSubsectionSizeQuads = ((InSubsectionSizeQuads + 1) >> Mip) - 1;
		checkSlow(MipSubsectionSizeQuads <= 0);

		int32 MipSizeU = FMath::Max<int32>(WeightmapSizeU >> Mip, 1);
		int32 MipSizeV = FMath::Max<int32>(WeightmapSizeV >> Mip, 1);

		int32 PrevMipSizeU = FMath::Max<int32>(WeightmapSizeU >> (Mip - 1), 1);
		int32 PrevMipSizeV = FMath::Max<int32>(WeightmapSizeV >> (Mip - 1), 1);

		for (int32 Y = 0; Y < MipSizeV; Y++)
		{
			for (int32 X = 0; X < MipSizeU; X++)
			{
				DataType* TexData = &(TextureMipData[Mip])[X + Y * MipSizeU];

				DataType *PreMipTexData00 = &(TextureMipData[Mip - 1])[(X * 2 + 0) + (Y * 2 + 0)  * PrevMipSizeU];
				DataType *PreMipTexData01 = &(TextureMipData[Mip - 1])[(X * 2 + 0) + (Y * 2 + 1)  * PrevMipSizeU];
				DataType *PreMipTexData10 = &(TextureMipData[Mip - 1])[(X * 2 + 1) + (Y * 2 + 0)  * PrevMipSizeU];
				DataType *PreMipTexData11 = &(TextureMipData[Mip - 1])[(X * 2 + 1) + (Y * 2 + 1)  * PrevMipSizeU];

				AverageTexData<DataType>(TexData, PreMipTexData00, PreMipTexData10, PreMipTexData01, PreMipTexData11);
			}
		}

		if (TextureDataInfo)
		{
			// These mip sizes are small enough that we may as well just update the whole mip.
			TextureDataInfo->AddMipUpdateRegion(Mip, 0, 0, MipSizeU - 1, MipSizeV - 1);
		}

		if (MipSizeU == 1 && MipSizeV == 1)
		{
			break;
		}
	}
}

void ULandscapeComponent::UpdateWeightmapMips(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* WeightmapTexture, TArray<FColor*>& WeightmapTextureMipData, int32 ComponentX1/*=0*/, int32 ComponentY1/*=0*/, int32 ComponentX2/*=MAX_int32*/, int32 ComponentY2/*=MAX_int32*/, struct FLandscapeTextureDataInfo* TextureDataInfo/*=nullptr*/)
{
	UpdateMipsTempl<FColor>(InNumSubsections, InSubsectionSizeQuads, WeightmapTexture, WeightmapTextureMipData, ComponentX1, ComponentY1, ComponentX2, ComponentY2, TextureDataInfo);
}

void ULandscapeComponent::UpdateDataMips(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* Texture, TArray<uint8*>& TextureMipData, int32 ComponentX1/*=0*/, int32 ComponentY1/*=0*/, int32 ComponentX2/*=MAX_int32*/, int32 ComponentY2/*=MAX_int32*/, struct FLandscapeTextureDataInfo* TextureDataInfo/*=nullptr*/)
{
	UpdateMipsTempl<uint8>(InNumSubsections, InSubsectionSizeQuads, Texture, TextureMipData, ComponentX1, ComponentY1, ComponentX2, ComponentY2, TextureDataInfo);
}

float ULandscapeComponent::GetLayerWeightAtLocation(const FVector& InLocation, ULandscapeLayerInfoObject* LayerInfo, TArray<uint8>* LayerCache, bool bUseEditingWeightmap)
{
	// Allocate and discard locally if no external cache is passed in.
	TArray<uint8> LocalCache;
	if (LayerCache == nullptr)
	{
		LayerCache = &LocalCache;
	}

	// Fill the cache if necessary
	if (LayerCache->Num() == 0)
	{
		FLandscapeComponentDataInterface CDI(this);
		if (!CDI.GetWeightmapTextureData(LayerInfo, *LayerCache, bUseEditingWeightmap))
		{
			// no data for this layer for this component.
			return 0.0f;
		}
	}

	// Find location
	const FVector TestLocation = GetComponentToWorld().InverseTransformPosition(InLocation);
	
	// Abort if the test location is not on this component
	if (TestLocation.X < 0 || TestLocation.Y < 0 || TestLocation.X > ComponentSizeQuads || TestLocation.Y > ComponentSizeQuads)
	{
		return 0.0f;
	}

	// Find data
	int32 X1 = FMath::FloorToInt(TestLocation.X);
	int32 Y1 = FMath::FloorToInt(TestLocation.Y);
	int32 X2 = FMath::CeilToInt(TestLocation.X);
	int32 Y2 = FMath::CeilToInt(TestLocation.Y);

	int32 Stride = (SubsectionSizeQuads + 1) * NumSubsections;

	// Min is to prevent the sampling of the final column from overflowing
	int32 IdxX1 = FMath::Min<int32>(((X1 / SubsectionSizeQuads) * (SubsectionSizeQuads + 1)) + (X1 % SubsectionSizeQuads), Stride - 1);
	int32 IdxY1 = FMath::Min<int32>(((Y1 / SubsectionSizeQuads) * (SubsectionSizeQuads + 1)) + (Y1 % SubsectionSizeQuads), Stride - 1);
	int32 IdxX2 = FMath::Min<int32>(((X2 / SubsectionSizeQuads) * (SubsectionSizeQuads + 1)) + (X2 % SubsectionSizeQuads), Stride - 1);
	int32 IdxY2 = FMath::Min<int32>(((Y2 / SubsectionSizeQuads) * (SubsectionSizeQuads + 1)) + (Y2 % SubsectionSizeQuads), Stride - 1);

	// sample
	float Sample11 = (float)((*LayerCache)[IdxX1 + Stride * IdxY1]) / 255.0f;
	float Sample21 = (float)((*LayerCache)[IdxX2 + Stride * IdxY1]) / 255.0f;
	float Sample12 = (float)((*LayerCache)[IdxX1 + Stride * IdxY2]) / 255.0f;
	float Sample22 = (float)((*LayerCache)[IdxX2 + Stride * IdxY2]) / 255.0f;

	float LerpX = FMath::Fractional(TestLocation.X);
	float LerpY = FMath::Fractional(TestLocation.Y);

	// Bilinear interpolate
	return FMath::Lerp(
		FMath::Lerp(Sample11, Sample21, LerpX),
		FMath::Lerp(Sample12, Sample22, LerpX),
		LerpY);

}

void ULandscapeComponent::GetComponentExtent(int32& MinX, int32& MinY, int32& MaxX, int32& MaxY) const
{
	MinX = FMath::Min(SectionBaseX, MinX);
	MinY = FMath::Min(SectionBaseY, MinY);
	MaxX = FMath::Max(SectionBaseX + ComponentSizeQuads, MaxX);
	MaxY = FMath::Max(SectionBaseY + ComponentSizeQuads, MaxY);
}

//
// ALandscape
//
bool ULandscapeInfo::AreAllComponentsRegistered() const
{
	const TArray<ALandscapeProxy*>& LandscapeProxies = ALandscapeProxy::GetLandscapeProxies();
	for(ALandscapeProxy* LandscapeProxy : LandscapeProxies)
	{
		if (LandscapeProxy->IsPendingKill())
		{
			continue;
		}

		if (LandscapeProxy->GetLandscapeGuid() == LandscapeGuid)
		{
			if (LandscapeProxy->SplineComponent && !LandscapeProxy->SplineComponent->IsRegistered())
			{
				return false;
			}

			for (ULandscapeComponent* LandscapeComponent : LandscapeProxy->LandscapeComponents)
			{
				if (LandscapeComponent && !LandscapeComponent->IsRegistered())
				{
					return false;
				}
			}
		}
	}

	return true;
}

#define MAX_LANDSCAPE_SUBSECTIONS 2

void ULandscapeInfo::GetComponentsInRegion(int32 X1, int32 Y1, int32 X2, int32 Y2, TSet<ULandscapeComponent*>& OutComponents, bool bOverlap) const
{
	// Find component range for this block of data
	// X2/Y2 Coordinates are "inclusive" max values
	int32 ComponentIndexX1, ComponentIndexY1, ComponentIndexX2, ComponentIndexY2;
	if (bOverlap)
	{
		ALandscape::CalcComponentIndicesOverlap(X1, Y1, X2, Y2, ComponentSizeQuads, ComponentIndexX1, ComponentIndexY1, ComponentIndexX2, ComponentIndexY2);
	}
	else
	{
		ALandscape::CalcComponentIndicesNoOverlap(X1, Y1, X2, Y2, ComponentSizeQuads, ComponentIndexX1, ComponentIndexY1, ComponentIndexX2, ComponentIndexY2);
	}

	for (int32 ComponentIndexY = ComponentIndexY1; ComponentIndexY <= ComponentIndexY2; ComponentIndexY++)
	{
		for (int32 ComponentIndexX = ComponentIndexX1; ComponentIndexX <= ComponentIndexX2; ComponentIndexX++)
		{
			ULandscapeComponent* Component = XYtoComponentMap.FindRef(FIntPoint(ComponentIndexX, ComponentIndexY));
			if (Component && !FLevelUtils::IsLevelLocked(Component->GetLandscapeProxy()->GetLevel()) && FLevelUtils::IsLevelVisible(Component->GetLandscapeProxy()->GetLevel()))
			{
				OutComponents.Add(Component);
			}
		}
	}
}

// A struct to remember where we have spare texture channels.
struct FWeightmapTextureAllocation
{
	int32 X;
	int32 Y;
	int32 ChannelsInUse;
	UTexture2D* Texture;
	FColor* TextureData;

	FWeightmapTextureAllocation(int32 InX, int32 InY, int32 InChannels, UTexture2D* InTexture, FColor* InTextureData)
		: X(InX)
		, Y(InY)
		, ChannelsInUse(InChannels)
		, Texture(InTexture)
		, TextureData(InTextureData)
	{}
};

// A struct to hold the info about each texture chunk of the total heightmap
struct FHeightmapInfo
{
	int32 HeightmapSizeU;
	int32 HeightmapSizeV;
	UTexture2D* HeightmapTexture;
	TArray<FColor*> HeightmapTextureMipData;
};

TArray<FName> ALandscapeProxy::GetLayersFromMaterial(UMaterialInterface* MaterialInterface)
{
	TArray<FName> Result;

	if (MaterialInterface)
	{
		TArray<FMaterialParameterInfo> OutParameterInfo;
		TArray<FGuid> Guids;
		if (UMaterialInstance* Instance = Cast<UMaterialInstance>(MaterialInterface))
		{
			Instance->GetAllParameterInfo<UMaterialExpressionLandscapeLayerBlend>(OutParameterInfo, Guids);
			Instance->GetAllParameterInfo<UMaterialExpressionLandscapeLayerWeight>(OutParameterInfo, Guids);
			Instance->GetAllParameterInfo<UMaterialExpressionLandscapeLayerSwitch>(OutParameterInfo, Guids);
			Instance->GetAllParameterInfo<UMaterialExpressionLandscapeLayerSample>(OutParameterInfo, Guids);
		}
		else if (UMaterial* Material = MaterialInterface->GetMaterial())
		{
			Material->GetAllParameterInfo<UMaterialExpressionLandscapeLayerBlend>(OutParameterInfo, Guids);
			Material->GetAllParameterInfo<UMaterialExpressionLandscapeLayerWeight>(OutParameterInfo, Guids);
			Material->GetAllParameterInfo<UMaterialExpressionLandscapeLayerSwitch>(OutParameterInfo, Guids);
			Material->GetAllParameterInfo<UMaterialExpressionLandscapeLayerSample>(OutParameterInfo, Guids);
		}

		for (const FMaterialParameterInfo& ParameterInfo : OutParameterInfo)
		{
			Result.AddUnique(ParameterInfo.Name);
		}
	}

	return Result;
}

TArray<FName> ALandscapeProxy::GetLayersFromMaterial() const
{
	return GetLayersFromMaterial(LandscapeMaterial);
}

ULandscapeLayerInfoObject* ALandscapeProxy::CreateLayerInfo(const TCHAR* LayerName, ULevel* Level)
{
	FName LayerObjectName = FName(*FString::Printf(TEXT("LayerInfoObject_%s"), LayerName));
	FString Path = Level->GetOutermost()->GetName() + TEXT("_sharedassets/");
	if (Path.StartsWith("/Temp/"))
	{
		Path = FString("/Game/") + Path.RightChop(FString("/Temp/").Len());
	}
	FString PackageName = Path + LayerObjectName.ToString();
	FString PackageFilename;
	int32 Suffix = 1;
	while (FPackageName::DoesPackageExist(PackageName, nullptr, &PackageFilename))
	{
		LayerObjectName = FName(*FString::Printf(TEXT("LayerInfoObject_%s_%d"), LayerName, Suffix));
		PackageName = Path + LayerObjectName.ToString();
		Suffix++;
	}
	UPackage* Package = CreatePackage(nullptr, *PackageName);
	ULandscapeLayerInfoObject* LayerInfo = NewObject<ULandscapeLayerInfoObject>(Package, LayerObjectName, RF_Public | RF_Standalone | RF_Transactional);
	LayerInfo->LayerName = LayerName;

	return LayerInfo;
}

ULandscapeLayerInfoObject* ALandscapeProxy::CreateLayerInfo(const TCHAR* LayerName)
{
	ULandscapeLayerInfoObject* LayerInfo = ALandscapeProxy::CreateLayerInfo(LayerName, GetLevel());

	check(LayerInfo);

	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (LandscapeInfo)
	{
		int32 Index = LandscapeInfo->GetLayerInfoIndex(LayerName, this);
		if (Index == INDEX_NONE)
		{
			LandscapeInfo->Layers.Add(FLandscapeInfoLayerSettings(LayerInfo, this));
		}
		else
		{
			LandscapeInfo->Layers[Index].LayerInfoObj = LayerInfo;
		}
	}

	return LayerInfo;
}

#define HEIGHTDATA(X,Y) (HeightData[ FMath::Clamp<int32>(Y,0,VertsY) * VertsX + FMath::Clamp<int32>(X,0,VertsX) ])
ENGINE_API extern bool GDisableAutomaticTextureMaterialUpdateDependencies;

LANDSCAPE_API void ALandscapeProxy::Import(const FGuid& InGuid, int32 InMinX, int32 InMinY, int32 InMaxX, int32 InMaxY, int32 InNumSubsections, int32 InSubsectionSizeQuads, const TMap<FGuid, TArray<uint16>>& InImportHeightData, 
										   const TCHAR* const InHeightmapFileName, const TMap<FGuid, TArray<FLandscapeImportLayerInfo>>& InImportMaterialLayerInfos, ELandscapeImportAlphamapType InImportMaterialLayerType, const TArray<FLandscapeLayer>* InImportLayers)
{
	check(InGuid.IsValid());
	check(InImportHeightData.Num() == InImportMaterialLayerInfos.Num());

	check(CanHaveLayersContent() || InImportLayers == nullptr);

	GWarn->BeginSlowTask(LOCTEXT("BeingImportingLandscapeTask", "Importing Landscape"), true);

	const int32 VertsX = InMaxX - InMinX + 1;
	const int32 VertsY = InMaxY - InMinY + 1;

	ComponentSizeQuads = InNumSubsections * InSubsectionSizeQuads;
	NumSubsections = InNumSubsections;
	SubsectionSizeQuads = InSubsectionSizeQuads;
	LandscapeGuid = InGuid;

	Modify();

	const int32 NumPatchesX = (VertsX - 1);
	const int32 NumPatchesY = (VertsY - 1);

	const int32 NumComponentsX = NumPatchesX / ComponentSizeQuads;
	const int32 NumComponentsY = NumPatchesY / ComponentSizeQuads;

	// currently only support importing into a new/blank landscape actor/proxy
	check(LandscapeComponents.Num() == 0);
	LandscapeComponents.Empty(NumComponentsX * NumComponentsY);

	for (int32 Y = 0; Y < NumComponentsY; Y++)
	{
		for (int32 X = 0; X < NumComponentsX; X++)
		{
			const int32 BaseX = InMinX + X * ComponentSizeQuads;
			const int32 BaseY = InMinY + Y * ComponentSizeQuads;

			ULandscapeComponent* LandscapeComponent = NewObject<ULandscapeComponent>(this, NAME_None, RF_Transactional);
			LandscapeComponent->SetRelativeLocation(FVector(BaseX, BaseY, 0));
			LandscapeComponent->SetupAttachment(GetRootComponent(), NAME_None);
			LandscapeComponents.Add(LandscapeComponent);
			LandscapeComponent->Init(BaseX, BaseY, ComponentSizeQuads, NumSubsections, SubsectionSizeQuads);

			// Assign shared properties
			LandscapeComponent->UpdatedSharedPropertiesFromActor();
		}
	}

	// Ensure that we don't pack so many heightmaps into a texture that their lowest LOD isn't guaranteed to be resident
#define MAX_HEIGHTMAP_TEXTURE_SIZE 512
	const int32 ComponentSizeVerts = NumSubsections * (SubsectionSizeQuads + 1);
	const int32 ComponentsPerHeightmap = FMath::Min(MAX_HEIGHTMAP_TEXTURE_SIZE / ComponentSizeVerts, 1 << (UTexture2D::GetStaticMinTextureResidentMipCount() - 2));
	check(ComponentsPerHeightmap > 0);

	// Count how many heightmaps we need and the X dimension of the final heightmap
	int32 NumHeightmapsX = 1;
	int32 FinalComponentsX = NumComponentsX;
	while (FinalComponentsX > ComponentsPerHeightmap)
	{
		FinalComponentsX -= ComponentsPerHeightmap;
		NumHeightmapsX++;
	}
	// Count how many heightmaps we need and the Y dimension of the final heightmap
	int32 NumHeightmapsY = 1;
	int32 FinalComponentsY = NumComponentsY;
	while (FinalComponentsY > ComponentsPerHeightmap)
	{
		FinalComponentsY -= ComponentsPerHeightmap;
		NumHeightmapsY++;
	}

	TArray<FHeightmapInfo> HeightmapInfos;

	for (int32 HmY = 0; HmY < NumHeightmapsY; HmY++)
	{
		for (int32 HmX = 0; HmX < NumHeightmapsX; HmX++)
		{
			FHeightmapInfo& HeightmapInfo = HeightmapInfos[HeightmapInfos.AddZeroed()];

			// make sure the heightmap UVs are powers of two.
			HeightmapInfo.HeightmapSizeU = (1 << FMath::CeilLogTwo(((HmX == NumHeightmapsX - 1) ? FinalComponentsX : ComponentsPerHeightmap) * ComponentSizeVerts));
			HeightmapInfo.HeightmapSizeV = (1 << FMath::CeilLogTwo(((HmY == NumHeightmapsY - 1) ? FinalComponentsY : ComponentsPerHeightmap) * ComponentSizeVerts));

			// Construct the heightmap textures
			HeightmapInfo.HeightmapTexture = CreateLandscapeTexture(HeightmapInfo.HeightmapSizeU, HeightmapInfo.HeightmapSizeV, TEXTUREGROUP_Terrain_Heightmap, TSF_BGRA8);

			int32 MipSubsectionSizeQuads = SubsectionSizeQuads;
			int32 MipSizeU = HeightmapInfo.HeightmapSizeU;
			int32 MipSizeV = HeightmapInfo.HeightmapSizeV;
			while (MipSizeU > 1 && MipSizeV > 1 && MipSubsectionSizeQuads >= 1)
			{
				int32 MipIndex = HeightmapInfo.HeightmapTextureMipData.Num();
				FColor* HeightmapTextureData = (FColor*)HeightmapInfo.HeightmapTexture->Source.LockMip(MipIndex);
				FMemory::Memzero(HeightmapTextureData, MipSizeU*MipSizeV*sizeof(FColor));
				HeightmapInfo.HeightmapTextureMipData.Add(HeightmapTextureData);

				MipSizeU >>= 1;
				MipSizeV >>= 1;

				MipSubsectionSizeQuads = ((MipSubsectionSizeQuads + 1) >> 1) - 1;
			}
		}
	}

	const FVector DrawScale3D = GetRootComponent()->GetRelativeScale3D();

	// layer to import data (Final or 1st layer)
	const FGuid FinalLayerGuid = FGuid();
	const TArray<uint16>& HeightData = InImportHeightData.FindChecked(FinalLayerGuid);
	const TArray<FLandscapeImportLayerInfo>& ImportLayerInfos = InImportMaterialLayerInfos.FindChecked(FinalLayerGuid);

	// Calculate the normals for each of the two triangles per quad.
	TArray<FVector> VertexNormals;
	VertexNormals.AddZeroed(VertsX * VertsY);
	for (int32 QuadY = 0; QuadY < NumPatchesY; QuadY++)
	{
		for (int32 QuadX = 0; QuadX < NumPatchesX; QuadX++)
		{
			const FVector Vert00 = FVector(0.0f, 0.0f, ((float)HEIGHTDATA(QuadX + 0, QuadY + 0) - 32768.0f)*LANDSCAPE_ZSCALE) * DrawScale3D;
			const FVector Vert01 = FVector(0.0f, 1.0f, ((float)HEIGHTDATA(QuadX + 0, QuadY + 1) - 32768.0f)*LANDSCAPE_ZSCALE) * DrawScale3D;
			const FVector Vert10 = FVector(1.0f, 0.0f, ((float)HEIGHTDATA(QuadX + 1, QuadY + 0) - 32768.0f)*LANDSCAPE_ZSCALE) * DrawScale3D;
			const FVector Vert11 = FVector(1.0f, 1.0f, ((float)HEIGHTDATA(QuadX + 1, QuadY + 1) - 32768.0f)*LANDSCAPE_ZSCALE) * DrawScale3D;

			const FVector FaceNormal1 = ((Vert00 - Vert10) ^ (Vert10 - Vert11)).GetSafeNormal();
			const FVector FaceNormal2 = ((Vert11 - Vert01) ^ (Vert01 - Vert00)).GetSafeNormal();

			// contribute to the vertex normals.
			VertexNormals[(QuadX + 1 + VertsX * (QuadY + 0))] += FaceNormal1;
			VertexNormals[(QuadX + 0 + VertsX * (QuadY + 1))] += FaceNormal2;
			VertexNormals[(QuadX + 0 + VertsX * (QuadY + 0))] += FaceNormal1 + FaceNormal2;
			VertexNormals[(QuadX + 1 + VertsX * (QuadY + 1))] += FaceNormal1 + FaceNormal2;
		}
	}

	// Weight values for each layer for each component.
	TArray<TArray<TArray<uint8>>> ComponentWeightValues;
	ComponentWeightValues.AddZeroed(NumComponentsX * NumComponentsY);

	for (int32 ComponentY = 0; ComponentY < NumComponentsY; ComponentY++)
	{
		for (int32 ComponentX = 0; ComponentX < NumComponentsX; ComponentX++)
		{
			ULandscapeComponent* const LandscapeComponent = LandscapeComponents[ComponentX + ComponentY*NumComponentsX];
			TArray<TArray<uint8>>& WeightValues = ComponentWeightValues[ComponentX + ComponentY*NumComponentsX];

			// Import alphamap data into local array and check for unused layers for this component.
			TArray<FLandscapeComponentAlphaInfo, TInlineAllocator<16>> EditingAlphaLayerData;
			for (int32 LayerIndex = 0; LayerIndex < ImportLayerInfos.Num(); LayerIndex++)
			{
				FLandscapeComponentAlphaInfo* NewAlphaInfo = new(EditingAlphaLayerData) FLandscapeComponentAlphaInfo(LandscapeComponent, LayerIndex);

				if (ImportLayerInfos[LayerIndex].LayerData.Num())
				{
					for (int32 AlphaY = 0; AlphaY <= LandscapeComponent->ComponentSizeQuads; AlphaY++)
					{
						const uint8* const OldAlphaRowStart = &ImportLayerInfos[LayerIndex].LayerData[(AlphaY + LandscapeComponent->GetSectionBase().Y - InMinY) * VertsX + (LandscapeComponent->GetSectionBase().X - InMinX)];
						uint8* const NewAlphaRowStart = &NewAlphaInfo->AlphaValues[AlphaY * (LandscapeComponent->ComponentSizeQuads + 1)];
						FMemory::Memcpy(NewAlphaRowStart, OldAlphaRowStart, LandscapeComponent->ComponentSizeQuads + 1);
					}
				}
			}

			for (int32 AlphaMapIndex = 0; AlphaMapIndex < EditingAlphaLayerData.Num(); AlphaMapIndex++)
			{
				if (EditingAlphaLayerData[AlphaMapIndex].IsLayerAllZero())
				{
					EditingAlphaLayerData.RemoveAt(AlphaMapIndex);
					AlphaMapIndex--;
				}
			}


			UE_LOG(LogLandscape, Log, TEXT("%s needs %d alphamaps"), *LandscapeComponent->GetName(), EditingAlphaLayerData.Num());

			TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = LandscapeComponent->GetWeightmapLayerAllocations();

			// Calculate weightmap weights for this component
			WeightValues.Empty(EditingAlphaLayerData.Num());
			WeightValues.AddZeroed(EditingAlphaLayerData.Num());
			ComponentWeightmapLayerAllocations.Empty(EditingAlphaLayerData.Num());

			TArray<bool, TInlineAllocator<16>> IsNoBlendArray;
			IsNoBlendArray.Empty(EditingAlphaLayerData.Num());
			IsNoBlendArray.AddZeroed(EditingAlphaLayerData.Num());

			for (int32 WeightLayerIndex = 0; WeightLayerIndex < WeightValues.Num(); WeightLayerIndex++)
			{
				// Lookup the original layer name
				WeightValues[WeightLayerIndex] = EditingAlphaLayerData[WeightLayerIndex].AlphaValues;
				new(ComponentWeightmapLayerAllocations) FWeightmapLayerAllocationInfo(ImportLayerInfos[EditingAlphaLayerData[WeightLayerIndex].LayerIndex].LayerInfo);
				IsNoBlendArray[WeightLayerIndex] = ImportLayerInfos[EditingAlphaLayerData[WeightLayerIndex].LayerIndex].LayerInfo->bNoWeightBlend;
			}

			// Discard the temporary alpha data
			EditingAlphaLayerData.Empty();

			if (InImportMaterialLayerType == ELandscapeImportAlphamapType::Layered)
			{
				// For each layer...
				for (int32 WeightLayerIndex = WeightValues.Num() - 1; WeightLayerIndex >= 0; WeightLayerIndex--)
				{
					// ... multiply all lower layers'...
					for (int32 BelowWeightLayerIndex = WeightLayerIndex - 1; BelowWeightLayerIndex >= 0; BelowWeightLayerIndex--)
					{
						int32 TotalWeight = 0;

						if (IsNoBlendArray[BelowWeightLayerIndex])
						{
							continue; // skip no blend
						}

						// ... values by...
						for (int32 Idx = 0; Idx < WeightValues[WeightLayerIndex].Num(); Idx++)
						{
							// ... one-minus the current layer's values
							int32 NewValue = (int32)WeightValues[BelowWeightLayerIndex][Idx] * (int32)(255 - WeightValues[WeightLayerIndex][Idx]) / 255;
							WeightValues[BelowWeightLayerIndex][Idx] = (uint8)NewValue;
							TotalWeight += NewValue;
						}

						if (TotalWeight == 0)
						{
							// Remove the layer as it has no contribution
							WeightValues.RemoveAt(BelowWeightLayerIndex);
							ComponentWeightmapLayerAllocations.RemoveAt(BelowWeightLayerIndex);
							IsNoBlendArray.RemoveAt(BelowWeightLayerIndex);

							// The current layer has been re-numbered
							WeightLayerIndex--;
						}
					}
				}
			}

			// Weight normalization for total should be 255...
			if (WeightValues.Num())
			{
				for (int32 Idx = 0; Idx < WeightValues[0].Num(); Idx++)
				{
					int32 TotalWeight = 0;
					int32 MaxLayerIdx = -1;
					int32 MaxWeight = INT_MIN;

					for (int32 WeightLayerIndex = 0; WeightLayerIndex < WeightValues.Num(); WeightLayerIndex++)
					{
						if (!IsNoBlendArray[WeightLayerIndex])
						{
							int32 Weight = WeightValues[WeightLayerIndex][Idx];
							TotalWeight += Weight;
							if (MaxWeight < Weight)
							{
								MaxWeight = Weight;
								MaxLayerIdx = WeightLayerIndex;
							}
						}
					}

					if (TotalWeight > 0 && TotalWeight != 255)
					{
						// normalization...
						float Factor = 255.0f / TotalWeight;
						TotalWeight = 0;
						for (int32 WeightLayerIndex = 0; WeightLayerIndex < WeightValues.Num(); WeightLayerIndex++)
						{
							if (!IsNoBlendArray[WeightLayerIndex])
							{
								WeightValues[WeightLayerIndex][Idx] = (uint8)(Factor * WeightValues[WeightLayerIndex][Idx]);
								TotalWeight += WeightValues[WeightLayerIndex][Idx];
							}
						}

						if (255 - TotalWeight && MaxLayerIdx >= 0)
						{
							WeightValues[MaxLayerIdx][Idx] += 255 - TotalWeight;
						}
					}
				}
			}
		}
	}

	// Remember where we have spare texture channels.
	TArray<FWeightmapTextureAllocation> TextureAllocations;

	for (int32 ComponentY = 0; ComponentY < NumComponentsY; ComponentY++)
	{
		const int32 HmY = ComponentY / ComponentsPerHeightmap;
		const int32 HeightmapOffsetY = (ComponentY - ComponentsPerHeightmap*HmY) * NumSubsections * (SubsectionSizeQuads + 1);

		for (int32 ComponentX = 0; ComponentX < NumComponentsX; ComponentX++)
		{
			const int32 HmX = ComponentX / ComponentsPerHeightmap;
			const FHeightmapInfo& HeightmapInfo = HeightmapInfos[HmX + HmY * NumHeightmapsX];

			ULandscapeComponent* LandscapeComponent = LandscapeComponents[ComponentX + ComponentY*NumComponentsX];

			// Lookup array of weight values for this component.
			const TArray<TArray<uint8>>& WeightValues = ComponentWeightValues[ComponentX + ComponentY*NumComponentsX];

			// Heightmap offsets
			const int32 HeightmapOffsetX = (ComponentX - ComponentsPerHeightmap*HmX) * NumSubsections * (SubsectionSizeQuads + 1);

			LandscapeComponent->HeightmapScaleBias = FVector4(1.0f / (float)HeightmapInfo.HeightmapSizeU, 1.0f / (float)HeightmapInfo.HeightmapSizeV, (float)((HeightmapOffsetX)) / (float)HeightmapInfo.HeightmapSizeU, ((float)(HeightmapOffsetY)) / (float)HeightmapInfo.HeightmapSizeV);
			LandscapeComponent->SetHeightmap(HeightmapInfo.HeightmapTexture);

			// Weightmap is sized the same as the component
			const int32 WeightmapSize = (SubsectionSizeQuads + 1) * NumSubsections;
			// Should be power of two
			check(FMath::IsPowerOfTwo(WeightmapSize));

			LandscapeComponent->WeightmapScaleBias = FVector4(1.0f / (float)WeightmapSize, 1.0f / (float)WeightmapSize, 0.5f / (float)WeightmapSize, 0.5f / (float)WeightmapSize);
			LandscapeComponent->WeightmapSubsectionOffset = (float)(SubsectionSizeQuads + 1) / (float)WeightmapSize;

			// Pointers to the texture data where we'll store each layer. Stride is 4 (FColor)
			TArray<uint8*> WeightmapTextureDataPointers;

			UE_LOG(LogLandscape, Log, TEXT("%s needs %d weightmap channels"), *LandscapeComponent->GetName(), WeightValues.Num());

			// Find texture channels to store each layer.
			int32 LayerIndex = 0;
			while (LayerIndex < WeightValues.Num())
			{
				const int32 RemainingLayers = WeightValues.Num() - LayerIndex;

				int32 BestAllocationIndex = -1;

				// if we need less than 4 channels, try to find them somewhere to put all of them
				if (RemainingLayers < 4)
				{
					int32 BestDistSquared = MAX_int32;
					for (int32 TryAllocIdx = 0; TryAllocIdx < TextureAllocations.Num(); TryAllocIdx++)
					{
						if (TextureAllocations[TryAllocIdx].ChannelsInUse + RemainingLayers <= 4)
						{
							FWeightmapTextureAllocation& TryAllocation = TextureAllocations[TryAllocIdx];
							const int32 TryDistSquared = FMath::Square(TryAllocation.X - ComponentX) + FMath::Square(TryAllocation.Y - ComponentY);
							if (TryDistSquared < BestDistSquared)
							{
								BestDistSquared = TryDistSquared;
								BestAllocationIndex = TryAllocIdx;
							}
						}
					}
				}

				TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = LandscapeComponent->GetWeightmapLayerAllocations();
				TArray<UTexture2D*>& ComponentWeightmapTextures = LandscapeComponent->GetWeightmapTextures();
				TArray<ULandscapeWeightmapUsage*>& ComponentWeightmapTexturesUsage = LandscapeComponent->GetWeightmapTexturesUsage();

				if (BestAllocationIndex != -1)
				{
					FWeightmapTextureAllocation& Allocation = TextureAllocations[BestAllocationIndex];
					ULandscapeWeightmapUsage* WeightmapUsage = WeightmapUsageMap.FindChecked(Allocation.Texture);
					ComponentWeightmapTexturesUsage.Add(WeightmapUsage);

					UE_LOG(LogLandscape, Log, TEXT("  ==> Storing %d channels starting at %s[%d]"), RemainingLayers, *Allocation.Texture->GetName(), Allocation.ChannelsInUse);

					for (int32 i = 0; i < RemainingLayers; i++)
					{
						ComponentWeightmapLayerAllocations[LayerIndex + i].WeightmapTextureIndex = ComponentWeightmapTextures.Num();
						ComponentWeightmapLayerAllocations[LayerIndex + i].WeightmapTextureChannel = Allocation.ChannelsInUse;
						WeightmapUsage->ChannelUsage[Allocation.ChannelsInUse] = LandscapeComponent;
						switch (Allocation.ChannelsInUse)
						{
						case 1:
							WeightmapTextureDataPointers.Add((uint8*)&Allocation.TextureData->G);
							break;
						case 2:
							WeightmapTextureDataPointers.Add((uint8*)&Allocation.TextureData->B);
							break;
						case 3:
							WeightmapTextureDataPointers.Add((uint8*)&Allocation.TextureData->A);
							break;
						default:
							// this should not occur.
							check(0);

						}
						Allocation.ChannelsInUse++;
					}

					LayerIndex += RemainingLayers;
					ComponentWeightmapTextures.Add(Allocation.Texture);
				}
				else
				{
					// We couldn't find a suitable place for these layers, so lets make a new one.
					UTexture2D* const WeightmapTexture = CreateLandscapeTexture(WeightmapSize, WeightmapSize, TEXTUREGROUP_Terrain_Weightmap, TSF_BGRA8);
					FColor* const MipData = (FColor*)WeightmapTexture->Source.LockMip(0);

					const int32 ThisAllocationLayers = FMath::Min<int32>(RemainingLayers, 4);
					new(TextureAllocations) FWeightmapTextureAllocation(ComponentX, ComponentY, ThisAllocationLayers, WeightmapTexture, MipData);
					ULandscapeWeightmapUsage* WeightmapUsage = WeightmapUsageMap.Add(WeightmapTexture, CreateWeightmapUsage());
					ComponentWeightmapTexturesUsage.Add(WeightmapUsage);

					UE_LOG(LogLandscape, Log, TEXT("  ==> Storing %d channels in new texture %s"), ThisAllocationLayers, *WeightmapTexture->GetName());

					WeightmapTextureDataPointers.Add((uint8*)&MipData->R);
					ComponentWeightmapLayerAllocations[LayerIndex + 0].WeightmapTextureIndex = ComponentWeightmapTextures.Num();
					ComponentWeightmapLayerAllocations[LayerIndex + 0].WeightmapTextureChannel = 0;
					WeightmapUsage->ChannelUsage[0] = LandscapeComponent;

					if (ThisAllocationLayers > 1)
					{
						WeightmapTextureDataPointers.Add((uint8*)&MipData->G);
						ComponentWeightmapLayerAllocations[LayerIndex + 1].WeightmapTextureIndex = ComponentWeightmapTextures.Num();
						ComponentWeightmapLayerAllocations[LayerIndex + 1].WeightmapTextureChannel = 1;
						WeightmapUsage->ChannelUsage[1] = LandscapeComponent;

						if (ThisAllocationLayers > 2)
						{
							WeightmapTextureDataPointers.Add((uint8*)&MipData->B);
							ComponentWeightmapLayerAllocations[LayerIndex + 2].WeightmapTextureIndex = ComponentWeightmapTextures.Num();
							ComponentWeightmapLayerAllocations[LayerIndex + 2].WeightmapTextureChannel = 2;
							WeightmapUsage->ChannelUsage[2] = LandscapeComponent;

							if (ThisAllocationLayers > 3)
							{
								WeightmapTextureDataPointers.Add((uint8*)&MipData->A);
								ComponentWeightmapLayerAllocations[LayerIndex + 3].WeightmapTextureIndex = ComponentWeightmapTextures.Num();
								ComponentWeightmapLayerAllocations[LayerIndex + 3].WeightmapTextureChannel = 3;
								WeightmapUsage->ChannelUsage[3] = LandscapeComponent;
							}
						}
					}
					ComponentWeightmapTextures.Add(WeightmapTexture);

					LayerIndex += ThisAllocationLayers;
				}
			}
			check(WeightmapTextureDataPointers.Num() == WeightValues.Num());

			FBox LocalBox(ForceInit);
			for (int32 SubsectionY = 0; SubsectionY < NumSubsections; SubsectionY++)
			{
				for (int32 SubsectionX = 0; SubsectionX < NumSubsections; SubsectionX++)
				{
					for (int32 SubY = 0; SubY <= SubsectionSizeQuads; SubY++)
					{
						for (int32 SubX = 0; SubX <= SubsectionSizeQuads; SubX++)
						{
							// X/Y of the vertex we're looking at in component's coordinates.
							const int32 CompX = SubsectionSizeQuads * SubsectionX + SubX;
							const int32 CompY = SubsectionSizeQuads * SubsectionY + SubY;

							// X/Y of the vertex we're looking indexed into the texture data
							const int32 TexX = (SubsectionSizeQuads + 1) * SubsectionX + SubX;
							const int32 TexY = (SubsectionSizeQuads + 1) * SubsectionY + SubY;

							const int32 WeightSrcDataIdx = CompY * (ComponentSizeQuads + 1) + CompX;
							const int32 HeightTexDataIdx = (HeightmapOffsetX + TexX) + (HeightmapOffsetY + TexY) * (HeightmapInfo.HeightmapSizeU);

							const int32 WeightTexDataIdx = (TexX)+(TexY)* (WeightmapSize);

							// copy height and normal data
							const uint16 HeightValue = HEIGHTDATA(CompX + LandscapeComponent->GetSectionBase().X - InMinX, CompY + LandscapeComponent->GetSectionBase().Y - InMinY);
							const FVector Normal = VertexNormals[CompX + LandscapeComponent->GetSectionBase().X - InMinX + VertsX * (CompY + LandscapeComponent->GetSectionBase().Y - InMinY)].GetSafeNormal();

							HeightmapInfo.HeightmapTextureMipData[0][HeightTexDataIdx].R = HeightValue >> 8;
							HeightmapInfo.HeightmapTextureMipData[0][HeightTexDataIdx].G = HeightValue & 255;
							HeightmapInfo.HeightmapTextureMipData[0][HeightTexDataIdx].B = FMath::RoundToInt(127.5f * (Normal.X + 1.0f));
							HeightmapInfo.HeightmapTextureMipData[0][HeightTexDataIdx].A = FMath::RoundToInt(127.5f * (Normal.Y + 1.0f));

							for (int32 WeightmapIndex = 0; WeightmapIndex < WeightValues.Num(); WeightmapIndex++)
							{
								WeightmapTextureDataPointers[WeightmapIndex][WeightTexDataIdx * 4] = WeightValues[WeightmapIndex][WeightSrcDataIdx];
							}

							// Get local space verts
							const FVector LocalVertex(CompX, CompY, LandscapeDataAccess::GetLocalHeight(HeightValue));
							LocalBox += LocalVertex;
						}
					}
				}
			}

			LandscapeComponent->CachedLocalBox = LocalBox;
		}
	}

	TArray<UTexture2D*> PendingTexturePlatformDataCreation;

	// Unlock the weightmaps' base mips
	for (int32 AllocationIndex = 0; AllocationIndex < TextureAllocations.Num(); AllocationIndex++)
	{
		UTexture2D* const WeightmapTexture = TextureAllocations[AllocationIndex].Texture;
		FColor* const BaseMipData = TextureAllocations[AllocationIndex].TextureData;

		// Generate mips for weightmaps
		ULandscapeComponent::GenerateWeightmapMips(NumSubsections, SubsectionSizeQuads, WeightmapTexture, BaseMipData);

		WeightmapTexture->Source.UnlockMip(0);

		WeightmapTexture->BeginCachePlatformData();
		WeightmapTexture->ClearAllCachedCookedPlatformData();
		PendingTexturePlatformDataCreation.Add(WeightmapTexture);
	}

	// Generate mipmaps for the components, and create the collision components
	for (int32 ComponentY = 0; ComponentY < NumComponentsY; ComponentY++)
	{
		for (int32 ComponentX = 0; ComponentX < NumComponentsX; ComponentX++)
		{
			const int32 HmX = ComponentX / ComponentsPerHeightmap;
			const int32 HmY = ComponentY / ComponentsPerHeightmap;
			FHeightmapInfo& HeightmapInfo = HeightmapInfos[HmX + HmY * NumHeightmapsX];

			ULandscapeComponent* LandscapeComponent = LandscapeComponents[ComponentX + ComponentY*NumComponentsX];
			LandscapeComponent->GenerateHeightmapMips(HeightmapInfo.HeightmapTextureMipData, ComponentX == NumComponentsX - 1 ? MAX_int32 : 0, ComponentY == NumComponentsY - 1 ? MAX_int32 : 0);
			LandscapeComponent->UpdateCollisionHeightData(
				HeightmapInfo.HeightmapTextureMipData[LandscapeComponent->CollisionMipLevel],
				LandscapeComponent->SimpleCollisionMipLevel > LandscapeComponent->CollisionMipLevel ? HeightmapInfo.HeightmapTextureMipData[LandscapeComponent->SimpleCollisionMipLevel] : nullptr);
			LandscapeComponent->UpdateCollisionLayerData();
		}
	}

	for (int32 HmIdx = 0; HmIdx < HeightmapInfos.Num(); HmIdx++)
	{
		FHeightmapInfo& HeightmapInfo = HeightmapInfos[HmIdx];

		// Add remaining mips down to 1x1 to heightmap texture. These do not represent quads and are just a simple averages of the previous mipmaps. 
		// These mips are not used for sampling in the vertex shader but could be sampled in the pixel shader.
		int32 Mip = HeightmapInfo.HeightmapTextureMipData.Num();
		int32 MipSizeU = (HeightmapInfo.HeightmapTexture->Source.GetSizeX()) >> Mip;
		int32 MipSizeV = (HeightmapInfo.HeightmapTexture->Source.GetSizeY()) >> Mip;
		while (MipSizeU > 1 && MipSizeV > 1)
		{
			HeightmapInfo.HeightmapTextureMipData.Add((FColor*)HeightmapInfo.HeightmapTexture->Source.LockMip(Mip));
			const int32 PrevMipSizeU = (HeightmapInfo.HeightmapTexture->Source.GetSizeX()) >> (Mip - 1);
			const int32 PrevMipSizeV = (HeightmapInfo.HeightmapTexture->Source.GetSizeY()) >> (Mip - 1);

			for (int32 Y = 0; Y < MipSizeV; Y++)
			{
				for (int32 X = 0; X < MipSizeU; X++)
				{
					FColor* const TexData = &(HeightmapInfo.HeightmapTextureMipData[Mip])[X + Y * MipSizeU];

					const FColor* const PreMipTexData00 = &(HeightmapInfo.HeightmapTextureMipData[Mip - 1])[(X * 2 + 0) + (Y * 2 + 0) * PrevMipSizeU];
					const FColor* const PreMipTexData01 = &(HeightmapInfo.HeightmapTextureMipData[Mip - 1])[(X * 2 + 0) + (Y * 2 + 1) * PrevMipSizeU];
					const FColor* const PreMipTexData10 = &(HeightmapInfo.HeightmapTextureMipData[Mip - 1])[(X * 2 + 1) + (Y * 2 + 0) * PrevMipSizeU];
					const FColor* const PreMipTexData11 = &(HeightmapInfo.HeightmapTextureMipData[Mip - 1])[(X * 2 + 1) + (Y * 2 + 1) * PrevMipSizeU];

					TexData->R = (((int32)PreMipTexData00->R + (int32)PreMipTexData01->R + (int32)PreMipTexData10->R + (int32)PreMipTexData11->R) >> 2);
					TexData->G = (((int32)PreMipTexData00->G + (int32)PreMipTexData01->G + (int32)PreMipTexData10->G + (int32)PreMipTexData11->G) >> 2);
					TexData->B = (((int32)PreMipTexData00->B + (int32)PreMipTexData01->B + (int32)PreMipTexData10->B + (int32)PreMipTexData11->B) >> 2);
					TexData->A = (((int32)PreMipTexData00->A + (int32)PreMipTexData01->A + (int32)PreMipTexData10->A + (int32)PreMipTexData11->A) >> 2);
				}
			}
			Mip++;
			MipSizeU >>= 1;
			MipSizeV >>= 1;
		}

		for (int32 i = 0; i < HeightmapInfo.HeightmapTextureMipData.Num(); i++)
		{
			HeightmapInfo.HeightmapTexture->Source.UnlockMip(i);
		}

		HeightmapInfo.HeightmapTexture->BeginCachePlatformData();
		HeightmapInfo.HeightmapTexture->ClearAllCachedCookedPlatformData();
		PendingTexturePlatformDataCreation.Add(HeightmapInfo.HeightmapTexture);
	}

	// Build a list of all unique materials the landscape uses
	TArray<UMaterialInterface*> LandscapeMaterials;

	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		int8 MaxLOD = FMath::CeilLogTwo(Component->SubsectionSizeQuads + 1) - 1;

		for (int8 LODIndex = 0; LODIndex < MaxLOD; ++LODIndex)
		{
			UMaterialInterface* Material = Component->GetLandscapeMaterial(LODIndex);
			LandscapeMaterials.AddUnique(Material);
		}
	}

	// Update all materials and recreate render state of all landscape components
	TArray<FComponentRecreateRenderStateContext> RecreateRenderStateContexts;

	{
		// We disable automatic material update context, to manage it manually
		GDisableAutomaticTextureMaterialUpdateDependencies = true;
	
		FMaterialUpdateContext UpdateContext(FMaterialUpdateContext::EOptions::Default & ~FMaterialUpdateContext::EOptions::RecreateRenderStates);

		for (UTexture2D* Texture : PendingTexturePlatformDataCreation)
		{
			Texture->FinishCachePlatformData();
			Texture->PostEditChange();
			
			TSet<UMaterial*> BaseMaterialsThatUseThisTexture;

			for (UMaterialInterface* MaterialInterface : LandscapeMaterials)
			{
				if (DoesMaterialUseTexture(MaterialInterface, Texture))
				{
					UMaterial* Material = MaterialInterface->GetMaterial();
					bool MaterialAlreadyCompute = false;
					BaseMaterialsThatUseThisTexture.Add(Material, &MaterialAlreadyCompute);

					if (!MaterialAlreadyCompute)
					{
						if (Material->IsTextureForceRecompileCacheRessource(Texture))
						{
							UpdateContext.AddMaterial(Material);
							Material->UpdateMaterialShaderCacheAndTextureReferences();
						}
					}
				}
			}
		}
		
		GDisableAutomaticTextureMaterialUpdateDependencies = false;

		// Update MaterialInstances (must be done after textures are fully initialized)
		UpdateAllComponentMaterialInstances(UpdateContext, RecreateRenderStateContexts);
	}

	// Recreate the render state for this component, needed to update the static drawlist which has cached the MaterialRenderProxies
	// Must be after the FMaterialUpdateContext is destroyed
	RecreateRenderStateContexts.Reset();

	// Create and initialize landscape info object
	ULandscapeInfo* LandscapeInfo = CreateLandscapeInfo();

	if (CanHaveLayersContent())
	{
		// Create the default layer first
		ALandscape* LandscapeActor = GetLandscapeActor();
		check(LandscapeActor != nullptr);
		if (LandscapeActor->GetLayerCount() == 0 && InImportLayers == nullptr)
		{
			LandscapeActor->CreateDefaultLayer();
		}

		// Components need to be registered to be able to import the layer content and we will remove them if they should have not been visible
		bool ShouldComponentBeRegistered = GetLevel()->bIsVisible;
		RegisterAllComponents();

		TSet<ULandscapeComponent*> ComponentsToProcess;

		struct FLayerImportSettings
		{
			FGuid SourceLayerGuid;
			FGuid DestinationLayerGuid;
		};

		TArray<FLayerImportSettings> LayerImportSettings;		

		// Only create Layers on main Landscape
		if (LandscapeActor == this && InImportLayers != nullptr)
		{
			for (const FLandscapeLayer& OldLayer : *InImportLayers)
			{
				FLandscapeLayer* NewLayer = LandscapeActor->DuplicateLayerAndMoveBrushes(OldLayer);
				check(NewLayer != nullptr);

				FLayerImportSettings ImportSettings;
				ImportSettings.SourceLayerGuid = OldLayer.Guid;
				ImportSettings.DestinationLayerGuid = NewLayer->Guid;
				LayerImportSettings.Add(ImportSettings);
			}

			LandscapeInfo->GetComponentsInRegion(InMinX, InMinY, InMaxX, InMaxY, ComponentsToProcess);
		}
		else
		{
			// In the case of a streaming proxy, we will generate the layer data for each components that the proxy hold so no need of the grid min/max to calculate the components to update
			if (LandscapeActor != this)
			{
				LandscapeActor->AddLayersToProxy(this);
			}

			// And we will fill all the landscape components with the provided final layer content put into the default layer (aka layer index 0)
			const FLandscapeLayer* DefaultLayer = LandscapeActor->GetLayer(0);
			check(DefaultLayer != nullptr);

			FLayerImportSettings ImportSettings;
			ImportSettings.SourceLayerGuid = FinalLayerGuid;
			ImportSettings.DestinationLayerGuid = DefaultLayer->Guid;
			LayerImportSettings.Add(ImportSettings);

			ComponentsToProcess.Append(LandscapeComponents);
		}

		check(LayerImportSettings.Num() != 0);
		// Currently only supports reimporting heightmap data into a single edit layer, which will always be the default layer
		ReimportDestinationLayerGuid = LayerImportSettings[0].DestinationLayerGuid;

		TSet<UTexture2D*> LayersTextures;

		for (const FLayerImportSettings& ImportSettings : LayerImportSettings)
		{
			FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo, false);
			FScopedSetLandscapeEditingLayer Scope(LandscapeActor, ImportSettings.DestinationLayerGuid);

			const TArray<uint16>* ImportHeightData = InImportHeightData.Find(ImportSettings.SourceLayerGuid);

			if (ImportHeightData != nullptr)
			{
				LandscapeEdit.SetHeightData(InMinX, InMinY, InMaxX, InMaxY, (uint16*)ImportHeightData->GetData(), 0, false, nullptr);
			}

			const TArray<FLandscapeImportLayerInfo>* ImportWeightData = InImportMaterialLayerInfos.Find(ImportSettings.SourceLayerGuid);

			if (ImportWeightData != nullptr)
			{
				for (const FLandscapeImportLayerInfo& MaterialLayerInfo : *ImportWeightData)
				{
					if (MaterialLayerInfo.LayerInfo != nullptr && MaterialLayerInfo.LayerData.Num() > 0)
					{
						LandscapeEdit.SetAlphaData(MaterialLayerInfo.LayerInfo, InMinX, InMinY, InMaxX, InMaxY, MaterialLayerInfo.LayerData.GetData(), 0, ELandscapeLayerPaintingRestriction::None, true, false);
					}
				}
			}

			for (ULandscapeComponent* Component : ComponentsToProcess)
			{
				FLandscapeLayerComponentData* ComponentLayerData = Component->GetLayerData(ImportSettings.DestinationLayerGuid);
				check(ComponentLayerData != nullptr);

				LayersTextures.Add(ComponentLayerData->HeightmapData.Texture);
				LayersTextures.Append(ComponentLayerData->WeightmapData.Textures);
			}
		}

		// Retrigger a caching of the platform data as we wrote again in the textures
		for (UTexture2D* Texture : LayersTextures)
		{
			Texture->ClearAllCachedCookedPlatformData();
			Texture->BeginCachePlatformData();
		}

		LandscapeActor->RequestLayersContentUpdateForceAll();

		if (!ShouldComponentBeRegistered)
		{
			UnregisterAllComponents();
		}
	}	
	else
	{
		if (GetLevel()->bIsVisible)
		{
			ReregisterAllComponents();
		}

		ReimportDestinationLayerGuid = FGuid();
		LandscapeInfo->RecreateCollisionComponents();
		LandscapeInfo->UpdateAllAddCollisions();
	}

	ReimportHeightmapFilePath = InHeightmapFileName;

	LandscapeInfo->UpdateLayerInfoMap();

	GWarn->EndSlowTask();
}

bool ALandscapeProxy::ExportToRawMesh(int32 InExportLOD, FMeshDescription& OutRawMesh) const
{
	FBoxSphereBounds GarbageBounds;
	return ExportToRawMesh(InExportLOD, OutRawMesh, GarbageBounds, true);
}

bool ALandscapeProxy::ExportToRawMesh(int32 InExportLOD, FMeshDescription& OutRawMesh, const FBoxSphereBounds& InBounds, bool bIgnoreBounds /*= false*/) const
{
	TInlineComponentArray<ULandscapeComponent*> RegisteredLandscapeComponents;
	GetComponents<ULandscapeComponent>(RegisteredLandscapeComponents);

	const FIntRect LandscapeSectionRect = GetBoundingRect();
	const FVector2D LandscapeUVScale = FVector2D(1.0f, 1.0f) / FVector2D(LandscapeSectionRect.Size());

	TVertexAttributesRef<FVector> VertexPositions = OutRawMesh.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
	TEdgeAttributesRef<bool> EdgeHardnesses = OutRawMesh.EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard);
	TEdgeAttributesRef<float> EdgeCreaseSharpnesses = OutRawMesh.EdgeAttributes().GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness);
	TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = OutRawMesh.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
	TVertexInstanceAttributesRef<FVector> VertexInstanceNormals = OutRawMesh.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	TVertexInstanceAttributesRef<FVector> VertexInstanceTangents = OutRawMesh.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
	TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = OutRawMesh.VertexInstanceAttributes().GetAttributesRef<float>(MeshAttribute::VertexInstance::BinormalSign);
	TVertexInstanceAttributesRef<FVector4> VertexInstanceColors = OutRawMesh.VertexInstanceAttributes().GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color);
	TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = OutRawMesh.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);

	if (VertexInstanceUVs.GetNumIndices() < 2)
	{
		VertexInstanceUVs.SetNumIndices(2);
	}

	// User specified LOD to export
	int32 LandscapeLODToExport = ExportLOD;
	if (InExportLOD != INDEX_NONE)
	{
		LandscapeLODToExport = FMath::Clamp<int32>(InExportLOD, 0, FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1);
	}

	// Export data for each component
	for (auto It = RegisteredLandscapeComponents.CreateConstIterator(); It; ++It)
	{
		ULandscapeComponent* Component = (*It);

		// Early out if the Landscape bounds and given bounds do not overlap at all
		if (!bIgnoreBounds && !FBoxSphereBounds::SpheresIntersect(Component->Bounds, InBounds))
		{
			continue;
		}

		FLandscapeComponentDataInterface CDI(Component, LandscapeLODToExport);
		const int32 ComponentSizeQuadsLOD = ((Component->ComponentSizeQuads + 1) >> LandscapeLODToExport) - 1;
		const int32 SubsectionSizeQuadsLOD = ((Component->SubsectionSizeQuads + 1) >> LandscapeLODToExport) - 1;
		const FIntPoint ComponentOffsetQuads = Component->GetSectionBase() - LandscapeSectionOffset - LandscapeSectionRect.Min;
		const FVector2D ComponentUVOffsetLOD = FVector2D(ComponentOffsetQuads)*((float)ComponentSizeQuadsLOD / ComponentSizeQuads);
		const FVector2D ComponentUVScaleLOD = LandscapeUVScale*((float)ComponentSizeQuads / ComponentSizeQuadsLOD);

		const int32 NumFaces = FMath::Square(ComponentSizeQuadsLOD) * 2;
		const int32 NumVertices = NumFaces * 3;

		OutRawMesh.ReserveNewVertices(NumVertices);
		OutRawMesh.ReserveNewPolygons(NumFaces);
		OutRawMesh.ReserveNewVertexInstances(NumVertices);
		OutRawMesh.ReserveNewEdges(NumVertices);

		FPolygonGroupID PolygonGroupID = FPolygonGroupID::Invalid;
		if (OutRawMesh.PolygonGroups().Num() < 1)
		{
			PolygonGroupID = OutRawMesh.CreatePolygonGroup();
			PolygonGroupImportedMaterialSlotNames[PolygonGroupID] = FName(TEXT("LandscapeMat_0"));
		}
		else
		{
			PolygonGroupID = OutRawMesh.PolygonGroups().GetFirstValidID();
		}

		// Check if there are any holes
		const int32 VisThreshold = 170;
		TArray<uint8> VisDataMap;
		TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = Component->GetWeightmapLayerAllocations();

		for (int32 AllocIdx = 0; AllocIdx < ComponentWeightmapLayerAllocations.Num(); AllocIdx++)
		{
			FWeightmapLayerAllocationInfo& AllocInfo = ComponentWeightmapLayerAllocations[AllocIdx];
			if (AllocInfo.LayerInfo == ALandscapeProxy::VisibilityLayer)
			{
				CDI.GetWeightmapTextureData(AllocInfo.LayerInfo, VisDataMap);
			}
		}

		const FIntPoint QuadPattern[6] =
		{
			//face 1
			FIntPoint(0, 0),
			FIntPoint(0, 1),
			FIntPoint(1, 1),
			//face 2
			FIntPoint(0, 0),
			FIntPoint(1, 1),
			FIntPoint(1, 0),
		};

		const int32 WeightMapSize = (SubsectionSizeQuadsLOD + 1) * Component->NumSubsections;

		const float SquaredSphereRadius = FMath::Square(InBounds.SphereRadius);

		//We need to not duplicate the vertex position, so we use the FIndexAndZ to achieve fast result
		TArray<FIndexAndZ> VertIndexAndZ;
		VertIndexAndZ.Reserve(ComponentSizeQuadsLOD*ComponentSizeQuadsLOD*UE_ARRAY_COUNT(QuadPattern));
		int32 CurrentIndex = 0;
		TMap<int32, FVector> IndexToPosition;
		IndexToPosition.Reserve(ComponentSizeQuadsLOD*ComponentSizeQuadsLOD*UE_ARRAY_COUNT(QuadPattern));
		for (int32 y = 0; y < ComponentSizeQuadsLOD; y++)
		{
			for (int32 x = 0; x < ComponentSizeQuadsLOD; x++)
			{
				for (int32 i = 0; i < UE_ARRAY_COUNT(QuadPattern); i++)
				{
					int32 VertexX = x + QuadPattern[i].X;
					int32 VertexY = y + QuadPattern[i].Y;
					FVector Position = CDI.GetWorldVertex(VertexX, VertexY);

					// If at least one vertex is within the given bounds we should process the quad  
					new(VertIndexAndZ)FIndexAndZ(CurrentIndex, Position);
					IndexToPosition.Add(CurrentIndex, Position);
					CurrentIndex++;
				}
			}
		}
		// Sort the vertices by z value
		VertIndexAndZ.Sort(FCompareIndexAndZ());

		auto FindPreviousIndex = [&VertIndexAndZ, &IndexToPosition](int32 Index)->int32
		{
			const FVector& PositionA = IndexToPosition[Index];
			FIndexAndZ CompressPosition(0, PositionA);
			// Search for lowest index duplicates
			int32 BestIndex = MAX_int32;
			for (int32 i = 0; i < IndexToPosition.Num(); i++)
			{
				if (CompressPosition.Z > (VertIndexAndZ[i].Z + SMALL_NUMBER))
				{
					//We will not find anything there is no point searching more
					break;
				}
				const FVector& PositionB = IndexToPosition[VertIndexAndZ[i].Index];
				if (PointsEqual(PositionA, PositionB, SMALL_NUMBER))
				{
					if (VertIndexAndZ[i].Index < BestIndex)
					{
						BestIndex = VertIndexAndZ[i].Index;
					}
				}
			}
			return BestIndex < MAX_int32 ? BestIndex : Index;
		};

		// Export to MeshDescription
		TMap<int32, FVertexID> IndexToVertexID;
		IndexToVertexID.Reserve(CurrentIndex);
		CurrentIndex = 0;
		for (int32 y = 0; y < ComponentSizeQuadsLOD; y++)
		{
			for (int32 x = 0; x < ComponentSizeQuadsLOD; x++)
			{
				FVector Positions[UE_ARRAY_COUNT(QuadPattern)];
				bool bProcess = bIgnoreBounds;

				// Fill positions
				for (int32 i = 0; i < UE_ARRAY_COUNT(QuadPattern); i++)
				{
					int32 VertexX = x + QuadPattern[i].X;
					int32 VertexY = y + QuadPattern[i].Y;
					Positions[i] = CDI.GetWorldVertex(VertexX, VertexY);

					// If at least one vertex is within the given bounds we should process the quad  
					if (!bProcess && InBounds.ComputeSquaredDistanceFromBoxToPoint(Positions[i]) < SquaredSphereRadius)
					{
						bProcess = true;
					}
				}

				if (bProcess)
				{
					//Fill the vertexID we need
					TArray<FVertexID> VertexIDs;
					VertexIDs.Reserve(UE_ARRAY_COUNT(QuadPattern));
					TArray<FVertexInstanceID> VertexInstanceIDs;
					VertexInstanceIDs.Reserve(UE_ARRAY_COUNT(QuadPattern));
					// Fill positions
					for (int32 i = 0; i < UE_ARRAY_COUNT(QuadPattern); i++)
					{
						int32 DuplicateLowestIndex = FindPreviousIndex(CurrentIndex);
						FVertexID VertexID;
						if (DuplicateLowestIndex < CurrentIndex)
						{
							VertexID = IndexToVertexID[DuplicateLowestIndex];
						}
						else
						{
							VertexID = OutRawMesh.CreateVertex();
							VertexPositions[VertexID] = Positions[i];
						}
						IndexToVertexID.Add(CurrentIndex, VertexID);
						VertexIDs.Add(VertexID);
						CurrentIndex++;
					}

					// Create triangle
					{
						// Whether this vertex is in hole
						bool bInvisible = false;
						if (VisDataMap.Num())
						{
							int32 TexelX, TexelY;
							CDI.VertexXYToTexelXY(x, y, TexelX, TexelY);
							bInvisible = (VisDataMap[CDI.TexelXYToIndex(TexelX, TexelY)] >= VisThreshold);
						}
						//Add vertexInstance and polygon only if we are visible
						if (!bInvisible)
						{
							VertexInstanceIDs.Add(OutRawMesh.CreateVertexInstance(VertexIDs[0]));
							VertexInstanceIDs.Add(OutRawMesh.CreateVertexInstance(VertexIDs[1]));
							VertexInstanceIDs.Add(OutRawMesh.CreateVertexInstance(VertexIDs[2]));

							VertexInstanceIDs.Add(OutRawMesh.CreateVertexInstance(VertexIDs[3]));
							VertexInstanceIDs.Add(OutRawMesh.CreateVertexInstance(VertexIDs[4]));
							VertexInstanceIDs.Add(OutRawMesh.CreateVertexInstance(VertexIDs[5]));

							// Fill other vertex data
							for (int32 i = 0; i < UE_ARRAY_COUNT(QuadPattern); i++)
							{
								int32 VertexX = x + QuadPattern[i].X;
								int32 VertexY = y + QuadPattern[i].Y;

								FVector LocalTangentX, LocalTangentY, LocalTangentZ;
								CDI.GetLocalTangentVectors(VertexX, VertexY, LocalTangentX, LocalTangentY, LocalTangentZ);

								VertexInstanceTangents[VertexInstanceIDs[i]] = LocalTangentX;
								VertexInstanceBinormalSigns[VertexInstanceIDs[i]] = GetBasisDeterminantSign(LocalTangentX, LocalTangentY, LocalTangentZ);
								VertexInstanceNormals[VertexInstanceIDs[i]] = LocalTangentZ;

								FVector2D UV = (ComponentUVOffsetLOD + FVector2D(VertexX, VertexY))*ComponentUVScaleLOD;
								VertexInstanceUVs.Set(VertexInstanceIDs[i], 0, UV);
								// Add lightmap UVs
								VertexInstanceUVs.Set(VertexInstanceIDs[i], 1, UV);
							}
							auto AddTriangle = [&OutRawMesh, &EdgeHardnesses, &EdgeCreaseSharpnesses, &PolygonGroupID, &VertexIDs, &VertexInstanceIDs](int32 BaseIndex)
							{
								//Create a polygon from this triangle
								TArray<FVertexInstanceID> PerimeterVertexInstances;
								PerimeterVertexInstances.SetNum(3);
								for (int32 Corner = 0; Corner < 3; ++Corner)
								{
									PerimeterVertexInstances[Corner] = VertexInstanceIDs[BaseIndex + Corner];
								}
								// Insert a polygon into the mesh
								TArray<FEdgeID> NewEdgeIDs;
								const FPolygonID NewPolygonID = OutRawMesh.CreatePolygon(PolygonGroupID, PerimeterVertexInstances, &NewEdgeIDs);
								for (const FEdgeID& NewEdgeID : NewEdgeIDs)
								{
									EdgeHardnesses[NewEdgeID] = false;
									EdgeCreaseSharpnesses[NewEdgeID] = 0.0f;
								}
							};
							AddTriangle(0);
							AddTriangle(3);
						}
					}
				}
				else
				{
					CurrentIndex += UE_ARRAY_COUNT(QuadPattern);
				}
			}
		}
	}

	//Compact the MeshDescription, if there was visibility mask or some bounding box clip, it need to be compacted so the sparse array are from 0 to n with no invalid data in between. 
	FElementIDRemappings ElementIDRemappings;
	OutRawMesh.Compact(ElementIDRemappings);
	return OutRawMesh.Polygons().Num() > 0;
}


FIntRect ALandscapeProxy::GetBoundingRect() const
{
	if (LandscapeComponents.Num() > 0)
	{
		FIntRect Rect(MAX_int32, MAX_int32, MIN_int32, MIN_int32);
		for (int32 CompIdx = 0; CompIdx < LandscapeComponents.Num(); CompIdx++)
		{
			Rect.Include(LandscapeComponents[CompIdx]->GetSectionBase());
		}
		Rect.Max += FIntPoint(ComponentSizeQuads, ComponentSizeQuads);
		Rect -= LandscapeSectionOffset;
		return Rect;
	}

	return FIntRect();
}

bool ALandscape::HasAllComponent()
{
	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info && Info->XYtoComponentMap.Num() == LandscapeComponents.Num())
	{
		// all components are owned by this Landscape actor (no Landscape Proxies)
		return true;
	}
	return false;
}

bool ULandscapeInfo::GetLandscapeExtent(int32& MinX, int32& MinY, int32& MaxX, int32& MaxY) const
{
	MinX = MAX_int32;
	MinY = MAX_int32;
	MaxX = MIN_int32;
	MaxY = MIN_int32;

	// Find range of entire landscape
	for (auto& XYComponentPair : XYtoComponentMap)
	{
		const ULandscapeComponent* Comp = XYComponentPair.Value;
		Comp->GetComponentExtent(MinX, MinY, MaxX, MaxY);
	}
	return (MinX != MAX_int32);
}

LANDSCAPE_API void ULandscapeInfo::ForAllLandscapeComponents(TFunctionRef<void(ULandscapeComponent*)> Fn) const
{
	ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
		for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
		{
			Fn(Component);
		}
	});
}

bool ULandscapeInfo::GetSelectedExtent(int32& MinX, int32& MinY, int32& MaxX, int32& MaxY) const
{
	MinX = MinY = MAX_int32;
	MaxX = MaxY = MIN_int32;
	for (auto& SelectedPointPair : SelectedRegion)
	{
		const FIntPoint Key = SelectedPointPair.Key;
		if (MinX > Key.X) MinX = Key.X;
		if (MaxX < Key.X) MaxX = Key.X;
		if (MinY > Key.Y) MinY = Key.Y;
		if (MaxY < Key.Y) MaxY = Key.Y;
	}
	if (MinX != MAX_int32)
	{
		return true;
	}
	// if SelectedRegion is empty, try SelectedComponents
	for (const ULandscapeComponent* Comp : SelectedComponents)
	{
		Comp->GetComponentExtent(MinX, MinY, MaxX, MaxY);
	}
	return MinX != MAX_int32;
}

FVector ULandscapeInfo::GetLandscapeCenterPos(float& LengthZ, int32 MinX /*= MAX_INT*/, int32 MinY /*= MAX_INT*/, int32 MaxX /*= MIN_INT*/, int32 MaxY /*= MIN_INT*/)
{
	// MinZ, MaxZ is Local coordinate
	float MaxZ = -HALF_WORLD_MAX, MinZ = HALF_WORLD_MAX;
	const float ScaleZ = DrawScale.Z;

	if (MinX == MAX_int32)
	{
		// Find range of entire landscape
		for (auto It = XYtoComponentMap.CreateIterator(); It; ++It)
		{
			ULandscapeComponent* Comp = It.Value();
			Comp->GetComponentExtent(MinX, MinY, MaxX, MaxY);
		}

		const int32 Dist = (ComponentSizeQuads + 1) >> 1; // Should be same in ALandscapeGizmoActiveActor::SetTargetLandscape
		FVector2D MidPoint(((float)(MinX + MaxX)) / 2.0f, ((float)(MinY + MaxY)) / 2.0f);
		MinX = FMath::FloorToInt(MidPoint.X) - Dist;
		MaxX = FMath::CeilToInt(MidPoint.X) + Dist;
		MinY = FMath::FloorToInt(MidPoint.Y) - Dist;
		MaxY = FMath::CeilToInt(MidPoint.Y) + Dist;
		check(MidPoint.X == ((float)(MinX + MaxX)) / 2.0f && MidPoint.Y == ((float)(MinY + MaxY)) / 2.0f);
	}

	check(MinX != MAX_int32);
	//if (MinX != MAX_int32)
	{
		int32 CompX1, CompX2, CompY1, CompY2;
		ALandscape::CalcComponentIndicesOverlap(MinX, MinY, MaxX, MaxY, ComponentSizeQuads, CompX1, CompY1, CompX2, CompY2);
		for (int32 IndexY = CompY1; IndexY <= CompY2; ++IndexY)
		{
			for (int32 IndexX = CompX1; IndexX <= CompX2; ++IndexX)
			{
				ULandscapeComponent* Comp = XYtoComponentMap.FindRef(FIntPoint(IndexX, IndexY));
				if (Comp)
				{
					ULandscapeHeightfieldCollisionComponent* CollisionComp = Comp->CollisionComponent.Get();
					if (CollisionComp)
					{
						uint16* Heights = (uint16*)CollisionComp->CollisionHeightData.Lock(LOCK_READ_ONLY);
						int32 CollisionSizeVerts = CollisionComp->CollisionSizeQuads + 1;

						int32 StartX = FMath::Max(0, MinX - CollisionComp->GetSectionBase().X);
						int32 StartY = FMath::Max(0, MinY - CollisionComp->GetSectionBase().Y);
						int32 EndX = FMath::Min(CollisionSizeVerts, MaxX - CollisionComp->GetSectionBase().X + 1);
						int32 EndY = FMath::Min(CollisionSizeVerts, MaxY - CollisionComp->GetSectionBase().Y + 1);

						for (int32 Y = StartY; Y < EndY; ++Y)
						{
							for (int32 X = StartX; X < EndX; ++X)
							{
								float Height = LandscapeDataAccess::GetLocalHeight(Heights[X + Y*CollisionSizeVerts]);
								MaxZ = FMath::Max(Height, MaxZ);
								MinZ = FMath::Min(Height, MinZ);
							}
						}
						CollisionComp->CollisionHeightData.Unlock();
					}
				}
			}
		}
	}

	const float MarginZ = 3;
	if (MaxZ < MinZ)
	{
		MaxZ = +MarginZ;
		MinZ = -MarginZ;
	}
	LengthZ = (MaxZ - MinZ + 2 * MarginZ) * ScaleZ;

	const FVector LocalPosition(((float)(MinX + MaxX)) / 2.0f, ((float)(MinY + MaxY)) / 2.0f, MinZ - MarginZ);
	return GetLandscapeProxy()->LandscapeActorToWorld().TransformPosition(LocalPosition);
}

bool ULandscapeInfo::IsValidPosition(int32 X, int32 Y)
{
	int32 CompX1, CompX2, CompY1, CompY2;
	ALandscape::CalcComponentIndicesOverlap(X, Y, X, Y, ComponentSizeQuads, CompX1, CompY1, CompX2, CompY2);
	if (XYtoComponentMap.FindRef(FIntPoint(CompX1, CompY1)))
	{
		return true;
	}
	if (XYtoComponentMap.FindRef(FIntPoint(CompX2, CompY2)))
	{
		return true;
	}
	return false;
}

void ULandscapeInfo::ExportHeightmap(const FString& Filename)
{
	int32 MinX = MAX_int32;
	int32 MinY = MAX_int32;
	int32 MaxX = -MAX_int32;
	int32 MaxY = -MAX_int32;

	if (!GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		return;
	}

	GWarn->BeginSlowTask(LOCTEXT("BeginExportingLandscapeHeightmapTask", "Exporting Landscape Heightmap"), true);

	ILandscapeEditorModule& LandscapeEditorModule = FModuleManager::GetModuleChecked<ILandscapeEditorModule>("LandscapeEditor");
	FLandscapeEditDataInterface LandscapeEdit(this);

	TArray<uint16> HeightData;
	HeightData.AddZeroed((MaxX - MinX + 1) * (MaxY - MinY + 1));
	LandscapeEdit.GetHeightDataFast(MinX, MinY, MaxX, MaxY, HeightData.GetData(), 0);

	const ILandscapeHeightmapFileFormat* HeightmapFormat = LandscapeEditorModule.GetHeightmapFormatByExtension(*FPaths::GetExtension(Filename, true));
	if (HeightmapFormat)
	{
		HeightmapFormat->Export(*Filename, HeightData, {(uint32)(MaxX - MinX + 1), (uint32)(MaxY - MinY + 1)}, DrawScale * FVector(1, 1, LANDSCAPE_ZSCALE));
	}

	GWarn->EndSlowTask();
}

void ULandscapeInfo::ExportLayer(ULandscapeLayerInfoObject* LayerInfo, const FString& Filename)
{
	check(LayerInfo);

	int32 MinX = MAX_int32;
	int32 MinY = MAX_int32;
	int32 MaxX = -MAX_int32;
	int32 MaxY = -MAX_int32;

	if (!GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		return;
	}

	GWarn->BeginSlowTask(LOCTEXT("BeginExportingLandscapeWeightmapTask", "Exporting Landscape Layer Weightmap"), true);

	ILandscapeEditorModule& LandscapeEditorModule = FModuleManager::GetModuleChecked<ILandscapeEditorModule>("LandscapeEditor");

	TArray<uint8> WeightData;
	WeightData.AddZeroed((MaxX - MinX + 1) * (MaxY - MinY + 1));

	FLandscapeEditDataInterface LandscapeEdit(this);
	LandscapeEdit.GetWeightDataFast(LayerInfo, MinX, MinY, MaxX, MaxY, WeightData.GetData(), 0);

	const ILandscapeWeightmapFileFormat* WeightmapFormat = LandscapeEditorModule.GetWeightmapFormatByExtension(*FPaths::GetExtension(Filename, true));
	if (WeightmapFormat)
	{
		WeightmapFormat->Export(*Filename, LayerInfo->LayerName, WeightData, {(uint32)(MaxX - MinX + 1), (uint32)(MaxY - MinY + 1)});
	}

	GWarn->EndSlowTask();
}

void ULandscapeInfo::DeleteLayer(ULandscapeLayerInfoObject* LayerInfo, const FName& LayerName)
{
	GWarn->BeginSlowTask(LOCTEXT("BeginDeletingLayerTask", "Deleting Layer"), true);

	// Remove data from all components
	FLandscapeEditDataInterface LandscapeEdit(this);
	LandscapeEdit.DeleteLayer(LayerInfo);

	// Remove from layer settings array
	{
		int32 LayerIndex = Layers.IndexOfByPredicate([LayerInfo, LayerName](const FLandscapeInfoLayerSettings& LayerSettings) { return LayerSettings.LayerInfoObj == LayerInfo && LayerSettings.LayerName == LayerName; });
		if (LayerIndex != INDEX_NONE)
		{
			Layers.RemoveAt(LayerIndex);
		}
	}

	ForAllLandscapeProxies([LayerInfo](ALandscapeProxy* Proxy)
	{
		Proxy->Modify();
		int32 Index = Proxy->EditorLayerSettings.IndexOfByKey(LayerInfo);
		if (Index != INDEX_NONE)
		{
			Proxy->EditorLayerSettings.RemoveAt(Index);
		}
	});

	//UpdateLayerInfoMap();

	GWarn->EndSlowTask();
}

void ULandscapeInfo::ReplaceLayer(ULandscapeLayerInfoObject* FromLayerInfo, ULandscapeLayerInfoObject* ToLayerInfo)
{
	if (ensure(FromLayerInfo != ToLayerInfo))
	{
		GWarn->BeginSlowTask(LOCTEXT("BeginReplacingLayerTask", "Replacing Layer"), true);

		// Remove data from all components
		FLandscapeEditDataInterface LandscapeEdit(this);
		LandscapeEdit.ReplaceLayer(FromLayerInfo, ToLayerInfo);

		// Convert array
		for (int32 j = 0; j < Layers.Num(); j++)
		{
			if (Layers[j].LayerInfoObj == FromLayerInfo)
			{
				Layers[j].LayerInfoObj = ToLayerInfo;
			}
		}

		ForAllLandscapeProxies([FromLayerInfo, ToLayerInfo](ALandscapeProxy* Proxy)
		{
			Proxy->Modify();
			FLandscapeEditorLayerSettings* ToEditorLayerSettings = Proxy->EditorLayerSettings.FindByKey(ToLayerInfo);
			if (ToEditorLayerSettings != nullptr)
			{
				// If the new layer already exists, simple remove the old layer
				int32 Index = Proxy->EditorLayerSettings.IndexOfByKey(FromLayerInfo);
				if (Index != INDEX_NONE)
				{
					Proxy->EditorLayerSettings.RemoveAt(Index);
				}
			}
			else
			{
				FLandscapeEditorLayerSettings* FromEditorLayerSettings = Proxy->EditorLayerSettings.FindByKey(FromLayerInfo);
				if (FromEditorLayerSettings != nullptr)
				{
					// If only the old layer exists (most common case), change it to point to the new layer info
					FromEditorLayerSettings->LayerInfoObj = ToLayerInfo;
				}
				else
				{
					// If neither exists in the EditorLayerSettings cache, add it
					Proxy->EditorLayerSettings.Add(FLandscapeEditorLayerSettings(ToLayerInfo));
				}
			}
		});

		//UpdateLayerInfoMap();

		GWarn->EndSlowTask();
	}
}

void ULandscapeInfo::GetUsedPaintLayers(const FGuid& InLayerGuid, TArray<ULandscapeLayerInfoObject*>& OutUsedLayerInfos) const
{
	OutUsedLayerInfos.Empty();
	ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
		for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
		{
			const TArray<FWeightmapLayerAllocationInfo>& AllocInfos = Component->GetWeightmapLayerAllocations(InLayerGuid);
			for (const FWeightmapLayerAllocationInfo& AllocInfo : AllocInfos)
			{
				OutUsedLayerInfos.AddUnique(AllocInfo.LayerInfo);
			}
		}
	});
}

void ALandscapeProxy::EditorApplyScale(const FVector& DeltaScale, const FVector* PivotLocation, bool bAltDown, bool bShiftDown, bool bCtrlDown)
{
	FVector ModifiedScale = DeltaScale;

	// Lock X and Y scaling to the same value
	ModifiedScale.X = ModifiedScale.Y = (FMath::Abs(DeltaScale.X) > FMath::Abs(DeltaScale.Y)) ? DeltaScale.X : DeltaScale.Y;

	// Correct for attempts to scale to 0 on any axis
	FVector CurrentScale = GetRootComponent()->GetRelativeScale3D();
	if (AActor::bUsePercentageBasedScaling)
	{
		if (ModifiedScale.X == -1)
		{
			ModifiedScale.X = ModifiedScale.Y = -(CurrentScale.X - 1) / CurrentScale.X;
		}
		if (ModifiedScale.Z == -1)
		{
			ModifiedScale.Z = -(CurrentScale.Z - 1) / CurrentScale.Z;
		}
	}
	else
	{
		if (ModifiedScale.X == -CurrentScale.X)
		{
			CurrentScale.X += 1;
			CurrentScale.Y += 1;
		}
		if (ModifiedScale.Z == -CurrentScale.Z)
		{
			CurrentScale.Z += 1;
		}
	}

	Super::EditorApplyScale(ModifiedScale, PivotLocation, bAltDown, bShiftDown, bCtrlDown);

	// We need to regenerate collision objects, they depend on scale value 
	for (ULandscapeHeightfieldCollisionComponent* Comp : CollisionComponents)
	{
		if (Comp)
		{
			Comp->RecreateCollision();
		}
	}
}

void ALandscapeProxy::EditorApplyMirror(const FVector& MirrorScale, const FVector& PivotLocation)
{
	Super::EditorApplyMirror(MirrorScale, PivotLocation);

	// We need to regenerate collision objects, they depend on scale value 
	for (ULandscapeHeightfieldCollisionComponent* Comp : CollisionComponents)
	{
		if (Comp)
		{
			Comp->RecreateCollision();
		}
	}
}

void ALandscapeProxy::PostEditMove(bool bFinished)
{
	// This point is only reached when Copy and Pasted
	Super::PostEditMove(bFinished);

	if (bFinished && !GetWorld()->IsGameWorld())
	{
		ULandscapeInfo::RecreateLandscapeInfo(GetWorld(), true);
		RecreateComponentsState();

		if (SplineComponent)
		{
			SplineComponent->CheckSplinesValid();
		}
	}
}

void ALandscapeProxy::PostEditImport()
{
	Super::PostEditImport();

	// during import this gets called multiple times, without a valid guid the first time
	if (LandscapeGuid.IsValid())
	{
		CreateLandscapeInfo();
	}

	UpdateAllComponentMaterialInstances();
}

void ALandscape::PostEditMove(bool bFinished)
{
	if (bFinished && !GetWorld()->IsGameWorld())
	{
		// align all proxies to landscape actor
		auto* LandscapeInfo = GetLandscapeInfo();
		if (LandscapeInfo)
		{
			LandscapeInfo->FixupProxiesTransform();
		}
	}

	Super::PostEditMove(bFinished);
}

void ALandscape::PostEditUndo()
{
	Super::PostEditUndo();

	RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_All);
}

bool ALandscape::ShouldImport(FString* ActorPropString, bool IsMovingLevel)
{
	return GetWorld() != nullptr && !GetWorld()->IsGameWorld();
}

void ALandscape::PostEditImport()
{
	check(GetWorld() && !GetWorld()->IsGameWorld());

	for (ALandscape* Landscape : TActorRange<ALandscape>(GetWorld()))
	{
		if (Landscape && Landscape != this && !Landscape->HasAnyFlags(RF_BeginDestroyed) && Landscape->LandscapeGuid == LandscapeGuid)
		{
			// Copy/Paste case, need to generate new GUID
			LandscapeGuid = FGuid::NewGuid();
			break;
		}
	}

	Super::PostEditImport();
}

void ALandscape::PostDuplicate(bool bDuplicateForPIE)
{
	if (!bDuplicateForPIE)
	{
		// Need to generate new GUID when duplicating
		LandscapeGuid = FGuid::NewGuid();
		// This makes sure at least we have a LandscapeInfo mapped for this GUID.
		CreateLandscapeInfo();
	}

	Super::PostDuplicate(bDuplicateForPIE);
}
#endif	//WITH_EDITOR

ULandscapeLayerInfoObject::ULandscapeLayerInfoObject(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, IsReferencedFromLoadedData(false)
#endif // WITH_EDITORONLY_DATA
{
	Hardness = 0.5f;
#if WITH_EDITORONLY_DATA
	MinimumCollisionRelevanceWeight = 0.0f;
	bNoWeightBlend = false;
	SplineFalloffModulationTexture = nullptr;
	SplineFalloffModulationColorMask = ESplineModulationColorMask::Red;
	SplineFalloffModulationTiling = 1.0f;
	SplineFalloffModulationBias = 0.5;
	SplineFalloffModulationScale = 1.0f;
#endif // WITH_EDITORONLY_DATA

	// Assign initial LayerUsageDebugColor
	if (!IsTemplate())
	{
		uint8 Hash[20];
		FString PathNameString = GetPathName();
		FSHA1::HashBuffer(*PathNameString, PathNameString.Len() * sizeof(PathNameString[0]), Hash);
		LayerUsageDebugColor = FLinearColor(float(Hash[0]) / 255.f, float(Hash[1]) / 255.f, float(Hash[2]) / 255.f, 1.f);
	}
}

#if WITH_EDITOR
void ULandscapeLayerInfoObject::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName NAME_Hardness = GET_MEMBER_NAME_CHECKED(ULandscapeLayerInfoObject, Hardness);
	static const FName NAME_PhysMaterial = GET_MEMBER_NAME_CHECKED(ULandscapeLayerInfoObject, PhysMaterial);
	static const FName NAME_LayerUsageDebugColor = GET_MEMBER_NAME_CHECKED(ULandscapeLayerInfoObject, LayerUsageDebugColor);
	static const FName NAME_MinimumCollisionRelevanceWeight = GET_MEMBER_NAME_CHECKED(ULandscapeLayerInfoObject, MinimumCollisionRelevanceWeight);
	static const FName NAME_R = FName(TEXT("R"));
	static const FName NAME_G = FName(TEXT("G"));
	static const FName NAME_B = FName(TEXT("B"));
	static const FName NAME_A = FName(TEXT("A"));

	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (GIsEditor)
	{
		if (PropertyName == NAME_Hardness)
		{
			Hardness = FMath::Clamp<float>(Hardness, 0.0f, 1.0f);
		}
		else if (PropertyName == NAME_PhysMaterial || PropertyName == NAME_MinimumCollisionRelevanceWeight)
		{
			for (TObjectIterator<ALandscapeProxy> It; It; ++It)
			{
				ALandscapeProxy* Proxy = *It;
				if (Proxy->GetWorld() && !Proxy->GetWorld()->IsPlayInEditor())
				{
					ULandscapeInfo* Info = Proxy->GetLandscapeInfo();
					if (Info)
					{
						for (int32 i = 0; i < Info->Layers.Num(); ++i)
						{
							if (Info->Layers[i].LayerInfoObj == this)
							{
								Proxy->ChangedPhysMaterial();
								break;
							}
						}
					}
				}
			}
		}
		else if (PropertyName == NAME_LayerUsageDebugColor || PropertyName == NAME_R || PropertyName == NAME_G || PropertyName == NAME_B || PropertyName == NAME_A)
		{
			LayerUsageDebugColor.A = 1.0f;
			for (TObjectIterator<ALandscapeProxy> It; It; ++It)
			{
				ALandscapeProxy* Proxy = *It;
				if (Proxy->GetWorld() && !Proxy->GetWorld()->IsPlayInEditor())
				{
					Proxy->MarkComponentsRenderStateDirty();
				}
			}
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULandscapeLayerInfoObject, SplineFalloffModulationTexture) ||
				PropertyName == GET_MEMBER_NAME_CHECKED(ULandscapeLayerInfoObject, SplineFalloffModulationColorMask) ||
				PropertyName == GET_MEMBER_NAME_CHECKED(ULandscapeLayerInfoObject, SplineFalloffModulationBias) ||
				PropertyName == GET_MEMBER_NAME_CHECKED(ULandscapeLayerInfoObject, SplineFalloffModulationScale) ||
				PropertyName == GET_MEMBER_NAME_CHECKED(ULandscapeLayerInfoObject, SplineFalloffModulationTiling))
		{
			for (TObjectIterator<ULandscapeInfo> It; It; ++It)
			{
				if(ALandscape* Landscape = It->LandscapeActor.Get())
				{
					Landscape->OnLayerInfoSplineFalloffModulationChanged(this);
				}
			}
		}
	}
}

void ULandscapeLayerInfoObject::PostLoad()
{
	Super::PostLoad();
	if (GIsEditor)
	{
		if (!HasAnyFlags(RF_Standalone))
		{
			SetFlags(RF_Standalone);
		}
		Hardness = FMath::Clamp<float>(Hardness, 0.0f, 1.0f);
	}
}

void ALandscapeProxy::RemoveXYOffsets()
{
	bool bFoundXYOffset = false;

	for (int32 i = 0; i < LandscapeComponents.Num(); ++i)
	{
		ULandscapeComponent* Comp = LandscapeComponents[i];
		if (Comp && Comp->XYOffsetmapTexture)
		{
			Comp->XYOffsetmapTexture->SetFlags(RF_Transactional);
			Comp->XYOffsetmapTexture->Modify();
			Comp->XYOffsetmapTexture->MarkPackageDirty();
			Comp->XYOffsetmapTexture->ClearFlags(RF_Standalone);
			Comp->Modify();
			Comp->MarkPackageDirty();
			Comp->XYOffsetmapTexture = nullptr;
			Comp->MarkRenderStateDirty();
			bFoundXYOffset = true;
		}
	}

	if (bFoundXYOffset)
	{
		RecreateCollisionComponents();
	}
}



void ALandscapeProxy::RecreateCollisionComponents()
{
	// We can assume these are all junk; they recreate as needed
	FlushGrassComponents();

	// Clear old CollisionComponent containers
	CollisionComponents.Empty();

	// Destroy any owned collision components
	TInlineComponentArray<ULandscapeHeightfieldCollisionComponent*> CollisionComps;
	GetComponents(CollisionComps);
	for (ULandscapeHeightfieldCollisionComponent* Component : CollisionComps)
	{
		Component->DestroyComponent();
	}

	TArray<USceneComponent*> AttachedCollisionComponents = RootComponent->GetAttachChildren().FilterByPredicate(
		[](USceneComponent* Component)
	{
		return Cast<ULandscapeHeightfieldCollisionComponent>(Component);
	});

	// Destroy any attached but un-owned collision components
	for (USceneComponent* Component : AttachedCollisionComponents)
	{
		Component->DestroyComponent();
	}

	// Recreate collision
	CollisionMipLevel = FMath::Clamp<int32>(CollisionMipLevel, 0, FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1);
	SimpleCollisionMipLevel = FMath::Clamp<int32>(SimpleCollisionMipLevel, 0, FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1);
	for (ULandscapeComponent* Comp : LandscapeComponents)
	{
		if (Comp)
		{
			Comp->CollisionMipLevel = CollisionMipLevel;
			Comp->SimpleCollisionMipLevel = SimpleCollisionMipLevel;
			Comp->DestroyCollisionData();
			Comp->UpdateCollisionData();
		}
	}
}

void ULandscapeInfo::RecreateCollisionComponents()
{
	ForAllLandscapeProxies([](ALandscapeProxy* Proxy)
	{
		Proxy->RecreateCollisionComponents();
	});
}

void ULandscapeInfo::RemoveXYOffsets()
{
	ForAllLandscapeProxies([](ALandscapeProxy* Proxy)
	{
		Proxy->RemoveXYOffsets();
	});
}

void ULandscapeInfo::PostponeTextureBaking()
{
	static const int32 PostponeValue = 60; //frames
	
	ForAllLandscapeProxies([](ALandscapeProxy* Proxy)
	{
		Proxy->UpdateBakedTexturesCountdown = PostponeValue;
	});
}

bool ULandscapeInfo::CanHaveLayersContent() const
{
	if (ALandscape* Landscape = LandscapeActor.Get())
	{
		return Landscape->CanHaveLayersContent();
	}
	return false;
}

void ULandscapeInfo::ClearDirtyData()
{
	if (ALandscape* Landscape = LandscapeActor.Get())
	{
		ForAllLandscapeComponents([=](ULandscapeComponent* InLandscapeComponent)
		{
			Landscape->ClearDirtyData(InLandscapeComponent);
		});
	}
}

void ULandscapeInfo::UpdateAllComponentMaterialInstances()
{
	ForAllLandscapeProxies([](ALandscapeProxy* Proxy)
	{
		Proxy->UpdateAllComponentMaterialInstances();
	});
}

ALandscapeProxy* ULandscapeInfo::MoveComponentsToLevel(const TArray<ULandscapeComponent*>& InComponents, ULevel* TargetLevel, FName NewProxyName)
{
	ALandscape* Landscape = LandscapeActor.Get();
	check(Landscape != nullptr);

	// Make sure references are in a different package (should be fixed up before calling this method)
	// Check the Physical Material is same package with Landscape
	if(Landscape->DefaultPhysMaterial && Landscape->DefaultPhysMaterial->GetOutermost() == Landscape->GetOutermost())
	{
		return nullptr;
	}

	// Check the LayerInfoObjects are not in same package as Landscape
	for (int32 i = 0; i < Layers.Num(); ++i)
	{
		ULandscapeLayerInfoObject* LayerInfo = Layers[i].LayerInfoObj;
		if (LayerInfo && LayerInfo->GetOutermost() == Landscape->GetOutermost())
		{
			return nullptr;
		}
	}

	// Check the Landscape Materials are not in same package as moved components
	for (ULandscapeComponent* Component : InComponents)
	{
		UMaterialInterface* LandscapeMaterial = Component->GetLandscapeMaterial();
		if (LandscapeMaterial && LandscapeMaterial->GetOutermost() == Component->GetOutermost())
		{
			return nullptr;
		}
	}

	struct FCompareULandscapeComponentBySectionBase
	{
		FORCEINLINE bool operator()(const ULandscapeComponent& A, const ULandscapeComponent& B) const
		{
			return (A.GetSectionBase().X == B.GetSectionBase().X) ? (A.GetSectionBase().Y < B.GetSectionBase().Y) : (A.GetSectionBase().X < B.GetSectionBase().X);
		}
	};
	TArray<ULandscapeComponent*> ComponentsToMove(InComponents);
	ComponentsToMove.Sort(FCompareULandscapeComponentBySectionBase());
		
	const int32 ComponentSizeVerts = Landscape->NumSubsections * (Landscape->SubsectionSizeQuads + 1);
	const int32 NeedHeightmapSize = 1 << FMath::CeilLogTwo(ComponentSizeVerts);

	TSet<ALandscapeProxy*> SelectProxies;
	TSet<ULandscapeComponent*> TargetSelectedComponents;
	TArray<ULandscapeHeightfieldCollisionComponent*> TargetSelectedCollisionComponents;
	for (ULandscapeComponent* Component : ComponentsToMove)
	{
		SelectProxies.Add(Component->GetLandscapeProxy());
		if (Component->GetLandscapeProxy()->GetOuter() != TargetLevel)
		{
			TargetSelectedComponents.Add(Component);
		}

		ULandscapeHeightfieldCollisionComponent* CollisionComp = Component->CollisionComponent.Get();
		SelectProxies.Add(CollisionComp->GetLandscapeProxy());
		if (CollisionComp->GetLandscapeProxy()->GetOuter() != TargetLevel)
		{
			TargetSelectedCollisionComponents.Add(CollisionComp);
		}
	}

	// Check which ones are need for height map change
	TSet<UTexture2D*> OldHeightmapTextures;
	for (ULandscapeComponent* Component : TargetSelectedComponents)
	{
		Component->Modify();
		OldHeightmapTextures.Add(Component->GetHeightmap());
	}

	// Need to split all the component which share Heightmap with selected components
	TMap<ULandscapeComponent*, bool> HeightmapUpdateComponents;
	HeightmapUpdateComponents.Reserve(TargetSelectedComponents.Num() * 4); // worst case
	for (ULandscapeComponent* Component : TargetSelectedComponents)
	{
		// Search neighbor only
		const int32 SearchX = Component->GetHeightmap()->Source.GetSizeX() / NeedHeightmapSize - 1;
		const int32 SearchY = Component->GetHeightmap()->Source.GetSizeY() / NeedHeightmapSize - 1;
		const FIntPoint ComponentBase = Component->GetSectionBase() / Component->ComponentSizeQuads;

		for (int32 Y = -SearchY; Y <= SearchY; ++Y)
		{
			for (int32 X = -SearchX; X <= SearchX; ++X)
			{
				ULandscapeComponent* const Neighbor = XYtoComponentMap.FindRef(ComponentBase + FIntPoint(X, Y));
				if (Neighbor && Neighbor->GetHeightmap() == Component->GetHeightmap() && !HeightmapUpdateComponents.Contains(Neighbor))
				{
					Neighbor->Modify();
					bool bNeedsMoveToCurrentLevel = TargetSelectedComponents.Contains(Neighbor);
					HeightmapUpdateComponents.Add(Neighbor, bNeedsMoveToCurrentLevel);
				}
			}
		}
	}

	ALandscapeProxy* LandscapeProxy = GetLandscapeProxyForLevel(TargetLevel);
	if (!LandscapeProxy)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = NewProxyName;
		SpawnParams.OverrideLevel = TargetLevel;
		LandscapeProxy = TargetLevel->GetWorld()->SpawnActor<ALandscapeStreamingProxy>(SpawnParams);
		
		// copy shared properties to this new proxy
		LandscapeProxy->GetSharedProperties(Landscape);
		LandscapeProxy->CreateLandscapeInfo();
		LandscapeProxy->SetActorLabel(LandscapeProxy->GetName());

		// set proxy location
		// by default first component location
		ULandscapeComponent* FirstComponent = *TargetSelectedComponents.CreateConstIterator();
		LandscapeProxy->GetRootComponent()->SetWorldLocationAndRotation(FirstComponent->GetComponentLocation(), FirstComponent->GetComponentRotation());
		LandscapeProxy->LandscapeSectionOffset = FirstComponent->GetSectionBase();

		// Hide(unregister) the new landscape if owning level currently in hidden state
		if (LandscapeProxy->GetLevel()->bIsVisible == false)
		{
			LandscapeProxy->UnregisterAllComponents();
		}
	}

	// Changing Heightmap format for selected components
	for (const auto& HeightmapUpdateComponentPair : HeightmapUpdateComponents)
	{
		ALandscape::SplitHeightmap(HeightmapUpdateComponentPair.Key, HeightmapUpdateComponentPair.Value ? LandscapeProxy : nullptr);
	}

	// Delete if it is no referenced textures...
	for (UTexture2D* Texture : OldHeightmapTextures)
	{
		Texture->SetFlags(RF_Transactional);
		Texture->Modify();
		Texture->MarkPackageDirty();
		Texture->ClearFlags(RF_Standalone);
	}

	for (ALandscapeProxy* Proxy : SelectProxies)
	{
		Proxy->Modify();
	}

	LandscapeProxy->Modify();
	LandscapeProxy->MarkPackageDirty();

	// Handle XY-offset textures (these don't need splitting, as they aren't currently shared between components like heightmaps/weightmaps can be)
	for (ULandscapeComponent* Component : TargetSelectedComponents)
	{
		if (Component->XYOffsetmapTexture)
		{
			Component->XYOffsetmapTexture->Modify();
			Component->XYOffsetmapTexture->Rename(nullptr, LandscapeProxy);
		}
	}

	// Change Weight maps...
	{
		FLandscapeEditDataInterface LandscapeEdit(this);
		for (ULandscapeComponent* Component : TargetSelectedComponents)
		{
			Component->ReallocateWeightmaps(&LandscapeEdit, false, true, false, true, LandscapeProxy);
			Component->ForEachLayer([&](const FGuid& LayerGuid, FLandscapeLayerComponentData& LayerData)
			{
				FScopedSetLandscapeEditingLayer Scope(Landscape, LayerGuid);
				Component->ReallocateWeightmaps(&LandscapeEdit, true, true, false, true, LandscapeProxy);
			});
			Landscape->RequestLayersContentUpdateForceAll();
		}

		// Need to Repacking all the Weight map (to make it packed well...)
		for (ALandscapeProxy* Proxy : SelectProxies)
		{
			Proxy->RemoveInvalidWeightmaps();
		}
	}

	// Move the components to the Proxy actor
	// This does not use the MoveSelectedActorsToCurrentLevel path as there is no support to only move certain components.
	for (ULandscapeComponent* Component : TargetSelectedComponents)
	{
		// Need to move or recreate all related data (Height map, Weight map, maybe collision components, allocation info)
		Component->GetLandscapeProxy()->LandscapeComponents.Remove(Component);
		Component->UnregisterComponent();
		Component->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		Component->InvalidateLightingCache();
		Component->Rename(nullptr, LandscapeProxy);
		LandscapeProxy->LandscapeComponents.Add(Component);
		Component->AttachToComponent(LandscapeProxy->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);

		// clear transient mobile data
		Component->MobileDataSourceHash.Invalidate();
		Component->MobileMaterialInterfaces.Reset();
		Component->MobileWeightmapTextures.Reset();

		Component->UpdateMaterialInstances();
	}
	LandscapeProxy->UpdateCachedHasLayersContent();

	for (ULandscapeHeightfieldCollisionComponent* Component : TargetSelectedCollisionComponents)
	{
		// Need to move or recreate all related data (Height map, Weight map, maybe collision components, allocation info)

		Component->GetLandscapeProxy()->CollisionComponents.Remove(Component);
		Component->UnregisterComponent();
		Component->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		Component->Rename(nullptr, LandscapeProxy);
		LandscapeProxy->CollisionComponents.Add(Component);
		Component->AttachToComponent(LandscapeProxy->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);

		// Move any foliage associated
		AInstancedFoliageActor::MoveInstancesForComponentToLevel(Component, TargetLevel);
	}
		
	// Register our new components if destination landscape is registered in scene 
	if (LandscapeProxy->GetRootComponent()->IsRegistered())
	{
		LandscapeProxy->RegisterAllComponents();
	}

	for (ALandscapeProxy* Proxy : SelectProxies)
	{
		if (Proxy->GetRootComponent()->IsRegistered())
		{
			Proxy->RegisterAllComponents();
		}
	}

	return LandscapeProxy;
}

void ALandscape::SplitHeightmap(ULandscapeComponent* Comp, ALandscapeProxy* TargetProxy, FMaterialUpdateContext* InOutUpdateContext, TArray<FComponentRecreateRenderStateContext>* InOutRecreateRenderStateContext, bool InReregisterComponent)
{
	ULandscapeInfo* Info = Comp->GetLandscapeInfo();

	// Make sure the heightmap UVs are powers of two.
	int32 ComponentSizeVerts = Comp->NumSubsections * (Comp->SubsectionSizeQuads + 1);
	int32 HeightmapSizeU = (1 << FMath::CeilLogTwo(ComponentSizeVerts));
	int32 HeightmapSizeV = (1 << FMath::CeilLogTwo(ComponentSizeVerts));

	ALandscapeProxy* SrcProxy = Comp->GetLandscapeProxy();
	ALandscapeProxy* DstProxy = TargetProxy ? TargetProxy : SrcProxy;
	SrcProxy->Modify();
	DstProxy->Modify();

	UTexture2D* OldHeightmapTexture = Comp->GetHeightmap(false);
	UTexture2D* NewHeightmapTexture = NULL;
	FVector4 OldHeightmapScaleBias = Comp->HeightmapScaleBias;
	FVector4 NewHeightmapScaleBias = FVector4(1.0f / (float)HeightmapSizeU, 1.0f / (float)HeightmapSizeV, 0.0f, 0.0f);

	{
		// Read old data and split
		FLandscapeEditDataInterface LandscapeEdit(Info);
		TArray<uint8> HeightData;
		HeightData.AddZeroed((1 + Comp->ComponentSizeQuads) * (1 + Comp->ComponentSizeQuads) * sizeof(uint16));
		// Because of edge problem, normal would be just copy from old component data
		TArray<uint8> NormalData;
		NormalData.AddZeroed((1 + Comp->ComponentSizeQuads) * (1 + Comp->ComponentSizeQuads) * sizeof(uint16));
		LandscapeEdit.GetHeightDataFast(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)HeightData.GetData(), 0, (uint16*)NormalData.GetData());

		// Create the new heightmap texture
		NewHeightmapTexture = DstProxy->CreateLandscapeTexture(HeightmapSizeU, HeightmapSizeV, TEXTUREGROUP_Terrain_Heightmap, TSF_BGRA8);
		ULandscapeComponent::CreateEmptyTextureMips(NewHeightmapTexture, true);
		Comp->HeightmapScaleBias = NewHeightmapScaleBias;
		Comp->SetHeightmap(NewHeightmapTexture);

		check(Comp->GetHeightmap(false) == Comp->GetHeightmap(true));
		LandscapeEdit.SetHeightData(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)HeightData.GetData(), 0, false, (uint16*)NormalData.GetData());
	}

	// End material update
	if (InOutUpdateContext != nullptr && InOutRecreateRenderStateContext != nullptr)
	{
		Comp->UpdateMaterialInstances(*InOutUpdateContext, *InOutRecreateRenderStateContext);
	}
	else
	{
		Comp->UpdateMaterialInstances();
	}

	// We disable automatic material update context, to manage it manually if we have a custom update context specified
	GDisableAutomaticTextureMaterialUpdateDependencies = (InOutUpdateContext != nullptr);

	NewHeightmapTexture->PostEditChange();

	if (InOutUpdateContext != nullptr)
	{
		// Build a list of all unique materials the landscape uses
		TArray<UMaterialInterface*> LandscapeMaterials;

		int8 MaxLOD = FMath::CeilLogTwo(Comp->SubsectionSizeQuads + 1) - 1;

		for (int8 LODIndex = 0; LODIndex < MaxLOD; ++LODIndex)
		{
			UMaterialInterface* Material = Comp->GetLandscapeMaterial(LODIndex);
			LandscapeMaterials.AddUnique(Material);
		}

		TSet<UMaterial*> BaseMaterialsThatUseThisTexture;

		for (UMaterialInterface* MaterialInterface : LandscapeMaterials)
		{
			if (DoesMaterialUseTexture(MaterialInterface, NewHeightmapTexture))
			{
				UMaterial* Material = MaterialInterface->GetMaterial();
				bool MaterialAlreadyCompute = false;
				BaseMaterialsThatUseThisTexture.Add(Material, &MaterialAlreadyCompute);

				if (!MaterialAlreadyCompute)
				{
					if (Material->IsTextureForceRecompileCacheRessource(NewHeightmapTexture))
					{
						InOutUpdateContext->AddMaterial(Material);
						Material->UpdateMaterialShaderCacheAndTextureReferences();
					}
				}
			}
		}
	}

	GDisableAutomaticTextureMaterialUpdateDependencies = false;

#if WITH_EDITORONLY_DATA
	check(Comp->GetLandscapeProxy()->HasLayersContent() == DstProxy->CanHaveLayersContent());
	if (Comp->GetLandscapeProxy()->HasLayersContent() && DstProxy->CanHaveLayersContent())
	{
		FLandscapeLayersTexture2DCPUReadBackResource* NewCPUReadBackResource = new FLandscapeLayersTexture2DCPUReadBackResource(NewHeightmapTexture->Source.GetSizeX(), NewHeightmapTexture->Source.GetSizeY(), NewHeightmapTexture->GetPixelFormat(), NewHeightmapTexture->Source.GetNumMips());
		BeginInitResource(NewCPUReadBackResource);
		DstProxy->HeightmapsCPUReadBack.Add(NewHeightmapTexture, NewCPUReadBackResource);

		// Free OldHeightmapTexture's CPUReadBackResource if not used by any component
		bool FreeCPUReadBack = true;
		for (ULandscapeComponent* Component : SrcProxy->LandscapeComponents)
		{
			if (Component != Comp && Component->GetHeightmap(false) == OldHeightmapTexture)
			{
				FreeCPUReadBack = false;
				break;
			}
		}
		if (FreeCPUReadBack)
		{
			FLandscapeLayersTexture2DCPUReadBackResource** OldCPUReadBackResource = SrcProxy->HeightmapsCPUReadBack.Find(OldHeightmapTexture);
			if (OldCPUReadBackResource)
			{
				if (FLandscapeLayersTexture2DCPUReadBackResource* ResourceToDelete = *OldCPUReadBackResource)
				{
					ReleaseResourceAndFlush(ResourceToDelete);
					delete ResourceToDelete;
					SrcProxy->HeightmapsCPUReadBack.Remove(OldHeightmapTexture);
				}
			}
		}

		// Move layer content to new layer heightmap
		FLandscapeEditDataInterface LandscapeEdit(Info);
		ALandscape* Landscape = Info->LandscapeActor.Get();
		Comp->ForEachLayer([&](const FGuid& LayerGuid, FLandscapeLayerComponentData& LayerData)
		{
			UTexture2D* OldLayerHeightmap = LayerData.HeightmapData.Texture;
			if (OldLayerHeightmap != nullptr)
			{
				FScopedSetLandscapeEditingLayer Scope(Landscape, LayerGuid);
				// Read old data and split
				TArray<uint8> LayerHeightData;
				LayerHeightData.AddZeroed((1 + Comp->ComponentSizeQuads) * (1 + Comp->ComponentSizeQuads) * sizeof(uint16));
				// Because of edge problem, normal would be just copy from old component data
				TArray<uint8> LayerNormalData;
				LayerNormalData.AddZeroed((1 + Comp->ComponentSizeQuads) * (1 + Comp->ComponentSizeQuads) * sizeof(uint16));

				// Read using old heightmap scale/bias
				Comp->HeightmapScaleBias = OldHeightmapScaleBias;
				LandscapeEdit.GetHeightDataFast(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)LayerHeightData.GetData(), 0, (uint16*)LayerNormalData.GetData());
				// Restore new heightmap scale/bias
				Comp->HeightmapScaleBias = NewHeightmapScaleBias;
				{
					UTexture2D* LayerHeightmapTexture = DstProxy->CreateLandscapeTexture(HeightmapSizeU, HeightmapSizeV, TEXTUREGROUP_Terrain_Heightmap, TSF_BGRA8);
					ULandscapeComponent::CreateEmptyTextureMips(LayerHeightmapTexture, true);
					LayerHeightmapTexture->PostEditChange();
					// Set Layer heightmap texture
					LayerData.HeightmapData.Texture = LayerHeightmapTexture;
					LandscapeEdit.SetHeightData(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)LayerHeightData.GetData(), 0, false, (uint16*)LayerNormalData.GetData());
				}
			}
		});

		Landscape->RequestLayersContentUpdateForceAll();
	}
#endif

	// Reregister
	if (InReregisterComponent)
	{
		FComponentReregisterContext ReregisterContext(Comp);
	}
}

namespace
{
	inline float AdjustStaticLightingResolution(float StaticLightingResolution, int32 NumSubsections, int32 SubsectionSizeQuads, int32 ComponentSizeQuads)
	{
		// Change Lighting resolution to proper one...
		if (StaticLightingResolution > 1.0f)
		{
			StaticLightingResolution = (int32)StaticLightingResolution;
		}
		else if (StaticLightingResolution < 1.0f)
		{
			// Restrict to 1/16
			if (StaticLightingResolution < 0.0625)
			{
				StaticLightingResolution = 0.0625;
			}

			// Adjust to 1/2^n
			int32 i = 2;
			int32 LightmapSize = (NumSubsections * (SubsectionSizeQuads + 1)) >> 1;
			while (StaticLightingResolution < (1.0f / i) && LightmapSize > 4)
			{
				i <<= 1;
				LightmapSize >>= 1;
			}
			StaticLightingResolution = 1.0f / i;

			int32 PixelPaddingX = GPixelFormats[PF_DXT1].BlockSizeX;

			int32 DestSize = (int32)((2 * PixelPaddingX + ComponentSizeQuads + 1) * StaticLightingResolution);
			StaticLightingResolution = (float)DestSize / (2 * PixelPaddingX + ComponentSizeQuads + 1);
		}

		return StaticLightingResolution;
	}
};

bool ALandscapeProxy::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	// Don't allow edition of properties that are shared with the parent landscape properties
	// See  ALandscapeProxy::FixupSharedData(ALandscape* Landscape)
	if (GetLandscapeActor() != this)
	{
		FName PropertyName = InProperty ? InProperty->GetFName() : NAME_None;

		if (PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, MaxLODLevel) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, TessellationComponentScreenSize) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, ComponentScreenSizeToUseSubSections) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, UseTessellationComponentScreenSizeFalloff) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, TessellationComponentScreenSizeFalloff) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LODDistributionSetting) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LOD0DistributionSetting) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LOD0ScreenSize) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, OccluderGeometryLOD) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, TargetDisplayOrder) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, TargetDisplayOrderList))
		{
			return false;
		}
	}

	return true;
}

void ALandscapeProxy::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	const FName SubPropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	bool bChangedPhysMaterial = false;

	if (PropertyName == FName(TEXT("RelativeScale3D")))
	{
		// RelativeScale3D isn't even a property of ALandscapeProxy, it's a property of the root component
		if (RootComponent)
		{
			const FVector OriginalScale = RootComponent->GetRelativeScale3D();
			FVector ModifiedScale = OriginalScale;

			// Lock X and Y scaling to the same value
			if (SubPropertyName == FName("Y"))
			{
				ModifiedScale.X = FMath::Abs(OriginalScale.Y)*FMath::Sign(ModifiedScale.X);
			}
			else if (SubPropertyName == FName("X"))
			{
				ModifiedScale.Y = FMath::Abs(OriginalScale.X)*FMath::Sign(ModifiedScale.Y);
			}

			ULandscapeInfo* Info = GetLandscapeInfo();

			// Correct for attempts to scale to 0 on any axis
			if (ModifiedScale.X == 0)
			{
				if (Info && Info->DrawScale.X < 0)
				{
					ModifiedScale.Y = ModifiedScale.X = -1;
				}
				else
				{
					ModifiedScale.Y = ModifiedScale.X = 1;
				}
			}
			if (ModifiedScale.Z == 0)
			{
				if (Info && Info->DrawScale.Z < 0)
				{
					ModifiedScale.Z = -1;
				}
				else
				{
					ModifiedScale.Z = 1;
				}
			}

			RootComponent->SetRelativeScale3D(ModifiedScale);

			// Update ULandscapeInfo cached DrawScale
			if (Info)
			{
				Info->DrawScale = ModifiedScale;
			}

			// We need to regenerate collision objects, they depend on scale value
			if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
			{
				for (int32 ComponentIndex = 0; ComponentIndex < CollisionComponents.Num(); ComponentIndex++)
				{
					ULandscapeHeightfieldCollisionComponent* Comp = CollisionComponents[ComponentIndex];
					if (Comp)
					{
						Comp->RecreateCollision();
					}
				}
			}
		}
	}

	if (GIsEditor && PropertyName == FName(TEXT("StreamingDistanceMultiplier")))
	{
		// Recalculate in a few seconds.
		GetWorld()->TriggerStreamingDataRebuild();
	}
	else if (GIsEditor && PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, DefaultPhysMaterial))
	{
		bChangedPhysMaterial = true;
	}
	else if (GIsEditor &&
		(PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, CollisionMipLevel) ||
		 PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, SimpleCollisionMipLevel) ||
		 PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, CollisionThickness) ||
		 PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, bBakeMaterialPositionOffsetIntoCollision) ||
		 PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, bGenerateOverlapEvents)))
	{
		if (bBakeMaterialPositionOffsetIntoCollision)
		{
			MarkComponentsRenderStateDirty();
		}
		if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
		{
			RecreateCollisionComponents();
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, TessellationComponentScreenSize))
	{
		ChangeTessellationComponentScreenSize(TessellationComponentScreenSize);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, ComponentScreenSizeToUseSubSections))
	{
		ChangeComponentScreenSizeToUseSubSections(ComponentScreenSizeToUseSubSections);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, UseTessellationComponentScreenSizeFalloff))
	{
		ChangeUseTessellationComponentScreenSizeFalloff(UseTessellationComponentScreenSizeFalloff);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, TessellationComponentScreenSizeFalloff))
	{
		ChangeTessellationComponentScreenSizeFalloff(TessellationComponentScreenSizeFalloff);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LODDistributionSetting)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LOD0DistributionSetting)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LOD0ScreenSize))
	{		
		MarkComponentsRenderStateDirty();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, bUseMaterialPositionOffsetInStaticLighting))
	{
		InvalidateLightingCache();
	}
	else if(
		PropertyName == FName(TEXT("CastShadow")) ||
		PropertyName == FName(TEXT("bCastDynamicShadow")) ||
		PropertyName == FName(TEXT("bCastStaticShadow")) ||
		PropertyName == FName(TEXT("bCastFarShadow")) ||
		PropertyName == FName(TEXT("bCastHiddenShadow")) ||
		PropertyName == FName(TEXT("bCastShadowAsTwoSided")) ||
		PropertyName == FName(TEXT("bAffectDistanceFieldLighting")) ||
		PropertyName == FName(TEXT("bRenderCustomDepth")) ||
		PropertyName == FName(TEXT("CustomDepthStencilValue")) ||
		PropertyName == FName(TEXT("LightingChannels")) ||
		PropertyName == FName(TEXT("LDMaxDrawDistance")))
	{
		// Replicate shared properties to all components.
		for (int32 ComponentIndex = 0; ComponentIndex < LandscapeComponents.Num(); ComponentIndex++)
		{
			ULandscapeComponent* Comp = LandscapeComponents[ComponentIndex];
			if (Comp)
			{
				Comp->UpdatedSharedPropertiesFromActor();
			}
		}
	}
	else if (GIsEditor && 
		(PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, OccluderGeometryLOD) || PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, bMeshHoles) || PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, MeshHolesMaxLod)))
	{
		CheckGenerateLandscapePlatformData(false, nullptr);
		MarkComponentsRenderStateDirty();
	}
	else if (PropertyName == FName(TEXT("bUseDynamicMaterialInstance")))
	{
		MarkComponentsRenderStateDirty();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, RuntimeVirtualTextures)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, VirtualTextureRenderPassType)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, VirtualTextureNumLods)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, VirtualTextureLodBias))
	{
		MarkComponentsRenderStateDirty();
	}

	// Remove null layer infos
	EditorLayerSettings.RemoveAll([](const FLandscapeEditorLayerSettings& Entry) { return Entry.LayerInfoObj == nullptr; });

	// Remove any null landscape components
	LandscapeComponents.RemoveAll([](const ULandscapeComponent* Component) { return Component == nullptr; });

	ULandscapeInfo* Info = GetLandscapeInfo();
	bool bRemovedAnyLayers = false;
	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = Component->GetWeightmapLayerAllocations(false);

		int32 NumNullLayers = Algo::CountIf(ComponentWeightmapLayerAllocations, [](const FWeightmapLayerAllocationInfo& Allocation) { return Allocation.LayerInfo == nullptr; });
		if (NumNullLayers > 0)
		{
			FLandscapeEditDataInterface LandscapeEdit(Info);
			for (int32 i = 0; i < NumNullLayers; ++i)
			{
				// DeleteLayer doesn't expect duplicates, so we need to call it once for each null
				Component->DeleteLayer(nullptr, LandscapeEdit);
			}
			bRemovedAnyLayers = true;
		}
	}
	if (bRemovedAnyLayers)
	{
		ALandscape* LandscapeActor = GetLandscapeActor();

		if(LandscapeActor != nullptr && LandscapeActor->HasLayersContent())
		{
			LandscapeActor->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_All);
		}
		else
		{
			ALandscapeProxy::InvalidateGeneratedComponentData(LandscapeComponents);
		}
	}

	// Must do this *after* correcting the scale or reattaching the landscape components will crash!
	// Must do this *after* clamping values / propogating values to components
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Call that posteditchange when components are registered
	if (bChangedPhysMaterial)
	{
		ChangedPhysMaterial();
	}
}

void ALandscapeStreamingProxy::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

	if (PropertyName == FName(TEXT("LandscapeActor")))
	{
		if (LandscapeActor && IsValidLandscapeActor(LandscapeActor.Get()))
		{
			LandscapeGuid = LandscapeActor->GetLandscapeGuid();
			if (GIsEditor && GetWorld() && !GetWorld()->IsPlayInEditor())
			{
				// TODO - only need to refresh the old and new landscape info
				ULandscapeInfo::RecreateLandscapeInfo(GetWorld(), false);
				FixupWeightmaps();
				InitializeProxyLayersWeightmapUsage();
			}
		}
		else
		{
			LandscapeActor = nullptr;
		}
	}
	else if (PropertyName == FName(TEXT("LandscapeMaterial")) || PropertyName == FName(TEXT("LandscapeHoleMaterial")) || PropertyName == FName(TEXT("LandscapeMaterialsOverride")))
	{
		bool RecreateMaterialInstances = true;

		if (PropertyName == FName(TEXT("LandscapeMaterialsOverride")) && PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
		{
			RecreateMaterialInstances = false;
		}

		if (RecreateMaterialInstances)
		{
			{
				FMaterialUpdateContext MaterialUpdateContext;
				GetLandscapeInfo()->UpdateLayerInfoMap(/*this*/);

				// Clear the parents out of combination material instances
				for (const auto& MICPair : MaterialInstanceConstantMap)
				{
					UMaterialInstanceConstant* MaterialInstance = MICPair.Value;
					MaterialInstance->BasePropertyOverrides.bOverride_BlendMode = false;
					MaterialInstance->SetParentEditorOnly(nullptr);
					MaterialUpdateContext.AddMaterialInstance(MaterialInstance);
				}

				// Remove our references to any material instances
				MaterialInstanceConstantMap.Empty();
			}

			UpdateAllComponentMaterialInstances();

			UWorld* World = GetWorld();

			if (World != nullptr && World->FeatureLevel <= ERHIFeatureLevel::ES3_1)
			{
				for (ULandscapeComponent * Component : LandscapeComponents)
				{
					if (Component != nullptr)
					{
						Component->CheckGenerateLandscapePlatformData(false, nullptr);
					}
				}
			}
		}
	}

	// Must do this *after* clamping values
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ALandscape::PreEditChange(FProperty* PropertyThatWillChange)
{
	PreEditLandscapeMaterial = LandscapeMaterial;
	PreEditLandscapeHoleMaterial = LandscapeHoleMaterial;
	PreEditLandscapeMaterialsOverride = LandscapeMaterialsOverride;

	Super::PreEditChange(PropertyThatWillChange);
}

void ALandscape::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

	bool ChangedMaterial = false;
	bool bNeedsRecalcBoundingBox = false;
	bool bChangedLighting = false;
	bool bChangedNavRelevance = false;
	bool bChangeRejectNavmeshUnder = false;
	bool bPropagateToProxies = false;

	ULandscapeInfo* Info = GetLandscapeInfo();

	if ((PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LandscapeMaterial) || PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LandscapeHoleMaterial) || MemberPropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LandscapeMaterialsOverride))
		&& PropertyChangedEvent.ChangeType != EPropertyChangeType::ArrayAdd)
	{
		bool HasMaterialChanged = false;

		if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
		{
			if (PreEditLandscapeMaterial != LandscapeMaterial || PreEditLandscapeHoleMaterial != LandscapeHoleMaterial || PreEditLandscapeMaterialsOverride.Num() != LandscapeMaterialsOverride.Num() || bIsPerformingInteractiveActionOnLandscapeMaterialOverride)
			{
				HasMaterialChanged = true;
			}

			if (!HasMaterialChanged)
			{
				for (int32 i = 0; i < LandscapeMaterialsOverride.Num(); ++i)
				{
					const FLandscapeProxyMaterialOverride& NewMaterialOverride = LandscapeMaterialsOverride[i];
					const FLandscapeProxyMaterialOverride& PreEditMaterialOverride = PreEditLandscapeMaterialsOverride[i];

					if (!(PreEditMaterialOverride == NewMaterialOverride))
					{
						HasMaterialChanged = true;
						break;
					}
				}
			}

			bIsPerformingInteractiveActionOnLandscapeMaterialOverride = false;
		}
		else
		{
			// We are probably using a slider or something similar in LandscapeMaterialsOverride
			bIsPerformingInteractiveActionOnLandscapeMaterialOverride = MemberPropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LandscapeMaterialsOverride);
		}

		if (Info != nullptr && HasMaterialChanged)
		{
			FMaterialUpdateContext MaterialUpdateContext;
			Info->UpdateLayerInfoMap(/*this*/);

			ChangedMaterial = true;

			// Clear the parents out of combination material instances
			for (const auto& MICPair : MaterialInstanceConstantMap)
			{
				UMaterialInstanceConstant* MaterialInstance = MICPair.Value;
				MaterialInstance->BasePropertyOverrides.bOverride_BlendMode = false;
				MaterialInstance->SetParentEditorOnly(nullptr);
				MaterialUpdateContext.AddMaterialInstance(MaterialInstance);
			}

			// Remove our references to any material instances
			MaterialInstanceConstantMap.Empty();
		}
	}
	else if (MemberPropertyName == FName(TEXT("RelativeScale3D")) ||
			 MemberPropertyName == FName(TEXT("RelativeLocation")) ||
			 MemberPropertyName == FName(TEXT("RelativeRotation")))
	{
		if (Info != nullptr)
		{
			// update transformations for all linked proxies 
			Info->FixupProxiesTransform();
			bNeedsRecalcBoundingBox = true;
		}
	}
	else if (GIsEditor && PropertyName == FName(TEXT("MaxLODLevel")))
	{
		MaxLODLevel = FMath::Clamp<int32>(MaxLODLevel, -1, FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1);
		bPropagateToProxies = true;
	}
	else if (PropertyName == FName(TEXT("TessellationComponentScreenSize")))
	{
		TessellationComponentScreenSize = FMath::Clamp<float>(TessellationComponentScreenSize, 0.01f, 1.0f);
		bPropagateToProxies = true;
	}
	else if (PropertyName == FName(TEXT("ComponentScreenSizeToUseSubSections")))
	{
		ComponentScreenSizeToUseSubSections = FMath::Clamp<float>(ComponentScreenSizeToUseSubSections, 0.01f, 1.0f);
		bPropagateToProxies = true;
	}
	else if (PropertyName == FName(TEXT("UseTessellationComponentScreenSizeFalloff")))
	{
		bPropagateToProxies = true;
	}
	else if (PropertyName == FName(TEXT("TessellationComponentScreenSizeFalloff")))
	{
		TessellationComponentScreenSizeFalloff = FMath::Clamp<float>(TessellationComponentScreenSizeFalloff, 0.01f, 1.0f);
		bPropagateToProxies = true;
	}
	else if (PropertyName == FName(TEXT("LODDistributionSetting")))
	{
		LODDistributionSetting = FMath::Clamp<float>(LODDistributionSetting, 1.0f, 10.0f);
		bPropagateToProxies = true;
	}
	else if (PropertyName == FName(TEXT("LOD0DistributionSetting")))
	{
		LOD0DistributionSetting = FMath::Clamp<float>(LOD0DistributionSetting, 1.0f, 10.0f);
		bPropagateToProxies = true;
	}
	else if (PropertyName == FName(TEXT("LOD0ScreenSize")))
	{
		LOD0ScreenSize = FMath::Clamp<float>(LOD0ScreenSize, 0.1f, 10.0f);
		bPropagateToProxies = true;
	}
	else if (PropertyName == FName(TEXT("CollisionMipLevel")))
	{
		CollisionMipLevel = FMath::Clamp<int32>(CollisionMipLevel, 0, FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1);
		bPropagateToProxies = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, DefaultPhysMaterial))
	{
		bPropagateToProxies = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, SimpleCollisionMipLevel))
	{
		SimpleCollisionMipLevel = FMath::Clamp<int32>(SimpleCollisionMipLevel, 0, FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1);
		bPropagateToProxies = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, bBakeMaterialPositionOffsetIntoCollision))
	{
		bPropagateToProxies = true;
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, OccluderGeometryLOD))
	{
		bPropagateToProxies = true;
	}
	else if (GIsEditor && PropertyName == FName(TEXT("StaticLightingResolution")))
	{
		StaticLightingResolution = ::AdjustStaticLightingResolution(StaticLightingResolution, NumSubsections, SubsectionSizeQuads, ComponentSizeQuads);
		bChangedLighting = true;
	}
	else if (GIsEditor && PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, StaticLightingLOD))
	{
		StaticLightingLOD = FMath::Clamp<int32>(StaticLightingLOD, 0, FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1);
		bChangedLighting = true;
	}
	else if (GIsEditor && PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, ExportLOD))
	{
		ExportLOD = FMath::Clamp<int32>(ExportLOD, 0, FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1);
	}
	else if (GIsEditor && PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, bUsedForNavigation))
	{
		bChangedNavRelevance = true;
	}
	else if (GIsEditor && PropertyName == GET_MEMBER_NAME_CHECKED(ALandscapeProxy, bFillCollisionUnderLandscapeForNavmesh))
	{
		bChangeRejectNavmeshUnder = true;
	}

	// Must do this *after* clamping values
	Super::PostEditChangeProperty(PropertyChangedEvent);

	bPropagateToProxies = bPropagateToProxies || bNeedsRecalcBoundingBox || bChangedLighting;

	if (Info != nullptr)
	{
		if (bPropagateToProxies)
		{
			// Propagate Event to Proxies...
			for (ALandscapeProxy* Proxy : Info->Proxies)
			{
				Proxy->GetSharedProperties(this);
				Proxy->PostEditChangeProperty(PropertyChangedEvent);
			}
		}

		// Update normals if DrawScale3D is changed
		if (MemberPropertyName == FName(TEXT("RelativeScale3D")))
		{
			FLandscapeEditDataInterface LandscapeEdit(Info);
			LandscapeEdit.RecalculateNormals();
		}

		if (bNeedsRecalcBoundingBox || ChangedMaterial || bChangedLighting || bChangedNavRelevance || bChangeRejectNavmeshUnder)
		{
			// We cannot iterate the XYtoComponentMap directly because reregistering components modifies the array.
			TArray<ULandscapeComponent*> AllComponents;
			Info->XYtoComponentMap.GenerateValueArray(AllComponents);
			for (ULandscapeComponent* Comp : AllComponents)
			{
				if (ensure(Comp))
				{
					Comp->Modify();

					if (bNeedsRecalcBoundingBox)
					{
						Comp->UpdateCachedBounds();
						Comp->UpdateBounds();
					}

					if (bChangedLighting)
					{
						Comp->InvalidateLightingCache();
					}

					if (bChangedNavRelevance)
					{
						Comp->UpdateNavigationRelevance();
					}

					if (bChangeRejectNavmeshUnder)
					{
						Comp->UpdateRejectNavmeshUnderneath();
					}
				}
			}

			if (ChangedMaterial)
			{
				UpdateAllComponentMaterialInstances();

				UWorld* World = GetWorld();

				if (World != nullptr && World->FeatureLevel <= ERHIFeatureLevel::ES3_1)
				{
					for (ULandscapeComponent * Component : LandscapeComponents)
					{
						if (Component != nullptr)
						{
							Component->CheckGenerateLandscapePlatformData(false, nullptr);
						}
					}
				}
			}
		}

		// Need to update Gizmo scene proxy
		if (bNeedsRecalcBoundingBox && GetWorld())
		{
			for (ALandscapeGizmoActiveActor* Gizmo : TActorRange<ALandscapeGizmoActiveActor>(GetWorld()))
			{
				Gizmo->MarkComponentsRenderStateDirty();
			}
		}

		// Must be done after the AActor::PostEditChange as we depend on the relinking of the landscapeInfo->LandscapeActor
		if (ChangedMaterial)
		{
			LandscapeMaterialChangedDelegate.Broadcast();
		}
	}

	PreEditLandscapeMaterial = nullptr;
	PreEditLandscapeHoleMaterial = nullptr;
	PreEditLandscapeMaterialsOverride.Empty();
}

void ALandscapeProxy::ChangedPhysMaterial()
{
	for (ULandscapeComponent* LandscapeComponent : LandscapeComponents)
	{
		if (LandscapeComponent && LandscapeComponent->IsRegistered())
		{
			ULandscapeHeightfieldCollisionComponent* CollisionComponent = LandscapeComponent->CollisionComponent.Get();
			if (CollisionComponent)
			{
				LandscapeComponent->UpdateCollisionLayerData();
				// Physical materials cooked into collision object, so we need to recreate it
				CollisionComponent->RecreateCollision();
			}
		}
	}
}

void ULandscapeComponent::SetLOD(bool bForcedLODChanged, int32 InLODValue)
{
	if (bForcedLODChanged)
	{
		ForcedLOD = InLODValue;
		if (ForcedLOD >= 0)
		{
			ForcedLOD = FMath::Clamp<int32>(ForcedLOD, 0, FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1);
		}
		else
		{
			ForcedLOD = -1;
		}
	}
	else
	{
		int32 MaxLOD = FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1;
		LODBias = FMath::Clamp<int32>(InLODValue, -MaxLOD, MaxLOD);
	}

	InvalidateLightingCache();
	MarkRenderStateDirty();

	// Update neighbor components
	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info)
	{
		FIntPoint ComponentBase = GetSectionBase() / ComponentSizeQuads;
		FIntPoint LandscapeKey[8] =
		{
			ComponentBase + FIntPoint(-1, -1),
			ComponentBase + FIntPoint(+0, -1),
			ComponentBase + FIntPoint(+1, -1),
			ComponentBase + FIntPoint(-1, +0),
			ComponentBase + FIntPoint(+1, +0),
			ComponentBase + FIntPoint(-1, +1),
			ComponentBase + FIntPoint(+0, +1),
			ComponentBase + FIntPoint(+1, +1)
		};

		for (int32 Idx = 0; Idx < 8; ++Idx)
		{
			ULandscapeComponent* Comp = Info->XYtoComponentMap.FindRef(LandscapeKey[Idx]);
			if (Comp)
			{
				Comp->Modify();
				Comp->InvalidateLightingCache();
				Comp->MarkRenderStateDirty();
			}
		}
	}
}

void ULandscapeComponent::PreEditChange(FProperty* PropertyThatWillChange)
{
	Super::PreEditChange(PropertyThatWillChange);
	if (GIsEditor && PropertyThatWillChange && (PropertyThatWillChange->GetFName() == GET_MEMBER_NAME_CHECKED(ULandscapeComponent, ForcedLOD) || PropertyThatWillChange->GetFName() == GET_MEMBER_NAME_CHECKED(ULandscapeComponent, LODBias)))
	{
		// PreEdit unregister component and re-register after PostEdit so we will lose XYtoComponentMap for this component
		ULandscapeInfo* Info = GetLandscapeInfo();
		if (Info)
		{
			FIntPoint ComponentKey = GetSectionBase() / ComponentSizeQuads;
			auto RegisteredComponent = Info->XYtoComponentMap.FindRef(ComponentKey);

			if (RegisteredComponent == nullptr)
			{
				Info->XYtoComponentMap.Add(ComponentKey, this);
			}
		}
	}
}

void ULandscapeComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

	if (PropertyName == FName(TEXT("OverrideMaterial")) || MemberPropertyName == FName(TEXT("OverrideMaterials")) || MemberPropertyName == FName(TEXT("MaterialPerLOD_Key")))
	{
		bool RecreateMaterialInstances = true;

		if (PropertyName == FName(TEXT("OverrideMaterials")) && PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
		{
			RecreateMaterialInstances = false;
		}

		if (RecreateMaterialInstances)
		{
			UpdateMaterialInstances();

			UWorld* World = GetWorld();

			if (World != nullptr && World->FeatureLevel <= ERHIFeatureLevel::ES3_1)
			{
				CheckGenerateLandscapePlatformData(false, nullptr);
			}
		}
	}
	else if (GIsEditor && (PropertyName == FName(TEXT("ForcedLOD")) || PropertyName == FName(TEXT("LODBias"))))
	{
		bool bForcedLODChanged = PropertyName == FName(TEXT("ForcedLOD"));
		SetLOD(bForcedLODChanged, bForcedLODChanged ? ForcedLOD : LODBias);
	}
	else if (GIsEditor && PropertyName == FName(TEXT("StaticLightingResolution")))
	{
		if (StaticLightingResolution > 0.0f)
		{
			StaticLightingResolution = ::AdjustStaticLightingResolution(StaticLightingResolution, NumSubsections, SubsectionSizeQuads, ComponentSizeQuads);
		}
		else
		{
			StaticLightingResolution = 0;
		}
		InvalidateLightingCache();
	}
	else if (GIsEditor && PropertyName == FName(TEXT("LightingLODBias")))
	{
		int32 MaxLOD = FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1;
		LightingLODBias = FMath::Clamp<int32>(LightingLODBias, -1, MaxLOD);
		InvalidateLightingCache();
	}
	else if (GIsEditor &&
		(PropertyName == GET_MEMBER_NAME_CHECKED(ULandscapeComponent, CollisionMipLevel) ||
		 PropertyName == GET_MEMBER_NAME_CHECKED(ULandscapeComponent, SimpleCollisionMipLevel)))
	{
		CollisionMipLevel = FMath::Clamp<int32>(CollisionMipLevel, 0, FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1);
		SimpleCollisionMipLevel = FMath::Clamp<int32>(SimpleCollisionMipLevel, 0, FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1);
		if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
		{
			DestroyCollisionData();
			UpdateCollisionData(); // Rebuild for new CollisionMipLevel
		}
	}

	// Must do this *after* clamping values
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

TSet<class ULandscapeComponent*> ULandscapeInfo::GetSelectedComponents() const
{
	return SelectedComponents;
}

TSet<class ULandscapeComponent*> ULandscapeInfo::GetSelectedRegionComponents() const
{
	return SelectedRegionComponents;
}

void ULandscapeInfo::UpdateSelectedComponents(TSet<ULandscapeComponent*>& NewComponents, bool bIsComponentwise /*=true*/)
{
	int32 InSelectType = bIsComponentwise ? FLandscapeEditToolRenderData::ST_COMPONENT : FLandscapeEditToolRenderData::ST_REGION;

	if (bIsComponentwise)
	{
		for (TSet<ULandscapeComponent*>::TIterator It(NewComponents); It; ++It)
		{
			ULandscapeComponent* Comp = *It;
			if ((Comp->EditToolRenderData.SelectedType & InSelectType) == 0)
			{
				Comp->Modify();
				int32 SelectedType = Comp->EditToolRenderData.SelectedType;
				SelectedType |= InSelectType;
				Comp->EditToolRenderData.UpdateSelectionMaterial(SelectedType, Comp);
				Comp->UpdateEditToolRenderData();
			}
		}

		// Remove the material from any old components that are no longer in the region
		TSet<ULandscapeComponent*> RemovedComponents = SelectedComponents.Difference(NewComponents);
		for (TSet<ULandscapeComponent*>::TIterator It(RemovedComponents); It; ++It)
		{
			ULandscapeComponent* Comp = *It;
			Comp->Modify();
			int32 SelectedType = Comp->EditToolRenderData.SelectedType;
			SelectedType &= ~InSelectType;
			Comp->EditToolRenderData.UpdateSelectionMaterial(SelectedType, Comp);
			Comp->UpdateEditToolRenderData();
		}
		SelectedComponents = NewComponents;
	}
	else
	{
		// Only add components...
		if (NewComponents.Num())
		{
			for (TSet<ULandscapeComponent*>::TIterator It(NewComponents); It; ++It)
			{
				ULandscapeComponent* Comp = *It;
				if ((Comp->EditToolRenderData.SelectedType & InSelectType) == 0)
				{
					Comp->Modify();
					int32 SelectedType = Comp->EditToolRenderData.SelectedType;
					SelectedType |= InSelectType;
					Comp->EditToolRenderData.UpdateSelectionMaterial(SelectedType, Comp);
					Comp->UpdateEditToolRenderData();
				}

				SelectedRegionComponents.Add(*It);
			}
		}
		else
		{
			// Remove the material from any old components that are no longer in the region
			for (TSet<ULandscapeComponent*>::TIterator It(SelectedRegionComponents); It; ++It)
			{
				ULandscapeComponent* Comp = *It;
				Comp->Modify();
				int32 SelectedType = Comp->EditToolRenderData.SelectedType;
				SelectedType &= ~InSelectType;
				Comp->EditToolRenderData.UpdateSelectionMaterial(SelectedType, Comp);
				Comp->UpdateEditToolRenderData();
			}
			SelectedRegionComponents = NewComponents;
		}
	}
}

void ULandscapeInfo::ClearSelectedRegion(bool bIsComponentwise /*= true*/)
{
	TSet<ULandscapeComponent*> NewComponents;
	UpdateSelectedComponents(NewComponents, bIsComponentwise);
	if (!bIsComponentwise)
	{
		SelectedRegion.Empty();
	}
}

void ULandscapeComponent::ReallocateWeightmaps(FLandscapeEditDataInterface* DataInterface, bool InCanUseEditingWeightmap, bool InSaveToTransactionBuffer, bool InInitPlatformDataAsync, bool InForceReallocate, ALandscapeProxy* InTargetProxy, TArray<UTexture2D*>* OutNewCreatedTextures)
{
	int32 NeededNewChannels = 0;
	ALandscapeProxy* TargetProxy = InTargetProxy ? InTargetProxy : GetLandscapeProxy();

	FGuid EditingLayerGUID = GetEditingLayerGUID();
	check(!TargetProxy->HasLayersContent() || !InCanUseEditingWeightmap || EditingLayerGUID.IsValid());
	FGuid TargetLayerGuid = InCanUseEditingWeightmap ? EditingLayerGUID : FGuid();

	TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = GetWeightmapLayerAllocations(InCanUseEditingWeightmap);
	TArray<UTexture2D*>& ComponentWeightmapTextures = GetWeightmapTextures(InCanUseEditingWeightmap);
	TArray<ULandscapeWeightmapUsage*>& ComponentWeightmapTexturesUsage = GetWeightmapTexturesUsage(InCanUseEditingWeightmap);

	// When force reallocating, skip tests to see if allocations are necessary based on Component's WeightmapLayeAllocInfo
	if (!InForceReallocate)
	{
		for (int32 LayerIdx = 0; LayerIdx < ComponentWeightmapLayerAllocations.Num(); LayerIdx++)
		{
			if (!ComponentWeightmapLayerAllocations[LayerIdx].IsAllocated())
			{
				NeededNewChannels++;
			}
		}

		// All channels allocated!
		if (NeededNewChannels == 0)
		{
			return;
		}
	}

	bool bMarkPackageDirty = DataInterface == nullptr ? true : DataInterface->GetShouldDirtyPackage();
	if (InSaveToTransactionBuffer)
	{
		Modify(bMarkPackageDirty);
		TargetProxy->Modify(bMarkPackageDirty);
	}

	if (!InForceReallocate)
	{
		// UE_LOG(LogLandscape, Log, TEXT("----------------------"));
		// UE_LOG(LogLandscape, Log, TEXT("Component %s needs %d layers (%d new)"), *GetName(), WeightmapLayerAllocations.Num(), NeededNewChannels);

		// See if our existing textures have sufficient space
		int32 ExistingTexAvailableChannels = 0;
		for (int32 TexIdx = 0; TexIdx < ComponentWeightmapTextures.Num(); TexIdx++)
		{
			ULandscapeWeightmapUsage* Usage = ComponentWeightmapTexturesUsage[TexIdx];
			check(Usage);
			check(Usage->LayerGuid == TargetLayerGuid);
			ExistingTexAvailableChannels += Usage->FreeChannelCount();

			if (ExistingTexAvailableChannels >= NeededNewChannels)
			{
				break;
			}
		}

		if (ExistingTexAvailableChannels >= NeededNewChannels)
		{
			// UE_LOG(LogLandscape, Log, TEXT("Existing texture has available channels"));

			// Allocate using our existing textures' spare channels.
			int32 CurrentAlloc = 0;
			for (int32 TexIdx = 0; TexIdx < ComponentWeightmapTextures.Num(); TexIdx++)
			{
				ULandscapeWeightmapUsage* Usage = ComponentWeightmapTexturesUsage[TexIdx];

				for (int32 ChanIdx = 0; ChanIdx < 4; ChanIdx++)
				{
					if (Usage->ChannelUsage[ChanIdx] == nullptr)
					{
						// Find next allocation to treat
						for (; CurrentAlloc < ComponentWeightmapLayerAllocations.Num(); ++CurrentAlloc)
						{
							FWeightmapLayerAllocationInfo& AllocInfo = ComponentWeightmapLayerAllocations[CurrentAlloc];

							if (!AllocInfo.IsAllocated())
							{
								break;
							}
						}

						FWeightmapLayerAllocationInfo& AllocInfo = ComponentWeightmapLayerAllocations[CurrentAlloc];
						check(!AllocInfo.IsAllocated());

						// Zero out the data for this texture channel
						if (DataInterface)
						{
							DataInterface->ZeroTextureChannel(ComponentWeightmapTextures[TexIdx], ChanIdx);
						}

						AllocInfo.WeightmapTextureIndex = TexIdx;
						AllocInfo.WeightmapTextureChannel = ChanIdx;

						if (InSaveToTransactionBuffer)
						{
							Usage->Modify(bMarkPackageDirty);
						}
						Usage->ChannelUsage[ChanIdx] = this;

						NeededNewChannels--;

						if (NeededNewChannels == 0)
						{
							return;
						}
					}
				}
			}
			// we should never get here.
			check(false);
		}
	}

	// UE_LOG(LogLandscape, Log, TEXT("Reallocating."));

	// We are totally reallocating the weightmap
	int32 TotalNeededChannels = ComponentWeightmapLayerAllocations.Num();
	int32 CurrentLayer = 0;
	TArray<UTexture2D*> NewWeightmapTextures;
	TArray<ULandscapeWeightmapUsage*> NewComponentWeightmapTexturesUsage;
	
	while (TotalNeededChannels > 0)
	{
		// UE_LOG(LogLandscape, Log, TEXT("Still need %d channels"), TotalNeededChannels);

		UTexture2D* CurrentWeightmapTexture = nullptr;
		ULandscapeWeightmapUsage* CurrentWeightmapUsage = nullptr;

		if (TotalNeededChannels < 4)
		{
			// UE_LOG(LogLandscape, Log, TEXT("Looking for nearest"));

			// see if we can find a suitable existing weightmap texture with sufficient channels
			int32 BestDistanceSquared = MAX_int32;
			for (auto& ItPair : TargetProxy->WeightmapUsageMap)
			{
				ULandscapeWeightmapUsage* TryWeightmapUsage = ItPair.Value;
				//
				if (TryWeightmapUsage->FreeChannelCount() >= TotalNeededChannels && TryWeightmapUsage->LayerGuid == TargetLayerGuid)
				{
					if (TryWeightmapUsage->IsEmpty())
					{
						CurrentWeightmapTexture = ItPair.Key;
						CurrentWeightmapUsage = TryWeightmapUsage;
						break;
					}
					else
					{
						// See if this candidate is closer than any others we've found
						for (int32 ChanIdx = 0; ChanIdx < ULandscapeWeightmapUsage::NumChannels; ChanIdx++)
						{
							if (TryWeightmapUsage->ChannelUsage[ChanIdx] != nullptr)
							{
								int32 TryDistanceSquared = (TryWeightmapUsage->ChannelUsage[ChanIdx]->GetSectionBase() - GetSectionBase()).SizeSquared();
								if (TryDistanceSquared < BestDistanceSquared)
								{
									CurrentWeightmapTexture = ItPair.Key;
									CurrentWeightmapUsage = TryWeightmapUsage;
									BestDistanceSquared = TryDistanceSquared;
								}
							}
						}
					}
				}
			}
		}

		bool NeedsUpdateResource = false;
		// No suitable weightmap texture
		if (CurrentWeightmapTexture == nullptr)
		{
			MarkPackageDirty();

			// Weightmap is sized the same as the component
			int32 WeightmapSize = (SubsectionSizeQuads + 1) * NumSubsections;

			// We need a new weightmap texture
			CurrentWeightmapTexture = TargetProxy->CreateLandscapeTexture(WeightmapSize, WeightmapSize, TEXTUREGROUP_Terrain_Weightmap, TSF_BGRA8);

			// Alloc dummy mips
			CreateEmptyTextureMips(CurrentWeightmapTexture, true);

			if (InInitPlatformDataAsync)
			{
				CurrentWeightmapTexture->BeginCachePlatformData();
				CurrentWeightmapTexture->ClearAllCachedCookedPlatformData();
			}
			else
			{
				CurrentWeightmapTexture->PostEditChange();
			}

			if (OutNewCreatedTextures != nullptr)
			{
				OutNewCreatedTextures->Add(CurrentWeightmapTexture);
			}

			// Store it in the usage map
			CurrentWeightmapUsage = TargetProxy->WeightmapUsageMap.Add(CurrentWeightmapTexture, TargetProxy->CreateWeightmapUsage());
			if (InSaveToTransactionBuffer)
			{
				CurrentWeightmapUsage->Modify(bMarkPackageDirty);
			}

			CurrentWeightmapUsage->LayerGuid = TargetLayerGuid;
			// UE_LOG(LogLandscape, Log, TEXT("Making a new texture %s"), *CurrentWeightmapTexture->GetName());
		}

		NewComponentWeightmapTexturesUsage.Add(CurrentWeightmapUsage);
		NewWeightmapTextures.Add(CurrentWeightmapTexture);

		for (int32 ChanIdx = 0; ChanIdx < 4 && TotalNeededChannels > 0; ChanIdx++)
		{
			// UE_LOG(LogLandscape, Log, TEXT("Finding allocation for layer %d"), CurrentLayer);

			if (CurrentWeightmapUsage->ChannelUsage[ChanIdx] == nullptr)
			{
				// Use this allocation
				FWeightmapLayerAllocationInfo& AllocInfo = ComponentWeightmapLayerAllocations[CurrentLayer];

				if (!AllocInfo.IsAllocated())
				{
					// New layer - zero out the data for this texture channel
					if (DataInterface)
					{
						DataInterface->ZeroTextureChannel(CurrentWeightmapTexture, ChanIdx);
						// UE_LOG(LogLandscape, Log, TEXT("Zeroing out channel %s.%d"), *CurrentWeightmapTexture->GetName(), ChanIdx);
					}
				}
				else
				{
					UTexture2D* OldWeightmapTexture = ComponentWeightmapTextures[AllocInfo.WeightmapTextureIndex];

					// Copy the data
					if (ensure(DataInterface != nullptr)) // it's not safe to skip the copy
					{
						DataInterface->CopyTextureChannel(CurrentWeightmapTexture, ChanIdx, OldWeightmapTexture, AllocInfo.WeightmapTextureChannel);
						DataInterface->ZeroTextureChannel(OldWeightmapTexture, AllocInfo.WeightmapTextureChannel);
						// UE_LOG(LogLandscape, Log, TEXT("Copying old channel (%s).%d to new channel (%s).%d"), *OldWeightmapTexture->GetName(), AllocInfo.WeightmapTextureChannel, *CurrentWeightmapTexture->GetName(), ChanIdx);
					}

					// Remove the old allocation
					ULandscapeWeightmapUsage* OldWeightmapUsage = ComponentWeightmapTexturesUsage[AllocInfo.WeightmapTextureIndex];
					if (InSaveToTransactionBuffer)
					{
						OldWeightmapUsage->Modify(bMarkPackageDirty);
					}
					OldWeightmapUsage->ChannelUsage[AllocInfo.WeightmapTextureChannel] = nullptr;
				}

				// Assign the new allocation
				if (InSaveToTransactionBuffer)
				{
					CurrentWeightmapUsage->Modify(bMarkPackageDirty);
				}
				CurrentWeightmapUsage->ChannelUsage[ChanIdx] = this;
				AllocInfo.WeightmapTextureIndex = NewWeightmapTextures.Num() - 1;
				AllocInfo.WeightmapTextureChannel = ChanIdx;
				CurrentLayer++;
				TotalNeededChannels--;
			}
		}
	}

	if (DataInterface)
	{
		// Update the mipmaps for the textures we edited
		for (int32 Idx = 0; Idx < NewWeightmapTextures.Num(); Idx++)
		{
			UTexture2D* WeightmapTexture = NewWeightmapTextures[Idx];
			FLandscapeTextureDataInfo* WeightmapDataInfo = DataInterface->GetTextureDataInfo(WeightmapTexture);

			int32 NumMips = WeightmapTexture->Source.GetNumMips();
			TArray<FColor*> WeightmapTextureMipData;
			WeightmapTextureMipData.AddUninitialized(NumMips);
			for (int32 MipIdx = 0; MipIdx < NumMips; MipIdx++)
			{
				WeightmapTextureMipData[MipIdx] = (FColor*)WeightmapDataInfo->GetMipData(MipIdx);
			}

			ULandscapeComponent::UpdateWeightmapMips(NumSubsections, SubsectionSizeQuads, WeightmapTexture, WeightmapTextureMipData, 0, 0, MAX_int32, MAX_int32, WeightmapDataInfo);
		}
	}

	// Replace the weightmap textures
	SetWeightmapTextures(MoveTemp(NewWeightmapTextures), InCanUseEditingWeightmap);
	SetWeightmapTexturesUsage(MoveTemp(NewComponentWeightmapTexturesUsage), InCanUseEditingWeightmap);	
}

void ALandscapeProxy::RemoveInvalidWeightmaps()
{
	if (GIsEditor)
	{
		for (TMap< UTexture2D*, ULandscapeWeightmapUsage* >::TIterator It(WeightmapUsageMap); It; ++It)
		{
			UTexture2D* Tex = It.Key();
			ULandscapeWeightmapUsage* Usage = It.Value();
			if (Usage->IsEmpty()) // Invalid Weight-map
			{
				if (Tex)
				{
					Tex->SetFlags(RF_Transactional);
					Tex->Modify();
					Tex->MarkPackageDirty();
					Tex->ClearFlags(RF_Standalone);
				}

				It.RemoveCurrent();
			}
		}

		// Remove Unused Weightmaps...
		for (int32 Idx = 0; Idx < LandscapeComponents.Num(); ++Idx)
		{
			ULandscapeComponent* Component = LandscapeComponents[Idx];
			Component->RemoveInvalidWeightmaps();
		}
	}
}

void ULandscapeComponent::RemoveInvalidWeightmaps()
{
	TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = GetWeightmapLayerAllocations();
	TArray<UTexture2D*>& ComponentWeightmapTextures = GetWeightmapTextures();
	TArray<ULandscapeWeightmapUsage*>& ComponentWeightmapTexturesUsage = GetWeightmapTexturesUsage();

	// Adjust WeightmapTextureIndex index for other layers
	TSet<int32> UnUsedTextureIndices;
	{
		TSet<int32> UsedTextureIndices;
		for (int32 LayerIdx = 0; LayerIdx < ComponentWeightmapLayerAllocations.Num(); LayerIdx++)
		{
			UsedTextureIndices.Add(ComponentWeightmapLayerAllocations[LayerIdx].WeightmapTextureIndex);
		}

		for (int32 WeightIdx = 0; WeightIdx < ComponentWeightmapTextures.Num(); ++WeightIdx)
		{
			if (!UsedTextureIndices.Contains(WeightIdx))
			{
				UnUsedTextureIndices.Add(WeightIdx);
			}
		}
	}

	int32 RemovedTextures = 0;
	for (int32 UnusedIndex : UnUsedTextureIndices)
	{
		int32 WeightmapTextureIndexToRemove = UnusedIndex - RemovedTextures;
		ComponentWeightmapTextures[WeightmapTextureIndexToRemove]->SetFlags(RF_Transactional);
		ComponentWeightmapTextures[WeightmapTextureIndexToRemove]->Modify();
		ComponentWeightmapTextures[WeightmapTextureIndexToRemove]->MarkPackageDirty();
		ComponentWeightmapTextures[WeightmapTextureIndexToRemove]->ClearFlags(RF_Standalone);
		ComponentWeightmapTextures.RemoveAt(WeightmapTextureIndexToRemove);

		ComponentWeightmapTexturesUsage.RemoveAt(WeightmapTextureIndexToRemove);

		// Adjust WeightmapTextureIndex index for other layers
		for (int32 LayerIdx = 0; LayerIdx < ComponentWeightmapLayerAllocations.Num(); LayerIdx++)
		{
			FWeightmapLayerAllocationInfo& Allocation = ComponentWeightmapLayerAllocations[LayerIdx];

			if (Allocation.WeightmapTextureIndex > WeightmapTextureIndexToRemove)
			{
				Allocation.WeightmapTextureIndex--;
			}

			checkSlow(Allocation.WeightmapTextureIndex < WeightmapTextures.Num());
		}
		RemovedTextures++;
	}
}

void ULandscapeComponent::InitHeightmapData(TArray<FColor>& Heights, bool bUpdateCollision)
{
	int32 ComponentSizeVerts = NumSubsections * (SubsectionSizeQuads + 1);

	if (Heights.Num() != FMath::Square(ComponentSizeVerts))
	{
		return;
	}

	// Handling old Height map....
	if (HeightmapTexture && HeightmapTexture->GetOutermost() != GetTransientPackage()
		&& HeightmapTexture->GetOutermost() == GetOutermost()
		&& HeightmapTexture->Source.GetSizeX() >= ComponentSizeVerts) // if Height map is not valid...
	{
		HeightmapTexture->SetFlags(RF_Transactional);
		HeightmapTexture->Modify();
		HeightmapTexture->MarkPackageDirty();
		HeightmapTexture->ClearFlags(RF_Standalone); // Delete if no reference...
	}

	// New Height map
	TArray<FColor*> HeightmapTextureMipData;
	// make sure the heightmap UVs are powers of two.
	int32 HeightmapSizeU = (1 << FMath::CeilLogTwo(ComponentSizeVerts));
	int32 HeightmapSizeV = (1 << FMath::CeilLogTwo(ComponentSizeVerts));

	// Height map construction
	SetHeightmap(GetLandscapeProxy()->CreateLandscapeTexture(HeightmapSizeU, HeightmapSizeV, TEXTUREGROUP_Terrain_Heightmap, TSF_BGRA8));

	int32 MipSubsectionSizeQuads = SubsectionSizeQuads;
	int32 MipSizeU = HeightmapSizeU;
	int32 MipSizeV = HeightmapSizeV;

	HeightmapScaleBias = FVector4(1.0f / (float)HeightmapSizeU, 1.0f / (float)HeightmapSizeV, 0.0f, 0.0f);

	int32 Mip = 0;
	while (MipSizeU > 1 && MipSizeV > 1 && MipSubsectionSizeQuads >= 1)
	{
		FColor* HeightmapTextureData = (FColor*)GetHeightmap()->Source.LockMip(Mip);
		if (Mip == 0)
		{
			FMemory::Memcpy(HeightmapTextureData, Heights.GetData(), MipSizeU*MipSizeV*sizeof(FColor));
		}
		else
		{
			FMemory::Memzero(HeightmapTextureData, MipSizeU*MipSizeV*sizeof(FColor));
		}
		HeightmapTextureMipData.Add(HeightmapTextureData);

		MipSizeU >>= 1;
		MipSizeV >>= 1;
		Mip++;

		MipSubsectionSizeQuads = ((MipSubsectionSizeQuads + 1) >> 1) - 1;
	}
	ULandscapeComponent::GenerateHeightmapMips(HeightmapTextureMipData);

	if (bUpdateCollision)
	{
		UpdateCollisionHeightData(
			HeightmapTextureMipData[CollisionMipLevel],
			SimpleCollisionMipLevel > CollisionMipLevel ? HeightmapTextureMipData[SimpleCollisionMipLevel] : nullptr);
	}

	for (int32 i = 0; i < HeightmapTextureMipData.Num(); i++)
	{
		GetHeightmap()->Source.UnlockMip(i);
	}
	GetHeightmap()->PostEditChange();
}

void ULandscapeComponent::InitWeightmapData(TArray<ULandscapeLayerInfoObject*>& LayerInfos, TArray<TArray<uint8> >& WeightmapData)
{
	if (LayerInfos.Num() != WeightmapData.Num() || LayerInfos.Num() <= 0)
	{
		return;
	}

	int32 ComponentSizeVerts = NumSubsections * (SubsectionSizeQuads + 1);

	// Validation..
	for (int32 Idx = 0; Idx < WeightmapData.Num(); ++Idx)
	{
		if (WeightmapData[Idx].Num() != FMath::Square(ComponentSizeVerts))
		{
			return;
		}
	}

	for (int32 Idx = 0; Idx < WeightmapTextures.Num(); ++Idx)
	{
		if (WeightmapTextures[Idx] && WeightmapTextures[Idx]->GetOutermost() != GetTransientPackage()
			&& WeightmapTextures[Idx]->GetOutermost() == GetOutermost()
			&& WeightmapTextures[Idx]->Source.GetSizeX() == ComponentSizeVerts)
		{
			WeightmapTextures[Idx]->SetFlags(RF_Transactional);
			WeightmapTextures[Idx]->Modify();
			WeightmapTextures[Idx]->MarkPackageDirty();
			WeightmapTextures[Idx]->ClearFlags(RF_Standalone); // Delete if no reference...
		}
	}
	WeightmapTextures.Empty();

	WeightmapLayerAllocations.Empty(LayerInfos.Num());
	for (int32 Idx = 0; Idx < LayerInfos.Num(); ++Idx)
	{
		new (WeightmapLayerAllocations)FWeightmapLayerAllocationInfo(LayerInfos[Idx]);
	}

	ReallocateWeightmaps();

	check(WeightmapLayerAllocations.Num() > 0 && WeightmapTextures.Num() > 0);

	int32 WeightmapSize = ComponentSizeVerts;
	WeightmapScaleBias = FVector4(1.0f / (float)WeightmapSize, 1.0f / (float)WeightmapSize, 0.5f / (float)WeightmapSize, 0.5f / (float)WeightmapSize);
	WeightmapSubsectionOffset = (float)(SubsectionSizeQuads + 1) / (float)WeightmapSize;

	TArray<void*> WeightmapDataPtrs;
	WeightmapDataPtrs.AddUninitialized(WeightmapTextures.Num());
	for (int32 WeightmapIdx = 0; WeightmapIdx < WeightmapTextures.Num(); ++WeightmapIdx)
	{
		WeightmapDataPtrs[WeightmapIdx] = WeightmapTextures[WeightmapIdx]->Source.LockMip(0);
	}

	for (int32 LayerIdx = 0; LayerIdx < WeightmapLayerAllocations.Num(); ++LayerIdx)
	{
		void* DestDataPtr = WeightmapDataPtrs[WeightmapLayerAllocations[LayerIdx].WeightmapTextureIndex];
		uint8* DestTextureData = (uint8*)DestDataPtr + ChannelOffsets[WeightmapLayerAllocations[LayerIdx].WeightmapTextureChannel];
		uint8* SrcTextureData = (uint8*)&WeightmapData[LayerIdx][0];

		for (int32 i = 0; i < WeightmapData[LayerIdx].Num(); i++)
		{
			DestTextureData[i * 4] = SrcTextureData[i];
		}
	}

	for (int32 Idx = 0; Idx < WeightmapTextures.Num(); Idx++)
	{
		UTexture2D* WeightmapTexture = WeightmapTextures[Idx];
		WeightmapTexture->Source.UnlockMip(0);
	}

	for (int32 Idx = 0; Idx < WeightmapTextures.Num(); Idx++)
	{
		UTexture2D* WeightmapTexture = WeightmapTextures[Idx];
		{
			const bool bShouldDirtyPackage = true;
			FLandscapeTextureDataInfo WeightmapDataInfo(WeightmapTexture, bShouldDirtyPackage);

			int32 NumMips = WeightmapTexture->Source.GetNumMips();
			TArray<FColor*> WeightmapTextureMipData;
			WeightmapTextureMipData.AddUninitialized(NumMips);
			for (int32 MipIdx = 0; MipIdx < NumMips; MipIdx++)
			{
				WeightmapTextureMipData[MipIdx] = (FColor*)WeightmapDataInfo.GetMipData(MipIdx);
			}

			ULandscapeComponent::UpdateWeightmapMips(NumSubsections, SubsectionSizeQuads, WeightmapTexture, WeightmapTextureMipData, 0, 0, MAX_int32, MAX_int32, &WeightmapDataInfo);
		}

		WeightmapTexture->PostEditChange();
	}

	FlushRenderingCommands();

	MaterialInstances.Empty(1);
	MaterialInstances.Add(nullptr);

	LODIndexToMaterialIndex.Empty(1);
	LODIndexToMaterialIndex.Add(0);

	//  TODO: need to update layer system?
}

#define MAX_LANDSCAPE_EXPORT_COMPONENTS_NUM		16
#define MAX_LANDSCAPE_PROP_TEXT_LENGTH			1024*1024*16

bool ALandscapeProxy::ShouldExport()
{
	if (!bIsMovingToLevel && LandscapeComponents.Num() > MAX_LANDSCAPE_EXPORT_COMPONENTS_NUM)
	{
		// Prompt to save startup packages
		if (EAppReturnType::Yes == FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
			NSLOCTEXT("UnrealEd", "LandscapeExport_Warning", "Landscape has large number({0}) of components, so it will use large amount memory to copy it to the clipboard. Do you want to proceed?"), FText::AsNumber(LandscapeComponents.Num()))))
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	return true;
}

bool ALandscapeProxy::ShouldImport(FString* ActorPropString, bool IsMovingToLevel)
{
	bIsMovingToLevel = IsMovingToLevel;
	if (!bIsMovingToLevel && ActorPropString && ActorPropString->Len() > MAX_LANDSCAPE_PROP_TEXT_LENGTH)
	{
		// Prompt to save startup packages
		if (EAppReturnType::Yes == FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
			NSLOCTEXT("UnrealEd", "LandscapeImport_Warning", "Landscape is about to import large amount memory ({0}MB) from the clipboard, which will take some time. Do you want to proceed?"), FText::AsNumber(ActorPropString->Len() >> 20))))
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	return true;
}

void ULandscapeComponent::ExportCustomProperties(FOutputDevice& Out, uint32 Indent)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}
	// Height map
	int32 NumVertices = FMath::Square(NumSubsections*(SubsectionSizeQuads + 1));
	FLandscapeComponentDataInterface DataInterface(this);
	TArray<FColor> Heightmap;
	DataInterface.GetHeightmapTextureData(Heightmap);
	check(Heightmap.Num() == NumVertices);

	Out.Logf(TEXT("%sCustomProperties LandscapeHeightData "), FCString::Spc(Indent));
	for (int32 i = 0; i < NumVertices; i++)
	{
		Out.Logf(TEXT("%x "), Heightmap[i].DWColor());
	}

	TArray<uint8> Weightmap;
	// Weight map
	Out.Logf(TEXT("LayerNum=%d "), WeightmapLayerAllocations.Num());
	for (int32 i = 0; i < WeightmapLayerAllocations.Num(); i++)
	{
		if (DataInterface.GetWeightmapTextureData(WeightmapLayerAllocations[i].LayerInfo, Weightmap))
		{
			Out.Logf(TEXT("LayerInfo=%s "), *WeightmapLayerAllocations[i].LayerInfo->GetPathName());
			for (int32 VertexIndex = 0; VertexIndex < NumVertices; VertexIndex++)
			{
				Out.Logf(TEXT("%x "), Weightmap[VertexIndex]);
			}
		}
	}

	Out.Logf(TEXT("\r\n"));
}


void ULandscapeComponent::ImportCustomProperties(const TCHAR* SourceText, FFeedbackContext* Warn)
{
	if (FParse::Command(&SourceText, TEXT("LandscapeHeightData")))
	{
		int32 NumVertices = FMath::Square(NumSubsections*(SubsectionSizeQuads + 1));

		TArray<FColor> Heights;
		Heights.Empty(NumVertices);
		Heights.AddZeroed(NumVertices);

		FParse::Next(&SourceText);
		int32 i = 0;
		TCHAR* StopStr;
		while (FChar::IsHexDigit(*SourceText))
		{
			if (i < NumVertices)
			{
				Heights[i++].DWColor() = FCString::Strtoi(SourceText, &StopStr, 16);
				while (FChar::IsHexDigit(*SourceText))
				{
					SourceText++;
				}
			}

			FParse::Next(&SourceText);
		}

		if (i != NumVertices)
		{
			Warn->Log(*NSLOCTEXT("Core", "SyntaxError", "Syntax Error").ToString());
		}

		int32 ComponentSizeVerts = NumSubsections * (SubsectionSizeQuads + 1);

		InitHeightmapData(Heights, false);

		// Weight maps
		int32 LayerNum = 0;
		if (FParse::Value(SourceText, TEXT("LayerNum="), LayerNum))
		{
			while (*SourceText && (!FChar::IsWhitespace(*SourceText)))
			{
				++SourceText;
			}
			FParse::Next(&SourceText);
		}

		if (LayerNum <= 0)
		{
			return;
		}

		// Init memory
		TArray<ULandscapeLayerInfoObject*> LayerInfos;
		LayerInfos.Empty(LayerNum);
		TArray<TArray<uint8>> WeightmapData;
		for (int32 LayerIndex = 0; LayerIndex < LayerNum; ++LayerIndex)
		{
			TArray<uint8> Weights;
			Weights.Empty(NumVertices);
			Weights.AddUninitialized(NumVertices);
			WeightmapData.Add(Weights);
		}

		int32 LayerIdx = 0;
		FString LayerInfoPath;
		while (*SourceText)
		{
			if (FParse::Value(SourceText, TEXT("LayerInfo="), LayerInfoPath))
			{
				LayerInfos.Add(LoadObject<ULandscapeLayerInfoObject>(nullptr, *LayerInfoPath));

				while (*SourceText && (!FChar::IsWhitespace(*SourceText)))
				{
					++SourceText;
				}
				FParse::Next(&SourceText);
				check(*SourceText);

				i = 0;
				while (FChar::IsHexDigit(*SourceText))
				{
					if (i < NumVertices)
					{
						(WeightmapData[LayerIdx])[i++] = (uint8)FCString::Strtoi(SourceText, &StopStr, 16);
						while (FChar::IsHexDigit(*SourceText))
						{
							SourceText++;
						}
					}
					FParse::Next(&SourceText);
				}

				if (i != NumVertices)
				{
					Warn->Log(*NSLOCTEXT("Core", "SyntaxError", "Syntax Error").ToString());
				}
				LayerIdx++;
			}
			else
			{
				break;
			}
		}

		InitWeightmapData(LayerInfos, WeightmapData);
	}
}

bool ALandscapeStreamingProxy::IsValidLandscapeActor(ALandscape* Landscape)
{
	if (Landscape)
	{
		if (!Landscape->HasAnyFlags(RF_BeginDestroyed))
		{
			if (LandscapeActor.IsNull() && !LandscapeGuid.IsValid())
			{
				return true; // always valid for newly created Proxy
			}
			if (((LandscapeActor && LandscapeActor == Landscape)
				|| (LandscapeActor.IsNull() && LandscapeGuid.IsValid() && LandscapeGuid == Landscape->GetLandscapeGuid()))
				&& ComponentSizeQuads == Landscape->ComponentSizeQuads
				&& NumSubsections == Landscape->NumSubsections
				&& SubsectionSizeQuads == Landscape->SubsectionSizeQuads)
			{
				return true;
			}
		}
	}
	return false;
}

/* Returns the list of layer names relevant to mobile platforms. Walks the material tree following feature level switch nodes. */
static void GetAllMobileRelevantLayerNames(TSet<FName>& OutLayerNames, UMaterial* InMaterial)
{
	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterIds;

	TArray<UMaterialExpression*> ES31Expressions;
	InMaterial->GetAllReferencedExpressions(ES31Expressions, nullptr, ERHIFeatureLevel::ES3_1);

	TArray<UMaterialExpression*> MobileExpressions = MoveTemp(ES31Expressions);

	for (UMaterialExpression* Expression : MobileExpressions)
	{
		UMaterialExpressionLandscapeLayerWeight* LayerWeightExpression = Cast<UMaterialExpressionLandscapeLayerWeight>(Expression);
		UMaterialExpressionLandscapeLayerSwitch* LayerSwitchExpression = Cast<UMaterialExpressionLandscapeLayerSwitch>(Expression);
		UMaterialExpressionLandscapeLayerSample* LayerSampleExpression = Cast<UMaterialExpressionLandscapeLayerSample>(Expression);
		UMaterialExpressionLandscapeLayerBlend*	LayerBlendExpression = Cast<UMaterialExpressionLandscapeLayerBlend>(Expression);
		UMaterialExpressionLandscapeVisibilityMask* VisibilityMaskExpression = Cast<UMaterialExpressionLandscapeVisibilityMask>(Expression);

		FMaterialParameterInfo BaseParameterInfo;
		BaseParameterInfo.Association = EMaterialParameterAssociation::GlobalParameter;
		BaseParameterInfo.Index = INDEX_NONE;

		if(LayerWeightExpression != nullptr)
		{
			LayerWeightExpression->GetAllParameterInfo(ParameterInfos, ParameterIds, BaseParameterInfo);
		}
		if (LayerSwitchExpression != nullptr)
		{
			LayerSwitchExpression->GetAllParameterInfo(ParameterInfos, ParameterIds, BaseParameterInfo);
		}
		if (LayerSampleExpression != nullptr)
		{
			LayerSampleExpression->GetAllParameterInfo(ParameterInfos, ParameterIds, BaseParameterInfo);
		}
		if (LayerBlendExpression != nullptr)
		{
			LayerBlendExpression->GetAllParameterInfo(ParameterInfos, ParameterIds, BaseParameterInfo);
		}
		if (VisibilityMaskExpression != nullptr)
		{
			VisibilityMaskExpression->GetAllParameterInfo(ParameterInfos, ParameterIds, BaseParameterInfo);
		}
	}

	for (FMaterialParameterInfo& Info : ParameterInfos)
	{
		OutLayerNames.Add(Info.Name);
	}
}

void ULandscapeComponent::GenerateMobileWeightmapLayerAllocations()
{
	TSet<FName> LayerNames;
	GetAllMobileRelevantLayerNames(LayerNames, GetLandscapeMaterial()->GetMaterial());
	MobileWeightmapLayerAllocations = WeightmapLayerAllocations.FilterByPredicate([&](const FWeightmapLayerAllocationInfo& Allocation) -> bool 
		{
			return Allocation.LayerInfo && LayerNames.Contains(Allocation.LayerInfo == ALandscapeProxy::VisibilityLayer ? UMaterialExpressionLandscapeVisibilityMask::ParameterName : Allocation.GetLayerName());
		}
	);
	MobileWeightmapLayerAllocations.StableSort(([&](const FWeightmapLayerAllocationInfo& A, const FWeightmapLayerAllocationInfo& B) -> bool
	{
		ULandscapeLayerInfoObject* LhsLayerInfo = A.LayerInfo;
		ULandscapeLayerInfoObject* RhsLayerInfo = B.LayerInfo;

		if (!LhsLayerInfo && !RhsLayerInfo) return false; // equally broken :P
		if (!LhsLayerInfo && RhsLayerInfo) return false; // broken layers sort to the end
		if (!RhsLayerInfo && LhsLayerInfo) return true;

		// Sort visibility layer to the front
		if (LhsLayerInfo == ALandscapeProxy::VisibilityLayer && RhsLayerInfo != ALandscapeProxy::VisibilityLayer) return true;
		if (RhsLayerInfo == ALandscapeProxy::VisibilityLayer && LhsLayerInfo != ALandscapeProxy::VisibilityLayer) return false;

		// Sort non-weight blended layers to the front so if we have exactly 3 layers, the 3rd is definitely weight-based.
		if (LhsLayerInfo->bNoWeightBlend && !RhsLayerInfo->bNoWeightBlend) return true;
		if (RhsLayerInfo->bNoWeightBlend && !LhsLayerInfo->bNoWeightBlend) return false;

		return false; // equal, preserve order
	}));
}

void ULandscapeComponent::GeneratePlatformPixelData()
{
	check(!IsTemplate());

	GenerateMobileWeightmapLayerAllocations();

	int32 WeightmapSize = (SubsectionSizeQuads + 1) * NumSubsections;

	MobileWeightmapTextures.Empty();

    UTexture2D* MobileWeightNormalmapTexture = GetLandscapeProxy()->CreateLandscapeTexture(WeightmapSize, WeightmapSize, TEXTUREGROUP_Terrain_Weightmap, TSF_BGRA8, nullptr, GMobileCompressLandscapeWeightMaps ? true : false );
	CreateEmptyTextureMips(MobileWeightNormalmapTexture, true);

	{
		FLandscapeTextureDataInterface LandscapeData;

		// copy normals into B/A channels
		LandscapeData.CopyTextureFromHeightmap(MobileWeightNormalmapTexture, 2, this, 2);
		LandscapeData.CopyTextureFromHeightmap(MobileWeightNormalmapTexture, 3, this, 3);

		UTexture2D* CurrentWeightmapTexture = MobileWeightNormalmapTexture;
		MobileWeightmapTextures.Add(CurrentWeightmapTexture);
		int32 CurrentChannel = 0;
		int32 RemainingChannels = 2;

		MobileBlendableLayerMask = 0;

		bool bAtLeastOneWeightBasedBlend = MobileWeightmapLayerAllocations.FindByPredicate([&](const FWeightmapLayerAllocationInfo& Allocation) -> bool { return !Allocation.LayerInfo->bNoWeightBlend; }) != nullptr;

		for (auto& Allocation : MobileWeightmapLayerAllocations)
		{
			if (Allocation.LayerInfo)
			{
				// If we can pack into 2 channels with the 3rd implied, track the mask for the weight blendable layers
				if (bAtLeastOneWeightBasedBlend && MobileWeightmapLayerAllocations.Num() <= 3)
				{
					MobileBlendableLayerMask |= (!Allocation.LayerInfo->bNoWeightBlend ? (1 << CurrentChannel) : 0);

					// we don't need to create a new texture for the 3rd layer
					if (RemainingChannels == 0)
					{
						Allocation.WeightmapTextureIndex = 0;
						Allocation.WeightmapTextureChannel = 2; // not a valid texture channel, but used for the mask.
						break;
					}
				}

				if (RemainingChannels == 0)
				{

					// create a new weightmap texture if we've run out of channels
					CurrentChannel = 0;
					RemainingChannels = 4;
                    CurrentWeightmapTexture = GetLandscapeProxy()->CreateLandscapeTexture(WeightmapSize, WeightmapSize, TEXTUREGROUP_Terrain_Weightmap, TSF_BGRA8, nullptr, GMobileCompressLandscapeWeightMaps ? true : false);
					CreateEmptyTextureMips(CurrentWeightmapTexture, true);
					MobileWeightmapTextures.Add(CurrentWeightmapTexture);
				}

				LandscapeData.CopyTextureFromWeightmap(CurrentWeightmapTexture, CurrentChannel, this, Allocation.LayerInfo);
				// update Allocation
				Allocation.WeightmapTextureIndex = MobileWeightmapTextures.Num() - 1;
				Allocation.WeightmapTextureChannel = CurrentChannel;
				CurrentChannel++;
				RemainingChannels--;
			}
		}
	}

	GDisableAutomaticTextureMaterialUpdateDependencies = true;
	for (int TextureIdx = 0; TextureIdx < MobileWeightmapTextures.Num(); TextureIdx++)
	{
		UTexture* Texture = MobileWeightmapTextures[TextureIdx];
		Texture->PostEditChange();

		// PostEditChange() will assign a random GUID to the texture, which leads to non-deterministic builds.
		// Compute a 128-bit hash based on the texture name and use that as a GUID to fix this issue.
		FTCHARToUTF8 Converted(*Texture->GetFullName());
		FMD5 MD5Gen;
		MD5Gen.Update((const uint8*)Converted.Get(), Converted.Length());
		uint32 Digest[4];
		MD5Gen.Final((uint8*)Digest);

		// FGuid::NewGuid() creates a version 4 UUID (at least on Windows), which will have the top 4 bits of the
		// second field set to 0100. We'll set the top bit to 1 in the GUID we create, to ensure that we can never
		// have a collision with textures which use implicitly generated GUIDs.
		Digest[1] |= 0x80000000;
		FGuid TextureGUID(Digest[0], Digest[1], Digest[2], Digest[3]);
		Texture->SetLightingGuid(TextureGUID);
	}
	GDisableAutomaticTextureMaterialUpdateDependencies = false;

	FLinearColor Masks[4];
	Masks[0] = FLinearColor(1, 0, 0, 0);
	Masks[1] = FLinearColor(0, 1, 0, 0);
	Masks[2] = FLinearColor(0, 0, 1, 0);
	Masks[3] = FLinearColor(0, 0, 0, 1);


	if (!GIsEditor)
	{
		// This path is used by game mode running with uncooked data, eg standalone executable Mobile Preview.
		// Game mode cannot create MICs, so we use a MaterialInstanceDynamic here.
		
		// Fallback to use non mobile materials if there is no mobile one
		if (MobileCombinationMaterialInstances.Num() == 0)
		{
			MobileCombinationMaterialInstances.Append(MaterialInstances);
		}

		MobileMaterialInterfaces.Reset();
		MobileMaterialInterfaces.Reserve(MobileCombinationMaterialInstances.Num());

		for (int32 MaterialIndex = 0; MaterialIndex < MobileCombinationMaterialInstances.Num(); ++MaterialIndex)
		{
			UMaterialInstanceDynamic* NewMobileMaterialInstance = UMaterialInstanceDynamic::Create(MobileCombinationMaterialInstances[MaterialIndex], this);

			// Set the layer mask
			for (const auto& Allocation : MobileWeightmapLayerAllocations)
			{
				if (Allocation.LayerInfo)
				{
					FName LayerName = Allocation.LayerInfo == ALandscapeProxy::VisibilityLayer ? UMaterialExpressionLandscapeVisibilityMask::ParameterName : Allocation.LayerInfo->LayerName;
					NewMobileMaterialInstance->SetVectorParameterValue(FName(*FString::Printf(TEXT("LayerMask_%s"), *LayerName.ToString())), Masks[Allocation.WeightmapTextureChannel]);
				}
			}

			for (int TextureIdx = 0; TextureIdx < MobileWeightmapTextures.Num(); TextureIdx++)
			{
				NewMobileMaterialInstance->SetTextureParameterValue(FName(*FString::Printf(TEXT("Weightmap%d"), TextureIdx)), MobileWeightmapTextures[TextureIdx]);
			}

			MobileMaterialInterfaces.Add(NewMobileMaterialInstance);
		}
	}
	else
	{
		// When cooking, we need to make a persistent MIC. In the editor we also do so in
		// case we start a Cook in Editor operation, which will reuse the MIC we create now.

		check(LODIndexToMaterialIndex.Num() > 0);		

		if (MaterialPerLOD.Num() == 0)
		{
			int32 MaxLOD = FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1;

			for (int32 LODIndex = 0; LODIndex <= MaxLOD; ++LODIndex)
			{
				UMaterialInterface* CurrentMaterial = GetLandscapeMaterial(LODIndex);

				if (MaterialPerLOD.Find(CurrentMaterial) == nullptr)
				{
					MaterialPerLOD.Add(CurrentMaterial, LODIndex);
				}
			}
		}

		MobileCombinationMaterialInstances.SetNumZeroed(MaterialPerLOD.Num());
		MobileMaterialInterfaces.Reset();
		MobileMaterialInterfaces.Reserve(MaterialPerLOD.Num());
		int8 MaterialIndex = 0;

		for (auto It = MaterialPerLOD.CreateConstIterator(); It; ++It)
		{
			const int8 MaterialLOD = It.Value();

			// Find or set a matching MIC in the Landscape's map.
			MobileCombinationMaterialInstances[MaterialIndex] = GetCombinationMaterial(nullptr, MobileWeightmapLayerAllocations, MaterialLOD, true);
			check(MobileCombinationMaterialInstances[MaterialIndex] != nullptr);

			UMaterialInstanceConstant* NewMobileMaterialInstance = NewObject<ULandscapeMaterialInstanceConstant>(this);

			NewMobileMaterialInstance->SetParentEditorOnly(MobileCombinationMaterialInstances[MaterialIndex]);

			// Set the layer mask
			for (const auto& Allocation : MobileWeightmapLayerAllocations)
			{
				if (Allocation.LayerInfo)
				{
					FName LayerName = Allocation.LayerInfo == ALandscapeProxy::VisibilityLayer ? UMaterialExpressionLandscapeVisibilityMask::ParameterName : Allocation.LayerInfo->LayerName;
					NewMobileMaterialInstance->SetVectorParameterValueEditorOnly(FName(*FString::Printf(TEXT("LayerMask_%s"), *LayerName.ToString())), Masks[Allocation.WeightmapTextureChannel]);
				}
			}

			for (int TextureIdx = 0; TextureIdx < MobileWeightmapTextures.Num(); TextureIdx++)
			{
				NewMobileMaterialInstance->SetTextureParameterValueEditorOnly(FName(*FString::Printf(TEXT("Weightmap%d"), TextureIdx)), MobileWeightmapTextures[TextureIdx]);
			}

			NewMobileMaterialInstance->PostEditChange();

			MobileMaterialInterfaces.Add(NewMobileMaterialInstance);
			++MaterialIndex;
		}
	}
}


// FBox2D version that uses integers
struct FIntBox2D
{
	FIntBox2D() : Min(INT32_MAX, INT32_MAX), Max(-INT32_MAX, -INT32_MAX) {}

	void Add(FIntPoint const& Pos) 
	{
		Min = FIntPoint(FMath::Min(Min.X, Pos.X), FMath::Min(Min.Y, Pos.Y));
		Max = FIntPoint(FMath::Max(Max.X, Pos.X), FMath::Max(Max.Y, Pos.Y));
	}

	void Add(FIntBox2D const& Rhs)
	{
		Min = FIntPoint(FMath::Min(Min.X, Rhs.Min.X), FMath::Min(Min.Y, Rhs.Min.Y));
		Max = FIntPoint(FMath::Max(Max.X, Rhs.Max.X), FMath::Max(Max.Y, Rhs.Max.Y));
	}

	bool Intersects(FIntBox2D const& Rhs)
	{
		return !((Rhs.Max.X < Min.X) || (Rhs.Min.X > Max.X) || (Rhs.Max.Y < Min.Y) || (Rhs.Min.Y > Max.Y));
	}

	FIntPoint Min;
	FIntPoint Max;
};

// Segment the hole map and return an array of hole bounding rectangles
void GetHoleBounds(int32 InSize, TArray<uint8> const& InVisibilityData, TArray<FIntBox2D>& OutHoleBounds)
{
	check(InVisibilityData.Num() == InSize * InSize);

	TArray<uint32> HoleSegmentLabels;
	HoleSegmentLabels.AddZeroed(InSize*InSize);

	TArray<uint32, TInlineAllocator<32>> LabelEquivalenceMap;
	LabelEquivalenceMap.Add(0);
	uint32 NextLabel = 1;

	// First pass fills HoleSegmentLabels with labels.
	for (int32 y = 0; y < InSize; ++y)
	{
		for (int32 x = 0; x < InSize; ++x)
		{
			const uint8 VisThreshold = 170;
			const bool bIsHole = InVisibilityData[y * InSize + x] >= VisThreshold;
			if (bIsHole)
			{
				uint8 WestLabel = (x > 0) ? HoleSegmentLabels[y * InSize + x - 1] : 0;
				uint8 NorthLabel = (y > 0) ? HoleSegmentLabels[(y - 1) * InSize + x] : 0;

				if (WestLabel != 0 && NorthLabel != 0 && WestLabel != NorthLabel)
				{
					uint32 MinLabel = FMath::Min(WestLabel, NorthLabel);
					uint32 MaxLabel = FMath::Max(WestLabel, NorthLabel);
					LabelEquivalenceMap[MaxLabel] = MinLabel;
					HoleSegmentLabels[y * InSize + x] = MinLabel;
				}
				else if (WestLabel != 0)
				{
					HoleSegmentLabels[y * InSize + x] = WestLabel;
				}
				else if (NorthLabel != 0)
				{
					HoleSegmentLabels[y * InSize + x] = NorthLabel;
				}
				else
				{
					LabelEquivalenceMap.Add(NextLabel);
					HoleSegmentLabels[y * InSize + x] = NextLabel++;
				}
			}
		}
	}

	// Resolve label equivalences.
	for (int32 Index = 0; Index < LabelEquivalenceMap.Num(); ++ Index)
	{
		int32 CommonIndex = Index;
		while (LabelEquivalenceMap[CommonIndex] != CommonIndex)
		{
			CommonIndex = LabelEquivalenceMap[CommonIndex];
		}
		LabelEquivalenceMap[Index] = CommonIndex;
	}

	// Flatten labels to be contiguous.
	int32 NumLabels = 0;
	for (int32 Index = 0; Index < LabelEquivalenceMap.Num(); ++Index)
	{
		if (LabelEquivalenceMap[Index] == Index)
		{
			LabelEquivalenceMap[Index] = NumLabels++;
		}
		else
		{
			LabelEquivalenceMap[Index] = LabelEquivalenceMap[LabelEquivalenceMap[Index]];
		}
	}

	// Second pass finds bounds for each label.
	// Could also write contiguous labels to HoleSegmentLabels here if we want to keep that info.
	OutHoleBounds.AddDefaulted(NumLabels);
	for (int32 y = 0; y < InSize - 1; ++y)
	{
		for (int32 x = 0; x < InSize - 1; ++x)
		{
			const int32 Index = InSize * y + x;
			const int32 Label = LabelEquivalenceMap[HoleSegmentLabels[Index]];
			OutHoleBounds[Label].Add(FIntPoint(x, y));
		}
	}
}

// Move vertex index up to the next location which obeys the condition:
// PosAt(VertexIndex, LodIndex) > PosAt(VertexIndex - 1, LodIndex + 1)
// Maths derived from pattern when analyzing a spreadsheet containing a dump of lod vertex positions.
inline void AlignVertexDown(int32 InLodIndex, int32& InOutVertexIndex)
{
	const int32 Offset = InOutVertexIndex & ((2 << InLodIndex) - 1);
	if (Offset < (1 << InLodIndex))
	{
		InOutVertexIndex -= Offset;
	}
}

// Move vertex index up to the next location which obeys the condition:
// PosAt(VertexIndex, LodIndex) < PosAt(VertexIndex + 1, LodIndex + 1)
// Maths derived from pattern when analyzing a spreadsheet containing a dump of lod vertex positions.
inline void AlignVertexUp(int32 InLodIndex, int32& InOutVertexIndex)
{
	const int32 Offset = (InOutVertexIndex + 1) & ((2 << InLodIndex) - 1);
	if (Offset > (1 << InLodIndex))
	{
		InOutVertexIndex += (1 << InLodIndex) - Offset;
	}
}

// Expand bounding rectangles from LodIndex-1 to LodIndex
void ExpandBoundsForLod(int32 InSize, int32 InLodIndex, TArray<FIntBox2D> const& InHoleBounds, TArray<FIntBox2D>& OutHoleBounds)
{
	OutHoleBounds.AddZeroed(InHoleBounds.Num());
	for (int32 i = 0; i < InHoleBounds.Num(); ++i)
	{
		// Expand
		const int32 ExpandDistance = (2 << InLodIndex) - 1;
		OutHoleBounds[i].Min.X = InHoleBounds[i].Min.X - ExpandDistance;
		OutHoleBounds[i].Min.Y = InHoleBounds[i].Min.Y - ExpandDistance;
		OutHoleBounds[i].Max.X = InHoleBounds[i].Max.X + ExpandDistance;
		OutHoleBounds[i].Max.Y = InHoleBounds[i].Max.Y + ExpandDistance;

		// Snap to continuous LOD borders so that consecutive vertices with different LODs don't overlap
		if (InLodIndex > 0)
		{
			AlignVertexDown(InLodIndex, OutHoleBounds[i].Min.X);
			AlignVertexDown(InLodIndex, OutHoleBounds[i].Min.Y);
			AlignVertexUp(InLodIndex, OutHoleBounds[i].Max.X);
			AlignVertexUp(InLodIndex, OutHoleBounds[i].Max.Y);
		}

		// Clamp to edges
		OutHoleBounds[i].Min.X = FMath::Max(OutHoleBounds[i].Min.X, 0);
		OutHoleBounds[i].Max.X = FMath::Min(OutHoleBounds[i].Max.X, InSize - 1);
		OutHoleBounds[i].Min.Y = FMath::Max(OutHoleBounds[i].Min.Y, 0);
		OutHoleBounds[i].Max.Y = FMath::Min(OutHoleBounds[i].Max.Y, InSize - 1);
	}
}

// Combine intersecting bounding rectangles into to form their bounding rectangles.
void CombineIntersectingBounds(TArray<FIntBox2D>& InOutHoleBounds)
{
	int i = 1;
	while (i < InOutHoleBounds.Num())
	{
		int j = i + 1;
		for (; j < InOutHoleBounds.Num(); ++j)
		{
			if (InOutHoleBounds[i].Intersects(InOutHoleBounds[j]))
			{
				InOutHoleBounds[i].Add(InOutHoleBounds[j]);
				InOutHoleBounds.RemoveAtSwap(j);
				break;
			}
		}
		if (j == InOutHoleBounds.Num())
		{
			++i;
		}
	}
}

// Build an array with an entry per vertex which contains the Lod at which that vertex falls inside a hole bounding rectangle. 
// This is the Lod at which we should clamp the vertex in the vertex shader.
void BuildHoleVertexLods(int32 InSize, int32 InNumLods, TArray<FIntBox2D> const& InHoleBounds, TArray<uint8>& OutHoleVertexLods)
{
	// Generate hole bounds for each Lod level from Lod0 InHoleBounds
	TArray< TArray<FIntBox2D> > HoleBoundsPerLevel;
	HoleBoundsPerLevel.AddDefaulted(InNumLods);
	HoleBoundsPerLevel[0] = InHoleBounds;

	for (int32 LodIndex = 1; LodIndex < InNumLods; ++LodIndex)
	{
		ExpandBoundsForLod(InSize, LodIndex, HoleBoundsPerLevel[LodIndex - 1], HoleBoundsPerLevel[LodIndex]);
	}

	for (int32 LodIndex = 0; LodIndex < InNumLods; ++LodIndex)
	{
		CombineIntersectingBounds(HoleBoundsPerLevel[LodIndex]);
	}

	// Initialize output to the max Lod
	OutHoleVertexLods.Init(InNumLods, InSize * InSize);

	// Fill by writing each Lod level in turn
	for (int32 LodIndex = InNumLods - 1; LodIndex >= 0; --LodIndex)
	{
		TArray<FIntBox2D> const& HoleBoundsAtLevel = HoleBoundsPerLevel[LodIndex];
		for (int32 BoxIndex = 1; BoxIndex < HoleBoundsAtLevel.Num(); ++BoxIndex)
		{
			const FIntPoint Min = HoleBoundsAtLevel[BoxIndex].Min;
			const FIntPoint Max = HoleBoundsAtLevel[BoxIndex].Max;
			
			for (int32 y = Min.Y; y <= Max.Y; ++y)
			{
				for (int32 x = Min.X; x <= Max.X; ++x)
				{
					OutHoleVertexLods[y * InSize + x] = LodIndex;
				}
			}
		}
	}
}

// Structure containing the hole render data required by the runtime rendering.
template <typename INDEX_TYPE>
struct FLandscapeHoleRenderData
{
	TArray<INDEX_TYPE> HoleIndices;
	int32 MinIndex;
	int32 MaxIndex;
};

// Serialize the hole render data.
template <typename INDEX_TYPE>
void SerializeHoleRenderData(FMemoryArchive& Ar, FLandscapeHoleRenderData<INDEX_TYPE>& InHoleRenderData)
{
	bool b16BitIndices = sizeof(INDEX_TYPE) == 2;
	Ar << b16BitIndices;

	Ar << InHoleRenderData.MinIndex;
	Ar << InHoleRenderData.MaxIndex;

	int32 HoleIndexCount = InHoleRenderData.HoleIndices.Num();
	Ar << HoleIndexCount;
	Ar.Serialize(InHoleRenderData.HoleIndices.GetData(), HoleIndexCount * sizeof(INDEX_TYPE));
}

// Take the processed hole map and generate the hole render data.
template <typename INDEX_TYPE>
void BuildHoleRenderData(int32 InNumSubsections, int32 InSubsectionSizeVerts, TArray<uint8> const& InVisibilityData, TArray<uint32>& InVertexToIndexMap, FLandscapeHoleRenderData<INDEX_TYPE>& OutHoleRenderData)
{
	const int32 SizeVerts = InNumSubsections * InSubsectionSizeVerts;
	const int32 SubsectionSizeQuads = InSubsectionSizeVerts - 1;
	const uint8 VisThreshold = 170;

	INDEX_TYPE MaxIndex = 0;
	INDEX_TYPE MinIndex = TNumericLimits<INDEX_TYPE>::Max();

	for (int32 SubY = 0; SubY < InNumSubsections; SubY++)
	{
		for (int32 SubX = 0; SubX < InNumSubsections; SubX++)
		{
			for (int32 y = 0; y < SubsectionSizeQuads; y++)
			{
				for (int32 x = 0; x < SubsectionSizeQuads; x++)
				{
					const int32 x0 = x;
					const int32 y0 = y;
					const int32 x1 = x + 1;
					const int32 y1 = y + 1;

					const int32 VertexIndex = (SubY * InSubsectionSizeVerts + y0) * SizeVerts + SubX * InSubsectionSizeVerts + x0;
					const bool bIsHole = InVisibilityData[VertexIndex] < VisThreshold;
					if (bIsHole)
					{
						INDEX_TYPE i00 = InVertexToIndexMap[FLandscapeVertexRef::GetVertexIndex(FLandscapeVertexRef(x0, y0, SubX, SubY), InNumSubsections, InSubsectionSizeVerts)];
						INDEX_TYPE i10 = InVertexToIndexMap[FLandscapeVertexRef::GetVertexIndex(FLandscapeVertexRef(x1, y0, SubX, SubY), InNumSubsections, InSubsectionSizeVerts)];
						INDEX_TYPE i11 = InVertexToIndexMap[FLandscapeVertexRef::GetVertexIndex(FLandscapeVertexRef(x1, y1, SubX, SubY), InNumSubsections, InSubsectionSizeVerts)];
						INDEX_TYPE i01 = InVertexToIndexMap[FLandscapeVertexRef::GetVertexIndex(FLandscapeVertexRef(x0, y1, SubX, SubY), InNumSubsections, InSubsectionSizeVerts)];

						OutHoleRenderData.HoleIndices.Add(i00);
						OutHoleRenderData.HoleIndices.Add(i11);
						OutHoleRenderData.HoleIndices.Add(i10);

						OutHoleRenderData.HoleIndices.Add(i00);
						OutHoleRenderData.HoleIndices.Add(i01);
						OutHoleRenderData.HoleIndices.Add(i11);

						// Update the min/max index ranges
						MaxIndex = FMath::Max<INDEX_TYPE>(MaxIndex, i00);
						MinIndex = FMath::Min<INDEX_TYPE>(MinIndex, i00);
						MaxIndex = FMath::Max<INDEX_TYPE>(MaxIndex, i10);
						MinIndex = FMath::Min<INDEX_TYPE>(MinIndex, i10);
						MaxIndex = FMath::Max<INDEX_TYPE>(MaxIndex, i11);
						MinIndex = FMath::Min<INDEX_TYPE>(MinIndex, i11);
						MaxIndex = FMath::Max<INDEX_TYPE>(MaxIndex, i01);
						MinIndex = FMath::Min<INDEX_TYPE>(MinIndex, i01);
					}
				}
			}
		}
	}

	OutHoleRenderData.MinIndex = MinIndex;
	OutHoleRenderData.MaxIndex = MaxIndex;
}

// Generates vertex and index buffer data from the component's height map and visibility textures.
// For use on mobile platforms that don't use vertex texture fetch for height or alpha testing for visibility.
void ULandscapeComponent::GeneratePlatformVertexData(const ITargetPlatform* TargetPlatform)
{
	if (IsTemplate())
	{
		return;
	}
	check(GetHeightmap());
	check(GetHeightmap()->Source.GetFormat() == TSF_BGRA8);

	TArray<uint8> NewPlatformData;
	FMemoryWriter PlatformAr(NewPlatformData);

	const int32 SubsectionSizeVerts = SubsectionSizeQuads + 1;
	const int32 MaxLOD = FMath::CeilLogTwo(SubsectionSizeVerts) - 1;
	const int32 NumMips = FMath::Min(LANDSCAPE_MAX_ES_LOD, GetHeightmap()->Source.GetNumMips());

	const float HeightmapSubsectionOffsetU = (float)(SubsectionSizeVerts) / (float)GetHeightmap()->Source.GetSizeX();
	const float HeightmapSubsectionOffsetV = (float)(SubsectionSizeVerts) / (float)GetHeightmap()->Source.GetSizeY();

	// Get the required height mip data
	TArray<TArray64<uint8>> HeightmapMipRawData;
	TArray64<FColor*> HeightmapMipData;
	for (int32 MipIdx = 0; MipIdx < NumMips; MipIdx++)
	{
		int32 MipSubsectionSizeVerts = (SubsectionSizeVerts) >> MipIdx;
		if (MipSubsectionSizeVerts > 1)
		{
			new(HeightmapMipRawData) TArray64<uint8>();
			GetHeightmap()->Source.GetMipData(HeightmapMipRawData.Last(), MipIdx);
			HeightmapMipData.Add((FColor*)HeightmapMipRawData.Last().GetData());
		}
	}

	// Get any hole data
	int32 NumHoleLods = 0;
	TArray< uint8 > VisibilityData;
	if (ComponentHasVisibilityPainted() && GetLandscapeProxy()->bMeshHoles)
	{
		TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapLayerAllocations = GetWeightmapLayerAllocations();
		for (int32 AllocIdx = 0; AllocIdx < ComponentWeightmapLayerAllocations.Num(); AllocIdx++)
		{
			FWeightmapLayerAllocationInfo& AllocInfo = ComponentWeightmapLayerAllocations[AllocIdx];
			if (AllocInfo.LayerInfo == ALandscapeProxy::VisibilityLayer)
			{
				NumHoleLods = FMath::Clamp<int32>(GetLandscapeProxy()->MeshHolesMaxLod, 1, NumMips);

				FLandscapeComponentDataInterface CDI(this, 0);
				CDI.GetWeightmapTextureData(AllocInfo.LayerInfo, VisibilityData);
				break;
			}
		}
	}

	// Layout index buffer to determine best vertex order.
	// This vertex layout code is duplicated in FLandscapeSharedBuffers::CreateIndexBuffers() to create matching index buffers at runtime.
	const int32 NumVertices = FMath::Square(SubsectionSizeVerts * NumSubsections);
	
	TArray<uint32> VertexToIndexMap;
	VertexToIndexMap.AddUninitialized(NumVertices);
	FMemory::Memset(VertexToIndexMap.GetData(), 0xFF, NumVertices * sizeof(uint32));
	
	TArray<FLandscapeVertexRef> VertexOrder;
	VertexOrder.Empty(NumVertices);

	const bool bStreamLandscapeMeshLODs = TargetPlatform && TargetPlatform->SupportsFeature(ETargetPlatformFeatures::LandscapeMeshLODStreaming);
	const int32 MaxLODClamp = FMath::Min((uint32)GetLandscapeProxy()->MaxLODLevel, (uint32)MAX_MESH_LOD_COUNT - 1u);
	const int32 NumStreamingLODs = bStreamLandscapeMeshLODs ? FMath::Min(MaxLOD, MaxLODClamp) : 0;
	TArray<int32> StreamingLODVertStartOffsets;
	StreamingLODVertStartOffsets.AddUninitialized(NumStreamingLODs);

	for (int32 Mip = MaxLOD; Mip >= 0; Mip--)
	{
		int32 LodSubsectionSizeQuads = (SubsectionSizeVerts >> Mip) - 1;
		float MipRatio = (float)SubsectionSizeQuads / (float)LodSubsectionSizeQuads; // Morph current MIP to base MIP

		if (Mip < NumStreamingLODs)
		{
			StreamingLODVertStartOffsets[Mip] = VertexOrder.Num();
		}

		for (int32 SubY = 0; SubY < NumSubsections; SubY++)
		{
			for (int32 SubX = 0; SubX < NumSubsections; SubX++)
			{
				for (int32 Y = 0; Y < LodSubsectionSizeQuads; Y++)
				{
					for (int32 X = 0; X < LodSubsectionSizeQuads; X++)
					{
						for (int32 CornerId = 0; CornerId < 4; CornerId++)
						{
							const int32 CornerX = FMath::RoundToInt((float)(X + (CornerId & 1)) * MipRatio);
							const int32 CornerY = FMath::RoundToInt((float)(Y + (CornerId >> 1)) * MipRatio);
							const FLandscapeVertexRef VertexRef(CornerX, CornerY, SubX, SubY);

							const int32 VertexIndex = FLandscapeVertexRef::GetVertexIndex(VertexRef, NumSubsections, SubsectionSizeVerts);
							if (VertexToIndexMap[VertexIndex] == 0xFFFFFFFF)
							{
								VertexToIndexMap[VertexIndex] = VertexOrder.Num();
								VertexOrder.Add(VertexRef);
							}
						}
					}
				}
			}
		}
	}

	if (VertexOrder.Num() != NumVertices)
	{
		UE_LOG(LogLandscape, Warning, TEXT("VertexOrder count of %d did not match expected size of %d"), VertexOrder.Num(), NumVertices);
	}

	// Build and serialize hole render data which includes a unique index buffer with the holes missing.
	// This fills HoleVertexLods which is required for filling the vertex data.
	TArray<uint8> HoleVertexLods;
	PlatformAr << NumHoleLods;
	if (NumHoleLods > 0)
	{
		TArray<FIntBox2D> HoleBounds;
		GetHoleBounds(SubsectionSizeVerts * NumSubsections, VisibilityData, HoleBounds);
		BuildHoleVertexLods(SubsectionSizeVerts * NumSubsections, NumHoleLods, HoleBounds, HoleVertexLods);

		if (NumVertices <= UINT16_MAX)
		{
			FLandscapeHoleRenderData<uint16> HoleRenderData;
			BuildHoleRenderData(NumSubsections, SubsectionSizeVerts, VisibilityData, VertexToIndexMap, HoleRenderData);
			SerializeHoleRenderData(PlatformAr, HoleRenderData);
		}
		else
		{
			FLandscapeHoleRenderData<uint32> HoleRenderData;
			BuildHoleRenderData(NumSubsections, SubsectionSizeVerts, VisibilityData, VertexToIndexMap, HoleRenderData);
			SerializeHoleRenderData(PlatformAr, HoleRenderData);
		}
	}

	// Fill in the vertices in the specified order.
	const int32 SizeVerts = SubsectionSizeVerts * NumSubsections;
	int32 NumInlineMobileVertices = NumStreamingLODs > 0 ? StreamingLODVertStartOffsets.Last() : FMath::Square(SizeVerts);
	TArray<FLandscapeMobileVertex> InlineMobileVertices;
	InlineMobileVertices.AddZeroed(NumInlineMobileVertices);
	FLandscapeMobileVertex* DstVert = InlineMobileVertices.GetData();

	int32 StreamingLODIdx = NumStreamingLODs - 1;
	TArray<TArray<uint8>> StreamingLODData;
	StreamingLODData.Empty(NumStreamingLODs);
	StreamingLODData.AddDefaulted(NumStreamingLODs);

	for (int32 Idx = 0; Idx < NumVertices; Idx++)
	{
		if (StreamingLODIdx >= 0
			&& (StreamingLODIdx >= NumHoleLods - 1)
			&& Idx >= StreamingLODVertStartOffsets[StreamingLODIdx])
		{
			const int32 EndIdx = StreamingLODIdx - 1 < 0 || StreamingLODIdx == NumHoleLods - 1 ?
				FMath::Square(SizeVerts) :
				StreamingLODVertStartOffsets[StreamingLODIdx - 1];
			const int32 NumVerts = EndIdx - StreamingLODVertStartOffsets[StreamingLODIdx];
			TArray<uint8>& StreamingLOD = StreamingLODData[StreamingLODIdx];
			StreamingLOD.Empty(NumVerts * sizeof(FLandscapeMobileVertex));
			StreamingLOD.AddZeroed(NumVerts * sizeof(FLandscapeMobileVertex));
			DstVert = (FLandscapeMobileVertex*)StreamingLOD.GetData();
			--StreamingLODIdx;
		}

		// Store XY position info
		const int32 X = VertexOrder[Idx].X;
		const int32 Y = VertexOrder[Idx].Y;
		const int32 SubX = VertexOrder[Idx].SubX;
		const int32 SubY = VertexOrder[Idx].SubY;

		DstVert->Position[0] = X;
		DstVert->Position[1] = Y;
		DstVert->Position[2] = (SubX << 4) | SubY;

		// Store hole info
		const int32 VertexIndex = (SubY * SubsectionSizeVerts + Y) * SizeVerts + SubX * SubsectionSizeVerts + X;
		const int32 HoleVertexLod = (NumHoleLods > 0) ? HoleVertexLods[VertexIndex] : 0;
		const int32 HoleMaxLod = (NumHoleLods > 0) ? NumHoleLods : 0;

		DstVert->Position[3] = (HoleMaxLod << 4) | HoleVertexLod;

		// Calculate min/max height for packing
		TArray<int32> MipHeights;
		MipHeights.AddZeroed(HeightmapMipData.Num());
		int32 LastIndex = 0;
		uint16 MaxHeight = 0, MinHeight = 65535;

		float HeightmapScaleBiasZ = HeightmapScaleBias.Z + HeightmapSubsectionOffsetU * (float)SubX;
		float HeightmapScaleBiasW = HeightmapScaleBias.W + HeightmapSubsectionOffsetV * (float)SubY;
		int32 BaseMipOfsX = FMath::RoundToInt(HeightmapScaleBiasZ * (float)GetHeightmap()->Source.GetSizeX());
		int32 BaseMipOfsY = FMath::RoundToInt(HeightmapScaleBiasW * (float)GetHeightmap()->Source.GetSizeY());

		for (int32 Mip = 0; Mip < HeightmapMipData.Num(); ++Mip)
		{
			int32 MipSizeX = GetHeightmap()->Source.GetSizeX() >> Mip;

			int32 CurrentMipOfsX = BaseMipOfsX >> Mip;
			int32 CurrentMipOfsY = BaseMipOfsY >> Mip;

			int32 MipX = X >> Mip;
			int32 MipY = Y >> Mip;

			FColor* CurrentMipSrcRow = HeightmapMipData[Mip] + (CurrentMipOfsY + MipY) * MipSizeX + CurrentMipOfsX;
			uint16 Height = CurrentMipSrcRow[MipX].R << 8 | CurrentMipSrcRow[MipX].G;

			MipHeights[Mip] = Height;
			MaxHeight = FMath::Max(MaxHeight, Height);
			MinHeight = FMath::Min(MinHeight, Height);
		}

		// Quantize min/max height so we can store each in 8 bits
		MaxHeight = (MaxHeight + 255) & (~255);
		MinHeight = (MinHeight) & (~255);

		DstVert->LODHeights[0] = MinHeight >> 8;
		DstVert->LODHeights[1] = MaxHeight >> 8;

		// Now quantize the mip heights to steps between MinHeight and MaxHeight
		for (int32 Mip = 0; Mip < HeightmapMipData.Num(); ++Mip)
		{
			check(Mip < 6);
			DstVert->LODHeights[2 + Mip] = FMath::RoundToInt(float(MipHeights[Mip] - MinHeight) / (MaxHeight - MinHeight) * 255);
		}

		DstVert++;
	}

	// Serialize vertex buffer
	PlatformAr << NumInlineMobileVertices;
	PlatformAr.Serialize(InlineMobileVertices.GetData(), NumInlineMobileVertices*sizeof(FLandscapeMobileVertex));

	// Generate occlusion mesh
	TArray<FVector> OccluderVertices;
	const int32 OcclusionMeshMip = FMath::Clamp<int32>(GetLandscapeProxy()->OccluderGeometryLOD, -1, HeightmapMipData.Num() - 1);

	if (OcclusionMeshMip >= 0 && (!TargetPlatform || TargetPlatform->SupportsFeature(ETargetPlatformFeatures::SoftwareOcclusion)))
	{
		int32 LodSubsectionSizeQuads = (SubsectionSizeVerts >> OcclusionMeshMip) - 1;
		float MipRatio = (float)SubsectionSizeQuads / (float)LodSubsectionSizeQuads;
		
		for (int32 SubY = 0; SubY < NumSubsections; SubY++)
		{
			for (int32 SubX = 0; SubX < NumSubsections; SubX++)
			{
				float HeightmapScaleBiasZ = HeightmapScaleBias.Z + HeightmapSubsectionOffsetU * (float)SubX;
				float HeightmapScaleBiasW = HeightmapScaleBias.W + HeightmapSubsectionOffsetV * (float)SubY;
				int32 BaseMipOfsX = FMath::RoundToInt(HeightmapScaleBiasZ * (float)GetHeightmap()->Source.GetSizeX());
				int32 BaseMipOfsY = FMath::RoundToInt(HeightmapScaleBiasW * (float)GetHeightmap()->Source.GetSizeY());

				for (int32 y = 0; y <= LodSubsectionSizeQuads; y++)
				{
					for (int32 x = 0; x <= LodSubsectionSizeQuads; x++)
					{
						int32 MipSizeX = GetHeightmap()->Source.GetSizeX() >> OcclusionMeshMip;

						int32 CurrentMipOfsX = BaseMipOfsX >> OcclusionMeshMip;
						int32 CurrentMipOfsY = BaseMipOfsY >> OcclusionMeshMip;
												
						FColor* CurrentMipSrcRow = HeightmapMipData[OcclusionMeshMip] + (CurrentMipOfsY + y) * MipSizeX + CurrentMipOfsX;
						uint16 Height = CurrentMipSrcRow[x].R << 8 | CurrentMipSrcRow[x].G;

						FVector VtxPos = FVector(x*MipRatio + SubX * SubsectionSizeQuads, y*MipRatio + SubY * SubsectionSizeQuads, ((float)Height - 32768.f) * LANDSCAPE_ZSCALE);
						OccluderVertices.Add(VtxPos);
					}
				}
			}
		}
	}

	int32 NumOccluderVerices = OccluderVertices.Num();
	PlatformAr << NumOccluderVerices;
	PlatformAr.Serialize(OccluderVertices.GetData(), NumOccluderVerices*sizeof(FVector));
	
	// Copy to PlatformData as Compressed
	PlatformData.InitializeFromUncompressedData(NewPlatformData, StreamingLODData);
}

UTexture2D* ALandscapeProxy::CreateLandscapeTexture(int32 InSizeX, int32 InSizeY, TextureGroup InLODGroup, ETextureSourceFormat InFormat, UObject* OptionalOverrideOuter, bool bCompress) const
{
	UObject* TexOuter = OptionalOverrideOuter ? OptionalOverrideOuter : const_cast<ALandscapeProxy*>(this);
	UTexture2D* NewTexture = NewObject<UTexture2D>(TexOuter);
	NewTexture->Source.Init2DWithMipChain(InSizeX, InSizeY, InFormat);
	NewTexture->SRGB = false;
	NewTexture->CompressionNone = !bCompress;
	NewTexture->MipGenSettings = TMGS_LeaveExistingMips;
	NewTexture->AddressX = TA_Clamp;
	NewTexture->AddressY = TA_Clamp;
	NewTexture->LODGroup = InLODGroup;

	return NewTexture;
}

UTexture2D* ALandscapeProxy::CreateLandscapeToolTexture(int32 InSizeX, int32 InSizeY, TextureGroup InLODGroup, ETextureSourceFormat InFormat) const
{
	UObject* TexOuter = const_cast<ALandscapeProxy*>(this);
	UTexture2D* NewTexture = NewObject<UTexture2D>(TexOuter);
	NewTexture->Source.Init(InSizeX, InSizeY, 1, 1, InFormat);
	NewTexture->SRGB = false;
	NewTexture->CompressionNone = true;
	NewTexture->MipGenSettings = TMGS_NoMipmaps;
	NewTexture->AddressX = TA_Clamp;
	NewTexture->AddressY = TA_Clamp;
	NewTexture->LODGroup = InLODGroup;

	return NewTexture;
}

ULandscapeWeightmapUsage* ALandscapeProxy::CreateWeightmapUsage()
{
	return NewObject<ULandscapeWeightmapUsage>(this, ULandscapeWeightmapUsage::StaticClass(), NAME_None, RF_Transactional);
}

void ALandscapeProxy::RemoveOverlappingComponent(ULandscapeComponent* Component)
{
	Modify();
	Component->Modify();
	if (Component->CollisionComponent.IsValid() && (Component->CollisionComponent->RenderComponent.Get() == Component || Component->CollisionComponent->RenderComponent.IsNull()))
	{
		Component->CollisionComponent->Modify();
		CollisionComponents.Remove(Component->CollisionComponent.Get());
		Component->CollisionComponent.Get()->DestroyComponent();
	}
	LandscapeComponents.Remove(Component);
	Component->DestroyComponent();
}

TArray<FLinearColor> ALandscapeProxy::SampleRTData(UTextureRenderTarget2D* InRenderTarget, FLinearColor InRect)
{

	if (!InRenderTarget)
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("SampleRTData_InvalidRenderTarget", "SampleRTData: Render Target must be non-null."));
		return { FLinearColor(0,0,0,0) };
	}
	else if (!InRenderTarget->Resource)
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("SampleRTData_ReleasedRenderTarget", "SampleRTData: Render Target has been released."));
		return { FLinearColor(0,0,0,0) };
	}
	else
	{
		ETextureRenderTargetFormat format = (InRenderTarget->RenderTargetFormat);

		if ((format == (RTF_RGBA16f)) || (format == (RTF_RGBA32f)) || (format == (RTF_RGBA8)))
		{

			FTextureRenderTargetResource* RTResource = InRenderTarget->GameThread_GetRenderTargetResource();

			InRect.R = FMath::Clamp(int(InRect.R), 0, InRenderTarget->SizeX - 1);
			InRect.G = FMath::Clamp(int(InRect.G), 0, InRenderTarget->SizeY - 1);
			InRect.B = FMath::Clamp(int(InRect.B), int(InRect.R + 1), InRenderTarget->SizeX);
			InRect.A = FMath::Clamp(int(InRect.A), int(InRect.G + 1), InRenderTarget->SizeY);
			FIntRect Rect = FIntRect(InRect.R, InRect.G, InRect.B, InRect.A);

			FReadSurfaceDataFlags ReadPixelFlags(RCM_MinMax);

			TArray<FColor> OutLDR;
			TArray<FLinearColor> OutHDR;

			TArray<FLinearColor> OutVals;

			bool ishdr = ((format == (RTF_R16f)) || (format == (RTF_RG16f)) || (format == (RTF_RGBA16f)) || (format == (RTF_R32f)) || (format == (RTF_RG32f)) || (format == (RTF_RGBA32f)));

			if (!ishdr)
			{
				RTResource->ReadPixels(OutLDR, ReadPixelFlags, Rect);
				for (auto i : OutLDR)
				{
					OutVals.Add(FLinearColor(float(i.R), float(i.G), float(i.B), float(i.A)) / 255.0f);
				}
			}
			else
			{
				RTResource->ReadLinearColorPixels(OutHDR, ReadPixelFlags, Rect);
				return OutHDR;
			}

			return OutVals;
		}
	}
	FMessageLog("Blueprint").Warning(LOCTEXT("SampleRTData_InvalidTexture", "SampleRTData: Currently only 4 channel formats are supported: RTF_RGBA8, RTF_RGBA16f, and RTF_RGBA32f."));

	return { FLinearColor(0,0,0,0) };
}

bool ALandscapeProxy::LandscapeImportHeightmapFromRenderTarget(UTextureRenderTarget2D* InRenderTarget, bool InImportHeightFromRGChannel)
{
	uint64 StartCycle = FPlatformTime::Cycles64();

	ALandscape* Landscape = GetLandscapeActor();
	if (Landscape == nullptr)
	{
		FMessageLog("Blueprint").Error(LOCTEXT("LandscapeImportHeightmapFromRenderTarget_NullLandscape", "LandscapeImportHeightmapFromRenderTarget: Landscape must be non-null."));
		return false;
	}

	if (Landscape->HasLayersContent())
	{
		//todo: Support an edit layer name input parameter to support import to edit layers.
		FMessageLog("Blueprint").Error(LOCTEXT("LandscapeImportHeightmapFromRenderTarget_LandscapeLayersNotSupported", "LandscapeImportHeightmapFromRenderTarget: Cannot import to landscape with Edit Layers enabled."));
		return false;
	}

	int32 MinX, MinY, MaxX, MaxY;
	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();

	if (!LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		FMessageLog("Blueprint").Error(LOCTEXT("LandscapeImportHeightmapFromRenderTarget_InvalidLandscapeExtends", "LandscapeImportHeightmapFromRenderTarget: The landscape min extends are invalid."));
		return false;
	}

	if (InRenderTarget == nullptr || InRenderTarget->Resource == nullptr)
	{
		FMessageLog("Blueprint").Error(LOCTEXT("LandscapeImportHeightmapFromRenderTarget_InvalidRT", "LandscapeImportHeightmapFromRenderTarget: Render Target must be non null and not released."));
		return false;
	}

	FTextureRenderTargetResource* RenderTargetResource = InRenderTarget->GameThread_GetRenderTargetResource();
	FIntRect SampleRect = FIntRect(0, 0, FMath::Min(1 + MaxX - MinX, InRenderTarget->SizeX), FMath::Min(1 + MaxY - MinY, InRenderTarget->SizeY));

	TArray<uint16> HeightData;

	switch (InRenderTarget->RenderTargetFormat)
	{
		case RTF_RGBA16f:
		case RTF_RGBA32f:
		{
			TArray<FLinearColor> OutputRTHeightmap;
			OutputRTHeightmap.Reserve(SampleRect.Width() * SampleRect.Height());

			RenderTargetResource->ReadLinearColorPixels(OutputRTHeightmap, FReadSurfaceDataFlags(RCM_MinMax, CubeFace_MAX), SampleRect);
			HeightData.Reserve(OutputRTHeightmap.Num());

			for (auto LinearColor : OutputRTHeightmap)
			{
				if (InImportHeightFromRGChannel)
				{
					FColor Color = LinearColor.ToFColor(false);
					uint16 Height = ((Color.R << 8) | Color.G);
					HeightData.Add(Height);
				}
				else
				{
					HeightData.Add((uint16)LinearColor.R);
				}
			}
		}
		break;			

		case RTF_RGBA8:
		{
			TArray<FColor> OutputRTHeightmap;
			OutputRTHeightmap.Reserve(SampleRect.Width() * SampleRect.Height());

			RenderTargetResource->ReadPixels(OutputRTHeightmap, FReadSurfaceDataFlags(RCM_MinMax, CubeFace_MAX), SampleRect);
			HeightData.Reserve(OutputRTHeightmap.Num());

			for (FColor Color : OutputRTHeightmap)
			{
				uint16 Height = ((Color.R << 8) | Color.G);
				HeightData.Add(Height);
			}
		}
		break;

		default:
		{
			FMessageLog("Blueprint").Error(LOCTEXT("LandscapeImportHeightmapFromRenderTarget_InvalidRTFormat", "LandscapeImportHeightmapFromRenderTarget: The Render Target format is invalid. We only support RTF_RGBA16f, RTF_RGBA32f, RTF_RGBA8"));
			return false;
		}
	}	

	FScopedTransaction Transaction(LOCTEXT("Undo_ImportHeightmap", "Importing Landscape Heightmap"));

	FHeightmapAccessor<false> HeightmapAccessor(LandscapeInfo);
	HeightmapAccessor.SetData(MinX, MinY, SampleRect.Width() - 1, SampleRect.Height() - 1, HeightData.GetData());

	double SecondsTaken = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartCycle);
	UE_LOG(LogLandscapeBP, Display, TEXT("Took %f seconds to import heightmap from render target."), SecondsTaken);

	return true;
}
#endif

bool ALandscapeProxy::LandscapeExportHeightmapToRenderTarget(UTextureRenderTarget2D* InRenderTarget, bool bInExportHeightIntoRGChannel, bool InExportLandscapeProxies)
{
#if WITH_EDITOR
	uint64 StartCycle = FPlatformTime::Cycles64();

	UMaterial* HeightmapRenderMaterial = LoadObject<UMaterial>(nullptr, TEXT("/Engine/EditorLandscapeResources/Landscape_Heightmap_To_RenderTarget2D.Landscape_Heightmap_To_RenderTarget2D"));
	if (HeightmapRenderMaterial == nullptr)
	{
		FMessageLog("Blueprint").Error(LOCTEXT("LandscapeExportHeightmapToRenderTarget_Landscape_Heightmap_To_RenderTarget2D.", "LandscapeExportHeightmapToRenderTarget: Material Landscape_Heightmap_To_RenderTarget2D not found in engine content."));
		return false;
	}

	TArray<ULandscapeComponent*> LandscapeComponentsToExport;
	//  Export the component of the specified proxy
	LandscapeComponentsToExport.Append(LandscapeComponents);

	// If requested, export all proxies
	if (InExportLandscapeProxies && (GetLandscapeActor() == this))
	{
		ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
		for (ALandscapeProxy* Proxy : LandscapeInfo->Proxies)
		{
			LandscapeComponentsToExport.Append(Proxy->LandscapeComponents);
		}
	}

	if (LandscapeComponentsToExport.Num() == 0)
	{
		return true;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	FTextureRenderTargetResource* RenderTargetResource = InRenderTarget->GameThread_GetRenderTargetResource();

	// Create a canvas for the render target and clear it to black
	FCanvas Canvas(RenderTargetResource, nullptr, 0, 0, 0, World->FeatureLevel);
	Canvas.Clear(FLinearColor::Black);

	// Find exported component's base offset
	FIntRect ComponentsExtent(MAX_int32, MAX_int32, MIN_int32, MIN_int32);
	for (ULandscapeComponent* Component : LandscapeComponentsToExport)
	{
		Component->GetComponentExtent(ComponentsExtent.Min.X, ComponentsExtent.Min.Y, ComponentsExtent.Max.X, ComponentsExtent.Max.Y);
	}
	FIntPoint ExportBaseOffset = ComponentsExtent.Min;

	struct FTrianglePerMID
	{
		UMaterialInstanceDynamic* HeightmapMID;
		TArray<FCanvasUVTri> TriangleList;
	};

	TMap<UTexture*, FTrianglePerMID> TrianglesPerHeightmap;

	for (const ULandscapeComponent* Component : LandscapeComponentsToExport)
	{
		FTrianglePerMID* TrianglesPerMID = TrianglesPerHeightmap.Find(Component->GetHeightmap());

		if (TrianglesPerMID == nullptr)
		{
			FTrianglePerMID Data;
			Data.HeightmapMID = UMaterialInstanceDynamic::Create(HeightmapRenderMaterial, this);
			Data.HeightmapMID->SetTextureParameterValue(TEXT("Heightmap"), Component->GetHeightmap());
			Data.HeightmapMID->SetScalarParameterValue(TEXT("ExportHeightIntoRGChannel"), bInExportHeightIntoRGChannel);
			TrianglesPerMID = &TrianglesPerHeightmap.Add(Component->GetHeightmap(), Data);
		}

		FIntPoint ComponentSectionBase = Component->GetSectionBase();
		FIntPoint ComponentHeightmapTextureSize(Component->GetHeightmap()->Source.GetSizeX(), Component->GetHeightmap()->Source.GetSizeY());
		int32 SubsectionSizeVerts = Component->SubsectionSizeQuads + 1;
		float HeightmapSubsectionOffsetU = (float)(SubsectionSizeVerts) / (float)ComponentHeightmapTextureSize.X;
		float HeightmapSubsectionOffsetV = (float)(SubsectionSizeVerts) / (float)ComponentHeightmapTextureSize.Y;

		for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
		{
			for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
			{
				FIntPoint SubSectionSectionBase = ComponentSectionBase - ExportBaseOffset;
				SubSectionSectionBase.X += Component->SubsectionSizeQuads * SubX;
				SubSectionSectionBase.Y += Component->SubsectionSizeQuads * SubY;

				// Offset for this component's data in heightmap texture
				float HeightmapOffsetU = Component->HeightmapScaleBias.Z + HeightmapSubsectionOffsetU * (float)SubX;
				float HeightmapOffsetV = Component->HeightmapScaleBias.W + HeightmapSubsectionOffsetV * (float)SubY;

				FCanvasUVTri Tri1;
				Tri1.V0_Pos = FVector2D(SubSectionSectionBase.X, SubSectionSectionBase.Y);
				Tri1.V1_Pos = FVector2D(SubSectionSectionBase.X + SubsectionSizeVerts, SubSectionSectionBase.Y);
				Tri1.V2_Pos = FVector2D(SubSectionSectionBase.X + SubsectionSizeVerts, SubSectionSectionBase.Y + SubsectionSizeVerts);

				Tri1.V0_UV = FVector2D(HeightmapOffsetU, HeightmapOffsetV);
				Tri1.V1_UV = FVector2D(HeightmapOffsetU + HeightmapSubsectionOffsetU, HeightmapOffsetV);
				Tri1.V2_UV = FVector2D(HeightmapOffsetU + HeightmapSubsectionOffsetU, HeightmapOffsetV + HeightmapSubsectionOffsetV);
				TrianglesPerMID->TriangleList.Add(Tri1);

				FCanvasUVTri Tri2;
				Tri2.V0_Pos = FVector2D(SubSectionSectionBase.X + SubsectionSizeVerts, SubSectionSectionBase.Y + SubsectionSizeVerts);
				Tri2.V1_Pos = FVector2D(SubSectionSectionBase.X, SubSectionSectionBase.Y + SubsectionSizeVerts);
				Tri2.V2_Pos = FVector2D(SubSectionSectionBase.X, SubSectionSectionBase.Y);

				Tri2.V0_UV = FVector2D(HeightmapOffsetU + HeightmapSubsectionOffsetU, HeightmapOffsetV + HeightmapSubsectionOffsetV);
				Tri2.V1_UV = FVector2D(HeightmapOffsetU, HeightmapOffsetV + HeightmapSubsectionOffsetV);
				Tri2.V2_UV = FVector2D(HeightmapOffsetU, HeightmapOffsetV);

				TrianglesPerMID->TriangleList.Add(Tri2);
			}
		}
	}

	for (auto& TriangleList : TrianglesPerHeightmap)
	{
		FCanvasTriangleItem TriItemList(MoveTemp(TriangleList.Value.TriangleList), nullptr);
		TriItemList.MaterialRenderProxy = TriangleList.Value.HeightmapMID->GetRenderProxy();
		TriItemList.BlendMode = SE_BLEND_Opaque;
		TriItemList.SetColor(FLinearColor::White);

		TriItemList.Draw(&Canvas);
	}

	TrianglesPerHeightmap.Reset();

	// Tell the rendering thread to draw any remaining batched elements
	Canvas.Flush_GameThread(true);

	ENQUEUE_RENDER_COMMAND(DrawHeightmapRTCommand)(
		[RenderTargetResource](FRHICommandListImmediate& RHICmdList)
		{
			// Copy (resolve) the rendered image from the frame buffer to its render target texture
			RHICmdList.CopyToResolveTarget(
				RenderTargetResource->GetRenderTargetTexture(),		// Source texture
				RenderTargetResource->TextureRHI,					// Dest texture
				FResolveParams());									// Resolve parameters
		});


	FlushRenderingCommands();

	double SecondsTaken = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartCycle);
	UE_LOG(LogLandscapeBP, Display, TEXT("Took %f seconds to export heightmap to render target."), SecondsTaken);
#endif
	return true;
}

#if WITH_EDITOR
bool ALandscapeProxy::LandscapeImportWeightmapFromRenderTarget(UTextureRenderTarget2D* InRenderTarget, FName InLayerName)
{
	ALandscape* Landscape = GetLandscapeActor();
	if (Landscape != nullptr)
	{
		if (Landscape->HasLayersContent())
		{
			//todo: Support an edit layer name input parameter to support import to edit layers.
			FMessageLog("Blueprint").Error(LOCTEXT("LandscapeImportWeightmapFromRenderTarget_LandscapeLayersNotSupported", "LandscapeImportWeightmapFromRenderTarget: Cannot import to landscape with Edit Layers enabled."));
			return false;
		}

		ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();

		int32 MinX, MinY, MaxX, MaxY;
		if (LandscapeInfo && LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
		{
			const uint32 LandscapeWidth = (uint32)(1 + MaxX - MinX);
			const uint32 LandscapeHeight = (uint32)(1 + MaxY - MinY);
			FLinearColor SampleRect = FLinearColor(0, 0, LandscapeWidth, LandscapeHeight);

			const uint32 RTWidth = InRenderTarget->SizeX;
			const uint32 RTHeight = InRenderTarget->SizeY;
			ETextureRenderTargetFormat format = (InRenderTarget->RenderTargetFormat);

			if (RTWidth >= LandscapeWidth && RTHeight >= LandscapeHeight)
			{
				TArray<FLinearColor> RTData;
				RTData = SampleRTData(InRenderTarget, SampleRect);

				TArray<uint8> LayerData;

				for (auto i : RTData)
				{
					LayerData.Add((uint8)(FMath::Clamp((float)i.R, 0.0f, 1.0f) * 255));
				}

				FLandscapeInfoLayerSettings CurWeightmapInfo;

				int32 Index = LandscapeInfo->GetLayerInfoIndex(InLayerName, LandscapeInfo->GetLandscapeProxy());

				if (ensure(Index != INDEX_NONE))
				{
					CurWeightmapInfo = LandscapeInfo->Layers[Index];
				}

				if (CurWeightmapInfo.LayerInfoObj == nullptr)
				{
					FMessageLog("Blueprint").Error(LOCTEXT("LandscapeImportRenderTarget_InvalidLayerInfoObject", "LandscapeImportWeightmapFromRenderTarget: Layers must first have Layer Info Objects assigned before importing."));
					return false;
				}

				FScopedTransaction Transaction(LOCTEXT("Undo_ImportWeightmap", "Importing Landscape Layer"));

				FAlphamapAccessor<false, false> AlphamapAccessor(LandscapeInfo, CurWeightmapInfo.LayerInfoObj);
				AlphamapAccessor.SetData(MinX, MinY, MaxX, MaxY, LayerData.GetData(), ELandscapeLayerPaintingRestriction::None);

				uint64 CycleEnd = FPlatformTime::Cycles64();
				UE_LOG(LogLandscape, Log, TEXT("Took %f seconds to import heightmap from render target"), FPlatformTime::ToSeconds64(CycleEnd));

				return true;
			}
			else
			{
				FMessageLog("Blueprint").Error(LOCTEXT("LandscapeImportRenderTarget_InvalidRenderTarget", "LandscapeImportWeightmapFromRenderTarget: Render target must be at least as large as landscape on each axis."));
				return false;
			}
		}
		else
		{
			return false;
		}
	}
	FMessageLog("Blueprint").Error(LOCTEXT("LandscapeImportRenderTarget_NullLandscape.", "LandscapeImportWeightmapFromRenderTarget: Landscape must be non-null."));
	return false;
}

bool ALandscapeProxy::LandscapeExportWeightmapToRenderTarget(UTextureRenderTarget2D* InRenderTarget, FName InLayerName)
{
	return false;
}

#endif //WITH_EDITOR

#undef LOCTEXT_NAMESPACE
