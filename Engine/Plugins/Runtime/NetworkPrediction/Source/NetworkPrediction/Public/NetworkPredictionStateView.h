// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Generic view into a managed instance's state
struct FNetworkPredictionStateView
{
	// Simulation PendingFrame number. This is the "server frame" number that will be used as input for the next tick.
	// This can be used for server authoritative timers/countdowns etc but should not be used to index into local frame
	// buffer storage. Local frame numbers are stored on the NetworkPredictionWorldManager's internal tick states.
	int32 PendingFrame = 0;

	// ::SimulationTick is in progress
	bool bTickInProgress = false;
		
	// Pending states: these are what will be used as input into the next SimulationTick call, if we are running a local tick.
	// if there is no local tick, for example in interpolation mode, these will set to the latest consume simulation frame.
	// (so, latest simulation frame used in interpolation for example. But not necessarily the latest received frame)
	void* PendingInputCmd = nullptr;
	void* PendingSyncState = nullptr;
	void* PendingAuxState = nullptr;

	// Presentation states: the latest locally smoothed/interpolated states that will not be fed back into the sim 
	// (these will be null in cases where there is no smoothing/interpolation)
	void* PresentationSyncState = nullptr;
	void* PresentationAuxState = nullptr;

	void UpdateView(int32 Frame, void* Input, void* Sync, void* Aux)
	{
		PendingFrame = Frame;
		PendingInputCmd = Input;
		PendingSyncState = Sync;
		PendingAuxState = Aux;
	}

	void UpdatePresentationView(void* Sync, void* Aux)
	{
		PresentationSyncState = Sync;
		PresentationAuxState = Aux;
	}

	void ClearPresentationView()
	{
		PresentationSyncState = nullptr;
		PresentationAuxState = nullptr;
	}
};