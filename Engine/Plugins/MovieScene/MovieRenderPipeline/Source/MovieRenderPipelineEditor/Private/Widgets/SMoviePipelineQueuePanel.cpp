// Copyright Epic Games, Inc. All Rights Reserved.

// Movie Pipeline Includes
#include "Widgets/SMoviePipelineQueuePanel.h"
#include "Widgets/MoviePipelineWidgetConstants.h"
#include "SMoviePipelineQueueEditor.h"
#include "SMoviePipelineConfigPanel.h"
#include "MovieRenderPipelineSettings.h"
#include "MovieRenderPipelineStyle.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineMasterConfig.h"
#include "MoviePipelineQueueSubsystem.h"

// Slate Includes
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

// ContentBrowser Includes
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"

// Misc
#include "Editor.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"

// UnrealEd Includes
#include "ScopedTransaction.h"
#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "FileHelpers.h"
#include "AssetToolsModule.h"
#include "Misc/FileHelper.h"


#define LOCTEXT_NAMESPACE "SMoviePipelineQueuePanel"

PRAGMA_DISABLE_OPTIMIZATION
void SMoviePipelineQueuePanel::Construct(const FArguments& InArgs)
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs(false, false, false, FDetailsViewArgs::HideNameArea, true);
	DetailsViewArgs.ColumnWidth = 0.7f;

	JobDetailsPanelWidget = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	// Create the child widgets that need to know about our pipeline
	PipelineQueueEditorWidget = SNew(SMoviePipelineQueueEditor)
		.OnEditConfigRequested(this, &SMoviePipelineQueuePanel::OnEditJobConfigRequested)
		.OnPresetChosen(this, &SMoviePipelineQueuePanel::OnJobPresetChosen)
		.OnJobSelectionChanged(this, &SMoviePipelineQueuePanel::OnSelectionChanged);

	// Reset us to no selection.
	OnSelectionChanged(TArray<UMoviePipelineExecutorJob*>());

	ChildSlot
	[
		SNew(SVerticalBox)

		// Create the toolbar for adding new items to the queue
		+ SVerticalBox::Slot()
		.Padding(FMargin(0.f, 1.0f))
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
			.BorderBackgroundColor(FLinearColor(.6, .6, .6, 1.0f))
			[
				SNew(SHorizontalBox)

				// Add a Level Sequence to the queue 
				+ SHorizontalBox::Slot()
				.Padding(MoviePipeline::ButtonOffset)
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					PipelineQueueEditorWidget->MakeAddSequenceJobButton()
				]

				// Remove a job (potentially already processed) from the the queue 
				+ SHorizontalBox::Slot()
				.Padding(MoviePipeline::ButtonOffset)
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					PipelineQueueEditorWidget->RemoveSelectedJobButton()
				]	

				// Spacer
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Fill)
				.FillWidth(1.f)
				[
					SNullWidget::NullWidget
				]

				// Presets Management Button
				+ SHorizontalBox::Slot()
				.Padding(MoviePipeline::ButtonOffset)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				.AutoWidth()
				[
					SNew(SComboButton)
					.ToolTipText(LOCTEXT("QueueManagementButton_Tooltip", "Export the current queue to an asset, or load a previously saved queue."))
					.ContentPadding(MoviePipeline::ButtonPadding)
					.ComboButtonStyle(FMovieRenderPipelineStyle::Get(), "ComboButton")
					.OnGetMenuContent(this, &SMoviePipelineQueuePanel::OnGenerateSavedQueuesMenu)
					.ForegroundColor(FSlateColor::UseForeground())
					.ButtonContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.Padding(0, 1, 4, 0)
						.AutoWidth()
						[
							SNew(SImage)
							.Image(FSlateIconFinder::FindIconBrushForClass(UMoviePipelineQueue::StaticClass()))
						]

						+ SHorizontalBox::Slot()
						.Padding(0, 1, 0, 0)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SavedQueueToolbarButton", "Load/Save Queue"))
						]
					]
				]
			]
		]
	
		// Main Queue Body
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Horizontal)
			+ SSplitter::Slot()
			.Value(3)
			[
				PipelineQueueEditorWidget.ToSharedRef()

			]
			+ SSplitter::Slot()
			.Value(1)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(FMargin(1.f, 1.0f))
				.Content()
				[
					SNew(SWidgetSwitcher)
					.WidgetIndex(this, &SMoviePipelineQueuePanel::GetDetailsViewWidgetIndex)
					.IsEnabled(this, &SMoviePipelineQueuePanel::IsDetailsViewEnabled)
					+ SWidgetSwitcher::Slot()
					[
						JobDetailsPanelWidget.ToSharedRef()
					]
					+ SWidgetSwitcher::Slot()
					.Padding(2.0f, 24.0f, 2.0f, 2.0f)
					[
						SNew(SBox)
						.HAlign(EHorizontalAlignment::HAlign_Center)
						.Content()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NoJobSelected", "Select a job to view details."))
						]
					]
				]
			]
		]

		// Footer Bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
			.BorderBackgroundColor(FLinearColor(.6, .6, .6, 1.0f))
			.Padding(FMargin(0, 2, 0, 2))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Left)
				.FillWidth(1.f)
				[
					SNullWidget::NullWidget
				]

				// Render Local in Process
				+ SHorizontalBox::Slot()
				.Padding(MoviePipeline::ButtonOffset)
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Right)
				.AutoWidth()
				[
					SNew(SButton)
					.ContentPadding(MoviePipeline::ButtonPadding)
					.ButtonStyle(FMovieRenderPipelineStyle::Get(), "FlatButton.Success")
					.IsEnabled(this, &SMoviePipelineQueuePanel::IsRenderLocalEnabled)
					.OnClicked(this, &SMoviePipelineQueuePanel::OnRenderLocalRequested)
					.Content()
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "NormalText.Important")
						.Text(LOCTEXT("RenderQueueLocal_Text", "Render (Local)"))
						.ToolTipText(LOCTEXT("RenderQueueLocal_Tooltip", "Renders the current queue in the current process using Play in Editor."))
						.Margin(FMargin(4, 0, 4, 0))
					]
				]

				// Render Remotely (Separate Process or Farm)
				+ SHorizontalBox::Slot()
				.Padding(MoviePipeline::ButtonOffset)
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Right)
				.AutoWidth()
				[
					SNew(SButton)
					.ContentPadding(MoviePipeline::ButtonPadding)
					.ButtonStyle(FMovieRenderPipelineStyle::Get(), "FlatButton.Success")
					.IsEnabled(this, &SMoviePipelineQueuePanel::IsRenderRemoteEnabled)
					.OnClicked(this, &SMoviePipelineQueuePanel::OnRenderRemoteRequested)
					.Content()
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "NormalText.Important")
						.Text(LOCTEXT("RenderQueueRemote_Text", "Render (Remote)"))
						.ToolTipText(LOCTEXT("RenderQueueRemote_Tooltip", "Renders the current queue in a separate process."))
						.Margin(FMargin(4, 0, 4, 0))
					]
				]
			]
		]
	];
}

PRAGMA_ENABLE_OPTIMIZATION

FReply SMoviePipelineQueuePanel::OnRenderLocalRequested()
{
	UMoviePipelineQueueSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
	check(Subsystem);

	const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
	check(ProjectSettings->DefaultLocalExecutor != nullptr);

	Subsystem->RenderQueueWithExecutor(ProjectSettings->DefaultLocalExecutor);
	return FReply::Handled();
}

bool SMoviePipelineQueuePanel::IsRenderLocalEnabled() const
{
	UMoviePipelineQueueSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
	check(Subsystem);

	const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
	const bool bHasExecutor = ProjectSettings->DefaultLocalExecutor != nullptr;
	const bool bNotRendering = !Subsystem->IsRendering();

	bool bAtLeastOneJobAvailable = false;
	for (UMoviePipelineExecutorJob* Job : Subsystem->GetQueue()->GetJobs())
	{
		if (!Job->IsConsumed())
		{
			bAtLeastOneJobAvailable = true;
			break;
		}
	}

	const bool bWorldIsActive = GEditor->IsPlaySessionInProgress();
	return bHasExecutor && bNotRendering && bAtLeastOneJobAvailable && !bWorldIsActive;
}

FReply SMoviePipelineQueuePanel::OnRenderRemoteRequested()
{
	UMoviePipelineQueueSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
	check(Subsystem);

	const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
	check(ProjectSettings->DefaultRemoteExecutor != nullptr);

	Subsystem->RenderQueueWithExecutor(ProjectSettings->DefaultRemoteExecutor);
	return FReply::Handled();
}

bool SMoviePipelineQueuePanel::IsRenderRemoteEnabled() const
{
	UMoviePipelineQueueSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
	check(Subsystem);

	const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
	const bool bHasExecutor = ProjectSettings->DefaultRemoteExecutor != nullptr;
	const bool bNotRendering = !Subsystem->IsRendering();

	bool bAtLeastOneJobAvailable = false;
	for (UMoviePipelineExecutorJob* Job : Subsystem->GetQueue()->GetJobs())
	{
		if (!Job->IsConsumed())
		{
			bAtLeastOneJobAvailable = true;
			break;
		}
	}

	return bHasExecutor && bNotRendering && bAtLeastOneJobAvailable;
}

void SMoviePipelineQueuePanel::OnJobPresetChosen(TWeakObjectPtr<UMoviePipelineExecutorJob> InJob, TWeakObjectPtr<UMovieSceneCinematicShotSection> InShot)
{
	// Store the preset so the next job they make will use it.
	UMovieRenderPipelineProjectSettings* ProjectSettings = GetMutableDefault<UMovieRenderPipelineProjectSettings>();
	ProjectSettings->LastPresetOrigin = InJob->GetPresetOrigin();
	ProjectSettings->SaveConfig();
}

void SMoviePipelineQueuePanel::OnEditJobConfigRequested(TWeakObjectPtr<UMoviePipelineExecutorJob> InJob, TWeakObjectPtr<UMovieSceneCinematicShotSection> InShot)
{
	// Only allow one editor open at once for now.
	if (WeakEditorWindow.IsValid())
	{
		FWidgetPath ExistingWindowPath;
		if (FSlateApplication::Get().FindPathToWidget(WeakEditorWindow.Pin().ToSharedRef(), ExistingWindowPath, EVisibility::All))
		{
			WeakEditorWindow.Pin()->BringToFront();
			FSlateApplication::Get().SetAllUserFocus(ExistingWindowPath, EFocusCause::SetDirectly);
		}

		return;
	}

	TSharedRef<SWindow> EditorWindow =
		SNew(SWindow)
		.ClientSize(FVector2D(700, 600));

	TSharedRef<SMoviePipelineConfigPanel> ConfigEditorPanel =
		SNew(SMoviePipelineConfigPanel, UMoviePipelineMasterConfig::StaticClass())
		.Job(InJob)
		.OnConfigurationModified(this, &SMoviePipelineQueuePanel::OnConfigUpdatedForJob)
		.OnConfigurationSetToPreset(this, &SMoviePipelineQueuePanel::OnConfigUpdatedForJobToPreset)
		.BasePreset(InJob->GetPresetOrigin())
		.BaseConfig(InJob->GetConfiguration());

	EditorWindow->SetContent(ConfigEditorPanel);


	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (ParentWindow.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(EditorWindow, ParentWindow.ToSharedRef());
	}

	WeakEditorWindow = EditorWindow;
}

void SMoviePipelineQueuePanel::OnConfigWindowClosed()
{
	if (WeakEditorWindow.IsValid())
	{
		WeakEditorWindow.Pin()->RequestDestroyWindow();
	}
}

void SMoviePipelineQueuePanel::OnConfigUpdatedForJob(TWeakObjectPtr<UMoviePipelineExecutorJob> InJob, UMoviePipelineConfigBase* InConfig)
{
	if (InJob.IsValid())
	{
		UMoviePipelineMasterConfig* MasterConfig = Cast<UMoviePipelineMasterConfig>(InConfig);
		if (MasterConfig)
		{
			InJob->SetConfiguration(MasterConfig);
		}
	}

	OnConfigWindowClosed();
}

void SMoviePipelineQueuePanel::OnConfigUpdatedForJobToPreset(TWeakObjectPtr<UMoviePipelineExecutorJob> InJob, UMoviePipelineConfigBase* InConfig)
{
	if (InJob.IsValid())
	{
		UMoviePipelineMasterConfig* MasterConfig = Cast<UMoviePipelineMasterConfig>(InConfig);
		if (MasterConfig)
		{
			InJob->SetPresetOrigin(MasterConfig);
		}
	}

	// Store the preset they used as the last set one
	OnJobPresetChosen(InJob, nullptr);

	OnConfigWindowClosed();
}

void SMoviePipelineQueuePanel::OnSelectionChanged(const TArray<UMoviePipelineExecutorJob*>& InSelectedJobs)
{
	TArray<UObject*> Jobs;
	for (UMoviePipelineExecutorJob* Job : InSelectedJobs)
	{
		Jobs.Add(Job);
	}
	
	JobDetailsPanelWidget->SetObjects(Jobs);
	NumSelectedJobs = InSelectedJobs.Num();
}

int32 SMoviePipelineQueuePanel::GetDetailsViewWidgetIndex() const
{
	return NumSelectedJobs == 0;
}

bool SMoviePipelineQueuePanel::IsDetailsViewEnabled() const
{
	TArray<TWeakObjectPtr<UObject>> OutObjects= JobDetailsPanelWidget->GetSelectedObjects();

	bool bAllEnabled = true;
	for (TWeakObjectPtr<UObject> Object : OutObjects)
	{
		const UMoviePipelineExecutorJob* Job = Cast<UMoviePipelineExecutorJob>(Object);
		if (Job && Job->IsConsumed())
		{
			bAllEnabled = false;
			break;
		}
	}

	return bAllEnabled;
}

TSharedRef<SWidget> SMoviePipelineQueuePanel::OnGenerateSavedQueuesMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("SaveAsQueue_Text", "Save As Asset"),
		LOCTEXT("SaveAsQueue_Tip", "Save the current configuration as a new preset that can be shared between multiple jobs, or imported later as the base of a new configuration."),
		FSlateIcon(FEditorStyle::Get().GetStyleSetName(), "AssetEditor.SaveAsset.Greyscale"),
		FUIAction(FExecuteAction::CreateSP(this, &SMoviePipelineQueuePanel::OnSaveAsAsset))
	);

	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.SelectionMode = ESelectionMode::Single;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::Column;
		AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.bShowBottomToolbar = true;
		AssetPickerConfig.bAutohideSearchBar = false;
		AssetPickerConfig.bAllowDragging = false;
		AssetPickerConfig.bCanShowClasses = false;
		AssetPickerConfig.bShowPathInColumnView = true;
		AssetPickerConfig.bShowTypeInColumnView = false;
		AssetPickerConfig.bSortByPathInColumnView = false;

		AssetPickerConfig.AssetShowWarningText = LOCTEXT("NoQueueAssets_Warning", "No Queues Found");
		AssetPickerConfig.Filter.ClassNames.Add(UMoviePipelineQueue::StaticClass()->GetFName());
		AssetPickerConfig.Filter.bRecursiveClasses = true;
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SMoviePipelineQueuePanel::OnImportSavedQueueAssest);
	}

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("LoadQueue_MenuSection", "Load Queue"));
	{
		TSharedRef<SWidget> PresetPicker = SNew(SBox)
			.MinDesiredWidth(400.f)
			.MinDesiredHeight(400.f)
			[
				ContentBrowser.CreateAssetPicker(AssetPickerConfig)
			];

		MenuBuilder.AddWidget(PresetPicker, FText(), true, false);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

bool SMoviePipelineQueuePanel::OpenSaveDialog(const FString& InDefaultPath, const FString& InNewNameSuggestion, FString& OutPackageName)
{
	FSaveAssetDialogConfig SaveAssetDialogConfig;
	{
		SaveAssetDialogConfig.DefaultPath = InDefaultPath;
		SaveAssetDialogConfig.DefaultAssetName = InNewNameSuggestion;
		SaveAssetDialogConfig.AssetClassNames.Add(UMoviePipelineQueue::StaticClass()->GetFName());
		SaveAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
		SaveAssetDialogConfig.DialogTitleOverride = LOCTEXT("SaveQueueAssetDialogTitle", "Save Queue Asset");
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	FString SaveObjectPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(SaveAssetDialogConfig);

	if (!SaveObjectPath.IsEmpty())
	{
		OutPackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
		return true;
	}

	return false;
}

bool SMoviePipelineQueuePanel::GetSavePresetPackageName(const FString& InExistingName, FString& OutName)
{
	UMovieRenderPipelineProjectSettings* ConfigSettings = GetMutableDefault<UMovieRenderPipelineProjectSettings>();

	// determine default package path
	const FString DefaultSaveDirectory = ConfigSettings->PresetSaveDir.Path;

	FString DialogStartPath;
	FPackageName::TryConvertFilenameToLongPackageName(DefaultSaveDirectory, DialogStartPath);
	if (DialogStartPath.IsEmpty())
	{
		DialogStartPath = TEXT("/Game");
	}

	// determine default asset name
	FString DefaultName = InExistingName;

	FString UniquePackageName;
	FString UniqueAssetName;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(DialogStartPath / DefaultName, TEXT(""), UniquePackageName, UniqueAssetName);

	FString DialogStartName = FPaths::GetCleanFilename(UniqueAssetName);

	FString UserPackageName;
	FString NewPackageName;

	// get destination for asset
	bool bFilenameValid = false;
	while (!bFilenameValid)
	{
		if (!OpenSaveDialog(DialogStartPath, DialogStartName, UserPackageName))
		{
			return false;
		}

		NewPackageName = UserPackageName;

		FText OutError;
		bFilenameValid = FFileHelper::IsFilenameValidForSaving(NewPackageName, OutError);
	}

	// Update to the last location they saved to so it remembers their settings next time.
	ConfigSettings->PresetSaveDir.Path = FPackageName::GetLongPackagePath(UserPackageName);
	ConfigSettings->SaveConfig();
	OutName = MoveTemp(NewPackageName);
	return true;
}

void SMoviePipelineQueuePanel::OnSaveAsAsset()
{
	UMoviePipelineQueueSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
	check(Subsystem);
	UMoviePipelineQueue* CurrentQueue = Subsystem->GetQueue();

	FString PackageName;
	if (!GetSavePresetPackageName(CurrentQueue->GetName(), PackageName))
	{
		return;
	}
	
	// Saving into a new package
	const FString NewAssetName = FPackageName::GetLongPackageAssetName(PackageName);
	UPackage* NewPackage = CreatePackage(nullptr, *PackageName);
	UMoviePipelineQueue* DuplicateQueue = DuplicateObject<UMoviePipelineQueue>(CurrentQueue, NewPackage, *NewAssetName);

	if (DuplicateQueue)
	{
		DuplicateQueue->SetFlags(RF_Public | RF_Standalone | RF_Transactional);

		FAssetRegistryModule::AssetCreated(DuplicateQueue);

		FEditorFileUtils::EPromptReturnCode PromptReturnCode = FEditorFileUtils::PromptForCheckoutAndSave({ NewPackage }, false, false);
	}
}

void SMoviePipelineQueuePanel::OnImportSavedQueueAssest(const FAssetData& InPresetAsset)
{
	FSlateApplication::Get().DismissAllMenus();

	UMoviePipelineQueue* SavedQueue = CastChecked<UMoviePipelineQueue>(InPresetAsset.GetAsset());
	if (SavedQueue)
	{
		// Duplicate the queue so we don't start modifying the one in the asset.
		UMoviePipelineQueueSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
		check(Subsystem);

		Subsystem->GetQueue()->CopyFrom(SavedQueue);
	}
}

#undef LOCTEXT_NAMESPACE // SMoviePipelineQueuePanel