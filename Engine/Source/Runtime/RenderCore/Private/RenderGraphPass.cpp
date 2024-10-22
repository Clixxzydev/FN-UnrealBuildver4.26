// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphPass.h"

void FRDGPassParameterStruct::ClearUniformBuffers() const
{
	/** The pass parameter struct is mostly POD, with the exception of uniform buffers.
	 *  Since the explicit type of the struct is unknown, the method traverses and destructs
	 *  all uniform buffer references manually.
	 */
	const auto& Resources = Layout->Resources;

	for (int32 ResourceIndex = 0; ResourceIndex < Resources.Num(); ++ResourceIndex)
	{
		const EUniformBufferBaseType MemberType = Resources[ResourceIndex].MemberType;

		if (MemberType == UBMT_REFERENCED_STRUCT)
		{
			const uint16 MemberOffset = Resources[ResourceIndex].MemberOffset;

			FUniformBufferRHIRef* UniformBufferPtr = reinterpret_cast<FUniformBufferRHIRef*>(Contents + MemberOffset);
			*UniformBufferPtr = FUniformBufferRHIRef();
		}
	}
}

FUniformBufferStaticBindings FRDGPassParameterStruct::GetGlobalUniformBuffers() const
{
	FUniformBufferStaticBindings GlobalUniformBuffers;

	const auto& Resources = Layout->Resources;

	for (int32 ResourceIndex = 0; ResourceIndex < Resources.Num(); ++ResourceIndex)
	{
		const EUniformBufferBaseType MemberType = Resources[ResourceIndex].MemberType;

		if (MemberType == UBMT_REFERENCED_STRUCT)
		{
			const uint16 MemberOffset = Resources[ResourceIndex].MemberOffset;

			FRHIUniformBuffer* UniformBufferPtr = *reinterpret_cast<FUniformBufferRHIRef*>(Contents + MemberOffset);

			if (UniformBufferPtr && UniformBufferPtr->HasStaticSlot())
			{
				GlobalUniformBuffers.AddUniformBuffer(UniformBufferPtr);
			}
		}
	}

	return GlobalUniformBuffers;
}

FRDGPass::FRDGPass(
	FRDGEventName&& InName,
	FRDGPassParameterStruct InParameterStruct,
	ERDGPassFlags InPassFlags)
	: Name(static_cast<FRDGEventName&&>(InName))
	, ParameterStruct(InParameterStruct)
	, PassFlags(InPassFlags)
{}

void FRDGPass::Execute(FRHICommandListImmediate& RHICmdList) const
{
	SCOPED_NAMED_EVENT(FRDGPass_Execute, FColor::Emerald);

	const FUniformBufferStaticBindings GlobalUniformBuffers = ParameterStruct.GetGlobalUniformBuffers();

	if (GlobalUniformBuffers.GetUniformBufferCount())
	{
		RHICmdList.SetGlobalUniformBuffers(GlobalUniformBuffers);
	}

	ExecuteImpl(RHICmdList);

	if (GlobalUniformBuffers.GetUniformBufferCount())
	{
		RHICmdList.SetGlobalUniformBuffers({});
	}

	ParameterStruct.ClearUniformBuffers();
}