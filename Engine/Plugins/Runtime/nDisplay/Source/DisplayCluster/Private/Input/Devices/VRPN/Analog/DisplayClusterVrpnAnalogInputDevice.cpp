// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/Devices/VRPN/Analog/DisplayClusterVrpnAnalogInputDevice.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"


FDisplayClusterVrpnAnalogInputDevice::FDisplayClusterVrpnAnalogInputDevice(const FDisplayClusterConfigInput& Config) :
	FDisplayClusterVrpnAnalogInputDataHolder(Config)
{
}

FDisplayClusterVrpnAnalogInputDevice::~FDisplayClusterVrpnAnalogInputDevice()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterInputDevice
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterVrpnAnalogInputDevice::Update()
{
	if (DevImpl)
	{
		UE_LOG(LogDisplayClusterInputVRPN, Verbose, TEXT("Updating device: %s"), *GetId());
		DevImpl->mainloop();
	}
}

bool FDisplayClusterVrpnAnalogInputDevice::Initialize()
{
	FString Addr;
	if (!DisplayClusterHelpers::str::ExtractValue(ConfigData.Params, FString(DisplayClusterStrings::cfg::data::input::Address), Addr))
	{
		UE_LOG(LogDisplayClusterInputVRPN, Error, TEXT("%s - device address not found"), *ToString());
		return false;
	}

	// Instantiate device implementation
	DevImpl.Reset(new vrpn_Analog_Remote(TCHAR_TO_UTF8(*Addr)));
	
	// Register update handler
	if (DevImpl->register_change_handler(this, &FDisplayClusterVrpnAnalogInputDevice::HandleAnalogDevice) != 0)
	{
		UE_LOG(LogDisplayClusterInputVRPN, Error, TEXT("%s - couldn't register VRPN change handler"), *ToString());
		return false;
	}

	// Base initialization
	return FDisplayClusterVrpnAnalogInputDataHolder::Initialize();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterVrpnAnalogInputDevice
//////////////////////////////////////////////////////////////////////////////////////////////
void VRPN_CALLBACK FDisplayClusterVrpnAnalogInputDevice::HandleAnalogDevice(void * UserData, vrpn_ANALOGCB const AnalogData)
{
	auto Dev = reinterpret_cast<FDisplayClusterVrpnAnalogInputDevice*>(UserData);

	for (int32 i = 0; i < AnalogData.num_channel; ++i)
	{
		auto Item = Dev->DeviceData.Find(i);
		if (!Item)
		{
			Item = &Dev->DeviceData.Add(i);
		}

		Item->AxisValue = static_cast<float>(AnalogData.channel[i]);
		UE_LOG(LogDisplayClusterInputVRPN, VeryVerbose, TEXT("Axis %s:%d - %f"), *Dev->GetId(), i, Item->AxisValue);
	}
}
