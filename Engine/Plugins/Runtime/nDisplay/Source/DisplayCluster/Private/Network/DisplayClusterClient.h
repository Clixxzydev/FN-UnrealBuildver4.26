// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Network/DisplayClusterMessage.h"
#include "Network/DisplayClusterSocketOps.h"

#include "Misc/DisplayClusterConstants.h"


/**
 * TCP client
 */
class FDisplayClusterClient
	: protected FDisplayClusterSocketOps
{
public:
	FDisplayClusterClient(const FString& InName);
	virtual ~FDisplayClusterClient();

public:
	// Connects to a server
	bool Connect(const FString& InAddr, const int32 InPort, const int32 TriesAmount, const float TryDelay);
	// Terminates current connection
	void Disconnect();

	virtual bool SendMsg(const TSharedPtr<FDisplayClusterMessage>& Msg) override final;
	virtual TSharedPtr<FDisplayClusterMessage> RecvMsg() override final;

	TSharedPtr<FDisplayClusterMessage> SendRecvMsg(const TSharedPtr<FDisplayClusterMessage>& Msg);

	virtual FString GetName() const override final
	{ return Name; }

	inline bool IsConnected() const
	{ return IsOpen(); }

protected:
	// Creates client socket
	FSocket* CreateSocket(const FString& InName);

private:
	// Client name
	const FString Name;
};

