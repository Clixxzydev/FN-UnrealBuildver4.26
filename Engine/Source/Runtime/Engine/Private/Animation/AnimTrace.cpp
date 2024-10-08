// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimTrace.h"

#if ANIM_TRACE_ENABLED

#include "Trace/Trace.inl"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimInstanceProxy.h"
#include "ObjectTrace.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/CommandLine.h"
#include "Engine/SkeletalMesh.h"
#include "Math/TransformNonVectorized.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpaceBase.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimTypes.h"
#include "TraceFilter.h"

UE_TRACE_CHANNEL_DEFINE(AnimationChannel);

UE_TRACE_EVENT_BEGIN(Animation, TickRecord)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, AssetId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(float, BlendWeight)
	UE_TRACE_EVENT_FIELD(float, PlaybackTime)
	UE_TRACE_EVENT_FIELD(float, RootMotionWeight)
	UE_TRACE_EVENT_FIELD(float, PlayRate)
	UE_TRACE_EVENT_FIELD(float, BlendSpacePositionX)
	UE_TRACE_EVENT_FIELD(float, BlendSpacePositionY)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
	UE_TRACE_EVENT_FIELD(bool, Looping)
	UE_TRACE_EVENT_FIELD(bool, IsBlendSpace)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, SkeletalMesh2, Important)
	UE_TRACE_EVENT_FIELD(uint64, Id)
	UE_TRACE_EVENT_FIELD(int32[], ParentIndices)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, SkeletalMeshComponent2)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, ComponentId)
	UE_TRACE_EVENT_FIELD(uint64, MeshId)
	UE_TRACE_EVENT_FIELD(float[], ComponentToWorld)
	UE_TRACE_EVENT_FIELD(float[], Pose)
	UE_TRACE_EVENT_FIELD(uint32[], CurveIds)
	UE_TRACE_EVENT_FIELD(float[], CurveValues)
	UE_TRACE_EVENT_FIELD(uint16, LodIndex)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, SkeletalMeshFrame)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, ComponentId)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimGraph)
	UE_TRACE_EVENT_FIELD(uint64, StartCycle)
	UE_TRACE_EVENT_FIELD(uint64, EndCycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeCount)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
	UE_TRACE_EVENT_FIELD(uint8, Phase)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeStart)
	UE_TRACE_EVENT_FIELD(uint64, StartCycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, PreviousNodeId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(float, Weight)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
	UE_TRACE_EVENT_FIELD(uint8, Phase)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeEnd)
	UE_TRACE_EVENT_FIELD(uint64, EndCycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueBool)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
	UE_TRACE_EVENT_FIELD(bool, Value)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueInt)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(int32, Value)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueFloat)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(float, Value)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueVector2D)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(float, ValueX)
	UE_TRACE_EVENT_FIELD(float, ValueY)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueVector)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(float, ValueX)
	UE_TRACE_EVENT_FIELD(float, ValueY)
	UE_TRACE_EVENT_FIELD(float, ValueZ)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueString)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueObject)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, Value)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimNodeValueClass)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, Value)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, KeyLength)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, AnimSequencePlayer)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(float, Position)
	UE_TRACE_EVENT_FIELD(float, Length)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, BlendSpacePlayer)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, BlendSpaceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(float, PositionX)
	UE_TRACE_EVENT_FIELD(float, PositionY)
	UE_TRACE_EVENT_FIELD(float, PositionZ)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, StateMachineState)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(int32, NodeId)
	UE_TRACE_EVENT_FIELD(int32, StateMachineIndex)
	UE_TRACE_EVENT_FIELD(int32, StateIndex)
	UE_TRACE_EVENT_FIELD(float, StateWeight)
	UE_TRACE_EVENT_FIELD(float, ElapsedTime)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, Name, Important)
	UE_TRACE_EVENT_FIELD(uint32, Id)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, Notify)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, AssetId)
	UE_TRACE_EVENT_FIELD(uint64, NotifyId)
	UE_TRACE_EVENT_FIELD(uint32, NameId)
	UE_TRACE_EVENT_FIELD(float, Time)
	UE_TRACE_EVENT_FIELD(float, Duration)
	UE_TRACE_EVENT_FIELD(uint8, NotifyEventType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, SyncMarker)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint32, NameId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Animation, Montage)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, AnimInstanceId)
	UE_TRACE_EVENT_FIELD(uint64, MontageId)
	UE_TRACE_EVENT_FIELD(uint32, CurrentSectionNameId)
	UE_TRACE_EVENT_FIELD(uint32, NextSectionNameId)
	UE_TRACE_EVENT_FIELD(float, Weight)
	UE_TRACE_EVENT_FIELD(float, DesiredWeight)
	UE_TRACE_EVENT_FIELD(uint16, FrameCounter)
UE_TRACE_EVENT_END()

// Object annotations used for tracing
FUObjectAnnotationSparseBool GSkeletalMeshTraceAnnotations;

// Map used for unique name output
TMap<FName, uint32> GAnimTraceNames;

// Global unique name index
uint32 GAnimTraceCurrentNameId = 1;

// Critical section used to lock global name map & index
FCriticalSection GAnimTraceNameCriticalSection;

// Scratch buffers for various traces to avoid allocation churn.
// These can be removed when lambda support is added for array fields to remove a memcpy.
struct FAnimTraceScratchBuffers : public TThreadSingleton<FAnimTraceScratchBuffers>
{
	// Curve values/IDs for skeletal mesh component
	TArray<float> CurveValues;
	TArray<uint32> CurveIds;

	// Parent indices for skeletal meshes
	TArray<int32> ParentIndices;
};

class FSuspendCounter : public TThreadSingleton<FSuspendCounter>
{
public:
	FSuspendCounter()
		: SuspendCount(0)
	{}

	int32 SuspendCount;
};

FAnimTrace::FScopedAnimNodeTraceSuspend::FScopedAnimNodeTraceSuspend()
{
	FSuspendCounter::Get().SuspendCount++;
}

FAnimTrace::FScopedAnimNodeTraceSuspend::~FScopedAnimNodeTraceSuspend()
{
	FSuspendCounter::Get().SuspendCount--;
	check(FSuspendCounter::Get().SuspendCount >= 0);
}

FAnimTrace::FScopedAnimNodeTrace::FScopedAnimNodeTrace(const FAnimationInitializeContext& InContext)
	: Context(InContext)
{
	if(FSuspendCounter::Get().SuspendCount == 0)
	{
		OutputAnimNodeStart(InContext, FPlatformTime::Cycles64(), InContext.GetPreviousNodeId(), InContext.GetCurrentNodeId(), 0.0f, 0.0f, (__underlying_type(EPhase))EPhase::Initialize);
	}
}

FAnimTrace::FScopedAnimNodeTrace::FScopedAnimNodeTrace(const FAnimationUpdateContext& InContext)
	: Context(InContext)
{
	if(FSuspendCounter::Get().SuspendCount == 0)
	{
		OutputAnimNodeStart(InContext, FPlatformTime::Cycles64(), InContext.GetPreviousNodeId(), InContext.GetCurrentNodeId(), InContext.GetFinalBlendWeight(), InContext.GetRootMotionWeightModifier(), (__underlying_type(EPhase))EPhase::Update);
	}
}

FAnimTrace::FScopedAnimNodeTrace::FScopedAnimNodeTrace(const FAnimationCacheBonesContext& InContext)
	: Context(InContext)
{
	if(FSuspendCounter::Get().SuspendCount == 0)
	{
		OutputAnimNodeStart(InContext, FPlatformTime::Cycles64(), InContext.GetPreviousNodeId(), InContext.GetCurrentNodeId(), 0.0f, 0.0f, (__underlying_type(EPhase))EPhase::CacheBones);
	}
}

FAnimTrace::FScopedAnimNodeTrace::FScopedAnimNodeTrace(const FPoseContext& InContext)
	: Context(InContext)
{
	if(FSuspendCounter::Get().SuspendCount == 0)
	{
		OutputAnimNodeStart(InContext, FPlatformTime::Cycles64(), InContext.GetPreviousNodeId(), InContext.GetCurrentNodeId(), 0.0f, 0.0f, (__underlying_type(EPhase))EPhase::Evaluate);
	}
}

FAnimTrace::FScopedAnimNodeTrace::FScopedAnimNodeTrace(const FComponentSpacePoseContext& InContext)
	: Context(InContext)
{
	if(FSuspendCounter::Get().SuspendCount == 0)
	{
		OutputAnimNodeStart(InContext, FPlatformTime::Cycles64(), InContext.GetPreviousNodeId(), InContext.GetCurrentNodeId(), 0.0f, 0.0f, (__underlying_type(EPhase))EPhase::Evaluate);
	}
}

FAnimTrace::FScopedAnimNodeTrace::~FScopedAnimNodeTrace()
{
	if(FSuspendCounter::Get().SuspendCount == 0)
	{
		OutputAnimNodeEnd(Context, FPlatformTime::Cycles64());
	}
}

FAnimTrace::FScopedAnimGraphTrace::FScopedAnimGraphTrace(const FAnimationInitializeContext& InContext)
	: StartCycle(FPlatformTime::Cycles64())
	, Context(InContext)
	, Phase(EPhase::Initialize)
{}

FAnimTrace::FScopedAnimGraphTrace::FScopedAnimGraphTrace(const FAnimationUpdateContext& InContext)
	: StartCycle(FPlatformTime::Cycles64())
	, Context(InContext)
	, Phase(EPhase::Update)
{}

FAnimTrace::FScopedAnimGraphTrace::FScopedAnimGraphTrace(const FAnimationCacheBonesContext& InContext)
	: StartCycle(FPlatformTime::Cycles64())
	, Context(InContext)
	, Phase(EPhase::CacheBones)
{}

FAnimTrace::FScopedAnimGraphTrace::FScopedAnimGraphTrace(const FPoseContext& InContext)
	: StartCycle(FPlatformTime::Cycles64())
	, Context(InContext)
	, Phase(EPhase::Evaluate)
{}

FAnimTrace::FScopedAnimGraphTrace::FScopedAnimGraphTrace(const FComponentSpacePoseContext& InContext)
	: StartCycle(FPlatformTime::Cycles64())
	, Context(InContext)
	, Phase(EPhase::Evaluate)
{}

FAnimTrace::FScopedAnimGraphTrace::~FScopedAnimGraphTrace()
{
	OutputAnimGraph(Context, StartCycle, FPlatformTime::Cycles64(), (__underlying_type(EPhase))Phase);
}

void FAnimTrace::OutputAnimTickRecord(const FAnimationBaseContext& InContext, const FAnimTickRecord& InTickRecord)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	if (CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent()))
	{
		return;
	}

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);

	if(InTickRecord.SourceAsset)
	{
		TRACE_OBJECT(InTickRecord.SourceAsset);

		float PlaybackTime = *InTickRecord.TimeAccumulator;
		if(InTickRecord.SourceAsset->IsA<UAnimMontage>())
		{
			PlaybackTime = InTickRecord.Montage.CurrentPosition;
		}

		float BlendSpacePositionX = 0.0f;
		float BlendSpacePositionY = 0.0f;
		const bool bIsBlendSpace = InTickRecord.SourceAsset->IsA<UBlendSpaceBase>();
		if(bIsBlendSpace)
	{
			BlendSpacePositionX = InTickRecord.BlendSpace.BlendSpacePositionX;
			BlendSpacePositionY = InTickRecord.BlendSpace.BlendSpacePositionY;
	}

		UE_TRACE_LOG(Animation, TickRecord, AnimationChannel)
			<< TickRecord.Cycle(FPlatformTime::Cycles64())
			<< TickRecord.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
			<< TickRecord.AssetId(FObjectTrace::GetObjectId(InTickRecord.SourceAsset))
			<< TickRecord.NodeId(InContext.GetCurrentNodeId())
			<< TickRecord.BlendWeight(InTickRecord.EffectiveBlendWeight)
			<< TickRecord.PlaybackTime(PlaybackTime)
			<< TickRecord.RootMotionWeight(InTickRecord.RootMotionWeightModifier)
			<< TickRecord.PlayRate(InTickRecord.PlayRateMultiplier)
			<< TickRecord.BlendSpacePositionX(BlendSpacePositionX)
			<< TickRecord.BlendSpacePositionY(BlendSpacePositionY)
			<< TickRecord.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(AnimInstance))
			<< TickRecord.Looping(InTickRecord.bLooping)
			<< TickRecord.IsBlendSpace(bIsBlendSpace);
	}
}

void FAnimTrace::OutputSkeletalMesh(const USkeletalMesh* InMesh)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled || InMesh == nullptr)
	{
		return;
	}

	if(GSkeletalMeshTraceAnnotations.Get(InMesh))
	{
		return;
	}

	TRACE_OBJECT(InMesh);

	uint32 BoneCount = (uint32)InMesh->RefSkeleton.GetNum();

	TArray<int32>& ParentIndices = FAnimTraceScratchBuffers::Get().ParentIndices;
	ParentIndices.Reset();
	ParentIndices.SetNumUninitialized(BoneCount);

	int32 BoneIndex = 0;
	for(const FMeshBoneInfo& BoneInfo : InMesh->RefSkeleton.GetRefBoneInfo())
	{
		ParentIndices[BoneIndex++] = BoneInfo.ParentIndex;
	}

	UE_TRACE_LOG(Animation, SkeletalMesh2, AnimationChannel)
		<< SkeletalMesh2.Id(FObjectTrace::GetObjectId(InMesh))
		<< SkeletalMesh2.ParentIndices(ParentIndices.GetData(), ParentIndices.Num());

	GSkeletalMeshTraceAnnotations.Set(InMesh);
}

uint32 FAnimTrace::OutputName(const FName& InName)
{
	uint32 NameId = 0;
	bool bShouldTrace = false;
	if(InName != NAME_None)
	{
		FScopeLock ScopeLock(&GAnimTraceNameCriticalSection);

		uint32* ExistingIdPtr = GAnimTraceNames.Find(InName);
		if(ExistingIdPtr == nullptr)
		{
			NameId = GAnimTraceCurrentNameId++;
			GAnimTraceNames.Add(InName, NameId);
			bShouldTrace = true;
		}
		else
		{
			NameId = *ExistingIdPtr;
		}
	}

	if(bShouldTrace)
	{
		int32 NameStringLength = InName.GetStringLength() + 1;

		auto StringCopyFunc = [NameStringLength, &InName](uint8* Out)
		{
			InName.ToString(reinterpret_cast<TCHAR*>(Out), NameStringLength);
		};

		UE_TRACE_LOG(Animation, Name, AnimationChannel, NameStringLength * sizeof(TCHAR))
			<< Name.Id(NameId)
			<< Name.Attachment(StringCopyFunc);
	}

	return NameId;
}

void FAnimTrace::OutputSkeletalMeshComponent(const USkeletalMeshComponent* InComponent)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled || InComponent == nullptr)
	{
		return;
	}

	if (CANNOT_TRACE_OBJECT(InComponent))
	{
		return;
	}

	int32 BoneCount = InComponent->GetComponentSpaceTransforms().Num();
	int32 CurveCount = 0;
	UAnimInstance* AnimInstance = InComponent->GetAnimInstance();

	if(AnimInstance)
	{
		for(EAnimCurveType CurveType : TEnumRange<EAnimCurveType>())
		{
			CurveCount += AnimInstance->GetAnimationCurveList(CurveType).Num();
		}
	}
	
	if(BoneCount > 0 || CurveCount > 0)
	{
		TRACE_OBJECT(InComponent);
		TRACE_SKELETAL_MESH(InComponent->SkeletalMesh);

		TArray<float>& CurveValues = FAnimTraceScratchBuffers::Get().CurveValues;
		CurveValues.Reset();
		CurveValues.SetNumUninitialized(CurveCount);
		TArray<uint32>& CurveIds = FAnimTraceScratchBuffers::Get().CurveIds;
		CurveIds.Reset();
		CurveIds.SetNumUninitialized(CurveCount);

		if(CurveCount > 0 && AnimInstance)
		{
			int32 CurveIndex = 0;
			for(EAnimCurveType CurveType : TEnumRange<EAnimCurveType>())
			{
				for(TPair<FName, float> CurvePair : AnimInstance->GetAnimationCurveList(CurveType))
				{
					CurveIds[CurveIndex] = OutputName(CurvePair.Key);
					CurveValues[CurveIndex] = CurvePair.Value;
					CurveIndex++;
				}
			}
		}

		UE_TRACE_LOG(Animation, SkeletalMeshComponent2, AnimationChannel)
			<< SkeletalMeshComponent2.Cycle(FPlatformTime::Cycles64())
			<< SkeletalMeshComponent2.ComponentId(FObjectTrace::GetObjectId(InComponent))
			<< SkeletalMeshComponent2.MeshId(FObjectTrace::GetObjectId(InComponent->SkeletalMesh))
			<< SkeletalMeshComponent2.ComponentToWorld(reinterpret_cast<const float*>(&InComponent->GetComponentToWorld()), sizeof(FTransform) / sizeof(float))
			<< SkeletalMeshComponent2.Pose(reinterpret_cast<const float*>(InComponent->GetComponentSpaceTransforms().GetData()), BoneCount * (sizeof(FTransform) / sizeof(float)))
			<< SkeletalMeshComponent2.CurveIds(CurveIds.GetData(), CurveIds.Num())
			<< SkeletalMeshComponent2.CurveValues(CurveValues.GetData(), CurveValues.Num())
			<< SkeletalMeshComponent2.LodIndex((uint16)InComponent->PredictedLODLevel)
			<< SkeletalMeshComponent2.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(InComponent));
	}
}

void FAnimTrace::OutputSkeletalMeshFrame(const USkeletalMeshComponent* InComponent)
{
	TRACE_OBJECT(InComponent);
	UE_TRACE_LOG(Animation, SkeletalMeshFrame, AnimationChannel)
		<< SkeletalMeshFrame.Cycle(FPlatformTime::Cycles64())
		<< SkeletalMeshFrame.ComponentId(FObjectTrace::GetObjectId(InComponent))
		<< SkeletalMeshFrame.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(InComponent));
}

void FAnimTrace::OutputAnimGraph(const FAnimationBaseContext& InContext, uint64 InStartCycle, uint64 InEndCycle, uint8 InPhase)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	if (CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent()))
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	const UAnimInstance* AnimInstance = Cast<UAnimInstance>(InContext.AnimInstanceProxy->GetAnimInstanceObject());
	const UAnimBlueprintGeneratedClass* BPClass = Cast<UAnimBlueprintGeneratedClass>(AnimInstance->GetClass());

	TRACE_OBJECT(AnimInstance);

	UE_TRACE_LOG(Animation, AnimGraph, AnimationChannel)
		<< AnimGraph.StartCycle(InStartCycle)
		<< AnimGraph.EndCycle(InEndCycle)
		<< AnimGraph.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
		<< AnimGraph.NodeCount(BPClass ? BPClass->GetAnimNodeProperties().Num() : 0)
		<< AnimGraph.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(AnimInstance))
		<< AnimGraph.Phase(InPhase);
}

void FAnimTrace::OutputAnimNodeStart(const FAnimationBaseContext& InContext, uint64 InStartCycle, int32 InPreviousNodeId, int32 InNodeId, float InBlendWeight, float InRootMotionWeight, uint8 InPhase)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	if(InNodeId == INDEX_NONE)
	{
		return;
	}

	if (CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent()))
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);

	FString DisplayNameString;
	IAnimClassInterface* AnimBlueprintClass = InContext.GetAnimClass();
	if(AnimBlueprintClass)
	{
		const TArray<FStructProperty*>& AnimNodeProperties = AnimBlueprintClass->GetAnimNodeProperties();
		check(AnimNodeProperties.IsValidIndex(InNodeId));
		FStructProperty* LinkedProperty = AnimNodeProperties[InNodeId];
		check(LinkedProperty->Struct);

#if WITH_EDITOR
		DisplayNameString = LinkedProperty->Struct->GetDisplayNameText().ToString();
#else
		DisplayNameString = LinkedProperty->Struct->GetName();
#endif

		DisplayNameString.RemoveFromStart(TEXT("Anim Node "));
	}
	else
	{
		DisplayNameString = TEXT("Anim Node");
	}

	check(InPreviousNodeId != InNodeId);

	UE_TRACE_LOG(Animation, AnimNodeStart, AnimationChannel, (DisplayNameString.Len() + 1) * sizeof(TCHAR))
		<< AnimNodeStart.StartCycle(InStartCycle)
		<< AnimNodeStart.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
		<< AnimNodeStart.PreviousNodeId(InPreviousNodeId)
		<< AnimNodeStart.NodeId(InNodeId)
		<< AnimNodeStart.Weight(InBlendWeight)
		<< AnimNodeStart.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(AnimInstance))
		<< AnimNodeStart.Phase(InPhase)
		<< AnimNodeStart.Attachment(*DisplayNameString, (DisplayNameString.Len() + 1) * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimNodeEnd(const FAnimationBaseContext& InContext, uint64 InEndCycle)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	if (CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent()))
	{
		return;
	}

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);

	UE_TRACE_LOG(Animation, AnimNodeEnd, AnimationChannel)
		<< AnimNodeEnd.EndCycle(InEndCycle)
		<< AnimNodeEnd.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance));
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, bool InValue)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	if (CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent()))
	{
		return;
	}

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueBool, AnimationChannel, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueBool.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueBool.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
		<< AnimNodeValueBool.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueBool.KeyLength(KeyLength)
		<< AnimNodeValueBool.Value(InValue)
		<< AnimNodeValueBool.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(AnimInstance))
		<< AnimNodeValueBool.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, int32 InValue)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	if (CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent()))
	{
		return;
	}

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueInt, AnimationChannel, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueInt.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueInt.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
		<< AnimNodeValueInt.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueInt.KeyLength(KeyLength)
		<< AnimNodeValueInt.Value(InValue)
		<< AnimNodeValueInt.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(AnimInstance))
		<< AnimNodeValueInt.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, float InValue)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	if (CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent()))
	{
		return;
	}

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueFloat, AnimationChannel, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueFloat.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueFloat.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
		<< AnimNodeValueFloat.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueFloat.KeyLength(KeyLength)
		<< AnimNodeValueFloat.Value(InValue)
		<< AnimNodeValueFloat.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(AnimInstance))
		<< AnimNodeValueFloat.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const FVector2D& InValue)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueVector2D, AnimationChannel, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueVector2D.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueVector2D.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
		<< AnimNodeValueVector2D.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueVector2D.KeyLength(KeyLength)
		<< AnimNodeValueVector2D.ValueX(InValue.X)
		<< AnimNodeValueVector2D.ValueY(InValue.Y)
		<< AnimNodeValueVector2D.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(AnimInstance))
		<< AnimNodeValueVector2D.Attachment(InKey, KeyLength * sizeof(TCHAR));
}


void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const FRotator& Value)
{
	const FVector VectorValue(Value.Roll, Value.Pitch, Value.Yaw);
	OutputAnimNodeValue(InContext, InKey, VectorValue);
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const FVector& InValue)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	if (CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent()))
	{
		return;
	}

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueVector, AnimationChannel, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueVector.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueVector.AnimInstanceId(FObjectTrace::GetObjectId(InContext.AnimInstanceProxy->GetAnimInstanceObject()))
		<< AnimNodeValueVector.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueVector.KeyLength(KeyLength)
		<< AnimNodeValueVector.ValueX(InValue.X)
		<< AnimNodeValueVector.ValueY(InValue.Y)
		<< AnimNodeValueVector.ValueZ(InValue.Z)
		<< AnimNodeValueVector.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(AnimInstance))
		<< AnimNodeValueVector.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const FName& InValue)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	if (CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent()))
	{
		return;
	}

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);

	int32 KeyLength = FCString::Strlen(InKey) + 1;
	int32 ValueLength = InValue.GetStringLength() + 1;

	auto StringCopyFunc = [KeyLength, ValueLength, InKey, &InValue](uint8* Out)
	{
		FCString::Strncpy(reinterpret_cast<TCHAR*>(Out), InKey, KeyLength);
		InValue.ToString(reinterpret_cast<TCHAR*>(Out) + KeyLength, ValueLength);
	};

	UE_TRACE_LOG(Animation, AnimNodeValueString, AnimationChannel, (KeyLength + ValueLength) * sizeof(TCHAR))
		<< AnimNodeValueString.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueString.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
		<< AnimNodeValueString.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueString.KeyLength(KeyLength)
		<< AnimNodeValueString.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(AnimInstance))
		<< AnimNodeValueString.Attachment(StringCopyFunc);
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const TCHAR* InValue)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	if (CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent()))
	{
		return;
	}

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);

	int32 KeyLength = FCString::Strlen(InKey) + 1;
	int32 ValueLength = FCString::Strlen(InValue) + 1;

	auto StringCopyFunc = [KeyLength, ValueLength, InKey, InValue](uint8* Out)
	{
		FCString::Strncpy(reinterpret_cast<TCHAR*>(Out), InKey, KeyLength);
		FCString::Strncpy(reinterpret_cast<TCHAR*>(Out) + KeyLength, InValue, ValueLength);
	};

	UE_TRACE_LOG(Animation, AnimNodeValueString, AnimationChannel, (KeyLength + ValueLength) * sizeof(TCHAR))
		<< AnimNodeValueString.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueString.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
		<< AnimNodeValueString.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueString.KeyLength(KeyLength)
		<< AnimNodeValueString.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(AnimInstance))
		<< AnimNodeValueString.Attachment(StringCopyFunc);
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const UObject* InValue)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	if (CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent()))
	{
		return;
	}

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);
	TRACE_OBJECT(InValue);

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueObject, AnimationChannel, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueObject.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueObject.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
		<< AnimNodeValueObject.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueObject.Value(FObjectTrace::GetObjectId(InValue))
		<< AnimNodeValueObject.KeyLength(KeyLength)
		<< AnimNodeValueObject.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(AnimInstance))
		<< AnimNodeValueObject.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimNodeValue(const FAnimationBaseContext& InContext, const TCHAR* InKey, const UClass* InValue)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	if (CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent()))
	{
		return;
	}

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);
	TRACE_CLASS(InValue);

	int32 KeyLength = FCString::Strlen(InKey) + 1;

	UE_TRACE_LOG(Animation, AnimNodeValueClass, AnimationChannel, KeyLength * sizeof(TCHAR))
		<< AnimNodeValueClass.Cycle(FPlatformTime::Cycles64())
		<< AnimNodeValueClass.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
		<< AnimNodeValueClass.NodeId(InContext.GetCurrentNodeId())
		<< AnimNodeValueClass.Value(FObjectTrace::GetObjectId(InValue))
		<< AnimNodeValueClass.KeyLength(KeyLength)
		<< AnimNodeValueClass.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(AnimInstance))
		<< AnimNodeValueClass.Attachment(InKey, KeyLength * sizeof(TCHAR));
}

void FAnimTrace::OutputAnimSequencePlayer(const FAnimationBaseContext& InContext, const FAnimNode_SequencePlayer& InNode)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	if (CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent()))
	{
		return;
	}

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);

	UE_TRACE_LOG(Animation, AnimSequencePlayer, AnimationChannel)
		<< AnimSequencePlayer.Cycle(FPlatformTime::Cycles64())
		<< AnimSequencePlayer.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
		<< AnimSequencePlayer.NodeId(InContext.GetCurrentNodeId())
		<< AnimSequencePlayer.Position(InNode.GetAccumulatedTime())
		<< AnimSequencePlayer.Length(InNode.Sequence ? InNode.Sequence->SequenceLength : 0.0f)
		<< AnimSequencePlayer.FrameCounter(InNode.Sequence ? InNode.Sequence->GetNumberOfFrames() : 0);
}

void FAnimTrace::OutputStateMachineState(const FAnimationBaseContext& InContext, int32 InStateMachineIndex, int32 InStateIndex, float InStateWeight, float InElapsedTime)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	check(InContext.AnimInstanceProxy);

	if (CANNOT_TRACE_OBJECT(InContext.AnimInstanceProxy->GetSkelMeshComponent()))
	{
		return;
	}

	UObject* AnimInstance = InContext.AnimInstanceProxy->GetAnimInstanceObject();
	TRACE_OBJECT(AnimInstance);

	UE_TRACE_LOG(Animation, StateMachineState, AnimationChannel)
		<< StateMachineState.Cycle(FPlatformTime::Cycles64())
		<< StateMachineState.AnimInstanceId(FObjectTrace::GetObjectId(AnimInstance))
		<< StateMachineState.NodeId(InContext.GetCurrentNodeId())
		<< StateMachineState.StateMachineIndex(InStateMachineIndex)
		<< StateMachineState.StateIndex(InStateIndex)
		<< StateMachineState.StateWeight(InStateWeight)
		<< StateMachineState.ElapsedTime(InElapsedTime);
}

void FAnimTrace::OutputAnimNotify(UAnimInstance* InAnimInstance, const FAnimNotifyEvent& InNotifyEvent, ENotifyEventType InEventType)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}
	
	if (CANNOT_TRACE_OBJECT(InAnimInstance->GetSkelMeshComponent()))
	{
		return;
	}

	TRACE_OBJECT(InAnimInstance);

	const UObject* NotifyObject = nullptr;
	const UObject* NotifyAsset = nullptr;
	if(InNotifyEvent.Notify)
	{
		NotifyObject = InNotifyEvent.Notify;
		NotifyAsset = NotifyObject->GetOuter();
	}
	else if(InNotifyEvent.NotifyStateClass)
	{
		NotifyObject = InNotifyEvent.NotifyStateClass;
		NotifyAsset = NotifyObject->GetOuter();
	}

	TRACE_OBJECT(NotifyAsset);
	TRACE_OBJECT(NotifyObject);

	const uint32 NameId = OutputName(InNotifyEvent.NotifyName);

	UE_TRACE_LOG(Animation, Notify, AnimationChannel)
		<< Notify.Cycle(FPlatformTime::Cycles64())
		<< Notify.AnimInstanceId(FObjectTrace::GetObjectId(InAnimInstance))
		<< Notify.AssetId(FObjectTrace::GetObjectId(NotifyAsset))
		<< Notify.NotifyId(FObjectTrace::GetObjectId(NotifyObject))
		<< Notify.NameId(NameId)
		<< Notify.Time(InNotifyEvent.GetTime())
		<< Notify.Duration(InNotifyEvent.GetDuration())
		<< Notify.NotifyEventType((uint8)InEventType);
}

void FAnimTrace::OutputAnimSyncMarker(UAnimInstance* InAnimInstance, const FPassedMarker& InPassedSyncMarker)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	if (CANNOT_TRACE_OBJECT(InAnimInstance->GetSkelMeshComponent()))
	{
		return;
	}

	TRACE_OBJECT(InAnimInstance);

	const uint32 NameId = OutputName(InPassedSyncMarker.PassedMarkerName);

	UE_TRACE_LOG(Animation, SyncMarker, AnimationChannel)
		<< SyncMarker.Cycle(FPlatformTime::Cycles64())
		<< SyncMarker.AnimInstanceId(FObjectTrace::GetObjectId(InAnimInstance))
		<< SyncMarker.NameId(NameId);
}

void FAnimTrace::OutputMontage(UAnimInstance* InAnimInstance, const FAnimMontageInstance& InMontageInstance)
{
	bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(AnimationChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	if(InMontageInstance.Montage != nullptr)
	{
		if (CANNOT_TRACE_OBJECT(InAnimInstance->GetSkelMeshComponent()))
		{
			return;
		}

		TRACE_OBJECT(InAnimInstance);
		TRACE_OBJECT(InMontageInstance.Montage);

		const uint32 CurrentSectionNameId = OutputName(InMontageInstance.GetCurrentSection());
		const uint32 NextSectionNameId = OutputName(InMontageInstance.GetNextSection());

		UE_TRACE_LOG(Animation, Montage, AnimationChannel)
			<< Montage.Cycle(FPlatformTime::Cycles64())
			<< Montage.AnimInstanceId(FObjectTrace::GetObjectId(InAnimInstance))
			<< Montage.MontageId(FObjectTrace::GetObjectId(InMontageInstance.Montage))
			<< Montage.CurrentSectionNameId(CurrentSectionNameId)
			<< Montage.NextSectionNameId(NextSectionNameId)
			<< Montage.Weight(InMontageInstance.GetWeight())
			<< Montage.DesiredWeight(InMontageInstance.GetDesiredWeight())
			<< Montage.FrameCounter(FObjectTrace::GetObjectWorldTickCounter(InAnimInstance));
	}
}

#endif