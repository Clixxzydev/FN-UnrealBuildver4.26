// Copyright Epic Games, Inc. All Rights Reserved

#pragma once
#include "Engine/EngineTypes.h"
#include "Misc/StringBuilder.h"
#include "NetworkPredictionStateTypes.h"
#include "NetworkPredictionSimulation.h"
#include "NetworkPredictionTickState.h"
#include "NetworkPredictionReplicationProxy.h"
#include "BaseMovementSimulation.h"

class UAnimInstance;

// Very crude parameter pack for root motion parameters. The idea being each root motion source can have source-defined
// parameters. This version just works on a block of memory without safety or optimizations (NetSerialize does not quantize anything)
template<int32 InlineSize=128>
struct TMockParameterPack
{
	TArray<uint8, TInlineAllocator<128>> Data;

	void NetSerialize(const FNetSerializeParams& P)
	{
		npCheckf(Data.Num() <= 255, TEXT("Parameter size too big %d"), Data.Num());
		
		if(P.Ar.IsSaving())
		{	
			uint8 Size = Data.Num();
			P.Ar << Size;
			P.Ar.Serialize(Data.GetData(), Data.Num());
		}
		else
		{
			uint8 Size = 0;
			P.Ar << Size;
			Data.SetNumUninitialized(Size, false);
			P.Ar.Serialize(Data.GetData(), Size);
		}
	}

	void ToString(FAnsiStringBuilderBase& Out) const
	{
		Out.Appendf("ParameterPack Size: %d", Data.Num());
	}

	template<typename T>
	void SetByType(const T* RawData)
	{
		Data.SetNumUninitialized(sizeof(T), false);
		FMemory::Memcpy(Data.GetData(), RawData, sizeof(T));
	}

	template<typename T>
	const T* GetByType() const
	{
		if (npEnsureMsgf(Data.Num() == sizeof(T), TEXT("Parameter size %d does not match Type size: %d"), Data.Num(), sizeof(T)))
		{
			return (T*)Data.GetData();
		}
		return nullptr;
	}

	bool operator==(const TMockParameterPack<InlineSize> &Other) const
	{
		return Data.Num() == Other.Data.Num() && FMemory::Memcmp(Data.GetData(), Other.Data.GetData(), Data.Num()) == 0;
	}

	bool operator!=(const TMockParameterPack<InlineSize> &Other) const { return !(*this == Other); }
};



// This is an initial prototype of root motion in the Network Prediction system. It is meant to flesh out some ideas before 
// settling on a final design for the future of root motion. In other words, we do not expect the code here in NetworkPredictionExtas
// to be used directly in shipping systems.

// High level idea:
//	-Get montage based root motion stood up
//	-Expand on the idea of "Root Motion Sources" meaning any kind of motion-driving logic that can be decoupled from the "character/pawn movement system"
//	-This would include simple curve based motions, programatically defined motion ("move towards actor"), or more complex, dynamic animation based motion.
//	-Eventually this folds back into the "new movement system" and/or possibly becomes something that can stand on its own without being driven by the former (TBD).

// Issues / Callouts:
//	-UObject* replication inside NP sync/aux state is not supported (Without disabling WithNetSharedSerialization)
//	
//

struct FMockRootMotionInputCmd
{
	// State that is generated by the client. Strictly speaking for RootMotion, an InputCmd doesn't
	// make sense - input is the concern of the higher level system that would decide to play 
	// RootMotions. For this mock example though, we'll make an InputCmd that can trigger an animation
	// to play from the client. That way, the client can initiate an animation predictively.
	//
	// The real world example would be more like "InputCmd says activate an abilty, the ability says
	// to play a montage".

	int32	PlaySourceID = INDEX_NONE;	// Which RootMotionSourceID to trigger
	int32	PlayCount = 0;				// Counter - to allow back to back playing of same anim

	TMockParameterPack<> Parameters;

	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << PlaySourceID;
		P.Ar << PlayCount;

		Parameters.NetSerialize(P);
	}

	void ToString(FAnsiStringBuilderBase& Out) const
	{
		Out.Appendf("PlaySourceID: %d\n", PlaySourceID);
		Out.Appendf("PlayCount: %d\n", PlayCount);

		Parameters.ToString(Out);
	}
};

struct FMockRootMotionSyncState
{
	// Transform state. In the final version we may want to decouple this from the animation state,
	// For example if a "movement simulation" was driving things, it may "own" the transform
	// and feed it into the root motion system. But this is meant to be a stand alone mock example.
	FVector Location;
	FRotator Rotation;

	// ---------------------------------------------
	// Core Root Motion state
	// ---------------------------------------------

	// Maps to the actual thing driving root motion. Initially this will map to a UAnimMontage,
	// but we really want this to be able to map to anything that can drive motion.
	int32	RootMotionSourceID = INDEX_NONE;

	// The root motion state for this instance. This is hard coded for montages right now.
	// We could instead allocate a generic block of memory for the RootMotionSourceID to 
	// use however it wants. This would allow different root motion sources to have different
	// internal state (PlayPosition) and different parameterization (PlayRate).

	float	PlayPosition = 0.f;
	float	PlayRate = 0.f;

	// Counter to catch new input cmds
	int32	InputPlayCount = 0;

	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << Location;
		P.Ar << Rotation;

		P.Ar << RootMotionSourceID;
		P.Ar << PlayPosition;
		P.Ar << PlayRate;
	}
	void ToString(FAnsiStringBuilderBase& Out) const
	{
		Out.Appendf("Loc: X=%.2f Y=%.2f Z=%.2f\n", Location.X, Location.Y, Location.Z);
		Out.Appendf("Rot: P=%.2f Y=%.2f R=%.2f\n", Rotation.Pitch, Rotation.Yaw, Rotation.Roll);

		Out.Appendf("RootMotionSourceID: %d\n", RootMotionSourceID);
		Out.Appendf("PlayPosition: %.2f\n", PlayPosition);
		Out.Appendf("PlayRate: %.2f\n", PlayRate);
	}

	void Interpolate(const FMockRootMotionSyncState* From, const FMockRootMotionSyncState* To, float PCT)
	{
		static constexpr float TeleportThreshold = 1000.f * 1000.f;
		if (FVector::DistSquared(From->Location, To->Location) > TeleportThreshold)
		{
			*this = *To;
		}
		else
		{
			Location = FMath::Lerp(From->Location, To->Location, PCT);
			Rotation = FMath::Lerp(From->Rotation, To->Rotation, PCT);
		}

		// This is a case where strictly interpolating Sync/Aux state may not be enough in all situations.
		// While its fine for interpolating across the same RootMotionSourceID, when interpolating between
		// different sources, the Driver may want to blend between two animation poses for example
		// (so rather than interpolating Sync/Aux state, we want to interpolate Driver state).
		// This could be made possible by template specialization of FNetworkDriver<ModelDef>::Interpolate
		// (currently it is not supported, but we probably should do it)

		if (From->RootMotionSourceID == To->RootMotionSourceID)
		{
			this->RootMotionSourceID = To->RootMotionSourceID;
			this->PlayPosition = FMath::Lerp(From->PlayPosition, To->PlayPosition, PCT);
			this->PlayRate = FMath::Lerp(From->PlayRate, To->PlayRate, PCT);
		}
		else
		{
			*this = *To;
		}

	}

	bool ShouldReconcile(const FMockRootMotionSyncState& AuthorityState) const
	{
		const float TransformErrorTolerance = 1.f;

		const bool bShouldReconcile =	!Location.Equals(AuthorityState.Location, TransformErrorTolerance) ||
				RootMotionSourceID != AuthorityState.RootMotionSourceID || 
				!FMath::IsNearlyZero(PlayPosition - AuthorityState.PlayPosition) || 
				!FMath::IsNearlyZero(PlayRate - AuthorityState.PlayRate);

		return bShouldReconcile;
	}
};

// The aux state should hold state that does not frequently change. It is otherwise the same as sync state.
// (note that optimizations for sparse aux storage are not complete yet)
struct FMockRootMotionAuxState
{
	TMockParameterPack<> Parameters;

	void NetSerialize(const FNetSerializeParams& P)
	{
		Parameters.NetSerialize(P);
	}

	void ToString(FAnsiStringBuilderBase& Out) const
	{
		Parameters.ToString(Out);
	}

	bool ShouldReconcile(const FMockRootMotionAuxState& AuthorityState) const
	{
		return this->Parameters != AuthorityState.Parameters;
	}

	void Interpolate(const FMockRootMotionAuxState* From, const FMockRootMotionAuxState* To, float PCT)
	{
		this->Parameters = To->Parameters;
	}
};

// This is the interface into "things that actually provide root motion"
class IMockRootMotionSourceMap
{
public:

	// Advance the root motion state by the given TimeStep
	virtual FTransform StepRootMotion(const FNetSimTimeStep& TimeStep, const FMockRootMotionSyncState* In, FMockRootMotionSyncState* Out, const FMockRootMotionAuxState* Aux) = 0;

	// Push the Sync state to the AnimInstance
	//	this is debatable - the simulation code doesn't need to call this, its really the concern of the driver (UMockRootMotionComponent)
	//	and not all potential root motion sources are going to want to set a pose. 
	virtual void FinalizePose(const FMockRootMotionSyncState* Sync, UAnimInstance* AnimInstance) = 0;
};

// This just defines the state types that the simulation uses
using MockRootMotionStateTypes = TNetworkPredictionStateTypes<FMockRootMotionInputCmd, FMockRootMotionSyncState, FMockRootMotionAuxState>;

// The actual NetworkPrediction simulation code that implements root motion movement
//	(root motion evaluation itself is done via IMockRootMotionSourceMap but the actual 'how to move thing given a delta' is done here)
class FMockRootMotionSimulation : public FBaseMovementSimulation
{
public:

	// The main tick function
	void SimulationTick(const FNetSimTimeStep& TimeStep, const TNetSimInput<MockRootMotionStateTypes>& Input, const TNetSimOutput<MockRootMotionStateTypes>& Output);

	// Simulation's interface for mapping ID -> RootMotionSource
	IMockRootMotionSourceMap* SourceMap = nullptr;

	// The component the root motion is relative to. This was found to be needed since, in our examples, we author root motion anims where Y is forward
	// and we rotate the mesh components at the actor level so that X is forward. We need to know which component to rotate the root motion animation relative to.
	// If we continue with this, this means all non anim based root motions should expect to follow the same convention. Need clarity here from animation team.
	USceneComponent* RootMotionComponent = nullptr;
};