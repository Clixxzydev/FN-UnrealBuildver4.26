﻿// Copyright Epic Games, Inc. All Rights Reserved.


#include "MeshCreator.h"
#include "Contour.h"
#include "Part.h"
#include "Data.h"
#include "ContourList.h"

#include "ConstrainedDelaunay2.h"


FMeshCreator::FMeshCreator() :
	Data(MakeShared<FData>()),
	Glyph(MakeShared<FText3DGlyph>())
{
	Data->SetGlyph(Glyph);
}

void FMeshCreator::CreateMeshes(const TSharedContourNode Root, const float Extrude, const float Bevel, const EText3DBevelType Type, const int32 BevelSegments)
{
	CreateFrontMesh(Root);
	if (Contours->Num() == 0)
	{
		return;
	}

	CreateBevelMesh(Bevel, Type, BevelSegments);
	CreateExtrudeMesh(Extrude, Bevel, Type);
}

void FMeshCreator::SetFrontAndBevelTextureCoordinates(const float Bevel)
{
	EText3DGroupType GroupType = FMath::IsNearlyZero(Bevel) ? EText3DGroupType::Front : EText3DGroupType::Bevel;
	int32 GroupIndex = static_cast<int32>(GroupType);

	FBox2D Box;
	TText3DGroupList& Groups = Glyph->GetGroups();

	const int32 FirstVertex = Groups[GroupIndex].FirstVertex;
	const int32 LastVertex = Groups[GroupIndex + 1].FirstVertex;

	TVertexAttributesConstRef<FVector> Positions = Glyph->GetStaticMeshAttributes().GetVertexPositions();

	const FVector& FirstPosition = Positions[FVertexID(FirstVertex)];
	const FVector2D PositionFlat = { FirstPosition.Y, FirstPosition.Z };

	Box.Min = PositionFlat;
	Box.Max = PositionFlat;


	for (int32 VertexIndex = FirstVertex + 1; VertexIndex < LastVertex; VertexIndex++)
	{
		const FVector& Position = Positions[FVertexID(VertexIndex)];

		Box.Min.X = FMath::Min(Box.Min.X, Position.Y);
		Box.Min.Y = FMath::Min(Box.Min.Y, Position.Z);
		Box.Max.X = FMath::Max(Box.Max.X, Position.Y);
		Box.Max.Y = FMath::Max(Box.Max.Y, Position.Z);
	}

	FStaticMeshAttributes& StaticMeshAttributes = Glyph->GetStaticMeshAttributes();
	TVertexAttributesRef<FVector> VertexPositions = StaticMeshAttributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = StaticMeshAttributes.GetVertexInstanceUVs();

	auto SetTextureCoordinates = [Groups, VertexPositions, VertexInstanceUVs, &Box](const EText3DGroupType Type)
	{
		const int32 TypeFirstVertex = Groups[static_cast<int32>(Type)].FirstVertex;
		const int32 TypeLastVertex = Groups[static_cast<int32>(Type) + 1].FirstVertex;

		for (int32 Index = TypeFirstVertex; Index < TypeLastVertex; Index++)
		{
			const FVector Position = VertexPositions[FVertexID(Index)];
			const FVector2D TextureCoordinate = (FVector2D(Position.Y, Position.Z) - Box.Min) / Box.Max;
			VertexInstanceUVs[FVertexInstanceID(Index)] = { TextureCoordinate.X, 1.f - TextureCoordinate.Y };
		}
	};

	SetTextureCoordinates(EText3DGroupType::Front);
	SetTextureCoordinates(EText3DGroupType::Bevel);
}

void FMeshCreator::MirrorGroups(const float Extrude)
{
	MirrorGroup(EText3DGroupType::Front, EText3DGroupType::Back, Extrude);
	MirrorGroup(EText3DGroupType::Bevel, EText3DGroupType::Bevel, Extrude);
}

void FMeshCreator::BuildMesh(UStaticMesh* StaticMesh, class UMaterial* DefaultMaterial)
{
	Glyph->Build(StaticMesh, DefaultMaterial);
}

void FMeshCreator::CreateFrontMesh(const TSharedContourNode Root)
{
	int32 VertexCount = 0;
	AddToVertexCount(Root, &VertexCount);

	Data->SetCurrentGroup(EText3DGroupType::Front);
	Data->ResetDoneExtrude();
	Data->SetMinBevelTarget();
	Contours = MakeShared<FContourList>();

	int32 VertexIndex = Data->AddVertices(VertexCount);
	TriangulateAndConvert(Root, &VertexIndex);

	Contours->Initialize();
}

void FMeshCreator::CreateBevelMesh(const float Bevel, const EText3DBevelType Type, const int32 BevelSegments)
{
	Data->SetCurrentGroup(EText3DGroupType::Bevel);

	if (FMath::IsNearlyZero(Bevel))
	{
		return;
	}

	switch (Type)
	{
	case EText3DBevelType::Linear:
	{
		BevelLinearWithSegments(Bevel, Bevel, BevelSegments, FVector2D(1.f, -1.f).GetSafeNormal());
		break;
	}
	case EText3DBevelType::Convex:
	{
		BevelCurve(HALF_PI, BevelSegments, [Bevel](const float CosCurr, const float SinCurr, const float CosNext, const float SinNext)
		{
				return FVector2D(CosCurr - CosNext, SinNext - SinCurr) * Bevel;
			});
		break;
	}
	case EText3DBevelType::Concave:
		{
		BevelCurve(HALF_PI, BevelSegments, [Bevel](const float CosCurr, const float SinCurr, const float CosNext, const float SinNext)
			{
				return FVector2D(SinNext - SinCurr, CosCurr - CosNext) * Bevel;
			});
		break;
			}
	case EText3DBevelType::HalfCircle:
	{
		BevelCurve(PI, BevelSegments, [Bevel](const float CosCurr, const float SinCurr, const float CosNext, const float SinNext)
		{
			return FVector2D(SinCurr - SinNext, CosCurr - CosNext) * Bevel;
		});
		break;
		}
	case EText3DBevelType::OneStep:
	{
		BevelWithSteps(Bevel, 1, BevelSegments);
		break;
	}
	case EText3DBevelType::TwoSteps:
	{
		BevelWithSteps(Bevel, 2, BevelSegments);
		break;
	}
	case EText3DBevelType::Engraved:
	{
		BevelLinearWithSegments(-Bevel, 0.f, BevelSegments, FVector2D(-1.f, 0.f));
		BevelLinearWithSegments(0.f, Bevel, BevelSegments, FVector2D(0.f, -1.f));
		BevelLinearWithSegments(Bevel, 0.f, BevelSegments, FVector2D(1.f, 0.f));
		break;
	}
	default:
		break;
	}
}

void FMeshCreator::CreateExtrudeMesh(float Extrude, float Bevel, const EText3DBevelType Type)
{
	Data->SetCurrentGroup(EText3DGroupType::Extrude);

	if (Type != EText3DBevelType::HalfCircle)
	{
		Bevel = FMath::Clamp(Bevel, 0.0f, Extrude / 2.f);
	}

	if (Type != EText3DBevelType::HalfCircle && Type != EText3DBevelType::Engraved)
	{
	Extrude -= Bevel * 2.0f;
	}

	Data->SetExpandTotal(Bevel);
	Data->SetExtrude(Extrude);
	Data->SetExpand(0.f);

	const FVector2D Normal(1.f, 0.f);
	Data->SetNormals(Normal, Normal);

	for (FContour& Contour : *Contours)
	{
		for (const FPartPtr& Part : Contour)
		{
			Part->ResetDoneExpand();
		}
	}


	TArray<float> TextureCoordinateVs;

	for (FContour& Contour : *Contours)
	{
		// Compute TexCoord.V-s for each point
		TextureCoordinateVs.Reset(Contour.Num() - 1);
		const FPartPtr First = Contour[0];
		TextureCoordinateVs.Add(First->Length());

		int32 Index = 1;
		for (FPartConstPtr Edge = First->Next; Edge != First->Prev; Edge = Edge->Next)
		{
			TextureCoordinateVs.Add(TextureCoordinateVs[Index - 1] + Edge->Length());
			Index++;
		}


		const float ContourLength = TextureCoordinateVs.Last() + Contour.Last()->Length();

		if (FMath::IsNearlyZero(ContourLength))
		{
			continue;
		}


		for (float& PointY : TextureCoordinateVs)
		{
			PointY /= ContourLength;
		}

		// Duplicate contour
		Data->SetMinBevelTarget();
		const bool bFirstSmooth = First->bSmooth;
		// It's set to sharp because we need 2 vertices with TexCoord.Y values 0 and 1 (for smooth points only one vertex is added)
		First->bSmooth = false;

		// First point in contour is processed separately
		{
			EmptyPaths(First);
			ExpandPointWithoutAddingVertices(First);

			const FVector2D TexCoordPrev(0.f, 0.f);
			const FVector2D TexCoordCurr(0.f, 1.f);

			if (bFirstSmooth)
			{
				AddVertexSmooth(First, TexCoordPrev);
				AddVertexSmooth(First, TexCoordCurr);
			}
			else
			{
				AddVertexSharp(First, First->Prev, TexCoordPrev);
				AddVertexSharp(First, First, TexCoordCurr);
			}
		}

		Index = 1;
		for (FPartPtr Point = First->Next; Point != First; Point = Point->Next)
		{
			EmptyPaths(Point);
			ExpandPoint(Point, {0.f, 1.f - TextureCoordinateVs[Index++ - 1]});
		}


		// Add extruded vertices
		Data->SetMaxBevelTarget();

		// Similarly to duplicating vertices, first point is processed separately
		{
			ExpandPointWithoutAddingVertices(First);

			const FVector2D TexCoordPrev(1.f, 0.f);
			const FVector2D TexCoordCurr(1.f, 1.f);

			if (bFirstSmooth)
			{
				AddVertexSmooth(First, TexCoordPrev);
				AddVertexSmooth(First, TexCoordCurr);
			}
			else
			{
				AddVertexSharp(First, First->Prev, TexCoordPrev);
				AddVertexSharp(First, First, TexCoordCurr);
			}
		}

		Index = 1;
		for (FPartPtr Point = First->Next; Point != First; Point = Point->Next)
		{
			ExpandPoint(Point, {1.f, 1.f - TextureCoordinateVs[Index++ - 1]});
		}

		for (const FPartPtr& Edge : Contour)
		{
			Data->FillEdge(Edge, false);
		}
	}
}

void FMeshCreator::MirrorGroup(const EText3DGroupType TypeIn, const EText3DGroupType TypeOut, const float Extrude)
{
	TText3DGroupList& Groups = Glyph->GetGroups();

	const FText3DPolygonGroup GroupIn = Groups[static_cast<int32>(TypeIn)];
	const FText3DPolygonGroup GroupNext = Groups[static_cast<int32>(TypeIn) + 1];

	const int32 VerticesInNum = GroupNext.FirstVertex - GroupIn.FirstVertex;
	const int32 TrianglesInNum = GroupNext.FirstTriangle - GroupIn.FirstTriangle;

	FMeshDescription& MeshDescription = Glyph->GetMeshDescription();
	const int32 TotalVerticesNum = MeshDescription.Vertices().Num();

	Data->SetGlyph(Glyph);
	Data->SetCurrentGroup(TypeOut);
	Data->AddVertices(VerticesInNum);

	FStaticMeshAttributes& StaticMeshAttributes = Glyph->GetStaticMeshAttributes();
	TVertexAttributesRef<FVector> VertexPositions = StaticMeshAttributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector> VertexNormals = StaticMeshAttributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector> VertexTangents = StaticMeshAttributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<FVector2D> VertexUVs = StaticMeshAttributes.GetVertexInstanceUVs();

	for (int32 VertexIndex = 0; VertexIndex < VerticesInNum; VertexIndex++)
	{
		const FVertexID VertexID(GroupIn.FirstVertex + VertexIndex);
		const FVertexInstanceID InstanceID(static_cast<uint32>(VertexID.GetValue()));

		const FVector Position = VertexPositions[VertexID];
		const FVector Normal = VertexNormals[InstanceID];
		const FVector Tangent = VertexTangents[InstanceID];

		Data->AddVertex({ Extrude - Position.X, Position.Y, Position.Z }, { -Tangent.X, Tangent.Y, Tangent.Z }, { -Normal.X, Normal.Y, Normal.Z }, VertexUVs[InstanceID]);
	}

	Data->AddTriangles(TrianglesInNum);

	for (int32 TriangleIndex = 0; TriangleIndex < TrianglesInNum; TriangleIndex++)
	{
		const FMeshTriangle& Triangle = MeshDescription.Triangles()[FTriangleID(GroupIn.FirstTriangle + TriangleIndex)];

		uint32 Instance0 = static_cast<uint32>(TotalVerticesNum + Triangle.GetVertexInstanceID(0).GetValue() - GroupIn.FirstVertex);
		uint32 Instance2 = static_cast<uint32>(TotalVerticesNum + Triangle.GetVertexInstanceID(2).GetValue() - GroupIn.FirstVertex);
		uint32 Instance1 = static_cast<uint32>(TotalVerticesNum + Triangle.GetVertexInstanceID(1).GetValue() - GroupIn.FirstVertex);
		Data->AddTriangle(Instance0, Instance2, Instance1);
	}
}

void FMeshCreator::AddToVertexCount(const TSharedContourNode Node, int32* const VertexCount)
{
	for (const TSharedContourNode Child : Node->Children)
	{
		*VertexCount += Child->Contour->VertexCount();
		AddToVertexCount(Child, VertexCount);
	}
}

void FMeshCreator::TriangulateAndConvert(const TSharedContourNode Node, int32* const VertexIndex)
{
	// If this is solid region
	if (!Node->bClockwise)
	{
		int32 VertexCount = 0;
		FConstrainedDelaunay2f Triangulation;
		Triangulation.FillRule = FConstrainedDelaunay2f::EFillRule::Positive;

		const TSharedPtr<FContourList> ContoursLocal = Contours;
		const TSharedRef<class FData> DataLocal = Data;
		auto ProcessContour = [ContoursLocal, DataLocal, &VertexCount, &Triangulation](const TSharedContourNode NodeIn)
		{
			// Create contour in old format
			FContour& Contour = ContoursLocal->Add();
			const FPolygon2f& Polygon = *NodeIn->Contour;

			for (const FVector2f Vertex : Polygon.GetVertices())
			{
				// Add point to contour in old format
				FPartPtr Point = MakeShared<FPart>();
				Contour.Add(Point);
				Point->Position = FVector2D(Vertex);
				// Add point to mesh
				DataLocal->AddVertex({Vertex.X, Vertex.Y}, {1.f, 0.f}, {-1.f, 0.f, 0.f});
			}

			VertexCount += Polygon.VertexCount();
			// Add contour to triangulation
			Triangulation.Add(Polygon, NodeIn->bClockwise);
		};


		// Outter
		ProcessContour(Node);

		// Holes
		for (const TSharedContourNode Child : Node->Children)
		{
			ProcessContour(Child);
		}


		Triangulation.Triangulate();
		const TArray<FIndex3i>& Triangles = Triangulation.Triangles;
		Data->AddTriangles(Triangles.Num());

		for (const FIndex3i& Triangle : Triangles)
		{
			Data->AddTriangle(*VertexIndex + Triangle.A, *VertexIndex + Triangle.C, *VertexIndex + Triangle.B);
		}

		*VertexIndex += VertexCount;
	}

	// Continue with children
	for (const TSharedContourNode Child : Node->Children)
	{
		TriangulateAndConvert(Child, VertexIndex);
	}
}

void FMeshCreator::BevelLinearWithSegments(const float Extrude, const float Expand, const int32 BevelSegments, const FVector2D Normal)
{
	for (int32 Index = 0; Index < BevelSegments; Index++)
	{
		BevelLinear(Extrude / BevelSegments, Expand / BevelSegments, Normal, Normal, false);
	}
}

void FMeshCreator::BevelCurve(const float Angle, const int32 BevelSegments, TFunction<FVector2D(const float CurrentCos, const float CurrentSin, const float NextCos, const float Next)> ComputeOffset)
{
	float CosCurr = 0.0f;
	float SinCurr = 0.0f;

	float CosNext = 0.0f;
	float SinNext = 0.0f;

	FVector2D OffsetNext;
	bool bSmoothNext = false;

	FVector2D NormalNext;
	FVector2D NormalEnd;

	auto UpdateAngle = [Angle, &CosNext, &SinNext, BevelSegments](const int32 Index)
	{
		const float Step = Angle / BevelSegments;
		FMath::SinCos(&SinNext, &CosNext, Index * Step);
	};

	auto MakeStep = [UpdateAngle, &OffsetNext, ComputeOffset, &CosCurr, &SinCurr, &CosNext, &SinNext, &NormalNext](int32 Index)
	{
		UpdateAngle(Index);
		OffsetNext = ComputeOffset(CosCurr, SinCurr, CosNext, SinNext);
		NormalNext = FVector2D(OffsetNext.X, -OffsetNext.Y).GetSafeNormal();
	};


	UpdateAngle(0);

	CosCurr = CosNext;
	SinCurr = SinNext;

	MakeStep(1);
	for (int32 Index = 0; Index < BevelSegments; Index++)
	{
		CosCurr = CosNext;
		SinCurr = SinNext;

		const FVector2D OffsetCurr = OffsetNext;

		const FVector2D NormalCurr = NormalNext;
		FVector2D NormalStart;

		const bool bFirst = (Index == 0);
		const bool bLast = (Index == BevelSegments - 1);

		const bool bSmooth = bSmoothNext;

		if (!bLast)
		{
			MakeStep(Index + 2);
			bSmoothNext = FVector2D::DotProduct(NormalCurr, NormalNext) >= -FPart::CosMaxAngleSides;
		}

		NormalStart = bFirst ? NormalCurr : (bSmooth ? NormalEnd : NormalCurr);
		NormalEnd = bLast ? NormalCurr : (bSmoothNext ? (NormalCurr + NormalNext).GetSafeNormal() : NormalCurr);

		BevelLinear(OffsetCurr.X, OffsetCurr.Y, NormalStart, NormalEnd, bSmooth);
	}
}

void FMeshCreator::BevelWithSteps(const float Bevel, const int32 Steps, const int32 BevelSegments)
{
	const float BevelPerStep = Bevel / Steps;

	for (int32 Step = 0; Step < Steps; Step++)
	{
		BevelLinearWithSegments(BevelPerStep, 0.f, BevelSegments, FVector2D(1.f, 0.f));
		BevelLinearWithSegments(0.f, BevelPerStep, BevelSegments, FVector2D(0.f, -1.f));
	}
}

void FMeshCreator::BevelLinear(const float Extrude, const float Expand, FVector2D NormalStart, FVector2D NormalEnd, const bool bSmooth)
{
	Reset(Extrude, Expand, NormalStart, NormalEnd);

	if (!bSmooth)
	{
		DuplicateContourVertices();
	}

	BevelPartsWithoutIntersectingNormals();

	Data->IncreaseDoneExtrude();
}

void FMeshCreator::DuplicateContourVertices()
{
	Data->SetMinBevelTarget();

	for (FContour& Contour : *Contours)
	{
		for (const FPartPtr& Point : Contour)
		{
			EmptyPaths(Point);
			// Duplicate points of contour (expansion with value 0)
			ExpandPoint(Point);
		}
	}
}

void FMeshCreator::Reset(const float Extrude, const float Expand, FVector2D NormalStart, FVector2D NormalEnd)
{
	Data->SetExtrude(Extrude);
	Data->SetExpand(Expand);

	Data->SetNormals(NormalStart, NormalEnd);
	Contours->Reset();
}

void FMeshCreator::BevelPartsWithoutIntersectingNormals()
{
	Data->SetMaxBevelTarget();
	const float MaxExpand = Data->GetExpand();

	for (FContour& Contour : *Contours)
	{
		for (const FPartPtr& Point : Contour)
		{
			if (!FMath::IsNearlyEqual(Point->DoneExpand, MaxExpand) || FMath::IsNearlyZero(MaxExpand))
			{
				ExpandPoint(Point);
			}

			const float Delta = MaxExpand - Point->DoneExpand;

			Point->AvailableExpandNear -= Delta;
			Point->DecreaseExpandsFar(Delta);
		}

		for (const FPartPtr& Edge : Contour)
		{
			Data->FillEdge(Edge, false);
		}
	}
}

void FMeshCreator::EmptyPaths(const FPartPtr Point) const
{
	Point->PathPrev.Empty();
	Point->PathNext.Empty();
}

void FMeshCreator::ExpandPoint(const FPartPtr Point, const FVector2D TextureCoordinates)
{
	ExpandPointWithoutAddingVertices(Point);

	if (Point->bSmooth)
	{
		AddVertexSmooth(Point, TextureCoordinates);
	}
	else
	{
		AddVertexSharp(Point, Point->Prev, TextureCoordinates);
		AddVertexSharp(Point, Point, TextureCoordinates);
	}
}

void FMeshCreator::ExpandPointWithoutAddingVertices(const FPartPtr Point) const
{
	Point->Position = Data->Expanded(Point);
	const int32 FirstAdded = Data->AddVertices(Point->bSmooth ? 1 : 2);

	Point->PathPrev.Add(FirstAdded);
	Point->PathNext.Add(Point->bSmooth ? FirstAdded : FirstAdded + 1);
}

void FMeshCreator::AddVertexSmooth(const FPartConstPtr Point, const FVector2D TextureCoordinates)
{
	const FPartConstPtr Curr = Point;
	const FPartConstPtr Prev = Point->Prev;

	Data->AddVertex(Point, (Prev->TangentX + Curr->TangentX).GetSafeNormal(), (Data->ComputeTangentZ(Prev, Point->DoneExpand) + Data->ComputeTangentZ(Curr, Point->DoneExpand)).GetSafeNormal(), TextureCoordinates);
}

void FMeshCreator::AddVertexSharp(const FPartConstPtr Point, const FPartConstPtr Edge, const FVector2D TextureCoordinates)
{
	Data->AddVertex(Point, Edge->TangentX, Data->ComputeTangentZ(Edge, Point->DoneExpand).GetSafeNormal(), TextureCoordinates);
}
