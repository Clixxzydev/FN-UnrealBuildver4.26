// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Misc/Guid.h"
#include "Misc/Attribute.h"
#include "Layout/Visibility.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "SequencerNodeTree.h"
#include "DisplayNodes/SequencerDisplayNode.h"
#include "UObject/GCObject.h"
#include "MovieSceneSequenceID.h"
#include "IMovieScenePlayer.h"
#include "ITimeSlider.h"
#include "Framework/Commands/UICommandList.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Animation/CurveHandle.h"
#include "Animation/CurveSequence.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "TickableEditorObject.h"
#include "EditorUndoClient.h"
#include "KeyPropertyParams.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "ISequencerObjectChangeListener.h"
#include "SequencerSelection.h"
#include "SequencerSelectionPreview.h"
#include "SequencerCustomizationManager.h"
#include "Editor/EditorWidgets/Public/ITransportControl.h"
#include "Evaluation/MovieSceneSequenceTransform.h"
#include "Evaluation/MovieScenePlayback.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "LevelEditor.h"
#include "AcquiredResources.h"
#include "SequencerSettings.h"
#include "Curves/RichCurve.h"
#include "Sections/MovieScene3DTransformSection.h"

class AActor;
class ACameraActor;
class APlayerController;
class FLevelEditorViewportClient;
class FMenuBuilder;
class FMovieSceneClipboard;
class FSequencerObjectBindingNode;
class FSequencerTrackNode;
class FViewportClient;
class IDetailKeyframeHandler;
class IAssetViewport;
class IMenu;
class FCurveEditor;
class ISequencerEditTool;
class FSequencerKeyCollection;
class FObjectBindingTagCache;
class ISequencerTrackEditor;
class ISequencerEditorObjectBinding;
class SSequencer;
class ULevel;
class UMovieSceneSequence;
class UMovieSceneSubSection;
class USequencerSettings;
class UMovieSceneCopyableBinding;
class UMovieSceneCompiledDataManager;
class UMovieSceneCopyableTrack;
struct FMovieSceneTimeController;
struct FMovieScenePossessable;
struct FTransformData;
struct ISequencerHotspot;
struct FKeyAttributes;
struct FNotificationInfo;

enum class EMapChangeType : uint8;

/**
 * Sequencer is the editing tool for MovieScene assets.
 */
class FSequencer final
	: public ISequencer
	, public FGCObject
	, public FEditorUndoClient
	, public FTickableEditorObject
{
public:

	/** Constructor */
	FSequencer();

	/** Virtual destructor */
	virtual ~FSequencer();

public:

	/**
	 * Initializes sequencer
	 *
	 * @param InitParams Initialization parameters.
	 * @param InObjectChangeListener The object change listener to use.
	 * @param TrackEditorDelegates Delegates to call to create auto-key handlers for this sequencer.
	 * @param EditorObjectBindingDelegates Delegates to call to create object bindings for this sequencer.
	 */
	void InitSequencer(const FSequencerInitParams& InitParams, const TSharedRef<ISequencerObjectChangeListener>& InObjectChangeListener, const TArray<FOnCreateTrackEditor>& TrackEditorDelegates, const TArray<FOnCreateEditorObjectBinding>& EditorObjectBindingDelegates);

	/** @return The current view range */
	virtual FAnimatedRange GetViewRange() const override;
	virtual void SetViewRange(TRange<double> NewViewRange, EViewRangeInterpolation Interpolation = EViewRangeInterpolation::Animated) override;

	/** @return The current clamp range */
	FAnimatedRange GetClampRange() const;
	virtual void SetClampRange(TRange<double> InNewClampRange) override;

public:

	virtual TRange<FFrameNumber> GetSelectionRange() const override;

	/**
	 * Set the selection selection range.
	 *
	 * @param Range The new range to set.
	 * @see GetSelectionRange, SetSelectionRangeEnd, SetSelectionRangeStart
	 */
	void SetSelectionRange(TRange<FFrameNumber> Range);
	
	virtual void SetSelectionRangeEnd() override;

	virtual void SetSelectionRangeStart() override;

	/** Clear and reset the selection range. */
	void ResetSelectionRange();

	/** Select all keys that fall into the current selection range. */
	void SelectInSelectionRange(bool bSelectKeys, bool bSelectSections);

	/**
	 * Get the currently viewed sub sequence range
	 *
	 * @return The sub sequence range, or an empty optional if we're viewing the root.
	 */
	TOptional<TRange<FFrameNumber>> GetSubSequenceRange() const;

	/**
	 * Compute a major grid interval and number of minor divisions to display
	 */
	bool GetGridMetrics(const float PhysicalWidth, const double InViewStart, const double InViewEnd, double& OutMajorInterval, int32& OutMinorDivisions) const;

public:

	/**
	 * Get the playback range.
	 *
	 * @return Playback range.
	 * @see SetPlaybackRange, SetPlaybackRangeEnd, SetPlaybackRangeStart
	 */
	TRange<FFrameNumber> GetPlaybackRange() const;

	/**
	 * Set the playback range.
	 *
	 * @param Range The new range to set.
	 * @see GetPlaybackRange, SetPlaybackRangeEnd, SetPlaybackRangeStart
	 */
	void SetPlaybackRange(TRange<FFrameNumber> Range);
	
	/**
	 * Set the selection range to the next or previous shot's range.
	 *
	 */	
	void SetSelectionRangeToShot(const bool bNextShot);

	/**
	 * Set the playback range to all the shot's playback ranges.
	 *
	 */	
	void SetPlaybackRangeToAllShots();

public:

	bool IsPlaybackRangeLocked() const;
	void TogglePlaybackRangeLocked();
	void ResetViewRange();
	void ZoomViewRange(float InZoomDelta);
	void ZoomInViewRange();
	void ZoomOutViewRange();

public:
	/** Gets the tree of nodes which is used to populate the animation outliner. */
	TSharedRef<FSequencerNodeTree> GetNodeTree()
	{
		return NodeTree;
	}

	FObjectBindingTagCache* GetObjectBindingTagCache()
	{
		return ObjectBindingTagCache.Get();
	}

	bool IsPerspectiveViewportPossessionEnabled() const override
	{
		return bPerspectiveViewportPossessionEnabled;
	}

	bool IsPerspectiveViewportCameraCutEnabled() const override
	{
		return bPerspectiveViewportCameraCutEnabled;
	}

	/**
	 * Pops the current focused movie scene from the stack.  The parent of this movie scene will be come the focused one
	 */
	void PopToSequenceInstance( FMovieSceneSequenceIDRef SequenceID );

	/** Deletes the passed in sections. */
	void DeleteSections(const TSet<TWeakObjectPtr<UMovieSceneSection> > & Sections);

	/** Deletes the currently selected in keys. */
	void DeleteSelectedKeys();

	/** Set interpolation modes. */
	void SetInterpTangentMode(ERichCurveInterpMode InterpMode, ERichCurveTangentMode TangentMode);

	/** Toggle tangent weight mode. */
	void ToggleInterpTangentWeightMode();

	/** Snap the currently selected keys to frame. */
	void SnapToFrame();

	/** Are there keys to snap? */
	bool CanSnapToFrame() const;

	/** Transform the selected keys and sections */
	void TransformSelectedKeysAndSections(FFrameTime InDeltaTime, float InScale);

	/** Translate the selected keys and section by the time snap interval */
	void TranslateSelectedKeysAndSections(bool bTranslateLeft);

	/** Stretch time*/
	void StretchTime(FFrameTime InDeltaTime);

	/** Shrink time*/
	void ShrinkTime(FFrameTime InDeltaTime);

	/** Bake transform */
	void BakeTransform();

	/** Sync using source timecode */
	void SyncSectionsUsingSourceTimecode();

	/**
	 * @return Movie scene tools used by the sequencer
	 */
	const TArray<TSharedPtr<ISequencerTrackEditor>>& GetTrackEditors() const
	{
		return TrackEditors;
	}

public:

	/** @return The set of vertical frames */
	TSet<FFrameNumber> GetVerticalFrames() const;

	/** @return The set of marked frames */
	TArray<FMovieSceneMarkedFrame> GetMarkedFrames() const;

	TArray<FMovieSceneMarkedFrame> GetGlobalMarkedFrames() const;
	void InvalidateGlobalMarkedFramesCache() { bGlobalMarkedFramesCached = false; }
	void UpdateGlobalMarkedFramesCache();

	/** 
	  * Disables all global marked frames from all sub-sequences
	  */
	void ClearGlobalMarkedFrames();

protected:

	/** Set/Clear a Mark at the current time */
	void ToggleMarkAtPlayPosition();
	void StepToNextMark();
	void StepToPreviousMark();

	/**
	 * @param InMarkIndex The marked frame index to set
	 * @param InFrameNumber The FrameNumber in Ticks
	 */
	void SetMarkedFrame(int32 InMarkIndex, FFrameNumber InFrameNumber);

	/**
	 * @param	FrameNumber The FrameNumber in Ticks
	 */
	void AddMarkedFrame(FFrameNumber FrameNumber);

	/**
	 * @param InMarkIndex The marked frame index to delete
     */
	void DeleteMarkedFrame(int32 InMarkIndex);

	void DeleteAllMarkedFrames();

public:

	/**
	 * Converts the specified possessable GUID to a spawnable
	 *
	 * @param	PossessableGuid		The guid of the possessable to convert
	 */
	void ConvertToSpawnable(TSharedRef<FSequencerObjectBindingNode> NodeToBeConverted);

	/**
	 * Converts the specified spawnable GUID to a possessable
	 *
	 * @param	SpawnableGuid		The guid of the spawnable to convert
	 */
	void ConvertToPossessable(TSharedRef<FSequencerObjectBindingNode> NodeToBeConverted);

	/**
	 * Converts all the currently selected nodes to be spawnables, if possible
	 */
	void ConvertSelectedNodesToSpawnables();

	/**
	 * Converts all the currently selected nodes to be possessables, if possible
	 */
	void ConvertSelectedNodesToPossessables();

	/*
	 * Set the spawnable level for the currently selected objects
	 */
	void SetSelectedNodesSpawnableLevel(FName InLevelName);

protected:

	/**
	 * Attempts to add a new spawnable to the MovieScene for the specified asset, class, or actor
	 *
	 * @param	Object	The asset, class, or actor to add a spawnable for
	 * @param	ActorFactory	Optional actor factory to use to create spawnable type
	 *
	 * @return	The spawnable ID, or invalid ID on failure
	 */
	FGuid AddSpawnable( UObject& Object, UActorFactory* ActorFactory = nullptr );

	/**
	 * Save default spawnable state for the currently selected objects
	 */
	void SaveSelectedNodesSpawnableState();

public:

	/** Called when new actors are dropped in the viewport. */
	void OnNewActorsDropped(const TArray<UObject*>& DroppedObjects, const TArray<AActor*>& DroppedActors);

	/**
	 * Call when an asset is dropped into the sequencer. Will proprogate this
	 * to all tracks, and potentially consume this asset
	 * so it won't be added as a spawnable
	 *
	 * @param DroppedAsset		The asset that is dropped in
	 * @param TargetObjectGuid	Object to be targeted on dropping
	 * @return					If true, this asset should be consumed
	 */
	virtual bool OnHandleAssetDropped( UObject* DroppedAsset, const FGuid& TargetObjectGuid );
	
	/**
	 * Called to delete all moviescene data from a node
	 *
	 * @param NodeToBeDeleted	Node with data that should be deleted
	 * @return true if anything was deleted, otherwise false.
	 */
	virtual bool OnRequestNodeDeleted( TSharedRef<const FSequencerDisplayNode> NodeToBeDeleted, const bool bKeepState );

	/** Zooms to the edges of all currently selected sections and keys. */
	void ZoomToFit();

	/** Gets the overlay fading animation curve lerp. */
	float GetOverlayFadeCurve() const;

	/** Gets the command bindings for the sequencer */
	virtual TSharedPtr<FUICommandList> GetCommandBindings(ESequencerCommandBindings Type = ESequencerCommandBindings::Sequencer) const override
	{
		if (Type == ESequencerCommandBindings::Sequencer)
		{
			return SequencerCommandBindings;
		}
		else if (Type == ESequencerCommandBindings::CurveEditor)
		{
			return CurveEditorSharedBindings;
		}

		return SequencerSharedBindings;
	}

	/**
	 * Builds up the sequencer's "Add Track" menu.
	 *
	 * @param MenuBuilder The menu builder to add things to.
	 */
	void BuildAddTrackMenu(FMenuBuilder& MenuBuilder);

	/**
	 * Builds up the object bindings in sequencer's "Add Track" menu.
	 *
	 * @param MenuBuilder The menu builder to add things to.
	 */
	void BuildAddObjectBindingsMenu(FMenuBuilder& MenuBuilder);

	/**
	 * Builds up the track menu for object binding nodes in the outliner
	 * 
	 * @param MenuBuilder	The track menu builder to add things to
	 * @param ObjectBindings The array of object bindings to add tracks to (if there are more than 1 selected)
	 * @param ObjectClass	The class of the selected object
	 */
	void BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass);

	/**
	 * Builds up the edit buttons for object binding nodes in the outliner
	 * 
	 * @param EditBox	    The edit box to add things to
	 * @param ObjectBinding	The object binding of the selected node
	 * @param ObjectClass	The class of the selected object
	 */
	void BuildObjectBindingEditButtons(TSharedPtr<SHorizontalBox> EditBox, const FGuid& ObjectBinding, const UClass* ObjectClass);

	/**
	 * Builds up the menu of node groups to add selected nodes to
	 *
	 * @param MenuBuilder The menu builder to add things to.
	 */
	void BuildAddSelectedToNodeGroupMenu(FMenuBuilder& MenuBuilder);

	/** Called when an actor is dropped into Sequencer */
	void OnActorsDropped( const TArray<TWeakObjectPtr<AActor> >& Actors );

	void RecordSelectedActors();

	/** Functions to push on to the transport controls we use */
	FReply OnRecord();
	FReply OnPlayForward(bool bTogglePlay);
	FReply OnPlayBackward(bool bTogglePlay);
	FReply OnStepForward();
	FReply OnStepBackward();
	FReply OnJumpToStart();
	FReply OnJumpToEnd();
	FReply OnCycleLoopMode();
	FReply SetPlaybackEnd();
	FReply SetPlaybackStart();
	FReply JumpToPreviousKey();
	FReply JumpToNextKey();

	bool CanAddTransformKeysForSelectedObjects() const;
	void OnAddTransformKeysForSelectedObjects(EMovieSceneTransformChannel Channel);

	/** Get the visibility of the record button */
	EVisibility GetRecordButtonVisibility() const;

	/** Delegate handler for recording starting */
	void HandleRecordingStarted(UMovieSceneSequence* Sequence);

	/** Delegate handler for recording finishing */
	void HandleRecordingFinished(UMovieSceneSequence* Sequence);

	/** Set the new global time, accounting for looping options */
	void SetLocalTimeLooped(FFrameTime InTime);

	ESequencerLoopMode GetLoopMode() const;

	EPlaybackMode::Type GetPlaybackMode() const;

	/** @return The toolkit that this sequencer is hosted in (if any) */
	TSharedPtr<IToolkitHost> GetToolkitHost() const { return ToolkitHost.Pin(); }

	const FSequencerHostCapabilities& GetHostCapabilities() const { return HostCapabilities; }

	/** @return Whether or not this sequencer is used in the level editor */
	bool IsLevelEditorSequencer() const { return bIsEditingWithinLevelEditor; }

	/** @return Whether to show the curve editor or not */
	void SetShowCurveEditor(bool bInShowCurveEditor);
	/** @return If the curve editor is currently visible. */
	bool GetCurveEditorIsVisible() const;

	/** Called to save the current movie scene */
	void SaveCurrentMovieScene();

	/** Called to save the current movie scene under a new name */
	void SaveCurrentMovieSceneAs();

	FReply NavigateForward();
	FReply NavigateBackward();
	bool CanNavigateForward() const;
	bool CanNavigateBackward() const;
	FText GetNavigateForwardTooltip() const;
	FText GetNavigateBackwardTooltip() const;

	/** Called when a user executes the assign actor to track menu item */
	void AssignActor(FMenuBuilder& MenuBuilder, FGuid ObjectBinding);
	FGuid DoAssignActor(AActor*const* InActors, int32 NumActors, FGuid ObjectBinding);

	/** Called when a user executes the assign selected to track menu item */
	void AddActorsToBinding(FGuid ObjectBinding, const TArray<AActor*>& InActors);
	void ReplaceBindingWithActors(FGuid ObjectBinding, const TArray<AActor*>& InActors);
	void RemoveActorsFromBinding(FGuid ObjectBinding, const TArray<AActor*>& InActors);
	void RemoveAllBindings(FGuid ObjectBinding);
	void RemoveInvalidBindings(FGuid ObjectBinding);

	/** Called when a user executes the delete node menu item */
	void DeleteNode(TSharedRef<FSequencerDisplayNode> NodeToBeDeleted, const bool bKeepState);
	void DeleteSelectedNodes(const bool bKeepState);

	/** @return The list of nodes which must be moved to move the current selected nodes */
	TArray<TSharedRef<FSequencerDisplayNode> > GetSelectedNodesToMove();

	/** Called when a user executes the move to new folder menu item */
	void MoveSelectedNodesToNewFolder();
	void MoveNodeToFolder(TSharedRef<FSequencerDisplayNode> NodeToMove, UMovieSceneFolder* DestinationFolder);

	/** Called when a user executes the copy track menu item */
	void CopySelectedObjects(TArray<TSharedPtr<FSequencerObjectBindingNode>>& ObjectNodes, /*out*/ FString& ExportedText);
	void CopySelectedTracks(TArray<TSharedPtr<FSequencerTrackNode>>& TrackNodes, /*out*/ FString& ExportedText);
	void ExportObjectsToText(TArray<UObject*> ObjectsToExport, /*out*/ FString& ExportedText);

	/** Called when a user executes the paste track menu item */
	bool CanPaste(const FString& TextToImport);
	/**
	 * Attempts to paste from the clipboard
	 * @return Whether the paste event was handled
	 */
	bool DoPaste(bool bClearSelection = false);
	bool PasteTracks(const FString& TextToImport, TArray<FNotificationInfo>& PasteErrors, bool bClearSelection = false);
	bool PasteSections(const FString& TextToImport, TArray<FNotificationInfo>& PasteErrors);
	bool PasteObjectBindings(const FString& TextToImport, TArray<FNotificationInfo>& PasteErrors, bool bClearSelection = false);

	void ImportTracksFromText(const FString& TextToImport, /*out*/ TArray<UMovieSceneCopyableTrack*>& ImportedTracks);
	void ImportSectionsFromText(const FString& TextToImport, /*out*/ TArray<UMovieSceneSection*>& ImportedSections);
	void ImportObjectBindingsFromText(const FString& TextToImport, /*out*/ TArray<UMovieSceneCopyableBinding*>& ImportedObjects);

	/** Called when a user executes the active node menu item */
	void ToggleNodeActive();
	bool IsNodeActive() const;

	/** Called when a user executes the locked node menu item */
	void ToggleNodeLocked();
	bool IsNodeLocked() const;

	/** Called when a user executes the Group menu item */
	void GroupSelectedSections();
	bool CanGroupSelectedSections() const;

	/** Called when a user executes the Ungroup menu item */
	void UngroupSelectedSections();
	bool CanUngroupSelectedSections() const;

	/** Called when a user executes the set key time for selected keys */
	bool CanSetKeyTime() const;
	void SetKeyTime();
	void OnSetKeyTimeTextCommitted(const FText& InText, ETextCommit::Type CommitInfo);

	/** Called when a user executes the rekey for selected keys */
	bool CanRekey() const;
	void Rekey();

	void SelectKey(UMovieSceneSection* InSection, TSharedPtr<IKeyArea> InKeyArea, FKeyHandle KeyHandle, bool bToggle);

	/** Updates the external selection to match the current sequencer selection. */
	void SynchronizeExternalSelectionWithSequencerSelection();

	/** Updates the sequencer selection to match the current external selection. */
	void SynchronizeSequencerSelectionWithExternalSelection();
		
	/** Updates the sequencer selection to match the list of node paths. */
	void SelectNodesByPath(const TSet<FString>& NodePaths);

	/** Whether the binding is visible in the tree view */
	bool IsBindingVisible(const FMovieSceneBinding& InBinding);

	/** Whether the track is visible in the tree view */
	bool IsTrackVisible(const UMovieSceneTrack* InTrack);

	/** Call when the path to a display node changes, to update anything tracking the node via path */
	void OnNodePathChanged(const FString& OldPath, const FString& NewPath);

	void OnSelectedNodesOnlyChanged();

	TSharedPtr<FCurveEditor> GetCurveEditor() const
	{
		return CurveEditorModel;
	}

	/** Will create a custom menu if FSequencerViewParams::OnBuildCustomContextMenuForGuid is specified. */
	void BuildCustomContextMenuForGuid(FMenuBuilder& MenuBuilder, FGuid ObjectBinding);

public:

	/** Copy the selection, whether it's keys or tracks */
	void CopySelection();

	/** Cut the selection, whether it's keys or tracks */
	void CutSelection();

	/** Duplicate the selection */
	void DuplicateSelection();

	/** Copy the selected keys to the clipboard */
	void CopySelectedKeys();

	/** Copy the selected keys to the clipboard, then delete them as part of an undoable transaction */
	void CutSelectedKeys();

	/** Copy the selected sections to the clipboard */
	void CopySelectedSections();

	/** Copy the selected sections to the clipboard, then delete them as part of an undoable transaction */
	void CutSelectedSections();

	/** Get the in-memory clipboard stack */
	const TArray<TSharedPtr<FMovieSceneClipboard>>& GetClipboardStack() const;

	/** Promote a clipboard to the top of the clipboard stack, and update its timestamp */
	void OnClipboardUsed(TSharedPtr<FMovieSceneClipboard> Clipboard);

	/** Create camera and set it as the current camera cut. */
	void CreateCamera();

	/** Called when a new camera is added. Locks the viewport to the NewCamera is not null. */
	void NewCameraAdded(FGuid CameraGuid, ACameraActor* NewCamera = nullptr);

	/** Attempts to automatically fix up broken actor references in the current scene. */
	void FixActorReferences();

	/** Rebinds all possessable references in the current sequence to update them to the latest referencing mechanism. */
	void RebindPossessableReferences();

	/** Imports the animation from an fbx file. */
	void ImportFBX();
	void ImportFBXOntoSelectedNodes();

	/** Exports the animation to an fbx file. */
	void ExportFBX();

	/** Exports the animation to a camera anim asset. */
	void ExportToCameraAnim();

	/** */
	void ShowReadOnlyError() const;

public:
	
	/** Access the currently active track area edit tool */
	const ISequencerEditTool* GetEditTool() const;

	/** Get the current active hotspot */
	TSharedPtr<ISequencerHotspot> GetHotspot() const;

	/** Set the hotspot to something else */
	void SetHotspot(TSharedPtr<ISequencerHotspot> NewHotspot);

protected:

	/** The current hotspot that can be set from anywhere to initiate drags */
	TSharedPtr<ISequencerHotspot> Hotspot;

public:

	/** Put the sequencer in a horizontally auto-scrolling state with the given rate */
	void StartAutoscroll(float UnitsPerS);

	/** Stop the sequencer from auto-scrolling */
	void StopAutoscroll();

	/** Scroll the sequencer vertically by the specified number of slate units */
	void VerticalScroll(float ScrollAmountUnits);

	/**
	 * Update auto-scroll mechanics as a result of a new time position
	 */
	void UpdateAutoScroll(double NewTime, float ThresholdPercentage = 0.025f);

	/** Autoscrub to destination time */
	void AutoScrubToTime(FFrameTime DestinationTime);

public:

	//~ FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;

public:

	//~ FTickableEditorObject Interface

	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FSequencer, STATGROUP_Tickables); }

public:

	//~ ISequencer Interface

	virtual void Close() override;
	virtual FOnCloseEvent& OnCloseEvent() override { return OnCloseEventDelegate; }
	virtual TSharedRef<SWidget> GetSequencerWidget() const override;
	virtual FMovieSceneSequenceIDRef GetRootTemplateID() const override { return ActiveTemplateIDs[0]; }
	virtual FMovieSceneSequenceIDRef GetFocusedTemplateID() const override { return ActiveTemplateIDs.Top(); }
	virtual UMovieSceneSubSection* FindSubSection(FMovieSceneSequenceID SequenceID) const override;
	virtual UMovieSceneSequence* GetRootMovieSceneSequence() const override;
	virtual UMovieSceneSequence* GetFocusedMovieSceneSequence() const override;
	virtual FMovieSceneRootEvaluationTemplateInstance& GetEvaluationTemplate() override { return RootTemplateInstance; }
	virtual void ResetToNewRootSequence(UMovieSceneSequence& NewSequence) override;
	virtual void FocusSequenceInstance(UMovieSceneSubSection& InSubSection) override;
	virtual void SuppressAutoEvaluation(UMovieSceneSequence* Sequence, const FGuid& InSequenceSignature) override;
	virtual EAutoChangeMode GetAutoChangeMode() const override;
	virtual void SetAutoChangeMode(EAutoChangeMode AutoChangeMode) override;
	virtual EAllowEditsMode GetAllowEditsMode() const override;
	virtual void SetAllowEditsMode(EAllowEditsMode AllowEditsMode) override;
	virtual EKeyGroupMode GetKeyGroupMode() const override;
	virtual void SetKeyGroupMode(EKeyGroupMode) override;
	virtual bool GetKeyInterpPropertiesOnly() const override;
	virtual void SetKeyInterpPropertiesOnly(bool bKeyInterpPropertiesOnly) override;
	virtual EMovieSceneKeyInterpolation GetKeyInterpolation() const override;
	virtual void SetKeyInterpolation(EMovieSceneKeyInterpolation) override;
	virtual bool GetInfiniteKeyAreas() const override;
	virtual void SetInfiniteKeyAreas(bool bInfiniteKeyAreas) override;
	virtual bool GetAutoSetTrackDefaults() const override;
	virtual FQualifiedFrameTime GetLocalTime() const override;
	virtual FQualifiedFrameTime GetGlobalTime() const override;
	virtual uint32 GetLocalLoopIndex() const override;
	virtual void SetLocalTime(FFrameTime Time, ESnapTimeMode SnapTimeMode = ESnapTimeMode::STM_None) override;
	virtual void SetLocalTimeDirectly(FFrameTime NewTime) override;
	virtual void SetGlobalTime(FFrameTime Time) override;
	virtual void RequestInvalidateCachedData() override { bNeedsInvalidateCachedData = true; }
	virtual void RequestEvaluate() override { bNeedsEvaluate = true; }
	virtual void ForceEvaluate() override;
	virtual void SetPerspectiveViewportPossessionEnabled(bool bEnabled) override;
	virtual void SetPerspectiveViewportCameraCutEnabled(bool bEnabled) override;
	virtual void RenderMovie(UMovieSceneSection* InSection) const override;
	virtual void EnterSilentMode() override;
	virtual void ExitSilentMode() override;
	virtual bool IsInSilentMode() const override { return SilentModeCount != 0; }
	virtual FGuid GetHandleToObject(UObject* Object, bool bCreateHandleIfMissing = true, const FName& CreatedFolderName = NAME_None) override;
	virtual ISequencerObjectChangeListener& GetObjectChangeListener() override;
protected:
	virtual void NotifyMovieSceneDataChangedInternal() override;
public:
	virtual void NotifyMovieSceneDataChanged( EMovieSceneDataChangeType DataChangeType ) override;
	virtual void RefreshTree() override;
	virtual void UpdatePlaybackRange() override;
	virtual void SetPlaybackSpeed(float InPlaybackSpeed) override { PlaybackSpeed = InPlaybackSpeed; }
	virtual float GetPlaybackSpeed() const override { return PlaybackSpeed; }
	virtual TArray<FGuid> AddActors(const TArray<TWeakObjectPtr<AActor> >& InActors, bool bSelectActors = true) override;
	virtual void AddSubSequence(UMovieSceneSequence* Sequence) override;
	virtual bool CanKeyProperty(FCanKeyPropertyParams CanKeyPropertyParams) const override;
	virtual void KeyProperty(FKeyPropertyParams KeyPropertyParams) override;
	virtual FSequencerSelection& GetSelection() override;
	virtual FSequencerSelectionPreview& GetSelectionPreview() override;
	virtual void SuspendSelectionBroadcast() override;
	virtual void ResumeSelectionBroadcast() override;
	virtual void GetSelectedTracks(TArray<UMovieSceneTrack*>& OutSelectedTracks) override;
	virtual void GetSelectedSections(TArray<UMovieSceneSection*>& OutSelectedSections) override;
	virtual void GetSelectedFolders(TArray<UMovieSceneFolder*>& OutSelectedFolders) override;
	virtual void GetSelectedKeyAreas(TArray<const IKeyArea*>& OutSelectedKeyAreas)  override;
	virtual void GetSelectedObjects(TArray<FGuid>& OutObjects) override;
	virtual void SelectObject(FGuid ObjectBinding) override;
	virtual void SelectTrack(UMovieSceneTrack* Track) override;
	virtual void SelectSection(UMovieSceneSection* Section) override;
	virtual void SelectFolder(UMovieSceneFolder* Folder) override;
	virtual void SelectByPropertyPaths(const TArray<FString>& InPropertyPaths) override;
	virtual void SelectByChannels(UMovieSceneSection* Section, TArrayView<const FMovieSceneChannelHandle> InChannels, bool bSelectParentInstead, bool bSelect) override;
	virtual void SelectByNthCategoryNode(UMovieSceneSection* Section, int Index, bool bSelect) override;
	virtual void EmptySelection() override;
	virtual void ThrobKeySelection() override;
	virtual void ThrobSectionSelection() override;
	virtual FOnGlobalTimeChanged& OnGlobalTimeChanged() override { return OnGlobalTimeChangedDelegate; }
	virtual FOnPlayEvent& OnPlayEvent() override { return OnPlayDelegate; }
	virtual FOnStopEvent& OnStopEvent() override { return OnStopDelegate; }
	virtual FOnBeginScrubbingEvent& OnBeginScrubbingEvent() override { return OnBeginScrubbingDelegate; }
	virtual FOnEndScrubbingEvent& OnEndScrubbingEvent() override { return OnEndScrubbingDelegate; }
	virtual FOnMovieSceneDataChanged& OnMovieSceneDataChanged() override { return OnMovieSceneDataChangedDelegate; }
	virtual FOnMovieSceneBindingsChanged& OnMovieSceneBindingsChanged() override { return OnMovieSceneBindingsChangedDelegate; }
	virtual FOnMovieSceneBindingsPasted& OnMovieSceneBindingsPasted() override { return OnMovieSceneBindingsPastedDelegate; }
	virtual FOnSelectionChangedObjectGuids& GetSelectionChangedObjectGuids() override { return OnSelectionChangedObjectGuidsDelegate; }
	virtual FOnSelectionChangedTracks& GetSelectionChangedTracks() override { return OnSelectionChangedTracksDelegate; }
	virtual FOnCurveDisplayChanged& GetCurveDisplayChanged() override { return OnCurveDisplayChanged; }
	virtual FOnSelectionChangedSections& GetSelectionChangedSections() override { return OnSelectionChangedSectionsDelegate; }
	virtual FGuid CreateBinding(UObject& InObject, const FString& InName) override;
	virtual UObject* GetPlaybackContext() const override;
	virtual TArray<UObject*> GetEventContexts() const override; 
	virtual FOnActorAddedToSequencer& OnActorAddedToSequencer() override;
	virtual FOnPreSave& OnPreSave() override;
	virtual FOnPostSave& OnPostSave() override;
	virtual FOnActivateSequence& OnActivateSequence() override;
	virtual FOnCameraCut& OnCameraCut() override;
	virtual TSharedRef<INumericTypeInterface<double>> GetNumericTypeInterface() const override;
	virtual TSharedRef<SWidget> MakeTransportControls(bool bExtended) override;
	virtual FReply OnPlay(bool bTogglePlay = true) override;
	virtual void Pause() override;
	virtual TSharedRef<SWidget> MakeTimeRange(const TSharedRef<SWidget>& InnerContent, bool bShowWorkingRange, bool bShowViewRange, bool bShowPlaybackRange) override;
	virtual UObject* FindSpawnedObjectOrTemplate(const FGuid& BindingId) override;
	virtual FGuid MakeNewSpawnable(UObject& SourceObject, UActorFactory* ActorFactory = nullptr, bool bSetupDefaults = true) override;
	virtual bool IsReadOnly() const override;
	virtual void ExternalSelectionHasChanged() override { SynchronizeSequencerSelectionWithExternalSelection(); }
	virtual void ObjectImplicitlyAdded(UObject* InObject) const override;
	/** Access the user-supplied settings object */
	virtual USequencerSettings* GetSequencerSettings() override { return Settings; }
	virtual void SetSequencerSettings(USequencerSettings* InSettings) override { Settings = InSettings; }
	virtual TSharedPtr<class ITimeSlider> GetTopTimeSliderWidget() const override;
	virtual void ResetTimeController() override;
	virtual void SetFilterOn(const FText& InName, bool bOn) override;


public:

	// IMovieScenePlayer interface

	virtual void UpdateCameraCut(UObject* CameraObject, const EMovieSceneCameraCutParams& CameraCutParams) override;
	virtual void NotifyBindingsChanged() override;
	virtual void SetViewportSettings(const TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) override;
	virtual void GetViewportSettings(TMap<FViewportClient*, EMovieSceneViewportParams>& ViewportParamsMap) const override;
	virtual EMovieScenePlayerStatus::Type GetPlaybackStatus() const override;
	virtual void SetPlaybackStatus(EMovieScenePlayerStatus::Type InPlaybackStatus) override;
	virtual FMovieSceneSpawnRegister& GetSpawnRegister() override { return *SpawnRegister; }
	virtual bool IsPreview() const override { return SilentModeCount != 0; }

protected:

	/** Reevaluate the sequence at the current time */
	void EvaluateInternal(FMovieSceneEvaluationRange InRange, bool bHasJumped = false);

	/** Reset data about a movie scene when pushing or popping a movie scene. */
	void ResetPerMovieSceneData();

	/** Update the time bounds to the focused movie scene */
	void UpdateTimeBoundsToFocusedMovieScene();

	/**
	 * Gets the far time boundaries of the currently edited movie scene
	 * If the scene has shots, it only takes the shot section boundaries
	 * Otherwise, it finds the furthest boundaries of all sections
	 */
	TRange<FFrameNumber> GetTimeBounds() const;
	
	/**
	 * Gets the time boundaries of the currently filtering shot sections.
	 * If there are no shot filters, an empty range is returned.
	 */
	TRange<float> GetFilteringShotsTimeBounds() const;

	/**
	 * Called when the clamp range is changed by the user
	 *
	 * @param	NewClampRange The new clamp range
	 */
	void OnClampRangeChanged( TRange<double> NewClampRange );

	/*
	 * Called to get the nearest key
	 *
	 * @param InTime The time to get the nearest key to
	 * @param bSearchAllTracks If true this will search all tracks for a potential nearest.
	 *						 False will return keys only from the currently selected track.
	 * @return NearestKey
	 */
	FFrameNumber OnGetNearestKey(FFrameTime InTime, bool bSearchAllTracks);

	/**
	 * Called when the scrub position is changed by the user
	 * This will stop any playback from happening
	 *
	 * @param NewScrubPosition	The new scrub position
	 */
	void OnScrubPositionChanged( FFrameTime NewScrubPosition, bool bScrubbing );

	/** Called when the user has begun scrubbing */
	void OnBeginScrubbing();

	/** Called when the user has finished scrubbing */
	void OnEndScrubbing();

	/** Called when the user has begun dragging the playback range */
	void OnPlaybackRangeBeginDrag();

	/** Called when the user has finished dragging the playback range */
	void OnPlaybackRangeEndDrag();

	/** Called when the user has begun dragging the selection range */
	void OnSelectionRangeBeginDrag();

	/** Called when the user has finished dragging the selection range */
	void OnSelectionRangeEndDrag();

	/** Called when the user has begun dragging a mark */
	void OnMarkBeginDrag();

	/** Called when the user has finished dragging a mark */
	void OnMarkEndDrag();

	/** Get the unqualified local time */
	FFrameTime GetLocalFrameTime() const { return GetLocalTime().Time; }

	/** Get the frame time text */
	FString GetFrameTimeText() const;

	/** The parent sequence that the scrub position display text is relative to */
	FMovieSceneSequenceID GetScrubPositionParent() const;
	
	/** The parent sequence chain of the current sequence */
	TArray<FMovieSceneSequenceID> GetScrubPositionParentChain() const;
	
	/** Called when the scrub position parent sequence is changed */
	void OnScrubPositionParentChanged(FMovieSceneSequenceID InScrubPositionParent);

	/** Exports sequence to a FBX file */
	void ExportFBXInternal(const FString& Filename, TArray<FGuid>& Bindings);

protected:

	/**
	 * Ensure that the specified local time is in the view
	 */
	void ScrollIntoView(float InLocalTime);

	/**
	 * Calculates the amount of encroachment the specified time has into the autoscroll region, if any
	 */
	TOptional<float> CalculateAutoscrollEncroachment(double NewTime, float ThresholdPercentage = 0.1f) const;

	/** Called to toggle auto-scroll on and off */
	void OnToggleAutoScroll();

	/**
	 * Whether auto-scroll is enabled.
	 *
	 * @return true if auto-scroll is enabled, false otherwise.
	 */
	bool IsAutoScrollEnabled() const;

	/** Find the viewed sequence asset in the content browser. */
	void FindInContentBrowser();

	/** Get the asset we're currently editing, if applicable. */
	UObject* GetCurrentAsset() const;

protected:

	/** Get all the keys for the current sequencer selection */
	virtual void GetKeysFromSelection(TUniquePtr<FSequencerKeyCollection>& KeyCollection, float DuplicateThresholdSeconds) override;

	void GetAllKeys(TUniquePtr<FSequencerKeyCollection>& KeyCollection, float DuplicateThresoldSeconds);

	UMovieSceneSection* FindNextOrPreviousShot(UMovieSceneSequence* Sequence, FFrameNumber SearchFromTime, const bool bNext) const;

protected:
	
	/** Called when a user executes the delete command to delete sections or keys */
	void DeleteSelectedItems();
	
	/** Transport controls */
	void TogglePlay();
	void JumpToStart();
	void JumpToEnd();
	void ShuttleForward();
	void ShuttleBackward();
	void StepForward();
	void StepBackward();
	void StepToNextKey();
	void StepToPreviousKey();
	void StepToNextCameraKey();
	void StepToPreviousCameraKey();
	void StepToNextShot();
	void StepToPreviousShot();

	void ExpandAllNodesAndDescendants();
	void CollapseAllNodesAndDescendants();

	/** Expand or collapse selected nodes */
	void ToggleExpandCollapseNodes();

	/** Expand or collapse selected nodes and descendants*/
	void ToggleExpandCollapseNodesAndDescendants();

	/** Sort all nodes and their descendants by category then alphabetically */
	void SortAllNodesAndDescendants();

	/** Add selected actors to sequencer */
	void AddSelectedActors();

	/** Manually sets a key for the selected objects at the current time */
	void SetKey();

	/** Modeless Version of the String Entry Box */
	void GenericTextEntryModeless(const FText& DialogText, const FText& DefaultText, FOnTextCommitted OnTextComitted);
	
	/** Closes the popup created by GenericTextEntryModeless*/
	void CloseEntryPopupMenu();

	/** Trim a section to the left or right */
	void TrimSection(bool bTrimLeft);

	/** Split a section */
	void SplitSection();

	/** Generates command bindings for UI commands */
	void BindCommands();

	//~ Begin FEditorUndoClient Interface
	virtual bool MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjects) const override;
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
	// End of FEditorUndoClient

	void OnSelectedOutlinerNodesChanged();

	void AddNodeGroupsCollectionChangedDelegate();
	void RemoveNodeGroupsCollectionChangedDelegate();

	void OnNodeGroupsCollectionChanged();

public:
	void AddSelectedNodesToNewNodeGroup();
	void AddSelectedNodesToExistingNodeGroup(UMovieSceneNodeGroup* NodeGroup);
	void AddNodesToExistingNodeGroup(const TArray<TSharedRef<FSequencerDisplayNode>>& Nodes, UMovieSceneNodeGroup* NodeGroup);

private:

	/** Updates a viewport client from camera cut data */
	void UpdatePreviewLevelViewportClientFromCameraCut(FLevelEditorViewportClient& InViewportClient, UObject* InCameraObject, const EMovieSceneCameraCutParams& CameraCutParams);

	/** Expands Possessables with multiple bindings into indidual Possessables for each binding */
	TArray<FGuid> ExpandMultiplePossessableBindings(FGuid PossessableGuid);

	/** Internal conversion function that doesn't perform expensive reset/update tasks */
	TArray<FMovieSceneSpawnable*> ConvertToSpawnableInternal(FGuid PossessableGuid);

	/** Internal conversion function that doesn't perform expensive reset/update tasks */
	FMovieScenePossessable* ConvertToPossessableInternal(FGuid SpawnableGuid);

	/** Recurses through a folder to replace converted GUID with new GUID */
	bool ReplaceFolderBindingGUID(UMovieSceneFolder *Folder, FGuid Original, FGuid Converted);

	/** Internal function to render movie for a given start/end time */
	void RenderMovieInternal(TRange<FFrameNumber> Range, bool bSetFrameOverrides = false) const;

	/** Handles adding a new folder to the outliner tree. */
	void OnAddFolder();

	/** Handles loading in previously recorded data. */
	void OnLoadRecordedData();

	/** Adds the track to the selected folder (if FGuid is invalid) and selects the track, throbs it, and notifies the sequence to rebuild any necessary data. */
	void OnAddTrack(const TWeakObjectPtr<UMovieSceneTrack>& InTrack, const FGuid& ObjectBinding) override;

	/** Determines the selected parent folders and returns the node path to the first folder. Also expands the first folder. */
	void CalculateSelectedFolderAndPath(TArray<UMovieSceneFolder*>& OutSelectedParentFolders, FString& OutNewNodePath);

	/** Returns the tail folder from the given Folder Path, creating each folder if needed. */
	UMovieSceneFolder* CreateFoldersRecursively(const TArray<FString>& FolderPaths, int32 FolderPathIndex, UMovieScene* OwningMovieScene, UMovieSceneFolder* ParentFolder, const TArray<UMovieSceneFolder*>& FoldersToSearch);

	/** Create set playback start transport control */
	TSharedRef<SWidget> OnCreateTransportSetPlaybackStart();

	/** Create jump to previous key transport control */
	TSharedRef<SWidget> OnCreateTransportJumpToPreviousKey();

	/** Create jump to next key transport control */
	TSharedRef<SWidget> OnCreateTransportJumpToNextKey();

	/** Create set playback end transport control */
	TSharedRef<SWidget> OnCreateTransportSetPlaybackEnd();

	/** Select keys and/or sections in a display node that fall into the current selection range. */
	void SelectInSelectionRange(const TSharedRef<FSequencerDisplayNode>& DisplayNode, const TRange<FFrameNumber>& SelectionRange, bool bSelectKeys, bool bSelectSections);
	
	/** Create loop mode transport control */
	TSharedRef<SWidget> OnCreateTransportLoopMode();

	/** Create record transport control */
	TSharedRef<SWidget> OnCreateTransportRecord();

	/** Possess PIE viewports with the specified camera settings (a mirror of level viewport possession, but for game viewport clients) */
	void PossessPIEViewports(UObject* CameraObject, const EMovieSceneCameraCutParams& CameraCutParams);

	/** Update the locked subsequence range (displayed as playback range for subsequences), and root to local transform */
	void UpdateSubSequenceData();

	/** Adjust sequencer customizations based on the currently focused sequence */
	void UpdateSequencerCustomizations();

	/** Rerun construction scripts on bound actors */
	void RerunConstructionScripts();

	/** Get actors that want to rerun construction scripts */
	void GetConstructionScriptActors(UMovieScene*, FMovieSceneSequenceIDRef SequenceID, TSet<TWeakObjectPtr<AActor> >& BoundActors, TArray < TPair<FMovieSceneSequenceID, FGuid> >& BoundGuids);

	/** Check whether we're viewing the master sequence or not */
	bool IsViewingMasterSequence() const { return ActiveTemplateIDs.Num() == 1; }

	/** Get the default key attributes to apply to newly created keys on the curve editor */
	FKeyAttributes GetDefaultKeyAttributes() const;

	/** Recompile any dirty director blueprints in the sequence hierarchy */
	void RecompileDirtyDirectors();

	void ToggleAsyncEvaluation();
	bool UsesAsyncEvaluation();

	void UpdateCachedPlaybackContext();

public:


	/** Helper function which returns how many frames (in tick resolution) one display rate frame represents. */
	double GetDisplayRateDeltaFrameCount() const;

	/** Retrieve the desired scrubber style for this instance. */
	ESequencerScrubberStyle GetScrubStyle() const
	{
		return ScrubStyle;
	}


private:

	/** Update the time bases for the current movie scene */
	void UpdateTimeBases();

	/** View modifier for level editor viewports. */
	void ModifyViewportClientView(FMinimalViewInfo& ViewInfo);

	/** User-supplied settings object for this sequencer */
	USequencerSettings* Settings;

	/** Command list for sequencer commands (Sequencer widgets only). */
	TSharedRef<FUICommandList> SequencerCommandBindings;

	/** Command list for sequencer commands (shared by non-Sequencer). */
	TSharedRef<FUICommandList> SequencerSharedBindings;

	/** Command list privately shared with the Curve Editor to allow a subset of keybinds to have matching behavior there. */
	TSharedRef<FUICommandList> CurveEditorSharedBindings;

	/** List of tools we own */
	TArray<TSharedPtr<ISequencerTrackEditor>> TrackEditors;

	/** List of object bindings we can use */
	TArray<TSharedPtr<ISequencerEditorObjectBinding>> ObjectBindings;

	/** Listener for object changes being made while this sequencer is open*/
	TSharedPtr<ISequencerObjectChangeListener> ObjectChangeListener;

	/** Main sequencer widget */
	TSharedPtr<SSequencer> SequencerWidget;
	
	/** Spawn register for keeping track of what is spawned */
	TSharedPtr<FMovieSceneSpawnRegister> SpawnRegister;

	/** The asset editor that created this Sequencer if any */
	TWeakPtr<IToolkitHost> ToolkitHost;

	/** A copy of the supported features/capabilities we were initialized with. */
	FSequencerHostCapabilities HostCapabilities;
	
	/** Active customizations. */
	TArray<TUniquePtr<ISequencerCustomization>> ActiveCustomizations;

	TWeakObjectPtr<UMovieSceneSequence> RootSequence;
	FMovieSceneRootEvaluationTemplateInstance RootTemplateInstance;

	/** A stack of the current sequence hierarchy for keeping track of nestled sequences. */
	TArray<FMovieSceneSequenceID> ActiveTemplateIDs;

	/** A stack of sequences that have been navigated to. */
	TArray<FMovieSceneSequenceID> TemplateIDForwardStack;
	TArray<FMovieSceneSequenceID> TemplateIDBackwardStack;

	/**
	* The active state of each sequence. A sequence can be in another sequence multiple times
	* and the owning subsection contains the active state of the sequence, so this stack is kept 
	* in sync with the ActiveTemplateIDs as you enter a sequence via the specific subsection node.
	 */
	TArray<bool> ActiveTemplateStates;

	/** Time transformation from the root sequence to the currently edited sequence. */
	FMovieSceneSequenceTransform RootToLocalTransform;

	/** Current loop of the current sub-sequence, if we are in a looping sub-sequence. */
	FMovieSceneWarpCounter RootToLocalLoopCounter;

	/** The time range target to be viewed */
	TRange<double> TargetViewRange;

	/** The last time range that was viewed */
	TRange<double> LastViewRange;

	/** The view range before zooming */
	TRange<double> ViewRangeBeforeZoom;

	/** The amount of autoscroll pan offset that is currently being applied */
	TOptional<float> AutoscrollOffset;

	/** The amount of autoscrub offset that is currently being applied */
	TOptional<float> AutoscrubOffset;

	struct FAutoScrubTarget
	{
		FAutoScrubTarget(FFrameTime InDestinationTime, FFrameTime InSourceTime, double InStartTime)
			: DestinationTime(InDestinationTime)
			, SourceTime(InSourceTime)
			, StartTime(InStartTime) {}
		FFrameTime DestinationTime;
		FFrameTime SourceTime;
		double StartTime;
	};

	TOptional<FAutoScrubTarget> AutoScrubTarget;

	/** Zoom smoothing curves */
	FCurveSequence ZoomAnimation;
	FCurveHandle ZoomCurve;

	/** Overlay fading curves */
	FCurveSequence OverlayAnimation;
	FCurveHandle OverlayCurve;

	/** Whether we are playing, recording, etc. */
	EMovieScenePlayerStatus::Type PlaybackState;

	/** Current play position */
	FMovieScenePlaybackPosition PlayPosition;

	/** Local loop index at the time we began scrubbing */
	uint32 LocalLoopIndexOnBeginScrubbing;

	/** Local loop index to add for the purposes of displaying it in the UI */
	uint32 LocalLoopIndexOffsetDuringScrubbing;

	/** The playback speed */
	float PlaybackSpeed;

	/** The shuttle multiplier */
	float ShuttleMultiplier;

	bool bPerspectiveViewportPossessionEnabled;
	bool bPerspectiveViewportCameraCutEnabled;

	/** True if this sequencer is being edited within the level editor */
	bool bIsEditingWithinLevelEditor;

	/** Whether the sequence should be editable or read only */
	bool bReadOnly;

	/** Scrub style provided on construction */
	ESequencerScrubberStyle ScrubStyle;

	/** Generic Popup Entry */
	TWeakPtr<IMenu> EntryPopupMenu;

	/** Stores a dirty bit for whether the sequencer tree (and other UI bits) may need to be refreshed.  We
	    do this simply to avoid refreshing the UI more than once per frame. (e.g. during live recording where
		the MovieScene data can change many times per frame.) */
	bool bNeedTreeRefresh;

	FSequencerSelection Selection;
	FSequencerSelectionPreview SelectionPreview;

	/** Represents the tree of nodes to display in the animation outliner. */
	TSharedRef<FSequencerNodeTree> NodeTree;

	/** A delegate which is called when the sequencer closes. */
	FOnCloseEvent OnCloseEventDelegate;

	/** A delegate which is called any time the global time changes. */
	FOnGlobalTimeChanged OnGlobalTimeChangedDelegate;

	/** A delegate which is called whenever the user begins playing the sequence. */
	FOnPlayEvent OnPlayDelegate;

	/** A delegate which is called whenever the user stops playing the sequence. */
	FOnStopEvent OnStopDelegate;

	/** A delegate which is called whenever the user begins scrubbing. */
	FOnBeginScrubbingEvent OnBeginScrubbingDelegate;

	/** A delegate which is called whenever the user stops scrubbing. */
	FOnEndScrubbingEvent OnEndScrubbingDelegate;

	/** A delegate which is called any time the movie scene data is changed. */
	FOnMovieSceneDataChanged OnMovieSceneDataChangedDelegate;

	/** A delegate which is called any time the movie scene bindings are changed. */
	FOnMovieSceneBindingsChanged OnMovieSceneBindingsChangedDelegate;

	/** A delegate which is called any time a binding is pasted. */
	FOnMovieSceneBindingsPasted OnMovieSceneBindingsPastedDelegate;

	/** A delegate which is called any time the sequencer selection changes. */
	FOnSelectionChangedObjectGuids OnSelectionChangedObjectGuidsDelegate;

	/** A delegate which is called any time the sequencer selection changes. */
	FOnSelectionChangedTracks OnSelectionChangedTracksDelegate;

	/** A delegate which is called any time the sequencers curve eidtor selection changes. */
	FOnCurveDisplayChanged OnCurveDisplayChanged;

	/** A delegate which is called any time the sequencer selection changes. */
	FOnSelectionChangedSections OnSelectionChangedSectionsDelegate;

	FOnActorAddedToSequencer OnActorAddedToSequencerEvent;
	FOnCameraCut OnCameraCutEvent;
	FOnPreSave OnPreSaveEvent;
	FOnPostSave OnPostSaveEvent;
	FOnActivateSequence OnActivateSequenceEvent;

	/** Delegate for Curve Display Changed Event from the Curve Editor, which we than pass to the FOnCurveDisplayChanged delegate */
	void OnCurveModelDisplayChanged(FCurveModel *InCurveModel, bool bDisplayed);

	int32 SilentModeCount;

	/** When true the sequencer selection is being updated from changes to the external selection. */
	bool bUpdatingSequencerSelection;

	/** When true the external selection is being updated from changes to the sequencer selection. */
	bool bUpdatingExternalSelection;

	/** The maximum tick rate prior to playing (used for overriding delta time during playback). */
	TOptional<double> OldMaxTickRate;

	/** Timing manager that can adjust playback times */
	TSharedPtr<FMovieSceneTimeController> TimeController;

	struct FCachedViewTarget
	{
		/** The player controller we're possessing */
		TWeakObjectPtr<APlayerController> PlayerController;
		/** The view target it was pointing at before we took over */
		TWeakObjectPtr<AActor> ViewTarget;
	};

	/** Cached array of view targets that were set before we possessed the player controller with a camera from sequencer */
	TArray<FCachedViewTarget> PrePossessionViewTargets;

	/** Attribute used to retrieve the playback context for this frame */
	TAttribute<UObject*> PlaybackContextAttribute;

	/** Cached playback context for this frame */
	TWeakObjectPtr<UObject> CachedPlaybackContext;

	/** Attribute used to retrieve event contexts */
	TAttribute<TArray<UObject*>> EventContextsAttribute;

	/** Event contexts retrieved from the above attribute once per frame */
	TArray<TWeakObjectPtr<UObject>> CachedEventContexts;

	/** When true, sequence will be forcefully evaluated on the next tick */
	bool bNeedsEvaluate;

	/** When true, cached data will be invalidated on the next tick */
	bool bNeedsInvalidateCachedData;

	FAcquiredResources AcquiredResources;

	bool bGlobalMarkedFramesCached;
	TArray<FMovieSceneMarkedFrame> GlobalMarkedFramesCache;

	/** The range of the currently displayed sub sequence in relation to its parent section, in the resolution of the current sub sequence */
	TRange<FFrameNumber> SubSequenceRange;

	UMovieSceneCompiledDataManager* CompiledDataManager;

	TMap<FName, TFunction<void()>> CleanupFunctions;

	/** Transient collection of keys that is used for jumping between keys contained within the current selection */
	TUniquePtr<FSequencerKeyCollection> SelectedKeyCollection;

	TSharedPtr<FCurveEditor> CurveEditorModel;

	/** A signature that will suppress auto evaluation when it is the only change dirtying the template. */
	TOptional<TTuple<TWeakObjectPtr<UMovieSceneSequence>, FGuid>> SuppressAutoEvalSignature;

	TUniquePtr<FObjectBindingTagCache> ObjectBindingTagCache;

	struct FCachedViewState
	{
		FCachedViewState()
			: bValid(false)
			, bIsViewportUIHidden(false) {}

	public:
		void StoreViewState();
		void RestoreViewState();

	private:
		bool bValid;
		bool bIsViewportUIHidden;
		TArray<TPair<int32, bool> > GameViewStates;
	};

	FCachedViewState CachedViewState;
	
	struct FViewModifierInfo
	{
		bool bApplyViewModifier = false;
		FVector ViewModifierLocation = FVector::ZeroVector;
		FRotator ViewModifierRotation = FRotator::ZeroRotator;
		float ViewModifierFOV = 0.f;
	};
	/** Information for previewing camera cut blends. This will be applied to the editor viewport during blends. */
	FViewModifierInfo ViewModifierInfo;
	/** Information cached before entering silent mode, so we can restore it afterwards. */
	FViewModifierInfo CachedViewModifierInfo;
	
	/** Original editor camera info, for when previewing a sequence with a blend from/to gameplay. */
	bool bHasPreAnimatedInfo;
	FVector PreAnimatedViewportLocation;
	FRotator PreAnimatedViewportRotation;
	float PreAnimatedViewportFOV;

	TOptional<FMovieSceneSequenceID> ScrubPositionParent;
};
