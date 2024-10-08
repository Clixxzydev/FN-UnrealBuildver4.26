// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Lights.h"
#include "Scene/StaticMesh.h"
#include "Scene/InstancedStaticMesh.h"
#include "Scene/Landscape.h"
#include "MeshPassProcessor.h"
#include "IrradianceCaching.h"

class FGPULightmass;

namespace GPULightmass
{
class FLightmapRenderer;
class FVolumetricLightmapRenderer;

class FFullyCachedRayTracingMeshCommandContext : public FRayTracingMeshCommandContext
{
public:
	FFullyCachedRayTracingMeshCommandContext(
		TChunkedArray<FRayTracingMeshCommand>& CommandStorage,
		TArray<FVisibleRayTracingMeshCommand>& VisibleCommandStorage,
		uint32 InGeometrySegmentIndex = ~0u,
		uint32 InRayTracingInstanceIndex = ~0u
	)
		: CommandStorage(CommandStorage)
		, VisibleCommandStorage(VisibleCommandStorage)
		, GeometrySegmentIndex(InGeometrySegmentIndex)
		, RayTracingInstanceIndex(InRayTracingInstanceIndex) {}

	virtual FRayTracingMeshCommand& AddCommand(const FRayTracingMeshCommand& Initializer) override final
	{
		const int32 Index = CommandStorage.AddElement(Initializer);
		FRayTracingMeshCommand& NewCommand = CommandStorage[Index];
		NewCommand.GeometrySegmentIndex = GeometrySegmentIndex;
		return NewCommand;
	}

	virtual void FinalizeCommand(FRayTracingMeshCommand& RayTracingMeshCommand) override final 
	{
		FVisibleRayTracingMeshCommand NewVisibleMeshCommand;
		NewVisibleMeshCommand.RayTracingMeshCommand = &RayTracingMeshCommand;
		NewVisibleMeshCommand.InstanceIndex = RayTracingInstanceIndex;
		VisibleCommandStorage.Add(NewVisibleMeshCommand);
		check(NewVisibleMeshCommand.RayTracingMeshCommand);
	}

private:
	TChunkedArray<FRayTracingMeshCommand>& CommandStorage;
	TArray<FVisibleRayTracingMeshCommand>& VisibleCommandStorage;
	uint32 GeometrySegmentIndex;
	uint32 RayTracingInstanceIndex;
};

struct FCachedRayTracingSceneData
{
	TArray<FVisibleRayTracingMeshCommand> VisibleRayTracingMeshCommands;
	TChunkedArray<FRayTracingMeshCommand> MeshCommandStorage;

	FStructuredBufferRHIRef PrimitiveSceneDataBufferRHI;
	FShaderResourceViewRHIRef PrimitiveSceneDataBufferSRV;

	FStructuredBufferRHIRef LightmapSceneDataBufferRHI;
	FShaderResourceViewRHIRef LightmapSceneDataBufferSRV;

	TArray<FRayTracingGeometryInstance> RayTracingGeometryInstances;

	TUniformBufferRef<FViewUniformShaderParameters> CachedViewUniformBuffer;

	void SetupViewUniformBufferFromSceneRenderState(class FSceneRenderState& Scene);
	void SetupFromSceneRenderState(class FSceneRenderState& Scene);
};

class FSceneRenderState
{
public:
	void RenderThreadInit();
	void BackgroundTick();

	FRayTracingSceneRHIRef RayTracingScene;
	FRayTracingPipelineState* RayTracingPipelineState;
	TUniquePtr<FViewInfo> ReferenceView;

	TUniquePtr<FCachedRayTracingSceneData> CachedRayTracingScene;

	TGeometryInstanceRenderStateCollection<FStaticMeshInstanceRenderState> StaticMeshInstanceRenderStates;
	TGeometryInstanceRenderStateCollection<FInstanceGroupRenderState> InstanceGroupRenderStates;
	TGeometryInstanceRenderStateCollection<FLandscapeRenderState> LandscapeRenderStates;

	TEntityArray<FLightmapRenderState> LightmapRenderStates;

	FLightSceneRenderState LightSceneRenderState;

	TUniquePtr<FLightmapRenderer> LightmapRenderer;
	TUniquePtr<FVolumetricLightmapRenderer> VolumetricLightmapRenderer;
	TUniquePtr<FIrradianceCache> IrradianceCache;

	void SetupRayTracingScene();
	void DestroyRayTracingScene();

	void CalculateDistributionPrefixSumForAllLightmaps();

	volatile int32 Percentage = 0;
};

class FScene;

class FGeometryRange
{
public:
	FGeometryRange(FScene& Scene) : Scene(Scene) {}

	FGeometryIterator begin();
	FGeometryIterator end();

private:
	FScene& Scene;
};

class FScene
{
public:
	FScene(FGPULightmass* InGPULightmass);

	FGPULightmass* GPULightmass;

	const FMeshMapBuildData* GetComponentLightmapData(const UPrimitiveComponent* InComponent, int32 LODIndex);
	const FLightComponentMapBuildData* GetComponentLightmapData(const ULightComponent* InComponent);

	void AddGeometryInstanceFromComponent(UStaticMeshComponent* InComponent);
	void RemoveGeometryInstanceFromComponent(UStaticMeshComponent* InComponent);

	void AddGeometryInstanceFromComponent(UInstancedStaticMeshComponent* InComponent);
	void RemoveGeometryInstanceFromComponent(UInstancedStaticMeshComponent* InComponent);

	void AddGeometryInstanceFromComponent(ULandscapeComponent* InComponent);
	void RemoveGeometryInstanceFromComponent(ULandscapeComponent* InComponent);

	void AddLight(USkyLightComponent* SkyLight);
	void RemoveLight(USkyLightComponent* SkyLight);

	template<typename LightComponentType>
	void AddLight(LightComponentType* Light);

	template<typename LightComponentType>
	void RemoveLight(LightComponentType* Light);

	template<typename LightComponentType>
	bool HasLight(LightComponentType* Light);

	void GatherImportanceVolumes();
	
	void BackgroundTick();
	void ApplyFinishedLightmapsToWorld();
	void RemoveAllComponents();

	TGeometryArray<FStaticMeshInstance> StaticMeshInstances;
	TGeometryArray<FInstanceGroup> InstanceGroups;
	TGeometryArray<FLandscape> Landscapes;

	FGeometryRange Geometries;

	TEntityArray<FLightmap> Lightmaps;

	FLightScene LightScene;

	FSceneRenderState RenderState;

	bool bNeedsVoxelization = true;

private:
	TMap<UStaticMeshComponent*, FStaticMeshInstanceRef> RegisteredStaticMeshComponentUObjects;
	TMap<UInstancedStaticMeshComponent*, FInstanceGroupRef> RegisteredInstancedStaticMeshComponentUObjects;
	TMap<ULandscapeComponent*, FLandscapeRef> RegisteredLandscapeComponentUObjects;
};

}
