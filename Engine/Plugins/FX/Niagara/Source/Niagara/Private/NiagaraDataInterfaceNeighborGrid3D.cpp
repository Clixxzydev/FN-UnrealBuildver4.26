// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceNeighborGrid3D.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraRenderer.h"
#include "NiagaraShaderParticleID.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceNeighborGrid3D"

static const FString MaxNeighborsPerCellName(TEXT("MaxNeighborsPerCell_"));
static const FString ParticleNeighborsName(TEXT("ParticleNeighbors_"));
static const FString ParticleNeighborCountName(TEXT("ParticleNeighborCount_"));
static const FString OutputParticleNeighborsName(TEXT("OutputParticleNeighbors_"));
static const FString OutputParticleNeighborCountName(TEXT("OutputParticleNeighborCount_"));


// Global VM function names, also used by the shaders code generation methods.

static const FName MaxNeighborsPerCellFunctionName("MaxNeighborsPerCell");
static const FName NeighborGridIndexToLinearFunctionName("NeighborGridIndexToLinear");
static const FName GetParticleNeighborFunctionName("GetParticleNeighbor");
static const FName SetParticleNeighborFunctionName("SetParticleNeighbor");
static const FName GetParticleNeighborCountFunctionName("GetParticleNeighborCount");
static const FName SetParticleNeighborCountFunctionName("SetParticleNeighborCount");


/*--------------------------------------------------------------------------------------------------------------------------*/
struct FNiagaraDataInterfaceParametersCS_NeighborGrid3D : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_NeighborGrid3D, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		NumCellsParam.Bind(ParameterMap, *(NumCellsName + ParameterInfo.DataInterfaceHLSLSymbol));		
		CellSizeParam.Bind(ParameterMap, *(CellSizeName + ParameterInfo.DataInterfaceHLSLSymbol));

		MaxNeighborsPerCellParam.Bind(ParameterMap, *(MaxNeighborsPerCellName + ParameterInfo.DataInterfaceHLSLSymbol));
		WorldBBoxSizeParam.Bind(ParameterMap, *(WorldBBoxSizeName + ParameterInfo.DataInterfaceHLSLSymbol));
		
		ParticleNeighborsGridParam.Bind(ParameterMap,  *(ParticleNeighborsName + ParameterInfo.DataInterfaceHLSLSymbol));
		ParticleNeighborCountGridParam.Bind(ParameterMap,  *(ParticleNeighborCountName + ParameterInfo.DataInterfaceHLSLSymbol));

		OutputParticleNeighborsGridParam.Bind(ParameterMap, *(OutputParticleNeighborsName + ParameterInfo.DataInterfaceHLSLSymbol));
		OutputParticleNeighborCountGridParam.Bind(ParameterMap, *(OutputParticleNeighborCountName + ParameterInfo.DataInterfaceHLSLSymbol));		
	}

	// #todo(dmp): make resource transitions batched
	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		// Get shader and DI
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
		FNiagaraDataInterfaceProxyNeighborGrid3D* VFDI = static_cast<FNiagaraDataInterfaceProxyNeighborGrid3D*>(Context.DataInterface);

		NeighborGrid3DRWInstanceData* ProxyData = VFDI->SystemInstancesToProxyData.Find(Context.SystemInstanceID);
		float CellSizeTmp[3];

		if (!ProxyData)
		{
			CellSizeTmp[0] = CellSizeTmp[1] = CellSizeTmp[2] = 1.0f;
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumCellsParam, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, CellSizeParam, CellSizeTmp);
			SetShaderValue(RHICmdList, ComputeShaderRHI, MaxNeighborsPerCellParam, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldBBoxSizeParam, FVector(0.0f, 0.0f, 0.0f));
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ParticleNeighborsGridParam, FNiagaraRenderer::GetDummyIntBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ParticleNeighborCountGridParam, FNiagaraRenderer::GetDummyIntBuffer());
			if (OutputParticleNeighborsGridParam.IsUAVBound())
			{
				RHICmdList.SetUAVParameter(ComputeShaderRHI, OutputParticleNeighborsGridParam.GetUAVIndex(), Context.Batcher->GetEmptyRWBufferFromPool(RHICmdList, PF_R32_SINT));
			}

			if (OutputParticleNeighborCountGridParam.IsUAVBound())
			{
				RHICmdList.SetUAVParameter(ComputeShaderRHI, OutputParticleNeighborCountGridParam.GetUAVIndex(), Context.Batcher->GetEmptyRWBufferFromPool(RHICmdList, PF_R32_SINT));
			}

			return;
		}

		SetShaderValue(RHICmdList, ComputeShaderRHI, NumCellsParam, ProxyData->NumCells);

		// #todo(dmp): remove this computation here
		CellSizeTmp[0] = ProxyData->WorldBBoxSize.X / ProxyData->NumCells.X;
		CellSizeTmp[1] = ProxyData->WorldBBoxSize.Y / ProxyData->NumCells.Y;
		CellSizeTmp[2] = ProxyData->WorldBBoxSize.Z / ProxyData->NumCells.Z;
		SetShaderValue(RHICmdList, ComputeShaderRHI, CellSizeParam, CellSizeTmp);

		SetShaderValue(RHICmdList, ComputeShaderRHI, MaxNeighborsPerCellParam, ProxyData->MaxNeighborsPerCell);		
		SetShaderValue(RHICmdList, ComputeShaderRHI, WorldBBoxSizeParam, ProxyData->WorldBBoxSize);
		
		if (!Context.IsOutputStage)
		{
			if (ParticleNeighborsGridParam.IsBound())
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, ProxyData->NeighborhoodBuffer.UAV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, ParticleNeighborsGridParam, ProxyData->NeighborhoodBuffer.SRV);
			}

			if (ParticleNeighborCountGridParam.IsBound())
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, ProxyData->NeighborhoodCountBuffer.UAV);
				SetSRVParameter(RHICmdList, ComputeShaderRHI, ParticleNeighborCountGridParam, ProxyData->NeighborhoodCountBuffer.SRV);
			}

			if (OutputParticleNeighborsGridParam.IsUAVBound())
			{
				RHICmdList.SetUAVParameter(ComputeShaderRHI, OutputParticleNeighborsGridParam.GetUAVIndex(), Context.Batcher->GetEmptyRWBufferFromPool(RHICmdList, PF_R32_SINT));
			}

			if (OutputParticleNeighborCountGridParam.IsUAVBound())
			{
				RHICmdList.SetUAVParameter(ComputeShaderRHI, OutputParticleNeighborCountGridParam.GetUAVIndex(), Context.Batcher->GetEmptyRWBufferFromPool(RHICmdList, PF_R32_SINT));
			}
		}
		else
		{
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ParticleNeighborsGridParam, FNiagaraRenderer::GetDummyIntBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ParticleNeighborCountGridParam, FNiagaraRenderer::GetDummyIntBuffer());

			if (OutputParticleNeighborsGridParam.IsBound())
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, ProxyData->NeighborhoodBuffer.UAV);
				OutputParticleNeighborsGridParam.SetBuffer(RHICmdList, ComputeShaderRHI, ProxyData->NeighborhoodBuffer);
			}

			if (OutputParticleNeighborCountGridParam.IsBound())
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, ProxyData->NeighborhoodCountBuffer.UAV);
				OutputParticleNeighborCountGridParam.SetBuffer(RHICmdList, ComputeShaderRHI, ProxyData->NeighborhoodCountBuffer);
			}
		}
		// Note: There is a flush in PreEditChange to make sure everything is synced up at this point 
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const 
	{
		if (OutputParticleNeighborsGridParam.IsBound())
		{
			OutputParticleNeighborsGridParam.UnsetUAV(RHICmdList, Context.Shader.GetComputeShader());
		}

		if (OutputParticleNeighborCountGridParam.IsBound())
		{
			OutputParticleNeighborCountGridParam.UnsetUAV(RHICmdList, Context.Shader.GetComputeShader());
		}
	}

private:
	LAYOUT_FIELD(FShaderParameter, NumCellsParam);
	LAYOUT_FIELD(FShaderParameter, CellSizeParam);
	LAYOUT_FIELD(FShaderParameter, MaxNeighborsPerCellParam);
	LAYOUT_FIELD(FShaderParameter, WorldBBoxSizeParam);
	LAYOUT_FIELD(FShaderResourceParameter, ParticleNeighborsGridParam);
	LAYOUT_FIELD(FShaderResourceParameter, ParticleNeighborCountGridParam);
	LAYOUT_FIELD(FRWShaderParameter, OutputParticleNeighborCountGridParam);
	LAYOUT_FIELD(FRWShaderParameter, OutputParticleNeighborsGridParam);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_NeighborGrid3D);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceNeighborGrid3D, FNiagaraDataInterfaceParametersCS_NeighborGrid3D);

UNiagaraDataInterfaceNeighborGrid3D::UNiagaraDataInterfaceNeighborGrid3D(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, MaxNeighborsPerCell(8)
{
	SetResolutionMethod = ESetResolutionMethod::CellSize;

	Proxy.Reset(new FNiagaraDataInterfaceProxyNeighborGrid3D());	
}


void UNiagaraDataInterfaceNeighborGrid3D::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	Super::GetFunctions(OutFunctions);

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = MaxNeighborsPerCellFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("MaxNeighborsPerCell")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = NeighborGridIndexToLinearFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexZ")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Neighbor")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear Index")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetParticleNeighborFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NeighborIndex")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetParticleNeighborFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NeighborIndex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IGNORE")));

		Sig.bExperimental = true;
		Sig.bWriteFunction = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetParticleNeighborCountFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NeighborCount")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetParticleNeighborCountFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Linear")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Increment")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("PrevNeighborCount")));

		Sig.bExperimental = true;
		Sig.bWriteFunction = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceNeighborGrid3D, GetWorldBBoxSize);

void UNiagaraDataInterfaceNeighborGrid3D::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	Super::GetVMExternalFunction(BindingInfo, InstanceData, OutFunc);

	// #todo(dmp): this overrides the empty function set by the super class
	if (BindingInfo.Name == WorldBBoxSizeFunctionName) {
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceNeighborGrid3D, GetWorldBBoxSize)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == MaxNeighborsPerCellFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }	
	else if (BindingInfo.Name == NeighborGridIndexToLinearFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == GetParticleNeighborFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == SetParticleNeighborFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == GetParticleNeighborCountFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == SetParticleNeighborCountFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
}

void UNiagaraDataInterfaceNeighborGrid3D::GetWorldBBoxSize(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<NeighborGrid3DRWInstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutWorldBoundsX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldBoundsY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutWorldBoundsZ(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutWorldBoundsX.GetDestAndAdvance() = WorldBBoxSize.X;
		*OutWorldBoundsY.GetDestAndAdvance() = WorldBBoxSize.Y;
		*OutWorldBoundsZ.GetDestAndAdvance() = WorldBBoxSize.Z;
	}
}

bool UNiagaraDataInterfaceNeighborGrid3D::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceNeighborGrid3D* OtherTyped = CastChecked<const UNiagaraDataInterfaceNeighborGrid3D>(Other);

	return OtherTyped->MaxNeighborsPerCell == MaxNeighborsPerCell;
}

void UNiagaraDataInterfaceNeighborGrid3D::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);

	static const TCHAR *FormatDeclarations = TEXT(R"(
		int {MaxNeighborsPerCellName};
		Buffer<int> {ParticleNeighborsName};
		Buffer<int> {ParticleNeighborCountName};
		RWBuffer<int> RW{OutputParticleNeighborsName};
		RWBuffer<int> RW{OutputParticleNeighborCountName};
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations = {
		{ TEXT("MaxNeighborsPerCellName"),  MaxNeighborsPerCellName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("ParticleNeighborsName"),    ParticleNeighborsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("ParticleNeighborCountName"),    ParticleNeighborCountName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("OutputParticleNeighborsName"),    OutputParticleNeighborsName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("OutputParticleNeighborCountName"),    OutputParticleNeighborCountName + ParamInfo.DataInterfaceHLSLSymbol },
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceNeighborGrid3D::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	bool ParentRet = Super::GetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, OutHLSL);
	if (ParentRet)
	{
		return true;
	} else if (FunctionInfo.DefinitionName == MaxNeighborsPerCellFunctionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out int Out_MaxNeighborsPerCell)
			{
				Out_MaxNeighborsPerCell = {MaxNeighborsPerCellName};
			}
		)");
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("MaxNeighborsPerCellName"), MaxNeighborsPerCellName + ParamInfo.DataInterfaceHLSLSymbol},

		};
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NeighborGridIndexToLinearFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, int In_IndexZ, int In_Neighbor, out int Out_Linear)
			{
				Out_Linear = In_Neighbor + In_IndexX * {MaxNeighborsPerCellName} + In_IndexY * {MaxNeighborsPerCellName}*{NumCellsName}.x + In_IndexZ * {MaxNeighborsPerCellName}*{NumCellsName}.x*{NumCellsName}.y;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("MaxNeighborsPerCellName"), MaxNeighborsPerCellName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetParticleNeighborFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_Index, out int Out_ParticleNeighborIndex)
			{
				Out_ParticleNeighborIndex = {ParticleNeighbors}[In_Index];				
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("ParticleNeighbors"), ParticleNeighborsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetParticleNeighborFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_Index, int In_ParticleNeighborIndex, out int Out_Ignore)
			{
				RW{OutputParticleNeighbors}[In_Index] = In_ParticleNeighborIndex;				
				Out_Ignore = 0;
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("OutputParticleNeighbors"), OutputParticleNeighborsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetParticleNeighborCountFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_Index, out int Out_ParticleNeighborIndex)
			{
				Out_ParticleNeighborIndex = {ParticleNeighborCount}[In_Index];				
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("ParticleNeighborCount"), ParticleNeighborCountName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetParticleNeighborCountFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_Index, int In_Increment, out int PreviousNeighborCount)
			{				
				InterlockedAdd(RW{OutputParticleNeighborCount}[In_Index], In_Increment, PreviousNeighborCount);				
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("OutputParticleNeighborCount"), OutputParticleNeighborCountName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}

	return false;
}

bool UNiagaraDataInterfaceNeighborGrid3D::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{

	NeighborGrid3DRWInstanceData* InstanceData = new (PerInstanceData) NeighborGrid3DRWInstanceData();

	FNiagaraDataInterfaceProxyNeighborGrid3D* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyNeighborGrid3D>();

	FIntVector RT_NumCells = NumCells;
	uint32 RT_MaxNeighborsPerCell = MaxNeighborsPerCell;	
	FVector RT_WorldBBoxSize = WorldBBoxSize;
	TSet<int> RT_OutputShaderStages = OutputShaderStages;
	TSet<int> RT_IterationShaderStages = IterationShaderStages;

	float TmpCellSize = RT_WorldBBoxSize[0] / RT_NumCells[0];

	if (SetResolutionMethod == ESetResolutionMethod::MaxAxis)
	{
		TmpCellSize = FMath::Max(FMath::Max(WorldBBoxSize.X, WorldBBoxSize.Y), WorldBBoxSize.Z) / NumCellsMaxAxis;
	}
	else if (SetResolutionMethod == ESetResolutionMethod::CellSize)
	{
		TmpCellSize = CellSize;
	}

	// compute world bounds and padding based on cell size
	if (SetResolutionMethod == ESetResolutionMethod::MaxAxis || SetResolutionMethod == ESetResolutionMethod::CellSize)
	{
		RT_NumCells.X = WorldBBoxSize.X / TmpCellSize;
		RT_NumCells.Y = WorldBBoxSize.Y / TmpCellSize;
		RT_NumCells.Z = WorldBBoxSize.Z / TmpCellSize;

		// Pad grid by 1 cell if our computed bounding box is too small
		if (WorldBBoxSize.X > WorldBBoxSize.Y && WorldBBoxSize.X > WorldBBoxSize.Z)
		{
			if (!FMath::IsNearlyEqual(TmpCellSize * RT_NumCells.Y, WorldBBoxSize.Y))
			{
				RT_NumCells.Y++;
			}

			if (!FMath::IsNearlyEqual(TmpCellSize * RT_NumCells.Z, WorldBBoxSize.Z))
			{
				RT_NumCells.Z++;
			}
		}
		else if (WorldBBoxSize.Y > WorldBBoxSize.X && WorldBBoxSize.Y > WorldBBoxSize.Z)
		{
			if (!FMath::IsNearlyEqual(TmpCellSize * RT_NumCells.X, WorldBBoxSize.X))
			{
				RT_NumCells.X++;
			}

			if (!FMath::IsNearlyEqual(TmpCellSize * RT_NumCells.Z, WorldBBoxSize.Z))
			{
				RT_NumCells.Z++;
			}
		}
		else if (WorldBBoxSize.Z > WorldBBoxSize.X && WorldBBoxSize.Z > WorldBBoxSize.Y)
		{
			if (!FMath::IsNearlyEqual(TmpCellSize * RT_NumCells.X, WorldBBoxSize.X))
			{
				RT_NumCells.X++;
			}

			if (!FMath::IsNearlyEqual(TmpCellSize * RT_NumCells.Y, WorldBBoxSize.Y))
			{
				RT_NumCells.Y++;
			}
		}

		RT_WorldBBoxSize = FVector(RT_NumCells.X, RT_NumCells.Y, RT_NumCells.Z) * TmpCellSize;		 
	}
	
	InstanceData->CellSize = TmpCellSize;
	InstanceData->WorldBBoxSize = RT_WorldBBoxSize;
	InstanceData->MaxNeighborsPerCell = RT_MaxNeighborsPerCell;	
	InstanceData->NumCells = RT_NumCells;

	// @todo-threadsafety. This would be a race but I'm taking a ref here. Not ideal in the long term.
	// Push Updates to Proxy.
	ENQUEUE_RENDER_COMMAND(FUpdateData)(
		[RT_Proxy, RT_NumCells, RT_MaxNeighborsPerCell, RT_WorldBBoxSize, RT_OutputShaderStages, RT_IterationShaderStages, InstanceID = SystemInstance->GetId()](FRHICommandListImmediate& RHICmdList)
	{
		check(!RT_Proxy->SystemInstancesToProxyData.Contains(InstanceID));
		NeighborGrid3DRWInstanceData* TargetData = &RT_Proxy->SystemInstancesToProxyData.Add(InstanceID);

		TargetData->NumCells = RT_NumCells;
		TargetData->MaxNeighborsPerCell = RT_MaxNeighborsPerCell;		
		TargetData->WorldBBoxSize = RT_WorldBBoxSize;

		RT_Proxy->OutputSimulationStages_DEPRECATED = RT_OutputShaderStages;
		RT_Proxy->IterationSimulationStages_DEPRECATED = RT_IterationShaderStages;

		// #todo(dmp): element count is still defined on the proxy and not the instance data
		RT_Proxy->SetElementCount(RT_NumCells.X *  RT_NumCells.Y * RT_NumCells.Z);

		TargetData->NeighborhoodCountBuffer.Initialize(sizeof(int32), TargetData->NumCells.X*TargetData->NumCells.Y*TargetData->NumCells.Z, EPixelFormat::PF_R32_SINT, BUF_Static, TEXT("NiagaraNeighborGrid3D::NeighborCount"));
		TargetData->NeighborhoodBuffer.Initialize(sizeof(int32), TargetData->NumCells.X*TargetData->NumCells.Y*TargetData->NumCells.Z*TargetData->MaxNeighborsPerCell, EPixelFormat::PF_R32_SINT, BUF_Static, TEXT("NiagaraNeighborGrid3D::NeighborsGrid"));

	});

	return true;
}

void UNiagaraDataInterfaceNeighborGrid3D::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{		
	NeighborGrid3DRWInstanceData* InstanceData = static_cast<NeighborGrid3DRWInstanceData*>(PerInstanceData);
	InstanceData->~NeighborGrid3DRWInstanceData();

	FNiagaraDataInterfaceProxyNeighborGrid3D* ThisProxy = GetProxyAs<FNiagaraDataInterfaceProxyNeighborGrid3D>();
	if (!ThisProxy)
		return;

	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[ThisProxy, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
	{
		//check(ThisProxy->SystemInstancesToProxyData.Contains(InstanceID));
		ThisProxy->SystemInstancesToProxyData.Remove(InstanceID);
	}
	);
}

void FNiagaraDataInterfaceProxyNeighborGrid3D::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{
	if (Context.IsOutputStage)
	{
		NeighborGrid3DRWInstanceData* ProxyData = SystemInstancesToProxyData.Find(Context.SystemInstanceID);

		SCOPED_DRAW_EVENT(RHICmdList, NiagaraNeighborGrid3DClearNeighborInfo);
		ERHIFeatureLevel::Type FeatureLevel = Context.Batcher->GetFeatureLevel();

		FRHIUnorderedAccessView* TransitionBuffers[] = { ProxyData->NeighborhoodBuffer.UAV, ProxyData->NeighborhoodCountBuffer.UAV };
		RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, TransitionBuffers, UE_ARRAY_COUNT(TransitionBuffers));
		NiagaraFillGPUIntBuffer(RHICmdList, FeatureLevel, ProxyData->NeighborhoodBuffer, -1);
		NiagaraFillGPUIntBuffer(RHICmdList, FeatureLevel, ProxyData->NeighborhoodCountBuffer, 0);
	}
}

bool UNiagaraDataInterfaceNeighborGrid3D::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceNeighborGrid3D* OtherTyped = CastChecked<UNiagaraDataInterfaceNeighborGrid3D>(Destination);


	OtherTyped->MaxNeighborsPerCell = MaxNeighborsPerCell;


	return true;
}

#undef LOCTEXT_NAMESPACE