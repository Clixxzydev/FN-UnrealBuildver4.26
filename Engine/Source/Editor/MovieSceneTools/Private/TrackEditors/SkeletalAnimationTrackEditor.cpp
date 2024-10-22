// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/SkeletalAnimationTrackEditor.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "AssetData.h"
#include "Animation/AnimSequenceBase.h"
#include "Modules/ModuleManager.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBox.h"
#include "SequencerSectionPainter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "CommonMovieSceneTools.h"
#include "AssetRegistryModule.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "MatineeImportTools.h"
#include "Matinee/InterpTrackAnimControl.h"
#include "SequencerUtilities.h"
#include "ISectionLayoutBuilder.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/PoseAsset.h"
#include "EditorStyleSet.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "MovieSceneTimeHelpers.h"
#include "SequencerTimeSliderController.h"
#include "AnimationEditorUtils.h"
#include "Factories/PoseAssetFactory.h"
#include "Misc/MessageDialog.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Blueprint.h"

#include "CommonMovieSceneTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"

#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "PropertyEditorModule.h"
#include "IDetailChildrenBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "IPropertyUtilities.h"
#include "Factories/AnimSequenceFactory.h"
#include "MovieSceneToolHelpers.h"


namespace SkeletalAnimationEditorConstants
{
	// @todo Sequencer Allow this to be customizable
	const uint32 AnimationTrackHeight = 20;
}

#define LOCTEXT_NAMESPACE "FSkeletalAnimationTrackEditor"

USkeletalMeshComponent* AcquireSkeletalMeshFromObjectGuid(const FGuid& Guid, TSharedPtr<ISequencer> SequencerPtr)
{
	UObject* BoundObject = SequencerPtr.IsValid() ? SequencerPtr->FindSpawnedObjectOrTemplate(Guid) : nullptr;

	if (AActor* Actor = Cast<AActor>(BoundObject))
	{
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(Component))
			{
				return SkeletalMeshComp;
			}
		}
	}
	else if(USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(BoundObject))
	{
		if (SkeletalMeshComponent->SkeletalMesh)
		{
			return SkeletalMeshComponent;
		}
	}

	return nullptr;
}

USkeleton* GetSkeletonFromComponent(UActorComponent* InComponent)
{
	USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(InComponent);
	if (SkeletalMeshComp && SkeletalMeshComp->SkeletalMesh && SkeletalMeshComp->SkeletalMesh->Skeleton)
	{
		// @todo Multiple actors, multiple components
		return SkeletalMeshComp->SkeletalMesh->Skeleton;
	}

	return nullptr;
}

USkeleton* AcquireSkeletonFromObjectGuid(const FGuid& Guid, TSharedPtr<ISequencer> SequencerPtr)
{
	UObject* BoundObject = SequencerPtr.IsValid() ? SequencerPtr->FindSpawnedObjectOrTemplate(Guid) : nullptr;

	AActor* Actor = Cast<AActor>(BoundObject);

	if (!Actor)
	{
		if (UChildActorComponent* ChildActorComponent = Cast<UChildActorComponent>(BoundObject))
		{
			Actor = ChildActorComponent->GetChildActor();
		}
	}

	if (Actor)
	{
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (USkeleton* Skeleton = GetSkeletonFromComponent(Component))
			{
				return Skeleton;
			}
		}

		AActor* ActorCDO = Cast<AActor>(Actor->GetClass()->GetDefaultObject());
		if (ActorCDO)
		{
			for (UActorComponent* Component : ActorCDO->GetComponents())
			{
				if (USkeleton* Skeleton = GetSkeletonFromComponent(Component))
				{
					return Skeleton;
				}
			}
		}

		UBlueprintGeneratedClass* ActorBlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(Actor->GetClass());
		if (ActorBlueprintGeneratedClass)
		{
			const TArray<USCS_Node*>& ActorBlueprintNodes = ActorBlueprintGeneratedClass->SimpleConstructionScript->GetAllNodes();

			for (USCS_Node* Node : ActorBlueprintNodes)
			{
				if (Node->ComponentClass->IsChildOf(USkeletalMeshComponent::StaticClass()))
				{
					if (USkeleton* Skeleton = GetSkeletonFromComponent(Node->GetActualComponentTemplate(ActorBlueprintGeneratedClass)))
					{
						return Skeleton;
					}
				}
			}
		}
	}
	else if(USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(BoundObject))
	{
		if (USkeleton* Skeleton = GetSkeletonFromComponent(SkeletalMeshComponent))
		{
			return Skeleton;
		}
	}

	return nullptr;
}


class FMovieSceneSkeletalAnimationParamsDetailCustomization : public IPropertyTypeCustomization
{
public:
	FMovieSceneSkeletalAnimationParamsDetailCustomization(const FSequencerSectionPropertyDetailsViewCustomizationParams& InParams)
		: Params(InParams)
	{
	}

	// IDetailCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
	}

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		const FName AnimationPropertyName = GET_MEMBER_NAME_CHECKED(FMovieSceneSkeletalAnimationParams, Animation);

		uint32 NumChildren;
		PropertyHandle->GetNumChildren(NumChildren);
		for (uint32 i = 0; i < NumChildren; ++i)
		{
			TSharedPtr<IPropertyHandle> ChildPropertyHandle = PropertyHandle->GetChildHandle(i);
			IDetailPropertyRow& ChildPropertyRow = ChildBuilder.AddProperty(ChildPropertyHandle.ToSharedRef());

			// Let most properties be whatever they want to be... we just want to customize the `Animation` property
			// by making it look like a normal asset reference property, but with some custom filtering.
			if (ChildPropertyHandle->GetProperty()->GetFName() == AnimationPropertyName)
			{
				FDetailWidgetRow& Row = ChildPropertyRow.CustomWidget();

				if (Params.ParentObjectBindingGuid.IsValid())
				{
					// Store the compatible skeleton's name, and create a property widget with a filter that will check
					// for animations that match that skeleton.
					USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(Params.ParentObjectBindingGuid, Params.Sequencer);
					SkeletonName = FAssetData(Skeleton).GetExportTextName();

					TSharedPtr<IPropertyUtilities> PropertyUtilities = CustomizationUtils.GetPropertyUtilities();

					TSharedRef<SObjectPropertyEntryBox> ContentWidget = SNew(SObjectPropertyEntryBox)
						.PropertyHandle(ChildPropertyHandle)
						.AllowedClass(UAnimSequenceBase::StaticClass())
						.DisplayThumbnail(true)
						.ThumbnailPool(PropertyUtilities.IsValid() ? PropertyUtilities->GetThumbnailPool() : nullptr)
						.OnShouldFilterAsset(FOnShouldFilterAsset::CreateRaw(this, &FMovieSceneSkeletalAnimationParamsDetailCustomization::ShouldFilterAsset));

					Row.NameContent()[ChildPropertyHandle->CreatePropertyNameWidget()];
					Row.ValueContent()[ContentWidget];

					float MinDesiredWidth, MaxDesiredWidth;
					ContentWidget->GetDesiredWidth(MinDesiredWidth, MaxDesiredWidth);
					Row.ValueContent().MinWidth = MinDesiredWidth;
					Row.ValueContent().MaxWidth = MaxDesiredWidth;

					// The content widget already contains a "reset to default" button, so we don't want the details view row
					// to make another one. We add this metadata on the property handle instance to suppress it.
					ChildPropertyHandle->SetInstanceMetaData(TEXT("NoResetToDefault"), TEXT("true"));
				}
			}
		}
	}

	bool ShouldFilterAsset(const FAssetData& AssetData)
	{
		// Since the `SObjectPropertyEntryBox` doesn't support passing some `Filter` properties for the asset picker, 
		// we just combine the tag value filtering we want (i.e. checking the skeleton compatibility) along with the
		// other filtering we already get from the track editor's filter callback.
		FSkeletalAnimationTrackEditor& TrackEditor = static_cast<FSkeletalAnimationTrackEditor&>(Params.TrackEditor);
		if (TrackEditor.ShouldFilterAsset(AssetData))
		{
			return true;
		}

		if (!SkeletonName.IsEmpty())
		{
			const FString& SkeletonTag = AssetData.GetTagValueRef<FString>(TEXT("Skeleton"));
			if (SkeletonTag != SkeletonName)
			{
				return true;
			}
		}

		return false;
	}

private:
	FSequencerSectionPropertyDetailsViewCustomizationParams Params;
	FString SkeletonName;
};


FSkeletalAnimationSection::FSkeletalAnimationSection( UMovieSceneSection& InSection, TWeakPtr<ISequencer> InSequencer)
	: Section(*CastChecked<UMovieSceneSkeletalAnimationSection>(&InSection))
	, Sequencer(InSequencer)
	, InitialFirstLoopStartOffsetDuringResize(0)
	, InitialStartTimeDuringResize(0)
{ }


UMovieSceneSection* FSkeletalAnimationSection::GetSectionObject()
{ 
	return &Section;
}


FText FSkeletalAnimationSection::GetSectionTitle() const
{
	if (Section.Params.Animation != nullptr)
	{
		return FText::FromString( Section.Params.Animation->GetName() );
	}
	return LOCTEXT("NoAnimationSection", "No Animation");
}


float FSkeletalAnimationSection::GetSectionHeight() const
{
	return (float)SkeletalAnimationEditorConstants::AnimationTrackHeight;
}


FMargin FSkeletalAnimationSection::GetContentPadding() const
{
	return FMargin(8.0f, 8.0f);
}


int32 FSkeletalAnimationSection::OnPaintSection( FSequencerSectionPainter& Painter ) const
{
	const ESlateDrawEffect DrawEffects = Painter.bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	
	const FTimeToPixel& TimeToPixelConverter = Painter.GetTimeConverter();

	int32 LayerId = Painter.PaintSectionBackground();

	static const FSlateBrush* GenericDivider = FEditorStyle::GetBrush("Sequencer.GenericDivider");

	if (!Section.HasStartFrame() || !Section.HasEndFrame())
	{
		return LayerId;
	}

	FFrameRate TickResolution = TimeToPixelConverter.GetTickResolution();

	// Add lines where the animation starts and ends/loops
	const float AnimPlayRate = FMath::IsNearlyZero(Section.Params.PlayRate) || Section.Params.Animation == nullptr ? 1.0f : Section.Params.PlayRate * Section.Params.Animation->RateScale;
	const float SeqLength = (Section.Params.GetSequenceLength() - TickResolution.AsSeconds(Section.Params.StartFrameOffset + Section.Params.EndFrameOffset)) / AnimPlayRate;
	const float FirstLoopSeqLength = SeqLength - TickResolution.AsSeconds(Section.Params.FirstLoopStartFrameOffset) / AnimPlayRate;

	if (!FMath::IsNearlyZero(SeqLength, KINDA_SMALL_NUMBER) && SeqLength > 0)
	{
		float MaxOffset  = Section.GetRange().Size<FFrameTime>() / TickResolution;
		float OffsetTime = FirstLoopSeqLength;
		float StartTime  = Section.GetInclusiveStartFrame() / TickResolution;

		while (OffsetTime < MaxOffset)
		{
			float OffsetPixel = TimeToPixelConverter.SecondsToPixel(StartTime + OffsetTime) - TimeToPixelConverter.SecondsToPixel(StartTime);

			FSlateDrawElement::MakeBox(
				Painter.DrawElements,
				LayerId,
				Painter.SectionGeometry.MakeChild(
					FVector2D(2.f, Painter.SectionGeometry.Size.Y-2.f),
					FSlateLayoutTransform(FVector2D(OffsetPixel, 1.f))
				).ToPaintGeometry(),
				GenericDivider,
				DrawEffects
			);

			OffsetTime += SeqLength;
		}
	}

	TSharedPtr<ISequencer> SequencerPtr = Sequencer.Pin();
	if (Painter.bIsSelected && SequencerPtr.IsValid())
	{
		FFrameTime CurrentTime = SequencerPtr->GetLocalTime().Time;
		if (Section.GetRange().Contains(CurrentTime.FrameNumber) && Section.Params.Animation != nullptr)
		{
			// Draw the current time next to the scrub handle
			const float AnimTime = Section.MapTimeToAnimation(CurrentTime, TickResolution);
			int32 FrameTime = Section.Params.Animation->GetFrameAtTime(AnimTime);

			DrawFrameNumberHint(Painter, CurrentTime, FrameTime);
		}
	}
	
	return LayerId;
}

void FSkeletalAnimationSection::BeginResizeSection()
{
	InitialFirstLoopStartOffsetDuringResize = Section.Params.FirstLoopStartFrameOffset;
	InitialStartTimeDuringResize   = Section.HasStartFrame() ? Section.GetInclusiveStartFrame() : 0;
}

void FSkeletalAnimationSection::ResizeSection(ESequencerSectionResizeMode ResizeMode, FFrameNumber ResizeTime)
{
	// Adjust the start offset when resizing from the beginning
	if (ResizeMode == SSRM_LeadingEdge)
	{
		FFrameRate FrameRate   = Section.GetTypedOuter<UMovieScene>()->GetTickResolution();
		FFrameNumber StartOffset = FrameRate.AsFrameNumber((ResizeTime - InitialStartTimeDuringResize) / FrameRate * Section.Params.PlayRate);

		StartOffset += InitialFirstLoopStartOffsetDuringResize;

		if (StartOffset < 0)
		{
			FFrameTime FrameTimeOver = FFrameTime::FromDecimal(StartOffset.Value / Section.Params.PlayRate);

			// Ensure start offset is not less than 0 and adjust ResizeTime
			ResizeTime = ResizeTime - FrameTimeOver.GetFrame();

			StartOffset = FFrameNumber(0);
		}
		else
		{
			// If the start offset exceeds the length of one loop, trim it back.
			const FFrameNumber SeqLength = FrameRate.AsFrameNumber(Section.Params.GetSequenceLength()) - Section.Params.StartFrameOffset - Section.Params.EndFrameOffset;
			StartOffset = StartOffset % SeqLength;
		}

		Section.Params.FirstLoopStartFrameOffset = StartOffset;
	}

	ISequencerSection::ResizeSection(ResizeMode, ResizeTime);
}

void FSkeletalAnimationSection::BeginSlipSection()
{
	BeginResizeSection();
}

void FSkeletalAnimationSection::SlipSection(FFrameNumber SlipTime)
{
	FFrameRate FrameRate = Section.GetTypedOuter<UMovieScene>()->GetTickResolution();
	FFrameNumber StartOffset = FrameRate.AsFrameNumber((SlipTime - InitialStartTimeDuringResize) / FrameRate * Section.Params.PlayRate);

	StartOffset += InitialFirstLoopStartOffsetDuringResize;

	if (StartOffset < 0)
	{
		// Ensure start offset is not less than 0 and adjust ResizeTime
		SlipTime = SlipTime - StartOffset;

		StartOffset = FFrameNumber(0);
	}
	else
	{
		// If the start offset exceeds the length of one loop, trim it back.
		const FFrameNumber SeqLength = FrameRate.AsFrameNumber(Section.Params.GetSequenceLength()) - Section.Params.StartFrameOffset - Section.Params.EndFrameOffset;
		StartOffset = StartOffset % SeqLength;
	}

	Section.Params.FirstLoopStartFrameOffset = StartOffset;

	ISequencerSection::SlipSection(SlipTime);
}

bool FSkeletalAnimationTrackEditor::CreatePoseAsset(const TArray<UObject*> NewAssets, FGuid InObjectBinding)
{
	USkeletalMeshComponent* SkeletalMeshComponent = AcquireSkeletalMeshFromObjectGuid(InObjectBinding, GetSequencer());

	bool bResult = false;
	if (NewAssets.Num() > 0)
	{
		for (auto NewAsset : NewAssets)
		{
			UPoseAsset* NewPoseAsset = Cast<UPoseAsset>(NewAsset);
			if (NewPoseAsset)
			{
				NewPoseAsset->AddOrUpdatePoseWithUniqueName(SkeletalMeshComponent);
				bResult = true;
			}
		}

		// if it contains error, warn them
		if (bResult)
		{				
			FText NotificationText;
			if (NewAssets.Num() == 1)
			{
				NotificationText = FText::Format(LOCTEXT("NumPoseAssetsCreated", "{0} Pose assets created."), NewAssets.Num());
			}
			else
			{
				NotificationText = FText::Format(LOCTEXT("PoseAssetsCreated", "Pose asset created: '{0}'."), FText::FromString(NewAssets[0]->GetName()));
			}
						
			FNotificationInfo Info(NotificationText);	
			Info.ExpireDuration = 8.0f;	
			Info.bUseLargeFont = false;
			Info.Hyperlink = FSimpleDelegate::CreateLambda([NewAssets]()
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAssets(NewAssets);
			});
			Info.HyperlinkText = FText::Format(LOCTEXT("OpenNewPoseAssetHyperlink", "Open {0}"), FText::FromString(NewAssets[0]->GetName()));
				
			TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
			if ( Notification.IsValid() )
			{
				Notification->SetCompletionState( SNotificationItem::CS_Success );
			}
		}
		else
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("FailedToCreateAsset", "Failed to create asset"));
		}
	}
	return bResult;
}


void FSkeletalAnimationTrackEditor::HandleCreatePoseAsset(FGuid InObjectBinding)
{
	USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(InObjectBinding, GetSequencer());
	if (Skeleton)
	{
		TArray<TWeakObjectPtr<UObject>> Skeletons;
		Skeletons.Add(Skeleton);
		AnimationEditorUtils::ExecuteNewAnimAsset<UPoseAssetFactory, UPoseAsset>(Skeletons, FString("_PoseAsset"), FAnimAssetCreated::CreateSP(this, &FSkeletalAnimationTrackEditor::CreatePoseAsset, InObjectBinding), false);
	}
}


void FSkeletalAnimationSection::CustomizePropertiesDetailsView(TSharedRef<IDetailsView> DetailsView, const FSequencerSectionPropertyDetailsViewCustomizationParams& InParams) const
{
	DetailsView->RegisterInstancedCustomPropertyTypeLayout(
		TEXT("MovieSceneSkeletalAnimationParams"),
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([=]() { return MakeShared<FMovieSceneSkeletalAnimationParamsDetailCustomization>(InParams); }));
}


FSkeletalAnimationTrackEditor::FSkeletalAnimationTrackEditor( TSharedRef<ISequencer> InSequencer )
	: FMovieSceneTrackEditor( InSequencer ) 
{ }


TSharedRef<ISequencerTrackEditor> FSkeletalAnimationTrackEditor::CreateTrackEditor( TSharedRef<ISequencer> InSequencer )
{
	return MakeShareable( new FSkeletalAnimationTrackEditor( InSequencer ) );
}


bool FSkeletalAnimationTrackEditor::SupportsType( TSubclassOf<UMovieSceneTrack> Type ) const
{
	return Type == UMovieSceneSkeletalAnimationTrack::StaticClass();
}


TSharedRef<ISequencerSection> FSkeletalAnimationTrackEditor::MakeSectionInterface( UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding )
{
	check( SupportsType( SectionObject.GetOuter()->GetClass() ) );
	
	return MakeShareable( new FSkeletalAnimationSection(SectionObject, GetSequencer()) );
}


bool FSkeletalAnimationTrackEditor::HandleAssetAdded(UObject* Asset, const FGuid& TargetObjectGuid)
{
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();

	if (Asset->IsA<UAnimSequenceBase>() && SequencerPtr.IsValid())
	{
		UAnimSequenceBase* AnimSequence = Cast<UAnimSequenceBase>(Asset);
		
		if (TargetObjectGuid.IsValid() && AnimSequence->CanBeUsedInComposition())
		{
			USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(TargetObjectGuid, GetSequencer());

			if (Skeleton && Skeleton == AnimSequence->GetSkeleton())
			{
				UObject* Object = SequencerPtr->FindSpawnedObjectOrTemplate(TargetObjectGuid);
				
				UMovieSceneTrack* Track = nullptr;

				const FScopedTransaction Transaction(LOCTEXT("AddAnimation_Transaction", "Add Animation"));

				int32 RowIndex = INDEX_NONE;
				AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FSkeletalAnimationTrackEditor::AddKeyInternal, Object, AnimSequence, Track, RowIndex));

				return true;
			}
		}
	}
	return false;
}

void FSkeletalAnimationTrackEditor::BuildObjectBindingContextMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	if (ObjectClass->IsChildOf(USkeletalMeshComponent::StaticClass()) || ObjectClass->IsChildOf(AActor::StaticClass()) || ObjectClass->IsChildOf(UChildActorComponent::StaticClass()))
	{
		ConstructObjectBindingTrackMenu(MenuBuilder, ObjectBindings);
	}
}

bool FSkeletalAnimationTrackEditor::CreateAnimationSequence(const TArray<UObject*> NewAssets, USkeletalMeshComponent* SkelMeshComp)
{
	bool bResult = false;
	if (NewAssets.Num() > 0)
	{
		for (UObject* NewAsset : NewAssets)
		{
			UAnimSequence* AnimSequence = Cast<UAnimSequence>(NewAsset);
			if (AnimSequence)
			{
				const TSharedPtr<ISequencer> ParentSequencer = GetSequencer();
				UMovieScene* MovieScene = ParentSequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
				FMovieSceneSequenceIDRef Template = ParentSequencer->GetFocusedTemplateID();
				FMovieSceneSequenceTransform RootToLocalTransform;
				bResult  = MovieSceneToolHelpers::ExportToAnimSequence(AnimSequence, MovieScene, ParentSequencer.Get(), SkelMeshComp, Template, RootToLocalTransform);
			}
		}

		// if it contains error, warn them
		if (bResult)
		{
			FText NotificationText;
			if (NewAssets.Num() == 1)
			{
				NotificationText = FText::Format(LOCTEXT("NumAnimSequenceAssetsCreated", "{0} Anim Sequence  assets created."), NewAssets.Num());
			}
			else
			{
				NotificationText = FText::Format(LOCTEXT("AnimSequenceAssetsCreated", "Anim Sequence asset created: '{0}'."), FText::FromString(NewAssets[0]->GetName()));
			}

			FNotificationInfo Info(NotificationText);
			Info.ExpireDuration = 8.0f;
			Info.bUseLargeFont = false;
			Info.Hyperlink = FSimpleDelegate::CreateLambda([NewAssets]()
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAssets(NewAssets);
			});
			Info.HyperlinkText = FText::Format(LOCTEXT("OpenNewPoseAssetHyperlink", "Open {0}"), FText::FromString(NewAssets[0]->GetName()));

			TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
			if (Notification.IsValid())
			{
				Notification->SetCompletionState(SNotificationItem::CS_Success);
			}
		}
		else
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("FailedToCreateAsset", "Failed to create asset"));
		}
	}
	return bResult;
}

void FSkeletalAnimationTrackEditor::HandleCreateAnimationSequence(USkeletalMeshComponent* SkelMeshComp, USkeleton* Skeleton)
{
	if (SkelMeshComp)
	{
		TArray<TWeakObjectPtr<UObject>> Skels;
		if (SkelMeshComp->SkeletalMesh)
		{
			Skels.Add(SkelMeshComp->SkeletalMesh);
		}
		else
		{
			Skels.Add(Skeleton);
		}
		AnimationEditorUtils::ExecuteNewAnimAsset<UAnimSequenceFactory, UAnimSequence>(Skels, FString("_Sequence"), FAnimAssetCreated::CreateSP(this, &FSkeletalAnimationTrackEditor::CreateAnimationSequence, SkelMeshComp), false);
	}
}

void FSkeletalAnimationTrackEditor::ConstructObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings)
{
	if(ObjectBindings.Num() > 0)
	{
		USkeletalMeshComponent* SkelMeshComp =  AcquireSkeletalMeshFromObjectGuid(ObjectBindings[0], GetSequencer());

		if (SkelMeshComp)
		{

			MenuBuilder.BeginSection("Create Animation Assets", LOCTEXT("CreateAnimationAssetsName", "Create Animation Assets"));

			USkeleton* Skeleton = GetSkeletonFromComponent(SkelMeshComp);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("CreateAnimSequence", "Create Animation Sequence"),
				LOCTEXT("PasteCreateAnimSequenceTooltip", "Create Animation Sequence for this Skeletal Mesh. Note it will create it based upon the Sequencer Display Range and Display Frame Rate"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FSkeletalAnimationTrackEditor::HandleCreateAnimationSequence, SkelMeshComp,Skeleton)),
				NAME_None,
				EUserInterfaceActionType::Button);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("CreatePoseAsset", "Create Pose Asset"),
				LOCTEXT("CreatePoseAsset_ToolTip", "Create Animation from current Pose"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FSkeletalAnimationTrackEditor::HandleCreatePoseAsset, ObjectBindings[0])),
				NAME_None,
				EUserInterfaceActionType::Button);

			MenuBuilder.EndSection();
		}
	}
}

void FSkeletalAnimationTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	if (ObjectClass->IsChildOf(USkeletalMeshComponent::StaticClass()) || ObjectClass->IsChildOf(AActor::StaticClass()) || ObjectClass->IsChildOf(UChildActorComponent::StaticClass()))
	{
		const TSharedPtr<ISequencer> ParentSequencer = GetSequencer();

		USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(ObjectBindings[0], GetSequencer());

		if (Skeleton)
		{
			// Load the asset registry module
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

			// Collect a full list of assets with the specified class
			TArray<FAssetData> AssetDataList;
			AssetRegistryModule.Get().GetAssetsByClass(UAnimSequenceBase::StaticClass()->GetFName(), AssetDataList, true);

			if (AssetDataList.Num())
			{
				UMovieSceneTrack* Track = nullptr;

				MenuBuilder.AddSubMenu(
					LOCTEXT("AddAnimation", "Animation"), NSLOCTEXT("Sequencer", "AddAnimationTooltip", "Adds an animation track."),
					FNewMenuDelegate::CreateRaw(this, &FSkeletalAnimationTrackEditor::AddAnimationSubMenu, ObjectBindings, Skeleton, Track)
				);
			}
		}
	}
}

TSharedRef<SWidget> FSkeletalAnimationTrackEditor::BuildAnimationSubMenu(FGuid ObjectBinding, USkeleton* Skeleton, UMovieSceneTrack* Track)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	
	TArray<FGuid> ObjectBindings;
	ObjectBindings.Add(ObjectBinding);

	AddAnimationSubMenu(MenuBuilder, ObjectBindings, Skeleton, Track);

	return MenuBuilder.MakeWidget();
}

bool FSkeletalAnimationTrackEditor::ShouldFilterAsset(const FAssetData& AssetData)
{
	// we don't want montage
	if (AssetData.AssetClass == UAnimMontage::StaticClass()->GetFName())
	{
		return true;
	}

	const FString EnumString = AssetData.GetTagValueRef<FString>(GET_MEMBER_NAME_CHECKED(UAnimSequence, AdditiveAnimType));
	if (EnumString.IsEmpty())
	{
		return false;
	}

	UEnum* AdditiveTypeEnum = StaticEnum<EAdditiveAnimationType>();
	return ((EAdditiveAnimationType)AdditiveTypeEnum->GetValueByName(*EnumString) == AAT_RotationOffsetMeshSpace);
}

void FSkeletalAnimationTrackEditor::AddAnimationSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings, USkeleton* Skeleton, UMovieSceneTrack* Track)
{
	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateRaw( this, &FSkeletalAnimationTrackEditor::OnAnimationAssetSelected, ObjectBindings, Track);
		AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateRaw( this, &FSkeletalAnimationTrackEditor::OnAnimationAssetEnterPressed, ObjectBindings, Track);
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
		AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateRaw(this, &FSkeletalAnimationTrackEditor::ShouldFilterAsset);
		AssetPickerConfig.Filter.bRecursiveClasses = true;
		AssetPickerConfig.Filter.ClassNames.Add(UAnimSequenceBase::StaticClass()->GetFName());
		AssetPickerConfig.Filter.TagsAndValues.Add(TEXT("Skeleton"), FAssetData(Skeleton).GetExportTextName());
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TSharedPtr<SBox> MenuEntry = SNew(SBox)
		.WidthOverride(300.0f)
		.HeightOverride(300.f)
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		];

	MenuBuilder.AddWidget(MenuEntry.ToSharedRef(), FText::GetEmpty(), true);
}


void FSkeletalAnimationTrackEditor::OnAnimationAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings, UMovieSceneTrack* Track)
{
	FSlateApplication::Get().DismissAllMenus();

	UObject* SelectedObject = AssetData.GetAsset();
	TSharedPtr<ISequencer> SequencerPtr = GetSequencer();

	if (SelectedObject && SelectedObject->IsA(UAnimSequenceBase::StaticClass()) && SequencerPtr.IsValid())
	{
		UAnimSequenceBase* AnimSequence = CastChecked<UAnimSequenceBase>(AssetData.GetAsset());

		const FScopedTransaction Transaction(LOCTEXT("AddAnimation_Transaction", "Add Animation"));

		for (FGuid ObjectBinding : ObjectBindings)
		{
			UObject* Object = SequencerPtr->FindSpawnedObjectOrTemplate(ObjectBinding);
			int32 RowIndex = INDEX_NONE;
			AnimatablePropertyChanged(FOnKeyProperty::CreateRaw(this, &FSkeletalAnimationTrackEditor::AddKeyInternal, Object, AnimSequence, Track, RowIndex));
		}
	}
}

void FSkeletalAnimationTrackEditor::OnAnimationAssetEnterPressed(const TArray<FAssetData>& AssetData, TArray<FGuid> ObjectBindings, UMovieSceneTrack* Track)
{
	if (AssetData.Num() > 0)
	{
		OnAnimationAssetSelected(AssetData[0].GetAsset(), ObjectBindings, Track);
	}
}


FKeyPropertyResult FSkeletalAnimationTrackEditor::AddKeyInternal( FFrameNumber KeyTime, UObject* Object, class UAnimSequenceBase* AnimSequence, UMovieSceneTrack* Track, int32 RowIndex )
{
	FKeyPropertyResult KeyPropertyResult;

	FFindOrCreateHandleResult HandleResult = FindOrCreateHandleToObject( Object );
	FGuid ObjectHandle = HandleResult.Handle;
	KeyPropertyResult.bHandleCreated |= HandleResult.bWasCreated;
	if (ObjectHandle.IsValid())
	{
		if (!Track)
		{
			Track = AddTrack(GetSequencer()->GetFocusedMovieSceneSequence()->GetMovieScene(), ObjectHandle, UMovieSceneSkeletalAnimationTrack::StaticClass(), NAME_None);
			KeyPropertyResult.bTrackCreated = true;
		}

		if (ensure(Track))
		{
			Track->Modify();

			UMovieSceneSection* NewSection = Cast<UMovieSceneSkeletalAnimationTrack>(Track)->AddNewAnimationOnRow( KeyTime, AnimSequence, RowIndex );
			KeyPropertyResult.bTrackModified = true;
			KeyPropertyResult.SectionsCreated.Add(NewSection);

			GetSequencer()->EmptySelection();
			GetSequencer()->SelectSection(NewSection);
			GetSequencer()->ThrobSectionSelection();
		}
	}

	return KeyPropertyResult;
}

void CopyInterpAnimControlTrack(TSharedRef<ISequencer> Sequencer, UInterpTrackAnimControl* MatineeAnimControlTrack, UMovieSceneSkeletalAnimationTrack* SkeletalAnimationTrack)
{
	FFrameNumber EndPlaybackRange = UE::MovieScene::DiscreteExclusiveUpper(Sequencer.Get().GetFocusedMovieSceneSequence()->GetMovieScene()->GetPlaybackRange());

	if (FMatineeImportTools::CopyInterpAnimControlTrack(MatineeAnimControlTrack, SkeletalAnimationTrack, EndPlaybackRange))
	{
		Sequencer.Get().NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::MovieSceneStructureItemAdded );
	}
}

void FSkeletalAnimationTrackEditor::BuildTrackContextMenu( FMenuBuilder& MenuBuilder, UMovieSceneTrack* Track )
{
	UInterpTrackAnimControl* MatineeAnimControlTrack = nullptr;
	for ( UObject* CopyPasteObject : GUnrealEd->MatineeCopyPasteBuffer )
	{
		MatineeAnimControlTrack = Cast<UInterpTrackAnimControl>( CopyPasteObject );
		if ( MatineeAnimControlTrack != nullptr )
		{
			break;
		}
	}
	UMovieSceneSkeletalAnimationTrack* SkeletalAnimationTrack = Cast<UMovieSceneSkeletalAnimationTrack>( Track );
	MenuBuilder.AddMenuEntry(
		NSLOCTEXT( "Sequencer", "PasteMatineeAnimControlTrack", "Paste Matinee SkeletalAnimation Track" ),
		NSLOCTEXT( "Sequencer", "PasteMatineeAnimControlTrackTooltip", "Pastes keys from a Matinee float track into this track." ),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateStatic( &CopyInterpAnimControlTrack, GetSequencer().ToSharedRef(), MatineeAnimControlTrack, SkeletalAnimationTrack ),
			FCanExecuteAction::CreateLambda( [=]()->bool { return MatineeAnimControlTrack != nullptr && MatineeAnimControlTrack->AnimSeqs.Num() > 0 && SkeletalAnimationTrack != nullptr; } ) ) );
}

TSharedPtr<SWidget> FSkeletalAnimationTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(ObjectBinding, GetSequencer());

	if (Skeleton)
	{
		// Create a container edit box
		return SNew(SHorizontalBox)

		// Add the animation combo box
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			FSequencerUtilities::MakeAddButton(LOCTEXT("AnimationText", "Animation"), FOnGetContent::CreateSP(this, &FSkeletalAnimationTrackEditor::BuildAnimationSubMenu, ObjectBinding, Skeleton, Track), Params.NodeIsHovered, GetSequencer())
		];
	}

	else
	{
		return TSharedPtr<SWidget>();
	}
}

bool FSkeletalAnimationTrackEditor::OnAllowDrop(const FDragDropEvent& DragDropEvent, UMovieSceneTrack* Track, int32 RowIndex, const FGuid& TargetObjectGuid)
{
	if (!Track->IsA(UMovieSceneSkeletalAnimationTrack::StaticClass()))
	{
		return false;
	}

	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();

	if (!Operation.IsValid() || !Operation->IsOfType<FAssetDragDropOp>() )
	{
		return false;
	}
	
	if (!TargetObjectGuid.IsValid())
	{
		return false;
	}

	USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(TargetObjectGuid, GetSequencer());

	TSharedPtr<FAssetDragDropOp> DragDropOp = StaticCastSharedPtr<FAssetDragDropOp>( Operation );

	for (const FAssetData& AssetData : DragDropOp->GetAssets())
	{
		UAnimSequenceBase* AnimSequence = Cast<UAnimSequenceBase>(AssetData.GetAsset());

		const bool bValidAnimSequence = AnimSequence && AnimSequence->CanBeUsedInComposition();
		if (bValidAnimSequence && Skeleton && Skeleton == AnimSequence->GetSkeleton())
		{
			return true;
		}
	}

	return false;
}


FReply FSkeletalAnimationTrackEditor::OnDrop(const FDragDropEvent& DragDropEvent, UMovieSceneTrack* Track, int32 RowIndex, const FGuid& TargetObjectGuid)
{
	if (!Track->IsA(UMovieSceneSkeletalAnimationTrack::StaticClass()))
	{
		return FReply::Unhandled();
	}

	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();

	if (!Operation.IsValid() || !Operation->IsOfType<FAssetDragDropOp>() )
	{
		return FReply::Unhandled();
	}
	
	if (!TargetObjectGuid.IsValid())
	{
		return FReply::Unhandled();
	}

	USkeleton* Skeleton = AcquireSkeletonFromObjectGuid(TargetObjectGuid, GetSequencer());

	const FScopedTransaction Transaction(LOCTEXT("DropAssets", "Drop Assets"));

	TSharedPtr<FAssetDragDropOp> DragDropOp = StaticCastSharedPtr<FAssetDragDropOp>( Operation );

	FMovieSceneTrackEditor::BeginKeying();

	bool bAnyDropped = false;
	for (const FAssetData& AssetData : DragDropOp->GetAssets())
	{
		UAnimSequenceBase* AnimSequence = Cast<UAnimSequenceBase>(AssetData.GetAsset());
		const bool bValidAnimSequence = AnimSequence && AnimSequence->CanBeUsedInComposition();
		if (bValidAnimSequence && Skeleton && Skeleton == AnimSequence->GetSkeleton())
		{
			UObject* Object = GetSequencer()->FindSpawnedObjectOrTemplate(TargetObjectGuid);
				
			AnimatablePropertyChanged( FOnKeyProperty::CreateRaw(this, &FSkeletalAnimationTrackEditor::AddKeyInternal, Object, AnimSequence, Track, RowIndex));

			bAnyDropped = true;
		}
	}

	FMovieSceneTrackEditor::EndKeying();

	return bAnyDropped ? FReply::Handled() : FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
