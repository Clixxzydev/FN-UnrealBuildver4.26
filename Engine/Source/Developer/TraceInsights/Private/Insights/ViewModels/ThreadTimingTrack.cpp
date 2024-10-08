// Copyright Epic Games, Inc. All Rights Reserved.

#include "ThreadTimingTrack.h"

#include "CborReader.h"
#include "Fonts/FontMeasure.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Serialization/MemoryReader.h"
#include "Styling/SlateBrush.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/ITimingViewSession.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/TimerNode.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingEventSearch.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "Insights/Widgets/STimingView.h"

#define LOCTEXT_NAMESPACE "ThreadTimingTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////

static void AppendMetadataToTooltip(FTooltipDrawState& Tooltip, TArrayView<const uint8>& Metadata)
{
	FMemoryReaderView MemoryReader(Metadata);
	FCborReader CborReader(&MemoryReader, ECborEndianness::StandardCompliant);
	FCborContext Context;

	if (!CborReader.ReadNext(Context) || Context.MajorType() != ECborCode::Map)
	{
		return;
	}

	Tooltip.AddTitle(TEXT("Metadata:"));

	while (true)
	{
		// Read key
		if (!CborReader.ReadNext(Context) || !Context.IsString())
		{
			break;
		}

		FString Key(Context.AsLength(), Context.AsCString());
		Key += TEXT(":");

		if (!CborReader.ReadNext(Context))
		{
			break;
		}

		switch (Context.MajorType())
		{
		case ECborCode::Int:
		case ECborCode::Uint:
			{
				uint64 Value = Context.AsUInt();
				FString ValueStr = FString::Printf(TEXT("%llu"), Value);
				Tooltip.AddNameValueTextLine(Key, ValueStr);
				continue;
			}

		case ECborCode::TextString:
			{
				FString Value = Context.AsString();
				Tooltip.AddNameValueTextLine(Key, Value);
				continue;
			}

		case ECborCode::ByteString:
			{
				FAnsiStringView Value(Context.AsCString(), Context.AsLength());
				FString ValueStr(Value);
				Tooltip.AddNameValueTextLine(Key, ValueStr);
				continue;
			}
		}

		if (Context.RawCode() == (ECborCode::Prim|ECborCode::Value_4Bytes))
		{
			float Value = Context.AsFloat();
			FString ValueStr = FString::Printf(TEXT("%f"), Value);
			Tooltip.AddNameValueTextLine(Key, ValueStr);
			continue;
		}

		if (Context.RawCode() == (ECborCode::Prim|ECborCode::Value_8Bytes))
		{
			double Value = Context.AsDouble();
			FString ValueStr = FString::Printf(TEXT("%g"), Value);
			Tooltip.AddNameValueTextLine(Key, ValueStr);
			continue;
		}

		if (Context.IsFiniteContainer())
		{
			CborReader.SkipContainer(ECborCode::Array);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static void AppendMetadataToString(FString& Str, TArrayView<const uint8>& Metadata)
{
	FMemoryReaderView MemoryReader(Metadata);
	FCborReader CborReader(&MemoryReader, ECborEndianness::StandardCompliant);
	FCborContext Context;

	if (!CborReader.ReadNext(Context) || Context.MajorType() != ECborCode::Map)
	{
		return;
	}

	bool bFirst = true;

	while (true)
	{
		// Read key
		if (!CborReader.ReadNext(Context) || !Context.IsString())
		{
			break;
		}

		if (bFirst)
		{
			bFirst = false;
			Str += TEXT(" - ");
		}
		else
		{
			Str += TEXT(", ");
		}

		//FString Key(Context.AsLength(), Context.AsCString());
		//Str += Key;
		//Str += TEXT(":");

		if (!CborReader.ReadNext(Context))
		{
			break;
		}

		switch (Context.MajorType())
		{
		case ECborCode::Int:
		case ECborCode::Uint:
			{
				uint64 Value = Context.AsUInt();
				Str += FString::Printf(TEXT("%llu"), Value);
				continue;
			}

		case ECborCode::TextString:
			{
				Str += Context.AsString();
				continue;
			}

		case ECborCode::ByteString:
			{
				Str.AppendChars(Context.AsCString(), Context.AsLength());
				continue;
			}
		}

		if (Context.RawCode() == (ECborCode::Prim | ECborCode::Value_4Bytes))
		{
			float Value = Context.AsFloat();
			Str += FString::Printf(TEXT("%f"), Value);
			continue;
		}

		if (Context.RawCode() == (ECborCode::Prim | ECborCode::Value_8Bytes))
		{
			double Value = Context.AsDouble();
			Str += FString::Printf(TEXT("%g"), Value);
			continue;
		}

		if (Context.IsFiniteContainer())
		{
			CborReader.SkipContainer(ECborCode::Array);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static void AddTimingEventToBuilder(ITimingEventsTrackDrawStateBuilder& Builder,
									double EventStartTime, double EventEndTime, uint32 EventDepth,
									uint32 TimerIndex, const Trace::FTimingProfilerTimer* Timer)
{
	//const uint32 EventColor = FTimingEvent::ComputeEventColor(Timer->Id);
	const uint32 EventColor = FTimingEvent::ComputeEventColor(Timer->Name);

	Builder.AddEvent(EventStartTime, EventEndTime, EventDepth, EventColor,
		[TimerIndex, Timer, EventStartTime, EventEndTime](float Width)
		{
			FString EventName = Timer->Name;

			if (Width > EventName.Len() * 4.0f + 32.0f)
			{
				//EventName = TEXT("*") + EventName; // for debugging

				const double Duration = EventEndTime - EventStartTime;
				FTimingEventsTrackDrawStateBuilder::AppendDurationToEventName(EventName, Duration);

				if (int32(TimerIndex) < 0) // has metadata?
				{
					//EventName = TEXT("!") + EventName; // for debugging

					TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
					check(Session.IsValid());

					//Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

					const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

					const Trace::ITimingProfilerTimerReader* TimerReader;
					TimingProfilerProvider.ReadTimers([&TimerReader](const Trace::ITimingProfilerTimerReader& Out) { TimerReader = &Out; });

					TArrayView<const uint8> Metadata = TimerReader->GetMetadata(TimerIndex);
					if (Metadata.Num() > 0)
					{
						AppendMetadataToString(EventName, Metadata);
					}
				}
			}

			return EventName;
		});
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FThreadTimingSharedState
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FCpuTimingTrack> FThreadTimingSharedState::GetCpuTrack(uint32 InThreadId)
{
	TSharedPtr<FCpuTimingTrack>*const TrackPtrPtr = CpuTracks.Find(InThreadId);
	return TrackPtrPtr ? *TrackPtrPtr : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingSharedState::IsGpuTrackVisible() const
{
	return GpuTrack != nullptr && GpuTrack->IsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingSharedState::IsCpuTrackVisible(uint32 InThreadId) const
{
	const TSharedPtr<FCpuTimingTrack>*const TrackPtrPtr = CpuTracks.Find(InThreadId);
	return TrackPtrPtr && (*TrackPtrPtr)->IsVisible();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::OnBeginSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	if (TimingView && TimingView->IsAssetLoadingModeEnabled())
	{
		bShowHideAllGpuTracks = false;
		bShowHideAllCpuTracks = false;
	}
	else
	{
		bShowHideAllGpuTracks = true;
		bShowHideAllCpuTracks = true;
	}

	GpuTrack = nullptr;
	CpuTracks.Reset();
	ThreadGroups.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::OnEndSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	bShowHideAllGpuTracks = false;
	bShowHideAllCpuTracks = false;

	GpuTrack = nullptr;
	CpuTracks.Reset();
	ThreadGroups.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::Tick(Insights::ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	const Trace::ITimingProfilerProvider* TimingProfilerProvider = Trace::ReadTimingProfilerProvider(InAnalysisSession);
	if (TimingProfilerProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(InAnalysisSession);

		const Trace::ILoadTimeProfilerProvider* LoadTimeProfilerProvider = Trace::ReadLoadTimeProfilerProvider(InAnalysisSession);

		// Check if we have a GPU track.
		if (!GpuTrack.IsValid())
		{
			uint32 GpuTimelineIndex;
			if (TimingProfilerProvider->GetGpuTimelineIndex(GpuTimelineIndex))
			{
				GpuTrack = MakeShared<FGpuTimingTrack>(*this, TEXT("GPU"), nullptr, GpuTimelineIndex, 0);
				GpuTrack->SetOrder(FTimingTrackOrder::Gpu);
				GpuTrack->SetVisibilityFlag(bShowHideAllGpuTracks);
				InSession.AddScrollableTrack(GpuTrack);
			}
		}

		bool bTracksOrderChanged = false;
		int32 Order = FTimingTrackOrder::Cpu;

		// Iterate through threads.
		const Trace::IThreadProvider& ThreadProvider = Trace::ReadThreadProvider(InAnalysisSession);
		ThreadProvider.EnumerateThreads([this, &InSession, &bTracksOrderChanged, &Order, TimingProfilerProvider, LoadTimeProfilerProvider](const Trace::FThreadInfo& ThreadInfo)
		{
			// Check if this thread is part of a group?
			bool bIsGroupVisible = bShowHideAllCpuTracks;
			const TCHAR* const GroupName = ThreadInfo.GroupName ? ThreadInfo.GroupName : ThreadInfo.Name;
			if (GroupName != nullptr)
			{
				if (!ThreadGroups.Contains(GroupName))
				{
					//UE_LOG(TimingProfiler, Log, TEXT("New CPU Thread Group (%d) : \"%s\""), ThreadGroups.Num() + 1, GroupName);
					ThreadGroups.Add(GroupName, { GroupName, bIsGroupVisible, 0, Order });
				}
				else
				{
					FThreadGroup& ThreadGroup = ThreadGroups[GroupName];
					bIsGroupVisible = ThreadGroup.bIsVisible;
					ThreadGroup.Order = Order;
				}
			}

			// Check if there is an available Asset Loading track for this thread.
			bool bIsLoadingThread = false;
			uint32 LoadingTimelineIndex;
			if (LoadTimeProfilerProvider && LoadTimeProfilerProvider->GetCpuThreadTimelineIndex(ThreadInfo.Id, LoadingTimelineIndex))
			{
				bIsLoadingThread = true;
			}

			// Check if there is an available CPU track for this thread.
			uint32 CpuTimelineIndex;
			if (TimingProfilerProvider->GetCpuThreadTimelineIndex(ThreadInfo.Id, CpuTimelineIndex))
			{
				TSharedPtr<FCpuTimingTrack>* TrackPtrPtr = CpuTracks.Find(ThreadInfo.Id);
				if (TrackPtrPtr == nullptr)
				{
					FString TrackName(ThreadInfo.Name && *ThreadInfo.Name ? ThreadInfo.Name : FString::Printf(TEXT("Thread %u"), ThreadInfo.Id));

					// Create new Timing Events track for the CPU thread.
					TSharedPtr<FCpuTimingTrack> Track = MakeShared<FCpuTimingTrack>(*this, TrackName, GroupName, CpuTimelineIndex, ThreadInfo.Id);
					Track->SetOrder(Order);
					CpuTracks.Add(ThreadInfo.Id, Track);

					FThreadGroup& ThreadGroup = ThreadGroups[GroupName];
					ThreadGroup.NumTimelines++;

					if (TimingView && TimingView->IsAssetLoadingModeEnabled() && bIsLoadingThread)
					{
						Track->SetVisibilityFlag(true);
						ThreadGroup.bIsVisible = true;
					}
					else
					{
						Track->SetVisibilityFlag(bIsGroupVisible);
					}

					InSession.AddScrollableTrack(Track);
				}
				else
				{
					TSharedPtr<FCpuTimingTrack> Track = *TrackPtrPtr;
					if (Track->GetOrder() != Order)
					{
						Track->SetOrder(Order);
						bTracksOrderChanged = true;
					}
				}
			}

			constexpr int32 OrderIncrement = FTimingTrackOrder::GroupRange / 1000; // distribute max 1000 tracks in the order group range
			static_assert(OrderIncrement >= 1, "Order group range too small");
			Order += OrderIncrement;
		});

		if (bTracksOrderChanged)
		{
			InSession.InvalidateScrollableTracksOrder();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InOutMenuBuilder)
{
	if (&InSession != TimingView)
	{
		return;
	}

	InOutMenuBuilder.BeginSection("ThreadProfiler", LOCTEXT("ThreadProfilerHeading", "Threads"));
	{
		//TODO: MenuBuilder.AddMenuEntry(Commands.ShowAllGpuTracks);
		InOutMenuBuilder.AddMenuEntry(
			LOCTEXT("ShowAllGpuTracks", "GPU Track - Y"),
			LOCTEXT("ShowAllGpuTracks_Tooltip", "Show/hide the GPU track"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FThreadTimingSharedState::ShowHideAllGpuTracks),
					  FCanExecuteAction(),
					  FIsActionChecked::CreateSP(this, &FThreadTimingSharedState::IsAllGpuTracksToggleOn)),
			NAME_None, //"QuickFilterSeparator",
			EUserInterfaceActionType::ToggleButton
		);

		//TODO: MenuBuilder.AddMenuEntry(Commands.ShowAllCpuTracks);
		InOutMenuBuilder.AddMenuEntry(
			LOCTEXT("ShowAllCpuTracks", "CPU Thread Tracks - U"),
			LOCTEXT("ShowAllCpuTracks_Tooltip", "Show/hide all CPU tracks (and all CPU thread groups)"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FThreadTimingSharedState::ShowHideAllCpuTracks),
					  FCanExecuteAction(),
					  FIsActionChecked::CreateSP(this, &FThreadTimingSharedState::IsAllCpuTracksToggleOn)),
			NAME_None, //"QuickFilterSeparator",
			EUserInterfaceActionType::ToggleButton
		);
	}
	InOutMenuBuilder.EndSection();

	InOutMenuBuilder.BeginSection("ThreadGroups", LOCTEXT("ThreadGroupsHeading", "CPU Thread Groups"));
	CreateThreadGroupsMenu(InOutMenuBuilder);
	InOutMenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::CreateThreadGroupsMenu(FMenuBuilder& InOutMenuBuilder)
{
	// Sort the list of thread groups.
	TArray<const FThreadGroup*> SortedThreadGroups;
	SortedThreadGroups.Reserve(ThreadGroups.Num());
	for (const auto& KV : ThreadGroups)
	{
		SortedThreadGroups.Add(&KV.Value);
	}
	Algo::SortBy(SortedThreadGroups, &FThreadGroup::GetOrder);

	for (const FThreadGroup* ThreadGroupPtr : SortedThreadGroups)
	{
		const FThreadGroup& ThreadGroup = *ThreadGroupPtr;
		if (ThreadGroup.NumTimelines > 0)
		{
			InOutMenuBuilder.AddMenuEntry(
				//FText::FromString(ThreadGroup.Name),
				FText::Format(LOCTEXT("ThreadGroupFmt", "{0} ({1})"), FText::FromString(ThreadGroup.Name), ThreadGroup.NumTimelines),
				TAttribute<FText>(), // no tooltip
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FThreadTimingSharedState::ToggleTrackVisibilityByGroup_Execute, ThreadGroup.Name),
						  FCanExecuteAction::CreateLambda([] { return true; }),
						  FIsActionChecked::CreateSP(this, &FThreadTimingSharedState::ToggleTrackVisibilityByGroup_IsChecked, ThreadGroup.Name)),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::SetAllCpuTracksToggle(bool bOnOff)
{
	bShowHideAllCpuTracks = bOnOff;

	for (const auto& KV : CpuTracks)
	{
		FCpuTimingTrack& Track = *KV.Value;
		Track.SetVisibilityFlag(bShowHideAllCpuTracks);
	}

	for (auto& KV : ThreadGroups)
	{
		KV.Value.bIsVisible = bShowHideAllCpuTracks;
	}

	if (TimingView)
	{
		TimingView->OnTrackVisibilityChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::SetAllGpuTracksToggle(bool bOnOff)
{
	bShowHideAllGpuTracks = bOnOff;

	if (GpuTrack.IsValid())
	{
		GpuTrack->SetVisibilityFlag(bShowHideAllGpuTracks);

		if (TimingView)
		{
			TimingView->OnTrackVisibilityChanged();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingSharedState::ToggleTrackVisibilityByGroup_IsChecked(const TCHAR* InGroupName) const
{
	if (ThreadGroups.Contains(InGroupName))
	{
		const FThreadGroup& ThreadGroup = ThreadGroups[InGroupName];
		return ThreadGroup.bIsVisible;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingSharedState::ToggleTrackVisibilityByGroup_Execute(const TCHAR* InGroupName)
{
	if (ThreadGroups.Contains(InGroupName))
	{
		FThreadGroup& ThreadGroup = ThreadGroups[InGroupName];
		ThreadGroup.bIsVisible = !ThreadGroup.bIsVisible;

		for (const auto& KV : CpuTracks)
		{
			FCpuTimingTrack& Track = *KV.Value;
			if (Track.GetGroupName() == InGroupName)
			{
				Track.SetVisibilityFlag(ThreadGroup.bIsVisible);
			}
		}

		if (TimingView)
		{
			TimingView->OnTrackVisibilityChanged();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FThreadTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FThreadTimingTrack)

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid() && Trace::ReadTimingProfilerProvider(*Session.Get()))
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

		const Trace::ITimingProfilerTimerReader* TimerReader;
		TimingProfilerProvider.ReadTimers([&TimerReader](const Trace::ITimingProfilerTimerReader& Out) { TimerReader = &Out; });

		const FTimingTrackViewport& Viewport = Context.GetViewport();

		TimingProfilerProvider.ReadTimeline(TimelineIndex,
			[&Viewport, this, &Builder, TimerReader](const Trace::ITimingProfilerProvider::Timeline& Timeline)
			{
				if (FTimingEventsTrack::bUseDownSampling)
				{
					const double SecondsPerPixel = 1.0 / Viewport.GetScaleX();
					Timeline.EnumerateEventsDownSampled(Viewport.GetStartTime(), Viewport.GetEndTime(), SecondsPerPixel,
						[this, &Builder, TimerReader](double StartTime, double EndTime, uint32 Depth, const Trace::FTimingProfilerEvent& Event)
						{
							const Trace::FTimingProfilerTimer* Timer = TimerReader->GetTimer(Event.TimerIndex);
							if (ensure(Timer != nullptr))
							{
								AddTimingEventToBuilder(Builder, StartTime, EndTime, Depth, Event.TimerIndex, Timer);
							}
							else
							{
								Builder.AddEvent(StartTime, EndTime, Depth, 0xFF000000, [&Event](float) { return FString::Printf(TEXT("[%u]"), Event.TimerIndex); });
							}
							return Trace::EEventEnumerate::Continue;
						});
				}
				else
				{
					Timeline.EnumerateEvents(Viewport.GetStartTime(), Viewport.GetEndTime(),
						[this, &Builder, TimerReader](double StartTime, double EndTime, uint32 Depth, const Trace::FTimingProfilerEvent& Event)
						{
							const Trace::FTimingProfilerTimer* Timer = TimerReader->GetTimer(Event.TimerIndex);
							if (ensure(Timer != nullptr))
							{
								AddTimingEventToBuilder(Builder, StartTime, EndTime, Depth, Event.TimerIndex, Timer);
							}
							else
							{
								Builder.AddEvent(StartTime, EndTime, Depth, 0xFF000000, [&Event](float) { return FString::Printf(TEXT("[%u]"), Event.TimerIndex); });
							}
							return Trace::EEventEnumerate::Continue;
						});
				}
			});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::BuildFilteredDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	const TSharedPtr<ITimingEventFilter> EventFilterPtr = Context.GetEventFilter();
	if (EventFilterPtr.IsValid() && EventFilterPtr->FilterTrack(*this))
	{
		bool bFilterOnlyByEventType = false; // this is the most often use case, so the below code tries to optimize it
		uint64 FilterEventType = 0;
		if (EventFilterPtr->Is<FTimingEventFilterByEventType>())
		{
			bFilterOnlyByEventType = true;
			const FTimingEventFilterByEventType& EventFilter = EventFilterPtr->As<FTimingEventFilterByEventType>();
			FilterEventType = EventFilter.GetEventType();
		}

		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid() && Trace::ReadTimingProfilerProvider(*Session.Get()))
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

			const Trace::ITimingProfilerTimerReader* TimerReader;
			TimingProfilerProvider.ReadTimers([&TimerReader](const Trace::ITimingProfilerTimerReader& Out) { TimerReader = &Out; });

			const FTimingTrackViewport& Viewport = Context.GetViewport();

			if (bFilterOnlyByEventType)
			{
				TimingProfilerProvider.ReadTimeline(TimelineIndex,
					[&Viewport, this, &Builder, TimerReader, FilterEventType](const Trace::ITimingProfilerProvider::Timeline& Timeline)
					{
						// Note: Enumerating events for filtering should not use downsampling.
						//const double SecondsPerPixel = 1.0 / Viewport.GetScaleX();
						//Timeline.EnumerateEventsDownSampled(Viewport.GetStartTime(), Viewport.GetEndTime(), SecondsPerPixel,
						Timeline.EnumerateEvents(Viewport.GetStartTime(), Viewport.GetEndTime(),
							[this, &Builder, TimerReader, FilterEventType](double StartTime, double EndTime, uint32 Depth, const Trace::FTimingProfilerEvent& Event)
							{
								const Trace::FTimingProfilerTimer* Timer = TimerReader->GetTimer(Event.TimerIndex);
								if (ensure(Timer != nullptr))
								{
									if (Timer->Id == FilterEventType)
									{
										AddTimingEventToBuilder(Builder, StartTime, EndTime, Depth, Event.TimerIndex, Timer);
									}
								}
								return Trace::EEventEnumerate::Continue;
							});
					});
			}
			else // generic filter
			{
				TimingProfilerProvider.ReadTimeline(TimelineIndex,
					[&Viewport, this, &Builder, TimerReader, &EventFilterPtr](const Trace::ITimingProfilerProvider::Timeline& Timeline)
					{
						// Note: Enumerating events for filtering should not use downsampling.
						//const double SecondsPerPixel = 1.0 / Viewport.GetScaleX();
						//Timeline.EnumerateEventsDownSampled(Viewport.GetStartTime(), Viewport.GetEndTime(), SecondsPerPixel,
						Timeline.EnumerateEvents(Viewport.GetStartTime(), Viewport.GetEndTime(),
							[this, &Builder, TimerReader, &EventFilterPtr](double StartTime, double EndTime, uint32 Depth, const Trace::FTimingProfilerEvent& Event)
							{
								const Trace::FTimingProfilerTimer* Timer = TimerReader->GetTimer(Event.TimerIndex);
								if (ensure(Timer != nullptr))
								{
									FTimingEvent TimingEvent(SharedThis(this), StartTime, EndTime, Depth, Timer->Id);
									if (EventFilterPtr->FilterEvent(TimingEvent))
									{
										AddTimingEventToBuilder(Builder, StartTime, EndTime, Depth, Event.TimerIndex, Timer);
									}
								}
								return Trace::EEventEnumerate::Continue;
							});
					});
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::PostDraw(const ITimingTrackDrawContext& Context) const
{
	const TSharedPtr<const ITimingEvent> SelectedEventPtr = Context.GetSelectedEvent();
	if (SelectedEventPtr.IsValid() &&
		SelectedEventPtr->CheckTrack(this) &&
		SelectedEventPtr->Is<FTimingEvent>())
	{
		const FTimingEvent& SelectedEvent = SelectedEventPtr->As<FTimingEvent>();
		const ITimingViewDrawHelper& Helper = Context.GetHelper();
		DrawSelectedEventInfo(SelectedEvent, Context.GetViewport(), Context.GetDrawContext(), Helper.GetWhiteBrush(), Helper.GetEventFont());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::DrawSelectedEventInfo(const FTimingEvent& SelectedEvent, const FTimingTrackViewport& Viewport, const FDrawContext& DrawContext, const FSlateBrush* WhiteBrush, const FSlateFontInfo& Font) const
{
	FindTimingProfilerEvent(SelectedEvent, [&SelectedEvent, &Font, &Viewport, &DrawContext, &WhiteBrush](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
	{
		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		check(Session.IsValid());

		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

		const Trace::ITimingProfilerTimerReader* TimerReader;
		TimingProfilerProvider.ReadTimers([&TimerReader](const Trace::ITimingProfilerTimerReader& Out) { TimerReader = &Out; });

		const Trace::FTimingProfilerTimer* Timer = TimerReader->GetTimer(InFoundEvent.TimerIndex);
		if (Timer != nullptr)
		{
			FString Str = FString::Printf(TEXT("%s (Incl.: %s, Excl.: %s)"),
				Timer->Name,
				*TimeUtils::FormatTimeAuto(SelectedEvent.GetDuration()),
				*TimeUtils::FormatTimeAuto(SelectedEvent.GetExclusiveTime()));

			const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			const FVector2D Size = FontMeasureService->Measure(Str, Font);
			const float X = Viewport.GetWidth() - Size.X - 23.0f;
			const float Y = Viewport.GetHeight() - Size.Y - 18.0f;

			const FLinearColor BackgroundColor(0.05f, 0.05f, 0.05f, 1.0f);
			const FLinearColor TextColor(0.7f, 0.7f, 0.7f, 1.0f);

			DrawContext.DrawBox(X - 8.0f, Y - 2.0f, Size.X + 16.0f, Size.Y + 4.0f, WhiteBrush, BackgroundColor);
			DrawContext.LayerId++;

			DrawContext.DrawText(X, Y, Str, Font, TextColor);
			DrawContext.LayerId++;
		}
	});
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const
{
	InOutTooltip.ResetContent();

	if (InTooltipEvent.CheckTrack(this) && InTooltipEvent.Is<FTimingEvent>())
	{
		const FTimingEvent& TooltipEvent = InTooltipEvent.As<FTimingEvent>();

		FindTimingProfilerEvent(TooltipEvent, [this, &InOutTooltip, &TooltipEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
		{
			TSharedPtr<FTimingEvent> ParentTimingEvent;
			Trace::FTimingProfilerEvent ParentEvent;
			TSharedPtr<FTimingEvent> RootTimingEvent;
			Trace::FTimingProfilerEvent RootEvent;
			GetParentAndRoot(TooltipEvent, ParentTimingEvent, ParentEvent, RootTimingEvent, RootEvent);

			TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
			check(Session.IsValid());

			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

			const Trace::ITimingProfilerTimerReader* TimerReader;
			TimingProfilerProvider.ReadTimers([&TimerReader](const Trace::ITimingProfilerTimerReader& Out) { TimerReader = &Out; });

			const Trace::FTimingProfilerTimer* Timer = TimerReader->GetTimer(InFoundEvent.TimerIndex);
			FString TimerName = (Timer != nullptr) ? Timer->Name : TEXT("N/A");
			InOutTooltip.AddTitle(TimerName);

			if (ParentTimingEvent.IsValid() && TooltipEvent.GetDepth() > 0)
			{
				Timer = TimerReader->GetTimer(ParentEvent.TimerIndex);
				const TCHAR* ParentTimerName = (Timer != nullptr) ? Timer->Name : TEXT("N/A");
				FNumberFormattingOptions FormattingOptions;
				FormattingOptions.MaximumFractionalDigits = 2;
				const FString ValueStr = FString::Printf(TEXT("%s %s"), *FText::AsPercent(TooltipEvent.GetDuration() / ParentTimingEvent->GetDuration(), &FormattingOptions).ToString(), ParentTimerName);
				InOutTooltip.AddNameValueTextLine(TEXT("% of Parent:"), ValueStr);
			}

			if (RootTimingEvent.IsValid() && TooltipEvent.GetDepth() > 1)
			{
				Timer = TimerReader->GetTimer(RootEvent.TimerIndex);
				const TCHAR* RootTimerName = (Timer != nullptr) ? Timer->Name : TEXT("N/A");
				FNumberFormattingOptions FormattingOptions;
				FormattingOptions.MaximumFractionalDigits = 2;
				const FString ValueStr = FString::Printf(TEXT("%s %s"), *FText::AsPercent(TooltipEvent.GetDuration() / RootTimingEvent->GetDuration(), &FormattingOptions).ToString(), RootTimerName);
				InOutTooltip.AddNameValueTextLine(TEXT("% of Root:"), ValueStr);
			}

			InOutTooltip.AddNameValueTextLine(TEXT("Inclusive Time:"), TimeUtils::FormatTimeAuto(TooltipEvent.GetDuration()));

			{
				FNumberFormattingOptions FormattingOptions;
				FormattingOptions.MaximumFractionalDigits = 2;
				const FString ExclStr = FString::Printf(TEXT("%s (%s)"), *TimeUtils::FormatTimeAuto(TooltipEvent.GetExclusiveTime()), *FText::AsPercent(TooltipEvent.GetExclusiveTime() / TooltipEvent.GetDuration(), &FormattingOptions).ToString());
				InOutTooltip.AddNameValueTextLine(TEXT("Exclusive Time:"), ExclStr);
			}

			InOutTooltip.AddNameValueTextLine(TEXT("Depth:"), FString::Printf(TEXT("%d"), TooltipEvent.GetDepth()));

			TArrayView<const uint8> Metadata = TimerReader->GetMetadata(InFoundEvent.TimerIndex);
			if (Metadata.Num() > 0)
			{
				AppendMetadataToTooltip(InOutTooltip, Metadata);
			}
		});
	}

	InOutTooltip.UpdateLayout();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::GetParentAndRoot(const FTimingEvent& TimingEvent, TSharedPtr<FTimingEvent>& OutParentTimingEvent, Trace::FTimingProfilerEvent& OutParentEvent, TSharedPtr<FTimingEvent>& OutRootTimingEvent, Trace::FTimingProfilerEvent& OutRootEvent) const
{
	// Note: This function does not compute Exclusive Time for parent and root events.

	if (TimingEvent.GetDepth() > 0)
	{
		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			if (Trace::ReadTimingProfilerProvider(*Session.Get()))
			{
				const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

				TimingProfilerProvider.ReadTimeline(GetTimelineIndex(), [&TimingEvent, &OutParentTimingEvent, &OutParentEvent, &OutRootTimingEvent, &OutRootEvent](const Trace::ITimingProfilerProvider::Timeline& Timeline)
				{
					const double Time = (TimingEvent.GetStartTime() + TimingEvent.GetEndTime()) / 2;
					Timeline.EnumerateEvents(Time, Time, [&TimingEvent, &OutParentTimingEvent, &OutParentEvent, &OutRootTimingEvent, &OutRootEvent](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FTimingProfilerEvent& Event)
					{
						if (EventDepth == 0)
						{
							OutRootEvent = Event;
							OutRootTimingEvent = MakeShared<FTimingEvent>(TimingEvent.GetTrack(), EventStartTime, EventEndTime, EventDepth);
						}
						if (EventDepth == TimingEvent.GetDepth() - 1)
						{
							OutParentEvent = Event;
							OutParentTimingEvent = MakeShared<FTimingEvent>(TimingEvent.GetTrack(), EventStartTime, EventEndTime, EventDepth);
						}

						return Trace::EEventEnumerate::Continue;
					});
				});
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<const ITimingEvent> FThreadTimingTrack::SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const
{
	TSharedPtr<const ITimingEvent> FoundEvent;
	FindTimingProfilerEvent(InSearchParameters, [this, &FoundEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
	{
		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		check(Session.IsValid())

		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

		const Trace::ITimingProfilerTimerReader* TimerReader;
		TimingProfilerProvider.ReadTimers([&TimerReader](const Trace::ITimingProfilerTimerReader& Out) { TimerReader = &Out; });

		const Trace::FTimingProfilerTimer* Timer = TimerReader->GetTimer(InFoundEvent.TimerIndex);
		const uint64 EventType = (Timer != nullptr) ? Timer->Id : InFoundEvent.TimerIndex;
		FoundEvent = MakeShared<FTimingEvent>(SharedThis(this), InFoundStartTime, InFoundEndTime, InFoundDepth, EventType);
	});

	return FoundEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::UpdateEventStats(ITimingEvent& InOutEvent) const
{
	if (InOutEvent.CheckTrack(this) && InOutEvent.Is<FTimingEvent>())
	{
		FTimingEvent& TrackEvent = InOutEvent.As<FTimingEvent>();

		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			if (Trace::ReadTimingProfilerProvider(*Session.Get()))
			{
				const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

				// Compute Exclusive Time.
				TimingProfilerProvider.ReadTimeline(GetTimelineIndex(), [&TrackEvent](const Trace::ITimingProfilerProvider::Timeline& Timeline)
				{
					struct FEnumerationState
					{
						double EventStartTime;
						double EventEndTime;
						uint64 EventDepth;

						uint64 CurrentDepth;
						double LastTime;
						double ExclusiveTime;
						bool IsInEventScope;
					};
					FEnumerationState State;

					State.EventStartTime = TrackEvent.GetStartTime();
					State.EventEndTime = TrackEvent.GetEndTime();
					State.EventDepth = TrackEvent.GetDepth();

					State.CurrentDepth = 0;
					State.LastTime = 0.0;
					State.ExclusiveTime = 0.0;
					State.IsInEventScope = false;

					Timeline.EnumerateEvents(TrackEvent.GetStartTime(), TrackEvent.GetEndTime(), [&State](bool IsEnter, double Time, const Trace::FTimingProfilerEvent& Event)
					{
						if (IsEnter)
						{
							if (State.IsInEventScope && State.CurrentDepth == State.EventDepth + 1)
							{
								State.ExclusiveTime += Time - State.LastTime;
							}
							if (State.CurrentDepth == State.EventDepth && Time == State.EventStartTime)
							{
								State.IsInEventScope = true;
							}
							++State.CurrentDepth;
						}
						else
						{
							--State.CurrentDepth;
							if (State.CurrentDepth == State.EventDepth && Time == State.EventEndTime)
							{
								State.IsInEventScope = false;
								State.ExclusiveTime += Time - State.LastTime;
							}
						}
						State.LastTime = Time;
						return Trace::EEventEnumerate::Continue;
					});

					TrackEvent.SetExclusiveTime(State.ExclusiveTime);
				});
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::OnEventSelected(const ITimingEvent& InSelectedEvent) const
{
	if (InSelectedEvent.CheckTrack(this) && InSelectedEvent.Is<FTimingEvent>())
	{
		const FTimingEvent& TrackEvent = InSelectedEvent.As<FTimingEvent>();

		//uint64 TimerType = uint64(-1);
		//FindTimingProfilerEvent(TrackEvent, [&TimerType](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
		//{
		//	const Trace::FTimingProfilerTimer* Timer = TimerReader.GetTimer(InFoundEvent.TimerIndex);
		//	TimerType = (Timer != nullptr) ? Timer->Id : InFoundEvent.TimerIndex;
		//});

		const uint64 TimerType = TrackEvent.GetType();

		// Select the timer node corresponding to timing event type of selected timing event.
		FTimingProfilerManager::Get()->SetSelectedTimer(TimerType);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::OnClipboardCopyEvent(const ITimingEvent& InSelectedEvent) const
{
	if (InSelectedEvent.CheckTrack(this) && InSelectedEvent.Is<FTimingEvent>())
	{
		const FTimingEvent& TrackEvent = InSelectedEvent.As<FTimingEvent>();

		//uint64 TimerType = uint64(-1);
		//FindTimingProfilerEvent(TrackEvent, [&TimerType](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InFoundEvent)
		//{
		//	const Trace::FTimingProfilerTimer* Timer = TimerReader.GetTimer(InFoundEvent.TimerIndex);
		//	TimerType = (Timer != nullptr) ? Timer->Id : InFoundEvent.TimerIndex;
		//});

		const uint64 TimerType = TrackEvent.GetType();

		FTimerNodePtr TimerNodePtr = FTimingProfilerManager::Get()->GetTimerNode(TimerType);
		if (TimerNodePtr)
		{
			// Copy name of selected timing event to clipboard.
			FPlatformApplicationMisc::ClipboardCopy(*TimerNodePtr->GetName().ToString());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FThreadTimingTrack::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	if (GetGroupName() != nullptr)
	{
		MenuBuilder.BeginSection(TEXT("Misc"));
		{
			MenuBuilder.AddMenuEntry(
				FText::Format(LOCTEXT("CpuThreadGroupFmt", "CPU Thread Group: {0}"), FText::FromString(GetGroupName())),
				FText(),
				FSlateIcon(),
				FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })),
				NAME_None,
				EUserInterfaceActionType::Button
			);
		}
		MenuBuilder.EndSection();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingTrack::FindTimingProfilerEvent(const FTimingEvent& InTimingEvent, TFunctionRef<void(double, double, uint32, const Trace::FTimingProfilerEvent&)> InFoundPredicate) const
{
	auto MatchEvent = [&InTimingEvent](double InStartTime, double InEndTime, uint32 InDepth)
	{
		return InDepth == InTimingEvent.GetDepth()
			&& InStartTime == InTimingEvent.GetStartTime()
			&& InEndTime == InTimingEvent.GetEndTime();
	};

	const double Time = (InTimingEvent.GetStartTime() + InTimingEvent.GetEndTime()) / 2;
	FTimingEventSearchParameters SearchParameters(Time, Time, ETimingEventSearchFlags::StopAtFirstMatch, MatchEvent);
	SearchParameters.SearchHandle = &InTimingEvent.GetSearchHandle();
	return FindTimingProfilerEvent(SearchParameters, InFoundPredicate);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FThreadTimingTrack::FindTimingProfilerEvent(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const Trace::FTimingProfilerEvent&)> InFoundPredicate) const
{
	return TTimingEventSearch<Trace::FTimingProfilerEvent>::Search(
		InParameters,

		[this](TTimingEventSearch<Trace::FTimingProfilerEvent>::FContext& InContext)
		{
			TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
			if (Session.IsValid())
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

				if (Trace::ReadTimingProfilerProvider(*Session.Get()))
				{
					const Trace::ITimingProfilerProvider& TimingProfilerProvider = *Trace::ReadTimingProfilerProvider(*Session.Get());

					TimingProfilerProvider.ReadTimeline(GetTimelineIndex(), [&InContext](const Trace::ITimingProfilerProvider::Timeline& Timeline)
					{
						Timeline.EnumerateEvents(InContext.GetParameters().StartTime, InContext.GetParameters().EndTime, [&InContext](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FTimingProfilerEvent& Event)
						{
							InContext.Check(EventStartTime, EventEndTime, EventDepth, Event);
							return InContext.ShouldContinueSearching() ? Trace::EEventEnumerate::Continue : Trace::EEventEnumerate::Stop;
						});
					});
				}
			}
		},

		[&InFoundPredicate](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const Trace::FTimingProfilerEvent& InEvent)
		{
			InFoundPredicate(InFoundStartTime, InFoundEndTime, InFoundDepth, InEvent);
		},

		SearchCache);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
