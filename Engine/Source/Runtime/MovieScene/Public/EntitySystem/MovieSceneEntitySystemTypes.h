// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityIDs.h"
#include "Algo/Find.h"
#include "Stats/Stats2.h"
#include "Misc/EnumClassFlags.h"
#include "Containers/ArrayView.h"

#ifndef UE_MOVIESCENE_ENTITY_DEBUG
	#define UE_MOVIESCENE_ENTITY_DEBUG !UE_BUILD_SHIPPING
#endif


DECLARE_STATS_GROUP(TEXT("Movie Scene Evaluation Systems"), STATGROUP_MovieSceneECS, STATCAT_Advanced)

namespace UE
{
namespace MovieScene
{

enum class ESystemPhase : uint8
{
	/** Null phase which indicates that the system never runs, but still exists in the reference graph */
	None,

	/** Expensive: Phase that is run before instantiation any time any boundary is crossed in the sequence. Used to spawn new objects and trigger pre/post-spawn events. */
	Spawn,

	/** Expensive: Houses any system that needs to instantiate global entities into the linker, or make meaningful changes to entity structures.. */
	Instantiation,

	/** Fast, distributed: Houses the majority of evaluation systems that compute animation data. Entity manager is locked down for the duration of this phase. */
	Evaluation,

	/** Finalization phase for enything that wants to run after everything else. */
	Finalization,
};

enum class EComponentTypeFlags : uint8
{
	None = 0x00,

	/** This component type should be preserved when an entity is replaced with another during linking */
	Preserved = 0x1,

	/** Automatically copy this component to child components when being constructed through the component factory */
	CopyToChildren = 0x2,

	/** Indicates that this component type represents a cached value that should be migrated to blend outputs (and removed from blend inputs) */
	MigrateToOutput = 0x4,
};
ENUM_CLASS_FLAGS(EComponentTypeFlags);


enum class EComplexFilterMode : uint8
{
	OneOf       = 1 << 0,
	OneOrMoreOf = 1 << 1,
	AllOf       = 1 << 2,

	// High bit modifiers
	Negate      = 1 << 7, 
};
ENUM_CLASS_FLAGS(EComplexFilterMode);


struct FInterrogationChannel
{
	static constexpr uint32 INVALID_CHANNEL = ~0u;

	FInterrogationChannel() : Value(INVALID_CHANNEL) {}

	FInterrogationChannel operator++()
	{
		check( Value != (INVALID_CHANNEL-1) );
		return FInterrogationChannel(++Value);
	}

	explicit operator bool() const
	{
		return IsValid();
	}

	bool IsValid() const
	{
		return Value != INVALID_CHANNEL;
	}

	uint32 AsIndex() const
	{
		check(Value != INVALID_CHANNEL);
		return Value;
	}

	static FInterrogationChannel First()
	{
		return FInterrogationChannel(0);
	}

	static FInterrogationChannel Last()
	{
		return FInterrogationChannel(INVALID_CHANNEL);
	}

	friend uint32 GetTypeHash(FInterrogationChannel In)
	{
		return In.Value;
	}

	friend bool operator==(FInterrogationChannel A, FInterrogationChannel B)
	{
		return A.Value == B.Value;
	}

	friend bool operator!=(FInterrogationChannel A, FInterrogationChannel B)
	{
		return A.Value != B.Value;
	}

private:

	explicit FInterrogationChannel(uint32 InValue) : Value(InValue) {}

	uint32 Value;
};

struct MOVIESCENE_API FEntityComponentFilter
{

	void Reset()
	{
		AllMask.Reset();
		NoneMask.Reset();
		ComplexMasks.Reset();
	}

	bool Match(const FComponentMask& Input) const;

	bool IsValid() const;

	FEntityComponentFilter& All(const FComponentMask& InComponentMask)
	{
		AllMask.CombineWithBitwiseOR(InComponentMask, EBitwiseOperatorFlags::MaxSize);
		return *this;
	}
	FEntityComponentFilter& All(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		AllMask.SetAll(InComponentTypes);
		return *this;
	}


	FEntityComponentFilter& None(const FComponentMask& InComponentMask)
	{
		NoneMask.CombineWithBitwiseOR(InComponentMask, EBitwiseOperatorFlags::MaxSize);
		return *this;
	}
	FEntityComponentFilter& None(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		NoneMask.SetAll(InComponentTypes);
		return *this;
	}


	FEntityComponentFilter& Any(const FComponentMask& InComponentMask)
	{
		return Complex(InComponentMask, EComplexFilterMode::OneOrMoreOf);
	}
	FEntityComponentFilter& Any(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		return Complex(InComponentTypes, EComplexFilterMode::OneOrMoreOf);
	}

	FEntityComponentFilter& AnyLenient(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		FComponentMask Mask;
		for (FComponentTypeID TypeID : InComponentTypes)
		{
			if (TypeID)
			{
				Mask.Set(TypeID);
			}
		}
		return Any(Mask);
	}


	FEntityComponentFilter& Deny(std::initializer_list<FComponentTypeID> InComponentTypes)
	{
		return Complex(InComponentTypes, EComplexFilterMode::AllOf | EComplexFilterMode::Negate);
	}

	FEntityComponentFilter& Deny(const FComponentMask& InComponentMask)
	{
		return Complex(InComponentMask, EComplexFilterMode::AllOf | EComplexFilterMode::Negate);
	}


	FEntityComponentFilter& Complex(std::initializer_list<FComponentTypeID> InComponentTypes, EComplexFilterMode ComplexMode)
	{
		if (InComponentTypes.size() > 0)
		{
			FComplexMask& ComplexMask = ComplexMasks.Emplace_GetRef(ComplexMode);
			ComplexMask.Mask.SetAll(InComponentTypes);
		}
		return *this;
	}

	FEntityComponentFilter& Complex(const FComponentMask& InComponentMask, EComplexFilterMode ComplexMode)
	{
		if (InComponentMask.Num() > 0)
		{
			ComplexMasks.Emplace_GetRef(InComponentMask, ComplexMode);
		}
		return *this;
	}

	FEntityComponentFilter& Combine(const FEntityComponentFilter& CombineWith)
	{
		if (CombineWith.AllMask.Num() > 0)
		{
			AllMask.CombineWithBitwiseOR(CombineWith.AllMask, EBitwiseOperatorFlags::MaxSize);
		}

		if (CombineWith.NoneMask.Num() > 0)
		{
			NoneMask.CombineWithBitwiseOR(CombineWith.NoneMask, EBitwiseOperatorFlags::MaxSize);
		}

		if (CombineWith.ComplexMasks.Num() > 0)
		{
			ComplexMasks.Append(CombineWith.ComplexMasks);
		}
		return *this;
	}

private:

	struct FComplexMask
	{
		FComplexMask(EComplexFilterMode InMode)
			: Mode(InMode)
		{}
		FComplexMask(const FComponentMask& InMask, EComplexFilterMode InMode)
			: Mask(InMask), Mode(InMode)
		{}

		FComponentMask Mask;
		EComplexFilterMode Mode;
	};
	FComponentMask AllMask;
	FComponentMask NoneMask;
	TArray<FComplexMask> ComplexMasks;
};

struct FComponentHeader
{
	mutable uint8* Components;

	mutable FRWLock ReadWriteLock;

private:

	uint64 SerialNumber;

public:

	uint8 Sizeof;
	FComponentTypeID ComponentType;

	/**
	 * Whether this component header describes a tag component (i.e. a component with no data).
	 */
	bool IsTag() const
	{
		return Sizeof == 0;
	}

	/**
	 * Whether this component header is associated with a data buffer.
	 *
	 * Tag components don't have data. Non-tag components could have no data if their data buffer
	 * has been relocated, such as an entity allocation that has moved elsewhere because of a
	 * migration or mutation.
	 */
	bool HasData() const
	{
		return Components != nullptr;
	}

	/**
	 * Get the raw pointer to the associated component data buffer.
	 */
	void* GetValuePtr(int32 Offset) const
	{
		check(!IsTag() && Components != nullptr);
		return Components + Sizeof*Offset;
	}

	void PostWriteComponents(uint64 InSystemSerial)
	{
		SerialNumber = FMath::Max(SerialNumber, InSystemSerial);
	}

	bool HasBeenWrittenToSince(uint64 InSystemSerial) const
	{
		return SerialNumber > InSystemSerial;
	}
};


/**
 * FEntityAllocation is the authoritative storage of entity-component data within an FEntityManager. It stores component data in separate contiguous arrays, aligned to a cache line.
 * Storing component data in this way allows for cache-efficient and concurrent access to each component array in isolation. It also allows for write access to component arrays
 * at the same time as concurrent read-access to other component arrays within the same entity allocation.
 *
 * FEntityAllocations are custom allocated according to the size of its component capacity, which is loosely computed as sizeof(FEntityAllocation) + sizeof(ComponentData), not simply sizeof(FEntityAllocation).
 *
 * A typical allocation will look like this in memory:
 *
 *    uint32 {UniqueID}, uint16 {NumComponents}, uint16 {Size}, uint16 {Capacity}, uint16 {MaxCapacity}, uint32 {SerialNumber},
 *    FMovieSceneEntityID* {EntityIDs},   <-- points to FMovieSceneEntityID array at end of structure 
 *    FComponentHeader[NumComponents],    <-- each component header contains a component array ptr that points to its corresponding type array below
 *    (padding) FMovieSceneEntityID[Capacity],
 *    (padding) ComponentType1[Capacity],
 *    (padding) ComponentType2[Capacity],
 *    (padding) ComponentType3[Capacity],
 */
struct FEntityAllocation
{
	/**
	 * Constructor that initializes the defaults for this structure.
	 * CAUTION: Does not initialize ComponentHeaders - these constructors must be called manually
	 */
	FEntityAllocation()
		: SerialNumber(0)
		, NumComponents(0)
		, Size(0)
		, Capacity(0)
		, MaxCapacity(0)
	{}

	/**
	 * Manually invoked destructor that calls the destructor of each component header according to the number of components
	 */
	~FEntityAllocation()
	{
		for (int32 Index = 0; Index < NumComponents; ++Index)
		{
			ComponentHeaders[Index].~FComponentHeader();
		}
	}

	/** Entity allocations are non-copyable */
	FEntityAllocation(const FEntityAllocation&) = delete;
	FEntityAllocation& operator=(const FEntityAllocation&) = delete;


	/**
	 * Retrieve all of this allocation's component and tag headers.
	 */
	TArrayView<const FComponentHeader> GetComponentHeaders() const
	{
		return MakeArrayView(ComponentHeaders, NumComponents);
	}


	/**
	 * Retrieve all of this allocation's component and tag headers.
	 */
	TArrayView<FComponentHeader> GetComponentHeaders()
	{
		return MakeArrayView(ComponentHeaders, NumComponents);
	}


	/**
	 * Check whether this allocation has the specified component type
	 *
	 * @param ComponentTypeID The type ID for the component to check
	 * @return true if these entities have the specified component, false otherwise
	 */
	bool HasComponent(FComponentTypeID ComponentTypeID) const
	{
		return FindComponentHeader(ComponentTypeID) != nullptr;
	}


	/**
	 * Find a component header by its type
	 *
	 * @param ComponentTypeID The type ID for the component header to locate
	 * @return A pointer to the component header, or nullptr if one was not found
	 */
	const FComponentHeader* FindComponentHeader(FComponentTypeID ComponentTypeID) const
	{
		return Algo::FindBy(GetComponentHeaders(), ComponentTypeID, &FComponentHeader::ComponentType);
	}


	/**
	 * Find a component header by its type
	 *
	 * @param ComponentTypeID The type ID for the component header to locate
	 * @return A pointer to the component header, or nullptr if one was not found
	 */
	FComponentHeader* FindComponentHeader(FComponentTypeID ComponentTypeID)
	{
		return Algo::FindBy(GetComponentHeaders(), ComponentTypeID, &FComponentHeader::ComponentType);
	}


	/**
	 * Get a reference to a component header by its type. Will fail an assertion if it does not exist.
	 *
	 * @param ComponentTypeID The type ID for the component header to locate
	 * @return A reference to the component header
	 */
	const FComponentHeader& GetComponentHeaderChecked(FComponentTypeID ComponentTypeID) const
	{
		const FComponentHeader* Header = FindComponentHeader(ComponentTypeID);
		check(Header);
		return *Header;
	}


	/**
	 * Get a reference to a component header by its type. Will fail an assertion if it does not exist.
	 *
	 * @param ComponentTypeID The type ID for the component header to locate
	 * @return A reference to the component header
	 */
	FComponentHeader& GetComponentHeaderChecked(FComponentTypeID ComponentTypeID)
	{
		FComponentHeader* Header = FindComponentHeader(ComponentTypeID);
		check(Header);
		return *Header;
	}


	/**
	 * Retrieve all of this allocation's entity IDs
	 */
	TArrayView<const FMovieSceneEntityID> GetEntityIDs() const
	{
		return MakeArrayView(EntityIDs, Size);
	}


	/**
	 * Retrieve all of this allocation's entity IDs as a raw ptr
	 */
	const FMovieSceneEntityID* GetRawEntityIDs() const
	{
		return EntityIDs;
	}


	/**
	 * Get the unique identifier for this allocation. This identifier is unique to the specific allocation and entity manager, but is not globally unique.
	 * Typically used for caching component data on a per-allocation basis
	 */
	uint32 GetUniqueID() const
	{
		return UniqueID;
	}


	/**
	 * Retrieve this allocation's serial number. The serial number is incremented whenever a component is modified on this allocation, or when an entity is added or removed.
	 * Typically used for caching component data on a per-allocation basis
	 */
	bool HasStructureChangedSince(uint64 InSystemVersion) const
	{
		return SerialNumber > InSystemVersion;
	}


	/**
	 * Called when this allocation has been modified. Will invalidate any cached data based of this allocation's serial number
	 */
	void PostModifyStructureExcludingHeaders(uint64 InSystemSerial)
	{
		SerialNumber = FMath::Max(SerialNumber, InSystemSerial);
	}


	void PostModifyStructure(uint64 InSystemSerial)
	{
		SerialNumber = FMath::Max(SerialNumber, InSystemSerial);
		for (FComponentHeader& Header : GetComponentHeaders())
		{
			Header.PostWriteComponents(InSystemSerial);
		}
	}

	/**
	 * Get the number of component types and tags that exist within this allocation
	 */
	int32 GetNumComponentTypes() const
	{
		return int32(NumComponents);
	}


	/**
	 * Retrieve the number of entities in this allocation
	 */
	int32 Num() const
	{
		return int32(Size);
	}


	/**
	 * Retrieve the maximum number of entities that this allocation is allowed to grow to until a new one must be made
	 */
	int32 GetMaxCapacity() const
	{
		return int32(MaxCapacity);
	}


	/**
	 * Retrieve the number of entities this allocation can currently house without reallocation
	 */
	int32 GetCapacity() const
	{
		return int32(Capacity);
	}


	/**
	 * Retrieve the amount of empty space within this allocation
	 */
	int32 GetSlack() const
	{
		return int32(Capacity) - int32(Size);
	}

private:

	friend struct FEntityInitializer;

	/** Assigned to FEntityManager::GetSystemSerial whenever this allocation is written to */
	uint64 SerialNumber;
	/** Unique Identifier within this allocation's FEntityManager. This ID is never reused. */
	uint32 UniqueID;
	/** The number of component and tag types in this allocation (also defines the number of ComponentHeaders). */
	uint16 NumComponents;
	/** The number of entities currently allocated within this block. Defines the stride of each component array. */
	uint16 Size;
	/** The maximum number of entities currently allocated within this block including slack. Defines the maximum stride of each component array. */
	uint16 Capacity;
	/** The maximum number of entities that this entity is allowed to reallocate to accomodate for. */
	uint16 MaxCapacity;

	/** Pointer to the entity ID array (stored in the end padding of this structure). */
	FMovieSceneEntityID* EntityIDs;

	/** Pointer to separately allocated data buffer for components. */
	uint8* ComponentData;

public:
	/** Pointer to array of the component headers of size NumComponents (stored in the end padding of this structure). */
	FComponentHeader* ComponentHeaders;
};


/**
 * Defines a contiguous range of entities within an allocation
 */
struct FEntityRange
{
	const FEntityAllocation* Allocation = nullptr;
	int32 ComponentStartOffset = 0;
	int32 Num = 0;
};

struct FEntityDataLocation
{
	FEntityAllocation* Allocation;
	int32 ComponentOffset;
};

struct FEntityInfo
{
	FEntityDataLocation Data;
	FMovieSceneEntityID EntityID;
};

} // namespace MovieScene
} // namespace UE

