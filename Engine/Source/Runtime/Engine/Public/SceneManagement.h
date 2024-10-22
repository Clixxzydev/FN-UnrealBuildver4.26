// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneManagement.h: Scene manager definitions.
=============================================================================*/

#pragma once

// Includes the draw mesh macros

#include "CoreMinimal.h"
#include "Containers/ChunkedArray.h"
#include "Stats/Stats.h"
#include "Misc/Guid.h"
#include "Misc/MemStack.h"
#include "Misc/IQueuedWork.h"
#include "RHI.h"
#include "RenderResource.h"
#include "EngineDefines.h"
#include "HitProxies.h"
#include "SceneTypes.h"
#include "ConvexVolume.h"
#include "RendererInterface.h"
#include "Engine/TextureLightProfile.h"
#include "BatchedElements.h"
#include "MeshBatch.h"
#include "SceneUtils.h"
#include "LightmapUniformShaderParameters.h"
#include "DynamicBufferAllocator.h"
#include "Rendering/SkyAtmosphereCommonData.h"
#include "Rendering/SkyLightImportanceSampling.h"

class FCanvas;
class FLightMap;
class FLightmapResourceCluster;
class FLightSceneInfo;
class FLightSceneProxy;
class FPrimitiveSceneInfo;
class FPrimitiveSceneProxy;
class FSceneViewState;
class FShadowMap;
class FStaticMeshRenderData;
class UDecalComponent;
class ULightComponent;
class ULightMapTexture2D;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UShadowMapTexture2D;
class USkyAtmosphereComponent;
class FSkyAtmosphereRenderSceneInfo;
class USkyLightComponent;
struct FDynamicMeshVertex;
class ULightMapVirtualTexture2D;

DECLARE_LOG_CATEGORY_EXTERN(LogBufferVisualization, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogMultiView, Log, All);

// -----------------------------------------------------------------------------


/**
 * struct to hold the temporal LOD state within a view state
 */
struct ENGINE_API FTemporalLODState
{
	/** The last two camera origin samples collected for stateless temporal LOD transitions */
	FVector	TemporalLODViewOrigin[2];
	/** The last two fov-like parameters from the projection matrix for stateless temporal LOD transitions */
	float	TemporalDistanceFactor[2];
	/** The last two time samples collected for stateless temporal LOD transitions */
	float	TemporalLODTime[2];
	/** If non-zero, then we are doing temporal LOD smoothing, this is the time interval. */
	float	TemporalLODLag;

	FTemporalLODState()
		: TemporalLODLag(0.0f) // nothing else is used if this is zero
	{

	}
	/** 
	 * Returns the blend factor between the last two LOD samples
	 */
	float GetTemporalLODTransition(float LastRenderTime) const
	{
		if (TemporalLODLag == 0.0)
		{
			return 0.0f; // no fade
		}
		return FMath::Clamp((LastRenderTime - TemporalLODLag - TemporalLODTime[0]) / (TemporalLODTime[1] - TemporalLODTime[0]), 0.0f, 1.0f);
	}

	void UpdateTemporalLODTransition(const class FViewInfo& View, float LastRenderTime);
};

enum ESequencerState
{
	ESS_None,
	ESS_Paused,
	ESS_Playing,
};

/**
 * The scene manager's persistent view state.
 */
class FSceneViewStateInterface
{
public:
	FSceneViewStateInterface()
		:	ViewParent( NULL )
		,	NumChildren( 0 )
	{}
	
	/** Called in the game thread to destroy the view state. */
	virtual void Destroy() = 0;

public:
	/** Sets the view state's scene parent. */
	void SetViewParent(FSceneViewStateInterface* InViewParent)
	{
		if ( ViewParent )
		{
			// Assert that the existing parent does not have a parent.
			check( !ViewParent->HasViewParent() );
			// Decrement ref ctr of existing parent.
			--ViewParent->NumChildren;
		}

		if ( InViewParent && InViewParent != this )
		{
			// Assert that the incoming parent does not have a parent.
			check( !InViewParent->HasViewParent() );
			ViewParent = InViewParent;
			// Increment ref ctr of new parent.
			InViewParent->NumChildren++;
		}
		else
		{
			ViewParent = NULL;
		}
	}
	/** @return			The view state's scene parent, or NULL if none present. */
	FSceneViewStateInterface* GetViewParent()
	{
		return ViewParent;
	}
	/** @return			The view state's scene parent, or NULL if none present. */
	const FSceneViewStateInterface* GetViewParent() const
	{
		return ViewParent;
	}
	/** @return			true if the scene state has a parent, false otherwise. */
	bool HasViewParent() const
	{
		return GetViewParent() != NULL;
	}
	/** @return			true if this scene state is a parent, false otherwise. */
	bool IsViewParent() const
	{
		return NumChildren > 0;
	}
	
	/** @return	the derived view state object */
	virtual FSceneViewState* GetConcreteViewState () = 0;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) = 0;

	virtual SIZE_T GetSizeBytes() const { return 0; }

	/** Resets pool for GetReusableMID() */
	virtual void OnStartPostProcessing(FSceneView& CurrentView) = 0;

	/**
	 * Allows MIDs being created and released during view rendering without the overhead of creating and releasing objects
	 * As MID are not allowed to be parent of MID this gets fixed up by parenting it to the next Material or MIC
	 * @param InSource can be Material, MIC or MID, must not be 0
	 */
	virtual UMaterialInstanceDynamic* GetReusableMID(class UMaterialInterface* InSource) = 0;

	/**
	 * Clears the pool of mids being referenced by this view state 
	 */
	virtual void ClearMIDPool() = 0;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	/** If frozen view matrices are available, return a pointer to them */
	virtual const FViewMatrices* GetFrozenViewMatrices() const = 0;

	/** If frozen view matrices are available, set those as active on the SceneView */
	virtual void ActivateFrozenViewMatrices(FSceneView& SceneView) = 0;

	/** If frozen view matrices were set, restore the previous view matrices */
	virtual void RestoreUnfrozenViewMatrices(FSceneView& SceneView) = 0;
#endif
	// rest some state (e.g. FrameIndexMod8, TemporalAASampleIndex) to make the rendering [more] deterministic
	virtual void ResetViewState() = 0;

	/** Returns the temporal LOD struct from the viewstate */
	virtual FTemporalLODState& GetTemporalLODState() = 0;
	virtual const FTemporalLODState& GetTemporalLODState() const = 0;

	/** 
	 * Returns the blend factor between the last two LOD samples
	 */
	virtual float GetTemporalLODTransition() const = 0;

	/** 
	 * returns a unique key for the view state, non-zero
	 */
	virtual uint32 GetViewKey() const = 0;

	//
	virtual uint32 GetCurrentTemporalAASampleIndex() const = 0;

	virtual uint32 GetCurrentUnclampedTemporalAASampleIndex() const = 0;

	virtual void SetSequencerState(ESequencerState InSequencerState) = 0;

	virtual ESequencerState GetSequencerState() = 0;

	/** Returns the current PreExposure value. PreExposure is a custom scale applied to the scene color to prevent buffer overflow. */
	virtual float GetPreExposure() const = 0;

	/** 
	 * returns the occlusion frame counter 
	 */
	virtual uint32 GetOcclusionFrameCounter() const = 0;
protected:
	// Don't allow direct deletion of the view state, Destroy should be called instead.
	virtual ~FSceneViewStateInterface() {}

private:
	/** This scene state's view parent; NULL if no parent present. */
	FSceneViewStateInterface*	ViewParent;
	/** Reference counts the number of children parented to this state. */
	int32							NumChildren;
};

class FFrozenSceneViewMatricesGuard
{
public:
	FFrozenSceneViewMatricesGuard(FSceneView& SV)
		: SceneView(SV)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (SceneView.State)
		{
			SceneView.State->ActivateFrozenViewMatrices(SceneView);
		}
#endif
	}

	~FFrozenSceneViewMatricesGuard()
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (SceneView.State)
		{
			SceneView.State->RestoreUnfrozenViewMatrices(SceneView);
		}
#endif
	}

private:
	FSceneView& SceneView;
};

/**
 * The types of interactions between a light and a primitive.
 */
enum ELightInteractionType
{
	LIT_CachedIrrelevant,
	LIT_CachedLightMap,
	LIT_Dynamic,
	LIT_CachedSignedDistanceFieldShadowMap2D,

	LIT_MAX
};

/**
 * Information about an interaction between a light and a mesh.
 */
class FLightInteraction
{
public:

	// Factory functions.
	static FLightInteraction Dynamic() { return FLightInteraction(LIT_Dynamic); }
	static FLightInteraction LightMap() { return FLightInteraction(LIT_CachedLightMap); }
	static FLightInteraction Irrelevant() { return FLightInteraction(LIT_CachedIrrelevant); }
	static FLightInteraction ShadowMap2D() { return FLightInteraction(LIT_CachedSignedDistanceFieldShadowMap2D); }

	// Accessors.
	ELightInteractionType GetType() const { return Type; }

	/**
	 * Minimal initialization constructor.
	 */
	FLightInteraction(ELightInteractionType InType)
		: Type(InType)
	{}

private:
	ELightInteractionType Type;
};





/** The number of coefficients that are stored for each light sample. */ 
static const int32 NUM_STORED_LIGHTMAP_COEF = 4;

/** The number of directional coefficients which the lightmap stores for each light sample. */ 
static const int32 NUM_HQ_LIGHTMAP_COEF = 2;

/** The number of simple coefficients which the lightmap stores for each light sample. */ 
static const int32 NUM_LQ_LIGHTMAP_COEF = 2;

/** The index at which simple coefficients are stored in any array containing all NUM_STORED_LIGHTMAP_COEF coefficients. */ 
static const int32 LQ_LIGHTMAP_COEF_INDEX = 2;

/** Compile out low quality lightmaps to save memory */
// @todo-mobile: Need to fix this!
#ifndef ALLOW_LQ_LIGHTMAPS
#define ALLOW_LQ_LIGHTMAPS (PLATFORM_DESKTOP || PLATFORM_IOS || PLATFORM_ANDROID || PLATFORM_SWITCH || PLATFORM_LUMIN || PLATFORM_HOLOLENS)
#endif

/** Compile out high quality lightmaps to save memory */
#define ALLOW_HQ_LIGHTMAPS 1

/** Make sure at least one is defined */
#if !ALLOW_LQ_LIGHTMAPS && !ALLOW_HQ_LIGHTMAPS
#error At least one of ALLOW_LQ_LIGHTMAPS and ALLOW_HQ_LIGHTMAPS needs to be defined!
#endif

/**
 * Information about an interaction between a light and a mesh.
 */
class FLightMapInteraction
{
public:

	// Factory functions.
	static FLightMapInteraction None()
	{
		FLightMapInteraction Result;
		Result.Type = LMIT_None;
		return Result;
	}

	static FLightMapInteraction GlobalVolume()
	{
		FLightMapInteraction Result;
		Result.Type = LMIT_GlobalVolume;
		return Result;
	}

	static FLightMapInteraction Texture(
		const class ULightMapTexture2D* const* InTextures,
		const ULightMapTexture2D* InSkyOcclusionTexture,
		const ULightMapTexture2D* InAOMaterialMaskTexture,
		const FVector4* InCoefficientScales,
		const FVector4* InCoefficientAdds,
		const FVector2D& InCoordinateScale,
		const FVector2D& InCoordinateBias,
		bool bAllowHighQualityLightMaps);

	static FLightMapInteraction InitVirtualTexture(
		const ULightMapVirtualTexture2D* VirtualTexture,
		const FVector4* InCoefficientScales,
		const FVector4* InCoefficientAdds,
		const FVector2D& InCoordinateScale,
		const FVector2D& InCoordinateBias,
		bool bAllowHighQualityLightMaps);

	/** Default constructor. */
	FLightMapInteraction():
#if ALLOW_HQ_LIGHTMAPS
		HighQualityTexture(NULL),
		SkyOcclusionTexture(NULL),
		AOMaterialMaskTexture(NULL),
		VirtualTexture(NULL),
#endif
#if ALLOW_LQ_LIGHTMAPS
		LowQualityTexture(NULL),
#endif
		Type(LMIT_None)
	{}

	// Accessors.
	ELightMapInteractionType GetType() const { return Type; }
	
	const ULightMapTexture2D* GetTexture(bool bHighQuality) const
	{
		check(Type == LMIT_Texture);
#if ALLOW_LQ_LIGHTMAPS && ALLOW_HQ_LIGHTMAPS
		return bHighQuality ? HighQualityTexture : LowQualityTexture;
#elif ALLOW_HQ_LIGHTMAPS
		return HighQualityTexture;
#else
		return LowQualityTexture;
#endif
	}

	const ULightMapTexture2D* GetSkyOcclusionTexture() const
	{
		check(Type == LMIT_Texture);
#if ALLOW_HQ_LIGHTMAPS
		return SkyOcclusionTexture;
#else
		return NULL;
#endif
	}

	const ULightMapTexture2D* GetAOMaterialMaskTexture() const
	{
		check(Type == LMIT_Texture);
#if ALLOW_HQ_LIGHTMAPS
		return AOMaterialMaskTexture;
#else
		return NULL;
#endif
	}

	const ULightMapVirtualTexture2D* GetVirtualTexture() const
	{
		check(Type == LMIT_Texture);
#if ALLOW_HQ_LIGHTMAPS
		return VirtualTexture;
#else
		return NULL;
#endif
	}

	const FVector4* GetScaleArray() const
	{
#if ALLOW_LQ_LIGHTMAPS && ALLOW_HQ_LIGHTMAPS
		return AllowsHighQualityLightmaps() ? HighQualityCoefficientScales : LowQualityCoefficientScales;
#elif ALLOW_HQ_LIGHTMAPS
		return HighQualityCoefficientScales;
#else
		return LowQualityCoefficientScales;
#endif
	}

	const FVector4* GetAddArray() const
	{
#if ALLOW_LQ_LIGHTMAPS && ALLOW_HQ_LIGHTMAPS
		return AllowsHighQualityLightmaps() ? HighQualityCoefficientAdds : LowQualityCoefficientAdds;
#elif ALLOW_HQ_LIGHTMAPS
		return HighQualityCoefficientAdds;
#else
		return LowQualityCoefficientAdds;
#endif
	}
	
	const FVector2D& GetCoordinateScale() const
	{
		check(Type == LMIT_Texture);
		return CoordinateScale;
	}
	const FVector2D& GetCoordinateBias() const
	{
		check(Type == LMIT_Texture);
		return CoordinateBias;
	}

	uint32 GetNumLightmapCoefficients() const
	{
#if ALLOW_LQ_LIGHTMAPS && ALLOW_HQ_LIGHTMAPS
#if PLATFORM_DESKTOP && (!(UE_BUILD_SHIPPING || UE_BUILD_TEST) || WITH_EDITOR)		// This is to allow for dynamic switching between simple and directional light maps in the PC editor
		if( !AllowsHighQualityLightmaps() )
		{
			return NUM_LQ_LIGHTMAP_COEF;
		}
#endif
		return NumLightmapCoefficients;
#elif ALLOW_HQ_LIGHTMAPS
		return NUM_HQ_LIGHTMAP_COEF;
#else
		return NUM_LQ_LIGHTMAP_COEF;
#endif
	}

	/**
	* @return true if high quality lightmaps are allowed
	*/
	FORCEINLINE bool AllowsHighQualityLightmaps() const
	{
#if ALLOW_LQ_LIGHTMAPS && ALLOW_HQ_LIGHTMAPS
		return bAllowHighQualityLightMaps;
#elif ALLOW_HQ_LIGHTMAPS
		return true;
#else
		return false;
#endif
	}

	/** These functions are used for the Dummy lightmap policy used in LightMap density view mode. */
	/** 
	 *	Set the type.
	 *
	 *	@param	InType				The type to set it to.
	 */
	void SetLightMapInteractionType(ELightMapInteractionType InType)
	{
		Type = InType;
	}
	/** 
	 *	Set the coordinate scale.
	 *
	 *	@param	InCoordinateScale	The scale to set it to.
	 */
	void SetCoordinateScale(const FVector2D& InCoordinateScale)
	{
		CoordinateScale = InCoordinateScale;
	}
	/** 
	 *	Set the coordinate bias.
	 *
	 *	@param	InCoordinateBias	The bias to set it to.
	 */
	void SetCoordinateBias(const FVector2D& InCoordinateBias)
	{
		CoordinateBias = InCoordinateBias;
	}

private:

#if ALLOW_HQ_LIGHTMAPS
	FVector4 HighQualityCoefficientScales[NUM_HQ_LIGHTMAP_COEF];
	FVector4 HighQualityCoefficientAdds[NUM_HQ_LIGHTMAP_COEF];
	const class ULightMapTexture2D* HighQualityTexture;
	const ULightMapTexture2D* SkyOcclusionTexture;
	const ULightMapTexture2D* AOMaterialMaskTexture;
	const ULightMapVirtualTexture2D* VirtualTexture;
#endif

#if ALLOW_LQ_LIGHTMAPS
	FVector4 LowQualityCoefficientScales[NUM_LQ_LIGHTMAP_COEF];
	FVector4 LowQualityCoefficientAdds[NUM_LQ_LIGHTMAP_COEF];
	const class ULightMapTexture2D* LowQualityTexture;
#endif

#if ALLOW_LQ_LIGHTMAPS && ALLOW_HQ_LIGHTMAPS
	bool bAllowHighQualityLightMaps;
	uint32 NumLightmapCoefficients;
#endif

	ELightMapInteractionType Type;

	FVector2D CoordinateScale;
	FVector2D CoordinateBias;
};

/** Information about the static shadowing information for a primitive. */
class FShadowMapInteraction
{
public:

	// Factory functions.
	static FShadowMapInteraction None()
	{
		FShadowMapInteraction Result;
		Result.Type = SMIT_None;
		return Result;
	}

	static FShadowMapInteraction GlobalVolume()
	{
		FShadowMapInteraction Result;
		Result.Type = SMIT_GlobalVolume;
		return Result;
	}

	static FShadowMapInteraction Texture(
		class UShadowMapTexture2D* InTexture,
		const FVector2D& InCoordinateScale,
		const FVector2D& InCoordinateBias,
		const bool* InChannelValid,
		const FVector4& InInvUniformPenumbraSize)
	{
		FShadowMapInteraction Result;
		Result.Type = SMIT_Texture;
		Result.ShadowTexture = InTexture;
		Result.CoordinateScale = InCoordinateScale;
		Result.CoordinateBias = InCoordinateBias;
		Result.InvUniformPenumbraSize = InInvUniformPenumbraSize;

		for (int Channel = 0; Channel < 4; Channel++)
		{
			Result.bChannelValid[Channel] = InChannelValid[Channel];
		}

		return Result;
	}

	static FShadowMapInteraction InitVirtualTexture(
		class ULightMapVirtualTexture2D* InTexture,
		const FVector2D& InCoordinateScale,
		const FVector2D& InCoordinateBias,
		const bool* InChannelValid,
		const FVector4& InInvUniformPenumbraSize)
	{
		FShadowMapInteraction Result;
		Result.Type = SMIT_Texture;
		Result.VirtualTexture = InTexture;
		Result.CoordinateScale = InCoordinateScale;
		Result.CoordinateBias = InCoordinateBias;
		Result.InvUniformPenumbraSize = InInvUniformPenumbraSize;
		for (int Channel = 0; Channel < 4; Channel++)
		{
			Result.bChannelValid[Channel] = InChannelValid[Channel];
		}

		return Result;
	}

	/** Default constructor. */
	FShadowMapInteraction() :
		ShadowTexture(nullptr),
		VirtualTexture(nullptr),
		InvUniformPenumbraSize(FVector4(0, 0, 0, 0)),
		Type(SMIT_None)
	{
		for (int Channel = 0; Channel < UE_ARRAY_COUNT(bChannelValid); Channel++)
		{
			bChannelValid[Channel] = false;
		}
	}

	// Accessors.
	EShadowMapInteractionType GetType() const { return Type; }

	UShadowMapTexture2D* GetTexture() const
	{
		checkSlow(Type == SMIT_Texture);
		return ShadowTexture;
	}

	const ULightMapVirtualTexture2D* GetVirtualTexture() const
	{
		checkSlow(Type == SMIT_Texture);
		return VirtualTexture;
	}

	const FVector2D& GetCoordinateScale() const
	{
		checkSlow(Type == SMIT_Texture);
		return CoordinateScale;
	}

	const FVector2D& GetCoordinateBias() const
	{
		checkSlow(Type == SMIT_Texture);
		return CoordinateBias;
	}

	bool GetChannelValid(int32 ChannelIndex) const
	{
		checkSlow(Type == SMIT_Texture);
		return bChannelValid[ChannelIndex];
	}

	inline FVector4 GetInvUniformPenumbraSize() const
	{
		return InvUniformPenumbraSize;
	}

private:
	UShadowMapTexture2D* ShadowTexture;
	const ULightMapVirtualTexture2D* VirtualTexture;
	FVector2D CoordinateScale;
	FVector2D CoordinateBias;
	bool bChannelValid[4];
	FVector4 InvUniformPenumbraSize;
	EShadowMapInteractionType Type;
};

class FLightMap;
class FShadowMap;

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLightmapResourceClusterShaderParameters,ENGINE_API)
	SHADER_PARAMETER_TEXTURE(Texture2D, LightMapTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, SkyOcclusionTexture) 
	SHADER_PARAMETER_TEXTURE(Texture2D, AOMaterialMaskTexture) 
	SHADER_PARAMETER_TEXTURE(Texture2D, StaticShadowTexture)
	SHADER_PARAMETER_SRV(Texture2D<float4>, VTLightMapTexture) // VT
	SHADER_PARAMETER_SRV(Texture2D<float4>, VTLightMapTexture_1) // VT
	SHADER_PARAMETER_SRV(Texture2D<float4>, VTSkyOcclusionTexture) // VT
	SHADER_PARAMETER_SRV(Texture2D<float4>, VTAOMaterialMaskTexture) // VT
	SHADER_PARAMETER_SRV(Texture2D<float4>, VTStaticShadowTexture) // VT
	SHADER_PARAMETER_SAMPLER(SamplerState, LightMapSampler) 
	SHADER_PARAMETER_SAMPLER(SamplerState, SkyOcclusionSampler) 
	SHADER_PARAMETER_SAMPLER(SamplerState, AOMaterialMaskSampler) 
	SHADER_PARAMETER_SAMPLER(SamplerState, StaticShadowTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint4>, LightmapVirtualTexturePageTable0) // VT
	SHADER_PARAMETER_TEXTURE(Texture2D<uint4>, LightmapVirtualTexturePageTable1) // VT
END_GLOBAL_SHADER_PARAMETER_STRUCT()

class FLightmapClusterResourceInput
{
public:

	FLightmapClusterResourceInput()
	{
		LightMapTextures[0] = nullptr;
		LightMapTextures[1] = nullptr;
		SkyOcclusionTexture = nullptr;
		AOMaterialMaskTexture = nullptr;
		LightMapVirtualTexture = nullptr;
		ShadowMapTexture = nullptr;
	}

	const UTexture2D* LightMapTextures[2];
	const UTexture2D* SkyOcclusionTexture;
	const UTexture2D* AOMaterialMaskTexture;
	const ULightMapVirtualTexture2D* LightMapVirtualTexture;
	const UTexture2D* ShadowMapTexture;

	friend uint32 GetTypeHash(const FLightmapClusterResourceInput& Cluster)
	{
		// TODO - LightMapVirtualTexture needed here? What about Sky/AO textures?  Or is it enough to just check LightMapTexture[n]?
		return
			PointerHash(Cluster.LightMapTextures[0],
			PointerHash(Cluster.LightMapTextures[1],
			PointerHash(Cluster.LightMapVirtualTexture,
			PointerHash(Cluster.ShadowMapTexture))));
	}

	bool operator==(const FLightmapClusterResourceInput& Rhs) const
	{
		return LightMapTextures[0] == Rhs.LightMapTextures[0]
			&& LightMapTextures[1] == Rhs.LightMapTextures[1]
			&& SkyOcclusionTexture == Rhs.SkyOcclusionTexture
			&& AOMaterialMaskTexture == Rhs.AOMaterialMaskTexture
			&& LightMapVirtualTexture == Rhs.LightMapVirtualTexture
			&& ShadowMapTexture == Rhs.ShadowMapTexture;
	}
};

ENGINE_API void GetLightmapClusterResourceParameters(
	ERHIFeatureLevel::Type FeatureLevel, 
	const FLightmapClusterResourceInput& Input,
	IAllocatedVirtualTexture* AllocatedVT,
	FLightmapResourceClusterShaderParameters& Parameters);

class FDefaultLightmapResourceClusterUniformBuffer : public TUniformBuffer< FLightmapResourceClusterShaderParameters >
{
	typedef TUniformBuffer< FLightmapResourceClusterShaderParameters > Super;
public:
	virtual void InitDynamicRHI() override;
};

ENGINE_API extern TGlobalResource< FDefaultLightmapResourceClusterUniformBuffer > GDefaultLightmapResourceClusterUniformBuffer;

/**
 * An interface to cached lighting for a specific mesh.
 */
class FLightCacheInterface
{
public:
	FLightCacheInterface()
		: bGlobalVolumeLightmap(false)
		, LightMap(nullptr)
		, ShadowMap(nullptr)
		, ResourceCluster(nullptr)
	{
	}

	virtual ~FLightCacheInterface()
	{
	}

	// @param LightSceneProxy must not be 0
	virtual FLightInteraction GetInteraction(const class FLightSceneProxy* LightSceneProxy) const = 0;

	// helper function to implement GetInteraction(), call after checking for this: if(LightSceneProxy->HasStaticShadowing())
	// @param LightSceneProxy same as in GetInteraction(), must not be 0
	ENGINE_API ELightInteractionType GetStaticInteraction(const FLightSceneProxy* LightSceneProxy, const TArray<FGuid>& IrrelevantLights) const;
	
	ENGINE_API void CreatePrecomputedLightingUniformBuffer_RenderingThread(ERHIFeatureLevel::Type FeatureLevel);

	ENGINE_API bool GetVirtualTextureLightmapProducer(ERHIFeatureLevel::Type FeatureLevel, FVirtualTextureProducerHandle& OutProducerHandle);

	// @param InLightMap may be 0
	void SetLightMap(const FLightMap* InLightMap)
	{
		LightMap = InLightMap;
	}

	void SetResourceCluster(const FLightmapResourceCluster* InResourceCluster)
	{
		checkSlow(InResourceCluster);
		ResourceCluster = InResourceCluster;
	}

	// @return may be 0
	const FLightMap* GetLightMap() const
	{
		return LightMap;
	}

	// @param InShadowMap may be 0
	void SetShadowMap(const FShadowMap* InShadowMap)
	{
		ShadowMap = InShadowMap;
	}

	// @return may be 0
	const FShadowMap* GetShadowMap() const
	{
		return ShadowMap;
	}

	const FLightmapResourceCluster* GetResourceCluster() const
	{
		return ResourceCluster;
	}

	void SetGlobalVolumeLightmap(bool bInGlobalVolumeLightmap)
	{
		bGlobalVolumeLightmap = bInGlobalVolumeLightmap;
	}

	FRHIUniformBuffer* GetPrecomputedLightingBuffer() const
	{
		return PrecomputedLightingUniformBuffer;
	}

	void SetPrecomputedLightingBuffer(FRHIUniformBuffer* InPrecomputedLightingUniformBuffer)
	{
		PrecomputedLightingUniformBuffer = InPrecomputedLightingUniformBuffer;
	}

	ENGINE_API FLightMapInteraction GetLightMapInteraction(ERHIFeatureLevel::Type InFeatureLevel) const;

	ENGINE_API FShadowMapInteraction GetShadowMapInteraction(ERHIFeatureLevel::Type InFeatureLevel) const;

private:

	bool bGlobalVolumeLightmap;

	// The light-map used by the element. may be 0
	const FLightMap* LightMap;

	// The shadowmap used by the element, may be 0
	const FShadowMap* ShadowMap;

	const FLightmapResourceCluster* ResourceCluster;

	/** The uniform buffer holding mapping the lightmap policy resources. */
	FUniformBufferRHIRef PrecomputedLightingUniformBuffer;
};


template<typename TPendingTextureType>
class FAsyncEncode : public IQueuedWork
{
private:
	TPendingTextureType* PendingTexture;
	FThreadSafeCounter& Counter;
	ULevel* LightingScenario;
	class ITextureCompressorModule* Compressor;

public:

	FAsyncEncode(TPendingTextureType* InPendingTexture, ULevel* InLightingScenario, FThreadSafeCounter& InCounter, ITextureCompressorModule* InCompressor) : PendingTexture(nullptr), Counter(InCounter), Compressor(InCompressor)
	{
		LightingScenario = InLightingScenario;
		PendingTexture = InPendingTexture;
	}

	void Abandon()
	{
		PendingTexture->StartEncoding(LightingScenario, Compressor);
		Counter.Decrement();
	}

	void DoThreadedWork()
	{
		PendingTexture->StartEncoding(LightingScenario, Compressor);
		Counter.Decrement();
	}
};



// Information about a single shadow cascade.
class FShadowCascadeSettings
{
public:
	// The following 3 floats represent the view space depth of the split planes for this cascade.
	// SplitNear <= FadePlane <= SplitFar

	// The distance from the camera to the near split plane, in world units (linear).
	float SplitNear;

	// The distance from the camera to the far split plane, in world units (linear).
	float SplitFar;

	// in world units (linear).
	float SplitNearFadeRegion;

	// in world units (linear).
	float SplitFarFadeRegion;

	// ??
	// The distance from the camera to the start of the fade region, in world units (linear).
	// The area between the fade plane and the far split plane is blended to smooth between cascades.
	float FadePlaneOffset;

	// The length of the fade region (SplitFar - FadePlaneOffset), in world units (linear).
	float FadePlaneLength;

	// The accurate bounds of the cascade used for primitive culling.
	FConvexVolume ShadowBoundsAccurate;

	FPlane NearFrustumPlane;
	FPlane FarFrustumPlane;

	/** When enabled, the cascade only renders objects marked with bCastFarShadows enabled (e.g. Landscape). */
	bool bFarShadowCascade;

	/** 
	 * Index of the split if this is a whole scene shadow from a directional light, 
	 * Or index of the direction if this is a whole scene shadow from a point light, otherwise INDEX_NONE. 
	 */
	int32 ShadowSplitIndex;

	/** Strength of depth bias across cascades. */
	float CascadeBiasDistribution;
	
	FShadowCascadeSettings()
		: SplitNear(0.0f)
		, SplitFar(WORLD_MAX)
		, SplitNearFadeRegion(0.0f)
		, SplitFarFadeRegion(0.0f)
		, FadePlaneOffset(SplitFar)
		, FadePlaneLength(SplitFar - FadePlaneOffset)
		, bFarShadowCascade(false)
		, ShadowSplitIndex(INDEX_NONE)
		, CascadeBiasDistribution(1)
	{
	}
};

/** A projected shadow transform. */
class ENGINE_API FProjectedShadowInitializer
{
public:

	/** A translation that is applied to world-space before transforming by one of the shadow matrices. */
	FVector PreShadowTranslation;

	FMatrix WorldToLight;
	/** Non-uniform scale to be applied after WorldToLight. */
	FVector Scales;

	FVector FaceDirection;
	FBoxSphereBounds SubjectBounds;
	FVector4 WAxis;
	float MinLightW;
	float MaxDistanceToCastInLightW;

	/** Default constructor. */
	FProjectedShadowInitializer()
	{}

	bool IsCachedShadowValid(const FProjectedShadowInitializer& CachedShadow) const
	{
		return PreShadowTranslation == CachedShadow.PreShadowTranslation
			&& WorldToLight == CachedShadow.WorldToLight
			&& Scales == CachedShadow.Scales
			&& FaceDirection == CachedShadow.FaceDirection
			&& SubjectBounds.Origin == CachedShadow.SubjectBounds.Origin
			&& SubjectBounds.BoxExtent == CachedShadow.SubjectBounds.BoxExtent
			&& SubjectBounds.SphereRadius == CachedShadow.SubjectBounds.SphereRadius
			&& WAxis == CachedShadow.WAxis
			&& MinLightW == CachedShadow.MinLightW
			&& MaxDistanceToCastInLightW == CachedShadow.MaxDistanceToCastInLightW;
	}
};

/** Information needed to create a per-object projected shadow. */
class ENGINE_API FPerObjectProjectedShadowInitializer : public FProjectedShadowInitializer
{
public:

};

/** Information needed to create a whole scene projected shadow. */
class ENGINE_API FWholeSceneProjectedShadowInitializer : public FProjectedShadowInitializer
{
public:
	FShadowCascadeSettings CascadeSettings;
	bool bOnePassPointLightShadow;
	bool bRayTracedDistanceField;

	FWholeSceneProjectedShadowInitializer() :
		bOnePassPointLightShadow(false),
		bRayTracedDistanceField(false)
	{}

	bool IsCachedShadowValid(const FWholeSceneProjectedShadowInitializer& CachedShadow) const
	{
		return FProjectedShadowInitializer::IsCachedShadowValid((const FProjectedShadowInitializer&)CachedShadow)
			&& bOnePassPointLightShadow == CachedShadow.bOnePassPointLightShadow
			&& bRayTracedDistanceField == CachedShadow.bRayTracedDistanceField;
	}
};

inline bool DoesPlatformSupportDistanceFields(const FStaticShaderPlatform Platform)
{
	return Platform == SP_PCD3D_SM5
		|| Platform == SP_PS4
		|| IsMetalSM5Platform(Platform)
		|| Platform == SP_XBOXONE_D3D12
		|| IsVulkanSM5Platform(Platform)
		|| Platform == SP_SWITCH
		|| Platform == SP_SWITCH_FORWARD
		|| FDataDrivenShaderPlatformInfo::GetSupportsDistanceFields(Platform);
}

inline bool DoesPlatformSupportDistanceFieldShadowing(EShaderPlatform Platform)
{
	return DoesPlatformSupportDistanceFields(Platform);
}

inline bool DoesPlatformSupportDistanceFieldAO(EShaderPlatform Platform)
{
	return DoesPlatformSupportDistanceFields(Platform);
}

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileReflectionCaptureShaderParameters,ENGINE_API)
	SHADER_PARAMETER(FVector4, Params) // x - inv average brightness, y - sky cubemap max mip, zw - unused
	SHADER_PARAMETER_TEXTURE(TextureCube, Texture)
	SHADER_PARAMETER_SAMPLER(SamplerState, TextureSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

class FDefaultMobileReflectionCaptureUniformBuffer : public TUniformBuffer<FMobileReflectionCaptureShaderParameters>
{
	typedef TUniformBuffer<FMobileReflectionCaptureShaderParameters> Super;
public:
	virtual void InitDynamicRHI() override;
};

ENGINE_API extern TGlobalResource<FDefaultMobileReflectionCaptureUniformBuffer> GDefaultMobileReflectionCaptureUniformBuffer;

/** Represents a USkyLightComponent to the rendering thread. */
class ENGINE_API FSkyLightSceneProxy
{
public:

	/** Initialization constructor. */
	FSkyLightSceneProxy(const class USkyLightComponent* InLightComponent);

	void Initialize(
		float InBlendFraction, 
		const FSHVectorRGB3* InIrradianceEnvironmentMap, 
		const FSHVectorRGB3* BlendDestinationIrradianceEnvironmentMap,
		const float* InAverageBrightness,
		const float* BlendDestinationAverageBrightness);

	const USkyLightComponent* LightComponent;
	FTexture* ProcessedTexture;
	float BlendFraction;
	float SkyDistanceThreshold;
	FTexture* BlendDestinationProcessedTexture;
	uint8 bCastShadows:1;
	uint8 bWantsStaticShadowing:1;
	uint8 bHasStaticLighting:1;
	uint8 bCastVolumetricShadow:1;
	uint8 bCastRayTracedShadow:1;
	uint8 bAffectReflection:1;
	uint8 bAffectGlobalIllumination:1;
	uint8 bTransmission:1;
	TEnumAsByte<EOcclusionCombineMode> OcclusionCombineMode;
	float AverageBrightness;
	float IndirectLightingIntensity;
	float VolumetricScatteringIntensity;
	FSHVectorRGB3 IrradianceEnvironmentMap;
	float OcclusionMaxDistance;
	float Contrast;
	float OcclusionExponent;
	float MinOcclusion;
	FLinearColor OcclusionTint;
	bool bCloudAmbientOcclusion;
	float CloudAmbientOcclusionExtent;
	float CloudAmbientOcclusionStrength;
	float CloudAmbientOcclusionMapResolutionScale;
	float CloudAmbientOcclusionApertureScale;
	int32 SamplesPerPixel;
	bool bRealTimeCaptureEnabled;
	FVector CapturePosition;
	uint32 CaptureCubeMapResolution;
	FLinearColor LowerHemisphereColor;
	bool bLowerHemisphereIsSolidColor;
#if RHI_RAYTRACING
	FSkyLightImportanceSamplingData* ImportanceSamplingData;
#endif

	bool IsMovable() { return bMovable; }

	void SetLightColor(const FLinearColor& InColor)
	{
		LightColor = InColor;
	}
	FLinearColor GetEffectiveLightColor() const;

private:
	FLinearColor LightColor;
	const uint8 bMovable : 1;
};

/** Represents a USkyAtmosphereComponent to the rendering thread. */
class ENGINE_API FSkyAtmosphereSceneProxy
{
public:

	// Initialization constructor.
	FSkyAtmosphereSceneProxy(const USkyAtmosphereComponent* InComponent);
	~FSkyAtmosphereSceneProxy();

	FLinearColor GetSkyLuminanceFactor() const { return SkyLuminanceFactor; }
	FLinearColor GetTransmittanceAtZenith() const { return TransmittanceAtZenith; };
	float GetAerialPespectiveViewDistanceScale() const { return AerialPespectiveViewDistanceScale; }
	float GetHeightFogContribution() const { return HeightFogContribution; }
	float GetAerialPerspectiveStartDepthKm() const { return AerialPerspectiveStartDepthKm; }
	float GetTraceSampleCountScale() const { return TraceSampleCountScale; }

	const FAtmosphereSetup& GetAtmosphereSetup() const { return AtmosphereSetup; }

	void UpdateTransform(const FTransform& ComponentTransform, uint8 TranformMode) { AtmosphereSetup.UpdateTransform(ComponentTransform, TranformMode); }
	void ApplyWorldOffset(const FVector& InOffset) { AtmosphereSetup.ApplyWorldOffset(InOffset); }

	FVector GetAtmosphereLightDirection(int32 AtmosphereLightIndex, const FVector& DefaultDirection) const;

	bool bStaticLightingBuilt;
	FSkyAtmosphereRenderSceneInfo* RenderSceneInfo;
private:

	FAtmosphereSetup AtmosphereSetup;

	FLinearColor TransmittanceAtZenith;
	FLinearColor SkyLuminanceFactor;
	float AerialPespectiveViewDistanceScale;
	float HeightFogContribution;
	float AerialPerspectiveStartDepthKm;
	float TraceSampleCountScale;

	bool OverrideAtmosphericLight[NUM_ATMOSPHERE_LIGHTS];
	FVector OverrideAtmosphericLightDirection[NUM_ATMOSPHERE_LIGHTS];
};

/** Shader paraneter structure for rendering lights. */
BEGIN_SHADER_PARAMETER_STRUCT(FLightShaderParameters, ENGINE_API)
	// Position of the light in the world space.
	SHADER_PARAMETER(FVector, Position)

	// 1 / light's falloff radius from Position.
	SHADER_PARAMETER(float, InvRadius)

	// Color of the light.
	SHADER_PARAMETER(FVector, Color)

	// The exponent for the falloff of the light intensity from the distance.
	SHADER_PARAMETER(float, FalloffExponent)

	// Direction of the light if applies.
	SHADER_PARAMETER(FVector, Direction)

	// Factor to applies on the specular.
	SHADER_PARAMETER(float, SpecularScale)

	// One tangent of the light if applies.
	// Note: BiTangent is on purpose not stored for memory optimisation purposes.
	SHADER_PARAMETER(FVector, Tangent)

	// Radius of the point light.
	SHADER_PARAMETER(float, SourceRadius)

	// Dimensions of the light, for spot light, but also
	SHADER_PARAMETER(FVector2D, SpotAngles)

	// Radius of the soft source.
	SHADER_PARAMETER(float, SoftSourceRadius)

	// Other dimensions of the light source for rect light specifically.
	SHADER_PARAMETER(float, SourceLength)

	// Barn door angle for rect light
	SHADER_PARAMETER(float, RectLightBarnCosAngle)

	// Barn door length for rect light
	SHADER_PARAMETER(float, RectLightBarnLength)

	// Texture of the rect light.
	SHADER_PARAMETER_TEXTURE(Texture2D, SourceTexture)
END_SHADER_PARAMETER_STRUCT()


/** 
 * Encapsulates the data which is used to render a light by the rendering thread. 
 * The constructor is called from the game thread, and after that the rendering thread owns the object.
 * FLightSceneProxy is in the engine module and is subclassed to implement various types of lights.
 */
class ENGINE_API FLightSceneProxy
{
public:

	/** Initialization constructor. */
	FLightSceneProxy(const ULightComponent* InLightComponent);

	virtual ~FLightSceneProxy() 
	{
	}

	/**
	 * Tests whether the light affects the given bounding volume.
	 * @param Bounds - The bounding volume to test.
	 * @return True if the light affects the bounding volume
	 */
	virtual bool AffectsBounds(const FBoxSphereBounds& Bounds) const
	{
		return true;
	}

	virtual FSphere GetBoundingSphere() const
	{
		// Directional lights will have a radius of WORLD_MAX
		return FSphere(FVector::ZeroVector, WORLD_MAX);
	}

	/** @return radius of the light */
	virtual float GetRadius() const { return FLT_MAX; }
	virtual float GetOuterConeAngle() const { return 0.0f; }
	virtual float GetSourceRadius() const { return 0.0f; }
	virtual bool IsInverseSquared() const { return true; }
	virtual bool IsRectLight() const { return false; }
	virtual bool HasSourceTexture() const { return false; }
	virtual float GetLightSourceAngle() const { return 0.0f; }
	virtual float GetShadowSourceAngleFactor() const { return 1.0f; }
	virtual float GetTraceDistance() const { return 0.0f; }
	virtual float GetEffectiveScreenRadius(const FViewMatrices& ShadowViewMatrices) const { return 0.0f; }

	virtual FVector2D GetLightShaftConeParams() const
	{
		return FVector2D::ZeroVector;
	}

	/** Accesses parameters needed for rendering the light. */
	virtual void GetLightShaderParameters(FLightShaderParameters& PathTracingLightParameters) const {}

	virtual FVector2D GetDirectionalLightDistanceFadeParameters(ERHIFeatureLevel::Type InFeatureLevel, bool bPrecomputedLightingIsValid, int32 MaxNearCascades) const
	{
		return FVector2D(0, 0);
	}

	virtual bool GetLightShaftOcclusionParameters(float& OutOcclusionMaskDarkness, float& OutOcclusionDepthRange) const
	{
		OutOcclusionMaskDarkness = 0;
		OutOcclusionDepthRange = 1;
		return false;
	}

	virtual FVector GetLightPositionForLightShafts(FVector ViewOrigin) const
	{
		return GetPosition();
	}

	/**
	 * Sets up a projected shadow initializer for shadows from the entire scene.
	 * @return True if the whole-scene projected shadow should be used.
	 */
	virtual bool GetWholeSceneProjectedShadowInitializer(const FSceneViewFamily& ViewFamily, TArray<class FWholeSceneProjectedShadowInitializer, TInlineAllocator<6> >& OutInitializers) const
	{
		return false;
	}

	/** Whether this light should create per object shadows for dynamic objects. */
	virtual bool ShouldCreatePerObjectShadowsForDynamicObjects() const;

	/** Whether this light should create CSM for dynamic objects only (forward renderer) */
	virtual bool UseCSMForDynamicObjects() const;

	/** Returns the number of view dependent shadows this light will create, not counting distance field shadow cascades. */
	virtual uint32 GetNumViewDependentWholeSceneShadows(const FSceneView& View, bool bPrecomputedLightingIsValid) const { return 0; }

	/**
	 * Sets up a projected shadow initializer that's dependent on the current view for shadows from the entire scene.
	 * @param InCascadeIndex cascade index or INDEX_NONE for the distance field cascade
	 * @return True if the whole-scene projected shadow should be used.
	 */
	virtual bool GetViewDependentWholeSceneProjectedShadowInitializer(
		const class FSceneView& View, 
		int32 InCascadeIndex, 
		bool bPrecomputedLightingIsValid,
		class FWholeSceneProjectedShadowInitializer& OutInitializer) const
	{
		return false;
	}

	/**
	 * Sets up a projected shadow initializer for a reflective shadow map that's dependent on the current view for shadows from the entire scene.
	 * @return True if the whole-scene projected shadow should be used.
	 */
	virtual bool GetViewDependentRsmWholeSceneProjectedShadowInitializer(
		const class FSceneView& View, 
		const FBox& LightPropagationVolumeBounds,
		class FWholeSceneProjectedShadowInitializer& OutInitializer ) const
	{
		return false;
	}

	/**
	 * Sets up a projected shadow initializer for the given subject.
	 * @param SubjectBounds - The bounding volume of the subject.
	 * @param OutInitializer - Upon successful return, contains the initialization parameters for the shadow.
	 * @return True if a projected shadow should be cast by this subject-light pair.
	 */
	virtual bool GetPerObjectProjectedShadowInitializer(const FBoxSphereBounds& SubjectBounds,class FPerObjectProjectedShadowInitializer& OutInitializer) const
	{
		return false;
	}

	// @param InCascadeIndex cascade index or INDEX_NONE for the distance field cascade
	// @param OutCascadeSettings can be 0
	virtual FSphere GetShadowSplitBounds(const class FSceneView& View, int32 InCascadeIndex, bool bPrecomputedLightingIsValid, FShadowCascadeSettings* OutCascadeSettings) const { return FSphere(FVector::ZeroVector, 0); }
	virtual FSphere GetShadowSplitBoundsDepthRange(const FSceneView& View, FVector ViewOrigin, float SplitNear, float SplitFar, FShadowCascadeSettings* OutCascadeSettings) const { return FSphere(FVector::ZeroVector, 0); }

	virtual bool GetScissorRect(FIntRect& ScissorRect, const FSceneView& View, const FIntRect& ViewRect) const
	{
		ScissorRect = ViewRect;
		return false;
	}

	// @param OutScissorRect the scissor rect used if one is set
	// @return whether a scissor rect is set
	virtual bool SetScissorRect(FRHICommandList& RHICmdList, const FSceneView& View, const FIntRect& ViewRect, FIntRect* OutScissorRect = nullptr) const
	{
		return false;
	}

	virtual bool ShouldCreateRayTracedCascade(ERHIFeatureLevel::Type Type, bool bPrecomputedLightingIsValid, int32 MaxNearCascades) const { return false; }

	// Accessors.
	float GetUserShadowBias() const { return ShadowBias; }
	float GetUserShadowSlopeBias() const { return ShadowSlopeBias; }

	/** 
	 * Note: The Rendering thread must not dereference UObjects!  
	 * The game thread owns UObject state and may be writing to them at any time.
	 * Mirror the data in the scene proxy and access that instead.
	 */
	inline const ULightComponent* GetLightComponent() const { return LightComponent; }
	inline FSceneInterface* GetSceneInterface() const { return SceneInterface; }
	inline FLightSceneInfo* GetLightSceneInfo() const { return LightSceneInfo; }
	inline const FMatrix& GetWorldToLight() const { return WorldToLight; }
	inline const FMatrix& GetLightToWorld() const { return LightToWorld; }
	inline FVector GetDirection() const { return FVector(WorldToLight.M[0][0],WorldToLight.M[1][0],WorldToLight.M[2][0]); }
	inline FVector GetOrigin() const { return LightToWorld.GetOrigin(); }
	inline FVector4 GetPosition() const { return Position; }
	inline const FLinearColor& GetColor() const { return Color; }
	inline float GetIndirectLightingScale() const { return IndirectLightingScale; }
	inline float GetVolumetricScatteringIntensity() const { return VolumetricScatteringIntensity; }
	inline float GetShadowResolutionScale() const { return ShadowResolutionScale; }
	inline FGuid GetLightGuid() const { return LightGuid; }
	inline float GetShadowSharpen() const { return ShadowSharpen; }
	inline float GetContactShadowLength() const { return ContactShadowLength; }
	inline bool IsContactShadowLengthInWS() const { return bContactShadowLengthInWS; }
	inline float GetSpecularScale() const { return SpecularScale; }
	inline FVector GetLightFunctionScale() const { return LightFunctionScale; }
	inline float GetLightFunctionFadeDistance() const { return LightFunctionFadeDistance; }
	inline float GetLightFunctionDisabledBrightness() const { return LightFunctionDisabledBrightness; }
	inline UTextureLightProfile* GetIESTexture() const { return IESTexture; }
	inline FTexture* GetIESTextureResource() const { return IESTexture ? IESTexture->Resource : 0; }
	inline const FMaterialRenderProxy* GetLightFunctionMaterial() const { return LightFunctionMaterial; }
	inline bool IsMovable() const { return bMovable; }
	inline bool HasStaticLighting() const { return bStaticLighting; }
	inline bool HasStaticShadowing() const { return bStaticShadowing; }
	inline bool CastsDynamicShadow() const { return bCastDynamicShadow; }
	inline bool CastsStaticShadow() const { return bCastStaticShadow; }
	inline bool CastsTranslucentShadows() const { return bCastTranslucentShadows; }
	inline bool CastsVolumetricShadow() const { return bCastVolumetricShadow; }
	inline bool CastsHairStrandsDeepShadow() const { return bCastHairStrandsDeepShadow; }
	inline bool CastsRaytracedShadow() const { return bCastRaytracedShadow; }
	inline bool AffectReflection() const { return bAffectReflection; }
	inline bool AffectGlobalIllumination() const { return bAffectGlobalIllumination; }
	inline bool CastsShadowsFromCinematicObjectsOnly() const { return bCastShadowsFromCinematicObjectsOnly; }
	inline bool CastsModulatedShadows() const { return bCastModulatedShadows; }
	inline const FLinearColor& GetModulatedShadowColor() const { return ModulatedShadowColor; }
	inline const float GetShadowAmount() const { return ShadowAmount; }
	inline bool AffectsTranslucentLighting() const { return bAffectTranslucentLighting; }
	inline bool Transmission() const { return bTransmission; }
	inline bool UseRayTracedDistanceFieldShadows() const { return bUseRayTracedDistanceFieldShadows; }
	inline float GetRayStartOffsetDepthScale() const { return RayStartOffsetDepthScale; }
	inline bool IsTiledDeferredLightingSupported() const { return bTiledDeferredLightingSupported;  }
	inline uint8 GetLightType() const { return LightType; }
	inline uint8 GetLightingChannelMask() const { return LightingChannelMask; }
	inline FName GetComponentName() const { return ComponentName; }
	inline FName GetLevelName() const { return LevelName; }
	FORCEINLINE TStatId GetStatId() const 
	{ 
		return StatId; 
	}	
	inline int32 GetShadowMapChannel() const { return ShadowMapChannel; }
	inline int32 GetPreviewShadowMapChannel() const { return PreviewShadowMapChannel; }

	inline bool HasReflectiveShadowMap() const { return bHasReflectiveShadowMap; }
	inline bool NeedsLPVInjection() const { return bAffectDynamicIndirectLighting; }
	inline const class FStaticShadowDepthMap* GetStaticShadowDepthMap() const { return StaticShadowDepthMap; }

	inline bool GetForceCachedShadowsForMovablePrimitives() const { return bForceCachedShadowsForMovablePrimitives; }

	inline uint32 GetSamplesPerPixel() const { return SamplesPerPixel; }

	/**
	 * Shifts light position and all relevant data by an arbitrary delta.
	 * Called on world origin changes
	 * @param InOffset - The delta to shift by
	 */
	virtual void ApplyWorldOffset(FVector InOffset);

	virtual float GetMaxDrawDistance() const { return 0.0f; }
	virtual float GetFadeRange() const { return 0.0f; }

	// Atmosphere / Fog related functions.

	inline bool IsUsedAsAtmosphereSunLight() const { return bUsedAsAtmosphereSunLight; }
	inline uint8 GetAtmosphereSunLightIndex() const { return AtmosphereSunLightIndex; }
	inline FLinearColor GetAtmosphereSunDiskColorScale() const { return AtmosphereSunDiskColorScale; }
	virtual void SetAtmosphereRelatedProperties(FLinearColor TransmittanceFactor, FLinearColor SunOuterSpaceLuminance, bool bApplyAtmosphereTransmittanceToLightShaderParamIn) {}
	virtual FLinearColor GetOuterSpaceLuminance() const { return FLinearColor::White; }
	virtual FLinearColor GetTransmittanceFactor() const { return FLinearColor::White; }
	static float GetSunOnEarthHalfApexAngleRadian() 
	{ 
		const float SunOnEarthApexAngleDegree = 0.545f;	// Apex angle == angular diameter
		return 0.5f * SunOnEarthApexAngleDegree * PI / 180.0f;
	}
	/**
	 * @return the light half apex angle (half angular diameter) in radian.
	 */
	virtual float GetSunLightHalfApexAngleRadian() const { return GetSunOnEarthHalfApexAngleRadian() ; }

	virtual bool GetCastShadowsOnClouds() const { return false; }
	virtual bool GetCastShadowsOnAtmosphere() const { return false; }
	virtual bool GetCastCloudShadows() const { return false; }
	virtual float GetCloudShadowExtent() const { return 1.0f; }
	virtual float GetCloudShadowMapResolutionScale() const { return 1.0f; }
	virtual float GetCloudShadowStrength() const { return 1.0f; }
	virtual FLinearColor GetCloudScatteredLuminanceScale() const { return FLinearColor::White; }
	virtual bool GetUsePerPixelAtmosphereTransmittance() const { return false; }

protected:

	friend class FScene;

	/** The light component. */
	const ULightComponent* LightComponent;

	/** The scene the primitive is in. */
	FSceneInterface* SceneInterface;

	/** The homogenous position of the light. */
	FVector4 Position;

	/** The light color. */
	FLinearColor Color;

	/** A transform from world space into light space. */
	FMatrix WorldToLight;

	/** A transform from light space into world space. */
	FMatrix LightToWorld;

	/** The light's scene info. */
	class FLightSceneInfo* LightSceneInfo;

	/** Scale for indirect lighting from this light.  When 0, indirect lighting is disabled. */
	float IndirectLightingScale;

	/** Scales this light's intensity for volumetric scattering. */
	float VolumetricScatteringIntensity;

	float ShadowResolutionScale;

	/** User setting from light component, 0:no bias, 0.5:reasonable, larger object might appear to float */
	float ShadowBias;

	/** User setting from light component, 0:no bias, 0.5:reasonable, larger object might appear to float */
	float ShadowSlopeBias;

	/** Sharpen shadow filtering */
	float ShadowSharpen;

	/** Length of screen space ray trace for sharp contact shadows. */
	float ContactShadowLength;

	/** Specular scale */
	float SpecularScale;

	/** The light's persistent shadowing GUID. */
	FGuid LightGuid;

	/** 
	 * Shadow map channel which is used to match up with the appropriate static shadowing during a deferred shading pass.
	 * This is generated during a lighting build.
	 */
	int32 ShadowMapChannel;

	/** Transient shadowmap channel used to preview the results of stationary light shadowmap packing. */
	int32 PreviewShadowMapChannel;

	float RayStartOffsetDepthScale;

	const class FStaticShadowDepthMap* StaticShadowDepthMap;

	/** Light function parameters. */
	FVector	LightFunctionScale;
	float LightFunctionFadeDistance;
	float LightFunctionDisabledBrightness;
	const FMaterialRenderProxy* LightFunctionMaterial;

	/**
	 * IES texture (light profiles from real world measured data)
	 * We are safe to store a U pointer as those objects get deleted deferred, storing an FTexture pointer would crash if we recreate the texture 
	 */
	UTextureLightProfile* IESTexture;

	/** True: length of screen space ray trace for sharp contact shadows is in world space. False: in screen space. */
	uint8 bContactShadowLengthInWS : 1;

	/* True if the light's Mobility is set to Movable. */
	const uint8 bMovable : 1;

	/**
	 * Return True if a light's parameters as well as its position is static during gameplay, and can thus use static lighting.
	 * A light with HasStaticLighting() == true will always have HasStaticShadowing() == true as well.
	 */
	const uint8 bStaticLighting : 1;

	/** 
	 * Whether the light has static direct shadowing.  
	 * The light may still have dynamic brightness and color. 
	 * The light may or may not also have static lighting.
	 */
	const uint8 bStaticShadowing : 1;

	/** True if the light casts dynamic shadows. */
	const uint8 bCastDynamicShadow : 1;

	/** True if the light casts static shadows. */
	const uint8 bCastStaticShadow : 1;

	/** Whether the light is allowed to cast dynamic shadows from translucency. */
	const uint8 bCastTranslucentShadows : 1;

	/** Whether light from this light transmits through surfaces with subsurface scattering profiles. Requires light to be movable. */
	const uint8 bTransmission : 1;

	const uint8 bCastVolumetricShadow : 1;
	const uint8 bCastHairStrandsDeepShadow : 1;
	const uint8 bCastShadowsFromCinematicObjectsOnly : 1;

	const uint8 bForceCachedShadowsForMovablePrimitives : 1;

	/** Whether the light shadows are computed with shadow-mapping or ray-tracing (when available). */
	const uint8 bCastRaytracedShadow : 1;

	/** Whether the light affects objects in reflections, when ray-traced reflection is enabled. */
	const uint8 bAffectReflection : 1;

	/** Whether the light affects global illumination, when ray-traced global illumination is enabled. */
	const uint8 bAffectGlobalIllumination : 1;

	/** Whether the light affects translucency or not.  Disabling this can save GPU time when there are many small lights. */
	const uint8 bAffectTranslucentLighting : 1;

	/** Whether to consider light as a sunlight for atmospheric scattering and exponential height fog. */
	const uint8 bUsedAsAtmosphereSunLight : 1;

	/** Does the light have dynamic GI? */
	const uint8 bAffectDynamicIndirectLighting : 1;
	const uint8 bHasReflectiveShadowMap : 1;

	/** Whether to use ray traced distance field area shadows. */
	const uint8 bUseRayTracedDistanceFieldShadows : 1;

	/** Whether the light will cast modulated shadows when using the forward renderer (mobile). */
	uint8 bCastModulatedShadows : 1;

	/** Whether to render csm shadows for movable objects only (mobile). */
	uint8 bUseWholeSceneCSMForMovableObjects : 1;

	/** Whether the light supports rendering in tiled deferred pass */
	uint8 bTiledDeferredLightingSupported : 1;

	/** The index of the atmospheric light. Multiple lights can be considered when computing the sky/atmospheric scattering. */
	const uint8 AtmosphereSunLightIndex;

	const FLinearColor AtmosphereSunDiskColorScale;

	/** The light type (ELightComponentType) */
	const uint8 LightType;

	uint8 LightingChannelMask;

	/** Used for dynamic stats */
	TStatId StatId;

	/** The name of the light component. */
	FName ComponentName;

	/** The name of the level the light is in. */
	FName LevelName;

	/** Only for whole scene directional lights, if FarShadowCascadeCount > 0 and FarShadowDistance >= WholeSceneDynamicShadowRadius, where far shadow cascade should end. */
	float FarShadowDistance;

	/** Only for whole scene directional lights, 0: no FarShadowCascades, otherwise the count of cascades between WholeSceneDynamicShadowRadius and FarShadowDistance that are covered by distant shadow cascades. */
	uint32 FarShadowCascadeCount;
	
	/** Modulated shadow color. */
	FLinearColor ModulatedShadowColor;

	/** Control the amount of shadow occlusion. */
	float ShadowAmount;

	/** Samples per pixel for ray tracing */
	uint32 SamplesPerPixel;

	/**
	 * Updates the light proxy's cached transforms.
	 * @param InLightToWorld - The new light-to-world transform.
	 * @param InPosition - The new position of the light.
	 */
	void SetTransform(const FMatrix& InLightToWorld,const FVector4& InPosition);

	/** Updates the light's color. */
	void SetColor(const FLinearColor& InColor);
};


/** Encapsulates the data which is used to render a decal parallel to the game thread. */
class ENGINE_API FDeferredDecalProxy
{
public:
	/** constructor */
	FDeferredDecalProxy(const UDecalComponent* InComponent);

	/**
	 * Updates the decal proxy's cached transform.
	 * @param InComponentToWorldIncludingDecalSize - The new component-to-world transform including the DecalSize
	 */
	void SetTransformIncludingDecalSize(const FTransform& InComponentToWorldIncludingDecalSize);

	void InitializeFadingParameters(float AbsSpawnTime, float FadeDuration, float FadeStartDelay, float FadeInDuration, float FadeInStartDelay);

	/** @return True if the decal is visible in the given view. */
	bool IsShown( const FSceneView* View ) const;

	/** Pointer back to the game thread decal component. */
	const UDecalComponent* Component;

	UMaterialInterface* DecalMaterial;

	/** Used to compute the projection matrix on the render thread side, includes the DecalSize  */
	FTransform ComponentTrans;

private:
	/** Whether or not the decal should be drawn in the game, or when the editor is in 'game mode'. */
	bool DrawInGame;

	/** Whether or not the decal should be drawn in the editor. */
	bool DrawInEditor;

public:

	bool bOwnerSelected;

	/** Larger values draw later (on top). */
	int32 SortOrder;

	float InvFadeDuration;

	float InvFadeInDuration;

	/**
	* FadeT = saturate(1 - (AbsTime - FadeStartDelay - AbsSpawnTime) / FadeDuration)
	*
	*		refactored as muladd:
	*		FadeT = saturate((AbsTime * -InvFadeDuration) + ((FadeStartDelay + AbsSpawnTime + FadeDuration) * InvFadeDuration))
	*/
	float FadeStartDelayNormalized;

	float FadeInStartDelayNormalized;

	float FadeScreenSize;
};

/** Reflection capture shapes. */
namespace EReflectionCaptureShape
{
	enum Type
	{
		Sphere,
		Box,
		Plane,
		Num
	};
}

/** Represents a reflection capture to the renderer. */
class ENGINE_API FReflectionCaptureProxy
{
public:
	const class UReflectionCaptureComponent* Component;

	int32 PackedIndex;

	/** Used with mobile renderer */
	TUniformBufferRef<FMobileReflectionCaptureShaderParameters> MobileUniformBuffer;
	FTexture* EncodedHDRCubemap;
	float EncodedHDRAverageBrightness;

	EReflectionCaptureShape::Type Shape;

	// Properties shared among all shapes
	FVector Position;
	float InfluenceRadius;
	float Brightness;
	uint32 Guid;
	FVector CaptureOffset;
	int32 SortedCaptureIndex; // Index into ReflectionSceneData.SortedCaptures (and ReflectionCaptures uniform buffer).

	// Box properties
	FMatrix BoxTransform;
	FVector BoxScales;
	float BoxTransitionDistance;

	// Plane properties
	FPlane ReflectionPlane;
	FVector4 ReflectionXAxisAndYScale;

	bool bUsingPreviewCaptureData;

	FReflectionCaptureProxy(const class UReflectionCaptureComponent* InComponent);

	void SetTransform(const FMatrix& InTransform);
	void UpdateMobileUniformBuffer();
};

/** Calculated wind data with support for accumulating other weighted wind data */
class ENGINE_API FWindData
{
public:
	FWindData()
		: Speed(0.0f)
		, MinGustAmt(0.0f)
		, MaxGustAmt(0.0f)
		, Direction(1.0f, 0.0f, 0.0f)
	{
	}

	void PrepareForAccumulate();
	void AddWeighted(const FWindData& InWindData, float Weight);
	void NormalizeByTotalWeight(float TotalWeight);

	float Speed;
	float MinGustAmt;
	float MaxGustAmt;
	FVector Direction;
};

/** Represents a wind source component to the scene manager in the rendering thread. */
class ENGINE_API FWindSourceSceneProxy
{
public:	

	/** Initialization constructor. */
	FWindSourceSceneProxy(const FVector& InDirection, float InStrength, float InSpeed, float InMinGustAmt, float InMaxGustAmt) :
	  Position(FVector::ZeroVector),
		  Direction(InDirection),
		  Strength(InStrength),
		  Speed(InSpeed),
		  MinGustAmt(InMinGustAmt),
		  MaxGustAmt(InMaxGustAmt),
		  Radius(0),
		  bIsPointSource(false)
	  {}

	  /** Initialization constructor. */
	FWindSourceSceneProxy(const FVector& InPosition, float InStrength, float InSpeed, float InMinGustAmt, float InMaxGustAmt, float InRadius) :
	  Position(InPosition),
		  Direction(FVector::ZeroVector),
		  Strength(InStrength),
		  Speed(InSpeed),
		  MinGustAmt(InMinGustAmt),
		  MaxGustAmt(InMaxGustAmt),
		  Radius(InRadius),
		  bIsPointSource(true)
	  {}

	  bool GetWindParameters(const FVector& EvaluatePosition, FWindData& WindData, float& Weight) const;
	  bool GetDirectionalWindParameters(FWindData& WindData, float& Weight) const;
	  void ApplyWorldOffset(FVector InOffset);

private:

	FVector Position;
	FVector	Direction;
	float Strength;
	float Speed;
	float MinGustAmt;
	float MaxGustAmt;
	float Radius;
	bool bIsPointSource;
};




/**
 * An interface implemented by dynamic resources which need to be initialized and cleaned up by the rendering thread.
 */
class FDynamicPrimitiveResource
{
public:

	virtual void InitPrimitiveResource() = 0;
	virtual void ReleasePrimitiveResource() = 0;
};

/**
 * The base interface used to query a primitive for its dynamic elements.
 */
class FPrimitiveDrawInterface
{
public:

	const FSceneView* const View;

	/** Initialization constructor. */
	FPrimitiveDrawInterface(const FSceneView* InView):
		View(InView)
	{}

	virtual ~FPrimitiveDrawInterface()
	{
	}

	virtual bool IsHitTesting() = 0;
	virtual void SetHitProxy(HHitProxy* HitProxy) = 0;

	virtual void RegisterDynamicResource(FDynamicPrimitiveResource* DynamicResource) = 0;

	virtual void AddReserveLines(uint8 DepthPriorityGroup, int32 NumLines, bool bDepthBiased = false, bool bThickLines = false) = 0;

	virtual void DrawSprite(
		const FVector& Position,
		float SizeX,
		float SizeY,
		const FTexture* Sprite,
		const FLinearColor& Color,
		uint8 DepthPriorityGroup,
		float U,
		float UL,
		float V,
		float VL,
		uint8 BlendMode = 1 /*SE_BLEND_Masked*/
		) = 0;

	virtual void DrawLine(
		const FVector& Start,
		const FVector& End,
		const FLinearColor& Color,
		uint8 DepthPriorityGroup,
		float Thickness = 0.0f,
		float DepthBias = 0.0f,
		bool bScreenSpace = false
		) = 0;

	virtual void DrawPoint(
		const FVector& Position,
		const FLinearColor& Color,
		float PointSize,
		uint8 DepthPriorityGroup
		) = 0;

	/**
	 * Draw a mesh element.
	 * This should only be called through the DrawMesh function.
	 *
	 * @return Number of passes rendered for the mesh
	 */
	virtual int32 DrawMesh(const FMeshBatch& Mesh) = 0;
};

/**
 * An interface to a scene interaction.
 */
class ENGINE_API FViewElementDrawer
{
public:

	/**
	 * Draws the interaction using the given draw interface.
	 */
	virtual void Draw(const FSceneView* View,FPrimitiveDrawInterface* PDI) {}
};

/**
 * An interface used to query a primitive for its static elements.
 */
class FStaticPrimitiveDrawInterface
{
public:
	virtual ~FStaticPrimitiveDrawInterface() { }

	virtual void SetHitProxy(HHitProxy* HitProxy) = 0;

	/**
	  * Reserve memory for specified number of meshes in order to minimize number of allocations inside DrawMesh.
	  */
	virtual void ReserveMemoryForMeshes(int32 MeshNum) = 0;

	virtual void DrawMesh(
		const FMeshBatch& Mesh,
		float ScreenSize
		) = 0;
};



/** 
 * Convenience typedefs for a software occlusion mesh elements
 */
typedef TArray<FVector> FOccluderVertexArray;
typedef TArray<uint16> FOccluderIndexArray;
typedef TSharedPtr<FOccluderVertexArray, ESPMode::ThreadSafe> FOccluderVertexArraySP;
typedef TSharedPtr<FOccluderIndexArray, ESPMode::ThreadSafe> FOccluderIndexArraySP;

/**
 * An interface used to collect primitive occluder geometry.
 */
class FOccluderElementsCollector
{
public:
	virtual ~FOccluderElementsCollector() {};
	virtual void AddElements(const FOccluderVertexArraySP& Vertices, const FOccluderIndexArraySP& Indices, const FMatrix& LocalToWorld)
	{}
};

/** Primitive draw interface implementation used to store primitives requested to be drawn when gathering dynamic mesh elements. */
class ENGINE_API FSimpleElementCollector : public FPrimitiveDrawInterface
{
public:

	FSimpleElementCollector();
	~FSimpleElementCollector();

	virtual void SetHitProxy(HHitProxy* HitProxy) override;
	virtual void AddReserveLines(uint8 DepthPriorityGroup, int32 NumLines, bool bDepthBiased = false, bool bThickLines = false) override {}

	virtual void DrawSprite(
		const FVector& Position,
		float SizeX,
		float SizeY,
		const FTexture* Sprite,
		const FLinearColor& Color,
		uint8 DepthPriorityGroup,
		float U,
		float UL,
		float V,
		float VL,
		uint8 BlendMode = SE_BLEND_Masked
		) override;

	virtual void DrawLine(
		const FVector& Start,
		const FVector& End,
		const FLinearColor& Color,
		uint8 DepthPriorityGroup,
		float Thickness = 0.0f,
		float DepthBias = 0.0f,
		bool bScreenSpace = false
		) override;

	virtual void DrawPoint(
		const FVector& Position,
		const FLinearColor& Color,
		float PointSize,
		uint8 DepthPriorityGroup
		) override;

	virtual void RegisterDynamicResource(FDynamicPrimitiveResource* DynamicResource) override;

	// Not supported
	virtual bool IsHitTesting() override
	{ 
		static bool bTriggered = false;

		if (!bTriggered)
		{
			bTriggered = true;
			ensureMsgf(false, TEXT("FSimpleElementCollector::DrawMesh called"));
		}

		return false; 
	}

	// Not supported
	virtual int32 DrawMesh(const FMeshBatch& Mesh) override
	{
		static bool bTriggered = false;

		if (!bTriggered)
		{
			bTriggered = true;
			ensureMsgf(false, TEXT("FSimpleElementCollector::DrawMesh called"));
		}

		return 0;
	}

	void DrawBatchedElements(FRHICommandList& RHICmdList, const FMeshPassProcessorRenderState& DrawRenderState, const FSceneView& InView, EBlendModeFilter::Type Filter, ESceneDepthPriorityGroup DPG) const;

	bool HasPrimitives(ESceneDepthPriorityGroup DPG) const
	{
		if (DPG == SDPG_World)
		{
			return BatchedElements.HasPrimsToDraw();
		}

		return TopBatchedElements.HasPrimsToDraw();
	}

	/** The batched simple elements. */
	FBatchedElements BatchedElements;
	FBatchedElements TopBatchedElements;

private:

	FHitProxyId HitProxyId;
	uint16 PrimitiveMeshId;

	bool bIsMobileHDR;

	/** The dynamic resources which have been registered with this drawer. */
	TArray<FDynamicPrimitiveResource*,SceneRenderingAllocator> DynamicResources;

	friend class FMeshElementCollector;
};

/** 
 * Base class for a resource allocated from a FMeshElementCollector with AllocateOneFrameResource, which the collector releases.
 * This is useful for per-frame structures which are referenced by a mesh batch given to the FMeshElementCollector.
 */
class FOneFrameResource
{
public:

	virtual ~FOneFrameResource() {}
};

/** 
 * A reference to a mesh batch that is added to the collector, together with some cached relevance flags. 
 */
struct FMeshBatchAndRelevance
{
	const FMeshBatch* Mesh;

	/** The render info for the primitive which created this mesh, required. */
	const FPrimitiveSceneProxy* PrimitiveSceneProxy;

private:
	/** 
	 * Cached usage information to speed up traversal in the most costly passes (depth-only, base pass, shadow depth), 
	 * This is done so the Mesh does not have to be dereferenced to determine pass relevance. 
	 */
	uint32 bHasOpaqueMaterial : 1;
	uint32 bHasMaskedMaterial : 1;
	uint32 bRenderInMainPass : 1;

public:
	FMeshBatchAndRelevance(const FMeshBatch& InMesh, const FPrimitiveSceneProxy* InPrimitiveSceneProxy, ERHIFeatureLevel::Type FeatureLevel);

	bool GetHasOpaqueMaterial() const { return bHasOpaqueMaterial; }
	bool GetHasMaskedMaterial() const { return bHasMaskedMaterial; }
	bool GetHasOpaqueOrMaskedMaterial() const { return bHasOpaqueMaterial || bHasMaskedMaterial; }
	bool GetRenderInMainPass() const { return bRenderInMainPass; }
};

/** 
 * Encapsulates the gathering of meshes from the various FPrimitiveSceneProxy classes. 
 */
class FMeshElementCollector
{
public:

	/** Accesses the PDI for drawing lines, sprites, etc. */
	inline FPrimitiveDrawInterface* GetPDI(int32 ViewIndex)
	{
		return SimpleElementCollectors[ViewIndex];
	}

	/** 
	 * Allocates an FMeshBatch that can be safely referenced by the collector (lifetime will be long enough).
	 * Returns a reference that will not be invalidated due to further AllocateMesh() calls.
	 */
	inline FMeshBatch& AllocateMesh()
	{
		const int32 Index = MeshBatchStorage.Add(1);
		return MeshBatchStorage[Index];
	}

	/** Return dynamic index buffer for this collector. */
	FGlobalDynamicIndexBuffer& GetDynamicIndexBuffer()
	{
		check(DynamicIndexBuffer);
		return *DynamicIndexBuffer;
	}

	/** Return dynamic vertex buffer for this collector. */
	FGlobalDynamicVertexBuffer& GetDynamicVertexBuffer()
	{
		check(DynamicVertexBuffer);
		return *DynamicVertexBuffer;
	}

	/** Return dynamic read buffer for this collector. */
	FGlobalDynamicReadBuffer& GetDynamicReadBuffer()
	{
		check(DynamicReadBuffer);
		return *DynamicReadBuffer;
	}

	// @return number of MeshBatches collected (so far) for a given view
	uint32 GetMeshBatchCount(uint32 ViewIndex) const
	{
		return MeshBatches[ViewIndex]->Num();
	}

	// @return Number of elemenets collected so far for a given view.
	uint32 GetMeshElementCount(uint32 ViewIndex) const
	{
		return NumMeshBatchElementsPerView[ViewIndex];
	}

	/** 
	 * Adds a mesh batch to the collector for the specified view so that it can be rendered.
	 */
	ENGINE_API void AddMesh(int32 ViewIndex, FMeshBatch& MeshBatch);

	/** Add a material render proxy that will be cleaned up automatically */
	void RegisterOneFrameMaterialProxy(FMaterialRenderProxy* Proxy)
	{
		TemporaryProxies.Add(Proxy);
	}

	/** Allocates a temporary resource that is safe to be referenced by an FMeshBatch added to the collector. */
	template<typename T, typename... ARGS>
	T& AllocateOneFrameResource(ARGS&&... Args)
	{
		T* OneFrameResource = new (FMemStack::Get()) T(Forward<ARGS>(Args)...);
		OneFrameResources.Add(OneFrameResource);
		return *OneFrameResource;
	}

	FORCEINLINE bool ShouldUseTasks() const
	{
		return bUseAsyncTasks;
	}

	FORCEINLINE void AddTask(TFunction<void()>&& Task)
	{
		ParallelTasks.Add(new (FMemStack::Get()) TFunction<void()>(MoveTemp(Task)));
	}

	FORCEINLINE void AddTask(const TFunction<void()>& Task)
	{
		ParallelTasks.Add(new (FMemStack::Get()) TFunction<void()>(Task));
	}

	ENGINE_API void ProcessTasks();

	ENGINE_API ERHIFeatureLevel::Type GetFeatureLevel() const
	{
		return FeatureLevel;
	}

protected:

	ENGINE_API FMeshElementCollector(ERHIFeatureLevel::Type InFeatureLevel);

	~FMeshElementCollector()
	{
		check(!ParallelTasks.Num()); // We should have blocked on this already
		for (int32 ProxyIndex = 0; ProxyIndex < TemporaryProxies.Num(); ProxyIndex++)
		{
			delete TemporaryProxies[ProxyIndex];
		}

		// SceneRenderingAllocator does not handle destructors
		for (int32 ResourceIndex = 0; ResourceIndex < OneFrameResources.Num(); ResourceIndex++)
		{
			OneFrameResources[ResourceIndex]->~FOneFrameResource();
		}
	}

	void SetPrimitive(const FPrimitiveSceneProxy* InPrimitiveSceneProxy, FHitProxyId DefaultHitProxyId)
	{
		check(InPrimitiveSceneProxy);
		PrimitiveSceneProxy = InPrimitiveSceneProxy;

		for (int32 ViewIndex = 0; ViewIndex < SimpleElementCollectors.Num(); ViewIndex++)
		{
			SimpleElementCollectors[ViewIndex]->HitProxyId = DefaultHitProxyId;
			SimpleElementCollectors[ViewIndex]->PrimitiveMeshId = 0;
		}

		for (int32 ViewIndex = 0; ViewIndex < MeshIdInPrimitivePerView.Num(); ++ViewIndex)
		{
			MeshIdInPrimitivePerView[ViewIndex] = 0;
		}
	}

	void ClearViewMeshArrays()
	{
		Views.Empty();
		MeshBatches.Empty();
		SimpleElementCollectors.Empty();
		MeshIdInPrimitivePerView.Empty();
		DynamicPrimitiveShaderDataPerView.Empty();
		NumMeshBatchElementsPerView.Empty();
		DynamicIndexBuffer = nullptr;
		DynamicVertexBuffer = nullptr;
		DynamicReadBuffer = nullptr;
	}

	void AddViewMeshArrays(
		FSceneView* InView, 
		TArray<FMeshBatchAndRelevance,SceneRenderingAllocator>* ViewMeshes,
		FSimpleElementCollector* ViewSimpleElementCollector, 
		TArray<FPrimitiveUniformShaderParameters>* InDynamicPrimitiveShaderData,
		ERHIFeatureLevel::Type InFeatureLevel,
		FGlobalDynamicIndexBuffer* InDynamicIndexBuffer,
		FGlobalDynamicVertexBuffer* InDynamicVertexBuffer,
		FGlobalDynamicReadBuffer* InDynamicReadBuffer)
	{
		Views.Add(InView);
		MeshIdInPrimitivePerView.Add(0);
		MeshBatches.Add(ViewMeshes);
		NumMeshBatchElementsPerView.Add(0);
		SimpleElementCollectors.Add(ViewSimpleElementCollector);
		DynamicPrimitiveShaderDataPerView.Add(InDynamicPrimitiveShaderData);

		check(InDynamicIndexBuffer && InDynamicVertexBuffer && InDynamicReadBuffer);
		DynamicIndexBuffer = InDynamicIndexBuffer;
		DynamicVertexBuffer = InDynamicVertexBuffer;
		DynamicReadBuffer = InDynamicReadBuffer;
	}

	/** 
	 * Using TChunkedArray which will never realloc as new elements are added
	 * @todo - use mem stack
	 */
	TChunkedArray<FMeshBatch> MeshBatchStorage;

	/** Meshes to render */
	TArray<TArray<FMeshBatchAndRelevance, SceneRenderingAllocator>*, TInlineAllocator<2> > MeshBatches;

	/** Number of elements in gathered meshes per view. */
	TArray<int32, TInlineAllocator<2> > NumMeshBatchElementsPerView;

	/** PDIs */
	TArray<FSimpleElementCollector*, TInlineAllocator<2> > SimpleElementCollectors;

	/** Views being collected for */
	TArray<FSceneView*, TInlineAllocator<2> > Views;

	/** Current Mesh Id In Primitive per view */
	TArray<uint16, TInlineAllocator<2> > MeshIdInPrimitivePerView;

	/** Material proxies that will be deleted at the end of the frame. */
	TArray<FMaterialRenderProxy*, SceneRenderingAllocator> TemporaryProxies;

	/** Resources that will be deleted at the end of the frame. */
	TArray<FOneFrameResource*, SceneRenderingAllocator> OneFrameResources;

	/** Current primitive being gathered. */
	const FPrimitiveSceneProxy* PrimitiveSceneProxy;

	/** Dynamic buffer pools. */
	FGlobalDynamicIndexBuffer* DynamicIndexBuffer;
	FGlobalDynamicVertexBuffer* DynamicVertexBuffer;
	FGlobalDynamicReadBuffer* DynamicReadBuffer;

	ERHIFeatureLevel::Type FeatureLevel;

	/** This is related to some cvars and FApp stuff and if true means calling code should use async tasks. */
	const bool bUseAsyncTasks;

	/** Tasks to wait for at the end of gathering dynamic mesh elements. */
	TArray<TFunction<void()>*, SceneRenderingAllocator> ParallelTasks;

	/** Tracks dynamic primitive data for upload to GPU Scene for every view, when enabled. */
	TArray<TArray<FPrimitiveUniformShaderParameters>*, TInlineAllocator<2> > DynamicPrimitiveShaderDataPerView;

	friend class FSceneRenderer;
	friend class FDeferredShadingSceneRenderer;
	friend class FProjectedShadowInfo;
};

#if RHI_RAYTRACING
/**
 * Collector used to gather resources for the material mesh batches.
 * It is also the actual owner of the temporary, per-frame resources created for each mesh batch.
 * Mesh batches shall only weak-reference the resources located in the collector.
 */
class FRayTracingMeshResourceCollector : public FMeshElementCollector
{
public:
	// No MeshBatch should be allocated from an FRayTracingMeshResourceCollector.
	inline FMeshBatch& AllocateMesh() = delete;
	void AddMesh(int32 ViewIndex, FMeshBatch& MeshBatch) = delete;
	void RegisterOneFrameMaterialProxy(FMaterialRenderProxy* Proxy) = delete;

	FRayTracingMeshResourceCollector(
		ERHIFeatureLevel::Type InFeatureLevel,
		FGlobalDynamicIndexBuffer* InDynamicIndexBuffer,
		FGlobalDynamicVertexBuffer* InDynamicVertexBuffer,
		FGlobalDynamicReadBuffer* InDynamicReadBuffer)
		: FMeshElementCollector(InFeatureLevel)
	{
		DynamicIndexBuffer = InDynamicIndexBuffer;
		DynamicVertexBuffer = InDynamicVertexBuffer;
		DynamicReadBuffer = InDynamicReadBuffer;
	}
};

struct FRayTracingDynamicGeometryUpdateParams
{
	TArray<FMeshBatch> MeshBatches;

	bool bUsingIndirectDraw = false;
	// When bUsingIndirectDraw == false, NumVertices == the actual number of vertices to process
	// When bUsingIndirectDraw == true, it is the maximum possible vertices that GPU can emit
	uint32 NumVertices = 0;
	uint32 VertexBufferSize = 0;
	uint32 NumTriangles = 0;

	FRayTracingGeometry* Geometry = nullptr;
	FRWBuffer* Buffer = nullptr;

	bool bApplyWorldPositionOffset = true;
};

struct FRayTracingMaterialGatheringContext
{
	const class FScene* Scene;
	const FSceneView* ReferenceView;
	const FSceneViewFamily& ReferenceViewFamily;
	FRHICommandListImmediate& RHICmdList;

	FRayTracingMeshResourceCollector& RayTracingMeshResourceCollector;
	TArray<FRayTracingDynamicGeometryUpdateParams> DynamicRayTracingGeometriesToUpdate;
};
#endif

class FDynamicPrimitiveUniformBuffer : public FOneFrameResource
{
public:
	FDynamicPrimitiveUniformBuffer() = default;
	// FDynamicPrimitiveUniformBuffer is non-copyable
	FDynamicPrimitiveUniformBuffer(const FDynamicPrimitiveUniformBuffer&) = delete;

	virtual ~FDynamicPrimitiveUniformBuffer()
	{
		UniformBuffer.ReleaseResource();
	}

	TUniformBuffer<FPrimitiveUniformShaderParameters> UniformBuffer;

	ENGINE_API void Set(
		const FMatrix& LocalToWorld,
		const FMatrix& PreviousLocalToWorld,
		const FBoxSphereBounds& WorldBounds,
		const FBoxSphereBounds& LocalBounds,
		const FBoxSphereBounds& PreSkinnedLocalBounds,
		bool bReceivesDecals,
		bool bHasPrecomputedVolumetricLightmap,
		bool bDrawsVelocity,
		bool bOutputVelocity);

	/** Pass-through implementation which calls the overloaded Set function with LocalBounds for PreSkinnedLocalBounds. */
	ENGINE_API void Set(
		const FMatrix& LocalToWorld,
		const FMatrix& PreviousLocalToWorld,
		const FBoxSphereBounds& WorldBounds,
		const FBoxSphereBounds& LocalBounds,
		bool bReceivesDecals,
		bool bHasPrecomputedVolumetricLightmap,
		bool bDrawsVelocity,
		bool bOutputVelocity);
};

//
// Primitive drawing utility functions.
//

// Solid shape drawing utility functions. Not really designed for speed - more for debugging.
// These utilities functions are implemented in PrimitiveDrawingUtils.cpp.

// 10x10 tessellated plane at x=-1..1 y=-1...1 z=0
extern ENGINE_API void DrawPlane10x10(class FPrimitiveDrawInterface* PDI,const FMatrix& ObjectToWorld,float Radii,FVector2D UVMin, FVector2D UVMax,const FMaterialRenderProxy* MaterialRenderProxy,uint8 DepthPriority);

// draw simple triangle with material
extern ENGINE_API void DrawTriangle(class FPrimitiveDrawInterface* PDI, const FVector& A, const FVector& B, const FVector& C, const FMaterialRenderProxy* MaterialRenderProxy, uint8 DepthPriorityGroup);
extern ENGINE_API void DrawBox(class FPrimitiveDrawInterface* PDI,const FMatrix& BoxToWorld,const FVector& Radii,const FMaterialRenderProxy* MaterialRenderProxy,uint8 DepthPriority);
extern ENGINE_API void DrawSphere(class FPrimitiveDrawInterface* PDI,const FVector& Center,const FRotator& Orientation,const FVector& Radii,int32 NumSides,int32 NumRings,const FMaterialRenderProxy* MaterialRenderProxy,uint8 DepthPriority,bool bDisableBackfaceCulling=false);
extern ENGINE_API void DrawCone(class FPrimitiveDrawInterface* PDI,const FMatrix& ConeToWorld, float Angle1, float Angle2, uint32 NumSides, bool bDrawSideLines, const FLinearColor& SideLineColor, const FMaterialRenderProxy* MaterialRenderProxy, uint8 DepthPriority);

extern ENGINE_API void DrawCylinder(class FPrimitiveDrawInterface* PDI,const FVector& Base, const FVector& XAxis, const FVector& YAxis, const FVector& ZAxis,
	float Radius, float HalfHeight, uint32 Sides, const FMaterialRenderProxy* MaterialInstance, uint8 DepthPriority);

extern ENGINE_API void DrawCylinder(class FPrimitiveDrawInterface* PDI, const FMatrix& CylToWorld, const FVector& Base, const FVector& XAxis, const FVector& YAxis, const FVector& ZAxis,
	float Radius, float HalfHeight, uint32 Sides, const FMaterialRenderProxy* MaterialInstance, uint8 DepthPriority);

//Draws a cylinder along the axis from Start to End
extern ENGINE_API void DrawCylinder(class FPrimitiveDrawInterface* PDI, const FVector& Start, const FVector& End, float Radius, int32 Sides, const FMaterialRenderProxy* MaterialInstance, uint8 DepthPriority);


extern ENGINE_API void GetBoxMesh(const FMatrix& BoxToWorld,const FVector& Radii,const FMaterialRenderProxy* MaterialRenderProxy,uint8 DepthPriority,int32 ViewIndex,FMeshElementCollector& Collector);
extern ENGINE_API void GetOrientedHalfSphereMesh(const FVector& Center, const FRotator& Orientation, const FVector& Radii, int32 NumSides, int32 NumRings, float StartAngle, float EndAngle, const FMaterialRenderProxy* MaterialRenderProxy, uint8 DepthPriority, bool bDisableBackfaceCulling,
									int32 ViewIndex, FMeshElementCollector& Collector, bool bUseSelectionOutline = false, HHitProxy* HitProxy = NULL);
extern ENGINE_API void GetHalfSphereMesh(const FVector& Center, const FVector& Radii, int32 NumSides, int32 NumRings, float StartAngle, float EndAngle, const FMaterialRenderProxy* MaterialRenderProxy, uint8 DepthPriority, bool bDisableBackfaceCulling,
									int32 ViewIndex, FMeshElementCollector& Collector, bool bUseSelectionOutline=false, HHitProxy* HitProxy=NULL);
extern ENGINE_API void GetSphereMesh(const FVector& Center, const FVector& Radii, int32 NumSides, int32 NumRings, const FMaterialRenderProxy* MaterialRenderProxy, uint8 DepthPriority,
	bool bDisableBackfaceCulling, int32 ViewIndex, FMeshElementCollector& Collector);
extern ENGINE_API void GetSphereMesh(const FVector& Center,const FVector& Radii,int32 NumSides,int32 NumRings,const FMaterialRenderProxy* MaterialRenderProxy,uint8 DepthPriority,
									bool bDisableBackfaceCulling,int32 ViewIndex,FMeshElementCollector& Collector, bool bUseSelectionOutline, HHitProxy* HitProxy);
extern ENGINE_API void GetCylinderMesh(const FVector& Base, const FVector& XAxis, const FVector& YAxis, const FVector& ZAxis,
									float Radius, float HalfHeight, int32 Sides, const FMaterialRenderProxy* MaterialInstance, uint8 DepthPriority, int32 ViewIndex, FMeshElementCollector& Collector);
extern ENGINE_API void GetCylinderMesh(const FMatrix& CylToWorld, const FVector& Base, const FVector& XAxis, const FVector& YAxis, const FVector& ZAxis,
									float Radius, float HalfHeight, uint32 Sides, const FMaterialRenderProxy* MaterialInstance, uint8 DepthPriority, int32 ViewIndex, FMeshElementCollector& Collector);
//Draws a cylinder along the axis from Start to End
extern ENGINE_API void GetCylinderMesh(const FVector& Start, const FVector& End, float Radius, int32 Sides, const FMaterialRenderProxy* MaterialInstance, uint8 DepthPriority, int32 ViewIndex, FMeshElementCollector& Collector);


extern ENGINE_API void GetConeMesh(const FMatrix& LocalToWorld, float AngleWidth, float AngleHeight, uint32 NumSides,
									const FMaterialRenderProxy* MaterialRenderProxy, uint8 DepthPriority, int32 ViewIndex, FMeshElementCollector& Collector);
extern ENGINE_API void GetCapsuleMesh(const FVector& Origin, const FVector& XAxis, const FVector& YAxis, const FVector& ZAxis, const FLinearColor& Color, float Radius, float HalfHeight, int32 NumSides,
									const FMaterialRenderProxy* MaterialRenderProxy, uint8 DepthPriority, bool bDisableBackfaceCulling, int32 ViewIndex, FMeshElementCollector& Collector);


/**
 * Draws a circle using triangles.
 *
 * @param	PDI						Draw interface.
 * @param	Base					Center of the circle.
 * @param	XAxis					X alignment axis to draw along.
 * @param	YAxis					Y alignment axis to draw along.
 * @param	Color					Color of the circle.
 * @param	Radius					Radius of the circle.
 * @param	NumSides				Numbers of sides that the circle has.
 * @param	MaterialRenderProxy		Material to use for render 
 * @param	DepthPriority			Depth priority for the circle.
 */
extern ENGINE_API void DrawDisc(class FPrimitiveDrawInterface* PDI,const FVector& Base,const FVector& XAxis,const FVector& YAxis,FColor Color,float Radius,int32 NumSides, const FMaterialRenderProxy* MaterialRenderProxy, uint8 DepthPriority);


/**
 * Draws a flat arrow with an outline.
 *
 * @param	PDI						Draw interface.
 * @param	Base					Base of the arrow.
 * @param	XAxis					X alignment axis to draw along.
 * @param	YAxis					Y alignment axis to draw along.
 * @param	Color					Color of the circle.
 * @param	Length					Length of the arrow, from base to tip.
 * @param	Width					Width of the base of the arrow, head of the arrow will be 2x.
 * @param	MaterialRenderProxy		Material to use for render 
 * @param	DepthPriority			Depth priority for the circle.
 * @param	Thickness				Thickness of the lines comprising the arrow
 */

/*
x-axis is from point 0 to point 2
y-axis is from point 0 to point 1
		6
		/\
	   /  \
	  /    \
	 4_2  3_5
	   |  |
	   0__1
*/
extern ENGINE_API void DrawFlatArrow(class FPrimitiveDrawInterface* PDI,const FVector& Base,const FVector& XAxis,const FVector& YAxis,FColor Color,float Length,int32 Width, const FMaterialRenderProxy* MaterialRenderProxy, uint8 DepthPriority, float Thickness = 0.0f);

// Line drawing utility functions.

/**
 * Draws a wireframe box.
 *
 * @param	PDI				Draw interface.
 * @param	Box				The FBox to use for drawing.
 * @param	Color			Color of the box.
 * @param	DepthPriority	Depth priority for the circle.
 * @param	Thickness		Thickness of the lines comprising the box
 */
extern ENGINE_API void DrawWireBox(class FPrimitiveDrawInterface* PDI, const FBox& Box, const FLinearColor& Color, uint8 DepthPriority, float Thickness = 0.0f, float DepthBias = 0.0f, bool bScreenSpace = false);
extern ENGINE_API void DrawWireBox(class FPrimitiveDrawInterface* PDI, const FMatrix& Matrix, const FBox& Box, const FLinearColor& Color, uint8 DepthPriority, float Thickness = 0.0f, float DepthBias = 0.0f, bool bScreenSpace = false);

/**
 * Draws a circle using lines.
 *
 * @param	PDI				Draw interface.
 * @param	Base			Center of the circle.
 * @param	X				X alignment axis to draw along.
 * @param	Y				Y alignment axis to draw along.
 * @param	Z				Z alignment axis to draw along.
 * @param	Color			Color of the circle.
 * @param	Radius			Radius of the circle.
 * @param	NumSides		Numbers of sides that the circle has.
 * @param	DepthPriority	Depth priority for the circle.
 * @param	Thickness		Thickness of the lines comprising the circle
 */
extern ENGINE_API void DrawCircle(class FPrimitiveDrawInterface* PDI, const FVector& Base, const FVector& X, const FVector& Y, const FLinearColor& Color, float Radius, int32 NumSides, uint8 DepthPriority, float Thickness = 0.0f, float DepthBias = 0.0f, bool bScreenSpace = false);


/**
 * Draws an arc using lines.
 *
 * @param	PDI				Draw interface.
 * @param	Base			Center of the circle.
 * @param	X				Normalized axis from one point to the center
 * @param	Y				Normalized axis from other point to the center
 * @param   MinAngle        The minimum angle
 * @param   MaxAngle        The maximum angle
 * @param   Radius          Radius of the arc
 * @param	Sections		Numbers of sides that the circle has.
 * @param	Color			Color of the circle.
 * @param	DepthPriority	Depth priority for the circle.
 */
extern ENGINE_API void DrawArc(FPrimitiveDrawInterface* PDI, const FVector Base, const FVector X, const FVector Y, const float MinAngle, const float MaxAngle, const float Radius, const int32 Sections, const FLinearColor& Color, uint8 DepthPriority);

/**
 * Draws a sphere using circles.
 *
 * @param	PDI				Draw interface.
 * @param	Base			Center of the sphere.
 * @param	Color			Color of the sphere.
 * @param	Radius			Radius of the sphere.
 * @param	NumSides		Numbers of sides that the circle has.
 * @param	DepthPriority	Depth priority for the circle.
 * @param	Thickness		Thickness of the lines comprising the sphere
 */
extern ENGINE_API void DrawWireSphere(class FPrimitiveDrawInterface* PDI, const FVector& Base, const FLinearColor& Color, float Radius, int32 NumSides, uint8 DepthPriority, float Thickness = 0.0f, float DepthBias = 0.0f, bool bScreenSpace = false);
extern ENGINE_API void DrawWireSphere(class FPrimitiveDrawInterface* PDI, const FTransform& Transform, const FLinearColor& Color, float Radius, int32 NumSides, uint8 DepthPriority, float Thickness = 0.0f, float DepthBias = 0.0f, bool bScreenSpace = false);

/**
 * Draws a sphere using circles, automatically calculating a reasonable number of sides
 *
 * @param	PDI				Draw interface.
 * @param	Base			Center of the sphere.
 * @param	Color			Color of the sphere.
 * @param	Radius			Radius of the sphere.
 * @param	DepthPriority	Depth priority for the circle.
 * @param	Thickness		Thickness of the lines comprising the sphere
 */
extern ENGINE_API void DrawWireSphereAutoSides(class FPrimitiveDrawInterface* PDI, const FVector& Base, const FLinearColor& Color, float Radius, uint8 DepthPriority, float Thickness = 0.0f, float DepthBias = 0.0f, bool bScreenSpace = false);
extern ENGINE_API void DrawWireSphereAutoSides(class FPrimitiveDrawInterface* PDI, const FTransform& Transform, const FLinearColor& Color, float Radius, uint8 DepthPriority, float Thickness = 0.0f, float DepthBias = 0.0f, bool bScreenSpace = false);

/**
 * Draws a wireframe cylinder.
 *
 * @param	PDI				Draw interface.
 * @param	Base			Center pointer of the base of the cylinder.
 * @param	X				X alignment axis to draw along.
 * @param	Y				Y alignment axis to draw along.
 * @param	Z				Z alignment axis to draw along.
 * @param	Color			Color of the cylinder.
 * @param	Radius			Radius of the cylinder.
 * @param	HalfHeight		Half of the height of the cylinder.
 * @param	NumSides		Numbers of sides that the cylinder has.
 * @param	DepthPriority	Depth priority for the cylinder.
 * @param	Thickness		Thickness of the lines comprising the cylinder
 */
extern ENGINE_API void DrawWireCylinder(class FPrimitiveDrawInterface* PDI, const FVector& Base, const FVector& X, const FVector& Y, const FVector& Z, const FLinearColor& Color, float Radius, float HalfHeight, int32 NumSides, uint8 DepthPriority, float Thickness = 0.0f, float DepthBias = 0.0f, bool bScreenSpace = false);

/**
 * Draws a wireframe capsule.
 *
 * @param	PDI				Draw interface.
 * @param	Base			Center pointer of the base of the cylinder.
 * @param	X				X alignment axis to draw along.
 * @param	Y				Y alignment axis to draw along.
 * @param	Z				Z alignment axis to draw along.
 * @param	Color			Color of the cylinder.
 * @param	Radius			Radius of the cylinder.
 * @param	HalfHeight		Half of the height of the cylinder.
 * @param	NumSides		Numbers of sides that the cylinder has.
 * @param	DepthPriority	Depth priority for the cylinder.
 * @param	Thickness		Thickness of the lines comprising the cylinder
 */
extern ENGINE_API void DrawWireCapsule(class FPrimitiveDrawInterface* PDI, const FVector& Base, const FVector& X, const FVector& Y, const FVector& Z, const FLinearColor& Color, float Radius, float HalfHeight, int32 NumSides, uint8 DepthPriority, float Thickness = 0.0f, float DepthBias = 0.0f, bool bScreenSpace = false);

/**
 * Draws a wireframe chopped cone (cylinder with independent top and bottom radius).
 *
 * @param	PDI				Draw interface.
 * @param	Base			Center pointer of the base of the cone.
 * @param	X				X alignment axis to draw along.
 * @param	Y				Y alignment axis to draw along.
 * @param	Z				Z alignment axis to draw along.
 * @param	Color			Color of the cone.
 * @param	Radius			Radius of the cone at the bottom.
 * @param	TopRadius		Radius of the cone at the top.
 * @param	HalfHeight		Half of the height of the cone.
 * @param	NumSides		Numbers of sides that the cone has.
 * @param	DepthPriority	Depth priority for the cone.
 */
extern ENGINE_API void DrawWireChoppedCone(class FPrimitiveDrawInterface* PDI,const FVector& Base,const FVector& X,const FVector& Y,const FVector& Z,const FLinearColor& Color,float Radius,float TopRadius,float HalfHeight,int32 NumSides,uint8 DepthPriority);

/**
 * Draws a wireframe cone
 *
 * @param	PDI				Draw interface.
 * @param	Transform		Generic transform to apply (ex. a local-to-world transform).
 * @param	ConeLength		Pre-transform distance from apex to the perimeter of the cone base.  The Radius of the base is ConeLength * sin(ConeAngle).
 * @param	ConeAngle		Angle of the cone in degrees. This is 1/2 the cone aperture.
 * @param	ConeSides		Numbers of sides that the cone has.
 * @param	Color			Color of the cone.
 * @param	DepthPriority	Depth priority for the cone.
 * @param	Verts			Out param, the positions of the verts at the cone base.
 * @param	Thickness		Thickness of the lines comprising the cone
 */
extern ENGINE_API void DrawWireCone(class FPrimitiveDrawInterface* PDI, TArray<FVector>& Verts, const FMatrix& Transform, float ConeLength, float ConeAngle, int32 ConeSides, const FLinearColor& Color, uint8 DepthPriority, float Thickness = 0.0f, float DepthBias = 0.0f, bool bScreenSpace = false);
extern ENGINE_API void DrawWireCone(class FPrimitiveDrawInterface* PDI, TArray<FVector>& Verts, const FTransform& Transform, float ConeLength, float ConeAngle, int32 ConeSides, const FLinearColor& Color, uint8 DepthPriority, float Thickness = 0.0f, float DepthBias = 0.0f, bool bScreenSpace = false);

/**
 * Draws a wireframe cone with a arcs on the cap
 *
 * @param	PDI				Draw interface.
 * @param	Transform		Generic transform to apply (ex. a local-to-world transform).
 * @param	ConeLength		Pre-transform distance from apex to the perimeter of the cone base.  The Radius of the base is ConeLength * sin(ConeAngle).
 * @param	ConeAngle		Angle of the cone in degrees. This is 1/2 the cone aperture.
 * @param	ConeSides		Numbers of sides that the cone has.
 * @param   ArcFrequency    How frequently to draw an arc (1 means every vertex, 2 every 2nd etc.)
 * @param	CapSegments		How many lines to use to make the arc
 * @param	Color			Color of the cone.
 * @param	DepthPriority	Depth priority for the cone.
 */
extern ENGINE_API void DrawWireSphereCappedCone(FPrimitiveDrawInterface* PDI, const FTransform& Transform, float ConeLength, float ConeAngle, int32 ConeSides, int32 ArcFrequency, int32 CapSegments, const FLinearColor& Color, uint8 DepthPriority);

/**
 * Draws an oriented box.
 *
 * @param	PDI				Draw interface.
 * @param	Base			Center point of the box.
 * @param	X				X alignment axis to draw along.
 * @param	Y				Y alignment axis to draw along.
 * @param	Z				Z alignment axis to draw along.
 * @param	Color			Color of the box.
 * @param	Extent			Vector with the half-sizes of the box.
 * @param	DepthPriority	Depth priority for the cone.
 * @param	Thickness		Thickness of the lines comprising the box
 */
extern ENGINE_API void DrawOrientedWireBox(class FPrimitiveDrawInterface* PDI, const FVector& Base, const FVector& X, const FVector& Y, const FVector& Z, FVector Extent, const FLinearColor& Color, uint8 DepthPriority, float Thickness = 0.0f, float DepthBias = 0.0f, bool bScreenSpace = false);

/**
 * Draws a directional arrow (starting at ArrowToWorld.Origin and continuing for Length units in the X direction of ArrowToWorld).
 *
 * @param	PDI				Draw interface.
 * @param	ArrowToWorld	Transform matrix for the arrow.
 * @param	InColor			Color of the arrow.
 * @param	Length			Length of the arrow
 * @param	ArrowSize		Size of the arrow head.
 * @param	DepthPriority	Depth priority for the arrow.
 * @param	Thickness		Thickness of the lines comprising the arrow
 */
extern ENGINE_API void DrawDirectionalArrow(class FPrimitiveDrawInterface* PDI, const FMatrix& ArrowToWorld, const FLinearColor& InColor, float Length, float ArrowSize, uint8 DepthPriority, float Thickness = 0.0f);

/**
 * Draws a directional arrow with connected spokes.
 *
 * @param	PDI				Draw interface.
 * @param	ArrowToWorld	Transform matrix for the arrow.
 * @param	Color			Color of the arrow.
 * @param	ArrowHeight		Height of the the arrow head.
 * @param	ArrowWidth		Width of the arrow head.
 * @param	DepthPriority	Depth priority for the arrow.
 * @param	Thickness		Thickness of the lines used to draw the arrow.
 * @param	NumSpokes		Number of spokes used to make the arrow head.
 */
extern ENGINE_API void DrawConnectedArrow(class FPrimitiveDrawInterface* PDI, const FMatrix& ArrowToWorld, const FLinearColor& Color, float ArrowHeight, float ArrowWidth, uint8 DepthPriority, float Thickness = 0.5f, int32 NumSpokes = 6);

/**
 * Draws a axis-aligned 3 line star.
 *
 * @param	PDI				Draw interface.
 * @param	Position		Position of the star.
 * @param	Size			Size of the star
 * @param	InColor			Color of the arrow.
 * @param	DepthPriority	Depth priority for the star.
 */
extern ENGINE_API void DrawWireStar(class FPrimitiveDrawInterface* PDI, const FVector& Position, float Size, const FLinearColor& Color, uint8 DepthPriority);

/**
 * Draws a dashed line.
 *
 * @param	PDI				Draw interface.
 * @param	Start			Start position of the line.
 * @param	End				End position of the line.
 * @param	Color			Color of the arrow.
 * @param	DashSize		Size of each of the dashes that makes up the line.
 * @param	DepthPriority	Depth priority for the line.
 */
extern ENGINE_API void DrawDashedLine(class FPrimitiveDrawInterface* PDI, const FVector& Start, const FVector& End, const FLinearColor& Color, float DashSize, uint8 DepthPriority, float DepthBias = 0.0f);

/**
 * Draws a wireframe diamond.
 *
 * @param	PDI				Draw interface.
 * @param	DiamondMatrix	Transform Matrix for the diamond.
 * @param	Size			Size of the diamond.
 * @param	InColor			Color of the diamond.
 * @param	DepthPriority	Depth priority for the diamond.
 * @param	Thickness		How thick to draw diamond lines
 */
extern ENGINE_API void DrawWireDiamond(class FPrimitiveDrawInterface* PDI, const FMatrix& DiamondMatrix, float Size, const FLinearColor& InColor, uint8 DepthPriority, float Thickness = 0.0f);

/**
 * Draws a coordinate system (Red for X axis, Green for Y axis, Blue for Z axis).
 *
 * @param	PDI				Draw interface.
 * @param	AxisLoc			Location of the coordinate system.
 * @param	AxisRot			Location of the coordinate system.
 * @param	Scale			Scale for the axis lines.
 * @param	DepthPriority	Depth priority coordinate system.
 * @param	Thickness		How thick to draw the axis lines
 */
extern ENGINE_API void DrawCoordinateSystem(FPrimitiveDrawInterface* PDI, FVector const& AxisLoc, FRotator const& AxisRot, float Scale, uint8 DepthPriority, float Thickness = 0.0f);

/**
 * Draws a coordinate system with a fixed color.
 *
 * @param	PDI				Draw interface.
 * @param	AxisLoc			Location of the coordinate system.
 * @param	AxisRot			Location of the coordinate system.
 * @param	Scale			Scale for the axis lines.
 * @param	InColor			Color of the axis lines.
 * @param	DepthPriority	Depth priority coordinate system.
 * @param	Thickness		How thick to draw the axis lines
 */
extern ENGINE_API void DrawCoordinateSystem(FPrimitiveDrawInterface* PDI, FVector const& AxisLoc, FRotator const& AxisRot, float Scale, const FLinearColor& InColor, uint8 DepthPriority, float Thickness = 0.0f);

/**
 * Draws a wireframe of the bounds of a frustum as defined by a transform from clip-space into world-space.
 * @param PDI - The interface to draw the wireframe.
 * @param FrustumToWorld - A transform from clip-space to world-space that defines the frustum.
 * @param Color - The color to draw the wireframe with.
 * @param DepthPriority - The depth priority group to draw the wireframe with.
 */
extern ENGINE_API void DrawFrustumWireframe(
	FPrimitiveDrawInterface* PDI,
	const FMatrix& WorldToFrustum,
	FColor Color,
	uint8 DepthPriority
	);

void BuildConeVerts(float Angle1, float Angle2, float Scale, float XOffset, uint32 NumSides, TArray<FDynamicMeshVertex>& OutVerts, TArray<uint32>& OutIndices);

void BuildCylinderVerts(const FVector& Base, const FVector& XAxis, const FVector& YAxis, const FVector& ZAxis, float Radius, float HalfHeight, uint32 Sides, TArray<FDynamicMeshVertex>& OutVerts, TArray<uint32>& OutIndices);


/**
 * Given a base color and a selection state, returns a color which accounts for the selection state.
 * @param BaseColor - The base color of the object.
 * @param bSelected - The selection state of the object.
 * @param bHovered - True if the object has hover focus
 * @param bUseOverlayIntensity - True if the selection color should be modified by the selection intensity
 * @return The color to draw the object with, accounting for the selection state
 */
extern ENGINE_API FLinearColor GetSelectionColor(const FLinearColor& BaseColor,bool bSelected,bool bHovered, bool bUseOverlayIntensity = true);
extern ENGINE_API FLinearColor GetViewSelectionColor(const FLinearColor& BaseColor, const FSceneView& View, bool bSelected, bool bHovered, bool bUseOverlayIntensity, bool bIndividuallySelected);


/** Vertex Color view modes */
namespace EVertexColorViewMode
{
	enum Type
	{
		/** Invalid or undefined */
		Invalid,

		/** Color only */
		Color,
		
		/** Alpha only */
		Alpha,

		/** Red only */
		Red,

		/** Green only */
		Green,

		/** Blue only */
		Blue,
	};
}


/** Global vertex color view mode setting when SHOW_VertexColors show flag is set */
extern ENGINE_API EVertexColorViewMode::Type GVertexColorViewMode;

/**
 * Returns true if the given view is "rich", and all primitives should be forced down the dynamic drawing path so that ApplyViewModeOverrides can implement the rich view feature.
 * A view is rich if is missing the EngineShowFlags.Materials showflag, or has any of the render mode affecting showflags.
 */
extern ENGINE_API bool IsRichView(const FSceneViewFamily& ViewFamily);

#if WANTS_DRAW_MESH_EVENTS
	/**
	 * true if we debug material names with SCOPED_DRAW_EVENT.
	 * Toggle with "r.ShowMaterialDrawEvents" cvar.
	 */
	extern ENGINE_API void BeginMeshDrawEvent_Inner(FRHICommandList& RHICmdList, const class FPrimitiveSceneProxy* PrimitiveSceneProxy, const struct FMeshBatch& Mesh, struct FDrawEvent& DrawEvent);
#endif

FORCEINLINE void BeginMeshDrawEvent(FRHICommandList& RHICmdList, const class FPrimitiveSceneProxy* PrimitiveSceneProxy, const struct FMeshBatch& Mesh, struct FDrawEvent& DrawEvent, bool ShowMaterialDrawEvent)
{
#if WANTS_DRAW_MESH_EVENTS
	if (ShowMaterialDrawEvent)
	{
		BeginMeshDrawEvent_Inner(RHICmdList, PrimitiveSceneProxy, Mesh, DrawEvent);
	}
#endif
}

extern ENGINE_API void ApplyViewModeOverrides(
	int32 ViewIndex,
	const FEngineShowFlags& EngineShowFlags,
	ERHIFeatureLevel::Type FeatureLevel,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	bool bSelected,
	struct FMeshBatch& Mesh,
	FMeshElementCollector& Collector
	);

/** Draws the UV layout of the supplied asset (either StaticMeshRenderData OR SkeletalMeshRenderData, not both!) */
extern ENGINE_API void DrawUVs(FViewport* InViewport, FCanvas* InCanvas, int32 InTextYPos, const int32 LODLevel, int32 UVChannel, TArray<FVector2D> SelectedEdgeTexCoords, class FStaticMeshRenderData* StaticMeshRenderData, class FSkeletalMeshLODRenderData* SkeletalMeshRenderData);

/** Will return the view to use taking into account VR which has 2 views */
ENGINE_API const FSceneView& GetLODView(const FSceneView& InView);

/**
 * Computes the screen size of a given sphere bounds in the given view.
 * The screen size is the projected diameter of the bounding sphere of the model.
 * i.e. 0.5 means half the screen's maximum dimension.
 * @param Origin - Origin of the bounds in world space
 * @param SphereRadius - Radius of the sphere to use to calculate screen coverage
 * @param View - The view to calculate the display factor for
 * @return float - The screen size calculated
 */
float ENGINE_API ComputeBoundsScreenSize(const FVector4& Origin, const float SphereRadius, const FSceneView& View);

/**
 * Computes the screen size of a given sphere bounds in the given view.
 * The screen size is the projected diameter of the bounding sphere of the model. 
 * i.e. 0.5 means half the screen's maximum dimension.
 * @param BoundsOrigin - Origin of the bounds in world space
 * @param SphereRadius - Radius of the sphere to use to calculate screen coverage
 * @param ViewOrigin - The origin of the view to calculate the display factor for
 * @param ProjMatrix - The projection matrix used to scale screen size bounds
 * @return float - The screen size calculated
 */
float ENGINE_API ComputeBoundsScreenSize(const FVector4& BoundsOrigin, const float SphereRadius, const FVector4& ViewOrigin, const FMatrix& ProjMatrix);

/**
 * Computes the screen radius squared of a given sphere bounds in the given view. This is used at
 * runtime instead of ComputeBoundsScreenSize to avoid a square root.
 * It is a wrapper for the version below that does not take a FSceneView reference but parameters directly
 * @param Origin - Origin of the bounds in world space
 * @param SphereRadius - Radius of the sphere to use to calculate screen coverage
 * @param View - The view to calculate the display factor for
 * @return float - The screen size calculated
 */
float ENGINE_API ComputeBoundsScreenRadiusSquared(const FVector4& Origin, const float SphereRadius, const FSceneView& View);

/**
 * Computes the screen radius squared of a given sphere bounds in the given view. This is used at
 * runtime instead of ComputeBoundsScreenSize to avoid a square root.
 * @param Origin - Origin of the bounds in world space
 * @param SphereRadius - Radius of the sphere to use to calculate screen coverage
 * @param ViewOrigin - The view origin involved in the calculation
 * @param ProjMatrix - The projection matrix of the view involved in the calculation
 * @return float - The screen size calculated
 */
float ENGINE_API ComputeBoundsScreenRadiusSquared(const FVector4& BoundsOrigin, const float SphereRadius, const FVector4& ViewOrigin, const FMatrix& ProjMatrix);

/**
 * Computes the draw distance of a given sphere bounds in the given view with the specified screen size.
 * @param ScreenSize - The screen size (as computed by ComputeBoundsScreenSize)
 * @param SphereRadius - Radius of the sphere to use to calculate screen coverage
 * @param ProjMatrix - The projection matrix used to scale screen size bounds
 * @return float - The draw distance calculated
 */
float ENGINE_API ComputeBoundsDrawDistance(const float ScreenSize, const float SphereRadius, const FMatrix& ProjMatrix);

/**
 * Computes the LOD level for the given static meshes render data in the given view.
 * @param RenderData - Render data for the mesh
 * @param Origin - Origin of the bounds of the mesh in world space
 * @param SphereRadius - Radius of the sphere to use to calculate screen coverage
 * @param View - The view to calculate the LOD level for
 * @param FactorScale - multiplied by the computed screen size before computing LOD
 */
int8 ENGINE_API ComputeStaticMeshLOD(const FStaticMeshRenderData* RenderData, const FVector4& Origin, const float SphereRadius, const FSceneView& View, int32 MinLOD, float FactorScale = 1.0f);

/**
 * Computes the LOD level for the given static meshes render data in the given view, for one of the two temporal LOD samples
 * @param RenderData - Render data for the mesh
 * @param Origin - Origin of the bounds of the mesh in world space
 * @param SphereRadius - Radius of the sphere to use to calculate screen coverage
 * @param View - The view to calculate the LOD level for
 * @param FactorScale - multiplied by the computed screen size before computing LOD
 * @param SampleIndex - index (0 or 1) of the temporal sample to use
 */
int8 ENGINE_API ComputeTemporalStaticMeshLOD( const FStaticMeshRenderData* RenderData, const FVector4& Origin, const float SphereRadius, const FSceneView& View, int32 MinLOD, float FactorScale, int32 SampleIndex );

/**
 * Computes the LOD to render for the list of static meshes in the given view.
 * @param StaticMeshes - List of static meshes.
 * @param View - The view to render the LOD level for 
 * @param Origin - Origin of the bounds of the mesh in world space
 * @param SphereRadius - Radius of the sphere to use to calculate screen coverage
 */
struct FLODMask
{
	int8 DitheredLODIndices[2];

	FLODMask()
	{
		DitheredLODIndices[0] = MAX_int8;
		DitheredLODIndices[1] = MAX_int8;
	}

	void SetLOD(int32 LODIndex)
	{
		DitheredLODIndices[0] = LODIndex;
		DitheredLODIndices[1] = LODIndex;
	}
	void SetLODSample(int32 LODIndex, int32 SampleIndex)
	{
		DitheredLODIndices[SampleIndex] = (int8)LODIndex;
	}
	void ClampToFirstLOD(int8 FirstLODIdx)
	{
		DitheredLODIndices[0] = FMath::Max(DitheredLODIndices[0], FirstLODIdx);
		DitheredLODIndices[1] = FMath::Max(DitheredLODIndices[1], FirstLODIdx);
	}
	bool ContainsLOD(int32 LODIndex) const
	{
		return DitheredLODIndices[0] == LODIndex || DitheredLODIndices[1] == LODIndex;
	}

	//#dxr_todo UE-72106: We should probably add both LoDs but mask them based on their 
	//LodFade value within the BVH based on the LodFadeMask in the GBuffer
	bool ContainsRayTracedLOD(int32 LODIndex) const
	{
		return DitheredLODIndices[1] == LODIndex;
	}

	int8 GetRayTracedLOD()
	{
		return DitheredLODIndices[1];
	}

	bool IsDithered() const
	{
		return DitheredLODIndices[0] != DitheredLODIndices[1];
	}
};
FLODMask ENGINE_API ComputeLODForMeshes(const TArray<class FStaticMeshBatchRelevance>& StaticMeshRelevances, const FSceneView& View, const FVector4& Origin, float SphereRadius, int32 ForcedLODLevel, float& OutScreenRadiusSquared, int8 CurFirstLODIdx, float ScreenSizeScale = 1.0f, bool bDitheredLODTransition = true);
FLODMask ENGINE_API ComputeFastLODForMeshes(const TArray<float>& ScreenSizes, const FSceneView& View, const FVector4& Origin, float SphereRadius, int32 ForcedLODLevel, float& OutScreenRadiusSquared, float ScreenSizeScale = 1.0f, bool bDitheredLODTransition = true);

class FSharedSamplerState : public FRenderResource
{
public:
	FSamplerStateRHIRef SamplerStateRHI;
	bool bWrap;

	FSharedSamplerState(bool bInWrap) :
		bWrap(bInWrap)
	{}

	virtual void InitRHI() override;

	virtual void ReleaseRHI() override
	{
		SamplerStateRHI.SafeRelease();
	}
};

/** Sampler state using Wrap addressing and taking filter mode from the world texture group. */
extern ENGINE_API FSharedSamplerState* Wrap_WorldGroupSettings;

/** Sampler state using Clamp addressing and taking filter mode from the world texture group. */
extern ENGINE_API FSharedSamplerState* Clamp_WorldGroupSettings;

/** Initializes the shared sampler states. */
extern ENGINE_API void InitializeSharedSamplerStates();

/**
* Cache of read-only console variables used by the scene renderer
*/
struct FReadOnlyCVARCache
{
	static ENGINE_API const FReadOnlyCVARCache& Get();

	bool bEnablePointLightShadows;
	bool bEnableStationarySkylight;
	bool bEnableAtmosphericFog;
	bool bEnableLowQualityLightmaps;
	bool bAllowStaticLighting;
	bool bSupportSkyAtmosphere;

	// Mobile specific
	bool bMobileAllowMovableDirectionalLights;
	bool bMobileAllowDistanceFieldShadows;
	bool bMobileEnableStaticAndCSMShadowReceivers;
	int32 NumMobileMovablePointLights;
	int32 MobileSkyLightPermutation;
	bool bMobileMovablePointLightsUseStaticBranch;
	
	bool bInitialized;
	void Init();
};
