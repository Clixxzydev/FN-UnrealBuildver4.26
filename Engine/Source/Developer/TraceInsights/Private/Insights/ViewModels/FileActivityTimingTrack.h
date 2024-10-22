// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ITimingViewExtender.h"
#include "Insights/ViewModels/TimingEventsTrack.h"

class FTimingEventSearchParameters;
class STimingView;

class FOverviewFileActivityTimingTrack;
class FDetailedFileActivityTimingTrack;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFileActivitySharedState : public Insights::ITimingViewExtender, public TSharedFromThis<FFileActivitySharedState>
{
	friend class FOverviewFileActivityTimingTrack;
	friend class FDetailedFileActivityTimingTrack;

public:
	struct FIoFileActivity
	{
		uint64 Id;
		const TCHAR* Path;
		double StartTime;
		double EndTime;
		double CloseStartTime;
		double CloseEndTime;
		int32 EventCount;
		int32 Depth;
	};

	struct FIoTimingEvent
	{
		double StartTime;
		double EndTime;
		uint32 Depth;
		uint32 Type; // Trace::EFileActivityType + "Failed" flag
		uint64 Offset;
		uint64 Size;
		TSharedPtr<FIoFileActivity> FileActivity;
	};

public:
	explicit FFileActivitySharedState(STimingView* InTimingView) : TimingView(InTimingView) {}
	virtual ~FFileActivitySharedState() = default;

	// ITimingViewExtender
	virtual void OnBeginSession(Insights::ITimingViewSession& InSession) override;
	virtual void OnEndSession(Insights::ITimingViewSession& InSession) override;
	virtual void Tick(Insights::ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession) override;
	virtual void ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InOutMenuBuilder) override;

	const TArray<FIoTimingEvent>& GetAllEvents() const { return AllIoEvents; }

	void RequestUpdate() { bForceIoEventsUpdate = true; }

	bool IsMergeLanesToggleOn() const { return bMergeIoLanes; }
	void ToggleMergeLanes() { bMergeIoLanes = !bMergeIoLanes; RequestUpdate(); }

	bool IsAllIoTracksToggleOn() const { return bShowHideAllIoTracks; }
	void SetAllIoTracksToggle(bool bOnOff);
	void ShowAllIoTracks() { SetAllIoTracksToggle(true); }
	void HideAllIoTracks() { SetAllIoTracksToggle(false); }
	void ShowHideAllIoTracks() { SetAllIoTracksToggle(!IsAllIoTracksToggleOn()); }

	bool IsIoOverviewTrackVisible() const;
	void ShowHideIoOverviewTrack();

	bool IsIoActivityTrackVisible() const;
	void ShowHideIoActivityTrack();

	bool IsOnlyErrorsToggleOn() const;
	void ToggleOnlyErrors();

	bool AreBackgroundEventsVisible() const;
	void ToggleBackgroundEvents();

private:
	void BuildSubMenu(FMenuBuilder& InOutMenuBuilder);

private:
	STimingView* TimingView;

	TSharedPtr<FOverviewFileActivityTimingTrack> IoOverviewTrack;
	TSharedPtr<FDetailedFileActivityTimingTrack> IoActivityTrack;

	bool bShowHideAllIoTracks;
	bool bForceIoEventsUpdate;
	bool bMergeIoLanes; // merge lanes of file activity events in a way that avoids duplication (for the Activity track)

	TArray<TSharedPtr<FIoFileActivity>> FileActivities;
	TMap<uint64, TSharedPtr<FIoFileActivity>> FileActivityMap;

	/** All IO events, cached. */
	TArray<FIoTimingEvent> AllIoEvents;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFileActivityTimingTrack : public FTimingEventsTrack
{
	INSIGHTS_DECLARE_RTTI(FFileActivityTimingTrack, FTimingEventsTrack)

public:
	explicit FFileActivityTimingTrack(FFileActivitySharedState& InSharedState, const FString& InName)
		: FTimingEventsTrack(InName)
		, SharedState(InSharedState)
		, bIgnoreEventDepth(false)
		, bIgnoreDuration(false)
		, bShowOnlyErrors(false)
	{
	}
	virtual ~FFileActivityTimingTrack() {}

	virtual void InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const override;

	bool IsOnlyErrorsToggleOn() const { return bShowOnlyErrors; }
	void ToggleOnlyErrors() { bShowOnlyErrors = !bShowOnlyErrors; SetDirtyFlag(); }

protected:
	bool FindIoTimingEvent(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const FFileActivitySharedState::FIoTimingEvent&)> InFoundPredicate) const;

protected:
	FFileActivitySharedState& SharedState;
	bool bIgnoreEventDepth;
	bool bIgnoreDuration;
	bool bShowOnlyErrors; // shows only the events with errors (for the Overview track)
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FOverviewFileActivityTimingTrack : public FFileActivityTimingTrack
{
public:
	explicit FOverviewFileActivityTimingTrack(FFileActivitySharedState& InSharedState)
		: FFileActivityTimingTrack(InSharedState, TEXT("I/O Overview"))
	{
		bIgnoreEventDepth = true;
		bIgnoreDuration = true;
		//bShowOnlyErrors = true;
	}

	virtual void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;
	virtual const TSharedPtr<const ITimingEvent> SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const override;
	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FDetailedFileActivityTimingTrack : public FFileActivityTimingTrack
{
public:
	explicit FDetailedFileActivityTimingTrack(FFileActivitySharedState& InSharedState)
		: FFileActivityTimingTrack(InSharedState, TEXT("I/O Activity"))
		, bShowBackgroundEvents(false)
	{
		//bShowOnlyErrors = true;
	}

	virtual void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;
	virtual const TSharedPtr<const ITimingEvent> SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const override;
	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;

	bool AreBackgroundEventsVisible() const { return bShowBackgroundEvents; }
	void ToggleBackgroundEvents() { bShowBackgroundEvents = !bShowBackgroundEvents; SetDirtyFlag(); }

private:
	bool bShowBackgroundEvents; // shows the file activity backgroud events; from the Open event to the last Read/Write event, for each activity
};

////////////////////////////////////////////////////////////////////////////////////////////////////
