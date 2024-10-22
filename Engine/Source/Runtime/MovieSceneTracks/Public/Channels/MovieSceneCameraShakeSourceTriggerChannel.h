// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/CameraShake.h"
#include "CoreTypes.h"
#include "Curves/KeyHandle.h"
#include "Channels/MovieSceneChannel.h"
#include "Channels/MovieSceneChannelData.h"
#include "Channels/MovieSceneChannelTraits.h"
#include "MovieSceneCameraShakeSourceTriggerChannel.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneCameraShakeSourceTrigger
{
	GENERATED_BODY()

	FMovieSceneCameraShakeSourceTrigger()
		: ShakeClass(nullptr)
		, PlayScale(1.f)
		, PlaySpace(ECameraAnimPlaySpace::CameraLocal)
		, UserDefinedPlaySpace(ForceInitToZero)
	{}

	FMovieSceneCameraShakeSourceTrigger(TSubclassOf<UCameraShake> InShakeClass)
		: ShakeClass(InShakeClass)
		, PlayScale(1.f)
		, PlaySpace(ECameraAnimPlaySpace::CameraLocal)
		, UserDefinedPlaySpace(ForceInitToZero)
	{}

	/** Class of the camera shake to play */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Shake")
	TSubclassOf<UCameraShake> ShakeClass;

	/** Scalar that affects shake intensity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Shake")
	float PlayScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Shake")
	TEnumAsByte<ECameraAnimPlaySpace::Type> PlaySpace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Shake")
	FRotator UserDefinedPlaySpace;
};

USTRUCT()
struct MOVIESCENETRACKS_API FMovieSceneCameraShakeSourceTriggerChannel : public FMovieSceneChannel
{
	GENERATED_BODY()

	FORCEINLINE TMovieSceneChannelData<FMovieSceneCameraShakeSourceTrigger> GetData()
	{
		return TMovieSceneChannelData<FMovieSceneCameraShakeSourceTrigger>(&KeyTimes, &KeyValues, &KeyHandles);
	}

	FORCEINLINE TMovieSceneChannelData<const FMovieSceneCameraShakeSourceTrigger> GetData() const
	{
		return TMovieSceneChannelData<const FMovieSceneCameraShakeSourceTrigger>(&KeyTimes, &KeyValues);
	}

public:
	// ~ FMovieSceneChannel Interface
	virtual void GetKeys(const TRange<FFrameNumber>& WithinRange, TArray<FFrameNumber>* OutKeyTimes, TArray<FKeyHandle>* OutKeyHandles) override;
	virtual void GetKeyTimes(TArrayView<const FKeyHandle> InHandles, TArrayView<FFrameNumber> OutKeyTimes) override;
	virtual void SetKeyTimes(TArrayView<const FKeyHandle> InHandles, TArrayView<const FFrameNumber> InKeyTimes) override;
	virtual void DuplicateKeys(TArrayView<const FKeyHandle> InHandles, TArrayView<FKeyHandle> OutNewHandles) override;
	virtual void DeleteKeys(TArrayView<const FKeyHandle> InHandles) override;
	virtual void DeleteKeysFrom(FFrameNumber InTime, bool bDeleteKeysBefore) override;
	virtual void ChangeFrameResolution(FFrameRate SourceRate, FFrameRate DestinationRate) override;
	virtual TRange<FFrameNumber> ComputeEffectiveRange() const override;
	virtual int32 GetNumKeys() const override;
	virtual void Reset() override;
	virtual void Offset(FFrameNumber DeltaPosition) override;

private:
	/** Array of times for each key */
	UPROPERTY(meta=(KeyTimes))
	TArray<FFrameNumber> KeyTimes;

	/** Array of values that correspond to each key time */
	UPROPERTY(meta=(KeyValues))
	TArray<FMovieSceneCameraShakeSourceTrigger> KeyValues;

	FMovieSceneKeyHandleMap KeyHandles;
};

template<>
struct TMovieSceneChannelTraits<FMovieSceneCameraShakeSourceTriggerChannel> : TMovieSceneChannelTraitsBase<FMovieSceneCameraShakeSourceTriggerChannel>
{
	enum { SupportsDefaults = false };
};

inline bool EvaluateChannel(const FMovieSceneCameraShakeSourceTriggerChannel* InChannel, FFrameTime InTime, FMovieSceneCameraShakeSourceTrigger& OutValue)
{
	return false;
}

