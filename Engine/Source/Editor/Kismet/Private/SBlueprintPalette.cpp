// Copyright Epic Games, Inc. All Rights Reserved.

#include "SBlueprintPalette.h"
#include "Widgets/IToolTip.h"
#include "Widgets/Layout/SSplitter.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"
#include "Widgets/SOverlay.h"
#include "SlateOptMacros.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Input/SCheckBox.h"
#include "Styling/CoreStyle.h"
#include "EditorStyleSet.h"
#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Components/TimelineComponent.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Misc/FileHelper.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "EdGraphSchema_K2_Actions.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "Engine/SCS_Node.h"
#include "Internationalization/Culture.h"
#include "BlueprintEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "ScopedTransaction.h"
#include "EditorWidgetsModule.h"
#include "AssetRegistryModule.h"
#include "SMyBlueprint.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "IDocumentation.h"
#include "SBlueprintLibraryPalette.h"
#include "SBlueprintFavoritesPalette.h"
#include "BlueprintPaletteFavorites.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationGraph.h"
#include "AnimationStateGraph.h"
#include "AnimStateConduitNode.h"
#include "AnimationTransitionGraph.h"
#include "BlueprintActionMenuItem.h"
#include "BlueprintActionMenuUtils.h"
#include "BlueprintDragDropMenuItem.h"
#include "TutorialMetaData.h"
#include "BlueprintEditorSettings.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "SPinTypeSelector.h"
#include "GraphEditorSettings.h"
#include "UObject/WeakFieldPtr.h"
#include "BlueprintNodeSpawner.h"

#define LOCTEXT_NAMESPACE "BlueprintPalette"

/*******************************************************************************
* Static File Helpers
*******************************************************************************/

/** namespace'd to avoid collisions during unified builds */
namespace BlueprintPalette
{
	static FString const ConfigSection("BlueprintEditor.Palette");
	static FString const FavoritesHeightConfigKey("FavoritesHeightRatio");
	static FString const LibraryHeightConfigKey("LibraryHeightRatio");
}

/**
 * A helper method intended for constructing tooltips on palette items 
 * associated with specific blueprint variables (gets a string representing the 
 * specified variable's type)
 * 
 * @param  VarScope	The struct that owns the variable in question.
 * @param  VarName	The name of the variable you want the type of.
 * @param  Detailed	If true the returned string includes SubCategoryObject
 * @return A string representing the variable's type (empty if the variable couldn't be found).
 */
static FString GetVarType(UStruct* VarScope, FName VarName, bool bUseObjToolTip, bool bDetailed = false)
{
	FString VarDesc;

	if (VarScope)
	{
		if (FProperty* Property = FindFProperty<FProperty>(VarScope, VarName))
		{
			// If it is an object property, see if we can get a nice class description instead of just the name
			FObjectProperty* ObjProp = CastField<FObjectProperty>(Property);
			if (bUseObjToolTip && ObjProp && ObjProp->PropertyClass)
			{
				VarDesc = ObjProp->PropertyClass->GetToolTipText().ToString();
			}

			// Name of type
			if (VarDesc.Len() == 0)
			{
				const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

				FEdGraphPinType PinType;
				if (K2Schema->ConvertPropertyToPinType(Property, PinType)) // use schema to get the color
				{
					VarDesc = UEdGraphSchema_K2::TypeToText(PinType).ToString();
				}
			}
		}
	}

	return VarDesc;
}

/**
 * Util function that helps construct a tooltip for a specific variable action 
 * (attempts to grab the variable's "tooltip" metadata).
 * 
 * @param  InBlueprint	The blueprint that the palette is associated.
 * @param  VarClass		The class that owns the variable.
 * @param  VarName		The variable you want a tooltip for.
 * @return A string from the variable's "tooltip" metadata (empty if the variable wasn't found, or it didn't have the metadata).
 */
static FString GetVarTooltip(UBlueprint* InBlueprint, UClass* VarClass, FName VarName)
{
	FString ResultTooltip;
	if (VarClass)
	{
	
		if (FProperty* Property = FindFProperty<FProperty>(VarClass, VarName))
		{
			// discover if the variable property is a non blueprint user variable
			UClass* SourceClass = Property->GetOwnerClass();
			if( SourceClass && SourceClass->ClassGeneratedBy == nullptr )
			{
				ResultTooltip = Property->GetToolTipText().ToString();
			}
			else
			{
				const UBlueprint* SourceBlueprint = SourceClass ? Cast<UBlueprint>(SourceClass->ClassGeneratedBy) : nullptr;
				FBlueprintEditorUtils::GetBlueprintVariableMetaData(SourceBlueprint ? SourceBlueprint : InBlueprint, VarName, nullptr, TEXT("tooltip"), ResultTooltip);
			}
		}
	}

	return ResultTooltip;
}

/**
 * A utility function intended to aid the construction of a specific blueprint 
 * palette item (specifically FEdGraphSchemaAction_K2Graph palette items). Based 
 * off of the sub-graph's type, this gets an icon representing said sub-graph.
 * 
 * @param  ActionIn		The FEdGraphSchemaAction_K2Graph action that the palette item represents.
 * @param  BlueprintIn	The blueprint currently being edited (that the action is for).
 * @param  IconOut		An icon denoting the sub-graph's type.
 * @param  ColorOut		An output color, further denoting the specified action.
 * @param  ToolTipOut	The tooltip to display when the icon is hovered over (describing the sub-graph type).
 */
static void GetSubGraphIcon(FEdGraphSchemaAction_K2Graph const* const ActionIn, UBlueprint const* BlueprintIn, FSlateBrush const*& IconOut, FSlateColor& ColorOut, FText& ToolTipOut)
{
	check(BlueprintIn);

	switch (ActionIn->GraphType)
	{
	case EEdGraphSchemaAction_K2Graph::Graph:
		{
			if (ActionIn->EdGraph)
			{
				IconOut = FBlueprintEditor::GetGlyphForGraph(ActionIn->EdGraph);
			}
			else
			{
				IconOut = FEditorStyle::GetBrush(TEXT("GraphEditor.EventGraph_16x"));
			}

			ToolTipOut = LOCTEXT("EventGraph_ToolTip", "Event Graph");
		}
		break;
	case EEdGraphSchemaAction_K2Graph::Subgraph:
		{
			if (Cast<UAnimationStateMachineGraph>(ActionIn->EdGraph))
			{
				IconOut = FEditorStyle::GetBrush(TEXT("GraphEditor.StateMachine_16x") );
				ToolTipOut = LOCTEXT("AnimationStateMachineGraph_ToolTip", "Animation State Machine");
			}
			else if (Cast<UAnimationStateGraph>(ActionIn->EdGraph))
			{
				IconOut = FEditorStyle::GetBrush(TEXT("GraphEditor.State_16x") );
				ToolTipOut = LOCTEXT("AnimationState_ToolTip", "Animation State");
			}
			else if (Cast<UAnimationTransitionGraph>(ActionIn->EdGraph))
			{
				UAnimStateConduitNode* EdGraphOuter = Cast<UAnimStateConduitNode>(ActionIn->EdGraph->GetOuter());
				if (EdGraphOuter)
				{
					IconOut = FEditorStyle::GetBrush(TEXT("GraphEditor.Conduit_16x"));
					ToolTipOut = LOCTEXT("ConduitGraph_ToolTip", "Conduit");
				}
				else
				{
					IconOut = FEditorStyle::GetBrush(TEXT("GraphEditor.Rule_16x"));
					ToolTipOut = LOCTEXT("AnimationTransitionGraph_ToolTip", "Animation Transition Rule");
				}
			}
			else
			{
				IconOut = FEditorStyle::GetBrush(TEXT("GraphEditor.SubGraph_16x") );
				ToolTipOut = LOCTEXT("EventSubgraph_ToolTip", "Event Subgraph");
			}
		}
		break;
	case EEdGraphSchemaAction_K2Graph::Macro:
		{
			IconOut = FEditorStyle::GetBrush(TEXT("GraphEditor.Macro_16x"));
			if ( ActionIn->EdGraph == nullptr )
			{
				ToolTipOut = LOCTEXT("PotentialOverride_Tooltip", "Potential Override");	
			}
			else
			{
				// Need to see if this is a function overriding something in the parent, or 
				;
				if (UFunction* OverrideFunc = FindUField<UFunction>(BlueprintIn->ParentClass, ActionIn->FuncName))
				{
					ToolTipOut = LOCTEXT("Override_Tooltip", "Override");
				}
				else 
				{
					ToolTipOut = LOCTEXT("Macro_Tooltip", "Macro");
				}
			}
		}
		break;
	case EEdGraphSchemaAction_K2Graph::Interface:
		{
			IconOut = FEditorStyle::GetBrush(TEXT("GraphEditor.InterfaceFunction_16x"));

			FFormatNamedArguments Args;
			Args.Add(TEXT("InterfaceName"), FText::FromName(ActionIn->FuncName));
			ToolTipOut = FText::Format(LOCTEXT("FunctionFromInterface_Tooltip", "Function (from Interface '{InterfaceName}')"), Args);
			if (UFunction* OverrideFunc = FindUField<UFunction>(BlueprintIn->SkeletonGeneratedClass, ActionIn->FuncName))
			{
				if (UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(OverrideFunc))
				{
					ToolTipOut = FText::Format(LOCTEXT("EventFromInterface_Tooltip", "Event (from Interface '{InterfaceName}')"), Args);
					ColorOut = FLinearColor::Yellow;
				}
			}
		}
		break;
	case EEdGraphSchemaAction_K2Graph::Function:
		{
			if (ActionIn->EdGraph == nullptr)
			{
				IconOut = FEditorStyle::GetBrush(TEXT("GraphEditor.PotentialOverrideFunction_16x"));
				ToolTipOut = LOCTEXT("PotentialOverride_Tooltip", "Potential Override");	
			}
			else
			{
				if (ActionIn->EdGraph->IsA(UAnimationGraph::StaticClass()))
				{
					IconOut = FEditorStyle::GetBrush(TEXT("GraphEditor.Animation_16x"));
				}
				else if (UFunction* OverrideFunc = FindUField<UFunction>(BlueprintIn->ParentClass, ActionIn->FuncName))
				{
					const bool bIsPureFunction = OverrideFunc && OverrideFunc->HasAnyFunctionFlags(FUNC_BlueprintPure);
					IconOut = FEditorStyle::GetBrush(bIsPureFunction ? TEXT("GraphEditor.OverridePureFunction_16x") : TEXT("GraphEditor.OverrideFunction_16x"));
					ToolTipOut = LOCTEXT("Override_Tooltip", "Override");
				}
				else
				{
					UFunction* Function = FindUField<UFunction>(BlueprintIn->SkeletonGeneratedClass, ActionIn->FuncName);
					const bool bIsPureFunction = Function && Function->HasAnyFunctionFlags(FUNC_BlueprintPure);

					IconOut = FEditorStyle::GetBrush(bIsPureFunction ? TEXT("GraphEditor.PureFunction_16x") : TEXT("GraphEditor.Function_16x"));
					if (ActionIn->EdGraph->IsA(UAnimationGraph::StaticClass()))
					{
						ToolTipOut = LOCTEXT("AnimationGraph_Tooltip", "Animation Graph");
					}
					else
					{
						ToolTipOut = LOCTEXT("Function_Tooltip", "Function");
					}
				}
			}
		}
		break;
	}
}

/**
 * A utility function intended to aid the construction of a specific blueprint 
 * palette item. This looks at the item's associated action, and based off its  
 * type, retrieves an icon, color and tooltip for the slate widget.
 * 
 * @param  ActionIn		The action associated with the palette item you want an icon for.
 * @param  BlueprintIn	The blueprint currently being edited (that the action is for).
 * @param  BrushOut		An output of the icon, best representing the specified action.
 * @param  ColorOut		An output color, further denoting the specified action.
 * @param  ToolTipOut	An output tooltip, best describing the specified action type.
 */
static void GetPaletteItemIcon(TSharedPtr<FEdGraphSchemaAction> ActionIn, UBlueprint const* BlueprintIn, FSlateBrush const*& BrushOut, FSlateColor& ColorOut, FText& ToolTipOut, FString& DocLinkOut, FString& DocExcerptOut, FSlateBrush const*& SecondaryBrushOut, FSlateColor& SecondaryColorOut)
{
	// Default to tooltip based on action supplied
	ToolTipOut = ActionIn->GetTooltipDescription().IsEmpty() ? ActionIn->GetMenuDescription() : ActionIn->GetTooltipDescription();

	if (ActionIn->GetTypeId() == FBlueprintActionMenuItem::StaticGetTypeId())
	{
		FBlueprintActionMenuItem* NodeSpawnerAction = (FBlueprintActionMenuItem*)ActionIn.Get();
		BrushOut = NodeSpawnerAction->GetMenuIcon(ColorOut);

		TSubclassOf<UEdGraphNode> VarNodeClass = NodeSpawnerAction->GetRawAction()->NodeClass;
		// if the node is a variable getter or setter, use the variable icon instead, because maps need two brushes
		if (*VarNodeClass && VarNodeClass->IsChildOf(UK2Node_Variable::StaticClass()))
		{
			const UK2Node_Variable* TemplateNode = Cast<UK2Node_Variable>(NodeSpawnerAction->GetRawAction()->GetTemplateNode());
			FProperty* Property = TemplateNode->GetPropertyForVariable();
			BrushOut = FBlueprintEditor::GetVarIconAndColorFromProperty(Property, ColorOut, SecondaryBrushOut, SecondaryColorOut);
		}
	}
	else if (ActionIn->GetTypeId() == FBlueprintDragDropMenuItem::StaticGetTypeId())
	{
		FBlueprintDragDropMenuItem* DragDropAction = (FBlueprintDragDropMenuItem*)ActionIn.Get();
		BrushOut = DragDropAction->GetMenuIcon(ColorOut);
	}
	// for backwards compatibility:
	else if (UK2Node const* const NodeTemplate = FBlueprintActionMenuUtils::ExtractNodeTemplateFromAction(ActionIn))
	{
		// If the node wants to create tooltip text, use that instead, because its probably more detailed
		FText NodeToolTipText = NodeTemplate->GetTooltipText();
		if (!NodeToolTipText.IsEmpty())
		{
			ToolTipOut = NodeToolTipText;
		}

		// Ask node for a palette icon
		FLinearColor IconLinearColor = FLinearColor::White;
		BrushOut = NodeTemplate->GetIconAndTint(IconLinearColor).GetOptionalIcon();
		ColorOut = IconLinearColor;
	}
	// for MyBlueprint tab specific actions:
	else if (ActionIn->GetTypeId() == FEdGraphSchemaAction_K2Graph::StaticGetTypeId())
	{
		FEdGraphSchemaAction_K2Graph const* GraphAction = (FEdGraphSchemaAction_K2Graph const*)ActionIn.Get();
		GetSubGraphIcon(GraphAction, BlueprintIn, BrushOut, ColorOut, ToolTipOut);
	}
	else if (ActionIn->GetTypeId() == FEdGraphSchemaAction_K2Delegate::StaticGetTypeId())
	{
		FEdGraphSchemaAction_K2Delegate* DelegateAction = (FEdGraphSchemaAction_K2Delegate*)ActionIn.Get();

		BrushOut = FEditorStyle::GetBrush(TEXT("GraphEditor.Delegate_16x"));
		FFormatNamedArguments Args;
		Args.Add(TEXT("EventDispatcherName"), FText::FromName(DelegateAction->GetDelegateName()));
		ToolTipOut = FText::Format(LOCTEXT("Delegate_Tooltip", "Event Dispatcher '{EventDispatcherName}'"), Args);
	}
	else if (ActionIn->GetTypeId() == FEdGraphSchemaAction_K2Var::StaticGetTypeId())
	{
		FEdGraphSchemaAction_K2Var* VarAction = (FEdGraphSchemaAction_K2Var*)ActionIn.Get();

		UClass* VarClass = VarAction->GetVariableClass();
		BrushOut = FBlueprintEditor::GetVarIconAndColor(VarClass, VarAction->GetVariableName(), ColorOut, SecondaryBrushOut, SecondaryColorOut);
		ToolTipOut = FText::FromString(GetVarType(VarClass, VarAction->GetVariableName(), true, true));

		DocLinkOut = TEXT("Shared/Editor/Blueprint/VariableTypes");
		DocExcerptOut = GetVarType(VarClass, VarAction->GetVariableName(), false, false);
	}
	else if (ActionIn->GetTypeId() == FEdGraphSchemaAction_K2LocalVar::StaticGetTypeId())
	{
		FEdGraphSchemaAction_K2LocalVar* LocalVarAction = (FEdGraphSchemaAction_K2LocalVar*)ActionIn.Get();

		UStruct* VarScope = LocalVarAction->GetVariableScope();
		BrushOut = FBlueprintEditor::GetVarIconAndColor(VarScope, LocalVarAction->GetVariableName(), ColorOut, SecondaryBrushOut, SecondaryColorOut);
		ToolTipOut = FText::FromString(GetVarType(VarScope, LocalVarAction->GetVariableName(), true));

		DocLinkOut = TEXT("Shared/Editor/Blueprint/VariableTypes");
		DocExcerptOut = GetVarType(VarScope, LocalVarAction->GetVariableName(), false);
	}
	else if (ActionIn->GetTypeId() == FEdGraphSchemaAction_K2Enum::StaticGetTypeId())
	{
		BrushOut = FEditorStyle::GetBrush(TEXT("GraphEditor.EnumGlyph"));
		ToolTipOut = LOCTEXT("Enum_Tooltip", "Enum Asset");
	}
	else if (ActionIn->GetTypeId() == FEdGraphSchemaAction_K2Struct::StaticGetTypeId())
	{
		BrushOut = FEditorStyle::GetBrush(TEXT("GraphEditor.StructGlyph"));
		ToolTipOut = LOCTEXT("Struct_Tooltip", "Struct Asset");
	}
}

/**
 * Takes the existing tooltip and concats a path id (for the specified action) 
 * to the end.
 * 
 * @param  ActionIn		The action you want to show the path for.
 * @param  OldToolTip	The tooltip that you're replacing (we fold it into the new one)/
 * @return The newly created tooltip (now with the action's path tacked on the bottom).
 */
static TSharedRef<IToolTip> ConstructToolTipWithActionPath(TSharedPtr<FEdGraphSchemaAction> ActionIn, TSharedPtr<IToolTip> OldToolTip)
{
	TSharedRef<IToolTip> NewToolTip = OldToolTip.ToSharedRef();

	FFavoritedBlueprintPaletteItem ActionItem(ActionIn);
	if (ActionItem.IsValid())
	{
		static FTextBlockStyle PathStyle = FTextBlockStyle()
			.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.SetColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.4f));

		NewToolTip = SNew(SToolTip)

		// Emulate text-only tool-tip styling that SToolTip uses when no custom content is supplied.  We want node tool-tips to 
		// be styled just like text-only tool-tips
		.BorderImage( FCoreStyle::Get().GetBrush("ToolTip.BrightBackground") )
		.TextMargin(FMargin(11.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			[
				OldToolTip->GetContentWidget()
			]

			+ SVerticalBox::Slot()
			.HAlign(EHorizontalAlignment::HAlign_Right)
			[
				SNew(STextBlock)
				.TextStyle( FEditorStyle::Get(), "Documentation.SDocumentationTooltip")
				.Text(FText::FromString(ActionItem.ToString()))
				//.TextStyle(&PathStyle)
			]
		];
	}

	return NewToolTip;
}

/*******************************************************************************
* FBlueprintPaletteItemRenameUtils
*******************************************************************************/

/** A set of utilities to aid SBlueprintPaletteItem when the user attempts to rename one. */
struct FBlueprintPaletteItemRenameUtils
{
private:
	static bool VerifyNewAssetName(UObject* Object, const FText& InNewText, FText& OutErrorMessage)
	{
		if (!Object)
		{
			return false;
		}

		if (Object->GetName() == InNewText.ToString())
		{
			return true;
		}

		TArray<FAssetData> AssetData;
		FAssetRegistryModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetToolsModule.Get().GetAssetsByPath(FName(*FPaths::GetPath(Object->GetOutermost()->GetPathName())), AssetData);

		if(!FFileHelper::IsFilenameValidForSaving(InNewText.ToString(), OutErrorMessage) || !FName(*InNewText.ToString()).IsValidObjectName( OutErrorMessage ))
		{
			return false;
		}
		else if( InNewText.ToString().Len() > NAME_SIZE )
		{
			OutErrorMessage = LOCTEXT("RenameFailed_NameTooLong", "Names must have fewer than 100 characters!");
		}
		else
		{
			// Check to see if the name conflicts
			for ( const FAssetData& AssetInfo : AssetData)
			{
				if(AssetInfo.AssetName.ToString() == InNewText.ToString())
				{
					OutErrorMessage = FText::FromString(TEXT("Asset name already in use!"));
					return false;
				}
			}
		}

		return true;
	}

	static void CommitNewAssetName(UObject* Object, FBlueprintEditor* BlueprintEditor, const FText& NewText)
	{
		if (Object && BlueprintEditor)
		{
			if(Object->GetName() != NewText.ToString())
			{
				TArray<FAssetRenameData> AssetsAndNames;
				const FString PackagePath = FPackageName::GetLongPackagePath(Object->GetOutermost()->GetName());
				new(AssetsAndNames) FAssetRenameData(Object, PackagePath, NewText.ToString());

				FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
				AssetToolsModule.Get().RenameAssetsWithDialog(AssetsAndNames);
			}

			TSharedPtr<SMyBlueprint> MyBlueprint = BlueprintEditor->GetMyBlueprintWidget();
			if (MyBlueprint.IsValid())
			{
				MyBlueprint->SelectItemByName(FName(*Object->GetPathName()));
			}	
		}
	}

public:
	/**
	 * Determines whether the enum node, associated with the selected action, 
	 * can be renamed with the specified text.
	 * 
	 * @param  InNewText		The text you want to verify.
	 * @param  OutErrorMessage	Text explaining why the associated node couldn't be renamed (if the return value is false).
	 * @param  ActionPtr		The selected action that the calling palette item represents.
	 * @return True if it is ok to rename the associated node with the given string (false if not).
	 */
	static bool VerifyNewEnumName(const FText& InNewText, FText& OutErrorMessage, TWeakPtr<FEdGraphSchemaAction> ActionPtr)
	{
		// Should never make it here with anything but an enum action
		check(ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2Enum::StaticGetTypeId());

		FEdGraphSchemaAction_K2Enum* EnumAction = (FEdGraphSchemaAction_K2Enum*)ActionPtr.Pin().Get();
		
		return VerifyNewAssetName(EnumAction ? EnumAction->Enum : nullptr, InNewText, OutErrorMessage);
	}

	/**
	 * Take the verified text and renames the enum node associated with the 
	 * selected action.
	 * 
	 * @param  NewText				The new (verified) text to rename the node with.
	 * @param  InTextCommit			A value denoting how the text was entered.
	 * @param  ActionPtr			The selected action that the calling palette item represents.
	 * @param  BlueprintEditorPtr	A pointer to the blueprint editor that the palette belongs to.
	 */
	static void CommitNewEnumName(const FText& NewText, ETextCommit::Type InTextCommit, TWeakPtr<FEdGraphSchemaAction> ActionPtr, TWeakPtr<FBlueprintEditor> BlueprintEditorPtr)
	{
		// Should never make it here with anything but an enum action
		check(ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2Enum::StaticGetTypeId());

		FEdGraphSchemaAction_K2Enum* EnumAction = (FEdGraphSchemaAction_K2Enum*)ActionPtr.Pin().Get();

		if(EnumAction->Enum->GetName() != NewText.ToString())
		{
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			TArray<FAssetRenameData> AssetsAndNames;
			const FString PackagePath = FPackageName::GetLongPackagePath(EnumAction->Enum->GetOutermost()->GetName());
			new(AssetsAndNames) FAssetRenameData(EnumAction->Enum, PackagePath, NewText.ToString());

			BlueprintEditorPtr.Pin()->GetMyBlueprintWidget()->SelectItemByName(FName("ConstructionScript"));

			AssetToolsModule.Get().RenameAssetsWithDialog(AssetsAndNames);
		}

		BlueprintEditorPtr.Pin()->GetMyBlueprintWidget()->SelectItemByName(FName(*EnumAction->Enum->GetPathName()));
	}

	/**
	* Determines whether the struct node, associated with the selected action,
	* can be renamed with the specified text.
	*
	* @param  InNewText		The text you want to verify.
	* @param  OutErrorMessage	Text explaining why the associated node couldn't be renamed (if the return value is false).
	* @param  ActionPtr		The selected action that the calling palette item represents.
	* @return True if it is ok to rename the associated node with the given string (false if not).
	*/
	static bool VerifyNewStructName(const FText& InNewText, FText& OutErrorMessage, TWeakPtr<FEdGraphSchemaAction> ActionPtr)
	{
		// Should never make it here with anything but a struct action
		check(ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2Struct::StaticGetTypeId());

		FEdGraphSchemaAction_K2Struct* Action = (FEdGraphSchemaAction_K2Struct*)ActionPtr.Pin().Get();

		return VerifyNewAssetName(Action ? Action->Struct : nullptr, InNewText, OutErrorMessage);
	}

	/**
	 * Determines whether the event node, associated with the selected action, 
	 * can be renamed with the specified text.
	 * 
	 * @param  InNewText		The text you want to verify.
	 * @param  OutErrorMessage	Text explaining why the associated node couldn't be renamed (if the return value is false).
	 * @param  ActionPtr		The selected action that the calling palette item represents.
	 * @return True if it is ok to rename the associated node with the given string (false if not).
	 */
	static bool VerifyNewEventName(const FText& InNewText, FText& OutErrorMessage, TWeakPtr<FEdGraphSchemaAction> ActionPtr)
	{
		bool bIsNameValid = false;
		OutErrorMessage = LOCTEXT("RenameFailed_NodeRename", "Cannot rename associated node!");

		check(ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2Event::StaticGetTypeId());
		FEdGraphSchemaAction_K2Event* EventAction = (FEdGraphSchemaAction_K2Event*)ActionPtr.Pin().Get();

		UK2Node* AssociatedNode = EventAction->NodeTemplate;
		if (AssociatedNode && AssociatedNode->GetCanRenameNode())
		{
			TSharedPtr<INameValidatorInterface> NodeNameValidator = FNameValidatorFactory::MakeValidator(AssociatedNode);
			bIsNameValid = (NodeNameValidator->IsValid(InNewText.ToString(), true) == EValidatorResult::Ok);
		}
		return bIsNameValid;
	}

	/**
	 * Take the verified text and renames the struct node associated with the 
	 * selected action.
	 * 
	 * @param  NewText				The new (verified) text to rename the node with.
	 * @param  InTextCommit			A value denoting how the text was entered.
	 * @param  ActionPtr			The selected action that the calling palette item represents.
	 * @param  BlueprintEditorPtr	A pointer to the blueprint editor that the palette belongs to.
	 */
	static void CommitNewStructName(const FText& NewText, ETextCommit::Type InTextCommit, TWeakPtr<FEdGraphSchemaAction> ActionPtr, TWeakPtr<FBlueprintEditor> BlueprintEditorPtr)
	{
		// Should never make it here with anything but a struct action
		check(ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2Struct::StaticGetTypeId());

		FEdGraphSchemaAction_K2Struct* Action = (FEdGraphSchemaAction_K2Struct*)ActionPtr.Pin().Get();

		CommitNewAssetName(Action ? Action->Struct : nullptr, BlueprintEditorPtr.Pin().Get(), NewText);
	}

	/**
	 * Take the verified text and renames the event node associated with the 
	 * selected action.
	 * 
	 * @param  NewText		The new (verified) text to rename the node with.
	 * @param  InTextCommit	A value denoting how the text was entered.
	 * @param  ActionPtr	The selected action that the calling palette item represents.
	 */
	static void CommitNewEventName(const FText& NewText, ETextCommit::Type InTextCommit, TWeakPtr<FEdGraphSchemaAction> ActionPtr)
	{
		check(ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2Event::StaticGetTypeId());

		FEdGraphSchemaAction_K2Event* EventAction = (FEdGraphSchemaAction_K2Event*)ActionPtr.Pin().Get();
		if (EventAction->NodeTemplate)
		{
			EventAction->NodeTemplate->OnRenameNode(NewText.ToString());
		}
	}

	/**
	 * Determines whether the target node, associated with the selected action, 
	 * can be renamed with the specified text.
	 * 
	 * @param  InNewText		The text you want to verify.
	 * @param  OutErrorMessage	Text explaining why the associated node couldn't be renamed (if the return value is false).
	 * @param  ActionPtr		The selected action that the calling palette item represents.
	 * @return True if it is ok to rename the associated node with the given string (false if not).
	 */
	static bool VerifyNewTargetNodeName(const FText& InNewText, FText& OutErrorMessage, TWeakPtr<FEdGraphSchemaAction> ActionPtr)
	{

		bool bIsNameValid = false;
		OutErrorMessage = LOCTEXT("RenameFailed_NodeRename", "Cannot rename associated node!");

		check(ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2TargetNode::StaticGetTypeId());
		FEdGraphSchemaAction_K2TargetNode* TargetNodeAction = (FEdGraphSchemaAction_K2TargetNode*)ActionPtr.Pin().Get();

		UK2Node* AssociatedNode = TargetNodeAction->NodeTemplate;
		if (AssociatedNode && AssociatedNode->GetCanRenameNode())
		{
			TSharedPtr<INameValidatorInterface> NodeNameValidator = FNameValidatorFactory::MakeValidator(AssociatedNode);
			bIsNameValid = (NodeNameValidator->IsValid(InNewText.ToString(), true) == EValidatorResult::Ok);
		}
		return bIsNameValid;
	}

	/**
	 * Take the verified text and renames the target node associated with the 
	 * selected action.
	 * 
	 * @param  NewText		The new (verified) text to rename the node with.
	 * @param  InTextCommit	A value denoting how the text was entered.
	 * @param  ActionPtr	The selected action that the calling palette item represents.
	 */
	static void CommitNewTargetNodeName(const FText& NewText, ETextCommit::Type InTextCommit, TWeakPtr<FEdGraphSchemaAction> ActionPtr)
	{
		check(ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2TargetNode::StaticGetTypeId());

		FEdGraphSchemaAction_K2TargetNode* TargetNodeAction = (FEdGraphSchemaAction_K2TargetNode*)ActionPtr.Pin().Get();
		if (TargetNodeAction->NodeTemplate)
		{
			TargetNodeAction->NodeTemplate->OnRenameNode(NewText.ToString());
		}
	}
};

/*******************************************************************************
* SPinTypeSelectorHelper
*******************************************************************************/

class SPinTypeSelectorHelper : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SPinTypeSelectorHelper ) {}
	SLATE_END_ARGS()

	/**
	 * Constructs a PinTypeSelector widget (for variable actions only, so that 
	 * the user can modify the variable's type without going to the details panel).
	 * 
	 * @param  InArgs					A set of slate arguments, defined above.
	 * @param  InVariableProperty		The variable property to select
	 * @param  InBlueprintEditor			A pointer to the blueprint editor that the palette belongs to.
	 */
	void Construct(const FArguments& InArgs, FProperty* InVariableProperty, UBlueprint* InBlueprint, TWeakPtr<FBlueprintEditor> InBlueprintEditor)
	{
		BlueprintObj = InBlueprint;
		BlueprintEditorPtr = InBlueprintEditor;
		VariableProperty = InVariableProperty;

		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		this->ChildSlot
		[
			SNew(SPinTypeSelector, FGetPinTypeTree::CreateUObject(Schema, &UEdGraphSchema_K2::GetVariableTypeTree))
			.Schema(Schema)
			.TargetPinType(this, &SPinTypeSelectorHelper::OnGetVarType)
			.OnPinTypeChanged(this, &SPinTypeSelectorHelper::OnVarTypeChanged)
			.TypeTreeFilter(ETypeTreeFilter::None)
			.SelectorType(BlueprintEditorPtr.IsValid() ? SPinTypeSelector::ESelectorType::Compact : SPinTypeSelector::ESelectorType::None)
		];
	}

private:
	FEdGraphPinType OnGetVarType() const
	{
		if (FProperty* VarProp = const_cast<FProperty*>(VariableProperty.Get()))
		{
			const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
			FEdGraphPinType Type;
			K2Schema->ConvertPropertyToPinType(VarProp, Type);
			return Type;
		}
		return FEdGraphPinType();
	}

	void OnVarTypeChanged(const FEdGraphPinType& InNewPinType)
	{
		if (FBlueprintEditorUtils::IsPinTypeValid(InNewPinType))
		{
			if (FProperty* VarProp = VariableProperty.Get())
			{
				FName VarName = VarProp->GetFName();

				if (VarName != NAME_None)
				{
					// Set the MyBP tab's last pin type used as this, for adding lots of variables of the same type
					BlueprintEditorPtr.Pin()->GetMyBlueprintWidget()->GetLastPinTypeUsed() = InNewPinType;

					if (UFunction* LocalVariableScope = VarProp->GetOwner<UFunction>())
					{
						FBlueprintEditorUtils::ChangeLocalVariableType(BlueprintObj, LocalVariableScope, VarName, InNewPinType);
					}
					else
					{
						FBlueprintEditorUtils::ChangeMemberVariableType(BlueprintObj, VarName, InNewPinType);
					}
				}
			}
		}
	}

private:
	/** The action that the owning palette entry represents */
	TWeakPtr<FEdGraphSchemaAction_K2Var> ActionPtr;

	/** Pointer back to the blueprint that is being displayed: */
	UBlueprint* BlueprintObj;

	/** Pointer back to the blueprint editor that owns this, optional because of diff and merge views: */
	TWeakPtr<FBlueprintEditor>     BlueprintEditorPtr;

	/** Variable Property to change the type of */
	TWeakFieldPtr<FProperty> VariableProperty;
};

/*******************************************************************************
* SPaletteItemVisibilityToggle
*******************************************************************************/

class SPaletteItemVisibilityToggle : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SPaletteItemVisibilityToggle ) {}
	SLATE_END_ARGS()

	/**
	 * Constructs a visibility-toggle widget (for variable actions only, so that 
	 * the user can modify the variable's "edit-on-instance" state).
	 * 
	 * @param  InArgs			A set of slate arguments, defined above.
	 * @param  ActionPtrIn		The FEdGraphSchemaAction that the parent item represents.
	 * @param  BlueprintEdPtrIn	A pointer to the blueprint editor that the palette belongs to.
	 */
	void Construct(const FArguments& InArgs, TWeakPtr<FEdGraphSchemaAction> ActionPtrIn, TWeakPtr<FBlueprintEditor> InBlueprintEditor, UBlueprint* InBlueprint)
	{
		ActionPtr = ActionPtrIn;
		BlueprintEditorPtr = InBlueprintEditor;
		BlueprintObj = InBlueprint;
		TSharedPtr<FEdGraphSchemaAction> PaletteAction = ActionPtrIn.Pin();

		bool bShouldHaveAVisibilityToggle = false;
		if (PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2Var::StaticGetTypeId())
		{
			FProperty* VariableProp = StaticCastSharedPtr<FEdGraphSchemaAction_K2Var>(PaletteAction)->GetProperty();
			FObjectProperty* VariableObjProp = CastField<FObjectProperty>(VariableProp);

			UStruct* VarSourceScope = (VariableProp ? CastChecked<UStruct>(VariableProp->GetOwner<UObject>()) : nullptr);
			const bool bIsBlueprintVariable = (VarSourceScope == BlueprintObj->SkeletonGeneratedClass);
			const bool bIsComponentVar = (VariableObjProp && VariableObjProp->PropertyClass && VariableObjProp->PropertyClass->IsChildOf(UActorComponent::StaticClass()));
			bShouldHaveAVisibilityToggle = bIsBlueprintVariable && (!bIsComponentVar || FBlueprintEditorUtils::IsVariableCreatedByBlueprint(BlueprintObj, VariableObjProp));
		}

		this->ChildSlot[
			SNew(SBorder)
				.Padding( 0.0f )
				.BorderImage(FEditorStyle::GetBrush("NoBorder"))
				.ColorAndOpacity(this, &SPaletteItemVisibilityToggle::GetVisibilityToggleColor)
			[
				SNew( SCheckBox )
					.ToolTipText(this, &SPaletteItemVisibilityToggle::GetVisibilityToggleToolTip)
					.Visibility(bShouldHaveAVisibilityToggle ? EVisibility::Visible : EVisibility::Collapsed)
					.OnCheckStateChanged(this, &SPaletteItemVisibilityToggle::OnVisibilityToggleFlipped)
					.IsChecked(this, &SPaletteItemVisibilityToggle::GetVisibilityToggleState)
					// a style using the normal checkbox images but with the toggle button layout
					.Style( FEditorStyle::Get(), "CheckboxLookToggleButtonCheckbox")	
				[
					SNew( SVerticalBox )
					+SVerticalBox::Slot()
						.AutoHeight()
						.VAlign( VAlign_Center )
						.HAlign( HAlign_Center )
					[
						SNew( SImage )
							.Image( this, &SPaletteItemVisibilityToggle::GetVisibilityIcon )
							.ColorAndOpacity( FLinearColor::Black )
					]
				]
			]
		];
	}

private:
	/**
	 * Used by this visibility-toggle widget to see if the property represented 
	 * by this item is visible outside of Kismet.
	 * 
	 * @return ECheckBoxState::Checked if the property is visible, false if not (or if the property wasn't found)
	 */
	ECheckBoxState GetVisibilityToggleState() const
	{
		TSharedPtr<FEdGraphSchemaAction> PaletteAction = ActionPtr.Pin();
		if ( PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2Var::StaticGetTypeId() )
		{
			TSharedPtr<FEdGraphSchemaAction_K2Var> VarAction = StaticCastSharedPtr<FEdGraphSchemaAction_K2Var>(PaletteAction);
			if (FProperty* VariableProperty = VarAction->GetProperty())
			{
				return VariableProperty->HasAnyPropertyFlags(CPF_DisableEditOnInstance) ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
			}
		}

		return ECheckBoxState::Unchecked;
	}

	/**
	 * Used by this visibility-toggle widget when the user makes a change to the
	 * checkbox (modifies the property represented by this item by flipping its
	 * edit-on-instance flag).
	 * 
	 * @param  InNewState	The new state that the user set the checkbox to.
	 */
	void OnVisibilityToggleFlipped(ECheckBoxState InNewState)
	{
		if( !BlueprintEditorPtr.IsValid() )
		{
			return;
		}

		TSharedPtr<FEdGraphSchemaAction> PaletteAction = ActionPtr.Pin();
		if ( PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2Var::StaticGetTypeId() )
		{
			TSharedPtr<FEdGraphSchemaAction_K2Var> VarAction = StaticCastSharedPtr<FEdGraphSchemaAction_K2Var>(PaletteAction);

			// Toggle the flag on the blueprint's version of the variable description, based on state
			const bool bVariableIsExposed = ( InNewState == ECheckBoxState::Checked );

			FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(BlueprintObj, VarAction->GetVariableName(), !bVariableIsExposed);
		}
	}

	/**
	 * Used by this visibility-toggle widget to convey the visibility of the 
	 * property represented by this item.
	 * 
	 * @return A image representing the variable's "edit-on-instance" state.
	 */
	const FSlateBrush* GetVisibilityIcon() const
	{
		return GetVisibilityToggleState() == ECheckBoxState::Checked ?
			FEditorStyle::GetBrush( "Kismet.VariableList.ExposeForInstance" ) :
			FEditorStyle::GetBrush( "Kismet.VariableList.HideForInstance" );
	}

	/**
	 * Used by this visibility-toggle widget to convey the visibility of the 
	 * property represented by this item (as well as the status of the 
	 * variable's tooltip).
	 * 
	 * @return A color denoting the item's visibility and tootip status.
	 */
	FLinearColor GetVisibilityToggleColor() const 
	{
		if ( GetVisibilityToggleState() != ECheckBoxState::Checked )
		{
			return FColor(64, 64, 64).ReinterpretAsLinear();
		}
		else
		{
			TSharedPtr<FEdGraphSchemaAction_K2Var> VarAction = StaticCastSharedPtr<FEdGraphSchemaAction_K2Var>(ActionPtr.Pin());

			FString Result;
			FBlueprintEditorUtils::GetBlueprintVariableMetaData(BlueprintObj, VarAction->GetVariableName(), nullptr, TEXT("tooltip"), Result);

			if ( !Result.IsEmpty() )
			{
				return FColor(130, 219, 119).ReinterpretAsLinear(); //pastel green when tooltip exists
			}
			else
			{
				return FColor(215, 219, 119).ReinterpretAsLinear(); //pastel yellow if no tooltip to alert designer 
			}
		}
	}

	/**
	 * Used by this visibility-toggle widget to supply the toggle with a tooltip
	 * representing the "edit-on-instance" state of the variable represented by 
	 * this item.
	 * 
	 * @return Tooltip text for this toggle.
	 */
	FText GetVisibilityToggleToolTip() const
	{
		FText ToolTipText = FText::GetEmpty();
		if ( GetVisibilityToggleState() != ECheckBoxState::Checked )
		{
			ToolTipText = LOCTEXT("VariablePrivacy_not_public_Tooltip", "Variable is not public and will not be editable on an instance of this Blueprint.");
		}
		else
		{
			ToolTipText = LOCTEXT("VariablePrivacy_is_public_Tooltip", "Variable is public and is editable on each instance of this Blueprint.");
		}
		return ToolTipText;
	}

private:
	/** The action that the owning palette entry represents */
	TWeakPtr<FEdGraphSchemaAction> ActionPtr;

	/** Pointer back to the blueprint editor that owns this, optional because of diff and merge views: */
	TWeakPtr<FBlueprintEditor>     BlueprintEditorPtr;

	/** Pointer back to the blueprint that is being diplayed: */
	UBlueprint* BlueprintObj;
};

/*******************************************************************************
* SBlueprintPaletteItem Public Interface
*******************************************************************************/

//------------------------------------------------------------------------------
BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SBlueprintPaletteItem::Construct(const FArguments& InArgs, FCreateWidgetForActionData* const InCreateData, TWeakPtr<FBlueprintEditor> InBlueprintEditor)
{
	Construct(InArgs, InCreateData, InBlueprintEditor.Pin()->GetBlueprintObj(), InBlueprintEditor);
}

void SBlueprintPaletteItem::Construct(const FArguments& InArgs, FCreateWidgetForActionData* const InCreateData, UBlueprint* InBlueprint)
{
	Construct( InArgs, InCreateData, InBlueprint, TWeakPtr<FBlueprintEditor>() );
}

void SBlueprintPaletteItem::Construct(const FArguments& InArgs, FCreateWidgetForActionData* const InCreateData, UBlueprint* InBlueprint, TWeakPtr<FBlueprintEditor> InBlueprintEditor)
{
	check(InCreateData->Action.IsValid());
	check(InBlueprint);

	Blueprint = InBlueprint;

	bShowClassInTooltip = InArgs._ShowClassInTooltip;	

	TSharedPtr<FEdGraphSchemaAction> GraphAction = InCreateData->Action;
	ActionPtr = InCreateData->Action;
	BlueprintEditorPtr = InBlueprintEditor;

	const bool bIsFullyReadOnly = !InBlueprintEditor.IsValid() || InCreateData->bIsReadOnly;
	
	TWeakPtr<FEdGraphSchemaAction> WeakGraphAction = GraphAction;
	auto IsReadOnlyLambda = [WeakGraphAction, InBlueprintEditor, bIsFullyReadOnly]()
	{ 
		if(WeakGraphAction.IsValid() && InBlueprintEditor.IsValid())
		{
			return bIsFullyReadOnly || FBlueprintEditorUtils::IsPaletteActionReadOnly(WeakGraphAction.Pin(), InBlueprintEditor.Pin());
		}

		return bIsFullyReadOnly;
	};
	
	// We differentiate enabled/read-only state here to not dim icons out unnecessarily, which in some situations
	// (like the right-click palette menu) is confusing to users.
	auto IsEditingEnabledLambda = [InBlueprintEditor]()
	{ 
		if(InBlueprintEditor.IsValid())
		{
			return InBlueprintEditor.Pin()->InEditingMode();
		}

		return true;
	};

	TAttribute<bool> bIsReadOnly = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda(IsReadOnlyLambda));
	TAttribute<bool> bIsEditingEnabled = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda(IsEditingEnabledLambda));

	// construct the icon widget
	FSlateBrush const* IconBrush   = FEditorStyle::GetBrush(TEXT("NoBrush"));
	FSlateBrush const* SecondaryBrush = FEditorStyle::GetBrush(TEXT("NoBrush"));
	FSlateColor        IconColor   = FSlateColor::UseForeground();
	FSlateColor        SecondaryIconColor   = FSlateColor::UseForeground();
	FText			   IconToolTip = GraphAction->GetTooltipDescription();
	FString			   IconDocLink, IconDocExcerpt;
	GetPaletteItemIcon(GraphAction, Blueprint, IconBrush, IconColor, IconToolTip, IconDocLink, IconDocExcerpt, SecondaryBrush, SecondaryIconColor);
	TSharedRef<SWidget> IconWidget = CreateIconWidget(IconToolTip, IconBrush, IconColor, IconDocLink, IconDocExcerpt, SecondaryBrush, SecondaryIconColor);
	IconWidget->SetEnabled(bIsEditingEnabled);

	UBlueprintEditorSettings* Settings = GetMutableDefault<UBlueprintEditorSettings>();

	// Enum representing the access specifier of this function or variable
	enum class EAccessSpecifier : uint8
	{
		None		= 0,
		Private		= 1,
		Protected	= 2,
		Public		= 3
	};

	// We should only bother checking for access if the setting is on and this is not an animation graph
	const bool bShouldCheckForAccessSpec = Settings->bShowAccessSpecifier;

	EAccessSpecifier ActionAccessSpecifier = EAccessSpecifier::None;	

	// Setup a meta tag for this node
	FTutorialMetaData TagMeta("PaletteItem"); 
	if( ActionPtr.IsValid() )
	{
		TagMeta.Tag = *FString::Printf(TEXT("PaletteItem,%s,%d"), *GraphAction->GetMenuDescription().ToString(), GraphAction->GetSectionID());
		TagMeta.FriendlyName = GraphAction->GetMenuDescription().ToString();
	}
	// construct the text widget
	FSlateFontInfo NameFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	TSharedRef<SWidget> NameSlotWidget = CreateTextSlotWidget( NameFont, InCreateData, bIsReadOnly );
	
	// Will set the icon of this property to be a Pin Type selector. 
	auto GenerateVariableSettings = [&](FProperty* VariableProp)
	{
		if (VariableProp)
		{
			if (bShouldCheckForAccessSpec)
			{
				if (VariableProp->GetBoolMetaData(FBlueprintMetadata::MD_Private))
				{
					ActionAccessSpecifier = EAccessSpecifier::Private;
				}
				else if (VariableProp->GetBoolMetaData(FBlueprintMetadata::MD_Protected))
				{
					ActionAccessSpecifier = EAccessSpecifier::Protected;
				}
				else
				{
					ActionAccessSpecifier = EAccessSpecifier::Public;
				}
			}

			if (FBlueprintEditorUtils::IsVariableCreatedByBlueprint(Blueprint, VariableProp) || VariableProp->GetOwner<UFunction>())
			{
				const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
				IconWidget = SNew(SPinTypeSelectorHelper, VariableProp, Blueprint, BlueprintEditorPtr)
					.IsEnabled(bIsEditingEnabled);
			}
		}
	};

	// For Variables and Local Variables, we will convert the icon widget into a pin type selector.
	if (GraphAction->GetTypeId() == FEdGraphSchemaAction_K2Var::StaticGetTypeId())
	{	
		GenerateVariableSettings(StaticCastSharedPtr<FEdGraphSchemaAction_K2Var>(GraphAction)->GetProperty());
	}
	else if (GraphAction->GetTypeId() == FEdGraphSchemaAction_K2LocalVar::StaticGetTypeId())
	{
		GenerateVariableSettings(StaticCastSharedPtr<FEdGraphSchemaAction_K2LocalVar>(GraphAction)->GetProperty());
	}
	// Determine the access level of this action if it is a function graph or for interface events
	else if (bShouldCheckForAccessSpec && GraphAction->GetTypeId() == FEdGraphSchemaAction_K2Graph::StaticGetTypeId())
	{
		UFunction* FunctionToCheck = nullptr;

		if (FEdGraphSchemaAction_K2Graph* FuncGraphAction = (FEdGraphSchemaAction_K2Graph*)(GraphAction.Get()))
		{
			FunctionToCheck = FindUField<UFunction>(Blueprint->SkeletonGeneratedClass, FuncGraphAction->FuncName);

			// Handle override/interface functions
			if(!FunctionToCheck)
			{
				FBlueprintEditorUtils::GetOverrideFunctionClass(Blueprint, FuncGraphAction->FuncName, &FunctionToCheck);			
			}
		}

		// If we have found a function that matches this action name, then grab it's access specifier
		if (FunctionToCheck)
		{
			if (FunctionToCheck->HasAnyFunctionFlags(FUNC_Protected))
			{
				ActionAccessSpecifier = EAccessSpecifier::Protected;
			}
			else if (FunctionToCheck->HasAnyFunctionFlags(FUNC_Private))
			{
				ActionAccessSpecifier = EAccessSpecifier::Private;
			}
			else
			{
				ActionAccessSpecifier = EAccessSpecifier::Public;
			}
		}
	}

	FText AccessModifierText = FText::GetEmpty();

	switch (ActionAccessSpecifier)
	{
		case EAccessSpecifier::Public:
		{
			AccessModifierText = LOCTEXT("AccessModifierPublic", "public");
		}
		break;
		case EAccessSpecifier::Protected:
		{
			AccessModifierText = LOCTEXT("AccessModifierProtected", "protected");
		}
		break;
		case EAccessSpecifier::Private:
		{
			AccessModifierText = LOCTEXT("AccessModifierPrivate", "private");
		}
		break;
	}

	// Calculate a color so that the text gets brighter the more accessible the action is
	const bool AccessSpecifierEnabled = (ActionAccessSpecifier != EAccessSpecifier::None) && bShouldCheckForAccessSpec;

	// Create the widget with an icon
	TSharedRef<SHorizontalBox> ActionBox = SNew(SHorizontalBox)		
		.AddMetaData<FTutorialMetaData>(TagMeta);

	ActionBox.Get().AddSlot()
		.AutoWidth()
		[
			IconWidget
		];

	// Only add an access specifier if we have one
	if (ActionAccessSpecifier != EAccessSpecifier::None)
	{
		ActionBox.Get().AddSlot()
			.MaxWidth(50.f)
			.FillWidth(AccessSpecifierEnabled ? 0.4f : 0.0f)
			.Padding(FMargin(/* horizontal */ AccessSpecifierEnabled ? 6.0f : 0.0f, /* vertical */ 0.0f))
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				// Will only display text if we have a modifier level
					.IsEnabled(AccessSpecifierEnabled)
					.Text(AccessModifierText)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					// Bold if public
					.TextStyle(FEditorStyle::Get(), ActionAccessSpecifier == EAccessSpecifier::Public ? "BlueprintEditor.AccessModifier.Public" : "BlueprintEditor.AccessModifier.Default")
			];
	}

	ActionBox.Get().AddSlot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(/* horizontal */ 3.0f, /* vertical */ 0.0f)
		[
			NameSlotWidget
		];

	ActionBox.Get().AddSlot()
		.AutoWidth()
		.Padding(FMargin(3.0f, 0.0f))
		.VAlign(VAlign_Center)
		[
			SNew(SPaletteItemVisibilityToggle, ActionPtr, InBlueprintEditor, InBlueprint)
			.IsEnabled(bIsEditingEnabled)
		];

	// Now, create the actual widget
	ChildSlot
	[
		ActionBox
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SBlueprintPaletteItem::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (BlueprintEditorPtr.IsValid())
	{
		SGraphPaletteItem::OnDragEnter(MyGeometry, DragDropEvent);
	}
}

/*******************************************************************************
* SBlueprintPaletteItem Private Methods
*******************************************************************************/

//------------------------------------------------------------------------------
TSharedRef<SWidget> SBlueprintPaletteItem::CreateTextSlotWidget(const FSlateFontInfo& NameFont, FCreateWidgetForActionData* const InCreateData, TAttribute<bool> bIsReadOnlyIn)
{
	FName const ActionTypeId = InCreateData->Action->GetTypeId();

	FOnVerifyTextChanged OnVerifyTextChanged;
	FOnTextCommitted     OnTextCommitted;
		
	// enums have different rules for renaming that exist outside the bounds of other items.
	if (ActionTypeId == FEdGraphSchemaAction_K2Enum::StaticGetTypeId())
	{
		OnVerifyTextChanged.BindStatic(&FBlueprintPaletteItemRenameUtils::VerifyNewEnumName, ActionPtr);
		OnTextCommitted.BindStatic(&FBlueprintPaletteItemRenameUtils::CommitNewEnumName, ActionPtr, BlueprintEditorPtr);
	}
	else if (ActionTypeId == FEdGraphSchemaAction_K2Struct::StaticGetTypeId())
	{
		OnVerifyTextChanged.BindStatic(&FBlueprintPaletteItemRenameUtils::VerifyNewStructName, ActionPtr );
		OnTextCommitted.BindStatic(&FBlueprintPaletteItemRenameUtils::CommitNewStructName, ActionPtr, BlueprintEditorPtr);
	}
	else if (ActionTypeId == FEdGraphSchemaAction_K2Event::StaticGetTypeId())
	{
		OnVerifyTextChanged.BindStatic(&FBlueprintPaletteItemRenameUtils::VerifyNewEventName, ActionPtr);
		OnTextCommitted.BindStatic(&FBlueprintPaletteItemRenameUtils::CommitNewEventName, ActionPtr);
	}
	else if (ActionTypeId == FEdGraphSchemaAction_K2TargetNode::StaticGetTypeId())
	{
		OnVerifyTextChanged.BindStatic(&FBlueprintPaletteItemRenameUtils::VerifyNewTargetNodeName, ActionPtr);
		OnTextCommitted.BindStatic(&FBlueprintPaletteItemRenameUtils::CommitNewTargetNodeName, ActionPtr);
	}
	else
	{
		// default to our own rename methods
		OnVerifyTextChanged.BindSP(this, &SBlueprintPaletteItem::OnNameTextVerifyChanged);
		OnTextCommitted.BindSP(this, &SBlueprintPaletteItem::OnNameTextCommitted);
	}

	// Copy the mouse delegate binding if we want it
	if( InCreateData->bHandleMouseButtonDown )
	{
		MouseButtonDownDelegate = InCreateData->MouseButtonDownDelegate;
	}

	TSharedPtr<SToolTip> ToolTipWidget = ConstructToolTipWidget();

	TSharedPtr<SOverlay> DisplayWidget;
	TSharedPtr<SInlineEditableTextBlock> EditableTextElement;
	SAssignNew(DisplayWidget, SOverlay)
		+SOverlay::Slot()
		[
			SAssignNew(EditableTextElement, SInlineEditableTextBlock)
				.Text(this, &SBlueprintPaletteItem::GetDisplayText)
				.Font(NameFont)
				.HighlightText(InCreateData->HighlightText)
				.ToolTip(ToolTipWidget)
				.OnVerifyTextChanged(OnVerifyTextChanged)
				.OnTextCommitted(OnTextCommitted)
				.IsSelected(InCreateData->IsRowSelectedDelegate)
				.IsReadOnly(bIsReadOnlyIn)
		];
	InlineRenameWidget = EditableTextElement.ToSharedRef();

	InCreateData->OnRenameRequest->BindSP(InlineRenameWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode);

	if (GetDefault<UBlueprintEditorSettings>()->bShowActionMenuItemSignatures && ActionPtr.IsValid())
	{
		check(InlineRenameWidget.IsValid());
		TSharedPtr<IToolTip> ExistingToolTip = InlineRenameWidget->GetToolTip();

		DisplayWidget->AddSlot(0)
			[
				SNew(SHorizontalBox)
				.Visibility(EVisibility::Visible)
				.ToolTip(ConstructToolTipWithActionPath(ActionPtr.Pin(), ExistingToolTip))
			];
	}

	return DisplayWidget.ToSharedRef();
}

//------------------------------------------------------------------------------
FText SBlueprintPaletteItem::GetDisplayText() const
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	if (MenuDescriptionCache.IsOutOfDate(K2Schema))
	{
		TSharedPtr< FEdGraphSchemaAction > GraphAction = ActionPtr.Pin();
		if (GraphAction->GetTypeId() == FEdGraphSchemaAction_K2Enum::StaticGetTypeId())
		{
			FEdGraphSchemaAction_K2Enum* EnumAction = (FEdGraphSchemaAction_K2Enum*)GraphAction.Get();
			FText DisplayText = FText::FromString(EnumAction->Enum->GetName());
			MenuDescriptionCache.SetCachedText(DisplayText, K2Schema);
		}
		else if (GraphAction->GetTypeId() == FEdGraphSchemaAction_K2Struct::StaticGetTypeId())
		{
			FEdGraphSchemaAction_K2Struct* StructAction = (FEdGraphSchemaAction_K2Struct*)GraphAction.Get();
			FText DisplayText = StructAction->Struct ? FText::FromString(StructAction->Struct->GetName()) : FText::FromString(TEXT("None"));
			MenuDescriptionCache.SetCachedText(DisplayText, K2Schema);
		}
		else
		{
			MenuDescriptionCache.SetCachedText(ActionPtr.Pin()->GetMenuDescription(), K2Schema);
		}
	}

	return MenuDescriptionCache;
}

//------------------------------------------------------------------------------
bool SBlueprintPaletteItem::OnNameTextVerifyChanged(const FText& InNewText, FText& OutErrorMessage)
{
	FString TextAsString = InNewText.ToString();

	FName OriginalName;

	UStruct* ValidationScope = nullptr;

	// Check if certain action names are unchanged.
	if (ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2Var::StaticGetTypeId())
	{
		FEdGraphSchemaAction_K2Var* VarAction = (FEdGraphSchemaAction_K2Var*)ActionPtr.Pin().Get();
		OriginalName = (VarAction->GetVariableName());
	}
	else if (ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2LocalVar::StaticGetTypeId())
	{
		FEdGraphSchemaAction_K2LocalVar* LocalVarAction = (FEdGraphSchemaAction_K2LocalVar*)ActionPtr.Pin().Get();
		OriginalName = (LocalVarAction->GetVariableName());
		
		ValidationScope = LocalVarAction->GetVariableScope();
	}
	else
	{
		UEdGraph* Graph = nullptr;

		if(ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2Graph::StaticGetTypeId())
		{
			FEdGraphSchemaAction_K2Graph* GraphAction = (FEdGraphSchemaAction_K2Graph*)ActionPtr.Pin().Get();
			Graph = GraphAction->EdGraph;
		}
		else if(ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2Delegate::StaticGetTypeId())
		{
			FEdGraphSchemaAction_K2Delegate* DelegateAction = (FEdGraphSchemaAction_K2Delegate*)ActionPtr.Pin().Get();
			Graph = DelegateAction->EdGraph;
		}

		if (Graph)
		{
			OriginalName = Graph->GetFName();
		}
	}

	UBlueprint* BlueprintObj = BlueprintEditorPtr.Pin()->GetBlueprintObj();
	check(BlueprintObj);

	if (BlueprintObj->SimpleConstructionScript)
	{
		for (USCS_Node* Node : BlueprintObj->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName() == OriginalName && !FComponentEditorUtils::IsValidVariableNameString(Node->ComponentTemplate, InNewText.ToString()))
			{
				OutErrorMessage = LOCTEXT("RenameFailed_NotValid", "This name is reserved for engine use.");
				return false;
			}
		}
	}

	TSharedPtr<INameValidatorInterface> NameValidator = MakeShareable(new FKismetNameValidator(BlueprintObj, OriginalName, ValidationScope));

	EValidatorResult ValidatorResult = NameValidator->IsValid(TextAsString);
	switch (ValidatorResult)
	{
	case EValidatorResult::Ok:
	case EValidatorResult::ExistingName:
		// These are fine, don't need to surface to the user, the rename can 'proceed' even if the name is the existing one
		break;
	default:
		OutErrorMessage = INameValidatorInterface::GetErrorText(TextAsString, ValidatorResult);
		break;
	}

	return OutErrorMessage.IsEmpty();
}

//------------------------------------------------------------------------------
void SBlueprintPaletteItem::OnNameTextCommitted(const FText& NewText, ETextCommit::Type InTextCommit)
{
	const FString NewNameString = NewText.ToString();
	const FName NewName = *NewNameString;

	if(ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2Graph::StaticGetTypeId())
	{
		FEdGraphSchemaAction_K2Graph* GraphAction = (FEdGraphSchemaAction_K2Graph*)ActionPtr.Pin().Get();

		UEdGraph* Graph = GraphAction->EdGraph;
		if (Graph && (Graph->bAllowDeletion || Graph->bAllowRenaming))
		{
			if (GraphAction->EdGraph)
			{
				if (const UEdGraphSchema* GraphSchema = GraphAction->EdGraph->GetSchema())
				{
					FGraphDisplayInfo DisplayInfo;
					GraphSchema->GetGraphDisplayInformation(*GraphAction->EdGraph, DisplayInfo);

					// Check if the name is unchanged
					if (NewText.EqualTo(DisplayInfo.PlainName))
					{
						return;
					}
				}
			}

			// Make sure we aren't renaming the graph into something that already exists
			UEdGraph* ExistingGraph = FindObject<UEdGraph>(Graph->GetOuter(), *NewNameString );
			if (ExistingGraph == nullptr || ExistingGraph == Graph)
			{
				const FScopedTransaction Transaction( LOCTEXT( "Rename Function", "Rename Function" ) );
				FBlueprintEditorUtils::RenameGraph(Graph, NewNameString );
			}
		}
	}
	else if(ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2Delegate::StaticGetTypeId())
	{
		FEdGraphSchemaAction_K2Delegate* DelegateAction = (FEdGraphSchemaAction_K2Delegate*)ActionPtr.Pin().Get();

		UEdGraph* Graph = DelegateAction->EdGraph;
		if (Graph && (Graph->bAllowDeletion || Graph->bAllowRenaming))
		{
			if (const UEdGraphSchema* GraphSchema = Graph->GetSchema())
			{
				FGraphDisplayInfo DisplayInfo;
				GraphSchema->GetGraphDisplayInformation(*Graph, DisplayInfo);

				// Check if the name is unchanged
				if (NewText.EqualTo(DisplayInfo.PlainName))
				{
					return;
				}
			}

			// Make sure we aren't renaming the graph into something that already exists
			UEdGraph* ExistingGraph = FindObject<UEdGraph>(Graph->GetOuter(), *NewNameString );
			if (ExistingGraph == nullptr || ExistingGraph == Graph)
			{
				const FScopedTransaction Transaction( LOCTEXT( "Rename Delegate", "Rename Event Dispatcher" ) );
				const FName OldName =  Graph->GetFName();

				UBlueprint* BlueprintObj = BlueprintEditorPtr.Pin()->GetBlueprintObj();
				FBlueprintEditorUtils::RenameMemberVariable(BlueprintObj, OldName, NewName);
			}
		}
	}
	else if (ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2Var::StaticGetTypeId())
	{
		FEdGraphSchemaAction_K2Var* VarAction = (FEdGraphSchemaAction_K2Var*)ActionPtr.Pin().Get();

		// Check if the name is unchanged
		if (NewName.IsEqual(VarAction->GetVariableName(), ENameCase::CaseSensitive))
		{
			return;
		}

		const FScopedTransaction Transaction( LOCTEXT( "RenameVariable", "Rename Variable" ) );

		BlueprintEditorPtr.Pin()->GetBlueprintObj()->Modify();

		// Double check we're not renaming a timeline disguised as a variable
		bool bIsTimeline = false;
		if (FProperty* VariableProperty = VarAction->GetProperty())
		{
			// Don't allow removal of timeline properties - you need to remove the timeline node for that
			FObjectProperty* ObjProperty = CastField<FObjectProperty>(VariableProperty);
			if (ObjProperty && ObjProperty->PropertyClass == UTimelineComponent::StaticClass())
			{
				bIsTimeline = true;
			}
		}

		// Rename as a timeline if required
		if (bIsTimeline)
		{
			FBlueprintEditorUtils::RenameTimeline(BlueprintEditorPtr.Pin()->GetBlueprintObj(), VarAction->GetVariableName(), NewName);
		}
		else
		{
			FBlueprintEditorUtils::RenameMemberVariable(BlueprintEditorPtr.Pin()->GetBlueprintObj(), VarAction->GetVariableName(), NewName);
		}
	}
	else if (ActionPtr.Pin()->GetTypeId() == FEdGraphSchemaAction_K2LocalVar::StaticGetTypeId())
	{
		FEdGraphSchemaAction_K2LocalVar* LocalVarAction = (FEdGraphSchemaAction_K2LocalVar*)ActionPtr.Pin().Get();

		// Check if the name is unchanged
		if (NewName.IsEqual(LocalVarAction->GetVariableName(), ENameCase::CaseSensitive))
		{
			return;
		}

		const FScopedTransaction Transaction( LOCTEXT( "RenameVariable", "Rename Variable" ) );

		BlueprintEditorPtr.Pin()->GetBlueprintObj()->Modify();

		FBlueprintEditorUtils::RenameLocalVariable(BlueprintEditorPtr.Pin()->GetBlueprintObj(), LocalVarAction->GetVariableScope(), LocalVarAction->GetVariableName(), NewName);
	}
	BlueprintEditorPtr.Pin()->GetMyBlueprintWidget()->SelectItemByName(NewName, ESelectInfo::OnMouseClick);
}

//------------------------------------------------------------------------------
FText SBlueprintPaletteItem::GetToolTipText() const
{
	TSharedPtr<FEdGraphSchemaAction> PaletteAction = ActionPtr.Pin();

	FText ToolTipText;
	FText ClassDisplayName;

	if (PaletteAction.IsValid())
	{
		// Default tooltip is taken from the action
		ToolTipText = PaletteAction->GetTooltipDescription().IsEmpty() ? PaletteAction->GetMenuDescription() : PaletteAction->GetTooltipDescription();

		if(PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2AddComponent::StaticGetTypeId())
		{
			FEdGraphSchemaAction_K2AddComponent* AddCompAction = (FEdGraphSchemaAction_K2AddComponent*)PaletteAction.Get();
			// Show component-specific tooltip
			UClass* ComponentClass = *(AddCompAction->ComponentClass);
			if (ComponentClass)
			{
				ToolTipText = ComponentClass->GetToolTipText();
			}
		}
		else if (UK2Node const* const NodeTemplate = FBlueprintActionMenuUtils::ExtractNodeTemplateFromAction(PaletteAction))
		{
			// If the node wants to create tooltip text, use that instead, because its probably more detailed
			FText NodeToolTipText = NodeTemplate->GetTooltipText();
			if (!NodeToolTipText.IsEmpty())
			{
				ToolTipText = NodeToolTipText;
			}

			if (UK2Node_CallFunction const* CallFuncNode = Cast<UK2Node_CallFunction const>(NodeTemplate))
			{			
				if(UClass* ParentClass = CallFuncNode->FunctionReference.GetMemberParentClass(CallFuncNode->GetBlueprintClassFromNode()))
				{
					UBlueprint* BlueprintObj = UBlueprint::GetBlueprintFromClass(ParentClass);
					if (BlueprintObj == nullptr)
					{
						ClassDisplayName = ParentClass->GetDisplayNameText();
					}
					else if (!BlueprintObj->HasAnyFlags(RF_Transient))
					{
						ClassDisplayName = FText::FromName(BlueprintObj->GetFName());
					}					
				}
			}
		}
		else if (PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2Graph::StaticGetTypeId())
		{
			FEdGraphSchemaAction_K2Graph* GraphAction = (FEdGraphSchemaAction_K2Graph*)PaletteAction.Get();
			if (GraphAction->EdGraph)
			{
				if (const UEdGraphSchema* GraphSchema = GraphAction->EdGraph->GetSchema())
				{
					FGraphDisplayInfo DisplayInfo;
					GraphSchema->GetGraphDisplayInformation(*(GraphAction->EdGraph), DisplayInfo);
					ToolTipText = DisplayInfo.Tooltip;
				}
			}
		}
		else if (PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2Var::StaticGetTypeId())
		{
			FEdGraphSchemaAction_K2Var* VarAction = (FEdGraphSchemaAction_K2Var*)PaletteAction.Get();
			UClass* VarClass = VarAction->GetVariableClass();
			if (bShowClassInTooltip && VarClass)
			{
				UBlueprint* BlueprintObj = UBlueprint::GetBlueprintFromClass(VarClass);
				ClassDisplayName = (BlueprintObj ? FText::FromName(BlueprintObj->GetFName()) : VarClass->GetDisplayNameText());
			}
			else
			{
				FString Result = GetVarTooltip(Blueprint, VarClass, VarAction->GetVariableName());
				// Only use the variable tooltip if it has been filled out.
				ToolTipText = FText::FromString( !Result.IsEmpty() ? Result : GetVarType(VarClass, VarAction->GetVariableName(), true, true) );
			}
		}
		else if (PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2LocalVar::StaticGetTypeId())
		{
			FEdGraphSchemaAction_K2LocalVar* LocalVarAction = (FEdGraphSchemaAction_K2LocalVar*)PaletteAction.Get();
			// The variable scope can not be found in intermediate graphs
			if(LocalVarAction->GetVariableScope())
			{
				UClass* VarClass = CastChecked<UClass>(LocalVarAction->GetVariableScope()->GetOuter());
				if (bShowClassInTooltip && (VarClass != nullptr))
				{
					UBlueprint* BlueprintObj = UBlueprint::GetBlueprintFromClass(VarClass);
					ClassDisplayName = (BlueprintObj ? FText::FromName(BlueprintObj->GetFName()) : VarClass->GetDisplayNameText());
				}
				else
				{
					FString Result;
					FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, LocalVarAction->GetVariableName(), LocalVarAction->GetVariableScope(), TEXT("tooltip"), Result);
					// Only use the variable tooltip if it has been filled out.
					ToolTipText = FText::FromString( !Result.IsEmpty() ? Result : GetVarType(LocalVarAction->GetVariableScope(), LocalVarAction->GetVariableName(), true, true) );
				}
			}
		}
		else if (PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2Delegate::StaticGetTypeId())
		{
			FEdGraphSchemaAction_K2Delegate* DelegateAction = (FEdGraphSchemaAction_K2Delegate*)PaletteAction.Get();
			
			FString Result = GetVarTooltip(Blueprint, DelegateAction->GetDelegateClass(), DelegateAction->GetDelegateName());
			ToolTipText = !Result.IsEmpty() ? FText::FromString(Result) : FText::FromName(DelegateAction->GetDelegateName());
		}
		else if (PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2Enum::StaticGetTypeId())
		{
			FEdGraphSchemaAction_K2Enum* EnumAction = (FEdGraphSchemaAction_K2Enum*)PaletteAction.Get();
			if (EnumAction->Enum)
			{
				ToolTipText = FText::FromName(EnumAction->Enum->GetFName());
			}
		}
		else if (PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2TargetNode::StaticGetTypeId())
		{
			FEdGraphSchemaAction_K2TargetNode* TargetNodeAction = (FEdGraphSchemaAction_K2TargetNode*)PaletteAction.Get();
			if (TargetNodeAction->NodeTemplate)
			{
				ToolTipText = TargetNodeAction->NodeTemplate->GetTooltipText();
			}
		}
	}

	if (bShowClassInTooltip && !ClassDisplayName.IsEmpty())
	{
		ToolTipText = FText::Format(LOCTEXT("BlueprintItemClassTooltip", "{0}\nClass: {1}"), ToolTipText, ClassDisplayName);
	}

	return ToolTipText;
}

TSharedPtr<SToolTip> SBlueprintPaletteItem::ConstructToolTipWidget() const
{
	TSharedPtr<FEdGraphSchemaAction> PaletteAction = ActionPtr.Pin();
	UEdGraphNode const* const NodeTemplate = FBlueprintActionMenuUtils::ExtractNodeTemplateFromAction(PaletteAction);

	FBlueprintActionMenuItem::FDocExcerptRef DocExcerptRef;

	if (PaletteAction.IsValid())
	{
		if (NodeTemplate != nullptr)
		{
			// Take rich tooltip from node
			DocExcerptRef.DocLink = NodeTemplate->GetDocumentationLink();
			DocExcerptRef.DocExcerptName = NodeTemplate->GetDocumentationExcerptName();

			// sometimes, with FBlueprintActionMenuItem's, the NodeTemplate 
			// doesn't always reflect the node that will be spawned (some things 
			// we don't want to be executed until spawn time, like adding of 
			// component templates)... in that case, the 
			// FBlueprintActionMenuItem's may have a more specific documentation 
			// link of its own (most of the time, it will reflect the NodeTemplate's)
			if ( !DocExcerptRef.IsValid() && (PaletteAction->GetTypeId() == FBlueprintActionMenuItem::StaticGetTypeId()) )
			{
				FBlueprintActionMenuItem* NodeSpawnerAction = (FBlueprintActionMenuItem*)PaletteAction.Get();
				DocExcerptRef = NodeSpawnerAction->GetDocumentationExcerpt();
			}
		}
		else if (PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2Graph::StaticGetTypeId())
		{
			FEdGraphSchemaAction_K2Graph* GraphAction = (FEdGraphSchemaAction_K2Graph*)PaletteAction.Get();
			if (GraphAction->EdGraph)
			{
				FGraphDisplayInfo DisplayInfo;
				if (const UEdGraphSchema* GraphSchema = GraphAction->EdGraph->GetSchema())
				{
					GraphSchema->GetGraphDisplayInformation(*(GraphAction->EdGraph), DisplayInfo);
				}

				DocExcerptRef.DocLink = DisplayInfo.DocLink;
				DocExcerptRef.DocExcerptName = DisplayInfo.DocExcerptName;
			}
		}
		else if (PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2Var::StaticGetTypeId())
		{
			FEdGraphSchemaAction_K2Var* VarAction = (FEdGraphSchemaAction_K2Var*)PaletteAction.Get();
			UClass* VarClass = VarAction->GetVariableClass();
			if (!bShowClassInTooltip || VarClass == nullptr)
			{
				// Don't show big tooltip if we are showing class as well (means we are not in MyBlueprint)
				DocExcerptRef.DocLink = TEXT("Shared/Editors/BlueprintEditor/GraphTypes");
				DocExcerptRef.DocExcerptName = TEXT("Variable");
			}
		}
		else if (PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2Event::StaticGetTypeId())
		{
			DocExcerptRef.DocLink = TEXT("Shared/Editors/BlueprintEditor/GraphTypes");
			DocExcerptRef.DocExcerptName = TEXT("Event");
		}
		else if (PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2AddComment::StaticGetTypeId() ||
			PaletteAction->GetTypeId() == FEdGraphSchemaAction_NewStateComment::StaticGetTypeId())
		{
			// Taking tooltip from action is fine
			const UEdGraphNode_Comment* DefaultComment = GetDefault<UEdGraphNode_Comment>();
			DocExcerptRef.DocLink = DefaultComment->GetDocumentationLink();
			DocExcerptRef.DocExcerptName = DefaultComment->GetDocumentationExcerptName();
		}
		else if (PaletteAction->GetTypeId() == FEdGraphSchemaAction_K2LocalVar::StaticGetTypeId())
		{
			// Don't show big tooltip if we are showing class as well (means we are not in MyBlueprint)
			DocExcerptRef.DocLink = TEXT("Shared/Editors/BlueprintEditor/GraphTypes");
			DocExcerptRef.DocExcerptName = TEXT("LocalVariable");
		}
	}

	// Setup the attribute for dynamically pulling the tooltip
	TAttribute<FText> TextAttribute;
	TextAttribute.Bind(this, &SBlueprintPaletteItem::GetToolTipText);

	TSharedRef< SToolTip > TooltipWidget = IDocumentation::Get()->CreateToolTip(TextAttribute, nullptr, DocExcerptRef.DocLink, DocExcerptRef.DocExcerptName);

	// English speakers have no real need to know this exists.
	if ( (NodeTemplate != nullptr) && (FInternationalization::Get().GetCurrentCulture()->GetTwoLetterISOLanguageName() != TEXT("en")) )
	{
		FText NativeNodeName = FText::FromString(NodeTemplate->GetNodeTitle(ENodeTitleType::ListView).BuildSourceString());
		const FTextBlockStyle& SubduedTextStyle = FEditorStyle::GetWidgetStyle<FTextBlockStyle>("Documentation.SDocumentationTooltipSubdued");

		TSharedPtr<SToolTip> InternationalTooltip;
		TSharedPtr<SVerticalBox> TooltipBody;

		SAssignNew(InternationalTooltip, SToolTip)
			// Emulate text-only tool-tip styling that SToolTip uses 
			// when no custom content is supplied.  We want node tool-
			// tips to be styled just like text-only tool-tips
			.BorderImage( FCoreStyle::Get().GetBrush("ToolTip.BrightBackground") )
			.TextMargin(FMargin(11.0f))
		[
			SAssignNew(TooltipBody, SVerticalBox)
		];

		if (!DocExcerptRef.IsValid())
		{
			auto GetNativeNamePromptVisibility = []()->EVisibility
			{
				FModifierKeysState KeyState = FSlateApplication::Get().GetModifierKeys();
				return KeyState.IsAltDown() ? EVisibility::Collapsed : EVisibility::Visible;
			};

			TooltipBody->AddSlot()
			[
				SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "Documentation.SDocumentationTooltip")
					.Text(NativeNodeName)
					.Visibility_Lambda([GetNativeNamePromptVisibility]()->EVisibility
					{
						return (GetNativeNamePromptVisibility() == EVisibility::Visible) ? EVisibility::Collapsed : EVisibility::Visible;
					})
			];

			TooltipBody->AddSlot()
			[
				SNew(SHorizontalBox)
					.Visibility_Lambda(GetNativeNamePromptVisibility)
				+SHorizontalBox::Slot()
				[
					TooltipWidget->GetContentWidget()
				]
			];

			TooltipBody->AddSlot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.f, 8.f, 0.f, 0.f)
			[

				SNew(STextBlock)
					.Text( LOCTEXT("NativeNodeName", "hold (Alt) for native node name") )
					.TextStyle(&SubduedTextStyle)
					.Visibility_Lambda(GetNativeNamePromptVisibility)
			];
		}
		else
		{
			auto GetNativeNodeNameVisibility = []()->EVisibility
			{
				FModifierKeysState KeyState = FSlateApplication::Get().GetModifierKeys();
				return KeyState.IsAltDown() && KeyState.IsControlDown() ? EVisibility::Visible : EVisibility::Collapsed;
			};

			// give the "advanced" tooltip a header
			TooltipBody->AddSlot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				.Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
					.AutoWidth()
				[
					SNew(STextBlock)
					.TextStyle(&SubduedTextStyle)
						.Text(LOCTEXT("NativeNodeNameLabel", "Native Node Name: "))
						.Visibility_Lambda(GetNativeNodeNameVisibility)
				]
				+SHorizontalBox::Slot()
					.AutoWidth()
				[
					SNew(STextBlock)
						.TextStyle(&SubduedTextStyle)
						.Text(NativeNodeName)
						.Visibility_Lambda(GetNativeNodeNameVisibility)
				]
			];

			TooltipBody->AddSlot()
			[
				TooltipWidget->GetContentWidget()
			];
		}

		return InternationalTooltip;
	}
	return TooltipWidget;
}

/*******************************************************************************
* SBlueprintPalette
*******************************************************************************/

//------------------------------------------------------------------------------
void SBlueprintPalette::Construct(const FArguments& InArgs, TWeakPtr<FBlueprintEditor> InBlueprintEditor)
{
	// Create the asset discovery indicator
	FEditorWidgetsModule& EditorWidgetsModule = FModuleManager::LoadModuleChecked<FEditorWidgetsModule>("EditorWidgets");
	TSharedRef<SWidget> AssetDiscoveryIndicator = EditorWidgetsModule.CreateAssetDiscoveryIndicator(EAssetDiscoveryIndicatorScaleMode::Scale_Vertical);

	float FavoritesHeightRatio = 0.33f;
	GConfig->GetFloat(*BlueprintPalette::ConfigSection, *BlueprintPalette::FavoritesHeightConfigKey, FavoritesHeightRatio, GEditorPerProjectIni);
	float LibraryHeightRatio = 1.f - FavoritesHeightRatio;
	GConfig->GetFloat(*BlueprintPalette::ConfigSection, *BlueprintPalette::LibraryHeightConfigKey, LibraryHeightRatio, GEditorPerProjectIni);

	bool bUseLegacyLayout = false;
	GConfig->GetBool(*BlueprintPalette::ConfigSection, TEXT("bUseLegacyLayout"), bUseLegacyLayout, GEditorIni);

	if (bUseLegacyLayout)
	{
		this->ChildSlot
		[
			SAssignNew(LibraryWrapper, SBlueprintLibraryPalette, InBlueprintEditor)
				.UseLegacyLayout(bUseLegacyLayout)
		];
	}
	else 
	{
		this->ChildSlot
		[
			SAssignNew(PaletteSplitter, SSplitter)
				.Orientation(Orient_Vertical)
				.OnSplitterFinishedResizing(this, &SBlueprintPalette::OnSplitterResized)
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("FullBlueprintPalette")))

			+ SSplitter::Slot()
			.Value(FavoritesHeightRatio)
			[
				SNew(SBlueprintFavoritesPalette, InBlueprintEditor)
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("BlueprintPaletteFavorites")))
			]

			+ SSplitter::Slot()
			.Value(LibraryHeightRatio)
			[
				SNew(SBlueprintLibraryPalette, InBlueprintEditor)
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("BlueprintPaletteLibrary")))
			]
		];
	}	
}

//------------------------------------------------------------------------------
void SBlueprintPalette::OnSplitterResized() const
{
	FChildren const* const SplitterChildren = PaletteSplitter->GetChildren();
	for (int32 SlotIndex = 0; SlotIndex < SplitterChildren->Num(); ++SlotIndex)
	{
		SSplitter::FSlot const& SplitterSlot = PaletteSplitter->SlotAt(SlotIndex);

		if (SplitterSlot.GetWidget() == FavoritesWrapper)
		{
			GConfig->SetFloat(*BlueprintPalette::ConfigSection, *BlueprintPalette::FavoritesHeightConfigKey, SplitterSlot.SizeValue.Get(), GEditorPerProjectIni);
		}
		else if (SplitterSlot.GetWidget() == LibraryWrapper)
		{
			GConfig->SetFloat(*BlueprintPalette::ConfigSection, *BlueprintPalette::LibraryHeightConfigKey, SplitterSlot.SizeValue.Get(), GEditorPerProjectIni);
		}

	}
}

#undef LOCTEXT_NAMESPACE
