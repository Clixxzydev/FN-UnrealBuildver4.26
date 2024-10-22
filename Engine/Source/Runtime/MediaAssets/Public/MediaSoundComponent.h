// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SynthComponent.h"

#include "Containers/Array.h"
#include "HAL/CriticalSection.h"
#include "MediaSampleQueue.h"
#include "Misc/Timespan.h"
#include "Templates/Atomic.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
#include "DSP/SpectrumAnalyzer.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/EnvelopeFollower.h"
#include "Sound/SoundClass.h"

#include "MediaSoundComponent.generated.h"

class FMediaAudioResampler;
class FMediaPlayerFacade;
class IMediaAudioSample;
class IMediaPlayer;
class UMediaPlayer;


/**
 * Available media sound channel types.
 */
UENUM()
enum class EMediaSoundChannels
{
	/** Mono (1 channel). */
	Mono,

	/** Stereo (2 channels). */
	Stereo,

	/** Surround sound (7.1 channels; for UI). */
	Surround
};

UENUM(BlueprintType)
enum class EMediaSoundComponentFFTSize : uint8
{
	Min_64,
	Small_256,
	Medium_512,
	Large_1024,
};

USTRUCT(BlueprintType)
struct FMediaSoundComponentSpectralData
{
	GENERATED_USTRUCT_BODY()

	// The frequency hz of the spectrum value
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpectralData")
	float FrequencyHz;

	// The magnitude of the spectrum at this frequency
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpectralData")
	float Magnitude;
};

/**
 * Implements a sound component for playing a media player's audio output.
 */
UCLASS(ClassGroup=Media, editinlinenew, meta=(BlueprintSpawnableComponent))
class MEDIAASSETS_API UMediaSoundComponent
	: public USynthComponent
{
	GENERATED_BODY()

public:

	/** Media sound channel type. */
	UPROPERTY(EditAnywhere, Category="Media")
	EMediaSoundChannels Channels;

	/** Dynamically adjust the sample rate if audio and media clock desynchronize. */
	UPROPERTY(EditAnywhere, Category="Media", AdvancedDisplay)
	bool DynamicRateAdjustment;

	/**
	 * Factor for calculating the sample rate adjustment.
	 *
	 * If dynamic rate adjustment is enabled, this number is multiplied with the drift
	 * between the audio and media clock (in 100ns ticks) to determine the adjustment.
	 * that is to be multiplied into the current playrate.
	 */
	UPROPERTY(EditAnywhere, Category="Media", AdvancedDisplay)
	float RateAdjustmentFactor;

	/**
	 * The allowed range of dynamic rate adjustment.
	 *
	 * If dynamic rate adjustment is enabled, and the necessary adjustment
	 * falls outside of this range, audio samples will be dropped.
	 */
	UPROPERTY(EditAnywhere, Category="Media", AdvancedDisplay)
	FFloatRange RateAdjustmentRange;

public:

	/**
	 * Create and initialize a new instance.
	 *
	 * @param ObjectInitializer Initialization parameters.
	 */
	UMediaSoundComponent(const FObjectInitializer& ObjectInitializer);

	/** Virtual destructor. */
	~UMediaSoundComponent();

public:

	/**
	 * Get the attenuation settings based on the current component settings.
	 *
	 * @param OutAttenuationSettings Will contain the attenuation settings, if available.
	 * @return true if attenuation settings were returned, false if attenuation is disabled.
	 */
	UFUNCTION(BlueprintCallable, Category="Media|MediaSoundComponent", meta=(DisplayName="Get Attenuation Settings To Apply", ScriptName="GetAttenuationSettingsToApply"))
	bool BP_GetAttenuationSettingsToApply(FSoundAttenuationSettings& OutAttenuationSettings);

	/**
	 * Get the media player that provides the audio samples.
	 *
	 * @return The component's media player, or nullptr if not set.
	 * @see SetMediaPlayer
	 */
	UFUNCTION(BlueprintCallable, Category="Media|MediaSoundComponent")
	UMediaPlayer* GetMediaPlayer() const;

	/**
	 * Set the media player that provides the audio samples.
	 *
	 * @param NewMediaPlayer The player to set.
	 * @see GetMediaPlayer
	 */
	UFUNCTION(BlueprintCallable, Category="Media|MediaSoundComponent")
	void SetMediaPlayer(UMediaPlayer* NewMediaPlayer);

	/** Turns on spectral analysis of the audio generated in the media sound component. */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaSoundComponent")
	void SetEnableSpectralAnalysis(bool bInSpectralAnalysisEnabled);
	
	/** Sets the settings to use for spectral analysis. */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaSoundComponent")
	void SetSpectralAnalysisSettings(TArray<float> InFrequenciesToAnalyze, EMediaSoundComponentFFTSize InFFTSize = EMediaSoundComponentFFTSize::Medium_512);

	/** Retrieves the spectral data if spectral analysis is enabled. */
	UFUNCTION(BlueprintCallable, Category = "TimeSynth")
	TArray<FMediaSoundComponentSpectralData> GetSpectralData();

	/** Turns on amplitude envelope following the audio in the media sound component. */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaSoundComponent")
	void SetEnableEnvelopeFollowing(bool bInEnvelopeFollowing);

	/** Sets the envelope attack and release times (in ms). */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaSoundComponent")
	void SetEnvelopeFollowingsettings(int32 AttackTimeMsec, int32 ReleaseTimeMsec);

	/** Retrieves the current amplitude envelope. */
	UFUNCTION(BlueprintCallable, Category = "TimeSynth")
	float GetEnvelopeValue() const;

public:

	void UpdatePlayer();

#if WITH_EDITOR
	/**
	 * Set the component's default media player property.
	 *
	 * @param NewMediaPlayer The player to set.
	 * @see SetMediaPlayer
	 */
	void SetDefaultMediaPlayer(UMediaPlayer* NewMediaPlayer);
#endif

public:

	//~ TAttenuatedComponentVisualizer interface

	void CollectAttenuationShapesForVisualization(TMultiMap<EAttenuationShape::Type, FBaseAttenuationSettings::AttenuationShapeDetails>& ShapeDetailsMap) const;

public:

	//~ UActorComponent interface

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

public:

	//~ USceneComponent interface

	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;

public:

	//~ UObject interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:

	/**
	 * Get the attenuation settings based on the current component settings.
	 *
	 * @return Attenuation settings, or nullptr if attenuation is disabled.
	 */
	const FSoundAttenuationSettings* GetSelectedAttenuationSettings() const;

protected:

	//~ USynthComponent interface

	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

protected:

	/**
	 * The media player asset associated with this component.
	 *
	 * This property is meant for design-time convenience. To change the
	 * associated media player at run-time, use the SetMediaPlayer method.
	 *
	 * @see SetMediaPlayer
	 */
	UPROPERTY(EditAnywhere, Category="Media")
	UMediaPlayer* MediaPlayer;

private:

	/** The player's current play rate (cached for use on audio thread). */
	TAtomic<float> CachedRate;

	/** The player's current time (cached for use on audio thread). */
	TAtomic<FTimespan> CachedTime;

	/** Critical section for synchronizing access to PlayerFacadePtr. */
	FCriticalSection CriticalSection;

	/** The player that is currently associated with this component. */
	TWeakObjectPtr<UMediaPlayer> CurrentPlayer;

	/** The player facade that's currently providing texture samples. */
	TWeakPtr<FMediaPlayerFacade, ESPMode::ThreadSafe> CurrentPlayerFacade;

	/** Adjusts the output sample rate to synchronize audio and media clock. */
	float RateAdjustment;

	/** The audio resampler. */
	FMediaAudioResampler* Resampler;

	/** Audio sample queue. */
	TSharedPtr<FMediaAudioSampleQueue, ESPMode::ThreadSafe> SampleQueue;

	/* Time of last sample played. */
	TAtomic<FTimespan> LastPlaySampleTime;

	/** Which frequencies to analyze. */
	TArray<float> FrequenciesToAnalyze;

	/** The FFT bin-size to use for FFT analysis. Smaller sizes make it more reactive but less accurate in the frequency space. */
	EMediaSoundComponentFFTSize FFTSize;

	/** Spectrum analyzer used for analyzing audio in media. */
	Audio::FSpectrumAnalyzer SpectrumAnalyzer;
	Audio::FSpectrumAnalyzerSettings SpectrumAnalyzerSettings;

	Audio::FEnvelopeFollower EnvelopeFollower;
	int32 EnvelopeFollowerAttackTime;
	int32 EnvelopeFollowerReleaseTime;
	float CurrentEnvelopeValue;
	FCriticalSection EnvelopeFollowerCriticalSection;

	/** Scratch buffer to mix in source audio to from decoder */
	Audio::AlignedFloatBuffer AudioScratchBuffer;

	/** Whether or not spectral analysis is enabled. */
	bool bSpectralAnalysisEnabled;

	/** Whether or not envelope following is enabled. */
	bool bEnvelopeFollowingEnabled;

	/** Whether or not envelope follower settings changed. */
	bool bEnvelopeFollowerSettingsChanged;

private:

	static USoundClass* DefaultMediaSoundClassObject;
};
