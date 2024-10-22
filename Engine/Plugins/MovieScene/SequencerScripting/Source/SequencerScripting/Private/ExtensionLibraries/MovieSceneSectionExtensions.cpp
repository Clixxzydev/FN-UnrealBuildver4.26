// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtensionLibraries/MovieSceneSectionExtensions.h"
#include "SequencerScriptingRange.h"
#include "MovieSceneSection.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneChannelHandle.h"
#include "KeysAndChannels/MovieSceneScriptingBool.h"
#include "KeysAndChannels/MovieSceneScriptingByte.h"
#include "KeysAndChannels/MovieSceneScriptingInteger.h"
#include "KeysAndChannels/MovieSceneScriptingFloat.h"
#include "KeysAndChannels/MovieSceneScriptingString.h"
#include "KeysAndChannels/MovieSceneScriptingEvent.h"
#include "KeysAndChannels/MovieSceneScriptingActorReference.h"
#include "KeysAndChannels/MovieSceneScriptingObjectPath.h"
#include "Sections/MovieSceneSubSection.h"


FSequencerScriptingRange UMovieSceneSectionExtensions::GetRange(UMovieSceneSection* Section)
{
	UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>();
	return FSequencerScriptingRange::FromNative(Section->GetRange(), MovieScene->GetTickResolution());
}

bool UMovieSceneSectionExtensions::HasStartFrame(UMovieSceneSection* Section)
{
	return Section->HasStartFrame();
}

int32 UMovieSceneSectionExtensions::GetStartFrame(UMovieSceneSection* Section)
{
	if (!Section->HasStartFrame())
	{
		FFrame::KismetExecutionMessage(TEXT("Section does not have a start frame"), ELogVerbosity::Error);
		return -1;
	}

	UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>();
	if (MovieScene)
	{
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();
		return ConvertFrameTime(UE::MovieScene::DiscreteInclusiveLower(Section->GetRange()), MovieScene->GetTickResolution(), DisplayRate).FloorToFrame().Value;
	}
	else
	{
		return -1;
	}
}

float UMovieSceneSectionExtensions::GetStartFrameSeconds(UMovieSceneSection* Section)
{
	if (!Section->HasStartFrame())
	{
		FFrame::KismetExecutionMessage(TEXT("Section does not have a start frame"), ELogVerbosity::Error);
		return -1.f;
	}

	UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>();
	if (MovieScene)
	{
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();
		return DisplayRate.AsSeconds(ConvertFrameTime(UE::MovieScene::DiscreteInclusiveLower(Section->GetRange()), MovieScene->GetTickResolution(), DisplayRate));
	}

	return -1.f;
}

bool UMovieSceneSectionExtensions::HasEndFrame(UMovieSceneSection* Section)
{
	return Section->HasEndFrame();
}


int32 UMovieSceneSectionExtensions::GetEndFrame(UMovieSceneSection* Section)
{
	if (!Section->HasEndFrame())
	{
		FFrame::KismetExecutionMessage(TEXT("Section does not have an end frame"), ELogVerbosity::Error);
		return -1;
	}

	UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>();
	if (MovieScene)
	{
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();
		return ConvertFrameTime(UE::MovieScene::DiscreteExclusiveUpper(Section->GetRange()), MovieScene->GetTickResolution(), DisplayRate).FloorToFrame().Value;
	}
	else
	{
		return -1;
	}
}

float UMovieSceneSectionExtensions::GetEndFrameSeconds(UMovieSceneSection* Section)
{
	if (!Section->HasEndFrame())
	{
		FFrame::KismetExecutionMessage(TEXT("Section does not have an end frame"), ELogVerbosity::Error);
		return -1.f;
	}

	UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>();
	if (MovieScene)
	{
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();
		return DisplayRate.AsSeconds(ConvertFrameTime(UE::MovieScene::DiscreteExclusiveUpper(Section->GetRange()), MovieScene->GetTickResolution(), DisplayRate));
	}

	return -1.f;
}

void UMovieSceneSectionExtensions::SetRange(UMovieSceneSection* Section, int32 StartFrame, int32 EndFrame)
{
	UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>();
	if (MovieScene)
	{
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();

		TRange<FFrameNumber> NewRange;
		NewRange.SetLowerBound(TRangeBound<FFrameNumber>::Inclusive(ConvertFrameTime(StartFrame, DisplayRate, MovieScene->GetTickResolution()).FrameNumber));
		NewRange.SetUpperBound(TRangeBound<FFrameNumber>::Exclusive(ConvertFrameTime(EndFrame, DisplayRate, MovieScene->GetTickResolution()).FrameNumber));

		if (NewRange.GetLowerBound().IsOpen() || NewRange.GetUpperBound().IsOpen() || NewRange.GetLowerBoundValue() <= NewRange.GetUpperBoundValue())
		{
			Section->SetRange(NewRange);
		}
		else
		{
			FFrame::KismetExecutionMessage(TEXT("Invalid range specified"), ELogVerbosity::Error);
		}
	}
}

void UMovieSceneSectionExtensions::SetRangeSeconds(UMovieSceneSection* Section, float StartTime, float EndTime)
{
	UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>();
	if (MovieScene)
	{
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();

		TRange<FFrameNumber> NewRange;
		NewRange.SetLowerBound((StartTime * MovieScene->GetTickResolution()).RoundToFrame());
		NewRange.SetUpperBound((EndTime * MovieScene->GetTickResolution()).RoundToFrame());

		if (NewRange.GetLowerBound().IsOpen() || NewRange.GetUpperBound().IsOpen() || NewRange.GetLowerBoundValue() <= NewRange.GetUpperBoundValue())
		{
			Section->SetRange(NewRange);
		}
		else
		{
			FFrame::KismetExecutionMessage(TEXT("Invalid range specified"), ELogVerbosity::Error);
		}
	}
}

void UMovieSceneSectionExtensions::SetStartFrame(UMovieSceneSection* Section, int32 StartFrame)
{
	UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>();
	if (MovieScene)
	{
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();

		Section->SetStartFrame(ConvertFrameTime(StartFrame, DisplayRate, MovieScene->GetTickResolution()).FrameNumber);
	}
}

void UMovieSceneSectionExtensions::SetStartFrameSeconds(UMovieSceneSection* Section, float StartTime)
{
	UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>();
	if (MovieScene)
	{
		Section->SetStartFrame((StartTime * MovieScene->GetTickResolution()).RoundToFrame());
	}
}

void UMovieSceneSectionExtensions::SetStartFrameBounded(UMovieSceneSection* Section, bool bIsBounded)
{
	UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>();
	if (MovieScene)
	{
		if (bIsBounded)
		{
			int32 NewFrameNumber = 0;
			if (!MovieScene->GetPlaybackRange().GetLowerBound().IsOpen())
			{
				NewFrameNumber = MovieScene->GetPlaybackRange().GetLowerBoundValue().Value;
			}

			Section->SectionRange.Value.SetLowerBound(TRangeBound<FFrameNumber>(FFrameNumber(NewFrameNumber)));
		}
		else
		{
			Section->SectionRange.Value.SetLowerBound(TRangeBound<FFrameNumber>());
		}
	}
}

void UMovieSceneSectionExtensions::SetEndFrame(UMovieSceneSection* Section, int32 EndFrame)
{
	UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>();
	if (MovieScene)
	{
		FFrameRate DisplayRate = MovieScene->GetDisplayRate();

		Section->SetEndFrame(TRangeBound<FFrameNumber>::Exclusive(ConvertFrameTime(EndFrame, DisplayRate, MovieScene->GetTickResolution()).FrameNumber));
	}
}

void UMovieSceneSectionExtensions::SetEndFrameSeconds(UMovieSceneSection* Section, float EndTime)
{
	UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>();
	if (MovieScene)
	{
		Section->SetEndFrame(TRangeBound<FFrameNumber>::Exclusive((EndTime * MovieScene->GetTickResolution()).RoundToFrame()));
	}
}

void UMovieSceneSectionExtensions::SetEndFrameBounded(UMovieSceneSection* Section, bool bIsBounded)
{
	UMovieScene* MovieScene = Section->GetTypedOuter<UMovieScene>();
	if (MovieScene)
	{
		if (bIsBounded)
		{
			int32 NewFrameNumber = 0;
			if (!MovieScene->GetPlaybackRange().GetUpperBound().IsOpen())
			{
				NewFrameNumber = MovieScene->GetPlaybackRange().GetUpperBoundValue().Value;
			}

			Section->SectionRange.Value.SetUpperBound(TRangeBound<FFrameNumber>(FFrameNumber(NewFrameNumber)));
		}
		else
		{
			Section->SectionRange.Value.SetUpperBound(TRangeBound<FFrameNumber>());
		}
	}
}

template<typename ChannelType, typename ScriptingChannelType>
void GetScriptingChannelsForChannel(FMovieSceneChannelProxy& ChannelProxy, TWeakObjectPtr<UMovieSceneSequence> Sequence, TArray<UMovieSceneScriptingChannel*>& OutChannels)
{
	const FMovieSceneChannelEntry* Entry = ChannelProxy.FindEntry(ChannelType::StaticStruct()->GetFName());
	if (Entry)
	{
		TArrayView<const FMovieSceneChannelMetaData> MetaData = Entry->GetMetaData();
		for (int32 Index = 0; Index < MetaData.Num(); ++Index)
		{
			if (MetaData[Index].bEnabled)
			{
				const FName ChannelName = MetaData[Index].Name;
				ScriptingChannelType* ScriptingChannel = NewObject<ScriptingChannelType>(GetTransientPackage(), ChannelName);
				ScriptingChannel->ChannelHandle = ChannelProxy.MakeHandle<ChannelType>(Index);
				ScriptingChannel->OwningSequence = Sequence;

				OutChannels.Add(ScriptingChannel);
			}
		}
	}
}

TArray<UMovieSceneScriptingChannel*> UMovieSceneSectionExtensions::GetChannels(UMovieSceneSection* Section)
{
	TArray<UMovieSceneScriptingChannel*> Channels;
	if (!Section)
	{
		FFrame::KismetExecutionMessage(TEXT("Cannot get channels for null section"), ELogVerbosity::Error);
		return Channels;
	}
	FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();

	// ToDo: This isn't a great way of collecting all of the channel types and instantiating the correct UObject for it
	// but given that we're already hard-coding the UObject implementations... We currently support the following channel
	// types: Bool, Byte, Float, Integer, String, Event, ActorReference
	TWeakObjectPtr<UMovieSceneSequence> Sequence = Section->GetTypedOuter<UMovieSceneSequence>();
	GetScriptingChannelsForChannel<FMovieSceneBoolChannel, UMovieSceneScriptingBoolChannel>(ChannelProxy, Sequence, Channels);
	GetScriptingChannelsForChannel<FMovieSceneByteChannel, UMovieSceneScriptingByteChannel>(ChannelProxy, Sequence, Channels);
	GetScriptingChannelsForChannel<FMovieSceneFloatChannel, UMovieSceneScriptingFloatChannel>(ChannelProxy, Sequence, Channels);
	GetScriptingChannelsForChannel<FMovieSceneIntegerChannel, UMovieSceneScriptingIntegerChannel>(ChannelProxy, Sequence, Channels);
	GetScriptingChannelsForChannel<FMovieSceneStringChannel, UMovieSceneScriptingStringChannel>(ChannelProxy, Sequence, Channels);
	GetScriptingChannelsForChannel<FMovieSceneEventChannel, UMovieSceneScriptingEventChannel>(ChannelProxy, Sequence, Channels);
	GetScriptingChannelsForChannel<FMovieSceneActorReferenceData, UMovieSceneScriptingActorReferenceChannel>(ChannelProxy, Sequence, Channels);
	GetScriptingChannelsForChannel<FMovieSceneObjectPathChannel, UMovieSceneScriptingObjectPathChannel>(ChannelProxy, Sequence, Channels);

	return Channels;
}

TArray<UMovieSceneScriptingChannel*> UMovieSceneSectionExtensions::FindChannelsByType(UMovieSceneSection* Section, TSubclassOf<UMovieSceneScriptingChannel> ChannelType)
{
	TArray<UMovieSceneScriptingChannel*> Channels;
	if (!Section)
	{
		FFrame::KismetExecutionMessage(TEXT("Cannot get channels for null section"), ELogVerbosity::Error);
		return Channels;
	}
 
	FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
 
	// ToDo: This needs to be done dynamically in the future but there's not a good way to do that right now with all of the SFINAE-based Templating driving these channels
	TWeakObjectPtr<UMovieSceneSequence> Sequence = Section->GetTypedOuter<UMovieSceneSequence>();

	if (ChannelType == UMovieSceneScriptingBoolChannel::StaticClass())					{ GetScriptingChannelsForChannel<FMovieSceneBoolChannel, UMovieSceneScriptingBoolChannel>(ChannelProxy, Sequence, Channels); }
	else if (ChannelType == UMovieSceneScriptingByteChannel::StaticClass())				{ GetScriptingChannelsForChannel<FMovieSceneByteChannel, UMovieSceneScriptingByteChannel>(ChannelProxy, Sequence, Channels); }
	else if (ChannelType == UMovieSceneScriptingFloatChannel::StaticClass())			{ GetScriptingChannelsForChannel<FMovieSceneFloatChannel, UMovieSceneScriptingFloatChannel>(ChannelProxy, Sequence, Channels); }
	else if (ChannelType == UMovieSceneScriptingIntegerChannel::StaticClass())			{ GetScriptingChannelsForChannel<FMovieSceneIntegerChannel, UMovieSceneScriptingIntegerChannel>(ChannelProxy, Sequence, Channels); }
	else if (ChannelType == UMovieSceneScriptingStringChannel::StaticClass())			{ GetScriptingChannelsForChannel<FMovieSceneStringChannel, UMovieSceneScriptingStringChannel>(ChannelProxy, Sequence, Channels); }
	else if (ChannelType == UMovieSceneScriptingEventChannel::StaticClass())			{ GetScriptingChannelsForChannel<FMovieSceneEventChannel, UMovieSceneScriptingEventChannel>(ChannelProxy, Sequence, Channels); }
	else if (ChannelType == UMovieSceneScriptingActorReferenceChannel::StaticClass()) { GetScriptingChannelsForChannel<FMovieSceneActorReferenceData, UMovieSceneScriptingActorReferenceChannel>(ChannelProxy, Sequence, Channels); }
	else if (ChannelType == UMovieSceneScriptingObjectPathChannel::StaticClass()) { GetScriptingChannelsForChannel<FMovieSceneObjectPathChannel, UMovieSceneScriptingObjectPathChannel>(ChannelProxy, Sequence, Channels); }
	else
	{
		FFrame::KismetExecutionMessage(TEXT("Unsupported ChannelType for FindChannelsByType!"), ELogVerbosity::Error);
	}


	return Channels;
}


bool GetSubSectionChain(UMovieSceneSubSection* InSubSection, UMovieSceneSequence* ParentSequence, TArray<UMovieSceneSubSection*>& SubSectionChain)
{
	UMovieScene* ParentMovieScene = ParentSequence->GetMovieScene();
	for (UMovieSceneTrack* MasterTrack : ParentMovieScene->GetMasterTracks())
	{
		for (UMovieSceneSection* Section : MasterTrack->GetAllSections())
		{
			if (Section == InSubSection)
			{
				SubSectionChain.Add(InSubSection);
				return true;
			}
			if (Section->IsA(UMovieSceneSubSection::StaticClass()))
			{
				UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section);
				if (GetSubSectionChain(InSubSection, SubSection->GetSequence(), SubSectionChain))
				{
					SubSectionChain.Add(SubSection);
				}
			}
		}
	}
	return false;
}


int32 UMovieSceneSectionExtensions::GetParentSequenceFrame(UMovieSceneSubSection* InSubSection, int32 InFrame, UMovieSceneSequence* ParentSequence)
{
	if (InSubSection == nullptr || ParentSequence == nullptr)
	{
		UE_LOG(LogMovieScene, Error, TEXT("UMovieSceneSectionExtension::GetParentSequenceFrame failed because either sub section or parent sequence is null! SubSection: %s ParentSequence: %s"), InSubSection ? *InSubSection->GetFullName() : TEXT("None"), ParentSequence ? *ParentSequence->GetFullName() : TEXT("None"));
		return InFrame;
	}

	TArray<UMovieSceneSubSection*> SubSectionChain;
	GetSubSectionChain(InSubSection, ParentSequence, SubSectionChain);
		
	FFrameRate LocalDisplayRate = InSubSection->GetSequence()->GetMovieScene()->GetDisplayRate();
	FFrameRate LocalTickResolution = InSubSection->GetSequence()->GetMovieScene()->GetTickResolution();
	FFrameTime LocalFrameTime = ConvertFrameTime(InFrame, LocalDisplayRate, LocalTickResolution);
		
	for (int32 SectionIndex = 0; SectionIndex < SubSectionChain.Num(); ++SectionIndex)
	{
		LocalFrameTime = LocalFrameTime * SubSectionChain[SectionIndex]->OuterToInnerTransform().InverseLinearOnly();
	}

	FFrameRate ParentDisplayRate = ParentSequence->GetMovieScene()->GetDisplayRate();
	FFrameRate ParentTickResolution = ParentSequence->GetMovieScene()->GetTickResolution();

	LocalFrameTime = ConvertFrameTime(LocalFrameTime, ParentTickResolution, ParentDisplayRate);
	return LocalFrameTime.GetFrame().Value;
}
