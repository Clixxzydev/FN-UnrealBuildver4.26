// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieSceneSequenceInstance.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieSceneSequenceUpdaters.h"

#include "Compilation/MovieSceneCompiledVolatilityManager.h"
#include "Compilation/MovieSceneCompiledDataManager.h"

#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "Evaluation/Instances/MovieSceneTrackEvaluator.h"

#include "IMovieScenePlayer.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneTimeHelpers.h"

#include "Algo/IndexOf.h"

namespace UE
{
namespace MovieScene
{


DECLARE_CYCLE_STAT(TEXT("Sequence Instance Update"), MovieSceneEval_SequenceInstanceUpdate, STATGROUP_MovieSceneEval);
DECLARE_CYCLE_STAT(TEXT("[External] Sequence Instance Post-Update"), MovieSceneEval_SequenceInstancePostUpdate, STATGROUP_MovieSceneEval);

FSequenceInstance::FSequenceInstance(UMovieSceneEntitySystemLinker* Linker, IMovieScenePlayer* Player, FInstanceHandle InInstanceHandle)
	: SequenceID(MovieSceneSequenceID::Root)
	, PlayerIndex(Player->GetUniqueIndex())
	, InstanceHandle(InInstanceHandle)
	, RootInstanceHandle(InstanceHandle)
{
	bFinished = true;
	bHasEverUpdated = false;

	CompiledDataID = Player->GetEvaluationTemplate().GetCompiledDataID();

	FMovieSceneObjectCache& ObjectCache = Player->State.GetObjectCache(SequenceID);
	OnInvalidateObjectBindingHandle = ObjectCache.OnBindingInvalidated.AddUObject(Linker, &UMovieSceneEntitySystemLinker::InvalidateObjectBinding, InstanceHandle);

	InvalidateCachedData(Linker);
}

FSequenceInstance::FSequenceInstance(UMovieSceneEntitySystemLinker* Linker, IMovieScenePlayer* Player, FInstanceHandle InInstanceHandle, FInstanceHandle InRootInstanceHandle, FMovieSceneSequenceID InSequenceID, FMovieSceneCompiledDataID InCompiledDataID)
	: CompiledDataID(InCompiledDataID)
	, SequenceID(InSequenceID)
	, PlayerIndex(Player->GetUniqueIndex())
	, InstanceHandle(InInstanceHandle)
	, RootInstanceHandle(InRootInstanceHandle)
{
	bFinished = true;
	bHasEverUpdated = false;

	FMovieSceneObjectCache& ObjectCache = Player->State.GetObjectCache(SequenceID);
	OnInvalidateObjectBindingHandle = ObjectCache.OnBindingInvalidated.AddUObject(Linker, &UMovieSceneEntitySystemLinker::InvalidateObjectBinding, InstanceHandle);

	InvalidateCachedData(Linker);
}

FSequenceInstance::~FSequenceInstance()
{}

FSequenceInstance::FSequenceInstance(FSequenceInstance&&) = default;

FSequenceInstance& FSequenceInstance::operator=(FSequenceInstance&&) = default;

IMovieScenePlayer* FSequenceInstance::GetPlayer() const
{
	return IMovieScenePlayer::Get(PlayerIndex);
}

void FSequenceInstance::InitializeLegacyEvaluator(UMovieSceneEntitySystemLinker* Linker)
{
	IMovieScenePlayer* Player = GetPlayer();
	check(Player);

	UMovieSceneCompiledDataManager* CompiledDataManager = Player->GetEvaluationTemplate().GetCompiledDataManager();
	FMovieSceneCompiledDataEntry    CompiledEntry       = CompiledDataManager->GetEntry(CompiledDataID);

	if (EnumHasAnyFlags(CompiledEntry.AccumulatedMask, EMovieSceneSequenceCompilerMask::EvaluationTemplate))
	{
		if (!LegacyEvaluator)
		{
			LegacyEvaluator = MakeUnique<FMovieSceneTrackEvaluator>(CompiledEntry.GetSequence(), CompiledDataManager);
		}
	}
	else if (LegacyEvaluator)
	{
		LegacyEvaluator->Finish(*Player);
		LegacyEvaluator = nullptr;
	}
}

void FSequenceInstance::InvalidateCachedData(UMovieSceneEntitySystemLinker* Linker)
{
	Ledger.Invalidate();

	IMovieScenePlayer* Player = GetPlayer();
	check(Player);

	UMovieSceneCompiledDataManager* CompiledDataManager = Player->GetEvaluationTemplate().GetCompiledDataManager();

	UMovieSceneSequence* Sequence = CompiledDataManager->GetEntry(CompiledDataID).GetSequence();
	Player->State.AssignSequence(SequenceID, *Sequence, *Player);

	if (SequenceID == MovieSceneSequenceID::Root)
	{
		// Try and recreate the volatility manager if this sequence is now volatile
		if (!VolatilityManager)
		{
			VolatilityManager = FCompiledDataVolatilityManager::Construct(*Player, CompiledDataID, CompiledDataManager);
			if (VolatilityManager)
			{
				VolatilityManager->ConditionalRecompile(*Player, CompiledDataID, CompiledDataManager);
			}
		}

		ISequenceUpdater::FactoryInstance(SequenceUpdater, CompiledDataManager, CompiledDataID);

		SequenceUpdater->InvalidateCachedData(Linker);

		if (LegacyEvaluator)
		{
			LegacyEvaluator->InvalidateCachedData();
		}

		InitializeLegacyEvaluator(Linker);
	}
}

void FSequenceInstance::DissectContext(UMovieSceneEntitySystemLinker* Linker, const FMovieSceneContext& InContext, TArray<TRange<FFrameTime>>& OutDissections)
{
	check(SequenceID == MovieSceneSequenceID::Root);


	IMovieScenePlayer* Player = GetPlayer();

	if (VolatilityManager)
	{
		UMovieSceneCompiledDataManager* CompiledDataManager = Player->GetEvaluationTemplate().GetCompiledDataManager();
		if (VolatilityManager->ConditionalRecompile(*Player, CompiledDataID, CompiledDataManager))
		{
			InvalidateCachedData(Linker);
		}
	}

	SequenceUpdater->DissectContext(Linker, Player, InContext, OutDissections);
}

void FSequenceInstance::Start(UMovieSceneEntitySystemLinker* Linker, const FMovieSceneContext& InContext)
{
	check(SequenceID == MovieSceneSequenceID::Root);

	bFinished = false;
	bHasEverUpdated = true;

	IMovieScenePlayer* Player = GetPlayer();
	if (Player->PreAnimatedState.IsGlobalCaptureEnabled())
	{
		GlobalStateMarker = Linker->CaptureGlobalState();
	}

	SequenceUpdater->Start(Linker, InstanceHandle, Player, InContext);
}

void FSequenceInstance::Update(UMovieSceneEntitySystemLinker* Linker, const FMovieSceneContext& InContext)
{
	SCOPE_CYCLE_COUNTER(MovieSceneEval_SequenceInstanceUpdate);

	bHasEverUpdated = true;

	if (bFinished)
	{
		Start(Linker, InContext);
	}

	Context = InContext;
	SequenceUpdater->Update(Linker, InstanceHandle, GetPlayer(), InContext);
}

void FSequenceInstance::Finish(UMovieSceneEntitySystemLinker* Linker)
{
	if (IsRootSequence() && !bHasEverUpdated)
	{
		return;
	}

	bFinished = true;
	Ledger.UnlinkEverything(Linker);

	Ledger = FEntityLedger();

	if (SequenceUpdater)
	{
		SequenceUpdater->Finish(Linker, InstanceHandle, GetPlayer());
	}

	if (LegacyEvaluator)
	{
		IMovieScenePlayer* Player = IMovieScenePlayer::Get(PlayerIndex);
		if (ensure(Player))
		{
			LegacyEvaluator->Finish(*Player);
		}
	}
}

void FSequenceInstance::PreEvaluation(UMovieSceneEntitySystemLinker* Linker)
{
	if (IsRootSequence())
	{
		IMovieScenePlayer* Player = GetPlayer();
		if (ensure(Player))
		{
			Player->PreEvaluation(Context);
		}
	}
}

void FSequenceInstance::PostEvaluation(UMovieSceneEntitySystemLinker* Linker)
{
	if (bFinished)
	{
		GlobalStateMarker = nullptr;
	}

	if (LegacyEvaluator)
	{
		IMovieScenePlayer* Player = IMovieScenePlayer::Get(PlayerIndex);
		if (ensure(Player))
		{
			if (bFinished)
			{
				LegacyEvaluator->Finish(*Player);
			}
			else
			{
				// @todo: override root ID
				LegacyEvaluator->Evaluate(Context, *Player);
			}
		}
	}

	Ledger.UnlinkOneShots(Linker);

	if (IsRootSequence())
	{
		IMovieScenePlayer* Player = GetPlayer();
		if (ensure(Player))
		{
			SCOPE_CYCLE_COUNTER(MovieSceneEval_SequenceInstancePostUpdate);

			Player->PostEvaluation(Context);
		}
	}
}

void FSequenceInstance::DestroyImmediately(UMovieSceneEntitySystemLinker* Linker)
{
	if (!Ledger.IsEmpty())
	{
		UE_LOG(LogMovieScene, Verbose, TEXT("Instance being destroyed without first having been finished by calling Finish()"));
		Ledger.UnlinkEverything(Linker);
	}

	if (SequenceUpdater)
	{
		SequenceUpdater->Destroy(Linker);
	}
}

FInstanceHandle FSequenceInstance::FindSubInstance(FMovieSceneSequenceID SubSequenceID) const
{
	return SequenceUpdater ? SequenceUpdater->FindSubInstance(SubSequenceID) : FInstanceHandle();
}

FMovieSceneEntityID FSequenceInstance::FindEntity(UObject* Owner, uint32 EntityID) const
{
	return Ledger.FindImportedEntity(FMovieSceneEvaluationFieldEntityKey{ Owner, EntityID });
}

} // namespace MovieScene
} // namespace UE
