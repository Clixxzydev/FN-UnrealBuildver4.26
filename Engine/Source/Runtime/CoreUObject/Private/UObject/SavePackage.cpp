// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/SavePackage.h"

#if UE_WITH_SAVEPACKAGE
#include "CoreMinimal.h"
#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Stats/Stats.h"
#include "Async/AsyncWork.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/LargeMemoryReader.h"
#include "Serialization/BufferArchive.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/ObjectThumbnail.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/Object.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "Serialization/PropertyLocalizationDataGathering.h"
#include "UObject/Package.h"
#include "Templates/Casts.h"
#include "UObject/LazyObjectPtr.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "UObject/ObjectRedirector.h"
#include "Misc/PackageName.h"
#include "Serialization/BulkData.h"
#include "UObject/PackageFileSummary.h"
#include "UObject/ObjectResource.h"
#include "UObject/Linker.h"
#include "UObject/LinkerLoad.h"
#include "UObject/LinkerSave.h"
#include "UObject/EditorObjectVersion.h"
#include "Blueprint/BlueprintSupport.h"
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "UObject/Interface.h"
#include "Interfaces/ITargetPlatform.h"
#include "UObject/UObjectThreadContext.h"
#include "UObject/GCScopeLock.h"
#include "ProfilingDebugging/CookStats.h"
#include "UObject/DebugSerializationFlags.h"
#include "UObject/EnumProperty.h"
#include "HAL/IConsoleManager.h"
#include "Serialization/ArchiveStackTrace.h"
#include "UObject/CoreRedirects.h"
#include "Serialization/ArchiveObjectCrc32.h"
#include "Serialization/Formatters/BinaryArchiveFormatter.h"
#include "Serialization/Formatters/JsonArchiveOutputFormatter.h"
#include "Serialization/ArchiveUObjectFromStructuredArchive.h"
#include "Serialization/UnversionedPropertySerialization.h"
#include "UObject/AsyncWorkSequence.h"
#include "Serialization/BulkDataManifest.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogSavePackage, Log, All);

#if UE_TRACE_ENABLED && !UE_BUILD_SHIPPING
UE_TRACE_CHANNEL(SaveTimeChannel)
#define SCOPED_SAVETIMER(TimerName) TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(TimerName, SaveTimeChannel)
#else
#define SCOPED_SAVETIMER(TimerName)
#endif

static const FName WorldClassName = FName("World");
static const FName PrestreamPackageClassName = FName("PrestreamPackage");
static FCriticalSection InitializeCoreClassesCritSec;

#define VALIDATE_INITIALIZECORECLASSES 0
#define EXPORT_SORTING_DETAILED_LOGGING 0

static void SaveThumbnails(UPackage* InOuter, FLinkerSave* Linker, FStructuredArchive::FSlot Slot);
static void SaveAssetRegistryData(UPackage* InOuter, FLinkerSave* Linker, FStructuredArchive::FSlot Slot);
static void SaveBulkData(FLinkerSave* Linker, const UPackage* InOuter, const TCHAR* Filename, const ITargetPlatform* TargetPlatform,
						 FSavePackageContext* SavePackageContext, const bool bTextFormat, const bool bDiffing, const bool bComputeHash, TAsyncWorkSequence<FMD5>& AsyncWriteAndHashSequence, int64& TotalPackageSizeUncompressed);
static void SaveWorldLevelInfo(UPackage* InOuter, FLinkerSave* Linker, FStructuredArchive::FRecord Record);
static EObjectMark GetExcludedObjectMarksForTargetPlatform(const class ITargetPlatform* TargetPlatform, const bool bIsCooking);

#if ENABLE_COOK_STATS
#include "ProfilingDebugging/ScopedTimers.h"

namespace SavePackageStats
{
	static int32 NumPackagesSaved = 0;
	static double SavePackageTimeSec = 0.0;
	static double TagPackageExportsPresaveTimeSec = 0.0;
	static double TagPackageExportsTimeSec = 0.0;
	static double FullyLoadLoadersTimeSec = 0.0;
	static double ResetLoadersTimeSec = 0.0;
	static double TagPackageExportsGetObjectsWithOuter = 0.0;
	static double TagPackageExportsGetObjectsWithMarks = 0.0;
	static double SerializeImportsTimeSec = 0.0;
	static double SortExportsSeekfreeInnerTimeSec = 0.0;
	static double SerializeExportsTimeSec = 0.0;
	static double SerializeBulkDataTimeSec = 0.0;
	static double AsyncWriteTimeSec = 0.0;
	static double MBWritten = 0.0;
	TMap<FName, FArchiveDiffStats> PackageDiffStats;
	static int32 NumberOfDifferentPackages = 0;
	void AddSavePackageStats(FCookStatsManager::AddStatFuncRef AddStat)
	{
		// Don't use FCookStatsManager::CreateKeyValueArray because there's just too many arguments. Don't need to overburden the compiler here.
		TArray<FCookStatsManager::StringKeyValue> StatsList;
		StatsList.Empty(15);
		#define ADD_COOK_STAT(Name) StatsList.Emplace(TEXT(#Name), LexToString(Name))
		ADD_COOK_STAT(NumPackagesSaved);
		ADD_COOK_STAT(SavePackageTimeSec);
		ADD_COOK_STAT(TagPackageExportsPresaveTimeSec);
		ADD_COOK_STAT(TagPackageExportsTimeSec);
		ADD_COOK_STAT(FullyLoadLoadersTimeSec);
		ADD_COOK_STAT(ResetLoadersTimeSec);
		ADD_COOK_STAT(TagPackageExportsGetObjectsWithOuter);
		ADD_COOK_STAT(TagPackageExportsGetObjectsWithMarks);
		ADD_COOK_STAT(SerializeImportsTimeSec);
		ADD_COOK_STAT(SortExportsSeekfreeInnerTimeSec);
		ADD_COOK_STAT(SerializeExportsTimeSec);
		ADD_COOK_STAT(SerializeBulkDataTimeSec);
		ADD_COOK_STAT(AsyncWriteTimeSec);
		ADD_COOK_STAT(MBWritten);

		AddStat(TEXT("Package.Save"), StatsList);

		{
			PackageDiffStats.ValueSort([](const FArchiveDiffStats& Lhs, const FArchiveDiffStats& Rhs){ return Lhs.NewFileTotalSize > Rhs.NewFileTotalSize; });

			StatsList.Empty(15);
			for (const TPair<FName, FArchiveDiffStats>& Stat : PackageDiffStats)
			{
				StatsList.Emplace(Stat.Key.ToString(), LexToString((double)Stat.Value.NewFileTotalSize / 1024.0 / 1024.0));
			}

			AddStat(TEXT("Package.DifferentPackagesSizeMBPerAsset"), StatsList);
		}

		{
			PackageDiffStats.ValueSort([](const FArchiveDiffStats& Lhs, const FArchiveDiffStats& Rhs){ return Lhs.NumDiffs > Rhs.NumDiffs; });

			StatsList.Empty(15);
			for (const TPair<FName, FArchiveDiffStats>& Stat : PackageDiffStats)
			{
				StatsList.Emplace(Stat.Key.ToString(), LexToString(Stat.Value.NumDiffs));
			}

			AddStat(TEXT("Package.NumberOfDifferencesInPackagesPerAsset"), StatsList);
		}

		{
			PackageDiffStats.ValueSort([](const FArchiveDiffStats& Lhs, const FArchiveDiffStats& Rhs){ return Lhs.DiffSize > Rhs.DiffSize; });

			StatsList.Empty(15);
			for (const TPair<FName, FArchiveDiffStats>& Stat : PackageDiffStats)
			{
				StatsList.Emplace(Stat.Key.ToString(), LexToString((double)Stat.Value.DiffSize / 1024.0 / 1024.0));
			}

			AddStat(TEXT("Package.PackageDifferencesSizeMBPerAsset"), StatsList);
		}

		int64 NewFileTotalSize = 0;
		int64 NumDiffs         = 0;
		int64 DiffSize         = 0;
		for (const TPair<FName, FArchiveDiffStats>& PackageStat : PackageDiffStats)
		{
			NewFileTotalSize += PackageStat.Value.NewFileTotalSize;
			NumDiffs         += PackageStat.Value.NumDiffs;
			DiffSize         += PackageStat.Value.DiffSize;
		}

		double DifferentPackagesSizeMB       = (double)NewFileTotalSize / 1024.0 / 1024.0;
		int32  NumberOfDifferencesInPackages = NumDiffs;
		double PackageDifferencesSizeMB      = (double)DiffSize / 1024.0 / 1024.0;

		StatsList.Empty(15);
		ADD_COOK_STAT(NumberOfDifferentPackages);
		ADD_COOK_STAT(DifferentPackagesSizeMB);
		ADD_COOK_STAT(NumberOfDifferencesInPackages);
		ADD_COOK_STAT(PackageDifferencesSizeMB);

		AddStat(TEXT("Package.DiffTotal"), StatsList);

		#undef ADD_COOK_STAT		
		
		const FString TotalString = TEXT("Total");
	}
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats(AddSavePackageStats);
}
#endif

#if WITH_EDITORONLY_DATA

/**
 * Calculates a checksum on an object's serialized data stream, but only of its non-editor properties.
 */
class COREUOBJECT_API FArchiveObjectCrc32NonEditorProperties : public FArchiveObjectCrc32
{
	using Super = FArchiveObjectCrc32;

public:
	FArchiveObjectCrc32NonEditorProperties()
		: EditorOnlyProp(0)
	{
	}

	virtual void Serialize(void* Data, int64 Length)
	{
		int32 NewEditorOnlyProp = EditorOnlyProp + this->IsEditorOnlyPropertyOnTheStack();
		TGuardValue<int32> Guard(EditorOnlyProp, NewEditorOnlyProp);
		if (NewEditorOnlyProp == 0)
		{
			Super::Serialize(Data, Length);
		}
	}

	virtual FString GetArchiveName() const
	{
		return TEXT("FArchiveObjectCrc32NonEditorProperties");
	}
	
private:
	int32 EditorOnlyProp;
};

#else

class COREUOBJECT_API FArchiveObjectCrc32NonEditorProperties : public FArchiveObjectCrc32
{
};

#endif

static bool HasUnsaveableOuter(UObject* InObj, UPackage* InSavingPackage)
{
	UObject* Obj = InObj;
	while (Obj)
	{
		if (Obj->GetClass()->HasAnyClassFlags(CLASS_Deprecated) && !Obj->HasAnyFlags(RF_ClassDefaultObject))
		{
			if (!InObj->IsPendingKill() && InObj->GetOutermost() == InSavingPackage)
			{
				UE_LOG(LogSavePackage, Warning, TEXT("%s has a deprecated outer %s, so it will not be saved"), *InObj->GetFullName(), *Obj->GetFullName());
			}
			return true; 
		}

		if(Obj->IsPendingKill())
		{
			return true;
		}

		if (Obj->HasAnyFlags(RF_Transient) && !Obj->IsNative())
		{
			return true;
		}

		Obj = Obj->GetOuter();
	}
	return false;
}

static void CheckObjectPriorToSave(FArchiveUObject& Ar, UObject* InObj, UPackage* InSavingPackage)
{
	if (!InObj)
	{
		return;
	}
	UObject* SerializedObject = nullptr;
	FUObjectSerializeContext* SaveContext = Ar.GetSerializeContext();
	check(SaveContext);
	SerializedObject = SaveContext->SerializedObject;

	if (!InObj->IsValidLowLevelFast() || !InObj->IsValidLowLevel())
	{
		UE_LOG(LogLinker, Fatal, TEXT("Attempt to save bogus object %p SaveContext.SerializedObject=%s  SerializedProperty=%s"), (void*)InObj, *GetFullNameSafe(SerializedObject), *GetFullNameSafe(Ar.GetSerializedProperty()));
		return;
	}
	// if the object class is abstract or has been marked as deprecated, mark this
	// object as transient so that it isn't serialized
	if ( InObj->GetClass()->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) )
	{
		if ( !InObj->HasAnyFlags(RF_ClassDefaultObject) || InObj->GetClass()->HasAnyClassFlags(CLASS_Deprecated) )
		{
			InObj->SetFlags(RF_Transient);
		}
		if ( !InObj->HasAnyFlags(RF_ClassDefaultObject) && InObj->GetClass()->HasAnyClassFlags(CLASS_HasInstancedReference) )
		{
			TArray<UObject*> ComponentReferences;
			FReferenceFinder ComponentCollector(ComponentReferences, InObj, false, true, true);
			ComponentCollector.FindReferences(InObj, SerializedObject, Ar.GetSerializedProperty());

			for ( int32 Index = 0; Index < ComponentReferences.Num(); Index++ )
			{
				ComponentReferences[Index]->SetFlags(RF_Transient);
			}
		}
	}
	else if ( HasUnsaveableOuter(InObj, InSavingPackage) )
	{
		InObj->SetFlags(RF_Transient);
	}

	if ( InObj->HasAnyFlags(RF_ClassDefaultObject)
		&& (InObj->GetClass()->ClassGeneratedBy == nullptr || !InObj->GetClass()->HasAnyFlags(RF_Transient)) )
	{
		// if this is the class default object, make sure it's not
		// marked transient for any reason, as we need it to be saved
		// to disk (unless it's associated with a transient generated class)
		InObj->ClearFlags(RF_Transient);
	}
}

static bool EndSavingIfCancelled() 
{
	if ( GWarn->ReceivedUserCancel() )
	{
		return true;
	}
	return false;
}

static FThreadSafeCounter OutstandingAsyncWrites;

void UPackage::WaitForAsyncFileWrites()
{
	while (OutstandingAsyncWrites.GetValue())
	{
		FPlatformProcess::Sleep(0.0f);
	}
}

static void WriteToFile(const FString& Filename, const uint8* InDataPtr, int64 InDataSize)
{
	IFileManager& FileManager = IFileManager::Get();

	if (FArchive* Ar = FileManager.CreateFileWriter(*Filename))
	{
		Ar->Serialize(/* grrr */ const_cast<uint8*>(InDataPtr), InDataSize);
		delete Ar;

		if (FileManager.FileSize(*Filename) != InDataSize)
		{
			FileManager.Delete(*Filename);

			UE_LOG(LogSavePackage, Fatal, TEXT("Could not save to %s!"), *Filename);
		}
	}
	else
	{
		UE_LOG(LogSavePackage, Fatal, TEXT("Could not write to %s!"), *Filename);
	}
}

struct FLargeMemoryDelete
{
	void operator()(uint8* Ptr) const
	{
		if (Ptr)
		{
			FMemory::Free(Ptr);
		}
	}
};

typedef TUniquePtr<uint8, FLargeMemoryDelete> FLargeMemoryPtr;

enum class EAsyncWriteOptions
{
	None = 0,
	WriteFileToDisk = 0x01,
	ComputeHash = 0x02
};
ENUM_CLASS_FLAGS(EAsyncWriteOptions)

static void AsyncWriteFile(TAsyncWorkSequence<FMD5>& AsyncWriteAndHashSequence, FLargeMemoryPtr Data, const int64 DataSize, const TCHAR* Filename, EAsyncWriteOptions Options)
{
	OutstandingAsyncWrites.Increment();
	FString OutputFilename(Filename);
	AsyncWriteAndHashSequence.AddWork([Data = MoveTemp(Data), DataSize, OutputFilename = MoveTemp(OutputFilename), Options](FMD5& State) mutable
	{
		if (EnumHasAnyFlags(Options, EAsyncWriteOptions::ComputeHash))
		{
			State.Update(Data.Get(), DataSize);
		}

		if (EnumHasAnyFlags(Options, EAsyncWriteOptions::WriteFileToDisk))
		{
			WriteToFile(OutputFilename, Data.Get(), DataSize);
		}

		OutstandingAsyncWrites.Decrement();
	});
}

static void AsyncWriteFileWithSplitExports(TAsyncWorkSequence<FMD5>& AsyncWriteAndHashSequence, FLargeMemoryPtr Data, const int64 DataSize, const int64 HeaderSize, const TCHAR* Filename, EAsyncWriteOptions Options)
{
	OutstandingAsyncWrites.Increment();
	FString OutputFilename(Filename);
	AsyncWriteAndHashSequence.AddWork([Data = MoveTemp(Data), DataSize, HeaderSize, OutputFilename = MoveTemp(OutputFilename), Options](FMD5& State) mutable
	{
		if (EnumHasAnyFlags(Options, EAsyncWriteOptions::ComputeHash))
		{
			State.Update(Data.Get(), DataSize);
		}

		if (EnumHasAnyFlags(Options, EAsyncWriteOptions::WriteFileToDisk))
		{
			// Write .uasset file
			WriteToFile(OutputFilename, Data.Get(), HeaderSize);

			// Write .uexp file
			const FString FilenameExports = FPaths::ChangeExtension(OutputFilename, TEXT(".uexp"));
			WriteToFile(FilenameExports, Data.Get() + HeaderSize, DataSize - HeaderSize);
		}

		OutstandingAsyncWrites.Decrement();
	});
}

class FPackageNameMapSaver
{
public:
	void MarkNameAsReferenced(FName Name)
	{
		ReferencedNames.Add(Name.GetDisplayIndex());
	}

	void MarkNameAsReferenced(FNameEntryId Name)
	{
		ReferencedNames.Add(Name);
	}

	bool NameExists(FNameEntryId ComparisonId) const
	{
		for (FNameEntryId DisplayId : ReferencedNames)
		{
			if (FName::GetComparisonIdFromDisplayId(DisplayId) == ComparisonId)
			{
				return true;
			}
		}

		return false;
	}

	void UpdateLinker(FLinkerSave& Linker, FLinkerLoad* Conform, FArchive* BinarySaver);

private:
	TSet<FNameEntryId> ReferencedNames;
};

#if WITH_EDITOR
static void AddReplacementsNames(FPackageNameMapSaver& NameMapSaver, UObject* Obj, const ITargetPlatform* TargetPlatform)
{
	if (TargetPlatform)
	{
		if (const IBlueprintNativeCodeGenCore* Coordinator = IBlueprintNativeCodeGenCore::Get())
		{
			const FCompilerNativizationOptions& NativizationOptions = Coordinator->GetNativizationOptionsForPlatform(TargetPlatform);
			if (const UClass* ReplObjClass = Coordinator->FindReplacedClassForObject(Obj, NativizationOptions))
			{
				NameMapSaver.MarkNameAsReferenced(ReplObjClass->GetFName());
			}

			FName ReplacedName;
			Coordinator->FindReplacedNameAndOuter(Obj, ReplacedName, NativizationOptions); //TODO: should we care about replaced outer ?
			if (ReplacedName != NAME_None)
			{
				NameMapSaver.MarkNameAsReferenced(ReplacedName);
			}
		}
	}
}
#endif //WITH_EDITOR

bool IsEditorOnlyObject(const UObject* InObject, bool bCheckRecursive, bool bCheckMarks)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("IsEditorOnlyObject"), STAT_IsEditorOnlyObject, STATGROUP_LoadTime);

	// Configurable via ini setting
	static struct FCanStripEditorOnlyExportsAndImports
	{
		bool bCanStripEditorOnlyObjects;
		FCanStripEditorOnlyExportsAndImports()
			: bCanStripEditorOnlyObjects(true)
		{
			GConfig->GetBool(TEXT("Core.System"), TEXT("CanStripEditorOnlyExportsAndImports"), bCanStripEditorOnlyObjects, GEngineIni);
		}
		FORCEINLINE operator bool() const { return bCanStripEditorOnlyObjects; }
	} CanStripEditorOnlyExportsAndImports;
	if (!CanStripEditorOnlyExportsAndImports)
	{
		return false;
	}
	check(InObject);

	if ((bCheckMarks && InObject->HasAnyMarks(OBJECTMARK_EditorOnly)) || InObject->IsEditorOnly())
	{
		return true;
	}

	// If this is a package that is editor only or the object is in editor-only package,
	// the object is editor-only too.
	const bool bIsAPackage = InObject->IsA<UPackage>();
	const UPackage* Package = (bIsAPackage ? static_cast<const UPackage*>(InObject) : InObject->GetOutermost());
	if (Package && Package->HasAnyPackageFlags(PKG_EditorOnly))
	{
		return true;
	}

	if (bCheckRecursive && !InObject->IsNative())
	{
		UObject* Outer = InObject->GetOuter();
		if (Outer && Outer != Package)
		{
			if (IsEditorOnlyObject(Outer, true, bCheckMarks))
			{
				return true;
			}
		}
		const UStruct* InStruct = Cast<UStruct>(InObject);
		if (InStruct)
		{
			const UStruct* SuperStruct = InStruct->GetSuperStruct();
			if (SuperStruct && IsEditorOnlyObject(SuperStruct, true, bCheckMarks))
			{
				return true;
			}
		}
		else
		{
			if (IsEditorOnlyObject(InObject->GetClass(), true, bCheckMarks))
			{
				return true;
			}

			UObject* Archetype = InObject->GetArchetype();
			if (Archetype && IsEditorOnlyObject(Archetype, true, bCheckMarks))
			{
				return true;
			}
		}
	}
	return false;
}

/**
 * Marks object as not for client, not for server, or editor only. Recurses up outer/class chain as necessary
 */
static void ConditionallyExcludeObjectForTarget(UObject* Obj, EObjectMark ExcludedObjectMarks, const ITargetPlatform* TargetPlatform, const bool bIsCooking)
{
#if WITH_EDITOR

	if (!Obj || Obj->GetOutermost()->GetFName() == GLongCoreUObjectPackageName)
	{
		// No object or in CoreUObject, don't exclude
		return;
	}

	auto InheritMarks = [](EObjectMark& MarksToModify, UObject* ObjToCheck, uint32 MarkMask)
	{
		EObjectMark ObjToCheckMarks = ObjToCheck->GetAllMarks();

		MarksToModify = (EObjectMark)(MarksToModify | (ObjToCheckMarks & MarkMask));
	};

	// MarksToProcess is a superset of marks retrieved from UPackage::GetExcludedObjectMarksForTargetPlatform
	const uint32 MarksToProcess = OBJECTMARK_EditorOnly | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer | OBJECTMARK_KeepForTargetPlatform;
	check((ExcludedObjectMarks & ~MarksToProcess) == 0);

	EObjectMark CurrentMarks = OBJECTMARK_NOMARKS;
	InheritMarks(CurrentMarks, Obj, MarksToProcess);

	if ((CurrentMarks & MarksToProcess) != 0)
	{
		// Already marked
		return;
	}

	UObject* ObjOuter = Obj->GetOuter();
	UClass* ObjClass = Obj->GetClass();
	
	if (bIsCooking && TargetPlatform)
	{
		// Check for nativization replacement
		if (const IBlueprintNativeCodeGenCore* Coordinator = IBlueprintNativeCodeGenCore::Get())
		{
			const FCompilerNativizationOptions& NativizationOptions = Coordinator->GetNativizationOptionsForPlatform(TargetPlatform);
			FName UnusedName;
			if (UClass* ReplacedClass = Coordinator->FindReplacedClassForObject(Obj, NativizationOptions))
			{
				ObjClass = ReplacedClass;
			}
			if (UObject* ReplacedOuter = Coordinator->FindReplacedNameAndOuter(Obj, /*out*/UnusedName, NativizationOptions))
			{
				ObjOuter = ReplacedOuter;
			}
		}
	}

	EObjectMark NewMarks = CurrentMarks;

	// Recurse into parents, then compute inherited marks
	ConditionallyExcludeObjectForTarget(ObjClass, ExcludedObjectMarks, TargetPlatform, bIsCooking);
	InheritMarks(NewMarks, ObjClass, OBJECTMARK_EditorOnly | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer);

	if (ObjOuter)
	{
		ConditionallyExcludeObjectForTarget(ObjOuter, ExcludedObjectMarks, TargetPlatform, bIsCooking);
		InheritMarks(NewMarks, ObjOuter, OBJECTMARK_EditorOnly | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer);
	}

	// Check parent struct if we have one
	UStruct* ThisStruct = dynamic_cast<UStruct*>(Obj);
	if (ThisStruct && ThisStruct->GetSuperStruct())
	{
		UObject* SuperStruct = ThisStruct->GetSuperStruct();
		ConditionallyExcludeObjectForTarget(SuperStruct, ExcludedObjectMarks, TargetPlatform, bIsCooking);
		InheritMarks(NewMarks, SuperStruct, OBJECTMARK_EditorOnly | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer);
	}

	// Check archetype, this may not have been covered in the case of components
	UObject* Archetype = Obj->GetArchetype();
	if (Archetype)
	{
		ConditionallyExcludeObjectForTarget(Archetype, ExcludedObjectMarks, TargetPlatform, bIsCooking);
		InheritMarks(NewMarks, Archetype, OBJECTMARK_EditorOnly | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer);
	}

	if (!Obj->HasAnyFlags(RF_ClassDefaultObject))
	{
		// CDOs must be included if their class is so only inherit marks, for everything else we check the native overrides as well
		if (!(NewMarks & OBJECTMARK_EditorOnly) && IsEditorOnlyObject(Obj, false, false))
		{
			NewMarks = (EObjectMark)(NewMarks | OBJECTMARK_EditorOnly);
		}

		if (!(NewMarks & OBJECTMARK_NotForClient) && !Obj->NeedsLoadForClient())
		{
			NewMarks = (EObjectMark)(NewMarks | OBJECTMARK_NotForClient);
		}

		if (!(NewMarks & OBJECTMARK_NotForServer) && !Obj->NeedsLoadForServer())
		{
			NewMarks = (EObjectMark)(NewMarks | OBJECTMARK_NotForServer);
		}

		if ((!(NewMarks & OBJECTMARK_NotForServer) || !(NewMarks & OBJECTMARK_NotForClient)) && TargetPlatform && !Obj->NeedsLoadForTargetPlatform(TargetPlatform))
		{
			NewMarks = (EObjectMark)(NewMarks | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer);
		}
	}

	// If NotForClient and NotForServer, it is implicitly editor only
	if ((NewMarks & OBJECTMARK_NotForClient) && (NewMarks & OBJECTMARK_NotForServer))
	{
		NewMarks = (EObjectMark)(NewMarks | OBJECTMARK_EditorOnly);
	}

	// If not excluded after a full set of tests, it is implicitly a keep
	if (NewMarks == 0)
	{
		NewMarks = OBJECTMARK_KeepForTargetPlatform;
	}

	// If our marks are different than original, set them on the object
	if (CurrentMarks != NewMarks)
	{
		Obj->Mark(NewMarks);
	}
#endif
}

/** For a CDO get all of the subobjects templates nested inside it or it's class */
static void GetCDOSubobjects(UObject* CDO, TArray<UObject*>& Subobjects)
{
	TArray<UObject*> CurrentSubobjects;
	TArray<UObject*> NextSubobjects;

	// Recursively search for subobjects. Only care about ones that have a full subobject chain as some nested objects are set wrong
	GetObjectsWithOuter(CDO->GetClass(), NextSubobjects, false);
	GetObjectsWithOuter(CDO, NextSubobjects, false);

	while (NextSubobjects.Num() > 0)
	{
		CurrentSubobjects = NextSubobjects;
		NextSubobjects.Empty();
		for (UObject* SubObj : CurrentSubobjects)
		{
			if (SubObj->HasAnyFlags(RF_DefaultSubObject | RF_ArchetypeObject))
			{
				Subobjects.Add(SubObj);
				GetObjectsWithOuter(SubObj, NextSubobjects, false);
			}
		}
	}
}

/**
 * Archive for tagging objects and names that must be exported
 * to the file.  It tags the objects passed to it, and recursively
 * tags all of the objects this object references.
 */
class FArchiveSaveTagExports : public FArchiveUObject
{
public:
	/**
	 * Constructor
	 * 
	 * @param	InOuter		the package to save
	 */
	FArchiveSaveTagExports( UPackage* InOuter )
	: Outer(InOuter)
	{
		this->SetIsSaving(true);
		this->SetIsPersistent(true);
		ArIsObjectReferenceCollector = true;
		ArShouldSkipBulkData	= true;
	}

	void ProcessBaseObject(UObject* BaseObject);
	virtual FArchive& operator<<(UObject*& Obj) override;
	virtual FArchive& operator<<(FWeakObjectPtr& Value) override;
	virtual void SetSerializeContext(FUObjectSerializeContext* InLoadContext) override
	{
		LoadContext = InLoadContext;
	}
	virtual FUObjectSerializeContext* GetSerializeContext() override
	{
		return LoadContext;
	}
	/**
	 * Package we're currently saving.  Only objects contained
	 * within this package will be tagged for serialization.
	 */
	UPackage* Outer;

	virtual FString GetArchiveName() const;

private:

	TArray<UObject*> TaggedObjects;
	TRefCountPtr<FUObjectSerializeContext> LoadContext;

	void ProcessTaggedObjects();
};

FString FArchiveSaveTagExports::GetArchiveName() const
{
	return Outer != nullptr
		? *FString::Printf(TEXT("SaveTagExports (%s)"), *Outer->GetName())
		: TEXT("SaveTagExports");
}

FArchive& FArchiveSaveTagExports::operator<<(FWeakObjectPtr& Value)
{
	if (IsEventDrivenLoaderEnabledInCookedBuilds() && IsCooking())
	{
		// Always serialize weak pointers for the purposes of object tagging
		UObject* Object = static_cast<UObject*>(Value.Get(true));
		*this << Object;
	}
	else
	{
		FArchiveUObject::SerializeWeakObjectPtr(*this, Value);
	}
	return *this;
}

FArchive& FArchiveSaveTagExports::operator<<(UObject*& Obj)
{
	if (!Obj || Obj->HasAnyMarks(OBJECTMARK_TagExp) || Obj->HasAnyFlags(RF_Transient) || !Obj->IsInPackage(Outer))
	{
		return *this;
	}

	check(Outer);

	// Check transient and pending kill flags for outers
	CheckObjectPriorToSave(*this, Obj, Outer);

	// The object may have become transient in CheckObjectPriorToSave
	if (Obj->HasAnyFlags(RF_Transient))
	{
		return *this;
	}

	// Check outer chain for any exlcuded object marks
	const EObjectMark ExcludedObjectMarks = GetExcludedObjectMarksForTargetPlatform(CookingTarget(), IsCooking());
	ConditionallyExcludeObjectForTarget(Obj, ExcludedObjectMarks, CookingTarget(), IsCooking());

	if (!Obj->HasAnyMarks((EObjectMark)(ExcludedObjectMarks)))
	{
		// It passed filtering so mark as export
		Obj->Mark(OBJECTMARK_TagExp);

		// First, serialize this object's archetype 
		UObject* Template = Obj->GetArchetype();
		*this << Template;

		// If this is a CDO, gather it's subobjects and serialize them
		if (Obj->HasAnyFlags(RF_ClassDefaultObject))
		{
			if (IsEventDrivenLoaderEnabledInCookedBuilds() && IsCooking())
			{
				// Gets all subobjects defined in a class, including the CDO, CDO components and blueprint-created components
				TArray<UObject*> ObjectTemplates;
				ObjectTemplates.Add(Obj);

				GetCDOSubobjects(Obj, ObjectTemplates);

				for (UObject* ObjTemplate : ObjectTemplates)
				{
					// Recurse into templates
					*this << ObjTemplate;
				}
			}
		}
	
		// NeedsLoadForEditor game is inherited to child objects, so check outer chain
		bool bNeedsLoadForEditorGame = false;
		for (UObject* OuterIt = Obj; OuterIt; OuterIt = OuterIt->GetOuter())
		{
			if (OuterIt->NeedsLoadForEditorGame())
			{
				bNeedsLoadForEditorGame = true;
				break;
			}
		}

		if(!bNeedsLoadForEditorGame && Obj->HasAnyFlags(RF_ClassDefaultObject))
		{
			bNeedsLoadForEditorGame = Obj->GetClass()->NeedsLoadForEditorGame();
		}

		if (!bNeedsLoadForEditorGame)
		{
			Obj->Mark(OBJECTMARK_NotAlwaysLoadedForEditorGame);
		}

		// Recurse with this object's class and package.
		UObject* Class  = Obj->GetClass();
		UObject* Parent = Obj->GetOuter();
		*this << Class << Parent;

		TaggedObjects.Add(Obj);
	}
	return *this;
}

/**
 * Serializes the specified object, tagging all objects it references.
 *
 * @param	BaseObject	the object that should be serialized; usually the package root or
 *						[in the case of a map package] the map's UWorld object.
 */
void FArchiveSaveTagExports::ProcessBaseObject(UObject* BaseObject)
{
	(*this) << BaseObject;
	ProcessTaggedObjects();
}

/**
 * Iterates over all objects which were encountered during serialization of the root object, serializing each one in turn.
 * Objects encountered during that serialization are then added to the array and iteration continues until no new objects are
 * added to the array.
 */
void FArchiveSaveTagExports::ProcessTaggedObjects()
{
	const int32 ArrayPreSize = 1024; // Was originally total number of objects, but this was unreasonably large
	TArray<UObject*> CurrentlyTaggedObjects;
	CurrentlyTaggedObjects.Empty(ArrayPreSize);
	while (TaggedObjects.Num())
	{
		CurrentlyTaggedObjects += TaggedObjects;
		TaggedObjects.Empty();

		for (int32 ObjIndex = 0; ObjIndex < CurrentlyTaggedObjects.Num(); ObjIndex++)
		{
			UObject* Obj = CurrentlyTaggedObjects[ObjIndex];

			if (Obj->HasAnyFlags(RF_ClassDefaultObject))
			{
				Obj->GetClass()->SerializeDefaultObject(Obj, *this);
			}
			// In the CDO case the above would serialize most of the references, including transient properties
			// but we still want to serialize the object using the normal path to collect all custom versions it might be using.
			Obj->Serialize(*this);
		}

		CurrentlyTaggedObjects.Empty(ArrayPreSize);
	}
}

/**
 * Archive for tagging objects and names that must be listed in the
 * file's imports table.
 */
class FArchiveSaveTagImports : public FArchiveUObject
{
public:
	FLinkerSave* Linker;
	FPackageNameMapSaver& NameMapSaver;
	TArray<UObject*> Dependencies;
	TArray<UObject*> NativeDependencies;
	TArray<UObject*> OtherImports;
	bool bIgnoreDependencies;

	/** Helper object to save/store state of bIgnoreDependencies */
	class FScopeIgnoreDependencies
	{
		FArchiveSaveTagImports& Archive;
		bool bScopedIgnoreDependencies;
		
	public:
		FScopeIgnoreDependencies(FArchiveSaveTagImports& InArchive)
			: Archive(InArchive)
			, bScopedIgnoreDependencies(InArchive.bIgnoreDependencies)
		{
			Archive.bIgnoreDependencies = true;
		}
		~FScopeIgnoreDependencies()
		{
			Archive.bIgnoreDependencies = bScopedIgnoreDependencies;
		}
	};

	FArchiveSaveTagImports(FLinkerSave* InLinker, FPackageNameMapSaver& InNameMapSaver)
		: Linker(InLinker)
		, NameMapSaver(InNameMapSaver)
		, bIgnoreDependencies(false)
	{
		check(Linker);

		this->SetIsSaving(true);
		this->SetIsPersistent(true);
		ArIsObjectReferenceCollector = true;
		ArShouldSkipBulkData	= true;

		ArPortFlags = Linker->GetPortFlags();
		SetCookingTarget(Linker->CookingTarget());
	}

	virtual FArchive& operator<<(UObject*& Obj) override;
	virtual FArchive& operator<< (struct FWeakObjectPtr& Value) override;
	virtual FArchive& operator<<(FLazyObjectPtr& LazyObjectPtr) override;
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;
	virtual FArchive& operator<<(FName& Name) override;
	
	virtual void MarkSearchableName(const UObject* TypeObject, const FName& ValueName) const override;
	virtual FString GetArchiveName() const override;
	virtual void SetSerializeContext(FUObjectSerializeContext* InLoadContext) override
	{
		LoadContext = InLoadContext;
	}
	virtual FUObjectSerializeContext* GetSerializeContext() override
	{
		return LoadContext;
	}

private:

	TRefCountPtr<FUObjectSerializeContext> LoadContext;
};

FString FArchiveSaveTagImports::GetArchiveName() const
{
	if ( Linker != nullptr && Linker->LinkerRoot )
	{
		return FString::Printf(TEXT("SaveTagImports (%s)"), *Linker->LinkerRoot->GetName());
	}

	return TEXT("SaveTagImports");
}

FArchive& FArchiveSaveTagImports::operator<< (struct FWeakObjectPtr& Value)
{
	if (IsEventDrivenLoaderEnabledInCookedBuilds() && IsCooking())
	{
		// Always serialize weak pointers for the purposes of object tagging
		UObject* Object = static_cast<UObject*>(Value.Get(true));
		*this << Object;
	}
	else
	{
		FArchiveUObject::SerializeWeakObjectPtr(*this, Value);
	}
	return *this;
}

FArchive& FArchiveSaveTagImports::operator<<( UObject*& Obj )
{
	// Check transient and pending kill flags for outers
	CheckObjectPriorToSave(*this, Obj, nullptr);

	const EObjectMark ExcludedObjectMarks = GetExcludedObjectMarksForTargetPlatform( CookingTarget(), IsCooking() );
	ConditionallyExcludeObjectForTarget(Obj, ExcludedObjectMarks, CookingTarget(), IsCooking());
	bool bExcludePackageFromCook = Obj && FCoreUObjectDelegates::ShouldCookPackageForPlatform.IsBound() ? !FCoreUObjectDelegates::ShouldCookPackageForPlatform.Execute(Obj->GetOutermost(), CookingTarget()) : false;

	// Skip PendingKill objects and objects that don't pass the platform mark filter
	if (Obj && (ExcludedObjectMarks == OBJECTMARK_NOMARKS || !Obj->HasAnyMarks(ExcludedObjectMarks)) && !bExcludePackageFromCook)
	{
		bool bIsNative = Obj->IsNative();
		if( !Obj->HasAnyFlags(RF_Transient) || bIsNative)
		{
			const bool bIsTopLevelPackage = Obj->GetOuter() == nullptr && dynamic_cast<UPackage*>(Obj);
			UObject* Outer = Obj->GetOuter();

			// See if this is inside a native class
			while (!bIsNative && Outer)
			{
				if (dynamic_cast<UClass*>(Outer) && Outer->IsNative())
				{
					bIsNative = true;
				}
				Outer = Outer->GetOuter();
			}

			// We add objects as dependencies even if they're also exports
			if (!bIsTopLevelPackage && !bIgnoreDependencies)
			{
				TArray<UObject*>& DependencyArray = bIsNative ? NativeDependencies : Dependencies;
				if (DependencyArray.Contains(Obj))
				{
					return *this;
				}
				DependencyArray.Add(Obj);
			}
			
			if (!Obj->HasAnyMarks(OBJECTMARK_TagExp))  
			{
				// Add into other imports list unless it's already there
				if (bIsTopLevelPackage || bIgnoreDependencies)
				{
					if (OtherImports.Contains(Obj))
					{
						return *this;
					}

					OtherImports.Add(Obj);
				}

				// Mark this object as an import
				Obj->Mark(OBJECTMARK_TagImp);
				UClass* ClassObj = Cast<UClass>(Obj);

				// Don't recurse into CDOs if we're already ignoring dependencies, we only want to recurse into our outer chain in that case
				if (IsEventDrivenLoaderEnabledInCookedBuilds() && IsCooking() && !bIsNative && !bIgnoreDependencies && ClassObj)
				{
					// We don't want to add this to Dependencies, we simply want it to be an import so that a serialization before creation dependency can be created to the CDO
					FScopeIgnoreDependencies IgnoreDependencies(*this);
					UObject* CDO = ClassObj->GetDefaultObject();

					if (CDO)
					{
						// Gets all subobjects defined in a class, including the CDO, CDO components and blueprint-created components
						TArray<UObject*> ObjectTemplates;
						ObjectTemplates.Add(CDO);

						GetCDOSubobjects(CDO, ObjectTemplates);

						for (UObject* ObjTemplate : ObjectTemplates)
						{
							// Recurse into templates
							*this << ObjTemplate;
						}
					}
#if WITH_EDITOR
					AddReplacementsNames(NameMapSaver, Obj, CookingTarget());
#endif //WITH_EDITOR
				}

				// Recurse into parent
				UObject* Parent = Obj->GetOuter();
#if WITH_EDITOR
				if (IsCooking() && CookingTarget())
				{
					if (const IBlueprintNativeCodeGenCore* Coordinator = IBlueprintNativeCodeGenCore::Get())
					{
						FName UnusedName;
						UObject* ReplacedOuter = Coordinator->FindReplacedNameAndOuter(Obj, /*out*/UnusedName, Coordinator->GetNativizationOptionsForPlatform(CookingTarget()));
						Parent = ReplacedOuter ? ReplacedOuter : Obj->GetOuter();
					}
				}
#endif //WITH_EDITOR
				if( Parent )
				{
					*this << Parent;
				}

				// if the object has a non null package set, recurse into it
				UPackage* Package = Obj->GetExternalPackage();
				if (Package && Package != Obj)
				{
					*this << Package;
				}

				// For things with a BP-created class we need to recurse into that class so the import ClassPackage will load properly
				// We don't do this for native classes to avoid bloating the import table
				UClass* ObjClass = Obj->GetClass();

				if (!ObjClass->IsNative())
				{
					*this << ObjClass;
				}
			}
		}
	}
	return *this;
}

FArchive& FArchiveSaveTagImports::operator<<( FLazyObjectPtr& LazyObjectPtr)
{
	FUniqueObjectGuid ID;
	ID = LazyObjectPtr.GetUniqueID();
	return *this << ID;
}

FArchive& FArchiveSaveTagImports::operator<<(FSoftObjectPath& Value)
{
	if (Value.IsValid())
	{
		Value.SerializePath(*this);

		FSoftObjectPathThreadContext& ThreadContext = FSoftObjectPathThreadContext::Get();
		FName ReferencingPackageName, ReferencingPropertyName;
		ESoftObjectPathCollectType CollectType = ESoftObjectPathCollectType::AlwaysCollect;
		ESoftObjectPathSerializeType SerializeType = ESoftObjectPathSerializeType::AlwaysSerialize;

		ThreadContext.GetSerializationOptions(ReferencingPackageName, ReferencingPropertyName, CollectType, SerializeType, this);

		if (CollectType != ESoftObjectPathCollectType::NeverCollect)
		{
			// Don't track if this is a never collect path
			FString Path = Value.ToString();
			FName PackageName = FName(*FPackageName::ObjectPathToPackageName(Path));
			NameMapSaver.MarkNameAsReferenced(PackageName);
			Linker->SoftPackageReferenceList.AddUnique(PackageName);
		}
	}
	return *this;
}

FArchive& FArchiveSaveTagImports::operator<<(FName& Name)
{
	NameMapSaver.MarkNameAsReferenced(Name);
	return *this;
}

void FArchiveSaveTagImports::MarkSearchableName(const UObject* TypeObject, const FName& ValueName) const
{
	if (!TypeObject)
	{
		return;
	}

	if (!Dependencies.Contains(TypeObject))
	{
		// Serialize object to make sure it ends up in import table
		// This is doing a const cast to avoid backward compatibility issues
		FArchiveSaveTagImports* MutableArchive = const_cast<FArchiveSaveTagImports*>(this);
		UObject* TempObject = const_cast<UObject*>(TypeObject);
		(*MutableArchive) << TempObject;
	}

	// Manually mark the name as referenced, in case it got skipped due to delta serialization
	NameMapSaver.MarkNameAsReferenced(ValueName);

	Linker->SearchableNamesObjectMap.FindOrAdd(TypeObject).AddUnique(ValueName);
}

/**
 * Find most likely culprit that caused the objects in the passed in array to be considered for saving.
 *
 * @param	BadObjects	array of objects that are considered "bad" (e.g. non- RF_Public, in different map package, ...)
 * @return	UObject that is considered the most likely culprit causing them to be referenced or NULL
 */
static void FindMostLikelyCulprit( TArray<UObject*> BadObjects, UObject*& MostLikelyCulprit, const FProperty*& PropertyRef )
{
	MostLikelyCulprit	= nullptr;

	// Iterate over all objects that are marked as unserializable/ bad and print out their referencers.
	for( int32 BadObjIndex=0; BadObjIndex<BadObjects.Num(); BadObjIndex++ )
	{
		UObject* Obj = BadObjects[BadObjIndex];

		UE_LOG(LogSavePackage, Warning, TEXT("\r\nReferencers of %s:"), *Obj->GetFullName() );
		
		FReferencerInformationList Refs;

		if (IsReferenced(Obj, RF_Public, EInternalObjectFlags::Native, true, &Refs))
		{
			for (int32 i = 0; i < Refs.ExternalReferences.Num(); i++)
			{
				UObject* RefObj = Refs.ExternalReferences[i].Referencer;
				if (RefObj->HasAnyMarks(EObjectMark(OBJECTMARK_TagExp | OBJECTMARK_TagImp)))
				{
					if (RefObj->GetFName() == NAME_PersistentLevel || RefObj->GetClass()->GetFName() == WorldClassName)
					{
						// these types of references should be ignored
						continue;
					}

					UE_LOG(LogSavePackage, Warning, TEXT("\t%s (%i refs)"), *RefObj->GetFullName(), Refs.ExternalReferences[i].TotalReferences);
					for (int32 j = 0; j < Refs.ExternalReferences[i].ReferencingProperties.Num(); j++)
					{
						const FProperty* Prop = Refs.ExternalReferences[i].ReferencingProperties[j];
						UE_LOG(LogSavePackage, Warning, TEXT("\t\t%i) %s"), j, *Prop->GetFullName());
						PropertyRef = Prop;
					}

					MostLikelyCulprit = Obj;
				}
			}
		}
	}
}

/**
 * Helper structure encapsulating functionality to sort a linker's name map according to the order of the names a package being conformed against.
 */
struct FObjectNameSortHelper
{
private:
	/** the linker that we're sorting names for */
	friend struct TDereferenceWrapper<FNameEntryId, FObjectNameSortHelper>;

	/** Comparison function used by Sort */
	FORCEINLINE bool operator()( const FName& A, const FName& B ) const
	{
		return A.Compare(B) < 0;
	}

	/** Comparison function used by Sort */
	FORCEINLINE bool operator()(FNameEntryId A, FNameEntryId B) const
	{
		// Could be implemented without constructing FName but would new FNameEntry comparison API
		return A != B && operator()(FName::CreateFromDisplayId(A, 0), FName::CreateFromDisplayId(B, 0));
	}

public:


	/**
	 * Sorts names according to the order in which they occur in the list of NameIndices.  If a package is specified to be conformed against, ensures that the order
	 * of the names match the order in which the corresponding names occur in the old package.
	 *
	 * @param	Linker				linker containing the names that need to be sorted
	 * @param	LinkerToConformTo	optional linker to conform against.
	 */
	void SortNames( FLinkerSave* Linker, FLinkerLoad* LinkerToConformTo, FPackageNameMapSaver& NameMapSaver)
	{
		int32 SortStartPosition = 0;

		if ( LinkerToConformTo != nullptr )
		{
			SortStartPosition = LinkerToConformTo->NameMap.Num();
			TArray<FNameEntryId> ConformedNameMap = LinkerToConformTo->NameMap;
			for ( int32 NameIndex = 0; NameIndex < Linker->NameMap.Num(); NameIndex++ )
			{
				FNameEntryId CurrentName = Linker->NameMap[NameIndex];
				if ( !ConformedNameMap.Contains(CurrentName) )
				{
					ConformedNameMap.Add(CurrentName);
				}
			}

			Linker->NameMap = ConformedNameMap;
			for ( int32 NameIndex = 0; NameIndex < Linker->NameMap.Num(); NameIndex++ )
			{
				FNameEntryId CurrentName = Linker->NameMap[NameIndex];
				NameMapSaver.MarkNameAsReferenced(CurrentName);
			}
		}

		if ( SortStartPosition < Linker->NameMap.Num() )
		{
			Sort( &Linker->NameMap[SortStartPosition], Linker->NameMap.Num() - SortStartPosition, FObjectNameSortHelper() );
		}
	}
};

void FPackageNameMapSaver::UpdateLinker(FLinkerSave& Linker, FLinkerLoad* Conform, FArchive* BinarySaver)
{
	// Add names
	Linker.NameMap.Reserve(Linker.NameMap.Num() + ReferencedNames.Num());
	for (FNameEntryId Name : ReferencedNames)
	{
		Linker.NameMap.Add(Name);
	}

	// Sort names
	FObjectNameSortHelper NameSortHelper;
	NameSortHelper.SortNames(&Linker, Conform, *this);

	// Serialize names and build NameIndicies
	if (BinarySaver)
	{
		Linker.Summary.NameCount = Linker.NameMap.Num();
		for (int32 i = 0; i < Linker.NameMap.Num(); i++)
		{
			FName::GetEntry(Linker.NameMap[i])->Write(Linker);
			Linker.NameIndices.Add(Linker.NameMap[i], i);
		}
	}
}

/**
 * Helper structure to encapsulate sorting a linker's import table according to the of the import table of the package being conformed against.
 */
struct FObjectImportSortHelper
{
private:
	/**
	 * Map of UObject => full name; optimization for sorting.
	 */
	TMap<UObject*,FString>			ObjectToFullNameMap;

	/** the linker that we're sorting names for */
	friend struct TDereferenceWrapper<FObjectImport,FObjectImportSortHelper>;

	/** Comparison function used by Sort */
	bool operator()( const FObjectImport& A, const FObjectImport& B ) const
	{
		int32 Result = 0;
		if ( A.XObject == nullptr )
		{
			Result = 1;
		}
		else if ( B.XObject == nullptr )
		{
			Result = -1;
		}
		else
		{
			const FString* FullNameA = ObjectToFullNameMap.Find(A.XObject);
			const FString* FullNameB = ObjectToFullNameMap.Find(B.XObject);
			checkSlow(FullNameA);
			checkSlow(FullNameB);

			Result = FCString::Stricmp(**FullNameA, **FullNameB);
		}

		return Result < 0;
	}

public:

	/**
	 * Sorts imports according to the order in which they occur in the list of imports.  If a package is specified to be conformed against, ensures that the order
	 * of the imports match the order in which the corresponding imports occur in the old package.
	 *
	 * @param	Linker				linker containing the imports that need to be sorted
	 * @param	LinkerToConformTo	optional linker to conform against.
	 */
	void SortImports( FLinkerSave* Linker, FLinkerLoad* LinkerToConformTo=nullptr )
	{
		int32 SortStartPosition=0;
		TArray<FObjectImport>& Imports = Linker->ImportMap;
		if ( LinkerToConformTo )
		{
			// intended to be a copy
			TArray<FObjectImport> Orig = Imports;
			Imports.Empty(Imports.Num());

			// this array tracks which imports from the new package exist in the old package
			TArray<uint8> Used;
			Used.AddZeroed(Orig.Num());

			TMap<FString,int32> OriginalImportIndexes;
			OriginalImportIndexes.Reserve(Orig.Num());
			ObjectToFullNameMap.Reserve(Orig.Num());
			for ( int32 i = 0; i < Orig.Num(); i++ )
			{
				FObjectImport& Import = Orig[i];
				FString ImportFullName = Import.XObject->GetFullName();

				OriginalImportIndexes.Add( *ImportFullName, i );
				ObjectToFullNameMap.Add(Import.XObject, *ImportFullName);
			}

			for( int32 i=0; i<LinkerToConformTo->ImportMap.Num(); i++ )
			{
				// determine whether the new version of the package contains this export from the old package
				int32* OriginalImportPosition = OriginalImportIndexes.Find( *LinkerToConformTo->GetImportFullName(i) );
				if( OriginalImportPosition )
				{
					// this import exists in the new package as well,
					// create a copy of the FObjectImport located at the original index and place it
					// into the matching position in the new package's import map
					FObjectImport* NewImport = new(Imports) FObjectImport( Orig[*OriginalImportPosition] );
					check(NewImport->XObject == Orig[*OriginalImportPosition].XObject);
					Used[ *OriginalImportPosition ] = 1;
				}
				else
				{
					// this import no longer exists in the new package
					new(Imports)FObjectImport( nullptr );
				}
			}

			SortStartPosition = LinkerToConformTo->ImportMap.Num();
			for( int32 i=0; i<Used.Num(); i++ )
			{
				if( !Used[i] )
				{
					// the FObjectImport located at pos "i" in the original import table did not
					// exist in the old package - add it to the end of the import table
					new(Imports) FObjectImport( Orig[i] );
				}
			}
		}
		else
		{
			ObjectToFullNameMap.Reserve(Linker->ImportMap.Num());
			for ( int32 ImportIndex = 0; ImportIndex < Linker->ImportMap.Num(); ImportIndex++ )
			{
				const FObjectImport& Import = Linker->ImportMap[ImportIndex];
				if ( Import.XObject )
				{
					ObjectToFullNameMap.Add(Import.XObject, Import.XObject->GetFullName());
				}
			}
		}

		if ( SortStartPosition < Linker->ImportMap.Num() )
		{
			Sort( &Linker->ImportMap[SortStartPosition], Linker->ImportMap.Num() - SortStartPosition, *this );
		}
	}
};

/**
 * Helper structure to encapsulate sorting a linker's export table alphabetically, taking into account conforming to other linkers.
 */
struct FObjectExportSortHelper
{
private:
	struct FObjectFullName
	{
	public:
		FObjectFullName(const UObject* Object, const UObject* Root)
		{
			ClassName = Object->GetClass()->GetFName();
			const UObject* Current = Object;
			while (Current != nullptr && Current != Root)
			{
				Path.Insert(Current->GetFName(), 0);
				Current = Current->GetOuter();
			}
		}

		FObjectFullName(FObjectFullName&& InFullName)
		{
			ClassName = InFullName.ClassName;
			Swap(Path, InFullName.Path);
		}
		FName ClassName;
		TArray<FName> Path;
	};

	bool bUseFObjectFullName;


	TMap<UObject*, FObjectFullName> ObjectToObjectFullNameMap;

	/**
	 * Map of UObject => full name; optimization for sorting.
	 */
	TMap<UObject*,FString>			ObjectToFullNameMap;

	/** the linker that we're sorting exports for */
	friend struct TDereferenceWrapper<FObjectExport,FObjectExportSortHelper>;

	/** Comparison function used by Sort */
	bool operator()( const FObjectExport& A, const FObjectExport& B ) const
	{
		int32 Result = 0;
		if ( A.Object == nullptr )
		{
			Result = 1;
		}
		else if ( B.Object == nullptr )
		{
			Result = -1;
		}
		else
		{
			if (bUseFObjectFullName)
			{
				const FObjectFullName* FullNameA = ObjectToObjectFullNameMap.Find(A.Object);
				const FObjectFullName* FullNameB = ObjectToObjectFullNameMap.Find(B.Object);
				checkSlow(FullNameA);
				checkSlow(FullNameB);

				if (FullNameA->ClassName != FullNameB->ClassName)
				{
					Result = FCString::Stricmp(*FullNameA->ClassName.ToString(), *FullNameB->ClassName.ToString());
				}
				else
				{
					int Num = FMath::Min(FullNameA->Path.Num(), FullNameB->Path.Num());
					for (int I = 0; I < Num; ++I)
					{
						if (FullNameA->Path[I] != FullNameB->Path[I])
						{
							Result = FCString::Stricmp(*FullNameA->Path[I].ToString(), *FullNameB->Path[I].ToString());
							break;
						}
					}
					if (Result == 0)
					{
						Result = FullNameA->Path.Num() - FullNameB->Path.Num();
					}
				}
			}
			else
			{
				const FString* FullNameA = ObjectToFullNameMap.Find(A.Object);
				const FString* FullNameB = ObjectToFullNameMap.Find(B.Object);
				checkSlow(FullNameA);
				checkSlow(FullNameB);

				Result = FCString::Stricmp(**FullNameA, **FullNameB);
			}
		}

		return Result < 0;
	}

public:

	FObjectExportSortHelper() : bUseFObjectFullName(false)
	{}

	/**
	 * Sorts exports alphabetically.  If a package is specified to be conformed against, ensures that the order
	 * of the exports match the order in which the corresponding exports occur in the old package.
	 *
	 * @param	Linker				linker containing the exports that need to be sorted
	 * @param	LinkerToConformTo	optional linker to conform against.
	 */
	void SortExports( FLinkerSave* Linker, FLinkerLoad* LinkerToConformTo=nullptr, bool InbUseFObjectFullName = false)
	{
		bUseFObjectFullName = InbUseFObjectFullName;

		if (bUseFObjectFullName)
		{
			ObjectToObjectFullNameMap.Reserve(Linker->ExportMap.Num());
		}
		else
		{
			ObjectToFullNameMap.Reserve(Linker->ExportMap.Num());
		}

		int32 SortStartPosition=0;
		if ( LinkerToConformTo )
		{
			// build a map of object full names to the index into the new linker's export map prior to sorting.
			// we need to do a little trickery here to generate an object path name that will match what we'll get back
			// when we call GetExportFullName on the LinkerToConformTo's exports, due to localized packages and forced exports.
			const FString LinkerName = Linker->LinkerRoot->GetName();
			const FString PathNamePrefix = LinkerName + TEXT(".");

			// Populate object to current index map.
			TMap<FString,int32> OriginalExportIndexes;
			OriginalExportIndexes.Reserve(Linker->ExportMap.Num());
			for( int32 ExportIndex=0; ExportIndex < Linker->ExportMap.Num(); ExportIndex++ )
			{
				const FObjectExport& Export = Linker->ExportMap[ExportIndex];
				if( Export.Object )
				{
					// get the path name for this object; if the object is contained within the package we're saving,
					// we don't want the returned path name to contain the package name since we'll be adding that on
					// to ensure that forced exports have the same outermost name as the non-forced exports
					FString ObjectPathName = Export.Object != Linker->LinkerRoot
						? Export.Object->GetPathName(Linker->LinkerRoot)
						: LinkerName;
						
					FString ExportFullName = Export.Object->GetClass()->GetName() + TEXT(" ") + PathNamePrefix + ObjectPathName;

					// Set the index (key) in the map to the index of this object into the export map.
					OriginalExportIndexes.Add( *ExportFullName, ExportIndex );
					if (bUseFObjectFullName)
					{
						FObjectFullName ObjectFullName(Export.Object, Linker->LinkerRoot);
						ObjectToObjectFullNameMap.Add(Export.Object, MoveTemp(ObjectFullName)); 
					}
					else
					{
						ObjectToFullNameMap.Add(Export.Object, *ExportFullName);
					}
				}
			}

			// backup the existing export list so we can empty the linker's actual list
			TArray<FObjectExport> OldExportMap = Linker->ExportMap;
			Linker->ExportMap.Empty(Linker->ExportMap.Num());

			// this array tracks which exports from the new package exist in the old package
			TArray<uint8> Used;
			Used.AddZeroed(OldExportMap.Num());

			for( int32 i = 0; i<LinkerToConformTo->ExportMap.Num(); i++ )
			{
				// determine whether the new version of the package contains this export from the old package
				FString ExportFullName = LinkerToConformTo->GetExportFullName(i, *LinkerName);
				int32* OriginalExportPosition = OriginalExportIndexes.Find( *ExportFullName );
				if( OriginalExportPosition )
				{
					// this export exists in the new package as well,
					// create a copy of the FObjectExport located at the original index and place it
					// into the matching position in the new package's export map
					FObjectExport* NewExport = new(Linker->ExportMap) FObjectExport( OldExportMap[*OriginalExportPosition] );
					check(NewExport->Object == OldExportMap[*OriginalExportPosition].Object);
					Used[ *OriginalExportPosition ] = 1;
				}
				else
				{

					// this export no longer exists in the new package; to ensure that the _LinkerIndex matches, add an empty entry to pad the list
					new(Linker->ExportMap)FObjectExport( nullptr );
					UE_LOG(LogSavePackage, Log, TEXT("No matching export found in new package for original export %i: %s"), i, *ExportFullName);
				}
			}



			SortStartPosition = LinkerToConformTo->ExportMap.Num();
			for( int32 i=0; i<Used.Num(); i++ )
			{
				if( !Used[i] )
				{
					// the FObjectExport located at pos "i" in the original export table did not
					// exist in the old package - add it to the end of the export table
					new(Linker->ExportMap) FObjectExport( OldExportMap[i] );
				}
			}

#if DO_GUARD_SLOW

			// sanity-check: make sure that all exports which existed in the linker before we sorted exist in the linker's export map now
			{
				TSet<UObject*> ExportObjectList;
				for( int32 ExportIndex=0; ExportIndex<Linker->ExportMap.Num(); ExportIndex++ )
				{
					ExportObjectList.Add(Linker->ExportMap[ExportIndex].Object);
				}

				for( int32 OldExportIndex=0; OldExportIndex<OldExportMap.Num(); OldExportIndex++ )
				{
					check(ExportObjectList.Contains(OldExportMap[OldExportIndex].Object));
				}
			}
#endif
		}
		else
		{
			for ( int32 ExportIndex = 0; ExportIndex < Linker->ExportMap.Num(); ExportIndex++ )
			{
				const FObjectExport& Export = Linker->ExportMap[ExportIndex];
				if ( Export.Object )
				{
					if (bUseFObjectFullName)
					{
						FObjectFullName ObjectFullName(Export.Object, nullptr);
						ObjectToObjectFullNameMap.Add(Export.Object, MoveTemp(ObjectFullName));
					}
					else
					{
						ObjectToFullNameMap.Add(Export.Object, Export.Object->GetFullName());
					}
				}
			}
		}

		if ( SortStartPosition < Linker->ExportMap.Num() )
		{
			Sort( &Linker->ExportMap[SortStartPosition], Linker->ExportMap.Num() - SortStartPosition, *this );
		}
	}
};


class FExportReferenceSorter : public FArchiveUObject
{
	/**
	 * Verifies that all objects which will be force-loaded when the export at RelativeIndex is created and/or loaded appear in the sorted list of exports
	 * earlier than the export at RelativeIndex.
	 *
	 * Used for tracking down the culprit behind dependency sorting bugs.
	 *
	 * @param	RelativeIndex	the index into the sorted export list to check dependencies for
	 * @param	CheckObject		the object that will be force-loaded by the export at RelativeIndex
	 * @param	ReferenceType	the relationship between the object at RelativeIndex and CheckObject (archetype, class, etc.)
	 * @param	out_ErrorString	if incorrect sorting is detected, receives data containing more information about the incorrectly sorted object.
	 *
	 * @param	true if the export at RelativeIndex appears later than the exports associated with any objects that it will force-load; false otherwise.
	 */
	bool VerifyDependency( const int32 RelativeIndex, UObject* CheckObject, const FString& ReferenceType, FString& out_ErrorString )
	{
		bool bResult = false;

		checkf(ReferencedObjects.IsValidIndex(RelativeIndex), TEXT("Invalid index specified: %i (of %i)"), RelativeIndex, ReferencedObjects.Num());

		UObject* SourceObject = ReferencedObjects[RelativeIndex];
		checkf(SourceObject, TEXT("nullptr Object at location %i in ReferencedObjects list"), RelativeIndex);
		checkf(CheckObject, TEXT("CheckObject is nullptr for %s (%s)"), *SourceObject->GetFullName(), *ReferenceType);

		if ( SourceObject->GetOutermost() != CheckObject->GetOutermost() )
		{
			// not in the same package; therefore we can assume that the dependent object will exist
			bResult = true;
		}
		else
		{
			int32 OtherIndex = ReferencedObjects.Find(CheckObject);
			if ( OtherIndex != INDEX_NONE )
			{
				if ( OtherIndex < RelativeIndex )
				{
					bResult = true;
				}
				else
				{
					out_ErrorString = FString::Printf(TEXT("Sorting error detected (%s appears later in ReferencedObjects list)!  %i) %s   =>  %i) %s"), *ReferenceType, RelativeIndex,
						*SourceObject->GetFullName(), OtherIndex, *CheckObject->GetFullName());

					bResult = false;
				}
			}
			else
			{
				// the object isn't in the list of ReferencedObjects, which means it wasn't processed as a result of processing the source object; this
				// might indicate a bug, but might also just mean that the CheckObject was first referenced by an earlier export
				int32 ProcessedIndex = ProcessedObjects.Find(CheckObject);
				if ( ProcessedIndex != INDEX_NONE )
				{
					OtherIndex = ProcessedIndex;
					int32 SourceIndex = ProcessedObjects.Find(SourceObject);

					if ( OtherIndex < SourceIndex )
					{
						bResult = true;
					}
					else
					{
						out_ErrorString = FString::Printf(TEXT("Sorting error detected (%s was processed but not added to ReferencedObjects list)!  %i/%i) %s   =>  %i) %s"),
							*ReferenceType, RelativeIndex, SourceIndex, *SourceObject->GetFullName(), OtherIndex, *CheckObject->GetFullName());
						bResult = false;
					}
				}
				else
				{
					int32 SourceIndex = ProcessedObjects.Find(SourceObject);

					out_ErrorString = FString::Printf(TEXT("Sorting error detected (%s has not yet been processed)!  %i/%i) %s   =>  %s"),
						*ReferenceType, RelativeIndex, SourceIndex, *SourceObject->GetFullName(), *CheckObject->GetFullName());

					bResult = false;
				}
			}
		}

		return bResult;
	}

	/**
	 * Pre-initializes the list of processed objects with the boot-strap classes.
	 */
	void InitializeCoreClasses()
	{
#if 1
		FScopeLock ScopeLock(&InitializeCoreClassesCritSec);
		check(CoreClasses.Num() == 0);
		check(ReferencedObjects.Num() == 0);
		check(SerializedObjects.Num() == 0);
		check(bIgnoreFieldReferences == false);

		static bool bInitializedStaticCoreClasses = false;
		static TArray<UClass*> StaticCoreClasses;
		static TArray<UObject*> StaticCoreReferencedObjects;
		static FOrderedObjectSet StaticProcessedObjects;
		static TSet<UObject*> StaticSerializedObjects;
		
		

		// Helper class to register FlushInitializedStaticCoreClasses callback on first SavePackage run
		struct FAddFlushInitalizedStaticCoreClasses
		{
			FAddFlushInitalizedStaticCoreClasses() 
			{
				FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddStatic(FlushInitalizedStaticCoreClasses);
			}
			/** Wrapper function to handle default parameter when used as function pointer */
			static void FlushInitalizedStaticCoreClasses()
			{
				bInitializedStaticCoreClasses = false;
			}
		};
		static FAddFlushInitalizedStaticCoreClasses MaybeAddAddFlushInitializedStaticCoreClasses;

#if VALIDATE_INITIALIZECORECLASSES
		bool bWasValid = bInitializedStaticCoreClasses;
		bInitializedStaticCoreClasses = false;
#endif

		if (!bInitializedStaticCoreClasses)
		{
			bInitializedStaticCoreClasses = true;


			// initialize the tracking maps with the core classes
			UClass* CoreClassList[] =
			{
				UObject::StaticClass(),
				UField::StaticClass(),
				UStruct::StaticClass(),
				UScriptStruct::StaticClass(),
				UFunction::StaticClass(),
				UEnum::StaticClass(),
				UClass::StaticClass(),
				UInterface::StaticClass()
			};

			for (UClass* CoreClass : CoreClassList)
			{
				CoreClasses.AddUnique(CoreClass);

				ReferencedObjects.Add(CoreClass);
				ReferencedObjects.Add(CoreClass->GetDefaultObject());
			}

			for (UClass* CoreClass : CoreClasses)
			{
				ProcessStruct(CoreClass);
			}

			CoreReferencesOffset = ReferencedObjects.Num();


#if VALIDATE_INITIALIZECORECLASSES
			if (bWasValid)
			{
				// make sure everything matches up 
				check(CoreClasses.Num() == StaticCoreClasses.Num());
				check(ReferencedObjects.Num() == StaticCoreReferencedObjects.Num());
				check(ProcessedObjects.Num() == StaticProcessedObjects.Num());
				check(SerializedObjects.Num() == StaticSerializedObjects.Num());
				
				
				for (int I = 0; I < CoreClasses.Num(); ++I)
				{
					check(CoreClasses[I] == StaticCoreClasses[I]);
				}
				for (int I = 0; I < ReferencedObjects.Num(); ++I)
				{
					check(ReferencedObjects[I] == StaticCoreReferencedObjects[I]);
				}
				for (const auto& ProcessedObject : ProcessedObjects.ObjectsSet)
				{
					check(ProcessedObject.Value == StaticProcessedObjects.Find(ProcessedObject.Key));
				}
				for (const auto& SerializedObject : SerializedObjects)
				{
					check(StaticSerializedObjects.Find(SerializedObject));
				}
			}
#endif

			StaticCoreClasses = CoreClasses;
			StaticCoreReferencedObjects = ReferencedObjects;
			StaticProcessedObjects = ProcessedObjects;
			StaticSerializedObjects = SerializedObjects;

			check(CurrentClass == nullptr);
			check(CurrentInsertIndex == INDEX_NONE);
		}
		else
		{
			CoreClasses = StaticCoreClasses;
			ReferencedObjects = StaticCoreReferencedObjects;
			ProcessedObjects = StaticProcessedObjects;
			SerializedObjects = StaticSerializedObjects;

			CoreReferencesOffset = StaticCoreReferencedObjects.Num();
		}

#else
		// initialize the tracking maps with the core classes
		UClass* CoreClassList[] =
		{
			UObject::StaticClass(),
			UField::StaticClass(),
			UStruct::StaticClass(),
			UScriptStruct::StaticClass(),
			UFunction::StaticClass(),
			UEnum::StaticClass(),
			UClass::StaticClass(),
			FProperty::StaticClass(),
			FByteProperty::StaticClass(),
			FIntProperty::StaticClass(),
			FBoolProperty::StaticClass(),
			FFloatProperty::StaticClass(),
			FDoubleProperty::StaticClass(),
			FObjectProperty::StaticClass(),
			FClassProperty::StaticClass(),
			FInterfaceProperty::StaticClass(),
			FNameProperty::StaticClass(),
			FStrProperty::StaticClass(),
			FArrayProperty::StaticClass(),
			FTextProperty::StaticClass(),
			FStructProperty::StaticClass(),
			FDelegateProperty::StaticClass(),
			UInterface::StaticClass(),
			FMulticastDelegateProperty::StaticClass(),
			FWeakObjectProperty::StaticClass(),
			FObjectPropertyBase::StaticClass(),
			FLazyObjectProperty::StaticClass(),
			FSoftObjectProperty::StaticClass(),
			FSoftClassProperty::StaticClass(),
			FMapProperty::StaticClass(),
			FSetProperty::StaticClass(),
			FEnumProperty::StaticClass()
		};

		for (UClass* CoreClass : CoreClassList)
		{
			CoreClasses.AddUnique(CoreClass);

			ReferencedObjects.Add(CoreClass);
			ReferencedObjects.Add(CoreClass->GetDefaultObject());
		}

		for (UClass* CoreClass : CoreClasses)
		{
			ProcessStruct(CoreClass);
		}

		CoreReferencesOffset = ReferencedObjects.Num();
#endif
	}

	/**
	 * Adds an object to the list of referenced objects, ensuring that the object is not added more than one.
	 *
	 * @param	Object			the object to add to the list
	 * @param	InsertIndex		the index to insert the object into the export list
	 */
	void AddReferencedObject( UObject* Object, int32 InsertIndex )
	{
		if ( Object != nullptr && !ReferencedObjects.Contains(Object) )
		{
			ReferencedObjects.Insert(Object, InsertIndex);
		}
	}

	/**
	 * Handles serializing and calculating the correct insertion point for an object that will be force-loaded by another object (via an explicit call to Preload).
	 * If the RequiredObject is a UStruct or true is specified for bProcessObject, the RequiredObject will be inserted into the list of exports just before the object
	 * that has a dependency on this RequiredObject.
	 *
	 * @param	RequiredObject		the object which must be created and loaded first
	 * @param	bProcessObject		normally, only the class and archetype for non-UStruct objects are inserted into the list;  specify true to override this behavior
	 *								if RequiredObject is going to be force-loaded, rather than just created
	 */
	void HandleDependency( UObject* RequiredObject, bool bProcessObject=false )
	{
		if ( RequiredObject != nullptr )
		{
			check(CurrentInsertIndex!=INDEX_NONE);

			const int32 PreviousReferencedObjectCount = ReferencedObjects.Num();
			const int32 PreviousInsertIndex = CurrentInsertIndex;

			if (!PackageToSort || RequiredObject->GetOutermost() == PackageToSort)
			{
				// Don't compute prerequisites for objects outside the package, this will recurse into all native properties
				if (UStruct* RequiredObjectStruct = dynamic_cast<UStruct*>(RequiredObject))
				{
					// if this is a struct/class/function/state, it may have a super that needs to be processed first
					ProcessStruct(RequiredObjectStruct);
				}
				else if (bProcessObject)
				{
					// this means that RequiredObject is being force-loaded by the referencing object, rather than simply referenced
					ProcessObject(RequiredObject);
				}
				else
				{
					// only the object's class and archetype are force-loaded, so only those objects need to be in the list before
					// whatever object was referencing RequiredObject
					if (ProcessedObjects.Find(RequiredObject->GetOuter()) == INDEX_NONE)
					{
						HandleDependency(RequiredObject->GetOuter());
					}

					// class is needed before archetype, but we need to process these in reverse order because we are inserting into the list.
					ProcessObject(RequiredObject->GetArchetype());
					ProcessStruct(RequiredObject->GetClass());
				}
			}
			// InsertIndexOffset is the amount the CurrentInsertIndex was incremented during the serialization of SuperField; we need to
			// subtract out this number to get the correct location of the new insert index
			const int32 InsertIndexOffset = CurrentInsertIndex - PreviousInsertIndex;
			const int32 InsertIndexAdvanceCount = (ReferencedObjects.Num() - PreviousReferencedObjectCount) - InsertIndexOffset;
			if ( InsertIndexAdvanceCount > 0 )
			{
				// if serializing SuperField added objects to the list of ReferencedObjects, advance the insertion point so that
				// subsequence objects are placed into the list after the SuperField and its dependencies.
				CurrentInsertIndex += InsertIndexAdvanceCount;
			}
		}
	}

public:
	/**
	 * Constructor
	 */
	FExportReferenceSorter()
	{
		ArIsObjectReferenceCollector = true;
		this->SetIsPersistent(true);
		this->SetIsSaving(true);

		InitializeCoreClasses();
	}

	/**
	 * Verifies that the sorting algorithm is working correctly by checking all objects in the ReferencedObjects array to make sure that their
	 * required objects appear in the list first
	 */
	void VerifySortingAlgorithm()
	{
		FString ErrorString;
		for ( int32 VerifyIndex = CoreReferencesOffset; VerifyIndex < ReferencedObjects.Num(); VerifyIndex++ )
		{
			UObject* Object = ReferencedObjects[VerifyIndex];
			
			// first, make sure that the object's class and archetype appear earlier in the list
			UClass* ObjectClass = Object->GetClass();
			if ( !VerifyDependency(VerifyIndex, ObjectClass, TEXT("Class"), ErrorString) )
			{
				UE_LOG(LogSavePackage, Log, TEXT("%s"), *ErrorString);
			}

			UObject* ObjectArchetype = Object->GetArchetype();
			if ( ObjectArchetype != nullptr && !VerifyDependency(VerifyIndex, ObjectArchetype, TEXT("Archetype"), ErrorString) )
			{
				UE_LOG(LogSavePackage, Log, TEXT("%s"), *ErrorString);
			}

			// UObjectRedirectors are always force-loaded as the loading code needs immediate access to the object pointed to by the Redirector
			UObjectRedirector* Redirector = dynamic_cast<UObjectRedirector*>(Object);
			if ( Redirector != nullptr && Redirector->DestinationObject != nullptr )
			{
				// the Redirector does not force-load the destination object, so we only need its class and archetype.
				UClass* RedirectorDestinationClass = Redirector->DestinationObject->GetClass();
				if ( !VerifyDependency(VerifyIndex, RedirectorDestinationClass, TEXT("Redirector DestinationObject Class"), ErrorString) )
				{
					UE_LOG(LogSavePackage, Log, TEXT("%s"), *ErrorString);
				}

				UObject* RedirectorDestinationArchetype = Redirector->DestinationObject->GetArchetype();
				if ( RedirectorDestinationArchetype != nullptr 
				&& !VerifyDependency(VerifyIndex, RedirectorDestinationArchetype, TEXT("Redirector DestinationObject Archetype"), ErrorString) )
				{
					UE_LOG(LogSavePackage, Log, TEXT("%s"), *ErrorString);
				}
			}
		}
	}

	/**
	 * Clears the list of encountered objects; should be called if you want to re-use this archive.
	 */
	void Clear()
	{
		ReferencedObjects.RemoveAt(CoreReferencesOffset, ReferencedObjects.Num() - CoreReferencesOffset);
	}

	/**
	 * Get the list of new objects which were encountered by this archive; excludes those objects which were passed into the constructor
	 */
	void GetExportList( TArray<UObject*>& out_Exports, UPackage* OuterPackage, bool bIncludeCoreClasses=false )
	{
		PackageToSort = OuterPackage;
		if ( !bIncludeCoreClasses )
		{
			const int32 NumReferencedObjects = ReferencedObjects.Num() - CoreReferencesOffset;
			if ( NumReferencedObjects > 0 )
			{
				int32 OutputIndex = out_Exports.Num();

				out_Exports.AddUninitialized(NumReferencedObjects);
				for ( int32 RefIndex = CoreReferencesOffset; RefIndex < ReferencedObjects.Num(); RefIndex++ )
				{
					out_Exports[OutputIndex++] = ReferencedObjects[RefIndex];
				}
			}
		}
		else
		{
			out_Exports += ReferencedObjects;
		}
	}

	/** 
	 * UObject serialization operator
	 *
	 * @param	Object	an object encountered during serialization of another object
	 *
	 * @return	reference to instance of this class
	 */
	FArchive& operator<<( UObject*& Object )
	{
		// we manually handle class default objects, so ignore those here
		if ( Object != nullptr && !Object->HasAnyFlags(RF_ClassDefaultObject) )
		{
			if ( ProcessedObjects.Find(Object) == INDEX_NONE )
			{
				// if this object is not a UField, it is an object instance that is referenced through script or defaults (when processing classes) or
				// through an normal object reference (when processing the non-class exports).  Since classes and class default objects
				// are force-loaded (and thus, any objects referenced by the class's script or defaults will be created when the class
				// is force-loaded), we'll need to be sure that the referenced object's class and archetype are inserted into the list
				// of exports before the class, so that when CreateExport is called for this object reference we don't have to seek.
				// Note that in the non-UField case, we don't actually need the object itself to appear before the referencing object/class because it won't
				// be force-loaded (thus we don't need to add the referenced object to the ReferencedObject list)

				if (Cast<UField>(Object))
				{
					// when field processing is enabled, ignore any referenced classes since a class's class and CDO are both intrinsic and
					// attempting to deal with them here will only cause problems
					if ( !bIgnoreFieldReferences && !dynamic_cast<UClass*>(Object) )
					{
						if ( CurrentClass == nullptr || Object->GetOuter() != CurrentClass )
						{
							if ( UStruct* StructObject = dynamic_cast<UStruct*>(Object) )
							{
								// if this is a struct/class/function/state, it may have a super that needs to be processed first (Preload force-loads UStruct::SuperField)
								ProcessStruct(StructObject);
							}
							else
							{
								// properties that are enum references need their enums loaded first so that config importing works
								if (UEnum* Enum = Cast<UEnum>(Object))
								{
									HandleDependency(Enum, /*bProcessObject =*/true);
								}

								// a normal field - property, enum, const; just insert it into the list and keep going
								ProcessedObjects.Add(Object);
								
								AddReferencedObject(Object, CurrentInsertIndex);
								if ( !SerializedObjects.Contains(Object) )
								{
									SerializedObjects.Add(Object);
									Object->Serialize(*this);
								}
							}
						}
					}
				}
				else
				{
					HandleDependency(Object);
				}
			}
		}

		return *this;
	}

	/**
	 * Adds a normal object to the list of sorted exports.  Ensures that any objects which will be force-loaded when this object is created or loaded are inserted into
	 * the list before this object.
	 *
	 * @param	Object	the object to process.
	 */
	void ProcessObject( UObject* Object )
	{
		// we manually handle class default objects, so ignore those here
		if ( Object != nullptr )
		{
			if ( !Object->HasAnyFlags(RF_ClassDefaultObject) )
			{
				if ( ProcessedObjects.Find(Object) == INDEX_NONE )
				{
					ProcessedObjects.Add(Object);

					const bool bRecursiveCall = CurrentInsertIndex != INDEX_NONE;
					if ( !bRecursiveCall )
					{
						CurrentInsertIndex = ReferencedObjects.Num();
					}

					// when an object is created (CreateExport), its class and archetype will be force-loaded, so we'll need to make sure that those objects
					// are placed into the list before this object so that when CreateExport calls Preload on these objects, no seeks occur
					// The object's Outer isn't force-loaded, but it will be created before the current object, so we'll need to ensure that its archetype & class
					// are placed into the list before this object.
					HandleDependency(Object->GetClass(), true);
					HandleDependency(Object->GetOuter());
					HandleDependency(Object->GetArchetype(), true);

					// UObjectRedirectors are always force-loaded as the loading code needs immediate access to the object pointed to by the Redirector
					UObjectRedirector* Redirector = dynamic_cast<UObjectRedirector*>(Object);
					if ( Redirector != nullptr && Redirector->DestinationObject != nullptr )
					{
						// the Redirector does not force-load the destination object, so we only need its class and archetype.
						HandleDependency(Redirector->DestinationObject);
					}

					// now we add this object to the list
					AddReferencedObject(Object, CurrentInsertIndex);

					// then serialize the object - any required references encountered during serialization will be inserted into the list before this object, but after this object's
					// class and archetype
					if ( !SerializedObjects.Contains(Object) )
					{
						SerializedObjects.Add(Object);
						Object->Serialize(*this);
					}

					if ( !bRecursiveCall )
					{
						CurrentInsertIndex = INDEX_NONE;
					}
				}
			}
		}
	}

	/**
	 * Adds a UStruct object to the list of sorted exports.  Handles serialization and insertion for any objects that will be force-loaded by this struct (via an explicit call to Preload).
	 *
	 * @param	StructObject	the struct to process
	 */
	void ProcessStruct( UStruct* StructObject )
	{
		if ( StructObject != nullptr )
		{
			if ( ProcessedObjects.Find(StructObject) == INDEX_NONE )
			{
				ProcessedObjects.Add(StructObject);

				const bool bRecursiveCall = CurrentInsertIndex != INDEX_NONE;
				if ( !bRecursiveCall )
				{
					CurrentInsertIndex = ReferencedObjects.Num();
				}

				// this must be done after we've established a CurrentInsertIndex
				HandleDependency(StructObject->GetInheritanceSuper());

				// insert the class/function/state/struct into the list
				AddReferencedObject(StructObject, CurrentInsertIndex);
				if ( !SerializedObjects.Contains(StructObject) )
				{
					const bool bPreviousIgnoreFieldReferences = bIgnoreFieldReferences;

					// first thing to do is collect all actual objects referenced by this struct's script or defaults
					// so we turn off field serialization so that we don't have to worry about handling this struct's fields just yet
					bIgnoreFieldReferences = true;

					bool const bIsClassObject = (dynamic_cast<UClass*>(StructObject) != nullptr);

					SerializedObjects.Add(StructObject);
					StructObject->Serialize(*this);

					// at this point, any objects which were referenced through this struct's script or defaults will be in the list of exports, and 
					// the CurrentInsertIndex will have been advanced so that the object processed will be inserted just before this struct in the array
					// (i.e. just after class/archetypes for any objects which were referenced by this struct's script)

					// now re-enable field serialization and process the struct's properties, functions, enums, structs, etc.  They will be inserted into the list
					// just ahead of the struct itself, so that those objects are created first during seek-free loading.
					bIgnoreFieldReferences = false;
					
					// invoke the serialize operator rather than calling Serialize directly so that the object is handled correctly (i.e. if it is a struct, then we should
					// call ProcessStruct, etc. and all this logic is already contained in the serialization operator)
					if (!bIsClassObject)
					{
						// before processing the Children reference, set the CurrentClass to the class which contains this StructObject so that we
						// don't inadvertently serialize other fields of the owning class too early.
						CurrentClass = StructObject->GetOwnerClass();
					}					

					(*this) << (UObject*&)StructObject->Children;
					CurrentClass = nullptr; //-V519

					(*this) << (UObject*&)StructObject->Next;

					bIgnoreFieldReferences = bPreviousIgnoreFieldReferences;
				}

				// Preload will force-load the class default object when called on a UClass object, so make sure that the CDO is always immediately after its class
				// in the export list; we can't resolve this circular reference, but hopefully we the CDO will fit into the same memory block as the class during 
				// seek-free loading.
				UClass* ClassObject = dynamic_cast<UClass*>(StructObject);
				if ( ClassObject != nullptr )
				{
					UObject* CDO = ClassObject->GetDefaultObject();
					ensureMsgf(nullptr != CDO, TEXT("Error: Invalid CDO in class %s"), *GetPathNameSafe(ClassObject));
					if ((ProcessedObjects.Find(CDO) == INDEX_NONE) && (nullptr != CDO))
					{
						ProcessedObjects.Add(CDO);

						if ( !SerializedObjects.Contains(CDO) )
						{
							SerializedObjects.Add(CDO);
							CDO->Serialize(*this);
						}

						int32 ClassIndex = ReferencedObjects.Find(ClassObject);
						check(ClassIndex != INDEX_NONE);

						// we should be the only one adding CDO's to the list, so this assertion is to catch cases where someone else
						// has added the CDO to the list (as it will probably be in the wrong spot).
						check(!ReferencedObjects.Contains(CDO) || CoreClasses.Contains(ClassObject));
						AddReferencedObject(CDO, ClassIndex + 1);
					}
				}

				if ( !bRecursiveCall )
				{
					CurrentInsertIndex = INDEX_NONE;
				}
			}
		}
	}

	/** Do nothing when serializing soft references, this is required because the presave on soft references can fix redirectors, which is unsafe at this point */
	virtual FArchive& operator<<(FLazyObjectPtr& Value) override { return *this; }
	virtual FArchive& operator<<(FSoftObjectPtr& Value) override { return *this; }
	virtual FArchive& operator<<(FSoftObjectPath& Value) override { return *this; }

private:

	/**
	 * The index into the ReferencedObjects array to insert new objects
	 */
	int32 CurrentInsertIndex = INDEX_NONE;

	/**
	 * The index into the ReferencedObjects array for the first object not referenced by one of the core classes
	 */
	int32 CoreReferencesOffset = INDEX_NONE;

	/**
	 * The classes which are pre-added to the array of ReferencedObjects.  Used for resolving a number of circular dependecy issues between
	 * the boot-strap classes.
	 */
	TArray<UClass*> CoreClasses;

	/**
	 * The list of objects that have been evaluated by this archive so far.
	 */
	struct FOrderedObjectSet
	{
		TMap<UObject*, int32> ObjectsMap;

		int32 Add(UObject* Object)
		{
			const int32 Index = ObjectsMap.Num();
			ObjectsMap.Add(Object, Index);
			return Index;
		}

		inline int32 Find(UObject* Object) const
		{
			const int32 *Index = ObjectsMap.Find(Object);
			if (Index)
			{
				return *Index;
			}
			return INDEX_NONE;
		}
		inline int32 Num() const
		{
			return ObjectsMap.Num();
		}
	};
	FOrderedObjectSet ProcessedObjects;

	/**
	 * The list of objects that have been serialized; used to prevent calling Serialize on an object more than once.
	 */
	TSet<UObject*> SerializedObjects;

	/**
	 * The list of new objects that were encountered by this archive
	 */
	TArray<UObject*> ReferencedObjects;

	/**
	 * Controls whether to process UField objects encountered during serialization of an object.
	 */
	bool bIgnoreFieldReferences = false;

	/**
	 * The UClass currently being processed.  This is used to prevent serialization of a UStruct's Children member causing other fields of the same class to be processed too early due
	 * to being referenced (directly or indirectly) by that field.  For example, if a class has two functions which both have a struct parameter of a struct type which is declared in the same class,
	 * the struct would be inserted into the list immediately before the first function processed.  The second function would be inserted into the list just before the struct.  At runtime,
	 * the "second" function would be created first, which would end up force-loading the struct.  This would cause an unacceptible seek because the struct appears later in the export list, thus
	 * hasn't been created yet.
	 */
	UClass* CurrentClass = nullptr;

	/** Package to constrain checks to */
	UPackage* PackageToSort = nullptr;
};

/**
 * Helper structure encapsulating functionality to sort a linker's export map to allow seek free
 * loading by creating the exports in the order they are in the export map.
 */
struct FObjectExportSeekFreeSorter
{
	/**
	 * Sorts exports in passed in linker in order to avoid seeking when creating them in order and also
	 * conform the order to an already existing linker if non- NULL.
	 *
	 * @param	Linker				LinkerSave to sort export map
	 * @param	LinkerToConformTo	LinkerLoad to conform LinkerSave to if non- NULL
	 */
	void SortExports( FLinkerSave* Linker, FLinkerLoad* LinkerToConformTo )
	{
		SortArchive.SetCookingTarget(Linker->CookingTarget());

		int32					FirstSortIndex = LinkerToConformTo ? LinkerToConformTo->ExportMap.Num() : 0;
		TMap<UObject*,int32>	OriginalExportIndexes;

		// Populate object to current index map.
		for( int32 ExportIndex=FirstSortIndex; ExportIndex<Linker->ExportMap.Num(); ExportIndex++ )
		{
			const FObjectExport& Export = Linker->ExportMap[ExportIndex];
			if( Export.Object )
			{
				// Set the index (key) in the map to the index of this object into the export map.
				OriginalExportIndexes.Add( Export.Object, ExportIndex );
			}
		}

		bool bRetrieveInitialReferences = true;

		// Now we need to sort the export list according to the order in which objects will be loaded.  For the sake of simplicity, 
		// process all classes first so they appear in the list first (along with any objects those classes will force-load) 
		for( int32 ExportIndex=FirstSortIndex; ExportIndex<Linker->ExportMap.Num(); ExportIndex++ )
		{
			const FObjectExport& Export = Linker->ExportMap[ExportIndex];
			if( UClass* ExportObjectClass = dynamic_cast<UClass*>(Export.Object) )
			{
				SortArchive.Clear();
				SortArchive.ProcessStruct(ExportObjectClass);
#if EXPORT_SORTING_DETAILED_LOGGING
				TArray<UObject*> ReferencedObjects;
				SortArchive.GetExportList(ReferencedObjects, Linker->LinkerRoot, bRetrieveInitialReferences);

				UE_LOG(LogSavePackage, Log, TEXT("Referenced objects for (%i) %s in %s"), ExportIndex, *Export.Object->GetFullName(), *Linker->LinkerRoot->GetName());
				for ( int32 RefIndex = 0; RefIndex < ReferencedObjects.Num(); RefIndex++ )
				{
					UE_LOG(LogSavePackage, Log, TEXT("\t%i) %s"), RefIndex, *ReferencedObjects[RefIndex]->GetFullName());
				}
				if ( ReferencedObjects.Num() > 1 )
				{
					// insert a blank line to make the output more readable
					UE_LOG(LogSavePackage, Log, TEXT(""));
				}

				SortedExports += ReferencedObjects;
#else
				SortArchive.GetExportList(SortedExports, Linker->LinkerRoot, bRetrieveInitialReferences);
#endif
				bRetrieveInitialReferences = false;
			}

		}

#if EXPORT_SORTING_DETAILED_LOGGING
		UE_LOG(LogSavePackage, Log, TEXT("*************   Processed %i classes out of %i possible exports for package %s.  Beginning second pass...   *************"), SortedExports.Num(), Linker->ExportMap.Num() - FirstSortIndex, *Linker->LinkerRoot->GetName());
#endif

		// All UClasses, CDOs, functions, properties, etc. are now in the list - process the remaining objects now
		for ( int32 ExportIndex = FirstSortIndex; ExportIndex < Linker->ExportMap.Num(); ExportIndex++ )
		{
			const FObjectExport& Export = Linker->ExportMap[ExportIndex];
			if ( Export.Object )
			{
				SortArchive.Clear();
				SortArchive.ProcessObject(Export.Object);
#if EXPORT_SORTING_DETAILED_LOGGING
				TArray<UObject*> ReferencedObjects;
				SortArchive.GetExportList(ReferencedObjects, Linker->LinkerRoot, bRetrieveInitialReferences);

				UE_LOG(LogSavePackage, Log, TEXT("Referenced objects for (%i) %s in %s"), ExportIndex, *Export.Object->GetFullName(), *Linker->LinkerRoot->GetName());
				for ( int32 RefIndex = 0; RefIndex < ReferencedObjects.Num(); RefIndex++ )
				{
					UE_LOG(LogSavePackage, Log, TEXT("\t%i) %s"), RefIndex, *ReferencedObjects[RefIndex]->GetFullName());
				}
				if ( ReferencedObjects.Num() > 1 )
				{
					// insert a blank line to make the output more readable
					UE_LOG(LogSavePackage, Log, TEXT(""));
				}

				SortedExports += ReferencedObjects;
#else
				SortArchive.GetExportList(SortedExports, Linker->LinkerRoot, bRetrieveInitialReferences);
#endif
				bRetrieveInitialReferences = false;
			}
		}

#if EXPORT_SORTING_DETAILED_LOGGING
		SortArchive.VerifySortingAlgorithm();
#endif
		// Back up existing export map and empty it so we can repopulate it in a sorted fashion.
		TArray<FObjectExport> OldExportMap = Linker->ExportMap;
		Linker->ExportMap.Empty( OldExportMap.Num() );

		// Add exports that can't be re-jiggled as they are part of the exports of the to be
		// conformed to Linker.
		for( int32 ExportIndex=0; ExportIndex<FirstSortIndex; ExportIndex++ )
		{
			Linker->ExportMap.Add( OldExportMap[ExportIndex] );
		}

		// Create new export map from sorted exports.
		for( int32 ObjectIndex=0; ObjectIndex<SortedExports.Num(); ObjectIndex++ )
		{
			// See whether this object was part of the to be sortable exports map...
			UObject* Object		= SortedExports[ObjectIndex];
			int32* ExportIndexPtr	= OriginalExportIndexes.Find( Object );
			if( ExportIndexPtr )
			{
				// And add it if it has been.
				Linker->ExportMap.Add( OldExportMap[*ExportIndexPtr] );
			}
		}

		// Manually add any new NULL exports last as they won't be in the SortedExportsObjects list. 
		// A NULL Export.Object can occur if you are e.g. saving an object in the game that is 
		// OBJECTMARK_NotForClient.
		for( int32 ExportIndex=FirstSortIndex; ExportIndex<OldExportMap.Num(); ExportIndex++ )
		{
			const FObjectExport& Export = OldExportMap[ExportIndex];
			if( Export.Object == nullptr )
			{
				Linker->ExportMap.Add( Export );
			}
		}
	}

private:
	/**
	 * Archive for sorting an objects references according to the order in which they'd be loaded.
	 */
	FExportReferenceSorter SortArchive;

	/** Array of regular objects encountered by CollectExportsInOrderOfUse					*/
	TArray<UObject*>	SortedExports;
};

// helper class for clarification, encapsulation, and elimination of duplicate code
struct FPackageExportTagger
{
	UObject*		Base;
	EObjectFlags	TopLevelFlags;
	UPackage*		Package;
	const class ITargetPlatform* TargetPlatform;

	FPackageExportTagger(UObject* CurrentBase, EObjectFlags CurrentFlags, UPackage* InPackage, const class ITargetPlatform* InTargetPlatform)
	:	Base(CurrentBase)
	,	TopLevelFlags(CurrentFlags)
	,	Package(InPackage)
	,	TargetPlatform(InTargetPlatform)
	{}

	void TagPackageExports( FArchiveSaveTagExports& ExportTagger, bool bRoutePresave )
	{
		const bool bIsCooking = !!TargetPlatform;

		// Route PreSave on Base and serialize it for export tagging.
		if( Base )
		{
			if ( bRoutePresave )
			{
				if (bIsCooking && Base->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
				{
					FArchiveObjectCrc32NonEditorProperties CrcArchive;

					int32 Before = CrcArchive.Crc32(Base);
					Base->PreSave(TargetPlatform);
					int32 After = CrcArchive.Crc32(Base);

					if (Before != After)
					{
						UE_ASSET_LOG(
							LogSavePackage,
							Warning,
							Base,
							TEXT("Non-deterministic cook warning - PreSave() has modified %s '%s' - a resave may be required"),
							Base->HasAnyFlags(RF_ClassDefaultObject) ? TEXT("CDO") : TEXT("archetype"),
							*Base->GetName()
						);
					}
				}
				else
				{
					Base->PreSave(TargetPlatform);
				}
			}

			ExportTagger.ProcessBaseObject(Base);
		}
		if (TopLevelFlags != RF_NoFlags)
		{
			TArray<UObject *> ObjectsInPackage;
			{
				COOK_STAT(FScopedDurationTimer SerializeTimer(SavePackageStats::TagPackageExportsGetObjectsWithOuter));
				GetObjectsWithPackage(Package, ObjectsInPackage);
			}
			// Serialize objects to tag them as OBJECTMARK_TagExp.
			for( int32 Index = 0; Index < ObjectsInPackage.Num(); Index++ )
			{
				UObject* Obj = ObjectsInPackage[Index];
				// Allowed object that have any of the top level flags or have an assigned that we are saving
				if (Obj->HasAnyFlags(TopLevelFlags))
				{
					ExportTagger.ProcessBaseObject(Obj);
				}
			}
		}
		if ( bRoutePresave )
		{
			// Route PreSave.
			{
				TArray<UObject*> TagExpObjects;
				{
					COOK_STAT(FScopedDurationTimer SerializeTimer(SavePackageStats::TagPackageExportsGetObjectsWithMarks));
					GetObjectsWithAnyMarks(TagExpObjects, OBJECTMARK_TagExp);
				}
				for(int32 Index = 0; Index < TagExpObjects.Num(); Index++)
				{
					UObject* Obj = TagExpObjects[Index];
					check(Obj->HasAnyMarks(OBJECTMARK_TagExp));
					//@warning: Objects created from within PreSave will NOT have PreSave called on them!!!
					if (bIsCooking && Obj->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
					{
						FArchiveObjectCrc32NonEditorProperties CrcArchive;

						int32 Before = CrcArchive.Crc32(Obj);
						Obj->PreSave(TargetPlatform);
						int32 After = CrcArchive.Crc32(Obj);

						if (Before != After)
						{
							UE_ASSET_LOG(
								LogSavePackage,
								Warning,
								Obj,
								TEXT("Non-deterministic cook warning - PreSave() has modified %s '%s' - a resave may be required"),
								Obj->HasAnyFlags(RF_ClassDefaultObject) ? TEXT("CDO") : TEXT("archetype"),
								*Obj->GetName()
							);
						}
					}
					else
					{
						Obj->PreSave(TargetPlatform);
					}
				}
			}
		}
	}
};


/** checks whether it is valid to conform NewPackage to OldPackage
 * i.e, that there are no incompatible changes between the two
 * @warning: this function needs to load objects from the old package to do the verification
 * it's very important that it cleans up after itself to avoid conflicts with e.g. script compilation
 * @param NewPackage - the new package being saved
 * @param OldLinker - linker of the old package to conform against
 * @param Error - log device to send any errors
 * @return whether NewPackage can be conformed
 */
static bool ValidateConformCompatibility(UPackage* NewPackage, FLinkerLoad* OldLinker, FOutputDevice* Error)
{
	// various assumptions made about Core and its contents prevent loading a version mapped to a different name from working correctly
	// Core script typically doesn't have any replication related definitions in it anyway
	if (NewPackage->GetFName() == NAME_CoreUObject || NewPackage->GetFName() == GLongCoreUObjectPackageName)
	{
		return true;
	}

	// save the RF_TagGarbageTemp flag for all objects so our use of it doesn't clobber anything
	TMap<UObject*, uint8> ObjectFlagMap;
	for (TObjectIterator<UObject> It; It; ++It)
	{
		ObjectFlagMap.Add(*It, (It->GetFlags() & RF_TagGarbageTemp) ? 1 : 0);
	}

	// this is needed to successfully find intrinsic classes/properties
	OldLinker->LoadFlags |= LOAD_NoWarn | LOAD_Quiet | LOAD_FindIfFail;

	// unfortunately, to get at the classes and their properties we will also need to load the default objects
	// but the remapped package won't be bound to its native instance, so we need to manually copy the constructors
	// so that classes with their own Serialize() implementations are loaded correctly
	{
		BeginLoad(OldLinker->GetSerializeContext());
		for (int32 i = 0; i < OldLinker->ExportMap.Num(); i++)
		{
			UClass* NewClass = (UClass*)StaticFindObjectFast(UClass::StaticClass(), NewPackage, OldLinker->ExportMap[i].ObjectName, true, false);
			UClass* OldClass = static_cast<UClass*>(OldLinker->Create(UClass::StaticClass(), OldLinker->ExportMap[i].ObjectName, OldLinker->LinkerRoot, LOAD_None, false));
			if (OldClass != nullptr && NewClass != nullptr && OldClass->IsNative() && NewClass->IsNative())
			{
				OldClass->ClassConstructor = NewClass->ClassConstructor;
				OldClass->ClassVTableHelperCtorCaller = NewClass->ClassVTableHelperCtorCaller;
				OldClass->ClassAddReferencedObjects = NewClass->ClassAddReferencedObjects;
			}
		}
		EndLoad(OldLinker->GetSerializeContext());
	}

	bool bHadCompatibilityErrors = false;
	// check for illegal change of networking flags on class fields
	for (int32 i = 0; i < OldLinker->ExportMap.Num(); i++)
	{
		if (OldLinker->GetExportClassName(i) == NAME_Class)
		{
			// load the object so we can analyze it
			BeginLoad(OldLinker->GetSerializeContext());
			UClass* OldClass = static_cast<UClass*>(OldLinker->Create(UClass::StaticClass(), OldLinker->ExportMap[i].ObjectName, OldLinker->LinkerRoot, LOAD_None, false));			
			EndLoad(OldLinker->GetSerializeContext());
			if (OldClass != nullptr)
			{
				UClass* NewClass = FindObjectFast<UClass>(NewPackage, OldClass->GetFName(), true, false);
				if (NewClass != nullptr)
				{
					for (TFieldIterator<FField> OldFieldIt(OldClass, EFieldIteratorFlags::ExcludeSuper); OldFieldIt; ++OldFieldIt)
					{
						for (TFieldIterator<FField> NewFieldIt(NewClass, EFieldIteratorFlags::ExcludeSuper); NewFieldIt; ++NewFieldIt)
						{
							if (OldFieldIt->GetFName() == NewFieldIt->GetFName())
							{
								FProperty* OldProp = CastField<FProperty>(*OldFieldIt);
								FProperty* NewProp = CastField<FProperty>(*NewFieldIt);
								if (OldProp != nullptr && NewProp != nullptr)
								{
									if ((OldProp->PropertyFlags & CPF_Net) != (NewProp->PropertyFlags & CPF_Net))
									{
										Error->Logf(ELogVerbosity::Error, TEXT("Network flag mismatch for property %s"), *NewProp->GetPathName());
										bHadCompatibilityErrors = true;
									}
								}
							}
						}
					}

					for (TFieldIterator<UField> OldFieldIt(OldClass,EFieldIteratorFlags::ExcludeSuper); OldFieldIt; ++OldFieldIt)
					{
						for (TFieldIterator<UField> NewFieldIt(NewClass,EFieldIteratorFlags::ExcludeSuper); NewFieldIt; ++NewFieldIt)
						{
							if (OldFieldIt->GetFName() == NewFieldIt->GetFName())
							{
								UFunction* OldFunc = dynamic_cast<UFunction*>(*OldFieldIt);
								UFunction* NewFunc = dynamic_cast<UFunction*>(*NewFieldIt);
								if (OldFunc != nullptr && NewFunc != nullptr)
								{
									if ((OldFunc->FunctionFlags & (FUNC_Net | FUNC_NetServer | FUNC_NetClient)) != (NewFunc->FunctionFlags & (FUNC_Net | FUNC_NetServer | FUNC_NetClient)))
									{
										Error->Logf(ELogVerbosity::Error, TEXT("Network flag mismatch for function %s"), *NewFunc->GetPathName());
										bHadCompatibilityErrors = true;
									}
								}
							}
						}
					}
				}
			}
		}
	}
	// delete all of the newly created objects from the old package by marking everything else and deleting all unmarked objects
	for (TObjectIterator<UObject> It; It; ++It)
	{
		It->SetFlags(RF_TagGarbageTemp);
	}
	for (int32 i = 0; i < OldLinker->ExportMap.Num(); i++)
	{
		if (OldLinker->ExportMap[i].Object != nullptr)
		{
			OldLinker->ExportMap[i].Object->ClearFlags(RF_TagGarbageTemp);
		}
	}
	CollectGarbage(RF_TagGarbageTemp, true);

	// restore RF_TagGarbageTemp flag value
	for (TMap<UObject*, uint8>::TIterator It(ObjectFlagMap); It; ++It)
	{
		UObject* Obj = It.Key();
		check(Obj->IsValidLowLevel()); // if this crashes we deleted something we shouldn't have
		if (It.Value())
		{
			Obj->SetFlags(RF_TagGarbageTemp);
		}
		else
		{
			Obj->ClearFlags(RF_TagGarbageTemp);
		}
	}
	// verify that we cleaned up after ourselves
	for (int32 i = 0; i < OldLinker->ExportMap.Num(); i++)
	{
		checkf(OldLinker->ExportMap[i].Object == nullptr, TEXT("Conform validation code failed to clean up after itself! Surviving object: %s"), *OldLinker->ExportMap[i].Object->GetPathName());
	}

	// finally, abort if there were any errors
	return !bHadCompatibilityErrors;
}

/**
 * Determines the set of object marks that should be excluded for the target platform
 *
 * @param TargetPlatform	The platform being saved for
 * @param bIsCooking		Whether we are cooking or not
 *
 * @return Excluded object marks specific for the particular target platform, objects with any of these marks will be rejected from the cook
 */
EObjectMark GetExcludedObjectMarksForTargetPlatform( const class ITargetPlatform* TargetPlatform, const bool bIsCooking )
{
	EObjectMark ObjectMarks = OBJECTMARK_NOMARKS;

	if( TargetPlatform && bIsCooking )
	{
		if (!TargetPlatform->HasEditorOnlyData())
		{
			ObjectMarks = (EObjectMark)(ObjectMarks | OBJECTMARK_EditorOnly);
		}
		
		const bool bIsServerOnly = TargetPlatform->IsServerOnly();
		const bool bIsClientOnly = TargetPlatform->IsClientOnly();

		if( bIsServerOnly )
		{
			ObjectMarks = (EObjectMark)(ObjectMarks | OBJECTMARK_NotForServer);
		}
		else if( bIsClientOnly )
		{
			ObjectMarks = (EObjectMark)(ObjectMarks | OBJECTMARK_NotForClient);
		}
	}

	return ObjectMarks;
}

#if WITH_EDITOR
/**
 * Helper function to sort export objects by fully qualified names.
 */
bool ExportObjectSorter(const UObject& Lhs, const UObject& Rhs)
{
	// Check names first.
	if (Lhs.GetFName() != Rhs.GetFName())
	{
		return Lhs.GetFName().LexicalLess(Rhs.GetFName());
	}

	// Names equal, compare class names.
	if (Lhs.GetClass()->GetFName() != Rhs.GetClass()->GetFName())
	{
		return Lhs.GetClass()->GetFName().LexicalLess(Rhs.GetClass()->GetFName());
	}

	// Compare by outers if they exist.
	if (Lhs.GetOuter() && Rhs.GetOuter())
	{
		return Lhs.GetOuter()->GetFName().LexicalLess(Rhs.GetOuter()->GetFName());
	}

	if (Lhs.GetOuter())
	{
		return true;
	}

	return false;
}

/**
* Helper equality comparator for export objects. Compares by names, class names and outer names.
*/
bool ExportEqualityComparator(UObject* Lhs, UObject* Rhs)
{
	check(Lhs && Rhs);
	return Lhs->GetOuter() == Rhs->GetOuter()
		&& Lhs->GetClass() == Rhs->GetClass()
		&& Lhs->GetFName() == Rhs->GetFName();
}

/**
 * Remove OBJECTMARK_TagExp from duplicated objects.
 */
TMap<UObject*, UObject*> UnmarkExportTagFromDuplicates()
{
	TMap<UObject*, UObject*> RedirectDuplicatesToOriginals;
	TArray<UObject*> Objects;
	GetObjectsWithAnyMarks(Objects, OBJECTMARK_TagExp);

	Objects.Sort(ExportObjectSorter);

	int32 LastUniqueObjectIndex = 0;
	for (int32 CurrentObjectIndex = 1; CurrentObjectIndex < Objects.Num(); ++CurrentObjectIndex)
	{
		UObject* LastUniqueObject = Objects[LastUniqueObjectIndex];
		UObject* CurrentObject = Objects[CurrentObjectIndex];

		// Check if duplicates with different pointers
		if (LastUniqueObject != CurrentObject
			// but matching names
			&& ExportEqualityComparator(LastUniqueObject, CurrentObject))
		{
			// Don't export duplicates.
			CurrentObject->UnMark(OBJECTMARK_TagExp);
			RedirectDuplicatesToOriginals.Add(CurrentObject, LastUniqueObject);
		}
		else
		{
			LastUniqueObjectIndex = CurrentObjectIndex;
		}
	}

	return RedirectDuplicatesToOriginals;
}

COREUOBJECT_API extern bool GOutputCookingWarnings;

class FDiffSerializeArchive : public FLargeMemoryWriter
{
private:
	FArchive *TestArchive;
	TArray<FName> DebugDataStack;
	bool bDisable;
public:


	FDiffSerializeArchive(const TCHAR* InFilename, FArchive *InTestArchive) : FLargeMemoryWriter(0, true, InFilename), TestArchive(InTestArchive)
	{ 
		ArDebugSerializationFlags = DSF_IgnoreDiff;
		bDisable = false;
	}

	virtual void Serialize(void* InData, int64 Num) override
	{
		TArray<int8> TestMemory;

		if (TestArchive)
		{
			int64 Pos = FMath::Min(FLargeMemoryWriter::Tell(), TestArchive->TotalSize());
			TestArchive->Seek(Pos);
			TestMemory.AddZeroed(Num);
			int64 ReadSize = FMath::Min(Num, TestArchive->TotalSize() - Pos);
			TestArchive->Serialize((void*)TestMemory.GetData(), ReadSize);

			if (!(ArDebugSerializationFlags&DSF_IgnoreDiff) && (!bDisable))
			{
				if (FMemory::Memcmp((void*)TestMemory.GetData(), InData, Num) != 0)
				{
					// get the calls debug callstack and 
					FString DebugStackString;
					for (const auto& DebugData : DebugDataStack)
					{
						DebugStackString += DebugData.ToString();
						DebugStackString += TEXT("->");
					}

					UE_LOG(LogSavePackage, Warning, TEXT("Diff cooked package archive recognized a difference %lld Filename %s, stack %s "), Pos, *GetArchiveName(), *DebugStackString);

					// only log one message per archive, from this point the entire package is probably messed up
					bDisable = true;
				}
			}
		}
		FLargeMemoryWriter::Serialize(InData, Num);
	}

	virtual void PushDebugDataString(const FName& DebugData) override
	{
		DebugDataStack.Add(DebugData);
	}
	virtual void PopDebugDataString() override
	{
		DebugDataStack.Pop();
	}

	virtual FString GetArchiveName() const override
	{
		return TestArchive->GetArchiveName();
	}
};

#endif


struct FEDLCookChecker : public TThreadSingleton<FEDLCookChecker>
{
	friend TThreadSingleton<FEDLCookChecker>;

	struct FEDLNodeID
	{
		TArray<FName> ObjectPath;
		bool bDepIsSerialize;

		FEDLNodeID() : bDepIsSerialize(false) {}
		FEDLNodeID(UObject* DepObject, bool bInDepIsSerialize)
			: bDepIsSerialize(bInDepIsSerialize)
		{
			while (DepObject)
			{
				ObjectPath.Add(DepObject->GetFName());
				DepObject = DepObject->GetOuter();
			}
		}

		bool operator==(const FEDLNodeID& Other) const
		{
			return bDepIsSerialize == Other.bDepIsSerialize && ObjectPath == Other.ObjectPath;
		}

		FString ToString() const
		{
			FString RetString = bDepIsSerialize ? TEXT("Serialize:") : TEXT("Create:");
			for (int32 NameIdx = ObjectPath.Num() - 1; NameIdx >= 0; --NameIdx)
			{
				RetString += ObjectPath[NameIdx].ToString();
				if (NameIdx > 0)
				{
					if (NameIdx == ObjectPath.Num() - 1)
					{
						RetString += TEXT(".");
					}
					else
					{
						RetString += TEXT(":");
					}
				}
			}
			return RetString;
		}

		friend FORCEINLINE uint32 GetTypeHash(const FEDLNodeID& A)
		{
			uint32 Hash = 0;
			for (const FName& Name : A.ObjectPath)
			{
				Hash = HashCombine(Hash, GetTypeHash(Name));
			}
			return (Hash << 1) | (uint32)A.bDepIsSerialize;
		}
	};

	static FCriticalSection CookCheckerInstanceCritical;
	static TArray<FEDLCookChecker*> CookCheckerInstances;

	bool bIsActive;
	TMultiMap<FEDLNodeID, FName> ImportToImportingPackage;
	TSet<FEDLNodeID> Exports;
	TMultiMap<FEDLNodeID, FEDLNodeID> NodePrereqs;

	FEDLCookChecker()
	{
		SetActiveIfNeeded();

		FScopeLock CookCheckerInstanceLock(&CookCheckerInstanceCritical);
		CookCheckerInstances.Add(this);
	}

	void SetActiveIfNeeded()
	{
		bIsActive = IsEventDrivenLoaderEnabledInCookedBuilds() && !FParse::Param(FCommandLine::Get(), TEXT("DisableEDLCookChecker"));
	}

	void Reset()
	{
		check(!GIsSavingPackage);

		ImportToImportingPackage.Empty();
		Exports.Empty();
		NodePrereqs.Empty();
		bIsActive = false;
	}

	void AddImport(UObject* Import, UPackage* ImportingPackage)
	{
		if (bIsActive)
		{
			if (!Import->GetOutermost()->HasAnyPackageFlags(PKG_CompiledIn))
			{
				FEDLNodeID ImportID(Import, true);
				FName ImportingPackageName = ImportingPackage->GetFName();

				ImportToImportingPackage.Add(MoveTemp(ImportID), MoveTemp(ImportingPackageName));
			}
		}
	}
	void AddExport(UObject* Export)
	{
		if (bIsActive)
		{
			FEDLNodeID ExportID(Export, true);
			Exports.Add(MoveTemp(ExportID));
			AddArc(Export, false, Export, true); // every export must be created before it can be serialize...these arcs are implicit and not listed in any table.
		}
	}

	void AddArc(UObject* DepObject, bool bDepIsSerialize, UObject* Export, bool bExportIsSerialize)
	{
		if (bIsActive)
		{
			FEDLNodeID ExportID(Export, bExportIsSerialize);
			FEDLNodeID DepID(DepObject, bDepIsSerialize);

			NodePrereqs.Add(MoveTemp(ExportID), MoveTemp(DepID));
		}
	}

	static void StartSavingEDLCookInfoForVerification()
	{
		FScopeLock CookCheckerInstanceLock(&CookCheckerInstanceCritical);
		for (FEDLCookChecker* Checker : CookCheckerInstances)
		{
			Checker->Reset();
			Checker->SetActiveIfNeeded();
		}
	}

	static bool CheckForCyclesInner(TMultiMap<FEDLNodeID, FEDLNodeID>& NodePrereqs, TSet<FEDLNodeID>& Visited, TSet<FEDLNodeID>& Stack, const FEDLNodeID& Visit, FEDLNodeID& FailNode)
	{
		bool bResult = false;
		if (Stack.Contains(Visit))
		{
			FailNode = Visit;
			bResult = true;
		}
		else
		{
			bool bWasAlreadyTested = false;
			Visited.Add(Visit, &bWasAlreadyTested);
			if (!bWasAlreadyTested)
			{
				Stack.Add(Visit);
				for (auto It = NodePrereqs.CreateConstKeyIterator(Visit); !bResult && It; ++It)
				{
					bResult = CheckForCyclesInner(NodePrereqs, Visited, Stack, It.Value(), FailNode);
				}
				Stack.Remove(Visit);
			}
		}
		UE_CLOG(bResult && Stack.Contains(FailNode), LogSavePackage, Error, TEXT("Cycle Node %s"), *Visit.ToString());
		return bResult;
	}

	static void Verify(bool bFullReferencesExpected)
	{
		check(!GIsSavingPackage);

		bool bIsActive = false;
		TMultiMap<FEDLNodeID, FName> ImportToImportingPackage;
		TSet<FEDLNodeID> Exports;
		TMultiMap<FEDLNodeID, FEDLNodeID> NodePrereqs;

		{
			FScopeLock CookCheckerInstanceLock(&CookCheckerInstanceCritical);
			for (FEDLCookChecker* Checker : CookCheckerInstances)
			{
				if (Checker->bIsActive)
				{
					bIsActive = true;
					Exports.Append(MoveTemp(Checker->Exports));
					ImportToImportingPackage.Append(MoveTemp(Checker->ImportToImportingPackage));
					NodePrereqs.Append(MoveTemp(Checker->NodePrereqs));
				}
				Checker->Reset();
			}			
		}

		if (bIsActive && Exports.Num())
		{
			double StartTime = FPlatformTime::Seconds();
			
			if (bFullReferencesExpected)
			{
				// imports to things that are not exports...
				for (const auto& Pair : ImportToImportingPackage)
				{
					if (!Exports.Contains(Pair.Key))
					{
						UE_LOG(LogSavePackage, Warning, TEXT("%s imported %s, but it was never saved as an export."), *Pair.Value.ToString(), *Pair.Key.ToString());
					}
				}
			}
			// cycles in the dep graph
			TSet<FEDLNodeID> Visited;
			TSet<FEDLNodeID> Stack;
			bool bHadCycle = false;
			for (const FEDLNodeID& Export : Exports)
			{
				FEDLNodeID FailNode;
				if (CheckForCyclesInner(NodePrereqs, Visited, Stack, Export, FailNode))
				{
					UE_LOG(LogSavePackage, Error, TEXT("----- %s contained a cycle (listed above)."), *FailNode.ToString());
					bHadCycle = true;
				}
			}
			if (bHadCycle)
			{
				UE_LOG(LogSavePackage, Fatal, TEXT("EDL dep graph contained a cycle (see errors, above). This is fatal at runtime so it is fatal at cook time."));
			}
			UE_LOG(LogSavePackage, Display, TEXT("Took %fs to verify the EDL loading graph."), float(FPlatformTime::Seconds() - StartTime));
		}
	}
};

FCriticalSection FEDLCookChecker::CookCheckerInstanceCritical;
TArray<FEDLCookChecker*> FEDLCookChecker::CookCheckerInstances;

void StartSavingEDLCookInfoForVerification()
{
	FEDLCookChecker::StartSavingEDLCookInfoForVerification();
}

void VerifyEDLCookInfo(bool bFullReferencesExpected)
{
	FEDLCookChecker::Verify(bFullReferencesExpected);
}

void AddFileToHash(FString const& Filename, FMD5& Hash)
{
	TArray<uint8> LocalScratch;
	LocalScratch.SetNumUninitialized(1024 * 64);

	FArchive* Ar = IFileManager::Get().CreateFileReader(*Filename);
	
	const int64 Size = Ar->TotalSize();
	int64 Position = 0;

	while (Position < Size)
	{
		const auto ReadNum = FMath::Min(Size - Position, (int64)LocalScratch.Num());
		Ar->Serialize(LocalScratch.GetData(), ReadNum);
		Hash.Update(LocalScratch.GetData(), ReadNum);
		Position += ReadNum;
	}
	delete Ar;
}

FSavePackageResultStruct UPackage::Save(UPackage* InOuter, UObject* Base, EObjectFlags TopLevelFlags, const TCHAR* Filename,
	FOutputDevice* Error, FLinkerNull* ConformNO, bool bForceByteSwapping, bool bWarnOfLongFilename, uint32 SaveFlags,
	const class ITargetPlatform* TargetPlatform, const FDateTime& FinalTimeStamp, bool bSlowTask, FArchiveDiffMap* InOutDiffMap,
	FSavePackageContext* SavePackageContext)
{
	COOK_STAT(FScopedDurationTimer FuncSaveTimer(SavePackageStats::SavePackageTimeSec));
	COOK_STAT(SavePackageStats::NumPackagesSaved++);
	SCOPED_SAVETIMER(UPackage_Save);

	FLinkerLoad* Conform = nullptr;

	// Sanity checks
	check(InOuter);
	check(Filename);
	const bool bIsCooking = TargetPlatform != nullptr;


#if WITH_EDITOR
	TMap<UObject*, UObject*> ReplacedImportOuters;

	// Add the external package flag when not cooking
	if (TopLevelFlags != RF_NoFlags && !bIsCooking)
	{
		TopLevelFlags |= RF_HasExternalPackage;
	}

	// if the in memory package filename is different the filename we are saving it to,
	// regenerate a new persistent id for it.
	FString PackageFilename(Filename);
	bool bIsValidLongPackageName = FPackageName::TryConvertFilenameToLongPackageName(PackageFilename, PackageFilename);
	if (!bIsCooking && !InOuter->FileName.IsNone() && InOuter->FileName.ToString() != PackageFilename && !(SaveFlags & SAVE_FromAutosave))
	{
		InOuter->SetPersistentGuid(FGuid::NewGuid());
	}
#endif //WITH_EDITOR

	const bool bSavingConcurrent = !!(SaveFlags & ESaveFlags::SAVE_Concurrent);

	if (FPlatformProperties::HasEditorOnlyData())
	{
		TRefCountPtr<FUObjectSerializeContext> SaveContext(FUObjectThreadContext::Get().GetSerializeContext());

		const bool bComputeHash = (SaveFlags & SAVE_ComputeHash) != 0;
#if !WITH_EDITOR
		const bool bDiffing = false;
#else
		const bool bDiffing = (SaveFlags & (SAVE_DiffCallstack | SAVE_DiffOnly)) != 0;

		struct FDiffSettings
		{
			int32 MaxDiffsToLog;
			bool bIgnoreHeaderDiffs;
			bool bSaveForDiff;
			FDiffSettings(bool bDiffing)
				: MaxDiffsToLog(5)
				, bIgnoreHeaderDiffs(false)
				, bSaveForDiff(false)
			{
				if (bDiffing)
				{
					GConfig->GetInt(TEXT("CookSettings"), TEXT("MaxDiffsToLog"), MaxDiffsToLog, GEditorIni);
					// Command line override for MaxDiffsToLog
					FParse::Value(FCommandLine::Get(), TEXT("MaxDiffstoLog="), MaxDiffsToLog);

					GConfig->GetBool(TEXT("CookSettings"), TEXT("IgnoreHeaderDiffs"), bIgnoreHeaderDiffs, GEditorIni);
					// Command line override for IgnoreHeaderDiffs
					if (bIgnoreHeaderDiffs)
					{						
						bIgnoreHeaderDiffs = !FParse::Param(FCommandLine::Get(), TEXT("HeaderDiffs"));
					}
					else
					{
						bIgnoreHeaderDiffs = FParse::Param(FCommandLine::Get(), TEXT("IgnoreHeaderDiffs"));
					}
					bSaveForDiff = FParse::Param(FCommandLine::Get(), TEXT("SaveForDiff"));
				}
			}
		} DiffSettings(bDiffing);
#endif

		if (GIsSavingPackage && !bSavingConcurrent)
		{
			ensureMsgf(false, TEXT("Recursive SavePackage() is not supported"));
			return ESavePackageResult::Error;
		}

		bool bDiffOnlyIdentical = true;
		FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
		FEDLCookChecker& EDLCookChecker = FEDLCookChecker::Get();

#if WITH_EDITORONLY_DATA
		if (bIsCooking && (!(SaveFlags & ESaveFlags::SAVE_KeepEditorOnlyCookedPackages)))
		{
			static struct FCanSkipEditorReferencedPackagesWhenCooking
			{
				bool bCanSkipEditorReferencedPackagesWhenCooking;
				FCanSkipEditorReferencedPackagesWhenCooking()
					: bCanSkipEditorReferencedPackagesWhenCooking(true)
				{
					GConfig->GetBool(TEXT("Core.System"), TEXT("CanSkipEditorReferencedPackagesWhenCooking"), bCanSkipEditorReferencedPackagesWhenCooking, GEngineIni);
				}
				FORCEINLINE operator bool() const { return bCanSkipEditorReferencedPackagesWhenCooking; }
			} CanSkipEditorReferencedPackagesWhenCooking;

			// Don't save packages marked as editor-only.
			if (CanSkipEditorReferencedPackagesWhenCooking && InOuter->IsLoadedByEditorPropertiesOnly())
			{
				UE_CLOG(!(SaveFlags & SAVE_NoError), LogSavePackage, Display, TEXT("Package loaded by editor-only properties: %s. Package will not be saved."), *InOuter->GetName());
				return ESavePackageResult::ReferencedOnlyByEditorOnlyData;
			}
			else if (InOuter->HasAnyPackageFlags(PKG_EditorOnly))
			{
				UE_CLOG(!(SaveFlags & SAVE_NoError), LogSavePackage, Display, TEXT("Package marked as editor-only: %s. Package will not be saved."), *InOuter->GetName());
				return ESavePackageResult::ReferencedOnlyByEditorOnlyData;
			}
		}
#endif
		// if we are cooking we should be doing it in the editor
		// otherwise some other assumptions are bad
		check(!bIsCooking || WITH_EDITOR);
#if WITH_EDITOR
		if (!bIsCooking)
		{
			// Attempt to create a backup of this package before it is saved, if applicable
			if (FCoreUObjectDelegates::AutoPackageBackupDelegate.IsBound())
			{
				FCoreUObjectDelegates::AutoPackageBackupDelegate.Execute(*InOuter);
			}
		}

#endif	// #if WITH_EDITOR

		// do any path replacements on the source DestFile
		const FString NewPath = FString(Filename);

		// point to the new version of the path
		Filename = *NewPath;

		if (!bSavingConcurrent)
		{
			// We need to fulfill all pending streaming and async loading requests to then allow us to lock the global IO manager. 
			// The latter implies flushing all file handles which is a pre-requisite of saving a package. The code basically needs 
			// to be sure that we are not reading from a file that is about to be overwritten and that there is no way we might 
			// start reading from the file till we are done overwriting it.
			FlushAsyncLoading();
		}

		(*GFlushStreamingFunc)();

		uint32 Time = 0; CLOCK_CYCLES(Time);
		int64 TotalPackageSizeUncompressed = 0;

		TFuture<FMD5Hash> PackageMD5Destination;
		TAsyncWorkSequence<FMD5> AsyncWriteAndHashSequence;

		// Make sure package is fully loaded before saving. 
		if (!Base && !InOuter->IsFullyLoaded())
		{
			if (!(SaveFlags & SAVE_NoError))
			{
				// We cannot save packages that aren't fully loaded as it would clobber existing not loaded content.
				FText ErrorText;
				if (InOuter->ContainsMap())
				{
					FFormatNamedArguments Arguments;
					Arguments.Add(TEXT("Name"), FText::FromString(NewPath));
					ErrorText = FText::Format(NSLOCTEXT("SavePackage", "CannotSaveMapPartiallyLoaded", "Map '{Name}' cannot be saved as it has only been partially loaded"), Arguments);
				}
				else
				{
					FFormatNamedArguments Arguments;
					Arguments.Add(TEXT("Name"), FText::FromString(NewPath));
					ErrorText = FText::Format(NSLOCTEXT("SavePackage", "CannotSaveAssetPartiallyLoaded", "Asset '{Name}' cannot be saved as it has only been partially loaded"), Arguments);
				}
				Error->Logf(ELogVerbosity::Warning, TEXT("%s"), *ErrorText.ToString());
			}
			return ESavePackageResult::Error;
		}

		// Make sure package is allowed to be saved.
		if (!TargetPlatform && FCoreUObjectDelegates::IsPackageOKToSaveDelegate.IsBound())
		{
			bool bIsOKToSave = FCoreUObjectDelegates::IsPackageOKToSaveDelegate.Execute(InOuter, Filename, Error);
			if (!bIsOKToSave)
			{
				if (!(SaveFlags & SAVE_NoError))
				{
					FText ErrorText;
					if (InOuter->ContainsMap())
					{
						FFormatNamedArguments Arguments;
						Arguments.Add(TEXT("Name"), FText::FromString(NewPath));
						ErrorText = FText::Format(NSLOCTEXT("SavePackage", "MapSaveNotAllowed", "Map '{Name}' is not allowed to save (see log for reason)"), Arguments);
					}
					else
					{
						FFormatNamedArguments Arguments;
						Arguments.Add(TEXT("Name"), FText::FromString(NewPath));
						ErrorText = FText::Format(NSLOCTEXT("SavePackage", "AssetSaveNotAllowed", "Asset '{Name}' is not allowed to save (see log for reason)"), Arguments);
					}
					Error->Logf(ELogVerbosity::Warning, TEXT("%s"), *ErrorText.ToString());
				}
				return ESavePackageResult::Error;
			}
		}

		// if we're conforming, validate that the packages are compatible
		if (Conform != nullptr && !ValidateConformCompatibility(InOuter, Conform, Error))
		{
			if (!(SaveFlags & SAVE_NoError))
			{
				FText ErrorText;
				if (InOuter->ContainsMap())
				{
					FFormatNamedArguments Arguments;
					Arguments.Add(TEXT("Name"), FText::FromString(NewPath));
					ErrorText = FText::Format(NSLOCTEXT("SavePackage", "CannotSaveMapConformIncompatibility", "Conformed Map '{Name}' cannot be saved as it is incompatible with the original"), Arguments);
				}
				else
				{
					FFormatNamedArguments Arguments;
					Arguments.Add(TEXT("Name"), FText::FromString(NewPath));
					ErrorText = FText::Format(NSLOCTEXT("SavePackage", "CannotSaveAssetConformIncompatibility", "Conformed Asset '{Name}' cannot be saved as it is incompatible with the original"), Arguments);
				}
				Error->Logf(ELogVerbosity::Error, TEXT("%s"), *ErrorText.ToString());
			}
			return ESavePackageResult::Error;
		}

		const bool FilterEditorOnly = InOuter->HasAnyPackageFlags(PKG_FilterEditorOnly);

		// Route PreSaveRoot to allow e.g. the world to attach components for the persistent level.
		// If we are saving concurrently, this should have been called before UPackage::Save was called.
		bool bCleanupIsRequired = false;
		if (Base && !bSavingConcurrent)
		{
			bCleanupIsRequired = Base->PreSaveRoot(Filename);
		}

		// Init.
		FString CleanFilename = FPaths::GetCleanFilename(Filename);

		FFormatNamedArguments Args;
		Args.Add(TEXT("CleanFilename"), FText::FromString(CleanFilename));

		FText StatusMessage = FText::Format(NSLOCTEXT("Core", "SavingFile", "Saving file: {CleanFilename}..."), Args);

		const int32 TotalSaveSteps = 33;
		FScopedSlowTask SlowTask(TotalSaveSteps, StatusMessage, bSlowTask);
		SlowTask.MakeDialog(SaveFlags & SAVE_FromAutosave ? true : false);

		SlowTask.EnterProgressFrame();

		bool Success = true;
		bool bRequestStub = false;
		{
			// FullyLoad the package's Loader, so that anything we need to serialize (bulkdata, thumbnails) is available
			COOK_STAT(FScopedDurationTimer SaveTimer(SavePackageStats::FullyLoadLoadersTimeSec));
			EnsureLoadingComplete(InOuter);
		}
		SlowTask.EnterProgressFrame();

		// Untag all objects and names.
		UnMarkAllObjects();

		TArray<UObject*> CachedObjects;

		// structure to track what every export needs to import (native only)
		TMap<UObject*, TArray<UObject*> > NativeObjectDependencies;

		// Size of serialized out package in bytes. This is before compression.
		int32 PackageSize = INDEX_NONE;
		{			
			// TODO: Require a SavePackageContext and move to EditorEngine
			FPackageNameMapSaver NameMapSaver;

			uint32 ComparisonFlags = PPF_DeepCompareInstances | PPF_DeepCompareDSOsOnly;

			// Export objects (tags them as OBJECTMARK_TagExp).
			FArchiveSaveTagExports ExportTaggerArchive( InOuter );
			ExportTaggerArchive.SetPortFlags( ComparisonFlags );
			ExportTaggerArchive.SetCookingTarget(TargetPlatform);
			ExportTaggerArchive.SetSerializeContext(SaveContext);

			check( ExportTaggerArchive.IsCooking() == !!TargetPlatform );
			check( ExportTaggerArchive.IsCooking() == bIsCooking );

			// Tag exports and route presave.
			FPackageExportTagger PackageExportTagger(Base, TopLevelFlags, InOuter, TargetPlatform);
			{
				SCOPED_SAVETIMER(UPackage_Save_TagExportsWithPresave);

				COOK_STAT(FScopedDurationTimer SaveTimer(SavePackageStats::TagPackageExportsPresaveTimeSec));
				// Do not route presave if saving concurrently. This should have been done before the concurrent save started.
				// Also if we're trying to diff the package and gathering callstacks, Presave has already been done
				const bool bRoutePresave = !bSavingConcurrent && !(SaveFlags & SAVE_DiffCallstack);
				PackageExportTagger.TagPackageExports(ExportTaggerArchive, bRoutePresave);
				ExportTaggerArchive.SetFilterEditorOnly(FilterEditorOnly);
			}
		
#if USE_STABLE_LOCALIZATION_KEYS
			if (GIsEditor)
			{
				// We need to ensure that we have a package localization namespace as the package loading will need it
				// We need to do this before entering the GIsSavingPackage block as it may change the package meta-data
				TextNamespaceUtil::EnsurePackageNamespace(InOuter);
			}
#endif // USE_STABLE_LOCALIZATION_KEYS

			if (InOuter->WorldTileInfo.IsValid())
			{
				// collect custom version from wc tile info
				ExportTaggerArchive << *(InOuter->WorldTileInfo);
			}

			{
				check(!IsGarbageCollecting());
				// set GIsSavingPackage here as it is now illegal to create any new object references; they potentially wouldn't be saved correctly								
				struct FScopedSavingFlag
				{
					FScopedSavingFlag(bool InSavingConcurrent)
						: bSavingConcurrent(InSavingConcurrent)
					{
						// We need the same lock as GC so that no StaticFindObject can happen in parallel to saveing a package
						if (IsInGameThread())
						{
							FGCCSyncObject::Get().GCLock();
						}
						else
						{
							FGCCSyncObject::Get().LockAsync();
						}

						// Do not change GIsSavingPackage while saving concurrently. It should have been set before and after all packages are saved
						if (!bSavingConcurrent)
						{
							GIsSavingPackage = true;
						}
					}
					~FScopedSavingFlag() 
					{ 
						if (!bSavingConcurrent)
						{
							GIsSavingPackage = false;
						}
						if (IsInGameThread())
						{
							FGCCSyncObject::Get().GCUnlock();
						}
						else
						{
							FGCCSyncObject::Get().UnlockAsync();
						}
					}

					bool bSavingConcurrent;
				} IsSavingFlag(bSavingConcurrent);

			
				{
					SCOPED_SAVETIMER(UPackage_Save_TagExports);
					COOK_STAT(FScopedDurationTimer SaveTimer(SavePackageStats::TagPackageExportsTimeSec));
					// Clear all marks (OBJECTMARK_TagExp and exclusion marks) again as we need to redo tagging below.
					UnMarkAllObjects();
			
					// We need to serialize objects yet again to tag objects that were created by PreSave as OBJECTMARK_TagExp.
					PackageExportTagger.TagPackageExports( ExportTaggerArchive, false );
				}

				// Kick off any Precaching required for the target platform to save these objects
				// only need to do this if we are cooking a different platform then the one which is currently running
				// TODO: if save package is canceled then call ClearCache on each object

#if WITH_EDITOR
				if ( bIsCooking && !bSavingConcurrent )
				{
					TArray<UObject*> TagExpObjects;
					GetObjectsWithAnyMarks(TagExpObjects, OBJECTMARK_TagExp);
					for ( int Index =0; Index < TagExpObjects.Num(); ++Index)
					{
						UObject *ExpObject = TagExpObjects[Index];
						if ( ExpObject->HasAnyMarks( OBJECTMARK_TagExp ) )
						{
							ExpObject->BeginCacheForCookedPlatformData( TargetPlatform );
							CachedObjects.Add( ExpObject );
						}
					}
				}
#endif

				SlowTask.EnterProgressFrame();

				// structure to track what every export needs to import
				TMap<UObject*, TArray<UObject*> > ObjectDependencies;

				// and a structure to track non-redirector references
				TSet<UObject*> DependenciesReferencedByNonRedirectors;
		
				/** If true, we are going to save to disk async to save time. */
				const bool bSaveAsync = !!(SaveFlags & SAVE_Async);

				const bool bSaveUnversioned = !!(SaveFlags & SAVE_Unversioned);

				TUniquePtr<FLinkerSave> Linker = nullptr;
				FArchiveFormatterType* Formatter = nullptr;
				FArchive* TextFormatArchive = nullptr;
				const bool bTextFormat = FString(Filename).EndsWith(FPackageName::GetTextAssetPackageExtension()) || FString(Filename).EndsWith(FPackageName::GetTextMapPackageExtension());

				const FString BaseFilename = FPaths::GetBaseFilename(Filename);
				// Make temp file. CreateTempFilename guarantees unique, non-existing filename.
				// The temp file will be saved in the game save folder to not have to deal with potentially too long paths.
				// Since the temp filename may include a 32 character GUID as well, limit the user prefix to 32 characters.
				TOptional<FString> TempFilename;
				TOptional<FString> TextFormatTempFilename;
				ON_SCOPE_EXIT
				{
					// free the file handle and delete the temporary file
					Linker->CloseAndDestroySaver();
					if (TempFilename.IsSet())
					{
						IFileManager::Get().Delete(*TempFilename.GetValue());
					}
					if (TextFormatTempFilename.IsSet())
					{
						IFileManager::Get().Delete(*TextFormatTempFilename.GetValue());
					}
				};
	
				{
					SCOPED_SAVETIMER(UPackage_Save_CreateLinkerSave);

#if WITH_EDITOR
					FString DiffCookedPackagesPath;
					// if we are cooking and we have diff cooked packages on the commandline then do some special stuff

					// Finds the asset object within a package
					auto FindAssetInPackage = [](UPackage* Package) -> UObject*
					{
						UObject* Asset = nullptr;
						ForEachObjectWithOuter(Package, [&Asset](UObject* Object)
							{
								if (!Asset && Object->IsAsset())
								{
									Asset = Object;
								}
							}, /*bIncludeNestedObjects*/ false);
						return Asset;
					};

					if (TargetPlatform != nullptr && (SaveFlags & SAVE_DiffCallstack))
					{
						// The entire package will be serialized to memory and then compared against package on disk.
						// Each difference will be log with its Serialize call stack trace
						FArchive* Saver = new FArchiveStackTrace(FindAssetInPackage(InOuter), *InOuter->FileName.ToString(), true, InOutDiffMap);
						Linker = TUniquePtr<FLinkerSave>(new FLinkerSave(InOuter, Saver, bForceByteSwapping, bSaveUnversioned));
					}
					else if (TargetPlatform != nullptr && (SaveFlags & SAVE_DiffOnly))
					{
						// The entire package will be serialized to memory and then compared against package on disk
						FArchive* Saver = new FArchiveStackTrace(FindAssetInPackage(InOuter), *InOuter->FileName.ToString(), false);
						Linker = TUniquePtr<FLinkerSave>(new FLinkerSave(InOuter, Saver, bForceByteSwapping, bSaveUnversioned));
					}
					else if ((!!TargetPlatform) && FParse::Value(FCommandLine::Get(), TEXT("DiffCookedPackages="), DiffCookedPackagesPath))
					{
						UE_LOG(LogSavePackage, Warning, TEXT("The DiffCookedPackages command line argument is now deprecated, please use the -diffonly commandline for the cook commandlet instead."));

						FString TestArchiveFilename = Filename;
						// TestArchiveFilename.ReplaceInline(TEXT("Cooked"), TEXT("CookedDiff"));
						DiffCookedPackagesPath.ReplaceInline(TEXT("\\"), TEXT("/"));
						FString CookedPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() + TEXT("Cooked/"));
						CookedPath.ReplaceInline(TEXT("\\"), TEXT("/"));
						TestArchiveFilename.ReplaceInline(*CookedPath, *DiffCookedPackagesPath);

						FArchive* TestArchive = IFileManager::Get().CreateFileReader(*TestArchiveFilename);
						FArchive* Saver = new FDiffSerializeArchive(*InOuter->FileName.ToString(), TestArchive);
						Linker = TUniquePtr<FLinkerSave>(new FLinkerSave(InOuter, Saver, bForceByteSwapping));
					}
					else
#endif
						if (bSaveAsync)
						{
							// Allocate the linker with a memory writer, forcing byte swapping if wanted.
							Linker = TUniquePtr<FLinkerSave>(new FLinkerSave(InOuter, bForceByteSwapping, bSaveUnversioned));
						}
						else
						{
							// Allocate the linker, forcing byte swapping if wanted.
							TempFilename = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), *BaseFilename.Left(32));
							Linker = TUniquePtr<FLinkerSave>(new FLinkerSave(InOuter, *TempFilename.GetValue(), bForceByteSwapping, bSaveUnversioned));
						}

#if WITH_TEXT_ARCHIVE_SUPPORT
					if (bTextFormat)
					{
						if (TempFilename.IsSet())
						{
							TextFormatTempFilename = TempFilename.GetValue() + FPackageName::GetTextAssetPackageExtension();
						}
						else
						{
							TextFormatTempFilename = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), *BaseFilename.Left(32)) + FPackageName::GetTextAssetPackageExtension();
						}
						TextFormatArchive = IFileManager::Get().CreateFileWriter(*TextFormatTempFilename.GetValue());
						FJsonArchiveOutputFormatter* OutputFormatter = new FJsonArchiveOutputFormatter(*TextFormatArchive);
						OutputFormatter->SetObjectIndicesMap(&Linker->ObjectIndicesMap);
						Formatter = OutputFormatter;
					}
					else
#endif
					{
						Formatter = new FBinaryArchiveFormatter(*(FArchive*)Linker.Get());
					}
				}

				FStructuredArchive* StructuredArchive = new FStructuredArchive(*Formatter);
				FStructuredArchive::FRecord StructuredArchiveRoot = StructuredArchive->Open().EnterRecord();
				StructuredArchiveRoot.GetUnderlyingArchive().SetSerializeContext(SaveContext);
#if WITH_EDITOR
				if (!!TargetPlatform)
				{
					Linker->SetDebugSerializationFlags(DSF_EnableCookerWarnings | Linker->GetDebugSerializationFlags());
				}
#endif

				if (!(Linker->Summary.PackageFlags & PKG_FilterEditorOnly))
				{
					// The Editor version is used as part of the check to see if a package is too old to use the gather cache, so we always have to add it if we have gathered loc for this asset
					// We need to set the editor custom version before we copy the version container to the summary, otherwise we may end up with corrupt assets
					// because we later do it on the Linker when actually gathering loc data
					ExportTaggerArchive.UsingCustomVersion(FEditorObjectVersion::GUID);
				}

				// Use the custom versions we had previously gleaned from the export tag pass
				Linker->Summary.SetCustomVersionContainer(ExportTaggerArchive.GetCustomVersions());

				Linker->SetPortFlags(ComparisonFlags);
				Linker->SetFilterEditorOnly( FilterEditorOnly );
				Linker->SetCookingTarget(TargetPlatform);

				bool bUseUnversionedProperties = bSaveUnversioned && CanUseUnversionedPropertySerialization(TargetPlatform);
				Linker->SetUseUnversionedPropertySerialization(bUseUnversionedProperties);
				Linker->Saver->SetUseUnversionedPropertySerialization(bUseUnversionedProperties);
				if (bUseUnversionedProperties)
				{
					Linker->Summary.PackageFlags |= PKG_UnversionedProperties;
					Linker->LinkerRoot->SetPackageFlags(PKG_UnversionedProperties);
				}

				// Make sure the package has the same version as the linker
				InOuter->LinkerPackageVersion = Linker->UE4Ver();
				InOuter->LinkerLicenseeVersion = Linker->LicenseeUE4Ver();
				InOuter->LinkerCustomVersion = Linker->GetCustomVersions();

				if (EndSavingIfCancelled())
				{ 
					return ESavePackageResult::Canceled; 
				}
				SlowTask.EnterProgressFrame();
			
				// keep a list of objects that would normally have gone into the dependency map, but since they are from cross-level dependencies, won't be found in the import map
				TArray<UObject*> DependenciesToIgnore;

				// When cooking, strip export objects that 
				//	are both not for client and not for server by default
				//	are for client if target is client only
				//	are for server if target is server only
				if (Linker->IsCooking())
				{
					TArray<UObject*> TagExpObjects;
					GetObjectsWithAnyMarks(TagExpObjects, OBJECTMARK_TagExp);

					const EObjectMark ExcludedObjectMarks = GetExcludedObjectMarksForTargetPlatform(TargetPlatform, Linker->IsCooking());
					if (Linker->IsCooking() && ExcludedObjectMarks != OBJECTMARK_NOMARKS)
					{
						// Make sure that nothing is in the export table that should have been filtered out
						for (UObject* ObjExport : TagExpObjects)
						{
							if (!ensureMsgf(!ObjExport->HasAnyMarks(ExcludedObjectMarks), TEXT("Object %s is marked for export, but has excluded mark!"), *ObjExport->GetPathName()))
							{
								ObjExport->UnMark(OBJECTMARK_TagExp);
							}
						}
						GetObjectsWithAnyMarks(TagExpObjects, OBJECTMARK_TagExp);
					}

					// Exports got filtered out already if they're not for this platform
					if (TagExpObjects.Num() == 0)
					{
						UE_CLOG(!(SaveFlags & SAVE_NoError), LogSavePackage, Verbose, TEXT("No exports found (or all exports are editor-only) for %s. Package will not be saved."), *BaseFilename);
						return ESavePackageResult::ContainsEditorOnlyData;
					}

#if WITH_EDITOR
					if (bIsCooking && TargetPlatform)
					{
						if (const IBlueprintNativeCodeGenCore* Coordinator = IBlueprintNativeCodeGenCore::Get())
						{
							EReplacementResult ReplacmentResult = Coordinator->IsTargetedForReplacement(InOuter, Coordinator->GetNativizationOptionsForPlatform(TargetPlatform));
							if (ReplacmentResult == EReplacementResult::ReplaceCompletely)
							{
								if (IsEventDrivenLoaderEnabledInCookedBuilds() && TargetPlatform)
								{
									// the package isn't actually in the export map, but that is ok, we add it as export anyway for error checking
									EDLCookChecker.AddExport(InOuter);

									for (UObject* ObjExport : TagExpObjects)
									{
										// Register exports, these will exist at runtime because they are compiled in
										EDLCookChecker.AddExport(ObjExport);
									}
								}

								UE_LOG(LogSavePackage, Verbose, TEXT("Package %s contains assets that are being converted to native code."), *InOuter->GetName());
								return ESavePackageResult::ReplaceCompletely;
							}
							else if (ReplacmentResult == EReplacementResult::GenerateStub)
							{
								bRequestStub = true;
							}
						}
					}
#endif
				}

				// Import objects & names.
				TSet<UPackage*> PrestreamPackages;
				{
					SCOPED_SAVETIMER(UPackage_Save_TagImports);
					
					TArray<UObject*> TagExpObjects;
					GetObjectsWithAnyMarks(TagExpObjects, OBJECTMARK_TagExp);
					for(int32 Index = 0; Index < TagExpObjects.Num(); Index++)
					{
						UObject* Obj = TagExpObjects[Index];
						check(Obj->HasAnyMarks(OBJECTMARK_TagExp));

						// Build list.
						FArchiveSaveTagImports ImportTagger(Linker.Get(), NameMapSaver);
						ImportTagger.SetPortFlags(ComparisonFlags);
						ImportTagger.SetFilterEditorOnly(FilterEditorOnly);
						ImportTagger.SetSerializeContext(SaveContext);

						UClass* Class = Obj->GetClass();

						if ( Obj->HasAnyFlags(RF_ClassDefaultObject) )
						{
							Class->SerializeDefaultObject(Obj, ImportTagger);
						}
						else
						{
							Obj->Serialize( ImportTagger );
						}

						ImportTagger << Class;

						// Obj can be saved in package different than their outer, if our outer isn't the package being saved check if we need to tag it as import
						UObject* Outer = Obj->GetOuter();
						if (Outer->GetOutermost() != InOuter)
						{
							ImportTagger << Outer;
						}
						
						UObject* Template = Obj->GetArchetype();
						if (Template)
						{
							// If we're not cooking for the event driven loader, exclude the CDO
							if (Template != Class->GetDefaultObject() || (IsEventDrivenLoaderEnabledInCookedBuilds() && TargetPlatform))
							{
								ImportTagger << Template;
							}

							static struct FDumpChangesSettings
							{
								FString ObjectName;
								FString ArchetypeName;

								FDumpChangesSettings()
								{
									const TCHAR* CommandLine = FCommandLine::Get();

									// Check if we want to dump objects by name
									FString LocalObjectName;
									if (FParse::Value(CommandLine, TEXT("dumpsavestate="), LocalObjectName))
									{
										UE_LOG(LogSavePackage, Warning, TEXT("The -dumpsavestate command line argument is now deprecated. It will soon be removed in a future release."));
										ObjectName = MoveTemp(LocalObjectName);
									}

									// Check if we want to dump objects by their CDO name
									FString LocalArchetypeName;
									if (FParse::Value(CommandLine, TEXT("dumpsavestatebyarchetype="), LocalArchetypeName))
									{
										UE_LOG(LogSavePackage, Warning, TEXT("The -dumpsavestatebyarchetype command line argument is now deprecated. It will soon be removed in a future release."));
										ArchetypeName = MoveTemp(LocalArchetypeName);
									}
								}
							} DumpChangesSettings;

							// Dump objects and their CDO during save to show how those objects are being delta-serialized
							if (Obj->GetFName() == *DumpChangesSettings.ObjectName || Template->GetFName() == *DumpChangesSettings.ArchetypeName)
							{
								auto DumpPropertiesToText = [](UObject* Object)
								{
									TArray<TTuple<FProperty*, FString>> Result;
									for (FProperty* Prop : TFieldRange<FProperty>(Object->GetClass()))
									{
										FString PropState;
										const void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Object);
										Prop->ExportTextItem(PropState, PropAddr, nullptr, Object, PPF_None);

										Result.Emplace(Prop, MoveTemp(PropState));
									}
									return Result;
								};

								TArray<TTuple<FProperty*, FString>> TemplateOutput = DumpPropertiesToText(Template);
								TArray<TTuple<FProperty*, FString>> ObjOutput      = DumpPropertiesToText(Obj);

								FString TemplateText = FString::JoinBy(TemplateOutput, TEXT("\n"), [](const TTuple<FProperty*, FString>& PropValue)
								{
									return FString::Printf(TEXT("  %s: %s"), *PropValue.Get<0>()->GetName(), *PropValue.Get<1>());
								});
								FString ObjText = FString::JoinBy(ObjOutput, TEXT("\n"), [](const TTuple<FProperty*, FString>& PropValue)
								{
									return FString::Printf(TEXT("  %s: %s"), *PropValue.Get<0>()->GetName(), *PropValue.Get<1>());
								});
								UE_LOG(LogSavePackage, Warning, TEXT("---\nArchetype: %s\n%s\nObject: %s\n%s\n---"), *Template->GetFullName(), *TemplateText, *Obj->GetFullName(), *ObjText);
							}
						}

						if (IsEventDrivenLoaderEnabledInCookedBuilds() && TargetPlatform)
						{
							TArray<UObject*> Deps;
							Obj->GetPreloadDependencies(Deps);
							for (UObject* Dep : Deps)
							{
								// We assume nothing in coreuobject ever loads assets in a constructor
								if (Dep && Dep->GetOutermost()->GetFName() != GLongCoreUObjectPackageName)
								{
									// We want to tag these as imports, but not as dependencies
									FArchiveSaveTagImports::FScopeIgnoreDependencies IgnoreDependencies(ImportTagger);
									ImportTagger << Dep;
								}
							}
							static const IConsoleVariable* ProcessPrestreamingRequests = IConsoleManager::Get().FindConsoleVariable(TEXT("s.ProcessPrestreamingRequests"));
							if (ProcessPrestreamingRequests->GetInt())
							{
								Deps.Reset();
								Obj->GetPrestreamPackages(Deps);
								for (UObject* Dep : Deps)
								{
									if (Dep)
									{
										UPackage* Pkg = Dep->GetOutermost();
										if (!Pkg->HasAnyPackageFlags(PKG_CompiledIn) && Obj->HasAnyMarks(OBJECTMARK_TagExp))
										{
											PrestreamPackages.Add(Pkg);
										}
									}
								}
							}
						}

						if( Obj->IsInPackage(GetTransientPackage()) )
						{
							UE_LOG(LogSavePackage, Fatal, TEXT("%s"), *FString::Printf( TEXT("Transient object imported: %s"), *Obj->GetFullName() ) );
						}

						if (Obj->GetClass() != UObjectRedirector::StaticClass())
						{
							DependenciesReferencedByNonRedirectors.Append(ImportTagger.Dependencies);
						}
						ObjectDependencies.Add(Obj, MoveTemp(ImportTagger.Dependencies));
						NativeObjectDependencies.Add(Obj, MoveTemp(ImportTagger.NativeDependencies));
					}
				}
				if (PrestreamPackages.Num())
				{
					TSet<UPackage*> KeptPrestreamPackages;
					for (UPackage* Pkg : PrestreamPackages)
					{
						if (!Pkg->HasAnyMarks(OBJECTMARK_TagImp))
						{
							Pkg->Mark(OBJECTMARK_TagImp);
							KeptPrestreamPackages.Add(Pkg);
						}
					}
					Exchange(PrestreamPackages, KeptPrestreamPackages);
				}

#if WITH_EDITOR
				// Remove TagExp from duplicate objects.
				TMap<UObject*, UObject*> DuplicateRedirects = UnmarkExportTagFromDuplicates();
#endif // WITH_EDITOR

				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				TArray<UObject*> PrivateObjects;
				TArray<UObject*> ObjectsInOtherMaps;
				TArray<UObject*> LevelObjects;

				// Tag the names for all relevant object, classes, and packages.
				{
					SCOPED_SAVETIMER(UPackage_Save_TagNames);
					// Gather the top level objects to validate references
					TArray<UObject*> TopLevelObjects;
					GetObjectsWithPackage(InOuter, TopLevelObjects, false);
					auto IsInAnyTopLevelObject = [&TopLevelObjects](UObject* InObject) -> bool
					{
						for (UObject* TopObject : TopLevelObjects)
						{
							if (InObject->IsInOuter(TopObject))
							{
								return true;
							}
						}
						return false;
					};
					auto AnyTopLevelObjectIsIn = [&TopLevelObjects](UObject* InObject) -> bool
					{
						for (UObject* TopObject : TopLevelObjects)
						{
							if (TopObject->IsInOuter(InObject))
							{
								return true;
							}
						}
						return false;
					};
					auto AnyTopLevelObjectHasSameOutermostObject = [&TopLevelObjects](UObject* InObject) -> bool
					{
						UObject* Outermost = InObject->GetOutermostObject();
						for (UObject* TopObject : TopLevelObjects)
						{
							if (TopObject->GetOutermostObject() == Outermost)
							{
								return true;
							}
						}
						return false;

					};

					TArray<UObject*> TagExpImpObjects;
					GetObjectsWithAnyMarks(TagExpImpObjects, EObjectMark(OBJECTMARK_TagExp|OBJECTMARK_TagImp));
					for(int32 Index = 0; Index < TagExpImpObjects.Num(); Index++)
					{
						UObject* Obj = TagExpImpObjects[Index];
						check(Obj->HasAnyMarks(EObjectMark(OBJECTMARK_TagExp|OBJECTMARK_TagImp)));

						NameMapSaver.MarkNameAsReferenced(Obj->GetFName());
#if WITH_EDITOR
						AddReplacementsNames(NameMapSaver, Obj, TargetPlatform);
#endif //WITH_EDITOR
						if( Obj->GetOuter() )
						{
							NameMapSaver.MarkNameAsReferenced(Obj->GetOuter()->GetFName());
						}

						if( Obj->HasAnyMarks(OBJECTMARK_TagImp) )
						{
							// Make sure the package name of an import is referenced as it might be different than its outer
							UPackage* ObjPackage = Obj->GetPackage();
							check(ObjPackage);
							NameMapSaver.MarkNameAsReferenced(ObjPackage->GetFName());

							NameMapSaver.MarkNameAsReferenced(Obj->GetClass()->GetFName());
							check(Obj->GetClass()->GetOuter());
							NameMapSaver.MarkNameAsReferenced(Obj->GetClass()->GetOuter()->GetFName());
						
							// if a private object was marked by the cooker, it will be in memory on load, and will be found. helps with some objects
							// from a package in a package being moved into Startup_int.xxx, but not all
							// Imagine this:
							// Package P:
							//   - A (private)
							//   - B (public, references A)
							//   - C (public, references A)
							// Map M:
							//   - MObj (references B)
							// Startup Package S:
							//   - SObj (references C)
							// When Startup is cooked, it will pull in C and A. When M is cooked, it will pull in B, but not A, because
							// A was already marked by the cooker. M.xxx now has a private import to A, which is normally illegal, hence
							// the OBJECTMARK_MarkedByCooker check below
							if (PrestreamPackages.Contains(ObjPackage))
							{
								NameMapSaver.MarkNameAsReferenced(PrestreamPackageClassName);
								// These are not errors
								UE_LOG(LogSavePackage, Display, TEXT("Prestreaming package %s "), *ObjPackage->GetPathName()); //-V595
								continue;
							}

							// if this import shares a outer with top level object of this package then the reference is acceptable if we aren't cooking
							if (!bIsCooking && (IsInAnyTopLevelObject(Obj) || AnyTopLevelObjectIsIn(Obj) || AnyTopLevelObjectHasSameOutermostObject(Obj)))
							{
								continue;
							}

							if( !Obj->HasAnyFlags(RF_Public) && !Obj->HasAnyFlags(RF_Transient))
							{
								if (!IsEventDrivenLoaderEnabledInCookedBuilds() || !TargetPlatform || !ObjPackage->HasAnyPackageFlags(PKG_CompiledIn))
								{
									PrivateObjects.Add(Obj);
								}
							}

							// See whether the object we are referencing is in another map package.
							if(ObjPackage->ContainsMap())
							{
								if ( ObjPackage != Obj && Obj->GetFName() != NAME_PersistentLevel && Obj->GetClass()->GetFName() != WorldClassName )
								{
									ObjectsInOtherMaps.Add(Obj);

									if ( DependenciesReferencedByNonRedirectors.Contains(Obj) )
									{
										UE_LOG(LogSavePackage, Warning, TEXT( "Obj in another map: %s"), *Obj->GetFullName() );
									}
								}
								else
								{
									LevelObjects.Add(Obj);
								}
							}
						}
					}
				}

				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				if ( LevelObjects.Num() > 0 && ObjectsInOtherMaps.Num() == 0 )
				{
					ObjectsInOtherMaps = LevelObjects;
				}

				// It is allowed for redirectors to reference objects in other maps.
				// Form the list of objects that erroneously reference another map.
				TArray<UObject*> IllegalObjectsInOtherMaps;
				for ( auto ObjIt = ObjectsInOtherMaps.CreateConstIterator(); ObjIt; ++ObjIt )
				{
					if ( DependenciesReferencedByNonRedirectors.Contains(*ObjIt) )
					{
						IllegalObjectsInOtherMaps.Add(*ObjIt);
					}
				}

				// The graph is linked to objects in a different map package!
				if (IllegalObjectsInOtherMaps.Num() )
				{
					UObject* MostLikelyCulprit = nullptr;
					const FProperty* PropertyRef = nullptr;

					// construct a string containing up to the first 5 objects problem objects
					FString ObjectNames;
					int32 MaxNamesToDisplay = 5;
					bool DisplayIsLimited = true;

					if (IllegalObjectsInOtherMaps.Num() < MaxNamesToDisplay)
					{
						MaxNamesToDisplay = IllegalObjectsInOtherMaps.Num();
						DisplayIsLimited = false;
					}

					for (int32 Idx = 0; Idx < MaxNamesToDisplay; Idx++)
					{
						ObjectNames += IllegalObjectsInOtherMaps[Idx]->GetName() + TEXT("\n");
					}
					
					// if there are more than 5 items we indicated this by adding "..." at the end of the list
					if (DisplayIsLimited)
					{
						ObjectNames += TEXT("...\n");
					}

					Args.Empty();
					Args.Add( TEXT("FileName"), FText::FromString( Filename ) );
					Args.Add( TEXT("ObjectNames"), FText::FromString( ObjectNames ) );
					const FText Message = FText::Format( NSLOCTEXT("Core", "LinkedToObjectsInOtherMap_FindCulpritQ", "Can't save {FileName}: Graph is linked to object(s) in external map.\nExternal Object(s):\n{ObjectNames}  \nTry to find the chain of references to that object (may take some time)?"), Args );

					FString CulpritString = TEXT( "Unknown" );
					bool bFindCulprit = IsRunningCommandlet() || (FMessageDialog::Open(EAppMsgType::YesNo, Message) == EAppReturnType::Yes);
					if (bFindCulprit)
					{
						FindMostLikelyCulprit(IllegalObjectsInOtherMaps, MostLikelyCulprit, PropertyRef);
						if (MostLikelyCulprit != nullptr && PropertyRef != nullptr)
						{
							CulpritString = FString::Printf(TEXT("%s (%s)"), *MostLikelyCulprit->GetFullName(), *PropertyRef->GetName());
						}
						else if (MostLikelyCulprit != nullptr)
						{
							CulpritString = FString::Printf(TEXT("%s (Unknown property)"), *MostLikelyCulprit->GetFullName());
						}
					}

					FString ErrorMessage = FString::Printf(TEXT("Can't save %s: Graph is linked to object %s in external map"), Filename, *CulpritString);
					if (!(SaveFlags & SAVE_NoError))
					{
						Error->Logf(ELogVerbosity::Warning, TEXT("%s"), *ErrorMessage);
					}
					else
					{
						UE_LOG(LogSavePackage, Error, TEXT("%s"), *ErrorMessage);
					}
					return ESavePackageResult::Error;
				}

				// The graph is linked to private objects!
				if (PrivateObjects.Num())
				{
					UObject* MostLikelyCulprit = nullptr;
					const FProperty* PropertyRef = nullptr;
					
					// construct a string containing up to the first 5 objects problem objects
					FString ObjectNames;
					int32 MaxNamesToDisplay = 5;
					bool DisplayIsLimited = true;

					if (PrivateObjects.Num() < MaxNamesToDisplay)
					{
						MaxNamesToDisplay = PrivateObjects.Num();
						DisplayIsLimited = false;
					}

					for (int32 Idx = 0; Idx < MaxNamesToDisplay; Idx++)
					{
						ObjectNames += PrivateObjects[Idx]->GetName() + TEXT("\n");
					}

					// if there are more than 5 items we indicated this by adding "..." at the end of the list
					if (DisplayIsLimited)
					{
						ObjectNames += TEXT("...\n");
					}

					Args.Empty();
					Args.Add( TEXT("FileName"), FText::FromString( Filename ) );
					Args.Add( TEXT("ObjectNames"), FText::FromString( ObjectNames ) );
					const FText Message = FText::Format( NSLOCTEXT("Core", "LinkedToPrivateObjectsInOtherPackage_FindCulpritQ", "Can't save {FileName}: Graph is linked to private object(s) in an external package.\nExternal Object(s):\n{ObjectNames}  \nTry to find the chain of references to that object (may take some time)?"), Args );

					FString CulpritString = TEXT( "Unknown" );
					if (FMessageDialog::Open(EAppMsgType::YesNo, Message) == EAppReturnType::Yes)
					{
						FindMostLikelyCulprit(PrivateObjects, MostLikelyCulprit, PropertyRef);
						CulpritString = FString::Printf(TEXT("%s (%s)"),
							(MostLikelyCulprit != nullptr) ? *MostLikelyCulprit->GetFullName() : TEXT("(unknown culprit)"),
							(PropertyRef != nullptr) ? *PropertyRef->GetName() : TEXT("unknown property ref"));
					}

					if (!(SaveFlags & SAVE_NoError))
					{
						Error->Logf(ELogVerbosity::Warning, TEXT("Can't save %s: Graph is linked to external private object %s"), Filename, *CulpritString);
					}
					return ESavePackageResult::Error;
				}

				// Write fixed-length file summary to overwrite later.
				if( Conform )
				{
					// Conform to previous generation of file.
					UE_LOG(LogSavePackage, Log,  TEXT("Conformal save, relative to: %s, Generation %i"), *Conform->Filename, Conform->Summary.Generations.Num()+1 );
					Linker->Summary.Guid = Conform->Summary.Guid;
#if WITH_EDITORONLY_DATA
					Linker->Summary.PersistentGuid = Conform->Summary.PersistentGuid;					
#endif
					Linker->Summary.Generations = Conform->Summary.Generations;
				}
				else if (SaveFlags & SAVE_KeepGUID)
				{
					// First generation file, keep existing GUID
					Linker->Summary.Guid = InOuter->Guid;
#if WITH_EDITORONLY_DATA
					Linker->Summary.PersistentGuid = InOuter->PersistentGuid;
#endif
					Linker->Summary.Generations = TArray<FGenerationInfo>();
				}
				else
				{
					// First generation file.
					Linker->Summary.Guid = FGuid::NewGuid();
#if WITH_EDITORONLY_DATA
					Linker->Summary.PersistentGuid = InOuter->PersistentGuid;
#endif
					Linker->Summary.Generations = TArray<FGenerationInfo>();

					// make sure the UPackage's copy of the GUID is up to date
					InOuter->Guid = Linker->Summary.Guid;
				}
				new(Linker->Summary.Generations)FGenerationInfo(0, 0);

				{
#if WITH_EDITOR
					FArchiveStackTraceIgnoreScope IgnoreSummaryDiffsScope(DiffSettings.bIgnoreHeaderDiffs);
#endif // WITH_EDITOR
					if (!bTextFormat)
					{
						StructuredArchiveRoot.GetUnderlyingArchive() << Linker->Summary;
					}
				}
				int32 OffsetAfterPackageFileSummary = Linker->Tell();
		
				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();


#if WITH_EDITOR
				if ( GOutputCookingWarnings )
				{
					// check the name list for uniqueobjectnamefor cooking
					static FNameEntryId NAME_UniqueObjectNameForCooking = FName("UniqueObjectNameForCooking").GetComparisonIndex();
					if (NameMapSaver.NameExists(NAME_UniqueObjectNameForCooking))
					{
						UE_LOG(LogSavePackage, Warning, TEXT("Saving object into cooked package %s which was created at cook time"), Filename);
					}
				}
#endif

				// Build NameMap.
				Linker->Summary.NameOffset = Linker->Tell();
				{
					SCOPED_SAVETIMER(UPackage_Save_BuildNameMap);
#if WITH_EDITOR
					FArchive::FScopeSetDebugSerializationFlags S(*Linker, DSF_IgnoreDiff, true);
					FArchiveStackTraceIgnoreScope IgnoreSummaryDiffsScope(DiffSettings.bIgnoreHeaderDiffs);
#endif
					NameMapSaver.UpdateLinker(*Linker.Get(), Conform, bTextFormat ? nullptr : Linker->Saver);
				}
				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				FStructuredArchive::FStream Stream = StructuredArchiveRoot.EnterStream(SA_FIELD_NAME(TEXT("GatherableTextData")));
				Linker->Summary.GatherableTextDataOffset = 0;
				Linker->Summary.GatherableTextDataCount = 0;
				if (!(Linker->Summary.PackageFlags & PKG_FilterEditorOnly))
				{
					SCOPED_SAVETIMER(UPackage_Save_WriteGatherableTextData);

					// The Editor version is used as part of the check to see if a package is too old to use the gather cache, so we always have to add it if we have gathered loc for this asset
					// Note that using custom version here only works because we already added it to the export tagger before the package summary was serialized
					Linker->UsingCustomVersion(FEditorObjectVersion::GUID);

					bool bCanCacheGatheredText = false;
					
					// Gathers from the given package
					EPropertyLocalizationGathererResultFlags GatherableTextResultFlags = EPropertyLocalizationGathererResultFlags::Empty;
					FPropertyLocalizationDataGatherer(Linker->GatherableTextDataMap, InOuter, GatherableTextResultFlags);

					// We can only cache packages that:
					//	1) Don't contain script data, as script data is very volatile and can only be safely gathered after it's been compiled (which happens automatically on asset load).
					//	2) Don't contain text keyed with an incorrect package localization ID, as these keys will be changed later during save.
					bCanCacheGatheredText = !EnumHasAnyFlags(GatherableTextResultFlags, EPropertyLocalizationGathererResultFlags::HasScript | EPropertyLocalizationGathererResultFlags::HasTextWithInvalidPackageLocalizationID);

					if (bCanCacheGatheredText)
					{
						Linker->Summary.GatherableTextDataOffset = Linker->Tell();

						// Save gatherable text data.
						Linker->Summary.GatherableTextDataCount = Linker->GatherableTextDataMap.Num();
						for (FGatherableTextData& GatherableTextData : Linker->GatherableTextDataMap)
						{
							Stream.EnterElement() << GatherableTextData;
						}
					}
				}

				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				// Build ImportMap.
				{
					SCOPED_SAVETIMER(UPackage_Save_BuildImportMap);

					TArray<UObject*> TagImpObjects;

					const EObjectMark ExcludedObjectMarks = GetExcludedObjectMarksForTargetPlatform(TargetPlatform, Linker->IsCooking());
					GetObjectsWithAnyMarks(TagImpObjects, OBJECTMARK_TagImp);

					if (Linker->IsCooking() && ExcludedObjectMarks != OBJECTMARK_NOMARKS)
					{
						// Make sure that nothing is in the import table that should have been filtered out
						for (UObject* ObjImport : TagImpObjects)
						{
							if (!ensureMsgf(!ObjImport->HasAnyMarks(ExcludedObjectMarks), TEXT("Object %s is marked for import, but has excluded mark!"), *ObjImport->GetPathName()))
							{
								ObjImport->UnMark(OBJECTMARK_TagImp);
							}
						}
						GetObjectsWithAnyMarks(TagImpObjects, OBJECTMARK_TagImp);
					}

					for(int32 Index = 0; Index < TagImpObjects.Num(); Index++)
					{
						UObject* Obj = TagImpObjects[Index];
						check(Obj->HasAnyMarks(OBJECTMARK_TagImp));
						UClass* ObjClass = Obj->GetClass();
#if WITH_EDITOR
						FName ReplacedName = NAME_None;
						if (bIsCooking && TargetPlatform)
						{
							if (const IBlueprintNativeCodeGenCore* Coordinator = IBlueprintNativeCodeGenCore::Get())
							{
								const FCompilerNativizationOptions& NativizationOptions = Coordinator->GetNativizationOptionsForPlatform(TargetPlatform);
								if (UClass* ReplacedClass = Coordinator->FindReplacedClassForObject(Obj, NativizationOptions))
								{
									ObjClass = ReplacedClass;
								}
								if (UObject* ReplacedOuter = Coordinator->FindReplacedNameAndOuter(Obj, /*out*/ReplacedName, NativizationOptions))
								{
									ReplacedImportOuters.Add(Obj, ReplacedOuter);
								}
							}
						}

						bool bExcludePackageFromCook = FCoreUObjectDelegates::ShouldCookPackageForPlatform.IsBound() ? !FCoreUObjectDelegates::ShouldCookPackageForPlatform.Execute(Obj->GetOutermost(), TargetPlatform) : false;			
						if (bExcludePackageFromCook)
						{
							continue;
						}
#endif //WITH_EDITOR
						FObjectImport* LocObjectImport = new(Linker->ImportMap)FObjectImport(Obj, ObjClass);

						if (PrestreamPackages.Contains((UPackage*)Obj))
						{
							LocObjectImport->ClassName = PrestreamPackageClassName;
						}
#if WITH_EDITOR
						if (ReplacedName != NAME_None)
						{
							LocObjectImport->ObjectName = ReplacedName;
						}
#endif //WITH_EDITOR
					}
				}


				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				// sort and conform imports
				FObjectImportSortHelper ImportSortHelper;
				{
					SCOPED_SAVETIMER(UPackage_Save_SortImports);
					ImportSortHelper.SortImports(Linker.Get(), Conform);
					Linker->Summary.ImportCount = Linker->ImportMap.Num();
				}

				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				
				// Build ExportMap.
				{
					SCOPED_SAVETIMER(UPackage_Save_BuildExportMap);

					TArray<UObject*> TagExpObjects;
					GetObjectsWithAnyMarks(TagExpObjects, OBJECTMARK_TagExp);
					for(int32 Index = 0; Index < TagExpObjects.Num(); Index++)
					{
						UObject* Obj = TagExpObjects[Index];
						check(Obj->HasAnyMarks(OBJECTMARK_TagExp));
						new( Linker->ExportMap )FObjectExport( Obj );
					}
				}

#if WITH_EDITOR
				if (GOutputCookingWarnings)
				{
					// check the name list for uniqueobjectnamefor cooking
					static FName NAME_UniqueObjectNameForCooking(TEXT("UniqueObjectNameForCooking"));

					for (const auto& Export : Linker->ExportMap)
					{
						const auto& NameInUse = Export.ObjectName;
						if (NameInUse.GetComparisonIndex() == NAME_UniqueObjectNameForCooking.GetComparisonIndex())
						{
							UObject* Outer = Export.Object->GetOuter();
							UE_LOG(LogSavePackage, Warning, TEXT(" into cooked package %s which was created at cook time, Object Name %s, Full Path %s, Class %s, Outer %s, Outer class %s"), Filename, *NameInUse.ToString(), *Export.Object->GetFullName(), *Export.Object->GetClass()->GetName(), Outer ? *Outer->GetName() : TEXT("None"), Outer ? *Outer->GetClass()->GetName() : TEXT("None") );
						}
					}
				}
#endif

				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				// Sort exports alphabetically and conform the export table (if necessary)
				FObjectExportSortHelper ExportSortHelper;
				{
					SCOPED_SAVETIMER(UPackage_Save_SortExports);
					ExportSortHelper.SortExports(Linker.Get(), Conform);
				}
				
				// Sort exports for seek-free loading.
				if (Linker->IsCooking() || Conform)
				{
					SCOPED_SAVETIMER(UPackage_Save_SortExportsForSeekFree);
					COOK_STAT(FScopedDurationTimer SaveTimer(SavePackageStats::SortExportsSeekfreeInnerTimeSec));
					FObjectExportSeekFreeSorter SeekFreeSorter;
					SeekFreeSorter.SortExports( Linker.Get(), Conform );
				}

				Linker->Summary.ExportCount = Linker->ExportMap.Num();

				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				// Pre-size depends map.
				Linker->DependsMap.AddZeroed( Linker->ExportMap.Num() );

				// track import and export object linker index
				TMap<UObject*, FPackageIndex> ImportToIndexMap;
				TMap<UObject*, FPackageIndex> ExportToIndexMap;
				for (int32 ImpIndex = 0; ImpIndex < Linker->ImportMap.Num(); ImpIndex++)
				{
					ImportToIndexMap.Add(Linker->ImportMap[ImpIndex].XObject, FPackageIndex::FromImport(ImpIndex));
				}

				for (int32 ExpIndex = 0; ExpIndex < Linker->ExportMap.Num(); ExpIndex++)
				{
					ExportToIndexMap.Add(Linker->ExportMap[ExpIndex].Object, FPackageIndex::FromExport(ExpIndex));
				}

				// go back over the (now sorted) exports and fill out the DependsMap
				{
					SCOPED_SAVETIMER(UPackage_Save_BuildExportDependsMap);
					for (int32 ExpIndex = 0; ExpIndex < Linker->ExportMap.Num(); ExpIndex++)
					{
						UObject* Object = Linker->ExportMap[ExpIndex].Object;
						// sorting while conforming can create NULL export map entries, so skip those depends map
						if (Object == nullptr)
						{
							UE_LOG(LogSavePackage, Warning, TEXT("Object is missing for an export, unable to save dependency map. Most likely this is caused my conforming against a package that is missing this object. See log for more info"));
							if (!(SaveFlags & SAVE_NoError))
							{
								Error->Logf(ELogVerbosity::Warning, TEXT("%s"), *FText::Format(NSLOCTEXT("Core", "SavePackageObjectIsMissingExport", "Object is missing for an export, unable to save dependency map for asset '{0}'. Most likely this is caused my conforming against a asset that is missing this object. See log for more info"), FText::FromString(FString(Filename))).ToString());
							}
							continue;
						}

						// add a dependency map entry also
						TArray<FPackageIndex>& DependIndices = Linker->DependsMap[ExpIndex];
						// find all the objects needed by this export
						TArray<UObject*>* SrcDepends = ObjectDependencies.Find(Object);
						checkf(SrcDepends, TEXT("Couldn't find dependency map for %s"), *Object->GetFullName());

						// go through each object and...
						DependIndices.Reserve(SrcDepends->Num());
						for (int32 DependIndex = 0; DependIndex < SrcDepends->Num(); DependIndex++)
						{
							UObject* DependentObject = (*SrcDepends)[DependIndex];

							FPackageIndex DependencyIndex;

							// if the dependency is in the same package, we need to save an index into our ExportMap
							if (DependentObject->GetOutermost() == Linker->LinkerRoot)
							{
								// ... find the associated ExportIndex
								DependencyIndex = ExportToIndexMap.FindRef(DependentObject);
							}
							// otherwise we need to save an index into the ImportMap
							else
							{
								// ... find the associated ImportIndex
								DependencyIndex = ImportToIndexMap.FindRef(DependentObject);
							}

#if WITH_EDITOR
							// If we still didn't find index, maybe it was a duplicate export which got removed.
							// Check if we have a redirect to original.
							if (DependencyIndex.IsNull() && DuplicateRedirects.Contains(DependentObject))
							{
								UObject** const RedirectObj = DuplicateRedirects.Find(DependentObject);
								if (RedirectObj)
								{
									DependencyIndex = ExportToIndexMap.FindRef(*RedirectObj);
								}
							}
#endif
							// if we didn't find it (FindRef returns 0 on failure, which is good in this case), then we are in trouble, something went wrong somewhere
							checkf(!DependencyIndex.IsNull(), TEXT("Failed to find dependency index for %s (%s)"), *DependentObject->GetFullName(), *Object->GetFullName());

							// add the import as an import for this export
							DependIndices.Add(DependencyIndex);
						}
					}
				}


				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				// Set linker reverse mappings.
				// also set netplay required data for any UPackages in the export map
				for( int32 i=0; i<Linker->ExportMap.Num(); i++ )
				{
					UObject* Object = Linker->ExportMap[i].Object;
					if( Object )
					{
						Linker->ObjectIndicesMap.Add(Object, FPackageIndex::FromExport(i));

						UPackage* Package = dynamic_cast<UPackage*>(Object);
						if (Package != nullptr)
						{
							Linker->ExportMap[i].PackageFlags = Package->GetPackageFlags();
							if (!Package->HasAnyPackageFlags(PKG_ServerSideOnly))
							{
								Linker->ExportMap[i].PackageGuid = Package->GetGuid();
							}
						}
					}
				}

				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				// If this is a map package, make sure there is a world or level in the export map.
				if ( InOuter->ContainsMap() )
				{
					bool bContainsMap = false;
					for( int32 i=0; i<Linker->ExportMap.Num(); i++ )
					{
						UObject* Object = Linker->ExportMap[i].Object;
						
						// Consider redirectors to world/levels as map packages too.
						if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Object))
						{
							Object = Redirector->DestinationObject;
						}
							
						if ( Object )
						{
							const FString ExportClassName = Object->GetClass()->GetName();
							if( ExportClassName == TEXT("World") || ExportClassName == TEXT("Level") )
							{
								bContainsMap = true;
								break;
							}
						}
					}
					if (!bContainsMap)
					{
						ensureMsgf(false, TEXT("Attempting to save a map package '%s' that does not contain a map object."), *InOuter->GetName());
						UE_LOG(LogSavePackage, Error, TEXT("Attempting to save a map package '%s' that does not contain a map object."), *InOuter->GetName());

						if (!(SaveFlags & SAVE_NoError))
						{
							Error->Logf(ELogVerbosity::Warning, TEXT("%s"), *FText::Format( NSLOCTEXT( "Core", "SavePackageNoMap", "Attempting to save a map asset '{0}' that does not contain a map object" ), FText::FromString( FString( Filename ) ) ).ToString() );
						}
						Success = false;
					}
				}


				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				for( int32 i=0; i<Linker->ImportMap.Num(); i++ )
				{
					UObject* Object = Linker->ImportMap[i].XObject;
					if( Object != nullptr )
					{
						const FPackageIndex PackageIndex = FPackageIndex::FromImport(i);
						//ensure(!Linker->ObjectIndicesMap.Contains(Object)); // this ensure will fail
						Linker->ObjectIndicesMap.Add(Object, PackageIndex);
					}
					else
					{
						// the only reason we should ever have a NULL object in the import is when this package is being conformed against another package, and the new
						// version of the package no longer has this import
						checkf(Conform != NULL, TEXT("NULL XObject for import %i - Object: %s Class: %s"), i, *Linker->ImportMap[i].ObjectName.ToString(), *Linker->ImportMap[i].ClassName.ToString());
					}
				}
				if (IsEventDrivenLoaderEnabledInCookedBuilds() && TargetPlatform)
				{
					EDLCookChecker.AddExport(InOuter); // the package isn't actually in the export map, but that is ok, we add it as export anyway for error checking
					for (int32 i = 0; i < Linker->ImportMap.Num(); i++)
					{
						UObject* Object = Linker->ImportMap[i].XObject;
						if (Object != nullptr)
						{
							EDLCookChecker.AddImport(Object, InOuter);
						}
					}
				}

				// Convert the searchable names map from UObject to packageindex
				for (const TPair<const UObject *, TArray<FName>>& SearchableNamePair : Linker->SearchableNamesObjectMap)
				{
					const FPackageIndex PackageIndex = Linker->MapObject(SearchableNamePair.Key);

					// This should always be in the imports already
					if (ensure(!PackageIndex.IsNull()))
					{
						Linker->SearchableNamesMap.FindOrAdd(PackageIndex) = SearchableNamePair.Value;
					}
				}
				// Clear temporary map
				Linker->SearchableNamesObjectMap.Empty();

				SlowTask.EnterProgressFrame();

				// Find components referenced by exports.

				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				// Save dummy import map, overwritten later.
				if (!bTextFormat)
				{
					SCOPED_SAVETIMER(UPackage_Save_WriteDummyImportMap);
#if WITH_EDITOR
					FArchiveStackTraceIgnoreScope IgnoreSummaryDiffsScope(DiffSettings.bIgnoreHeaderDiffs);
#endif // WITH_EDITOR
					Linker->Summary.ImportOffset = Linker->Tell();
					for (int32 i = 0; i < Linker->ImportMap.Num(); i++)
					{
						FObjectImport& Import = Linker->ImportMap[i];
						StructuredArchiveRoot.GetUnderlyingArchive() << Import;
					}
				}
				int32 OffsetAfterImportMap = Linker->Tell();


				if (EndSavingIfCancelled())
				{
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				// Save dummy export map, overwritten later.
				if (!bTextFormat)
				{
					SCOPED_SAVETIMER(UPackage_Save_WriteDummyExportMap);
#if WITH_EDITOR
					FArchiveStackTraceIgnoreScope IgnoreSummaryDiffsScope(DiffSettings.bIgnoreHeaderDiffs);
#endif // WITH_EDITOR
					Linker->Summary.ExportOffset = Linker->Tell();
					for (int32 i = 0; i < Linker->ExportMap.Num(); i++)
					{
						FObjectExport& Export = Linker->ExportMap[i];
						*Linker << Export;
					}
				}
				int32 OffsetAfterExportMap = Linker->Tell();


				if (EndSavingIfCancelled())
				{
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				if (!bTextFormat)
				{
					SCOPED_SAVETIMER(UPackage_Save_WriteDependsMap);

					FStructuredArchive::FStream DependsStream = StructuredArchiveRoot.EnterStream(SA_FIELD_NAME(TEXT("DependsMap")));
					if (Linker->IsCooking())
					{
#if WITH_EDITOR
						FArchiveStackTraceIgnoreScope IgnoreSummaryDiffsScope(DiffSettings.bIgnoreHeaderDiffs);
#endif // WITH_EDITOR
						//@todo optimization, this should just be stripped entirely from cooked packages
						TArray<FPackageIndex> Depends; // empty array
						Linker->Summary.DependsOffset = Linker->Tell();
						for (int32 i = 0; i < Linker->ExportMap.Num(); i++)
						{
							DependsStream.EnterElement() << Depends;
						}
					}
					else
					{
						// save depends map (no need for later patching)
						check(Linker->DependsMap.Num() == Linker->ExportMap.Num());
						Linker->Summary.DependsOffset = Linker->Tell();
						for (int32 i = 0; i < Linker->ExportMap.Num(); i++)
						{
							TArray<FPackageIndex>& Depends = Linker->DependsMap[i];
							DependsStream.EnterElement() << Depends;
						}
					}
				}


				if (EndSavingIfCancelled())
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				// Only save string asset and searchable name map if saving for editor
				if (!(Linker->Summary.PackageFlags & PKG_FilterEditorOnly))
				{
					SCOPED_SAVETIMER(UPackage_Save_SaveSoftPackagesAndSearchableNames);

					// Save soft package references
					Linker->Summary.SoftPackageReferencesOffset = Linker->Tell();
					Linker->Summary.SoftPackageReferencesCount = Linker->SoftPackageReferenceList.Num();
					if (!bTextFormat)
					{
#if WITH_EDITOR
						FArchive::FScopeSetDebugSerializationFlags S(*Linker, DSF_IgnoreDiff, true);
#endif
						FStructuredArchive::FStream SoftReferenceStream = StructuredArchiveRoot.EnterStream(SA_FIELD_NAME(TEXT("SoftReferences")));
						for (FName& SoftPackageName : Linker->SoftPackageReferenceList)
						{
							SoftReferenceStream.EnterElement() << SoftPackageName;
						}

						// Save searchable names map
						Linker->Summary.SearchableNamesOffset = Linker->Tell();
						Linker->SerializeSearchableNamesMap(StructuredArchiveRoot.EnterField(SA_FIELD_NAME(TEXT("SearchableNames"))));
					}
				}
				else
				{
					Linker->Summary.SoftPackageReferencesCount = 0;
					Linker->Summary.SoftPackageReferencesOffset = 0;
					Linker->Summary.SearchableNamesOffset = 0;
				}

				{
#if WITH_EDITOR
					FArchiveStackTraceIgnoreScope IgnoreSummaryDiffsScope(DiffSettings.bIgnoreHeaderDiffs);
#endif // WITH_EDITOR

					// Save thumbnails
					{
						SCOPED_SAVETIMER(UPackage_Save_SaveThumbnails);
						SaveThumbnails(InOuter, Linker.Get(), StructuredArchiveRoot.EnterField(SA_FIELD_NAME(TEXT("Thumbnails"))));
					}

					if (!bTextFormat)
					{	
						// Save asset registry data so the editor can search for information about assets in this package
						SCOPED_SAVETIMER(UPackage_Save_SaveAssetRegistryData);
						SaveAssetRegistryData(InOuter, Linker.Get(), StructuredArchiveRoot.EnterField(SA_FIELD_NAME(TEXT("AssetRegistry"))));
					}

					// Save level information used by World browser
					{
						SCOPED_SAVETIMER(UPackage_Save_WorldLevelData);
						SaveWorldLevelInfo(InOuter, Linker.Get(), StructuredArchiveRoot);
					}
				}

				// Map export indices
				{
					SCOPED_SAVETIMER(UPackage_Save_MapExportIndices);

					for (int32 i = 0; i < Linker->ExportMap.Num(); i++)
					{
						FObjectExport& Export = Linker->ExportMap[i];
						if (Export.Object)
						{
							// Set class index.
							// If this is *exactly* a UClass, store null instead; for anything else, including UClass-derived classes, map it
							UClass* ObjClass = Export.Object->GetClass();
							if (ObjClass != UClass::StaticClass())
							{
								Export.ClassIndex = Linker->MapObject(ObjClass);
								checkf(!Export.ClassIndex.IsNull(), TEXT("Export %s class is not mapped when saving %s"), *Export.Object->GetFullName(), *Linker->LinkerRoot->GetName());
							}
							else
							{
								Export.ClassIndex = FPackageIndex();
							}

							if (IsEventDrivenLoaderEnabledInCookedBuilds() && TargetPlatform)
							{
								UObject* Archetype = Export.Object->GetArchetype();
								check(Archetype);
								check(Archetype->IsA(Export.Object->HasAnyFlags(RF_ClassDefaultObject) ? ObjClass->GetSuperClass() : ObjClass));
								Export.TemplateIndex = Linker->MapObject(Archetype);
								UE_CLOG(Export.TemplateIndex.IsNull(), LogSavePackage, Fatal, TEXT("%s was an archetype of %s but returned a null index mapping the object."), *Archetype->GetFullName(), *Export.Object->GetFullName());
								check(!Export.TemplateIndex.IsNull());
							}

							// Set the parent index, if this export represents a UStruct-derived object
							if (UStruct* Struct = dynamic_cast<UStruct*>(Export.Object))
							{
								if (Struct->GetSuperStruct() != nullptr)
								{
									Export.SuperIndex = Linker->MapObject(Struct->GetSuperStruct());
									checkf(!Export.SuperIndex.IsNull(),
										TEXT("Export Struct (%s) of type (%s) inheriting from (%s) of type (%s) has not mapped super struct."),
										*GetPathNameSafe(Struct),
										*(Struct->GetClass()->GetName()),
										*GetPathNameSafe(Struct->GetSuperStruct()),
										*(Struct->GetSuperStruct()->GetClass()->GetName())
									);
								}
								else
								{
									Export.SuperIndex = FPackageIndex();
								}
							}
							else
							{
								Export.SuperIndex = FPackageIndex();
							}

							// Set FPackageIndex for this export's Outer. If the export's Outer
							// is the UPackage corresponding to this package's LinkerRoot, the
							if (Export.Object->GetOuter() != InOuter)
							{
								check(Export.Object->GetOuter());
								Export.OuterIndex = Linker->MapObject(Export.Object->GetOuter());

								// The outer of an object is now allowed to be an export hence, the following asserts do not stand anymore:
								//checkf(Export.Object->GetOuter()->IsInPackage(InOuter),
								//	TEXT("Export Object (%s) Outer (%s) mismatch."),
								//	*(Export.Object->GetPathName()),
								//	*(Export.Object->GetOuter()->GetPathName()));
								//checkf(!Export.OuterIndex.IsImport(),
								//	TEXT("Export Object (%s) Outer (%s) is an Import."),
								//	*(Export.Object->GetPathName()),
								//	*(Export.Object->GetOuter()->GetPathName()));

								if (Linker->IsCooking() && IsEventDrivenLoaderEnabledInCookedBuilds())
								{
									// Only packages are allowed to have no outer
									ensureMsgf(Export.OuterIndex != FPackageIndex() || Export.Object->IsA(UPackage::StaticClass()), TEXT("Export %s has no valid outer when cooking!"), *Export.Object->GetPathName());
								}
							}
							else
							{
								// this export's Outer is the LinkerRoot for this package
								Export.OuterIndex = FPackageIndex();
							}
						}
					}
				}

				Linker->Summary.PreloadDependencyOffset = Linker->Tell();
				Linker->Summary.PreloadDependencyCount = -1;

				if (Linker->IsCooking() && IsEventDrivenLoaderEnabledInCookedBuilds())
				{
#if WITH_EDITOR
					FArchiveStackTraceIgnoreScope IgnoreSummaryDiffsScope(DiffSettings.bIgnoreHeaderDiffs);
#endif // WITH_EDITOR

					const EObjectMark ExcludedObjectMarks = GetExcludedObjectMarksForTargetPlatform(Linker->CookingTarget(), Linker->IsCooking());
					Linker->Summary.PreloadDependencyCount = 0;

					auto IncludeObjectAsDependency = [&Linker, ExcludedObjectMarks](int32 CallSite, TSet<FPackageIndex>& AddTo, UObject* ToTest, UObject* ForObj, bool bMandatory, bool bOnlyIfInLinkerTable)
					{
						// Skip transient, editor only, and excluded client/server objects
						if (ToTest)
						{
							UPackage* Outermost = ToTest->GetOutermost();
							check(Outermost);
							if (Outermost->GetFName() == GLongCoreUObjectPackageName)
							{
								return; // We assume nothing in coreuobject ever loads assets in a constructor
							}
							FPackageIndex Index = Linker->MapObject(ToTest);
							if (Index.IsNull() && bOnlyIfInLinkerTable)
							{
								return;
							}
							if (!Index.IsNull() && (ToTest->HasAllFlags(RF_Transient) && !ToTest->IsNative()))
							{
								UE_LOG(LogSavePackage, Warning, TEXT("A dependency '%s' of '%s' is in the linker table, but is transient. We will keep the dependency anyway (%d)."), *ToTest->GetFullName(), *ForObj->GetFullName(), CallSite);
							}
							if (!Index.IsNull() && ToTest->IsPendingKill())
							{
								UE_LOG(LogSavePackage, Warning, TEXT("A dependency '%s' of '%s' is in the linker table, but is pending kill. We will keep the dependency anyway (%d)."), *ToTest->GetFullName(), *ForObj->GetFullName(), CallSite);
							}
							bool bNotFiltered = (ExcludedObjectMarks == OBJECTMARK_NOMARKS || !ToTest->HasAnyMarks(ExcludedObjectMarks)) && (!(Linker->Summary.PackageFlags & PKG_FilterEditorOnly) || !IsEditorOnlyObject(ToTest, false, true));
							if (bMandatory && !bNotFiltered)
							{
								UE_LOG(LogSavePackage, Warning, TEXT("A dependency '%s' of '%s' was filtered, but is mandatory. This indicates a problem with editor only stripping. We will keep the dependency anyway (%d)."), *ToTest->GetFullName(), *ForObj->GetFullName(), CallSite);
								bNotFiltered = true;
							}
							if (bNotFiltered)
							{
								if (!Index.IsNull())
								{
									AddTo.Add(Index);
									return;
								}
								else if (!ToTest->HasAnyFlags(RF_Transient))
								{
									UE_CLOG(Outermost->HasAnyPackageFlags(PKG_CompiledIn), LogSavePackage, Verbose, TEXT("A compiled in dependency '%s' of '%s' was not actually in the linker tables and so will be ignored (%d)."), *ToTest->GetFullName(), *ForObj->GetFullName(), CallSite);
									UE_CLOG(!Outermost->HasAnyPackageFlags(PKG_CompiledIn), LogSavePackage, Fatal, TEXT("A dependency '%s' of '%s' was not actually in the linker tables and so will be ignored (%d)."), *ToTest->GetFullName(), *ForObj->GetFullName(), CallSite);
								}
							}
							check(!bMandatory);
						}
					};

					auto IncludeIndexAsDependency = [&Linker](TSet<FPackageIndex>& AddTo, FPackageIndex Dep)
					{
						if (!Dep.IsNull())
						{
							UObject* ToTest = Dep.IsExport() ? Linker->Exp(Dep).Object : Linker->Imp(Dep).XObject;
							if (ToTest)
							{
								UPackage* Outermost = ToTest->GetOutermost();
								if (Outermost && Outermost->GetFName() != GLongCoreUObjectPackageName) // We assume nothing in coreuobject ever loads assets in a constructor
								{
									AddTo.Add(Dep);
								}
							}
						}
					};

					FStructuredArchive::FStream DepedenciesStream = StructuredArchiveRoot.EnterStream(SA_FIELD_NAME(TEXT("PreloadDependencies")));
					TArray<UObject*> Subobjects;
					TArray<UObject*> Deps;
					TSet<FPackageIndex> SerializationBeforeCreateDependencies;
					TSet<FPackageIndex> SerializationBeforeSerializationDependencies;
					TSet<FPackageIndex> CreateBeforeSerializationDependencies;
					TSet<FPackageIndex> CreateBeforeCreateDependencies;
					for (int32 i = 0; i < Linker->ExportMap.Num(); i++)
					{
						FObjectExport& Export = Linker->ExportMap[i];
						if (Export.Object)
						{
							EDLCookChecker.AddExport(Export.Object);
							{
								SerializationBeforeCreateDependencies.Reset();
								IncludeIndexAsDependency(SerializationBeforeCreateDependencies, Export.ClassIndex);
								UObject* CDO = Export.Object->GetArchetype();
								IncludeObjectAsDependency(1, SerializationBeforeCreateDependencies, CDO, Export.Object, true, false);
								Subobjects.Reset();
								GetObjectsWithOuter(CDO, Subobjects);
								for (UObject* SubObj : Subobjects)
								{
									// Only include subobject archetypes
									if (SubObj->HasAnyFlags(RF_DefaultSubObject | RF_ArchetypeObject))
									{
										while (SubObj->HasAnyFlags(RF_Transient)) // transient components are stripped by the ICH, so find the one it will really use at runtime
										{
											UObject* SubObjArch = SubObj->GetArchetype();
											if (SubObjArch->GetClass()->HasAnyClassFlags(CLASS_Native | CLASS_Intrinsic))
											{
												break;
											}
											SubObj = SubObjArch;
										}
										if (!SubObj->IsPendingKill())
										{
											IncludeObjectAsDependency(2, SerializationBeforeCreateDependencies, SubObj, Export.Object, false, false);
										}
									}
								}
							}
							{
								SerializationBeforeSerializationDependencies.Reset();
								Deps.Reset();
								Export.Object->GetPreloadDependencies(Deps);

								for (UObject* Obj : Deps)
								{
									IncludeObjectAsDependency(3, SerializationBeforeSerializationDependencies, Obj, Export.Object, false, true);
								}
								if (Export.Object->HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject))
								{
									UObject *Outer = Export.Object->GetOuter();
									if (!Outer->IsA(UPackage::StaticClass()))
									{
										IncludeObjectAsDependency(4, SerializationBeforeSerializationDependencies, Outer, Export.Object, true, false);
									}
								}
								if (Export.Object->IsA(UClass::StaticClass()))
								{
									// we need to load archetypes of our subobjects before we load the class
									UObject* CDO = CastChecked<UClass>(Export.Object)->GetDefaultObject();
									Subobjects.Reset();
									GetObjectsWithOuter(CDO, Subobjects);
									for (UObject* SubObj : Subobjects)
									{
										// Only include subobject archetypes
										if (SubObj->HasAnyFlags(RF_DefaultSubObject | RF_ArchetypeObject))
										{
											SubObj = SubObj->GetArchetype();
											while (SubObj->HasAnyFlags(RF_Transient)) // transient components are stripped by the ICH, so find the one it will really use at runtime
											{
												UObject* SubObjArch = SubObj->GetArchetype();
												if (SubObjArch->GetClass()->HasAnyClassFlags(CLASS_Native | CLASS_Intrinsic))
												{
													break;
												}
												SubObj = SubObjArch;
											}
											if (!SubObj->IsPendingKill())
											{
												IncludeObjectAsDependency(5, SerializationBeforeSerializationDependencies, SubObj, Export.Object, false, false);
											}
										}
									}
								}
							}

							{
								CreateBeforeSerializationDependencies.Reset();
								UClass* Class = Cast<UClass>(Export.Object);
								UObject* ClassCDO = Class ? Class->GetDefaultObject() : nullptr;
								{
									TArray<FPackageIndex>& Depends = Linker->DependsMap[i];
									for (FPackageIndex Dep : Depends)
									{
										UObject* ToTest = Dep.IsExport() ? Linker->Exp(Dep).Object : Linker->Imp(Dep).XObject;
										if (ToTest != ClassCDO)
										{
											IncludeIndexAsDependency(CreateBeforeSerializationDependencies, Dep);
										}
									}
								}
								{
									TArray<UObject*>& NativeDeps = NativeObjectDependencies[Export.Object];
									for (UObject* ToTest : NativeDeps)
									{
										if (ToTest != ClassCDO)
										{
											IncludeObjectAsDependency(6, CreateBeforeSerializationDependencies, ToTest, Export.Object, false, true);
										}
									}
								}
							}

							{
								CreateBeforeCreateDependencies.Reset();
								IncludeIndexAsDependency(CreateBeforeCreateDependencies, Export.OuterIndex);
								IncludeIndexAsDependency(CreateBeforeCreateDependencies, Export.SuperIndex);
							}

							auto AddArcForDepChecking = [&Linker, &Export, &EDLCookChecker](bool bExportIsSerialize, FPackageIndex Dep, bool bDepIsSerialize)
							{
								check(Export.Object);
								check(!Dep.IsNull());
								UObject* DepObject = Dep.IsExport() ? Linker->Exp(Dep).Object : Linker->Imp(Dep).XObject;
								check(DepObject);

								Linker->DepListForErrorChecking.Add(Dep);

								EDLCookChecker.AddArc(DepObject, bDepIsSerialize, Export.Object, bExportIsSerialize);
							};

							for (FPackageIndex Index : SerializationBeforeSerializationDependencies)
							{
								if (SerializationBeforeCreateDependencies.Contains(Index))
								{
									continue; // if the other thing must be serialized before we create, then this is a redundant dep
								}
								if (Export.FirstExportDependency == -1)
								{
									Export.FirstExportDependency = Linker->Summary.PreloadDependencyCount;
									check(Export.SerializationBeforeSerializationDependencies == 0 && Export.CreateBeforeSerializationDependencies == 0 && Export.SerializationBeforeCreateDependencies == 0 && Export.CreateBeforeCreateDependencies == 0);
								}
								Linker->Summary.PreloadDependencyCount++;
								Export.SerializationBeforeSerializationDependencies++;
								DepedenciesStream.EnterElement() << Index;
								AddArcForDepChecking(true, Index, true);
							}
							for (FPackageIndex Index : CreateBeforeSerializationDependencies)
							{
								if (SerializationBeforeCreateDependencies.Contains(Index))
								{
									continue; // if the other thing must be serialized before we create, then this is a redundant dep
								}
								if (SerializationBeforeSerializationDependencies.Contains(Index))
								{
									continue; // if the other thing must be serialized before we serialize, then this is a redundant dep
								}
								if (CreateBeforeCreateDependencies.Contains(Index))
								{
									continue; // if the other thing must be created before we are created, then this is a redundant dep
								}
								if (Export.FirstExportDependency == -1)
								{
									Export.FirstExportDependency = Linker->Summary.PreloadDependencyCount;
									check(Export.SerializationBeforeSerializationDependencies == 0 && Export.CreateBeforeSerializationDependencies == 0 && Export.SerializationBeforeCreateDependencies == 0 && Export.CreateBeforeCreateDependencies == 0);
								}
								Linker->Summary.PreloadDependencyCount++;
								Export.CreateBeforeSerializationDependencies++;
								DepedenciesStream.EnterElement() << Index;
								AddArcForDepChecking(true, Index, false);
							}
							for (FPackageIndex Index : SerializationBeforeCreateDependencies)
							{
								if (Export.FirstExportDependency == -1)
								{
									Export.FirstExportDependency = Linker->Summary.PreloadDependencyCount;
									check(Export.SerializationBeforeSerializationDependencies == 0 && Export.CreateBeforeSerializationDependencies == 0 && Export.SerializationBeforeCreateDependencies == 0 && Export.CreateBeforeCreateDependencies == 0);
								}
								Linker->Summary.PreloadDependencyCount++;
								Export.SerializationBeforeCreateDependencies++;
								DepedenciesStream.EnterElement() << Index;
								AddArcForDepChecking(false, Index, true);
							}
							for (FPackageIndex Index : CreateBeforeCreateDependencies)
							{
								if (Export.FirstExportDependency == -1)
								{
									Export.FirstExportDependency = Linker->Summary.PreloadDependencyCount;
									check(Export.SerializationBeforeSerializationDependencies == 0 && Export.CreateBeforeSerializationDependencies == 0 && Export.SerializationBeforeCreateDependencies == 0 && Export.CreateBeforeCreateDependencies == 0);
								}
								Linker->Summary.PreloadDependencyCount++;
								Export.CreateBeforeCreateDependencies++;
								DepedenciesStream.EnterElement() << Index;
								AddArcForDepChecking(false, Index, false);
							}
						}
					}
					UE_LOG(LogSavePackage, Verbose, TEXT("Saved %d dependencies for %d exports."), Linker->Summary.PreloadDependencyCount, Linker->ExportMap.Num());
				}

				
				Linker->Summary.TotalHeaderSize	= Linker->Tell();

				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame(1, NSLOCTEXT("Core", "ProcessingExports", "ProcessingExports..."));

				// look for this package in the list of packages to generate script SHA for 
				TArray<uint8>* ScriptSHABytes = FLinkerSave::PackagesToScriptSHAMap.Find(*FPaths::GetBaseFilename(Filename));

				// if we want to generate the SHA key, start tracking script writes
				if (ScriptSHABytes)
				{
					Linker->StartScriptSHAGeneration();
				}

#if WITH_EDITOR
				TArray<FLargeMemoryWriter, TInlineAllocator<4>> AdditionalFilesFromExports;
#endif
				{
					COOK_STAT(FScopedDurationTimer SaveTimer(SavePackageStats::SerializeExportsTimeSec));
					SCOPED_SAVETIMER(UPackage_Save_SaveExports);
#if WITH_EDITOR
					FArchive::FScopeSetDebugSerializationFlags S(*Linker, DSF_IgnoreDiff, true);
#endif
					FScopedSlowTask ExportScope(Linker->ExportMap.Num());

					FStructuredArchive::FRecord ExportsRecord = StructuredArchiveRoot.EnterRecord(SA_FIELD_NAME(TEXT("Exports")));

					// Save exports.
					int32 LastExportSaveStep = 0;
					for( int32 i=0; i<Linker->ExportMap.Num(); i++ )
					{
						if ( EndSavingIfCancelled() )
						{ 
							return ESavePackageResult::Canceled;
						}
						ExportScope.EnterProgressFrame();

						FObjectExport& Export = Linker->ExportMap[i];
						if (Export.Object)
						{
							// Save the object data.
							Export.SerialOffset = Linker->Tell();
							Linker->CurrentlySavingExport = FPackageIndex::FromExport(i);
							// UE_LOG(LogSavePackage, Log, TEXT("export %s for %s"), *Export.Object->GetFullName(), *Linker->CookingTarget()->PlatformName());

							FString ObjectName = Export.Object->GetPathName(InOuter);
							FStructuredArchive::FSlot ExportSlot = ExportsRecord.EnterField(SA_FIELD_NAME(*ObjectName));

							if (bTextFormat)
							{
								FObjectTextExport ObjectTextExport(Export, InOuter);
								ExportSlot << ObjectTextExport;
							}

#if WITH_EDITOR
							bool bSupportsText = UClass::IsSafeToSerializeToStructuredArchives(Export.Object->GetClass());
#else
							bool bSupportsText = false;
#endif

							if ( Export.Object->HasAnyFlags(RF_ClassDefaultObject) )
							{
								if (bSupportsText)
								{
									Export.Object->GetClass()->SerializeDefaultObject(Export.Object, ExportSlot);
								}
								else
								{
									FArchiveUObjectFromStructuredArchive Adapter(ExportSlot);
									Export.Object->GetClass()->SerializeDefaultObject(Export.Object, Adapter.GetArchive());
									Adapter.Close();
								}
							}
							else
							{
								TGuardValue<UObject*> GuardSerializedObject(SaveContext->SerializedObject, Export.Object);

								if (bSupportsText)
								{
									FStructuredArchive::FRecord ExportRecord = ExportSlot.EnterRecord();
									Export.Object->Serialize(ExportRecord);
								}
								else
								{
									FArchiveUObjectFromStructuredArchive Adapter(ExportSlot);
									Export.Object->Serialize(Adapter.GetArchive());
									Adapter.Close();
								}

#if WITH_EDITOR
								if (bIsCooking)
								{
									Export.Object->CookAdditionalFiles(Filename, TargetPlatform,
										[&AdditionalFilesFromExports](const TCHAR* Filename, void* Data, int64 Size)
									{
										FLargeMemoryWriter& Writer = AdditionalFilesFromExports.Emplace_GetRef(0, true, Filename);
										Writer.Serialize(Data, Size);
									});
								}
#endif
							}
							Linker->CurrentlySavingExport = FPackageIndex();
							Export.SerialSize = Linker->Tell() - Export.SerialOffset;

							// Mark object as having been saved.
							Export.Object->Mark(OBJECTMARK_Saved);
						}
					}
				}

				// if we want to generate the SHA key, get it out now that the package has finished saving
				if (ScriptSHABytes && Linker->ContainsCode())
				{
					// make space for the 20 byte key
					ScriptSHABytes->Empty(20);
					ScriptSHABytes->AddUninitialized(20);

					// retrieve it
					Linker->GetScriptSHAKey(ScriptSHABytes->GetData());
				}

				
				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}

				SlowTask.EnterProgressFrame(1, NSLOCTEXT("Core", "SerializingBulkData", "Serializing bulk data"));

				SaveBulkData(Linker.Get(), InOuter, Filename, TargetPlatform, SavePackageContext, bTextFormat, bDiffing,
							 bComputeHash, AsyncWriteAndHashSequence, TotalPackageSizeUncompressed );

#if WITH_EDITOR
				if (bIsCooking && AdditionalFilesFromExports.Num() > 0)
				{
					const bool bWriteFileToDisk = !bDiffing;
					for (FLargeMemoryWriter& Writer : AdditionalFilesFromExports)
					{
						const int64 Size = Writer.TotalSize();
						TotalPackageSizeUncompressed += Size;

						if (bComputeHash || bWriteFileToDisk)
						{
							FLargeMemoryPtr DataPtr(Writer.ReleaseOwnership());

							EAsyncWriteOptions WriteOptions(EAsyncWriteOptions::None);
							if (bComputeHash)
							{
								WriteOptions |= EAsyncWriteOptions::ComputeHash;
							}
							if (bWriteFileToDisk)
							{
								WriteOptions |= EAsyncWriteOptions::WriteFileToDisk;
							}
							AsyncWriteFile(AsyncWriteAndHashSequence, MoveTemp(DataPtr), Size, *Writer.GetArchiveName(), WriteOptions);
						}
					}
					AdditionalFilesFromExports.Empty();
				}
#endif

			
				// write the package post tag
				if (!bTextFormat)
				{
					uint32 Tag = PACKAGE_FILE_TAG;
					StructuredArchiveRoot.GetUnderlyingArchive() << Tag;
				}

				// We capture the package size before the first seek to work with archives that don't report the file
				// size correctly while the file is still being written to.
				PackageSize = Linker->Tell();

				// Save the import map.
				{
#if WITH_EDITOR
					FArchiveStackTraceIgnoreScope IgnoreSummaryDiffsScope(DiffSettings.bIgnoreHeaderDiffs);
#endif // WITH_EDITOR

					if (!bTextFormat)
					{
						Linker->Seek(Linker->Summary.ImportOffset);

						int32 NumImports = Linker->ImportMap.Num();
						FStructuredArchive::FStream ImportTableStream = StructuredArchiveRoot.EnterStream(SA_FIELD_NAME(TEXT("ImportTable")));

						for (int32 i = 0; i < Linker->ImportMap.Num(); i++)
						{
							FObjectImport& Import = Linker->ImportMap[i];
							if (Import.XObject)
							{
								// Set the package index.
								if (Import.XObject->GetOuter())
								{
									// if an import outer is an export and that import doesn't have a specific package set then, there's an error
									const bool bWrongImport = Import.XObject->GetOuter()->IsInPackage(InOuter) && Import.XObject->GetExternalPackage() == nullptr;
									if (bWrongImport)
									{
										if (!Import.XObject->HasAllFlags(RF_Transient) || !Import.XObject->IsNative())
										{
											UE_LOG(LogSavePackage, Warning, TEXT("Bad Object=%s"), *Import.XObject->GetFullName());
										}
										else
										{
											// if an object is marked RF_Transient and native, it is either an intrinsic class or
											// a property of an intrinsic class.  Only properties of intrinsic classes will have
											// an Outer that passes the check for "GetOuter()->IsInPackage(InOuter)" (thus ending up in this
											// block of code).  Just verify that the Outer for this property is also marked RF_Transient and Native
											check(Import.XObject->GetOuter()->HasAllFlags(RF_Transient) && Import.XObject->GetOuter()->IsNative());
										}
									}
									check(!bWrongImport || Import.XObject->HasAllFlags(RF_Transient) || Import.XObject->IsNative());
#if WITH_EDITOR
									UObject** ReplacedOuter = ReplacedImportOuters.Find(Import.XObject);
									if (ReplacedOuter && *ReplacedOuter)
									{
										Import.OuterIndex = Linker->MapObject(*ReplacedOuter);
										ensure(Import.OuterIndex != FPackageIndex());
									}
									else
#endif
									{
										Import.OuterIndex = Linker->MapObject(Import.XObject->GetOuter());
									}

									// if the import has a package set, set it up
									if (UPackage* ImportPackage = Import.XObject->GetExternalPackage())
									{
										Import.SetPackageName(ImportPackage->GetFName());
									}

									if (Linker->IsCooking() && IsEventDrivenLoaderEnabledInCookedBuilds())
									{
										// Only package imports are allowed to have no outer
										ensureMsgf(Import.OuterIndex != FPackageIndex() || Import.ClassName == NAME_Package, TEXT("Import %s has no valid outer when cooking!"), *Import.XObject->GetPathName());
									}
								}
							}
							else
							{
								checkf(Conform != nullptr, TEXT("NULL XObject for import %i - Object: %s Class: %s"), i, *Import.ObjectName.ToString(), *Import.ClassName.ToString());
							}

							// Save it.
							ImportTableStream.EnterElement() << Import;
						}
					}
				}

				// Save the export map.
				if (!bTextFormat)
				{
					check( Linker->Tell() == OffsetAfterImportMap );
					Linker->Seek(Linker->Summary.ExportOffset);

					int32 NumExports = Linker->ExportMap.Num();
					FStructuredArchive::FStream ExportTableStream = StructuredArchiveRoot.EnterStream(SA_FIELD_NAME(TEXT("ExportTable")));
					{
#if WITH_EDITOR
						FArchive::FScopeSetDebugSerializationFlags S(*Linker, DSF_IgnoreDiff, true);
						FArchiveStackTraceIgnoreScope IgnoreSummaryDiffsScope(DiffSettings.bIgnoreHeaderDiffs);
#endif
						for (int32 i = 0; i < Linker->ExportMap.Num(); i++)
						{
							FObjectExport& Export = Linker->ExportMap[i];
							ExportTableStream.EnterElement() << Export;
						}
					}

					check( Linker->Tell() == OffsetAfterExportMap );
				}

				if (EndSavingIfCancelled())
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				FFormatNamedArguments NamedArgs;
				NamedArgs.Add( TEXT("CleanFilename"), FText::FromString( CleanFilename ) );
				SlowTask.DefaultMessage = FText::Format( NSLOCTEXT("Core", "Finalizing", "Finalizing: {CleanFilename}..."), NamedArgs );

				//@todo: remove ExportCount and NameCount - no longer used
				Linker->Summary.Generations.Last().ExportCount = Linker->Summary.ExportCount;
				Linker->Summary.Generations.Last().NameCount   = Linker->Summary.NameCount;	

				// create the package source (based on developer or user created)
	#if (UE_BUILD_SHIPPING && WITH_EDITOR)
				Linker->Summary.PackageSource = FMath::Rand() * FMath::Rand();
	#else
				Linker->Summary.PackageSource = FCrc::StrCrc_DEPRECATED(*FPaths::GetBaseFilename(Filename).ToUpper());
	#endif

				// Flag package as requiring localization gather if the archive requires localization gathering.
				Linker->LinkerRoot->ThisRequiresLocalizationGather(Linker->RequiresLocalizationGather());
				
				// Update package flags from package, in case serialization has modified package flags.
				Linker->Summary.PackageFlags = Linker->LinkerRoot->GetPackageFlags() & ~PKG_NewlyCreated;
				
				{
					// Verify that the final serialization pass hasn't added any new custom versions. Otherwise this will result in crashes when loading the package.
					bool bNewCustomVersionsUsed = false;
					for (const FCustomVersion& LinkerCustomVer : Linker->GetCustomVersions().GetAllVersions())
					{
						if (Linker->Summary.GetCustomVersionContainer().GetVersion(LinkerCustomVer.Key) == nullptr)
						{
							UE_LOG(LogSavePackage, Error,
								TEXT("Unexpected custom version \"%s\" found when saving %s. This usually happens when export tagging and final serialization paths differ. Package will not be saved."),
								*LinkerCustomVer.GetFriendlyName().ToString(), *Linker->LinkerRoot->GetName());
							bNewCustomVersionsUsed = true;
						}
					}
					if (bNewCustomVersionsUsed)
					{
						return ESavePackageResult::Error;
					}
				}

				if (!bTextFormat)
				{
					Linker->Seek(0);
				}
				{
#if WITH_EDITOR
					FArchiveStackTraceIgnoreScope IgnoreSummaryDiffsScope(DiffSettings.bIgnoreHeaderDiffs);
#endif // WITH_EDITOR
					StructuredArchiveRoot.EnterField(SA_FIELD_NAME(TEXT("Summary"))) << Linker->Summary;
				}

				if (!bTextFormat)
				{
					check( Linker->Tell() == OffsetAfterPackageFileSummary );
				}

				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				// Destroy archives used for saving, closing file handle.
				if (!bSaveAsync)
				{
					const bool bFileWriterSuccess = Linker->CloseAndDestroySaver();

					delete StructuredArchive;
					delete Formatter;
					delete TextFormatArchive;

					if (!bFileWriterSuccess)
					{
						UE_LOG(LogSavePackage, Error, TEXT("Error writing temp file '%s' for '%s'"),
							TempFilename.IsSet() ? *TempFilename.GetValue() : TEXT("UNKNOWN"), Filename);
						return ESavePackageResult::Error;
					}
				}
				UNCLOCK_CYCLES(Time);
				UE_CLOG(!bDiffing, LogSavePackage, Verbose,  TEXT("Save=%.2fms"), FPlatformTime::ToMilliseconds(Time) );
		
				if ( EndSavingIfCancelled() )
				{ 
					return ESavePackageResult::Canceled;
				}
				SlowTask.EnterProgressFrame();

				if( Success == true )
				{
					{
						// If we're writing to the existing file call ResetLoaders on the Package so that we drop the handle to the file on disk and can write to it
						COOK_STAT(FScopedDurationTimer SaveTimer(SavePackageStats::ResetLoadersTimeSec));
						ResetLoadersForSave(InOuter, Filename);
					}

					// Compress the temporarily file to destination.
					if (bSaveAsync)
					{						
						FString NewPathToSave = NewPath;
#if WITH_EDITOR

						if (SaveFlags & SAVE_DiffCallstack)
						{
							const TCHAR* CutoffString = TEXT("UEditorEngine::Save()");
							FArchiveStackTrace* Writer = (FArchiveStackTrace*)(Linker->Saver);
							TMap<FName, FArchiveDiffStats> PackageDiffStats;
							Writer->CompareWith(*NewPath, IsEventDrivenLoaderEnabledInCookedBuilds() ? Linker->Summary.TotalHeaderSize : 0, CutoffString, DiffSettings.MaxDiffsToLog, PackageDiffStats);
							TotalPackageSizeUncompressed += Writer->TotalSize();

							auto MergeStats = [](TMap<FName, FArchiveDiffStats>& InOut, const TMap<FName, FArchiveDiffStats>& ToMerge)
							{
								for (const TPair<FName, FArchiveDiffStats>& Stat : ToMerge)
								{
									InOut.FindOrAdd(Stat.Key).DiffSize         += Stat.Value.DiffSize;
									InOut.FindOrAdd(Stat.Key).NewFileTotalSize += Stat.Value.NewFileTotalSize;
									InOut.FindOrAdd(Stat.Key).NumDiffs         += Stat.Value.NumDiffs;
								}
							};

							COOK_STAT(SavePackageStats::NumberOfDifferentPackages++);
							COOK_STAT(MergeStats(SavePackageStats::PackageDiffStats, PackageDiffStats));

							if (DiffSettings.bSaveForDiff)
							{
								NewPathToSave = FPaths::Combine(FPaths::GetPath(NewPath), FPaths::GetBaseFilename(NewPath) + TEXT("_ForDiff") + FPaths::GetExtension(NewPath, true));
							}
						}
						else if (SaveFlags & SAVE_DiffOnly)
						{
							FArchiveStackTrace* Writer = (FArchiveStackTrace*)(Linker->Saver);
							FArchiveDiffMap OutDiffMap;
							bDiffOnlyIdentical = Writer->GenerateDiffMap(*NewPath, IsEventDrivenLoaderEnabledInCookedBuilds() ? Linker->Summary.TotalHeaderSize : 0, DiffSettings.MaxDiffsToLog, OutDiffMap);
							TotalPackageSizeUncompressed += Writer->TotalSize();
							if (InOutDiffMap)
							{
								*InOutDiffMap = MoveTemp(OutDiffMap);
							}
						}
						
						if (!(SaveFlags & SAVE_DiffOnly) && (!(SaveFlags & SAVE_DiffCallstack) || DiffSettings.bSaveForDiff))
#endif // WITH_EDITOR
						{
							UE_LOG(LogSavePackage, Verbose, TEXT("Async saving from memory to '%s'"), *NewPathToSave);

							FLargeMemoryWriter* Writer = static_cast<FLargeMemoryWriter*>(Linker->Saver);
							const int64 DataSize = Writer->TotalSize();

							if (!(SaveFlags & SAVE_DiffCallstack))  // Avoid double counting the package size if SAVE_DiffCallstack flag is set and DiffSettings.bSaveForDiff == true.
								TotalPackageSizeUncompressed += DataSize;

							if (IsEventDrivenLoaderEnabledInCookedBuilds() && Linker->IsCooking())
							{
								if (SavePackageContext != nullptr && SavePackageContext->PackageStoreWriter != nullptr)
								{
									FIoBuffer IoBuffer(FIoBuffer::AssumeOwnership, Writer->ReleaseOwnership(), DataSize);

									if (bComputeHash)
									{
										FIoBuffer InnerBuffer(IoBuffer.Data(), IoBuffer.DataSize(), IoBuffer);
										AsyncWriteAndHashSequence.AddWork([InnerBuffer = MoveTemp(InnerBuffer)](FMD5& State)
										{
											State.Update(InnerBuffer.Data(), InnerBuffer.DataSize());
										});
									}

									const int32 HeaderSize = Linker->Summary.TotalHeaderSize;

									FPackageStoreWriter::HeaderInfo HeaderInfo;
									HeaderInfo.PackageName		= InOuter->GetFName();
									HeaderInfo.LooseFilePath	= Filename;

									SavePackageContext->PackageStoreWriter->WriteHeader(HeaderInfo, FIoBuffer(IoBuffer.Data(), HeaderSize, IoBuffer));

									FPackageStoreWriter::ExportsInfo ExportsInfo;
									ExportsInfo.LooseFilePath	= Filename;
									ExportsInfo.PackageName		= InOuter->GetFName();

									const uint8* ExportsData = IoBuffer.Data() + HeaderSize;
									const int32 ExportCount = Linker->ExportMap.Num();

									ExportsInfo.Exports.Reserve(ExportCount);

									for (const FObjectExport& Export : Linker->ExportMap)
									{
										ExportsInfo.Exports.Add(FIoBuffer(IoBuffer.Data() + Export.SerialOffset, Export.SerialSize, IoBuffer));
									}

									SavePackageContext->PackageStoreWriter->WriteExports(ExportsInfo, FIoBuffer(ExportsData, DataSize - HeaderSize, IoBuffer));
								}
								else
								{
									EAsyncWriteOptions WriteOptions(EAsyncWriteOptions::WriteFileToDisk);
									if (bComputeHash)
									{
										WriteOptions |= EAsyncWriteOptions::ComputeHash;
									}
									AsyncWriteFileWithSplitExports(AsyncWriteAndHashSequence, FLargeMemoryPtr(Writer->ReleaseOwnership()), DataSize, Linker->Summary.TotalHeaderSize, *NewPathToSave, WriteOptions);
								}
							}
							else
							{
								EAsyncWriteOptions WriteOptions(EAsyncWriteOptions::WriteFileToDisk);
								if (bComputeHash)
								{
									WriteOptions |= EAsyncWriteOptions::ComputeHash;
								}
								AsyncWriteFile(AsyncWriteAndHashSequence, FLargeMemoryPtr(Writer->ReleaseOwnership()), DataSize, *NewPathToSave, WriteOptions);
							}
						}
						Linker->CloseAndDestroySaver();

						delete StructuredArchive;
						delete Formatter;
						delete TextFormatArchive;
					}
					// Move the temporary file.
					else
					{
						check(TempFilename.IsSet());

						if (bTextFormat)
						{
							check(TextFormatTempFilename.IsSet());
							IFileManager::Get().Delete(*TempFilename.GetValue());
							TempFilename = TextFormatTempFilename;
							TextFormatTempFilename.Reset();
						}

						UE_LOG(LogSavePackage, Log,  TEXT("Moving '%s' to '%s'"), TempFilename.IsSet() ? *TempFilename.GetValue() : TEXT("UNKNOWN"), *NewPath );
						TotalPackageSizeUncompressed += PackageSize;

						Success = IFileManager::Get().Move( *NewPath, *TempFilename.GetValue());
						TempFilename.Reset();

						if (FinalTimeStamp != FDateTime::MinValue())
						{
							IFileManager::Get().SetTimeStamp(*NewPath, FinalTimeStamp);
						}

						if (bComputeHash)
						{
							OutstandingAsyncWrites.Increment();
							AsyncWriteAndHashSequence.AddWork([NewPath](FMD5& State)
							{
								AddFileToHash(NewPath, State);
								OutstandingAsyncWrites.Decrement();
							});
						}
					}

					if( Success == false )
					{
						if (SaveFlags & SAVE_NoError)
						{
							UE_LOG(LogSavePackage, Warning, TEXT("%s"), *FString::Printf( TEXT("Error saving '%s'"), Filename ) );
						}
						else
						{
							UE_LOG(LogSavePackage, Error, TEXT("%s"), *FString::Printf( TEXT("Error saving '%s'"), Filename ) );
							Error->Logf(ELogVerbosity::Warning, TEXT("%s"), *FText::Format( NSLOCTEXT( "Core", "SaveWarning", "Error saving '{0}'" ), FText::FromString( FString( Filename ) ) ).ToString() );
						}
					}
					else
					{
						// Mark exports and the package as RF_Loaded after they've been serialized
						// This is to ensue that newly created packages are properly marked as loaded (since they now exist on disk and 
						// in memory in the exact same state).
						for (auto& Export : Linker->ExportMap)
						{
							if (Export.Object)
							{
								Export.Object->SetFlags(RF_WasLoaded|RF_LoadCompleted);
							}
						}
						if (Linker->LinkerRoot)
						{
							// And finally set the flag on the package itself.
							Linker->LinkerRoot->SetFlags(RF_WasLoaded|RF_LoadCompleted);
						}

						// Clear dirty flag if desired
						if (!(SaveFlags & SAVE_KeepDirty))
						{
							InOuter->SetDirtyFlag(false);
						}
						
						// Update package FileSize value
						InOuter->FileSize = PackageSize;

						// Warn about long package names, which may be bad for consoles with limited filename lengths.
						if( bWarnOfLongFilename == true )
						{
							int32 MaxFilenameLength = FPlatformMisc::GetMaxPathLength();

							// If the name is of the form "_LOC_xxx.ext", remove the loc data before the length check
							FString CleanBaseFilename = BaseFilename;
							if( CleanBaseFilename.Find( "_LOC_" ) == BaseFilename.Len() - 8 )
							{
								CleanBaseFilename = BaseFilename.LeftChop( 8 );
							}

							if( CleanBaseFilename.Len() > MaxFilenameLength )
							{
								if (SaveFlags & SAVE_NoError)
								{
									UE_LOG(LogSavePackage, Warning, TEXT("%s"), *FString::Printf( TEXT("Filename is too long (%d characters); this may interfere with cooking for consoles. Unreal filenames should be no longer than %s characters. Filename value: %s"), BaseFilename.Len(), MaxFilenameLength, *BaseFilename ) );
								}
								else
								{
									FFormatNamedArguments Arguments;
									Arguments.Add(TEXT("FileName"), FText::FromString( BaseFilename ));
									Arguments.Add(TEXT("MaxLength"), FText::AsNumber( MaxFilenameLength ));
									Error->Logf(ELogVerbosity::Warning, TEXT("%s"), *FText::Format( NSLOCTEXT( "Core", "Error_FilenameIsTooLongForCooking", "Filename '{FileName}' is too long; this may interfere with cooking for consoles. Unreal filenames should be no longer than {MaxLength} characters." ), Arguments ).ToString() );
								}
							}
						}
					}
				}
				COOK_STAT(SavePackageStats::MBWritten += ((double)TotalPackageSizeUncompressed) / 1024.0 / 1024.0);

				SlowTask.EnterProgressFrame();
			}

			// Route PostSaveRoot to allow e.g. the world to detach components for the persistent level that were
			// attached in PreSaveRoot.
			// If we are saving concurrently, this should be called after UPackage::Save.
			// Also if we're trying to diff the package and gathering callstacks, Presave has already been done
			if( Base && !bSavingConcurrent && !(SaveFlags & SAVE_DiffCallstack) )
			{
				Base->PostSaveRoot( bCleanupIsRequired );
			}

			SlowTask.EnterProgressFrame();
			
#if WITH_EDITOR
			if ( !bSavingConcurrent )
			{
				for ( int CachedObjectIndex = 0; CachedObjectIndex < CachedObjects.Num(); ++CachedObjectIndex )
				{
					CachedObjects[CachedObjectIndex]->ClearCachedCookedPlatformData(TargetPlatform);
				}
			}
#endif

		}
		if( Success == true )
		{
			// Package has been save, so unmark NewlyCreated flag.
			InOuter->ClearPackageFlags(PKG_NewlyCreated);

			// send a message that the package was saved
			UPackage::PackageSavedEvent.Broadcast(Filename, InOuter);
		}

		// We're done!
		SlowTask.EnterProgressFrame();

		UE_CLOG(!bDiffing, LogSavePackage, Verbose, TEXT("Finished SavePackage %s"), Filename);

		if (Success)
		{
			// if the save was successful, update the internal package filename path if we aren't currently cooking
#if WITH_EDITOR
			if (TargetPlatform == nullptr && bIsValidLongPackageName)
			{
				InOuter->FileName = *PackageFilename;
			}
#endif

			auto HashCompletionFunc = [](FMD5& State)
			{
				FMD5Hash OutputHash;
				OutputHash.Set(State);
				return OutputHash;
			};

			if (bRequestStub)
			{
				return FSavePackageResultStruct(ESavePackageResult::GenerateStub, TotalPackageSizeUncompressed, AsyncWriteAndHashSequence.Finalize(EAsyncExecution::TaskGraph, MoveTemp(HashCompletionFunc)));
			}
			else
			{
				return FSavePackageResultStruct(bDiffOnlyIdentical ? ESavePackageResult::Success : ESavePackageResult::DifferentContent, TotalPackageSizeUncompressed, AsyncWriteAndHashSequence.Finalize(EAsyncExecution::TaskGraph, MoveTemp(HashCompletionFunc)));
			}
		}
		else
		{
			if (bRequestStub)
			{
				UE_LOG(LogSavePackage, Warning, TEXT("C++ stub requested, but package failed to save, may cause compile errors: %s"), Filename);
			}
			return ESavePackageResult::Error;
		}
	}
	else
	{
		return ESavePackageResult::Error;
	}
}

bool UPackage::SavePackage(UPackage* InOuter, UObject* Base, EObjectFlags TopLevelFlags, const TCHAR* Filename,
	FOutputDevice* Error, FLinkerNull* Conform, bool bForceByteSwapping, bool bWarnOfLongFilename, uint32 SaveFlags,
	const class ITargetPlatform* TargetPlatform, const FDateTime&  FinalTimeStamp, bool bSlowTask)
{
	const FSavePackageResultStruct Result = Save(InOuter, Base, TopLevelFlags, Filename, Error, Conform, bForceByteSwapping,
		bWarnOfLongFilename, SaveFlags, TargetPlatform, FinalTimeStamp, bSlowTask);
	return Result == ESavePackageResult::Success;
}

/**
 * Static: Saves thumbnail data for the specified package outer and linker
 *
 * @param	InOuter							the outer to use for the new package
 * @param	Linker							linker we're currently saving with
 * @param	Slot							structed archive slot we are saving too (temporary)
 */
static void SaveThumbnails(UPackage* InOuter, FLinkerSave* Linker, FStructuredArchive::FSlot Slot)
{
	FStructuredArchive::FRecord Record = Slot.EnterRecord();

	Linker->Summary.ThumbnailTableOffset = 0;

#if WITH_EDITORONLY_DATA
	// Do we have any thumbnails to save?
	if( !(Linker->Summary.PackageFlags & PKG_FilterEditorOnly) && InOuter->HasThumbnailMap() )
	{
		const FThumbnailMap& PackageThumbnailMap = InOuter->GetThumbnailMap();


		// Figure out which objects have thumbnails.  Note that we only want to save thumbnails
		// for objects that are actually in the export map.  This is so that we avoid saving out
		// thumbnails that were cached for deleted objects and such.
		TArray< FObjectFullNameAndThumbnail > ObjectsWithThumbnails;
		for( int32 i=0; i<Linker->ExportMap.Num(); i++ )
		{
			FObjectExport& Export = Linker->ExportMap[i];
			if( Export.Object )
			{
				const FName ObjectFullName( *Export.Object->GetFullName() );
				const FObjectThumbnail* ObjectThumbnail = PackageThumbnailMap.Find( ObjectFullName );
		
				// if we didn't find the object via full name, try again with ??? as the class name, to support having
				// loaded old packages without going through the editor (ie cooking old packages)
				if (ObjectThumbnail == nullptr)
				{
					// can't overwrite ObjectFullName, so that we add it properly to the map
					FName OldPackageStyleObjectFullName = FName(*FString::Printf(TEXT("??? %s"), *Export.Object->GetPathName()));
					ObjectThumbnail = PackageThumbnailMap.Find(OldPackageStyleObjectFullName);
				}
				if( ObjectThumbnail != nullptr )
				{
					// IMPORTANT: We save all thumbnails here, even if they are a shared (empty) thumbnail!
					// Empty thumbnails let us know that an asset is in a package without having to
					// make a linker for it.
					ObjectsWithThumbnails.Add( FObjectFullNameAndThumbnail( ObjectFullName, ObjectThumbnail ) );
				}
			}
		}

		// preserve thumbnail rendered for the level
		const FObjectThumbnail* ObjectThumbnail = PackageThumbnailMap.Find(FName(*InOuter->GetFullName()));
		if (ObjectThumbnail != nullptr)
		{
			ObjectsWithThumbnails.Add( FObjectFullNameAndThumbnail(FName(*InOuter->GetFullName()), ObjectThumbnail ) );
		}
		
		// Do we have any thumbnails?  If so, we'll save them out along with a table of contents
		if( ObjectsWithThumbnails.Num() > 0 )
		{
			// Save out the image data for the thumbnails
			FStructuredArchive::FStream ThumbnailStream = Record.EnterStream(SA_FIELD_NAME(TEXT("Thumbnails")));

			for( int32 CurObjectIndex = 0; CurObjectIndex < ObjectsWithThumbnails.Num(); ++CurObjectIndex )
			{
				FObjectFullNameAndThumbnail& CurObjectThumb = ObjectsWithThumbnails[ CurObjectIndex ];

				// Store the file offset to this thumbnail
				CurObjectThumb.FileOffset = Linker->Tell();

				// Serialize the thumbnail!
				FObjectThumbnail* SerializableThumbnail = const_cast< FObjectThumbnail* >( CurObjectThumb.ObjectThumbnail );
				SerializableThumbnail->Serialize(ThumbnailStream.EnterElement());
			}


			// Store the thumbnail table of contents
			{
				Linker->Summary.ThumbnailTableOffset = Linker->Tell();

				// Save number of thumbnails
				int32 ThumbnailCount = ObjectsWithThumbnails.Num();
				FStructuredArchive::FArray IndexArray = Record.EnterField(SA_FIELD_NAME(TEXT("Index"))).EnterArray(ThumbnailCount);

				// Store a list of object names along with the offset in the file where the thumbnail is stored
				for( int32 CurObjectIndex = 0; CurObjectIndex < ObjectsWithThumbnails.Num(); ++CurObjectIndex )
				{
					const FObjectFullNameAndThumbnail& CurObjectThumb = ObjectsWithThumbnails[ CurObjectIndex ];

					// Object name
					const FString ObjectFullName = CurObjectThumb.ObjectFullName.ToString();

					// Break the full name into it's class and path name parts
					const int32 FirstSpaceIndex = ObjectFullName.Find( TEXT( " " ) );
					check( FirstSpaceIndex != INDEX_NONE && FirstSpaceIndex > 0 );
					FString ObjectClassName = ObjectFullName.Left( FirstSpaceIndex );
					const FString ObjectPath = ObjectFullName.Mid( FirstSpaceIndex + 1 );

					// Remove the package name from the object path since that will be implicit based
					// on the package file name
					FString ObjectPathWithoutPackageName = ObjectPath.Mid( ObjectPath.Find( TEXT( "." ) ) + 1 );

					// File offset for the thumbnail (already saved out.)
					int32 FileOffset = CurObjectThumb.FileOffset;

					IndexArray.EnterElement().EnterRecord()
						<< SA_VALUE(TEXT("ObjectClassName"), ObjectClassName)
						<< SA_VALUE(TEXT("ObjectPathWithoutPackageName"), ObjectPathWithoutPackageName)
						<< SA_VALUE(TEXT("FileOffset"), FileOffset);
				}
			}
		}
	}

	// if content browser isn't enabled, clear the thumbnail map so we're not using additional memory for nothing
	if ( !GIsEditor || IsRunningCommandlet() )
	{
		InOuter->ThumbnailMap.Reset();
	}
#endif
}

static void SaveAssetRegistryData(UPackage* InOuter, FLinkerSave* Linker, FStructuredArchive::FSlot Slot)
{
	// Make a copy of the tag map
	TArray<UObject*> AssetObjects;

	if( !(Linker->Summary.PackageFlags & PKG_FilterEditorOnly) )
	{
		// Find any exports which are not in the tag map
		for( int32 i=0; i<Linker->ExportMap.Num(); i++ )
		{
			FObjectExport& Export = Linker->ExportMap[i];
			if( Export.Object && Export.Object->IsAsset() )
			{
				AssetObjects.Add(Export.Object);
			}
		}
	}

	// Store the asset registry offset in the file
	Linker->Summary.AssetRegistryDataOffset = Linker->Tell();

	// Save the number of objects in the tag map
	int32 ObjectCount = AssetObjects.Num();
	FStructuredArchive::FArray AssetArray = Slot.EnterArray(ObjectCount);

	// If there are any Asset Registry tags, add them to the summary
	for (int32 ObjectIdx = 0; ObjectIdx < AssetObjects.Num(); ++ObjectIdx)
	{
		const UObject* Object = AssetObjects[ObjectIdx];

		// Exclude the package name in the object path, we just need to know the path relative to the package we are saving
		FString ObjectPath = Object->GetPathName(InOuter);
		FString ObjectClassName = Object->GetClass()->GetName();
		
		TArray<UObject::FAssetRegistryTag> SourceTags;
		Object->GetAssetRegistryTags(SourceTags);

		TArray<UObject::FAssetRegistryTag> Tags;
		for (UObject::FAssetRegistryTag& SourceTag : SourceTags)
		{
			UObject::FAssetRegistryTag* Existing = Tags.FindByPredicate([SourceTag](const UObject::FAssetRegistryTag& InTag) { return InTag.Name == SourceTag.Name; });
			if (Existing)
			{
				Existing->Value = SourceTag.Value;
			}
			else
			{
				Tags.Add(SourceTag);
			}
		}

		int32 TagCount = Tags.Num();

		FStructuredArchive::FRecord AssetRecord = AssetArray.EnterElement().EnterRecord();
		AssetRecord << SA_VALUE(TEXT("Path"), ObjectPath) << SA_VALUE(TEXT("Class"), ObjectClassName);

		FStructuredArchive::FMap TagMap = AssetRecord.EnterField(SA_FIELD_NAME(TEXT("Tags"))).EnterMap(TagCount);
				
		for (TArray<UObject::FAssetRegistryTag>::TConstIterator TagIter(Tags); TagIter; ++TagIter)
		{
			FString Key = TagIter->Name.ToString();
			FString Value = TagIter->Value;

			TagMap.EnterElement(Key) << Value;
		}
	}
}

void SaveBulkData(FLinkerSave* Linker, const UPackage* InOuter, const TCHAR* Filename, const ITargetPlatform* TargetPlatform,
				  FSavePackageContext* SavePackageContext, const bool bTextFormat, const bool bDiffing, const bool bComputeHash, TAsyncWorkSequence<FMD5>& AsyncWriteAndHashSequence, int64& TotalPackageSizeUncompressed)
{
	// Now we write all the bulkdata that is supposed to be at the end of the package
	// and fix up the offset
	const int64 StartOfBulkDataArea = Linker->Tell();
	Linker->Summary.BulkDataStartOffset = StartOfBulkDataArea;

	check(!bTextFormat || Linker->BulkDataToAppend.Num() == 0);

	if (!bTextFormat && Linker->BulkDataToAppend.Num() > 0)
	{
		COOK_STAT(FScopedDurationTimer SaveTimer(SavePackageStats::SerializeBulkDataTimeSec));

		FScopedSlowTask BulkDataFeedback(Linker->BulkDataToAppend.Num());

		TUniquePtr<FLargeMemoryWriter> BulkArchive;
		TUniquePtr<FLargeMemoryWriter> OptionalBulkArchive;
		TUniquePtr<FLargeMemoryWriter> MappedBulkArchive;

		uint32 ExtraBulkDataFlags = 0;

		static const struct FUseSeparateBulkDataFiles
		{
			bool bEnable = false;

			FUseSeparateBulkDataFiles()
			{
				GConfig->GetBool(TEXT("Core.System"), TEXT("UseSeperateBulkDataFiles"), /* out */ bEnable, GEngineIni);

				if (IsEventDrivenLoaderEnabledInCookedBuilds())
				{
					// Always split bulk data when splitting cooked files
					bEnable = true;
				}
			}
		} ShouldUseSeparateBulkDataFiles;

		const bool bShouldUseSeparateBulkFile = ShouldUseSeparateBulkDataFiles.bEnable && Linker->IsCooking();

		if (bShouldUseSeparateBulkFile)
		{
			ExtraBulkDataFlags = BULKDATA_PayloadInSeperateFile;

			BulkArchive.Reset(new FLargeMemoryWriter(0, /* IsPersistent */ true));
			OptionalBulkArchive.Reset(new FLargeMemoryWriter(0, /* IsPersistent */ true));
			MappedBulkArchive.Reset(new FLargeMemoryWriter(0, /* IsPersistent */ true));
		}

		// If we are not allowing BulkData to go to the IoStore and we will be saving the BulkData to a separate file then 
		// we cannot manipulate the offset as we cannot 'fix' it at runtime with the AsyncLoader2
		// 
		// We should remove the manipulated offset entirely, at least for separate files but for now we need to leave it to
		// prevent larger patching sizes.
		if (SavePackageContext != nullptr && SavePackageContext->bForceLegacyOffsets == false && bShouldUseSeparateBulkFile)
		{
			ExtraBulkDataFlags |= BULKDATA_NoOffsetFixUp;
		}

		bool bAlignBulkData = false;
		int64 BulkDataAlignment = 0;

		if (TargetPlatform)
		{
			bAlignBulkData = TargetPlatform->SupportsFeature(ETargetPlatformFeatures::MemoryMappedFiles);
			BulkDataAlignment = TargetPlatform->GetMemoryMappingAlignment();
		}

		uint16 BulkDataIndex = 1;
		for (FLinkerSave::FBulkDataStorageInfo& BulkDataStorageInfo : Linker->BulkDataToAppend)
		{
			BulkDataFeedback.EnterProgressFrame();

			// Set bulk data flags to what they were during initial serialization (they might have changed after that)
			const uint32 OldBulkDataFlags = BulkDataStorageInfo.BulkData->GetBulkDataFlags();
			uint32 ModifiedBulkDataFlags = BulkDataStorageInfo.BulkDataFlags | ExtraBulkDataFlags;
			const bool bBulkItemIsOptional = (ModifiedBulkDataFlags & BULKDATA_OptionalPayload) != 0;
			bool bBulkItemIsMapped = bAlignBulkData && ((ModifiedBulkDataFlags & BULKDATA_MemoryMappedPayload) != 0);

			if (bBulkItemIsMapped && bBulkItemIsOptional)
			{
				UE_LOG(LogSavePackage, Warning, TEXT("%s has bulk data that is both mapped and optional. This is not currently supported. Will not be mapped."), Filename);
				ModifiedBulkDataFlags &= ~BULKDATA_MemoryMappedPayload;
				bBulkItemIsMapped = false;
			}

			BulkDataStorageInfo.BulkData->ClearBulkDataFlags(0xFFFFFFFF);
			BulkDataStorageInfo.BulkData->SetBulkDataFlags(ModifiedBulkDataFlags);

			FArchive* TargetArchive = Linker;

			if (bShouldUseSeparateBulkFile)
			{
				check(MappedBulkArchive && OptionalBulkArchive && BulkArchive);

				if (bBulkItemIsOptional)
				{
					TargetArchive = OptionalBulkArchive.Get();
				}
				else if (bBulkItemIsMapped)
				{
					TargetArchive = MappedBulkArchive.Get();
				}
				else
				{
					TargetArchive = BulkArchive.Get();
				}
			}

			// Pad archive for proper alignment for memory mapping
			if (bBulkItemIsMapped && BulkDataAlignment > 0)
			{
				const int64 BulkStartOffset = TargetArchive->Tell();

				if (!IsAligned(BulkStartOffset, BulkDataAlignment))
				{
					const int64 AlignedOffset = Align(BulkStartOffset, BulkDataAlignment);

					int64 Padding = AlignedOffset - BulkStartOffset;
					check(Padding > 0);

					uint64 Zero64 = 0;
					while (Padding >= 8)
					{
						*TargetArchive << Zero64;
						Padding -= 8;
					}

					uint8 Zero8 = 0;
					while (Padding > 0)
					{
						*TargetArchive << Zero8;
						Padding--;
					}

					check(TargetArchive->Tell() == AlignedOffset);
				}
			}

			const int64 BulkStartOffset = TargetArchive->Tell();

			int64 StoredBulkStartOffset = (ModifiedBulkDataFlags& BULKDATA_NoOffsetFixUp) == 0 ? BulkStartOffset - StartOfBulkDataArea : BulkStartOffset;

			BulkDataStorageInfo.BulkData->SerializeBulkData(*TargetArchive, BulkDataStorageInfo.BulkData->Lock(LOCK_READ_ONLY));

			int64 BulkEndOffset = TargetArchive->Tell();
			const int64 LinkerEndOffset = Linker->Tell();

			int64 SizeOnDisk = BulkEndOffset - BulkStartOffset;

			Linker->Seek(BulkDataStorageInfo.BulkDataFlagsPos);
			*Linker << ModifiedBulkDataFlags;

			Linker->Seek(BulkDataStorageInfo.BulkDataOffsetInFilePos);
			*Linker << StoredBulkStartOffset;

			Linker->Seek(BulkDataStorageInfo.BulkDataSizeOnDiskPos);
			if (ModifiedBulkDataFlags & BULKDATA_Size64Bit)
			{
				*Linker << SizeOnDisk;
			}
			else
			{
				check(SizeOnDisk < (1LL << 31));
				int32 SizeOnDiskAsInt32 = SizeOnDisk;
				*Linker << SizeOnDiskAsInt32;
			}

			if (SavePackageContext != nullptr && SavePackageContext->BulkDataManifest != nullptr)
			{
				auto BulkDataTypeFromFlags = [](uint32 BulkDataFlags) 
				{
					if (BulkDataFlags & BULKDATA_MemoryMappedPayload)
					{
						return FPackageStoreBulkDataManifest::EBulkdataType::MemoryMapped;
					}

					if (BulkDataFlags & BULKDATA_OptionalPayload)
					{
						return FPackageStoreBulkDataManifest::EBulkdataType::Optional;
					}

					return FPackageStoreBulkDataManifest::EBulkdataType::Normal;
				};

				const FPackageStoreBulkDataManifest::EBulkdataType Type = BulkDataTypeFromFlags(BulkDataStorageInfo.BulkDataFlags);

				SavePackageContext->BulkDataManifest->AddFileAccess(Filename, Type, StoredBulkStartOffset, BulkStartOffset, SizeOnDisk);
			}
			
			Linker->Seek(LinkerEndOffset);

			// Restore BulkData flags to before serialization started
			BulkDataStorageInfo.BulkData->ClearBulkDataFlags(0xFFFFFFFF);
			BulkDataStorageInfo.BulkData->SetBulkDataFlags(OldBulkDataFlags);
			BulkDataStorageInfo.BulkData->Unlock();
		}

		if (BulkArchive)
		{
			check(OptionalBulkArchive);
			check(MappedBulkArchive);

			const bool bWriteBulkToDisk = !bDiffing;

			if (SavePackageContext != nullptr && SavePackageContext->PackageStoreWriter != nullptr && bWriteBulkToDisk)
			{
				auto AddSizeAndConvertToIoBuffer = [&TotalPackageSizeUncompressed](FLargeMemoryWriter* Writer)
				{
					const int64 TotalSize = Writer->TotalSize();
					TotalPackageSizeUncompressed += TotalSize;
					return FIoBuffer(FIoBuffer::AssumeOwnership, Writer->ReleaseOwnership(), TotalSize);
				};

				FPackageStoreWriter::FBulkDataInfo BulkInfo;
				BulkInfo.PackageName = InOuter->GetFName();
				BulkInfo.LooseFilePath = Filename;
				BulkInfo.BulkdataType = FPackageStoreWriter::FBulkDataInfo::Standard;

				SavePackageContext->PackageStoreWriter->WriteBulkdata(BulkInfo, AddSizeAndConvertToIoBuffer(BulkArchive.Get()));

				BulkInfo.BulkdataType = FPackageStoreWriter::FBulkDataInfo::Optional;
				SavePackageContext->PackageStoreWriter->WriteBulkdata(BulkInfo, AddSizeAndConvertToIoBuffer(OptionalBulkArchive.Get()));

				BulkInfo.BulkdataType = FPackageStoreWriter::FBulkDataInfo::Mmap;
				SavePackageContext->PackageStoreWriter->WriteBulkdata(BulkInfo, AddSizeAndConvertToIoBuffer(MappedBulkArchive.Get()));
			}
			else
			{
				auto WriteBulkData = [&](FLargeMemoryWriter* Archive, const TCHAR* BulkFileExtension)
				{
					if (const int64 DataSize = Archive->TotalSize())
					{
						TotalPackageSizeUncompressed += DataSize;

						if (bComputeHash || bWriteBulkToDisk)
						{
							FLargeMemoryPtr DataPtr(Archive->ReleaseOwnership());

							const FString ArchiveFilename = FPaths::ChangeExtension(Filename, BulkFileExtension);

							EAsyncWriteOptions WriteOptions(EAsyncWriteOptions::None);
							if (bComputeHash)
							{
								WriteOptions |= EAsyncWriteOptions::ComputeHash;
							}
							if (bWriteBulkToDisk)
							{
								WriteOptions |= EAsyncWriteOptions::WriteFileToDisk;
							}
							AsyncWriteFile(AsyncWriteAndHashSequence, MoveTemp(DataPtr), DataSize, *ArchiveFilename, WriteOptions);
						}
					}
				};

				WriteBulkData(BulkArchive.Get(), TEXT(".ubulk"));			// Regular separate bulk data file
				WriteBulkData(OptionalBulkArchive.Get(), TEXT(".uptnl"));	// Optional bulk data
				WriteBulkData(MappedBulkArchive.Get(), TEXT(".m.ubulk"));	// Memory-mapped bulk data
			}
		}
	}

	Linker->BulkDataToAppend.Empty();
}

static void SaveWorldLevelInfo(UPackage* InOuter, FLinkerSave* Linker, FStructuredArchive::FRecord Record)
{
	Linker->Summary.WorldTileInfoDataOffset = 0;
	
	if(InOuter->WorldTileInfo.IsValid())
	{
		Linker->Summary.WorldTileInfoDataOffset = Linker->Tell();
		Record << SA_VALUE(TEXT("WorldLevelInfo"), *(InOuter->WorldTileInfo));
	}
}

bool UPackage::IsEmptyPackage(UPackage* Package, const UObject* LastReferencer)
{
	// Don't count null or volatile packages as empty, just let them be NULL or get GCed
	if ( Package != nullptr )
	{
		// Make sure the package is fully loaded before determining if it is empty
		if( !Package->IsFullyLoaded() )
		{
			Package->FullyLoad();
		}

		bool bIsEmpty = true;
		ForEachObjectWithPackage(Package, [LastReferencer, &bIsEmpty](UObject* InObject)
		{
			// if the package contains at least one object that has asset registry data and isn't the `LastReferencer` consider it not empty
			if (InObject->IsAsset() && InObject != LastReferencer)
			{
				bIsEmpty = false;
				// we can break out of the iteration as soon as we find one valid object
				return false;
			}
			return true;
		// Don't consider transient, class default or pending kill objects
		}, false, RF_Transient | RF_ClassDefaultObject, EInternalObjectFlags::PendingKill);
		return bIsEmpty;
	}

	// Invalid package
	return false;
}

//////////////////////////////////////////////////////////////////////////
// TODO: this should go elsewhere, this file is big enough as it is already
//

FPackageStoreWriter::FPackageStoreWriter() = default;
FPackageStoreWriter::~FPackageStoreWriter() = default;

FLooseFileWriter::FLooseFileWriter() = default;
FLooseFileWriter::~FLooseFileWriter() = default;

void FLooseFileWriter::WriteHeader(const FLooseFileWriter::HeaderInfo& Info, const FIoBuffer& HeaderData)
{
	WriteToFile(Info.LooseFilePath, HeaderData.Data(), HeaderData.DataSize());
}

void FLooseFileWriter::WriteExports(const FLooseFileWriter::ExportsInfo& Info, const FIoBuffer& ExportsData)
{
	const FString ArchiveFilename = FPaths::ChangeExtension(Info.LooseFilePath, TEXT(".uexp"));

	WriteToFile(ArchiveFilename, ExportsData.Data(), ExportsData.DataSize());
}

void FLooseFileWriter::WriteBulkdata(const FLooseFileWriter::FBulkDataInfo& Info, const FIoBuffer& BulkData)
{
	if (BulkData.DataSize() == 0)
		return;

	const TCHAR* BulkFileExtension = nullptr;

	switch (Info.BulkdataType)
	{
		case FPackageStoreWriter::FBulkDataInfo::Standard:
			BulkFileExtension = TEXT(".ubulk");		// Regular separate bulk data file
			break;

		case FPackageStoreWriter::FBulkDataInfo::Mmap:
			BulkFileExtension = TEXT(".m.ubulk");	// Memory-mapped bulk data
			break;

		case FPackageStoreWriter::FBulkDataInfo::Optional:
			BulkFileExtension = TEXT(".uptnl");		// Optional bulk data
			break;

		default:
			check(false);
	}

	const FString ArchiveFilename = FPaths::ChangeExtension(Info.LooseFilePath, BulkFileExtension);

	WriteToFile(ArchiveFilename, BulkData.Data(), BulkData.DataSize());
}

FSavePackageContext::~FSavePackageContext()
{
	delete PackageStoreWriter;
	delete BulkDataManifest;
}
#endif	// UE_WITH_SAVEPACKAGE
