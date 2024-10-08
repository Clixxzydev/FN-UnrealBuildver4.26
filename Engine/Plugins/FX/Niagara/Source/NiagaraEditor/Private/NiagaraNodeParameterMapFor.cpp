// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraNodeParameterMapFor.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraEditorUtilities.h"
#include "SNiagaraGraphNodeConvert.h"
#include "NiagaraHlslTranslator.h"
#include "Templates/SharedPointer.h"
#include "NiagaraGraph.h"
#include "NiagaraConstants.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "EdGraph/EdGraphNode.h"

#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "NiagaraNodeParameterMapFor" 

UNiagaraNodeParameterMapFor::UNiagaraNodeParameterMapFor() : UNiagaraNodeParameterMapSet()
{

	UEdGraphNode::NodeUpgradeMessage = LOCTEXT("NodeExperimental", "This node is marked as experimental, use with care!");
}

void UNiagaraNodeParameterMapFor::AllocateDefaultPins()
{
	PinPendingRename = nullptr;
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	CreatePin(EGPD_Input, Schema->TypeDefinitionToPinType(FNiagaraTypeDefinition::GetParameterMapDef()), *UNiagaraNodeParameterMapBase::SourcePinName.ToString());
	CreatePin(EGPD_Input, Schema->TypeDefinitionToPinType(FNiagaraTypeDefinition::GetIntDef()), TEXT("Module.Iteration Count"));
	CreatePin(EGPD_Output, Schema->TypeDefinitionToPinType(FNiagaraTypeDefinition::GetParameterMapDef()), *UNiagaraNodeParameterMapBase::DestPinName.ToString());
	CreateAddPin(EGPD_Input);
}

void UNiagaraNodeParameterMapFor::Compile(FHlslNiagaraTranslator* Translator, TArray<int32>& Outputs)
{
	TArray<UEdGraphPin*> InputPins;
	GetInputPins(InputPins);
	if (Translator)
	{
		if (Translator->GetSimulationTarget() == ENiagaraSimTarget::GPUComputeSim)
		{
			const int32 IterationCount = Translator->CompilePin(InputPins[1]);
			Translator->ParameterMapForBegin(this, IterationCount);

			UNiagaraNodeParameterMapSet::Compile(Translator, Outputs);

			Translator->ParameterMapForEnd(this);
		}
		else
		{
			UNiagaraNodeParameterMapSet::Compile(Translator, Outputs);
			//Translator->Message(FNiagaraCompileEventSeverity::Log,LOCTEXT("UnsupportedParamMapFor", "Parameter map for is not yet supported on cpu."), this, nullptr);
		}
	}
}

FText UNiagaraNodeParameterMapFor::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("UNiagaraNodeParameterMapForName", "Map For");
}

#undef LOCTEXT_NAMESPACE
