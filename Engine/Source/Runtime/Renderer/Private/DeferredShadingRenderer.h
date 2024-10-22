// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DeferredShadingRenderer.h: Scene rendering definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "RendererInterface.h"
#include "StaticBoundShaderState.h"
#include "ScenePrivateBase.h"
#include "LightSceneInfo.h"
#include "SceneRendering.h"
#include "DepthRendering.h"
#include "ScreenSpaceDenoise.h"


enum class ERayTracingPrimaryRaysFlag : uint32;
enum class EVelocityPass : uint32;

class FSceneTextureParameters;
class FDistanceFieldAOParameters;
class UStaticMeshComponent;
class FExponentialHeightFogSceneInfo;
class FRaytracingLightDataPacked;
struct FSingleLayerWaterPassData;
struct FHeightFogRenderingParameters;
struct FRayTracingReflectionOptions;

class FLightShaftsOutput
{
public:
	// 0 if not rendered
	TRefCountPtr<IPooledRenderTarget> LightShaftOcclusion;
};

/**
 * Scene renderer that implements a deferred shading pipeline and associated features.
 */
class FDeferredShadingSceneRenderer : public FSceneRenderer
{
public:

	/** Defines which objects we want to render in the EarlyZPass. */
	EDepthDrawingMode EarlyZPassMode;
	bool bEarlyZPassMovable;
	bool bDitheredLODTransitionsUseStencil;
	int32 StencilLODMode = 0;
	
	FComputeFenceRHIRef TranslucencyLightingVolumeClearEndFence;

	FDeferredShadingSceneRenderer(const FSceneViewFamily* InViewFamily,FHitProxyConsumer* HitProxyConsumer);

	/** Clears a view */
	void ClearView(FRHICommandListImmediate& RHICmdList);

	/** Clears gbuffer where Z is still at the maximum value (ie no geometry rendered) */
	void ClearGBufferAtMaxZ(FRHICommandList& RHICmdList);

	/** Clears LPVs for all views */
	void ClearLPVs(FRHICommandListImmediate& RHICmdList);

	/** Propagates LPVs for all views */
	void UpdateLPVs(FRHICommandListImmediate& RHICmdList);

	/**
	 * Renders the scene's prepass for a particular view
	 * @return true if anything was rendered
	 */
	void RenderPrePassView(FRHICommandList& RHICmdList, const FViewInfo& View, const FMeshPassProcessorRenderState& DrawRenderState);

	/**
	 * Renders the scene's prepass for a particular view in parallel
	 * @return true if the depth was cleared
	 */
	bool RenderPrePassViewParallel(const FViewInfo& View, FRHICommandListImmediate& ParentCmdList, const FMeshPassProcessorRenderState& DrawRenderState, TFunctionRef<void()> AfterTasksAreStarted, bool bDoPrePre);

	/** 
	 * Culls local lights and reflection probes to a grid in frustum space, builds one light list and grid per view in the current Views.  
	 * Needed for forward shading or translucency using the Surface lighting mode, and clustered deferred shading. 
	 */
	void ComputeLightGrid(FRHICommandListImmediate& RHICmdList, bool bNeedLightGrid, FSortedLightSetSceneInfo &SortedLightSet);

	/** Renders the basepass for a given View, in parallel */
	void RenderBasePassViewParallel(FViewInfo& View, FRHICommandListImmediate& ParentCmdList, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, const FMeshPassProcessorRenderState& InDrawRenderState);

	/** Renders the basepass for a given View. */
	bool RenderBasePassView(FRHICommandListImmediate& RHICmdList, FViewInfo& View, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, const FMeshPassProcessorRenderState& InDrawRenderState);

	/** Renders the sky pass for a given View, in parallel */
	void RenderSkyPassViewParallel(FRHICommandListImmediate& ParentCmdList, FViewInfo& View, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, const FMeshPassProcessorRenderState& InDrawRenderState);
	/** Renders the sky pass for a given View. */
	bool RenderSkyPassView(FRHICommandListImmediate& RHICmdList, FViewInfo& View, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, const FMeshPassProcessorRenderState& InDrawRenderState);

	/** Renders editor primitives for a given View. */
	void RenderEditorPrimitives(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, const FMeshPassProcessorRenderState& DrawRenderState, bool& bOutDirty);

	/** Renders editor primitives for a given View. */
	void RenderEditorPrimitivesForDPG(FRHICommandList& RHICmdList, const FViewInfo& View, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, const FMeshPassProcessorRenderState& DrawRenderState, ESceneDepthPriorityGroup DepthPriorityGroup, bool& bOutDirty);

	/** 
	* Renders the scene's base pass 
	* @return true if anything was rendered
	*/
	bool RenderBasePass(FRHICommandListImmediate& RHICmdList, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, IPooledRenderTarget* ForwardScreenSpaceShadowMask, bool bParallelBasePass, bool bRenderLightmapDensity);

	/** Copy the scene color and depth buffer for water refraction*/
	void CopySingleLayerWaterTextures(FRHICommandListImmediate& RHICmdList, FSingleLayerWaterPassData& PassData);
	/** Begin the water GBuffer rendering pass*/
	static void BeginRenderingWaterGBuffer(FRHICommandList& RHICmdList, FExclusiveDepthStencil::Type DepthStencilAccess, bool bBindQuadOverdrawBuffers, EShaderPlatform InShaderPlatform);
	/** End the water GBuffer rendering pass*/
	void FinishWaterGBufferPassAndResolve(FRHICommandListImmediate& RHICmdList, FExclusiveDepthStencil::Type DepthStencilAccess);
	/**
	* Renders the scene's single layer water base pass
	* @return true if anything was rendered
	*/
	bool RenderSingleLayerWaterPass(FRHICommandListImmediate& RHICmdList, FSingleLayerWaterPassData& PassData, FExclusiveDepthStencil::Type WaterPassDepthStencilAccess, bool bParallel);
	/** Renders the water draw pass for a given View. */
	bool RenderSingleLayerWaterPassView(FRHICommandListImmediate& RHICmdList, FViewInfo& View, const FMeshPassProcessorRenderState& InDrawRenderState, bool bParallel);
	/** Render, denoise and composite the scene SSR and under water effect.*/
	void RenderSingleLayerWaterReflections(FRHICommandListImmediate& RHICmdList, FSingleLayerWaterPassData& PassData);

	/** Finishes the view family rendering. */
	void RenderFinish(FRHICommandListImmediate& RHICmdList);

	bool RenderHzb(FRHICommandListImmediate& RHICmdList);

	void RenderOcclusion(FRHICommandListImmediate& RHICmdList);

	void FinishOcclusion(FRHICommandListImmediate& RHICmdList);

	/** Renders the view family. */
	virtual void Render(FRHICommandListImmediate& RHICmdList) override;

	/** Render the view family's hit proxies. */
	virtual void RenderHitProxies(FRHICommandListImmediate& RHICmdList) override;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	void RenderVisualizeTexturePool(FRHICommandListImmediate& RHICmdList);
#endif

private:

	static FGraphEventRef TranslucencyTimestampQuerySubmittedFence[FOcclusionQueryHelpers::MaxBufferedOcclusionFrames + 1];
	static FGlobalDynamicIndexBuffer DynamicIndexBufferForInitViews;
	static FGlobalDynamicIndexBuffer DynamicIndexBufferForInitShadows;
	static FGlobalDynamicVertexBuffer DynamicVertexBufferForInitViews;
	static FGlobalDynamicVertexBuffer DynamicVertexBufferForInitShadows;
	static TGlobalResource<FGlobalDynamicReadBuffer> DynamicReadBufferForInitViews;
	static TGlobalResource<FGlobalDynamicReadBuffer> DynamicReadBufferForInitShadows;

	/** Creates a per object projected shadow for the given interaction. */
	void CreatePerObjectProjectedShadow(
		FRHICommandListImmediate& RHICmdList,
		FLightPrimitiveInteraction* Interaction,
		bool bCreateTranslucentObjectShadow,
		bool bCreateInsetObjectShadow,
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
		TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& OutPreShadows);

	/**
	* Used by RenderLights to figure out if light functions need to be rendered to the attenuation buffer.
	*
	* @param LightSceneInfo Represents the current light
	* @return true if anything got rendered
	*/
	bool CheckForLightFunction(const FLightSceneInfo* LightSceneInfo) const;

	/**
	* Performs once per frame setup prior to visibility determination.
	*/
	void PreVisibilityFrameSetup(FRHICommandListImmediate& RHICmdList);

	/** Determines which primitives are visible for each view. */
	bool InitViews(FRHICommandListImmediate& RHICmdList, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, struct FILCUpdatePrimTaskData& ILCTaskData);

	void InitViewsPossiblyAfterPrepass(FRHICommandListImmediate& RHICmdList, struct FILCUpdatePrimTaskData& ILCTaskData);

	void SetupSceneReflectionCaptureBuffer(FRHICommandListImmediate& RHICmdList);

	/**
	Updates auto-downsampling of separate translucency and sets FSceneRenderTargets::SeparateTranslucencyBufferSize.
	Also updates timers for stats on GPU translucency times.
	*/
	void UpdateTranslucencyTimersAndSeparateTranslucencyBufferSize(FRHICommandListImmediate& RHICmdList);

	void CreateIndirectCapsuleShadows();

	/**
	* Setup the prepass. This is split out so that in parallel we can do the fx prerender after we start the parallel tasks
	* @return true if the depth was cleared
	*/
	bool PreRenderPrePass(FRHICommandListImmediate& RHICmdList);

	void PreRenderDitherFill(FRHIAsyncComputeCommandListImmediate& RHICmdList, FSceneRenderTargets& SceneContext, FRHIUnorderedAccessView* StencilTextureUAV);
	void PreRenderDitherFill(FRHICommandListImmediate& RHICmdList, FSceneRenderTargets& SceneContext, FRHIUnorderedAccessView* StencilTextureUAV);

	void RenderPrePassEditorPrimitives(FRHICommandList& RHICmdList, const FViewInfo& View, const FMeshPassProcessorRenderState& DrawRenderState, EDepthDrawingMode DepthDrawingMode, bool bRespectUseAsOccluderFlag);

	/**
	 * Renders the scene's prepass and occlusion queries.
	 * @return true if the depth was cleared
	 */
	bool RenderPrePass(FRHICommandListImmediate& RHICmdList, TFunctionRef<void()> AfterTasksAreStarted);

	/**
	 * Renders the active HMD's hidden area mask as a depth prepass, if available.
	 * @return true if depth is cleared
	 */
	bool RenderPrePassHMD(FRHICommandListImmediate& RHICmdList);


	/** Renders the scene's fogging. */
	bool RenderFog(FRHICommandListImmediate& RHICmdList, const FLightShaftsOutput& LightShaftsOutput);
	void RenderUnderWaterFog(FRHICommandListImmediate& RHICmdList, FSingleLayerWaterPassData& PassData);
	
	/** Renders the scene's fogging for a view. */
	void RenderViewFog(FRHICommandList& RHICmdList, const FViewInfo& View, const FHeightFogRenderingParameters& Params);

	/** Renders the scene's atmosphere. */
	void RenderAtmosphere(FRHICommandListImmediate& RHICmdList, const FLightShaftsOutput& LightShaftsOutput);

	/** Render debug visualizations about the sky atmosphere into the scene render target.*/
	void RenderDebugSkyAtmosphere(FRHICommandListImmediate& RHICmdList);

	void RenderDiffuseIndirectAndAmbientOcclusion(FRHICommandListImmediate& RHICmdList);

	/** Renders sky lighting and reflections that can be done in a deferred pass. */
	void RenderDeferredReflectionsAndSkyLighting(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& DynamicBentNormalAO, TRefCountPtr<IPooledRenderTarget>& VelocityRT, struct FHairStrandsRenderingData* HairDatas);
	void RenderDeferredReflectionsAndSkyLightingHair(FRHICommandListImmediate& RHICmdList, struct FHairStrandsRenderingData* HairDatas);

	/** Computes DFAO, modulates it to scene color (which is assumed to contain diffuse indirect lighting), and stores the output bent normal for use occluding specular. */
	void RenderDFAOAsIndirectShadowing(
		FRHICommandListImmediate& RHICmdList,
		const TRefCountPtr<IPooledRenderTarget>& VelocityTexture,
		TRefCountPtr<IPooledRenderTarget>& DynamicBentNormalAO);

	/** Render Ambient Occlusion using mesh distance fields and the surface cache, which supports dynamic rigid meshes. */
	bool RenderDistanceFieldLighting(
		FRHICommandListImmediate& RHICmdList, 
		const class FDistanceFieldAOParameters& Parameters, 
		const TRefCountPtr<IPooledRenderTarget>& VelocityTexture,
		TRefCountPtr<IPooledRenderTarget>& OutDynamicBentNormalAO,
		bool bModulateToSceneColor,
		bool bVisualizeAmbientOcclusion);

	/** Render Ambient Occlusion using mesh distance fields on a screen based grid. */
	void RenderDistanceFieldAOScreenGrid(
		FRHICommandListImmediate& RHICmdList, 
		const FViewInfo& View,
		const FDistanceFieldAOParameters& Parameters, 
		const TRefCountPtr<IPooledRenderTarget>& VelocityTexture,
		const TRefCountPtr<IPooledRenderTarget>& DistanceFieldNormal, 
		TRefCountPtr<IPooledRenderTarget>& OutDynamicBentNormalAO);

	void RenderMeshDistanceFieldVisualization(FRHICommandListImmediate& RHICmdList, const FDistanceFieldAOParameters& Parameters);

	/** Whether tiled deferred is supported and can be used at all. */
	bool CanUseTiledDeferred() const;

	/** Whether to use tiled deferred shading given a number of lights that support it. */
	bool ShouldUseTiledDeferred(int32 NumTiledDeferredLights) const;

	/** 
	 * True if the 'r.UseClusteredDeferredShading' flag is 1 and sufficient feature level. 
	 * NOTE: When true it takes precedence over the TiledDeferred path, since they handle the same lights.
	 */
	bool ShouldUseClusteredDeferredShading() const;

	/**
	 * Have the requisite lights been injected into the light grid, AKA can we run the shading pass?
	 */
	bool AreClusteredLightsInLightGrid() const;


	/** Add a clustered deferred shading lighting render pass.	Note: in the future it should take the RenderGraph builder as argument */
	void AddClusteredDeferredShadingPass(FRHICommandListImmediate& RHICmdList, const FSortedLightSetSceneInfo &SortedLightsSet);

	/** Renders the lights in SortedLights in the range [TiledDeferredLightsStart, TiledDeferredLightsEnd) using tiled deferred shading. */
	void RenderTiledDeferredLighting(FRHICommandListImmediate& RHICmdList, const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& SortedLights, int32 TiledDeferredLightsStart, int32 TiledDeferredLightsEnd, const FSimpleLightArray& SimpleLights);

	/**
	 * Rounds up lights and sorts them according to what type of renderer supports them. The result is stored in OutSortedLights 
	 * NOTE: Also extracts the SimpleLights AND adds them to the sorted range (first sub-range). 
	 */
	void GatherAndSortLights(FSortedLightSetSceneInfo& OutSortedLights);

	/** Renders the scene's lighting. */
	void RenderLights(FRHICommandListImmediate& RHICmdList, FSortedLightSetSceneInfo &SortedLightSet, const struct FHairStrandsRenderingData* HairDatas);

	/** Renders an array of lights for the stationary light overlap viewmode. */
	void RenderLightArrayForOverlapViewmode(FRHICommandListImmediate& RHICmdList, const TSparseArray<FLightSceneInfoCompact>& LightArray);

	/** Render stationary light overlap as complexity to scene color. */
	void RenderStationaryLightOverlap(FRHICommandListImmediate& RHICmdList);
	
	/** Issues a timestamp query for the beginning of the separate translucency pass. */
	void BeginTimingSeparateTranslucencyPass(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	/** Issues a timestamp query for the end of the separate translucency pass. */
	void EndTimingSeparateTranslucencyPass(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	/** Issues a timestamp query for the beginning of the separate translucency modulate pass. */
	void BeginTimingSeparateTranslucencyModulatePass(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	/** Issues a timestamp query for the end of the separate translucency modulate pass. */
	void EndTimingSeparateTranslucencyModulatePass(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	/** Setup the downsampled view uniform parameters if it was not already built */
	void SetupDownsampledTranslucencyViewParameters(
		FRHICommandListImmediate& RHICmdList, 
		const FViewInfo& View,
		FViewUniformShaderParameters& DownsampledTranslucencyViewParameters);

	/** Resolve the scene color if any translucent material needs it. */
	void ConditionalResolveSceneColorForTranslucentMaterials(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& SceneColorCopy);

	/** Renders the scene's translucency passes. */
	void RenderTranslucency(FRHICommandListImmediate& RHICmdList, bool bDrawUnderwaterViews = false);

	/** Renders the scene's translucency given a specific pass. */
	void RenderTranslucencyInner(FRHICommandListImmediate& RHICmdList, ETranslucencyPass::Type TranslucencyPass, IPooledRenderTarget* SceneColorCopy, bool bDrawUnderwaterViews = false);

	/** Renders the scene's light shafts */
	void RenderLightShaftOcclusion(FRHICommandListImmediate& RHICmdList, FLightShaftsOutput& Output);

	void RenderLightShaftBloom(FRHICommandListImmediate& RHICmdList);

	bool ShouldRenderVelocities() const;

	/** Renders the velocities of movable objects for the motion blur effect. */
	void RenderVelocities(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& VelocityRT, EVelocityPass VelocityPass, bool bClearVelocityRT);

	/** Renders the velocities of movable objects for the motion blur effect. */
	void RenderVelocitiesInner(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& VelocityRT, EVelocityPass VelocityPass);
	void RenderVelocitiesInnerParallel(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& VelocityRT, EVelocityPass VelocityPass);

	/** Renders world-space lightmap density instead of the normal color. */
	bool RenderLightMapDensities(FRHICommandListImmediate& RHICmdList);

	/** Renders one of the EDebugViewShaderMode instead of the normal color. */
	bool RenderDebugViewMode(FRHICommandListImmediate& RHICmdList);

	/** Updates the downsized depth buffer with the current full resolution depth buffer. */
	void UpdateDownsampledDepthSurface(FRHICommandList& RHICmdList);

	/** Downsample the scene depth with a specified scale factor to a specified render target*/
	void DownsampleDepthSurface(FRHICommandList& RHICmdList, const FTexture2DRHIRef& RenderTarget, const FViewInfo& View, float ScaleFactor, bool bUseMaxDepth);

	void CopyStencilToLightingChannelTexture(FRHICommandList& RHICmdList);

	/** Injects reflective shadowmaps into LPVs */
	bool InjectReflectiveShadowMaps(FRHICommandListImmediate& RHICmdList, const FLightSceneInfo* LightSceneInfo);

	/** Renders capsule shadows for all per-object shadows using it for the given light. */
	bool RenderCapsuleDirectShadows(
		FRHICommandListImmediate& RHICmdList, 
		const FLightSceneInfo& LightSceneInfo, 
		IPooledRenderTarget* ScreenShadowMaskTexture,
		const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& CapsuleShadows, 
		bool bProjectingForForwardShading) const;

	/** Sets up ViewState buffers for rendering capsule shadows. */
	void SetupIndirectCapsuleShadows(
		FRHICommandListImmediate& RHICmdList, 
		const FViewInfo& View, 
		int32& NumCapsuleShapes, 
		int32& NumMeshesWithCapsules, 
		int32& NumMeshDistanceFieldCasters,
		FRHIShaderResourceView*& IndirectShadowLightDirectionSRV) const;

	/** Renders indirect shadows from capsules modulated onto scene color. */
	void RenderIndirectCapsuleShadows(
		FRHICommandListImmediate& RHICmdList, 
		FRHITexture* IndirectLightingTexture,
		FRHITexture* ExistingIndirectOcclusionTexture) const;

	/** Renders capsule shadows for movable skylights, using the cone of visibility (bent normal) from DFAO. */
	void RenderCapsuleShadowsForMovableSkylight(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& BentNormalOutput) const;

	/** Render deferred projections of shadows for a given light into the light attenuation buffer. */
	bool RenderShadowProjections(FRHICommandListImmediate& RHICmdList, const FLightSceneInfo* LightSceneInfo, IPooledRenderTarget* ScreenShadowMaskTexture, IPooledRenderTarget* ScreenShadowMaskSubPixelTexture, const struct FHairStrandsRenderingData* HairDatas, bool& bInjectedTranslucentVolume);

	/** Render shadow projections when forward rendering. */
	void RenderForwardShadingShadowProjections(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& ForwardScreenSpaceShadowMask, TRefCountPtr<IPooledRenderTarget>& ForwardScreenSpaceShadowMaskSubPixel, const struct FHairStrandsRenderingData* InHairDatas);

	/**
	  * Used by RenderLights to render a light function to the attenuation buffer.
	  *
	  * @param LightSceneInfo Represents the current light
	  * @param LightIndex The light's index into FScene::Lights
	  */
	bool RenderLightFunction(FRHICommandListImmediate& RHICmdList, const FLightSceneInfo* LightSceneInfo, IPooledRenderTarget* ScreenShadowMaskTexture, bool bLightAttenuationCleared, bool bProjectingForForwardShading);

	/** Renders a light function indicating that whole scene shadowing being displayed is for previewing only, and will go away in game. */
	bool RenderPreviewShadowsIndicator(FRHICommandListImmediate& RHICmdList, const FLightSceneInfo* LightSceneInfo, IPooledRenderTarget* ScreenShadowMaskTexture, bool bLightAttenuationCleared);

	/** Renders a light function with the given material. */
	bool RenderLightFunctionForMaterial(
		FRHICommandListImmediate& RHICmdList, 
		const FLightSceneInfo* LightSceneInfo, 
		IPooledRenderTarget* ScreenShadowMaskTexture,
		const FMaterialRenderProxy* MaterialProxy, 
		bool bLightAttenuationCleared,
		bool bProjectingForForwardShading, 
		bool bRenderingPreviewShadowsIndicator);

	/**
	  * Used by RenderLights to render a light to the scene color buffer.
	  *
	  * @param LightSceneInfo Represents the current light
	  * @param LightIndex The light's index into FScene::Lights
	  * @return true if anything got rendered
	  */
	void RenderLight(FRHICommandList& RHICmdList, const FLightSceneInfo* LightSceneInfo, IPooledRenderTarget* ScreenShadowMaskTexture, const struct FHairStrandsVisibilityViews* InHairVisibilityViews, bool bRenderOverlap, bool bIssueDrawEvent);
	void RenderLightsForHair(FRHICommandListImmediate& RHICmdList, FSortedLightSetSceneInfo &SortedLightSet, const FHairStrandsRenderingData* HairDatas, TRefCountPtr<IPooledRenderTarget>& InScreenShadowMaskSubPixelTexture);

	/** Specialized version of RenderLight for hair (run lighting evaluation on at sub-pixel rate, without depth bound) */
	void RenderLightForHair(FRHICommandList& RHICmdList, const FLightSceneInfo* LightSceneInfo, IPooledRenderTarget* ScreenShadowMaskSubPixelTexture, struct FHairStrandsTransmittanceMaskData* InTransmittanceMaskData, const struct FHairStrandsVisibilityViews* InHairVisibilityViews);

	/** Renders an array of simple lights using standard deferred shading. */
	void RenderSimpleLightsStandardDeferred(FRHICommandListImmediate& RHICmdList, const FSimpleLightArray& SimpleLights);

	/** Clears the translucency lighting volumes before light accumulation. */
	void ClearTranslucentVolumeLighting(FRHICommandListImmediate& RHICmdListViewIndex, int32 ViewIndex);

	/** Clears the translucency lighting volume via an async compute shader overlapped with the basepass. */
	void ClearTranslucentVolumeLightingAsyncCompute(FRHICommandListImmediate& RHICmdList);

	/** Add AmbientCubemap to the lighting volumes. */
	void InjectAmbientCubemapTranslucentVolumeLighting(FRHICommandList& RHICmdList, const FViewInfo& View, int32 ViewIndex);

	/** Clears the volume texture used to accumulate per object shadows for translucency. */
	void ClearTranslucentVolumePerObjectShadowing(FRHICommandList& RHICmdList, const int32 ViewIndex);

	/** Accumulates the per object shadow's contribution for translucency. */
	void AccumulateTranslucentVolumeObjectShadowing(FRHICommandList& RHICmdList, const FProjectedShadowInfo* InProjectedShadowInfo, bool bClearVolume, const FViewInfo& View, const int32 ViewIndex);

	/** Accumulates direct lighting for the given light.  InProjectedShadowInfo can be NULL in which case the light will be unshadowed. */
	void InjectTranslucentVolumeLighting(FRHICommandListImmediate& RHICmdList, const FLightSceneInfo& LightSceneInfo, const FProjectedShadowInfo* InProjectedShadowInfo, const FViewInfo& View, int32 ViewIndex);

	/** Accumulates direct lighting for an array of unshadowed lights. */
	void InjectTranslucentVolumeLightingArray(FRHICommandListImmediate& RHICmdList, const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& SortedLights, int32 FirstLightIndex, int32 LightsEndIndex);

	/** Accumulates direct lighting for simple lights. */
	void InjectSimpleTranslucentVolumeLightingArray(FRHICommandListImmediate& RHICmdList, const FSimpleLightArray& SimpleLights, const FViewInfo& View, const int32 ViewIndex);

	/** Filters the translucency lighting volumes to reduce aliasing. */
	void FilterTranslucentVolumeLighting(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const int32 ViewIndex);

	bool ShouldRenderVolumetricFog() const;

	void SetupVolumetricFog();

	void RenderLocalLightsForVolumetricFog(
		FRDGBuilder& GraphBuilder,
		FViewInfo& View,
		bool bUseTemporalReprojection,
		const struct FVolumetricFogIntegrationParameterData& IntegrationData,
		const FExponentialHeightFogSceneInfo& FogInfo,
		FIntVector VolumetricFogGridSize,
		FVector GridZParams,
		const FPooledRenderTargetDesc& VolumeDesc,
		FRDGTexture*& OutLocalShadowedLightScattering);

	void RenderLightFunctionForVolumetricFog(
		FRDGBuilder& GraphBuilder,
		FViewInfo& View,
		FIntVector VolumetricFogGridSize,
		float VolumetricFogMaxDistance,
		FMatrix& OutLightFunctionWorldToShadow,
		FRDGTexture*& OutLightFunctionTexture,
		bool& bOutUseDirectionalLightShadowing);

	void VoxelizeFogVolumePrimitives(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const FVolumetricFogIntegrationParameterData& IntegrationData,
		FIntVector VolumetricFogGridSize,
		FVector GridZParams,
		float VolumetricFogDistance);

	void ComputeVolumetricFog(FRHICommandListImmediate& RHICmdList);

	void VisualizeVolumetricLightmap(FRHICommandListImmediate& RHICmdList);

	/** Render image based reflections (SSR, Env, SkyLight) without compute shaders */
	void RenderStandardDeferredImageBasedReflections(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit, bool bReflectionEnv, const TRefCountPtr<IPooledRenderTarget>& DynamicBentNormalAO, TRefCountPtr<IPooledRenderTarget>& VelocityRT);

	bool HasDeferredPlanarReflections(const FViewInfo& View) const;
	void RenderDeferredPlanarReflections(FRDGBuilder& GraphBuilder, const FSceneTextureParameters& SceneTextures, const FViewInfo& View, FRDGTextureRef& ReflectionsOutput);

	bool ShouldDoReflectionEnvironment() const;
	
	bool ShouldRenderDistanceFieldAO() const;

	/** Whether distance field global data structures should be prepared for features that use it. */
	bool ShouldPrepareForDistanceFieldShadows() const;
	bool ShouldPrepareForDistanceFieldAO() const;
	bool ShouldPrepareForDFInsetIndirectShadow() const;

	bool ShouldPrepareDistanceFieldScene() const;
	bool ShouldPrepareGlobalDistanceField() const;
	bool ShouldPrepareHeightFieldScene() const;

	void UpdateGlobalDistanceFieldObjectBuffers(FRHICommandListImmediate& RHICmdList);
	void UpdateGlobalHeightFieldObjectBuffers(FRHICommandListImmediate& RHICmdList);
	void AddOrRemoveSceneHeightFieldPrimitives(bool bSkipAdd = false);
	void PrepareDistanceFieldScene(FRHICommandListImmediate& RHICmdList, bool bSplitDispatch);

	void RenderViewTranslucency(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FMeshPassProcessorRenderState& DrawRenderState, ETranslucencyPass::Type TranslucenyPass);
	void RenderViewTranslucencyParallel(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FMeshPassProcessorRenderState& DrawRenderState, ETranslucencyPass::Type TranslucencyPass);

	void CopySceneCaptureComponentToTarget(FRHICommandListImmediate& RHICmdList);

	void SetupImaginaryReflectionTextureParameters(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FSceneTextureParameters* OutTextures);
	void RenderRayTracingReflections(
		FRDGBuilder& GraphBuilder,
		const FSceneTextureParameters& SceneTextures,
		const FViewInfo& View,
		const FRayTracingReflectionOptions& Options,
		IScreenSpaceDenoiser::FReflectionsInputs* OutDenoiserInputs);

	void RenderRayTracingDeferredReflections(
		FRDGBuilder& GraphBuilder,
		const FSceneTextureParameters& SceneTextures,
		const FViewInfo& View,
		const FRayTracingReflectionOptions& Options,
		IScreenSpaceDenoiser::FReflectionsInputs* OutDenoiserInputs);

	void RenderDitheredLODFadingOutMask(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	void RenderRayTracingShadows(
		FRDGBuilder& GraphBuilder,
		const FSceneTextureParameters& SceneTextures,
		const FViewInfo& View,
		const FLightSceneInfo& LightSceneInfo,
		const IScreenSpaceDenoiser::FShadowRayTracingConfig& RayTracingConfig,
		const IScreenSpaceDenoiser::EShadowRequirements DenoiserRequirements,
		const struct FHairStrandsOcclusionResources* HairResources,
		FRDGTextureUAV* OutShadowMaskUAV,
		FRDGTextureUAV* OutRayHitDistanceUAV,
		FRDGTextureUAV* SubPixelRayTracingShadowMaskUAV);

	void RenderRayTracingStochasticRectLight(FRHICommandListImmediate& RHICmdList, const FLightSceneInfo& RectLightSceneInfo, TRefCountPtr<IPooledRenderTarget>& RectLightRT, TRefCountPtr<IPooledRenderTarget>& HitDistanceRT);
	void CompositeRayTracingSkyLight(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& SkyLightRT, TRefCountPtr<IPooledRenderTarget>& HitDistanceRT);
	
	bool RenderRayTracingGlobalIllumination(
		FRDGBuilder& GraphBuilder, 
		FSceneTextureParameters& SceneTextures,
		FViewInfo& View,
		IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig* RayTracingConfig,
		IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs);
	
	void RenderRayTracingGlobalIlluminationBruteForce(
		FRDGBuilder& GraphBuilder,
		FSceneTextureParameters& SceneTextures,
		FViewInfo& View,
		const IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig& RayTracingConfig,
		int32 UpscaleFactor,
		IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs);

	void RayTracingGlobalIlluminationCreateGatherPoints(
		FRDGBuilder& GraphBuilder,
		FSceneTextureParameters& SceneTextures,
		FViewInfo& View,
		int32 UpscaleFactor,
		FRDGBufferRef& GatherPointsBuffer,
		FIntVector& GatherPointsResolution);

	void RenderRayTracingGlobalIlluminationFinalGather(
		FRDGBuilder& GraphBuilder,
		FSceneTextureParameters& SceneTextures,
		FViewInfo& View,
		const IScreenSpaceDenoiser::FAmbientOcclusionRayTracingConfig& RayTracingConfig,
		int32 UpscaleFactor,
		IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs);
	
	void RenderRayTracingAmbientOcclusion(
		FRDGBuilder& GraphBuilder,
		FViewInfo& View,
		const FSceneTextureParameters& SceneTextures,
		FRDGTextureRef* OutAmbientOcclusionTexture);
	

#if RHI_RAYTRACING
	template <int TextureImportanceSampling> void RenderRayTracingRectLightInternal(FRHICommandListImmediate& RHICmdList, const TArray<FViewInfo>& Views, const FLightSceneInfo& RectLightSceneInfo, TRefCountPtr<IPooledRenderTarget>& ScreenShadowMaskTexture, TRefCountPtr<IPooledRenderTarget>& RayDistanceTexture);
	void VisualizeRectLightMipTree(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FRWBuffer& RectLightMipTree, const FIntVector& RectLightMipTreeDimensions);

	void GenerateSkyLightVisibilityRays(FRDGBuilder& GraphBuilder, FRDGBufferRef& SkyLightVisibilityRays, FIntVector& Dimensions);
	void VisualizeSkyLightMipTree(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FRWBuffer& SkyLightMipTreePosX, FRWBuffer& SkyLightMipTreePosY, FRWBuffer& SkyLightMipTreePosZ, FRWBuffer& SkyLightMipTreeNegX, FRWBuffer& SkyLightMipTreeNegY, FRWBuffer& SkyLightMipTreeNegZ, const FIntVector& SkyLightMipDimensions);
	void RenderRayTracingSkyLight(FRHICommandListImmediate& RHICmdList, TRefCountPtr<IPooledRenderTarget>& SkyLightRT, TRefCountPtr<IPooledRenderTarget>& HitDistanceRT, const struct FHairStrandsRenderingData* HairDatas);

	void RenderRayTracingPrimaryRaysView(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FRDGTextureRef* InOutColorTexture,
		FRDGTextureRef* InOutRayHitDistanceTexture,
		int32 SamplePerPixel,
		int32 HeightFog,
		float ResolutionFraction,
		ERayTracingPrimaryRaysFlag Flags);
	void RenderRayTracingTranslucency(FRHICommandListImmediate& RHICmdList);
	void RenderRayTracingTranslucencyView(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		FRDGTextureRef* OutColorTexture,
		FRDGTextureRef* OutRayHitDistanceTexture,
		int32 SamplePerPixel,
		int32 HeightFog,
		float ResolutionFraction);

	/** Lighting Evaluation shader setup (used by ray traced reflections and translucency) */
	void SetupRayTracingLightingMissShader(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	/** Path tracing functions. */
	void RenderPathTracing(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	void BuildVarianceMipTree(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FTextureRHIRef MeanAndDeviationTexture,
		FRWBuffer& VarianceMipTree, FIntVector& VarianceMipTreeDimensions);

	void VisualizeVarianceMipTree(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, const FRWBuffer& VarianceMipTree, FIntVector VarianceMipTreeDimensions);

	void ComputePathCompaction(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FRHITexture* RadianceTexture, FRHITexture* SampleCountTexture, FRHITexture* PixelPositionTexture,
		FRHIUnorderedAccessView* RadianceSortedRedUAV, FRHIUnorderedAccessView* RadianceSortedGreenUAV, FRHIUnorderedAccessView* RadianceSortedBlueUAV, FRHIUnorderedAccessView* RadianceSortedAlphaUAV, FRHIUnorderedAccessView* SampleCountSortedUAV);

	void ComputeRayCount(FRHICommandListImmediate& RHICmdList, const FViewInfo& View, FRHITexture* RayCountPerPixelTexture);

	void WaitForRayTracingScene(FRHICommandListImmediate& RHICmdList);

	/** Debug ray tracing functions. */
	void RenderRayTracingDebug(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);
	void RenderRayTracingBarycentrics(FRHICommandListImmediate& RHICmdList, const FViewInfo& View);

	bool GatherRayTracingWorldInstances(FRHICommandListImmediate& RHICmdList);
	bool DispatchRayTracingWorldUpdates(FRHICommandListImmediate& RHICmdList);
	FRayTracingPipelineState* BindRayTracingMaterialPipeline(FRHICommandList& RHICmdList, FViewInfo& View, const TArrayView<FRHIRayTracingShader*>& RayGenShaderTable, FRHIRayTracingShader* DefaultClosestHitShader);
	FRayTracingPipelineState* BindRayTracingDeferredMaterialGatherPipeline(FRHICommandList& RHICmdList, const FViewInfo& View, FRHIRayTracingShader* RayGenShader);

	// #dxr_todo: UE-72565: refactor ray tracing effects to not be member functions of DeferredShadingRenderer. Register each effect at startup and just loop over them automatically
	static void PrepareRayTracingReflections(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingDeferredReflections(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareSingleLayerWaterRayTracingReflections(const FViewInfo& View, const FScene& Scene, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingShadows(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingAmbientOcclusion(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingSkyLight(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingRectLight(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingGlobalIllumination(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingTranslucency(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PrepareRayTracingDebug(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	static void PreparePathTracing(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);

	/** Lighting evaluation shader registration */
	static FRHIRayTracingShader* GetRayTracingLightingMissShader(FViewInfo& View);

	FComputeFenceRHIRef RayTracingDynamicGeometryUpdateBeginFence; // Signaled when ray tracing AS can start building
	FComputeFenceRHIRef RayTracingDynamicGeometryUpdateEndFence; // Signaled when all AS for this frame are built

#endif // RHI_RAYTRACING

	/** Set to true if the lights needed for clustered shading have been injected in the light grid (set in ComputeLightGrid). */
	bool bClusteredShadingLightsInLightGrid;
};

DECLARE_CYCLE_STAT_EXTERN(TEXT("PrePass"), STAT_CLM_PrePass, STATGROUP_CommandListMarkers, );