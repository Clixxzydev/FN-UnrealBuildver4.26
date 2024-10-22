// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/UnrealString.h"
#include "CookTypes.h"
#include "HAL/CriticalSection.h"
#include "HAL/Platform.h"
#include "RingBuffer.h"
#include "UObject/NameTypes.h"

class FEvent;
class ITargetPlatform;

namespace UE
{
namespace Cook
{

	/**
	 * Structure holding the data for a request for the CookOnTheFlyServer to cook a FileName. Includes platform which file is requested for.
	 * These requests are external to the cooker's scheduler, and do not use the FPackageData the scheduler uses internally.
	 */
	struct FFilePlatformRequest
	{
	protected:
		FName Filename;
		TArray<const ITargetPlatform*> Platforms;
		FCompletionCallback CompletionCallback;

	public:
		FFilePlatformRequest() = default;

		FFilePlatformRequest(const FName& InFilename);
		FFilePlatformRequest(const FName& InFilename, const ITargetPlatform* InPlatform, FCompletionCallback&& InCompletionCallback = FCompletionCallback());
		FFilePlatformRequest(const FName& InFilename, const TArrayView<const ITargetPlatform* const>& InPlatforms, FCompletionCallback&& InCompletionCallback = FCompletionCallback());
		FFilePlatformRequest(const FName& InFilename, TArray<const ITargetPlatform*>&& InPlatforms, FCompletionCallback&& InCompletionCallback = FCompletionCallback());
		FFilePlatformRequest(const FFilePlatformRequest& InFilePlatformRequest);
		FFilePlatformRequest(FFilePlatformRequest&& InFilePlatformRequest);
		FFilePlatformRequest& operator=(FFilePlatformRequest&& InFileRequest);

		void SetFilename(FString InFilename);
		const FName& GetFilename() const;

		const TArray<const ITargetPlatform*>& GetPlatforms() const;
		TArray<const ITargetPlatform*>& GetPlatforms();
		void RemovePlatform(const ITargetPlatform* Platform);
		void AddPlatform(const ITargetPlatform* Platform);
		bool HasPlatform(const ITargetPlatform* Platform) const;

		/** A callback that the scheduler will call after the request is processed and is cooked, fails to cook, is canceled, or is skipped because it already exists. */
		FCompletionCallback& GetCompletionCallback();

		bool IsValid() const;
		void Clear();
		bool operator==(const FFilePlatformRequest& InFileRequest) const;
		FString ToString() const;
	};


	/**
	 * A container class for External Requests made to the cooker.
	 * External Requests are cook requests that are made outside of the scheduler's lock and hence need to be separately synchronized.
	 * External Requests can be either a request to cook a given FileName (packages are identified by FileName in this container) on given platforms,
	 * or a request to run an arbitrary callback inside the scheduler's lock.
	 * This class is threadsafe; all methods are guarded by a CriticalSection. 
	 */
	class FExternalRequests
	{
	public:

		FExternalRequests(FCriticalSection& InRequestLock);

		/**
		 * Lockless value for the number of External Requests in the container.
		 * May be out of date after calling; do not assume the number of actual requests is any one of equal, greater than, or less than the returned value.
		 * Intended usage is for the Scheduler to be the only consumer of requests, and to use this value for rough reporting of periodic progress.
		 */
		int32 GetNumRequests() const;

		/**
		 * Lockless value for the number of External Requests in the container.
		 * May be out of date after calling; do not assume a true return value means Requests are actually present or a false value means no Requests are present.
		 * Intended usage is for the Scheduler to be the only consumer of requests, and to use this value for periodic checking of whether there is any work that justifies the expense of taking the lock.
		 * In a single-consumer case, HasRequests will eventually correctly return true as long as the consumer is not consuming.
		 */
		bool HasRequests() const;

		/** Add a callback-type request. The scheduler will run all callbacks (in FIFO order) as soon as it completes its current task. */
		void AddCallback(FSchedulerCallback&& Callback);

		/** Add the given cook-type request, merging its list of platforms with any existing request if one already exists. */
		void EnqueueUnique(FFilePlatformRequest&& FileRequest, bool bForceFrontOfQueue = false);
		/** Unsynchronized version of the EnqueueUnique function, used by CookOnTheFlyServer for batched calls to enqueue, done within the RequestLock. */
		void ThreadUnsafeEnqueueUnique(FFilePlatformRequest&& FileRequest, bool bForceFrontOfQueue = false);

		/**
		 * If this FExternalRequests has any callbacks, dequeue them all into OutCallbacks and return EExternalRequestType::Callback; Callbacks take priority over cook requests.
		 * Otherwise, if there are any cook requests, set OutToBuild to the front request and return EExternalRequestType::Cook.
		 * Otherwise, return EExternalRequestType::None.
		 */
		EExternalRequestType DequeueRequest(TArray<FSchedulerCallback>& OutCallbacks, FFilePlatformRequest& OutToBuild);
		/* Move any existing callbacks onto OutCallbacks, and return whether any were added. */
		bool DequeueCallbacks(TArray<FSchedulerCallback>& OutCallbacks);
		/** Eliminate all callbacks and cook requests and free memory */
		void EmptyRequests();
		/** Move all callbacks into OutCallbacks, and all cook requests into OutCookRequests. This is used when cancelling a cook session. */
		void DequeueAll(TArray<FSchedulerCallback>& OutCallbacks, TArray<FFilePlatformRequest>& OutCookRequests);

		/** Remove references to the given platform from all cook requests. */
		void OnRemoveSessionPlatform(const ITargetPlatform* TargetPlatform);

		/** Return the CriticalSection used to guard access to the data in this FExternalRequests. This is used for batched calls to methods. */
		FCriticalSection& GetRequestLock();

	public:
		/** An FEvent the scheduler can sleep on when waiting for new cookonthefly requests. */
		FEvent* CookRequestEvent = nullptr;
	private:
		/* Implementation for DequeueCallbacks that assumes the caller has entered the RequestLock. */
		bool ThreadUnsafeDequeueCallbacks(TArray<FSchedulerCallback>& OutCallbacks);

		/** Queue of the FileName for the cook-type requests in this instance. The FileName can be used to look up the rest of the data for the request. */
		TRingBuffer<FName> Queue;
		/** Map of the extended information for the cook-type requests in this instance, keyed by the FileName of the request. */
		TMap<FName, FFilePlatformRequest> RequestMap;
		TArray<FSchedulerCallback> Callbacks;
		FCriticalSection& RequestLock;
		int32 RequestCount = 0;
	};
}
}