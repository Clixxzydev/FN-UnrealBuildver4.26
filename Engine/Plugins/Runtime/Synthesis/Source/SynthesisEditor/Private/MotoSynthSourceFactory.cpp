// Copyright Epic Games, Inc. All Rights Reserved.
#include "MotoSynthSourceFactory.h"
#include "MotoSynthSourceAsset.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "SynthesisEditorModule.h"
#include "Sound/SoundWave.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_MotoSynthPreset::GetSupportedClass() const
{
	return UMotoSynthPreset::StaticClass();
}

const TArray<FText>& FAssetTypeActions_MotoSynthPreset::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		LOCTEXT("AssetSoundSynthesisSubMenu", "Synthesis")
	};

	return SubMenus;
}

UMotoSynthPresetFactory::UMotoSynthPresetFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMotoSynthPreset::StaticClass();

	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UMotoSynthPresetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UMotoSynthPreset* NewAsset = NewObject<UMotoSynthPreset >(InParent, InName, Flags);
	return NewAsset;
}

UClass* FAssetTypeActions_MotoSynthSource::GetSupportedClass() const
{
	return UMotoSynthSource::StaticClass();
}

UMotoSynthSourceFactory::UMotoSynthSourceFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMotoSynthSource::StaticClass();

	bCreateNew = false;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UMotoSynthSourceFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	if (StagedSoundWave.IsValid())
	{
		USoundWave* SoundWave = StagedSoundWave.Get();
		
		if (SoundWave)
		{
			// Warn that we're ignoring non-mono source for motosynth source. Mixing channels to mono would likely destroy the source asset anyway, so we're
			// only going to use the mono channel (left) as the source
			if (SoundWave->NumChannels > 1)
			{
				UE_LOG(LogSynthesisEditor, Warning, TEXT("Sound source used as moto synth source has more than one channel. Only using the 0th channel index (left) for moto synth source."));
			}

			UMotoSynthSource* NewAsset = NewObject<UMotoSynthSource>(InParent, InName, Flags);
			
			NewAsset->SoundWaveSource = SoundWave;
			NewAsset->UpdateSourceData();
			NewAsset->PerformGrainTableAnalysis();
			return NewAsset;
		}

		StagedSoundWave.Reset();
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE