// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cluster/Controller/DisplayClusterNodeCtrlBase.h"


/**
 * Abstract cluster node controller (cluster mode).
 */
class FDisplayClusterClusterNodeCtrlBase
	: public FDisplayClusterNodeCtrlBase
{
public:
	FDisplayClusterClusterNodeCtrlBase(const FString& CtrlName, const FString& NodeName);
	virtual ~FDisplayClusterClusterNodeCtrlBase() = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterNodeController
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool IsStandalone() const override final
	{ return false; }

protected:
	virtual void OverrideMasterAddr(FString& Addr)
	{ }
};
