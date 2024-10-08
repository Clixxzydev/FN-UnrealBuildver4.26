// Copyright Epic Games, Inc. All Rights Reserved.

#include "VPUtilitiesEditorModule.h"

#include "Framework/Docking/WorkspaceItem.h"
#include "GameplayTagContainer.h"
#include "Editor.h"
#include "EditorUtilitySubsystem.h"
#include "HAL/IConsoleManager.h"
#include "LevelEditor.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "OSCManager.h"
#include "OSCServer.h"
#include "SGenlockProviderTab.h"
#include "Textures/SlateIcon.h"
#include "VPSettings.h"
#include "VPUtilitiesEditorSettings.h"
#include "VPUtilitiesEditorStyle.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"


#define LOCTEXT_NAMESPACE "VPUtilitiesEditor"

DEFINE_LOG_CATEGORY(LogVPUtilitiesEditor);

const FName FVPUtilitiesEditorModule::VPRoleNotificationBarIdentifier = TEXT("VPRoles");

void FVPUtilitiesEditorModule::StartupModule()
{
	FVPUtilitiesEditorStyle::Register();

	CustomUIHandler.Reset(NewObject<UVPCustomUIHandler>());
	CustomUIHandler->Init();

	{
		const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
		TSharedRef<FWorkspaceItem> MediaBrowserGroup = MenuStructure.GetDeveloperToolsMiscCategory()->GetParent()->AddGroup(
			LOCTEXT("WorkspaceMenu_VirtualProductionCategory", "Virtual Production"),
			FSlateIcon(),
			true);

		SGenlockProviderTab::RegisterNomadTabSpawner(MediaBrowserGroup);
	}

	RegisterSettings();

	if (GetDefault<UVPUtilitiesEditorSettings>()->bStartOSCServerAtLaunch)
	{
		InitializeOSCServer();
	}
}

void FVPUtilitiesEditorModule::ShutdownModule()
{
	UnregisterSettings();
	SGenlockProviderTab::UnregisterNomadTabSpawner();

	if (UObjectInitialized())
	{
		CustomUIHandler->Uninit();
	}

	CustomUIHandler.Reset();

	FVPUtilitiesEditorStyle::Unregister();
}

UOSCServer* FVPUtilitiesEditorModule::GetOSCServer() const
{
	return OSCServer.Get();
}

void FVPUtilitiesEditorModule::RegisterSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "VirtualProduction",
			LOCTEXT("VirtualProductionSettingsName", "Virtual Production"),
			LOCTEXT("VirtualProductionSettingsDescription", "Configure the Virtual Production settings."),
			GetMutableDefault<UVPSettings>());

		ISettingsSectionPtr EditorSettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "VirtualProductionEditor",
			LOCTEXT("VirtualProductionEditorSettingsName", "Virtual Production Editor"),
			LOCTEXT("VirtualProductionEditorSettingsDescription", "Configure the Virtual Production Editor settings."),
			GetMutableDefault<UVPUtilitiesEditorSettings>());

		EditorSettingsSection->OnModified().BindRaw(this, &FVPUtilitiesEditorModule::OnSettingsModified);
	}

	FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor");
	if (LevelEditorModule != nullptr)
	{
		FLevelEditorModule::FStatusBarItem Item;
		Item.Label = LOCTEXT("VPRolesLabel", "VP Roles: ");
		Item.Value = MakeAttributeLambda([]() { return FText::FromString(GetMutableDefault<UVPSettings>()->GetRoles().ToStringSimple()); });
		Item.Visibility = MakeAttributeLambda([]() { return GetMutableDefault<UVPSettings>()->bShowRoleInEditor ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; });
		LevelEditorModule->AddStatusBarItem(VPRoleNotificationBarIdentifier, Item);
	}
}

void FVPUtilitiesEditorModule::UnregisterSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "VirtualProduction");
		SettingsModule->UnregisterSettings("Project", "Plugins", "VirtualProductionEditor");
	}

	FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor");
	if (LevelEditorModule != nullptr)
	{
		LevelEditorModule->RemoveStatusBarItem(VPRoleNotificationBarIdentifier);
	}
}

void FVPUtilitiesEditorModule::InitializeOSCServer()
{
	if (OSCServer)
	{
		OSCServer->Stop();
	}

	const UVPUtilitiesEditorSettings* Settings = GetDefault<UVPUtilitiesEditorSettings>();
	const FString& ServerAddress = Settings->OSCServerAddress;
	uint16 ServerPort = Settings->OSCServerPort;

	if (OSCServer)
	{
		OSCServer->SetAddress(ServerAddress, ServerPort);
		OSCServer->Listen();
	}
	else
	{
		OSCServer.Reset(UOSCManager::CreateOSCServer(ServerAddress, ServerPort, false, true, FString()));
		
#if WITH_EDITOR
		// Allow it to tick in editor, so that messages are parsed.
		// Only doing it upon creation so that the user can make it non-tickable if desired (and manage that thereafter).
		if (OSCServer)
		{
			OSCServer->SetTickInEditor(true);
		}
#endif // WITH_EDITOR
	}

	const TArray<FSoftObjectPath>& ListenerPaths = Settings->StartupOSCListeners;
	for (const FSoftObjectPath& ListenerPath : ListenerPaths)
	{
		if (ListenerPath.IsValid())
		{
			UObject* Object = ListenerPath.TryLoad();
			if (Object && !Object->IsPendingKillOrUnreachable() && GEditor)
			{
				GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>()->TryRun(Object);
			}
		}
	}
}

bool FVPUtilitiesEditorModule::OnSettingsModified()
{
	const UVPUtilitiesEditorSettings* Settings = GetDefault<UVPUtilitiesEditorSettings>();
	if (Settings->bStartOSCServerAtLaunch)
	{
		InitializeOSCServer();
	}
	else if(OSCServer)
	{
		OSCServer->Stop();
	}

	IConsoleVariable* GizmoCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("VI.ShowTransformGizmo"));
	GizmoCVar->Set(Settings->bUseTransformGizmo);

	IConsoleVariable* InertiaCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("VI.HighSpeedInertiaDamping"));
	InertiaCVar->Set(Settings->bUseGripInertiaDamping ? Settings->InertiaDamping : 0);
	return true;
}


IMPLEMENT_MODULE(FVPUtilitiesEditorModule, VPUtilitiesEditor)

#undef LOCTEXT_NAMESPACE
