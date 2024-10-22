// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Network/Service/DisplayClusterService.h"
#include "Network/Protocol/IPDisplayClusterClusterSyncProtocol.h"
#include "Network/DisplayClusterMessage.h"
#include "Misc/DisplayClusterBarrier.h"


/**
 * Cluster synchronization service
 */
class FDisplayClusterClusterSyncService
	: public  FDisplayClusterService
	, private IPDisplayClusterClusterSyncProtocol
{
public:
	FDisplayClusterClusterSyncService(const FString& InAddr, const int32 InPort);
	virtual ~FDisplayClusterClusterSyncService();

public:
	virtual bool Start() override;
	void Shutdown() override;

protected:
	virtual TSharedPtr<FDisplayClusterSessionBase> CreateSession(FSocket* InSocket, const FIPv4Endpoint& InEP) override;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterSessionListener
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void NotifySessionOpen(FDisplayClusterSessionBase* InSession) override;
	virtual void NotifySessionClose(FDisplayClusterSessionBase* InSession) override;
	virtual TSharedPtr<FDisplayClusterMessage> ProcessMessage(const TSharedPtr<FDisplayClusterMessage>& Request) override;

private:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterClusterSyncProtocol
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void WaitForGameStart() override;
	virtual void WaitForFrameStart() override;
	virtual void WaitForFrameEnd() override;
	virtual void WaitForTickEnd() override;
	virtual void GetDeltaTime(float& DeltaSeconds) override;
	virtual void GetFrameTime(TOptional<FQualifiedFrameTime>& FrameTime) override;
	virtual void GetSyncData(FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup)  override;
	virtual void GetInputData(FDisplayClusterMessage::DataType& InputData) override;
	virtual void GetEventsData(FDisplayClusterMessage::DataType& EventsData) override;
	virtual void GetNativeInputData(FDisplayClusterMessage::DataType& NativeInputData) override;

private:
	// Game start sync barrier
	FDisplayClusterBarrier BarrierGameStart;
	// Frame start barrier
	FDisplayClusterBarrier BarrierFrameStart;
	// Frame end barrier
	FDisplayClusterBarrier BarrierFrameEnd;
	// Tick end barrier
	FDisplayClusterBarrier BarrierTickEnd;
};
