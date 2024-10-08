// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_CustomProperty.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "DetailLayoutBuilder.h"
#include "AnimationGraphSchema.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimLayerInterface.h"
#include "DetailCategoryBuilder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "KismetCompilerMisc.h"
#include "KismetCompiler.h"

#define LOCTEXT_NAMESPACE "CustomPropNode"

void UAnimGraphNode_CustomProperty::CreateClassVariablesFromBlueprint(FKismetCompilerContext& InCompilerContext)
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (!Pin->bOrphanedPin && !UAnimationGraphSchema::IsPosePin(Pin->PinType))
		{
			// avoid to add properties which already exist on the custom node.
			// for example the ControlRig_CustomNode has a pin called "alpha" which is not custom.
			if (FStructProperty* NodeProperty = CastField<FStructProperty>(GetClass()->FindPropertyByName(TEXT("Node"))))
			{
				if(NodeProperty->Struct->FindPropertyByName(Pin->GetFName()))
				{
					continue;
				}
			}

			// Add prefix to avoid collisions
			FString PrefixedName = GetPinTargetVariableName(Pin);

			// Create a property on the new class to hold the pin data
			FProperty* NewProperty = FKismetCompilerUtilities::CreatePropertyOnScope(InCompilerContext.NewClass, FName(*PrefixedName), Pin->PinType, InCompilerContext.NewClass, CPF_None, CastChecked<UEdGraphSchema_K2>(GetSchema()), InCompilerContext.MessageLog);
			if (NewProperty)
			{
				FKismetCompilerUtilities::LinkAddedProperty(InCompilerContext.NewClass, NewProperty);
			}
		}
	}
}

void UAnimGraphNode_CustomProperty::OnProcessDuringCompilation(FAnimBlueprintCompilerContext& InCompilerContext)
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (!Pin->bOrphanedPin && !UAnimationGraphSchema::IsPosePin(Pin->PinType))
		{
			// avoid to add properties which already exist on the custom node.
			// for example the ControlRig_CustomNode has a pin called "alpha" which is not custom.
			if (FStructProperty* NodeProperty = CastField<FStructProperty>(GetClass()->FindPropertyByName(TEXT("Node"))))
			{
				if(NodeProperty->Struct->FindPropertyByName(Pin->GetFName()))
				{
					continue;
				}
			}
			
			FString PrefixedName = GetPinTargetVariableName(Pin);

			// Add mappings to the node
			UClass* InstClass = GetTargetSkeletonClass();
			if (FProperty* FoundProperty = FindFProperty<FProperty>(InstClass, Pin->PinName))
			{
				AddSourceTargetProperties(*PrefixedName, FoundProperty->GetFName());
			}
			else
			{
				AddSourceTargetProperties(*PrefixedName, Pin->GetFName());
			}
		}
	}
}

void UAnimGraphNode_CustomProperty::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);

	UAnimBlueprint* AnimBP = CastChecked<UAnimBlueprint>(GetBlueprint());

	UObject* OriginalNode = MessageLog.FindSourceObject(this);

	if(NeedsToSpecifyValidTargetClass())
	{
		// Check we have a class set
		UClass* TargetClass = GetTargetClass();
		if(!TargetClass)
		{
			MessageLog.Error(TEXT("Linked graph node @@ has no valid instance class to spawn."), this);
		}
	}
}

void UAnimGraphNode_CustomProperty::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	Super::ReallocatePinsDuringReconstruction(OldPins);

	// Grab the SKELETON class here as when we are reconstructed during during BP compilation
	// the full generated class is not yet present built.
	UClass* TargetClass = GetTargetSkeletonClass();

	if(!TargetClass)
	{
		// Nothing to search for properties
		return;
	}

	// Need the schema to extract pin types
	const UEdGraphSchema_K2* Schema = CastChecked<UEdGraphSchema_K2>(GetSchema());

	// Default anim schema for util funcions
	const UAnimationGraphSchema* AnimGraphDefaultSchema = GetDefault<UAnimationGraphSchema>();

	// Grab the list of properties we can expose
	TArray<FProperty*> ExposablePropeties;
	GetExposableProperties(ExposablePropeties);

	// We'll track the names we encounter by removing from this list, if anything remains the properties
	// have been removed from the target class and we should remove them too
	TArray<FName> BeginExposableNames = KnownExposableProperties;

	for(FProperty* Property : ExposablePropeties)
	{
		FName PropertyName = Property->GetFName();
		BeginExposableNames.Remove(PropertyName);

		if(!KnownExposableProperties.Contains(PropertyName))
		{
			// New property added to the target class
			KnownExposableProperties.Add(PropertyName);
		}

		if(ExposedPropertyNames.Contains(PropertyName) && FBlueprintEditorUtils::PropertyStillExists(Property))
		{
			FEdGraphPinType PinType;

			verify(Schema->ConvertPropertyToPinType(Property, PinType));

			UEdGraphPin* NewPin = CreatePin(EEdGraphPinDirection::EGPD_Input, PinType, Property->GetFName());
			NewPin->PinFriendlyName = Property->GetDisplayNameText();

			// We cant interrogate CDO here as we may be mid-compile, so we can only really
			// reset to the autogenerated default.
			AnimGraphDefaultSchema->ResetPinToAutogeneratedDefaultValue(NewPin, false);

			CustomizePinData(NewPin, PropertyName, INDEX_NONE);
		}
	}

	// Remove any properties that no longer exist on the target class
	for(FName& RemovedPropertyName : BeginExposableNames)
	{
		KnownExposableProperties.Remove(RemovedPropertyName);
	}
}

void UAnimGraphNode_CustomProperty::GetInstancePinProperty(const UClass* InOwnerInstanceClass, UEdGraphPin* InInputPin, FProperty*& OutProperty)
{
	// The actual name of the instance property
	FString FullName = GetPinTargetVariableName(InInputPin);

	if(FProperty* Property = FindFProperty<FProperty>(InOwnerInstanceClass, *FullName))
	{
		OutProperty = Property;
	}
	else
	{
		OutProperty = nullptr;
	}
}

FString UAnimGraphNode_CustomProperty::GetPinTargetVariableName(const UEdGraphPin* InPin) const
{
	return TEXT("__CustomProperty_") + InPin->PinName.ToString() + TEXT("_") + NodeGuid.ToString();
}

FText UAnimGraphNode_CustomProperty::GetPropertyTypeText(FProperty* Property)
{
	FText PropertyTypeText;

	if(FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		PropertyTypeText = StructProperty->Struct->GetDisplayNameText();
	}
	else if(FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
	{
		PropertyTypeText = ObjectProperty->PropertyClass->GetDisplayNameText();
	}
	else if(FFieldClass* PropClass = Property->GetClass())
	{
		PropertyTypeText = PropClass->GetDisplayNameText();
	}
	else
	{
		PropertyTypeText = LOCTEXT("PropertyTypeUnknown", "Unknown");
	}
	
	return PropertyTypeText;
}

void UAnimGraphNode_CustomProperty::RebuildExposedProperties()
{
	ExposedPropertyNames.Empty();
	KnownExposableProperties.Empty();
	TArray<FProperty*> ExposableProperties;
	GetExposableProperties(ExposableProperties);
	for(FProperty* Property : ExposableProperties)
	{
		KnownExposableProperties.Add(Property->GetFName());
	}
}

ECheckBoxState UAnimGraphNode_CustomProperty::AreAllPropertiesExposed() const
{
	if(ExposedPropertyNames.Num() == 0)
	{
		return ECheckBoxState::Unchecked;
	}
	else
	{
		for(FName PropertyName : KnownExposableProperties)
		{
			if(!ExposedPropertyNames.Contains(PropertyName))
			{
				return ECheckBoxState::Undetermined;
			}
		}
	}

	return ECheckBoxState::Checked;
}

void UAnimGraphNode_CustomProperty::OnPropertyExposeAllCheckboxChanged(ECheckBoxState NewState)
{
	if(NewState == ECheckBoxState::Checked)
	{
		ExposedPropertyNames = KnownExposableProperties;
	}
	else
	{
		ExposedPropertyNames.Empty();
	}

	ReconstructNode();
}

ECheckBoxState UAnimGraphNode_CustomProperty::IsPropertyExposed(FName PropertyName) const
{
	return ExposedPropertyNames.Contains(PropertyName) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void UAnimGraphNode_CustomProperty::OnPropertyExposeCheckboxChanged(ECheckBoxState NewState, FName PropertyName)
{
	if(NewState == ECheckBoxState::Checked)
	{
		ExposedPropertyNames.AddUnique(PropertyName);
	}
	else if(NewState == ECheckBoxState::Unchecked)
	{
		ExposedPropertyNames.Remove(PropertyName);
	}

	ReconstructNode();
}

void UAnimGraphNode_CustomProperty::OnInstanceClassChanged(IDetailLayoutBuilder* DetailBuilder)
{
	if(DetailBuilder)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}

UObject* UAnimGraphNode_CustomProperty::GetJumpTargetForDoubleClick() const
{
	UClass* InstanceClass = GetTargetClass();
	
	if(InstanceClass)
	{
		return InstanceClass->ClassGeneratedBy;
	}

	return nullptr;
}

bool UAnimGraphNode_CustomProperty::HasExternalDependencies(TArray<class UStruct*>* OptionalOutput /*= NULL*/) const
{
	UClass* InstanceClassToUse = GetTargetClass();

	// Add our instance class... If that changes we need a recompile
	if(InstanceClassToUse && OptionalOutput)
	{
		OptionalOutput->AddUnique(InstanceClassToUse);
	}

	bool bSuperResult = Super::HasExternalDependencies(OptionalOutput);
	return InstanceClassToUse || bSuperResult;
}

void UAnimGraphNode_CustomProperty::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Super::CustomizeDetails(DetailBuilder);

	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(FName(TEXT("Settings")));

	// Customize InstanceClass
	{
		TSharedRef<IPropertyHandle> ClassHandle = DetailBuilder.GetProperty(TEXT("Node.InstanceClass"), GetClass());
		if (ClassHandle->IsValidHandle())
		{
			ClassHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateUObject(this, &UAnimGraphNode_CustomProperty::OnStructuralPropertyChanged, &DetailBuilder));
		}
	}
}

void UAnimGraphNode_CustomProperty::GetExposableProperties( TArray<FProperty*>& OutExposableProperties) const
{
	OutExposableProperties.Empty();

	UClass* TargetClass = GetTargetClass();

	if(TargetClass)
	{
		const UEdGraphSchema_K2* Schema = CastChecked<UEdGraphSchema_K2>(GetSchema());

		for(TFieldIterator<FProperty> It(TargetClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FProperty* CurProperty = *It;
			FEdGraphPinType PinType;

			if(CurProperty->HasAllPropertyFlags(CPF_Edit | CPF_BlueprintVisible) && CurProperty->HasAllFlags(RF_Public) && Schema->ConvertPropertyToPinType(CurProperty, PinType))
			{
				OutExposableProperties.Add(CurProperty);
			}
		}
	}
}

void UAnimGraphNode_CustomProperty::AddSourceTargetProperties(const FName& InSourcePropertyName, const FName& InTargetPropertyName)
{
	FAnimNode_CustomProperty* CustomPropAnimNode = GetCustomPropertyNode();
	if (CustomPropAnimNode)
	{
		CustomPropAnimNode->SourcePropertyNames.Add(InSourcePropertyName);
		CustomPropAnimNode->DestPropertyNames.Add(InTargetPropertyName);
	}
}

UClass* UAnimGraphNode_CustomProperty::GetTargetClass() const
{
	const FAnimNode_CustomProperty* CustomPropAnimNode = GetCustomPropertyNode();
	if (CustomPropAnimNode)
	{
		return CustomPropAnimNode->GetTargetClass();
	}

	return nullptr;
}

UClass* UAnimGraphNode_CustomProperty::GetTargetSkeletonClass() const
{
	UClass* TargetClass = GetTargetClass();
	if(TargetClass && TargetClass->ClassGeneratedBy)
	{
		UBlueprint* Blueprint = CastChecked<UBlueprint>(TargetClass->ClassGeneratedBy);
		if(Blueprint)
		{
			if (Blueprint->SkeletonGeneratedClass)
			{
				return Blueprint->SkeletonGeneratedClass;
			}
		}
	}
	return TargetClass;
}

void UAnimGraphNode_CustomProperty::OnStructuralPropertyChanged(IDetailLayoutBuilder* DetailBuilder)
{
	if(DetailBuilder)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}

#undef LOCTEXT_NAMESPACE