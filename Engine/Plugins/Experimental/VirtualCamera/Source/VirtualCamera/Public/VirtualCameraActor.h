// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "IRemoteSessionRole.h"
#include "IVirtualCameraController.h"
#include "IVirtualCameraOptions.h"
#include "IVirtualCameraPresetContainer.h"
#include "LiveLinkRole.h"
#include "Templates/UniquePtr.h"
#include "UObject/NoExportTypes.h"
#if WITH_EDITOR
#include "UnrealEdMisc.h"
#endif

#include "VirtualCameraActor.generated.h"

struct FVirtualCameraViewportSettings;
class IRemoteSessionUnmanagedRole;
class UCineCameraComponent;
class URemoteSessionMediaCapture;
class URemoteSessionMediaOutput;
class UUserWidget;
class UVirtualCameraMovement;
class UVPFullScreenUserWidget;
class UWorld;

#if WITH_EDITOR
class UBlueprint;
struct FAssetData;
struct FCanDeleteAssetResult;
#endif


UCLASS(Blueprintable, BlueprintType, Category="VirtualCamera", DisplayName="VirtualCameraActor")
class VIRTUALCAMERA_API AVirtualCameraActor : public AActor, public IVirtualCameraController, public IVirtualCameraPresetContainer, public IVirtualCameraOptions
{
	GENERATED_BODY()

public:

	AVirtualCameraActor(const FObjectInitializer& ObjectInitializer);
	AVirtualCameraActor(FVTableHelper& Helper);
	~AVirtualCameraActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "VirtualCamera | Component")
	UCineCameraComponent* StreamedCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "VirtualCamera | Component")
	UCineCameraComponent* RecordingCamera;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VirtualCamera | Movement")
	FLiveLinkSubjectRepresentation LiveLinkSubject;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "VirtualCamera | Movement")
	UVirtualCameraMovement* MovementComponent;

	UPROPERTY(Transient, EditDefaultsOnly, Category = "VirtualCamera | MediaOutput")
	URemoteSessionMediaOutput* MediaOutput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualCamera | UMG")
	TSubclassOf<UUserWidget> CameraUMGClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualCamera | Streaming")
	FVector2D TargetDeviceResolution;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualCamera | Streaming")
	int32 RemoteSessionPort;

protected:

	UPROPERTY(EditDefaultsOnly, Category = "VirtualCamera | UMG")
	UVPFullScreenUserWidget* CameraScreenWidget;

	UPROPERTY(Transient, EditDefaultsOnly, Category = "VirtualCamera | MediaOutput")
	URemoteSessionMediaCapture* MediaCapture;

	UPROPERTY(Transient)
	UWorld* ActorWorld;

	UPROPERTY(Transient)
	USceneComponent* DefaultSceneRoot;

	UPROPERTY(Transient)
	USceneComponent* SceneOffset;

	UPROPERTY(Transient)
	USceneComponent* CameraOffset;

	UPROPERTY(Transient)
	AActor* PreviousViewTarget;

	/** Should focus plane be shown on all touch focus events */
	UPROPERTY(BlueprintReadOnly, Category = "VirtualCamera | Focus")
	bool bAllowFocusVisualization;

	UPROPERTY(BlueprintReadOnly, Category = "VirtualCamera | Focus")
	EVirtualCameraFocusMethod FocusMethod;

	/**
	 * Delegate that will is triggered before transform is set onto Actor.
	 * @param FVirtualCameraTransform Transform data that is passed to delegate.
	 * @return FVirtualCameraTransform Manipulated transform that will be set onto Actor.
	 */
	UPROPERTY(EditAnywhere, Category = "VirtualCamera")
	FPreSetVirtualCameraTransform OnPreSetVirtualCameraTransform;

	/**
	 * Delegate that will be triggered when an actor has been clicked/touched.
	 * @note Delegate will run on Touch/Mouse-Down
	 * @param AActor* Pointer to the actor that was clicked.
	 */
	UPROPERTY(EditAnywhere, Category = "VirtualCamera | Focus")
	FOnActorClickedDelegate OnActorClickedDelegate;

	/**
	 * This delegate is triggered at the end of a tick in editor/pie/game.
	 * @note The Actor is only ticked while it is being streamed.
	 * @param float Delta Time in seconds.
	 */
	UPROPERTY(EditAnywhere, Category = "VirtualCamera")
	FVirtualCameraTickDelegateGroup OnVirtualCameraUpdatedDelegates;

	/** The next preset number */
	static int32 PresetIndex;

	/* Stores the list of settings presets, and saved presets */
	UPROPERTY(EditAnywhere, Category = "VirtualCamera | Settings")
	TMap<FString, FVirtualCameraSettingsPreset> SettingsPresets;

	/** The desired unit in which to display focus distance */
	EUnit DesiredDistanceUnits;

	/** Whether to save all settings when streaming is stopped */
	bool bSaveSettingsOnStopStreaming;

public:

	//~ Begin AActor interface
	virtual void Destroyed() override;
#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override;
#endif
	virtual void Tick(float DeltaSeconds) override;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor interface

	//~ Begin IVirtualCameraController Interface
public:
	virtual bool StartStreaming() override;
	virtual bool StopStreaming() override;
	virtual UWorld* GetControllerWorld() override;
protected:
	virtual UCineCameraComponent* GetStreamedCameraComponent_Implementation() const override;
	virtual UCineCameraComponent* GetRecordingCameraComponent_Implementation() const override;
	virtual UCineCameraComponent* GetActiveCameraComponent_Implementation() const override;
	virtual ULevelSequencePlaybackController* GetSequenceController_Implementation() const override;
	virtual TScriptInterface<IVirtualCameraPresetContainer> GetPresetContainer_Implementation() override;
	virtual TScriptInterface<IVirtualCameraOptions> GetOptions_Implementation() override;
	virtual FLiveLinkSubjectRepresentation GetLiveLinkRepresentation_Implementation() const override;
	virtual void SetLiveLinkRepresentation_Implementation(const FLiveLinkSubjectRepresentation& InLiveLinkRepresentation) override;
	virtual bool IsStreaming_Implementation() const override;
	virtual void SetSaveSettingsOnStopStreaming_Implementation(bool bShouldSettingsSave) override;
	virtual void SetRelativeTransform_Implementation(const FTransform& InControllerTransform) override;
	virtual FTransform GetRelativeTransform_Implementation() const override;
	virtual void AddBlendableToCamera_Implementation(const TScriptInterface<IBlendableInterface>& InBlendableToAdd, float InWeight) override;
	virtual void SetFocusDistance_Implementation(float InFocusDistanceCentimeters) override;
	virtual void SetTrackedActorForFocus_Implementation(AActor* InActorToTrack, const FVector& InTrackingPointOffset) override;
	virtual void SetFocusMethod_Implementation(EVirtualCameraFocusMethod InNewFocusMethod) override;
	virtual EVirtualCameraFocusMethod GetFocusMethod_Implementation() const override;
	virtual void SetFocusVisualization_Implementation(bool bInShowFocusVisualization) override;
	virtual void SetReticlePosition_Implementation(const FVector2D& InViewportPosition) override;
	virtual FVector2D GetReticlePosition_Implementation() const override;
	virtual void UpdateHyperfocalDistance_Implementation() override;
	virtual float GetHyperfocalDistance_Implementation() const override;
	virtual bool ShouldSaveSettingsOnStopStreaming_Implementation() const override;
	virtual void SetBeforeSetVirtualCameraTransformDelegate_Implementation(const FPreSetVirtualCameraTransform& InDelegate) override;
	virtual void SetOnActorClickedDelegate_Implementation(const FOnActorClickedDelegate& InDelegate) override;
	virtual void AddOnVirtualCameraUpdatedDelegate_Implementation(const FVirtualCameraTickDelegate& InDelegate) override;
	virtual void RemoveOnVirtualCameraUpdatedDelegate_Implementation(const FVirtualCameraTickDelegate& InDelegate) override;
	//~ End  IVirtualCameraController Interface

	//~ Begin IVirtualCameraPresetContainer Interface
protected:
	virtual FString SavePreset_Implementation(const bool bSaveCameraSettings, const bool bSaveStabilization, const bool bSaveAxisLocking, const bool bSaveMotionScale) override;
	virtual bool LoadPreset_Implementation(const FString& PresetName) override;
	virtual int32 DeletePreset_Implementation(const FString& PresetName) override;
	virtual TMap<FString, FVirtualCameraSettingsPreset> GetSettingsPresets_Implementation() override;
	//~ End IVirtualCameraPresetContainer Interface

	//~ Begin IVirtualCameraOptions Interface
protected:
	virtual void SetDesiredDistanceUnits_Implementation(const EUnit DesiredUnits) override;
	virtual EUnit GetDesiredDistanceUnits_Implementation() override;
	virtual bool IsFocusVisualizationAllowed_Implementation() override;
	//~ End IVirtualCameraOptions Interface

private:

	void OnImageChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance, const FString& Type, ERemoteSessionChannelMode Mode);
	void OnInputChannelCreated(TWeakPtr<IRemoteSessionChannel> Instance, const FString& Type, ERemoteSessionChannelMode Mode);

	void OnTouchEventOutsideUMG(const FVector2D& InViewportPosition);

	/** Stores the current camera settings to a save game for later use. */
	void SaveSettings();
	/** Restores settings from save game. */
	void LoadSettings();

	UCineCameraComponent* GetActiveCameraComponentInternal() const;
	void SetRelativeTransformInternal(const FTransform& InRelativeTransform);

	void UpdateAutoFocus();

#if WITH_EDITOR
	void OnMapChanged(UWorld* World, EMapChangeType ChangeType);
	void OnBlueprintPreCompile(UBlueprint* Blueprint);
	void OnPrepareToCleanseEditorObject(UObject* Object);
	void OnAssetRemoved(const FAssetData& AssetData);
	void OnAssetsCanDelete(const TArray<UObject*>& InAssetsToDelete, FCanDeleteAssetResult& CanDeleteResult);
#endif

private:

	bool bIsStreaming;
	TSharedPtr<IRemoteSessionUnmanagedRole> RemoteSessionHost;
	TUniquePtr<FVirtualCameraViewportSettings> ViewportSettingsBackup;
	FHitResult LastViewportTouchResult;
	FVector2D ReticlePosition;
	float HyperfocalDistance;
};
