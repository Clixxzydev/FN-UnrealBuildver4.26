// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FMemoryGraphTrack;

namespace Insights
{

typedef int64 FMemoryTagId;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemoryTag
{
	friend class FMemoryTagList;

public:
	static const FMemoryTagId InvalidTagId = -1;

public:
	//FMemoryTag();
	//~FMemoryTag();

	int32 GetIndex() const { return Index; }
	FMemoryTagId GetId() const { return Id; }
	FMemoryTagId GetParentId() const { return ParentId; }

	const FString& GetStatName() const { return StatName; }
	const FString& GetStatFullName() const { return StatFullName; }

	bool MatchesWildcard(const FString& FullName) const;
	bool MatchesWildcard(const TArray<FString>& FullNames) const;

	uint64 GetTrackers() const { return Trackers; }

	const FLinearColor& GetColor() const { return Color; }
	void SetColor(FLinearColor InColor) { Color = InColor; }
	void SetColorAuto();

	FMemoryTag* GetParent() const { return Parent; }
	const TSet<FMemoryTag*>& GetChildren() const { return Children; }

	bool IsAddedToGraph() const { return Tracks.Num() > 0; }
	const TSet<TSharedPtr<FMemoryGraphTrack>>& GetGraphTracks() const { return Tracks; }
	void AddTrack(TSharedPtr<FMemoryGraphTrack> InTrack) { Tracks.Add(InTrack); }
	void RemoveTrack(TSharedPtr<FMemoryGraphTrack> InTrack) { Tracks.Remove(InTrack); }
	void RemoveAllTracks() { Tracks.Reset(); }

private:
	int32 Index = -1;
	FMemoryTagId Id = InvalidTagId; // LLM tag id
	FMemoryTagId ParentId = InvalidTagId;
	FString StatName;
	FString StatFullName; // includes the parent prefix
	uint64 Trackers = 0; // bit flags for trackers
	FLinearColor Color;
	FMemoryTag* Parent = nullptr;
	TSet<FMemoryTag*> Children;
	TSet<TSharedPtr<FMemoryGraphTrack>> Tracks; // tracks using this LLM tag
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemoryTagList
{
public:
	FMemoryTagList();
	~FMemoryTagList();

	uint32 GetSerialNumber() const { return SerialNumber; }
	const TArray<FMemoryTag*>& GetTags() const { return Tags; }

	const FMemoryTag* GetTagById(FMemoryTagId InTagId) const { return TagIdMap.FindRef(InTagId); }

	FMemoryTag* GetTagById(FMemoryTagId InTagId) { return TagIdMap.FindRef(InTagId); }

	void Reset();
	void Update();

	/**
	 * Filters the list of tags using wildcard matching (on tag's full name).
	 * Returns the number of tags added to the output tags array.
	 */
	int32 FilterTags(const TArray<FString>& InIncludeStats, const TArray<FString>& InIgnoreStats, TArray<FMemoryTag*>& OutTags) const;

private:
	void UpdateInternal();

private:
	TArray<FMemoryTag*> Tags; // the list of memory tags; owns the allocated memory
	TMap<uint32, FMemoryTag*> TagIdMap;
	uint32 LastTraceSerialNumber;
	uint32 SerialNumber;
	uint64 NextUpdateTimestamp;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
