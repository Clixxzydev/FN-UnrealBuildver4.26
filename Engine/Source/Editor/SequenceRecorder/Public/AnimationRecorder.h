// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/GCObject.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimationRecordingSettings.h"
#include "Components/SkinnedMeshComponent.h"
#include "Animation/AnimNotifyQueue.h"
#include "Serializers/MovieSceneAnimationSerialization.h"

class UAnimBoneCompressionSettings;
class UAnimNotify;
class UAnimNotifyState;
class UAnimSequence;
class USkeletalMeshComponent;

DECLARE_LOG_CATEGORY_EXTERN(AnimationSerialization, Verbose, All);

//////////////////////////////////////////////////////////////////////////
// FAnimationRecorder

// records the mesh pose to animation input
struct SEQUENCERECORDER_API FAnimationRecorder : public FGCObject
{
private:
	/** Frame count used to signal an unbounded animation */
	static const int32 UnBoundedFrameCount = -1;

private:
	float IntervalTime;
	int32 MaxFrame;
	int32 LastFrame;
	float TimePassed;
	UAnimSequence* AnimationObject;
	TArray<FTransform> PreviousSpacesBases;
	FBlendedHeapCurve PreviousAnimCurves;
	FTransform PreviousComponentToWorld;
	FTransform InvInitialRootTransform;
	FTransform InitialRootTransform;
	int32 SkeletonRootIndex;

	/** Array of currently active notifies that have duration */
	TArray<TPair<const FAnimNotifyEvent*, bool>> ActiveNotifies;

	/** Unique notifies added to this sequence during recording */
	TMap<UAnimNotify*, UAnimNotify*> UniqueNotifies;

	/** Unique notify states added to this sequence during recording */
	TMap<UAnimNotifyState*, UAnimNotifyState*> UniqueNotifyStates;

	static float DefaultSampleRate;

public:
	FAnimationRecorder();
	~FAnimationRecorder();

	// FGCObject interface start
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	// FGCObject interface end

	/** Starts recording an animation. Prompts for asset path and name via dialog if none provided */
	bool TriggerRecordAnimation(USkeletalMeshComponent* Component);
	bool TriggerRecordAnimation(USkeletalMeshComponent* Component, const FString& AssetPath, const FString& AssetName);

	void StartRecord(USkeletalMeshComponent* Component, UAnimSequence* InAnimationObject);
	UAnimSequence* StopRecord(bool bShowMessage);
	void UpdateRecord(USkeletalMeshComponent* Component, float DeltaTime);
	UAnimSequence* GetAnimationObject() const { return AnimationObject; }
	bool InRecording() const { return AnimationObject != nullptr; }
	float GetTimeRecorded() const { return TimePassed; }

	/** Sets a new sample rate & max length for this recorder. Don't call while recording. */
	void SetSampleRateAndLength(float SampleRateHz, float LengthInMinutes);

	bool SetAnimCompressionScheme(UAnimBoneCompressionSettings* Settings);

	const FTransform& GetInitialRootTransform() const { return InitialRootTransform; }

	/** If true, it will record root to include LocalToWorld */
	uint8 bRecordLocalToWorld :1;
	/** If true, asset will be saved to disk after recording. If false, asset will remain in mem and can be manually saved. */
	uint8 bAutoSaveAsset : 1;
	/** If true, the root bone transform will be removed from all bone transforms */
	uint8 bRemoveRootTransform : 1;
	/** If true we check delta time at beginning of recording */
	uint8 bCheckDeltaTimeAtBeginning : 1;
	/** The interpolation mode for the recorded keys */
	ERichCurveInterpMode InterpMode;
	/** The tangent mode for the recorded keys*/
	ERichCurveTangentMode TangentMode;
	/** Serializer, if set we also store data out incrementally while running*/
	FAnimationSerializer* AnimationSerializer;


private:
	bool Record(USkeletalMeshComponent* Component, FTransform const& ComponentToWorld, const TArray<FTransform>& SpacesBases, const FBlendedHeapCurve& AnimationCurves, int32 FrameToAdd);

	void RecordNotifies(USkeletalMeshComponent* Component, const TArray<FAnimNotifyEventReference>& AnimNotifies, float DeltaTime, float RecordTime);

	void FixupNotifies();

	// recording curve data 
	struct FBlendedCurve
	{
		template<typename Allocator>
		FBlendedCurve(TArray<float, Allocator> CW, TBitArray<Allocator> VCW)
		{
			CurveWeights = CW;
			ValidCurveWeights = VCW;
		}

		TArray<float> CurveWeights;
		TBitArray<> ValidCurveWeights;
	};

	TArray<FBlendedCurve> RecordedCurves;
	TArray<uint16> const * UIDToArrayIndexLUT;
};

//////////////////////////////////////////////////////////////////////////
// FAnimRecorderInstance

struct SEQUENCERECORDER_API FAnimRecorderInstance
{
public:
	FAnimRecorderInstance();
	~FAnimRecorderInstance();

	void Init(USkeletalMeshComponent* InComponent, const FString& InAssetPath, const FString& InAssetName, const FAnimationRecordingSettings& InSettings);

	void Init(USkeletalMeshComponent* InComponent, UAnimSequence* InSequence, FAnimationSerializer *InAnimationSerializer, const FAnimationRecordingSettings& InSettings);

	bool BeginRecording();
	void Update(float DeltaTime);
	void FinishRecording(bool bShowMessage = true);

private:
	void InitInternal(USkeletalMeshComponent* InComponent, const FAnimationRecordingSettings& Settings, FAnimationSerializer *InAnimationSerializer = nullptr);

public:
	TWeakObjectPtr<USkeletalMeshComponent> SkelComp;
	TWeakObjectPtr<UAnimSequence> Sequence;
	FString AssetPath;
	FString AssetName;

	/** Original ForcedLodModel setting on the SkelComp, so we can modify it and restore it when we are done. */
	int CachedSkelCompForcedLodModel;

	TSharedPtr<FAnimationRecorder> Recorder;

	/** Used to store/restore update flag when recording */
	EVisibilityBasedAnimTickOption CachedVisibilityBasedAnimTickOption;

	/** Used to store/restore URO when recording */
	bool bCachedEnableUpdateRateOptimizations;
};


//////////////////////////////////////////////////////////////////////////
// FAnimationRecorderManager

struct SEQUENCERECORDER_API FAnimationRecorderManager
{
public:
	/** Singleton accessor */
	static FAnimationRecorderManager& Get();

	/** Destructor */
	virtual ~FAnimationRecorderManager();

	/** Starts recording an animation. */
	bool RecordAnimation(USkeletalMeshComponent* Component, const FString& AssetPath = FString(), const FString& AssetName = FString(), const FAnimationRecordingSettings& Settings = FAnimationRecordingSettings());

	bool RecordAnimation(USkeletalMeshComponent* Component, UAnimSequence* Sequence, const FAnimationRecordingSettings& Settings = FAnimationRecordingSettings());
	
	bool RecordAnimation(USkeletalMeshComponent* Component, UAnimSequence* Sequence, FAnimationSerializer *InAnimationSerializer,  const FAnimationRecordingSettings& Settings = FAnimationRecordingSettings());

	bool IsRecording(USkeletalMeshComponent* Component);

	bool IsRecording();

	UAnimSequence* GetCurrentlyRecordingSequence(USkeletalMeshComponent* Component);
	float GetCurrentRecordingTime(USkeletalMeshComponent* Component);
	void StopRecordingAnimation(USkeletalMeshComponent* Component, bool bShowMessage = true);
	void StopRecordingAllAnimations();
	const FTransform& GetInitialRootTransform(USkeletalMeshComponent* Component) const;

	void Tick(float DeltaTime);

	void Tick(USkeletalMeshComponent* Component, float DeltaTime);

	void StopRecordingDeadAnimations(bool bShowMessage = true);

private:
	/** Constructor, private - use Get() function */
	FAnimationRecorderManager();

	TArray<FAnimRecorderInstance> RecorderInstances;

	void HandleEndPIE(bool bSimulating);
};

