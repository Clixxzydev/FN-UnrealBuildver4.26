// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterSceneComponentSyncThis.h"

#include "GameFramework/Actor.h"


UDisplayClusterSceneComponentSyncThis::UDisplayClusterSceneComponentSyncThis(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Children of UDisplayClusterSceneComponent must always Tick to be able to process VRPN tracking
	PrimaryComponentTick.bCanEverTick = true;
}

void UDisplayClusterSceneComponentSyncThis::BeginPlay()
{
	Super::BeginPlay();
}


void UDisplayClusterSceneComponentSyncThis::TickComponent( float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent( DeltaTime, TickType, ThisTickFunction );
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterClusterSyncObject
//////////////////////////////////////////////////////////////////////////////////////////////
bool UDisplayClusterSceneComponentSyncThis::IsDirty() const
{
	return (LastSyncLoc != GetRelativeLocation() || LastSyncRot != GetRelativeRotation() || LastSyncScale != GetRelativeScale3D());
}

void UDisplayClusterSceneComponentSyncThis::ClearDirty()
{
	LastSyncLoc = GetRelativeLocation();
	LastSyncRot = GetRelativeRotation();
	LastSyncScale = GetRelativeScale3D();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// UDisplayClusterSceneComponentSync
//////////////////////////////////////////////////////////////////////////////////////////////
FString UDisplayClusterSceneComponentSyncThis::GenerateSyncId()
{
	return FString::Printf(TEXT("ST_%s"), *GetOwner()->GetName());
}

FTransform UDisplayClusterSceneComponentSyncThis::GetSyncTransform() const
{
	return GetRelativeTransform();
}

void UDisplayClusterSceneComponentSyncThis::SetSyncTransform(const FTransform& t)
{
	SetRelativeTransform(t);
}
