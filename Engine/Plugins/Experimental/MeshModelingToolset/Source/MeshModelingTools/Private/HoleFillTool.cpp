// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoleFillTool.h"
#include "ToolBuilderUtil.h"
#include "InteractiveToolManager.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "ToolSetupUtil.h"
#include "MeshNormals.h"
#include "Changes/DynamicMeshChangeTarget.h"
#include "DynamicMeshToMeshDescription.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "MeshBoundaryLoops.h"
#include "MeshOpPreviewHelpers.h"
#include "Selection/PolygonSelectionMechanic.h"

#define LOCTEXT_NAMESPACE "UHoleFillTool"

/*
 * ToolBuilder
 */

bool UHoleFillToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UHoleFillToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);

	UHoleFillTool* NewTool = NewObject<UHoleFillTool>(SceneState.ToolManager);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));
	NewTool->SetWorld(SceneState.World);

	return NewTool;
}

/*
 * Tool properties
 */
void UHoleFillToolActions::PostAction(EHoleFillToolActions Action)
{
	if (ParentTool.IsValid())
	{
		ParentTool->RequestAction(Action);
	}
}


void UHoleFillStatisticsProperties::Initialize(const UHoleFillTool& HoleFillTool)
{
	if (HoleFillTool.Topology == nullptr)
	{
		return;
	}

	int Initial = HoleFillTool.Topology->Edges.Num();
	int Selected = 0;
	int Successful = 0;
	int Failed = 0;
	int Remaining = Initial;
	
	InitialHoles = FString::FromInt(Initial);
	SelectedHoles = FString::FromInt(Selected);
	SuccessfulFills = FString::FromInt(Successful);
	FailedFills = FString::FromInt(Failed);
	RemainingHoles = FString::FromInt(Remaining);
}

void UHoleFillStatisticsProperties::Update(const UHoleFillTool& HoleFillTool, const FHoleFillOp& Op)
{
	if (HoleFillTool.Topology == nullptr)
	{
		return;
	}

	int Initial = HoleFillTool.Topology->Edges.Num();
	int Selected = Op.Loops.Num();
	int Failed = Op.NumFailedLoops;
	int Successful = Selected - Failed;
	int Remaining = Initial - Successful;

	InitialHoles = FString::FromInt(Initial);
	SelectedHoles = FString::FromInt(Selected);
	SuccessfulFills = FString::FromInt(Successful);
	FailedFills = FString::FromInt(Failed);
	RemainingHoles = FString::FromInt(Remaining);
}

/*
* Op Factory
*/

TUniquePtr<FDynamicMeshOperator> UHoleFillOperatorFactory::MakeNewOperator()
{
	TUniquePtr<FHoleFillOp> FillOp = MakeUnique<FHoleFillOp>();

	FTransform LocalToWorld = FillTool->ComponentTarget->GetWorldTransform();
	FillOp->SetResultTransform((FTransform3d)LocalToWorld);
	FillOp->OriginalMesh = FillTool->OriginalMesh;
	FillOp->MeshUVScaleFactor = FillTool->MeshUVScaleFactor;
	FillTool->GetLoopsToFill(FillOp->Loops);
	FillOp->FillType = FillTool->Properties->FillType;

	FillOp->FillOptions.bRemoveIsolatedTriangles = FillTool->Properties->bRemoveIsolatedTriangles;

	// Smooth fill properties
	FillOp->SmoothFillOptions = FillTool->SmoothHoleFillProperties->ToSmoothFillOptions();

	return FillOp;
}


/*
 * Tool
 */

void UHoleFillTool::Setup()
{
	USingleSelectionTool::Setup();

	if (!ComponentTarget)
	{
		return;
	}

	// create mesh to operate on
	OriginalMesh = MakeShared<FDynamicMesh3>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(ComponentTarget->GetMesh(), *OriginalMesh);

	// initialize properties
	Properties = NewObject<UHoleFillToolProperties>(this, TEXT("Hole Fill Settings"));
	Properties->RestoreProperties(this);
	AddToolPropertySource(Properties);
	SetToolPropertySourceEnabled(Properties, true);

	SmoothHoleFillProperties = NewObject<USmoothHoleFillProperties>(this, TEXT("Smooth Fill Settings"));
	SmoothHoleFillProperties->RestoreProperties(this);
	AddToolPropertySource(SmoothHoleFillProperties);
	SetToolPropertySourceEnabled(SmoothHoleFillProperties, Properties->FillType == EHoleFillOpFillType::Smooth);

	// Set up a callback for when the type of fill changes
	Properties->WatchProperty(Properties->FillType,
		[this](EHoleFillOpFillType NewType)
	{
		SetToolPropertySourceEnabled(SmoothHoleFillProperties, (NewType == EHoleFillOpFillType::Smooth));
	});

	Actions = NewObject<UHoleFillToolActions>(this, TEXT("Hole Fill Actions"));
	Actions->Initialize(this);
	AddToolPropertySource(Actions);
	SetToolPropertySourceEnabled(Actions, true);

	Statistics = NewObject<UHoleFillStatisticsProperties>();
	AddToolPropertySource(Statistics);
	SetToolPropertySourceEnabled(Statistics, true);

	ToolPropertyObjects.Add(this);

	// click behavior
	USingleClickInputBehavior* ClickBehavior = NewObject<USingleClickInputBehavior>();
	ClickBehavior->Initialize(this);
	AddInputBehavior(ClickBehavior);

	// hover behavior
	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>();
	HoverBehavior->Initialize(this);
	AddInputBehavior(HoverBehavior);

	// initialize hit query
	MeshSpatial.SetMesh(OriginalMesh.Get());

	// initialize topology
	Topology = MakeUnique<FBasicTopology>(OriginalMesh.Get(), false);
	bool bTopologyOK = Topology->RebuildTopology();

	// Set up selection mechanic to find and select edges
	SelectionMechanic = NewObject<UPolygonSelectionMechanic>(this);
	SelectionMechanic->bAddSelectionFilterPropertiesToParentTool = false;
	SelectionMechanic->Setup(this);
	SelectionMechanic->Properties->bSelectEdges = true;
	SelectionMechanic->Properties->bSelectFaces = false;
	SelectionMechanic->Properties->bSelectVertices = false;
	SelectionMechanic->Initialize(OriginalMesh.Get(),
		ComponentTarget->GetWorldTransform(),
		TargetWorld,
		Topology.Get(),
		[this]() { return &MeshSpatial; },
		[]() { return true; }	// allow adding to selection without modifier key
	);
	
	// Store a UV scale based on the original mesh bounds
	MeshUVScaleFactor = (1.0 / OriginalMesh->GetBounds().MaxDim());

	Statistics->Initialize(*this);

	// initialize the PreviewMesh+BackgroundCompute object
	SetupPreview();
	InvalidatePreviewResult();

	if (!bTopologyOK)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("LoopFindError", "Error finding hole boundary loops."),
			EToolMessageLevel::UserWarning);

		SetToolPropertySourceEnabled(Properties, false);
		SetToolPropertySourceEnabled(SmoothHoleFillProperties, false);
		SetToolPropertySourceEnabled(Actions, false);
	}
	else if (Topology->Edges.Num() == 0)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("NoHoleNotification", "This mesh has no holes to fill."),
			EToolMessageLevel::UserWarning);

		SetToolPropertySourceEnabled(Properties, false);
		SetToolPropertySourceEnabled(SmoothHoleFillProperties, false);
		SetToolPropertySourceEnabled(Actions, false);
	}
	else
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("HoleFillToolDescription", 
				"Holes in the mesh are highlighted. Select individual holes to fill or use the Select All or Clear buttons."),
			EToolMessageLevel::UserNotification);

		// Hide all meshes except the Preview
		ComponentTarget->SetOwnerVisibility(false);
	}

}

void UHoleFillTool::OnTick(float DeltaTime)
{
	if (Preview)
	{
		Preview->Tick(DeltaTime);
	}

	if (bHavePendingAction)
	{
		ApplyAction(PendingAction);
		bHavePendingAction = false;
		PendingAction = EHoleFillToolActions::NoAction;
	}
}

void UHoleFillTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	InvalidatePreviewResult();
}

bool UHoleFillTool::CanAccept() const
{
	return Preview->HaveValidResult();
}

void UHoleFillTool::Shutdown(EToolShutdownType ShutdownType)
{
	Properties->SaveProperties(this);
	SmoothHoleFillProperties->SaveProperties(this);

	if (SelectionMechanic)
	{
		SelectionMechanic->Shutdown();
	}

	ComponentTarget->SetOwnerVisibility(true);

	FDynamicMeshOpResult Result = Preview->Shutdown();
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("HoleFillToolTransactionName", "Hole Fill Tool"));

		check(Result.Mesh.Get() != nullptr);
		ComponentTarget->CommitMesh([&Result](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;

			// full conversion if normal topology changed or faces were inverted
			Converter.Convert(Result.Mesh.Get(), *CommitParams.MeshDescription);
		});

		GetToolManager()->EndUndoTransaction();
	}
}

FInputRayHit UHoleFillTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	FHitResult OutHit;
	if (SelectionMechanic->TopologyHitTest(ClickPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}

	return FInputRayHit(TNumericLimits<float>::Max());
}

void UHoleFillTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	// update selection
	GetToolManager()->BeginUndoTransaction(LOCTEXT("PolyMeshSelectionChange", "Selection"));
	SelectionMechanic->BeginChange();

	FVector3d LocalHitPosition, LocalHitNormal;
	bool bSelectionModified = SelectionMechanic->UpdateSelection(ClickPos.WorldRay, LocalHitPosition, LocalHitNormal);

	if (bSelectionModified)
	{
		UpdateActiveBoundaryLoopSelection();
		InvalidatePreviewResult();
	}

	SelectionMechanic->EndChangeAndEmitIfModified();
	GetToolManager()->EndUndoTransaction();
}


FInputRayHit UHoleFillTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	FHitResult OutHit;
	if (SelectionMechanic->TopologyHitTest(PressPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}
	return FInputRayHit();
}

bool UHoleFillTool::OnUpdateHover(const FInputDeviceRay& DevicePos) 
{
	SelectionMechanic->UpdateHighlight(DevicePos.WorldRay);
	return true;
}

void UHoleFillTool::OnEndHover()
{
	SelectionMechanic->ClearHighlight();
}

void UHoleFillTool::RequestAction(EHoleFillToolActions ActionType)
{
	if (bHavePendingAction)
	{
		return;
	}

	PendingAction = ActionType;
	bHavePendingAction = true;
}

void UHoleFillTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}


void UHoleFillTool::InvalidatePreviewResult()
{
	// Clear any warning message
	GetToolManager()->DisplayMessage({}, EToolMessageLevel::UserWarning);
	Preview->InvalidateResult();
}

void UHoleFillTool::SetupPreview()
{
	UHoleFillOperatorFactory* OpFactory = NewObject<UHoleFillOperatorFactory>();
	OpFactory->FillTool = this;

	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(OpFactory, "Preview");
	Preview->Setup(this->TargetWorld, OpFactory);

	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	Preview->ConfigureMaterials(MaterialSet.Materials,
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);

	// configure secondary render material
	UMaterialInterface* SelectionMaterial = ToolSetupUtil::GetSelectionMaterial(FLinearColor(0.8f, 0.75f, 0.0f), GetToolManager());
	if (SelectionMaterial != nullptr)
	{
		Preview->PreviewMesh->SetSecondaryRenderMaterial(SelectionMaterial);
	}

	// enable secondary triangle buffers
	Preview->OnOpCompleted.AddLambda(
		[this](const FDynamicMeshOperator* Op)
	{
		const FHoleFillOp* HoleFillOp = (const FHoleFillOp*)(Op);
		NewTriangleIDs = TSet<int32>(HoleFillOp->NewTriangles);

		// Notify the user if any holes could not be filled
		if (HoleFillOp->NumFailedLoops > 0)
		{
			GetToolManager()->DisplayMessage(
				FText::Format(LOCTEXT("FillFailNotification", "Failed to fill {0} holes."), HoleFillOp->NumFailedLoops),
				EToolMessageLevel::UserWarning);
		}

		Statistics->Update(*this, *HoleFillOp);
	});

	Preview->PreviewMesh->EnableSecondaryTriangleBuffers(
		[this](const FDynamicMesh3* Mesh, int32 TriangleID)
	{
		return NewTriangleIDs.Contains(TriangleID);
	});

	// set initial preview to un-processed mesh
	Preview->PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
	Preview->PreviewMesh->UpdatePreview(OriginalMesh.Get());

	Preview->SetVisibility(true);
}


void UHoleFillTool::ApplyAction(EHoleFillToolActions ActionType)
{
	switch (ActionType)
	{
	case EHoleFillToolActions::SelectAll:
		SelectAll();
		break;
	case EHoleFillToolActions::ClearSelection:
		ClearSelection();
		break;
	}
}

void UHoleFillTool::SelectAll()
{
	FGroupTopologySelection NewSelection;
	for (int32 i = 0; i < Topology->Edges.Num(); ++i)
	{
		NewSelection.SelectedEdgeIDs.Add(i);
	}

	SelectionMechanic->SetSelection(NewSelection);
	UpdateActiveBoundaryLoopSelection();
	InvalidatePreviewResult();
}


void UHoleFillTool::ClearSelection()
{
	SelectionMechanic->ClearSelection();
	UpdateActiveBoundaryLoopSelection();
	InvalidatePreviewResult();
}

void UHoleFillTool::UpdateActiveBoundaryLoopSelection()
{
	ActiveBoundaryLoopSelection.Reset();

	const FGroupTopologySelection& ActiveSelection = SelectionMechanic->GetActiveSelection();
	int NumEdges = ActiveSelection.SelectedEdgeIDs.Num();
	if (NumEdges == 0)
	{
		return;
	}

	ActiveBoundaryLoopSelection.Reserve(NumEdges);
	for (int32 k = 0; k < NumEdges; ++k)
	{
		int32 EdgeID = ActiveSelection.SelectedEdgeIDs[k];
		if (Topology->IsBoundaryEdge(EdgeID))
		{
			FSelectedBoundaryLoop& Loop = ActiveBoundaryLoopSelection.Emplace_GetRef();
			Loop.EdgeTopoID = EdgeID;
			Loop.EdgeIDs = Topology->GetGroupEdgeEdges(EdgeID);
		}
	}
}


void UHoleFillTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (SelectionMechanic)
	{
		SelectionMechanic->Render(RenderAPI);
	}
}


void UHoleFillTool::GetLoopsToFill(TArray<FEdgeLoop>& OutLoops) const
{
	OutLoops.Reset();
	FMeshBoundaryLoops BoundaryLoops(OriginalMesh.Get());

	for (const FSelectedBoundaryLoop& FillEdge : ActiveBoundaryLoopSelection)
	{
		if (OriginalMesh->IsBoundaryEdge(FillEdge.EdgeIDs[0]))		// may no longer be boundary due to previous fill
		{
			int32 LoopID = BoundaryLoops.FindLoopContainingEdge(FillEdge.EdgeIDs[0]);
			if (LoopID >= 0)
			{
				OutLoops.Add(BoundaryLoops.Loops[LoopID]);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
