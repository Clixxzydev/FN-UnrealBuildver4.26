// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_StateResult.h"
#include "GraphEditorSettings.h"
#include "AnimBlueprintCompiler.h"

#define LOCTEXT_NAMESPACE "A3Nodes"

/////////////////////////////////////////////////////
// UAnimGraphNode_StateResult

UAnimGraphNode_StateResult::UAnimGraphNode_StateResult(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FLinearColor UAnimGraphNode_StateResult::GetNodeTitleColor() const
{
	return GetDefault<UGraphEditorSettings>()->ResultNodeTitleColor;
}

FText UAnimGraphNode_StateResult::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("AnimGraphNodeStateResult_Title", "Output Animation Pose");
}

FText UAnimGraphNode_StateResult::GetTooltipText() const
{
	return LOCTEXT("AnimGraphNodeStateResult_Tooltip", "This is the output of this animation state");
}

bool UAnimGraphNode_StateResult::IsSinkNode() const
{
	return true;
}

void UAnimGraphNode_StateResult::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	// Intentionally empty. This node is auto-generated when a new graph is created.
}

FString UAnimGraphNode_StateResult::GetDocumentationLink() const
{
	return TEXT("Shared/GraphNodes/AnimationStateMachine");
}

void UAnimGraphNode_StateResult::OnProcessDuringCompilation(FAnimBlueprintCompilerContext& InCompilerContext)
{
	UAnimGraphNode_StateResult* TrueNode = InCompilerContext.MessageLog.FindSourceObjectTypeChecked<UAnimGraphNode_StateResult>(this);

	Node.Name = TrueNode->GetGraph()->GetFName();
}

#undef LOCTEXT_NAMESPACE
