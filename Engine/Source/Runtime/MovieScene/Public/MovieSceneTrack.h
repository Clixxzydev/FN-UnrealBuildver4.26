// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumClassFlags.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "MovieSceneSignedObject.h"
#include "MovieSceneSection.h"
#include "Misc/InlineValue.h"
#include "Compilation/MovieSceneSegmentCompiler.h"
#include "MovieSceneTrackEvaluationField.h"
#include "MovieSceneTrack.generated.h"

struct FMovieSceneEvaluationTrack;
struct FMovieSceneTrackSegmentBlender;
struct FMovieSceneTrackRowSegmentBlender;
struct IMovieSceneTemplateGenerator;

template<typename> struct TMovieSceneEvaluationTree;

/** Generic evaluation options for any track */
USTRUCT()
struct FMovieSceneTrackEvalOptions
{
	GENERATED_BODY()
	
	FMovieSceneTrackEvalOptions()
		: bCanEvaluateNearestSection(false)
		, bEvalNearestSection(false)
		, bEvaluateInPreroll(false)
		, bEvaluateInPostroll(false)
		, bEvaluateNearestSection_DEPRECATED(false)
	{}

	/** true when the value of bEvalNearestSection is to be considered for the track */
	UPROPERTY()
	uint32 bCanEvaluateNearestSection : 1;

	/** When evaluating empty space on a track, will evaluate the last position of the previous section (if possible), or the first position of the next section, in that order of preference. */
	UPROPERTY(EditAnywhere, Category="General", DisplayName="Evaluate Nearest Section", meta=(EditCondition=bCanEvaluateNearestSection))
	uint32 bEvalNearestSection : 1;

	/** Evaluate this track as part of its parent sub-section's pre-roll, if applicable */
	UPROPERTY(EditAnywhere, Category="General")
	uint32 bEvaluateInPreroll : 1;

	/** Evaluate this track as part of its parent sub-section's post-roll, if applicable */
	UPROPERTY(EditAnywhere, Category="General")
	uint32 bEvaluateInPostroll : 1;

	UPROPERTY()
	uint32 bEvaluateNearestSection_DEPRECATED : 1;
};


/** Generic display options for any track */
USTRUCT()
struct FMovieSceneTrackDisplayOptions
{
	GENERATED_BODY()

	FMovieSceneTrackDisplayOptions()
		: bShowVerticalFrames(false)
	{}

	/** Show bounds as vertical frames */
	UPROPERTY(EditAnywhere, Category = "General")
	uint32 bShowVerticalFrames : 1;
};

/** Returns what kind of section easing we support in the editor */
enum class EMovieSceneTrackEasingSupportFlags
{
	None = 0,
	AutomaticEaseIn = 1 << 0,
	AutomaticEaseOut = 1 << 1,
	ManualEaseIn = 1 << 2,
	ManualEaseOut = 1 << 3,
	AutomaticEasing = AutomaticEaseIn | AutomaticEaseOut,
	ManualEasing = ManualEaseIn | ManualEaseOut,
	All = AutomaticEasing | ManualEasing
};
ENUM_CLASS_FLAGS(EMovieSceneTrackEasingSupportFlags)

/** Parameters for the `SupportsEasing` method */
struct FMovieSceneSupportsEasingParams
{
	// Non-null if we are asking for a specific section.
	const UMovieSceneSection* ForSection;

	FMovieSceneSupportsEasingParams() : ForSection(nullptr) {}
	FMovieSceneSupportsEasingParams(const UMovieSceneSection* InSection) : ForSection(InSection) {}
};

#if WITH_EDITOR
/** Parameters for sections moving in the editor */
struct FMovieSceneSectionMovedParams
{
	EPropertyChangeType::Type MoveType;

	FMovieSceneSectionMovedParams(EPropertyChangeType::Type InMoveType) : MoveType(InMoveType) {}
};
#endif

/**
 * Base class for a track in a Movie Scene
 */
UCLASS(abstract, DefaultToInstanced, MinimalAPI, BlueprintType)
class UMovieSceneTrack
	: public UMovieSceneSignedObject
{
	GENERATED_BODY()

public:

	MOVIESCENE_API UMovieSceneTrack(const FObjectInitializer& InInitializer);

public:

	/** General evaluation options for a given track */
	UPROPERTY(EditAnywhere, Category = "General", meta = (ShowOnlyInnerProperties))
	FMovieSceneTrackEvalOptions EvalOptions;

#if WITH_EDITORONLY_DATA
	/** General display options for a given track */
	UPROPERTY(EditAnywhere, Category = "General", meta = (ShowOnlyInnerProperties))
	FMovieSceneTrackDisplayOptions DisplayOptions;
#endif

	/**
	 * Gets what kind of blending is supported by this section
	 */
	FMovieSceneBlendTypeField GetSupportedBlendTypes() const
	{
		return SupportedBlendTypes;
	}

	/** 
	 * Get compiler rules to use when compiling sections that overlap on the same row.
	 * These define how to deal with overlapping sections and empty space on a row
	 */
	MOVIESCENE_API virtual FMovieSceneTrackRowSegmentBlenderPtr GetRowSegmentBlender() const;

	/** 
	 * Get compiler rules to use when compiling sections that overlap on different rows.
	 * These define how to deal with overlapping sections and empty space at the track level
	 */
	MOVIESCENE_API virtual FMovieSceneTrackSegmentBlenderPtr GetTrackSegmentBlender() const;

	/**
	 * Update all auto-generated easing curves for all sections in this track
	 */
	MOVIESCENE_API void UpdateEasing();

protected:

	//~ UObject interface
	MOVIESCENE_API virtual void PostInitProperties() override;
	MOVIESCENE_API virtual void PostLoad() override;
	MOVIESCENE_API virtual bool IsPostLoadThreadSafe() const override;

	/** Intentionally not a UPROPERTY so this isn't serialized */
	FMovieSceneBlendTypeField SupportedBlendTypes;

	/** Whether evaluation of this track has been disabled via mute/solo */
	UPROPERTY()
	bool bIsEvalDisabled;

public:

	/**
	 * Retrieve a fully up-to-date evaluation field for this track.
	 */
	MOVIESCENE_API const FMovieSceneTrackEvaluationField& GetEvaluationField();

	/**
	 * Retrieve a version number for the logic implemented in PopulateEvaluationTree.
	 *
	 * The evaluation field is cached with the data and is invalidated if the data signature changes, or
	 * if this version number changes. You should bump this version number if you start/stop overriding,
	 * or otherwise change the logic, of PopulateEvaluationTree.
	 */
	virtual int8 GetEvaluationFieldVersion() const { return 0; }

	MOVIESCENE_API FGuid FindObjectBindingGuid() const;

protected:

	enum class ETreePopulationMode : uint8
	{
		None,
		Blended,
		HighPass,
		HighPassPerRow,
	};

	ETreePopulationMode BuiltInTreePopulationMode;

private:

	virtual bool PopulateEvaluationTree(TMovieSceneEvaluationTree<FMovieSceneTrackEvaluationData>& OutData) const
	{
		return false;
	}

private:

	void UpdateEvaluationTree();
	void AddSectionRangesToTree(TArrayView<UMovieSceneSection* const> Sections, TMovieSceneEvaluationTree<FMovieSceneTrackEvaluationData>& OutData);
	void AddSectionPrePostRollRangesToTree(TArrayView<UMovieSceneSection* const> Sections, TMovieSceneEvaluationTree<FMovieSceneTrackEvaluationData>& OutData);

	/** The guid of the object signature that the EvaluationField member relates to */
	UPROPERTY()
	FGuid EvaluationFieldGuid;

#if WITH_EDITORONLY_DATA
	/** The version of the logic in PopulateEvaluationTree when the EvaluationField was cached */
	UPROPERTY()
	int8 EvaluationFieldVersion;
#endif

	/** An array of entries that define when specific sections should be evaluated on this track */
	UPROPERTY()
	FMovieSceneTrackEvaluationField EvaluationField;

public:

	/**
	 * @return The name that makes this track unique from other track of the same class.
	 */
	virtual FName GetTrackName() const { return NAME_None; }

	/**
	 * @return Whether or not this track has any data in it.
	 */
	virtual bool IsEmpty() const PURE_VIRTUAL(UMovieSceneTrack::IsEmpty, return true;);

	/**
	 * Removes animation data.
	 */
	virtual void RemoveAllAnimationData() { }

	/**
	* @return Whether or not this track supports multiple row indices.
	*/
	virtual bool SupportsMultipleRows() const
	{
		return SupportedBlendTypes.Num() != 0;
	}

	virtual EMovieSceneTrackEasingSupportFlags SupportsEasing(FMovieSceneSupportsEasingParams& Params) const
	{
		return SupportedBlendTypes.Num() > 0 ? EMovieSceneTrackEasingSupportFlags::All : EMovieSceneTrackEasingSupportFlags::None;
	}

	/** Set This Section as the one to key. If track doesn't support layered blends then don't implement
	* @param InSection		Section that we want to key
	*/
	 virtual void SetSectionToKey(UMovieSceneSection* InSection) {};

	/** Get the section we want to key. If track doesn't support layered blends it will return nulltpr.
	* @return The section to key if one exists
	*/
	 virtual UMovieSceneSection* GetSectionToKey() const { return nullptr; }

	/** Gets the greatest row index of all the sections owned by this track. */
	MOVIESCENE_API int32 GetMaxRowIndex() const;

	/**
	 * Updates the row indices of sections owned by this track so that all row indices which are used are consecutive with no gaps. 
	 * @return Whether or not fixes were made. 
	 */
	MOVIESCENE_API bool FixRowIndices();

	/**
	* @return Whether evaluation of this track should be disabled due to mute/solo settings
	*/
	MOVIESCENE_API bool IsEvalDisabled() const { return bIsEvalDisabled; };

	/**
	* Called by Sequencer to set whether evaluation of this track should be disabled due to mute/solo settings
	*/
	MOVIESCENE_API void SetEvalDisabled(bool bEvalDisabled) { bIsEvalDisabled = bEvalDisabled; }

public:

	/*
	 * Does this track support this section class type?

	 * @param ClassType The movie scene section class type
	 * @return Whether this track supports this section class type
	 */
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const PURE_VIRTUAL(UMovieSceneTrack::SupportsType, return false;);

	/**
	 * Add a section to this track.
	 *
	 * @param Section The section to add.
	 */
	virtual void AddSection(UMovieSceneSection& Section) PURE_VIRTUAL(UMovieSceneSection::AddSection,);

	/**
	 * Generates a new section suitable for use with this track.
	 *
	 * @return a new section suitable for use with this track.
	 */
	virtual class UMovieSceneSection* CreateNewSection() PURE_VIRTUAL(UMovieSceneTrack::CreateNewSection, return nullptr;);
	
	/**
	 * Called when all the sections of the track need to be retrieved.
	 * 
	 * @return List of all the sections in the track.
	 */
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const PURE_VIRTUAL(UMovieSceneTrack::GetAllSections, static TArray<UMovieSceneSection*> Empty; return Empty;);

	/**
	 * Checks to see if the section is in this track.
	 *
	 * @param Section The section to query for.
	 * @return True if the section is in this track.
	 */
	virtual bool HasSection(const UMovieSceneSection& Section) const PURE_VIRTUAL(UMovieSceneSection::HasSection, return false;);

	/**
	 * Removes a section from this track.
	 *
	 * @param Section The section to remove.
	 */
	virtual void RemoveSection(UMovieSceneSection& Section) PURE_VIRTUAL(UMovieSceneSection::RemoveSection, );

	/**
	 * Removes a section from this track at a particular index
	 *
	 * @param SectionIndex The section index to remove.
	 */
	virtual void RemoveSectionAt(int32 SectionIndex) PURE_VIRTUAL(UMovieSceneSection::RemoveSectionAt, );

#if WITH_EDITORONLY_DATA

	/**
	 * Get the track's display name.
	 *
	 * @return Display name text.
	 */
	virtual FText GetDisplayName() const PURE_VIRTUAL(UMovieSceneTrack::GetDisplayName, return FText::FromString(TEXT("Unnamed Track")););

	/**
	 * Get this track's color tint.
	 *
	 * @return Color Tint.
	 */
	const FColor& GetColorTint() const
	{
		return TrackTint;
	}

	/**
	 * Set this track's color tint.
	 *
	 * @param InTrackTint The color to tint this track.
	 */
	void SetColorTint(const FColor& InTrackTint)
	{
		TrackTint = InTrackTint;
	}

	/**
	 * Get this folder's desired sorting order
	 */
	int32 GetSortingOrder() const
	{
		return SortingOrder;
	}

	/**
	 * Set this folder's desired sorting order.
	 *
	 * @param InSortingOrder The higher the value the further down the list the folder will be.
	 */
	void SetSortingOrder(const int32 InSortingOrder)
	{
		SortingOrder = InSortingOrder;
	}

	/**
	 * @return Whether or not this track supports the creation of default sections when the track is created.
	 */
	virtual bool SupportsDefaultSections() const
	{
		return bSupportsDefaultSections;
	}

protected:

	/** The object binding that this track resides within */
	UPROPERTY()
	FGuid ObjectBindingID;

	/** This track's tint color */
	UPROPERTY(EditAnywhere, Category=General, DisplayName=Color)
	FColor TrackTint;

	/** This folder's desired sorting order */
	UPROPERTY()
	int32 SortingOrder;

	/** Does this track support the creation of a default section when created? */
	UPROPERTY()
	bool bSupportsDefaultSections;

public:
#endif

#if WITH_EDITOR
	/**
	 * Called if the section is moved in Sequencer.
	 *
	 * @param Section The section that moved.
	 */
	virtual void OnSectionMoved(UMovieSceneSection& Section, const FMovieSceneSectionMovedParams& Params) {}
#endif
};
