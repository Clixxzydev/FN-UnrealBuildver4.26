// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RenderResource.h: Render resource definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/List.h"
#include "RHI.h"
#include "RenderCore.h"
#include "Serialization/MemoryLayout.h"
#include "Containers/DynamicRHIResourceArray.h"

/** Number of frames after which unused global resource allocations will be discarded. */
extern int32 GGlobalBufferNumFramesUnusedThresold;

/**
 * A rendering resource which is owned by the rendering thread.
 * NOTE - Adding new virtual methods to this class may require stubs added to FViewport/FDummyViewport, otherwise certain modules may have link errors
 */
class RENDERCORE_API FRenderResource
{
public:
	template<typename FunctionType>
	static void ForAllResources(const FunctionType& Function)
	{
		const TArray<FRenderResource*>& ResourceList = GetResourceList();
		ResourceListIterationActive.Increment();
		for (int32 Index = 0; Index < ResourceList.Num(); ++Index)
		{
			FRenderResource* Resource = ResourceList[Index];
			if (Resource)
			{
				checkSlow(Resource->ListIndex == Index);
				Function(Resource);
			}
		}
		ResourceListIterationActive.Decrement();
	}

	static void InitRHIForAllResources()
	{
		ForAllResources([](FRenderResource* Resource) { Resource->InitRHI(); });
		// Dynamic resources can have dependencies on static resources (with uniform buffers) and must initialized last!
		ForAllResources([](FRenderResource* Resource) { Resource->InitDynamicRHI(); });
	}

	static void ReleaseRHIForAllResources()
	{
		ForAllResources([](FRenderResource* Resource) { check(Resource->IsInitialized()); Resource->ReleaseRHI(); });
		ForAllResources([](FRenderResource* Resource) { Resource->ReleaseDynamicRHI(); });
	}

	static void ChangeFeatureLevel(ERHIFeatureLevel::Type NewFeatureLevel);

	/** Default constructor. */
	FRenderResource()
		: ListIndex(INDEX_NONE)
		, FeatureLevel(ERHIFeatureLevel::Num)
	{}

	/** Constructor when we know what feature level this resource should support */
	FRenderResource(ERHIFeatureLevel::Type InFeatureLevel)
		: ListIndex(INDEX_NONE)
		, FeatureLevel(InFeatureLevel)
	{}

	/** Destructor used to catch unreleased resources. */
	virtual ~FRenderResource();

	/**
	 * Initializes the dynamic RHI resource and/or RHI render target used by this resource.
	 * Called when the resource is initialized, or when reseting all RHI resources.
	 * Resources that need to initialize after a D3D device reset must implement this function.
	 * This is only called by the rendering thread.
	 */
	virtual void InitDynamicRHI() {}

	/**
	 * Releases the dynamic RHI resource and/or RHI render target resources used by this resource.
	 * Called when the resource is released, or when reseting all RHI resources.
	 * Resources that need to release before a D3D device reset must implement this function.
	 * This is only called by the rendering thread.
	 */
	virtual void ReleaseDynamicRHI() {}

	/**
	 * Initializes the RHI resources used by this resource.
	 * Called when entering the state where both the resource and the RHI have been initialized.
	 * This is only called by the rendering thread.
	 */
	virtual void InitRHI() {}

	/**
	 * Releases the RHI resources used by this resource.
	 * Called when leaving the state where both the resource and the RHI have been initialized.
	 * This is only called by the rendering thread.
	 */
	virtual void ReleaseRHI() {}

	/**
	 * Initializes the resource.
	 * This is only called by the rendering thread.
	 */
	virtual void InitResource();

	/**
	 * Prepares the resource for deletion.
	 * This is only called by the rendering thread.
	 */
	virtual void ReleaseResource();

	/**
	 * If the resource's RHI resources have been initialized, then release and reinitialize it.  Otherwise, do nothing.
	 * This is only called by the rendering thread.
	 */
	void UpdateRHI();

	/** @return The resource's friendly name.  Typically a UObject name. */
	virtual FString GetFriendlyName() const { return TEXT("undefined"); }

	// Accessors.
	FORCEINLINE bool IsInitialized() const { return ListIndex != INDEX_NONE; }

	/** Initialize all resources initialized before the RHI was initialized */
	static void InitPreRHIResources();

private:
	/** @return The global initialized resource list. */
	static TArray<FRenderResource*>& GetResourceList();

	static FThreadSafeCounter ResourceListIterationActive;

	int32 ListIndex;

protected:
	// This is used during mobile editor preview refactor, this will eventually be replaced with a parameter to InitRHI() etc..
	void SetFeatureLevel(const FStaticFeatureLevel InFeatureLevel) { FeatureLevel = (ERHIFeatureLevel::Type)InFeatureLevel; }
	const FStaticFeatureLevel GetFeatureLevel() const { return FeatureLevel == ERHIFeatureLevel::Num ? FStaticFeatureLevel(GMaxRHIFeatureLevel) : FeatureLevel; }
	FORCEINLINE bool HasValidFeatureLevel() const { return FeatureLevel < ERHIFeatureLevel::Num; }

private:
	TEnumAsByte<ERHIFeatureLevel::Type> FeatureLevel;
};

/**
 * Sends a message to the rendering thread to initialize a resource.
 * This is called in the game thread.
 */
extern RENDERCORE_API void BeginInitResource(FRenderResource* Resource);

/**
 * Sends a message to the rendering thread to update a resource.
 * This is called in the game thread.
 */
extern RENDERCORE_API void BeginUpdateResourceRHI(FRenderResource* Resource);

/**
 * Sends a message to the rendering thread to release a resource.
 * This is called in the game thread.
 */
extern RENDERCORE_API void BeginReleaseResource(FRenderResource* Resource);

/**
* Enables the batching of calls to BeginReleaseResource
* This is called in the game thread.
*/
extern RENDERCORE_API void StartBatchedRelease();

/**
* Disables the batching of calls to BeginReleaseResource
* This is called in the game thread.
*/
extern RENDERCORE_API void EndBatchedRelease();

/**
 * Sends a message to the rendering thread to release a resource, and spins until the rendering thread has processed the message.
 * This is called in the game thread.
 */
extern RENDERCORE_API void ReleaseResourceAndFlush(FRenderResource* Resource);

/** Used to declare a render resource that is initialized/released by static initialization/destruction. */
template<class ResourceType>
class TGlobalResource : public ResourceType
{
public:

	/** Default constructor. */
	TGlobalResource()
	{
		InitGlobalResource();
	}

	/** Initialization constructor: 1 parameter. */
	template<typename T1>
	explicit TGlobalResource(T1 Param1)
		: ResourceType(Param1)
	{
		InitGlobalResource();
	}

	/** Initialization constructor: 2 parameters. */
	template<typename T1, typename T2>
	explicit TGlobalResource(T1 Param1, T2 Param2)
		: ResourceType(Param1, Param2)
	{
		InitGlobalResource();
	}

	/** Initialization constructor: 3 parameters. */
	template<typename T1, typename T2, typename T3>
	explicit TGlobalResource(T1 Param1, T2 Param2, T3 Param3)
		: ResourceType(Param1, Param2, Param3)
	{
		InitGlobalResource();
	}

	/** Destructor. */
	virtual ~TGlobalResource()
	{
		ReleaseGlobalResource();
	}

private:

	/**
	 * Initialize the global resource.
	 */
	void InitGlobalResource()
	{
		if(IsInRenderingThread())
		{
			// If the resource is constructed in the rendering thread, directly initialize it.
			((ResourceType*)this)->InitResource();
		}
		else
		{
			// If the resource is constructed outside of the rendering thread, enqueue a command to initialize it.
			BeginInitResource((ResourceType*)this);
		}
	}

	/**
	 * Release the global resource.
	 */
	void ReleaseGlobalResource()
	{
		// This should be called in the rendering thread, or at shutdown when the rendering thread has exited.
		// However, it may also be called at shutdown after an error, when the rendering thread is still running.
		// To avoid a second error in that case we don't assert.
#if 0
		check(IsInRenderingThread());
#endif

		// Cleanup the resource.
		((ResourceType*)this)->ReleaseResource();
	}
};

enum EMipFadeSettings
{
	MipFade_Normal = 0,
	MipFade_Slow,

	MipFade_NumSettings,
};

/** Mip fade settings, selectable by chosing a different EMipFadeSettings. */
struct FMipFadeSettings
{
	FMipFadeSettings( float InFadeInSpeed, float InFadeOutSpeed )
		:	FadeInSpeed( InFadeInSpeed )
		,	FadeOutSpeed( InFadeOutSpeed )
	{
	}

	/** How many seconds to fade in one mip-level. */
	float FadeInSpeed;

	/** How many seconds to fade out one mip-level. */
	float FadeOutSpeed;
};

/** Whether to enable mip-level fading or not: +1.0f if enabled, -1.0f if disabled. */
extern RENDERCORE_API float GEnableMipLevelFading;

/** Global mip fading settings, indexed by EMipFadeSettings. */
extern RENDERCORE_API FMipFadeSettings GMipFadeSettings[MipFade_NumSettings];

/**
 * Functionality for fading in/out texture mip-levels.
 */
struct FMipBiasFade
{
	/** Default constructor that sets all values to default (no mips). */
	FMipBiasFade()
	:	TotalMipCount(0.0f)
	,	MipCountDelta(0.0f)
	,	StartTime(0.0f)
	,	MipCountFadingRate(0.0f)
	,	BiasOffset(0.0f)
	{
	}

	/** Number of mip-levels in the texture. */
	float	TotalMipCount;

	/** Number of mip-levels to fade (negative if fading out / decreasing the mipcount). */
	float	MipCountDelta;

	/** Timestamp when the fade was started. */
	float	StartTime;

	/** Number of seconds to interpolate through all MipCountDelta (inverted). */
	float	MipCountFadingRate;

	/** Difference between total texture mipcount and the starting mipcount for the fade. */
	float	BiasOffset;

	/**
	 *	Sets up a new interpolation target for the mip-bias.
	 *	@param ActualMipCount	Number of mip-levels currently in memory
	 *	@param TargetMipCount	Number of mip-levels we're changing to
	 *	@param LastRenderTime	Timestamp when it was last rendered (FApp::CurrentTime time space)
	 *	@param FadeSetting		Which fade speed settings to use
	 */
	RENDERCORE_API void	SetNewMipCount( float ActualMipCount, float TargetMipCount, double LastRenderTime, EMipFadeSettings FadeSetting );

	/**
	 *	Calculates the interpolated mip-bias based on the current time.
	 *	@return				Interpolated mip-bias value
	 */
	inline float	CalcMipBias() const
	{
		float DeltaTime		= GRenderingRealtimeClock.GetCurrentTime() - StartTime;
		float TimeFactor	= FMath::Min<float>(DeltaTime * MipCountFadingRate, 1.0f);
		float MipBias		= BiasOffset - MipCountDelta*TimeFactor;
		return FMath::FloatSelect(GEnableMipLevelFading, MipBias, 0.0f);
	}

	/**
	 *	Checks whether the mip-bias is still interpolating.
	 *	@return				true if the mip-bias is still interpolating
	 */
	inline bool	IsFading( ) const
	{
		float DeltaTime = GRenderingRealtimeClock.GetCurrentTime() - StartTime;
		float TimeFactor = DeltaTime * MipCountFadingRate;
		return (FMath::Abs<float>(MipCountDelta) > SMALL_NUMBER && TimeFactor < 1.0f);
	}
};

/** A textures resource. */
class FTexture : public FRenderResource
{
public:

	/** The texture's RHI resource. */
	FTextureRHIRef		TextureRHI;

	/** The sampler state to use for the texture. */
	FSamplerStateRHIRef SamplerStateRHI;

	/** Sampler state to be used in deferred passes when discontinuities in ddx / ddy would cause too blurry of a mip to be used. */
	FSamplerStateRHIRef DeferredPassSamplerStateRHI;

	/** The last time the texture has been bound */
	mutable double		LastRenderTime;

	/** Base values for fading in/out mip-levels. */
	FMipBiasFade		MipBiasFade;

	/** true if the texture is in a greyscale texture format. */
	bool				bGreyScaleFormat;

	/**
	 * true if the texture is in the same gamma space as the intended rendertarget (e.g. screenshots).
	 * The texture will have sRGB==false and bIgnoreGammaConversions==true, causing a non-sRGB texture lookup
	 * and no gamma-correction in the shader.
	 */
	bool				bIgnoreGammaConversions;

	/** 
	 * Is the pixel data in this texture sRGB?
	 **/
	bool				bSRGB;

	/** Default constructor. */
	FTexture()
	: TextureRHI(NULL)
	, SamplerStateRHI(NULL)
	, DeferredPassSamplerStateRHI(NULL)
	, LastRenderTime(-FLT_MAX)
	, bGreyScaleFormat(false)
	, bIgnoreGammaConversions(false)
	, bSRGB(false)
	{}

	// Destructor
	virtual ~FTexture() {}
	
	/** Returns the width of the texture in pixels. */
	virtual uint32 GetSizeX() const
	{
		return 0;
	}
	/** Returns the height of the texture in pixels. */
	virtual uint32 GetSizeY() const
	{
		return 0;
	}
	/** Returns the depth of the texture in pixels. */
	virtual uint32 GetSizeZ() const
	{
		return 0;
	}

	// FRenderResource interface.
	virtual void ReleaseRHI() override
	{
		TextureRHI.SafeRelease();
		SamplerStateRHI.SafeRelease();
		DeferredPassSamplerStateRHI.SafeRelease();
	}
	virtual FString GetFriendlyName() const override { return TEXT("FTexture"); }

protected:
	RENDERCORE_API static FRHISamplerState* GetOrCreateSamplerState(const FSamplerStateInitializerRHI& Initializer);
};

/** A textures resource that includes an SRV. */
class FTextureWithSRV : public FTexture
{
public:
	/** SRV that views the entire texture */
	FShaderResourceViewRHIRef ShaderResourceViewRHI;

	/** *optional* UAV that views the entire texture */
	FUnorderedAccessViewRHIRef UnorderedAccessViewRHI;


	virtual ~FTextureWithSRV() {}

	virtual void ReleaseRHI() override
	{
		ShaderResourceViewRHI.SafeRelease();
		UnorderedAccessViewRHI.SafeRelease();
		FTexture::ReleaseRHI();
	}
};

/** A texture reference resource. */
class RENDERCORE_API FTextureReference : public FRenderResource
{
public:
	/** The texture reference's RHI resource. */
	FTextureReferenceRHIRef	TextureReferenceRHI;


private:
	/** The last time the texture has been rendered via this reference. */
	FLastRenderTimeContainer LastRenderTimeRHI;

	/** True if the texture reference has been initialized from the game thread. */
	bool bInitialized_GameThread;

public:
	/** Default constructor. */
	FTextureReference();

	// Destructor
	virtual ~FTextureReference();

	/** Returns the last time the texture has been rendered via this reference. */
	double GetLastRenderTime() const { return LastRenderTimeRHI.GetLastRenderTime(); }

	/** Invalidates the last render time. */
	void InvalidateLastRenderTime();

	/** Returns true if the texture reference has been initialized from the game thread. */
	bool IsInitialized_GameThread() const { return bInitialized_GameThread; }

	/** Kicks off the initialization process on the game thread. */
	void BeginInit_GameThread();

	/** Kicks off the release process on the game thread. */
	void BeginRelease_GameThread();

	// FRenderResource interface.
	virtual void InitRHI();
	virtual void ReleaseRHI();
	virtual FString GetFriendlyName() const;
};

/** A vertex buffer resource */
class RENDERCORE_API FVertexBuffer : public FRenderResource
{
public:
	FVertexBufferRHIRef VertexBufferRHI;

	/** Destructor. */
	virtual ~FVertexBuffer() {}

	// FRenderResource interface.
	virtual void ReleaseRHI() override
	{
		VertexBufferRHI.SafeRelease();
	}
	virtual FString GetFriendlyName() const override { return TEXT("FVertexBuffer"); }
};

class RENDERCORE_API FVertexBufferWithSRV : public FVertexBuffer
{
public:
	/** SRV that views the entire texture */
	FShaderResourceViewRHIRef ShaderResourceViewRHI;

	/** *optional* UAV that views the entire texture */
	FUnorderedAccessViewRHIRef UnorderedAccessViewRHI;

	virtual void ReleaseRHI() override
	{
		ShaderResourceViewRHI.SafeRelease();
		UnorderedAccessViewRHI.SafeRelease();
		FVertexBuffer::ReleaseRHI();
	}
};

/**
* A vertex buffer with a single color component.  This is used on meshes that don't have a color component
* to keep from needing a separate vertex factory to handle this case.
*/
class FNullColorVertexBuffer : public FVertexBuffer
{
public:
	/** 
	* Initialize the RHI for this rendering resource 
	*/
	virtual void InitRHI() override
	{
		// create a static vertex buffer
		FRHIResourceCreateInfo CreateInfo;
		
		void* LockedData = nullptr;
		VertexBufferRHI = RHICreateAndLockVertexBuffer(sizeof(uint32) * 4, BUF_Static | BUF_ZeroStride | BUF_ShaderResource, CreateInfo, LockedData);
		uint32* Vertices = (uint32*)LockedData;
		Vertices[0] = FColor(255, 255, 255, 255).DWColor();
		Vertices[1] = FColor(255, 255, 255, 255).DWColor();
		Vertices[2] = FColor(255, 255, 255, 255).DWColor();
		Vertices[3] = FColor(255, 255, 255, 255).DWColor();
		RHIUnlockVertexBuffer(VertexBufferRHI);
		VertexBufferSRV = RHICreateShaderResourceView(VertexBufferRHI, sizeof(FColor), PF_R8G8B8A8);
	}

	virtual void ReleaseRHI() override
	{
		VertexBufferSRV.SafeRelease();
		FVertexBuffer::ReleaseRHI();
	}

	FShaderResourceViewRHIRef VertexBufferSRV;
};

/** The global null color vertex buffer, which is set with a stride of 0 on meshes without a color component. */
extern RENDERCORE_API TGlobalResource<FNullColorVertexBuffer> GNullColorVertexBuffer;

/** An index buffer resource. */
class FIndexBuffer : public FRenderResource
{
public:
	FIndexBufferRHIRef IndexBufferRHI;

	/** Destructor. */
	virtual ~FIndexBuffer() {}

	// FRenderResource interface.
	virtual void ReleaseRHI() override
	{
		IndexBufferRHI.SafeRelease();
	}
	virtual FString GetFriendlyName() const override { return TEXT("FIndexBuffer"); }
};


FORCEINLINE bool ShouldCompileRayTracingShadersForProject(EShaderPlatform ShaderPlatform)
{
	if (RHISupportsRayTracingShaders(ShaderPlatform))
	{
		extern RENDERCORE_API uint64 GRayTracingPlaformMask;
		return !!(GRayTracingPlaformMask & (1ull << ShaderPlatform));
	}
	else
	{
		return false;
	}
}

// Returns `true` when running on RT-capable machine, RT support is enabled for the project and by game graphics options.
// This function may only be called at runtime, never during cooking.
extern RENDERCORE_API bool IsRayTracingEnabled();

/** A ray tracing geometry resource */
class RENDERCORE_API FRayTracingGeometry : public FRenderResource
{
public:
	TResourceArray<uint8> RawData;

	/** Default constructor. */
	FRayTracingGeometry()
	{}

	/** Destructor. */
	virtual ~FRayTracingGeometry() {}

#if RHI_RAYTRACING
	FRayTracingGeometryRHIRef RayTracingGeometryRHI;
	FRayTracingGeometryInitializer Initializer;

	/** When set to NonSharedVertexBuffers, then shared vertex buffers are not used  */
	static constexpr int64 NonSharedVertexBuffers = -1;

	/** 
	Vertex buffers for dynamic geometries may be sub-allocated from a shared pool, which is periodically reset and its generation ID is incremented.
	Geometries that use the shared buffer must be updated (rebuilt or refit) before they are used for rendering after the pool is reset.
	This is validated by comparing the current shared pool generation ID against generation IDs stored in FRayTracingGeometry during latest update.
	*/
	int64 DynamicGeometrySharedBufferGenerationID = NonSharedVertexBuffers;

	// FRenderResource interface.
	virtual void ReleaseRHI() override
	{
		RayTracingGeometryRHI.SafeRelease();
	}
	virtual FString GetFriendlyName() const override { return TEXT("FRayTracingGeometry"); }

	void SetInitializer(const FRayTracingGeometryInitializer& InInitializer)
	{
		Initializer = InInitializer;
	}

	virtual void InitRHI() override
	{
		if (!IsRayTracingEnabled())
			return;

		check(RawData.Num() == 0 || Initializer.OfflineData == nullptr);
		if (RawData.Num())
		{
			Initializer.OfflineData = &RawData;
		}

		bool bAllSegmentsAreValid = true;
		for (const FRayTracingGeometrySegment& Segment : Initializer.Segments)
		{
			if (!Segment.VertexBuffer)
			{
				bAllSegmentsAreValid = false;
				break;
			}
		}

		if (Initializer.IndexBuffer && bAllSegmentsAreValid)
		{
			RayTracingGeometryRHI = RHICreateRayTracingGeometry(Initializer);
			if (Initializer.OfflineData == nullptr)
			{
				FRHICommandListExecutor::GetImmediateCommandList().BuildAccelerationStructure(RayTracingGeometryRHI);
			}
		}
	}


#endif
};

#if RHI_RAYTRACING
class RENDERCORE_API FRayTracingScene : public FRenderResource
{
public:
	FRayTracingSceneRHIRef RayTracingSceneRHI = nullptr;

	virtual FString GetFriendlyName() const override { return TEXT("FRayTracingScene"); }

	virtual void ReleaseRHI()
	{
		RayTracingSceneRHI.SafeRelease();
	}
};
#endif // RHI_RAYTRACING

/**
 * A system for dynamically allocating GPU memory for vertices.
 */
class RENDERCORE_API FGlobalDynamicVertexBuffer
{
public:
	/**
	 * Information regarding an allocation from this buffer.
	 */
	struct FAllocation
	{
		/** The location of the buffer in main memory. */
		uint8* Buffer;
		/** The vertex buffer to bind for draw calls. */
		FVertexBuffer* VertexBuffer;
		/** The offset in to the vertex buffer. */
		uint32 VertexOffset;

		/** Default constructor. */
		FAllocation()
			: Buffer(NULL)
			, VertexBuffer(NULL)
			, VertexOffset(0)
		{
		}

		/** Returns true if the allocation is valid. */
		FORCEINLINE bool IsValid() const
		{
			return Buffer != NULL;
		}
	};

	/** Default constructor. */
	FGlobalDynamicVertexBuffer();

	/** Destructor. */
	~FGlobalDynamicVertexBuffer();

	/**
	 * Allocates space in the global vertex buffer.
	 * @param SizeInBytes - The amount of memory to allocate in bytes.
	 * @returns An FAllocation with information regarding the allocated memory.
	 */
	FAllocation Allocate(uint32 SizeInBytes);

	/**
	 * Commits allocated memory to the GPU.
	 *		WARNING: Once this buffer has been committed to the GPU, allocations
	 *		remain valid only until the next call to Allocate!
	 */
	void Commit();

	/** Returns true if log statements should be made because we exceeded GMaxVertexBytesAllocatedPerFrame */
	bool IsRenderAlarmLoggingEnabled() const;

private:
	/** The pool of vertex buffers from which allocations are made. */
	struct FDynamicVertexBufferPool* Pool;

	/** A total of all allocations made since the last commit. Used to alert about spikes in memory usage. */
	size_t TotalAllocatedSinceLastCommit;
};

/**
 * A system for dynamically allocating GPU memory for indices.
 */
class RENDERCORE_API FGlobalDynamicIndexBuffer
{
public:
	/**
	 * Information regarding an allocation from this buffer.
	 */
	struct FAllocation
	{
		/** The location of the buffer in main memory. */
		uint8* Buffer = nullptr;
		/** The vertex buffer to bind for draw calls. */
		FIndexBuffer* IndexBuffer = nullptr;
		/** The offset in to the index buffer. */
		uint32 FirstIndex = 0;

		/** Returns true if the allocation is valid. */
		FORCEINLINE bool IsValid() const
		{
			return Buffer != NULL;
		}
	};

	/** Information data with usage details to avoid passing around parameters. */
	struct FAllocationEx : public FAllocation
	{
		FAllocationEx() = default;

		FAllocationEx(const FAllocation& InRef, uint32 InNumIndices, uint32 InIndexStride) 
			: FAllocation(InRef)
			, NumIndices(InNumIndices)
			, IndexStride(InIndexStride) 
		{}

		/** The number of indices allocated. */
		uint32 NumIndices = 0;
		/** The allocation stride (2 or 4 bytes). */
		uint32 IndexStride = 0;
		/** The maximum value of the indices used. */
		uint32 MaxUsedIndex = 0;
	};

	/** Default constructor. */
	FGlobalDynamicIndexBuffer();

	/** Destructor. */
	~FGlobalDynamicIndexBuffer();

	/**
	 * Allocates space in the global index buffer.
	 * @param NumIndices - The number of indices to allocate.
	 * @param IndexStride - The size of an index (2 or 4 bytes).
	 * @returns An FAllocation with information regarding the allocated memory.
	 */
	FAllocation Allocate(uint32 NumIndices, uint32 IndexStride);

	/**
	 * Helper function to allocate.
	 * @param NumIndices - The number of indices to allocate.
	 * @returns an FAllocation with information regarding the allocated memory.
	 */
	template <typename IndexType>
	FORCEINLINE FAllocationEx Allocate(uint32 NumIndices)
	{
		return FAllocationEx(Allocate(NumIndices, sizeof(IndexType)), NumIndices, sizeof(IndexType));
	}

	/**
	 * Commits allocated memory to the GPU.
	 *		WARNING: Once this buffer has been committed to the GPU, allocations
	 *		remain valid only until the next call to Allocate!
	 */
	void Commit();

private:
	/** The pool of vertex buffers from which allocations are made. */
	struct FDynamicIndexBufferPool* Pools[2];
};

/**
 * A list of the most recently used bound shader states.
 * This is used to keep bound shader states that have been used recently from being freed, as they're likely to be used again soon.
 */

template<uint32 Size, bool TThreadSafe = true>
class TBoundShaderStateHistory : public FRenderResource
{
public:

	/** Initialization constructor. */
	TBoundShaderStateHistory():
		NextBoundShaderStateIndex(0)
	{}

	/** Adds a bound shader state to the history. */
	FORCEINLINE void Add(FRHIBoundShaderState* BoundShaderState)
	{
		if (TThreadSafe && GRHISupportsParallelRHIExecute)
		{
			BoundShaderStateHistoryLock.Lock();
		}
		BoundShaderStates[NextBoundShaderStateIndex] = BoundShaderState;
		NextBoundShaderStateIndex = (NextBoundShaderStateIndex + 1) % Size;
		if (TThreadSafe && GRHISupportsParallelRHIExecute)
		{
			BoundShaderStateHistoryLock.Unlock();
		}
	}

	FRHIBoundShaderState* GetLast()
	{
		check(!GRHISupportsParallelRHIExecute);
		// % doesn't work as we want on negative numbers, so handle the wraparound manually
		uint32 LastIndex = NextBoundShaderStateIndex == 0 ? Size - 1 : NextBoundShaderStateIndex - 1;
		return BoundShaderStates[LastIndex];
	}

	// FRenderResource interface.
	virtual void ReleaseRHI()
	{
		if (TThreadSafe && GRHISupportsParallelRHIExecute)
		{
			BoundShaderStateHistoryLock.Lock();
		}
		for(uint32 Index = 0;Index < Size;Index++)
		{
			BoundShaderStates[Index].SafeRelease();
		}
		if (TThreadSafe && GRHISupportsParallelRHIExecute)
		{
			BoundShaderStateHistoryLock.Unlock();
		}
	}

private:

	FBoundShaderStateRHIRef BoundShaderStates[Size];
	uint32 NextBoundShaderStateIndex;
	FCriticalSection BoundShaderStateHistoryLock;
};

/**Note, this should only be used when a platform requires special shader compilation for 32 bit pixel format render targets.
Does not replace pixel format associations across the board**/

FORCEINLINE bool PlatformRequires128bitRT(EPixelFormat PixelFormat)
{
	switch (PixelFormat)
	{
	case PF_R32_FLOAT:
	case PF_G32R32F:
	case PF_A32B32G32R32F:
		return FDataDrivenShaderPlatformInfo::GetRequiresExplicit128bitRT(GMaxRHIShaderPlatform);
	default:
		return false;
	}
}