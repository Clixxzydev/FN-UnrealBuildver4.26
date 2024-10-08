// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MovieRenderPipelineDataTypes.h"

#include "MoviePipelineBlueprintLibrary.generated.h"

// Forward Declare
class UMoviePipeline;
class UMovieSceneSequence;

UCLASS(meta = (ScriptName = "MoviePipelineLibrary"))
class MOVIERENDERPIPELINECORE_API UMoviePipelineBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	* Duplicates the specified sequence using a medium depth copy. Standard duplication will only duplicate
	* the top level Sequence (since shots and sub-sequences are other standalone assets) so this function
	* recursively duplicates the given sequence, shot and subsequence and then fixes up the references to
	* point to newly duplicated sequences.
	*
	* Use at your own risk. Some features may not work when duplicated (complex object binding arrangements,
	* blueprint GetSequenceBinding nodes, etc.) but can be useful when wanting to create a bunch of variations
	* with minor differences (such as swapping out an actor, track, etc.)
	*
	* This does not duplicate any assets that the sequence points to outside of Shots/Subsequences.
	*
	* @param	Outer		- The Outer of the newly duplicated object. Leave null for TransientPackage();
	* @param	InSequence	- The sequence to recursively duplicate.
	* @return				- The duplicated sequence, or null if no sequence was provided to duplicate.
	*/
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	static UMovieSceneSequence* DuplicateSequence(UObject* Outer, UMovieSceneSequence* InSequence);

	/**
	* Get the estimated amount of time remaining for the current pipeline. Based on looking at the total
	* amount of samples to render vs. how many have been completed so far. Inaccurate when Time Dilation
	* is used, and gets more accurate over the course of the render.
	*
	* @param	InPipeline	The pipeline to get the time estimate from.
	* @param	OutEstimate	The resulting estimate, or FTimespan() if estimate is not valid.
	* @return				True if a valid estimate can be calculated, or false if it is not ready yet (ie: not enough samples rendered)
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static bool GetEstimatedTimeRemaining(const UMoviePipeline* InPipeline, FTimespan& OutEstimate);

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static FDateTime GetJobInitializationTime(const UMoviePipeline* InMoviePipeline);

	/**
	* Get the current state of the specified Pipeline. See EMovieRenderPipelineState for more detail about each state.
	*
	* @param	InPipeline	The pipeline to get the state for.
	* @return				Current State.
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static EMovieRenderPipelineState GetPipelineState(const UMoviePipeline* InPipeline);

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static EMovieRenderShotState GetCurrentSegmentState(UMoviePipeline* InMoviePipeline);

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static FText GetJobName(UMoviePipeline* InMoviePipeline);

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static FText GetJobAuthor(UMoviePipeline* InMoviePipeline);


	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static void GetOverallOutputFrames(const UMoviePipeline* InMoviePipeline, int32& OutCurrentIndex, int32& OutTotalCount);

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static FText GetCurrentSegmentName(UMoviePipeline* InMoviePipeline);

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static void GetOverallSegmentCounts(const UMoviePipeline* InMoviePipeline, int32& OutCurrentIndex, int32& OutTotalCount);

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static FMoviePipelineSegmentWorkMetrics GetCurrentSegmentWorkMetrics(const UMoviePipeline* InMoviePipeline);

	/** Gets the completion percent of the Pipeline in 0-1 */
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static float GetCompletionPercentage(const UMoviePipeline* InPipeline);

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static FTimecode GetMasterTimecode(const UMoviePipeline* InMoviePipeline);

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static FFrameNumber GetMasterFrameNumber(const UMoviePipeline* InMoviePipeline);

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static FTimecode GetCurrentShotTimecode(const UMoviePipeline* InMoviePipeline);

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static FFrameNumber GetCurrentShotFrameNumber(const UMoviePipeline* InMoviePipeline);

	/** Get the package name for the map in this job. The level travel command requires the package path and not the asset path. */
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static FString GetMapPackageName(UMoviePipelineExecutorJob* InJob);

	/** Scan the provided sequence in the job to see which camera cut sections we would try to render and update the job's shotlist. */
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	static void UpdateJobShotListFromSequence(ULevelSequence* InSequence, UMoviePipelineExecutorJob* InJob);
	
	/** If version number is manually specifies, returns that, otherwise search the Output Directory for the highest version already existing an increments it by one. */
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	static int32 ResolveVersionNumber(const UMoviePipeline* InMoviePipeline);
};