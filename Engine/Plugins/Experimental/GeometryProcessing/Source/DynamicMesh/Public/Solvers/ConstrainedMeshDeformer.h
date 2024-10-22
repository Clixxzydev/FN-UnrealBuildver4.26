// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"
#include "Solvers/ConstrainedMeshSolver.h"
#include "Solvers/MeshLaplacian.h"
#include "DynamicMesh3.h"

namespace UE
{
	namespace MeshDeformation
	{

		/**
		*  Solves the linear system for p_vec
		*
		*         ( Transpose(L) * L   + (0  0      )  ) p_vec = source_vec + ( 0              )
		*		  (                      (0 lambda^2)  )                      ( lambda^2 c_vec )
		*
		*   where:  L := laplacian for the mesh,
		*           source_vec := Transpose(L)*L mesh_vertex_positions
		*           lambda := weights
		*           c_vec := constrained positions
		*
		* Expected Use:
		*
		*   // Create Deformation Solver from Mesh
		*   TUniquePtr<IConstrainedMeshSolver>  MeshDeformer = ConstructConstrainedMeshDeformer(ELaplacianWeightScheme::ClampedCotangent, DynamicMesh);
		*
		*   // Add constraints.
		*   for..
		*   {
		*   	int32 VtxId = ..; double Weight = ..; FVector3d TargetPos = ..;  bool bPostFix = ...;
		*   	MeshDeformer->AddConstraint(VtxId, Weight, TargetPos, bPostFix);
		*   }
		*
		*   // Solve for new mesh vertex locations
		*   TArray<FVector3d> PositionBuffer;
		*   MeshDeformer->Deform(PositionBuffer);
		*
		*   // Update Mesh? for (int32 VtxId : DynamicMesh.VertexIndices()) DynamicMesh.SetVertex(VtxId, PositionBuffer[VtxId]);
		*   ...
		*
		*   // Update constraint positions.
		*   for ..
		*   {
		*   	int32 VtxId = ..;  FVector3d TargetPos = ..; bool bPostFix = ...;
		*	    MeshDeformer->UpdateConstraintPosition(VtxId, TargetPos, bPostFix);
		*   }
		*
		*   // Solve for new vertex locations.
		*   MeshDeformer->Deform(PositionBuffer);
		*   // Update Mesh?
		*/
		TUniquePtr<UE::Solvers::IConstrainedMeshSolver> DYNAMICMESH_API ConstructConstrainedMeshDeformer(const ELaplacianWeightScheme WeightScheme, const FDynamicMesh3& DynamicMesh);

	}
}