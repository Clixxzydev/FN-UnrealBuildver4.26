// Copyright Epic Games, Inc. All Rights Reserved.

//-----------------------------------------------------------------------------
//	Include Files
//-----------------------------------------------------------------------------
#include "D3D12RHIPrivate.h"

int32 GGlobalViewHeapBlockSize = 2000;
static FAutoConsoleVariableRef CVarGlobalViewHeapBlockSize(
	TEXT("D3D12.GlobalViewHeapBlockSize"),
	GGlobalViewHeapBlockSize,
	TEXT("Block size for sub allocations on the global view descriptor heap."),
	ECVF_ReadOnly
);

// Define template functions that are only declared in the header.
#if USE_STATIC_ROOT_SIGNATURE
template void FD3D12DescriptorCache::SetConstantBuffers<SF_Vertex>(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask, uint32 Count, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetConstantBuffers<SF_Hull>(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask, uint32 Count, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetConstantBuffers<SF_Domain>(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask, uint32 Count, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetConstantBuffers<SF_Geometry>(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask, uint32 Count, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetConstantBuffers<SF_Pixel>(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask, uint32 Count, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetConstantBuffers<SF_Compute>(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask, uint32 Count, uint32& HeapSlot);
#else
template void FD3D12DescriptorCache::SetConstantBuffers<SF_Vertex>(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask);
template void FD3D12DescriptorCache::SetConstantBuffers<SF_Hull>(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask);
template void FD3D12DescriptorCache::SetConstantBuffers<SF_Domain>(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask);
template void FD3D12DescriptorCache::SetConstantBuffers<SF_Geometry>(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask);
template void FD3D12DescriptorCache::SetConstantBuffers<SF_Pixel>(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask);
template void FD3D12DescriptorCache::SetConstantBuffers<SF_Compute>(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask);
#endif

template void FD3D12DescriptorCache::SetSRVs<SF_Vertex>(const FD3D12RootSignature* RootSignature, FD3D12ShaderResourceViewCache& Cache, const SRVSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetSRVs<SF_Hull>(const FD3D12RootSignature* RootSignature, FD3D12ShaderResourceViewCache& Cache, const SRVSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetSRVs<SF_Domain>(const FD3D12RootSignature* RootSignature, FD3D12ShaderResourceViewCache& Cache, const SRVSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetSRVs<SF_Geometry>(const FD3D12RootSignature* RootSignature, FD3D12ShaderResourceViewCache& Cache, const SRVSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetSRVs<SF_Pixel>(const FD3D12RootSignature* RootSignature, FD3D12ShaderResourceViewCache& Cache, const SRVSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetSRVs<SF_Compute>(const FD3D12RootSignature* RootSignature, FD3D12ShaderResourceViewCache& Cache, const SRVSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot);

template void FD3D12DescriptorCache::SetUAVs<SF_Pixel>(const FD3D12RootSignature* RootSignature, FD3D12UnorderedAccessViewCache& Cache, const UAVSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetUAVs<SF_Compute>(const FD3D12RootSignature* RootSignature, FD3D12UnorderedAccessViewCache& Cache, const UAVSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot);

template void FD3D12DescriptorCache::SetSamplers<SF_Vertex>(const FD3D12RootSignature* RootSignature, FD3D12SamplerStateCache& Cache, const SamplerSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetSamplers<SF_Hull>(const FD3D12RootSignature* RootSignature, FD3D12SamplerStateCache& Cache, const SamplerSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetSamplers<SF_Domain>(const FD3D12RootSignature* RootSignature, FD3D12SamplerStateCache& Cache, const SamplerSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetSamplers<SF_Geometry>(const FD3D12RootSignature* RootSignature, FD3D12SamplerStateCache& Cache, const SamplerSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetSamplers<SF_Pixel>(const FD3D12RootSignature* RootSignature, FD3D12SamplerStateCache& Cache, const SamplerSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot);
template void FD3D12DescriptorCache::SetSamplers<SF_Compute>(const FD3D12RootSignature* RootSignature, FD3D12SamplerStateCache& Cache, const SamplerSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot);

bool FD3D12DescriptorCache::HeapRolledOver(D3D12_DESCRIPTOR_HEAP_TYPE Type)
{
	// A heap rolled over, so set the descriptor heaps again and return if the heaps actually changed.
	return SetDescriptorHeaps();
}

void FD3D12DescriptorCache::HeapLoopedAround(D3D12_DESCRIPTOR_HEAP_TYPE Type)
{
	if (Type == FD3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
	{
		SamplerMap.Reset();
	}
}

FD3D12DescriptorCache::FD3D12DescriptorCache(FRHIGPUMask Node)
	: FD3D12DeviceChild(nullptr)
	, FD3D12SingleNodeGPUObject(Node)
	, pNullSRV(nullptr)
	, pNullRTV(nullptr)
	, pNullUAV(nullptr)
	, pPreviousViewHeap(nullptr)
	, pPreviousSamplerHeap(nullptr)
	, CurrentViewHeap(nullptr)
	, CurrentSamplerHeap(nullptr)
	, LocalViewHeap(nullptr)
	, LocalSamplerHeap(nullptr, Node, this)
	, SubAllocatedViewHeap(Node, this)
	, SamplerMap(271) // Prime numbers for better hashing
	, bUsingGlobalSamplerHeap(false)
	, NumLocalViewDescriptors(0)
{
	CmdContext = nullptr;
}

void FD3D12DescriptorCache::Init(FD3D12Device* InParent, FD3D12CommandContext* InCmdContext, uint32 InNumLocalViewDescriptors, uint32 InNumSamplerDescriptors)
{
	Parent = InParent;
	CmdContext = InCmdContext;

	LocalSamplerHeap.SetParentDevice(InParent);
	SubAllocatedViewHeap.Init(InParent, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Always Init a local sampler heap as the high level cache will always miss initialy
	// so we need something to fall back on (The view heap never rolls over so we init that one
	// lazily as a backup to save memory)
	LocalSamplerHeap.Init(InNumSamplerDescriptors, FD3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

	NumLocalViewDescriptors = InNumLocalViewDescriptors;

	CurrentViewHeap = &SubAllocatedViewHeap;
	CurrentSamplerHeap = &LocalSamplerHeap;
	bUsingGlobalSamplerHeap = false;

	// Create default views
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	SRVDesc.Texture2D.MipLevels = 1;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	pNullSRV = new FD3D12DescriptorHandleSRV(GetParentDevice());
	pNullSRV->CreateView(SRVDesc, nullptr);

	D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = {};
	RTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	RTVDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	RTVDesc.Texture2D.MipSlice = 0;
	pNullRTV = new FD3D12DescriptorHandleRTV(GetParentDevice());
	pNullRTV->CreateView(RTVDesc, nullptr);

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	UAVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	UAVDesc.Texture2D.MipSlice = 0;
	pNullUAV = new FD3D12DescriptorHandleUAV(GetParentDevice());
	pNullUAV->CreateViewWithCounter(UAVDesc, nullptr, nullptr);

#if USE_STATIC_ROOT_SIGNATURE
	pNullCBV = new FD3D12ConstantBufferView(GetParentDevice(), nullptr);
#endif

	const FSamplerStateInitializerRHI SamplerDesc(
		SF_Trilinear,
		AM_Clamp,
		AM_Clamp,
		AM_Clamp,
		0,
		0,
		0,
		FLT_MAX
		);

	FSamplerStateRHIRef Sampler = InParent->CreateSampler(SamplerDesc);

	pDefaultSampler = static_cast<FD3D12SamplerState*>(Sampler.GetReference());

	// The default sampler must have ID=0
	// DescriptorCache::SetSamplers relies on this
	check(pDefaultSampler->ID == 0);
}

void FD3D12DescriptorCache::Clear()
{
	delete pNullSRV; pNullSRV = nullptr;
	delete pNullUAV; pNullUAV = nullptr;
	delete pNullRTV; pNullRTV = nullptr;
#if USE_STATIC_ROOT_SIGNATURE
	delete pNullCBV; pNullCBV = nullptr;
#endif
}

void FD3D12DescriptorCache::BeginFrame()
{
	FD3D12GlobalOnlineSamplerHeap& DeviceSamplerHeap = GetParentDevice()->GetGlobalSamplerHeap();

	{
		FScopeLock Lock(&DeviceSamplerHeap.GetCriticalSection());
		if (DeviceSamplerHeap.DescriptorTablesDirty())
		{
			LocalSamplerSet = DeviceSamplerHeap.GetUniqueDescriptorTables();
		}
	}

	SwitchToGlobalSamplerHeap();
}

void FD3D12DescriptorCache::EndFrame()
{
	if (UniqueTables.Num())
	{
		GatherUniqueSamplerTables();
	}
}

void FD3D12DescriptorCache::GatherUniqueSamplerTables()
{
	FD3D12GlobalOnlineSamplerHeap& DeviceSamplerHeap = GetParentDevice()->GetGlobalSamplerHeap();

	FScopeLock Lock(&DeviceSamplerHeap.GetCriticalSection());

	auto& TableSet = DeviceSamplerHeap.GetUniqueDescriptorTables();

	for (auto& Table : UniqueTables)
	{
		if (TableSet.Contains(Table) == false)
		{
			if (DeviceSamplerHeap.CanReserveSlots(Table.Key.Count))
			{
				uint32 HeapSlot = DeviceSamplerHeap.ReserveSlots(Table.Key.Count);

				if (HeapSlot != FD3D12OnlineHeap::HeapExhaustedValue)
				{
					D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor = DeviceSamplerHeap.GetCPUSlotHandle(HeapSlot);

					GetParentDevice()->GetDevice()->CopyDescriptors(
						1, &DestDescriptor, &Table.Key.Count,
						Table.Key.Count, Table.CPUTable, nullptr /* sizes */,
						FD3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

					Table.GPUHandle = DeviceSamplerHeap.GetGPUSlotHandle(HeapSlot);
					TableSet.Add(Table);

					DeviceSamplerHeap.ToggleDescriptorTablesDirtyFlag(true);
				}
			}
		}
	}

	// Reset the tables as the next frame should inherit them from the global heap
	UniqueTables.Empty();
}

bool FD3D12DescriptorCache::SetDescriptorHeaps()
{
	// Sometimes there is no underlying command list for the context.
	// In that case, there is nothing to do and that's ok since we'll call this function again later when a command list is opened.
	if (CmdContext->CommandListHandle == nullptr)
	{
		return false;
	}

	// See if the descriptor heaps changed.
	bool bHeapChanged = false;
	ID3D12DescriptorHeap* const pCurrentViewHeap = CurrentViewHeap->GetHeap();
	if (pPreviousViewHeap != pCurrentViewHeap)
	{
		// The view heap changed, so dirty the descriptor tables.
		bHeapChanged = true;
		CmdContext->StateCache.DirtyViewDescriptorTables();

		INC_DWORD_STAT_BY(STAT_ViewHeapChanged, pPreviousViewHeap == nullptr ? 0 : 1);	// Don't count the initial set on a command list.
	}

	ID3D12DescriptorHeap* const pCurrentSamplerHeap = CurrentSamplerHeap->GetHeap();
	if (pPreviousSamplerHeap != pCurrentSamplerHeap)
	{
		// The sampler heap changed, so dirty the descriptor tables.
		bHeapChanged = true;
		CmdContext->StateCache.DirtySamplerDescriptorTables();

		// Reset the sampler map since it will have invalid entries for the new heap.
		SamplerMap.Reset();

		INC_DWORD_STAT_BY(STAT_SamplerHeapChanged, pPreviousSamplerHeap == nullptr ? 0 : 1);	// Don't count the initial set on a command list.
	}

	// Set the descriptor heaps.
	if (bHeapChanged)
	{
		ID3D12DescriptorHeap* /*const*/ ppHeaps[] = { pCurrentViewHeap, pCurrentSamplerHeap };
		CmdContext->CommandListHandle->SetDescriptorHeaps(UE_ARRAY_COUNT(ppHeaps), ppHeaps);

		pPreviousViewHeap = pCurrentViewHeap;
		pPreviousSamplerHeap = pCurrentSamplerHeap;
	}

	check(pPreviousSamplerHeap == pCurrentSamplerHeap);
	check(pPreviousViewHeap == pCurrentViewHeap);
	return bHeapChanged;
}


D3D12RHI_API void FD3D12DescriptorCache::SetCurrentCommandList(const FD3D12CommandListHandle& CommandListHandle)
{
	// Clear the previous heap pointers (since it's a new command list) and then set the current descriptor heaps.
	pPreviousViewHeap = nullptr;
	pPreviousSamplerHeap = nullptr;

	CurrentViewHeap->SetCurrentCommandList(CommandListHandle);

	// The global sampler heap doesn't care about the current command list
	LocalSamplerHeap.SetCurrentCommandList(CommandListHandle);

	// Update the descriptor heap
	SetDescriptorHeaps();
}

void FD3D12DescriptorCache::SetVertexBuffers(FD3D12VertexBufferCache& Cache)
{
	const uint32 Count = Cache.MaxBoundVertexBufferIndex + 1;
	if (Count == 0)
	{
		return; // No-op
	}

	CmdContext->CommandListHandle.UpdateResidency(Cache.ResidencyHandles, Count);
	CmdContext->CommandListHandle->IASetVertexBuffers(0, Count, Cache.CurrentVertexBufferViews);
}

template <EShaderFrequency ShaderStage>
void FD3D12DescriptorCache::SetUAVs(const FD3D12RootSignature* RootSignature, FD3D12UnorderedAccessViewCache& Cache, const UAVSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot)
{
	static_assert(ShaderStage < SF_NumStandardFrequencies, "Unexpected shader frequency.");

	UAVSlotMask& CurrentDirtySlotMask = Cache.DirtySlotMask[ShaderStage];
	check(CurrentDirtySlotMask != 0);	// All dirty slots for the current shader stage.
	check(SlotsNeededMask != 0);		// All dirty slots for the current shader stage AND used by the current shader stage.
	check(SlotsNeeded != 0);

	// Reserve heap slots
	// Note: SlotsNeeded already accounts for the UAVStartSlot. For example, if a shader has 4 UAVs starting at slot 2 then SlotsNeeded will be 6 (because the root descriptor table currently starts at slot 0).
	uint32 FirstSlotIndex = HeapSlot;
	HeapSlot += SlotsNeeded;

	CD3DX12_CPU_DESCRIPTOR_HANDLE DestDescriptor(CurrentViewHeap->GetCPUSlotHandle(FirstSlotIndex));
	CD3DX12_GPU_DESCRIPTOR_HANDLE BindDescriptor(CurrentViewHeap->GetGPUSlotHandle(FirstSlotIndex));
	CD3DX12_CPU_DESCRIPTOR_HANDLE SrcDescriptors[MAX_UAVS];

	FD3D12CommandListHandle& CommandList = CmdContext->CommandListHandle;

	const uint32 UAVStartSlot = Cache.StartSlot[ShaderStage];
	auto& UAVs = Cache.Views[ShaderStage];

	// Fill heap slots
	for (uint32 SlotIndex = 0; SlotIndex < SlotsNeeded; SlotIndex++)
	{
		if ((SlotIndex < UAVStartSlot) || (UAVs[SlotIndex] == nullptr))
		{
			SrcDescriptors[SlotIndex] = pNullUAV->GetHandle();
		}
		else
		{
			SrcDescriptors[SlotIndex] = UAVs[SlotIndex]->GetView();

			FD3D12DynamicRHI::TransitionResource(CommandList, UAVs[SlotIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			CommandList.UpdateResidency(Cache.ResidencyHandles[ShaderStage][SlotIndex]);
		}
	}
	FD3D12UnorderedAccessViewCache::CleanSlots(CurrentDirtySlotMask, SlotsNeeded);

	check((CurrentDirtySlotMask & SlotsNeededMask) == 0);	// Check all slots that needed to be set, were set.

	// Gather the descriptors from the offline heaps to the online heap
	GetParentDevice()->GetDevice()->CopyDescriptors(
		1, &DestDescriptor, &SlotsNeeded,
		SlotsNeeded, SrcDescriptors, nullptr /* sizes */,
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	if (ShaderStage == SF_Pixel)
	{
		const uint32 RDTIndex = RootSignature->UAVRDTBindSlot(ShaderStage);
		CommandList->SetGraphicsRootDescriptorTable(RDTIndex, BindDescriptor);
	}
	else
	{
		check(ShaderStage == SF_Compute);
		const uint32 RDTIndex = RootSignature->UAVRDTBindSlot(ShaderStage);
		CommandList->SetComputeRootDescriptorTable(RDTIndex, BindDescriptor);
	}

	// We changed the descriptor table, so all resources bound to slots outside of the table's range are now dirty.
	// If a shader needs to use resources bound to these slots later, we need to set the descriptor table again to ensure those
	// descriptors are valid.
	const UAVSlotMask OutsideCurrentTableRegisterMask = ~(((UAVSlotMask)1 << SlotsNeeded) - (UAVSlotMask)1);
	Cache.Dirty(ShaderStage, OutsideCurrentTableRegisterMask);

#ifdef VERBOSE_DESCRIPTOR_HEAP_DEBUG
	FMsg::Logf(__FILE__, __LINE__, TEXT("DescriptorCache"), ELogVerbosity::Log, TEXT("SetUnorderedAccessViewTable [STAGE %d] to slots %d - %d"), (int32)ShaderStage, FirstSlotIndex, FirstSlotIndex + SlotsNeeded - 1);
#endif
}

void FD3D12DescriptorCache::SetRenderTargets(FD3D12RenderTargetView** RenderTargetViewArray, uint32 Count, FD3D12DepthStencilView* DepthStencilTarget)
{
	// NOTE: For this function, setting zero render targets might not be a no-op, since this is also used
	//       sometimes for only setting a depth stencil.

	D3D12_CPU_DESCRIPTOR_HANDLE RTVDescriptors[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];

	FD3D12CommandListHandle& CommandList = CmdContext->CommandListHandle;

	// Fill heap slots
	for (uint32 i = 0; i < Count; i++)
	{
		if (RenderTargetViewArray[i] != NULL)
		{
			// RTV should already be in the correct state. It is transitioned in RHISetRenderTargets.
			FD3D12DynamicRHI::TransitionResource(CommandList, RenderTargetViewArray[i], D3D12_RESOURCE_STATE_RENDER_TARGET);
			RTVDescriptors[i] = RenderTargetViewArray[i]->GetView();

			CommandList.UpdateResidency(RenderTargetViewArray[i]->GetResource());
		}
		else
		{
			RTVDescriptors[i] = pNullRTV->GetHandle();
		}
	}

	if (DepthStencilTarget != nullptr)
	{
		FD3D12DynamicRHI::TransitionResource(CommandList, DepthStencilTarget);

		const D3D12_CPU_DESCRIPTOR_HANDLE DSVDescriptor = DepthStencilTarget->GetView();
		CommandList->OMSetRenderTargets(Count, RTVDescriptors, 0, &DSVDescriptor);
		CommandList.UpdateResidency(DepthStencilTarget->GetResource());
	}
	else
	{
		CA_SUPPRESS(6001);
		CommandList->OMSetRenderTargets(Count, RTVDescriptors, 0, nullptr);
	}
}

void FD3D12DescriptorCache::SetStreamOutTargets(FD3D12Resource** Buffers, uint32 Count, const uint32* Offsets)
{
	// Determine how many slots are really needed, since the Count passed in is a pre-defined maximum
	uint32 SlotsNeeded = 0;
	for (int32 i = Count - 1; i >= 0; i--)
	{
		if (Buffers[i] != NULL)
		{
			SlotsNeeded = i + 1;
			break;
		}
	}

	if (0 == SlotsNeeded)
	{
		return; // No-op
	}

	D3D12_STREAM_OUTPUT_BUFFER_VIEW SOViews[D3D12_SO_BUFFER_SLOT_COUNT] = { };

	FD3D12CommandListHandle& CommandList = CmdContext->CommandListHandle;

	// Fill heap slots
	for (uint32 i = 0; i < SlotsNeeded; i++)
	{
		if (Buffers[i])
		{
			CommandList.UpdateResidency(Buffers[i]);
		}

		D3D12_STREAM_OUTPUT_BUFFER_VIEW &currentView = SOViews[i];
		currentView.BufferLocation = (Buffers[i] != nullptr) ? Buffers[i]->GetGPUVirtualAddress() : 0;

		// MS - The following view members are not correct
		check(0);
		currentView.BufferFilledSizeLocation = 0;
		currentView.SizeInBytes = -1;

		if (Buffers[i] != nullptr)
		{
			FD3D12DynamicRHI::TransitionResource(CommandList, Buffers[i], D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
		}
	}

	CommandList->SOSetTargets(0, SlotsNeeded, SOViews);
}

template <EShaderFrequency ShaderStage>
void FD3D12DescriptorCache::SetSamplers(const FD3D12RootSignature* RootSignature, FD3D12SamplerStateCache& Cache, const SamplerSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot)
{
	static_assert(ShaderStage < SF_NumStandardFrequencies, "Unexpected shader frequency.");

	check(CurrentSamplerHeap != &GetParentDevice()->GetGlobalSamplerHeap());
	check(bUsingGlobalSamplerHeap == false);

	SamplerSlotMask& CurrentDirtySlotMask = Cache.DirtySlotMask[ShaderStage];
	check(CurrentDirtySlotMask != 0);	// All dirty slots for the current shader stage.
	check(SlotsNeededMask != 0);		// All dirty slots for the current shader stage AND used by the current shader stage.
	check(SlotsNeeded != 0);

	auto& Samplers = Cache.States[ShaderStage];

	D3D12_GPU_DESCRIPTOR_HANDLE BindDescriptor = { 0 };
	bool CacheHit = false;

	// Check to see if the sampler configuration is already in the sampler heap
	FD3D12SamplerArrayDesc Desc = {};
	if (SlotsNeeded <= UE_ARRAY_COUNT(Desc.SamplerID))
	{
		Desc.Count = SlotsNeeded;

		SamplerSlotMask CacheDirtySlotMask = CurrentDirtySlotMask;	// Temp mask
		for (uint32 SlotIndex = 0; SlotIndex < SlotsNeeded; SlotIndex++)
		{
			Desc.SamplerID[SlotIndex] = Samplers[SlotIndex] ? Samplers[SlotIndex]->ID : 0;
		}
		FD3D12SamplerStateCache::CleanSlots(CacheDirtySlotMask, SlotsNeeded);

		// The hash uses all of the bits
		for (uint32 SlotIndex = SlotsNeeded; SlotIndex < UE_ARRAY_COUNT(Desc.SamplerID); SlotIndex++)
		{
			Desc.SamplerID[SlotIndex] = 0;
		}

		D3D12_GPU_DESCRIPTOR_HANDLE* FoundDescriptor = SamplerMap.Find(Desc);
		if (FoundDescriptor)
		{
			check(IsHeapSet(LocalSamplerHeap.GetHeap()));
			BindDescriptor = *FoundDescriptor;
			CacheHit = true;
			CurrentDirtySlotMask = CacheDirtySlotMask;
		}
	}

	if (!CacheHit)
	{
		// Reserve heap slots
		const uint32 FirstSlotIndex = HeapSlot;
		HeapSlot += SlotsNeeded;
		D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor = CurrentSamplerHeap->GetCPUSlotHandle(FirstSlotIndex);
		BindDescriptor = CurrentSamplerHeap->GetGPUSlotHandle(FirstSlotIndex);

		checkSlow(SlotsNeeded <= MAX_SAMPLERS);

		// Fill heap slots
		CD3DX12_CPU_DESCRIPTOR_HANDLE SrcDescriptors[MAX_SAMPLERS];
		for (uint32 SlotIndex = 0; SlotIndex < SlotsNeeded; SlotIndex++)
		{
			if (Samplers[SlotIndex] != nullptr)
			{
				SrcDescriptors[SlotIndex] = Samplers[SlotIndex]->Descriptor;
			}
			else
			{
				SrcDescriptors[SlotIndex] = pDefaultSampler->Descriptor;
			}
		}
		FD3D12SamplerStateCache::CleanSlots(CurrentDirtySlotMask, SlotsNeeded);

		GetParentDevice()->GetDevice()->CopyDescriptors(
			1, &DestDescriptor, &SlotsNeeded,
			SlotsNeeded, SrcDescriptors, nullptr /* sizes */,
			FD3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

		// Remember the locations of the samplers in the sampler map
		if (SlotsNeeded <= UE_ARRAY_COUNT(Desc.SamplerID))
		{
			UniqueTables.Add(FD3D12UniqueSamplerTable(Desc, SrcDescriptors));

			SamplerMap.Add(Desc, BindDescriptor);
		}
	}

	FD3D12CommandListHandle& CommandList = CmdContext->CommandListHandle;

	if (ShaderStage == SF_Compute)
	{
		const uint32 RDTIndex = RootSignature->SamplerRDTBindSlot(ShaderStage);
		CommandList->SetComputeRootDescriptorTable(RDTIndex, BindDescriptor);
	}
	else
	{
		const uint32 RDTIndex = RootSignature->SamplerRDTBindSlot(ShaderStage);
		CommandList->SetGraphicsRootDescriptorTable(RDTIndex, BindDescriptor);
	}

	// We changed the descriptor table, so all resources bound to slots outside of the table's range are now dirty.
	// If a shader needs to use resources bound to these slots later, we need to set the descriptor table again to ensure those
	// descriptors are valid.
	const SamplerSlotMask OutsideCurrentTableRegisterMask = ~(((SamplerSlotMask)1 << SlotsNeeded) - (SamplerSlotMask)1);
	Cache.Dirty(ShaderStage, OutsideCurrentTableRegisterMask);

#ifdef VERBOSE_DESCRIPTOR_HEAP_DEBUG
	FMsg::Logf(__FILE__, __LINE__, TEXT("DescriptorCache"), ELogVerbosity::Log, TEXT("SetSamplerTable [STAGE %d] to slots %d - %d"), (int32)ShaderStage, FirstSlotIndex, FirstSlotIndex + SlotsNeeded - 1);
#endif
}

template <EShaderFrequency ShaderStage>
void FD3D12DescriptorCache::SetSRVs(const FD3D12RootSignature* RootSignature, FD3D12ShaderResourceViewCache& Cache, const SRVSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot)
{
	static_assert(ShaderStage < SF_NumStandardFrequencies, "Unexpected shader frequency.");

	SRVSlotMask& CurrentDirtySlotMask = Cache.DirtySlotMask[ShaderStage];
	check(CurrentDirtySlotMask != 0);	// All dirty slots for the current shader stage.
	check(SlotsNeededMask != 0);		// All dirty slots for the current shader stage AND used by the current shader stage.
	check(SlotsNeeded != 0);

	FD3D12CommandListHandle& CommandList = CmdContext->CommandListHandle;

	auto& SRVs = Cache.Views[ShaderStage];

	// Reserve heap slots
	uint32 FirstSlotIndex = HeapSlot;
	HeapSlot += SlotsNeeded;

	D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor = CurrentViewHeap->GetCPUSlotHandle(FirstSlotIndex);
	D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptors[MAX_SRVS];

	for (uint32 SlotIndex = 0; SlotIndex < SlotsNeeded; SlotIndex++)
	{
		if (SRVs[SlotIndex] != nullptr)
		{
			SrcDescriptors[SlotIndex] = SRVs[SlotIndex]->GetView();

			if (SRVs[SlotIndex]->IsDepthStencilResource())
			{
				FD3D12DynamicRHI::TransitionResource(CommandList, SRVs[SlotIndex], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_DEPTH_READ);
			}
			else if (SRVs[SlotIndex]->GetSkipFastClearFinalize())
			{
				FD3D12DynamicRHI::TransitionResource(CommandList, SRVs[SlotIndex], CmdContext->SkipFastClearEliminateState);
			}
			else
			{
				FD3D12DynamicRHI::TransitionResource(CommandList, SRVs[SlotIndex], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			}

			CommandList.UpdateResidency(Cache.ResidencyHandles[ShaderStage][SlotIndex]);
		}
		else
		{
			SrcDescriptors[SlotIndex] = pNullSRV->GetHandle();
		}
		check(SrcDescriptors[SlotIndex].ptr != 0);
	}
	FD3D12ShaderResourceViewCache::CleanSlots(CurrentDirtySlotMask, SlotsNeeded);

	ID3D12Device* Device = GetParentDevice()->GetDevice();
	Device->CopyDescriptors(1, &DestDescriptor, &SlotsNeeded, SlotsNeeded, SrcDescriptors, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


	check((CurrentDirtySlotMask & SlotsNeededMask) == 0);	// Check all slots that needed to be set, were set.

	const D3D12_GPU_DESCRIPTOR_HANDLE BindDescriptor = CurrentViewHeap->GetGPUSlotHandle(FirstSlotIndex);

	if (ShaderStage == SF_Compute)
	{
		const uint32 RDTIndex = RootSignature->SRVRDTBindSlot(ShaderStage);
		CommandList->SetComputeRootDescriptorTable(RDTIndex, BindDescriptor);
	}
	else
	{
		const uint32 RDTIndex = RootSignature->SRVRDTBindSlot(ShaderStage);
		CommandList->SetGraphicsRootDescriptorTable(RDTIndex, BindDescriptor);
	}

	// We changed the descriptor table, so all resources bound to slots outside of the table's range are now dirty.
	// If a shader needs to use resources bound to these slots later, we need to set the descriptor table again to ensure those
	// descriptors are valid.
	const SRVSlotMask OutsideCurrentTableRegisterMask = ~(((SRVSlotMask)1 << SlotsNeeded) - (SRVSlotMask)1);
	Cache.Dirty(ShaderStage, OutsideCurrentTableRegisterMask);

#ifdef VERBOSE_DESCRIPTOR_HEAP_DEBUG
	FMsg::Logf(__FILE__, __LINE__, TEXT("DescriptorCache"), ELogVerbosity::Log, TEXT("SetShaderResourceViewTable [STAGE %d] to slots %d - %d"), (int32)ShaderStage, FirstSlotIndex, FirstSlotIndex + SlotsNeeded - 1);
#endif
}

template <EShaderFrequency ShaderStage>
#if USE_STATIC_ROOT_SIGNATURE
void FD3D12DescriptorCache::SetConstantBuffers(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask, uint32 SlotsNeeded, uint32& HeapSlot)
#else
void FD3D12DescriptorCache::SetConstantBuffers(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask)
#endif
{
	static_assert(ShaderStage < SF_NumStandardFrequencies, "Unexpected shader frequency.");

	CBVSlotMask& CurrentDirtySlotMask = Cache.DirtySlotMask[ShaderStage];
	check(CurrentDirtySlotMask != 0);	// All dirty slots for the current shader stage.
	check(SlotsNeededMask != 0);		// All dirty slots for the current shader stage AND used by the current shader stage.

	FD3D12CommandListHandle& CommandList = CmdContext->CommandListHandle;
	ID3D12Device* Device = GetParentDevice()->GetDevice();

	// Process root CBV
	const CBVSlotMask RDCBVSlotsNeededMask = GRootCBVSlotMask & SlotsNeededMask;
	check(RDCBVSlotsNeededMask); // Check this wasn't a wasted call.

#if USE_STATIC_ROOT_SIGNATURE
	// Now desc table with CBV
	auto& CBVHandles = Cache.CBHandles[ShaderStage];

	// Reserve heap slots
	uint32 FirstSlotIndex = HeapSlot;
	check(SlotsNeeded != 0);
	HeapSlot += SlotsNeeded;

	D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor = CurrentViewHeap->GetCPUSlotHandle(FirstSlotIndex);
	const uint32 DescriptorSize = CurrentViewHeap->GetDescriptorSize();

	//Device->CopyDescriptors(1, &DestDescriptor, &DescriptorSize, SlotsNeeded, CBVHandles, SrcSizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	for (uint32 SlotIndex = 0; SlotIndex < SlotsNeeded; SlotIndex++)
	{
		if (CBVHandles[SlotIndex].ptr != 0)
		{
			Device->CopyDescriptorsSimple(1, DestDescriptor, CBVHandles[SlotIndex], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			// Update residency.
			CommandList.UpdateResidency(Cache.ResidencyHandles[ShaderStage][SlotIndex]);
		}
		else
		{
			Device->CopyDescriptorsSimple(1, DestDescriptor, pNullCBV->OfflineDescriptorHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

		DestDescriptor.ptr += DescriptorSize;

		// Clear the dirty bit.
		FD3D12ConstantBufferCache::CleanSlot(CurrentDirtySlotMask, SlotIndex);
	}

	check((CurrentDirtySlotMask & SlotsNeededMask) == 0);	// Check all slots that needed to be set, were set.

	const D3D12_GPU_DESCRIPTOR_HANDLE BindDescriptor = CurrentViewHeap->GetGPUSlotHandle(FirstSlotIndex);

	if (ShaderStage == SF_Compute)
	{
		const uint32 RDTIndex = RootSignature->CBVRDTBindSlot(ShaderStage);
		ensure(RDTIndex != 255);
		CommandList->SetComputeRootDescriptorTable(RDTIndex, BindDescriptor);
	}
	else
	{
		const uint32 RDTIndex = RootSignature->CBVRDTBindSlot(ShaderStage);
		ensure(RDTIndex != 255);
		CommandList->SetGraphicsRootDescriptorTable(RDTIndex, BindDescriptor);
	}

	// We changed the descriptor table, so all resources bound to slots outside of the table's range are now dirty.
	// If a shader needs to use resources bound to these slots later, we need to set the descriptor table again to ensure those
	// descriptors are valid.
	const CBVSlotMask OutsideCurrentTableRegisterMask = ~(((CBVSlotMask)1 << SlotsNeeded) - (CBVSlotMask)1);
	Cache.Dirty(ShaderStage, OutsideCurrentTableRegisterMask);

#ifdef VERBOSE_DESCRIPTOR_HEAP_DEBUG
	FMsg::Logf(__FILE__, __LINE__, TEXT("DescriptorCache"), ELogVerbosity::Log, TEXT("SetShaderResourceViewTable [STAGE %d] to slots %d - %d"), (int32)ShaderStage, FirstSlotIndex, FirstSlotIndex + SlotsNeeded - 1);
#endif
#else

	// Set root descriptors.
	// At least one needed root descriptor is dirty.
	const uint32 BaseIndex = RootSignature->CBVRDBaseBindSlot(ShaderStage);
	ensure(BaseIndex != 255);
	const uint32 RDCBVsNeeded = FMath::FloorLog2(RDCBVSlotsNeededMask) + 1;	// Get the index of the most significant bit that's set.
	check(RDCBVsNeeded <= MAX_ROOT_CBVS);
	for (uint32 SlotIndex = 0; SlotIndex < RDCBVsNeeded; SlotIndex++)
	{
		// Only set the root descriptor if it's dirty and we need to set it (it can be used by the shader).
		if (FD3D12ConstantBufferCache::IsSlotDirty(RDCBVSlotsNeededMask, SlotIndex))
		{
			const D3D12_GPU_VIRTUAL_ADDRESS& CurrentGPUVirtualAddress = Cache.CurrentGPUVirtualAddress[ShaderStage][SlotIndex];
			check(CurrentGPUVirtualAddress != 0);
			if (ShaderStage == SF_Compute)
			{
				CommandList->SetComputeRootConstantBufferView(BaseIndex + SlotIndex, CurrentGPUVirtualAddress);
			}
			else
			{
				CommandList->SetGraphicsRootConstantBufferView(BaseIndex + SlotIndex, CurrentGPUVirtualAddress);
			}

			// Update residency.
			CommandList.UpdateResidency(Cache.ResidencyHandles[ShaderStage][SlotIndex]);

			// Clear the dirty bit.
			FD3D12ConstantBufferCache::CleanSlot(CurrentDirtySlotMask, SlotIndex);
		}
	}
	check((CurrentDirtySlotMask & RDCBVSlotsNeededMask) == 0);	// Check all slots that needed to be set, were set.

	static_assert(GDescriptorTableCBVSlotMask == 0, "FD3D12DescriptorCache::SetConstantBuffers needs to be updated to handle descriptor tables.");	// Check that all CBVs slots are controlled by root descriptors.
#endif	
}

bool FD3D12DescriptorCache::SwitchToContextLocalViewHeap(const FD3D12CommandListHandle& CommandListHandle)
{
	if (LocalViewHeap == nullptr)
	{
		UE_LOG(LogD3D12RHI, Log, TEXT("This should only happen in the Editor where it doesn't matter as much. If it happens in game you should increase the device global heap size!"));
		
		// Allocate the heap lazily
		LocalViewHeap = new FD3D12LocalOnlineHeap(GetParentDevice(), GetGPUMask(), this);
		if (LocalViewHeap)
		{
			check(NumLocalViewDescriptors);
			LocalViewHeap->Init(NumLocalViewDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		else
		{
			check(false);
			return false;
		}
	}

	LocalViewHeap->SetCurrentCommandList(CommandListHandle);
	CurrentViewHeap = LocalViewHeap;
	const bool bDescriptorHeapsChanged = SetDescriptorHeaps();

	check(IsHeapSet(LocalViewHeap->GetHeap()));
	return bDescriptorHeapsChanged;
}

bool FD3D12DescriptorCache::SwitchToContextLocalSamplerHeap()
{
	bool bDescriptorHeapsChanged = false;
	if (UsingGlobalSamplerHeap())
	{
		bUsingGlobalSamplerHeap = false;
		CurrentSamplerHeap = &LocalSamplerHeap;
		bDescriptorHeapsChanged = SetDescriptorHeaps();
	}

	check(IsHeapSet(LocalSamplerHeap.GetHeap()));
	return bDescriptorHeapsChanged;
}

bool FD3D12DescriptorCache::SwitchToGlobalSamplerHeap()
{
	bool bDescriptorHeapsChanged = false;
	if (!UsingGlobalSamplerHeap())
	{
		bUsingGlobalSamplerHeap = true;
		CurrentSamplerHeap = &GetParentDevice()->GetGlobalSamplerHeap();
		bDescriptorHeapsChanged = SetDescriptorHeaps();
	}

	// Sometimes this is called when there is no underlying command list.
	// This is OK, as the desriptor heaps will be set when a command list is opened.
	check((CmdContext->CommandListHandle == nullptr) || IsHeapSet(GetParentDevice()->GetGlobalSamplerHeap().GetHeap()));
	return bDescriptorHeapsChanged;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FD3D12OnlineHeap
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
Initialization constructor
**/
FD3D12OnlineHeap::FD3D12OnlineHeap(FD3D12Device* Device, FRHIGPUMask Node, bool CanLoopAround)
	: FD3D12DeviceChild(Device)
	, FD3D12SingleNodeGPUObject(Node)
	, DescriptorSize(0)
	, bCanLoopAround(CanLoopAround)
	, NextSlotIndex(0)
	, FirstUsedSlot(0)
	, Desc({})
{};


/**
Check if requested number of slots still fit the heap
**/
bool FD3D12OnlineHeap::CanReserveSlots(uint32 NumSlots)
{
	const uint32 HeapSize = GetTotalSize();

	// Sanity checks
	if (0 == NumSlots)
	{
		return true;
	}
	if (NumSlots > HeapSize)
	{
#if !defined(_HAS_EXCEPTIONS) || _HAS_EXCEPTIONS == 1
		throw E_OUTOFMEMORY;
#else
		UE_LOG(LogD3D12RHI, Fatal, TEXT("Unable to reserve slot"));
#endif
	}
	uint32 FirstRequestedSlot = NextSlotIndex;
	uint32 SlotAfterReservation = NextSlotIndex + NumSlots;

	// TEMP: Disable wrap around by not allowing it to reserve slots if the heap is full.
	if (SlotAfterReservation > HeapSize)
	{
		return false;
	}

	return true;

	// TEMP: Uncomment this code once the heap wrap around is fixed.
	//if (SlotAfterReservation <= HeapSize)
	//{
	//	return true;
	//}

	//// Try looping around to prevent rollovers
	//SlotAfterReservation = NumSlots;

	//if (SlotAfterReservation <= FirstUsedSlot)
	//{
	//	return true;
	//}

	//return false;
}


/**
Reserve requested amount of descriptor slots - should fit, user has to check with CanReserveSlots first
**/
uint32 FD3D12OnlineHeap::ReserveSlots(uint32 NumSlotsRequested)
{
#ifdef VERBOSE_DESCRIPTOR_HEAP_DEBUG
	FMsg::Logf(__FILE__, __LINE__, TEXT("DescriptorCache"), ELogVerbosity::Log, TEXT("Requesting reservation [TYPE %d] with %d slots, required fence is %d"),
		(int32)Desc.Type, NumSlotsRequested, RequiredFenceForCurrentCL);
#endif

	const uint32 HeapSize = GetTotalSize();

	// Sanity checks
	if (NumSlotsRequested > HeapSize)
	{
#if !defined(_HAS_EXCEPTIONS) || _HAS_EXCEPTIONS == 1
		throw E_OUTOFMEMORY;
#else
		return HeapExhaustedValue;
#endif
	}

	// CanReserveSlots should have been called first
	check(CanReserveSlots(NumSlotsRequested));

	// Decide which slots will be reserved and what needs to be cleaned up
	uint32 FirstRequestedSlot = NextSlotIndex;
	uint32 SlotAfterReservation = NextSlotIndex + NumSlotsRequested;

	// Loop around if the end of the heap has been reached
	if (bCanLoopAround && SlotAfterReservation > HeapSize)
	{
		FirstRequestedSlot = 0;
		SlotAfterReservation = NumSlotsRequested;

		FirstUsedSlot = SlotAfterReservation;

		// Notify the derived class that the heap has been looped around
		HeapLoopedAround();
	}

	// Note where to start looking next time
	NextSlotIndex = SlotAfterReservation;

	if (Desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
	{
		INC_DWORD_STAT_BY(STAT_NumReservedViewOnlineDescriptors, NumSlotsRequested);
	}
	else
	{
		INC_DWORD_STAT_BY(STAT_NumReservedSamplerOnlineDescriptors, NumSlotsRequested);
	}

	return FirstRequestedSlot;
}


/**
Increment the internal slot counter - only used by threadlocal sampler heap
**/
void FD3D12OnlineHeap::SetNextSlot(uint32 NextSlot)
{
	// For samplers, ReserveSlots will be called with a conservative estimate
	// This is used to correct for the actual number of heap slots used
	check(NextSlot <= NextSlotIndex);

	check(Desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	DEC_DWORD_STAT_BY(STAT_NumReservedSamplerOnlineDescriptors, NextSlotIndex - NextSlot);

	NextSlotIndex = NextSlot;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FD3D12GlobalSamplerOnlineHeap
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
Allocate and initialize the global sampler heap
**/
void FD3D12GlobalOnlineSamplerHeap::Init(uint32 TotalSize)
{
	D3D12_DESCRIPTOR_HEAP_FLAGS HeapFlags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	Desc = {};
	Desc.Flags = HeapFlags;
	Desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	Desc.NumDescriptors = TotalSize;
	Desc.NodeMask = GetGPUMask().GetNative();

	VERIFYD3D12RESULT(GetParentDevice()->GetDevice()->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(Heap.GetInitReference())));
	SetName(Heap, L"Device Global - Online Sampler Heap");

	CPUBase = Heap->GetCPUDescriptorHandleForHeapStart();
	GPUBase = Heap->GetGPUDescriptorHandleForHeapStart();
	DescriptorSize = GetParentDevice()->GetDevice()->GetDescriptorHandleIncrementSize(Desc.Type);

	INC_DWORD_STAT(STAT_NumSamplerOnlineDescriptorHeaps);
	INC_MEMORY_STAT_BY(STAT_SamplerOnlineDescriptorHeapMemory, Desc.NumDescriptors * GetDescriptorSize());
}


/**
No rollover supported
**/
bool FD3D12GlobalOnlineSamplerHeap::RollOver()
{
	check(false);
	UE_LOG(LogD3D12RHI, Fatal, TEXT("Global Descriptor heaps can't roll over!"));
	return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FD3D12GlobalHeap
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
@brief Allocate and initialize the global heap
**/
void FD3D12GlobalHeap::Init(D3D12_DESCRIPTOR_HEAP_TYPE InType, uint32 InTotalSize)
{
	Type = InType;
	TotalSize = InTotalSize;

	// Setup the descriptor
	D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
	Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	Desc.Type = InType;
	Desc.NumDescriptors = TotalSize;
	Desc.NodeMask = GetGPUMask().GetNative();

	// Allocate the heap and name it
	VERIFYD3D12RESULT(GetParentDevice()->GetDevice()->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(Heap.GetInitReference())));
	SetName(Heap, Desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? L"Device Global - Online View Heap" : L"Device Global - Online Sampler Heap");

	// Extract useful data from created heap
	CPUBase = Heap->GetCPUDescriptorHandleForHeapStart();
	GPUBase = Heap->GetGPUDescriptorHandleForHeapStart();
	DescriptorSize = GetParentDevice()->GetDevice()->GetDescriptorHandleIncrementSize(Desc.Type);

	// Update the stats
	if (Desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
	{
		INC_DWORD_STAT(STAT_NumViewOnlineDescriptorHeaps);
		INC_MEMORY_STAT_BY(STAT_ViewOnlineDescriptorHeapMemory, Desc.NumDescriptors * DescriptorSize);
	}
	else
	{
		INC_DWORD_STAT(STAT_NumSamplerOnlineDescriptorHeaps);
		INC_MEMORY_STAT_BY(STAT_SamplerOnlineDescriptorHeapMemory, Desc.NumDescriptors * DescriptorSize);
	}

	INC_DWORD_STAT_BY(STAT_GlobalViewHeapFreeDescriptors, TotalSize);
	
	// Compute amount of free blocks
	uint32 BlockSize = GGlobalViewHeapBlockSize;
	uint32 BlockCount = TotalSize / BlockSize;
	ReleasedBlocks.Reserve(BlockCount);

	// Allocate the free blocks
	uint32 CurrentBaseSlot = 0;
	for (uint32 BlockIndex = 0; BlockIndex < BlockCount; ++BlockIndex)
	{
		// Last entry take the rest
		uint32 ActualBlockSize = (BlockIndex == (BlockCount - 1)) ? TotalSize - CurrentBaseSlot : BlockSize;
		FreeBlocks.Enqueue(new FD3D12GlobalHeapBlock(CurrentBaseSlot, ActualBlockSize));
		CurrentBaseSlot += ActualBlockSize;
	}
}


/**
Allocate a new heap block - will also check if released blocks can be freed again
**/
FD3D12GlobalHeapBlock* FD3D12GlobalHeap::AllocateHeapBlock()
{
	SCOPED_NAMED_EVENT(FD3D12GlobalHeap_AllocateHeapBlock, FColor::Silver);

	FScopeLock Lock(&CriticalSection);

	// Check if certain released blocks are free again
	UpdateFreeBlocks();

	// Free block
	FD3D12GlobalHeapBlock* Result = nullptr;
	FreeBlocks.Dequeue(Result);

	if (Result)
	{
		// Update stats
		INC_DWORD_STAT(STAT_GlobalViewHeapBlockAllocations);
		DEC_DWORD_STAT_BY(STAT_GlobalViewHeapFreeDescriptors, Result->Size);
		INC_DWORD_STAT_BY(STAT_GlobalViewHeapReservedDescriptors, Result->Size);
	}

	return Result;
}


/**
Free given block - can still be used by the GPU (SyncPoint needs to be setup by the caller and will be used to check if the block can be reused again)
**/
void FD3D12GlobalHeap::FreeHeapBlock(FD3D12GlobalHeapBlock* InHeapBlock)
{
	FScopeLock Lock(&CriticalSection);

	// Update stats
	DEC_DWORD_STAT_BY(STAT_GlobalViewHeapReservedDescriptors, InHeapBlock->Size);
	INC_DWORD_STAT_BY(STAT_GlobalViewHeapUsedDescriptors, InHeapBlock->SizeUsed);
	INC_DWORD_STAT_BY(STAT_GlobalViewHeapWastedDescriptors, InHeapBlock->Size - InHeapBlock->SizeUsed);

	ReleasedBlocks.Add(InHeapBlock);
}


/**
Find all the blocks which are not used by the GPU anymore
**/
void FD3D12GlobalHeap::UpdateFreeBlocks()
{
	for (int32 BlockIndex = 0; BlockIndex < ReleasedBlocks.Num(); ++BlockIndex)
	{
		// Check if GPU is ready consuming the block data
		FD3D12GlobalHeapBlock* ReleasedBlock = ReleasedBlocks[BlockIndex];
		if (ReleasedBlock->SyncPoint.IsComplete())
		{
			// Update stats
			DEC_DWORD_STAT_BY(STAT_GlobalViewHeapUsedDescriptors, ReleasedBlock->SizeUsed);
			DEC_DWORD_STAT_BY(STAT_GlobalViewHeapWastedDescriptors, ReleasedBlock->Size - ReleasedBlock->SizeUsed);
			INC_DWORD_STAT_BY(STAT_GlobalViewHeapFreeDescriptors, ReleasedBlock->Size);

			ReleasedBlock->SizeUsed = 0;
			FreeBlocks.Enqueue(ReleasedBlock);

			// don't want to resize, but optional parameter is missing
			ReleasedBlocks.RemoveAtSwap(BlockIndex);
			BlockIndex--;
		}
	}

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FD3D12SubAllocatedOnlineHeap
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
Initialize the sub allocated online heap
**/
void FD3D12SubAllocatedOnlineHeap::Init(FD3D12Device* InDevice, D3D12_DESCRIPTOR_HEAP_TYPE InHeapType)
{
	SetParentDevice(InDevice);
	HeapType = InHeapType;
}


/**
Handle roll over on the sub allocated online heap - needs a new block
**/
bool FD3D12SubAllocatedOnlineHeap::RollOver()
{
	// Try and allocate a new block from the global heap
	AllocateBlock();

	// Sub-allocated descriptor heaps don't change, so no need to set descriptor heaps if we still have a block allocated
	return CurrentBlock == nullptr;
}


/**
Set the current command list which needs to be notified about changes
**/
void FD3D12SubAllocatedOnlineHeap::SetCurrentCommandList(const FD3D12CommandListHandle& CommandListHandle)
{
	// Update the current command list
	CurrentCommandList = CommandListHandle;

	// Allocate a new block if we don't have one yet
	if (CurrentBlock == nullptr)
	{
		AllocateBlock();
	}
}


/**
Tries to allocate a new block from the global heap - if it fails then it will switch to thread local view heap
**/
bool FD3D12SubAllocatedOnlineHeap::AllocateBlock()
{
	FD3D12GlobalHeap& GlobalHeap = GetParentDevice()->GetGlobalViewHeap();

	// If we still have a block, then free it first
	if (CurrentBlock)
	{
		// Update actual used size
		check(FirstUsedSlot == 0);
		CurrentBlock->SizeUsed = NextSlotIndex;

		// Create the sync point on the current command list
		CurrentBlock->SyncPoint = FD3D12CLSyncPoint(CurrentCommandList);

		GlobalHeap.FreeHeapBlock(CurrentBlock);		
		CurrentBlock = nullptr;
	}

	// Try and allocate from the global heap
	CurrentBlock = GlobalHeap.AllocateHeapBlock();

	// Reset counters
	NextSlotIndex = 0;
	FirstUsedSlot = 0;
	Heap.SafeRelease();

	// Extract global heap data
	if (CurrentBlock)
	{
		DescriptorSize = GlobalHeap.GetDescriptorSize();
		CPUBase = GlobalHeap.GetCPUSlotHandle(CurrentBlock);
		GPUBase = GlobalHeap.GetGPUSlotHandle(CurrentBlock);
		Heap = GlobalHeap.GetHeap();
		Desc = Heap->GetDesc();
	}
	else
	{
		// Notify parent that we have run out of sub allocations
		// This should *never* happen but we will handle it and revert to local heaps to be safe
		UE_LOG(LogD3D12RHI, Warning, TEXT("Descriptor cache ran out of sub allocated descriptor blocks! Moving to Context local View heap strategy"));
		DescriptorCache->SwitchToContextLocalViewHeap(CurrentCommandList);
	}

	// Allocation succeeded?
	return (CurrentBlock != nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FD3D12LocalOnlineHeap
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
Initialize a thread local online heap
**/
void FD3D12LocalOnlineHeap::Init(uint32 NumDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE Type)
{
	Desc = {};
	Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	Desc.Type = Type;
	Desc.NumDescriptors = NumDescriptors;
	Desc.NodeMask = GetGPUMask().GetNative();

	//LLM_SCOPE(ELLMTag::DescriptorCache);
	VERIFYD3D12RESULT(GetParentDevice()->GetDevice()->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(Heap.GetInitReference())));
	SetName(Heap, Desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? L"Thread Local - Online View Heap" : L"Thread Local - Online Sampler Heap");

	Entry.Heap = Heap;

	CPUBase = Heap->GetCPUDescriptorHandleForHeapStart();
	GPUBase = Heap->GetGPUDescriptorHandleForHeapStart();
	DescriptorSize = GetParentDevice()->GetDevice()->GetDescriptorHandleIncrementSize(Type);

	if (Desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
	{
		INC_DWORD_STAT(STAT_NumViewOnlineDescriptorHeaps);
		INC_MEMORY_STAT_BY(STAT_ViewOnlineDescriptorHeapMemory, Desc.NumDescriptors * GetDescriptorSize());
	}
	else
	{
		INC_DWORD_STAT(STAT_NumSamplerOnlineDescriptorHeaps);
		INC_MEMORY_STAT_BY(STAT_SamplerOnlineDescriptorHeapMemory, Desc.NumDescriptors * GetDescriptorSize());
	}
}


/**
Handle roll over
**/
bool FD3D12LocalOnlineHeap::RollOver()
{
	// Enqueue the current entry
	ensureMsgf(CurrentCommandList != nullptr, TEXT("Would have set up a sync point with a null commandlist."));
	Entry.SyncPoint = CurrentCommandList;
	ReclaimPool.Enqueue(Entry);

	if (ReclaimPool.Peek(Entry) && Entry.SyncPoint.IsComplete())
	{
		ReclaimPool.Dequeue(Entry);

		Heap = Entry.Heap;
	}
	else
	{
		UE_LOG(LogD3D12RHI, Log, TEXT("OnlineHeap RollOver Detected. Increase the heap size to prevent creation of additional heaps"));

		//LLM_SCOPE(ELLMTag::DescriptorCache);

		VERIFYD3D12RESULT(GetParentDevice()->GetDevice()->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(Heap.GetInitReference())));
		SetName(Heap, Desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? L"Thread Local - Online View Heap" : L"Thread Local - Online Sampler Heap");

		if (Desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		{
			INC_DWORD_STAT(STAT_NumViewOnlineDescriptorHeaps);
			INC_MEMORY_STAT_BY(STAT_ViewOnlineDescriptorHeapMemory, Desc.NumDescriptors * GetDescriptorSize());
		}
		else
		{
			INC_DWORD_STAT(STAT_NumSamplerOnlineDescriptorHeaps);
			INC_MEMORY_STAT_BY(STAT_SamplerOnlineDescriptorHeapMemory, Desc.NumDescriptors * GetDescriptorSize());
		}

		Entry.Heap = Heap;
	}

	NextSlotIndex = 0;
	FirstUsedSlot = 0;

	// Notify other layers of heap change
	CPUBase = Heap->GetCPUDescriptorHandleForHeapStart();
	GPUBase = Heap->GetGPUDescriptorHandleForHeapStart();
	return DescriptorCache->HeapRolledOver(Desc.Type);
}


/**
Handle loop around on the heap
**/
void FD3D12LocalOnlineHeap::HeapLoopedAround()
{
	DescriptorCache->HeapLoopedAround(Desc.Type);
}


/**
Update the command list which should be notified about changes
**/
void FD3D12LocalOnlineHeap::SetCurrentCommandList(const FD3D12CommandListHandle& CommandListHandle)
{
	if (CurrentCommandList != nullptr && NextSlotIndex > 0)
	{
		// Track the previous command list
		SyncPointEntry SyncPoint;
		SyncPoint.SyncPoint = CurrentCommandList;
		SyncPoint.LastSlotInUse = NextSlotIndex - 1;
		SyncPoints.Enqueue(SyncPoint);

		Entry.SyncPoint = CurrentCommandList;

		// Free up slots for finished command lists
		while (SyncPoints.Peek(SyncPoint) && SyncPoint.SyncPoint.IsComplete())
		{
			SyncPoints.Dequeue(SyncPoint);
			FirstUsedSlot = SyncPoint.LastSlotInUse + 1;
		}
	}

	// Update the current command list
	CurrentCommandList = CommandListHandle;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Util
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32 GetTypeHash(const FD3D12SamplerArrayDesc& Key)
{
	return FD3D12PipelineStateCache::HashData((void*)Key.SamplerID, Key.Count * sizeof(Key.SamplerID[0]));
}

uint32 GetTypeHash(const FD3D12QuantizedBoundShaderState& Key)
{
	return FD3D12PipelineStateCache::HashData((void*)&Key, sizeof(Key));
}
