// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterVrpnKeyboardInputDataHolder.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "vrpn/vrpn_Button.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif


/**
 * VRPN button device implementation
 */
class FDisplayClusterVrpnKeyboardInputDevice
	: public FDisplayClusterVrpnKeyboardInputDataHolder
{
public:
	FDisplayClusterVrpnKeyboardInputDevice(const FDisplayClusterConfigInput& Config);
	virtual ~FDisplayClusterVrpnKeyboardInputDevice();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterInputDevice
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void PreUpdate() override;
	virtual void Update() override;
	virtual bool Initialize() override;

private:
	// Data update handler
	static void VRPN_CALLBACK HandleKeyboardDevice(void *UserData, vrpn_BUTTONCB const ButtonData);

private:
	// The device (PIMPL)
	TUniquePtr<vrpn_Button_Remote> DevImpl;
};
