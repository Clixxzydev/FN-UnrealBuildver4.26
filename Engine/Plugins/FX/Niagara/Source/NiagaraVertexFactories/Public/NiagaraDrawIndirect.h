// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraDrawIndirect.h: Niagara shader to generate the draw indirect args for Niagara renderers.
==============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderPermutation.h"
#include "ShaderParameterUtils.h"

#define NIAGARA_DRAW_INDIRECT_ARGS_GEN_THREAD_COUNT 64
#define NIAGARA_DRAW_INDIRECT_ARGS_SIZE 5
#define NIAGARA_DRAW_INDIRECT_TASK_INFO_SIZE 4

// #define NIAGARA_COPY_BUFFER_THREAD_COUNT 64
// #define NIAGARA_COPY_BUFFER_BUFFER_COUNT 3

/**
* Task info when generating draw indirect frame buffer.
* Task is either about generate Niagara renderers drawindirect buffer,
* or about resetting released instance counters.
*/
struct FNiagaraDrawIndirectArgGenTaskInfo
{
	FNiagaraDrawIndirectArgGenTaskInfo(uint32 InInstanceCountBufferOffset, uint32 InNumIndicesPerInstance, uint32 InStartIndexLocation, bool bInUseCulledCounts)
		: InstanceCountBufferOffset(InInstanceCountBufferOffset)
		, NumIndicesPerInstance(InNumIndicesPerInstance)
		, StartIndexLocation(InStartIndexLocation)
		, bUseCulledCounts(bInUseCulledCounts ? 1 : 0)
	{}

	uint32 InstanceCountBufferOffset;
	uint32 NumIndicesPerInstance; // When -1 the counter needs to be reset to 0.
	uint32 StartIndexLocation;
	uint32 bUseCulledCounts;

	bool operator==(const FNiagaraDrawIndirectArgGenTaskInfo& Rhs) const
	{
		return InstanceCountBufferOffset == Rhs.InstanceCountBufferOffset
			&& NumIndicesPerInstance == Rhs.NumIndicesPerInstance
			&& StartIndexLocation == Rhs.StartIndexLocation
			&& bUseCulledCounts == Rhs.bUseCulledCounts;
	}
};

/**
 * Compute shader used to generate GPU emitter draw indirect args.
 * It also resets unused instance count entries.
 */
class NIAGARAVERTEXFACTORIES_API FNiagaraDrawIndirectArgsGenCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraDrawIndirectArgsGenCS);

public:
	class FSupportsTextureRW : SHADER_PERMUTATION_INT("SUPPORTS_TEXTURE_RW", 2);
	using FPermutationDomain = TShaderPermutationDomain<FSupportsTextureRW>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	FNiagaraDrawIndirectArgsGenCS() {}
	FNiagaraDrawIndirectArgsGenCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer);

	//bool Serialize(FArchive& Ar);
	void SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* DrawIndirectArgsUAV, FRHIUnorderedAccessView* InstanceCountsUAV);
	void SetParameters(FRHICommandList& RHICmdList, FRHIShaderResourceView* TaskInfosBuffer, FRHIShaderResourceView* CulledInstanceCountsBuffer, int32 NumArgGenTasks, int32 NumInstanceCountClearTasks);
	void UnbindBuffers(FRHICommandList& RHICmdList);

protected:
	
	LAYOUT_FIELD(FShaderResourceParameter, TaskInfosParam)
	LAYOUT_FIELD(FShaderResourceParameter, CulledInstanceCountsParam);
	LAYOUT_FIELD(FRWShaderParameter, InstanceCountsParam)
	LAYOUT_FIELD(FRWShaderParameter, DrawIndirectArgsParam)
	LAYOUT_FIELD(FShaderParameter, TaskCountParam)
};

/**
 * Compute shader used to reset unused instance count entries. Used if the platform doesn't support RW texture buffers
 */
class NIAGARAVERTEXFACTORIES_API FNiagaraDrawIndirectResetCountsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraDrawIndirectResetCountsCS);

public:
	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	FNiagaraDrawIndirectResetCountsCS() {}
	FNiagaraDrawIndirectResetCountsCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer);

	//bool Serialize(FArchive& Ar);
	void SetOutput(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* InstanceCountsUAV);
	void SetParameters(FRHICommandList& RHICmdList, FRHIShaderResourceView* TaskInfosBuffer, int32 NumArgGenTasks, int32 NumInstanceCountClearTasks);
	void UnbindBuffers(FRHICommandList& RHICmdList);

protected:

	LAYOUT_FIELD(FShaderResourceParameter, TaskInfosParam)
	LAYOUT_FIELD(FRWShaderParameter, InstanceCountsParam)
	LAYOUT_FIELD(FShaderParameter, TaskCountParam)
};