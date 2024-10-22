// Copyright Epic Games, Inc. All Rights Reserved.

/**
 *
 * This file contains the various draw mesh macros that display draw calls
 * inside of PIX.
 */

// Colors that are defined for a particular mesh type
// Each event type will be displayed using the defined color
#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "RHI.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "GpuProfilerTrace.h"

// Note:  WITH_PROFILEGPU should be 0 for final builds
#define WANTS_DRAW_MESH_EVENTS (RHI_COMMAND_LIST_DEBUG_TRACES || (WITH_PROFILEGPU && PLATFORM_SUPPORTS_DRAW_MESH_EVENTS))

class FRealtimeGPUProfiler;
class FRealtimeGPUProfilerEvent;
class FRealtimeGPUProfilerFrame;
class FRenderQueryPool;
class FScopedGPUStatEvent;

#if WANTS_DRAW_MESH_EVENTS

	/**
	 * Class that logs draw events based upon class scope. Draw events can be seen
	 * in PIX
	 */
	struct RENDERCORE_API FDrawEvent
	{
		/** Cmdlist to push onto. */
		FRHIComputeCommandList* RHICmdList;

		/** Default constructor, initializing all member variables. */
		FORCEINLINE FDrawEvent()
			: RHICmdList(nullptr)
		{}

		/**
		 * Terminate the event based upon scope
		 */
		FORCEINLINE ~FDrawEvent()
		{
			if (RHICmdList)
			{
				Stop();
			}
		}

		/**
		 * Function for logging a PIX event with var args
		 */
		void CDECL Start(FRHIComputeCommandList& RHICmdList, FColor Color, const TCHAR* Fmt, ...);
		void Stop();
	};

	template <typename TRHICmdList>
	struct TDrawEvent : FDrawEvent {};

	struct RENDERCORE_API FDrawEventRHIExecute
	{
		/** Context to execute on*/
		class IRHIComputeContext* RHICommandContext;

		/** Default constructor, initializing all member variables. */
		FORCEINLINE FDrawEventRHIExecute()
			: RHICommandContext(nullptr)
		{}

		/**
		* Terminate the event based upon scope
		*/
		FORCEINLINE ~FDrawEventRHIExecute()
		{
			if (RHICommandContext)
			{
				Stop();
			}
		}

		/**
		* Function for logging a PIX event with var args
		*/
		void CDECL Start(IRHIComputeContext& InRHICommandContext, FColor Color, const TCHAR* Fmt, ...);
		void Stop();
	};

	#define SCOPED_GPU_EVENT(RHICmdList, Name) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, FColor(0), TEXT(#Name));
	#define SCOPED_GPU_EVENT_COLOR(RHICmdList, Color, Name) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, Color, TEXT(#Name));
	#define SCOPED_GPU_EVENTF(RHICmdList, Name, Format, ...) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, FColor(0), Format, ##__VA_ARGS__);
	#define SCOPED_GPU_EVENTF_COLOR(RHICmdList, Color, Name, Format, ...) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, Color, Format, ##__VA_ARGS__);
	#define SCOPED_CONDITIONAL_GPU_EVENT(RHICmdList, Name, Condition) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents() && (Condition)) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, FColor(0), TEXT(#Name));
	#define SCOPED_CONDITIONAL_GPU_EVENT_COLOR(RHICmdList, Name, Color, Condition) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents() && (Condition)) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, Color, TEXT(#Name));
	#define SCOPED_CONDITIONAL_GPU_EVENTF(RHICmdList, Name, Condition, Format, ...) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents() && (Condition)) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, FColor(0), Format, ##__VA_ARGS__);
	#define SCOPED_CONDITIONAL_GPU_EVENTF_COLOR(RHICmdList, Color, Name, Condition, Format, ...) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents() && (Condition)) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, Color, Format, ##__VA_ARGS__);
	#define BEGIN_GPU_EVENTF(RHICmdList, Name, Event, Format, ...) if(GetEmitDrawEvents()) Event.Start(RHICmdList, FColor(0), Format, ##__VA_ARGS__);
	#define BEGIN_GPU_EVENTF_COLOR(RHICmdList, Color, Name, Event, Format, ...) if(GetEmitDrawEvents()) Event.Start(RHICmdList, Color, Format, ##__VA_ARGS__);
	#define STOP_GPU_EVENT(Event) (Event).Stop();

	// Macros to allow for scoping of draw events outside of RHI function implementations
	#define SCOPED_DRAW_EVENT(RHICmdList, Name) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, FColor(0), TEXT(#Name));
	#define SCOPED_DRAW_EVENT_COLOR(RHICmdList, Color, Name) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, Color, TEXT(#Name));
	#define SCOPED_DRAW_EVENTF(RHICmdList, Name, Format, ...) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, FColor(0), Format, ##__VA_ARGS__);
	#define SCOPED_DRAW_EVENTF_COLOR(RHICmdList, Color, Name, Format, ...) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, Color, Format, ##__VA_ARGS__);
	#define SCOPED_CONDITIONAL_DRAW_EVENT(RHICmdList, Name, Condition) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents() && (Condition)) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, FColor(0), TEXT(#Name));
	#define SCOPED_CONDITIONAL_DRAW_EVENT_COLOR(RHICmdList, Name, Color, Condition) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents() && (Condition)) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, Color, TEXT(#Name));
	#define SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, Name, Condition, Format, ...) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents() && (Condition)) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, FColor(0), Format, ##__VA_ARGS__);
	#define SCOPED_CONDITIONAL_DRAW_EVENTF_COLOR(RHICmdList, Color, Name, Condition, Format, ...) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents() && (Condition)) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdList, Color, Format, ##__VA_ARGS__);
	#define BEGIN_DRAW_EVENTF(RHICmdList, Name, Event, Format, ...) if(GetEmitDrawEvents()) Event.Start(RHICmdList, FColor(0), Format, ##__VA_ARGS__);
	#define BEGIN_DRAW_EVENTF_COLOR(RHICmdList, Color, Name, Event, Format, ...) if(GetEmitDrawEvents()) Event.Start(RHICmdList, Color, Format, ##__VA_ARGS__);
	#define STOP_DRAW_EVENT(Event) (Event).Stop();

	#define SCOPED_COMPUTE_EVENT SCOPED_GPU_EVENT
	#define SCOPED_COMPUTE_EVENT_COLOR SCOPED_GPU_EVENT_COLOR
	#define SCOPED_COMPUTE_EVENTF SCOPED_GPU_EVENTF
	#define SCOPED_COMPUTE_EVENTF_COLOR SCOPED_GPU_EVENTF_COLOR
	#define SCOPED_CONDITIONAL_COMPUTE_EVENT SCOPED_CONDITIONAL_GPU_EVENT
	#define SCOPED_CONDITIONAL_COMPUTE_EVENT_COLOR SCOPED_CONDITIONAL_GPU_EVENT_COLOR
	#define SCOPED_CONDITIONAL_COMPUTE_EVENTF SCOPED_CONDITIONAL_GPU_EVENTF
	#define SCOPED_CONDITIONAL_COMPUTE_EVENTF_COLOR SCOPED_CONDITIONAL_GPU_EVENTF_COLOR

	// Macros to allow for scoping of draw events within RHI function implementations
	#define SCOPED_RHI_DRAW_EVENT(RHICmdContext, Name) FDrawEventRHIExecute PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdContext, FColor(0), TEXT(#Name));
	#define SCOPED_RHI_DRAW_EVENT_COLOR(RHICmdContext, Color, Name) FDrawEventRHIExecute PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdContext, Color, TEXT(#Name));
	#define SCOPED_RHI_DRAW_EVENTF(RHICmdContext, Name, Format, ...) FDrawEventRHIExecute PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdContext, FColor(0), Format, ##__VA_ARGS__);
	#define SCOPED_RHI_DRAW_EVENTF_COLOR(RHICmdContext, Color, Name, Format, ...) FDrawEventRHIExecute PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdContext, Color, Format, ##__VA_ARGS__);
	#define SCOPED_RHI_CONDITIONAL_DRAW_EVENT(RHICmdContext, Name, Condition) FDrawEventRHIExecute PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents() && (Condition)) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdContext, FColor(0), TEXT(#Name));
	#define SCOPED_RHI_CONDITIONAL_DRAW_EVENT_COLOR(RHICmdContext, Color, Name, Condition) FDrawEventRHIExecute PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents() && (Condition)) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdContext, Color, TEXT(#Name));
	#define SCOPED_RHI_CONDITIONAL_DRAW_EVENTF(RHICmdContext, Name, Condition, Format, ...) FDrawEventRHIExecute PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents() && (Condition)) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdContext, FColor(0), Format, ##__VA_ARGS__);
	#define SCOPED_RHI_CONDITIONAL_DRAW_EVENTF_COLOR(RHICmdContext, Color, Name, Condition, Format, ...) FDrawEventRHIExecute PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents() && (Condition)) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(RHICmdContext, Color, Format, ##__VA_ARGS__);

#else

	struct RENDERCORE_API FDrawEvent
	{
	};

	#define SCOPED_GPU_EVENT(...)
	#define SCOPED_GPU_EVENT_COLOR(...)
	#define SCOPED_GPU_EVENTF(...)
	#define SCOPED_GPU_EVENTF_COLOR(...)
	#define SCOPED_CONDITIONAL_GPU_EVENT(...)
	#define SCOPED_CONDITIONAL_GPU_EVENT_COLOR(...)
	#define SCOPED_CONDITIONAL_GPU_EVENTF(...)
	#define SCOPED_CONDITIONAL_GPU_EVENTF_COLOR(...)
	#define BEGIN_GPU_EVENTF(...)
	#define BEGIN_GPU_EVENTF_COLOR(...)
	#define STOP_GPU_EVENT(...)

	#define SCOPED_DRAW_EVENT(...)
	#define SCOPED_DRAW_EVENT_COLOR(...)
	#define SCOPED_DRAW_EVENTF(...)
	#define SCOPED_DRAW_EVENTF_COLOR(...)
	#define SCOPED_CONDITIONAL_DRAW_EVENT(...)
	#define SCOPED_CONDITIONAL_DRAW_EVENT_COLOR(...)
	#define SCOPED_CONDITIONAL_DRAW_EVENTF(...)
	#define SCOPED_CONDITIONAL_DRAW_EVENTF_COLOR(...)
	#define BEGIN_DRAW_EVENTF(...)
	#define BEGIN_DRAW_EVENTF_COLOR(...)
	#define STOP_DRAW_EVENT(...)

	#define SCOPED_COMPUTE_EVENT(RHICmdList, Name)
	#define SCOPED_COMPUTE_EVENT_COLOR(RHICmdList, Name)
	#define SCOPED_COMPUTE_EVENTF(RHICmdList, Name, Format, ...)
	#define SCOPED_COMPUTE_EVENTF_COLOR(RHICmdList, Name)
	#define SCOPED_CONDITIONAL_COMPUTE_EVENT(RHICmdList, Name, Condition)
	#define SCOPED_CONDITIONAL_COMPUTE_EVENT_COLOR(RHICmdList, Name, Condition)
	#define SCOPED_CONDITIONAL_COMPUTE_EVENTF(RHICmdList, Name, Condition, Format, ...)
	#define SCOPED_CONDITIONAL_COMPUTE_EVENTF_COLOR(RHICmdList, Name, Condition)

	#define SCOPED_RHI_DRAW_EVENT(...)
	#define SCOPED_RHI_DRAW_EVENT_COLOR(...)
	#define SCOPED_RHI_DRAW_EVENTF(...)
	#define SCOPED_RHI_DRAW_EVENTF_COLOR(...)
	#define SCOPED_RHI_CONDITIONAL_DRAW_EVENT(...)
	#define SCOPED_RHI_CONDITIONAL_DRAW_EVENT_COLOR(...)
	#define SCOPED_RHI_CONDITIONAL_DRAW_EVENTF(...)
	#define SCOPED_RHI_CONDITIONAL_DRAW_EVENTF_COLOR(...)

#endif

// GPU stats
#ifndef HAS_GPU_STATS
	#if ( STATS || CSV_PROFILER || GPUPROFILERTRACE_ENABLED ) && (!UE_BUILD_SHIPPING)
	#define HAS_GPU_STATS 1
	#else
	#define HAS_GPU_STATS 0
	#endif
#endif

#if HAS_GPU_STATS
 CSV_DECLARE_CATEGORY_MODULE_EXTERN(RENDERCORE_API,GPU);
 // The DECLARE_GPU_STAT macros both declare and define a stat (for use in a single CPP)
 #define DECLARE_GPU_STAT(StatName) DECLARE_FLOAT_COUNTER_STAT(TEXT(#StatName), Stat_GPU_##StatName, STATGROUP_GPU); CSV_DEFINE_STAT(GPU,StatName); FDrawCallCategoryName DrawcallCountCategory_##StatName;
 #define DECLARE_GPU_DRAWCALL_STAT(StatName) DECLARE_FLOAT_COUNTER_STAT(TEXT(#StatName), Stat_GPU_##StatName, STATGROUP_GPU); CSV_DEFINE_STAT(GPU,StatName); FDrawCallCategoryName DrawcallCountCategory_##StatName((TCHAR*)TEXT(#StatName));
 #define DECLARE_GPU_DRAWCALL_STAT_EXTERN(StatName) DECLARE_FLOAT_COUNTER_STAT_EXTERN(TEXT(#StatName), Stat_GPU_##StatName, STATGROUP_GPU, ); CSV_DECLARE_STAT_EXTERN(GPU,StatName); extern FDrawCallCategoryName DrawcallCountCategory_##StatName;
 #define DECLARE_GPU_STAT_NAMED(StatName, NameString) DECLARE_FLOAT_COUNTER_STAT(NameString, Stat_GPU_##StatName, STATGROUP_GPU); CSV_DEFINE_STAT(GPU,StatName); FDrawCallCategoryName DrawcallCountCategory_##StatName;
 #define DECLARE_GPU_DRAWCALL_STAT_NAMED(StatName, NameString) DECLARE_FLOAT_COUNTER_STAT(NameString, Stat_GPU_##StatName, STATGROUP_GPU); CSV_DEFINE_STAT(GPU,StatName); FDrawCallCategoryName DrawcallCountCategory_##StatName((TCHAR*)TEXT(#StatName));

 // Extern GPU stats are needed where a stat is used in multiple CPPs. Use the DECLARE_GPU_STAT_NAMED_EXTERN in the header and DEFINE_GPU_STAT in the CPPs
 #define DECLARE_GPU_STAT_NAMED_EXTERN(StatName, NameString) DECLARE_FLOAT_COUNTER_STAT_EXTERN(NameString, Stat_GPU_##StatName, STATGROUP_GPU, ); CSV_DECLARE_STAT_EXTERN(GPU,StatName); extern FDrawCallCategoryName DrawcallCountCategory_##StatName;
 #define DEFINE_GPU_STAT(StatName) DEFINE_STAT(Stat_GPU_##StatName); CSV_DEFINE_STAT(GPU,StatName); FDrawCallCategoryName DrawcallCountCategory_##StatName;
 #define DEFINE_GPU_DRAWCALL_STAT(StatName) DEFINE_STAT(Stat_GPU_##StatName); CSV_DEFINE_STAT(GPU,StatName); FDrawCallCategoryName DrawcallCountCategory_##StatName((TCHAR*)TEXT(#StatName));
#if STATS
  #define SCOPED_GPU_STAT(RHICmdList, StatName) FScopedGPUStatEvent PREPROCESSOR_JOIN(GPUStatEvent_##StatName,__LINE__); PREPROCESSOR_JOIN(GPUStatEvent_##StatName,__LINE__).Begin(RHICmdList, CSV_STAT_FNAME(StatName), GET_STATID( Stat_GPU_##StatName ).GetName(), &DrawcallCountCategory_##StatName.Counter);
 #else
  #define SCOPED_GPU_STAT(RHICmdList, StatName) FScopedGPUStatEvent PREPROCESSOR_JOIN(GPUStatEvent_##StatName,__LINE__); PREPROCESSOR_JOIN(GPUStatEvent_##StatName,__LINE__).Begin(RHICmdList, CSV_STAT_FNAME(StatName), FName(), &DrawcallCountCategory_##StatName.Counter );
#endif
 #define GPU_STATS_BEGINFRAME(RHICmdList) FRealtimeGPUProfiler::Get()->BeginFrame(RHICmdList);
 #define GPU_STATS_ENDFRAME(RHICmdList) FRealtimeGPUProfiler::Get()->EndFrame(RHICmdList);
#else
 #define DECLARE_GPU_STAT(StatName)
 #define DECLARE_GPU_DRAWCALL_STAT(StatName)
 #define DECLARE_GPU_DRAWCALL_STAT_EXTERN(StatName)
 #define DECLARE_GPU_STAT_NAMED(StatName, NameString)
 #define DECLARE_GPU_DRAWCALL_STAT_NAMED(StatName, NameString)
 #define DECLARE_GPU_STAT_NAMED_EXTERN(StatName, NameString)
 #define DEFINE_GPU_STAT(StatName)
 #define DEFINE_GPU_DRAWCALL_STAT(StatName)
 #define SCOPED_GPU_STAT(RHICmdList, StatName) 
 #define GPU_STATS_BEGINFRAME(RHICmdList) 
 #define GPU_STATS_ENDFRAME(RHICmdList) 
#endif

bool AreGPUStatsEnabled();

#if HAS_GPU_STATS

class FRealtimeGPUProfilerEvent;
class FRealtimeGPUProfilerFrame;
class FRenderQueryPool;

/**
* FRealtimeGPUProfiler class. This manages recording and reporting all for GPU stats
*/
class FRealtimeGPUProfiler
{
	static FRealtimeGPUProfiler* Instance;
public:
	// Singleton interface
	static RENDERCORE_API FRealtimeGPUProfiler* Get();

	/** *Safe release of the singleton */
	static RENDERCORE_API void SafeRelease();

	/** Per-frame update */
	RENDERCORE_API void BeginFrame(FRHICommandListImmediate& RHICmdList);
	RENDERCORE_API void EndFrame(FRHICommandListImmediate& RHICmdList);

	/** Final cleanup */
	UE_DEPRECATED(4.23, "Use FRealtimeGPUProfiler::SafeRelease() instead.")
	RENDERCORE_API void Release();

	/** Push/pop events */
	void PushEvent(FRHICommandListImmediate& RHICmdList, const FName& Name, const FName& StatName);
	void PopEvent(FRHICommandListImmediate& RHICmdList);

private:
	FRealtimeGPUProfiler();

	/** Deinitialize of the object*/
	void Cleanup();


	/** Ringbuffer of profiler frames */
	TArray<FRealtimeGPUProfilerFrame*> Frames;

	int32 WriteBufferIndex;
	int32 ReadBufferIndex;
	uint32 WriteFrameNumber;
	uint32 QueryCount = 0;
	FRenderQueryPoolRHIRef RenderQueryPool;
	bool bStatGatheringPaused;
	bool bInBeginEndBlock;
};

/**
* Class that logs GPU Stat events for the realtime GPU profiler
*/
class FScopedGPUStatEvent
{
	/** Cmdlist to push onto. */
	FRHICommandListImmediate* RHICmdList;

	int32* NumDrawCallsPtr;

public:
	/** Default constructor, initializing all member variables. */
	FORCEINLINE FScopedGPUStatEvent()
		: RHICmdList(nullptr)
		, NumDrawCallsPtr(nullptr)
	{}

	/**
	* Terminate the event based upon scope
	*/
	FORCEINLINE ~FScopedGPUStatEvent()
	{
		if (RHICmdList)
		{
			End();
		}
	}

	/**
	* Start/Stop functions for timer stats
	*/
	RENDERCORE_API void Begin(FRHICommandList& InRHICmdList, const FName& Name, const FName& StatName, int32* InNumDrawCallsPtr);
	RENDERCORE_API void End();
};
#endif // HAS_GPU_STATS
