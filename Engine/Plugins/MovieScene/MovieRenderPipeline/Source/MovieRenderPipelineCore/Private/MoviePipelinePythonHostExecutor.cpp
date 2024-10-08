// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelinePythonHostExecutor.h"
#include "Misc/CoreDelegates.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Engine/Engine.h"

void UMoviePipelinePythonHostExecutor::Execute_Implementation(UMoviePipelineQueue* InPipelineQueue)
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::Game)
		{
			LastLoadedWorld = WorldContext.World();
		}
	}

	PipelineQueue = InPipelineQueue;
	
	// Register C++ only callbacks that we will forward onto BP/Python
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMoviePipelinePythonHostExecutor::OnMapLoadFinished);

	// Now that we've done some C++ only things, call the Python version of this.
	ExecuteDelayed(InPipelineQueue);
}
	
void UMoviePipelinePythonHostExecutor::OnMapLoadFinished(UWorld* NewWorld)
{
	LastLoadedWorld = NewWorld;

	// This executor is only created after the world is loaded
	OnMapLoad(NewWorld);
}