// Copyright Epic Games, Inc. All Rights Reserved.


#include "Sound/SoundNodeQualityLevel.h"
#include "EngineGlobals.h"
#include "ActiveSound.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNode.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"

#if WITH_EDITORONLY_DATA
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#endif

static int32 CullSoundWaveHardReferencesCvar = 0;
FAutoConsoleVariableRef CVarCullSoundWaveHardReferences(
	TEXT("au.CullSoundWaveHardReferences"),
	CullSoundWaveHardReferencesCvar,
	TEXT("When set to 1, this deliberately removes USoundWave hard references from currently unselected quality nodes.\n")
	TEXT("0: Cull soundwaves, 1: do not cull sound waves."),
	ECVF_Default);


void USoundNodeQualityLevel::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	ReconcileNode(false);
#endif

	USoundCue::CacheQualityLevel();
	uint32 CachedQualityLevel = USoundCue::GetCachedQualityLevel();
	
	ensure(CachedQualityLevel != INDEX_NONE);

	if (!GIsEditor && CullSoundWaveHardReferencesCvar && ChildNodes.IsValidIndex(CachedQualityLevel))
	{
		// go through any waveplayers on an unselcted quality level and null out their soundwave references.
		for (int32 Index = 0; Index < ChildNodes.Num(); Index++)
		{
			if (Index == CachedQualityLevel)
			{
				continue;
			}
			else if(ChildNodes[Index] != nullptr)
			{
				ChildNodes[Index]->RemoveSoundWaveOnChildWavePlayers();
			}
		}
	}
}

#if WITH_EDITOR
void USoundNodeQualityLevel::ReconcileNode(bool bReconstructNode)
{
	while (ChildNodes.Num() > GetMinChildNodes())
	{
		RemoveChildNode(ChildNodes.Num()-1);
	}
	while (ChildNodes.Num() < GetMinChildNodes())
	{
		InsertChildNode(ChildNodes.Num());
	}
#if WITH_EDITORONLY_DATA
	if (GIsEditor && bReconstructNode && GraphNode)
	{
		GraphNode->ReconstructNode();
		GraphNode->GetGraph()->NotifyGraphChanged();
	}
#endif
}

FText USoundNodeQualityLevel::GetInputPinName(int32 PinIndex) const
{
	return GetDefault<UAudioSettings>()->GetQualityLevelSettings(PinIndex).DisplayName;
}
#endif

void USoundNodeQualityLevel::PrimeChildWavePlayers(bool bRecurse)
{
	// If we're able to retrieve a valid cached quality level for this sound cue,
	// only prime that quality level.
	USoundCue::CacheQualityLevel();
	uint32 QualityLevel = USoundCue::GetCachedQualityLevel();

	ensure(QualityLevel != INDEX_NONE);

#if WITH_EDITOR
	if (GIsEditor && QualityLevel < 0)
	{
		QualityLevel = GetDefault<ULevelEditorPlaySettings>()->PlayInEditorSoundQualityLevel;
	}
#endif

	if (ChildNodes.IsValidIndex(QualityLevel) && ChildNodes[QualityLevel])
	{
		ChildNodes[QualityLevel]->PrimeChildWavePlayers(bRecurse);
	}
}

void USoundNodeQualityLevel::RetainChildWavePlayers(bool bRecurse)
{
	// If we're able to retrieve a valid cached quality level for this sound cue,
	// only retain that quality level.
	USoundCue::CacheQualityLevel();
	uint32 QualityLevel = USoundCue::GetCachedQualityLevel();

	ensure(QualityLevel != INDEX_NONE);

#if WITH_EDITOR
	if (GIsEditor && QualityLevel < 0)
	{
		QualityLevel = GetDefault<ULevelEditorPlaySettings>()->PlayInEditorSoundQualityLevel;
	}
#endif

	if (ChildNodes.IsValidIndex(QualityLevel) && ChildNodes[QualityLevel])
	{
		ChildNodes[QualityLevel]->RetainChildWavePlayers(bRecurse);
	}
}

void USoundNodeQualityLevel::ReleaseRetainerOnChildWavePlayers(bool bRecurse)
{
	// If we're able to retrieve a valid cached quality level for this sound cue,
	// only release that quality level.
	int32 QualityLevel = USoundCue::GetCachedQualityLevel();

#if WITH_EDITOR
	if (GIsEditor && QualityLevel < 0)
	{
		QualityLevel = GetDefault<ULevelEditorPlaySettings>()->PlayInEditorSoundQualityLevel;
	}
#endif

	if (ChildNodes.IsValidIndex(QualityLevel) && ChildNodes[QualityLevel])
	{
		ChildNodes[QualityLevel]->ReleaseRetainerOnChildWavePlayers(bRecurse);
	}
}

int32 USoundNodeQualityLevel::GetMaxChildNodes() const
{
	return GetDefault<UAudioSettings>()->QualityLevels.Num();
}

int32 USoundNodeQualityLevel::GetMinChildNodes() const
{
	return GetDefault<UAudioSettings>()->QualityLevels.Num();
}

void USoundNodeQualityLevel::ParseNodes( FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash, FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams, TArray<FWaveInstance*>& WaveInstances )
{
#if WITH_EDITOR
	int32 QualityLevel = 0;

	if (GIsEditor)
	{
		RETRIEVE_SOUNDNODE_PAYLOAD( sizeof( int32 ) );
		DECLARE_SOUNDNODE_ELEMENT( int32, CachedQualityLevel );

		if (*RequiresInitialization)
		{
			const bool bIsPIESound = ((GEditor->bIsSimulatingInEditor || GEditor->PlayWorld != nullptr) && ActiveSound.GetWorldID() > 0);
			if (bIsPIESound)
			{
				CachedQualityLevel = GetDefault<ULevelEditorPlaySettings>()->PlayInEditorSoundQualityLevel;
			}
		}

		QualityLevel = CachedQualityLevel;
	}
	else
	{
		QualityLevel = USoundCue::GetCachedQualityLevel();
	}
#else
	const int32 QualityLevel = USoundCue::GetCachedQualityLevel();
#endif

	if (ChildNodes.IsValidIndex(QualityLevel) && ChildNodes[QualityLevel])
	{
		ChildNodes[QualityLevel]->ParseNodes( AudioDevice, GetNodeWaveInstanceHash(NodeWaveInstanceHash, ChildNodes[QualityLevel], QualityLevel), ActiveSound, ParseParams, WaveInstances );
	}
}
