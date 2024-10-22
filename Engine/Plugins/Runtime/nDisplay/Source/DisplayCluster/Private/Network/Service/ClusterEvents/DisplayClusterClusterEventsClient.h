// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Network/DisplayClusterClient.h"
#include "Network/Protocol/IPDisplayClusterClusterEventsProtocol.h"


/**
 * Cluster events synchronization client
 */
class FDisplayClusterClusterEventsClient
	: public FDisplayClusterClient
	, public IPDisplayClusterClusterEventsProtocol
{
public:
	FDisplayClusterClusterEventsClient();
	FDisplayClusterClusterEventsClient(const FString& InName);

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterClusterEventsProtocol
	//////////////////////////////////////////////////////////////////////////////////////////////
	void EmitClusterEvent(const FDisplayClusterClusterEvent& Event) override;
};
