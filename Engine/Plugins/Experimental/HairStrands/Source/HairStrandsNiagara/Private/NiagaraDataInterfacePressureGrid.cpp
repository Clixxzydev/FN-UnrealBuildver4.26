// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfacePressureGrid.h"
#include "NiagaraShader.h"
#include "NiagaraComponent.h"
#include "NiagaraRenderer.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstanceBatcher.h"

#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfacePressureGrid"
DEFINE_LOG_CATEGORY_STATIC(LogPressureGrid, Log, All);

//------------------------------------------------------------------------------------------------------------

static const FName BuildDistanceFieldName(TEXT("BuildDistanceField"));
static const FName BuildDensityFieldName(TEXT("BuildDensityField"));
static const FName SolveGridPressureName(TEXT("SolveGridPressure"));
static const FName ScaleCellFieldsName(TEXT("ScaleCellFields"));
static const FName SetSolidBoundaryName(TEXT("SetSolidBoundary"));
static const FName ComputeBoundaryWeightsName(TEXT("ComputeBoundaryWeights"));
static const FName GetNodePositionName(TEXT("GetNodePosition"));
static const FName GetDensityFieldName(TEXT("GetDensityField"));
static const FName UpdateDeformationGradientName(TEXT("UpdateDeformationGradient"));

//------------------------------------------------------------------------------------------------------------

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfacePressureGrid, FNDIVelocityGridParametersCS);

//------------------------------------------------------------------------------------------------------------

UNiagaraDataInterfacePressureGrid::UNiagaraDataInterfacePressureGrid(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNDIPressureGridProxy());
	NumAttributes = 18;
}

void UNiagaraDataInterfacePressureGrid::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	Super::GetFunctions(OutFunctions);
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNodePositionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Grid Cell")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Origin")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Grid Length")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Position")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetDensityFieldName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Origin")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Grid Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particle Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particle Density")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = UpdateDeformationGradientName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Velocity Gradient")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Deformation Gradient")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Deformation Gradient")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Gradient Determinant")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = BuildDistanceFieldName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Origin")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Grid Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particle Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Function Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = BuildDensityFieldName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Origin")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Grid Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particle Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particle Mass")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Particle Density")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Function Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SolveGridPressureName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Grid Cell")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Init Stage")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Project Status")));
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetSolidBoundaryName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Grid Cell")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Cell Distance")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Cell Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Boundary Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeBoundaryWeightsName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Grid Cell")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Weights Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ScaleCellFieldsName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bWriteFunction = true;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Pressure Grid")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Grid Cell")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Grid Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Transfer Status")));

		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, BuildDistanceField);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, BuildDensityField);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, SolveGridPressure);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, SetSolidBoundary);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, ScaleCellFields);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, ComputeBoundaryWeights);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, GetNodePosition);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, GetDensityField);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, UpdateDeformationGradient);

void UNiagaraDataInterfacePressureGrid::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	Super::GetVMExternalFunction(BindingInfo, InstanceData, OutFunc);

	if (BindingInfo.Name == BuildDistanceFieldName)
	{
		check(BindingInfo.GetNumInputs() == 8 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, BuildDistanceField)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == BuildDensityFieldName)
	{
		check(BindingInfo.GetNumInputs() == 10 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, BuildDensityField)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == UpdateDeformationGradientName)
	{
		check(BindingInfo.GetNumInputs() == 34 && BindingInfo.GetNumOutputs() == 17);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, UpdateDeformationGradient)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetNodePositionName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, GetNodePosition)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetDensityFieldName)
	{
		check(BindingInfo.GetNumInputs() == 8 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, GetDensityField)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SolveGridPressureName)
	{
		check(BindingInfo.GetNumInputs() == 3 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, SolveGridPressure)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetSolidBoundaryName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, SetSolidBoundary)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeBoundaryWeightsName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, ComputeBoundaryWeights)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ScaleCellFieldsName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfacePressureGrid, ScaleCellFields)::Bind(this, OutFunc);
	}
}

void UNiagaraDataInterfacePressureGrid::BuildDistanceField(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfacePressureGrid::BuildDensityField(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfacePressureGrid::SolveGridPressure(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfacePressureGrid::ComputeBoundaryWeights(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfacePressureGrid::SetSolidBoundary(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfacePressureGrid::ScaleCellFields(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfacePressureGrid::GetNodePosition(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfacePressureGrid::GetDensityField(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}
void UNiagaraDataInterfacePressureGrid::UpdateDeformationGradient(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

bool UNiagaraDataInterfacePressureGrid::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	if (Super::GetFunctionHLSL(ParamInfo, FunctionInfo, FunctionInstanceIndex, OutHLSL))
	{
		return true;
	}
	else
	{
		TMap<FString, FStringFormatArg> ArgsSample = {
			{TEXT("InstanceFunctionName"), FunctionInfo.InstanceName},
			{TEXT("PressureGridContextName"), TEXT("DIVelocityGrid_MAKE_CONTEXT(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")")},
		};

		if (FunctionInfo.DefinitionName == BuildDistanceFieldName)
		{
			static const TCHAR* FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in float3 GridOrigin, in float GridLength, 
					in float3 ParticlePosition, out bool OutFunctionStatus )
					{
						{PressureGridContextName} DIVelocityGrid_BuildDistanceField(DIContext,GridOrigin,GridLength,ParticlePosition,OutFunctionStatus);
					}
					)");
			OutHLSL += FString::Format(FormatSample, ArgsSample);
			return true;
		}
		else if (FunctionInfo.DefinitionName == BuildDensityFieldName)
		{
			static const TCHAR* FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in float3 GridOrigin, in float GridLength, 
					in float3 ParticlePosition, in float ParticleMass, in float ParticleDensity, out bool OutFunctionStatus )
					{
						{PressureGridContextName} DIVelocityGrid_BuildDensityField(DIContext,GridOrigin,GridLength,ParticlePosition,ParticleMass,ParticleDensity,OutFunctionStatus);
					}
					)");
			OutHLSL += FString::Format(FormatSample, ArgsSample);
			return true;
		}
		else if (FunctionInfo.DefinitionName == UpdateDeformationGradientName)
		{
			static const TCHAR* FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in float DeltaTime, in float4x4 VelocityGradient, 
				in float4x4 DeformationGradient, out float4x4 OutDeformationGradient, out float OutGradientDeterminant)
					{
						{PressureGridContextName} DIVelocityGrid_UpdateDeformationGradient(DIContext,DeltaTime,VelocityGradient,DeformationGradient,OutDeformationGradient,OutGradientDeterminant);
					}
					)");
			OutHLSL += FString::Format(FormatSample, ArgsSample);
			return true;
		}
		else if (FunctionInfo.DefinitionName == SolveGridPressureName)
		{
			static const TCHAR* FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in int GridCell, in int InitStage, out bool OutProjectStatus)
					{
						{PressureGridContextName} DIVelocityGrid_SolveGridPressure(DIContext,GridCell,InitStage,OutProjectStatus);
					}
					)");
			OutHLSL += FString::Format(FormatSample, ArgsSample);
			return true;
		}
		else if (FunctionInfo.DefinitionName == GetNodePositionName)
		{
			static const TCHAR* FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in int GridCell, in float3 GridOrigin, in float GridLength, out float3 OutGridPosition)
					{
						{PressureGridContextName} DIVelocityGrid_GetNodePosition(DIContext,GridCell,GridOrigin,GridLength,OutGridPosition);
					}
					)");
			OutHLSL += FString::Format(FormatSample, ArgsSample);
			return true;
		}
		else if (FunctionInfo.DefinitionName == GetDensityFieldName)
		{
			static const TCHAR* FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in float3 GridOrigin, in float GridLength, in float3 ParticlePosition, out float OutParticleDensity)
					{
						{PressureGridContextName} DIVelocityGrid_GetDensityField(DIContext,GridOrigin,GridLength,ParticlePosition,OutParticleDensity);
					}
					)");
			OutHLSL += FString::Format(FormatSample, ArgsSample);
			return true;
		}
		else if (FunctionInfo.DefinitionName == SetSolidBoundaryName)
		{
			static const TCHAR* FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in int GridCell, in float SolidDistance, in float3 SolidVelocity, out bool OutBoundaryStatus)
					{
						{PressureGridContextName} DIVelocityGrid_SetSolidBoundary(DIContext,GridCell,SolidDistance,SolidVelocity,OutBoundaryStatus);
					}
					)");
			OutHLSL += FString::Format(FormatSample, ArgsSample);
			return true;
		}
		else if (FunctionInfo.DefinitionName == ComputeBoundaryWeightsName)
		{
			static const TCHAR* FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in int GridCell, out bool OutWeightsStatus)
					{
						{PressureGridContextName} DIVelocityGrid_ComputeBoundaryWeights(DIContext,GridCell,OutWeightsStatus);
					}
					)");
			OutHLSL += FString::Format(FormatSample, ArgsSample);
			return true;
		}
		else if (FunctionInfo.DefinitionName == ScaleCellFieldsName)
		{
			static const TCHAR* FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in int GridCell, in float GridLength, in float DeltaTime, out bool OutTransferStatus)
					{
						{PressureGridContextName} DIVelocityGrid_ScaleCellFields(DIContext,GridCell,GridLength,DeltaTime,OutTransferStatus);
					}
					)");
			OutHLSL += FString::Format(FormatSample, ArgsSample);
			return true;
		}
	}

	OutHLSL += TEXT("\n");
	return false;
}

void UNiagaraDataInterfacePressureGrid::GetCommonHLSL(FString& OutHLSL)
{
	Super::GetCommonHLSL(OutHLSL);
	OutHLSL += TEXT("#include \"/Plugin/Experimental/HairStrands/Private/NiagaraDataInterfacePressureGrid.ush\"\n");
}

void UNiagaraDataInterfacePressureGrid::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("DIVelocityGrid_DECLARE_CONSTANTS(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")\n");
}

//------------------------------------------------------------------------------------------------------------

#define NIAGARA_HAIR_STRANDS_THREAD_COUNT 64

class FClearPressureGridCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearPressureGridCS);
	SHADER_USE_PARAMETER_STRUCT(FClearPressureGridCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, GridSize)
		SHADER_PARAMETER(int32, CopyPressure)
		SHADER_PARAMETER_SRV(Texture3D, GridCurrentBuffer)
		SHADER_PARAMETER_UAV(RWTexture3D, GridDestinationBuffer)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), NIAGARA_HAIR_STRANDS_THREAD_COUNT);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearPressureGridCS, "/Plugin/Experimental/HairStrands/Private/NiagaraClearPressureGrid.usf", "MainCS", SF_Compute);

static void AddClearPressureGridPass(
	FRDGBuilder& GraphBuilder,
	FRHIShaderResourceView* GridCurrentBuffer,
	FRHIUnorderedAccessView* GridDestinationBuffer,
	const FIntVector& GridSize, const bool CopyPressure)
{
	const uint32 GroupSize = NIAGARA_HAIR_STRANDS_THREAD_COUNT;
	const uint32 NumElements = (GridSize.X + 1) * (GridSize.Y + 1) * (GridSize.Z + 1);

	FClearPressureGridCS::FParameters* Parameters = GraphBuilder.AllocParameters<FClearPressureGridCS::FParameters>();
	Parameters->GridCurrentBuffer = GridCurrentBuffer;
	Parameters->GridDestinationBuffer = GridDestinationBuffer;
	Parameters->GridSize = GridSize;
	Parameters->CopyPressure = CopyPressure;

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	const uint32 DispatchCount = FMath::DivideAndRoundUp(NumElements, GroupSize);

	TShaderMapRef<FClearPressureGridCS> ComputeShader(ShaderMap);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ClearPressureGrid"),
		ComputeShader,
		Parameters,
		FIntVector(DispatchCount, 1, 1));
}

inline void ClearBuffer(FRHICommandList& RHICmdList, FNDIVelocityGridBuffer* CurrentGridBuffer, FNDIVelocityGridBuffer* DestinationGridBuffer, const FIntVector& GridSize, const bool CopyPressure)
{
	FRHIUnorderedAccessView* DestinationGridBufferUAV = DestinationGridBuffer->GridDataBuffer.UAV;
	FRHIShaderResourceView* CurrentGridBufferSRV = CurrentGridBuffer->GridDataBuffer.SRV;
	FRHIUnorderedAccessView* CurrentGridBufferUAV = CurrentGridBuffer->GridDataBuffer.UAV;

	if (DestinationGridBufferUAV != nullptr && CurrentGridBufferSRV != nullptr && CurrentGridBufferUAV != nullptr)
	{
		const FIntVector LocalGridSize = GridSize;
		const bool LocalCopyPressure = CopyPressure;

		ENQUEUE_RENDER_COMMAND(ClearPressureGrid)(
			[DestinationGridBufferUAV, CurrentGridBufferSRV, CurrentGridBufferUAV, LocalGridSize, LocalCopyPressure]
		(FRHICommandListImmediate& RHICmdListImm)
		{
			RHICmdListImm.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, DestinationGridBufferUAV);
			RHICmdListImm.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, CurrentGridBufferUAV);

			FRDGBuilder GraphBuilder(RHICmdListImm);

			AddClearPressureGridPass(
				GraphBuilder,
				CurrentGridBufferSRV, DestinationGridBufferUAV, LocalGridSize, LocalCopyPressure);

			GraphBuilder.Execute();
		});
	}
}

//------------------------------------------------------------------------------------------------------------

void FNDIPressureGridProxy::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{
	FNDIVelocityGridData* ProxyData =
		FNDIVelocityGridProxy::SystemInstancesToProxyData.Find(Context.SystemInstanceID);

	if (ProxyData != nullptr)
	{
		if (Context.SimulationStageIndex == 0)
		{
			ClearBuffer(RHICmdList, ProxyData->CurrentGridBuffer, ProxyData->DestinationGridBuffer, ProxyData->GridSize, true);
		}
	}
}

#undef LOCTEXT_NAMESPACE