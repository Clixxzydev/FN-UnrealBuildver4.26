// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "MovieRenderPipelineDataTypes.h"
#include "LevelSequence.h"
#include "MoviePipelineMasterConfig.h"
#include "MoviePipelineShotConfig.h"
#include "MoviePipelineConfigBase.h"

#include "MoviePipelineQueue.generated.h"

class UMoviePipelineMasterConfig;
class ULevel;
class ULevelSequence;

/**
* This class represents a segment of work within the Executor Job. This should be owned
* by the UMoviePipelineExecutorJob and can be created before the movie pipeline starts to
* configure some aspects about the segment (such as disabling it). When the movie pipeline
* starts, it will use the already existing ones, or generate new ones as needed.
*/
UCLASS(BlueprintType)
class MOVIERENDERPIPELINECORE_API UMoviePipelineExecutorShot : public UObject
{
	GENERATED_BODY()
public:
	UMoviePipelineExecutorShot()
		: bEnabled(true)
	{
		Progress = 0.f;
	}

public:
	/**
	* Set the status of this shot to the given value. This will be shown on the UI if progress
	* is set to a value less than zero. If progress is > 0 then the progress bar will be shown
	* on the UI instead. Progress and Status Message are cosmetic.
	*
	* For C++ implementations override `virtual void SetStatusMessage_Implementation() override`
	* For Python/BP implementations override
	*	@unreal.ufunction(override=True)
	*	def set_status_message(self, inStatus):
	*
	* @param InStatus	The status message you wish the executor to have.
	*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Movie Render Pipeline")
	void SetStatusMessage(const FString& InStatus);

	/**
	* Get the current status message for this shot. May be an empty string.
	*
	* For C++ implementations override `virtual FString GetStatusMessage_Implementation() override`
	* For Python/BP implementations override
	*	@unreal.ufunction(override=True)
	*	def get_status_message(self):
	*		return ?
	*/
	UFUNCTION(BlueprintPure, BlueprintNativeEvent, Category = "Movie Render Pipeline")
	FString GetStatusMessage() const;

	/**
	* Set the progress of this shot to the given value. If a positive value is provided
	* the UI will show the progress bar, while a negative value will make the UI show the
	* status message instead. Progress and Status Message are cosmetic and dependent on the
	* executor to update. Similar to the UMoviePipelineExecutor::SetStatusProgress function,
	* but at a per-job level basis instead.
	*
	* For C++ implementations override `virtual void SetStatusProgress_Implementation() override`
	* For Python/BP implementations override
	*	@unreal.ufunction(override=True)
	*	def set_status_progress(self, inStatus):
	*
	* @param InProgress	The progress (0-1 range) the executor should have.
	*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Movie Render Pipeline")
	void SetStatusProgress(const float InProgress);

	/**
	* Get the current progress as last set by SetStatusProgress. 0 by default.
	*
	* For C++ implementations override `virtual float GetStatusProgress_Implementation() override`
	* For Python/BP implementations override
	*	@unreal.ufunction(override=True)
	*	def get_status_progress(self):
	*		return ?
	*/
	UFUNCTION(BlueprintPure, BlueprintNativeEvent, Category = "Movie Render Pipeline")
	float GetStatusProgress() const;

protected:
	// UMoviePipipelineExecutorShot Interface
	virtual void SetStatusMessage_Implementation(const FString& InMessage) { StatusMessage = InMessage; }
	virtual void SetStatusProgress_Implementation(const float InProgress) { Progress = InProgress; }
	virtual FString GetStatusMessage_Implementation() const { return StatusMessage; }
	virtual float GetStatusProgress_Implementation() const { return Progress; }
	// ~UMoviePipipelineExecutorShot Interface

public:

	/** Should this shot be rendered? */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Render Pipeline")
	bool bEnabled;

	/** Soft object path to uniquley identify this shot. Both Inner and Outer path are compared. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Render Pipeline")
	FSoftObjectPath InnerPathKey;

	/** Soft object path to uniquley identify this shot. Both Inner and Outer path are compared. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Render Pipeline")
	FSoftObjectPath OuterPathKey;

	/** The name of the shot section that contains this shot. Can be empty. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Render Pipeline")
	FString OuterName;

	/** The name of the camera cut section that this shot represents. Can be empty. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Render Pipeline")
	FString InnerName;

	UPROPERTY(BlueprintReadWrite, Category = "Movie Render Pipeline")
	UMoviePipelineShotConfig* ShotOverrideConfig;
public:
	/** Transient information used by the active Movie Pipeline working on this shot. */
	FMoviePipelineCameraCutInfo ShotInfo;

protected:
	UPROPERTY(Transient)
	float Progress;
	UPROPERTY(Transient)
	FString StatusMessage;
};

/**
* A particular job within the Queue
*/
UCLASS(BlueprintType)
class MOVIERENDERPIPELINECORE_API UMoviePipelineExecutorJob : public UObject
{
	GENERATED_BODY()
public:
	UMoviePipelineExecutorJob()
	{
		StatusProgress = 0.f;
		bIsConsumed = false;
		Configuration = CreateDefaultSubobject<UMoviePipelineMasterConfig>("DefaultConfig");
	}

public:	
	/**
	* Set the status of this job to the given value. This will be shown on the UI if progress
	* is set to a value less than zero. If progress is > 0 then the progress bar will be shown
	* on the UI instead. Progress and Status Message are cosmetic and dependent on the
	* executor to update. Similar to the UMoviePipelineExecutor::SetStatusMessage function,
	* but at a per-job level basis instead. 
	*
	* For C++ implementations override `virtual void SetStatusMessage_Implementation() override`
	* For Python/BP implementations override
	*	@unreal.ufunction(override=True)
	*	def set_status_message(self, inStatus):
	*
	* @param InStatus	The status message you wish the executor to have.
	*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Movie Render Pipeline")
	void SetStatusMessage(const FString& InStatus);

	/**
	* Get the current status message for this job. May be an empty string.
	*
	* For C++ implementations override `virtual FString GetStatusMessage_Implementation() override`
	* For Python/BP implementations override
	*	@unreal.ufunction(override=True)
	*	def get_status_message(self):
	*		return ?
	*/
	UFUNCTION(BlueprintPure, BlueprintNativeEvent, Category = "Movie Render Pipeline")
	FString GetStatusMessage() const;

	/**
	* Set the progress of this job to the given value. If a positive value is provided
	* the UI will show the progress bar, while a negative value will make the UI show the 
	* status message instead. Progress and Status Message are cosmetic and dependent on the
	* executor to update. Similar to the UMoviePipelineExecutor::SetStatusProgress function,
	* but at a per-job level basis instead.
	*
	* For C++ implementations override `virtual void SetStatusProgress_Implementation() override`
	* For Python/BP implementations override
	*	@unreal.ufunction(override=True)
	*	def set_status_progress(self, inProgress):
	*
	* @param InProgress	The progress (0-1 range) the executor should have.
	*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Movie Render Pipeline")
	void SetStatusProgress(const float InProgress);

	/**
	* Get the current progress as last set by SetStatusProgress. 0 by default.
	*
	* For C++ implementations override `virtual float GetStatusProgress_Implementation() override`
	* For Python/BP implementations override
	*	@unreal.ufunction(override=True)
	*	def get_status_progress(self):
	*		return ?
	*/
	UFUNCTION(BlueprintPure, BlueprintNativeEvent, Category = "Movie Render Pipeline")
	float GetStatusProgress() const;

	/**
	* Set the job to be consumed. A consumed job is disabled in the UI and should not be
	* submitted for rendering again. This allows jobs to be added to a queue, the queue
	* submitted to a remote farm (consume the jobs) and then more jobs to be added and
	* the second submission to the farm won't re-submit the already in-progress jobs.
	*
	* Jobs can be unconsumed when the render finishes to re-enable editing.
	*
	* For C++ implementations override `virtual void SetConsumed_Implementation() override`
	* For Python/BP implementations override
	*	@unreal.ufunction(override=True)
	*	def set_consumed(self, isConsumed):
	*
	* @param bInConsumed	True if the job should be consumed and disabled for editing in the UI.
	*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Movie Render Pipeline")
	void SetConsumed(const bool bInConsumed);

	/**
	* Gets whether or not the job has been marked as being consumed. A consumed job is not editable
	* in the UI and should not be submitted for rendering as it is either already finished or
	* already in progress.
	*
	* For C++ implementations override `virtual bool IsConsumed_Implementation() override`
	* For Python/BP implementations override
	*	@unreal.ufunction(override=True)
	*	def is_consumed(self):
	*		return ?
	*/
	UFUNCTION(BlueprintPure, BlueprintNativeEvent, Category = "Movie Render Pipeline")
	bool IsConsumed() const;

	/**
	* Should be called to clear status and user data after duplication so that jobs stay
	* unique and don't pick up ids or other unwanted behavior from the pareant job.
	*
	* For C++ implementations override `virtual bool OnDuplicated_Implementation() override`
	* For Python/BP implementations override
	*	@unreal.ufunction(override=True)
	*	def on_duplicated(self):
	*/
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Movie Render Pipeline")
	void OnDuplicated();

	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	void SetPresetOrigin(UMoviePipelineMasterConfig* InPreset);

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	UMoviePipelineMasterConfig* GetPresetOrigin() const
	{
		return PresetOrigin.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	UMoviePipelineMasterConfig* GetConfiguration() const
	{
		return Configuration;
	}

	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	void SetConfiguration(UMoviePipelineMasterConfig* InPreset);

	UFUNCTION(BlueprintSetter, Category = "Movie Render Pipeline")
	void SetSequence(FSoftObjectPath InSequence);

public:
	// UObject Interface
#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
#endif
	// ~UObject Interface

protected:
	// UMoviePipelineExecutorJob Interface
	virtual void SetStatusMessage_Implementation(const FString& InMessage) { StatusMessage = InMessage; }
	virtual void SetStatusProgress_Implementation(const float InProgress) { StatusProgress = InProgress; }
	virtual void SetConsumed_Implementation(const bool bInConsumed) { bIsConsumed = bInConsumed; }
	virtual FString GetStatusMessage_Implementation() const { return StatusMessage; }
	virtual float GetStatusProgress_Implementation() const { return StatusProgress; }
	virtual bool IsConsumed_Implementation() const { return bIsConsumed; }
	virtual void OnDuplicated_Implementation();
	// ~UMoviePipelineExecutorJob Interface

public:
	/** (Optional) Name of the job. Shown on the default burn-in. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Render Pipeline")
	FString JobName;

	/** Which sequence should this job render? */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, BlueprintSetter = "SetSequence", Category = "Movie Render Pipeline", meta = (AllowedClasses = "LevelSequence"))
	FSoftObjectPath Sequence;

	/** Which map should this job render on */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Render Pipeline", meta = (AllowedClasses = "World"))
	FSoftObjectPath Map;

	/** (Optional) Name of the person who submitted the job. Can be shown in burn in as a first point of contact about the content. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Render Pipeline")
	FString Author;

	/** (Optional) Shot specific information. If a shot is missing from this list it will assume to be enabled and will be rendered. */
	UPROPERTY(BlueprintReadWrite, Instanced, Category = "Movie Render Pipeline")
	TArray<UMoviePipelineExecutorShot*> ShotInfo;

	/** 
	* Arbitrary data that can be associated with the job. Not used by default implementations, nor read.
	* This can be used to attach third party metadata such as job ids from remote farms. 
	* Not shown in the user interface.
	*/
	UPROPERTY(BlueprintReadWrite, Category = "Movie Render Pipeline")
	FString UserData;
private:
	UPROPERTY(Transient)
	FString StatusMessage;
	UPROPERTY(Transient)
	float StatusProgress;
	UPROPERTY(Transient)
	bool bIsConsumed;
private:
	/** 
	*/
	UPROPERTY(Instanced)
	UMoviePipelineMasterConfig* Configuration;

	/**
	*/
	UPROPERTY()
	TSoftObjectPtr<UMoviePipelineMasterConfig> PresetOrigin;
};

/**
* A queue is a list of jobs that have been executed, are executing and are waiting to be executed. These can be saved
* to specific assets to allow 
*/
UCLASS(BlueprintType)
class MOVIERENDERPIPELINECORE_API UMoviePipelineQueue : public UObject
{
	GENERATED_BODY()
public:
	
	UMoviePipelineQueue()
		: QueueSerialNumber(0)
	{
		// Ensure instances are always transactional
		if (!HasAnyFlags(RF_ClassDefaultObject))
		{
			SetFlags(RF_Transactional);
		}
	}
	
	/**
	* Allocates a new Job in this Queue. The Queue owns the jobs for memory management purposes,
	* and this will handle that for you. 
	*
	* @param InJobType	Specify the specific Job type that should be created. Custom Executors can use custom Job types to allow the user to provide more information.
	* @return	The created Executor job instance.
	*/
	UFUNCTION(BlueprintCallable, meta = (DeterminesOutputType = "InClass"), Category = "Movie Render Pipeline|Queue", meta=(InJobType="/Script/MovieRenderPipelineCore.MoviePipelineExecutorJob"))
	UMoviePipelineExecutorJob* AllocateNewJob(TSubclassOf<UMoviePipelineExecutorJob> InJobType);

	/**
	* Deletes the specified job from the Queue. 
	*
	* @param InJob	The job to look for and delete. 
	*/
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline|Queue")
	void DeleteJob(UMoviePipelineExecutorJob* InJob);

	/**
	* Duplicate the specific job and return the duplicate. Configurations are duplicated and not shared.
	*
	* @param InJob	The job to look for to duplicate.
	* @return The duplicated instance or nullptr if a duplicate could not be made.
	*/
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline|Queue")
	UMoviePipelineExecutorJob* DuplicateJob(UMoviePipelineExecutorJob* InJob);
	
	/**
	* Get all of the Jobs contained in this Queue.
	* @return The jobs contained by this queue.
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline|Queue")
	TArray<UMoviePipelineExecutorJob*> GetJobs() const
	{
		return Jobs;
	}

	/** 
	* Replace the contents of this queue with a copy of the contents from another queue. 
	*/
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	void CopyFrom(UMoviePipelineQueue* InQueue);
	
	/**
	 * Retrieve the serial number that is incremented when a job is added or removed from this list.
	 * @note: This field is not serialized, and not copied along with UObject duplication.
	 */
	uint32 GetQueueSerialNumber() const
	{
		return QueueSerialNumber;
	}

	void InvalidateSerialNumber()
	{
		QueueSerialNumber++;
	}

private:
	UPROPERTY(Instanced)
	TArray<UMoviePipelineExecutorJob*> Jobs;
	
private:
	int32 QueueSerialNumber;
};