// Copyright Epic Games, Inc. All Rights Reserved.

#include "DrawAndRevolveTool.h"

#include "AssetGenerationUtil.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "CompositionOps/CurveSweepOp.h"
#include "Generators/SweepGenerator.h"
#include "InteractiveToolManager.h" // To use SceneState.ToolManager
#include "Mechanics/ConstructionPlaneMechanic.h"
#include "Mechanics/CurveControlPointsMechanic.h"
#include "Properties/MeshMaterialProperties.h"
#include "Selection/ToolSelectionUtil.h"
#include "ToolSceneQueriesUtil.h"
#include "ToolSetupUtil.h"

#define LOCTEXT_NAMESPACE "UDrawAndRevolveTool"


const FText InitializationModeMessage = LOCTEXT("CurveInitialization", "Draw and a profile curve and it will be revolved around the purple draw plane axis. "
	"Ctrl+click repositions draw plane and axis. The curve is ended by clicking the end again or connecting to its start. Holding shift toggles snapping to "
	"be opposite the EnableSnapping setting.");
const FText EditModeMessage = LOCTEXT("CurveEditing", "Click points to select them, Shift+click to add/remove points to selection. Ctrl+click a segment "
	"to add a point, or select an endpoint and Ctrl+click somewhere on the plane to add to the ends. Backspace deletes selected points. Holding Shift "
	"toggles snapping to be opposite the EnableSnapping setting.");


// Tool builder

bool UDrawAndRevolveToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return (this->AssetAPI != nullptr);
}

UInteractiveTool* UDrawAndRevolveToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UDrawAndRevolveTool* NewTool = NewObject<UDrawAndRevolveTool>(SceneState.ToolManager);

	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);
	return NewTool;
}


// Operator factory

TUniquePtr<FDynamicMeshOperator> URevolveOperatorFactory::MakeNewOperator()
{
	TUniquePtr<FCurveSweepOp> CurveSweepOp = MakeUnique<FCurveSweepOp>();

	// Assemble profile curve
	CurveSweepOp->ProfileCurve.Reserve(RevolveTool->ControlPointsMechanic->GetNumPoints() + 2); // extra space for top/bottom caps
	RevolveTool->ControlPointsMechanic->ExtractPointPositions(CurveSweepOp->ProfileCurve);
	CurveSweepOp->bProfileCurveIsClosed = RevolveTool->ControlPointsMechanic->GetIsLoop();

	// If we are capping the top and bottom, we just add a couple extra vertices and mark the curve as being closed
	if (!CurveSweepOp->bProfileCurveIsClosed && RevolveTool->Settings->bConnectOpenProfileToAxis)
	{
		// Project first and last points onto the revolution axis.
		FVector3d FirstPoint = CurveSweepOp->ProfileCurve[0];
		FVector3d LastPoint = CurveSweepOp->ProfileCurve.Last();
		double DistanceAlongAxis = RevolveTool->RevolutionAxisDirection.Dot(LastPoint - RevolveTool->RevolutionAxisOrigin);
		FVector3d ProjectedPoint = RevolveTool->RevolutionAxisOrigin + (RevolveTool->RevolutionAxisDirection * DistanceAlongAxis);

		CurveSweepOp->ProfileCurve.Add(ProjectedPoint);

		DistanceAlongAxis = RevolveTool->RevolutionAxisDirection.Dot(FirstPoint - RevolveTool->RevolutionAxisOrigin);
		ProjectedPoint = RevolveTool->RevolutionAxisOrigin + (RevolveTool->RevolutionAxisDirection * DistanceAlongAxis);

		CurveSweepOp->ProfileCurve.Add(ProjectedPoint);
		CurveSweepOp->bProfileCurveIsClosed = true;
	}

	RevolveTool->Settings->ApplyToCurveSweepOp(*RevolveTool->MaterialProperties,
		RevolveTool->RevolutionAxisOrigin, RevolveTool->RevolutionAxisDirection, *CurveSweepOp);

	return CurveSweepOp;
}


// Tool itself

void UDrawAndRevolveTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 1,
		TEXT("DeletePoint"),
		LOCTEXT("DeletePointUIName", "Delete Point"),
		LOCTEXT("DeletePointTooltip", "Delete currently selected point(s)"),
		EModifierKey::None, EKeys::BackSpace,
		[this]() { OnBackspacePress(); });
}

void UDrawAndRevolveTool::OnBackspacePress()
{
	// TODO: Someday we'd like the mechanic to register the action itself and not have to catch and forward
	// it in the tool.
	if (ControlPointsMechanic)
	{
		ControlPointsMechanic->DeleteSelectedPoints();
	}
}

bool UDrawAndRevolveTool::CanAccept() const
{
	return Preview != nullptr && Preview->HaveValidResult();
}

void UDrawAndRevolveTool::Setup()
{
	UInteractiveTool::Setup();

	GetToolManager()->DisplayMessage(InitializationModeMessage, EToolMessageLevel::UserNotification);

	Settings = NewObject<URevolveToolProperties>(this, TEXT("Revolve Tool Settings"));
	Settings->RestoreProperties(this);
	Settings->bAllowedToEditDrawPlane = true;
	AddToolPropertySource(Settings);

	MaterialProperties = NewObject<UNewMeshMaterialProperties>(this);
	AddToolPropertySource(MaterialProperties);
	MaterialProperties->RestoreProperties(this);

	ControlPointsMechanic = NewObject<UCurveControlPointsMechanic>(this);
	ControlPointsMechanic->Setup(this);
	ControlPointsMechanic->SetWorld(TargetWorld);
	ControlPointsMechanic->OnPointsChanged.AddLambda([this]() {
		if (Preview)
		{
			Preview->InvalidateResult();
		}
		Settings->bAllowedToEditDrawPlane = (ControlPointsMechanic->GetNumPoints() == 0);
		});
	// This gets called when we enter/leave curve initialization mode
	ControlPointsMechanic->OnModeChanged.AddLambda([this]() {
		if (ControlPointsMechanic->IsInInteractiveIntialization())
		{
			// We're back to initializing so hide the preview
			if (Preview)
			{
				Preview->Cancel();
				Preview = nullptr;
			}
			GetToolManager()->DisplayMessage(InitializationModeMessage, EToolMessageLevel::UserNotification);
		}
		else
		{
			StartPreview();
			GetToolManager()->DisplayMessage(EditModeMessage, EToolMessageLevel::UserNotification);
		}
		});
	ControlPointsMechanic->SetSnappingEnabled(Settings->bEnableSnapping);

	UpdateRevolutionAxis(Settings->DrawPlaneAndAxis);

	// The plane mechanic lets us update the plane in which we draw the profile curve, as long as we haven't
	// started adding points to it already.
	FFrame3d ProfileDrawPlane(Settings->DrawPlaneAndAxis);
	PlaneMechanic = NewObject<UConstructionPlaneMechanic>(this);
	PlaneMechanic->Setup(this);
	PlaneMechanic->Initialize(TargetWorld, ProfileDrawPlane);
	PlaneMechanic->UpdateClickPriority(ControlPointsMechanic->ClickBehavior->GetPriority().MakeHigher());
	PlaneMechanic->CanUpdatePlaneFunc = [this]() 
	{ 
		return ControlPointsMechanic->GetNumPoints() == 0;
	};
	PlaneMechanic->OnPlaneChanged.AddLambda([this]() {
		Settings->DrawPlaneAndAxis = PlaneMechanic->Plane.ToFTransform();
		if (ControlPointsMechanic)
		{
			ControlPointsMechanic->SetPlane(PlaneMechanic->Plane);
		}
		UpdateRevolutionAxis(Settings->DrawPlaneAndAxis);
		});
	PlaneMechanic->SetEnableGridSnaping(Settings->bSnapToWorldGrid);

	ControlPointsMechanic->SetPlane(PlaneMechanic->Plane);
	ControlPointsMechanic->SetInteractiveInitialization(true);
}

void UDrawAndRevolveTool::UpdateRevolutionAxis(const FTransform& PlaneTransform)
{
	RevolutionAxisOrigin = PlaneTransform.GetLocation();
	RevolutionAxisDirection = PlaneTransform.GetRotation().GetAxisX();

	const int32 AXIS_SNAP_TARGET_ID = 1;
	ControlPointsMechanic->RemoveSnapLine(AXIS_SNAP_TARGET_ID);
	ControlPointsMechanic->AddSnapLine(AXIS_SNAP_TARGET_ID, FLine3d(RevolutionAxisOrigin, RevolutionAxisDirection));
}

void UDrawAndRevolveTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);
	MaterialProperties->SaveProperties(this);

	PlaneMechanic->Shutdown();
	ControlPointsMechanic->Shutdown();

	if (Preview)
	{
		if (ShutdownType == EToolShutdownType::Accept)
		{
			GenerateAsset(Preview->Shutdown());
		}
		else
		{
			Preview->Cancel();
		}
	}
}

void UDrawAndRevolveTool::GenerateAsset(const FDynamicMeshOpResult& Result)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("RevolveToolTransactionName", "Revolve Tool"));

	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld, Result.Mesh.Get(), Result.Transform, TEXT("RevolveResult"), MaterialProperties->Material);

	if (NewActor != nullptr)
	{
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
	}

	GetToolManager()->EndUndoTransaction();
}

void UDrawAndRevolveTool::StartPreview()
{
	URevolveOperatorFactory* RevolveOpCreator = NewObject<URevolveOperatorFactory>();
	RevolveOpCreator->RevolveTool = this;

	// Normally we wouldn't give the object a name, but since we may destroy the preview using undo,
	// the ability to reuse the non-cleaned up memory is useful. Careful if copy-pasting this!
	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(RevolveOpCreator, "RevolveToolPreview");

	Preview->Setup(TargetWorld, RevolveOpCreator);
	Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

	Preview->ConfigureMaterials(MaterialProperties->Material,
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));
	Preview->PreviewMesh->EnableWireframe(MaterialProperties->bWireframe);

	Preview->SetVisibility(true);
	Preview->InvalidateResult();
}

void UDrawAndRevolveTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(URevolveToolProperties, DrawPlaneAndAxis)))
	{
		FFrame3d ProfileDrawPlane(Settings->DrawPlaneAndAxis); // Casting to FFrame3d
		ControlPointsMechanic->SetPlane(PlaneMechanic->Plane);
		PlaneMechanic->SetPlaneWithoutBroadcast(ProfileDrawPlane);
		UpdateRevolutionAxis(Settings->DrawPlaneAndAxis);
	}

	PlaneMechanic->SetEnableGridSnaping(Settings->bSnapToWorldGrid);
	ControlPointsMechanic->SetSnappingEnabled(Settings->bEnableSnapping);

	if (Preview)
	{
		if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNewMeshMaterialProperties, Material)))
		{
			Preview->ConfigureMaterials(MaterialProperties->Material,
				ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));
		}

		Preview->PreviewMesh->EnableWireframe(MaterialProperties->bWireframe);
		Preview->InvalidateResult();
	}
}


void UDrawAndRevolveTool::OnTick(float DeltaTime)
{
	if (PlaneMechanic != nullptr)
	{
		PlaneMechanic->Tick(DeltaTime);
	}

	if (Preview)
	{
		Preview->Tick(DeltaTime);
	}
}


void UDrawAndRevolveTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	if (PlaneMechanic != nullptr)
	{
		PlaneMechanic->Render(RenderAPI);

		// Draw the axis of rotation
		float PdiScale = CameraState.GetPDIScalingFactor();
		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

		FColor AxisColor(240, 16, 240);
		double AxisThickness = 1.0 * PdiScale;
		double AxisHalfLength = ToolSceneQueriesUtil::CalculateDimensionFromVisualAngleD(CameraState, RevolutionAxisOrigin, 90);

		FVector3d StartPoint = RevolutionAxisOrigin - (RevolutionAxisDirection * (AxisHalfLength * PdiScale));
		FVector3d EndPoint = RevolutionAxisOrigin + (RevolutionAxisDirection * (AxisHalfLength * PdiScale));

		PDI->DrawLine((FVector)StartPoint, (FVector)EndPoint, AxisColor, SDPG_Foreground, 
			AxisThickness, 0.0f, true);
	}

	if (ControlPointsMechanic != nullptr)
	{
		ControlPointsMechanic->Render(RenderAPI);
	}
}

#undef LOCTEXT_NAMESPACE
