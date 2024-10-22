// Copyright Epic Games, Inc. All Rights Reserved.

#include "FoliageEditUtility.h"
#include "FoliageType.h"
#include "InstancedFoliage.h"
#include "LevelUtils.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "InstancedFoliageActor.h"
#include "ScopedTransaction.h"
#include "Dialogs/DlgPickAssetPath.h"
#include "AssetRegistryModule.h"
#include "FoliageEdMode.h"
#include "FileHelpers.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "FoliageEdMode"

UFoliageType* FFoliageEditUtility::SaveFoliageTypeObject(UFoliageType* InFoliageType)
{
	UFoliageType* TypeToSave = nullptr;

	if (!InFoliageType->IsAsset())
	{
		FString PackageName;
		UObject* FoliageSource = InFoliageType->GetSource();
		if (FoliageSource)
		{
			// Build default settings asset name and path
			PackageName = FPackageName::GetLongPackagePath(FoliageSource->GetOutermost()->GetName()) + TEXT("/") + FoliageSource->GetName() + TEXT("_FoliageType");
		}

		TSharedRef<SDlgPickAssetPath> SaveFoliageTypeDialog =
			SNew(SDlgPickAssetPath)
			.Title(LOCTEXT("SaveFoliageTypeDialogTitle", "Choose Location for Foliage Type Asset"))
			.DefaultAssetPath(FText::FromString(PackageName));

		if (SaveFoliageTypeDialog->ShowModal() != EAppReturnType::Cancel)
		{
			PackageName = SaveFoliageTypeDialog->GetFullAssetPath().ToString();
			UPackage* Package = CreatePackage(nullptr, *PackageName);

			// We should not save a copy of this duplicate into the transaction buffer as it's an asset
			InFoliageType->ClearFlags(RF_Transactional);
			TypeToSave = Cast<UFoliageType>(StaticDuplicateObject(InFoliageType, Package, *FPackageName::GetLongPackageAssetName(PackageName)));
			InFoliageType->SetFlags(RF_Transactional);

			TypeToSave->SetFlags(RF_Standalone | RF_Public | RF_Transactional);
			TypeToSave->Modify();

			// Notify the asset registry
			FAssetRegistryModule::AssetCreated(TypeToSave);
		}
	}
	else
	{
		TypeToSave = InFoliageType;
	}

	// Save to disk
	if (TypeToSave)
	{
		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Add(TypeToSave->GetOutermost());
		const bool bCheckDirty = false;
		const bool bPromptToSave = false;
		FEditorFileUtils::EPromptReturnCode ReturnValue = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptToSave);

		if (ReturnValue != FEditorFileUtils::PR_Success)
		{
			TypeToSave = nullptr;
		}
	}

	return TypeToSave;
}

void FFoliageEditUtility::ReplaceFoliageTypeObject(UWorld* InWorld, UFoliageType* OldType, UFoliageType* NewType)
{
	FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "FoliageMode_ReplaceSettingsObject", "Foliage Editing: Replace Settings Object"));

	// Collect set of all available foliage types
	ULevel* CurrentLevel = InWorld->GetCurrentLevel();
	const int32 NumLevels = InWorld->GetNumLevels();

	for (int32 LevelIdx = 0; LevelIdx < NumLevels; ++LevelIdx)
	{
		ULevel* Level = InWorld->GetLevel(LevelIdx);
		if (Level && Level->bIsVisible)
		{
			AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForLevel(Level);
			if (IFA)
			{
				IFA->Modify();
				TUniqueObj<FFoliageInfo> OldInfo;
				IFA->FoliageInfos.RemoveAndCopyValue(OldType, OldInfo);

				// Old component needs to go
				if (OldInfo->IsInitialized())
				{
					OldInfo->Uninitialize();
				}
				
				// Append instances if new foliage type is already exists in this actor
				// Otherwise just replace key entry for instances
				TUniqueObj<FFoliageInfo>* NewInfo = IFA->FoliageInfos.Find(NewType);
				if (NewInfo)
				{
					(*NewInfo)->Instances.Append(OldInfo->Instances);
					(*NewInfo)->ReallocateClusters(IFA, NewType);
				}
				else
				{
					// Make sure if type changes we have proper implementation
					TUniqueObj<FFoliageInfo>& NewFoliageInfo = IFA->FoliageInfos.Add(NewType, MoveTemp(OldInfo));
					NewFoliageInfo->ReallocateClusters(IFA, NewType);
				}
			}
		}
	}
}


void FFoliageEditUtility::MoveActorFoliageInstancesToLevel(ULevel* InTargetLevel, AActor* InIFA)
{
	// Can't move into a locked level
	if (FLevelUtils::IsLevelLocked(InTargetLevel))
	{
		FNotificationInfo NotificatioInfo(NSLOCTEXT("UnrealEd", "CannotMoveFoliageIntoLockedLevel", "Cannot move the selected foliage into a locked level"));
		NotificatioInfo.bUseThrobber = false;
		FSlateNotificationManager::Get().AddNotification(NotificatioInfo)->SetCompletionState(SNotificationItem::CS_Fail);
		return;
	}

	// Get a world context
	UWorld* World = InTargetLevel->OwningWorld;
	bool PromptToMoveFoliageTypeToAsset = World->GetStreamingLevels().Num() > 0;
	bool ShouldPopulateMeshList = false;

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "MoveSelectedFoliageToSelectedLevel", "Move Selected Foliage to Level"), !GEditor->IsTransactionActive());

	// Iterate over all foliage actors in the world and move selected instances to a foliage actor in the target level
	const int32 NumLevels = World->GetNumLevels();
	for (int32 LevelIdx = 0; LevelIdx < NumLevels; ++LevelIdx)
	{
		ULevel* Level = World->GetLevel(LevelIdx);
		if (Level != InTargetLevel)
		{
			AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForLevel(Level, /*bCreateIfNone*/ false);
			
			if (IFA == nullptr)
			{
				continue;
			}
			if (InIFA && IFA != InIFA)
			{
				continue;
			}

			bool CanMoveInstanceType = true;

			TMap<UFoliageType*, FFoliageInfo*> InstancesFoliageType = IFA->GetAllInstancesFoliageType();

			for (auto& MeshPair : InstancesFoliageType)
			{
				if (MeshPair.Key != nullptr && MeshPair.Value != nullptr && !MeshPair.Key->IsAsset())
				{
					// Keep previous selection
					TSet<int32> PreviousSelectionSet = MeshPair.Value->SelectedIndices;
					TArray<int32> PreviousSelectionArray;
					PreviousSelectionArray.Reserve(PreviousSelectionSet.Num());

					for (int32& Value : PreviousSelectionSet)
					{
						PreviousSelectionArray.Add(Value);
					}

					UFoliageType* NewFoliageType = SaveFoliageTypeObject(MeshPair.Key);

					if (NewFoliageType != nullptr && NewFoliageType != MeshPair.Key)
					{
						ReplaceFoliageTypeObject(World, MeshPair.Key, NewFoliageType);
					}

					CanMoveInstanceType = NewFoliageType != nullptr;

					if (NewFoliageType != nullptr)
					{
						// Restore previous selection for move operation
						FFoliageInfo* MeshInfo = IFA->FindInfo(NewFoliageType);
						MeshInfo->SelectInstances(IFA, true, PreviousSelectionArray);
					}
				}
			}

			// Update our actor if we saved some foliage type as asset
			if (CanMoveInstanceType)
			{
				IFA = AInstancedFoliageActor::GetInstancedFoliageActorForLevel(Level, /*bCreateIfNone*/ false);
				ensure(IFA != nullptr);

				IFA->MoveAllInstancesToLevel(InTargetLevel);
			}

			if (InIFA)
			{
				return;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
