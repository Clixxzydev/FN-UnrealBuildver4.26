// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UICommandList.h"
#include "Logging/LogMacros.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Insights/NetworkingProfiler/NetworkingProfilerCommands.h"

class SNetworkingProfilerWindow;

DECLARE_LOG_CATEGORY_EXTERN(NetworkingProfiler, Log, All);

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * This class manages the Networking Profiler (Networking Insights) state and settings.
 */
class FNetworkingProfilerManager : public TSharedFromThis<FNetworkingProfilerManager>, public IInsightsComponent
{
	friend class FNetworkingProfilerActionManager;

public:
	/** Creates the Networking Profiler manager, only one instance can exist. */
	FNetworkingProfilerManager(TSharedRef<FUICommandList> InCommandList);

	/** Virtual destructor. */
	virtual ~FNetworkingProfilerManager();

	/** Creates an instance of the Networking Profiler manager. */
	static TSharedPtr<FNetworkingProfilerManager> CreateInstance();

	/**
	 * @return the global instance of the Networking Profiler manager.
	 * This is an internal singleton and cannot be used outside TraceInsights.
	 * For external use:
	 *     IUnrealInsightsModule& Module = FModuleManager::Get().LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	 *     Module.GetNetworkingProfilerManager();
	 */
	static TSharedPtr<FNetworkingProfilerManager> Get();

	//////////////////////////////////////////////////
	// IInsightsComponent

	virtual void Initialize(IUnrealInsightsModule& InsightsModule) override;
	virtual void Shutdown() override;
	virtual void RegisterMajorTabs(IUnrealInsightsModule& InsightsModule) override;
	virtual void UnregisterMajorTabs() override;

	//////////////////////////////////////////////////

	/** @returns UI command list for the Networking Profiler manager. */
	const TSharedRef<FUICommandList> GetCommandList() const;

	/** @return an instance of the Networking Profiler commands. */
	static const FNetworkingProfilerCommands& GetCommands();

	/** @return an instance of the Networking Profiler action manager. */
	static FNetworkingProfilerActionManager& GetActionManager();

	void AddProfilerWindow(const TSharedRef<SNetworkingProfilerWindow>& InProfilerWindow)
	{
		ProfilerWindows.Add(InProfilerWindow);
	}

	void RemoveProfilerWindow(const TSharedRef<SNetworkingProfilerWindow>& InProfilerWindow)
	{
		ProfilerWindows.Remove(InProfilerWindow);
	}

	/**
	 * Converts profiler window weak pointer to a shared pointer and returns it.
	 * Make sure the returned pointer is valid before trying to dereference it.
	 */
	TSharedPtr<class SNetworkingProfilerWindow> GetProfilerWindow(int32 Index) const
	{
		return ProfilerWindows[Index].Pin();
	}

	void OnSessionChanged();

private:
	/** Binds our UI commands to delegates. */
	void BindCommands();

	/** Called to spawn the Networking Profiler major tab. */
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

	/** Callback called when the Networking Profiler major tab is closed. */
	void OnTabClosed(TSharedRef<SDockTab> TabBeingClosed);

	/** Updates this manager, done through FCoreTicker. */
	bool Tick(float DeltaTime);

private:
	bool bIsInitialized;
	bool bIsAvailable;
	uint64 AvailabilityCheckNextTimestamp;
	double AvailabilityCheckWaitTimeSec;

	/** The delegate to be invoked when this manager ticks. */
	FTickerDelegate OnTick;

	/** Handle to the registered OnTick. */
	FDelegateHandle OnTickHandle;

	/** List of UI commands for this manager. This will be filled by this and corresponding classes. */
	TSharedRef<FUICommandList> CommandList;

	/** An instance of the Networking Profiler action manager. */
	FNetworkingProfilerActionManager ActionManager;

	/** A list of weak pointers to the Networking Profiler windows. */
	TArray<TWeakPtr<class SNetworkingProfilerWindow>> ProfilerWindows;

	/** A shared pointer to the global instance of the NetworkingProfiler manager. */
	static TSharedPtr<FNetworkingProfilerManager> Instance;
};
