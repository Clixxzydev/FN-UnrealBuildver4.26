// Copyright Epic Games, Inc. All Rights Reserved.


#include "Kismet2/DebuggerCommands.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SSpinBox.h"
#include "Framework/Docking/TabManager.h"
#include "EditorStyleSet.h"
#include "Classes/EditorStyleSettings.h"
#include "GameFramework/Actor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Editor/UnrealEdEngine.h"
#include "Settings/EditorExperimentalSettings.h"
#include "GameFramework/PlayerStart.h"
#include "Components/CapsuleComponent.h"
#include "LevelEditorViewport.h"
#include "UnrealEdGlobals.h"
#include "EditorAnalytics.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/KismetDebugUtilities.h"

#include "Interfaces/TargetDeviceId.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "ITargetDeviceProxy.h"
#include "ITargetDeviceServicesModule.h"
#include "ISettingsModule.h"
#include "Interfaces/IMainFrameModule.h"

#include "EngineAnalytics.h"
#include "AnalyticsEventAttribute.h"
#include "Interfaces/IAnalyticsProvider.h"

#include "GameProjectGenerationModule.h"
#include "Interfaces/IProjectTargetPlatformEditorModule.h"
#include "PlatformInfo.h"

#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "Editor.h"

//@TODO: Remove this dependency
#include "EngineGlobals.h"
#include "LevelEditor.h"
#include "IAssetViewport.h"

#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"

#include "Interfaces/IProjectManager.h"

#include "InstalledPlatformInfo.h"
#include "PIEPreviewDeviceProfileSelectorModule.h"
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "IAndroidDeviceDetectionModule.h"
#include "IAndroidDeviceDetection.h"
#include "CookerSettings.h"
#include "HAL/PlatformFilemanager.h"
#include "SourceControlHelpers.h"
#include "ISourceControlModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "ToolMenus.h"
#include "SBlueprintEditorToolbar.h"
#include "SEnumCombobox.h"

#define LOCTEXT_NAMESPACE "DebuggerCommands"

void SGlobalPlayWorldActions::Construct(const FArguments& InArgs)
{
	// Always keep track of the current active play world actions widget so we later set user focus on it
	FPlayWorldCommands::SetActiveGlobalPlayWorldActionsWidget(SharedThis(this));

	ChildSlot
		[
			InArgs._Content.Widget
		];
}

FReply SGlobalPlayWorldActions::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Always keep track of the current active play world actions widget so we later set user focus on it
	FPlayWorldCommands::SetActiveGlobalPlayWorldActionsWidget(SharedThis(this));

	if (FPlayWorldCommands::GlobalPlayWorldActions->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	else
	{
		FPlayWorldCommands::SetActiveGlobalPlayWorldActionsWidget(TSharedPtr<SGlobalPlayWorldActions>());
		return FReply::Unhandled();
	}

}

bool SGlobalPlayWorldActions::SupportsKeyboardFocus() const
{
	return true;
}

// Put internal callbacks that we don't need to expose here in order to avoid unnecessary build dependencies outside of this module
class FInternalPlayWorldCommandCallbacks : public FPlayWorldCommandCallbacks
{
public:

	// Play In
	static void RepeatLastPlay_Clicked();
	static bool RepeatLastPlay_CanExecute();
	static FText GetRepeatLastPlayToolTip();
	static FSlateIcon GetRepeatLastPlayIcon();

	static void Simulate_Clicked();
	static bool Simulate_CanExecute();
	static bool Simulate_IsChecked();

	static void PlayInViewport_Clicked();
	static bool PlayInViewport_CanExecute();
	static void PlayInEditorFloating_Clicked();
	static bool PlayInEditorFloating_CanExecute();
	static void PlayInNewProcess_Clicked(EPlayModeType PlayModeType);
	static bool PlayInNewProcess_CanExecute();
	static void PlayInVR_Clicked();
	static bool PlayInVR_CanExecute();
	static bool PlayInModeIsChecked(EPlayModeType PlayMode);

	static void PlayInNewProcessPreviewDevice_Clicked(FString PIEPreviewDeviceName);
	static bool PlayInModeAndPreviewDeviceIsChecked(FString PIEPreviewDeviceName);

	static bool PlayInLocation_CanExecute(EPlayModeLocations Location);
	static void PlayInLocation_Clicked(EPlayModeLocations Location);
	static bool PlayInLocation_IsChecked(EPlayModeLocations Location);

	static void PlayInSettings_Clicked();

	// Launch On
	static void HandleLaunchOnDeviceActionExecute(FString DevicedId, FString DeviceName);
	static bool HandleLaunchOnDeviceActionCanExecute(FString DeviceName);
	static bool HandleLaunchOnDeviceActionIsChecked(FString DeviceName);

	// No Device
	static void HandleNoDeviceFoundActionExecute() {}
	static bool HandleNoDeviceFoundActionCanExecute() { return false; }

	static void HandleShowSDKTutorial(FString PlatformName, FString NotInstalledDocLink);

	static void RepeatLastLaunch_Clicked();
	static bool RepeatLastLaunch_CanExecute();
	static FText GetRepeatLastLaunchToolTip();
	static FSlateIcon GetRepeatLastLaunchIcon();
	static void OpenProjectLauncher_Clicked();
	static void OpenDeviceManager_Clicked();

	static FSlateIcon GetResumePlaySessionImage();
	static FText GetResumePlaySessionToolTip();
	static void StopPlaySession_Clicked();
	static void LateJoinSession_Clicked();
	static void SingleFrameAdvance_Clicked();

	static void ShowCurrentStatement_Clicked();
	static void StepInto_Clicked();
	static void StepOver_Clicked();
	static void StepOut_Clicked();

	static void TogglePlayPause_Clicked();

	// Mouse control
	static void GetMouseControlExecute();

	static void PossessEjectPlayer_Clicked();
	static bool CanPossessEjectPlayer();
	static FText GetPossessEjectLabel();
	static FText GetPossessEjectTooltip();
	static FSlateIcon GetPossessEjectImage();

	static bool CanLateJoin();
	static bool CanShowLateJoinButton();

	static bool IsStoppedAtBreakpoint();

	static bool CanShowNonPlayWorldOnlyActions();
	static bool CanShowVulkanNonPlayWorldOnlyActions();
	static bool CanShowVROnlyActions();

	static int32 GetNumberOfClients();
	static void SetNumberOfClients(int32 NumClients, ETextCommit::Type CommitInfo);

	static int32 GetNetPlayMode();
	static void SetNetPlayMode(int32 Value, ESelectInfo::Type CommitInfo);

protected:

	static void PlayInNewProcess(EPlayModeType PlayModeType, FString PIEPreviewDeviceName);

	/**
	 * Adds a message to the message log.
	 *
	 * @param Text The main message text.
	 * @param Detail The detailed description.
	 * @param TutorialLink A link to an associated tutorial.
	 * @param DocumentationLink A link to documentation.
	 */
	static void AddMessageLog(const FText& Text, const FText& Detail, const FString& TutorialLink, const FString& DocumentationLink);

	/**
	 * Checks whether the specified platform has a default device that can be launched on.
	 *
	 * @param PlatformName - The name of the platform to check.
	 *
	 * @return true if the platform can be played on, false otherwise.
	 */
	static bool CanLaunchOnDevice(const FString& DeviceName);

	/**
	 * Starts a game session on the default device of the specified platform.
	 *
	 * @param PlatformName - The name of the platform to play the game on.
	 */
	static void LaunchOnDevice(const FString& DeviceId, const FString& DeviceName);

	/** Get the player start location to use when starting PIE */
	static EPlayModeLocations GetPlayModeLocation();

	/** checks to see if we have everything needed to launch a build to device */
	static bool IsReadyToLaunchOnDevice(FString DeviceId);
};


/**
 * Called to leave K2 debugging mode
 */
static void LeaveDebuggingMode()
{
	if (GUnrealEd->PlayWorld != NULL)
	{
		GUnrealEd->PlayWorld->bDebugPauseExecution = false;
	}

	// Determine whether or not we are resuming play.
	const bool bIsResumingPlay = !FKismetDebugUtilities::IsSingleStepping() && !GEditor->ShouldEndPlayMap();

	if (FSlateApplication::Get().InKismetDebuggingMode() && bIsResumingPlay)
	{
		// Focus the game view port when resuming from debugging.
		FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor").FocusPIEViewport();
	}

	// Tell the application to stop ticking in this stack frame. The parameter controls whether or not to recapture the mouse to the game viewport.
	FSlateApplication::Get().LeaveDebuggingMode(!bIsResumingPlay);
}


//////////////////////////////////////////////////////////////////////////
// FPlayWorldCommands

TSharedPtr<FUICommandList> FPlayWorldCommands::GlobalPlayWorldActions;

TWeakPtr<SGlobalPlayWorldActions> FPlayWorldCommands::ActiveGlobalPlayWorldActionsWidget;

TWeakPtr<SGlobalPlayWorldActions> FPlayWorldCommands::GetActiveGlobalPlayWorldActionsWidget()
{
	return FPlayWorldCommands::ActiveGlobalPlayWorldActionsWidget;
}

void FPlayWorldCommands::SetActiveGlobalPlayWorldActionsWidget(TWeakPtr<SGlobalPlayWorldActions> ActiveWidget)
{
	FPlayWorldCommands::ActiveGlobalPlayWorldActionsWidget = ActiveWidget;
}

FPlayWorldCommands::FPlayWorldCommands()
	: TCommands<FPlayWorldCommands>("PlayWorld", LOCTEXT("PlayWorld", "Play World (PIE/SIE)"), "MainFrame", FEditorStyle::GetStyleSetName())
{
	ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();

	// initialize default Play device
	if (PlaySettings->LastExecutedLaunchName.IsEmpty())
	{
		FString RunningPlatformName = GetTargetPlatformManagerRef().GetRunningTargetPlatform()->PlatformName();
		FString PlayPlatformName;

		if (RunningPlatformName == TEXT("Windows"))
		{
			PlayPlatformName = TEXT("WindowsNoEditor");
		}
		else if (RunningPlatformName == TEXT("Mac"))
		{
			PlayPlatformName = TEXT("MacNoEditor");
		}
		else if (RunningPlatformName == TEXT("Linux"))
		{
			PlayPlatformName = TEXT("LinuxNoEditor");
		}
		else if (RunningPlatformName == TEXT("LinuxAArch64"))
		{
			PlayPlatformName = TEXT("LinuxAArch64NoEditor");
		}

		if (!PlayPlatformName.IsEmpty())
		{
			ITargetPlatform* PlayPlatform = GetTargetPlatformManagerRef().FindTargetPlatform(PlayPlatformName);

			if (PlayPlatform != nullptr)
			{
				ITargetDevicePtr PlayDevice = PlayPlatform->GetDefaultDevice();

				if (PlayDevice.IsValid())
				{
					PlaySettings->LastExecutedLaunchDevice = PlayDevice->GetId().ToString();
					PlaySettings->LastExecutedLaunchName = PlayDevice->GetName();
					PlaySettings->SaveConfig();
				}
			}
		}
	}
}


void FPlayWorldCommands::RegisterCommands()
{
	// SIE
	UI_COMMAND(Simulate, "Simulate", "Start simulating the game", EUserInterfaceActionType::Check, FInputChord(EKeys::S, EModifierKey::Alt));

	// PIE
	UI_COMMAND(RepeatLastPlay, "Play", "Launches a game preview session in the same mode as the last game preview session launched from the Game Preview Modes dropdown next to the Play button on the level editor toolbar", EUserInterfaceActionType::Button, FInputChord(EKeys::P, EModifierKey::Alt))
	UI_COMMAND(PlayInViewport, "Selected Viewport", "Play this level in the active level editor viewport", EUserInterfaceActionType::Check, FInputChord());
	UI_COMMAND(PlayInEditorFloating, "New Editor Window (PIE)", "Play this level in a new window", EUserInterfaceActionType::Check, FInputChord());
	UI_COMMAND(PlayInVR, "VR Preview", "Play this level in VR", EUserInterfaceActionType::Check, FInputChord());
	UI_COMMAND(PlayInMobilePreview, "Mobile Preview ES3.1 (PIE)", "Play this level as a mobile device preview in ES3.1 mode (runs in its own process)", EUserInterfaceActionType::Check, FInputChord());
	UI_COMMAND(PlayInVulkanPreview, "Vulkan Mobile Preview (PIE)", "Play this level using mobile Vulkan rendering (runs in its own process)", EUserInterfaceActionType::Check, FInputChord());
	UI_COMMAND(PlayInNewProcess, "Standalone Game", "Play this level in a new window that runs in its own process", EUserInterfaceActionType::Check, FInputChord());
	UI_COMMAND(PlayInCameraLocation, "Current Camera Location", "Spawn the player at the current camera location", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(PlayInDefaultPlayerStart, "Default Player Start", "Spawn the player at the map's default player start", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(PlayInNetworkSettings, "Network Settings...", "Open the settings for the 'Play In' feature", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(PlayInSettings, "Advanced Settings...", "Open the settings for the 'Play In' feature", EUserInterfaceActionType::Button, FInputChord());

	// SIE & PIE controls
	UI_COMMAND(StopPlaySession, "Stop", "Stop simulation", EUserInterfaceActionType::Button, FInputChord(EKeys::Escape));
	UI_COMMAND(ResumePlaySession, "Resume", "Resume simulation", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(PausePlaySession, "Pause", "Pause simulation", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(GetMouseControl, "Mouse Control", "Get mouse cursor while in PIE", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift, EKeys::F1));
	UI_COMMAND(LateJoinSession, "Add Client", "Add another client", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SingleFrameAdvance, "Skip", "Advances a single frame", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(TogglePlayPauseOfPlaySession, "Toggle Play/Pause", "Resume playing if paused, or pause if playing", EUserInterfaceActionType::Button, FInputChord(EKeys::Pause));
	UI_COMMAND(PossessEjectPlayer, "Possess or Eject Player", "Possesses or ejects the player from the camera", EUserInterfaceActionType::Button, FInputChord(EKeys::F8));
	UI_COMMAND(ShowCurrentStatement, "Locate", "Locate the currently active node", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(StepInto, "Step Into", "Step Into the next node to be executed", EUserInterfaceActionType::Button, PLATFORM_MAC ? FInputChord(EModifierKey::Control, EKeys::F11) : FInputChord(EKeys::F11));
	UI_COMMAND(StepOver, "Step Over", "Step to the next node to be executed in the current graph", EUserInterfaceActionType::Button, FInputChord(EKeys::F10));
	UI_COMMAND(StepOut, "Step Out", "Step Out to the next node to be executed in the parent graph", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Alt | EModifierKey::Shift, EKeys::F11));

	// Launch
	UI_COMMAND(RepeatLastLaunch, "Launch", "Launches the game on the device as the last session launched from the dropdown next to the Play on Device button on the level editor toolbar", EUserInterfaceActionType::Button, FInputChord(EKeys::P, EModifierKey::Alt | EModifierKey::Shift))
		UI_COMMAND(OpenProjectLauncher, "Project Launcher...", "Open the Project Launcher for advanced packaging, deploying and launching of your projects", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenDeviceManager, "Device Manager...", "View and manage connected devices.", EUserInterfaceActionType::Button, FInputChord());

	// PIE mobile preview devices.
	AddPIEPreviewDeviceCommands();
}

void FPlayWorldCommands::AddPIEPreviewDeviceCommands()
{
	auto PIEPreviewDeviceModule = FModuleManager::LoadModulePtr<FPIEPreviewDeviceModule>(TEXT("PIEPreviewDeviceProfileSelector"));
	if (PIEPreviewDeviceModule)
	{
		TArray<TSharedPtr<FUICommandInfo>>& TargetedMobilePreviewDeviceCommands = PlayInTargetedMobilePreviewDevices;
		const TArray<FString>& Devices = PIEPreviewDeviceModule->GetPreviewDeviceContainer().GetDeviceSpecificationsLocalizedName();
		PlayInTargetedMobilePreviewDevices.SetNum(Devices.Num());
		for (int32 DeviceIndex = 0; DeviceIndex < Devices.Num(); DeviceIndex++)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("Device"), FText::FromString(Devices[DeviceIndex]));
			const FText CommandLabel = FText::Format(LOCTEXT("DevicePreviewLaunchCommandLabel", "{Device}"), Args);
			const FText CommandDesc = FText::Format(LOCTEXT("DevicePreviewLaunchCommandDesc", "Launch on this computer using {Device}'s settings."), Args);

			FUICommandInfo::MakeCommandInfo(
				this->AsShared(),
				TargetedMobilePreviewDeviceCommands[DeviceIndex],
				FName(*CommandLabel.ToString()),
				CommandLabel,
				CommandDesc,
				FSlateIcon(FEditorStyle::GetStyleSetName(), "PlayWorld.PlayInMobilePreview"),
				EUserInterfaceActionType::Check,
				FInputChord());
		}
	}
}

void FPlayWorldCommands::BindGlobalPlayWorldCommands()
{
	check(!GlobalPlayWorldActions.IsValid());

	GlobalPlayWorldActions = MakeShareable(new FUICommandList);

	const FPlayWorldCommands& Commands = FPlayWorldCommands::Get();
	FUICommandList& ActionList = *GlobalPlayWorldActions;

	// SIE
	ActionList.MapAction(Commands.Simulate,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::Simulate_Clicked),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::Simulate_CanExecute),
		FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInModeIsChecked, PlayMode_Simulate),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowNonPlayWorldOnlyActions)
	);

	// PIE
	ActionList.MapAction(Commands.RepeatLastPlay,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::RepeatLastPlay_Clicked),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::RepeatLastPlay_CanExecute),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowNonPlayWorldOnlyActions)
	);

	ActionList.MapAction(Commands.PlayInViewport,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInViewport_Clicked),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInViewport_CanExecute),
		FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInModeIsChecked, PlayMode_InViewPort),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowNonPlayWorldOnlyActions)
	);

	ActionList.MapAction(Commands.PlayInEditorFloating,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInEditorFloating_Clicked),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInEditorFloating_CanExecute),
		FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInModeIsChecked, PlayMode_InEditorFloating),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowNonPlayWorldOnlyActions)
	);

	ActionList.MapAction(Commands.PlayInVR,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInVR_Clicked),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInVR_CanExecute),
		FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInModeIsChecked, PlayMode_InVR),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowVROnlyActions)
	);

	ActionList.MapAction(Commands.PlayInMobilePreview,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInNewProcess_Clicked, PlayMode_InMobilePreview),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInNewProcess_CanExecute),
		FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInModeIsChecked, PlayMode_InMobilePreview),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowNonPlayWorldOnlyActions)
	);

	ActionList.MapAction(Commands.PlayInVulkanPreview,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInNewProcess_Clicked, PlayMode_InVulkanPreview),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInNewProcess_CanExecute),
		FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInModeIsChecked, PlayMode_InVulkanPreview),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowVulkanNonPlayWorldOnlyActions)
	);

	ActionList.MapAction(Commands.PlayInNewProcess,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInNewProcess_Clicked, PlayMode_InNewProcess),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInNewProcess_CanExecute),
		FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInModeIsChecked, PlayMode_InNewProcess),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowNonPlayWorldOnlyActions)
	);

	ActionList.MapAction(Commands.PlayInCameraLocation,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInLocation_Clicked, PlayLocation_CurrentCameraLocation),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInLocation_CanExecute, PlayLocation_CurrentCameraLocation),
		FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInLocation_IsChecked, PlayLocation_CurrentCameraLocation),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowNonPlayWorldOnlyActions)
	);

	ActionList.MapAction(Commands.PlayInDefaultPlayerStart,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInLocation_Clicked, PlayLocation_DefaultPlayerStart),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInLocation_CanExecute, PlayLocation_DefaultPlayerStart),
		FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInLocation_IsChecked, PlayLocation_DefaultPlayerStart),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowNonPlayWorldOnlyActions)
	);

	ActionList.MapAction(Commands.PlayInSettings,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInSettings_Clicked)
	);

	// Launch
	ActionList.MapAction(Commands.OpenProjectLauncher,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::OpenProjectLauncher_Clicked)
	);

	ActionList.MapAction(Commands.OpenDeviceManager,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::OpenDeviceManager_Clicked)
	);

	ActionList.MapAction(Commands.RepeatLastLaunch,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::RepeatLastLaunch_Clicked),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::RepeatLastLaunch_CanExecute),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowNonPlayWorldOnlyActions)
	);


	// Stop play session
	ActionList.MapAction(Commands.StopPlaySession,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::StopPlaySession_Clicked),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::HasPlayWorld),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::HasPlayWorld)
	);

	// Late join session
	ActionList.MapAction(Commands.LateJoinSession,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::LateJoinSession_Clicked),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanLateJoin),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowLateJoinButton)
	);

	// Play, Pause, Toggle between play and pause
	ActionList.MapAction(Commands.ResumePlaySession,
		FExecuteAction::CreateStatic(&FPlayWorldCommandCallbacks::ResumePlaySession_Clicked),
		FCanExecuteAction::CreateStatic(&FPlayWorldCommandCallbacks::HasPlayWorldAndPaused),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateStatic(&FPlayWorldCommandCallbacks::HasPlayWorldAndPaused)
	);

	ActionList.MapAction(Commands.PausePlaySession,
		FExecuteAction::CreateStatic(&FPlayWorldCommandCallbacks::PausePlaySession_Clicked),
		FCanExecuteAction::CreateStatic(&FPlayWorldCommandCallbacks::HasPlayWorldAndRunning),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateStatic(&FPlayWorldCommandCallbacks::HasPlayWorldAndRunning)
	);

	ActionList.MapAction(Commands.SingleFrameAdvance,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::SingleFrameAdvance_Clicked),
		FCanExecuteAction::CreateStatic(&FPlayWorldCommandCallbacks::HasPlayWorldAndPaused),
		FIsActionChecked(),
		FIsActionChecked::CreateStatic(&FPlayWorldCommandCallbacks::HasPlayWorldAndPaused)
	);

	ActionList.MapAction(Commands.TogglePlayPauseOfPlaySession,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::TogglePlayPause_Clicked),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::HasPlayWorld),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::HasPlayWorld)
	);

	// Get mouse control from PIE
	ActionList.MapAction(Commands.GetMouseControl,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::GetMouseControlExecute),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::HasPlayWorld),
		FIsActionChecked(),
		FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::HasPlayWorld)
	);

	// Toggle PIE/SIE, Eject (PIE->SIE), and Possess (SIE->PIE)
	ActionList.MapAction(Commands.PossessEjectPlayer,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PossessEjectPlayer_Clicked),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanPossessEjectPlayer),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanPossessEjectPlayer)
	);

	// Breakpoint-only commands
	ActionList.MapAction(Commands.ShowCurrentStatement,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::ShowCurrentStatement_Clicked),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::IsStoppedAtBreakpoint),
		FIsActionChecked(),
		FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::IsStoppedAtBreakpoint)
	);

	ActionList.MapAction(Commands.StepInto,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::StepInto_Clicked),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::IsStoppedAtBreakpoint),
		FIsActionChecked(),
		FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::IsStoppedAtBreakpoint)
	);

	ActionList.MapAction(Commands.StepOver,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::StepOver_Clicked),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::IsStoppedAtBreakpoint),
		FIsActionChecked(),
		FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::IsStoppedAtBreakpoint)
	);

	ActionList.MapAction(Commands.StepOut,
		FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::StepOut_Clicked),
		FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::IsStoppedAtBreakpoint),
		FIsActionChecked(),
		FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::IsStoppedAtBreakpoint)
	);

	AddPIEPreviewDeviceActions(Commands, ActionList);
}

void FPlayWorldCommands::AddPIEPreviewDeviceActions(const FPlayWorldCommands &Commands, FUICommandList &ActionList)
{
	// PIE preview devices.
	auto PIEPreviewDeviceModule = FModuleManager::LoadModulePtr<FPIEPreviewDeviceModule>(TEXT("PIEPreviewDeviceProfileSelector"));
	if (PIEPreviewDeviceModule)
	{
		const TArray<TSharedPtr<FUICommandInfo>>& TargetedMobilePreviewDeviceCommands = Commands.PlayInTargetedMobilePreviewDevices;
		const TArray<FString>& Devices = PIEPreviewDeviceModule->GetPreviewDeviceContainer().GetDeviceSpecifications();
		for (int32 DeviceIndex = 0; DeviceIndex < Devices.Num(); DeviceIndex++)
		{
			ActionList.MapAction(TargetedMobilePreviewDeviceCommands[DeviceIndex],
				FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInNewProcessPreviewDevice_Clicked, Devices[DeviceIndex]),
				FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInNewProcess_CanExecute),
				FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::PlayInModeAndPreviewDeviceIsChecked, Devices[DeviceIndex]),
				FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowNonPlayWorldOnlyActions)
			);
		}
	}
}

void FPlayWorldCommands::BuildToolbar(FToolMenuSection& InSection, bool bIncludeLaunchButtonAndOptions)
{
	// Play
	InSection.AddEntry(FToolMenuEntry::InitToolBarButton(
		FPlayWorldCommands::Get().RepeatLastPlay,
		LOCTEXT("RepeatLastPlay", "Play"),
		TAttribute< FText >::Create( TAttribute< FText >::FGetter::CreateStatic( &FInternalPlayWorldCommandCallbacks::GetRepeatLastPlayToolTip ) ),
		TAttribute< FSlateIcon >::Create(TAttribute< FSlateIcon >::FGetter::CreateStatic(&FInternalPlayWorldCommandCallbacks::GetRepeatLastPlayIcon)),
		FName(TEXT("LevelToolbarPlay"))
	));

	// Play combo box
	FUIAction SpecialPIEOptionsMenuAction;
	SpecialPIEOptionsMenuAction.IsActionVisibleDelegate = FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowNonPlayWorldOnlyActions);

	InSection.AddEntry(FToolMenuEntry::InitComboButton(
		"PlayCombo",
		SpecialPIEOptionsMenuAction,
		FOnGetContent::CreateStatic(&GeneratePlayMenuContent, GlobalPlayWorldActions.ToSharedRef()),
		LOCTEXT("PlayCombo_Label", "Active Play Mode"),
		LOCTEXT("PIEComboToolTip", "Change Play Mode and Play Settings"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.RepeatLastPlay"),
		true
	));

	if (bIncludeLaunchButtonAndOptions)
	{
		InSection.AddDynamicEntry("LaunchButtons", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InDynamicSection)
		{
			if (GetDefault<UEditorStyleSettings>()->bShowLaunchMenus)
			{
				// Launch
				InDynamicSection.AddEntry(FToolMenuEntry::InitToolBarButton(
					FPlayWorldCommands::Get().RepeatLastLaunch,
					LOCTEXT("RepeatLastLaunch", "Launch"),
					TAttribute< FText >::Create(TAttribute< FText >::FGetter::CreateStatic(&FInternalPlayWorldCommandCallbacks::GetRepeatLastLaunchToolTip)),
					TAttribute< FSlateIcon >::Create(TAttribute< FSlateIcon >::FGetter::CreateStatic(&FInternalPlayWorldCommandCallbacks::GetRepeatLastLaunchIcon)),
					FName(TEXT("RepeatLastLaunch"))
				));

				// Launch combo box
				FUIAction LaunchMenuAction;
				LaunchMenuAction.IsActionVisibleDelegate = FIsActionButtonVisible::CreateStatic(&FInternalPlayWorldCommandCallbacks::CanShowNonPlayWorldOnlyActions);

				InDynamicSection.AddEntry(FToolMenuEntry::InitComboButton(
					"LaunchCombo",
					LaunchMenuAction,
					FOnGetContent::CreateStatic(&GenerateLaunchMenuContent, GlobalPlayWorldActions.ToSharedRef()),
					LOCTEXT("LaunchCombo_Label", "Launch Options"),
					LOCTEXT("PODComboToolTip", "Options for launching on a device"),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.RepeatLastLaunch"),
					true
				));
			}
		}));
	}

	// Resume/pause toggle (only one will be visible, and only in PIE/SIE)
	InSection.AddEntry(FToolMenuEntry::InitToolBarButton(FPlayWorldCommands::Get().ResumePlaySession, TAttribute<FText>(),
		TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateStatic(&FInternalPlayWorldCommandCallbacks::GetResumePlaySessionToolTip)),
		TAttribute<FSlateIcon>::Create(TAttribute<FSlateIcon>::FGetter::CreateStatic(&FInternalPlayWorldCommandCallbacks::GetResumePlaySessionImage)),
		FName(TEXT("ResumePlaySession"))
	));

	InSection.AddEntry(FToolMenuEntry::InitToolBarButton(FPlayWorldCommands::Get().PausePlaySession, TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), FName(TEXT("PausePlaySession"))));
	InSection.AddEntry(FToolMenuEntry::InitToolBarButton(FPlayWorldCommands::Get().SingleFrameAdvance, TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), FName(TEXT("SingleFrameAdvance"))));

	// Stop
	InSection.AddEntry(FToolMenuEntry::InitToolBarButton(FPlayWorldCommands::Get().StopPlaySession, TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), FName(TEXT("StopPlaySession"))));

	// Late Join
	InSection.AddEntry(FToolMenuEntry::InitToolBarButton(FPlayWorldCommands::Get().LateJoinSession, TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), FName(TEXT("LateJoinSession"))));

	// Eject/possess toggle
	InSection.AddEntry(FToolMenuEntry::InitToolBarButton(FPlayWorldCommands::Get().PossessEjectPlayer,
		TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateStatic(&FInternalPlayWorldCommandCallbacks::GetPossessEjectLabel)),
		TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateStatic(&FInternalPlayWorldCommandCallbacks::GetPossessEjectTooltip)),
		TAttribute<FSlateIcon>::Create(TAttribute<FSlateIcon>::FGetter::CreateStatic(&FInternalPlayWorldCommandCallbacks::GetPossessEjectImage)),
		FName(TEXT("PossessEjectPlayer"))
	));

	// Single-stepping only buttons
	InSection.AddEntry(FToolMenuEntry::InitToolBarButton(FPlayWorldCommands::Get().ShowCurrentStatement, TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), FName(TEXT("ShowCurrentStatement"))));
	InSection.AddEntry(FToolMenuEntry::InitToolBarButton(FPlayWorldCommands::Get().StepInto, TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), FName(TEXT("StepInto"))));
	InSection.AddEntry(FToolMenuEntry::InitToolBarButton(FPlayWorldCommands::Get().StepOver, TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), FName(TEXT("StepOver"))));
	InSection.AddEntry(FToolMenuEntry::InitToolBarButton(FPlayWorldCommands::Get().StepOut, TAttribute<FText>(), TAttribute<FText>(), TAttribute<FSlateIcon>(), FName(TEXT("StepOut"))));
}

// function will enumerate available Android devices that can export their profile to a json file
// called (below) from AddAndroidConfigExportMenu()
static void AddAndroidConfigExportSubMenus(FMenuBuilder& InMenuBuilder)
{
	IAndroidDeviceDetection* DeviceDetection = FModuleManager::LoadModuleChecked<IAndroidDeviceDetectionModule>("AndroidDeviceDetection").GetAndroidDeviceDetection();

	TMap<FString, FAndroidDeviceInfo> AndroidDeviceMap;

	// lock device map and copy its contents
	{
		FCriticalSection* DeviceLock = DeviceDetection->GetDeviceMapLock();
		FScopeLock Lock(DeviceLock);
		AndroidDeviceMap = DeviceDetection->GetDeviceMap();
	}

	for (auto& Pair : AndroidDeviceMap)
	{
		FAndroidDeviceInfo& DeviceInfo = Pair.Value;

		FString ModelName = DeviceInfo.Model + TEXT("[") + DeviceInfo.DeviceBrand + TEXT("]");

		// lambda function called to open the save dialog and trigger device export
		auto LambdaSaveConfigFile = [DeviceName = Pair.Key, DefaultFileName = ModelName, DeviceDetection]()
		{
			TArray<FString> OutputFileName;
			FString DefaultFolder = FPaths::EngineContentDir() + TEXT("Editor/PIEPreviewDeviceSpecs/Android/");

			bool bResult = FDesktopPlatformModule::Get()->SaveFileDialog(
				FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
				LOCTEXT("PackagePluginDialogTitle", "Save platform configuration...").ToString(),
				DefaultFolder,
				DefaultFileName,
				TEXT("Json config file (*.json)|*.json"),
				0,
				OutputFileName);

			if (bResult && OutputFileName.Num())
			{
				DeviceDetection->ExportDeviceProfile(OutputFileName[0], DeviceName);
			}
		};

		InMenuBuilder.AddMenuEntry(
			FText::FromString(ModelName),
			FText(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "AssetEditor.SaveAsset"),
			FUIAction(FExecuteAction::CreateLambda(LambdaSaveConfigFile))
		);
	}
}

// function adds a sub-menu that will enumerate Android devices whose profiles can be exported json files
static void AddAndroidConfigExportMenu(FMenuBuilder& InMenuBuilder)
{
	InMenuBuilder.AddMenuSeparator();

	InMenuBuilder.AddSubMenu(
		LOCTEXT("loc_AddAndroidConfigExportMenu", "Export device settings"),
		LOCTEXT("loc_tip_AddAndroidConfigExportMenu", "Export device settings to a Json file."),
		FNewMenuDelegate::CreateStatic(&AddAndroidConfigExportSubMenus),
		false,
		FSlateIcon(FEditorStyle::GetStyleSetName(), "MainFrame.SaveAll")
	);
}

static void MakePreviewDeviceMenu(FMenuBuilder& MenuBuilder)
{
	struct FLocal
	{
		static void AddDevicePreviewSubCategories(FMenuBuilder& MenuBuilderIn, TSharedPtr<FPIEPreviewDeviceContainerCategory> PreviewDeviceCategory)
		{
			const TArray<TSharedPtr<FUICommandInfo>>& TargetedMobilePreviewDeviceCommands = FPlayWorldCommands::Get().PlayInTargetedMobilePreviewDevices;
			int32 StartIndex = PreviewDeviceCategory->GetDeviceStartIndex();
			int32 EndIndex = StartIndex + PreviewDeviceCategory->GetDeviceCount();
			for (int32 Device = StartIndex; Device < EndIndex; Device++)
			{
				MenuBuilderIn.AddMenuEntry(TargetedMobilePreviewDeviceCommands[Device]);
			}

			static FText AndroidCategory = FText::FromString(TEXT("Android"));
			static FText IOSCategory = FText::FromString(TEXT("IOS"));

			// Android devices can export their profile to a json file which then can be used for PIE device simulations
			const FText& CategoryDisplayName = PreviewDeviceCategory->GetCategoryDisplayName();
			if (CategoryDisplayName.CompareToCaseIgnored(AndroidCategory) == 0)
			{
				// check to see if we have any connected devices
				bool bHasAndroidDevices = false;
				{
					IAndroidDeviceDetection* DeviceDetection = FModuleManager::LoadModuleChecked<IAndroidDeviceDetectionModule>("AndroidDeviceDetection").GetAndroidDeviceDetection();
					FCriticalSection* DeviceLock = DeviceDetection->GetDeviceMapLock();

					FScopeLock Lock(DeviceLock);
					bHasAndroidDevices = DeviceDetection->GetDeviceMap().Num() > 0;
				}

				// add the config. export menu
				if (bHasAndroidDevices)
				{
					AddAndroidConfigExportMenu(MenuBuilderIn);
				}
			}

			for (TSharedPtr<FPIEPreviewDeviceContainerCategory> SubCategory : PreviewDeviceCategory->GetSubCategories())
			{
				MenuBuilderIn.AddSubMenu(
					SubCategory->GetCategoryDisplayName(),
					SubCategory->GetCategoryToolTip(),
					FNewMenuDelegate::CreateStatic(&FLocal::AddDevicePreviewSubCategories, SubCategory)
				);
			}
		}
	};

	const TArray<TSharedPtr<FUICommandInfo>>& TargetedMobilePreviewDeviceCommands = FPlayWorldCommands::Get().PlayInTargetedMobilePreviewDevices;
	auto PIEPreviewDeviceModule = FModuleManager::LoadModulePtr<FPIEPreviewDeviceModule>(TEXT("PIEPreviewDeviceProfileSelector"));
	if (PIEPreviewDeviceModule)
	{
		const FPIEPreviewDeviceContainer& DeviceContainer = PIEPreviewDeviceModule->GetPreviewDeviceContainer();
		MenuBuilder.BeginSection("LevelEditorPlayModesPreviewDevice", LOCTEXT("PreviewDevicePlayButtonModesSection", "Preview Devices"));
		FLocal::AddDevicePreviewSubCategories(MenuBuilder, DeviceContainer.GetRootCategory());
		MenuBuilder.EndSection();
	}
}

TSharedRef< SWidget > FPlayWorldCommands::GeneratePlayMenuContent(TSharedRef<FUICommandList> InCommandList)
{
	static const FName MenuName("UnrealEd.PlayWorldCommands.PlayMenu");

	if (!UToolMenus::Get()->IsMenuRegistered(MenuName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(MenuName);

		struct FLocal
		{
			static void AddPlayModeMenuEntry(FToolMenuSection& Section, EPlayModeType PlayMode)
			{
				TSharedPtr<FUICommandInfo> PlayModeCommand;

				switch (PlayMode)
				{
				case PlayMode_InEditorFloating:
					PlayModeCommand = FPlayWorldCommands::Get().PlayInEditorFloating;
					break;

				case PlayMode_InMobilePreview:
					PlayModeCommand = FPlayWorldCommands::Get().PlayInMobilePreview;
					break;

				case PlayMode_InVulkanPreview:
					PlayModeCommand = FPlayWorldCommands::Get().PlayInVulkanPreview;
					break;

				case PlayMode_InNewProcess:
					PlayModeCommand = FPlayWorldCommands::Get().PlayInNewProcess;
					break;

				case PlayMode_InViewPort:
					PlayModeCommand = FPlayWorldCommands::Get().PlayInViewport;
					break;

				case PlayMode_InVR:
					PlayModeCommand = FPlayWorldCommands::Get().PlayInVR;
					break;

				case PlayMode_Simulate:
					PlayModeCommand = FPlayWorldCommands::Get().Simulate;
					break;
				}

				if (PlayModeCommand.IsValid())
				{
					Section.AddMenuEntry(PlayModeCommand);
				}
			}
		};

		// play in view port
		{
			FToolMenuSection& Section = Menu->AddSection("LevelEditorPlayModes", LOCTEXT("PlayButtonModesSection", "Modes"));
			FLocal::AddPlayModeMenuEntry(Section, PlayMode_InViewPort);
			FLocal::AddPlayModeMenuEntry(Section, PlayMode_InMobilePreview);

			if (GetDefault<UEditorExperimentalSettings>()->bMobilePIEPreviewDeviceLaunch)
			{
				Section.AddSubMenu(
					"TargetedMobilePreview",
					LOCTEXT("TargetedMobilePreviewSubMenu", "Mobile Preview (PIE)"),
					LOCTEXT("TargetedMobilePreviewSubMenu_ToolTip", "Play this level using a specified mobile device preview (runs in its own process)"),
					FNewMenuDelegate::CreateStatic(&MakePreviewDeviceMenu), false,
					FSlateIcon(FEditorStyle::GetStyleSetName(), "PlayWorld.PlayInMobilePreview")
				);
			}

			FLocal::AddPlayModeMenuEntry(Section, PlayMode_InVulkanPreview);
			FLocal::AddPlayModeMenuEntry(Section, PlayMode_InEditorFloating);
			FLocal::AddPlayModeMenuEntry(Section, PlayMode_InVR);
			FLocal::AddPlayModeMenuEntry(Section, PlayMode_InNewProcess);
			FLocal::AddPlayModeMenuEntry(Section, PlayMode_Simulate);
		}

		// tip section
		{
			FToolMenuSection& Section = Menu->AddSection("LevelEditorPlayTip");
			Section.AddEntry(FToolMenuEntry::InitWidget(
				"PlayIn",
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("PlayInTip", "Launching a game preview with a different mode will change your default 'Play' mode in the toolbar"))
				.WrapTextAt(250),
				FText::GetEmpty()));
		}

		// player start selection
		{
			FToolMenuSection& Section = Menu->AddSection("LevelEditorPlayPlayerStart", LOCTEXT("PlayButtonLocationSection", "Spawn player at..."));
			Section.AddMenuEntry(FPlayWorldCommands::Get().PlayInCameraLocation);
			Section.AddMenuEntry(FPlayWorldCommands::Get().PlayInDefaultPlayerStart);
		}

		// Basic network options
		const ULevelEditorPlaySettings* PlayInSettings = GetDefault<ULevelEditorPlaySettings>();
		{
			FToolMenuSection& Section = Menu->AddSection("LevelEditorPlayInWindowNetwork", LOCTEXT("LevelEditorPlayInWindowNetworkSection", "Multiplayer Options"));
			// Num Clients
			{
				TSharedRef<SWidget> NumPlayers = SNew(SSpinBox<int32>)	// Copy limits from PlayNumberOfClients meta data
					.MinValue(1)
					.MaxValue(64)
					.MinSliderValue(1)
					.MaxSliderValue(4)
					.Delta(1)
					.ToolTipText(LOCTEXT("NumberOfClientsToolTip", "How many client instances do you want to create? The first instance respects the Play Mode location (PIE/PINW) and additional instances respect the RunUnderOneProcess setting."))
					.Value_Static(&FInternalPlayWorldCommandCallbacks::GetNumberOfClients)
					.OnValueCommitted_Static(&FInternalPlayWorldCommandCallbacks::SetNumberOfClients);

				Section.AddEntry(FToolMenuEntry::InitWidget("NumPlayers", NumPlayers, LOCTEXT("NumberOfClientsMenuWidget", "Number of Players")));
			}
			// Net Mode
			{
				const UEnum* PlayNetModeEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("EPlayNetMode"));
			
				TSharedRef<SWidget> NetMode = SNew(SEnumComboBox, PlayNetModeEnum)
					.CurrentValue(TAttribute<int32>::Create(TAttribute<int32>::FGetter::CreateStatic(&FInternalPlayWorldCommandCallbacks::GetNetPlayMode)))
					.ButtonStyle(FEditorStyle::Get(), "FlatButton.Light")
					.ContentPadding(FMargin(2, 0))
					.Font(FEditorStyle::GetFontStyle("Sequencer.AnimationOutliner.RegularFont"))
					.OnEnumSelectionChanged(SEnumComboBox::FOnEnumSelectionChanged::CreateStatic(&FInternalPlayWorldCommandCallbacks::SetNetPlayMode))
					.ToolTipText(LOCTEXT("NetworkModeToolTip", "Which network mode should the clients launch in? A server will automatically be started if needed."));

				Section.AddEntry(FToolMenuEntry::InitWidget("NetMode", NetMode, LOCTEXT("NetworkModeMenuWidget", "Net Mode")));
			}
		}

		// settings
		{
			FToolMenuSection& Section = Menu->AddSection("LevelEditorPlaySettings");
			Section.AddMenuEntry(FPlayWorldCommands::Get().PlayInSettings);
		}
	}

	// Get all menu extenders for this context menu from the level editor module
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	TSharedPtr<FExtender> MenuExtender = LevelEditorModule.AssembleExtenders(InCommandList, LevelEditorModule.GetAllLevelEditorToolbarPlayMenuExtenders());
	FToolMenuContext MenuContext(InCommandList, MenuExtender);
	return UToolMenus::Get()->GenerateWidget(MenuName, MenuContext);
}

/*
 * Create an All_<platform>_devices_on_<host> submenu
 * can be extended to any othe All <Platform> aggregate proxy
*/
static void MakeAllDevicesSubMenu(FMenuBuilder& InMenuBuilder, const PlatformInfo::FPlatformInfo* InPlatformInfo, const TSharedPtr<ITargetDeviceProxy> DeviceProxy)
{
	ITargetDeviceServicesModule* TargetDeviceServicesModule = static_cast<ITargetDeviceServicesModule*>(FModuleManager::Get().LoadModule(TEXT("TargetDeviceServices")));
	IProjectTargetPlatformEditorModule& ProjectTargetPlatformEditorModule = FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor");

	TArray<FName> PlatformVariants;
	DeviceProxy->GetVariants(PlatformVariants);
	for (auto It = PlatformVariants.CreateIterator(); It; ++It)
	{
		FName Variant = *It;

		// for an aggregate (All_<platform>_devices_on_<host>) proxy, allow only the "Android_<texture_compression>" variants
		const PlatformInfo::FPlatformInfo* platformInfo = PlatformInfo::FindPlatformInfo(Variant);
		if (DeviceProxy->IsAggregated() && platformInfo != NULL &&
			(Variant == platformInfo->VanillaPlatformName || platformInfo->PlatformType != EBuildTargetType::Game))
		{
			continue;
		}

		FString DeviceListStr;
		bool bVariantHasDevices = false;

		const TSet<FString>& TargetDeviceIds = DeviceProxy->GetTargetDeviceIds(Variant);
		for (TSet<FString>::TConstIterator ItDeviceId(TargetDeviceIds); ItDeviceId; ++ItDeviceId)
		{
			TSharedPtr<ITargetDeviceProxy> PhysicalDeviceProxy = TargetDeviceServicesModule->GetDeviceProxyManager()->FindProxyDeviceForTargetDevice(*ItDeviceId);

			if (PhysicalDeviceProxy.IsValid())
			{
				DeviceListStr.AppendChar('\n');
				DeviceListStr.Append(*PhysicalDeviceProxy->GetName());
				bVariantHasDevices = true;
			}
		}

		if (!bVariantHasDevices)
		{
			continue;
		}

		FString PlatformVariantStr = Variant.ToString();
		FString PlatformId = PlatformVariantStr + TEXT("@") + PlatformVariantStr;

		// create an action
		FUIAction LaunchDeviceAction(
			FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::HandleLaunchOnDeviceActionExecute, PlatformId, PlatformVariantStr),
			FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::HandleLaunchOnDeviceActionCanExecute, PlatformVariantStr),
			FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::HandleLaunchOnDeviceActionIsChecked, PlatformVariantStr)
		);

		// generate display label
		FText Label = FText::FromString(PlatformVariantStr);

		// generate tooltip text with the devices' list
		FFormatNamedArguments TooltipArguments;
		TooltipArguments.Add(TEXT("DeviceList"), FText::FromString(DeviceListStr));
		FText Tooltip = FText::Format(LOCTEXT("LaunchDeviceToolTipText_LaunchOn", "Launch the game on:\n {DeviceList}"), TooltipArguments);

		// add a submenu entry
		InMenuBuilder.AddMenuEntry(
			LaunchDeviceAction,
			ProjectTargetPlatformEditorModule.MakePlatformMenuItemWidget(*InPlatformInfo, true, Label),
			NAME_None,
			Tooltip,
			EUserInterfaceActionType::Check
		);
	}
}

void PopulateLaunchMenu(UToolMenu* Menu)
{
	TArray<PlatformInfo::FVanillaPlatformEntry> VanillaPlatforms = PlatformInfo::BuildPlatformHierarchy(PlatformInfo::EPlatformFilter::All);

	VanillaPlatforms.Sort([](const PlatformInfo::FVanillaPlatformEntry& One, const PlatformInfo::FVanillaPlatformEntry& Two) -> bool
	{
		return One.PlatformInfo->DisplayName.CompareTo(Two.PlatformInfo->DisplayName) < 0;
	});

	// shared devices section
	ITargetDeviceServicesModule* TargetDeviceServicesModule = static_cast<ITargetDeviceServicesModule*>(FModuleManager::Get().LoadModule(TEXT("TargetDeviceServices")));
	IProjectTargetPlatformEditorModule& ProjectTargetPlatformEditorModule = FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor");

	TArray<FString> PlatformsToMaybeInstallLinksFor;
	PlatformsToMaybeInstallLinksFor.Add(TEXT("Android"));
	PlatformsToMaybeInstallLinksFor.Add(TEXT("IOS"));
	PlatformsToMaybeInstallLinksFor.Add(TEXT("Linux"));
	PlatformsToMaybeInstallLinksFor.Add(TEXT("Lumin"));
	TArray<FString> PlatformsToCheckFlavorsFor;
	PlatformsToCheckFlavorsFor.Add(TEXT("Android"));
	PlatformsToCheckFlavorsFor.Add(TEXT("IOS"));
	TArray<FName> PlatformsWithNoDevices;
	TArray<PlatformInfo::FPlatformInfo> PlatformsToAddInstallLinksFor;
	EProjectType ProjectType = FGameProjectGenerationModule::Get().ProjectHasCodeFiles() ? EProjectType::Code : EProjectType::Content;

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorLaunchDevices", LOCTEXT("LaunchButtonDevicesSection", "Devices"));
		for (const PlatformInfo::FVanillaPlatformEntry& VanillaPlatform : VanillaPlatforms)
		{
			// for the Editor we are only interested in launching standalone games
			if (VanillaPlatform.PlatformInfo->PlatformType != EBuildTargetType::Game || !VanillaPlatform.PlatformInfo->bEnabledForUse || !FInstalledPlatformInfo::Get().CanDisplayPlatform(VanillaPlatform.PlatformInfo->BinaryFolderName, ProjectType))
			{
				continue;
			}

			if (VanillaPlatform.PlatformInfo->SDKStatus == PlatformInfo::EPlatformSDKStatus::Installed)
			{
				// for each platform...
				TArray<TSharedPtr<ITargetDeviceProxy>> DeviceProxies;
				// the list of proxies include the "Al_Android" entry
				TargetDeviceServicesModule->GetDeviceProxyManager()->GetAllProxies(VanillaPlatform.PlatformInfo->VanillaPlatformName, DeviceProxies);

				// if this platform had no devices, but we want to show an extra option if not installed right
				if (DeviceProxies.Num() == 0)
				{
					if (PlatformsWithNoDevices.Find(VanillaPlatform.PlatformInfo->VanillaPlatformName) == INDEX_NONE)
					{
						// add an entry with a no devices found
						PlatformsWithNoDevices.Add(VanillaPlatform.PlatformInfo->VanillaPlatformName);
					}
				}
				else
				{
					// for each proxy...
					for (auto DeviceProxyIt = DeviceProxies.CreateIterator(); DeviceProxyIt; ++DeviceProxyIt)
					{
						TSharedPtr<ITargetDeviceProxy> DeviceProxy = *DeviceProxyIt;

						// create an All_<platform>_devices_on_<host> submenu
						if (DeviceProxy->IsAggregated())
						{
							FString AggregateDevicedName(FString::Printf(TEXT("  %s"), *DeviceProxy->GetName())); //align with the other menu entries
							FSlateIcon AggregateDeviceIcon(FEditorStyle::GetStyleSetName(), VanillaPlatform.PlatformInfo->GetIconStyleName(PlatformInfo::EPlatformIconSize::Normal));

							Section.AddSubMenu(
								NAME_None,
								FText::FromString(AggregateDevicedName),
								FText::FromString(AggregateDevicedName),
								FNewMenuDelegate::CreateStatic(&MakeAllDevicesSubMenu, VanillaPlatform.PlatformInfo, DeviceProxy),
								false, AggregateDeviceIcon, true
							);
							continue;
						}

						// ... create an action...
						FUIAction LaunchDeviceAction(
							FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::HandleLaunchOnDeviceActionExecute, DeviceProxy->GetTargetDeviceId(NAME_None), DeviceProxy->GetName()),
							FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::HandleLaunchOnDeviceActionCanExecute, DeviceProxy->GetName()),
							FIsActionChecked::CreateStatic(&FInternalPlayWorldCommandCallbacks::HandleLaunchOnDeviceActionIsChecked, DeviceProxy->GetName())
						);

						// ... generate display label...
						FFormatNamedArguments LabelArguments;
						LabelArguments.Add(TEXT("DeviceName"), FText::FromString(DeviceProxy->GetName()));

						if (!DeviceProxy->IsConnected())
						{
							LabelArguments.Add(TEXT("HostUser"), LOCTEXT("DisconnectedHint", " [Disconnected]"));
						}
						else if (DeviceProxy->GetHostUser() != FPlatformProcess::UserName(false))
						{
							LabelArguments.Add(TEXT("HostUser"), FText::FromString(DeviceProxy->GetHostUser()));
						}
						else
						{
							LabelArguments.Add(TEXT("HostUser"), FText::GetEmpty());
						}

						FText Label = FText::Format(LOCTEXT("LaunchDeviceLabel", "{DeviceName}{HostUser}"), LabelArguments);

						// ... generate tooltip text
						FFormatNamedArguments TooltipArguments;
						TooltipArguments.Add(TEXT("DeviceID"), FText::FromString(DeviceProxy->GetName()));
						TooltipArguments.Add(TEXT("DisplayName"), VanillaPlatform.PlatformInfo->DisplayName);
						FText Tooltip = FText::Format(LOCTEXT("LaunchDeviceToolTipText_ThisDevice", "Launch the game on this {DisplayName} device ({DeviceID})"), TooltipArguments);
						if (!DeviceProxy->IsAuthorized())
						{
							Tooltip = FText::Format(LOCTEXT("LaunchDeviceToolTipText_UnauthorizedOrLocked", "{DisplayName} device ({DeviceID}) is unauthorized or locked"), TooltipArguments);
						}

						FProjectStatus ProjectStatus;
						if (IProjectManager::Get().QueryStatusForCurrentProject(ProjectStatus) && !ProjectStatus.IsTargetPlatformSupported(VanillaPlatform.PlatformInfo->VanillaPlatformName))
						{
							FText TooltipLine2 = FText::Format(LOCTEXT("LaunchDevicePlatformWarning", "{DisplayName} is not listed as a target platform for this project, so may not run as expected."), TooltipArguments);
							Tooltip = FText::Format(FText::FromString(TEXT("{0}\n\n{1}")), Tooltip, TooltipLine2);
						}

						// ... and add a menu entry
						FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitMenuEntry(
							NAME_None,
							LaunchDeviceAction,
							ProjectTargetPlatformEditorModule.MakePlatformMenuItemWidget(*VanillaPlatform.PlatformInfo, true, Label)
						));
						Entry.ToolTip = Tooltip;
						Entry.UserInterfaceActionType = EUserInterfaceActionType::Check;
					}
				}
			}
			else
			{
				// if the platform wasn't installed, we'll add a menu item later (we never care about code in this case, since we don't compile)
				if (PlatformsToMaybeInstallLinksFor.Find(VanillaPlatform.PlatformInfo->VanillaPlatformName.ToString()) != INDEX_NONE)
				{
					PlatformsToAddInstallLinksFor.Add(*(VanillaPlatform.PlatformInfo));
				}
			}
		}
	}

	TWeakObjectPtr<UCookerSettings> CookerSettings = GetMutableDefault<UCookerSettings>();

	{
		FToolMenuSection& Section = Menu->AddSection("CookerSettings");

		FUIAction UIAction;
		UIAction.ExecuteAction = FExecuteAction::CreateLambda([CookerSettings]
		{
			CookerSettings->bCookOnTheFlyForLaunchOn = !CookerSettings->bCookOnTheFlyForLaunchOn;
			CookerSettings->Modify(true);

			// Update source control

			FString ConfigPath = FPaths::ConvertRelativePathToFull(CookerSettings->GetDefaultConfigFilename());

			if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*ConfigPath))
			{
				if (ISourceControlModule::Get().IsEnabled())
				{
					FText ErrorMessage;

					if (!SourceControlHelpers::CheckoutOrMarkForAdd(ConfigPath, FText::FromString(ConfigPath), NULL, ErrorMessage))
					{
						FNotificationInfo Info(ErrorMessage);
						Info.ExpireDuration = 3.0f;
						FSlateNotificationManager::Get().AddNotification(Info);
					}
				}
				else
				{
					if (!FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ConfigPath, false))
					{
						FNotificationInfo Info(FText::Format(LOCTEXT("FailedToMakeWritable", "Could not make {0} writable."), FText::FromString(ConfigPath)));
						Info.ExpireDuration = 3.0f;
						FSlateNotificationManager::Get().AddNotification(Info);
					}
				}
			}

			// Save settings
			CookerSettings->UpdateSinglePropertyInConfigFile(CookerSettings->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UCookerSettings, bCookOnTheFlyForLaunchOn)), CookerSettings->GetDefaultConfigFilename());
		});

		UIAction.GetActionCheckState = FGetActionCheckState::CreateLambda([CookerSettings]
		{
			return CookerSettings->bCookOnTheFlyForLaunchOn ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		});

		Section.AddMenuEntry(
			"CookOnTheFlyOnLaunch",
			LOCTEXT("CookOnTheFlyOnLaunch", "Enable cooking on the fly"),
			LOCTEXT("CookOnTheFlyOnLaunchDescription", "Cook on the fly instead of cooking upfront when launching"),
			FSlateIcon(),
			UIAction,
			EUserInterfaceActionType::ToggleButton
		);
	}

	if (PlatformsWithNoDevices.Num() > 0)
	{
		{
			FToolMenuSection& Section = Menu->AddSection("NoDevices");
			for (int32 PlatformIndex = 0; PlatformIndex < PlatformsWithNoDevices.Num(); PlatformIndex++)
			{
				const PlatformInfo::FPlatformInfo* PlatformInfo = PlatformInfo::FindVanillaPlatformInfo(PlatformsWithNoDevices[PlatformIndex]);

				// ... generate display label...
				FFormatNamedArguments LabelArguments;
				LabelArguments.Add(TEXT("DisplayName"), PlatformInfo->DisplayName);

				FText Label = FText::Format(LOCTEXT("NoDeviceLabel", "{DisplayName} - No Devices Found"), LabelArguments);

				// ... create an action...
				FUIAction NoDeviceAction(
					FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::HandleNoDeviceFoundActionExecute),
					FCanExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::HandleNoDeviceFoundActionCanExecute)
				);

				// ... generate tooltip text
				FFormatNamedArguments TooltipArguments;
				TooltipArguments.Add(TEXT("DisplayName"), PlatformInfo->DisplayName);
				FText Tooltip = FText::Format(LOCTEXT("LaunchNoDevicesToolTipText", "Found no connected devices for {DisplayName}"), TooltipArguments);

				// ... and add a menu entry
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitMenuEntry(
					NAME_None,
					NoDeviceAction,
					ProjectTargetPlatformEditorModule.MakePlatformMenuItemWidget(*PlatformInfo, true, Label)
				));
				Entry.ToolTip = Tooltip;
				Entry.UserInterfaceActionType = EUserInterfaceActionType::Check;
			}
		}
	}

	// tip section
	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorLaunchHint");
		Section.AddEntry(FToolMenuEntry::InitWidget(
			"LevelEditorLaunchHint",
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("ZoomToFitHorizontal", "Launching a game on a different device will change your default 'Launch' device in the toolbar"))
			.WrapTextAt(250),
			FText::GetEmpty()
		));
	}

	if (PlatformsToAddInstallLinksFor.Num() > 0)
	{
		{
			FToolMenuSection& Section = Menu->AddSection("SDKUninstalledTutorials");
			for (int32 PlatformIndex = 0; PlatformIndex < PlatformsToAddInstallLinksFor.Num(); PlatformIndex++)
			{
				const PlatformInfo::FPlatformInfo& Platform = PlatformsToAddInstallLinksFor[PlatformIndex];

				FUIAction Action(FExecuteAction::CreateStatic(&FInternalPlayWorldCommandCallbacks::HandleShowSDKTutorial, Platform.DisplayName.ToString(), Platform.SDKTutorial));

				FFormatNamedArguments LabelArguments;
				LabelArguments.Add(TEXT("PlatformName"), Platform.DisplayName);
				FText Label = FText::Format(LOCTEXT("LaunchPlatformLabel", "{PlatformName} Support"), LabelArguments);


				Section.AddMenuEntry(
					NAME_None,
					Label,
					LOCTEXT("PlatformSDK", "Show information on setting up the platform tools"),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.BrowseDocumentation"),
					Action,
					EUserInterfaceActionType::Button);
			}
		}
	}

	// options section
	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorLaunchOptions");
		Section.AddMenuEntry(FPlayWorldCommands::Get().OpenProjectLauncher,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Launcher.TabIcon")
		);

		Section.AddMenuEntry(FPlayWorldCommands::Get().OpenDeviceManager,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "DeviceDetails.TabIcon")
		);

		Section.AddDynamicEntry("OpenProjectTargetPlatform", FNewToolMenuDelegateLegacy::CreateLambda([](FMenuBuilder& MenuBuilder, UToolMenu* ToolMenu)
		{
			FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor").AddOpenProjectTargetPlatformEditorMenuItem(MenuBuilder);
		}));
	}
}

TSharedRef< SWidget > FPlayWorldCommands::GenerateLaunchMenuContent(TSharedRef<FUICommandList> InCommandList)
{
	static const FName MenuName("UnrealEd.PlayWorldCommands.LaunchMenu");

	if (!UToolMenus::Get()->IsMenuRegistered(MenuName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(MenuName);
		Menu->AddDynamicSection("DynamicSection", FNewToolMenuDelegate::CreateStatic(&PopulateLaunchMenu));
	}

	FToolMenuContext MenuContext(InCommandList);
	return UToolMenus::Get()->GenerateWidget(MenuName, MenuContext);
}


//////////////////////////////////////////////////////////////////////////
// FPlayWorldCommandCallbacks

void FPlayWorldCommandCallbacks::StartPlayFromHere()
{
	// Is a PIE session already running?  If so we close it first
	if (GUnrealEd->PlayWorld != NULL)
	{
		GUnrealEd->EndPlayMap();
	}

	FRequestPlaySessionParams SessionParams;

	UClass* const PlayerStartClass = GUnrealEd->PlayFromHerePlayerStartClass ? (UClass*)GUnrealEd->PlayFromHerePlayerStartClass : APlayerStart::StaticClass();

	// Figure out the start location of the player
	UCapsuleComponent*	DefaultCollisionComponent = CastChecked<UCapsuleComponent>(PlayerStartClass->GetDefaultObject<AActor>()->GetRootComponent());
	FVector	CollisionExtent = FVector(DefaultCollisionComponent->GetScaledCapsuleRadius(), DefaultCollisionComponent->GetScaledCapsuleRadius(), DefaultCollisionComponent->GetScaledCapsuleHalfHeight());
	SessionParams.StartLocation = GEditor->UnsnappedClickLocation + GEditor->ClickPlane * (FVector::BoxPushOut(GEditor->ClickPlane, CollisionExtent) + 0.1f);

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");

	TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveViewport();


	if (ActiveLevelViewport.IsValid() && ActiveLevelViewport->GetAssetViewportClient().IsPerspective())
	{
		// If there is no level viewport, a new window will be spawned to play in.
		SessionParams.DestinationSlateViewport = ActiveLevelViewport;
		SessionParams.StartRotation = ActiveLevelViewport->GetAssetViewportClient().GetViewRotation();
	}

	GUnrealEd->RequestPlaySession(SessionParams);
}


void FPlayWorldCommandCallbacks::ResumePlaySession_Clicked()
{
	if (HasPlayWorld())
	{
		LeaveDebuggingMode();
		GUnrealEd->PlaySessionResumed();
		uint32 UserIndex = 0;
		FSlateApplication::Get().SetUserFocusToGameViewport(UserIndex);
	}
}


void FPlayWorldCommandCallbacks::PausePlaySession_Clicked()
{
	if (HasPlayWorld())
	{
		GUnrealEd->PlayWorld->bDebugPauseExecution = true;
		GUnrealEd->PlaySessionPaused();
		if (IsInPIE()) {
			FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
			FSlateApplication::Get().ResetToDefaultInputSettings();

			TWeakPtr<SGlobalPlayWorldActions> ActiveGlobalPlayWorldWidget = FPlayWorldCommands::GetActiveGlobalPlayWorldActionsWidget();
			if (ActiveGlobalPlayWorldWidget.IsValid())
			{
				uint32 UserIndex = 0;
				FSlateApplication::Get().SetUserFocus(UserIndex, ActiveGlobalPlayWorldWidget.Pin());
			}
		}
	}
}


void FPlayWorldCommandCallbacks::SingleFrameAdvance_Clicked()
{
	if (HasPlayWorld())
	{
		FInternalPlayWorldCommandCallbacks::SingleFrameAdvance_Clicked();
	}
}

bool FPlayWorldCommandCallbacks::IsInSIE()
{
	return GEditor->bIsSimulatingInEditor;
}


bool FPlayWorldCommandCallbacks::IsInPIE()
{
	return (GEditor->PlayWorld != NULL) && (!GEditor->bIsSimulatingInEditor);
}


bool FPlayWorldCommandCallbacks::IsInSIE_AndRunning()
{
	return IsInSIE() && ((GEditor->PlayWorld == NULL) || !(GEditor->PlayWorld->bDebugPauseExecution));
}


bool FPlayWorldCommandCallbacks::IsInPIE_AndRunning()
{
	return IsInPIE() && ((GEditor->PlayWorld == NULL) || !(GEditor->PlayWorld->bDebugPauseExecution));
}


bool FPlayWorldCommandCallbacks::HasPlayWorld()
{
	return GEditor->PlayWorld != NULL;
}


bool FPlayWorldCommandCallbacks::HasPlayWorldAndPaused()
{
	return HasPlayWorld() && GUnrealEd->PlayWorld->bDebugPauseExecution;
}


bool FPlayWorldCommandCallbacks::HasPlayWorldAndRunning()
{
	return HasPlayWorld() && !GUnrealEd->PlayWorld->bDebugPauseExecution;
}


//////////////////////////////////////////////////////////////////////////
// FInternalPlayWorldCommandCallbacks

FText FInternalPlayWorldCommandCallbacks::GetPossessEjectLabel()
{
	if (IsInPIE())
	{
		return LOCTEXT("EjectLabel", "Eject");
	}
	else if (IsInSIE())
	{
		return LOCTEXT("PossessLabel", "Possess");
	}
	else
	{
		return LOCTEXT("ToggleBetweenPieAndSIELabel", "Toggle Between PIE and SIE");
	}
}


FText FInternalPlayWorldCommandCallbacks::GetPossessEjectTooltip()
{
	if (IsInPIE())
	{
		return LOCTEXT("EjectToolTip", "Detaches from the player controller, allowing regular editor controls");
	}
	else if (IsInSIE())
	{
		return LOCTEXT("PossessToolTip", "Attaches to the player controller, allowing normal gameplay controls");
	}
	else
	{
		return LOCTEXT("ToggleBetweenPieAndSIEToolTip", "Toggles the current play session between play in editor and simulate in editor");
	}
}


FSlateIcon FInternalPlayWorldCommandCallbacks::GetPossessEjectImage()
{
	if (IsInPIE())
	{
		return FSlateIcon(FEditorStyle::GetStyleSetName(), "PlayWorld.EjectFromPlayer");
	}
	else if (IsInSIE())
	{
		return FSlateIcon(FEditorStyle::GetStyleSetName(), "PlayWorld.PossessPlayer");
	}
	else
	{
		return FSlateIcon();
	}
}


bool FInternalPlayWorldCommandCallbacks::CanLateJoin()
{
	return HasPlayWorld();
}

bool FInternalPlayWorldCommandCallbacks::CanShowLateJoinButton()
{
	return GetDefault<UEditorExperimentalSettings>()->bAllowLateJoinInPIE && HasPlayWorld();
}

void SetLastExecutedPlayMode(EPlayModeType PlayMode)
{
	ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();
	PlaySettings->LastExecutedPlayModeType = PlayMode;

	FPropertyChangedEvent PropChangeEvent(ULevelEditorPlaySettings::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(ULevelEditorPlaySettings, LastExecutedPlayModeType)));
	PlaySettings->PostEditChangeProperty(PropChangeEvent);

	PlaySettings->SaveConfig();
}


void FInternalPlayWorldCommandCallbacks::Simulate_Clicked()
{
	// Is a simulation session already running?  If so, do nothing
	if (HasPlayWorld() && GUnrealEd->bIsSimulatingInEditor)
	{
		return;
	}

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));

	TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveViewport();
	if (ActiveLevelViewport.IsValid())
	{
		// Start a new simulation session!
		if (!HasPlayWorld())
		{
			if (FEngineAnalytics::IsAvailable())
			{
				FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.SimulateInEditor"));
			}
			SetLastExecutedPlayMode(PlayMode_Simulate);
			FRequestPlaySessionParams SessionParams;
			SessionParams.WorldType = EPlaySessionWorldType::SimulateInEditor;
			SessionParams.DestinationSlateViewport = ActiveLevelViewport;

			GUnrealEd->RequestPlaySession(SessionParams);
		}
		else if (ActiveLevelViewport->HasPlayInEditorViewport())
		{
			GUnrealEd->RequestToggleBetweenPIEandSIE();
		}
	}
}


bool FInternalPlayWorldCommandCallbacks::Simulate_CanExecute()
{
	// Can't simulate while already simulating; PIE is fine as we toggle to simulate
	return !(HasPlayWorld() && GUnrealEd->bIsSimulatingInEditor) && !GEditor->IsLightingBuildCurrentlyRunning();
}


bool FInternalPlayWorldCommandCallbacks::Simulate_IsChecked()
{
	return HasPlayWorld() && GUnrealEd->bIsSimulatingInEditor;
}


const TSharedRef < FUICommandInfo > GetLastPlaySessionCommand()
{
	const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();

	const FPlayWorldCommands& Commands = FPlayWorldCommands::Get();
	TSharedRef < FUICommandInfo > Command = Commands.PlayInViewport.ToSharedRef();

	switch (PlaySettings->LastExecutedPlayModeType)
	{
	case PlayMode_InViewPort:
		Command = Commands.PlayInViewport.ToSharedRef();
		break;

	case PlayMode_InEditorFloating:
		Command = Commands.PlayInEditorFloating.ToSharedRef();
		break;

	case PlayMode_InMobilePreview:
		Command = Commands.PlayInMobilePreview.ToSharedRef();
		break;

	case PlayMode_InTargetedMobilePreview:
	{
		// Scan through targeted mobile preview commands to find our match.
		for (auto PreviewerCommand : Commands.PlayInTargetedMobilePreviewDevices)
		{
			FName LastExecutedPIEPreviewDevice = FName(*PlaySettings->LastExecutedPIEPreviewDevice);
			if (PreviewerCommand->GetCommandName() == LastExecutedPIEPreviewDevice)
			{
				Command = PreviewerCommand.ToSharedRef();
				break;
			}
		}
		break;
	}

	case PlayMode_InVulkanPreview:
		Command = Commands.PlayInVulkanPreview.ToSharedRef();
		break;

	case PlayMode_InNewProcess:
		Command = Commands.PlayInNewProcess.ToSharedRef();
		break;

	case PlayMode_InVR:
		Command = Commands.PlayInVR.ToSharedRef();
		break;

	case PlayMode_Simulate:
		Command = Commands.Simulate.ToSharedRef();
	}

	return Command;
}



/** Report PIE usage to engine analytics */
void RecordLastExecutedPlayMode()
{
	if (FEngineAnalytics::IsAvailable())
	{
		const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();

		// play location
		FString PlayLocationString;

		switch (PlaySettings->LastExecutedPlayModeLocation)
		{
		case PlayLocation_CurrentCameraLocation:
			PlayLocationString = TEXT("CurrentCameraLocation");
			break;

		case PlayLocation_DefaultPlayerStart:
			PlayLocationString = TEXT("DefaultPlayerStart");
			break;

		default:
			PlayLocationString = TEXT("<UNKNOWN>");
		}

		// play mode
		FString PlayModeString;

		switch (PlaySettings->LastExecutedPlayModeType)
		{
		case PlayMode_InViewPort:
			PlayModeString = TEXT("InViewPort");
			break;

		case PlayMode_InEditorFloating:
			PlayModeString = TEXT("InEditorFloating");
			break;

		case PlayMode_InMobilePreview:
			PlayModeString = TEXT("InMobilePreview");
			break;

		case PlayMode_InTargetedMobilePreview:
			PlayModeString = TEXT("InTargetedMobilePreview");
			break;

		case PlayMode_InVulkanPreview:
			PlayModeString = TEXT("InVulkanPreview");
			break;

		case PlayMode_InNewProcess:
			PlayModeString = TEXT("InNewProcess");
			break;

		case PlayMode_InVR:
			PlayModeString = TEXT("InVR");
			break;

		case PlayMode_Simulate:
			PlayModeString = TEXT("Simulate");
			break;

		default:
			PlayModeString = TEXT("<UNKNOWN>");
		}

		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.PIE"), TEXT("PlayLocation"), PlayLocationString);
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.PIE"), TEXT("PlayMode"), PlayModeString);
	}
}


void SetLastExecutedLaunchMode(ELaunchModeType LaunchMode)
{
	ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();
	PlaySettings->LastExecutedLaunchModeType = LaunchMode;

	PlaySettings->PostEditChange();

	PlaySettings->SaveConfig();
}


void FInternalPlayWorldCommandCallbacks::RepeatLastPlay_Clicked()
{
	// Let a game have a go at settings before we play
	ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();
	PlaySettings->PostEditChange();

	// Grab the play command and execute it
	TSharedRef<FUICommandInfo> LastCommand = GetLastPlaySessionCommand();
	UE_LOG(LogTemp, Log, TEXT("Repeating last play command: %s"), *LastCommand->GetLabel().ToString());

	FPlayWorldCommands::GlobalPlayWorldActions->ExecuteAction(LastCommand);
}


bool FInternalPlayWorldCommandCallbacks::RepeatLastPlay_CanExecute()
{
	return FPlayWorldCommands::GlobalPlayWorldActions->CanExecuteAction(GetLastPlaySessionCommand());
}


FText FInternalPlayWorldCommandCallbacks::GetRepeatLastPlayToolTip()
{
	return GetLastPlaySessionCommand()->GetDescription();
}


FSlateIcon FInternalPlayWorldCommandCallbacks::GetRepeatLastPlayIcon()
{
	return GetLastPlaySessionCommand()->GetIcon();
}


void FInternalPlayWorldCommandCallbacks::PlayInViewport_Clicked()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));

	/** Set PlayInViewPort as the last executed play command */
	const FPlayWorldCommands& Commands = FPlayWorldCommands::Get();

	SetLastExecutedPlayMode(PlayMode_InViewPort);

	RecordLastExecutedPlayMode();

	TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveViewport();

	const bool bAtPlayerStart = (GetPlayModeLocation() == PlayLocation_DefaultPlayerStart);

	FRequestPlaySessionParams SessionParams;

	// Make sure we can find a path to the view port.  This will fail in cases where the view port widget
	// is in a backgrounded tab, etc.  We can't currently support starting PIE in a backgrounded tab
	// due to how PIE manages focus and requires event forwarding from the application.
	if (ActiveLevelViewport.IsValid() && FSlateApplication::Get().FindWidgetWindow(ActiveLevelViewport->AsWidget()).IsValid())
	{
		SessionParams.DestinationSlateViewport = ActiveLevelViewport;
		if (!bAtPlayerStart)
		{
			// Start the player where the camera is if not forcing from player start
			SessionParams.StartLocation = ActiveLevelViewport->GetAssetViewportClient().GetViewLocation();
			SessionParams.StartRotation = ActiveLevelViewport->GetAssetViewportClient().GetViewRotation();
		}
	}

	if (!HasPlayWorld())
	{
		// If there is an active level view port, play the game in it, otherwise make a new window.
		GUnrealEd->RequestPlaySession(SessionParams);
	}
	else
	{
		// There is already a play world active which means simulate in editor is happening
		// Toggle to PIE
		check(!GIsPlayInEditorWorld);
		GUnrealEd->RequestToggleBetweenPIEandSIE();
	}
}

bool FInternalPlayWorldCommandCallbacks::PlayInViewport_CanExecute()
{
	// Disallow PIE when compiling in the editor
	if (GEditor->bIsCompiling)
	{
		return false;
	}

	// Allow PIE if we don't already have a play session or the play session is simulate in editor (which we can toggle to PIE)
	return (!GEditor->IsPlaySessionInProgress() && !HasPlayWorld() && !GEditor->IsLightingBuildCurrentlyRunning()) || GUnrealEd->IsSimulateInEditorInProgress();
}


void FInternalPlayWorldCommandCallbacks::PlayInEditorFloating_Clicked()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));

	SetLastExecutedPlayMode(PlayMode_InEditorFloating);

	FRequestPlaySessionParams SessionParams;

	// Is a PIE session already running?  If not, then we'll kick off a new one
	if (!HasPlayWorld())
	{
		RecordLastExecutedPlayMode();

		const bool bAtPlayerStart = (GetPlayModeLocation() == PlayLocation_DefaultPlayerStart);
		if (!bAtPlayerStart)
		{
			TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveViewport();

			// Make sure we can find a path to the view port.  This will fail in cases where the view port widget
			// is in a backgrounded tab, etc.  We can't currently support starting PIE in a backgrounded tab
			// due to how PIE manages focus and requires event forwarding from the application.
			if (ActiveLevelViewport.IsValid() &&
				FSlateApplication::Get().FindWidgetWindow(ActiveLevelViewport->AsWidget()).IsValid())
			{
				// Start the player where the camera is if not forcing from player start
				SessionParams.StartLocation = ActiveLevelViewport->GetAssetViewportClient().GetViewLocation();
				SessionParams.StartRotation = ActiveLevelViewport->GetAssetViewportClient().GetViewRotation();
			}
		}

		// Spawn a new window to play in.
		GUnrealEd->RequestPlaySession(SessionParams);
	}
	else
	{
		// Terminate existing session.  This is deferred because we could be processing this from the play world and we should not clear the play world while in it.
		GUnrealEd->RequestEndPlayMap();
	}
}


bool FInternalPlayWorldCommandCallbacks::PlayInEditorFloating_CanExecute()
{
	return (!HasPlayWorld() || !GUnrealEd->bIsSimulatingInEditor) && !GEditor->IsLightingBuildCurrentlyRunning();
}

void FInternalPlayWorldCommandCallbacks::PlayInVR_Clicked()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));

	SetLastExecutedPlayMode(PlayMode_InVR);
	FRequestPlaySessionParams SessionParams;

	// Is a PIE session already running?  If not, then we'll kick off a new one
	if (!HasPlayWorld())
	{
		RecordLastExecutedPlayMode();

		const bool bAtPlayerStart = (GetPlayModeLocation() == PlayLocation_DefaultPlayerStart);
		if (!bAtPlayerStart)
		{
			TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveViewport();

			// Make sure we can find a path to the view port.  This will fail in cases where the view port widget
			// is in a backgrounded tab, etc.  We can't currently support starting PIE in a backgrounded tab
			// due to how PIE manages focus and requires event forwarding from the application.
			if (ActiveLevelViewport.IsValid() &&
				FSlateApplication::Get().FindWidgetWindow(ActiveLevelViewport->AsWidget()).IsValid())
			{
				// Start the player where the camera is if not forcing from player start
				SessionParams.StartLocation = ActiveLevelViewport->GetAssetViewportClient().GetViewLocation();
				SessionParams.StartRotation = ActiveLevelViewport->GetAssetViewportClient().GetViewRotation();
			}
		}

		const bool bHMDIsReady = (GEngine && GEngine->XRSystem.IsValid() && GEngine->XRSystem->GetHMDDevice() && GEngine->XRSystem->GetHMDDevice()->IsHMDConnected());
		if (bHMDIsReady)
		{
			SessionParams.SessionPreviewTypeOverride = EPlaySessionPreviewType::VRPreview;
		}

		// Spawn a new window to play in.
		GUnrealEd->RequestPlaySession(SessionParams);
	}
}


bool FInternalPlayWorldCommandCallbacks::PlayInVR_CanExecute()
{
	return (!HasPlayWorld() || !GUnrealEd->bIsSimulatingInEditor) && !GEditor->IsLightingBuildCurrentlyRunning() && GEngine && GEngine->XRSystem.IsValid();
}

void SetLastExecutedPIEPreviewDevice(FString PIEPreviewDevice)
{
	ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();
	PlaySettings->LastExecutedPIEPreviewDevice = PIEPreviewDevice;
	FPropertyChangedEvent PropChangeEvent(ULevelEditorPlaySettings::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(ULevelEditorPlaySettings, LastExecutedPIEPreviewDevice)));
	PlaySettings->PostEditChangeProperty(PropChangeEvent);
	PlaySettings->SaveConfig();
}

void FInternalPlayWorldCommandCallbacks::PlayInNewProcessPreviewDevice_Clicked(FString PIEPreviewDeviceName)
{
	SetLastExecutedPIEPreviewDevice(PIEPreviewDeviceName);
	PlayInNewProcess_Clicked(PlayMode_InTargetedMobilePreview);
}

void FInternalPlayWorldCommandCallbacks::PlayInNewProcess_Clicked(EPlayModeType PlayModeType)
{
	check(PlayModeType == PlayMode_InNewProcess || PlayModeType == PlayMode_InMobilePreview
		|| PlayModeType == PlayMode_InTargetedMobilePreview || PlayModeType == PlayMode_InVulkanPreview);

	SetLastExecutedPlayMode(PlayModeType);
	FRequestPlaySessionParams SessionParams;

	if (!HasPlayWorld())
	{
		RecordLastExecutedPlayMode();

		const bool bAtPlayerStart = (GetPlayModeLocation() == PlayLocation_DefaultPlayerStart);
		if (!bAtPlayerStart)
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
			TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveViewport();

			if (ActiveLevelViewport.IsValid() && FSlateApplication::Get().FindWidgetWindow(ActiveLevelViewport->AsWidget()).IsValid())
			{
				SessionParams.StartLocation = ActiveLevelViewport->GetAssetViewportClient().GetViewLocation();
				SessionParams.StartRotation = ActiveLevelViewport->GetAssetViewportClient().GetViewRotation();
			}
		}

		if (PlayModeType == PlayMode_InMobilePreview || PlayModeType == PlayMode_InTargetedMobilePreview)
		{
			if (PlayModeType == PlayMode_InTargetedMobilePreview)
			{
				SessionParams.MobilePreviewTargetDevice = GetDefault<ULevelEditorPlaySettings>()->LastExecutedPIEPreviewDevice;
			}

			SessionParams.SessionPreviewTypeOverride = EPlaySessionPreviewType::MobilePreview;
		}
		else if (PlayModeType == PlayMode_InVulkanPreview)
		{
			SessionParams.SessionPreviewTypeOverride = EPlaySessionPreviewType::VulkanPreview;
		}

		SessionParams.SessionDestination = EPlaySessionDestinationType::NewProcess;

		// Spawn a new window to play in.
		GUnrealEd->RequestPlaySession(SessionParams);
	}
	else
	{
		GUnrealEd->EndPlayMap();
	}
}


bool FInternalPlayWorldCommandCallbacks::PlayInNewProcess_CanExecute()
{
	return true;
}


bool FInternalPlayWorldCommandCallbacks::PlayInModeAndPreviewDeviceIsChecked(FString PIEPreviewDeviceName)
{
	return PlayInModeIsChecked(PlayMode_InTargetedMobilePreview) && GetDefault<ULevelEditorPlaySettings>()->LastExecutedPIEPreviewDevice == PIEPreviewDeviceName;
}

bool FInternalPlayWorldCommandCallbacks::PlayInModeIsChecked(EPlayModeType PlayMode)
{
	return (PlayMode == GetDefault<ULevelEditorPlaySettings>()->LastExecutedPlayModeType);
}


bool FInternalPlayWorldCommandCallbacks::PlayInLocation_CanExecute(EPlayModeLocations Location)
{
	switch (Location)
	{
	case PlayLocation_CurrentCameraLocation:
		return true;

	case PlayLocation_DefaultPlayerStart:
		return (GEditor->CheckForPlayerStart() != nullptr);

	default:
		return false;
	}
}


void FInternalPlayWorldCommandCallbacks::PlayInLocation_Clicked(EPlayModeLocations Location)
{
	ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();
	PlaySettings->LastExecutedPlayModeLocation = Location;
	PlaySettings->PostEditChange();
	PlaySettings->SaveConfig();
}


bool FInternalPlayWorldCommandCallbacks::PlayInLocation_IsChecked(EPlayModeLocations Location)
{
	switch (Location)
	{
	case PlayLocation_CurrentCameraLocation:
		return ((GetDefault<ULevelEditorPlaySettings>()->LastExecutedPlayModeLocation == PlayLocation_CurrentCameraLocation) || (GEditor->CheckForPlayerStart() == nullptr));

	case PlayLocation_DefaultPlayerStart:
		return ((GetDefault<ULevelEditorPlaySettings>()->LastExecutedPlayModeLocation == PlayLocation_DefaultPlayerStart) && (GEditor->CheckForPlayerStart() != nullptr));
	}

	return false;
}


void FInternalPlayWorldCommandCallbacks::PlayInSettings_Clicked()
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Editor", "LevelEditor", "PlayIn");
}

void FInternalPlayWorldCommandCallbacks::OpenProjectLauncher_Clicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FTabId("ProjectLauncher"));
}

void FInternalPlayWorldCommandCallbacks::OpenDeviceManager_Clicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FTabId("DeviceManager"));
}

void FInternalPlayWorldCommandCallbacks::RepeatLastLaunch_Clicked()
{
	const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();

	switch (PlaySettings->LastExecutedLaunchModeType)
	{
	case LaunchMode_OnDevice:
		if (IsReadyToLaunchOnDevice(PlaySettings->LastExecutedLaunchDevice))
		{
			LaunchOnDevice(PlaySettings->LastExecutedLaunchDevice, PlaySettings->LastExecutedLaunchName);
		}
		break;

	default:
		break;
	}
}


bool FInternalPlayWorldCommandCallbacks::RepeatLastLaunch_CanExecute()
{
	const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();

	switch (PlaySettings->LastExecutedLaunchModeType)
	{
	case LaunchMode_OnDevice:
		return CanLaunchOnDevice(PlaySettings->LastExecutedLaunchName);

	default:
		return false;
	}
}


FText FInternalPlayWorldCommandCallbacks::GetRepeatLastLaunchToolTip()
{
	const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();

	switch (PlaySettings->LastExecutedLaunchModeType)
	{
	case LaunchMode_OnDevice:
		if (CanLaunchOnDevice(PlaySettings->LastExecutedLaunchName))
		{
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("DeviceName"), FText::FromString(PlaySettings->LastExecutedLaunchName));

			return FText::Format(LOCTEXT("RepeatLaunchTooltip", "Launch this level on {DeviceName}"), Arguments);
		}

		break;

	default:
		break;
	}

	return LOCTEXT("RepeatLaunchSelectOptionToolTip", "Select a play-on target from the combo menu");
}


FSlateIcon FInternalPlayWorldCommandCallbacks::GetRepeatLastLaunchIcon()
{
	const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();

	// @todo gmp: add play mode specific icons
	switch (PlaySettings->LastExecutedLaunchModeType)
	{
	case LaunchMode_OnDevice:
		break;
	}

	static FName RepeatLastLaunchIcon("PlayWorld.RepeatLastLaunch");

	return FSlateIcon(FEditorStyle::GetStyleSetName(), RepeatLastLaunchIcon);
}

bool FInternalPlayWorldCommandCallbacks::IsReadyToLaunchOnDevice(FString DeviceId)
{
	int32 Index = 0;
	DeviceId.FindChar(TEXT('@'), Index);
	FString PlatformName = DeviceId.Left(Index);

	const PlatformInfo::FPlatformInfo* const PlatformInfo = PlatformInfo::FindPlatformInfo(FName(*PlatformName));
	checkf(PlatformInfo, TEXT("Unable to find PlatformInfo for %s"), *PlatformName);

	FGameProjectGenerationModule& GameProjectModule = FModuleManager::LoadModuleChecked<FGameProjectGenerationModule>(TEXT("GameProjectGeneration"));
	bool bHasCode = GameProjectModule.Get().ProjectHasCodeFiles();

	if (PlatformInfo->SDKStatus == PlatformInfo::EPlatformSDKStatus::NotInstalled)
	{
		IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		MainFrameModule.BroadcastMainFrameSDKNotInstalled(PlatformInfo->TargetPlatformName.ToString(), PlatformInfo->SDKTutorial);
		TArray<FAnalyticsEventAttribute> ParamArray;
		ParamArray.Add(FAnalyticsEventAttribute(TEXT("Time"), 0.0));
		FEditorAnalytics::ReportEvent(TEXT("Editor.LaunchOn.Failed"), PlatformInfo->TargetPlatformName.ToString(), bHasCode, EAnalyticsErrorCodes::SDKNotFound, ParamArray);
		return false;
	}

	const ITargetPlatform* Platform = GetTargetPlatformManager()->FindTargetPlatform(PlatformName);
	if (Platform)
	{
		FString NotInstalledTutorialLink;
		FString DocumentationLink;
		FText CustomizedLogMessage;

		EBuildConfiguration BuildConfiguration = GetDefault<ULevelEditorPlaySettings>()->GetLaunchBuildConfiguration();
		bool bEnableAssetNativization = false;
		int32 Result = Platform->CheckRequirements(bHasCode, BuildConfiguration, bEnableAssetNativization, NotInstalledTutorialLink, DocumentationLink, CustomizedLogMessage);

		// report to analytics
		FEditorAnalytics::ReportBuildRequirementsFailure(TEXT("Editor.LaunchOn.Failed"), PlatformName, bHasCode, Result);

		// report to message log
		bool UnrecoverableError = false;

		if ((Result & ETargetPlatformReadyStatus::SDKNotFound) != 0)
		{
			AddMessageLog(
				LOCTEXT("SdkNotFoundMessage", "Software Development Kit (SDK) not found."),
				CustomizedLogMessage.IsEmpty() ? FText::Format(LOCTEXT("SdkNotFoundMessageDetail", "Please install the SDK for the {0} target platform!"), Platform->DisplayName()) : CustomizedLogMessage,
				NotInstalledTutorialLink,
				DocumentationLink
			);

			UnrecoverableError = true;
		}

		if ((Result & ETargetPlatformReadyStatus::LicenseNotAccepted) != 0)
		{
			AddMessageLog(
				LOCTEXT("LicenseNotAcceptedMessage", "License not accepted."),
				CustomizedLogMessage.IsEmpty() ? LOCTEXT("LicenseNotAcceptedMessageDetail", "License must be accepted in project settings to deploy your app to the device.") : CustomizedLogMessage,
				NotInstalledTutorialLink,
				DocumentationLink
			);

			UnrecoverableError = true;
		}

		if ((Result & ETargetPlatformReadyStatus::ProvisionNotFound) != 0)
		{
			AddMessageLog(
				LOCTEXT("ProvisionNotFoundMessage", "Provision not found."),
				CustomizedLogMessage.IsEmpty() ? LOCTEXT("ProvisionNotFoundMessageDetail", "A provision is required for deploying your app to the device.") : CustomizedLogMessage,
				NotInstalledTutorialLink,
				DocumentationLink
			);

			UnrecoverableError = true;
		}

		if ((Result & ETargetPlatformReadyStatus::SigningKeyNotFound) != 0)
		{
			AddMessageLog(
				LOCTEXT("SigningKeyNotFoundMessage", "Signing key not found."),
				CustomizedLogMessage.IsEmpty() ? LOCTEXT("SigningKeyNotFoundMessageDetail", "The app could not be digitally signed, because the signing key is not configured.") : CustomizedLogMessage,
				NotInstalledTutorialLink,
				DocumentationLink
			);

			UnrecoverableError = true;
		}

		if ((Result & ETargetPlatformReadyStatus::ManifestNotFound) != 0)
		{
			AddMessageLog(
				LOCTEXT("ManifestNotFound", "Manifest not found."),
				CustomizedLogMessage.IsEmpty() ? LOCTEXT("ManifestNotFoundMessageDetail", "The generated application manifest could not be found.") : CustomizedLogMessage,
				NotInstalledTutorialLink,
				DocumentationLink
			);

			UnrecoverableError = true;
		}

		if ((Result & ETargetPlatformReadyStatus::RemoveServerNameEmpty) != 0
			&& (bHasCode || (Result & ETargetPlatformReadyStatus::CodeBuildRequired)
				|| (!FApp::GetEngineIsPromotedBuild() && !FApp::IsEngineInstalled())))
		{
			AddMessageLog(
				LOCTEXT("RemoveServerNameNotFound", "Remote compiling requires a server name. "),
				CustomizedLogMessage.IsEmpty() ? LOCTEXT("RemoveServerNameNotFoundDetail", "Please specify one in the Remote Server Name settings field.") : CustomizedLogMessage,
				NotInstalledTutorialLink,
				DocumentationLink
			);
			UnrecoverableError = true;
		}

		if (UnrecoverableError)
		{
			return false;
		}

		// report to main frame
		if ((Result & ETargetPlatformReadyStatus::CodeUnsupported) != 0)
		{
			// show the message
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NotSupported_CodeBased", "Sorry, launching a code-based project for the selected platform is currently not supported. This feature may be available in a future release."));
			return false;
		}
		if ((Result & ETargetPlatformReadyStatus::PluginsUnsupported) != 0)
		{
			// show the message
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NotSupported_Plugins", "Sorry, launching a project with third-party plugins is currently not supported for the selected platform. This feature may be available in a future release."));
			return false;
		}
	}
	else
	{
		IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		MainFrameModule.BroadcastMainFrameSDKNotInstalled(PlatformInfo->TargetPlatformName.ToString(), PlatformInfo->SDKTutorial);
		return false;
	}

	return true;
}

void FInternalPlayWorldCommandCallbacks::HandleLaunchOnDeviceActionExecute(FString DeviceId, FString DeviceName)
{
	if (IsReadyToLaunchOnDevice(DeviceId))
	{
		ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();

		PlaySettings->LastExecutedLaunchModeType = LaunchMode_OnDevice;
		PlaySettings->LastExecutedLaunchDevice = DeviceId;
		PlaySettings->LastExecutedLaunchName = DeviceName;

		PlaySettings->PostEditChange();

		PlaySettings->SaveConfig();

		LaunchOnDevice(DeviceId, DeviceName);
	}

}


bool FInternalPlayWorldCommandCallbacks::HandleLaunchOnDeviceActionCanExecute(FString DeviceName)
{
	return CanLaunchOnDevice(DeviceName);
}


bool FInternalPlayWorldCommandCallbacks::HandleLaunchOnDeviceActionIsChecked(FString DeviceName)
{
	return (DeviceName == GetDefault<ULevelEditorPlaySettings>()->LastExecutedLaunchName);
}


void FInternalPlayWorldCommandCallbacks::HandleShowSDKTutorial(FString PlatformName, FString NotInstalledDocLink)
{
	// broadcast this, and assume someone will pick it up
	IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	MainFrameModule.BroadcastMainFrameSDKNotInstalled(PlatformName, NotInstalledDocLink);
}

void FInternalPlayWorldCommandCallbacks::GetMouseControlExecute()
{
	if (IsInPIE()) {
		FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
		FSlateApplication::Get().ResetToDefaultInputSettings();

		TWeakPtr<SGlobalPlayWorldActions> ActiveGlobalPlayWorldWidget = FPlayWorldCommands::GetActiveGlobalPlayWorldActionsWidget();
		if (ActiveGlobalPlayWorldWidget.IsValid())
		{
			uint32 UserIndex = 0;
			FSlateApplication::Get().SetUserFocus(UserIndex, ActiveGlobalPlayWorldWidget.Pin());
		}
	}
}

FSlateIcon FInternalPlayWorldCommandCallbacks::GetResumePlaySessionImage()
{
	if (IsInPIE())
	{
		return FSlateIcon(FEditorStyle::GetStyleSetName(), "PlayWorld.ResumePlaySession");
	}
	else if (IsInSIE())
	{
		return FSlateIcon(FEditorStyle::GetStyleSetName(), "PlayWorld.Simulate");
	}
	else
	{
		return FSlateIcon();
	}
}


FText FInternalPlayWorldCommandCallbacks::GetResumePlaySessionToolTip()
{
	if (IsInPIE())
	{
		return LOCTEXT("ResumePIE", "Resume play-in-editor session");
	}
	else if (IsInSIE())
	{
		return LOCTEXT("ResumeSIE", "Resume simulation");
	}
	else
	{
		return FText();
	}
}


void FInternalPlayWorldCommandCallbacks::SingleFrameAdvance_Clicked()
{
	// We want to function just like Single stepping where we will stop at a breakpoint if one is encountered but we also want to stop after 1 tick if a breakpoint is not encountered.
	FKismetDebugUtilities::RequestSingleStepIn();
	if (HasPlayWorld())
	{
		GUnrealEd->PlayWorld->bDebugFrameStepExecution = true;
		LeaveDebuggingMode();
		GUnrealEd->PlaySessionSingleStepped();
	}
}


void FInternalPlayWorldCommandCallbacks::StopPlaySession_Clicked()
{
	if (HasPlayWorld())
	{
		GEditor->RequestEndPlayMap();
		LeaveDebuggingMode();
	}
}

void FInternalPlayWorldCommandCallbacks::LateJoinSession_Clicked()
{
	if (HasPlayWorld())
	{
		GEditor->RequestLateJoin();
	}
}

void FInternalPlayWorldCommandCallbacks::ShowCurrentStatement_Clicked()
{
	UEdGraphNode* CurrentInstruction = FKismetDebugUtilities::GetCurrentInstruction();
	if (CurrentInstruction != NULL)
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(CurrentInstruction);
	}
}


void FInternalPlayWorldCommandCallbacks::StepInto_Clicked()
{
	FKismetDebugUtilities::RequestSingleStepIn();
	if (HasPlayWorld())
	{
		LeaveDebuggingMode();
		GUnrealEd->PlaySessionSingleStepped();
	}
}

void FInternalPlayWorldCommandCallbacks::StepOver_Clicked()
{
	FKismetDebugUtilities::RequestStepOver();
	if (HasPlayWorld())
	{
		LeaveDebuggingMode();
		GUnrealEd->PlaySessionSingleStepped();
	}
}

void FInternalPlayWorldCommandCallbacks::StepOut_Clicked()
{
	FKismetDebugUtilities::RequestStepOut();
	if (HasPlayWorld())
	{
		LeaveDebuggingMode();
		GUnrealEd->PlaySessionSingleStepped();
	}
}

void FInternalPlayWorldCommandCallbacks::TogglePlayPause_Clicked()
{
	if (HasPlayWorld())
	{
		if (GUnrealEd->PlayWorld->IsPaused())
		{
			LeaveDebuggingMode();
			GUnrealEd->PlaySessionResumed();
			uint32 UserIndex = 0;
			FSlateApplication::Get().SetUserFocusToGameViewport(UserIndex);
		}
		else
		{
			GUnrealEd->PlayWorld->bDebugPauseExecution = true;
			GUnrealEd->PlaySessionPaused();
			if (IsInPIE()) {
				FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
				FSlateApplication::Get().ResetToDefaultInputSettings();

				TWeakPtr<SGlobalPlayWorldActions> ActiveGlobalPlayWorldWidget = FPlayWorldCommands::GetActiveGlobalPlayWorldActionsWidget();
				if (ActiveGlobalPlayWorldWidget.IsValid())
				{
					uint32 UserIndex = 0;
					FSlateApplication::Get().SetUserFocus(UserIndex, ActiveGlobalPlayWorldWidget.Pin());
				}
			}
		}
	}
}

bool FInternalPlayWorldCommandCallbacks::CanShowNonPlayWorldOnlyActions()
{
	return !HasPlayWorld();
}

bool FInternalPlayWorldCommandCallbacks::CanShowVulkanNonPlayWorldOnlyActions()
{
	return !HasPlayWorld() && GetDefault<UEditorExperimentalSettings>()->bAllowVulkanPreview && FModuleManager::Get().ModuleExists(TEXT("VulkanRHI"));
}

bool FInternalPlayWorldCommandCallbacks::CanShowVROnlyActions()
{
	return !HasPlayWorld();
}

int32 FInternalPlayWorldCommandCallbacks::GetNumberOfClients()
{
	const ULevelEditorPlaySettings* PlayInSettings = GetDefault<ULevelEditorPlaySettings>();
	int32 PlayNumberOfClients(0);
	PlayInSettings->GetPlayNumberOfClients(PlayNumberOfClients);	// Ignore 'state' of option (handled externally)
	return PlayNumberOfClients;
}


void FInternalPlayWorldCommandCallbacks::SetNumberOfClients(int32 NumClients, ETextCommit::Type CommitInfo)
{
	ULevelEditorPlaySettings* PlayInSettings = GetMutableDefault<ULevelEditorPlaySettings>();
	PlayInSettings->SetPlayNumberOfClients(NumClients);

	PlayInSettings->PostEditChange();
	PlayInSettings->SaveConfig();
}


int32 FInternalPlayWorldCommandCallbacks::GetNetPlayMode()
{
	const ULevelEditorPlaySettings* PlayInSettings = GetDefault<ULevelEditorPlaySettings>();
	EPlayNetMode NetMode;
	PlayInSettings->GetPlayNetMode(NetMode);

	return (int32)NetMode;
}

void FInternalPlayWorldCommandCallbacks::SetNetPlayMode(int32 Value, ESelectInfo::Type CommitInfo)
{
	ULevelEditorPlaySettings* PlayInSettings = GetMutableDefault<ULevelEditorPlaySettings>();
	PlayInSettings->SetPlayNetMode((EPlayNetMode)Value);

	PlayInSettings->PostEditChange();
	PlayInSettings->SaveConfig();
}


bool FInternalPlayWorldCommandCallbacks::IsStoppedAtBreakpoint()
{
	return GIntraFrameDebuggingGameThread;
}


void FInternalPlayWorldCommandCallbacks::PossessEjectPlayer_Clicked()
{
	GEditor->RequestToggleBetweenPIEandSIE();
}


bool FInternalPlayWorldCommandCallbacks::CanPossessEjectPlayer()
{
	if ((IsInSIE() || IsInPIE()) && !IsStoppedAtBreakpoint())
	{
		for (auto It = GUnrealEd->SlatePlayInEditorMap.CreateIterator(); It; ++It)
		{
			return It.Value().DestinationSlateViewport.IsValid();
		}
	}
	return false;
}


void FInternalPlayWorldCommandCallbacks::AddMessageLog(const FText& Text, const FText& Detail, const FString& TutorialLink, const FString& DocumentationLink)
{
	TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Error);
	Message->AddToken(FTextToken::Create(Text));
	Message->AddToken(FTextToken::Create(Detail));
	if (!TutorialLink.IsEmpty())
	{
		Message->AddToken(FTutorialToken::Create(TutorialLink));
	}
	if (!DocumentationLink.IsEmpty())
	{
		Message->AddToken(FDocumentationToken::Create(DocumentationLink));
	}
	FMessageLog MessageLog("PackagingResults");
	MessageLog.AddMessage(Message);
	MessageLog.Open();
}


bool FInternalPlayWorldCommandCallbacks::CanLaunchOnDevice(const FString& DeviceName)
{
	if (!GUnrealEd->IsPlayingViaLauncher())
	{
		static TWeakPtr<ITargetDeviceProxyManager> DeviceProxyManagerPtr;

		if (!DeviceProxyManagerPtr.IsValid())
		{
			ITargetDeviceServicesModule* TargetDeviceServicesModule = FModuleManager::Get().LoadModulePtr<ITargetDeviceServicesModule>(TEXT("TargetDeviceServices"));
			if (TargetDeviceServicesModule)
			{
				DeviceProxyManagerPtr = TargetDeviceServicesModule->GetDeviceProxyManager();
			}
		}

		TSharedPtr<ITargetDeviceProxyManager> DeviceProxyManager = DeviceProxyManagerPtr.Pin();
		if (DeviceProxyManager.IsValid())
		{
			TSharedPtr<ITargetDeviceProxy> DeviceProxy = DeviceProxyManager->FindProxy(DeviceName);
			if (DeviceProxy.IsValid() && DeviceProxy->IsConnected() && DeviceProxy->IsAuthorized())
			{
				return true;
			}

			// check if this is an aggregate proxy
			TArray<TSharedPtr<ITargetDeviceProxy>> Devices;
			DeviceProxyManager->GetProxies(FName(*DeviceName), false, Devices);

			// returns true if the game can be launched al least on 1 device
			for (auto DevicesIt = Devices.CreateIterator(); DevicesIt; ++DevicesIt)
			{
				TSharedPtr<ITargetDeviceProxy> DeviceAggregateProxy = *DevicesIt;
				if (DeviceAggregateProxy.IsValid() && DeviceAggregateProxy->IsConnected() && DeviceAggregateProxy->IsAuthorized())
				{
					return true;
				}
			}

		}
	}

	return false;
}


void FInternalPlayWorldCommandCallbacks::LaunchOnDevice(const FString& DeviceId, const FString& DeviceName)
{
	FTargetDeviceId TargetDeviceId;
	if (FTargetDeviceId::Parse(DeviceId, TargetDeviceId))
	{
		const PlatformInfo::FPlatformInfo* const PlatformInfo = PlatformInfo::FindPlatformInfo(*TargetDeviceId.GetPlatformName());
		check(PlatformInfo);

		if (FInstalledPlatformInfo::Get().IsPlatformMissingRequiredFile(PlatformInfo->BinaryFolderName))
		{
			if (!FInstalledPlatformInfo::OpenInstallerOptions())
			{
				FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MissingPlatformFilesLaunch", "Missing required files to launch on this platform."));
			}
			return;
		}

		if (FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor").ShowUnsupportedTargetWarning(*TargetDeviceId.GetPlatformName()))
		{
			GUnrealEd->CancelPlayingViaLauncher();

			FRequestPlaySessionParams::FLauncherDeviceInfo DeviceInfo;
			DeviceInfo.DeviceId = DeviceId;
			DeviceInfo.DeviceName = DeviceName;

			FRequestPlaySessionParams SessionParams;
			SessionParams.SessionDestination = EPlaySessionDestinationType::Launcher;
			SessionParams.LauncherTargetDevice = DeviceInfo;

			GUnrealEd->RequestPlaySession(SessionParams);
		}
	}
}


EPlayModeLocations FInternalPlayWorldCommandCallbacks::GetPlayModeLocation()
{
	// We can't use PlayLocation_DefaultPlayerStart without a player start position
	return GEditor->CheckForPlayerStart()
		? static_cast<EPlayModeLocations>(GetDefault<ULevelEditorPlaySettings>()->LastExecutedPlayModeLocation)
		: PlayLocation_CurrentCameraLocation;
}

#undef LOCTEXT_NAMESPACE
