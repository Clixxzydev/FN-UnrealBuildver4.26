// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LightSceneInfo.cpp: Light scene info implementation.
=============================================================================*/

#include "LightSceneInfo.h"
#include "Components/LightComponent.h"
#include "SceneCore.h"
#include "ScenePrivate.h"
#include "DistanceFieldLightingShared.h"

int32 GWholeSceneShadowUnbuiltInteractionThreshold = 500;
static FAutoConsoleVariableRef CVarWholeSceneShadowUnbuiltInteractionThreshold(
	TEXT("r.Shadow.WholeSceneShadowUnbuiltInteractionThreshold"),
	GWholeSceneShadowUnbuiltInteractionThreshold,
	TEXT("How many unbuilt light-primitive interactions there can be for a light before the light switches to whole scene shadows"),
	ECVF_RenderThreadSafe
	);

static int32 GRecordInteractionShadowPrimitives = 1;
FAutoConsoleVariableRef CVarRecordInteractionShadowPrimitives(
	TEXT("r.Shadow.RecordInteractionShadowPrimitives"),
	GRecordInteractionShadowPrimitives,
	TEXT(""),
	ECVF_RenderThreadSafe);

void FLightSceneInfoCompact::Init(FLightSceneInfo* InLightSceneInfo)
{
	LightSceneInfo = InLightSceneInfo;
	FSphere BoundingSphere = InLightSceneInfo->Proxy->GetBoundingSphere();
	BoundingSphere.W = BoundingSphere.W > 0.0f ? BoundingSphere.W : FLT_MAX;
	FMemory::Memcpy(&BoundingSphereVector,&BoundingSphere,sizeof(BoundingSphereVector));
	Color = InLightSceneInfo->Proxy->GetColor();
	LightType = InLightSceneInfo->Proxy->GetLightType();

	bCastDynamicShadow = InLightSceneInfo->Proxy->CastsDynamicShadow();
	bCastStaticShadow = InLightSceneInfo->Proxy->CastsStaticShadow();
	bStaticLighting = InLightSceneInfo->Proxy->HasStaticLighting();
	bAffectReflection = InLightSceneInfo->Proxy->AffectReflection();
	bAffectGlobalIllumination = InLightSceneInfo->Proxy->AffectGlobalIllumination();
	bCastRaytracedShadow = InLightSceneInfo->Proxy->CastsRaytracedShadow();
}

FLightSceneInfo::FLightSceneInfo(FLightSceneProxy* InProxy, bool InbVisible)
	: bRecordInteractionShadowPrimitives(!!GRecordInteractionShadowPrimitives && InProxy->GetLightType() != ELightComponentType::LightType_Directional)
	, DynamicInteractionOftenMovingPrimitiveList(NULL)
	, DynamicInteractionStaticPrimitiveList(NULL)
	, Proxy(InProxy)
	, Id(INDEX_NONE)
	, TileIntersectionResources(nullptr)
	, HeightFieldTileIntersectionResources(nullptr)
	, DynamicShadowMapChannel(-1)
	, bPrecomputedLightingIsValid(InProxy->GetLightComponent()->IsPrecomputedLightingValid())
	, bVisible(InbVisible)
	, bEnableLightShaftBloom(InProxy->GetLightComponent()->bEnableLightShaftBloom)
	, BloomScale(InProxy->GetLightComponent()->BloomScale)
	, BloomThreshold(InProxy->GetLightComponent()->BloomThreshold)
	, BloomMaxBrightness(InProxy->GetLightComponent()->BloomMaxBrightness)
	, BloomTint(InProxy->GetLightComponent()->BloomTint)
	, NumUnbuiltInteractions(0)
	, bCreatePerObjectShadowsForDynamicObjects(Proxy->ShouldCreatePerObjectShadowsForDynamicObjects())
	, Scene(InProxy->GetLightComponent()->GetScene()->GetRenderScene())
{
	// Only visible lights can be added in game
	check(bVisible || GIsEditor);

	BeginInitResource(this);
}

FLightSceneInfo::~FLightSceneInfo()
{
	ReleaseResource();
}

void FLightSceneInfo::AddToScene()
{
	const FLightSceneInfoCompact& LightSceneInfoCompact = Scene->Lights[Id];

	bool bIsValidLightTypeMobile = false;
	if (Scene->GetShadingPath() == EShadingPath::Mobile && Proxy->IsMovable())
	{
		static const auto MobileEnableMovableSpotLightsVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.EnableMovableSpotLights"));
		const uint8 LightType = Proxy->GetLightType();
		bIsValidLightTypeMobile = LightType == LightType_Rect || LightType == LightType_Point
			|| (LightType == LightType_Spot && MobileEnableMovableSpotLightsVar->GetValueOnRenderThread());
	}

	// Only need to create light interactions for lights that can cast a shadow,
	// As deferred shading doesn't need to know anything about the primitives that a light affects
	if (Proxy->CastsDynamicShadow()
		|| Proxy->CastsStaticShadow()
		// Lights that should be baked need to check for interactions to track unbuilt state correctly
		|| Proxy->HasStaticLighting()
		// Mobile path supports dynamic point/spot lights in the base pass using forward rendering, so we need to know the primitives
		|| bIsValidLightTypeMobile)
	{
		Scene->FlushAsyncLightPrimitiveInteractionCreation();
		
		// Directional lights have no finite extent and cannot meaningfully be in the LocalShadowCastingLightOctree
		if (LightSceneInfoCompact.LightType == LightType_Directional)
		{
			// 
			Scene->DirectionalShadowCastingLightIDs.Add(Id);

			// All primitives may interact with a directional light
			FMemMark MemStackMark(FMemStack::Get());
			for (FPrimitiveSceneInfo *PrimitiveSceneInfo : Scene->Primitives)
			{
				CreateLightPrimitiveInteraction(LightSceneInfoCompact, PrimitiveSceneInfo);
			}
		}
		else
		{
			// Add the light to the scene's light octree.
			Scene->LocalShadowCastingLightOctree.AddElement(LightSceneInfoCompact);
			// Find primitives that the light affects in the primitive octree.
			FMemMark MemStackMark(FMemStack::Get());

			Scene->PrimitiveOctree.FindElementsWithBoundsTest(GetBoundingBox(), [&LightSceneInfoCompact, this](const FPrimitiveSceneInfoCompact& PrimitiveSceneInfoCompact)
			{
				CreateLightPrimitiveInteraction(LightSceneInfoCompact, PrimitiveSceneInfoCompact);
			});
		}
	}
}

/**
 * If the light affects the primitive, create an interaction, and process children 
 * 
 * @param LightSceneInfoCompact Compact representation of the light
 * @param PrimitiveSceneInfoCompact Compact representation of the primitive
 */
void FLightSceneInfo::CreateLightPrimitiveInteraction(const FLightSceneInfoCompact& LightSceneInfoCompact, const FPrimitiveSceneInfoCompact& PrimitiveSceneInfoCompact)
{
	if(	LightSceneInfoCompact.AffectsPrimitive(PrimitiveSceneInfoCompact.Bounds, PrimitiveSceneInfoCompact.Proxy))
	{
		// create light interaction and add to light/primitive lists
		FLightPrimitiveInteraction::Create(this,PrimitiveSceneInfoCompact.PrimitiveSceneInfo);
	}
}


void FLightSceneInfo::RemoveFromScene()
{
	Scene->FlushAsyncLightPrimitiveInteractionCreation();

	if (OctreeId.IsValidId())
	{
		// Remove the light from the octree.
		Scene->LocalShadowCastingLightOctree.RemoveElement(OctreeId);
	}
	else
	{
		Scene->DirectionalShadowCastingLightIDs.RemoveSwap(Id);
	}

	Scene->CachedShadowMaps.Remove(Id);

	// Detach the light from the primitives it affects.
	Detach();
}

void FLightSceneInfo::Detach()
{
	check(IsInRenderingThread());

	InteractionShadowPrimitives.Empty();

	// implicit linked list. The destruction will update this "head" pointer to the next item in the list.
	while(DynamicInteractionOftenMovingPrimitiveList)
	{
		FLightPrimitiveInteraction::Destroy(DynamicInteractionOftenMovingPrimitiveList);
	}

	while(DynamicInteractionStaticPrimitiveList)
	{
		FLightPrimitiveInteraction::Destroy(DynamicInteractionStaticPrimitiveList);
	}
}

bool FLightSceneInfo::ShouldRenderLight(const FViewInfo& View) const
{
	// Only render the light if it is in the view frustum
	bool bLocalVisible = bVisible ? View.VisibleLightInfos[Id].bInViewFrustum : true;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	ELightComponentType Type = (ELightComponentType)Proxy->GetLightType();

	switch(Type)
	{
		case LightType_Directional:
			if(!View.Family->EngineShowFlags.DirectionalLights) 
			{
				bLocalVisible = false;
			}
			break;
		case LightType_Point:
			if(!View.Family->EngineShowFlags.PointLights) 
			{
				bLocalVisible = false;
			}
			break;
		case LightType_Spot:
			if(!View.Family->EngineShowFlags.SpotLights)
			{
				bLocalVisible = false;
			}
			break;
		case LightType_Rect:
			if(!View.Family->EngineShowFlags.RectLights)
			{
				bLocalVisible = false;
			}
			break;
	}
#endif

	return bLocalVisible
		// Only render lights with static shadowing for reflection captures, since they are only captured at edit time
		&& (!View.bStaticSceneOnly || Proxy->HasStaticShadowing())
		// Only render lights in the default channel, or if there are any primitives outside the default channel
		&& (Proxy->GetLightingChannelMask() & GetDefaultLightingChannelMask() || View.bUsesLightingChannels);
}

bool FLightSceneInfo::IsPrecomputedLightingValid() const
{
	return (bPrecomputedLightingIsValid && NumUnbuiltInteractions < GWholeSceneShadowUnbuiltInteractionThreshold) || !Proxy->HasStaticShadowing();
}

const TArray<FLightPrimitiveInteraction*>* FLightSceneInfo::GetInteractionShadowPrimitives(bool bSync) const
{
	if (bSync)
	{
		Scene->FlushAsyncLightPrimitiveInteractionCreation();
	}
	return bRecordInteractionShadowPrimitives ? &InteractionShadowPrimitives : nullptr;
}

FLightPrimitiveInteraction* FLightSceneInfo::GetDynamicInteractionOftenMovingPrimitiveList(bool bSync) const
{
	if (bSync)
	{
		Scene->FlushAsyncLightPrimitiveInteractionCreation();
	}
	return DynamicInteractionOftenMovingPrimitiveList;
}

FLightPrimitiveInteraction* FLightSceneInfo::GetDynamicInteractionStaticPrimitiveList(bool bSync) const
{
	if (bSync)
	{
		Scene->FlushAsyncLightPrimitiveInteractionCreation();
	}
	return DynamicInteractionStaticPrimitiveList;
}

void FLightSceneInfo::ReleaseRHI()
{
	if (TileIntersectionResources)
	{
		TileIntersectionResources->Release();
	}

	ShadowCapsuleShapesVertexBuffer.SafeRelease();
	ShadowCapsuleShapesSRV.SafeRelease();
}

/** Determines whether two bounding spheres intersect. */
FORCEINLINE bool AreSpheresNotIntersecting(
	const VectorRegister& A_XYZ,
	const VectorRegister& A_Radius,
	const VectorRegister& B_XYZ,
	const VectorRegister& B_Radius
	)
{
	const VectorRegister DeltaVector = VectorSubtract(A_XYZ,B_XYZ);
	const VectorRegister DistanceSquared = VectorDot3(DeltaVector,DeltaVector);
	const VectorRegister MaxDistance = VectorAdd(A_Radius,B_Radius);
	const VectorRegister MaxDistanceSquared = VectorMultiply(MaxDistance,MaxDistance);
	return !!VectorAnyGreaterThan(DistanceSquared,MaxDistanceSquared);
}

/**
* Tests whether this light affects the given primitive.  This checks both the primitive and light settings for light relevance
* and also calls AffectsBounds.
*
* @param CompactPrimitiveSceneInfo - The primitive to test.
* @return True if the light affects the primitive.
*/
bool FLightSceneInfoCompact::AffectsPrimitive(const FBoxSphereBounds& PrimitiveBounds, const FPrimitiveSceneProxy* PrimitiveSceneProxy) const
{
	// Check if the light's bounds intersect the primitive's bounds.
	// Directional lights reach everywhere (the hacky world max radius does not work for large worlds)
	if(LightType != LightType_Directional && AreSpheresNotIntersecting(
		BoundingSphereVector,
		VectorReplicate(BoundingSphereVector,3),
		VectorLoadFloat3(&PrimitiveBounds.Origin),
		VectorLoadFloat1(&PrimitiveBounds.SphereRadius)
		))
	{
		return false;
	}

	// Cull based on information in the full scene infos.

	if(!LightSceneInfo->Proxy->AffectsBounds(PrimitiveBounds))
	{
		return false;
	}

	if (LightSceneInfo->Proxy->CastsShadowsFromCinematicObjectsOnly() && !PrimitiveSceneProxy->CastsCinematicShadow())
	{
		return false;
	}

	if (!(LightSceneInfo->Proxy->GetLightingChannelMask() & PrimitiveSceneProxy->GetLightingChannelMask()))
	{
		return false;
	}

	return true;
}
