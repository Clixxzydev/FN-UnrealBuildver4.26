// Copyright Epic Games, Inc. All Rights Reserved.
#include "Channel.h"
#include "UObject/NameTypes.h"
#include "Misc/DateTime.h"

namespace Trace {

const FName FChannelProvider::ProviderName("ChannelProvider");

///////////////////////////////////////////////////////////////////////////////
FChannelProvider::FChannelProvider()
{
	TimeStamp = FDateTime::Now();
}

///////////////////////////////////////////////////////////////////////////////
void FChannelProvider::AnnounceChannel(const TCHAR* InChannelName, uint32 Id)
{
	FString ChannelName(InChannelName);
	ChannelName.GetCharArray()[0] = TChar<TCHAR>::ToUpper(ChannelName.GetCharArray()[0]);
	Channels.Add(FChannelEntry{
		Id,
		ChannelName,
		false,
	});

	TimeStamp = FDateTime::Now();
}

///////////////////////////////////////////////////////////////////////////////
void FChannelProvider::UpdateChannel(uint32 Id, bool bEnabled)
{
	const auto FoundEntry = Channels.FindByPredicate([Id](const FChannelEntry& Entry) {
		return Entry.Id == Id;
	});

	if (FoundEntry)
	{
		FoundEntry->bIsEnabled = bEnabled;
	}

	TimeStamp = FDateTime::Now();
}

///////////////////////////////////////////////////////////////////////////////
uint64 FChannelProvider::GetChannelCount() const
{
	return Channels.Num();
}

///////////////////////////////////////////////////////////////////////////////
const TArray<FChannelEntry>& FChannelProvider::GetChannels() const
{
	return Channels;
}

FDateTime FChannelProvider::GetTimeStamp() const
{
	return TimeStamp;
}

}
