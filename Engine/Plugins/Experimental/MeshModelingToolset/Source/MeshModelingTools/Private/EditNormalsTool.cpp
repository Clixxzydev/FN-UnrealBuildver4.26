// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditNormalsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "Selection/SelectClickedAction.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "InteractiveGizmoManager.h"

#include "AssetGenerationUtil.h"
#include "AssetUtils/MeshDescriptionUtil.h"
#include "Engine/Classes/Engine/StaticMesh.h"
#include "Engine/Classes/Components/StaticMeshComponent.h"


#define LOCTEXT_NAMESPACE "UEditNormalsTool"


/*
 * ToolBuilder
 */


bool UEditNormalsToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) > 0;
}

UInteractiveTool* UEditNormalsToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UEditNormalsTool* NewTool = NewObject<UEditNormalsTool>(SceneState.ToolManager);

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	check(Components.Num() > 0);

	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets;
	for (UActorComponent* ActorComponent : Components)
	{
		auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
		if ( MeshComponent )
		{
			ComponentTargets.Add(MakeComponentTarget(MeshComponent));
		}
	}

	NewTool->SetSelection(MoveTemp(ComponentTargets));
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}




/*
 * Tool
 */

UEditNormalsToolProperties::UEditNormalsToolProperties()
{
	bFixInconsistentNormals = false;
	bInvertNormals = false;
	bRecomputeNormals = true;
	NormalCalculationMethod = ENormalCalculationMethod::AreaAngleWeighting;
	SplitNormalMethod = ESplitNormalMethod::UseExistingTopology;
	SharpEdgeAngleThreshold = 60;
	bAllowSharpVertices = false;
}

UEditNormalsAdvancedProperties::UEditNormalsAdvancedProperties()
{
}


UEditNormalsTool::UEditNormalsTool()
{
}

void UEditNormalsTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UEditNormalsTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponent
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(false);
	}

	BasicProperties = NewObject<UEditNormalsToolProperties>(this, TEXT("Mesh Normals Settings"));
	AdvancedProperties = NewObject<UEditNormalsAdvancedProperties>(this, TEXT("Advanced Settings"));

	// initialize our properties
	AddToolPropertySource(BasicProperties);
	AddToolPropertySource(AdvancedProperties);

	// initialize the PreviewMesh+BackgroundCompute object
	UpdateNumPreviews();

	
	
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UEditNormalsTool::UpdateNumPreviews()
{
	int32 CurrentNumPreview = Previews.Num();
	int32 TargetNumPreview = ComponentTargets.Num();
	if (TargetNumPreview < CurrentNumPreview)
	{
		for (int32 PreviewIdx = CurrentNumPreview - 1; PreviewIdx >= TargetNumPreview; PreviewIdx--)
		{
			Previews[PreviewIdx]->Cancel();
		}
		Previews.SetNum(TargetNumPreview);
		OriginalDynamicMeshes.SetNum(TargetNumPreview);
	}
	else
	{
		OriginalDynamicMeshes.SetNum(TargetNumPreview);
		for (int32 PreviewIdx = CurrentNumPreview; PreviewIdx < TargetNumPreview; PreviewIdx++)
		{
			UEditNormalsOperatorFactory *OpFactory = NewObject<UEditNormalsOperatorFactory>();
			OpFactory->Tool = this;
			OpFactory->ComponentIndex = PreviewIdx;
			OriginalDynamicMeshes[PreviewIdx] = MakeShared<FDynamicMesh3>();
			FMeshDescriptionToDynamicMesh Converter;
			Converter.Convert(ComponentTargets[PreviewIdx]->GetMesh(), *OriginalDynamicMeshes[PreviewIdx]);

			UMeshOpPreviewWithBackgroundCompute* Preview = Previews.Add_GetRef(NewObject<UMeshOpPreviewWithBackgroundCompute>(OpFactory, "Preview"));
			Preview->Setup(this->TargetWorld, OpFactory);
			Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

			FComponentMaterialSet MaterialSet;
			ComponentTargets[PreviewIdx]->GetMaterialSet(MaterialSet);
			Preview->ConfigureMaterials(MaterialSet.Materials,
				ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
			);

			Preview->SetVisibility(true);
		}
	}
}


void UEditNormalsTool::Shutdown(EToolShutdownType ShutdownType)
{
	// Restore (unhide) the source meshes
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(true);
	}

	TArray<FDynamicMeshOpResult> Results;
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Results.Add(Preview->Shutdown());
	}
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GenerateAsset(Results);
	}
}

void UEditNormalsTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

TUniquePtr<FDynamicMeshOperator> UEditNormalsOperatorFactory::MakeNewOperator()
{
	TUniquePtr<FEditNormalsOp> NormalsOp = MakeUnique<FEditNormalsOp>();
	NormalsOp->bFixInconsistentNormals = Tool->BasicProperties->bFixInconsistentNormals;
	NormalsOp->bInvertNormals = Tool->BasicProperties->bInvertNormals;
	NormalsOp->bRecomputeNormals = Tool->BasicProperties->bRecomputeNormals;
	NormalsOp->SplitNormalMethod = Tool->BasicProperties->SplitNormalMethod;
	NormalsOp->bAllowSharpVertices = Tool->BasicProperties->bAllowSharpVertices;
	NormalsOp->NormalCalculationMethod = Tool->BasicProperties->NormalCalculationMethod;
	NormalsOp->NormalSplitThreshold = Tool->BasicProperties->SharpEdgeAngleThreshold;

	FTransform LocalToWorld = Tool->ComponentTargets[ComponentIndex]->GetWorldTransform();
	NormalsOp->OriginalMesh = Tool->OriginalDynamicMeshes[ComponentIndex];
	
	NormalsOp->SetTransform(LocalToWorld);

	return NormalsOp;
}



void UEditNormalsTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void UEditNormalsTool::OnTick(float DeltaTime)
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->Tick(DeltaTime);
	}
}



#if WITH_EDITOR
void UEditNormalsTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}
#endif

void UEditNormalsTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}



bool UEditNormalsTool::HasAccept() const
{
	return true;
}

bool UEditNormalsTool::CanAccept() const
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		if (!Preview->HaveValidResult())
		{
			return false;
		}
	}
	return true;
}


void UEditNormalsTool::GenerateAsset(const TArray<FDynamicMeshOpResult>& Results)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("EditNormalsToolTransactionName", "Edit Normals Tool"));

	check(Results.Num() == ComponentTargets.Num());
	
	for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget = ComponentTargets[ComponentIdx];

		// disable auto-generated normals StaticMesh build setting
		UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(ComponentTarget->GetOwnerComponent());
		if (StaticMeshComponent != nullptr)
		{
			UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
			if (ensure(StaticMesh != nullptr))
			{
				StaticMesh->Modify();
				UE::MeshDescription::FStaticMeshBuildSettingChange SettingsChange;
				SettingsChange.AutoGeneratedNormals = UE::MeshDescription::EBuildSettingBoolChange::Disable;
				UE::MeshDescription::ConfigureBuildSettings(StaticMesh, 0, SettingsChange);
			}
		}

		check(Results[ComponentIdx].Mesh.Get() != nullptr);
		ComponentTarget->CommitMesh([&Results, &ComponentIdx, this](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;
			
			if (BasicProperties->WillTopologyChange() || !FDynamicMeshToMeshDescription::HaveMatchingElementCounts(Results[ComponentIdx].Mesh.Get(), CommitParams.MeshDescription, false, true))
			{
				// full conversion if normal topology changed or faces were inverted
				Converter.Convert(Results[ComponentIdx].Mesh.Get(), *CommitParams.MeshDescription);
			}
			else
			{
				// otherwise just copy attributes
				Converter.UpdateAttributes(Results[ComponentIdx].Mesh.Get(), *CommitParams.MeshDescription, true, false);
			}
		});
	}

	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
