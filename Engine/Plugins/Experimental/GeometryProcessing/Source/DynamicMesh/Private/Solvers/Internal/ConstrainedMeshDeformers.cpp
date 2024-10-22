// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConstrainedMeshDeformers.h"
#include "MatrixSolver.h"
#include "ConstrainedPoissonSolver.h"


FConstrainedMeshDeformer::FConstrainedMeshDeformer(const FDynamicMesh3& DynamicMesh, const ELaplacianWeightScheme LaplacianType)
	: FConstrainedMeshDeformationSolver(DynamicMesh, LaplacianType, EMatrixSolverType::LU)
	, LaplacianVectors(FConstrainedMeshDeformationSolver::InternalVertexCount)
{


	// The current vertex positions 

	// Note: the OriginalInteriorPositions are being stored as member data 
	// for use if the solver is iterative.
	// FSOAPositions OriginalInteriorPositions; 
	ExtractInteriorVertexPositions(DynamicMesh, OriginalInteriorPositions);


	// The biharmonic part of the constrained solver
	//   Biharmonic := Laplacian^{T} * Laplacian

	const auto& Biharmonic = ConstrainedSolver->Biharmonic();

	// Compute the Laplacian Vectors
	//    := Biharmonic * VertexPostion
	// In the case of the cotangent laplacian this can be identified as the mean curvature * normal.
	checkSlow(LaplacianVectors.Num() == OriginalInteriorPositions.Num());

	for (int32 i = 0; i < 3; ++i)
	{
		LaplacianVectors.Array(i) = Biharmonic * OriginalInteriorPositions.Array(i);
	}
}


bool FConstrainedMeshDeformer::Deform(TArray<FVector3d>& PositionBuffer)
{

	// Update constraints.  This only trigger solver rebuild if the weights were updated.
	UpdateSolverConstraints();

	// Allocate space for the result as a struct of arrays
	FSOAPositions SolutionVector(InternalVertexCount);

	// Solve the linear system
	// NB: the original positions will only be used if the underlying solver type is iterative	
	bool bSuccess = ConstrainedSolver->SolveWithGuess(OriginalInteriorPositions, LaplacianVectors, SolutionVector);

	// Move any vertices to match bPostFix constraints

	UpdateWithPostFixConstraints(SolutionVector);

	// Allocate Position Buffer for random access writes
	int32 MaxVtxId = VtxLinearization.ToId().Num();
	PositionBuffer.Empty(MaxVtxId);
	PositionBuffer.AddUninitialized(MaxVtxId);

	// Export the computed internal positions:
	// Copy the results into the array of structs form.  
	// NB: this re-indexes so the results can be looked up using VtxId

	CopyInternalPositions(SolutionVector, PositionBuffer);

	// Copy the boundary
	// NB: this re-indexes so the results can be looked up using VtxId
	CopyBoundaryPositions(PositionBuffer);

	// the matrix solve state
	return bSuccess;

}
