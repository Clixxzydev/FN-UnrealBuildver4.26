// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp SimpleHoleFiller

#pragma once

#include "HoleFiller.h"
#include "MathUtil.h"
#include "VectorTypes.h"
#include "GeometryTypes.h"
#include "MeshBoundaryLoops.h"


class FDynamicMesh3;


/**
 * Fill an EdgeLoop hole with triangles.
 * Supports two fill modes, either a fan connected to a new central vertex, or a triangulation of the boundary polygon
 */
class DYNAMICMESH_API FSimpleHoleFiller : public IHoleFiller
{
public:
	enum class EFillType
	{
		TriangleFan,
		PolygonEarClipping
	};

	//
	// Inputs
	//
	FDynamicMesh3 *Mesh;
	FEdgeLoop Loop;
	EFillType FillType = EFillType::TriangleFan;

	//
	// Outputs
	//
	int32 NewVertex = IndexConstants::InvalidID;

public:
	/**
	 *  Construct simple hole filler (just adds a central vertex and a triangle fan)
	 */
	FSimpleHoleFiller(FDynamicMesh3* Mesh, FEdgeLoop Loop, EFillType InFillType = EFillType::TriangleFan) : 
		Mesh(Mesh), 
		Loop(Loop), 
		FillType(InFillType)
	{
	}

	virtual ~FSimpleHoleFiller() {}
	
	
	/**
	 * @return EOperationValidationResult::Ok if we can apply operation, or error code if we cannot
	 */
	virtual EOperationValidationResult Validate()
	{
		if (!Loop.IsBoundaryLoop(Mesh))
		{
			return EOperationValidationResult::Failed_UnknownReason;
		}

		return EOperationValidationResult::Ok;
	}

	bool Fill(int32 GroupID = -1) override;	


protected:
	bool Fill_Fan(int32 NewGroupID);
	bool Fill_EarClip(int32 NewGroupID);
};
