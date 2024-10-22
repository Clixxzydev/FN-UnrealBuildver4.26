// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/Devices/DisplayClusterInputDeviceTraits.h"
#include "Input/Devices/DisplayClusterInputDeviceBase.h"

#include "CoreMinimal.h"


/**
 * VRPN button device data holder. Responsible for data serialization and deserialization.
 */
class FDisplayClusterVrpnButtonInputDataHolder
	: public FDisplayClusterInputDeviceBase<EDisplayClusterInputDeviceType::VrpnButton>
{
public:
	FDisplayClusterVrpnButtonInputDataHolder(const FDisplayClusterConfigInput& Config);
	virtual ~FDisplayClusterVrpnButtonInputDataHolder();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterInputDevice
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Initialize() override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterStringSerializable
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual FString SerializeToString() const override final;
	virtual bool    DeserializeFromString(const FString& Data) override final;

private:
	// Serialization constants
	static constexpr auto SerializationDelimiter = TEXT("@");
	static constexpr auto SerializationItems = 3;
};
