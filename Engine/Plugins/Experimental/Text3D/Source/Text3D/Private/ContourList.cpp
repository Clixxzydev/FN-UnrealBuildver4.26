﻿// Copyright Epic Games, Inc. All Rights Reserved.


#include "ContourList.h"
#include "Data.h"
#include "Part.h"
#include "GlyphLoader.h"

#include "Math/UnrealMathUtility.h"

FContourList::FContourList()
{
}

FContour& FContourList::Add()
{
	AddTail(FContour());
	return GetTail()->GetValue();
}

void FContourList::Remove(const FContour& Contour)
{
	// Search with comparing pointers
	for (TDoubleLinkedList<FContour>::TDoubleLinkedListNode* Node = GetHead(); Node; Node = Node->GetNextNode())
	{
		if (&Node->GetValue() == &Contour)
		{
			RemoveNode(Node);
			break;
		}
	}
}

void FContourList::Reset()
{
	for (FContour& Contour : *this)
	{
		for (const FPartPtr& Part : Contour)
		{
			Part->ResetDoneExpand();
			Part->ResetInitialPosition();
		}
	}
}

void FContourList::Initialize()
{
	for (FContour& Contour : *this)
	{
		for (int32 Index = 0; Index < Contour.Num(); Index++)
		{
			FPartPtr Point = Contour[Index];

			Point->Prev = Contour[Contour.GetPrev(Index)];
			Point->Next = Contour[Contour.GetNext(Index)];
		}

		for (FPartPtr Edge : Contour)
		{
			Edge->ComputeTangentX();
		}

		for (const FPartPtr Point : Contour)
		{
			Point->ComputeSmooth();
		}

		for (int32 Index = 0; Index < Contour.Num(); Index++)
		{
			const FPartPtr Point = Contour[Index];

			if (!Point->bSmooth && Point->TangentsDotProduct() > 0.f)
			{
				const FPartPtr Curr = Point;
				const FPartPtr Prev = Point->Prev;

				const float TangentsCrossProduct = FVector2D::CrossProduct(-Prev->TangentX, Curr->TangentX);
				const float MinTangentsCrossProduct = 0.9f;

				if (FMath::Abs(TangentsCrossProduct) < MinTangentsCrossProduct)
				{
					const float OffsetDefault = 0.01f;
					const float Offset = FMath::Min3(Prev->Length() / 2.f, Curr->Length() / 2.f, OffsetDefault);

					const FPartPtr Added = MakeShared<FPart>();
					Contour.Insert(Added, Index);

					Prev->Next = Added;
					Added->Prev = Prev;
					Added->Next = Curr;
					Curr->Prev = Added;

					const FVector2D CornerPosition = Curr->Position;

					Curr->Position = CornerPosition + Curr->TangentX * Offset;
					Added->Position = CornerPosition - Prev->TangentX * Offset;

					Added->ComputeTangentX();

					Added->ComputeSmooth();
					Curr->ComputeSmooth();
				}
			}
		}

		for (const FPartPtr& Point : Contour)
		{
			Point->ComputeNormal();
			Point->ResetInitialPosition();
		}
	}
}
