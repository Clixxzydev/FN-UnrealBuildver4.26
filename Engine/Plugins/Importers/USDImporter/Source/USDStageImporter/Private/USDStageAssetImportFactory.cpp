// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDStageAssetImportFactory.h"

#include "USDLog.h"
#include "USDAssetImportData.h"
#include "USDStageImporter.h"
#include "USDStageImporterModule.h"
#include "USDStageImportOptions.h"
#include "USDStageImportOptionsWindow.h"

#include "ActorFactories/ActorFactoryStaticMesh.h"
#include "AssetImportTask.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "JsonObjectConverter.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/ScopedTimers.h"

#define LOCTEXT_NAMESPACE "USDStageAssetImportFactory"

UUsdStageAssetImportFactory::UUsdStageAssetImportFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = false;
	bEditAfterNew = true;
	SupportedClass = nullptr;

	// Its ok if we intercept most calls as there aren't other USD importers, and
	// for reimport we can definitely tell that we should be handling an asset, as we
	// use a custom asset import data
	ImportPriority += 100;

	bEditorImport = true;
	bText = false;

	Formats.Add(TEXT("usd;Universal Scene Descriptor files"));
	Formats.Add(TEXT("usda;Universal Scene Descriptor files"));
	Formats.Add(TEXT("usdc;Universal Scene Descriptor files"));
}

bool UUsdStageAssetImportFactory::DoesSupportClass(UClass* Class)
{
	return (Class == UStaticMesh::StaticClass() || Class == USkeletalMesh::StaticClass());
}

UClass* UUsdStageAssetImportFactory::ResolveSupportedClass()
{
	return UStaticMesh::StaticClass();
}

UObject* UUsdStageAssetImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	UObject* ImportedObject = nullptr;

	if (ImportContext.Init(InName.ToString(), Filename, Flags, IsAutomatedImport()))
	{
		GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport( this, InClass, InParent, InName, Parms );

		UUsdStageImporter* USDImporter = IUsdStageImporterModule::Get().GetImporter();
		USDImporter->ImportFromFile(ImportContext);

		GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, ImportContext.SceneActor);
		GEditor->BroadcastLevelActorListChanged();

		ImportContext.DisplayErrorMessages(ImportContext.bIsAutomated);

		ImportedObject = ImportContext.SceneActor;
	}
	else
	{
		bOutOperationCanceled = true;
	}

	return ImportedObject;
}

bool UUsdStageAssetImportFactory::FactoryCanImport(const FString& Filename)
{
	const FString Extension = FPaths::GetExtension(Filename);

	if (Extension == TEXT("usd") || Extension == TEXT("usda") || Extension == TEXT("usdc"))
	{
		return true;
	}

	return false;
}

void UUsdStageAssetImportFactory::CleanUp()
{
	ImportContext = FUsdStageImportContext();
	Super::CleanUp();
}

bool UUsdStageAssetImportFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	if (UUsdAssetImportData* ImportData = UUsdStageImporter::GetAssetImportData(Obj))
	{
		OutFilenames.Add(ImportData->GetFirstFilename());
		return true;
	}

	return false;
}

void UUsdStageAssetImportFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	if (NewReimportPaths.Num() != 1)
	{
		return;
	}

	if (UUsdAssetImportData* ImportData = UUsdStageImporter::GetAssetImportData(Obj))
	{
		ImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

EReimportResult::Type UUsdStageAssetImportFactory::Reimport(UObject* Obj)
{
	if (!Obj)
	{
		ImportContext.AddErrorMessage(EMessageSeverity::Error, LOCTEXT("ReimportErrorInvalidAsset", "Failed to reimport asset as it is invalid!"));
		return EReimportResult::Failed;
	}

	UUsdAssetImportData* ImportData = UUsdStageImporter::GetAssetImportData(Obj);
	if (!ImportData)
	{
		ImportContext.AddErrorMessage(EMessageSeverity::Error, FText::Format(LOCTEXT("ReimportErrorNoImportData", "Failed to reimport asset '{0}' as it doesn't seem to have import data!"), FText::FromName(Obj->GetFName())));
		return EReimportResult::Failed;
	}

	if (!ImportContext.Init(Obj->GetName(), ImportData->GetFirstFilename(), Obj->GetFlags(), IsAutomatedImport(), true))
	{
		ImportContext.AddErrorMessage(EMessageSeverity::Error, FText::Format(LOCTEXT("ReimportErrorNoContext", "Failed to initialize reimport context for asset '{0}'!"), FText::FromName(Obj->GetFName())));
		return EReimportResult::Failed;
	}

	ImportContext.PackagePath = Obj->GetOutermost()->GetPathName();

	UUsdStageImporter* USDImporter = IUsdStageImporterModule::Get().GetImporter();
	UObject* ReimportedAsset = nullptr;
	bool bSuccess = USDImporter->ReimportSingleAsset(ImportContext, Obj, ImportData, ReimportedAsset);

	ImportContext.DisplayErrorMessages(ImportContext.bIsAutomated);

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetReimport(Obj);

	return bSuccess ? EReimportResult::Succeeded : EReimportResult::Failed;
}

int32 UUsdStageAssetImportFactory::GetPriority() const
{
	return ImportPriority;
}

void UUsdStageAssetImportFactory::ParseFromJson(TSharedRef<class FJsonObject> ImportSettingsJson)
{
	FJsonObjectConverter::JsonObjectToUStruct(ImportSettingsJson, ImportOptions->GetClass(), ImportOptions, 0, CPF_InstancedReference);
}

#undef LOCTEXT_NAMESPACE