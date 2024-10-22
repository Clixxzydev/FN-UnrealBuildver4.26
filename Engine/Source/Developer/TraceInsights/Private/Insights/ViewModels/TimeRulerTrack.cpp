// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeRulerTrack.h"

#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include <limits>

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsStyle.h"
#include "Insights/ViewModels/DrawHelpers.h"
#include "Insights/ViewModels/TimingTrackViewport.h"

#define LOCTEXT_NAMESPACE "TimeRulerTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FTimeRulerTrack)

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimeRulerTrack::FTimeRulerTrack()
	: FBaseTimingTrack(TEXT("Time Ruler"))
	, WhiteBrush(FInsightsStyle::Get().GetBrush("WhiteBrush"))
	, Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
	, CrtMousePosTextWidth(0.0f)
	, CrtTimeMarkerTextWidth(0.0f)
{
	SetValidLocations(ETimingTrackLocation::TopDocked);
	SetOrder(FTimingTrackOrder::TimeRuler);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTimeRulerTrack::~FTimeRulerTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeRulerTrack::Reset()
{
	FBaseTimingTrack::Reset();

	bIsSelecting = false;
	SelectionStartTime = 0.0;
	SelectionEndTime = 0.0;

	bIsDragging = false;
	TimeMarker = std::numeric_limits<double>::infinity();

	constexpr float TimeRulerHeight = 24.0f;
	SetHeight(TimeRulerHeight);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeRulerTrack::SetSelection(const bool bInIsSelecting, const double InSelectionStartTime, const double InSelectionEndTime)
{
	bIsSelecting = bInIsSelecting;
	SelectionStartTime = InSelectionStartTime;
	SelectionEndTime = InSelectionEndTime;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeRulerTrack::SetTimeMarker(const bool bInIsDragging, const double InTimeMarker)
{
	bIsDragging = bInIsDragging;
	TimeMarker = InTimeMarker;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeRulerTrack::PostUpdate(const ITimingTrackUpdateContext& Context)
{
	//constexpr float HeaderWidth = 100.0f;
	//constexpr float HeaderHeight = 14.0f;

	const float MouseY = Context.GetMousePosition().Y;
	if (MouseY >= GetPosY() && MouseY < GetPosY() + GetHeight())
	{
		SetHoveredState(true);
		//const float MouseX = Context.GetMousePosition().X;
		//SetHeaderHoveredState(MouseX < HeaderWidth && MouseY < GetPosY() + HeaderHeight);
	}
	else
	{
		SetHoveredState(false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeRulerTrack::Draw(const ITimingTrackDrawContext& Context) const
{
	FDrawContext& DrawContext = Context.GetDrawContext();
	const FTimingTrackViewport& Viewport = Context.GetViewport();

	const float MinorTickMark = 5.0f;
	const float MajorTickMark = 20 * MinorTickMark;

	const float MinorTickMarkHeight = 5.0f;
	const float MajorTickMarkHeight = 11.0f;

	const float TextY = GetPosY() + MajorTickMarkHeight;

	double MinorTickMarkTime = Viewport.GetDurationForViewportDX(MinorTickMark);
	double MajorTickMarkTime = Viewport.GetDurationForViewportDX(MajorTickMark);

	double VX = Viewport.GetStartTime() * Viewport.GetScaleX();
	double MinorN = FMath::FloorToDouble(VX / static_cast<double>(MinorTickMark));
	double MajorN = FMath::FloorToDouble(VX / static_cast<double>(MajorTickMark));
	float MinorOX = static_cast<float>(FMath::RoundToDouble(MinorN * static_cast<double>(MinorTickMark) - VX));
	float MajorOX = static_cast<float>(FMath::RoundToDouble(MajorN * static_cast<double>(MajorTickMark) - VX));

	// Draw the time ruler's background.
	FDrawHelpers::DrawBackground(DrawContext, WhiteBrush, Viewport, GetPosY(), GetHeight());

	// Draw the minor tick marks.
	for (float X = MinorOX; X < Viewport.GetWidth(); X += MinorTickMark)
	{
		const bool bIsTenth = ((int32)(((X - MajorOX) / MinorTickMark) + 0.4f) % 2 == 0);
		const float MinorTickH = bIsTenth ? MinorTickMarkHeight : MinorTickMarkHeight - 1.0f;
		DrawContext.DrawBox(X, GetPosY(), 1.0f, MinorTickH, WhiteBrush,
			bIsTenth ? FLinearColor(0.3f, 0.3f, 0.3f, 1.0f) : FLinearColor(0.25f, 0.25f, 0.25f, 1.0f));
	}
	// Draw the major tick marks.
	for (float X = MajorOX; X < Viewport.GetWidth(); X += MajorTickMark)
	{
		DrawContext.DrawBox(X, GetPosY(), 1.0f, MajorTickMarkHeight, WhiteBrush, FLinearColor(0.4f, 0.4f, 0.4f, 1.0f));
	}
	DrawContext.LayerId++;

	const double DT = static_cast<double>(MajorTickMark) / Viewport.GetScaleX();
	const double Precision = FMath::Max(DT / 10.0, TimeUtils::Nanosecond);

	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	// Draw the time at major tick marks.
	for (float X = MajorOX; X < Viewport.GetWidth() + MajorTickMark; X += MajorTickMark)
	{
		const double T = Viewport.SlateUnitsToTime(X);
		FString Text = TimeUtils::FormatTime(T, Precision);
		const float TextWidth = FontMeasureService->Measure(Text, Font).X;
		DrawContext.DrawText(X - TextWidth / 2, TextY, Text, Font,
			(T < Viewport.GetMinValidTime() || T >= Viewport.GetMaxValidTime()) ? FLinearColor(0.7f, 0.5f, 0.5f, 1.0f) : FLinearColor(0.8f, 0.8f, 0.8f, 1.0f));
	}
	DrawContext.LayerId++;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeRulerTrack::PostDraw(const ITimingTrackDrawContext& Context) const
{
	FDrawContext& DrawContext = Context.GetDrawContext();
	const FTimingTrackViewport& Viewport = Context.GetViewport();
	const FVector2D& MousePosition = Context.GetMousePosition();

	bool bShowMousePos = !MousePosition.IsZero();
	if (bShowMousePos)
	{
		const double DT = 100.0 / Viewport.GetScaleX();

		const FLinearColor MousePosLineColor(0.9f, 0.9f, 0.9f, 0.1f);
		const FLinearColor MousePosTextBackgroundColor(0.9f, 0.9f, 0.9f, 1.0f);
		FLinearColor MousePosTextForegroundColor(0.1f, 0.1f, 0.1f, 1.0f);

		// Time at current mouse position.
		FString MousePosText;

		const double MousePosTime = Viewport.SlateUnitsToTime(MousePosition.X);
		const double MousePosPrecision = FMath::Max(DT / 100.0, TimeUtils::Nanosecond);
		if (MousePosition.Y >= GetPosY() && MousePosition.Y < GetPosY() + GetHeight())
		{
			// If mouse is hovering the time ruller, format time with a better precision (split seconds in ms, us, ns and ps).
			MousePosText = TimeUtils::FormatTimeSplit(MousePosTime, MousePosPrecision);
		}
		else
		{
			// Format current time with one more digit than the time at major tick marks.
			MousePosText = TimeUtils::FormatTime(MousePosTime, MousePosPrecision);
		}

		const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

		const float MousePosTextWidth = FMath::RoundToFloat(FontMeasureService->Measure(MousePosText, Font).X);

		if (!FMath::IsNearlyEqual(CrtMousePosTextWidth, MousePosTextWidth))
		{
			// Animate the box's width (to avoid flickering).
			CrtMousePosTextWidth = CrtMousePosTextWidth * 0.6f + MousePosTextWidth * 0.4f;
		}

		const float TextY = GetPosY() + 11.0f;

		float X = MousePosition.X;
		float W = CrtMousePosTextWidth + 4.0f;
		if (bIsSelecting && SelectionStartTime < SelectionEndTime)
		{
			// While selecting, display the current time on either left or right side of the selected time range (i.e. to not overlap the selection arrows).
			float SelectionX1 = Viewport.TimeToSlateUnitsRounded(SelectionStartTime);
			float SelectionX2 = Viewport.TimeToSlateUnitsRounded(SelectionEndTime);
			if (FMath::Abs(X - SelectionX1) > FMath::Abs(SelectionX2 - X))
			{
				X = SelectionX2 + W / 2;
			}
			else
			{
				X = SelectionX1 - W / 2;
			}
			MousePosTextForegroundColor = FLinearColor(FColor(32, 64, 128, 255));
		}
		else
		{
			// Draw horizontal line at mouse position.
			//DrawContext.DrawBox(0.0f, MousePosition.Y, Viewport.Width, 1.0f, WhiteBrush, MousePosLineColor);

			// Draw vertical line at mouse position.
			DrawContext.DrawBox(MousePosition.X, 0.0f, 1.0f, Viewport.GetHeight(), WhiteBrush, MousePosLineColor);

			// Stroke the vertical line above current time box.
			DrawContext.DrawBox(MousePosition.X, 0.0f, 1.0f, TextY, WhiteBrush, MousePosTextBackgroundColor);
		}

		// Fill the current time box.
		DrawContext.DrawBox(X - W / 2, TextY, W, 12.0f, WhiteBrush, MousePosTextBackgroundColor);
		DrawContext.LayerId++;

		// Draw current time text.
		DrawContext.DrawText(X - MousePosTextWidth / 2, TextY, MousePosText, Font, MousePosTextForegroundColor);
		DrawContext.LayerId++;
	}

	// Draw the time marker.
	const float TimeMarkerX = Viewport.TimeToSlateUnitsRounded(TimeMarker);
	if (TimeMarkerX >= 0.0f && TimeMarkerX < Viewport.GetWidth())
	{
		const FLinearColor TimeMarkerColor(0.85f, 0.5f, 0.03f, 0.5f);
		const FLinearColor TimeMarkerTextBackgroundColor(TimeMarkerColor.CopyWithNewOpacity(1.0f));
		const FLinearColor TimeMarkerTextForegroundColor(0.1f, 0.1f, 0.1f, 1.0f);

		// Draw the orange vertical line.
		DrawContext.DrawBox(TimeMarkerX, 0.0f, 1.0f, Viewport.GetHeight(), WhiteBrush, TimeMarkerColor);
		DrawContext.LayerId++;

		// Time at current marker
		FString TimeMarkerText;

		const double DT = 100.0 / Viewport.GetScaleX();

		const double MousePosPrecision = FMath::Max(DT / 100.0, TimeUtils::Nanosecond);
		if (!MousePosition.IsZero() && MousePosition.Y >= GetPosY() && MousePosition.Y < GetPosY() + GetHeight())
		{
			// If mouse is hovering the time ruler, format time with a better precision (split seconds in ms, us, ns and ps).
			TimeMarkerText = TimeUtils::FormatTimeSplit(TimeMarker, MousePosPrecision);
		}
		else
		{
			// Format current time with one more digit than the time at major tick marks.
			TimeMarkerText = TimeUtils::FormatTime(TimeMarker, MousePosPrecision);
		}

		const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

		const float TimeMarkerTextWidth = FMath::RoundToFloat(FontMeasureService->Measure(TimeMarkerText, Font).X);

		if (!FMath::IsNearlyEqual(CrtTimeMarkerTextWidth, TimeMarkerTextWidth))
		{
			// Animate the box's width (to avoid flickering).
			CrtTimeMarkerTextWidth = CrtTimeMarkerTextWidth * 0.6f + TimeMarkerTextWidth * 0.4f;
		}

		float X = TimeMarkerX;
		float W = CrtTimeMarkerTextWidth + 4.0f;

		// Fill the time marker box.
		DrawContext.DrawBox(X - W / 2, 0.0f, W, 12.0f, WhiteBrush, TimeMarkerTextBackgroundColor);
		DrawContext.LayerId++;

		// Draw time marker text.
		DrawContext.DrawText(X - TimeMarkerTextWidth / 2, 0.0f, TimeMarkerText, Font, TimeMarkerTextForegroundColor);
		DrawContext.LayerId++;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
