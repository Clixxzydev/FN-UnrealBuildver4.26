// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModelingModeAssetAPI.h"
#include "ModelingToolsEditorModeSettings.h"
#include "MeshDescription.h"
#include "Engine/Classes/Engine/Texture2D.h"
#include "Misc/Guid.h"

#include "StaticMeshComponentBuilder.h"



#define LOCTEXT_NAMESPACE "FModelingModeAssetAPI"






bool FModelingModeAssetAPI::GetNewActorPackagePath(
	UWorld* TargetWorld,
	FString ObjectBaseName,
	const FGeneratedStaticMeshAssetConfig& AssetConfig,
	FString& PackageFolderPathOut,
	FString& ObjectBaseNameOut )
{
	check(TargetWorld);
	const UModelingToolsEditorModeSettings* Settings = GetDefault<UModelingToolsEditorModeSettings>();
	EModelingModeAssetGenerationBehavior AutoGenMode = Settings->AssetGenerationMode;

	// figure out the appropriate root path to use
	FString PackageFolderPath = TEXT("/Game/");
	if (Settings->AssetGenerationLocation == EModelingModeAssetGenerationLocation::AutoGeneratedWorldRelativeAssetPath)
	{
		PackageFolderPath = GetWorldRelativeAssetRootPath(TargetWorld);
	}

	// combine with fixed AutoGen path name if it is not empty
	if (Settings->AutoGeneratedAssetPath.Len() > 0)
	{
		PackageFolderPath = FPaths::Combine(PackageFolderPath, Settings->AutoGeneratedAssetPath);
	}

	// append username-specific subfolder
	if (Settings->bUsePerUserAutogenSubfolder)
	{
		FString UsernameString(FPlatformProcess::UserName());
		if (UsernameString.Len() > 1)
		{
			PackageFolderPath = FPaths::Combine(PackageFolderPath, UsernameString);
		}
	}

	// if we want to use the currently-visible asset browser path, try to find one (this can fail if no asset browser is visible/etc)
	if (Settings->AssetGenerationLocation == EModelingModeAssetGenerationLocation::CurrentAssetBrowserPathIfAvailable)
	{
		FString CurrentAssetPath = GetActiveAssetFolderPath();
		if (CurrentAssetPath.IsEmpty() == false)
		{
			PackageFolderPath = CurrentAssetPath;
		}
	}

	// If we are in interactive mode, show the modal dialog and then get the path/name.
	// If the user cancels, we are going to discard the asset
	if (AutoGenMode == EModelingModeAssetGenerationBehavior::InteractivePromptToSave)
	{
		FString SelectedPath = InteractiveSelectAssetPath(ObjectBaseName, LOCTEXT("GenerateStaticMeshActorPathDialogWarning", "Choose Folder Path and Name for New Asset. Cancel to Discard New Asset."));
		if (SelectedPath.IsEmpty() == false)
		{
			PackageFolderPath = FPaths::GetPath(SelectedPath);
			ObjectBaseName = FPaths::GetBaseFilename(SelectedPath, true);
		}
		else
		{
			return false;
		}
	}

	FString UseBaseName = ObjectBaseName;
	if (Settings->bAppendRandomStringToName)
	{
		FGuid Guid = FGuid::NewGuid();
		FString GuidString = Guid.ToString(EGuidFormats::Short).ToUpper().Left(8);
		UseBaseName = FString::Printf(TEXT("%s_%s"), *UseBaseName, *GuidString);
	}

	PackageFolderPathOut = PackageFolderPath;
	ObjectBaseNameOut = UseBaseName;
	return true;
}





AActor* FModelingModeAssetAPI::GenerateStaticMeshActor(
	UWorld* TargetWorld,
	FTransform Transform,
	FString ObjectBaseName,
	FGeneratedStaticMeshAssetConfig&& AssetConfig)
{
	check(TargetWorld);
	const UModelingToolsEditorModeSettings* Settings = GetDefault<UModelingToolsEditorModeSettings>();
	EModelingModeAssetGenerationBehavior AutoGenMode = Settings->AssetGenerationMode;

	FString UsePackageFolderPath;
	FString UseBaseName;
	bool bContinue = GetNewActorPackagePath(TargetWorld, ObjectBaseName, AssetConfig, UsePackageFolderPath, UseBaseName);
	if (!bContinue)
	{
		return nullptr;
	}

	// create new package with unique local name
	FString UniqueAssetName;
	UPackage* AssetPackage = MakeNewAssetPackage(UsePackageFolderPath, UseBaseName, UniqueAssetName);

	// create new actor
	FRotator Rotation(0.0f, 0.0f, 0.0f);
	FActorSpawnParameters SpawnInfo;
	// @todo nothing here is specific to AStaticMeshActor...could we pass in a CDO and clone it instead of using SpawnActor?
	AStaticMeshActor* NewActor = TargetWorld->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, Rotation, SpawnInfo);
	NewActor->SetActorLabel(*UniqueAssetName);

	// construct new static mesh
	FStaticMeshComponentBuilder Builder;
	Builder.Initialize(AssetPackage, FName(*UniqueAssetName), AssetConfig.Materials.Num());

	if (AssetConfig.MeshDescription.IsValid())
	{
		*Builder.MeshDescription = *AssetConfig.MeshDescription;
	}
	else
	{
		// should generate default sphere here or something...
	}

	// create new mesh component and set as root of NewActor.
	Builder.CreateAndSetAsRootComponent(NewActor);

	// configure transform and materials of new component
	Builder.NewMeshComponent->SetWorldTransform((FTransform)Transform);
	for (int MatIdx = 0, NumMats = AssetConfig.Materials.Num(); MatIdx < NumMats; MatIdx++)
	{
		Builder.NewMeshComponent->SetMaterial(MatIdx, AssetConfig.Materials[MatIdx]);
	}

	// save the new asset (or don't, if that's what the user wants)
	if (AutoGenMode == EModelingModeAssetGenerationBehavior::AutoGenerateAndAutosave )
	{ 
		AutoSaveGeneratedAsset(Builder.NewStaticMesh, AssetPackage);
	}
	else if (AutoGenMode == EModelingModeAssetGenerationBehavior::InteractivePromptToSave)
	{
		AutoSaveGeneratedAsset(Builder.NewStaticMesh, AssetPackage);
		// this spawns a dialog that just allows save or not-save, but not renaming/etc, seems kind of useless...
		//InteractiveSaveGeneratedAsset(Builder.NewStaticMesh, AssetPackage);
	}
	else if (AutoGenMode == EModelingModeAssetGenerationBehavior::AutoGenerateButDoNotAutosave)
	{
		NotifyGeneratedAssetModified(Builder.NewStaticMesh, AssetPackage);
	}

	return NewActor;
}










bool FModelingModeAssetAPI::SaveGeneratedTexture2D(
	UTexture2D* GeneratedTexture,
	FString ObjectBaseName,
	const UObject* RelativeToAsset)
{
	check(RelativeToAsset);
	check(GeneratedTexture);
	check(GeneratedTexture->GetOuter() == GetTransientPackage());
	check(GeneratedTexture->Source.IsValid());	// texture needs to have valid source data to be savd
	const UModelingToolsEditorModeSettings* Settings = GetDefault<UModelingToolsEditorModeSettings>();
	EModelingModeAssetGenerationBehavior AutoGenMode = Settings->AssetGenerationMode;

	// find path to asset
	UPackage* AssetOuterPackage = CastChecked<UPackage>(RelativeToAsset->GetOuter());
	FString AssetPackageName = AssetOuterPackage->GetName();
	FString AssetPackageFolder = FPackageName::GetLongPackagePath(AssetPackageName);

	FString PackageFolderPath = AssetPackageFolder;

	// If we are in interactive mode, show the modal dialog and then get the path/name.
	// If the user cancels, we are going to discard the asset
	if (AutoGenMode == EModelingModeAssetGenerationBehavior::InteractivePromptToSave)
	{
		FString SelectedPath = InteractiveSelectAssetPath(ObjectBaseName, LOCTEXT("GenerateTexture2DAssetPathDialogWarning", "Choose Folder Path and Name for New Asset. Cancel to Discard New Asset."));
		if (SelectedPath.IsEmpty() == false)
		{
			PackageFolderPath = FPaths::GetPath(SelectedPath);
			ObjectBaseName = FPaths::GetBaseFilename(SelectedPath, true);
		}
		else
		{
			return false;
		}
	}

	// mangle the name
	FString UseBaseName = ObjectBaseName;
	if (Settings->bAppendRandomStringToName)
	{
		FGuid Guid = FGuid::NewGuid();
		FString GuidString = Guid.ToString(EGuidFormats::Short).ToUpper().Left(8);
		UseBaseName = FString::Printf(TEXT("%s_%s"), *UseBaseName, *GuidString);
	}

	// create new package
	FString UniqueAssetName;
	UPackage* AssetPackage = MakeNewAssetPackage(PackageFolderPath, UseBaseName, UniqueAssetName);

	// move texture from Transient package to real package
	GeneratedTexture->Rename( *UniqueAssetName, AssetPackage, REN_None);
	// remove transient flag, add public/standalone/transactional
	GeneratedTexture->ClearFlags(RF_Transient);
	GeneratedTexture->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
	// do we need to Modify() it? we are not doing any undo/redo
	GeneratedTexture->Modify();
	GeneratedTexture->UpdateResource();
	GeneratedTexture->PostEditChange();		// this may be necessary if any Materials are using this texture
	GeneratedTexture->MarkPackageDirty();


	// save the new asset (or don't, if that's what the user wants)
	if (AutoGenMode == EModelingModeAssetGenerationBehavior::AutoGenerateAndAutosave)
	{
		AutoSaveGeneratedAsset(GeneratedTexture, AssetPackage);
	}
	else if (AutoGenMode == EModelingModeAssetGenerationBehavior::InteractivePromptToSave)
	{
		AutoSaveGeneratedAsset(GeneratedTexture, AssetPackage);
		// this spawns a dialog that just allows save or not-save, but not renaming/etc, seems kind of useless...
		//InteractiveSaveGeneratedAsset(Builder.NewStaticMesh, AssetPackage);
	}
	else if (AutoGenMode == EModelingModeAssetGenerationBehavior::AutoGenerateButDoNotAutosave)
	{
		NotifyGeneratedAssetModified(GeneratedTexture, AssetPackage);
	}

	return true;
}




#undef LOCTEXT_NAMESPACE