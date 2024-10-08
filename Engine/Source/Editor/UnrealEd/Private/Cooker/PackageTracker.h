// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Set.h"
#include "CookRequests.h"
#include "CookTypes.h"
#include "HAL/CriticalSection.h"
#include "INetworkFileSystemModule.h"
#include "Misc/ScopeLock.h"
#include "RingBuffer.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/NameTypes.h"
#include "UObject/UObjectArray.h"

class ITargetPlatform;
class UPackage;

namespace UE
{
namespace Cook
{

	struct FPackageDatas;
	struct FRecompileRequest;

	/** Helper to pass a recompile request to game thread */
	struct FRecompileRequest
	{
		struct FShaderRecompileData RecompileData;
		volatile bool bComplete = false;
	};

	template<typename Type>
	struct FThreadSafeQueue
	{
	private:
		mutable FCriticalSection SynchronizationObject; // made this mutable so this class can have const functions and still be thread safe
		TRingBuffer<Type> Items;
	public:
		void Enqueue(const Type& Item)
		{
			FScopeLock ScopeLock(&SynchronizationObject);
			Items.PushBack(Item);
		}

		void EnqueueUnique(const Type& Item)
		{
			FScopeLock ScopeLock(&SynchronizationObject);
			for (const Type& Existing : Items)
			{
				if (Existing == Item)
				{
					return;
				}
			}
			Items.PushBack(Item);
		}

		bool Dequeue(Type* Result)
		{
			FScopeLock ScopeLock(&SynchronizationObject);
			if (Items.Num())
			{
				*Result = Items.PopFrontValue();
				return true;
			}
			return false;
		}

		void DequeueAll(TArray<Type>& Results)
		{
			FScopeLock ScopeLock(&SynchronizationObject);
			Results.Reserve(Results.Num() + Items.Num());
			while (!Items.IsEmpty())
			{
				Results.Add(Items.PopFrontValue());
			}
		}

		bool HasItems() const
		{
			FScopeLock ScopeLock(&SynchronizationObject);
			return Items.Num() > 0;
		}

		void Remove(const Type& Item)
		{
			FScopeLock ScopeLock(&SynchronizationObject);
			Items.Remove(Item);
		}

		void CopyItems(const TArray<Type>& InItems) const
		{
			FScopeLock ScopeLock(&SynchronizationObject);
			Items.Empty(InItems.Num());
			for (const Type& Item : InItems)
			{
				Items.PushBack(Item);
			}
		}

		int Num() const
		{
			FScopeLock ScopeLock(&SynchronizationObject);
			return Items.Num();
		}

		void Empty()
		{
			FScopeLock ScopeLock(&SynchronizationObject);
			Items.Empty();
		}
	};

	/** Simple thread safe proxy for TSet<FName> */
	template <typename T>
	class FThreadSafeSet
	{
		TSet<T> InnerSet;
		FCriticalSection SetCritical;
	public:
		void Add(T InValue)
		{
			FScopeLock SetLock(&SetCritical);
			InnerSet.Add(InValue);
		}
		bool AddUnique(T InValue)
		{
			FScopeLock SetLock(&SetCritical);
			if (!InnerSet.Contains(InValue))
			{
				InnerSet.Add(InValue);
				return true;
			}
			return false;
		}
		bool Contains(T InValue)
		{
			FScopeLock SetLock(&SetCritical);
			return InnerSet.Contains(InValue);
		}
		void Remove(T InValue)
		{
			FScopeLock SetLock(&SetCritical);
			InnerSet.Remove(InValue);
		}
		void Empty()
		{
			FScopeLock SetLock(&SetCritical);
			InnerSet.Empty();
		}

		void GetValues(TSet<T>& OutSet)
		{
			FScopeLock SetLock(&SetCritical);
			OutSet.Append(InnerSet);
		}
	};

	struct FThreadSafeUnsolicitedPackagesList
	{
		void AddCookedPackage(const FFilePlatformRequest& PlatformRequest);
		void GetPackagesForPlatformAndRemove(const ITargetPlatform* Platform, TArray<FName>& PackageNames);
		void Empty();

	private:
		FCriticalSection				SyncObject;
		TArray<FFilePlatformRequest>	CookedPackages;
	};

	struct FPackageTracker : public FUObjectArray::FUObjectCreateListener, public FUObjectArray::FUObjectDeleteListener
	{
	public:
		FPackageTracker(FPackageDatas& InPackageDatas);
		~FPackageTracker();

		/* Returns all packages that have been loaded since the last time GetNewPackages was called */
		TArray<UPackage*> GetNewPackages();

		virtual void NotifyUObjectCreated(const class UObjectBase* Object, int32 Index) override;
		virtual void NotifyUObjectDeleted(const class UObjectBase* Object, int32 Index) override;
		virtual void OnUObjectArrayShutdown() override;

		// This is the set of packages which have already had PostLoadFixup called 
		TSet<UPackage*>			PostLoadFixupPackages;

		// This is a complete list of currently loaded UPackages
		TFastPointerSet<UPackage*> LoadedPackages;

		// This list contains the UPackages loaded since last call to GetNewPackages
		TArray<UPackage*>		NewPackages;

		/** The package currently being loaded at CookOnTheFlyServer's direct request. Used to determine which load dependencies were not preloaded. */
		FPackageData* LoadingPackageData = nullptr;

		FPackageDatas& PackageDatas;

		FThreadSafeUnsolicitedPackagesList UnsolicitedCookedPackages;
		FThreadSafeQueue<FRecompileRequest*> RecompileRequests;

		FThreadSafeSet<FName> NeverCookPackageList;
		FThreadSafeSet<FName> UncookedEditorOnlyPackages; // set of packages that have been rejected due to being referenced by editor-only properties
		TFastPointerMap<const ITargetPlatform*, TSet<FName>> PlatformSpecificNeverCookPackages;
	}; 
}
}
