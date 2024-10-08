// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceInsightsModule.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Modules/ModuleManager.h"
#include "Trace/StoreClient.h"
#include "Trace/StoreService.h"
#include "TraceServices/ITraceServicesModule.h"
#include "Framework/Docking/LayoutService.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Insights/LoadingProfiler/LoadingProfilerManager.h"
#include "Insights/Log.h"
#include "Insights/NetworkingProfiler/NetworkingProfilerManager.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/MemoryProfiler/MemoryProfilerManager.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

DEFINE_LOG_CATEGORY(TraceInsights);

IMPLEMENT_MODULE(FTraceInsightsModule, TraceInsights);

FString FTraceInsightsModule::UnrealInsightsLayoutIni;

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTraceInsightsModule
////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::StartupModule()
{
	ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
	TraceAnalysisService = TraceServicesModule.GetAnalysisService();
	TraceModuleService = TraceServicesModule.GetModuleService();

	FInsightsStyle::Initialize();

	// Register FInsightsManager first, as the main component (first to init, last to shutdown).
	RegisterComponent(FInsightsManager::CreateInstance(TraceAnalysisService.ToSharedRef(), TraceModuleService.ToSharedRef()));

	// Register other default components.
	RegisterComponent(FTimingProfilerManager::CreateInstance());
	RegisterComponent(FLoadingProfilerManager::CreateInstance());
	RegisterComponent(FNetworkingProfilerManager::CreateInstance());
	RegisterComponent(FMemoryProfilerManager::CreateInstance());

	UnrealInsightsLayoutIni = FPaths::GetPath(GEngineIni) + "/UnrealInsightsLayout.ini";
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::ShutdownModule()
{
	if (PersistentLayout.IsValid())
	{
		// Save application layout.
		FLayoutSaveRestore::SaveToConfig(UnrealInsightsLayoutIni, PersistentLayout.ToSharedRef());
		GConfig->Flush(false, UnrealInsightsLayoutIni);
	}

	UnregisterTabSpawners();

	// Unregister components. Shutdown in the reverse order they were registered.
	for (int32 ComponentIndex = Components.Num() - 1; ComponentIndex >= 0; --ComponentIndex)
	{
		Components[ComponentIndex]->Shutdown();
	}
	Components.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::RegisterComponent(TSharedPtr<IInsightsComponent> Component)
{
	if (Component.IsValid())
	{
		Components.Add(Component.ToSharedRef());
		Component->Initialize(*this);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::UnregisterComponent(TSharedPtr<IInsightsComponent> Component)
{
	if (Component.IsValid())
	{
		Component->Shutdown();
		Components.Remove(Component.ToSharedRef());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::CreateDefaultStore()
{
	const FString StoreDir = FPaths::ProjectSavedDir() / TEXT("TraceSessions");

	FInsightsManager::Get()->SetStoreDir(StoreDir);

	// Create the Store Service.
	Trace::FStoreService::FDesc StoreServiceDesc;
	StoreServiceDesc.StoreDir = *StoreDir;
	StoreServiceDesc.RecorderPort = 1980;
	StoreServiceDesc.ThreadCount = 2;
	StoreService = TUniquePtr<Trace::FStoreService>(Trace::FStoreService::Create(StoreServiceDesc));

	if (StoreService.IsValid())
	{
		ConnectToStore(TEXT("127.0.0.1"), StoreService->GetPort());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Trace::FStoreClient* FTraceInsightsModule::GetStoreClient()
{
	return FInsightsManager::Get()->GetStoreClient();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTraceInsightsModule::ConnectToStore(const TCHAR* InStoreHost, uint32 InStorePort)
{
	return FInsightsManager::Get()->ConnectToStore(InStoreHost, InStorePort);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::RegisterTabSpawners()
{
	// Allow components to register major tabs.
	for (TSharedRef<IInsightsComponent>& Component : Components)
	{
		Component->RegisterMajorTabs(*this);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::UnregisterTabSpawners()
{
	// Unregister major tabs in the reverse order they were registered.
	for (int32 ComponentIndex = Components.Num() - 1; ComponentIndex >= 0; --ComponentIndex)
	{
		Components[ComponentIndex]->UnregisterMajorTabs();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::CreateSessionBrowser(bool bAllowDebugTools, bool bSingleProcess)
{
	FInsightsManager::Get()->SetOpenAnalysisInSeparateProcess(!bSingleProcess);

	RegisterTabSpawners();

	const float DPIScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(10.0f, 10.0f);

	TSharedRef<FTabManager::FLayout> DefaultLayout = FTabManager::NewLayout("TraceSessionBrowserLayout_v1.0");

	if (!bSingleProcess)
	{
		constexpr float WindowWidth = 920.0f;
		constexpr float WindowHeight = 665.0f;

		DefaultLayout->AddArea
		(
			FTabManager::NewArea(WindowWidth * DPIScaleFactor, WindowHeight * DPIScaleFactor)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(FInsightsManagerTabs::StartPageTabId, ETabState::OpenedTab)
				//->SetForegroundTab(FTabId(FInsightsManagerTabs::StartPageTabId))
				//->SetHideTabWell(true)
			)
		);
	}
	else
	{
		constexpr float WindowWidth = 1280.0f;
		constexpr float WindowHeight = 720.0f;

		DefaultLayout->AddArea
		(
			FTabManager::NewArea(WindowWidth * DPIScaleFactor, WindowHeight * DPIScaleFactor)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(FInsightsManagerTabs::StartPageTabId, ETabState::OpenedTab)
				->AddTab(FInsightsManagerTabs::SessionInfoTabId, ETabState::ClosedTab)
				->AddTab(FInsightsManagerTabs::TimingProfilerTabId, ETabState::ClosedTab)
				->AddTab(FInsightsManagerTabs::LoadingProfilerTabId, ETabState::ClosedTab)
				->AddTab(FInsightsManagerTabs::NetworkingProfilerTabId, ETabState::ClosedTab)
				->AddTab(FInsightsManagerTabs::MemoryProfilerTabId, ETabState::ClosedTab)
				->SetForegroundTab(FTabId(FInsightsManagerTabs::StartPageTabId))
				//->SetHideTabWell(true)
			)
		);
	}

	AddAreaForWidgetReflector(DefaultLayout, bAllowDebugTools);

	// Restore application layout.
	PersistentLayout = FLayoutSaveRestore::LoadFromConfig(UnrealInsightsLayoutIni, DefaultLayout);
	FGlobalTabmanager::Get()->RestoreFrom(PersistentLayout.ToSharedRef(), TSharedPtr<SWindow>());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::CreateSessionViewer(bool bAllowDebugTools)
{
	RegisterTabSpawners();

#if !WITH_EDITOR
	TSharedRef<FTabManager::FLayout> DefaultLayout = FTabManager::NewLayout("UnrealInsightsLayout_v1.0");

	AddAreaForSessionViewer(DefaultLayout);

	AddAreaForWidgetReflector(DefaultLayout, bAllowDebugTools);

	// Restore application layout.
	PersistentLayout = FLayoutSaveRestore::LoadFromConfig(UnrealInsightsLayoutIni, DefaultLayout);
	FGlobalTabmanager::Get()->RestoreFrom(PersistentLayout.ToSharedRef(), TSharedPtr<SWindow>());
#endif 
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::AddAreaForSessionViewer(TSharedRef<FTabManager::FLayout> Layout)
{
	TSharedRef<FTabManager::FStack> Stack = FTabManager::NewStack();

#if WITH_EDITOR
	// In editor, we default to all tabs closed.
	Stack->AddTab(FInsightsManagerTabs::StartPageTabId, ETabState::ClosedTab);
	Stack->AddTab(FInsightsManagerTabs::SessionInfoTabId, ETabState::ClosedTab);
	Stack->AddTab(FInsightsManagerTabs::TimingProfilerTabId, ETabState::ClosedTab);
	Stack->AddTab(FInsightsManagerTabs::LoadingProfilerTabId, ETabState::ClosedTab);
	Stack->AddTab(FInsightsManagerTabs::NetworkingProfilerTabId, ETabState::ClosedTab);
	Stack->AddTab(FInsightsManagerTabs::MemoryProfilerTabId, ETabState::ClosedTab);
	//Stack->SetForegroundTab(FTabId(FInsightsManagerTabs::TimingProfilerTabId));
#else
	Stack->AddTab(FInsightsManagerTabs::StartPageTabId, ETabState::ClosedTab);
	Stack->AddTab(FInsightsManagerTabs::SessionInfoTabId, ETabState::OpenedTab);
	Stack->AddTab(FInsightsManagerTabs::TimingProfilerTabId, ETabState::OpenedTab);
	Stack->AddTab(FInsightsManagerTabs::LoadingProfilerTabId, ETabState::ClosedTab);
	Stack->AddTab(FInsightsManagerTabs::NetworkingProfilerTabId, ETabState::ClosedTab);
	Stack->AddTab(FInsightsManagerTabs::MemoryProfilerTabId, ETabState::ClosedTab);
	Stack->SetForegroundTab(FTabId(FInsightsManagerTabs::TimingProfilerTabId));
#endif

	// Create area for the main window.
	const float DPIScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(10.0f, 10.0f);
	Layout->AddArea
	(
		FTabManager::NewArea(1280.f * DPIScaleFactor, 720.0f * DPIScaleFactor)
		->Split(Stack)
	);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::AddAreaForWidgetReflector(TSharedRef<FTabManager::FLayout> Layout, bool bAllowDebugTools)
{
	const float DPIScaleFactor = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(10.0f, 10.0f);

	// Create area and tab for Slate's WidgetReflector.
	Layout->AddArea
	(
		FTabManager::NewArea(600.0f * DPIScaleFactor, 600.0f * DPIScaleFactor)
		->SetWindow(FVector2D(10.0f * DPIScaleFactor, 10.0f * DPIScaleFactor), false)
		->Split
		(
			FTabManager::NewStack()->AddTab("WidgetReflector", bAllowDebugTools ? ETabState::OpenedTab : ETabState::ClosedTab)
		)
	);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::ShutdownUserInterface()
{
	check(PersistentLayout.IsValid());

	// Save application layout.
	FLayoutSaveRestore::SaveToConfig(UnrealInsightsLayoutIni, PersistentLayout.ToSharedRef());
	GConfig->Flush(false, UnrealInsightsLayoutIni);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::RegisterMajorTabConfig(const FName& InMajorTabId, const FInsightsMajorTabConfig& InConfig)
{
	TabConfigs.Add(InMajorTabId, InConfig);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::UnregisterMajorTabConfig(const FName& InMajorTabId)
{
	TabConfigs.Remove(InMajorTabId);
}


////////////////////////////////////////////////////////////////////////////////////////////////////

FOnRegisterMajorTabExtensions& FTraceInsightsModule::OnRegisterMajorTabExtension(const FName& InMajorTabId)
{
	return MajorTabExtensionDelegates.FindOrAdd(InMajorTabId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FInsightsMajorTabConfig& FTraceInsightsModule::FindMajorTabConfig(const FName& InMajorTabId) const
{
	const FInsightsMajorTabConfig* FoundConfig = TabConfigs.Find(InMajorTabId);
	if (FoundConfig != nullptr)
	{
		return *FoundConfig;
	}

	static FInsightsMajorTabConfig DefaultConfig;
	return DefaultConfig;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FOnRegisterMajorTabExtensions* FTraceInsightsModule::FindMajorTabLayoutExtension(const FName& InMajorTabId) const
{
	return MajorTabExtensionDelegates.Find(InMajorTabId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FString& FTraceInsightsModule::GetUnrealInsightsLayoutIni()
{
	return UnrealInsightsLayoutIni;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::SetUnrealInsightsLayoutIni(const FString& InIniPath)
{
	UnrealInsightsLayoutIni = InIniPath;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<const Trace::IAnalysisSession> FTraceInsightsModule::GetAnalysisSession() const
{
	return FInsightsManager::Get()->GetSession();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::StartAnalysisForTrace(uint32 InTraceId)
{
	if (InTraceId != 0)
	{
		FInsightsManager::Get()->LoadTrace(InTraceId);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::StartAnalysisForLastLiveSession()
{
	FInsightsManager::Get()->LoadLastLiveSession();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTraceInsightsModule::StartAnalysisForTraceFile(const TCHAR* InTraceFile)
{
	if (InTraceFile != nullptr)
	{
		FInsightsManager::Get()->LoadTraceFile(FString(InTraceFile));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
