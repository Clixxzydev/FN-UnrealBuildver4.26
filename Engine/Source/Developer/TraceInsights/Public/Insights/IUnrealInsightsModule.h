// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxExtender.h"

class FExtender;

namespace Trace
{
	class FStoreClient;
	class IAnalysisSession;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Major tab IDs for Insights tools */
struct TRACEINSIGHTS_API FInsightsManagerTabs
{
	static const FName StartPageTabId;
	static const FName SessionInfoTabId;
	static const FName TimingProfilerTabId;
	static const FName LoadingProfilerTabId;
	static const FName NetworkingProfilerTabId;
	static const FName MemoryProfilerTabId;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Tab IDs for the timing profiler */
struct TRACEINSIGHTS_API FTimingProfilerTabs
{
	// Tab identifiers
	static const FName ToolbarID;
	static const FName FramesTrackID;
	static const FName TimingViewID;
	static const FName TimersID;
	static const FName CallersID;
	static const FName CalleesID;
	static const FName StatsCountersID;
	static const FName LogViewID;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Configuration for an Insights minor tab. This is used to augment the standard supplied tabs from plugins. */
struct TRACEINSIGHTS_API FInsightsMinorTabConfig
{
	FName TabId;

	FText TabLabel;

	FText TabTooltip;

	FSlateIcon TabIcon;

	FOnSpawnTab OnSpawnTab;

	FOnFindTabToReuse OnFindTabToReuse;

	TSharedPtr<FWorkspaceItem> WorkspaceGroup;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Configuration for an Insights major tab */
struct TRACEINSIGHTS_API FInsightsMajorTabConfig
{
	FInsightsMajorTabConfig()
		: Layout(nullptr)
		, WorkspaceGroup(nullptr)
		, bIsAvailable(true)
	{}

	/** Helper function for creating unavailable tab configs */
	static FInsightsMajorTabConfig Unavailable()
	{
		FInsightsMajorTabConfig Config;
		Config.bIsAvailable = false;
		return Config;
	}

	/** Identifier for this config */
	FName ConfigId;

	/** Display name for this config */
	FText ConfigDisplayName;

	/** Label for the tab. If this is not set the default will be used */
	TOptional<FText> TabLabel;

	/** Tooltip for the tab. If this is not set the default will be used */
	TOptional<FText> TabTooltip;

	/** Icon for the tab. If this is not set the default will be used */
	TOptional<FSlateIcon> TabIcon;

	/** The tab layout to use. If not specified, the default will be used. */
	TSharedPtr<FTabManager::FLayout> Layout;

	/** The menu workspace group to use. If not specified, the default will be used. */
	TSharedPtr<FWorkspaceItem> WorkspaceGroup;

	/** Whether the tab is available for selection (i.e. registered with the tab manager) */
	bool bIsAvailable;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Combination of extenders applied to the individual major tabs within Insights */
struct TRACEINSIGHTS_API FInsightsMajorTabExtender
{
	FInsightsMajorTabExtender(TSharedPtr<FTabManager>& InTabManager) : MenuExtender(MakeShared<FExtender>()), TabManager(InTabManager) {}

	TSharedPtr<FExtender>& GetMenuExtender() { return MenuExtender; }
	FLayoutExtender& GetLayoutExtender() { return LayoutExtender; }
	FInsightsMinorTabConfig& AddMinorTabConfig() { return MinorTabs.AddDefaulted_GetRef(); }
	TSharedPtr<FTabManager> GetTabManager() const { return TabManager; }
	const TArray<FInsightsMinorTabConfig>& GetMinorTabs() const { return MinorTabs; }

protected:
	/** Extender used to add to the menu for this tab */
	TSharedPtr<FExtender> MenuExtender;

	/** Any additional minor tabs to add */
	TArray<FInsightsMinorTabConfig> MinorTabs;

	/** Extender used when creating the layout for this tab */
	FLayoutExtender LayoutExtender;

	/** Tab manager for this major tab*/
	TSharedPtr<FTabManager> TabManager;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Called back to register common layout extensions */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnRegisterMajorTabExtensions, FInsightsMajorTabExtender& /*MajorTabExtender*/);

/** Delegate invoked when a major tab is created */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnInsightsMajorTabCreated, FName /*MajorTabId*/, TSharedRef<FTabManager> /*TabManager*/)

class TRACEINSIGHTS_API IInsightsComponent;

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Interface for an Unreal Insights module. */
class TRACEINSIGHTS_API IUnrealInsightsModule : public IModuleInterface
{
public:
	/**
	 * Registers an IInsightsComponent. The component will Initialize().
	 */
	virtual void RegisterComponent(TSharedPtr<IInsightsComponent> Component) = 0;

	/**
	 * Unregisters an IInsightsComponent. The component will Shutdown().
	 */
	virtual void UnregisterComponent(TSharedPtr<IInsightsComponent> Component) = 0;

	//////////////////////////////////////////////////

	/**
	 * Creates the default trace store (for "Browser" mode).
	 */
	virtual void CreateDefaultStore() = 0;

	/**
	 * Gets the store client.
	 */
	virtual Trace::FStoreClient* GetStoreClient() = 0;

	/**
	 * Connects to a specified store.
	 *
	 * @param InStoreHost The host of the store to connect to.
	 * @param InStorePort The port of the store to connect to.
	 * @return If connected succesfully or not.
	 */
	virtual bool ConnectToStore(const TCHAR* InStoreHost, uint32 InStorePort) = 0;

	//////////////////////////////////////////////////

	/**
	 * Gets the current analysis session.
	 */
	virtual TSharedPtr<const Trace::IAnalysisSession> GetAnalysisSession() const = 0;

	/**
	 * Starts analysis of the specified trace. Called when the application starts in "Viewer" mode.
	 *
	 * @param InTraceId The id of the trace to analyze.
	 */
	virtual void StartAnalysisForTrace(uint32 InTraceId) = 0;

	/**
	 * Starts analysis of the last live session. Called when the application starts in "Viewer" mode.
	 */
	virtual void StartAnalysisForLastLiveSession() = 0;

	/**
	 * Starts analysis of the specified *.utrace file. Called when the application starts in "Viewer" mode.
	 *
	 * @param InTraceFile The filename (*.utrace) of the trace to analyze.
	 */
	virtual void StartAnalysisForTraceFile(const TCHAR* InTraceFile) = 0;

	//////////////////////////////////////////////////

	/**
	 * Registers a major tab layout. This defines how the major tab will appear when spawned.
	 * If this is not called prior to tabs being spawned then the built-in default layout will be used.
	 * @param InMajorTabId The major tab ID we are supplying a layout to
	 * @param InConfig The config to use when spawning the major tab
	 */
	virtual void RegisterMajorTabConfig(const FName& InMajorTabId, const FInsightsMajorTabConfig& InConfig) = 0;

	/**
	 * Unregisters a major tab layout. This will revert the major tab to spawning with its default layout
	 * @param InMajorTabId The major tab ID we are supplying a layout to
	 */
	virtual void UnregisterMajorTabConfig(const FName& InMajorTabId) = 0;

	/**
	 * Allows for registering a delegate callback for populating a FInsightsMajorTabExtender structure.
	 * @param InMajorTabId The major tab ID to register the delegate for
	 */
	virtual FOnRegisterMajorTabExtensions& OnRegisterMajorTabExtension(const FName& InMajorTabId) = 0;

	/** Callback invoked when a major tab is created */
	virtual FOnInsightsMajorTabCreated& OnMajorTabCreated() = 0;

	/** Finds a major tab config for the specified id. */
	virtual const FInsightsMajorTabConfig& FindMajorTabConfig(const FName& InMajorTabId) const = 0;

	/** Sets the ini path for saving persistent layout data. */
	virtual void SetUnrealInsightsLayoutIni(const FString& InIniPath) = 0;

	/**
	 * Called when the application starts in "Browser" mode.
	 */
	virtual void CreateSessionBrowser(bool bAllowDebugTools, bool bSingleProcess) = 0;

	/**
	 * Called when the application starts in "Viewer" mode.
	 */
	virtual void CreateSessionViewer(bool bAllowDebugTools) = 0;

	/**
	 * Called when the application shutsdown.
	 */
	virtual void ShutdownUserInterface() = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTS_API IInsightsComponent
{
public:
	/** Initializes this component. Called by TraceInsights module when this component is registered. */
	virtual void Initialize(IUnrealInsightsModule& Module) = 0;

	/** Shutsdown this component. Called by TraceInsights module when this component is unregistered. */
	virtual void Shutdown() = 0;

	/* Allows this component to register major tabs. */
	virtual void RegisterMajorTabs(IUnrealInsightsModule& InsightsModule) = 0;

	/* Requests this component to unregister its major tabs. */
	virtual void UnregisterMajorTabs() = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
