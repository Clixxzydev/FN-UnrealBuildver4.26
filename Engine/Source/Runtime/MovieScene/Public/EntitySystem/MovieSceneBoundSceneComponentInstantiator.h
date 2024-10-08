// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"

#include "MovieSceneBoundSceneComponentInstantiator.generated.h"

UCLASS()
class MOVIESCENE_API UMovieSceneBoundSceneComponentInstantiator : public UMovieSceneEntityInstantiatorSystem
{
	GENERATED_BODY()

	UMovieSceneBoundSceneComponentInstantiator(const FObjectInitializer& ObjInit);

private:

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;
};

