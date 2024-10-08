// Copyright Epic Games, Inc. All Rights Reserved.

#include "VoxelSolidifyMeshesTool.h"
#include "CompositionOps/VoxelSolidifyMeshesOp.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "Selection/ToolSelectionUtil.h"

#include "DynamicMesh3.h"
#include "MeshTransforms.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "InteractiveGizmoManager.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "CompositionOps/VoxelSolidifyMeshesOp.h"


#define LOCTEXT_NAMESPACE "UVoxelSolidifyMeshesTool"



void UVoxelSolidifyMeshesTool::SetupProperties()
{
	Super::SetupProperties();
	SolidifyProperties = NewObject<UVoxelSolidifyMeshesToolProperties>(this);
	SolidifyProperties->RestoreProperties(this);
	AddToolPropertySource(SolidifyProperties);
}


void UVoxelSolidifyMeshesTool::SaveProperties()
{
	Super::SaveProperties();
	SolidifyProperties->SaveProperties(this);
}


TUniquePtr<FDynamicMeshOperator> UVoxelSolidifyMeshesTool::MakeNewOperator()
{
	TUniquePtr<FVoxelSolidifyMeshesOp> Op = MakeUnique<FVoxelSolidifyMeshesOp>();

	Op->Transforms.SetNum(ComponentTargets.Num());
	Op->Meshes.SetNum(ComponentTargets.Num());
	for (int Idx = 0; Idx < ComponentTargets.Num(); Idx++)
	{
		Op->Meshes[Idx] = OriginalDynamicMeshes[Idx];
		Op->Transforms[Idx] = TransformProxies[Idx]->GetTransform();
	}

	Op->bSolidAtBoundaries = SolidifyProperties->bSolidAtBoundaries;
	Op->WindingThreshold = SolidifyProperties->WindingThreshold;
	Op->bMakeOffsetSurfaces = SolidifyProperties->bMakeOffsetSurfaces;
	Op->OffsetThickness = SolidifyProperties->OffsetThickness;
	Op->SurfaceSearchSteps = SolidifyProperties->SurfaceSearchSteps;
	Op->ExtendBounds = SolidifyProperties->ExtendBounds;
	VoxProperties->SetPropertiesOnOp(*Op);
	
	return Op;
}



FString UVoxelSolidifyMeshesTool::GetCreatedAssetName() const
{
	return TEXT("Solid");
}

FText UVoxelSolidifyMeshesTool::GetActionName() const
{
	return LOCTEXT("VoxelSolidifyMeshes", "Voxel Solidify");
}









#undef LOCTEXT_NAMESPACE
