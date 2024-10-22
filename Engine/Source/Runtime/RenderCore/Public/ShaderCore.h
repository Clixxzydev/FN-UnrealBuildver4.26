// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderCore.h: Shader core module definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Templates/RefCounting.h"
#include "Misc/SecureHash.h"
#include "Misc/Paths.h"
#include "Misc/CoreStats.h"
#include "UniformBuffer.h"

class Error;

/**
 * Controls whether shader related logs are visible.
 * Note: The runtime verbosity is driven by the console variable 'r.ShaderDevelopmentMode'
 */
#if UE_BUILD_DEBUG && (PLATFORM_UNIX)
RENDERCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogShaders, Log, All);
#else
RENDERCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogShaders, Error, All);
#endif

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Total Niagara Shaders"), STAT_ShaderCompiling_NumTotalNiagaraShaders, STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Total Niagara Shader Compiling Time"), STAT_ShaderCompiling_NiagaraShaders, STATGROUP_ShaderCompiling, RENDERCORE_API);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Total OpenColorIO Shaders"), STAT_ShaderCompiling_NumTotalOpenColorIOShaders, STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Total OpenColorIO Shader Compiling Time"), STAT_ShaderCompiling_OpenColorIOShaders, STATGROUP_ShaderCompiling, RENDERCORE_API);

DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Total Material Shader Compiling Time"),STAT_ShaderCompiling_MaterialShaders,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Total Global Shader Compiling Time"),STAT_ShaderCompiling_GlobalShaders,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("RHI Compile Time"),STAT_ShaderCompiling_RHI,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Loading Shader Files"),STAT_ShaderCompiling_LoadingShaderFiles,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("CRCing Shader Files"),STAT_ShaderCompiling_HashingShaderFiles,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("HLSL Translation"),STAT_ShaderCompiling_HLSLTranslation,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("DDC Loading"),STAT_ShaderCompiling_DDCLoading,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Material Loading"),STAT_ShaderCompiling_MaterialLoading,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Material Compiling"),STAT_ShaderCompiling_MaterialCompiling,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Total Material Shaders"),STAT_ShaderCompiling_NumTotalMaterialShaders,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Special Material Shaders"),STAT_ShaderCompiling_NumSpecialMaterialShaders,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Particle Material Shaders"),STAT_ShaderCompiling_NumParticleMaterialShaders,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Skinned Material Shaders"),STAT_ShaderCompiling_NumSkinnedMaterialShaders,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Lit Material Shaders"),STAT_ShaderCompiling_NumLitMaterialShaders,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Unlit Material Shaders"),STAT_ShaderCompiling_NumUnlitMaterialShaders,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Transparent Material Shaders"),STAT_ShaderCompiling_NumTransparentMaterialShaders,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Opaque Material Shaders"),STAT_ShaderCompiling_NumOpaqueMaterialShaders,STATGROUP_ShaderCompiling, RENDERCORE_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Masked Material Shaders"),STAT_ShaderCompiling_NumMaskedMaterialShaders,STATGROUP_ShaderCompiling, RENDERCORE_API);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Shaders Loaded"),STAT_Shaders_NumShadersLoaded,STATGROUP_Shaders, RENDERCORE_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Shader Resources Loaded"),STAT_Shaders_NumShaderResourcesLoaded,STATGROUP_Shaders, RENDERCORE_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Shader Maps Registered"),STAT_Shaders_NumShaderMaps,STATGROUP_Shaders, RENDERCORE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("RT Shader Load Time"),STAT_Shaders_RTShaderLoadTime,STATGROUP_Shaders, RENDERCORE_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Shaders Used"),STAT_Shaders_NumShadersUsedForRendering,STATGROUP_Shaders, RENDERCORE_API);
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Total RT Shader Init Time"),STAT_Shaders_TotalRTShaderInitForRenderingTime,STATGROUP_Shaders, RENDERCORE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Frame RT Shader Init Time"),STAT_Shaders_FrameRTShaderInitForRenderingTime,STATGROUP_Shaders, RENDERCORE_API);
DECLARE_MEMORY_STAT_EXTERN(TEXT("Shader Memory"),STAT_Shaders_ShaderMemory,STATGROUP_Shaders, RENDERCORE_API);
DECLARE_MEMORY_STAT_EXTERN(TEXT("Shader Resource Mem"),STAT_Shaders_ShaderResourceMemory,STATGROUP_Shaders, RENDERCORE_API);
DECLARE_MEMORY_STAT_EXTERN(TEXT("Shader Preload Mem"), STAT_Shaders_ShaderPreloadMemory, STATGROUP_Shaders, RENDERCORE_API);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Shaders Registered"), STAT_Shaders_NumShadersRegistered, STATGROUP_Shaders, RENDERCORE_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Shaders Duplicated"), STAT_Shaders_NumShadersDuplicated, STATGROUP_Shaders, RENDERCORE_API);

inline TStatId GetMemoryStatType(EShaderFrequency ShaderFrequency)
{
	static_assert(10 == SF_NumFrequencies, "EShaderFrequency has a bad size.");

	switch(ShaderFrequency)
	{
		case SF_Pixel:				return GET_STATID(STAT_PixelShaderMemory);
		case SF_Compute:			return GET_STATID(STAT_PixelShaderMemory);
		case SF_RayGen:				return GET_STATID(STAT_PixelShaderMemory);
		case SF_RayMiss:			return GET_STATID(STAT_PixelShaderMemory);
		case SF_RayHitGroup:		return GET_STATID(STAT_PixelShaderMemory);
		case SF_RayCallable:		return GET_STATID(STAT_PixelShaderMemory);
	}
	return GET_STATID(STAT_VertexShaderMemory);
}

/** Initializes shader hash cache from IShaderFormatModules. This must be called before reading any shader include. */
extern RENDERCORE_API void InitializeShaderHashCache();

/** Checks if shader include isn't skipped by a shader hash cache. */
extern RENDERCORE_API void CheckShaderHashCacheInclude(const FString& VirtualFilePath, EShaderPlatform ShaderPlatform);

/** Initializes cached shader type data.  This must be called before creating any FShaderType. */
extern RENDERCORE_API void InitializeShaderTypes();

/** Uninitializes cached shader type data.  This is needed before unloading modules that contain FShaderTypes. */
extern RENDERCORE_API void UninitializeShaderTypes();

/** Returns true if debug viewmodes are allowed for the current platform. */
extern RENDERCORE_API bool AllowDebugViewmodes();

/** Returns true if debug viewmodes are allowed for the given platform. */
extern RENDERCORE_API bool AllowDebugViewmodes(EShaderPlatform Platform);

/** Returns true if debug information should be kept for a given platform. */
extern RENDERCORE_API bool ShouldKeepShaderDebugInfo(EShaderPlatform Platform);

/** Returns true if debug information should be exported to separate files for a given platform . */
extern RENDERCORE_API bool ShouldExportShaderDebugInfo(EShaderPlatform Platform);

struct FShaderTarget
{
	uint32 Frequency : SF_NumBits;
	uint32 Platform : SP_NumBits;

	FShaderTarget()
	{}

	FShaderTarget(EShaderFrequency InFrequency,EShaderPlatform InPlatform)
	:	Frequency(InFrequency)
	,	Platform(InPlatform)
	{}

	friend bool operator==(const FShaderTarget& X, const FShaderTarget& Y)
	{
		return X.Frequency == Y.Frequency && X.Platform == Y.Platform;
	}

	friend FArchive& operator<<(FArchive& Ar,FShaderTarget& Target)
	{
		uint32 TargetFrequency = Target.Frequency;
		uint32 TargetPlatform = Target.Platform;
		Ar << TargetFrequency << TargetPlatform;
		if (Ar.IsLoading())
		{
			Target.Frequency = TargetFrequency;
			Target.Platform = TargetPlatform;
		}
		return Ar;
	}

	EShaderPlatform GetPlatform() const
	{
		return (EShaderPlatform)Platform;
	}

	EShaderFrequency GetFrequency() const
	{
		return (EShaderFrequency)Frequency;
	}

	friend inline uint32 GetTypeHash(FShaderTarget Target)
	{
		return ((Target.Frequency << SP_NumBits) | Target.Platform);
	}
};
DECLARE_INTRINSIC_TYPE_LAYOUT(FShaderTarget);

static_assert(sizeof(FShaderTarget) == sizeof(uint32), "FShaderTarget is expected to be bit-packed into a single uint32.");

enum class EShaderParameterType : uint8
{
	LooseData,
	UniformBuffer,
	Sampler,
	SRV,
	UAV,

	Num
};

struct FParameterAllocation
{
	uint16 BufferIndex;
	uint16 BaseIndex;
	uint16 Size;
	EShaderParameterType Type;
	mutable bool bBound;

	FParameterAllocation() :
		Type(EShaderParameterType::Num),
		bBound(false)
	{}

	friend FArchive& operator<<(FArchive& Ar,FParameterAllocation& Allocation)
	{
		Ar << Allocation.BufferIndex << Allocation.BaseIndex << Allocation.Size << Allocation.bBound;
		Ar << Allocation.Type;
		return Ar;
	}
};

inline bool operator==(const FParameterAllocation& A, const FParameterAllocation& B)
{
	return
		A.BufferIndex == B.BufferIndex && A.BaseIndex == B.BaseIndex && A.Size == B.Size && A.Type == B.Type && A.bBound == B.bBound;
}

inline bool operator!=(const FParameterAllocation& A, const FParameterAllocation& B)
{
	return !(A == B);
}

/**
 * A map of shader parameter names to registers allocated to that parameter.
 */
class FShaderParameterMap
{
public:

	FShaderParameterMap()
	{}

	RENDERCORE_API bool FindParameterAllocation(const TCHAR* ParameterName,uint16& OutBufferIndex,uint16& OutBaseIndex,uint16& OutSize) const;
	RENDERCORE_API bool ContainsParameterAllocation(const TCHAR* ParameterName) const;
	RENDERCORE_API void AddParameterAllocation(const TCHAR* ParameterName,uint16 BufferIndex,uint16 BaseIndex,uint16 Size,EShaderParameterType ParameterType);
	RENDERCORE_API void RemoveParameterAllocation(const TCHAR* ParameterName);

	/** Checks that all parameters are bound and asserts if any aren't in a debug build
	* @param InVertexFactoryType can be 0
	*/
	RENDERCORE_API void VerifyBindingsAreComplete(const TCHAR* ShaderTypeName, FShaderTarget Target, class FVertexFactoryType* InVertexFactoryType) const;

	/** Updates the hash state with the contents of this parameter map. */
	void UpdateHash(FSHA1& HashState) const;

	friend FArchive& operator<<(FArchive& Ar,FShaderParameterMap& InParameterMap)
	{
		// Note: this serialize is used to pass between UE4 and the shader compile worker, recompile both when modifying
		Ar << InParameterMap.ParameterMap;
		return Ar;
	}

	inline void GetAllParameterNames(TArray<FString>& OutNames) const
	{
		ParameterMap.GenerateKeyArray(OutNames);
	}

	inline const TMap<FString, FParameterAllocation>& GetParameterMap() const { return ParameterMap; }

	TMap<FString,FParameterAllocation> ParameterMap;
};

/** Container for shader compiler definitions. */
class FShaderCompilerDefinitions
{
public:

	FShaderCompilerDefinitions()
	{
		// Presize to reduce re-hashing while building shader jobs
		Definitions.Empty(50);
	}

	/**
	 * Works for TCHAR
	 * e.g. SetDefine(TEXT("NUM_SAMPLES"), TEXT("1"));
	 */
	void SetDefine(const TCHAR* Name, const TCHAR* Value)
	{
		Definitions.Add(Name, Value);
	}

	/**
	 * Works for uint32 and bool
	 * e.g. OutEnvironment.SetDefine(TEXT("REALLY"), bReally);
	 * e.g. OutEnvironment.SetDefine(TEXT("NUM_SAMPLES"), NumSamples);
	 */
	void SetDefine(const TCHAR* Name, uint32 Value)
	{
		// can be optimized
		Definitions.Add(Name, *FString::Printf(TEXT("%u"), Value));
	}

	void SetDefine(const TCHAR* Name, int32 Value)
	{
		// can be optimized
		Definitions.Add(Name, *FString::Printf(TEXT("%d"), Value));
	}

	/**
	 * Works for float
	 */
	void SetFloatDefine(const TCHAR* Name, float Value)
	{
		// can be optimized
		Definitions.Add(Name, *FString::Printf(TEXT("%f"), Value));
	}

	const TMap<FString,FString>& GetDefinitionMap() const
	{
		return Definitions;
	}

	friend FArchive& operator<<(FArchive& Ar,FShaderCompilerDefinitions& Defs)
	{
		return Ar << Defs.Definitions;
	}

	void Merge(const FShaderCompilerDefinitions& Other)
	{
		Definitions.Append(Other.Definitions);
	}

private:

	/** Map: definition -> value. */
	TMap<FString,FString> Definitions;
};

struct FBaseShaderResourceTable
{
	/** Bits indicating which resource tables contain resources bound to this shader. */
	uint32 ResourceTableBits;

	/** Mapping of bound SRVs to their location in resource tables. */
	TArray<uint32> ShaderResourceViewMap;

	/** Mapping of bound sampler states to their location in resource tables. */
	TArray<uint32> SamplerMap;

	/** Mapping of bound UAVs to their location in resource tables. */
	TArray<uint32> UnorderedAccessViewMap;

	/** Hash of the layouts of resource tables at compile time, used for runtime validation. */
	TArray<uint32> ResourceTableLayoutHashes;

	FBaseShaderResourceTable() :
		ResourceTableBits(0)
	{
	}

	friend bool operator==(const FBaseShaderResourceTable &A, const FBaseShaderResourceTable& B)
	{
		bool bEqual = true;
		bEqual &= (A.ResourceTableBits == B.ResourceTableBits);
		bEqual &= (A.ShaderResourceViewMap.Num() == B.ShaderResourceViewMap.Num());
		bEqual &= (A.SamplerMap.Num() == B.SamplerMap.Num());
		bEqual &= (A.UnorderedAccessViewMap.Num() == B.UnorderedAccessViewMap.Num());
		bEqual &= (A.ResourceTableLayoutHashes.Num() == B.ResourceTableLayoutHashes.Num());
		if (!bEqual)
		{
			return false;
		}
		bEqual &= (FMemory::Memcmp(A.ShaderResourceViewMap.GetData(), B.ShaderResourceViewMap.GetData(), A.ShaderResourceViewMap.GetTypeSize()*A.ShaderResourceViewMap.Num()) == 0);
		bEqual &= (FMemory::Memcmp(A.SamplerMap.GetData(), B.SamplerMap.GetData(), A.SamplerMap.GetTypeSize()*A.SamplerMap.Num()) == 0);
		bEqual &= (FMemory::Memcmp(A.UnorderedAccessViewMap.GetData(), B.UnorderedAccessViewMap.GetData(), A.UnorderedAccessViewMap.GetTypeSize()*A.UnorderedAccessViewMap.Num()) == 0);
		bEqual &= (FMemory::Memcmp(A.ResourceTableLayoutHashes.GetData(), B.ResourceTableLayoutHashes.GetData(), A.ResourceTableLayoutHashes.GetTypeSize()*A.ResourceTableLayoutHashes.Num()) == 0);
		return bEqual;
	}
};

inline FArchive& operator<<(FArchive& Ar, FBaseShaderResourceTable& SRT)
{
	Ar << SRT.ResourceTableBits;
	Ar << SRT.ShaderResourceViewMap;
	Ar << SRT.SamplerMap;
	Ar << SRT.UnorderedAccessViewMap;
	Ar << SRT.ResourceTableLayoutHashes;

	return Ar;
}

inline FArchive& operator<<(FArchive& Ar, FResourceTableEntry& Entry)
{
	Ar << Entry.UniformBufferName;
	Ar << Entry.Type;
	Ar << Entry.ResourceIndex;
	return Ar;
}

/** The environment used to compile a shader. */
struct FShaderCompilerEnvironment : public FRefCountedObject
{
	// Map of the virtual file path -> content.
	// The virtual file paths are the ones that USF files query through the #include "<The Virtual Path of the file>"
	TMap<FString,FString> IncludeVirtualPathToContentsMap;
	
	TMap<FString,TSharedPtr<FString>> IncludeVirtualPathToExternalContentsMap;

	TArray<uint32> CompilerFlags;
	TMap<uint32,uint8> RenderTargetOutputFormatsMap;
	TMap<FString,FResourceTableEntry> ResourceTableMap;
	TMap<FString,uint32> ResourceTableLayoutHashes;
	TMap<FString, FString> ResourceTableLayoutSlots;
	TMap<FString, FString> RemoteServerData;
	TMap<FString, FString> ShaderFormatCVars;

	const ITargetPlatform* TargetPlatform = nullptr;

	/** Default constructor. */
	FShaderCompilerEnvironment()
	{
		// Presize to reduce re-hashing while building shader jobs
		IncludeVirtualPathToContentsMap.Empty(15);
	}

	/** Initialization constructor. */
	explicit FShaderCompilerEnvironment(const FShaderCompilerDefinitions& InDefinitions)
		: Definitions(InDefinitions)
	{
	}

	/**
	 * Works for TCHAR
	 * e.g. SetDefine(TEXT("NAME"), TEXT("Test"));
	 * e.g. SetDefine(TEXT("NUM_SAMPLES"), 1);
	 * e.g. SetDefine(TEXT("DOIT"), true);
	 */
	void SetDefine(const TCHAR* Name, const TCHAR* Value)	{ Definitions.SetDefine(Name, Value); }
	void SetDefine(const TCHAR* Name, uint32 Value)			{ Definitions.SetDefine(Name, Value); }
	void SetDefine(const TCHAR* Name, int32 Value)			{ Definitions.SetDefine(Name, Value); }
	void SetDefine(const TCHAR* Name, bool Value)			{ Definitions.SetDefine(Name, (uint32)Value); }
	void SetDefine(const TCHAR* Name, float Value)			{ Definitions.SetFloatDefine(Name, Value); }

	const TMap<FString,FString>& GetDefinitions() const
	{
		return Definitions.GetDefinitionMap();
	}

	void SetRenderTargetOutputFormat(uint32 RenderTargetIndex, EPixelFormat PixelFormat)
	{
		RenderTargetOutputFormatsMap.Add(RenderTargetIndex, PixelFormat);
	}

	friend FArchive& operator<<(FArchive& Ar,FShaderCompilerEnvironment& Environment)
	{
		// Note: this serialize is used to pass between UE4 and the shader compile worker, recompile both when modifying
		Ar << Environment.IncludeVirtualPathToContentsMap;

		// Note: skipping Environment.IncludeVirtualPathToExternalContentsMap, which is handled by FShaderCompileUtilities::DoWriteTasks in order to maintain sharing

		Ar << Environment.Definitions;
		Ar << Environment.CompilerFlags;
		Ar << Environment.RenderTargetOutputFormatsMap;
		Ar << Environment.ResourceTableMap;
		Ar << Environment.ResourceTableLayoutHashes;
		Ar << Environment.ResourceTableLayoutSlots;
		Ar << Environment.RemoteServerData;
		Ar << Environment.ShaderFormatCVars;

		return Ar;
	}
	
	void Merge(const FShaderCompilerEnvironment& Other)
	{
		// Merge the include maps
		// Merge the values of any existing keys
		for (TMap<FString,FString>::TConstIterator It(Other.IncludeVirtualPathToContentsMap); It; ++It )
		{
			FString* ExistingContents = IncludeVirtualPathToContentsMap.Find(It.Key());

			if (ExistingContents)
			{
				ExistingContents->Append(It.Value());
			}
			else
			{
				IncludeVirtualPathToContentsMap.Add(It.Key(), It.Value());
			}
		}

		check(Other.IncludeVirtualPathToExternalContentsMap.Num() == 0);

		CompilerFlags.Append(Other.CompilerFlags);
		ResourceTableMap.Append(Other.ResourceTableMap);
		ResourceTableLayoutHashes.Append(Other.ResourceTableLayoutHashes);
		ResourceTableLayoutSlots.Append(Other.ResourceTableLayoutSlots);
		Definitions.Merge(Other.Definitions);
		RenderTargetOutputFormatsMap.Append(Other.RenderTargetOutputFormatsMap);
		RemoteServerData.Append(Other.RemoteServerData);
		ShaderFormatCVars.Append(Other.ShaderFormatCVars);
	}

private:

	FShaderCompilerDefinitions Definitions;
};

// if this changes you need to make sure all shaders get invalidated
struct FShaderCodePackedResourceCounts
{
	// for FindOptionalData() and AddOptionalData()
	static const uint8 Key = 'p';

	bool bGlobalUniformBufferUsed;
	uint8 NumSamplers;
	uint8 NumSRVs;
	uint8 NumCBs;
	uint8 NumUAVs;
	uint16 OutputMask; //Mask of rendertargets bound
};

// if this changes you need to make sure all shaders get invalidated
struct FShaderCodeFeatures
{
	// for FindOptionalData() and AddOptionalData()
	static const uint8 Key = 'x';

	bool bUsesWaveOps;
};

// if this changes you need to make sure all shaders get invalidated
struct FShaderCodeVendorExtension
{
	// for FindOptionalData() and AddOptionalData()
	static const uint8 Key = 'v';

	uint32 VendorId;
	FParameterAllocation Parameter;

	friend FArchive& operator<<(FArchive& Ar, FShaderCodeVendorExtension& Extension)
	{
		return Ar << Extension.VendorId << Extension.Parameter;
	}
};

inline bool operator==(const FShaderCodeVendorExtension& A, const FShaderCodeVendorExtension& B)
{
	return A.VendorId == B.VendorId && A.Parameter == B.Parameter;
}

inline bool operator!=(const FShaderCodeVendorExtension& A, const FShaderCodeVendorExtension& B)
{
	return !(A == B);
}

#ifndef RENDERCORE_ATTRIBUTE_UNALIGNED
// TODO find out if using GCC_ALIGN(1) instead of this new #define break on all kinds of platforms...
#define RENDERCORE_ATTRIBUTE_UNALIGNED
#endif
typedef int32  RENDERCORE_ATTRIBUTE_UNALIGNED unaligned_int32;
typedef uint32 RENDERCORE_ATTRIBUTE_UNALIGNED unaligned_uint32;

// later we can transform that to the actual class passed around at the RHI level
class FShaderCodeReader
{
	TArrayView<const uint8> ShaderCode;

public:
	FShaderCodeReader(TArrayView<const uint8> InShaderCode)
		: ShaderCode(InShaderCode)
	{
		check(ShaderCode.Num());
	}

	uint32 GetActualShaderCodeSize() const
	{
		return ShaderCode.Num() - GetOptionalDataSize();
	}

	// for convenience
	template <class T>
	const T* FindOptionalData() const
	{
		return (const T*)FindOptionalData(T::Key, sizeof(T));
	}


	// @param InKey e.g. FShaderCodePackedResourceCounts::Key
	// @return 0 if not found
	const uint8* FindOptionalData(uint8 InKey, uint8 ValueSize) const
	{
		check(ValueSize);

		const uint8* End = &ShaderCode[0] + ShaderCode.Num();

		int32 LocalOptionalDataSize = GetOptionalDataSize();

		const uint8* Start = End - LocalOptionalDataSize;
		// while searching don't include the optional data size
		End = End - sizeof(LocalOptionalDataSize);
		const uint8* Current = Start;

		while(Current < End)
		{
			uint8 Key = *Current++;
			uint32 Size = *((const unaligned_uint32*)Current);
			Current += sizeof(Size);

			if(Key == InKey && Size == ValueSize)
			{
				return Current;
			}

			Current += Size;
		}

		return 0;
	}

	const ANSICHAR* FindOptionalData(uint8 InKey) const
	{
		check(ShaderCode.Num() >= 4);

		const uint8* End = &ShaderCode[0] + ShaderCode.Num();

		int32 LocalOptionalDataSize = GetOptionalDataSize();

		const uint8* Start = End - LocalOptionalDataSize;
		// while searching don't include the optional data size
		End = End - sizeof(LocalOptionalDataSize);
		const uint8* Current = Start;

		while(Current < End)
		{
			uint8 Key = *Current++;
			uint32 Size = *((const unaligned_uint32*)Current);
			Current += sizeof(Size);

			if(Key == InKey)
			{
				return (ANSICHAR*)Current;
			}

			Current += Size;
		}

		return 0;
	}

	// Returns nullptr and Size -1 if key was not found
	const uint8* FindOptionalDataAndSize(uint8 InKey, int32& OutSize) const
	{
		check(ShaderCode.Num() >= 4);

		const uint8* End = &ShaderCode[0] + ShaderCode.Num();

		int32 LocalOptionalDataSize = GetOptionalDataSize();

		const uint8* Start = End - LocalOptionalDataSize;
		// while searching don't include the optional data size
		End = End - sizeof(LocalOptionalDataSize);
		const uint8* Current = Start;

		while (Current < End)
		{
			uint8 Key = *Current++;
			uint32 Size = *((const unaligned_uint32*)Current);
			Current += sizeof(Size);

			if (Key == InKey)
			{
				OutSize = Size;
				return Current;
			}

			Current += Size;
		}

		OutSize = -1;
		return nullptr;
	}

	int32 GetOptionalDataSize() const
	{
		if(ShaderCode.Num() < sizeof(int32))
		{
			return 0;
		}

		const uint8* End = &ShaderCode[0] + ShaderCode.Num();

		int32 LocalOptionalDataSize = ((const unaligned_int32*)End)[-1];

		check(LocalOptionalDataSize >= 0);
		check(ShaderCode.Num() >= LocalOptionalDataSize);

		return LocalOptionalDataSize;
	}

	int32 GetShaderCodeSize() const
	{
		return ShaderCode.Num() - GetOptionalDataSize();
	}
};

class FShaderCode
{
	// -1 if ShaderData was finalized
	mutable int32 OptionalDataSize;
	// access through class methods
	mutable TArray<uint8> ShaderCodeWithOptionalData;

public:

	FShaderCode()
	: OptionalDataSize(0)
	{
	}

	// adds CustomData or does nothing if that was already done before
	void FinalizeShaderCode() const
	{
		if(OptionalDataSize != -1)
		{
			OptionalDataSize += sizeof(OptionalDataSize);
			ShaderCodeWithOptionalData.Append((const uint8*)&OptionalDataSize, sizeof(OptionalDataSize));
			OptionalDataSize = -1;
		}
	}

	// for write access
	TArray<uint8>& GetWriteAccess()
	{
		return ShaderCodeWithOptionalData;
	}

	int32 GetShaderCodeSize() const
	{
		FinalizeShaderCode();

		FShaderCodeReader Wrapper(ShaderCodeWithOptionalData);

		return Wrapper.GetShaderCodeSize();
	}

	// inefficient, will/should be replaced by GetShaderCodeToRead()
	UE_DEPRECATED(4.26, "Please switch to GetShaderCodeToRead()")
	void GetShaderCodeLegacy(TArray<uint8>& Out) const
	{
		Out.Empty();

		Out.AddUninitialized(GetShaderCodeSize());
		FMemory::Memcpy(Out.GetData(), GetReadAccess().GetData(), ShaderCodeWithOptionalData.Num());
	}

	// for read access, can have additional data attached to the end
	const TArray<uint8>& GetReadAccess() const
	{
		FinalizeShaderCode();

		return ShaderCodeWithOptionalData;
	}

	// for convenience
	template <class T>
	void AddOptionalData(const T &In)
	{
		AddOptionalData(T::Key, (uint8*)&In, sizeof(T));
	}

	// Note: we don't hash the optional attachments in GenerateOutputHash() as they would prevent sharing (e.g. many material share the save VS)
	// can be called after the non optional data was stored in ShaderData
	// @param Key uint8 to save memory so max 255, e.g. FShaderCodePackedResourceCounts::Key
	// @param Size >0, only restriction is that sum of all optional data values must be < 4GB
	void AddOptionalData(uint8 Key, const uint8* ValuePtr, uint32 ValueSize)
	{
		check(ValuePtr);

		// don't add after Finalize happened
		check(OptionalDataSize >= 0);

		ShaderCodeWithOptionalData.Add(Key);
		ShaderCodeWithOptionalData.Append((const uint8*)&ValueSize, sizeof(ValueSize));
		ShaderCodeWithOptionalData.Append(ValuePtr, ValueSize);
		OptionalDataSize += sizeof(uint8) + sizeof(ValueSize) + (uint32)ValueSize;
	}

	// Note: we don't hash the optional attachments in GenerateOutputHash() as they would prevent sharing (e.g. many material share the save VS)
	// convenience, silently drops the data if string is too long
	// @param e.g. 'n' for the ShaderSourceFileName
	void AddOptionalData(uint8 Key, const ANSICHAR* InString)
	{
		uint32 Size = FCStringAnsi::Strlen(InString) + 1;
		AddOptionalData(Key, (uint8*)InString, Size);
	}

	friend FArchive& operator<<(FArchive& Ar, FShaderCode& Output)
	{
		if(Ar.IsLoading())
		{
			Output.OptionalDataSize = -1;
		}
		else
		{
			Output.FinalizeShaderCode();
		}

		// Note: this serialize is used to pass between UE4 and the shader compile worker, recompile both when modifying
		Ar << Output.ShaderCodeWithOptionalData;
		return Ar;
	}
};

/**
 * Converts an absolute or relative shader filename to a filename relative to
 * the shader directory.
 * @param InFilename - The shader filename.
 * @returns a filename relative to the shaders directory.
 */
extern RENDERCORE_API FString ParseVirtualShaderFilename(const FString& InFilename);

/** Replaces virtual platform path with appropriate path for a given ShaderPlatform. Returns true if path was changed. */
extern RENDERCORE_API bool ReplaceVirtualFilePathForShaderPlatform(FString& InOutVirtualFilePath, EShaderPlatform ShaderPlatform);

/** Loads the shader file with the given name.  If the shader file couldn't be loaded, throws a fatal error. */
extern RENDERCORE_API void LoadShaderSourceFileChecked(const TCHAR* VirtualFilePath, EShaderPlatform ShaderPlatform, FString& OutFileContents);

/**
 * Recursively populates IncludeFilenames with the include filenames from Filename
 */
extern RENDERCORE_API void GetShaderIncludes(const TCHAR* EntryPointVirtualFilePath, const TCHAR* VirtualFilePath, TArray<FString>& IncludeVirtualFilePaths, EShaderPlatform ShaderPlatform, uint32 DepthLimit=100);

/**
 * Calculates a Hash for the given filename if it does not already exist in the Hash cache.
 * @param Filename - shader file to Hash
 * @param ShaderPlatform - shader platform to Hash
 */
extern RENDERCORE_API const class FSHAHash& GetShaderFileHash(const TCHAR* VirtualFilePath, EShaderPlatform ShaderPlatform);

/**
 * Calculates a Hash for the list of filenames if it does not already exist in the Hash cache.
 */
extern RENDERCORE_API const class FSHAHash& GetShaderFilesHash(const TArray<FString>& VirtualFilePaths, EShaderPlatform ShaderPlatform);

extern void BuildShaderFileToUniformBufferMap(TMap<FString, TArray<const TCHAR*> >& ShaderFileToUniformBufferVariables);

/**
 * Flushes the shader file and CRC cache, and regenerates the binary shader files if necessary.
 * Allows shader source files to be re-read properly even if they've been modified since startup.
 */
extern RENDERCORE_API void FlushShaderFileCache();

extern RENDERCORE_API void VerifyShaderSourceFiles(EShaderPlatform ShaderPlatform);

struct FCachedUniformBufferDeclaration
{
	// Using SharedPtr so we can hand off lifetime ownership to FShaderCompilerEnvironment::IncludeVirtualPathToExternalContentsMap when invalidating this cache
	TSharedPtr<FString> Declaration;
};

/** Parses the given source file and its includes for references of uniform buffers, which are then stored in UniformBufferEntries. */
extern void GenerateReferencedUniformBuffers(
	const TCHAR* SourceFilename,
	const TCHAR* ShaderTypeName,
	const TMap<FString, TArray<const TCHAR*> >& ShaderFileToUniformBufferVariables,
	TMap<const TCHAR*,FCachedUniformBufferDeclaration>& UniformBufferEntries);

/** Records information about all the uniform buffer layouts referenced by UniformBufferEntries. */
extern RENDERCORE_API void SerializeUniformBufferInfo(class FShaderSaveArchive& Ar, const TMap<const TCHAR*,FCachedUniformBufferDeclaration>& UniformBufferEntries);



/**
 * Returns the map virtual shader directory path -> real shader directory path.
 */
extern RENDERCORE_API const TMap<FString, FString>& AllShaderSourceDirectoryMappings();

/** Hook for shader compile worker to reset the directory mappings. */
extern RENDERCORE_API void ResetAllShaderSourceDirectoryMappings();

/**
 * Maps a real shader directory existing on disk to a virtual shader directory.
 * @param VirtualShaderDirectory Unique absolute path of the virtual shader directory (ex: /Project).
 * @param RealShaderDirectory FPlatformProcess::BaseDir() relative path of the directory map.
 */
extern RENDERCORE_API void AddShaderSourceDirectoryMapping(const FString& VirtualShaderDirectory, const FString& RealShaderDirectory);

/** Validates that the uniform buffer at the requested static slot. */
extern RENDERCORE_API void ValidateStaticUniformBuffer(FRHIUniformBuffer* UniformBuffer, FUniformBufferStaticSlot Slot, uint32 ExpectedHash);

template <typename TRHIContext, typename TRHIShader>
void ApplyGlobalUniformBuffers(
	TRHIContext* CommandContext,
	TRHIShader* Shader,
	const TArray<FUniformBufferStaticSlot>& Slots,
	const TArray<uint32>& LayoutHashes,
	const TArray<FRHIUniformBuffer*>& UniformBuffers)
{
	checkf(LayoutHashes.Num() == Slots.Num(), TEXT("Shader %s, LayoutHashes %d, Slots %d"),
		Shader->GetShaderName(), LayoutHashes.Num(), Slots.Num());

	for (int32 BufferIndex = 0; BufferIndex < Slots.Num(); ++BufferIndex)
	{
		const FUniformBufferStaticSlot Slot = Slots[BufferIndex];

		if (IsUniformBufferStaticSlotValid(Slot))
		{
			FRHIUniformBuffer* Buffer = UniformBuffers[Slot];
			ValidateStaticUniformBuffer(Buffer, Slot, LayoutHashes[BufferIndex]);

			if (Buffer)
			{
				CommandContext->RHISetShaderUniformBuffer(Shader, BufferIndex, Buffer);
			}
		}
	}
}
