// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalVertexBuffer.cpp: Metal vertex buffer RHI implementation.
=============================================================================*/

#include "MetalRHIPrivate.h"
#include "MetalProfiler.h"
#include "MetalCommandBuffer.h"
#include "MetalCommandQueue.h"
#include "Containers/ResourceArray.h"
#include "RenderUtils.h"
#include "MetalLLM.h"
#include <objc/runtime.h>

#define METAL_POOL_BUFFER_BACKING 1

#if !METAL_POOL_BUFFER_BACKING
//DECLARE_MEMORY_STAT_EXTERN(TEXT("Used Device Buffer Memory"), STAT_MetalDeviceBufferMemory, STATGROUP_MetalRHI, );
DECLARE_MEMORY_STAT(TEXT("Used Device Buffer Memory"), STAT_MetalDeviceBufferMemory, STATGROUP_MetalRHI);
#endif

#if STATS
#define METAL_INC_DWORD_STAT_BY(Type, Name, Size) \
	switch(Type)	{ \
		case RRT_UniformBuffer: INC_DWORD_STAT_BY(STAT_MetalUniform##Name, Size); break; \
		case RRT_IndexBuffer: INC_DWORD_STAT_BY(STAT_MetalIndex##Name, Size); break; \
		case RRT_StructuredBuffer: \
		case RRT_VertexBuffer: INC_DWORD_STAT_BY(STAT_MetalVertex##Name, Size); break; \
		default: break; \
	}
#else
#define METAL_INC_DWORD_STAT_BY(Type, Name, Size)
#endif

@implementation FMetalBufferData

APPLE_PLATFORM_OBJECT_ALLOC_OVERRIDES(FMetalBufferData)

-(instancetype)init
{
	id Self = [super init];
	if (Self)
	{
		self->Data = nullptr;
		self->Len = 0;
	}
	return Self;
}
-(instancetype)initWithSize:(uint32)InSize
{
	id Self = [super init];
	if (Self)
	{
		self->Data = (uint8*)FMemory::Malloc(InSize);
		self->Len = InSize;
		check(self->Data);
	}
	return Self;
}
-(instancetype)initWithBytes:(void const*)InData length:(uint32)InSize
{
	id Self = [super init];
	if (Self)
	{
		self->Data = (uint8*)FMemory::Malloc(InSize);
		self->Len = InSize;
		check(self->Data);
		FMemory::Memcpy(self->Data, InData, InSize);
	}
	return Self;
}
-(void)dealloc
{
	if (self->Data)
	{
		FMemory::Free(self->Data);
		self->Data = nullptr;
		self->Len = 0;
	}
	[super dealloc];
}
@end


FMetalVertexBuffer::FMetalVertexBuffer(uint32 InSize, uint32 InUsage)
	: FRHIVertexBuffer(InSize, InUsage)
	, FMetalRHIBuffer(InSize, InUsage|EMetalBufferUsage_LinearTex, RRT_VertexBuffer)
{
}

FMetalVertexBuffer::~FMetalVertexBuffer()
{
}

void FMetalVertexBuffer::Swap(FMetalVertexBuffer& Other)
{
	FRHIVertexBuffer::Swap(Other);
	FMetalRHIBuffer::Swap(Other);
}

void FMetalRHIBuffer::Swap(FMetalRHIBuffer& Other)
{
	check(0);
	::Swap(*this, Other);
	
//	Other.Buffer.SetOwner(&Other, true);
//	Buffer.SetOwner(this, true);
}

static bool CanUsePrivateMemory()
{
	return (FMetalCommandQueue::SupportsFeature(EMetalFeaturesEfficientBufferBlits) || FMetalCommandQueue::SupportsFeature(EMetalFeaturesIABs));
}

bool FMetalRHIBuffer::UsePrivateMemory() const
{
	return (FMetalCommandQueue::SupportsFeature(EMetalFeaturesEfficientBufferBlits) && (Usage & (BUF_Dynamic|BUF_Static)))
	|| (FMetalCommandQueue::SupportsFeature(EMetalFeaturesIABs) && (Usage & (BUF_ShaderResource|BUF_UnorderedAccess)));
}

FMetalRHIBuffer::FMetalRHIBuffer(uint32 InSize, uint32 InUsage, ERHIResourceType InType)
: Data(nullptr)
, LastLockFrame(0)
, CurrentIndex(0)
, NumberOfBuffers(0)
, CurrentLockMode(RLM_Num)
, LockOffset(0)
, LockSize(0)
, Size(InSize)
, Usage(InUsage)
, Mode(BUFFER_STORAGE_MODE)
, Type(InType)
{
	// No life-time usage information? Enforce Dynamic.
	if((Usage & (BUF_Static | BUF_Dynamic | BUF_Volatile)) == 0)
	{
		Usage |= BUF_Dynamic;
	}
	
	const bool bIsStatic = (Usage & BUF_Static) != 0;
	const bool bIsDynamic = (Usage & BUF_Dynamic) != 0;
	const bool bIsVolatile = (Usage & BUF_Volatile) != 0;
	const bool bWantsView = (Usage & (BUF_ShaderResource | BUF_UnorderedAccess)) != 0;
	
	check(bIsStatic ^ bIsDynamic ^ bIsVolatile);

	Mode = UsePrivateMemory() ? mtlpp::StorageMode::Private : BUFFER_STORAGE_MODE;
	Mode = CanUsePrivateMemory() ? mtlpp::StorageMode::Private : Mode;
	
	if (InSize)
	{
		checkf(InSize <= 1024 * 1024 * 1024, TEXT("Metal doesn't support buffers > 1GB"));
		
		// Temporary buffers less than the buffer page size - currently 4Kb - is better off going through the set*Bytes API if available.
		// These can't be used for shader resources or UAVs if we want to use the 'Linear Texture' code path
		if (!(InUsage & (BUF_UnorderedAccess|BUF_ShaderResource|EMetalBufferUsage_GPUOnly)) && (InUsage & BUF_Volatile) && InSize < MetalBufferPageSize && (InSize < MetalBufferBytesSize))
		{
			Data = [[FMetalBufferData alloc] initWithSize:InSize];
			METAL_INC_DWORD_STAT_BY(Type, MemAlloc, InSize);
		}
		else
		{
			uint32 AllocSize = Size;
			
			if ((InUsage & EMetalBufferUsage_LinearTex) && !FMetalCommandQueue::SupportsFeature(EMetalFeaturesTextureBuffers))
			{
				if ((InUsage & BUF_UnorderedAccess) && ((InSize - AllocSize) < 512))
				{
					// Padding for write flushing when not using linear texture bindings for buffers
					AllocSize = Align(AllocSize + 512, 1024);
				}
				
				if (bWantsView)
				{
					uint32 NumElements = AllocSize;
					uint32 SizeX = NumElements;
					uint32 SizeY = 1;
					uint32 Dimension = GMaxTextureDimensions;
					while (SizeX > GMaxTextureDimensions)
					{
						while((NumElements % Dimension) != 0)
						{
							check(Dimension >= 1);
							Dimension = (Dimension >> 1);
						}
						SizeX = Dimension;
						SizeY = NumElements / Dimension;
						if(SizeY > GMaxTextureDimensions)
						{
							Dimension <<= 1;
							checkf(SizeX <= GMaxTextureDimensions, TEXT("Calculated width %u is greater than maximum permitted %d when converting buffer of size %u to a 2D texture."), Dimension, (int32)GMaxTextureDimensions, AllocSize);
							check(Dimension <= GMaxTextureDimensions);
							AllocSize = Align(Size, Dimension);
							NumElements = AllocSize;
							SizeX = NumElements;
						}
					}
					
					AllocSize = Align(AllocSize, 1024);
				}
			}
			
			// Static buffers will never be discarded. You can update them directly.
			if(bIsStatic)
			{
				NumberOfBuffers = 1;
			}
			else
			{
				check(bIsDynamic || bIsVolatile);
				NumberOfBuffers = 3;
			}
			
			check(NumberOfBuffers > 0);
			
			BufferPool.SetNum(NumberOfBuffers);
			
			// These allocations will not go into the pool.
			uint32 RequestedBufferOffsetAlignment = BufferOffsetAlignment;
			if(bWantsView)
			{
				// Buffer backed linear textures have specific align requirements
				// We don't know upfront the pixel format that may be requested for an SRV so we can't use minimumLinearTextureAlignmentForPixelFormat:
				RequestedBufferOffsetAlignment = BufferBackedLinearTextureOffsetAlignment;
			}
			
			AllocSize = Align(AllocSize, RequestedBufferOffsetAlignment);
			for(uint32 i = 0; i < NumberOfBuffers; i++)
			{
				FMetalBufferAndViews& Backing = BufferPool[i];

#if METAL_POOL_BUFFER_BACKING
				FMetalPooledBufferArgs ArgsCPU(GetMetalDeviceContext().GetDevice(), AllocSize, Usage, Mode);
				Backing.Buffer = GetMetalDeviceContext().CreatePooledBuffer(ArgsCPU);
				Backing.Buffer.SetOwner(nullptr, false);
#else
				
				NSUInteger Options = (((NSUInteger) Mode) << mtlpp::ResourceStorageModeShift);
				
				METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("AllocBuffer: %llu, %llu"), AllocSize, Options)));
				// Allocate one.
				Backing.Buffer = FMetalBuffer(MTLPP_VALIDATE(mtlpp::Device, GetMetalDeviceContext().GetDevice(), SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, NewBuffer(AllocSize, (mtlpp::ResourceOptions) Options)), false);
				
				#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
					MetalLLM::LogAllocBuffer(GetMetalDeviceContext().GetDevice(), Backing.Buffer);
				#endif
				INC_MEMORY_STAT_BY(STAT_MetalDeviceBufferMemory, Backing.Buffer.GetLength());
				
				if (GMetalBufferZeroFill && Mode != mtlpp::StorageMode::Private)
				{
					FMemory::Memset(((uint8*)Backing.Buffer.GetContents()), 0, Backing.Buffer.GetLength());
				}
				
				METAL_DEBUG_OPTION(GetMetalDeviceContext().ValidateIsInactiveBuffer(Backing.Buffer));
				METAL_FATAL_ASSERT(Backing.Buffer, TEXT("Failed to create buffer of size %u and resource options %u"), Size, (uint32)Options);
				
				if(bIsStatic)
				{
					[Backing.Buffer.GetPtr() setLabel:[NSString stringWithFormat:@"Static on frame %u", GetMetalDeviceContext().GetFrameNumberRHIThread()]];
				}
				else
				{
					[Backing.Buffer.GetPtr() setLabel:[NSString stringWithFormat:@"buffer on frame %u", GetMetalDeviceContext().GetFrameNumberRHIThread()]];
				}
				
#endif
			}
			
			for(FMetalBufferAndViews& Backing : BufferPool)
			{
				check(Backing.Buffer);
				check(AllocSize <= Backing.Buffer.GetLength());
				check(Backing.Buffer.GetStorageMode() == Mode);
				check(Backing.Views.Num() == 0);
			}
		}
	}
}

FMetalRHIBuffer::~FMetalRHIBuffer()
{
	if(TransferBuffer)
	{
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, TransferBuffer.GetLength());
		SafeReleaseMetalBuffer(TransferBuffer);
	}
	
	for(FMetalBufferAndViews& Backing : BufferPool)
	{
		check(Backing.Buffer);
		
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, Backing.Buffer.GetLength());
		SafeReleaseMetalBuffer(Backing.Buffer);
		
		for (auto& Pair : Backing.Views)
		{
			SafeReleaseMetalTexture(Pair.Value);
			Pair.Value = nil;
		}
		Backing.Views.Empty();
	}

	if (Data)
	{
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, Size);
		SafeReleaseMetalObject(Data);
	}
}

void FMetalRHIBuffer::Alias()
{
//	if (Mode == mtlpp::StorageMode::Private && Buffer.GetHeap() && !Buffer.IsAliasable())
//	{
//		Buffer.MakeAliasable();
//#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
//		MetalLLM::LogAliasBuffer(Buffer);
//#endif
//	}
}

void FMetalRHIBuffer::Unalias()
{
//	if (Mode == mtlpp::StorageMode::Private && Buffer.GetHeap() && Buffer.IsAliasable())
//	{
//		uint32 Len = Buffer.GetLength();
//		METAL_INC_DWORD_STAT_BY(Type, MemFreed, Len);
//		SafeReleaseMetalBuffer(Buffer);
//		Buffer = nil;
//
//		Alloc(Len, RLM_WriteOnly);
//	}
}

//void FMetalRHIBuffer::Alloc(uint32 InSize, EResourceLockMode LockMode)
//{
//	check(0);
//}

void FMetalRHIBuffer::AllocTransferBuffer(bool bOnRHIThread, uint32 InSize, EResourceLockMode LockMode)
{
	check(!TransferBuffer);
	FMetalPooledBufferArgs ArgsCPU(GetMetalDeviceContext().GetDevice(), InSize, BUF_Dynamic, mtlpp::StorageMode::Shared);
	TransferBuffer = GetMetalDeviceContext().CreatePooledBuffer(ArgsCPU);
	TransferBuffer.SetOwner(nullptr, false);
	check(TransferBuffer && TransferBuffer.GetPtr());
	METAL_INC_DWORD_STAT_BY(Type, MemAlloc, InSize);
	METAL_FATAL_ASSERT(TransferBuffer, TEXT("Failed to create buffer of size %u and storage mode %u"), InSize, (uint32)mtlpp::StorageMode::Shared);
}

void FMetalRHIBuffer::AllocLinearTextures(const LinearTextureMapKey& InLinearTextureMapKey)
{
	check(MetalIsSafeToUseRHIThreadResources());
	
	const bool bWantsView = ((Usage & (BUF_ShaderResource | BUF_UnorderedAccess)) != 0);
	check(bWantsView);
	{
		FMetalBufferAndViews& CurrentBacking = GetCurrentBackingInternal();
		FMetalBuffer& CurrentBuffer = CurrentBacking.Buffer;
		
		check(CurrentBuffer);
		uint32 Length = CurrentBuffer.GetLength();
		uint8 InFormat = InLinearTextureMapKey.Key;
		const FMetalLinearTextureDescriptor& LinearTextureDesc = InLinearTextureMapKey.Value;
		
		mtlpp::PixelFormat MTLFormat = (mtlpp::PixelFormat)GMetalBufferFormats[InFormat].LinearTextureFormat;
		
		mtlpp::TextureDescriptor Desc;
		NSUInteger TexUsage = mtlpp::TextureUsage::Unknown;
		if (Usage & BUF_ShaderResource)
		{
			TexUsage |= mtlpp::TextureUsage::ShaderRead;
		}
		if (Usage & BUF_UnorderedAccess)
		{
			TexUsage |= mtlpp::TextureUsage::ShaderWrite;
		}
		
		uint32 BytesPerElement = (0 == LinearTextureDesc.BytesPerElement) ? GPixelFormats[InFormat].BlockBytes : LinearTextureDesc.BytesPerElement;
		if (MTLFormat == mtlpp::PixelFormat::RG11B10Float && MTLFormat != (mtlpp::PixelFormat)GPixelFormats[InFormat].PlatformFormat)
		{
			BytesPerElement = 4;
		}

		const uint32 MinimumByteAlignment = GetMetalDeviceContext().GetDevice().GetMinimumLinearTextureAlignmentForPixelFormat((mtlpp::PixelFormat)GMetalBufferFormats[InFormat].LinearTextureFormat);
		const uint32 MinimumElementAlignment = MinimumByteAlignment / BytesPerElement;

		uint32 Offset = LinearTextureDesc.StartOffsetBytes;
		check(Offset % MinimumByteAlignment == 0);

		uint32 NumElements = (UINT_MAX == LinearTextureDesc.NumElements) ? ((Size - Offset) / BytesPerElement) : LinearTextureDesc.NumElements;
		NumElements = Align(NumElements, MinimumElementAlignment);

		uint32 RowBytes = NumElements * BytesPerElement;

		if (FMetalCommandQueue::SupportsFeature(EMetalFeaturesTextureBuffers))
		{
			NSUInteger Options = ((NSUInteger) Mode) << mtlpp::ResourceStorageModeShift;
			Desc = mtlpp::TextureDescriptor::TextureBufferDescriptor(MTLFormat, NumElements, mtlpp::ResourceOptions(Options), mtlpp::TextureUsage(TexUsage));
			Desc.SetAllowGPUOptimisedContents(false);
		}
		else
		{
			uint32 Width = NumElements;
			uint32 Height = 1;

			if (NumElements > GMaxTextureDimensions)
			{
				uint32 Dimension = GMaxTextureDimensions;
				while ((NumElements % Dimension) != 0)
				{
					check(Dimension >= 1);
					Dimension = (Dimension >> 1);
				}

				Width = Dimension;
				Height = NumElements / Dimension;

				// If we're just trying to fit as many elements as we can into
				// the available buffer space, we can trim some padding at the
				// end of the buffer in order to create widest possible linear
				// texture that will fit.
				if ((UINT_MAX == LinearTextureDesc.NumElements) && (Height > GMaxTextureDimensions))
				{
					Width = GMaxTextureDimensions;
					Height = 1;

					while ((Width * Height) < NumElements)
					{
						Height <<= 1;
					}

					while ((Width * Height) > NumElements)
					{
						Height -= 1;
					}
				}

				checkf(Width <= GMaxTextureDimensions, TEXT("Calculated width %u is greater than maximum permitted %d when converting buffer of size %llu with element stride %u to a 2D texture with %u elements."), Width, (int32)GMaxTextureDimensions, Length, BytesPerElement, NumElements);
				checkf(Height <= GMaxTextureDimensions, TEXT("Calculated height %u is greater than maximum permitted %d when converting buffer of size %llu with element stride %u to a 2D texture with %u elements."), Height, (int32)GMaxTextureDimensions, Length, BytesPerElement, NumElements);
			}

			RowBytes = Width * BytesPerElement;

			check(RowBytes % MinimumByteAlignment == 0);
			check((RowBytes * Height) + Offset <= Length);

			Desc = mtlpp::TextureDescriptor::Texture2DDescriptor(MTLFormat, Width, Height, NO);
			Desc.SetStorageMode(Mode);
			Desc.SetCpuCacheMode(CurrentBuffer.GetCpuCacheMode());
			Desc.SetUsage((mtlpp::TextureUsage)TexUsage);
		}

		for(FMetalBufferAndViews& Backing : BufferPool)
		{
			FMetalBuffer& Buffer = Backing.Buffer;
			FMetalTexture NewTexture = MTLPP_VALIDATE(mtlpp::Buffer, Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, NewTexture(Desc, Offset, RowBytes));
			METAL_FATAL_ASSERT(NewTexture, TEXT("Failed to create linear texture, desc %s from buffer %s"), *FString([Desc description]), *FString([Buffer description]));
			
			check(GMetalBufferFormats[InFormat].LinearTextureFormat == mtlpp::PixelFormat::RG11B10Float || GMetalBufferFormats[InFormat].LinearTextureFormat == (mtlpp::PixelFormat)NewTexture.GetPixelFormat());
			Backing.Views.Add(InLinearTextureMapKey, NewTexture);
		}
	}
	
	for(FMetalBufferAndViews& Backing : BufferPool)
	{
		LinearTextureMap& Views = Backing.Views;
		check(Views.Find(InLinearTextureMapKey) != nullptr);
	}
}

struct FMetalRHICommandCreateLinearTexture : public FRHICommand<FMetalRHICommandCreateLinearTexture>
{
	FMetalRHIBuffer* Buffer;
	TRefCountPtr<FRHIResource> Parent;
	EPixelFormat Format;
	FMetalLinearTextureDescriptor LinearTextureDesc;
	
	FORCEINLINE_DEBUGGABLE FMetalRHICommandCreateLinearTexture(FMetalRHIBuffer* InBuffer, FRHIResource* InParent, EPixelFormat InFormat, const FMetalLinearTextureDescriptor* InLinearTextureDescriptor)
		: Buffer(InBuffer)
		, Parent(InParent)
		, Format(InFormat)
		, LinearTextureDesc()
	{
		if (InLinearTextureDescriptor)
		{
			LinearTextureDesc = *InLinearTextureDescriptor;
		}
	}
	
	virtual ~FMetalRHICommandCreateLinearTexture()
	{
	}
	
	void Execute(FRHICommandListBase& CmdList)
	{
		check(MetalIsSafeToUseRHIThreadResources());
		Buffer->CreateLinearTexture(Format, Parent.GetReference(), &LinearTextureDesc);
	}
};

void FMetalRHIBuffer::CreateLinearTexture(EPixelFormat InFormat, FRHIResource* InParent, const FMetalLinearTextureDescriptor* InLinearTextureDescriptor)
{
	if ((Usage & (BUF_UnorderedAccess|BUF_ShaderResource)) && GMetalBufferFormats[InFormat].LinearTextureFormat != mtlpp::PixelFormat::Invalid)
	{
		if (IsRunningRHIInSeparateThread() && !IsInRHIThread() && !FRHICommandListExecutor::GetImmediateCommandList().Bypass())
		{
			new (FRHICommandListExecutor::GetImmediateCommandList().AllocCommand<FMetalRHICommandCreateLinearTexture>()) FMetalRHICommandCreateLinearTexture(this, InParent, InFormat, InLinearTextureDescriptor);
		}
		else
		{
			check(MetalIsSafeToUseRHIThreadResources());
			LinearTextureMapKey MapKey = (InLinearTextureDescriptor != nullptr) ? LinearTextureMapKey(InFormat, *InLinearTextureDescriptor) : LinearTextureMapKey(InFormat, FMetalLinearTextureDescriptor());
			
			FMetalBufferAndViews& Backing = GetCurrentBackingInternal();

			FMetalTexture* ExistingTexture = Backing.Views.Find(MapKey);
			if (!ExistingTexture)
			{
				AllocLinearTextures(MapKey);
			}
		}
	}
	
}

ns::AutoReleased<FMetalTexture> FMetalRHIBuffer::GetLinearTexture(EPixelFormat InFormat, const FMetalLinearTextureDescriptor* InLinearTextureDescriptor)
{
	ns::AutoReleased<FMetalTexture> Texture;
	if ((Usage & (BUF_UnorderedAccess|BUF_ShaderResource)) && GMetalBufferFormats[InFormat].LinearTextureFormat != mtlpp::PixelFormat::Invalid)
	{
		LinearTextureMapKey MapKey = (InLinearTextureDescriptor != nullptr) ? LinearTextureMapKey(InFormat, *InLinearTextureDescriptor) : LinearTextureMapKey(InFormat, FMetalLinearTextureDescriptor());

		FMetalBufferAndViews& Backing = GetCurrentBackingInternal();
		FMetalTexture* ExistingTexture = Backing.Views.Find(MapKey);
		if (ExistingTexture)
		{
			Texture = *ExistingTexture;
		}
	}
	return Texture;
}

void* FMetalRHIBuffer::Lock(bool bIsOnRHIThread, EResourceLockMode InLockMode, uint32 Offset, uint32 InSize)
{
	check(CurrentLockMode == RLM_Num);
	check(LockSize == 0 && LockOffset == 0);
	check(MetalIsSafeToUseRHIThreadResources());
	check(!TransferBuffer);
	
	if (Data)
	{
		check(Data->Data);
		return ((uint8*)Data->Data) + Offset;
	}
	
	// The system is very naughty and does not obey this rule
//	check(LastLockFrame == 0 || LastLockFrame != GetMetalDeviceContext().GetFrameNumberRHIThread());
	
	const bool bWriteLock = InLockMode == RLM_WriteOnly;
	const bool bIsStatic = (Usage & BUF_Static) != 0;
	const bool bIsDynamic = (Usage & BUF_Dynamic) != 0;
	const bool bIsVolatile = (Usage & BUF_Volatile) != 0;
	
	void* ReturnPointer = nullptr;
	
	uint32 Len = GetCurrentBacking().Buffer.GetLength(); // all buffers should have the same length or we are in trouble.
	check(Len >= InSize);
	
	if(bWriteLock)
	{
		if(bIsStatic)
		{
			// Static buffers do not discard. They just return the buffer or a transfer buffer.
			// You are not supposed to lock more than once a frame.
		}
		else
		{
			check(bIsDynamic || bIsVolatile);
			// cycle to next allocation
			AdvanceBackingIndex();
		}
		
		if(Mode == mtlpp::StorageMode::Private)
		{
			FMetalFrameAllocator::AllocationEntry TempBacking = GetMetalDeviceContext().GetTransferAllocator()->AcquireSpace(Len);
			GetMetalDeviceContext().NewLock(this, TempBacking);
			check(TempBacking.Backing);
			ReturnPointer = (uint8*) [TempBacking.Backing contents] + TempBacking.Offset;
		}
		else
		{
			check(GetCurrentBacking().Buffer);
			ReturnPointer = GetCurrentBackingInternal().Buffer.GetContents();
		}
		check(ReturnPointer != nullptr);
	}
	else
	{
		check(InLockMode == EResourceLockMode::RLM_ReadOnly);
		// assumes offset is 0 for reads.
		check(Offset == 0);
		
		if(Mode == mtlpp::StorageMode::Private)
		{
			SCOPE_CYCLE_COUNTER(STAT_MetalBufferPageOffTime);
			AllocTransferBuffer(true, Len, RLM_WriteOnly);
			check(TransferBuffer.GetLength() >= InSize);
			
			// Synchronise the buffer with the CPU
			GetMetalDeviceContext().CopyFromBufferToBuffer(GetCurrentBacking().Buffer, 0, TransferBuffer, 0, GetCurrentBacking().Buffer.GetLength());
			
			//kick the current command buffer.
			GetMetalDeviceContext().SubmitCommandBufferAndWait();
			
			ReturnPointer = TransferBuffer.GetPtr();
		}
		#if PLATFORM_MAC
		else if(Mode == mtlpp::StorageMode::Managed)
		{
			SCOPE_CYCLE_COUNTER(STAT_MetalBufferPageOffTime);
			
			// Synchronise the buffer with the CPU
			GetMetalDeviceContext().SynchroniseResource(GetCurrentBacking().Buffer);
			
			//kick the current command buffer.
			GetMetalDeviceContext().SubmitCommandBufferAndWait();
			
			ReturnPointer = GetCurrentBackingInternal().Buffer.GetContents();
		}
		#endif
		else
		{
			// Shared
			ReturnPointer = GetCurrentBackingInternal().Buffer.GetContents();
		}
	} // Read Path
	
	
	
	check(GetCurrentBacking().Buffer);
	check(!GetCurrentBacking().Buffer.IsAliasable());
	
	check(ReturnPointer);
	LockOffset = Offset;
	LockSize = InSize;
	CurrentLockMode = InLockMode;
	
	if(InSize == 0)
	{
		LockSize = Len;
	}
	
	ReturnPointer = ((uint8*) (ReturnPointer)) + Offset;
	return ReturnPointer;
}

void FMetalRHIBuffer::Unlock()
{
	check(MetalIsSafeToUseRHIThreadResources());

	if(!Data)
	{
		FMetalBufferAndViews& Backing = GetCurrentBackingInternal();
		FMetalBuffer& CurrentBuffer = Backing.Buffer;
		
		check(CurrentBuffer);
		check(LockSize > 0);
		const bool bWriteLock = CurrentLockMode == RLM_WriteOnly;
		
		if(bWriteLock)
		{
			check(!TransferBuffer);
			check(LockOffset == 0);
			check(LockSize <= CurrentBuffer.GetLength());
			if(Mode == mtlpp::StorageMode::Private)
			{
				FMetalFrameAllocator::AllocationEntry Entry = GetMetalDeviceContext().FetchAndRemoveLock(this);
				FMetalBuffer Transfer = Entry.Backing;
				GetMetalDeviceContext().AsyncCopyFromBufferToBuffer(Transfer, Entry.Offset, CurrentBuffer, 0, LockSize);
			}
#if PLATFORM_MAC
			else if(Mode == mtlpp::StorageMode::Managed)
			{
				if (GMetalBufferZeroFill)
					MTLPP_VALIDATE(mtlpp::Buffer, CurrentBuffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(0, CurrentBuffer.GetLength())));
				else
					MTLPP_VALIDATE(mtlpp::Buffer, CurrentBuffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(LockOffset, LockSize)));
			}
#endif //PLATFORM_MAC
			else
			{
				// shared buffers are always mapped so nothing happens
				check(Mode == mtlpp::StorageMode::Shared);
			}
		}
		else
		{
			check(CurrentLockMode == RLM_ReadOnly);
			if(TransferBuffer)
			{
				check(Mode == mtlpp::StorageMode::Private);
				SafeReleaseMetalBuffer(TransferBuffer);
				TransferBuffer = nil;
			}
		}
	}
	
	check(!TransferBuffer);
	CurrentLockMode = RLM_Num;
	LockSize = 0;
	LockOffset = 0;
	LastLockFrame = GetMetalDeviceContext().GetFrameNumberRHIThread();
}

FVertexBufferRHIRef FMetalDynamicRHI::RHICreateVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	check(0);
	@autoreleasepool {
	if (CreateInfo.bWithoutNativeResource)
	{
		return new FMetalVertexBuffer(0, 0);
	}
	
	// make the RHI object, which will allocate memory
	FMetalVertexBuffer* VertexBuffer = new FMetalVertexBuffer(Size, InUsage);

	if (CreateInfo.ResourceArray)
	{
		check(Size >= CreateInfo.ResourceArray->GetResourceDataSize());

		// make a buffer usable by CPU
		void* Buffer = ::RHILockVertexBuffer(VertexBuffer, 0, Size, RLM_WriteOnly);
		
		// copy the contents of the given data into the buffer
		FMemory::Memcpy(Buffer, CreateInfo.ResourceArray->GetResourceData(), Size);
		
		::RHIUnlockVertexBuffer(VertexBuffer);

		// Discard the resource array's contents.
		CreateInfo.ResourceArray->Discard();
	}
	else if (VertexBuffer->Mode == mtlpp::StorageMode::Private)
	{
		check (!VertexBuffer->TransferBuffer);

		if (GMetalBufferZeroFill && !FMetalCommandQueue::SupportsFeature(EMetalFeaturesFences))
		{
			for(FMetalRHIBuffer::FMetalBufferAndViews& Backing : VertexBuffer->BufferPool)
			{
				FMetalBuffer& TheBuffer = Backing.Buffer;
				GetMetalDeviceContext().FillBuffer(TheBuffer, ns::Range(0, TheBuffer.GetLength()), 0);
			}
		}
	}
#if PLATFORM_MAC
	else if (GMetalBufferZeroFill && VertexBuffer->Mode == mtlpp::StorageMode::Managed)
	{
		for(FMetalRHIBuffer::FMetalBufferAndViews& Backing : VertexBuffer->BufferPool)
		{
			FMetalBuffer& TheBuffer = Backing.Buffer;
			GetMetalDeviceContext().FillBuffer(TheBuffer, ns::Range(0, TheBuffer.GetLength()), 0);
			MTLPP_VALIDATE(mtlpp::Buffer, TheBuffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(0, TheBuffer)));
		}
	}
#endif

	return VertexBuffer;
	}
}

void* FMetalDynamicRHI::LockVertexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	@autoreleasepool {
	FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

	// default to vertex buffer memory
	return (uint8*)VertexBuffer->Lock(true, LockMode, Offset, Size);
	}
}

void FMetalDynamicRHI::UnlockVertexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI)
{
	@autoreleasepool {
	FMetalVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);

	VertexBuffer->Unlock();
	}
}

void FMetalDynamicRHI::RHICopyVertexBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIVertexBuffer* DestBufferRHI)
{
	@autoreleasepool {
		FMetalVertexBuffer* SrcVertexBuffer = ResourceCast(SourceBufferRHI);
		FMetalVertexBuffer* DstVertexBuffer = ResourceCast(DestBufferRHI);
		
		const FMetalBuffer& TheSrcBuffer = SrcVertexBuffer->GetCurrentBuffer();
		const FMetalBuffer& TheDstBuffer = DstVertexBuffer->GetCurrentBuffer();
	
		if (TheSrcBuffer && TheDstBuffer)
		{
			GetMetalDeviceContext().CopyFromBufferToBuffer(TheSrcBuffer, 0, TheDstBuffer, 0, FMath::Min(SrcVertexBuffer->GetSize(), DstVertexBuffer->GetSize()));
		}
		else if (TheDstBuffer)
		{
			FMetalPooledBufferArgs ArgsCPU(GetMetalDeviceContext().GetDevice(), SrcVertexBuffer->GetSize(), BUF_Dynamic, mtlpp::StorageMode::Shared);
			FMetalBuffer TempBuffer = GetMetalDeviceContext().CreatePooledBuffer(ArgsCPU);
			FMemory::Memcpy(TempBuffer.GetContents(), SrcVertexBuffer->Data->Data, SrcVertexBuffer->GetSize());
			GetMetalDeviceContext().CopyFromBufferToBuffer(TempBuffer, 0, TheDstBuffer, 0, FMath::Min(SrcVertexBuffer->GetSize(), DstVertexBuffer->GetSize()));
			SafeReleaseMetalBuffer(TempBuffer);
		}
		else
		{
			void const* SrcData = SrcVertexBuffer->Lock(true, RLM_ReadOnly, 0);
			void* DstData = DstVertexBuffer->Lock(true, RLM_WriteOnly, 0);
			FMemory::Memcpy(DstData, SrcData, FMath::Min(SrcVertexBuffer->GetSize(), DstVertexBuffer->GetSize()));
			SrcVertexBuffer->Unlock();
			DstVertexBuffer->Unlock();
		}
	}
}

//struct FMetalRHICommandInitialiseBuffer : public FRHICommand<FMetalRHICommandInitialiseBuffer>
//{
//	TRefCountPtr<FRHIResource> Resource;
//	FMetalRHIBuffer* Buffer;
//
//	FORCEINLINE_DEBUGGABLE FMetalRHICommandInitialiseBuffer(FMetalRHIBuffer* InBuffer, FRHIResource* InResource)
//	: Resource(InResource)
//	, Buffer(InBuffer)
//	{
//	}
//
//	virtual ~FMetalRHICommandInitialiseBuffer()
//	{
//	}
//
//	void Execute(FRHICommandListBase& CmdList)
//	{
//		const FMetalBuffer& TheBackingBuffer = Buffer->GetCurrentBuffer();
//		check(TheBackingBuffer);
//		if (Buffer->TransferBuffer)
//		{
//			uint32 Size = FMath::Min(TheBackingBuffer.GetLength(), Buffer->TransferBuffer.GetLength());
//			GetMetalDeviceContext().AsyncCopyFromBufferToBuffer(Buffer->TransferBuffer, 0, TheBackingBuffer, 0, Size);
//
//			if (Buffer->TransferBuffer)
//			{
//				SafeReleaseMetalBuffer(Buffer->TransferBuffer);
//				Buffer->TransferBuffer = nil;
//			}
//		}
//		else if (GMetalBufferZeroFill && !FMetalCommandQueue::SupportsFeature(EMetalFeaturesFences))
//		{
//			GetMetalDeviceContext().FillBuffer(TheBackingBuffer, ns::Range(0, TheBackingBuffer.GetLength()), 0);
//		}
//	}
//};

void FMetalRHIBuffer::Init_RenderThread(FRHICommandListImmediate& RHICmdList, uint32 InSize, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, FRHIResource* Resource)
{
	if (CreateInfo.ResourceArray)
	{
		check(InSize == CreateInfo.ResourceArray->GetResourceDataSize());
		
		if(Data)
		{
			FMemory::Memcpy(Data->Data, CreateInfo.ResourceArray->GetResourceData(), InSize);
		}
		else
		{
			if (Mode == mtlpp::StorageMode::Private)
			{
				if (RHICmdList.IsBottomOfPipe())
				{
					void* Backing = this->Lock(true, RLM_WriteOnly, 0, InSize);
					FMemory::Memcpy(Backing, CreateInfo.ResourceArray->GetResourceData(), InSize);
					this->Unlock();
				}
				else
				{
					void* Result = FMemory::Malloc(InSize, 16);
					FMemory::Memcpy(Result, CreateInfo.ResourceArray->GetResourceData(), InSize);
					
					RHICmdList.EnqueueLambda(
						[this, Result, InSize](FRHICommandList& RHICmdList)
						{
							void* Backing = this->Lock(true, RLM_WriteOnly, 0, InSize);
							FMemory::Memcpy(Backing, Result, InSize);
							this->Unlock();
							FMemory::Free(Result);
						});
				}
			}
			else
			{
				FMetalBuffer& TheBuffer = GetCurrentBufferInternal();
				FMemory::Memcpy(TheBuffer.GetContents(), CreateInfo.ResourceArray->GetResourceData(), InSize);
	#if PLATFORM_MAC
				if(Mode == mtlpp::StorageMode::Managed)
				{
					MTLPP_VALIDATE(mtlpp::Buffer, TheBuffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(0, GMetalBufferZeroFill ? TheBuffer.GetLength() : InSize)));
				}
	#endif
			}
		}
		
		// Discard the resource array's contents.
		CreateInfo.ResourceArray->Discard();
	}
}

FVertexBufferRHIRef FMetalDynamicRHI::CreateVertexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	@autoreleasepool {
		if (CreateInfo.bWithoutNativeResource)
		{
			return new FMetalVertexBuffer(0, 0);
		}
		
		// make the RHI object, which will allocate memory
		TRefCountPtr<FMetalVertexBuffer> VertexBuffer = new FMetalVertexBuffer(Size, InUsage);
		
		VertexBuffer->Init_RenderThread(RHICmdList, Size, InUsage, CreateInfo, VertexBuffer);
		
		return VertexBuffer.GetReference();
	}
}

void FMetalDynamicRHI::RHITransferVertexBufferUnderlyingResource(FRHIVertexBuffer* DestVertexBuffer, FRHIVertexBuffer* SrcVertexBuffer)
{
	check(DestVertexBuffer);
	FMetalVertexBuffer* Dest = ResourceCast(DestVertexBuffer);
	if (!SrcVertexBuffer)
	{
		TRefCountPtr<FMetalVertexBuffer> DeletionProxy = new FMetalVertexBuffer(0, 0);
		Dest->Swap(*DeletionProxy);
	}
	else
	{
		FMetalVertexBuffer* Src = ResourceCast(SrcVertexBuffer);
		Dest->Swap(*Src);
	}
}
