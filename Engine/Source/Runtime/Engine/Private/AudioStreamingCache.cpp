// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
AudioStreaming.cpp: Implementation of audio streaming classes.
=============================================================================*/

#include "AudioStreamingCache.h"
#include "Misc/CoreStats.h"
#include "Sound/AudioSettings.h"
#include "DerivedDataCacheInterface.h"
#include "Serialization/MemoryReader.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "Async/AsyncFileHandle.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "HAL/IConsoleManager.h"
#include "HAL/LowLevelMemTracker.h"
#include "AudioDecompress.h"
#include "AudioDevice.h"
#include "AudioCompressionSettingsUtils.h"

static int32 DebugMaxElementsDisplayCVar = 128;
FAutoConsoleVariableRef CVarDebugDisplayCaches(
	TEXT("au.streamcaching.MaxCachesToDisplay"),
	DebugMaxElementsDisplayCVar,
	TEXT("Sets the max amount of stream chunks to display on screen.\n")
	TEXT("n: Number of elements to display on screen."),
	ECVF_Default);

static int32 KeepCacheMissBufferOnFlushCVar = 1;
FAutoConsoleVariableRef CVarKeepCacheMissBufferOnFlush(
	TEXT("au.streamcaching.KeepCacheMissBufferOnFlush"),
	KeepCacheMissBufferOnFlushCVar,
	TEXT("IF set to 1, this will maintain the buffer of recorded cache misses after calling AudioMemReport. Otherwise, calling audiomemreport will flush all previous recorded cache misses.\n")
	TEXT("1: All cache misses from the  whole session will show up in audiomemreport. 0: Only cache misses since the previous call to audiomemreport will show up in the current audiomemreport."),
	ECVF_Default);

static int32 ForceBlockForLoadCVar = 0;
FAutoConsoleVariableRef CVarForceBlockForLoad(
	TEXT("au.streamcaching.ForceBlockForLoad"),
	ForceBlockForLoadCVar,
	TEXT("when set to a nonzero value, blocks GetLoadedChunk until the disk read is complete.\n")
	TEXT("n: Number of elements to display on screen."),
	ECVF_Default);

static int32 TrimCacheWhenOverBudgetCVar = 1;
FAutoConsoleVariableRef CVarTrimCacheWhenOverBudget(
	TEXT("au.streamcaching.TrimCacheWhenOverBudget"),
	TrimCacheWhenOverBudgetCVar,
	TEXT("when set to a nonzero value, TrimMemory will be called in AddOrTouchChunk to ensure we never go over budget.\n")
	TEXT("n: Number of elements to display on screen."),
	ECVF_Default);
	
static int32 AlwaysLogCacheMissesCVar = 0;
FAutoConsoleVariableRef CVarAlwaysLogCacheMisses(
	TEXT("au.streamcaching.AlwaysLogCacheMisses"),
	AlwaysLogCacheMissesCVar,
	TEXT("when set to a nonzero value, all cache misses will be added to the audiomemreport.\n")
	TEXT("0: Don't log cache misses until au.streamcaching.StartProfiling is called. 1: Always log cache misses."),
	ECVF_Default);

static int32 ReadRequestPriorityCVar = 2;
FAutoConsoleVariableRef CVarReadRequestPriority(
	TEXT("au.streamcaching.ReadRequestPriority"),
	ReadRequestPriorityCVar,
	TEXT("This cvar sets the default request priority for audio chunks when Stream Caching is turned on.\n")
	TEXT("0: High, 1: Normal, 2: Below Normal, 3: Low, 4: Min"),
	ECVF_Default);

static int32 PlaybackRequestPriorityCVar = 0;
FAutoConsoleVariableRef CVarPlaybackRequestPriority(
	TEXT("au.streamcaching.PlaybackRequestPriority"),
	PlaybackRequestPriorityCVar,
	TEXT("This cvar sets the default request priority for audio chunks that are about to play back but aren't in the cache.\n")
	TEXT("0: High, 1: Normal, 2: Below Normal, 3: Low, 4: Min"),
	ECVF_Default);

static int32 BlockForPendingLoadOnCacheOverflowCVar = 0;
FAutoConsoleVariableRef CVarBlockForPendingLoadOnCacheOverflow(
	TEXT("au.streamcaching.BlockForPendingLoadOnCacheOverflow"),
	BlockForPendingLoadOnCacheOverflowCVar,
	TEXT("This cvar sets the default request priority for audio chunks that are about to play back but aren't in the cache.\n")
	TEXT("0: when we blow the cache we clear any soundwave retainers. 1: when we blow the cache we attempt to cancel a load in flight."),
	ECVF_Default);

static int32 NumSoundWavesToClearOnCacheOverflowCVar = 0;
FAutoConsoleVariableRef CVarNumSoundWavesToClearOnCacheOverflow(
	TEXT("au.streamcaching.NumSoundWavesToClearOnCacheOverflow"),
	NumSoundWavesToClearOnCacheOverflowCVar,
	TEXT("When set > 0, we will attempt to release retainers for only that many sounds every time we have a cache overflow.\n")
	TEXT("0: reset all retained sounds on cache overflow, >0: evict this many sounds on any cache overflow."),
	ECVF_Default);

static float StreamCacheSizeOverrideMBCVar = 0.0f;
FAutoConsoleVariableRef CVarStreamCacheSizeOverrideMB(
	TEXT("au.streamcaching.StreamCacheSizeOverrideMB"),
	StreamCacheSizeOverrideMBCVar,
	TEXT("This cvar can be set to override the size of the cache.\n")
	TEXT("0: use cache size from project settings. n: the new cache size in megabytes."),
	ECVF_Default);

static int32 SaveAudioMemReportOnCacheOverflowCVar = 0;
FAutoConsoleVariableRef CVarSaveAudiomemReportOnCacheOverflow(
	TEXT("au.streamcaching.SaveAudiomemReportOnCacheOverflow"),
	SaveAudioMemReportOnCacheOverflowCVar,
	TEXT("When set to one, we print an audiomemreport when the cache has overflown.\n")
	TEXT("0: Disabled, 1: Enabled"),
	ECVF_Default);

static int32 UseObjectKeyInChunkKeyComparisonsCVar = 1;
FAutoConsoleVariableRef CVarUseObjectKeyInChunkKeyComparisons(
	TEXT("au.streamcaching.UseObjectKeyInChunkKeyComparisons"),
	UseObjectKeyInChunkKeyComparisonsCVar,
	TEXT("Enables the comparison of FObjectKeys when comparing Stream Cache Chunk Keys.  Without this FName collisions could occur if 2 SoundWaves have the same name.\n")
	TEXT("1: (default) Compare object keys.  0: Do not compare object keys."),
	ECVF_Default);

static FAutoConsoleCommand GFlushAudioCacheCommand(
	TEXT("au.streamcaching.FlushAudioCache"),
	TEXT("This will flush any non retained audio from the cache when Stream Caching is enabled."),
	FConsoleCommandDelegate::CreateStatic(
		[]()
	{
		static constexpr uint64 NumBytesToFree = TNumericLimits<uint64>::Max() / 2;
		uint64 NumBytesFreed = IStreamingManager::Get().GetAudioStreamingManager().TrimMemory(NumBytesToFree);

		UE_LOG(LogAudio, Display, TEXT("Audio Cache Flushed! %d megabytes free."), NumBytesFreed / (1024.0 * 1024.0));
	})
);

static FAutoConsoleCommand GResizeAudioCacheCommand(
	TEXT("au.streamcaching.ResizeAudioCacheTo"),
	TEXT("This will try to cull enough audio chunks to shrink the audio stream cache to the new size if neccessary, and keep the cache at that size."),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray< FString >& Args)
{
	if (Args.Num() < 1)
	{
		return;
	}

	const float InMB = FCString::Atof(*Args[0]);

	if (InMB <= 0.0f)
	{
		return;
	}

	static IConsoleVariable* StreamCacheSizeCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("au.streamcaching.StreamCacheSizeOverrideMB"));
	check(StreamCacheSizeCVar);

	uint64 NewCacheSizeInBytes = ((uint64)(InMB * 1024)) * 1024;
	uint64 OldCacheSizeInBytes = ((uint64)(StreamCacheSizeCVar->GetFloat() * 1024)) * 1024;

	// TODO: here we delete the difference between the old cache size and the new cache size,
	// but we don't actually need to do this unless the cache is full.
	// In the future we can use our current cache usage to figure out how much we need to trim.
	if (NewCacheSizeInBytes < OldCacheSizeInBytes)
	{
		uint64 NumBytesToFree = OldCacheSizeInBytes - NewCacheSizeInBytes;
		IStreamingManager::Get().GetAudioStreamingManager().TrimMemory(NumBytesToFree);
	}

	StreamCacheSizeCVar->Set(InMB);

	UE_LOG(LogAudio, Display, TEXT("Audio Cache Shrunk! Now set to be %f MB."), InMB);
})
);

static FAutoConsoleCommand GEnableProfilingAudioCacheCommand(
	TEXT("au.streamcaching.StartProfiling"),
	TEXT("This will start a performance-intensive profiling mode for this streaming manager. Profile stats can be output with audiomemreport."),
	FConsoleCommandDelegate::CreateStatic(
		[]()
{
	IStreamingManager::Get().GetAudioStreamingManager().SetProfilingMode(true);

	UE_LOG(LogAudio, Display, TEXT("Enabled profiling mode on the audio stream cache."));
})
);

static FAutoConsoleCommand GDisableProfilingAudioCacheCommand(
	TEXT("au.streamcaching.StopProfiling"),
	TEXT("This will start a performance-intensive profiling mode for this streaming manager. Profile stats can be output with audiomemreport."),
	FConsoleCommandDelegate::CreateStatic(
		[]()
{
	IStreamingManager::Get().GetAudioStreamingManager().SetProfilingMode(false);

	UE_LOG(LogAudio, Display, TEXT("Disabled profiling mode on the audio stream cache."));
})
);


bool FAudioChunkCache::FChunkKey::operator==(const FChunkKey& Other) const
{
	if (UseObjectKeyInChunkKeyComparisonsCVar != 0)
	{
#if WITH_EDITOR
	return (SoundWaveName == Other.SoundWaveName) && (ObjectKey == Other.ObjectKey) && (ChunkIndex == Other.ChunkIndex) && (ChunkRevision == Other.ChunkRevision);
#else
	return (SoundWaveName == Other.SoundWaveName) && (ObjectKey == Other.ObjectKey) && (ChunkIndex == Other.ChunkIndex);
#endif
	}
	else
	{
#if WITH_EDITOR
		return (SoundWaveName == Other.SoundWaveName) && (ChunkIndex == Other.ChunkIndex) && (ChunkRevision == Other.ChunkRevision);
#else
		return (SoundWaveName == Other.SoundWaveName) && (ChunkIndex == Other.ChunkIndex);
#endif
	}

}

FCachedAudioStreamingManager::FCachedAudioStreamingManager(const FCachedAudioStreamingManagerParams& InitParams)
{
	LLM_SCOPE(ELLMTag::AudioStreamCache);
	check(FPlatformCompressionUtilities::IsCurrentPlatformUsingStreamCaching());
	checkf(InitParams.Caches.Num() > 0, TEXT("FCachedAudioStreamingManager should be initialized with dimensions for at least one cache."));

	// const FAudioStreamCachingSettings& CacheSettings = FPlatformCompressionUtilities::GetStreamCachingSettingsForCurrentPlatform();
	for (const FCachedAudioStreamingManagerParams::FCacheDimensions& CacheDimensions : InitParams.Caches)
	{
		CacheArray.Emplace(CacheDimensions.MaxChunkSize, CacheDimensions.NumElements, CacheDimensions.MaxMemoryInBytes);
	}

	// Here we make sure our CacheArray is sorted from smallest MaxChunkSize to biggest, so that GetCacheForWave can scan through these caches to find the appropriate cache for the chunk size.
	CacheArray.Sort();
}

FCachedAudioStreamingManager::~FCachedAudioStreamingManager()
{
}

void FCachedAudioStreamingManager::UpdateResourceStreaming(float DeltaTime, bool bProcessEverything /*= false*/)
{
	// The cached audio streaming manager doesn't tick.
}

int32 FCachedAudioStreamingManager::BlockTillAllRequestsFinished(float TimeLimit, bool bLogResults)
{
	LLM_SCOPE(ELLMTag::AudioStreamCache);

	// TODO: Honor TimeLimit and bLogResults. Since we cancel any in flight read requests, this should not spin out.
	for (FAudioChunkCache& Cache : CacheArray)
	{
		Cache.CancelAllPendingLoads();
	}

	return 0;
}

void FCachedAudioStreamingManager::CancelForcedResources()
{
	// Unused.
}

void FCachedAudioStreamingManager::NotifyLevelChange()
{
	// Unused.
}

void FCachedAudioStreamingManager::SetDisregardWorldResourcesForFrames(int32 NumFrames)
{
	// Unused.
}

void FCachedAudioStreamingManager::AddLevel(class ULevel* Level)
{
	// Unused.
}

void FCachedAudioStreamingManager::RemoveLevel(class ULevel* Level)
{
	// Unused.
}

void FCachedAudioStreamingManager::NotifyLevelOffset(class ULevel* Level, const FVector& Offset)
{
	// Unused.
}

void FCachedAudioStreamingManager::AddStreamingSoundWave(USoundWave* SoundWave)
{
	// Unused.
}

void FCachedAudioStreamingManager::RemoveStreamingSoundWave(USoundWave* SoundWave)
{
	// Unused.
}

void FCachedAudioStreamingManager::AddDecoder(ICompressedAudioInfo* InCompressedAudioInfo)
{
	// Unused.
}

void FCachedAudioStreamingManager::RemoveDecoder(ICompressedAudioInfo* InCompressedAudioInfo)
{
	//Unused.
}

bool FCachedAudioStreamingManager::IsManagedStreamingSoundWave(const USoundWave* SoundWave) const
{
	// Unused. The concept of a sound wave being "managed" doesn't apply here.
	checkf(false, TEXT("Not Implemented!"));
	return true;
}

bool FCachedAudioStreamingManager::IsStreamingInProgress(const USoundWave* SoundWave)
{
	// This function is used in USoundWave cleanup.
	// Since this manager owns the binary data we are streaming off of,
	// It's safe to delete the USoundWave as long as
	// There are NO sound sources playing with this Sound Wave.
	//
	// This is because a playing sound source might kick off a load for a new chunk,
	// which dereferences the corresponding USoundWave.
	//
	// As of right now, this is handled by USoundWave::FreeResources(), called
	// by USoundWave::IsReadyForFinishDestroy.
	return false;
}

bool FCachedAudioStreamingManager::CanCreateSoundSource(const FWaveInstance* WaveInstance) const
{
	return true;
}

void FCachedAudioStreamingManager::AddStreamingSoundSource(FSoundSource* SoundSource)
{
	// Unused.
}

void FCachedAudioStreamingManager::RemoveStreamingSoundSource(FSoundSource* SoundSource)
{
	// Unused.
}

bool FCachedAudioStreamingManager::IsManagedStreamingSoundSource(const FSoundSource* SoundSource) const
{
	// Unused. The concept of a sound wave being "managed" doesn't apply here.
	checkf(false, TEXT("Not Implemented!"));
	return true;
}

FAudioChunkHandle FCachedAudioStreamingManager::GetLoadedChunk(const USoundWave* SoundWave, uint32 ChunkIndex, bool bBlockForLoad, bool bForImmediatePlayback) const
{
	LLM_SCOPE(ELLMTag::AudioStreamCache);
	bBlockForLoad |= (ForceBlockForLoadCVar != 0);

	// If this sound wave is managed by a cache, use that to get the chunk:
	FAudioChunkCache* Cache = GetCacheForWave(SoundWave);
	if (Cache)
	{
		// With this code, the zeroth chunk should never get hit.
		checkf(ChunkIndex != 0, TEXT("Decoder tried to access the zeroth chunk through the streaming manager. Use USoundWave::GetZerothChunk() instead."));

		// TODO:  See if we can avoid non-const calls on the USoundWave here.
		USoundWave* MutableWave = const_cast<USoundWave*>(SoundWave);
		const FAudioChunkCache::FChunkKey ChunkKey =
		{
			  MutableWave
			, SoundWave->GetFName()
			, ChunkIndex
			, FObjectKey(MutableWave)
#if WITH_EDITOR
			, (uint32)SoundWave->CurrentChunkRevision.GetValue()
#endif
		};

		if (!FAudioChunkCache::IsKeyValid(ChunkKey))
		{
			UE_LOG(LogAudio, Warning, TEXT("Invalid Chunk Index %d Requested for Wave %s!"), ChunkIndex, *SoundWave->GetName());
			return FAudioChunkHandle();
		}

		// The function call below increments the reference count to the internal chunk.
		uint64 LookupIDForChunk = ChunkKey.SoundWave->GetCacheLookupIDForChunk(ChunkKey.ChunkIndex);
		TArrayView<uint8> LoadedChunk = Cache->GetChunk(ChunkKey, bBlockForLoad, (bForImmediatePlayback || bBlockForLoad), LookupIDForChunk);
		
		// Ensure that, if we requested a synchronous load of this chunk, we didn't fail to load said chunk.
		UE_CLOG(bBlockForLoad && !LoadedChunk.GetData(), LogAudio, Display, TEXT("Synchronous load of chunk index %d for SoundWave %s failed to return any data. Likely because the cache was blown."), ChunkIndex, *SoundWave->GetName());

		// Set the updated cache offset for this chunk index.
		ChunkKey.SoundWave->SetCacheLookupIDForChunk(ChunkIndex, LookupIDForChunk);

		UE_CLOG(!bBlockForLoad && !LoadedChunk.GetData(), LogAudio, Display, TEXT("GetLoadedChunk called for chunk index %d of SoundWave %s when audio was not loaded yet. This will result in latency."), ChunkIndex, *SoundWave->GetName());

		// Finally, if there's a chunk after this in the sound, request that it is in the cache.
		const int32 NextChunk = GetNextChunkIndex(SoundWave, ChunkIndex);

		if (NextChunk != INDEX_NONE) 
		{
			const FAudioChunkCache::FChunkKey NextChunkKey = 
			{ 
				  MutableWave 
				, SoundWave->GetFName() 
				, ((uint32)NextChunk) 
				, FObjectKey(MutableWave)
#if WITH_EDITOR
				, (uint32)SoundWave->CurrentChunkRevision.GetValue()
#endif
			};

			uint64 LookupIDForNextChunk = Cache->AddOrTouchChunk(NextChunkKey, [](EAudioChunkLoadResult) {}, ENamedThreads::AnyThread, false);
			if (LookupIDForNextChunk == InvalidAudioStreamCacheLookupID)
			{
				// this bool is true while we are waiting on the game thread to reset chunk handles owned by USoundWaves
				static FThreadSafeBool bCacheCurrentlyBlown = false;

				if (!bCacheCurrentlyBlown)
				{
					bCacheCurrentlyBlown = true;
					Cache->IncrementCacheOverflowCounter();

					UE_LOG(LogAudio, Warning, TEXT("Cache overflow!!! couldn't load chunk %d for sound %s!"), ChunkIndex, *SoundWave->GetName());

					// gather SoundWaves to release compressed data on:
					TArray<FObjectKey> SoundWavesToRelease;

					if (NumSoundWavesToClearOnCacheOverflowCVar > 0)
					{
						SoundWavesToRelease = Cache->GetLeastRecentlyUsedRetainedSoundWaves(NumSoundWavesToClearOnCacheOverflowCVar);
					}

					AsyncTask(ENamedThreads::GameThread, [WavesToRelease = MoveTemp(SoundWavesToRelease)]() mutable
					{
						// Here we optionally capture the state of the cache when we overflowed:
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
						if (SaveAudioMemReportOnCacheOverflowCVar && GEngine && GEngine->GetMainAudioDevice())
						{
							GEngine->GetMainAudioDevice()->Exec(nullptr, TEXT("audiomemreport"));
						}
#endif // !UE_BUILD_SHIPPING && !UE_BUILD_TEST


						int32 NumChunksReleased = 0;

						for (TObjectIterator<USoundWave> It; It; ++It)
						{
							USoundWave* Wave = *It;
							if (Wave && Wave->IsRetainingAudio())
							{
								// If we have a specific list of sound waves to release, check if this sound wave is in it.
								if (WavesToRelease.Num())
								{
									for (int32 Index = 0; Index < WavesToRelease.Num(); Index++)
									{
										if (WavesToRelease[Index] == FObjectKey(Wave))
										{
											Wave->ReleaseCompressedAudio();
											WavesToRelease.RemoveAtSwap(Index);
											NumChunksReleased++;
											break;
										}
									}

									// If we've found every wave we're going to release, break out of the soundwave iterator.
									if (WavesToRelease.Num() == 0)
									{
										break;
									}
								}
								else
								{
									// Otherwise, we release all compressed audio by default.
									Wave->ReleaseCompressedAudio();
									NumChunksReleased++;
								}
							}
						}

						UE_LOG(LogAudio, Warning, TEXT("Removed %d retained sounds from the stream cache."), NumChunksReleased);

						bCacheCurrentlyBlown = false;
					});
				}
			}
			else
			{
				NextChunkKey.SoundWave->SetCacheLookupIDForChunk(NextChunkKey.ChunkIndex, LookupIDForNextChunk);
			}
		}

		return BuildChunkHandle(LoadedChunk.GetData(), LoadedChunk.Num(), SoundWave, SoundWave->GetFName(), ChunkIndex, LookupIDForChunk);
	}
	else
	{
		ensureMsgf(false, TEXT("Failed to find cache for wave %s. Are you sure this is a streaming wave?"), *SoundWave->GetName());
		return FAudioChunkHandle();
	}
}

FAudioChunkCache* FCachedAudioStreamingManager::GetCacheForWave(const USoundWave* InSoundWave) const
{
	LLM_SCOPE(ELLMTag::AudioStreamCache);
	check(InSoundWave);

	// We only cache chunks beyond the zeroth chunk of audio (which is inlined directly on the asset)
	if (InSoundWave->RunningPlatformData && InSoundWave->RunningPlatformData->Chunks.Num() > 1)
	{
		const int32 SoundWaveChunkSize = InSoundWave->RunningPlatformData->Chunks[1].AudioDataSize;
		return GetCacheForChunkSize(SoundWaveChunkSize);
	}
	else
	{
		return nullptr;
	}
}

FAudioChunkCache* FCachedAudioStreamingManager::GetCacheForChunkSize(uint32 InChunkSize) const
{
	LLM_SCOPE(ELLMTag::AudioStreamCache);
	// Iterate over our caches until we find the lowest MaxChunkSize cache this sound's chunks will fit into. 
	for (int32 CacheIndex = 0; CacheIndex < CacheArray.Num(); CacheIndex++)
	{
		check(CacheArray[CacheIndex].MaxChunkSize >= 0);
		if (InChunkSize <= ((uint32) CacheArray[CacheIndex].MaxChunkSize))
		{
			return const_cast<FAudioChunkCache*>(&CacheArray[CacheIndex]);
		}
	}

	// If we ever hit this, something may have wrong during cook.
	// Please check to make sure this platform's implementation of IAudioFormat honors the MaxChunkSize parameter passed into SplitDataForStreaming,
	// or that FStreamedAudioCacheDerivedDataWorker::BuildStreamedAudio() is passing the correct MaxChunkSize to IAudioFormat::SplitDataForStreaming.
	ensureMsgf(false, TEXT("Chunks in SoundWave are too large: %d bytes"), InChunkSize);
	return nullptr;
}

int32 FCachedAudioStreamingManager::GetNextChunkIndex(const USoundWave* InSoundWave, uint32 CurrentChunkIndex) const
{
	LLM_SCOPE(ELLMTag::AudioStreamCache);
	check(InSoundWave);
	// TODO: Figure out a way to tell whether this wave is looping or not. For now we always prime the first chunk
	// during the playback of the last chunk.
	// if(bNotLooping) return ((int32) CurrentChunkIndex) < (InSoundWave->RunningPlatformData->Chunks.Num() - 1);
	
	const int32 NumChunksTotal = InSoundWave->GetNumChunks();
	if (NumChunksTotal <= 2)
	{
		// If there's only one chunk to cache (besides the zeroth chunk, which is inlined),
		// We don't need to load anything.
		return INDEX_NONE;
	}
	else if(CurrentChunkIndex == (NumChunksTotal - 1))
	{
		// if we're on the last chunk, load the first chunk after the zeroth chunk.
		return 1;
	}
	else
	{
		// Otherwise, there's another chunk of audio after this one before the end of this file.
		return CurrentChunkIndex + 1;
	}
}

void FCachedAudioStreamingManager::AddReferenceToChunk(const FAudioChunkHandle& InHandle)
{
	LLM_SCOPE(ELLMTag::AudioStreamCache);
	FAudioChunkCache* Cache = GetCacheForChunkSize(InHandle.CachedDataNumBytes);
	check(Cache);

	USoundWave* MutableWave = const_cast<USoundWave*>(InHandle.CorrespondingWave);
	FAudioChunkCache::FChunkKey ChunkKey =
	{
		  MutableWave
		, InHandle.CorrespondingWaveName
		, ((uint32) InHandle.ChunkIndex)
		, FObjectKey(MutableWave)
#if WITH_EDITOR
		, InHandle.ChunkGeneration
#endif
	};

	Cache->AddNewReferenceToChunk(ChunkKey, InHandle.CacheLookupID);
}

void FCachedAudioStreamingManager::RemoveReferenceToChunk(const FAudioChunkHandle& InHandle)
{
	LLM_SCOPE(ELLMTag::AudioStreamCache);
	FAudioChunkCache* Cache = GetCacheForChunkSize(InHandle.CachedDataNumBytes);
	check(Cache);

	USoundWave* MutableWave = const_cast<USoundWave*>(InHandle.CorrespondingWave);
	FAudioChunkCache::FChunkKey ChunkKey =
	{
		  MutableWave
		, InHandle.CorrespondingWaveName
		, ((uint32)InHandle.ChunkIndex)
		, FObjectKey(MutableWave)
#if WITH_EDITOR
		, InHandle.ChunkGeneration
#endif
	};

	Cache->RemoveReferenceToChunk(ChunkKey, InHandle.CacheLookupID);
}

bool FCachedAudioStreamingManager::RequestChunk(USoundWave* SoundWave, uint32 ChunkIndex, TFunction<void(EAudioChunkLoadResult)> OnLoadCompleted, ENamedThreads::Type ThreadToCallOnLoadCompletedOn, bool bForImmediatePlayback)
{
	LLM_SCOPE(ELLMTag::AudioStreamCache);
	FAudioChunkCache* Cache = GetCacheForWave(SoundWave);
	if (Cache)
	{
		FAudioChunkCache::FChunkKey ChunkKey = 
		{
			SoundWave
		  , SoundWave->GetFName()
		  , ChunkIndex
		  , FObjectKey(SoundWave)
#if WITH_EDITOR
		  , (uint32)SoundWave->CurrentChunkRevision.GetValue()
#endif
		};

		uint64 LookupIDForChunk = Cache->AddOrTouchChunk(ChunkKey, OnLoadCompleted, ThreadToCallOnLoadCompletedOn, bForImmediatePlayback);
		SoundWave->SetCacheLookupIDForChunk(ChunkIndex, LookupIDForChunk);
		return LookupIDForChunk != InvalidAudioStreamCacheLookupID;
	}
	else
	{
		// This can hit if an out of bounds chunk was requested, or the zeroth chunk was requested from the streaming manager.
		ensureMsgf(false, TEXT("GetCacheForWave failed for SoundWave %s!"), *SoundWave->GetName());
		SoundWave->SetCacheLookupIDForChunk(ChunkIndex, InvalidAudioStreamCacheLookupID);
		return false;
	}
}

FAudioChunkCache::FAudioChunkCache(uint32 InMaxChunkSize, uint32 NumChunks, uint64 InMemoryLimitInBytes)
	: MaxChunkSize(InMaxChunkSize)
	, MostRecentElement(nullptr)
	, LeastRecentElement(nullptr)
	, ChunksInUse(0)
	, MemoryCounterBytes(0)
	, MemoryLimitBytes(InMemoryLimitInBytes)
	, bLogCacheMisses(false)
{
	CachePool.Reset(NumChunks);
	for (uint32 Index = 0; Index < NumChunks; Index++)
	{
		CachePool.Emplace(MaxChunkSize, Index);
	}
	CacheOverflowCount.Set(0);
}

FAudioChunkCache::~FAudioChunkCache()
{
	LLM_SCOPE(ELLMTag::AudioStreamCache);
	// While this is handled by the default destructor, we do this to ensure that we don't leak async read operations.
	CachePool.Reset();
	check(NumberOfLoadsInFlight.GetValue() == 0);
}

uint64 FAudioChunkCache::AddOrTouchChunk(const FChunkKey& InKey, TFunction<void(EAudioChunkLoadResult)> OnLoadCompleted, ENamedThreads::Type CallbackThread, bool bNeededForPlayback)
{
	// Update cache limit if needed.
	if (!FMath::IsNearlyZero(StreamCacheSizeOverrideMBCVar) && StreamCacheSizeOverrideMBCVar > 0.0f)
	{
		MemoryLimitBytes = ((uint64)(StreamCacheSizeOverrideMBCVar * 1024)) * 1024;
	}
	
	if (!IsKeyValid(InKey))
	{
		ensure(false);
		ExecuteOnLoadCompleteCallback(EAudioChunkLoadResult::ChunkOutOfBounds, OnLoadCompleted, CallbackThread);
		return InvalidAudioStreamCacheLookupID;
	}

	FScopeLock ScopeLock(&CacheMutationCriticalSection);

	const uint64 LookupIDForChunk = InKey.SoundWave->GetCacheLookupIDForChunk(InKey.ChunkIndex);
	FCacheElement* FoundElement = FindElementForKey(InKey, LookupIDForChunk);
	
	if (FoundElement)
	{
		TouchElement(FoundElement);
		if (FoundElement->bIsLoaded)
		{
			ExecuteOnLoadCompleteCallback(EAudioChunkLoadResult::AlreadyLoaded, OnLoadCompleted, CallbackThread);
		}

#if DEBUG_STREAM_CACHE
		FoundElement->DebugInfo.NumTimesTouched++;

		// Recursing in no longer needed at this point since the inherited loading behavior has already been cached by the time this information is needed
		const bool bRecurseSoundClasses = false;
		FoundElement->DebugInfo.LoadingBehavior = InKey.SoundWave->GetLoadingBehavior(bRecurseSoundClasses);

		FoundElement->DebugInfo.bLoadingBehaviorExternallyOverriden = InKey.SoundWave->bLoadingBehaviorOverridden;
#endif

		return FoundElement->CacheLookupID;
	}
	else
	{
		FCacheElement* CacheElement = InsertChunk(InKey);

		if (!CacheElement)
		{
			ExecuteOnLoadCompleteCallback(EAudioChunkLoadResult::CacheBlown, OnLoadCompleted, CallbackThread);
			return InvalidAudioStreamCacheLookupID;
		}

#if DEBUG_STREAM_CACHE
		CacheElement->DebugInfo.bWasCacheMiss = bNeededForPlayback;

		// Recursing in no longer needed at this point since the inherited loading behavior has already been cached by the time this information is needed
		const bool bRecurseSoundClasses = false;
		CacheElement->DebugInfo.LoadingBehavior = InKey.SoundWave->GetLoadingBehavior(bRecurseSoundClasses);

		CacheElement->DebugInfo.bLoadingBehaviorExternallyOverriden = InKey.SoundWave->bLoadingBehaviorOverridden;
#endif
		const FStreamedAudioChunk& Chunk = InKey.SoundWave->RunningPlatformData->Chunks[InKey.ChunkIndex];
		int32 ChunkDataSize = Chunk.AudioDataSize;

		if (TrimCacheWhenOverBudgetCVar != 0 && (MemoryCounterBytes + ChunkDataSize) > MemoryLimitBytes)
		{
			TrimMemory(MemoryCounterBytes + ChunkDataSize - MemoryLimitBytes);
		}

		KickOffAsyncLoad(CacheElement, InKey, OnLoadCompleted, CallbackThread, bNeededForPlayback);

		if (bNeededForPlayback && (bLogCacheMisses || AlwaysLogCacheMissesCVar))
		{
			// We missed 
			const uint32 TotalNumChunksInWave = InKey.SoundWave->GetNumChunks();

			FCacheMissInfo CacheMissInfo = { InKey.SoundWaveName, InKey.ChunkIndex, TotalNumChunksInWave, false };
			CacheMissQueue.Enqueue(MoveTemp(CacheMissInfo));
		}

		return CacheElement->CacheLookupID;
	}
}

TArrayView<uint8> FAudioChunkCache::GetChunk(const FChunkKey& InKey, bool bBlockForLoadCompletion, bool bNeededForPlayback, uint64& OutCacheOffset)
{
	FScopeLock ScopeLock(&CacheMutationCriticalSection);
	FCacheElement* FoundElement = FindElementForKey(InKey, OutCacheOffset);
	if (FoundElement)
	{
		OutCacheOffset = FoundElement->CacheLookupID;
		TouchElement(FoundElement);
		if (FoundElement->IsLoadInProgress())
		{
			if (bBlockForLoadCompletion)
			{
				FoundElement->WaitForAsyncLoadCompletion(false);
			}
			else
			{
				return TArrayView<uint8>();
			}
		}


		// If this value is ever negative, it means that we're decrementing more than we're incrementing:
		check(FoundElement->NumConsumers.GetValue() >= 0);
		FoundElement->NumConsumers.Increment();
		return TArrayView<uint8>(FoundElement->ChunkData, FoundElement->ChunkDataSize);
	}
	else
	{
		// If we missed it, kick off a new load with it.
		FoundElement = InsertChunk(InKey);
		if (!FoundElement)
		{
			OutCacheOffset = InvalidAudioStreamCacheLookupID;
			UE_LOG(LogAudio, Display, TEXT("GetChunk failed to find an available chunk slot in the cache, likely because the cache is blown."));
			return TArrayView<uint8>();
		}
		
		OutCacheOffset = FoundElement->CacheLookupID;

		if (bBlockForLoadCompletion)
		{
			FStreamedAudioChunk& Chunk = InKey.SoundWave->RunningPlatformData->Chunks[InKey.ChunkIndex];
			int32 ChunkAudioDataSize = Chunk.AudioDataSize;
#if DEBUG_STREAM_CACHE
			FoundElement->DebugInfo.NumTotalChunks = InKey.SoundWave->GetNumChunks() - 1;
			FoundElement->DebugInfo.TimeLoadStarted = FPlatformTime::Seconds();
#endif
			MemoryCounterBytes -= FoundElement->ChunkDataSize;

			// Reallocate our chunk data This allows us to shrink if possible.
			FoundElement->ChunkData = (uint8*)FMemory::Realloc(FoundElement->ChunkData, ChunkAudioDataSize);

			if (Chunk.DataSize != ChunkAudioDataSize)
			{
				// Unfortunately, GetCopy will write out the full zero-padded length of the bulk data,
				// rather than just the audio data. So we set the array to Chunk.DataSize, then shrink to Chunk.AudioDataSize.
				TArray<uint8> TempChunkBuffer;
				TempChunkBuffer.AddUninitialized(Chunk.DataSize);
				void* DataDestPtr = TempChunkBuffer.GetData();
				Chunk.BulkData.GetCopy(&DataDestPtr, true);

				FMemory::Memcpy(FoundElement->ChunkData, TempChunkBuffer.GetData(), ChunkAudioDataSize);
			}
			else
			{
				void* DataDestPtr = FoundElement->ChunkData;
				Chunk.BulkData.GetCopy(&DataDestPtr, true);
			}

			MemoryCounterBytes += ChunkAudioDataSize;

			// Populate key and DataSize. The async read request was set up to write directly into CacheElement->ChunkData.
			FoundElement->Key = InKey;
			FoundElement->ChunkDataSize = ChunkAudioDataSize;
			FoundElement->bIsLoaded = true;
#if DEBUG_STREAM_CACHE
			FoundElement->DebugInfo.TimeToLoad = (FPlatformTime::Seconds() - FoundElement->DebugInfo.TimeLoadStarted) * 1000.0f;

#endif
			// If this value is ever negative, it means that we're decrementing more than we're incrementing:
			if (ensureMsgf(FoundElement->NumConsumers.GetValue() >= 0, TEXT("NumConsumers was negative for FoundElement. Reseting to 1")))
			{
				FoundElement->NumConsumers.Increment();
			}
			else
			{
				FoundElement->NumConsumers.Set(1);
			}

			return TArrayView<uint8>(FoundElement->ChunkData, ChunkAudioDataSize);
		}
		else
		{
			KickOffAsyncLoad(FoundElement, InKey, [](EAudioChunkLoadResult InResult) {}, ENamedThreads::AnyThread, bNeededForPlayback);
		}
		if (bLogCacheMisses && !bBlockForLoadCompletion)
		{
			// Chunks missing. Log this as a miss.
			const uint32 TotalNumChunksInWave = InKey.SoundWave->GetNumChunks();
			FCacheMissInfo CacheMissInfo = { InKey.SoundWaveName, InKey.ChunkIndex, TotalNumChunksInWave, false };
			CacheMissQueue.Enqueue(MoveTemp(CacheMissInfo));
		}
		// We missed, return an empty array view.
		return TArrayView<uint8>();
	}
}

void FAudioChunkCache::AddNewReferenceToChunk(const FChunkKey& InKey, uint64 ChunkOffset)
{
	FScopeLock ScopeLock(&CacheMutationCriticalSection);
	FCacheElement* FoundElement = FindElementForKey(InKey, ChunkOffset);
	if (ensure(FoundElement))
	{
		// If this value is ever negative, it means that we're decrementing more than we're incrementing:
		check(FoundElement->NumConsumers.GetValue() >= 0);
		FoundElement->NumConsumers.Increment();
	}
}

void FAudioChunkCache::RemoveReferenceToChunk(const FChunkKey& InKey, uint64 ChunkOffset)
{
	FScopeLock ScopeLock(&CacheMutationCriticalSection);
	FCacheElement* FoundElement = FindElementForKey(InKey, ChunkOffset);
	if (ensure(FoundElement))
	{
		// If this value is ever less than 1 when we hit this code, it means that we're decrementing more than we're incrementing:
		check(FoundElement->NumConsumers.GetValue() >= 1);
		FoundElement->NumConsumers.Decrement();
	}
}

void FAudioChunkCache::ClearCache()
{
	FScopeLock ScopeLock(&CacheMutationCriticalSection);
	const uint32 NumChunks = CachePool.Num();

	CachePool.Reset(NumChunks);
	check(NumberOfLoadsInFlight.GetValue() == 0);

	for (uint32 Index = 0; Index < NumChunks; Index++)
	{
		CachePool.Emplace(MaxChunkSize, Index);
	}

	MostRecentElement = nullptr;
	LeastRecentElement = nullptr;
	ChunksInUse = 0;
}

uint64 FAudioChunkCache::TrimMemory(uint64 BytesToFree)
{
	FScopeLock ScopeLock(&CacheMutationCriticalSection);

	if (!MostRecentElement || MostRecentElement->LessRecentElement == nullptr)
	{
		return 0;
	}

	FCacheElement* CurrentElement = LeastRecentElement;

	// In order to avoid cycles, we always leave at least two chunks in the cache.
	const FCacheElement* ElementToStopAt = MostRecentElement->LessRecentElement;

	uint64 BytesFreed = 0;
	while (CurrentElement != ElementToStopAt && BytesFreed < BytesToFree)
	{
		if (CurrentElement->CanEvictChunk())
		{
			BytesFreed += CurrentElement->ChunkDataSize;
			MemoryCounterBytes -= CurrentElement->ChunkDataSize;
			// Empty the chunk data and invalidate the key.
			if(CurrentElement->ChunkData)
			{
				LLM_SCOPE(ELLMTag::AudioStreamCacheCompressedData);
				FMemory::Free(CurrentElement->ChunkData);
				CurrentElement->ChunkData = nullptr;
			}

			CurrentElement->ChunkDataSize = 0;
			CurrentElement->Key = FChunkKey();

#if DEBUG_STREAM_CACHE
			// Reset debug info:
			CurrentElement->DebugInfo.Reset();
#endif
		}

		// Important to note that we don't actually relink chunks here,
		// So by trimming memory we are not moving chunks up the recency list.
		CurrentElement = CurrentElement->MoreRecentElement;
	}

	return BytesFreed;
}

void FAudioChunkCache::BlockForAllPendingLoads() const
{
	bool bLoadInProgress = false;

	float TimeStarted = FPlatformTime::Seconds();

	do
	{
		// If we did find an in flight async load,
		// sleep to let other threads complete this task.
		if (bLoadInProgress)
		{
			float TimeSinceStarted = FPlatformTime::Seconds() - TimeStarted;
			UE_LOG(LogAudio, Log, TEXT("Waited %f seconds for async audio chunk loads."), TimeSinceStarted);
			FPlatformProcess::Sleep(0.0f);
		}

		{
			FScopeLock ScopeLock(const_cast<FCriticalSection*>(&CacheMutationCriticalSection));

			// Iterate through every element until we find one with a load in progress.
			const FCacheElement* CurrentElement = MostRecentElement;
			while (CurrentElement != nullptr)
			{
				bLoadInProgress |= CurrentElement->IsLoadInProgress();
				CurrentElement = CurrentElement->LessRecentElement;
			}
		}
	} while (bLoadInProgress);
}

void FAudioChunkCache::CancelAllPendingLoads()
{
	FScopeLock ScopeLock(&CacheMutationCriticalSection);
	FCacheElement* CurrentElement = MostRecentElement;
	while (CurrentElement != nullptr)
	{
		CurrentElement->WaitForAsyncLoadCompletion(true);
		CurrentElement = CurrentElement->LessRecentElement;
	}
}

uint64 FAudioChunkCache::ReportCacheSize()
{
	const uint32 NumChunks = CachePool.Num();

	return MaxChunkSize * NumChunks;
}

void FAudioChunkCache::BeginLoggingCacheMisses()
{
	bLogCacheMisses = true;
}

void FAudioChunkCache::StopLoggingCacheMisses()
{
	bLogCacheMisses = false;
}

FString FAudioChunkCache::FlushCacheMissLog()
{
	FString ConcatenatedCacheMisses;
	ConcatenatedCacheMisses.Append(TEXT("All Cache Misses:\nSoundWave:\t, ChunkIndex\n"));

	struct FMissedChunk
	{
		FName SoundWaveName;
		uint32 ChunkIndex;
		int32 MissCount;
	};

	struct FCacheMissSortPredicate
	{
		FORCEINLINE bool operator()(const FMissedChunk& A, const FMissedChunk& B) const
		{
			// Sort from highest miss count to lowest.
			return A.MissCount > B.MissCount;
		}
	};

	TMap<FChunkKey, int32> CacheMissCount;

	TQueue<FCacheMissInfo> BackupQueue;

	FCacheMissInfo CacheMissInfo;
	while (CacheMissQueue.Dequeue(CacheMissInfo))
	{
		ConcatenatedCacheMisses.Append(CacheMissInfo.SoundWaveName.ToString());
		ConcatenatedCacheMisses.Append(TEXT("\t, "));
		ConcatenatedCacheMisses.AppendInt(CacheMissInfo.ChunkIndex);
		ConcatenatedCacheMisses.Append(TEXT("\n"));

		FChunkKey Chunk =
		{
			  nullptr
			, CacheMissInfo.SoundWaveName
			, CacheMissInfo.ChunkIndex
			, FObjectKey()
#if WITH_EDITOR
			, 0
#endif
		};

		int32& MissCount = CacheMissCount.FindOrAdd(Chunk);
		MissCount++;

		if (KeepCacheMissBufferOnFlushCVar)
		{
			BackupQueue.Enqueue(CacheMissInfo);
		}
	}

	// Sort our cache miss count map:
	TArray<FMissedChunk> ChunkMissArray;
	for (auto& CacheMiss : CacheMissCount)
	{
		FMissedChunk MissedChunk =
		{
			CacheMiss.Key.SoundWaveName,
			CacheMiss.Key.ChunkIndex,
			CacheMiss.Value
		};

		ChunkMissArray.Add(MissedChunk);
	}

	ChunkMissArray.Sort(FCacheMissSortPredicate());

	FString TopChunkMissesLog = TEXT("Most Missed Chunks:\n");
	TopChunkMissesLog += TEXT("Name:\t, Index:\t, Miss Count:\n");
	for (FMissedChunk& MissedChunk : ChunkMissArray)
	{
		TopChunkMissesLog.Append(MissedChunk.SoundWaveName.ToString());
		TopChunkMissesLog.Append(TEXT("\t, "));
		TopChunkMissesLog.AppendInt(MissedChunk.ChunkIndex);
		TopChunkMissesLog.Append(TEXT("\t, "));
		TopChunkMissesLog.AppendInt(MissedChunk.MissCount);
		TopChunkMissesLog.Append(TEXT("\n"));
	}

	// If we are keeping the full cache miss buffer around, re-enqueue every cache miss we dequeued.
	// Note: This could be done more gracefully if TQueue had a move constructor.
	if (KeepCacheMissBufferOnFlushCVar)
	{
		while (BackupQueue.Dequeue(CacheMissInfo))
		{
			CacheMissQueue.Enqueue(CacheMissInfo);
		}
	}

	return TopChunkMissesLog + TEXT("\n") + ConcatenatedCacheMisses;
}

FAudioChunkCache::FCacheElement* FAudioChunkCache::FindElementForKey(const FChunkKey& InKey, uint64 CacheOffset)
{
	FScopeLock ScopeLock(&CacheMutationCriticalSection);

	// If we have a known cache offset, access that chunk directly.
	if (CacheOffset != InvalidAudioStreamCacheLookupID)
	{
		check(CacheOffset < CachePool.Num());
		// Finally, sanity check that the key is still the same.
		if (CachePool[CacheOffset].Key == InKey)
		{
			return &CachePool[CacheOffset];
		}
	}

	//Otherwise, linearly search the cache.
	FCacheElement* CurrentElement = MostRecentElement;


	// In debuggable situations, we breadcrumb how far down the cache the cache we were.
	int32 ElementPosition = 0;

	while (CurrentElement != nullptr)
	{
		if (InKey == CurrentElement->Key)
		{

#if DEBUG_STREAM_CACHE
			float& CMA = CurrentElement->DebugInfo.AverageLocationInCacheWhenNeeded;
			CMA += ((ElementPosition - CMA) / (CurrentElement->DebugInfo.NumTimesTouched + 1));
#endif

			return CurrentElement;
		}
		else
		{
			CurrentElement = CurrentElement->LessRecentElement;


			ElementPosition++;

			if (CurrentElement && ElementPosition >= ChunksInUse)
			{
				UE_LOG(LogAudio, Warning, TEXT("Possible cycle in our LRU cache list. Please check to ensure any place FCacheElement::MoreRecentElement or FCacheElement::LessRecentElement is changed is locked by CacheMutationCriticalSection."));
				return nullptr;
			}
		}
	}

	return CurrentElement;
}

void FAudioChunkCache::TouchElement(FCacheElement* InElement)
{
	checkSlow(InElement);

	// Check to ensure we do not have any cycles in our list.
	// If this first check is hit, try to ensure that EvictLeastRecent chunk isn't evicting the top two chunks.
	check(MostRecentElement == nullptr || MostRecentElement != LeastRecentElement);
	check(InElement->LessRecentElement != InElement);

	FScopeLock ScopeLock(&CacheMutationCriticalSection);

	// If this is already the most recent element, we don't need to do anything.
	if (InElement == MostRecentElement)
	{
		return;
	}

	// If this was previously the least recent chunk, update LeastRecentElement.
	if (LeastRecentElement == InElement)
	{
		LeastRecentElement = InElement->MoreRecentElement;
	}

	FCacheElement* PreviousLessRecent = InElement->LessRecentElement;
	FCacheElement* PreviousMoreRecent = InElement->MoreRecentElement;
	FCacheElement* PreviousMostRecent = MostRecentElement;

	check(PreviousMostRecent != InElement);

	// Move this element to the top:
	MostRecentElement = InElement;
	InElement->MoreRecentElement = nullptr;
	InElement->LessRecentElement = PreviousMostRecent;

	if (PreviousMostRecent != nullptr)
	{
		PreviousMostRecent->MoreRecentElement = InElement;
	}

	if (PreviousLessRecent == PreviousMoreRecent)
	{
		return;
	}
	else
	{
		// Link InElement's previous neighbors together:
		if (PreviousLessRecent != nullptr)
		{
			PreviousLessRecent->MoreRecentElement = PreviousMoreRecent;
		}

		if (PreviousMoreRecent != nullptr)
		{
			PreviousMoreRecent->LessRecentElement = PreviousLessRecent;
		}
	}
}

bool FAudioChunkCache::ShouldAddNewChunk() const
{
	return (ChunksInUse < CachePool.Num()) && (MemoryCounterBytes.Load() < MemoryLimitBytes);
}

FAudioChunkCache::FCacheElement* FAudioChunkCache::InsertChunk(const FChunkKey& InKey)
{
	FCacheElement* CacheElement = nullptr;

	{
		FScopeLock ScopeLock(&CacheMutationCriticalSection);

		if (ShouldAddNewChunk())
		{
			// We haven't filled up the pool yet, so we don't need to evict anything.
			CacheElement = &CachePool[ChunksInUse];
			CacheElement->CacheLookupID = ChunksInUse;
			ChunksInUse++;
		}
		else
		{
			static bool bLoggedCacheSaturated = false;
			if (!bLoggedCacheSaturated)
			{
				UE_LOG(LogAudio, Display, TEXT("Audio Stream Cache: Using %d of %d chunks.."), ChunksInUse, CachePool.Num());
				bLoggedCacheSaturated = true;
			}

			// The pools filled, so we're going to need to evict.
			CacheElement = EvictLeastRecentChunk();

			// If we blew the cache, it might be because we have too many loads in flight. Here we attempt to find a load in flight for an unreferenced chunk:
			if (BlockForPendingLoadOnCacheOverflowCVar && !CacheElement)
			{
				UE_LOG(LogAudio, Warning, TEXT("Failed to find an available chunk slot in the audio streaming manager. Finding a load in flight for an unreferenced chunk and cancelling it."));
				CacheElement = EvictLeastRecentChunk(true);
			}

			if (!CacheElement)
			{
				UE_LOG(LogAudio, Display, TEXT("Failed to find an available chunk slot in the audio streaming manager, likely because the cache was blown."));
				return nullptr;
			}
		}

		check(CacheElement);
		CacheElement->bIsLoaded = false;
		CacheElement->Key = InKey;
		TouchElement(CacheElement);

		// If we've got multiple chunks, we can not cache the least recent chunk
		// without worrying about a circular dependency.
		if (LeastRecentElement == nullptr && ChunksInUse > 1)
		{
			SetUpLeastRecentChunk();
		}
	}

	InKey.SoundWave->SetCacheLookupIDForChunk(InKey.ChunkIndex, CacheElement->CacheLookupID);
	return CacheElement;
}

void FAudioChunkCache::SetUpLeastRecentChunk()
{
	FScopeLock ScopeLock(&CacheMutationCriticalSection);

	FCacheElement* CacheElement = MostRecentElement;
	while (CacheElement->LessRecentElement != nullptr)
	{
		CacheElement = CacheElement->LessRecentElement;
	}

	LeastRecentElement = CacheElement;
}

FAudioChunkCache::FCacheElement* FAudioChunkCache::EvictLeastRecentChunk(bool bBlockForPendingLoads /* = false */)
{
	FCacheElement* CacheElement = LeastRecentElement;

	// If the least recent chunk is evictable, evict it.
	bool bIsChunkEvictable = CacheElement->CanEvictChunk();
	bool bIsChunkLoadingButUnreferenced = (CacheElement->IsLoadInProgress() && !CacheElement->IsInUse());

	if (bIsChunkEvictable)
	{
		FCacheElement* NewLeastRecentElement = CacheElement->MoreRecentElement;
		check(NewLeastRecentElement);

		LeastRecentElement = NewLeastRecentElement;
	}
	else if (bBlockForPendingLoads && bIsChunkLoadingButUnreferenced)
	{
		CacheElement->WaitForAsyncLoadCompletion(true);

		FCacheElement* NewLeastRecentElement = CacheElement->MoreRecentElement;
		check(NewLeastRecentElement);

		LeastRecentElement = NewLeastRecentElement;
	}
	else
	{
		// We should never hit this code path unless we have at least two chunks active.
		check(MostRecentElement && MostRecentElement->LessRecentElement);

		// In order to avoid cycles, we always leave at least two chunks in the cache.
		const FCacheElement* ElementToStopAt = MostRecentElement->LessRecentElement;

		// Otherwise, we need to crawl up the cache from least recent used to most to find a chunk that is not in use:
		while (CacheElement && CacheElement != ElementToStopAt)
		{
			// If the least recent chunk is evictable, evict it.
			bIsChunkEvictable = CacheElement->CanEvictChunk();
			bIsChunkLoadingButUnreferenced = (CacheElement->IsLoadInProgress() && !CacheElement->IsInUse());

			if (bIsChunkEvictable)
			{
				// Link the two neighboring chunks:
				if (CacheElement->MoreRecentElement)
				{
					CacheElement->MoreRecentElement->LessRecentElement = CacheElement->LessRecentElement;
				}

				// If we ever hit this while loop it means that CacheElement is not the least recently used element.
				check(CacheElement->LessRecentElement);
				CacheElement->LessRecentElement->MoreRecentElement = CacheElement->MoreRecentElement;
				break;
			}
			else if (bBlockForPendingLoads && bIsChunkLoadingButUnreferenced)
			{
				CacheElement->WaitForAsyncLoadCompletion(true);

				// Link the two neighboring chunks:
				if (CacheElement->MoreRecentElement)
				{
					CacheElement->MoreRecentElement->LessRecentElement = CacheElement->LessRecentElement;
				}

				// If we ever hit this while loop it means that CacheElement is not the least recently used element.
				check(CacheElement->LessRecentElement);
				CacheElement->LessRecentElement->MoreRecentElement = CacheElement->MoreRecentElement;
				break;
			}
			else
			{
				CacheElement = CacheElement->MoreRecentElement;
			}
		}

		// If we ever hit this, it means that we couldn't find any cache elements that aren't in use.
		if (!CacheElement || CacheElement == ElementToStopAt)
		{
			UE_LOG(LogAudio, Warning, TEXT("Cache blown! Please increase the cache size (currently %lu bytes) or load less audio."), ReportCacheSize());
			return nullptr;
		}
	}

#if DEBUG_STREAM_CACHE
	// Reset debug information:
	CacheElement->DebugInfo.Reset();
#endif

	return CacheElement;
}

TArray<FObjectKey> FAudioChunkCache::GetLeastRecentlyUsedRetainedSoundWaves(int32 NumSoundWavesToRetrieve)
{
	// Start at the least recent element, then crawl our way up the LRU cache, 
	// adding object keys for elements as we go.
	FCacheElement* CacheElement = LeastRecentElement;

	TArray<FObjectKey> SoundWavesToRelease;

	// In order to avoid cycles, we always leave at least two chunks in the cache.
	const FCacheElement* ElementToStopAt = MostRecentElement;

	while (CacheElement && CacheElement != ElementToStopAt && SoundWavesToRelease.Num() < NumSoundWavesToRetrieve)
	{
		// If the least recent chunk is evictable, evict it.
		if (CacheElement->IsInUse())
		{
			SoundWavesToRelease.Add(CacheElement->Key.ObjectKey);
		}

		CacheElement = CacheElement->MoreRecentElement;
	}

	return SoundWavesToRelease;
}

static FAutoConsoleTaskPriority CPrio_ClearAudioChunkCacheReadRequest(
	TEXT("TaskGraph.TaskPriorities.ClearAudioChunkCacheReadRequest"),
	TEXT("Task and thread priority for an async task that clears FCacheElement::ReadRequest"),
	ENamedThreads::BackgroundThreadPriority, // if we have background priority task threads, then use them...
	ENamedThreads::NormalTaskPriority, // .. at normal task priority
	ENamedThreads::NormalTaskPriority // if we don't have background threads, then use normal priority threads at normal task priority instead
);

class FClearAudioChunkCacheReadRequestTask
{
	IBulkDataIORequest* ReadRequest;

public:
	FORCEINLINE FClearAudioChunkCacheReadRequestTask(IBulkDataIORequest* InReadRequest)
		: ReadRequest(InReadRequest)
	{
	}
	static FORCEINLINE TStatId GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FClearAudioChunkCacheReadRequestTask, STATGROUP_TaskGraphTasks);
	}
	static FORCEINLINE ENamedThreads::Type GetDesiredThread()
	{
		return CPrio_ClearAudioChunkCacheReadRequest.Get();
	}
	FORCEINLINE static ESubsequentsMode::Type GetSubsequentsMode()
	{
		return ESubsequentsMode::FireAndForget;
	}
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		if (ReadRequest)
		{
			ReadRequest->WaitCompletion();
			delete ReadRequest;
			ReadRequest = nullptr;
		}
	}
};

void FAudioChunkCache::KickOffAsyncLoad(FCacheElement* CacheElement, const FChunkKey& InKey, TFunction<void(EAudioChunkLoadResult)> OnLoadCompleted, ENamedThreads::Type CallbackThread, bool bNeededForPlayback)
{
	check(CacheElement);

	const FStreamedAudioChunk& Chunk = InKey.SoundWave->RunningPlatformData->Chunks[InKey.ChunkIndex];
	int32 ChunkDataSize = Chunk.AudioDataSize;

	EAsyncIOPriorityAndFlags AsyncIOPriority = GetAsyncPriorityForChunk(InKey, bNeededForPlayback);

	MemoryCounterBytes -= CacheElement->ChunkDataSize;

	{
		LLM_SCOPE(ELLMTag::AudioStreamCacheCompressedData);

		// Reallocate our chunk data This allows us to shrink if possible.
		CacheElement->ChunkData = (uint8*) FMemory::Realloc(CacheElement->ChunkData, Chunk.AudioDataSize);
		CacheElement->ChunkDataSize = Chunk.AudioDataSize;
	}

	MemoryCounterBytes += CacheElement->ChunkDataSize;

#if DEBUG_STREAM_CACHE
	CacheElement->DebugInfo.NumTotalChunks = InKey.SoundWave->GetNumChunks() - 1;
	CacheElement->DebugInfo.LoadingBehavior = InKey.SoundWave->GetLoadingBehavior(false);
	CacheElement->DebugInfo.bLoadingBehaviorExternallyOverriden = InKey.SoundWave->bLoadingBehaviorOverridden;
#endif

	// In editor, we retrieve from the DDC. In non-editor situations, we read the chunk async from the pak file.
#if WITH_EDITORONLY_DATA
	if (Chunk.DerivedDataKey.IsEmpty() == false)
	{
		CacheElement->ChunkDataSize = ChunkDataSize;

		INC_DWORD_STAT_BY(STAT_AudioMemorySize, ChunkDataSize);
		INC_DWORD_STAT_BY(STAT_AudioMemory, ChunkDataSize);

		if (CacheElement->DDCTask.IsValid())
		{
			UE_CLOG(!CacheElement->DDCTask->IsDone(), LogAudio, Display, TEXT("DDC work was not finished for a requested audio streaming chunk slot berfore reuse; This may cause a hitch."));
			CacheElement->DDCTask->EnsureCompletion();
		}

#if DEBUG_STREAM_CACHE
		CacheElement->DebugInfo.TimeLoadStarted = FPlatformTime::Cycles64();
#endif


		TFunction<void(bool)> OnLoadComplete = [OnLoadCompleted, CallbackThread, CacheElement, InKey, ChunkDataSize](bool bRequestFailed)
		{
			// Populate key and DataSize. The async read request was set up to write directly into CacheElement->ChunkData.
			CacheElement->Key = InKey;
			CacheElement->ChunkDataSize = ChunkDataSize;
			CacheElement->bIsLoaded = true;

#if DEBUG_STREAM_CACHE
			CacheElement->DebugInfo.TimeToLoad = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - CacheElement->DebugInfo.TimeLoadStarted);
#endif
			EAudioChunkLoadResult ChunkLoadResult = bRequestFailed ? EAudioChunkLoadResult::Interrupted : EAudioChunkLoadResult::Completed;
			ExecuteOnLoadCompleteCallback(ChunkLoadResult, OnLoadCompleted, CallbackThread);
		};

		NumberOfLoadsInFlight.Increment();

		CacheElement->DDCTask.Reset(new FAsyncStreamDerivedChunkTask(
			Chunk.DerivedDataKey,
			CacheElement->ChunkData,
			ChunkDataSize,
			&NumberOfLoadsInFlight,
			MoveTemp(OnLoadComplete)
		));

		CacheElement->DDCTask->StartBackgroundTask();
	}
	else
#endif // #if WITH_EDITORONLY_DATA
	{
		if (CacheElement->IsLoadInProgress())
		{
			CacheElement->WaitForAsyncLoadCompletion(true);
		}

		// Sanity check our bulk data against our currently allocated chunk size in the cache.
		const int32 ChunkBulkDataSize = Chunk.BulkData.GetBulkDataSize();
		check(ChunkDataSize <= ChunkBulkDataSize);
		check(((uint32)ChunkDataSize) <= CacheElement->ChunkDataSize);

		// If we ever want to eliminate zero-padding in chunks, that could be verified here:
		//ensureAlwaysMsgf(AudioChunkSize == ChunkBulkDataSize, TEXT("For memory load on demand, we do not zero-pad to page sizes."));

		NumberOfLoadsInFlight.Increment();

		FBulkDataIORequestCallBack AsyncFileCallBack = [this, OnLoadCompleted, CacheElement, InKey, ChunkDataSize, CallbackThread](bool bWasCancelled, IBulkDataIORequest*)
		{
			// Take ownership of the read request and close the storage
			IBulkDataIORequest* LocalReadRequest = (IBulkDataIORequest*)FPlatformAtomics::InterlockedExchangePtr((void* volatile*)&CacheElement->ReadRequest, (void*)0x1);

			if (LocalReadRequest && (void*)LocalReadRequest != (void*)0x1)
			{
				// Delete the request to avoid hording space in pak cache
				TGraphTask<FClearAudioChunkCacheReadRequestTask>::CreateTask().ConstructAndDispatchWhenReady(LocalReadRequest);
			}

			// Populate key and DataSize. The async read request was set up to write directly into CacheElement->ChunkData.
			CacheElement->Key = InKey;
			CacheElement->ChunkDataSize = ChunkDataSize;
			CacheElement->bIsLoaded = true;

#if DEBUG_STREAM_CACHE
			CacheElement->DebugInfo.TimeToLoad = (FPlatformTime::Seconds() - CacheElement->DebugInfo.TimeLoadStarted) * 1000.0f;
#endif
			const EAudioChunkLoadResult LoadResult = bWasCancelled ? EAudioChunkLoadResult::Interrupted : EAudioChunkLoadResult::Completed;
			ExecuteOnLoadCompleteCallback(LoadResult, OnLoadCompleted, CallbackThread);

			NumberOfLoadsInFlight.Decrement();
		};

#if DEBUG_STREAM_CACHE
		CacheElement->DebugInfo.TimeLoadStarted = FPlatformTime::Seconds();
#endif
		
		CacheElement->ReadRequest = nullptr;
		IBulkDataIORequest* LocalReadRequest = Chunk.BulkData.CreateStreamingRequest(0, ChunkDataSize, AsyncIOPriority | AIOP_FLAG_DONTCACHE, &AsyncFileCallBack, CacheElement->ChunkData);
		if (!LocalReadRequest)
		{
			UE_LOG(LogAudio, Error, TEXT("Chunk load in audio LRU cache failed."));
			OnLoadCompleted(EAudioChunkLoadResult::ChunkOutOfBounds);
			NumberOfLoadsInFlight.Decrement();
		}
		else if (FPlatformAtomics::InterlockedCompareExchangePointer((void* volatile*)&CacheElement->ReadRequest, LocalReadRequest, nullptr) == (void*)0x1)
		{
			// The request is completed before we can store it. Just delete it
			TGraphTask<FClearAudioChunkCacheReadRequestTask>::CreateTask().ConstructAndDispatchWhenReady(LocalReadRequest);
		}
	}
}

EAsyncIOPriorityAndFlags FAudioChunkCache::GetAsyncPriorityForChunk(const FChunkKey& InKey, bool bNeededForPlayback)
{

	// TODO: In the future we can add an enum to USoundWaves to tweak load priority of individual assets.

	if (bNeededForPlayback)
	{
		switch (PlaybackRequestPriorityCVar)
		{
		case 4:
		{
			return AIOP_MIN;
		}
		case 3:
		{
			return AIOP_Low;
		}
		case 2:
		{
			return AIOP_BelowNormal;
		}
		case 1:
		{
			return AIOP_Normal;
		}
		case 0:
		default:
		{
			return AIOP_High;
		}
		}
	}
	else
	{
		switch (ReadRequestPriorityCVar)
		{
		case 4:
		{
			return AIOP_MIN;
		}
		case 3:
		{
			return AIOP_Low;
		}
		case 2:
		{
			return AIOP_BelowNormal;
		}
		case 1:
		{
			return AIOP_Normal;
		}
		case 0:
		default:
		{
			return AIOP_High;
		}
		}
	}

}

void FAudioChunkCache::ExecuteOnLoadCompleteCallback(EAudioChunkLoadResult Result, const TFunction<void(EAudioChunkLoadResult)>& OnLoadCompleted, const ENamedThreads::Type& CallbackThread)
{
	if (CallbackThread == ENamedThreads::AnyThread)
	{
		OnLoadCompleted(Result);
	}
	else
	{
		// Dispatch an async notify.
		AsyncTask(CallbackThread, [OnLoadCompleted, Result]()
		{
			OnLoadCompleted(Result);
		});
	}
}

bool FAudioChunkCache::IsKeyValid(const FChunkKey& InKey)
{
	return InKey.ChunkIndex < TNumericLimits<uint32>::Max() && ((int32)InKey.ChunkIndex) < InKey.SoundWave->RunningPlatformData->Chunks.Num();
}

#include "UnrealEngine.h"

int32 FCachedAudioStreamingManager::RenderStatAudioStreaming(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation)
{
	Canvas->DrawShadowedString(X, Y, TEXT("Stream Caches:"), UEngine::GetSmallFont(), FLinearColor::White);
	Y += 12;

	int32 CacheIndex = 0;
	int32 Height = Y;
	for (const FAudioChunkCache& Cache : CacheArray)
	{
		FString CacheTitle = *FString::Printf(TEXT("Cache %d"), CacheIndex);
		Canvas->DrawShadowedString(X, Y, *CacheTitle, UEngine::GetSmallFont(), FLinearColor::White);
		Y += 12;

		TPair<int, int> Size = Cache.DebugDisplay(World, Viewport, Canvas, X, Y, ViewLocation, ViewRotation);

		// Separate caches are laid out horizontally across the screen, so the total height is equal to our tallest cache panel:
		X += Size.Key;
		Height = FMath::Max(Height, Size.Value);
	}

	return Y + Height;
}

FString FCachedAudioStreamingManager::GenerateMemoryReport()
{
	FString OutputString;
	for (FAudioChunkCache& Cache : CacheArray)
	{
		OutputString += Cache.DebugPrint();
	}

	return OutputString;
}

void FCachedAudioStreamingManager::SetProfilingMode(bool bEnabled)
{
	if (bEnabled)
	{
		for (FAudioChunkCache& Cache : CacheArray)
		{
			Cache.BeginLoggingCacheMisses();
		}
	}
	else
	{
		for (FAudioChunkCache& Cache : CacheArray)
		{
			Cache.StopLoggingCacheMisses();
		}
	}
}

uint64 FCachedAudioStreamingManager::TrimMemory(uint64 NumBytesToFree)
{
	uint64 NumBytesLeftToFree = NumBytesToFree;

	// TODO: When we support multiple caches, it's probably best to do this in reverse,
	// since the caches are sorted from shortest sounds to longest.
	// Freeing longer chunks will get us bigger gains and (presumably) have lower churn.
	for (FAudioChunkCache& Cache : CacheArray)
	{
		uint64 NumBytesFreed = Cache.TrimMemory(NumBytesLeftToFree);

		// NumBytesFreed could potentially be more than what we requested to free (since we delete whole chunks at once).
		NumBytesLeftToFree -= FMath::Min(NumBytesFreed, NumBytesLeftToFree);

		// If we've freed all the memory we needed to, exit.
		if (NumBytesLeftToFree == 0)
		{
			break;
		}
	}

	check(NumBytesLeftToFree <= NumBytesToFree);
	uint64 TotalBytesFreed = NumBytesToFree - NumBytesLeftToFree;

	UE_LOG(LogAudio, Display, TEXT("Call to IAudioStreamingManager::TrimMemory successfully freed %lu of the requested %lu bytes."), TotalBytesFreed, NumBytesToFree);
	return TotalBytesFreed;
}

#include "Engine/Font.h"

TPair<int, int> FAudioChunkCache::DebugDisplay(UWorld* World, FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const FVector* ViewLocation, const FRotator* ViewRotation) const
{
	FScopeLock ScopeLock(const_cast<FCriticalSection*>(&CacheMutationCriticalSection));

	// Color scheme:
	static constexpr float ColorMax = 256.0f;


	// Chunk color for a single retainer.
	const FLinearColor RetainChunkColor(44.0f / ColorMax, 207.0f / ColorMax, 47 / ColorMax);

	// Chunk color we lerp to as more retainers are added for a chunk.
	const FLinearColor TotalMassRetainChunkColor(204 / ColorMax, 126 / ColorMax, 43 / ColorMax);

	// A chunk that's loaded but not retained.
	const FLinearColor LoadedChunkColor(47 / ColorMax, 44 / ColorMax, 207 / ColorMax);

	// A chunk that's been trimmed by TrimMemory.
	const FLinearColor TrimmedChunkColor(204 / ColorMax, 46 / ColorMax, 43 / ColorMax);

	// In editor builds, this is a chunk that was built in a previous version of the cook quality settings.
	const FLinearColor StaleChunkColor(143 / ColorMax, 73 / ColorMax, 70 / ColorMax);

	// A chunk that currently has an async load in flight.
	const FLinearColor CurrentlyLoadingChunkColor = FLinearColor::Yellow;


	const int32 InitialX = X;
	const int32 InitialY = Y;

	FString NumElementsDetail = *FString::Printf(TEXT("Number of chunks loaded: %d of %d"), ChunksInUse, CachePool.Num());

	const int32 NumCacheOverflows = CacheOverflowCount.GetValue();
	FString CacheOverflowsDetail = *FString::Printf(TEXT("The cache has blown %d times)"), NumCacheOverflows);

	// Offset our number of elements loaded horizontally to the right next to the cache title:
	int32 CacheTitleOffsetX = 0;
	int32 CacheTitleOffsetY = 0;
	UEngine::GetSmallFont()->GetStringHeightAndWidth(TEXT("Cache XX "), CacheTitleOffsetY, CacheTitleOffsetX);

	Canvas->DrawShadowedString(X + CacheTitleOffsetX, Y - 12, *NumElementsDetail, UEngine::GetSmallFont(), FLinearColor::Green);
	Y += 10;

	Canvas->DrawShadowedString(X + CacheTitleOffsetX, Y - 12, *CacheOverflowsDetail, UEngine::GetSmallFont(), NumCacheOverflows != 0? FLinearColor::Red : FLinearColor::Green);
	Y += 10;

	// First pass: We run through and get a snap shot of the amount of memory currently in use.
	FCacheElement* CurrentElement = MostRecentElement;
	uint32 NumBytesCounter = 0;

	while (CurrentElement != nullptr)
	{
		// Note: this is potentially a stale value if we're in the middle of FCacheElement::KickOffAsyncLoad.
		NumBytesCounter += CurrentElement->ChunkDataSize;
		CurrentElement = CurrentElement->LessRecentElement;
	}

	// Convert to megabytes and print the total size:
	const double NumMegabytesInUse = (double)NumBytesCounter / (1024 * 1024);
	const double MaxCacheSizeMB = ((double)MemoryLimitBytes) / (1024 * 1024);

	FString CacheMemoryUsage = *FString::Printf(TEXT("Using: %.4f Megabytes (%lu bytes). Max Potential Usage: %.4f Megabytes."), NumMegabytesInUse, MemoryCounterBytes.Load(), MaxCacheSizeMB);

	// We're going to align this horizontally with the number of elements right above it.
	Canvas->DrawShadowedString(X + CacheTitleOffsetX, Y, *CacheMemoryUsage, UEngine::GetSmallFont(), FLinearColor::Green);
	Y += 12;

	// Second Pass: We're going to list the actual chunks in the cache.
	CurrentElement = MostRecentElement;
	int32 Index = 0;

	float ColorLerpAmount = 0.0f;
	const float ColorLerpStep = 0.04f;

	// More detailed info about individual chunks here:
	while (CurrentElement != nullptr)
	{
		// We use a CVar to clamp the max amount of chunks we display.
		if (Index > DebugMaxElementsDisplayCVar)
		{
			break;
		}

		int32 NumTotalChunks = -1;
		int32 NumTimesTouched = -1;
		double TimeToLoad = -1.0;
		float AveragePlaceInCache = -1.0f;
		ESoundWaveLoadingBehavior LoadingBehavior = ESoundWaveLoadingBehavior::Uninitialized;
		bool bLoadingBehaviorExternallyOverriden = false;
		bool bWasCacheMiss = false;
		bool bIsStaleChunk = false;

#if DEBUG_STREAM_CACHE
		NumTotalChunks = CurrentElement->DebugInfo.NumTotalChunks;
		NumTimesTouched = CurrentElement->DebugInfo.NumTimesTouched;
		TimeToLoad = CurrentElement->DebugInfo.TimeToLoad;
		AveragePlaceInCache = CurrentElement->DebugInfo.AverageLocationInCacheWhenNeeded;
		LoadingBehavior = CurrentElement->DebugInfo.LoadingBehavior;
		bLoadingBehaviorExternallyOverriden = CurrentElement->DebugInfo.bLoadingBehaviorExternallyOverriden;
		bWasCacheMiss = CurrentElement->DebugInfo.bWasCacheMiss;
#endif

#if WITH_EDITOR
		// TODO: Worry about whether the sound wave is alive here. In most editor cases this is ok because the soundwave will always be loaded, but this may not be the case in the future.
		bIsStaleChunk = (CurrentElement->Key.SoundWave == nullptr) || (CurrentElement->Key.SoundWave->CurrentChunkRevision.GetValue() != CurrentElement->Key.ChunkRevision);
#endif

		const bool bWasTrimmed = CurrentElement->ChunkDataSize == 0;

		FString ElementInfo = *FString::Printf(TEXT("%4i. Size: %6.2f KB   Chunk: %d of %d   Request Count: %d    Average Index: %6.2f  Number of Handles Retaining Chunk: %d     Chunk Load Time(in ms): %6.4fms      Loading Behavior: %s%s      Name: %s Notes: %s %s"),
			Index,
			CurrentElement->ChunkDataSize / 1024.0f,
			CurrentElement->Key.ChunkIndex,
			NumTotalChunks,
			NumTimesTouched,
			AveragePlaceInCache,
			CurrentElement->NumConsumers.GetValue(),
			TimeToLoad,
			EnumToString(LoadingBehavior),
			bLoadingBehaviorExternallyOverriden ? TEXT("*") : TEXT(""),
			bWasTrimmed ? TEXT("TRIMMED CHUNK") : *CurrentElement->Key.SoundWaveName.ToString(),
			bWasCacheMiss ? TEXT("(Cache Miss!)") : TEXT(""),
			bIsStaleChunk ? TEXT("(Stale Chunk)") : TEXT("")
		);

		// Since there's a lot of info here,
		// Subtly fading the chunk info to gray seems to help as a visual indicator of how far down on the list things are.
		ColorLerpAmount = FMath::Min(ColorLerpAmount + ColorLerpStep, 1.0f);
		FLinearColor TextColor;
		if (bIsStaleChunk)
		{
			TextColor = FLinearColor::LerpUsingHSV(StaleChunkColor, FLinearColor::Gray, ColorLerpAmount);
		}
		else
		{
			TextColor = FLinearColor::LerpUsingHSV(LoadedChunkColor, FLinearColor::Gray, ColorLerpAmount);
		}

		// If there's a load in flight, paint this element yellow.
		if (CurrentElement->IsLoadInProgress())
		{
			TextColor = FLinearColor::Yellow;
		}
		else if (CurrentElement->IsInUse())
		{
			// We slowly fade our text color based on how many refererences there are to this chunk.
			static const float MaxNumHandles = 12.0f;

			ColorLerpAmount = FMath::Min(CurrentElement->NumConsumers.GetValue() / MaxNumHandles, 1.0f);
			TextColor = FLinearColor::LerpUsingHSV(RetainChunkColor, TotalMassRetainChunkColor, ColorLerpAmount);
		}
		else if (bWasTrimmed)
		{
			TextColor = TrimmedChunkColor;
		}

		Canvas->DrawShadowedString(X, Y, *ElementInfo, UEngine::GetSmallFont(), TextColor);
		Y += 12;

		CurrentElement = CurrentElement->LessRecentElement;
		Index++;
	}

	// The largest element of our debug panel is the initial memory details.
	int32 CacheMemoryTextOffsetX = 0;
	int32 CacheMemoryTextOffsetY = 0;
	UEngine::GetSmallFont()->GetStringHeightAndWidth(*CacheMemoryUsage, CacheMemoryTextOffsetX, CacheMemoryTextOffsetY);

	return TPair<int, int>(X + CacheTitleOffsetX + CacheMemoryTextOffsetX - InitialX, Y - InitialY);
}

FString FAudioChunkCache::DebugPrint()
{
	FScopeLock ScopeLock(const_cast<FCriticalSection*>(&CacheMutationCriticalSection));

	FString OutputString;

	FString NumElementsDetail = *FString::Printf(TEXT("Number of chunks loaded: %d of %d"), ChunksInUse, CachePool.Num());
	FString NumCacheOverflows = *FString::Printf(TEXT("The cache has blown %d times"), CacheOverflowCount.GetValue());

	OutputString += NumElementsDetail + TEXT("\n");
	OutputString += NumCacheOverflows + TEXT("\n");

	// First pass: We run through and get a snap shot of the amount of memory currently in use.
	FCacheElement* CurrentElement = MostRecentElement;
	uint32 NumBytesCounter = 0;

	uint32 NumBytesRetained = 0;

	while (CurrentElement != nullptr)
	{
		// Note: this is potentially a stale value if we're in the middle of FCacheElement::KickOffAsyncLoad.
		NumBytesCounter += CurrentElement->ChunkDataSize;

		if (CurrentElement->IsInUse())
		{
			NumBytesRetained += CurrentElement->ChunkDataSize;
		}

		CurrentElement = CurrentElement->LessRecentElement;
	}

	// Convert to megabytes and print the total size:
	const double NumMegabytesInUse = (double)NumBytesCounter / (1024 * 1024);
	const double NumMegabytesRetained = (double)NumBytesRetained / (1024 * 1024);

	const double MaxCacheSizeMB = ((double)MemoryLimitBytes) / (1024 * 1024);
	const double PercentageOfCacheRetained = NumMegabytesRetained / MaxCacheSizeMB;

	FString CacheMemoryHeader = *FString::Printf(TEXT("Retaining:\t, Loaded:\t, Max Potential Usage:\t, \n"));
	FString CacheMemoryUsage = *FString::Printf(TEXT("%.4f Megabytes (%.3f of total capacity)\t,  %.4f Megabytes (%lu bytes)\t, %.4f Megabytes\t, \n"), NumMegabytesRetained, PercentageOfCacheRetained, NumMegabytesInUse, MemoryCounterBytes.Load(), MaxCacheSizeMB);

	OutputString += CacheMemoryHeader + CacheMemoryUsage + TEXT("\n");

	// Second Pass: We're going to list the actual chunks in the cache.
	CurrentElement = MostRecentElement;
	int32 Index = 0;

	OutputString += TEXT("Index:\t, Size (KB):\t, Chunk:\t, Request Count:\t, Average Index:\t, Number of Handles Retaining Chunk:\t, Chunk Load Time:\t, Name: \t, LoadingBehavior: \t, Notes:\t, \n");

	// More detailed info about individual chunks here:
	while (CurrentElement != nullptr)
	{
		int32 NumTotalChunks = -1;
		int32 NumTimesTouched = -1;
		double TimeToLoad = -1.0;
		float AveragePlaceInCache = -1.0f;
		ESoundWaveLoadingBehavior LoadingBehavior = ESoundWaveLoadingBehavior::Uninitialized;
		bool bLoadingBehaviorExternallyOverriden = false;
		bool bWasCacheMiss = false;
		bool bIsStaleChunk = false;

#if DEBUG_STREAM_CACHE
		NumTotalChunks = CurrentElement->DebugInfo.NumTotalChunks;
		NumTimesTouched = CurrentElement->DebugInfo.NumTimesTouched;
		TimeToLoad = CurrentElement->DebugInfo.TimeToLoad;
		AveragePlaceInCache = CurrentElement->DebugInfo.AverageLocationInCacheWhenNeeded;
		LoadingBehavior = CurrentElement->DebugInfo.LoadingBehavior;
		bLoadingBehaviorExternallyOverriden = CurrentElement->DebugInfo.bLoadingBehaviorExternallyOverriden;
		bWasCacheMiss = CurrentElement->DebugInfo.bWasCacheMiss;
#endif

#if WITH_EDITOR
		// TODO: Worry about whether the sound wave is alive here. In most editor cases this is ok because the soundwave will always be loaded, but this may not be the case in the future.
		bIsStaleChunk = (CurrentElement->Key.SoundWave == nullptr) || (CurrentElement->Key.SoundWave->CurrentChunkRevision.GetValue() != CurrentElement->Key.ChunkRevision);
#endif

		const bool bWasTrimmed = CurrentElement->ChunkDataSize == 0;

		FString ElementInfo = *FString::Printf(TEXT("%4i.\t, %6.2f\t, %d of %d\t, %d\t, %6.2f\t, %d\t,  %6.4f\t, %s\t, %s%s, %s %s %s"),
			Index,
			CurrentElement->ChunkDataSize / 1024.0f,
			CurrentElement->Key.ChunkIndex,
			NumTotalChunks,
			NumTimesTouched,
			AveragePlaceInCache,
			CurrentElement->NumConsumers.GetValue(),
			TimeToLoad,
			bWasTrimmed ? TEXT("TRIMMED CHUNK") : *CurrentElement->Key.SoundWaveName.ToString(),
			EnumToString(LoadingBehavior),
			bLoadingBehaviorExternallyOverriden ? TEXT("*") : TEXT(""),
			bWasCacheMiss ? TEXT("(Cache Miss!)") : TEXT(""),
			bIsStaleChunk ? TEXT("(Stale Chunk)") : TEXT(""),
			CurrentElement->IsLoadInProgress() ? TEXT("(Loading In Progress)") : TEXT("")
		);

		if (!bWasTrimmed)
		{
			OutputString += ElementInfo + TEXT("\n");
		}

		CurrentElement = CurrentElement->LessRecentElement;
		Index++;
	}

	OutputString += TEXT("Cache Miss Log:\n");
	OutputString += FlushCacheMissLog();

	return OutputString;
}
