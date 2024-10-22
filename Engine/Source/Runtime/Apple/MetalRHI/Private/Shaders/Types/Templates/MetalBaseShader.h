// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalBaseShader.h: Metal RHI Base Shader Class Template.
=============================================================================*/

#pragma once

#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Shaders/Debugging/MetalShaderDebugCache.h"
#include "Shaders/MetalCompiledShaderKey.h"
#include "Shaders/MetalCompiledShaderCache.h"


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Shader Code Library Types


extern unsigned char ue4_stdlib_metal[];
extern unsigned int ue4_stdlib_metal_len;


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Base Shader Class Support Routines


extern mtlpp::LanguageVersion ValidateVersion(uint8 Version);


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Base Shader Class Template Defines


/** Set to 1 to enable shader debugging (makes the driver save the shader source) */
#define DEBUG_METAL_SHADERS (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Base Shader Class Template


template<typename BaseResourceType, int32 ShaderType>
class TMetalBaseShader : public BaseResourceType, public IRefCountedObject
{
public:
	enum
	{
		StaticFrequency = ShaderType
	};

	TMetalBaseShader()
	{
		// void
	}

	virtual ~TMetalBaseShader()
	{
		this->Destroy();
	}

	void Init(TArrayView<const uint8> InCode, FMetalCodeHeader& Header, mtlpp::Library InLibrary = nil);
	void Destroy();

	/**
	 * Gets the Metal source code as an NSString if available or nil if not.  Note
	 * that this will dynamically decompress from compressed data on first
	 * invocation.
	 */
	NSString* GetSourceCode();

	// IRefCountedObject interface:
	virtual uint32 AddRef() const override final;
	virtual uint32 Release() const override final;
	virtual uint32 GetRefCount() const override final;

	/** External bindings for this shader. */
	FMetalShaderBindings Bindings;

	// List of memory copies from RHIUniformBuffer to packed uniforms
	TArray<CrossCompiler::FUniformBufferCopyInfo> UniformBuffersCopyInfo;

	/* Argument encoders for shader IABs */
	TMap<uint32, mtlpp::ArgumentEncoder> ArgumentEncoders;

	/* Tier1 Argument buffer bitmasks */
	TMap<uint32, TBitArray<>> ArgumentBitmasks;

	/* Uniform buffer static slots */
	TArray<FUniformBufferStaticSlot> StaticSlots;

	/** The binding for the buffer side-table if present */
	int32 SideTableBinding = -1;

	/** CRC & Len for name disambiguation */
	uint32 SourceLen = 0;
	uint32 SourceCRC = 0;

	/** Hash for the shader/material permutation constants */
	uint32 ConstantValueHash = 0;

protected:
	mtlpp::Function GetCompiledFunction(bool const bAsync = false);

	// this is the compiler shader
	mtlpp::Function Function = nil;

private:
	// This is the MTLLibrary for the shader so we can dynamically refine the MTLFunction
	mtlpp::Library Library = nil;

	/** The debuggable text source */
	NSString* GlslCodeNSString = nil;

	/** The compressed text source */
	TArray<uint8> CompressedSource;

	/** The uncompressed text source size */
	uint32 CodeSize = 0;

	// Function constant states
	bool bHasFunctionConstants = false;
	bool bDeviceFunctionConstants = false;
};


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Base Shader Class Template Member Functions


template<typename BaseResourceType, int32 ShaderType>
void TMetalBaseShader<BaseResourceType, ShaderType>::Init(TArrayView<const uint8> InShaderCode, FMetalCodeHeader& Header, mtlpp::Library InLibrary)
{
	FShaderCodeReader ShaderCode(InShaderCode);

	FMemoryReaderView Ar(InShaderCode, true);

	Ar.SetLimitSize(ShaderCode.GetActualShaderCodeSize());

	// was the shader already compiled offline?
	uint8 OfflineCompiledFlag;
	Ar << OfflineCompiledFlag;
	check(OfflineCompiledFlag == 0 || OfflineCompiledFlag == 1);

	// get the header
	Ar << Header;

	ValidateVersion(Header.Version);

	SourceLen = Header.SourceLen;
	SourceCRC = Header.SourceCRC;

	// If this triggers than a level above us has failed to provide valid shader data and the cook is probably bogus
	UE_CLOG(Header.SourceLen == 0 || Header.SourceCRC == 0, LogMetal, Fatal, TEXT("Invalid Shader Bytecode provided."));

	bDeviceFunctionConstants = Header.bDeviceFunctionConstants;

	// remember where the header ended and code (precompiled or source) begins
	int32 CodeOffset = Ar.Tell();
	uint32 BufferSize = ShaderCode.GetActualShaderCodeSize() - CodeOffset;
	const ANSICHAR* SourceCode = (ANSICHAR*)InShaderCode.GetData() + CodeOffset;

	// Only archived shaders should be in here.
	UE_CLOG(InLibrary && !(Header.CompileFlags & (1 << CFLAG_Archive)), LogMetal, Warning, TEXT("Shader being loaded wasn't marked for archiving but a MTLLibrary was provided - this is unsupported."));

	if (!OfflineCompiledFlag)
	{
		UE_LOG(LogMetal, Display, TEXT("Loaded a text shader (will be slower to load)"));
	}

	bool bOfflineCompile = (OfflineCompiledFlag > 0);

	const ANSICHAR* ShaderSource = ShaderCode.FindOptionalData('c');
	bool bHasShaderSource = (ShaderSource && FCStringAnsi::Strlen(ShaderSource) > 0);

	static bool bForceTextShaders = FMetalCommandQueue::SupportsFeature(EMetalFeaturesGPUTrace);
	if (!bHasShaderSource)
	{
		int32 LZMASourceSize = 0;
		int32 SourceSize = 0;
		const uint8* LZMASource = ShaderCode.FindOptionalDataAndSize('z', LZMASourceSize);
		const uint8* UnSourceLen = ShaderCode.FindOptionalDataAndSize('u', SourceSize);
		if (LZMASource && LZMASourceSize > 0 && UnSourceLen && SourceSize == sizeof(uint32))
		{
			CompressedSource.Append(LZMASource, LZMASourceSize);
			memcpy(&CodeSize, UnSourceLen, sizeof(uint32));
			bHasShaderSource = false;
		}
#if !UE_BUILD_SHIPPING
		else if(bForceTextShaders)
		{
			GlslCodeNSString = [FMetalShaderDebugCache::Get().GetShaderCode(SourceLen, SourceCRC).GetPtr() retain];
		}
#endif
		if (bForceTextShaders && CodeSize && CompressedSource.Num())
		{
			bHasShaderSource = (GetSourceCode() != nil);
		}
	}
	else if (bOfflineCompile && bHasShaderSource)
	{
		GlslCodeNSString = [NSString stringWithUTF8String:ShaderSource];
		check(GlslCodeNSString);
		[GlslCodeNSString retain];
	}

	bHasFunctionConstants = (Header.bDeviceFunctionConstants);

	ConstantValueHash = 0;

	Library = InLibrary;

	bool bNeedsCompiling = false;

	// Find the existing compiled shader in the cache.
	uint32 FunctionConstantHash = ConstantValueHash;
	FMetalCompiledShaderKey Key(Header.SourceLen, Header.SourceCRC, FunctionConstantHash);

	Function = GetMetalCompiledShaderCache().FindRef(Key);
	if (!Library && Function)
	{
		Library = GetMetalCompiledShaderCache().FindLibrary(Function);
	}
	else
	{
		bNeedsCompiling = true;
	}

	Bindings = Header.Bindings;
	if (bNeedsCompiling || !Library)
	{
		if (bOfflineCompile && bHasShaderSource)
		{
			// For debug/dev/test builds we can use the stored code for debugging - but shipping builds shouldn't have this as it is inappropriate.
#if METAL_DEBUG_OPTIONS
			// For iOS/tvOS we must use runtime compilation to make the shaders debuggable, but
			bool bSavedSource = false;

#if PLATFORM_MAC
			const ANSICHAR* ShaderPath = ShaderCode.FindOptionalData('p');
			bool const bHasShaderPath = (ShaderPath && FCStringAnsi::Strlen(ShaderPath) > 0);

			// on Mac if we have a path for the shader we can access the shader code
			if (bHasShaderPath && !bForceTextShaders && (GetSourceCode() != nil))
			{
				FString ShaderPathString(ShaderPath);

				if (IFileManager::Get().MakeDirectory(*FPaths::GetPath(ShaderPathString), true))
				{
					FString Source(GetSourceCode());
					bSavedSource = FFileHelper::SaveStringToFile(Source, *ShaderPathString);
				}

				static bool bAttemptedAuth = false;
				if (!bSavedSource && !bAttemptedAuth)
				{
					bAttemptedAuth = true;

					if (IFileManager::Get().MakeDirectory(*FPaths::GetPath(ShaderPathString), true))
					{
						bSavedSource = FFileHelper::SaveStringToFile(FString(GlslCodeNSString), *ShaderPathString);
					}

					if (!bSavedSource)
					{
						FPlatformMisc::MessageBoxExt(EAppMsgType::Ok,
													 *NSLOCTEXT("MetalRHI", "ShaderDebugAuthFail", "Could not access directory required for debugging optimised Metal shaders. Falling back to slower runtime compilation of shaders for debugging.").ToString(), TEXT("Error"));
					}
				}
			}
#endif
			// Switch the compile mode so we get debuggable shaders even if we failed to save - if we didn't want
			// shader debugging we wouldn't have included the code...
			bOfflineCompile = bSavedSource || (bOfflineCompile && !bForceTextShaders);
#endif
		}

		if (bOfflineCompile METAL_DEBUG_OPTION(&& !(bHasShaderSource && bForceTextShaders)))
		{
			if (InLibrary)
			{
				Library = InLibrary;
			}
			else
			{
				METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewLibraryBinary: %d_%d"), SourceLen, SourceCRC)));

				// Archived shaders should never get in here.
				check(!(Header.CompileFlags & (1 << CFLAG_Archive)) || BufferSize > 0);

				// allow GCD to copy the data into its own buffer
				//		dispatch_data_t GCDBuffer = dispatch_data_create(InShaderCode.GetTypedData() + CodeOffset, ShaderCode.GetActualShaderCodeSize() - CodeOffset, nil, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
				ns::AutoReleasedError AError;
				void* Buffer = FMemory::Malloc( BufferSize );
				FMemory::Memcpy( Buffer, InShaderCode.GetData() + CodeOffset, BufferSize );
				dispatch_data_t GCDBuffer = dispatch_data_create(Buffer, BufferSize, dispatch_get_main_queue(), ^(void) { FMemory::Free(Buffer); } );

				// load up the already compiled shader
				Library = GetMetalDeviceContext().GetDevice().NewLibrary(GCDBuffer, &AError);
				dispatch_release(GCDBuffer);

				if (Library == nil)
				{
					NSLog(@"Failed to create library: %@", ns::Error(AError).GetPtr());
				}
			}
		}
		else
		{
			METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewLibrarySource: %d_%d"), SourceLen, SourceCRC)));
			NSString* ShaderString = ((OfflineCompiledFlag == 0) ? [NSString stringWithUTF8String:SourceCode] : GlslCodeNSString);

			if(Header.ShaderName.Len())
			{
				ShaderString = [NSString stringWithFormat:@"// %@\n%@", Header.ShaderName.GetNSString(), ShaderString];
			}

			static NSString* UE4StdLibString = [[NSString alloc] initWithBytes:ue4_stdlib_metal length:ue4_stdlib_metal_len encoding:NSUTF8StringEncoding];

			NSString* NewShaderString = [ShaderString stringByReplacingOccurrencesOfString:@"#include \"ue4_stdlib.metal\"" withString:UE4StdLibString];
			NewShaderString = [NewShaderString stringByReplacingOccurrencesOfString:@"#pragma once" withString:@""];

			mtlpp::CompileOptions CompileOptions;

#if DEBUG_METAL_SHADERS
			static bool bForceFastMath = FParse::Param(FCommandLine::Get(), TEXT("metalfastmath"));
			static bool bForceNoFastMath = FParse::Param(FCommandLine::Get(), TEXT("metalnofastmath"));
			if (bForceNoFastMath)
			{
				CompileOptions.SetFastMathEnabled(NO);
			}
			else if (bForceFastMath)
			{
				CompileOptions.SetFastMathEnabled(YES);
			}
			else
#endif
			{
				CompileOptions.SetFastMathEnabled((BOOL)(!(Header.CompileFlags & (1 << CFLAG_NoFastMath))));
			}

#if !PLATFORM_MAC || DEBUG_METAL_SHADERS
			NSMutableDictionary *PreprocessorMacros = [NSMutableDictionary new];
#if !PLATFORM_MAC // Pretty sure that as_type-casts work on macOS, but they don't for half2<->uint on older versions of the iOS runtime compiler.
			[PreprocessorMacros addEntriesFromDictionary: @{ @"METAL_RUNTIME_COMPILER" : @(1)}];
#endif
#if DEBUG_METAL_SHADERS
			[PreprocessorMacros addEntriesFromDictionary: @{ @"MTLSL_ENABLE_DEBUG_INFO" : @(1)}];
#endif
			CompileOptions.SetPreprocessorMacros(PreprocessorMacros);
#endif

			mtlpp::LanguageVersion MetalVersion;
			switch(Header.Version)
			{
				case 6:
				case 5:
				case 4:
					MetalVersion = mtlpp::LanguageVersion::Version2_1;
					break;
				case 3:
					MetalVersion = mtlpp::LanguageVersion::Version2_0;
					break;
				case 2:
					MetalVersion = mtlpp::LanguageVersion::Version1_2;
					break;
				case 1:
					MetalVersion = mtlpp::LanguageVersion::Version1_1;
					break;
				case 0:
#if PLATFORM_MAC
					MetalVersion = mtlpp::LanguageVersion::Version1_1;
#else
					MetalVersion = mtlpp::LanguageVersion::Version1_0;
#endif
					break;
				default:
					UE_LOG(LogRHI, Fatal, TEXT("Failed to create shader with unknown version %d: %s"), Header.Version, *FString(NewShaderString));
#if PLATFORM_MAC
					MetalVersion = mtlpp::LanguageVersion::Version1_1;
#else
					MetalVersion = mtlpp::LanguageVersion::Version1_0;
#endif
					break;
				}
			CompileOptions.SetLanguageVersion(MetalVersion);

			ns::AutoReleasedError Error;
			Library = GetMetalDeviceContext().GetDevice().NewLibrary(NewShaderString, CompileOptions, &Error);
			if (Library == nil)
			{
				UE_LOG(LogRHI, Error, TEXT("*********** Error\n%s"), *FString(NewShaderString));
				UE_LOG(LogRHI, Fatal, TEXT("Failed to create shader: %s"), *FString([Error.GetPtr() description]));
			}
			else if (Error != nil)
			{
				// Warning...
				UE_LOG(LogRHI, Warning, TEXT("*********** Warning\n%s"), *FString(NewShaderString));
				UE_LOG(LogRHI, Warning, TEXT("Created shader with warnings: %s"), *FString([Error.GetPtr() description]));
			}

			GlslCodeNSString = NewShaderString;
			[GlslCodeNSString retain];
		}

		GetCompiledFunction(true);
	}
	UniformBuffersCopyInfo = Header.UniformBuffersCopyInfo;
	SideTableBinding = Header.SideTable;

	StaticSlots.Reserve(Bindings.ShaderResourceTable.ResourceTableLayoutHashes.Num());

	for (uint32 LayoutHash : Bindings.ShaderResourceTable.ResourceTableLayoutHashes)
	{
		if (const FShaderParametersMetadata* Metadata = FindUniformBufferStructByLayoutHash(LayoutHash))
		{
			StaticSlots.Add(Metadata->GetLayout().StaticSlot);
		}
		else
		{
			StaticSlots.Add(MAX_UNIFORM_BUFFER_STATIC_SLOTS);
		}
	}
}

template<typename BaseResourceType, int32 ShaderType>
void TMetalBaseShader<BaseResourceType, ShaderType>::Destroy()
{
	[GlslCodeNSString release];
}

template<typename BaseResourceType, int32 ShaderType>
inline NSString* TMetalBaseShader<BaseResourceType, ShaderType>::GetSourceCode()
{
	if (!GlslCodeNSString && CodeSize && CompressedSource.Num())
	{
		GlslCodeNSString = DecodeMetalSourceCode(CodeSize, CompressedSource);
	}
	if (!GlslCodeNSString)
	{
		GlslCodeNSString = [FString::Printf(TEXT("Hash: %s, Name: Main_%0.8x_%0.8x"), *BaseResourceType::GetHash().ToString(), SourceLen, SourceCRC).GetNSString() retain];
	}
	return GlslCodeNSString;
}

template<typename BaseResourceType, int32 ShaderType>
uint32 TMetalBaseShader<BaseResourceType, ShaderType>::AddRef() const
{
	return FRHIResource::AddRef();
}

template<typename BaseResourceType, int32 ShaderType>
uint32 TMetalBaseShader<BaseResourceType, ShaderType>::Release() const
{
	return FRHIResource::Release();
}

template<typename BaseResourceType, int32 ShaderType>
uint32 TMetalBaseShader<BaseResourceType, ShaderType>::GetRefCount() const
{
	return FRHIResource::GetRefCount();
}

template<typename BaseResourceType, int32 ShaderType>
mtlpp::Function TMetalBaseShader<BaseResourceType, ShaderType>::GetCompiledFunction(bool const bAsync)
{
	mtlpp::Function Func = Function;

	if (!Func)
	{
		// Find the existing compiled shader in the cache.
		uint32 FunctionConstantHash = ConstantValueHash;
		FMetalCompiledShaderKey Key(SourceLen, SourceCRC, FunctionConstantHash);
		Func = Function = GetMetalCompiledShaderCache().FindRef(Key);

		if (!Func)
		{
			// Get the function from the library - the function name is "Main" followed by the CRC32 of the source MTLSL as 0-padded hex.
			// This ensures that even if we move to a unified library that the function names will be unique - duplicates will only have one entry in the library.
			NSString* Name = [NSString stringWithFormat:@"Main_%0.8x_%0.8x", SourceLen, SourceCRC];
			mtlpp::FunctionConstantValues ConstantValues(nil);
			if (bHasFunctionConstants)
			{
				ConstantValues = mtlpp::FunctionConstantValues();

				if (bDeviceFunctionConstants)
				{
					// Index 33 is the device vendor id constant
					ConstantValues.SetConstantValue(&GRHIVendorId, mtlpp::DataType::UInt, @"GMetalDeviceManufacturer");
				}
			}

			if (!bHasFunctionConstants || !bAsync)
			{
				METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewFunction: %s"), *FString(Name))));
				if (!bHasFunctionConstants)
				{
					Function = Library.NewFunction(Name);
				}
				else
				{
					ns::AutoReleasedError AError;
					Function = Library.NewFunction(Name, ConstantValues, &AError);
					ns::Error Error = AError;
					UE_CLOG(Function == nil, LogMetal, Error, TEXT("Failed to create function: %s"), *FString(Error.GetPtr().description));
					UE_CLOG(Function == nil, LogMetal, Fatal, TEXT("*********** Error\n%s"), *FString(GetSourceCode()));
				}

				check(Function);
				GetMetalCompiledShaderCache().Add(Key, Library, Function);

				Func = Function;
			}
			else
			{
				METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewFunctionAsync: %s"), *FString(Name))));
				METAL_GPUPROFILE(uint64 CPUStart = CPUStat.Stats ? CPUStat.Stats->CPUStartTime : 0);
#if ENABLE_METAL_GPUPROFILE
				ns::String nsName(Name);
				Library.NewFunction(Name, ConstantValues, [Key, this, CPUStart, nsName](mtlpp::Function const& NewFunction, ns::Error const& Error){
#else
				Library.NewFunction(Name, ConstantValues, [Key, this](mtlpp::Function const& NewFunction, ns::Error const& Error){
#endif
					METAL_GPUPROFILE(FScopedMetalCPUStats CompletionStat(FString::Printf(TEXT("NewFunctionCompletion: %s"), *FString(nsName.GetPtr()))));
					UE_CLOG(NewFunction == nil, LogMetal, Error, TEXT("Failed to create function: %s"), *FString(Error.GetPtr().description));
					UE_CLOG(NewFunction == nil, LogMetal, Fatal, TEXT("*********** Error\n%s"), *FString(GetSourceCode()));

					GetMetalCompiledShaderCache().Add(Key, Library, NewFunction);
#if ENABLE_METAL_GPUPROFILE
					if (CompletionStat.Stats)
					{
						CompletionStat.Stats->CPUStartTime = CPUStart;
					}
#endif
				});

				return nil;
			}
		}
	}

	if (FMetalCommandQueue::SupportsFeature(EMetalFeaturesIABs) && Bindings.ArgumentBuffers && ArgumentEncoders.Num() == 0)
	{
		uint32 ArgumentBuffers = Bindings.ArgumentBuffers;
		while(ArgumentBuffers)
		{
			uint32 Index = __builtin_ctz(ArgumentBuffers);
			ArgumentBuffers &= ~(1 << Index);

			mtlpp::ArgumentEncoder ArgumentEncoder = Function.NewArgumentEncoderWithBufferIndex(Index);
			ArgumentEncoders.Add(Index, ArgumentEncoder);

			TBitArray<> Resources;
			for (uint8 Id : Bindings.ArgumentBufferMasks[Index])
			{
				if (Id >= Resources.Num())
				{
					Resources.Add(false, (Id + 1) - Resources.Num());
				}
				Resources[Id] = true;
			}
			ArgumentBitmasks.Add(Index, Resources);
		}
	}

	check(Func);
	return Func;
}
