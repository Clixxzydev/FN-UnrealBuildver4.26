// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaTexture.h"
#include "MediaAssetsPrivate.h"

#include "ExternalTexture.h"
#include "IMediaClock.h"
#include "IMediaClockSink.h"
#include "IMediaModule.h"
#include "MediaPlayerFacade.h"
#include "Modules/ModuleManager.h"
#include "RenderUtils.h"
#include "RenderingThread.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "MediaPlayer.h"
#include "IMediaPlayer.h"
#include "Misc/MediaTextureResource.h"
#include "IMediaTextureSample.h"


/* Local helpers
 *****************************************************************************/

/**
 * Media clock sink for media textures.
 */
class FMediaTextureClockSink
	: public IMediaClockSink
{
public:

	FMediaTextureClockSink(UMediaTexture& InOwner)
		: Owner(&InOwner)
	{ }

	virtual ~FMediaTextureClockSink() { }

public:

	virtual void TickRender(FTimespan DeltaTime, FTimespan Timecode) override
	{
		if (UMediaTexture* OwnerPtr = Owner.Get())
		{
			OwnerPtr->TickResource(Timecode);
		}
	}

private:

	TWeakObjectPtr<UMediaTexture> Owner;
};


/* UMediaTexture structors
 *****************************************************************************/

UMediaTexture::UMediaTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, AddressX(TA_Clamp)
	, AddressY(TA_Clamp)
	, AutoClear(false)
	, ClearColor(FLinearColor::Black)
	, EnableGenMips(false)
	, NumMips(1)
	, NewStyleOutput(false)
	, OutputFormat(MTOF_Default)
	, CurrentAspectRatio(0.0f)
	, CurrentOrientation(MTORI_Original)
	, DefaultGuid(FGuid::NewGuid())
	, Dimensions(FIntPoint::ZeroValue)
	, Size(0)
	, CachedNextSampleTime(FTimespan::MinValue())
{
	NeverStream = true;
	SRGB = true;
}


/* UMediaTexture interface
 *****************************************************************************/

float UMediaTexture::GetAspectRatio() const
{
	if (Dimensions.Y == 0)
	{
		return 0.0f;
	}

	return (float)(Dimensions.X) / Dimensions.Y;
}


int32 UMediaTexture::GetHeight() const
{
	return Dimensions.Y;
}


UMediaPlayer* UMediaTexture::GetMediaPlayer() const
{
	return CurrentPlayer.Get();
}


int32 UMediaTexture::GetWidth() const
{
	return Dimensions.X;
}


void UMediaTexture::SetMediaPlayer(UMediaPlayer* NewMediaPlayer)
{
	CurrentPlayer = NewMediaPlayer;
	UpdateQueue();
}


void UMediaTexture::CacheNextAvailableSampleTime(FTimespan InNextSampleTime)
{
	CachedNextSampleTime = InNextSampleTime;
}

#if WITH_EDITOR

void UMediaTexture::SetDefaultMediaPlayer(UMediaPlayer* NewMediaPlayer)
{
	MediaPlayer = NewMediaPlayer;
	CurrentPlayer = MediaPlayer;
}

#endif


/* UTexture interface
 *****************************************************************************/

FTextureResource* UMediaTexture::CreateResource()
{
	if (!ClockSink.IsValid())
	{
		IMediaModule* MediaModule = FModuleManager::LoadModulePtr<IMediaModule>("Media");

		if (MediaModule != nullptr)
		{
			ClockSink = MakeShared<FMediaTextureClockSink, ESPMode::ThreadSafe>(*this);
			MediaModule->GetClock().AddSink(ClockSink.ToSharedRef());
		}
	}

	Filter = (EnableGenMips && (NumMips > 1)) ? TF_Trilinear : TF_Bilinear;

	return new FMediaTextureResource(*this, Dimensions, Size, ClearColor, CurrentGuid.IsValid() ? CurrentGuid : DefaultGuid, EnableGenMips, NumMips);
}


EMaterialValueType UMediaTexture::GetMaterialType() const
{
	if (NewStyleOutput)
	{
		return MCT_Texture2D;
	}
	return EnableGenMips ? MCT_Texture2D : MCT_TextureExternal;
}


float UMediaTexture::GetSurfaceWidth() const
{
	return Dimensions.X;
}


float UMediaTexture::GetSurfaceHeight() const
{
	return Dimensions.Y;
}


FGuid UMediaTexture::GetExternalTextureGuid() const
{
	if (EnableGenMips)
	{
		return FGuid();
	}
	FScopeLock Lock(&CriticalSection);
	return CurrentRenderedGuid;
}

void UMediaTexture::SetRenderedExternalTextureGuid(const FGuid& InNewGuid)
{
	check(IsInRenderingThread());

	FScopeLock Lock(&CriticalSection);
	CurrentRenderedGuid = InNewGuid;
}

/* UObject interface
 *****************************************************************************/

void UMediaTexture::BeginDestroy()
{
	if (ClockSink.IsValid())
	{
		IMediaModule* MediaModule = FModuleManager::LoadModulePtr<IMediaModule>("Media");

		if (MediaModule != nullptr)
		{
			MediaModule->GetClock().RemoveSink(ClockSink.ToSharedRef());
		}

		ClockSink.Reset();
	}

	//Unregister the last rendered Guid
	const FGuid LastRendered = GetExternalTextureGuid();
	if (LastRendered.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(MediaTextureUnregisterGuid)(
			[LastRendered](FRHICommandList& RHICmdList)
			{
				FExternalTextureRegistry::Get().UnregisterExternalTexture(LastRendered);
			});
	}

	Super::BeginDestroy();
}


FString UMediaTexture::GetDesc()
{
	return FString::Printf(TEXT("%ix%i [%s]"), Dimensions.X,  Dimensions.Y, GPixelFormats[PF_B8G8R8A8].Name);
}


void UMediaTexture::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	CumulativeResourceSize.AddUnknownMemoryBytes(Size);
}


void UMediaTexture::PostLoad()
{
	Super::PostLoad();

	CurrentPlayer = MediaPlayer;
}

bool UMediaTexture::IsPostLoadThreadSafe() const
{
	return false;
}

#if WITH_EDITOR

void UMediaTexture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName AddressXName = GET_MEMBER_NAME_CHECKED(UMediaTexture, AddressX);
	static const FName AddressYName = GET_MEMBER_NAME_CHECKED(UMediaTexture, AddressY);
	static const FName AutoClearName = GET_MEMBER_NAME_CHECKED(UMediaTexture, AutoClear);
	static const FName ClearColorName = GET_MEMBER_NAME_CHECKED(UMediaTexture, ClearColor);
	static const FName MediaPlayerName = GET_MEMBER_NAME_CHECKED(UMediaTexture, MediaPlayer);

	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	
	if (PropertyThatChanged == nullptr)
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);

		return;
	}

	const FName PropertyName = PropertyThatChanged->GetFName();

	if (PropertyName == MediaPlayerName)
	{
		CurrentPlayer = MediaPlayer;
	}

	// don't update resource for these properties
	if ((PropertyName == AutoClearName) ||
		(PropertyName == ClearColorName) ||
		(PropertyName == MediaPlayerName))
	{
		UObject::PostEditChangeProperty(PropertyChangedEvent);

		return;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	// notify materials for these properties
	if ((PropertyName == AddressXName) ||
		(PropertyName == AddressYName))
	{
		NotifyMaterials();
	}
}

#endif // WITH_EDITOR


/* UMediaTexture implementation
 *****************************************************************************/

void UMediaTexture::TickResource(FTimespan Timecode)
{
	if (Resource == nullptr)
	{
		return;
	}

	const FGuid PreviousGuid = CurrentGuid;

	// media player bookkeeping
	if (CurrentPlayer.IsValid())
	{
		UpdateQueue();
	}
	else if (CurrentGuid != DefaultGuid)
	{
		SampleQueue.Reset();
		CurrentGuid = DefaultGuid;
	}
	else if ((LastClearColor == ClearColor) && (LastSrgb == SRGB))
	{
		return; // nothing to render
	}

	LastClearColor = ClearColor;
	LastSrgb = SRGB;

	// set up render parameters
	FMediaTextureResource::FRenderParams RenderParams;

	if (UMediaPlayer* CurrentPlayerPtr = CurrentPlayer.Get())
	{
		const bool PlayerActive = CurrentPlayerPtr->IsPaused() || CurrentPlayerPtr->IsPlaying() || CurrentPlayerPtr->IsPreparing();

		if (PlayerActive)
		{
			check(CurrentPlayerPtr->GetPlayerFacade()->GetPlayer());

			if (CurrentPlayerPtr->GetPlayerFacade()->GetPlayer()->GetPlayerFeatureFlag(IMediaPlayer::EFeatureFlag::UsePlaybackTimingV2))
			{
				/*
					We are using the old-style "1sample queue to sink" architecture to actually just pass long only ONE sample at a time from the logic
					inside the player facade to the sinks. The selection as to what to render this frame is expected to be done earlier
					this frame on the gamethread, hence only a single output frame is selected and passed along...
				*/
				TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Sample;
				while (SampleQueue->Dequeue(Sample))
					;

				if (!Sample.IsValid())
				{
					// Player is active (do not clear), but we have no new data
					// -> we do not need to trigger anything on the renderthread
					return;
				}

				UpdateSampleInfo(Sample);

				RenderParams.TextureSample = Sample;

				RenderParams.Rate = CurrentPlayerPtr->GetRate();
				RenderParams.Time = Sample->GetTime();

				if (NewStyleOutput)
				{
					// For new-style output the sample's sRGB state controls what we output
					// (FOR NOW: this is too simplified if we have more then Rec703 material)
					SRGB = Sample->IsOutputSrgb();
					// Ensure sRGB changes will not trigger rendering the next time around
					LastSrgb = SRGB;
				}
			}
			else
			{
				//
				// Old style: pass queue along and dequeue only at render time
				//
				TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Sample;
				if (SampleQueue->Peek(Sample))
				{
					UpdateSampleInfo(Sample);
				}

				RenderParams.SampleSource = SampleQueue;

				RenderParams.Rate = CurrentPlayerPtr->GetRate();
				RenderParams.Time = CurrentPlayerPtr->GetTime();
			}
		}
		else 
		{
			CurrentAspectRatio = 0.0f;
			CurrentOrientation = MTORI_Original;

			if (!AutoClear)
			{
				return; // retain last frame
			}
		}
	}
	else if (!AutoClear && (CurrentGuid == PreviousGuid))
	{
		return; // retain last frame
	}

	// update filter state, responding to mips setting
	Filter = (EnableGenMips && (NumMips > 1)) ? TF_Trilinear : TF_Bilinear;

	// setup render parameters
	RenderParams.CanClear = AutoClear;
	RenderParams.ClearColor = ClearColor;
	RenderParams.PreviousGuid = PreviousGuid;
	RenderParams.CurrentGuid = CurrentGuid;
	RenderParams.SrgbOutput = SRGB;
	RenderParams.NumMips = NumMips;
	
	// redraw texture resource on render thread
	FMediaTextureResource* ResourceParam = (FMediaTextureResource*)Resource;
	ENQUEUE_RENDER_COMMAND(MediaTextureResourceRender)(
		[ResourceParam, RenderParams](FRHICommandListImmediate& RHICmdList)
		{
			ResourceParam->Render(RenderParams);
		});
}

void UMediaTexture::UpdateSampleInfo(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> & Sample)
{
	CurrentAspectRatio = (float)Sample->GetAspectRatio();
	switch (Sample->GetOrientation())
	{
		case EMediaOrientation::Original: CurrentOrientation = MTORI_Original; break;
		case EMediaOrientation::CW90: CurrentOrientation = MTORI_CW90; break;
		case EMediaOrientation::CW180: CurrentOrientation = MTORI_CW180; break;
		case EMediaOrientation::CW270: CurrentOrientation = MTORI_CW270; break;
		default: CurrentOrientation = MTORI_Original; break;
	}
}

void UMediaTexture::UpdateQueue()
{
	if (UMediaPlayer* CurrentPlayerPtr = CurrentPlayer.Get())
	{
		const FGuid PlayerGuid = CurrentPlayerPtr->GetGuid();

		if (CurrentGuid != PlayerGuid)
		{
			SampleQueue = MakeShared<FMediaTextureSampleQueue, ESPMode::ThreadSafe>();
			CurrentPlayerPtr->GetPlayerFacade()->AddVideoSampleSink(SampleQueue.ToSharedRef());
			CurrentGuid = PlayerGuid;
		}
	}
	else
	{
		SampleQueue.Reset();
	}
}

FTimespan UMediaTexture::GetNextSampleTime() const
{
	return CachedNextSampleTime;
}

int32 UMediaTexture::GetAvailableSampleCount() const
{
	return SampleQueue->Num();
}

float UMediaTexture::GetCurrentAspectRatio() const
{
	return CurrentAspectRatio;
}

MediaTextureOrientation UMediaTexture::GetCurrentOrientation() const
{
	return CurrentOrientation;
}
