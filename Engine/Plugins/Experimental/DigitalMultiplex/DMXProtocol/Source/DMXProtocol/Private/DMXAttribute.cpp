// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXAttribute.h"
#include "DMXProtocolModule.h"
#include "DMXProtocolSettings.h"

#include "Modules/ModuleManager.h"


IMPLEMENT_DMX_NAMELISTITEM_STATICVARS(FDMXAttributeName)

IMPLEMENT_DMX_NAMELISTITEM_GetAllValues(FDMXAttributeName)
{
	TArray<FName> PossibleValues;

	if (const UDMXProtocolSettings* DMXSettings = GetDefault<UDMXProtocolSettings>())
	{
		PossibleValues.Reserve(DMXSettings->Attributes.Num());

		for (const FDMXAttribute& Attribute : DMXSettings->Attributes)
		{
			PossibleValues.Emplace(Attribute.Name);
		}
	}

	return PossibleValues;
}

IMPLEMENT_DMX_NAMELISTITEM_IsValid(FDMXAttributeName)
{
	const UDMXProtocolSettings* DMXSettings = GetDefault<UDMXProtocolSettings>();
	for (const FDMXAttribute& SettingsAttribute : DMXSettings->Attributes)
	{
		if (InName == SettingsAttribute.Name)
		{
			return true;
		}
	}

	return false;
}

FDMXAttributeName::FDMXAttributeName()
{
	// This depends on the FDMXProtocolModule and can be called
	// on CDO creation, when the module might not be available yet.
	// So we first check if it is available.
	const IModuleInterface* DMXProtocolModule = FModuleManager::Get().GetModule(FDMXProtocolModule::BaseModuleName);
	if (DMXProtocolModule != nullptr)
	{
		if (const UDMXProtocolSettings* DMXSettings = GetDefault<UDMXProtocolSettings>())
		{
			if (DMXSettings->Attributes.Num() > 0)
			{
				Name = DMXSettings->Attributes.begin()->Name;
			}
		}
	}
}

FDMXAttributeName::FDMXAttributeName(const FDMXAttribute& InAttribute)
{
	Name = InAttribute.Name;
}

FDMXAttributeName::FDMXAttributeName(const FName& AttributeName)
{
	const UDMXProtocolSettings* DMXSettings = GetDefault<UDMXProtocolSettings>();
	for (const FDMXAttribute& SettingsAttribute : DMXSettings->Attributes)
	{
		if (SettingsAttribute.Name.IsEqual(AttributeName))
		{
			Name = SettingsAttribute.Name;
			return;
		}
	}

	Name = FDMXNameListItem::None;
}

void FDMXAttributeName::SetFromName(const FName& InName)
{
	*this = InName;
}

const FDMXAttribute& FDMXAttributeName::GetAttribute() const
{
	static const FDMXAttribute FailureAttribute;

	const UDMXProtocolSettings* DMXSettings = GetDefault<UDMXProtocolSettings>();
	if (DMXSettings == nullptr)
	{
		return FailureAttribute;
	}

	for (const FDMXAttribute& SettingsAttribute : DMXSettings->Attributes)
	{
		if (SettingsAttribute.Name.IsEqual(Name))
		{
			return SettingsAttribute;
		}
	}

	return FailureAttribute;
}
