// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


/**
 * Cluster events messages
 */
struct FDisplayClusterClusterEventsMsg
{
	constexpr static auto ArgName       = TEXT("Name");
	constexpr static auto ArgType       = TEXT("Type");
	constexpr static auto ArgCategory   = TEXT("Category");
	constexpr static auto ArgParameters = TEXT("Parameters");
	constexpr static auto ArgError      = TEXT("Error");
};
