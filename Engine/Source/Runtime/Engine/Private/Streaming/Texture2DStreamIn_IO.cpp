// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Texture2DStreamIn.cpp: Stream in helper for 2D textures using texture streaming files.
=============================================================================*/

#include "Streaming/Texture2DStreamIn_IO.h"
#include "RenderUtils.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "DerivedDataCacheInterface.h"
#include "Serialization/MemoryReader.h"
#include "Streaming/TextureStreamingHelpers.h"
#include "ContentStreaming.h"


FTexture2DStreamIn_IO::FTexture2DStreamIn_IO(UTexture2D* InTexture, int32 InRequestedMips, bool InPrioritizedIORequest)
	: FTexture2DStreamIn(InTexture, InRequestedMips)
	, bPrioritizedIORequest(InPrioritizedIORequest)

{
	IORequests.AddZeroed(InTexture->GetNumMips());
}

FTexture2DStreamIn_IO::~FTexture2DStreamIn_IO()
{
#if DO_CHECK
	for (const IBulkDataIORequest* IORequest : IORequests)
	{
		check(!IORequest);
	}
#endif
}

void FTexture2DStreamIn_IO::SetIORequests(const FContext& Context)
{
	SetAsyncFileCallback();
	
	const TIndirectArray<FTexture2DMipMap>& OwnerMips = Context.Texture->GetPlatformMips();
	const int32 CurrentFirstMip = Context.Resource->GetCurrentFirstMip();

	for (int32 MipIndex = PendingFirstMip; MipIndex < CurrentFirstMip && !IsCancelled(); ++MipIndex)
	{
		const FTexture2DMipMap& MipMap = OwnerMips[MipIndex];
		check(MipData[MipIndex]);

		const int64 BulkDataSize = MipMap.BulkData.GetBulkDataSize();
		if (BulkDataSize > 0)
		{
			// Increment as we push the requests. If a requests complete immediately, then it will call the callback
			// but that won't do anything because the tick would not try to acquire the lock since it is already locked.
			TaskSynchronization.Increment();

			IORequests[MipIndex] = MipMap.BulkData.CreateStreamingRequest(
				0,
				BulkDataSize,
				bPrioritizedIORequest ? (AIOP_FLAG_DONTCACHE|AIOP_BelowNormal) : (AIOP_FLAG_DONTCACHE|AIOP_Low),
				&AsyncFileCallBack,
				(uint8*)MipData[MipIndex]);
		}
		else // Bulk data size can only be 0 when not available, in which case, we need to recache the file state.
		{
			bFailedOnIOError = true;
			MarkAsCancelled();
			break;
		}
	}
}

void FTexture2DStreamIn_IO::CancelIORequests()
{
	for (int32 MipIndex = 0; MipIndex < IORequests.Num(); ++MipIndex)
	{
		IBulkDataIORequest* IORequest = IORequests[MipIndex];
		if (IORequest)
		{
			// Calling cancel will trigger the SetAsyncFileCallback() which will also try a tick but will fail.
			IORequest->Cancel();
		}
	}
}

void FTexture2DStreamIn_IO::ClearIORequests(const FContext& Context)
{
	const TIndirectArray<FTexture2DMipMap>& OwnerMips = Context.Texture->GetPlatformMips();
	const int32 CurrentFirstMip = Context.Resource->GetCurrentFirstMip();

	for (int32 MipIndex = PendingFirstMip; MipIndex < CurrentFirstMip; ++MipIndex)
	{
		IBulkDataIORequest* IORequest = IORequests[MipIndex];
		IORequests[MipIndex] = nullptr;

		if (IORequest != nullptr)
		{
			// If clearing requests not yet completed, cancel and wait.
			if (!IORequest->PollCompletion())
			{
				IORequest->Cancel();
				IORequest->WaitCompletion();
			}
			delete IORequest;
		}
	}
}

void FTexture2DStreamIn_IO::ReportIOError(const FContext& Context)
{
	// Invalidate the cache state of all initial mips (note that when using FIoChunkId each mip has a different value).
	if (bFailedOnIOError && Context.Texture)
	{
		IRenderAssetStreamingManager& StreamingManager = IStreamingManager::Get().GetTextureStreamingManager();
		const int32 CurrentFirstMip = Context.Resource->GetCurrentFirstMip();
		for (int32 MipIndex = 0; MipIndex < CurrentFirstMip; ++MipIndex)
		{
			StreamingManager.MarkMountedStateDirty(Context.Texture->GetMipIoFilenameHash(MipIndex));
		}

		UE_LOG(LogContentStreaming, Warning, TEXT("[%s] Stream in request failed due to IO error."), *Context.Texture->GetName());
	}
}

void FTexture2DStreamIn_IO::SetAsyncFileCallback()
{
	AsyncFileCallBack = [this](bool bWasCancelled, IBulkDataIORequest*)
	{
		// At this point task synchronization would hold the number of pending requests.
		TaskSynchronization.Decrement();
		
		if (bWasCancelled)
		{
			// If IO requests was cancelled but the streaming request wasn't, this is an IO error.
			if (!bIsCancelled)
			{
				bFailedOnIOError = true;
			}

			MarkAsCancelled();
		}

#if !UE_BUILD_SHIPPING
		// On some platforms the IO is too fast to test cancelation requests timing issues.
		if (FRenderAssetStreamingSettings::ExtraIOLatency > 0 && TaskSynchronization.GetValue() == 0)
		{
			FPlatformProcess::Sleep(FRenderAssetStreamingSettings::ExtraIOLatency * .001f); // Slow down the streaming.
		}
#endif

		// The tick here is intended to schedule the success or cancel callback.
		// Using TT_None ensure gets which could create a dead lock.
		Tick(FTexture2DUpdate::TT_None);
	};
}

void FTexture2DStreamIn_IO::Abort()
{
	if (!IsCancelled() && !IsCompleted())
	{
		FTexture2DStreamIn::Abort();

		if (HasPendingIORequests())
		{
			// Prevent the update from being considered done before this is finished.
			// By checking that it was not already canceled, we make sure this doesn't get called twice.
			(new FAsyncCancelIORequestsTask(this))->StartBackgroundTask();
		}
	}
}

bool FTexture2DStreamIn_IO::HasPendingIORequests()
{
	for (IBulkDataIORequest* IORequest : IORequests)
	{
		if (IORequest != nullptr)
		{
			return true;
		}
	}

	return false;
}

void FTexture2DStreamIn_IO::FCancelIORequestsTask::DoWork()
{
	check(PendingUpdate);
	// Acquire the lock of this object in order to cancel any pending IO.
	// If the object is currently being ticked, wait.
	const ETaskState PreviousTaskState = PendingUpdate->DoLock();
	PendingUpdate->CancelIORequests();
	PendingUpdate->DoUnlock(PreviousTaskState);
}
