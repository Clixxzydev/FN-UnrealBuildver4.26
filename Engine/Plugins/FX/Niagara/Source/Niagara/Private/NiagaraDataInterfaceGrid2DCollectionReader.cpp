// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraDataInterfaceGrid2DCollectionReader.h"
#include "NiagaraShader.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraRenderer.h"
#include "Engine/TextureRenderTarget2D.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceGrid2DCollectionReader"

const FString UNiagaraDataInterfaceGrid2DCollectionReader::NumTilesName(TEXT("NumTiles_"));

const FString UNiagaraDataInterfaceGrid2DCollectionReader::GridName(TEXT("Grid_"));
const FString UNiagaraDataInterfaceGrid2DCollectionReader::SamplerName(TEXT("Sampler_"));


// Global VM function names, also used by the shaders code generation methods.
const FName UNiagaraDataInterfaceGrid2DCollectionReader::GetValueFunctionName("GetGridValue");

const FName UNiagaraDataInterfaceGrid2DCollectionReader::SampleGridFunctionName("SampleGrid");

//#todo(dmp): it would be nice if this class didn't have duplciated code.  It is acting as a proxy for the grid it is reading from.  Refactoring
// could be nice here
/*--------------------------------------------------------------------------------------------------------------------------*/
struct FNiagaraDataInterfaceParametersCS_Grid2DCollectionReader : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_Grid2DCollectionReader, NonVirtual);
public:
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{			
		NumCellsParam.Bind(ParameterMap, *(NumCellsName + ParameterInfo.DataInterfaceHLSLSymbol));
		NumTilesParam.Bind(ParameterMap, *(UNiagaraDataInterfaceGrid2DCollectionReader::NumTilesName + ParameterInfo.DataInterfaceHLSLSymbol));

		CellSizeParam.Bind(ParameterMap, *(CellSizeName + ParameterInfo.DataInterfaceHLSLSymbol));

		WorldBBoxSizeParam.Bind(ParameterMap, *(WorldBBoxSizeName + ParameterInfo.DataInterfaceHLSLSymbol));

		GridParam.Bind(ParameterMap, *(UNiagaraDataInterfaceGrid2DCollectionReader::GridName + ParameterInfo.DataInterfaceHLSLSymbol));		

		SamplerParam.Bind(ParameterMap, *(UNiagaraDataInterfaceGrid2DCollectionReader::SamplerName + ParameterInfo.DataInterfaceHLSLSymbol));
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread());

		// Get shader and DI
		FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
				
		// #todo(dmp): read this from instance data and correct proxy from the reader's proxy
		FNiagaraDataInterfaceProxyGrid2DCollectionReaderProxy* ReaderDIProxy = static_cast<FNiagaraDataInterfaceProxyGrid2DCollectionReaderProxy*>(Context.DataInterface);
		
		FGrid2DCollectionReaderInstanceData_RenderThread* ReaderProxyData = ReaderDIProxy->SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);

		FGrid2DCollectionRWInstanceData_RenderThread* Grid2DProxyData = nullptr;

		if (ReaderProxyData && ReaderProxyData->GPUContext && ReaderProxyData->ProxyToUse)
		{
			Grid2DProxyData = ReaderProxyData->ProxyToUse->SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
		}

		// no proxy data so fill with dummy values
		if (Grid2DProxyData == nullptr)
		{			
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumCellsParam, FIntPoint(0, 0));			
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumTilesParam, FIntPoint(0, 0));
			SetShaderValue(RHICmdList, ComputeShaderRHI, CellSizeParam, FVector2D(0.f, 0.f));
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldBBoxSizeParam, FVector2D(0.f, 0.f));

			FRHISamplerState* SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			SetSamplerParameter(RHICmdList, ComputeShaderRHI, SamplerParam, SamplerState);
			
			FRHIShaderResourceView* InputGridBuffer = FNiagaraRenderer::GetDummyTextureReadBuffer2D();				
			SetSRVParameter(RHICmdList, Context.Shader.GetComputeShader(), GridParam, InputGridBuffer);			

			return;
		}

		check(Grid2DProxyData);

		int NumCellsTmp[2];
		NumCellsTmp[0] = Grid2DProxyData->NumCells.X;
		NumCellsTmp[1] = Grid2DProxyData->NumCells.Y;
		SetShaderValue(RHICmdList, ComputeShaderRHI, NumCellsParam, NumCellsTmp);	

		int NumTilesTmp[2];
		NumTilesTmp[0] = Grid2DProxyData->NumTiles.X;
		NumTilesTmp[1] = Grid2DProxyData->NumTiles.Y;
		SetShaderValue(RHICmdList, ComputeShaderRHI, NumTilesParam, NumTilesTmp);		

		SetShaderValue(RHICmdList, ComputeShaderRHI, CellSizeParam, Grid2DProxyData->CellSize);
				
		SetShaderValue(RHICmdList, ComputeShaderRHI, WorldBBoxSizeParam, Grid2DProxyData->WorldBBoxSize);

		FRHISamplerState *SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		SetSamplerParameter(RHICmdList, ComputeShaderRHI, SamplerParam, SamplerState);

		if (GridParam.IsBound())
		{
			FRHIShaderResourceView* InputGridBuffer;
			if (Grid2DProxyData->CurrentData != nullptr)
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, Grid2DProxyData->CurrentData->GridBuffer.UAV);
				InputGridBuffer = Grid2DProxyData->CurrentData->GridBuffer.SRV;
			}
			else
			{
				InputGridBuffer = FNiagaraRenderer::GetDummyTextureReadBuffer2D();
			}
			SetSRVParameter(RHICmdList, Context.Shader.GetComputeShader(), GridParam, InputGridBuffer);
		}
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const 
	{

	}

private:

	LAYOUT_FIELD(FShaderParameter, NumCellsParam);
	LAYOUT_FIELD(FShaderParameter, NumTilesParam);
	LAYOUT_FIELD(FShaderParameter, CellSizeParam);
	LAYOUT_FIELD(FShaderParameter, WorldBBoxSizeParam);

	LAYOUT_FIELD(FShaderResourceParameter, GridParam);
	
	
	LAYOUT_FIELD(FShaderResourceParameter, SamplerParam);
};

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_Grid2DCollectionReader);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceGrid2DCollectionReader, FNiagaraDataInterfaceParametersCS_Grid2DCollectionReader);


UNiagaraDataInterfaceGrid2DCollectionReader::UNiagaraDataInterfaceGrid2DCollectionReader(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyGrid2DCollectionReaderProxy());

	FNiagaraTypeDefinition Def(UObject::StaticClass());	
}


void UNiagaraDataInterfaceGrid2DCollectionReader::PostInitProperties()
{
	Super::PostInitProperties();

	//Can we register data interfaces as regular types and fold them into the FNiagaraVariable framework for UI and function calls etc?
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), /*bCanBeParameter*/ true, /*bCanBePayload*/ false, /*bIsUserDefined*/ false);
	}
}

void UNiagaraDataInterfaceGrid2DCollectionReader::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	Super::GetFunctions(OutFunctions);

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetValueFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("IndexY")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("AttributeIndex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleGridFunctionName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("UnitX")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("UnitY")));		
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("AttributeIndex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));

		Sig.bExperimental = true;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		OutFunctions.Add(Sig);
	}

}

// #todo(dmp): expose more CPU functionality
// #todo(dmp): ideally these would be exposed on the parent class, but we can't bind functions of parent classes but need to work on the interface
// for sharing an instance data object with the super class
void UNiagaraDataInterfaceGrid2DCollectionReader::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	Super::GetVMExternalFunction(BindingInfo, InstanceData, OutFunc);

	
	if (BindingInfo.Name == WorldBBoxSizeFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == CellSizeFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == GetValueFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
	else if (BindingInfo.Name == SampleGridFunctionName) { OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceRWBase::EmptyVMFunction); }
}

bool UNiagaraDataInterfaceGrid2DCollectionReader::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceGrid2DCollectionReader* OtherTyped = CastChecked<const UNiagaraDataInterfaceGrid2DCollectionReader>(Other);

	return OtherTyped != nullptr && OtherTyped->EmitterName == EmitterName && OtherTyped->DIName == DIName;		
}

void UNiagaraDataInterfaceGrid2DCollectionReader::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	Super::GetParameterDefinitionHLSL(ParamInfo, OutHLSL);

	static const TCHAR *FormatDeclarations = TEXT(R"(				
		Texture2D<float> {GridName};
		int2 {NumTiles};
		SamplerState {SamplerName};
	
	)");
	TMap<FString, FStringFormatArg> ArgsDeclarations = {				
		{ TEXT("GridName"),    GridName + ParamInfo.DataInterfaceHLSLSymbol },
		{ TEXT("SamplerName"),    SamplerName + ParamInfo.DataInterfaceHLSLSymbol },		
		{ TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol },
	};
	OutHLSL += FString::Format(FormatDeclarations, ArgsDeclarations);
}

bool UNiagaraDataInterfaceGrid2DCollectionReader::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	bool ParentRet = Super::GetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, OutHLSL);
	if (ParentRet)
	{
		return true;
	} 
	else if (FunctionInfo.DefinitionName == GetValueFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(int In_IndexX, int In_IndexY, int In_AttributeIndex, out float Out_Val)
			{
				int TileIndexX = In_AttributeIndex % {NumTiles}.x;
				int TileIndexY = In_AttributeIndex / {NumTiles}.x;

				Out_Val = {Grid}.Load(int3(In_IndexX + TileIndexX * {NumCellsName}.x, In_IndexY + TileIndexY * {NumCellsName}.y, 0));
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("Grid"), GridName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SampleGridFunctionName)
	{
		static const TCHAR *FormatBounds = TEXT(R"(
			void {FunctionName}(float In_UnitX, float In_UnitY, int In_AttributeIndex, out float Out_Val)
			{
				int TileIndexX = In_AttributeIndex % {NumTiles}.x;
				int TileIndexY = In_AttributeIndex / {NumTiles}.x;
				float2 UV =
				{
					In_UnitX / {NumTiles}.x + 1.0*TileIndexX/{NumTiles}.x,
					In_UnitY / {NumTiles}.y + 1.0*TileIndexY/{NumTiles}.y
				};
				float2 TileMin =
				{
					(TileIndexX * {NumCellsName}.x + 0.5) / ({NumTiles}.x * {NumCellsName}.x),
					(TileIndexY * {NumCellsName}.y + 0.5) / ({NumTiles}.y * {NumCellsName}.y),
				};
				float2 TileMax =
				{
					((TileIndexX + 1) * {NumCellsName}.x - 0.5) / ({NumTiles}.x * {NumCellsName}.x),
					((TileIndexY + 1) * {NumCellsName}.y - 0.5) / ({NumTiles}.y * {NumCellsName}.y),
				};
				UV = clamp(UV, TileMin, TileMax);
				
				Out_Val = {Grid}.SampleLevel({SamplerName}, UV, 0);
			}
		)");
		TMap<FString, FStringFormatArg> ArgsBounds = {
			{TEXT("FunctionName"), FunctionInfo.InstanceName},
			{TEXT("Grid"), GridName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("SamplerName"),    SamplerName + ParamInfo.DataInterfaceHLSLSymbol },
			{TEXT("NumTiles"),    NumTilesName + ParamInfo.DataInterfaceHLSLSymbol},
			{TEXT("NumCellsName"), NumCellsName + ParamInfo.DataInterfaceHLSLSymbol},
		};
		OutHLSL += FString::Format(FormatBounds, ArgsBounds);
		return true;
	}
	return false;
}

bool UNiagaraDataInterfaceGrid2DCollectionReader::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceGrid2DCollectionReader* OtherTyped = CastChecked<UNiagaraDataInterfaceGrid2DCollectionReader>(Destination);
	OtherTyped->EmitterName = EmitterName;
	OtherTyped->DIName = DIName;
	return true;
}

bool UNiagaraDataInterfaceGrid2DCollectionReader::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	check(Proxy);

	FGrid2DCollectionReaderInstanceData_GameThread* InstanceData = new (PerInstanceData) FGrid2DCollectionReaderInstanceData_GameThread();
	SystemInstancesToProxyData_GT.Emplace(SystemInstance->GetId(), InstanceData);

	TSharedPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> RT_EmitterInstance;

	InstanceData->EmitterName = EmitterName;
	InstanceData->DIName = DIName;
	for (TSharedPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> EmitterInstance : SystemInstance->GetEmitters())
	{
		if (EmitterName == EmitterInstance->GetCachedEmitter()->GetUniqueEmitterName())
		{
			InstanceData->EmitterInstance = EmitterInstance.Get();			
			break;
		}
	}

	// Push Updates to Proxy.
	FNiagaraDataInterfaceProxyGrid2DCollectionReaderProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollectionReaderProxy>();
	ENQUEUE_RENDER_COMMAND(FUpdateData)(
		[RT_Proxy, InstanceID = SystemInstance->GetId(), RT_InstanceData=*InstanceData](FRHICommandListImmediate& RHICmdList)
	{
		check(!RT_Proxy->SystemInstancesToProxyData_RT.Contains(InstanceID));
		FGrid2DCollectionReaderInstanceData_RenderThread* TargetData = &RT_Proxy->SystemInstancesToProxyData_RT.Add(InstanceID);			
		TargetData->ProxyToUse = nullptr;

		if (RT_InstanceData.EmitterInstance != nullptr)
		{
			TargetData->GPUContext = RT_InstanceData.EmitterInstance->GetGPUContext();
			if (TargetData->GPUContext != nullptr)
			{
				const TArray<FNiagaraScriptDataInterfaceCompileInfo>& DataInterfaceInfo = TargetData->GPUContext->GPUScript->GetVMExecutableData().DataInterfaceInfo;
				const TArray<UNiagaraDataInterface*>& DataInterfaces = TargetData->GPUContext->CombinedParamStore.GetDataInterfaces();

				FString FullName = FString("Emitter.") + RT_InstanceData.DIName;
				int Index = 0;

				// #todo(dmp): we are looking at the UObjects that define the DIs here 
				for (UNiagaraDataInterface* Interface : DataInterfaces)
				{
					if (DataInterfaceInfo[Index].Name.GetPlainNameString() == FullName)
					{
						TargetData->ProxyToUse = static_cast<FNiagaraDataInterfaceProxyGrid2DCollectionProxy*>(Interface->GetProxy());
					}
					++Index;
				}
			}
		}
	});
	
	return true;
}


void UNiagaraDataInterfaceGrid2DCollectionReader::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	SystemInstancesToProxyData_GT.Remove(SystemInstance->GetId());

	FGrid2DCollectionReaderInstanceData_GameThread* InstanceData = static_cast<FGrid2DCollectionReaderInstanceData_GameThread*>(PerInstanceData);

	InstanceData->~FGrid2DCollectionReaderInstanceData_GameThread();

	FNiagaraDataInterfaceProxyGrid2DCollectionReaderProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid2DCollectionReaderProxy>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[RT_Proxy, InstanceID=SystemInstance->GetId(), Batcher=SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
		{
			//check(ThisProxy->SystemInstancesToProxyData.Contains(InstanceID));
			RT_Proxy->SystemInstancesToProxyData_RT.Remove(InstanceID);
		}
	);
}

void UNiagaraDataInterfaceGrid2DCollectionReader::GetEmitterDependencies(UNiagaraSystem* Asset, TArray<UNiagaraEmitter*>& Dependencies) const
{
	if (!Asset)
	{
		return;
	}

	UNiagaraEmitter* FoundSourceEmitter = nullptr;
	for (const FNiagaraEmitterHandle& EmitterHandle : Asset->GetEmitterHandles())
	{
		UNiagaraEmitter* EmitterInstance = EmitterHandle.GetInstance();
		if (EmitterInstance && EmitterInstance->GetUniqueEmitterName() == EmitterName)
		{
			Dependencies.Add(EmitterInstance);
			return;
		}
	}
}

#undef LOCTEXT_NAMESPACE
