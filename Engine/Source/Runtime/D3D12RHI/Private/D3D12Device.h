// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12Device.h: D3D12 Device Interfaces
=============================================================================*/

#pragma once

#include "CoreMinimal.h"

class FD3D12DynamicRHI;
class FD3D12BasicRayTracingPipeline;
class FD3D12RayTracingDescriptorHeapCache;
class FD3D12RayTracingPipelineCache;

class FD3D12Device : public FD3D12SingleNodeGPUObject, public FNoncopyable, public FD3D12AdapterChild
{
public:
	FD3D12Device();
	FD3D12Device(FRHIGPUMask InGPUMask, FD3D12Adapter* InAdapter);
	virtual ~FD3D12Device();

	/** Initialized members*/
	void Initialize();

	void CreateCommandContexts();

	void InitPlatformSpecific();
	/**
	* Cleanup the device.
	* This function must be called from the main game thread.
	*/
	virtual void Cleanup();

	/**
	* Populates a D3D query's data buffer.
	* @param Query - The occlusion query to read data from.
	* @param bWait - If true, it will wait for the query to finish.
	* @return true if the query finished.
	*/
	bool GetQueryData(FD3D12RenderQuery& Query, bool bWait);

	ID3D12Device* GetDevice();

#if D3D12_RHI_RAYTRACING
	void									InitRayTracing();
	void									CleanupRayTracing();
	ID3D12Device5*							GetDevice5();
	ID3D12Device7*							GetDevice7();
	const FD3D12BasicRayTracingPipeline*	GetBasicRayTracingPipeline() const { return BasicRayTracingPipeline; }
	FD3D12RayTracingDescriptorHeapCache*	GetRayTracingDescriptorHeapCache() { return RayTracingDescriptorHeapCache; }
	FD3D12RayTracingPipelineCache*			GetRayTracingPipelineCache() { return RayTracingPipelineCache; }
#endif // D3D12_RHI_RAYTRACING

	FD3D12DynamicRHI* GetOwningRHI();

	inline FD3D12QueryHeap* GetOcclusionQueryHeap() { return &OcclusionQueryHeap; }
	inline FD3D12QueryHeap* GetTimestampQueryHeap() { return &TimestampQueryHeap; }
	FD3D12LinearQueryHeap* GetCmdListExecTimeQueryHeap();

	template <typename TViewDesc> FD3D12OfflineDescriptorManager& GetViewDescriptorAllocator();
	template<> FD3D12OfflineDescriptorManager& GetViewDescriptorAllocator<D3D12_SHADER_RESOURCE_VIEW_DESC>() { return SRVAllocator; }
	template<> FD3D12OfflineDescriptorManager& GetViewDescriptorAllocator<D3D12_RENDER_TARGET_VIEW_DESC>() { return RTVAllocator; }
	template<> FD3D12OfflineDescriptorManager& GetViewDescriptorAllocator<D3D12_DEPTH_STENCIL_VIEW_DESC>() { return DSVAllocator; }
	template<> FD3D12OfflineDescriptorManager& GetViewDescriptorAllocator<D3D12_UNORDERED_ACCESS_VIEW_DESC>() { return UAVAllocator; }
#if USE_STATIC_ROOT_SIGNATURE
	template<> FD3D12OfflineDescriptorManager& GetViewDescriptorAllocator<D3D12_CONSTANT_BUFFER_VIEW_DESC>() { return CBVAllocator; }
#else
	template<> FD3D12OfflineDescriptorManager& GetViewDescriptorAllocator<D3D12_CONSTANT_BUFFER_VIEW_DESC>() { check(false); abort(); }
#endif

	inline FD3D12OfflineDescriptorManager& GetSamplerDescriptorAllocator() { return SamplerAllocator; }
	inline FD3D12CommandListManager& GetCommandListManager() { return *CommandListManager; }
	inline FD3D12CommandListManager& GetCopyCommandListManager() { return *CopyCommandListManager; }
	inline FD3D12CommandListManager& GetAsyncCommandListManager() { return *AsyncCommandListManager; }
	inline FD3D12CommandAllocatorManager& GetTextureStreamingCommandAllocatorManager() { return TextureStreamingCommandAllocatorManager; }
	inline FD3D12DefaultBufferAllocator& GetDefaultBufferAllocator() { return DefaultBufferAllocator; }
	inline FD3D12GlobalOnlineSamplerHeap& GetGlobalSamplerHeap() { return GlobalSamplerHeap; }
	inline FD3D12GlobalHeap& GetGlobalViewHeap() { return GlobalViewHeap; }

	bool IsGPUIdle();

	inline const D3D12_HEAP_PROPERTIES &GetConstantBufferPageProperties() { return ConstantBufferPageProperties; }

	inline uint32 GetNumContexts() { return CommandContextArray.Num(); }
	inline FD3D12CommandContext& GetCommandContext(uint32 ThreadIndex = 0) const { return *CommandContextArray[ThreadIndex]; }

	inline uint32 GetNumAsyncComputeContexts() { return AsyncComputeContextArray.Num(); }
	inline FD3D12CommandContext& GetAsyncComputeContext(uint32 ThreadIndex = 0) const { return *AsyncComputeContextArray[ThreadIndex]; }

	inline FD3D12CommandContext* ObtainCommandContext() 
	{
		FScopeLock Lock(&FreeContextsLock);
		return FreeCommandContexts.Pop();
	}
	inline void ReleaseCommandContext(FD3D12CommandContext* CmdContext) 
	{
		check(!CmdContext || CmdContext->GetGPUIndex() == GetGPUIndex());
		FScopeLock Lock(&FreeContextsLock);
		FreeCommandContexts.Add(CmdContext);
	}

	FD3D12CommandListManager* GetCommandListManager(ED3D12CommandQueueType inQueueType) const;
	ID3D12CommandQueue* GetD3DCommandQueue(ED3D12CommandQueueType InQueueType = ED3D12CommandQueueType::Default) { return GetCommandListManager(InQueueType)->GetD3DCommandQueue(); }

	inline FD3D12CommandContext& GetDefaultCommandContext() const { return GetCommandContext(0); }
	inline FD3D12CommandContext& GetDefaultAsyncComputeContext() const { return GetAsyncComputeContext(0); }
	inline FD3D12FastAllocator& GetDefaultFastAllocator() { return DefaultFastAllocator; }
	inline FD3D12TextureAllocatorPool& GetTextureAllocator() { return TextureAllocator; }
	inline FD3D12ResidencyManager& GetResidencyManager() { return ResidencyManager; }

	TArray<FD3D12CommandListHandle> PendingCommandLists;

	D3D12RHI_API void RegisterGPUWork(uint32 NumPrimitives = 0, uint32 NumVertices = 0);
	D3D12RHI_API void RegisterGPUDispatch(FIntVector GroupCount);
	
	FD3D12SamplerState* CreateSampler(const FSamplerStateInitializerRHI& Initializer);
	void CreateSamplerInternal(const D3D12_SAMPLER_DESC& Desc, D3D12_CPU_DESCRIPTOR_HANDLE Descriptor);

	void BlockUntilIdle();

	FORCEINLINE FD3DGPUProfiler& GetGPUProfiler() { return GPUProfilingData; }

protected:

	/** A pool of command lists we can cycle through for the global D3D device */
	FD3D12CommandListManager* CommandListManager;
	FD3D12CommandListManager* CopyCommandListManager;
	FD3D12CommandListManager* AsyncCommandListManager;

	/** A pool of command allocators that texture streaming threads share */
	FD3D12CommandAllocatorManager TextureStreamingCommandAllocatorManager;

	// Must be before the StateCache so that destructor ordering is valid
	FD3D12OfflineDescriptorManager RTVAllocator;
	FD3D12OfflineDescriptorManager DSVAllocator;
	FD3D12OfflineDescriptorManager SRVAllocator;
	FD3D12OfflineDescriptorManager UAVAllocator;
#if USE_STATIC_ROOT_SIGNATURE
	FD3D12OfflineDescriptorManager CBVAllocator;
#endif
	FD3D12OfflineDescriptorManager SamplerAllocator;

	FD3D12GlobalOnlineSamplerHeap GlobalSamplerHeap;
	FD3D12GlobalHeap GlobalViewHeap;

	FD3D12QueryHeap OcclusionQueryHeap;
	FD3D12QueryHeap TimestampQueryHeap;
#if WITH_PROFILEGPU || D3D12_SUBMISSION_GAP_RECORDER
	FD3D12LinearQueryHeap CmdListExecTimeQueryHeap;
#endif

	FD3D12DefaultBufferAllocator DefaultBufferAllocator;

	TArray<FD3D12CommandContext*> CommandContextArray;
	TArray<FD3D12CommandContext*> FreeCommandContexts;
	FCriticalSection FreeContextsLock;

	TArray<FD3D12CommandContext*> AsyncComputeContextArray;

	TMap< D3D12_SAMPLER_DESC, TRefCountPtr<FD3D12SamplerState> > SamplerMap;
	uint32 SamplerID;

	// set by UpdateMSAASettings(), get by GetMSAAQuality()
	// [SampleCount] = Quality, 0xffffffff if not supported
	uint32 AvailableMSAAQualities[DX_MAX_MSAA_COUNT + 1];

	// set by UpdateConstantBufferPageProperties, get by GetConstantBufferPageProperties
	D3D12_HEAP_PROPERTIES ConstantBufferPageProperties;

	// shared code for different D3D12  devices (e.g. PC DirectX12 and XboxOne) called
	// after device creation and GRHISupportsAsyncTextureCreation was set and before resource init
	void SetupAfterDeviceCreation();

	// called by SetupAfterDeviceCreation() when the device gets initialized

	void UpdateMSAASettings();

	void UpdateConstantBufferPageProperties();

	void ReleasePooledUniformBuffers();

	FD3D12FastAllocator DefaultFastAllocator;

	FD3D12TextureAllocatorPool TextureAllocator;

	FD3D12ResidencyManager ResidencyManager;

#if D3D12_RHI_RAYTRACING
	FD3D12BasicRayTracingPipeline* BasicRayTracingPipeline = nullptr;
	FD3D12RayTracingPipelineCache* RayTracingPipelineCache = nullptr;
	// #dxr_todo UE-72158: unify RT descriptor cache with main FD3D12DescriptorCache
	FD3D12RayTracingDescriptorHeapCache* RayTracingDescriptorHeapCache = nullptr;
	void DestroyRayTracingDescriptorCache();
#endif

	FD3DGPUProfiler GPUProfilingData;
};
template <typename TDesc> 
void TD3D12ViewDescriptorHandle<TDesc>::AllocateDescriptorSlot()
{
	if (Parent)
	{
		FD3D12Device* Device = GetParentDevice();
		FD3D12OfflineDescriptorManager& DescriptorAllocator = Device->template GetViewDescriptorAllocator<TDesc>();
		Handle = DescriptorAllocator.AllocateHeapSlot(Index);
		check(Handle.ptr != 0);
	}
}

template <typename TDesc> 
void TD3D12ViewDescriptorHandle<TDesc>::FreeDescriptorSlot()
{
	if (Parent)
	{
		FD3D12Device* Device = GetParentDevice();
		FD3D12OfflineDescriptorManager& DescriptorAllocator = Device->template GetViewDescriptorAllocator<TDesc>();
		DescriptorAllocator.FreeHeapSlot(Handle, Index);
		Handle.ptr = 0;
	}
	check(!Handle.ptr);
}
