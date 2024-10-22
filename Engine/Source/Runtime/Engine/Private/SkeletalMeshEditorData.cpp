// Copyright Epic Games, Inc. All Rights Reserved.
#include "Engine/SkeletalMeshEditorData.h"

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "EngineDefines.h"
#include "Engine/EngineTypes.h"

#if WITH_EDITORONLY_DATA

#include "Rendering/SkeletalMeshLODImporterData.h"

#endif //WITH_EDITORONLY_DATA

#define LOCTEXT_NAMESPACE "SkeltalMeshEditorData"

DEFINE_LOG_CATEGORY_STATIC(LogSkeletalMeshEditorData, Log, All);

USkeletalMeshEditorData::USkeletalMeshEditorData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

#if WITH_EDITORONLY_DATA

void USkeletalMeshEditorData::PostLoad()
{
	ClearFlags(RF_Standalone);
	Super::PostLoad();
}

FRawSkeletalMeshBulkData& USkeletalMeshEditorData::GetLODImportedData(int32 LODIndex)
{

	//Create missing default item
	check(LODIndex >= 0);
	if (LODIndex >= RawSkeletalMeshBulkDatas.Num())
	{
		//Avoid changing the array outside of the main thread
		//The allocation must be done before going multi thread
		//TArray is not thread safe when allocating
		check(IsInGameThread());
		const int32 AddItemCount = 1 + (LODIndex - RawSkeletalMeshBulkDatas.Num());
		RawSkeletalMeshBulkDatas.Reserve(AddItemCount);
		while (LODIndex >= RawSkeletalMeshBulkDatas.Num())
		{
			RawSkeletalMeshBulkDatas.Add(MakeShared<FRawSkeletalMeshBulkData>());
		}
	}

	check(RawSkeletalMeshBulkDatas.IsValidIndex(LODIndex));
	//Return the Data
	return RawSkeletalMeshBulkDatas[LODIndex].Get();
}

bool USkeletalMeshEditorData::IsLODImportDataValid(int32 LODIndex)
{
	return RawSkeletalMeshBulkDatas.IsValidIndex(LODIndex);
}

void USkeletalMeshEditorData::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	//Serialize all LODs Raw imported source data
	FRawSkeletalMeshBulkData::Serialize(Ar, RawSkeletalMeshBulkDatas, this);
}

#endif //WITH_EDITORONLY_DATA

#undef LOCTEXT_NAMESPACE
