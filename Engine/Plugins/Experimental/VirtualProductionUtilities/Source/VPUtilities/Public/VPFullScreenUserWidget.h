// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "VPFullScreenUserWidget.generated.h"

class FWidgetRenderer;
class FVPWidgetPostProcessHitTester;
class SConstraintCanvas;
class SVirtualWindow;
class SViewport;
class SWidget;
class ULevel;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPostProcessComponent;
class UTextureRenderTarget2D;
class UWorld;

#if WITH_EDITOR
class SLevelViewport;
#endif


UENUM(BlueprintType)
enum class EVPWidgetDisplayType : uint8
{
	/** Do not display. */
	Inactive,
	/** Display on a game viewport. */
	Viewport,
	/** Display as a post process. */
	PostProcess,
};


USTRUCT()
struct FVPFullScreenUserWidget_Viewport
{
	GENERATED_BODY()

public:
	FVPFullScreenUserWidget_Viewport();
	bool Display(UWorld* World, UUserWidget* Widget);
	void Hide(UWorld* World);
	void Tick(UWorld* World, float DeltaSeconds);

private:
	bool bAddedToGameViewport;

	/** Constraint widget that contains the widget we want to display. */
	TWeakPtr<SConstraintCanvas> FullScreenCanvasWidget;

#if WITH_EDITOR
	/** Level viewport the widget was added to. */
	TWeakPtr<SLevelViewport> OverlayWidgetLevelViewport;
#endif
};

USTRUCT()
struct FVPFullScreenUserWidget_PostProcess
{
	GENERATED_BODY()

public:
	FVPFullScreenUserWidget_PostProcess();
	bool Display(UWorld* World, UUserWidget* Widget);
	void Hide(UWorld* World);
	void Tick(UWorld* World, float DeltaSeconds);

	TSharedPtr<SVirtualWindow> VPUTILITIES_API GetSlateWindow() const;

private:
	bool CreatePostProcessComponent(UWorld* World);
	void ReleasePostProcessComponent();

	bool CreateRenderer(UWorld* World, UUserWidget* Widget);
	void ReleaseRenderer();
	void TickRenderer(UWorld* World, float DeltaSeconds);

	FIntPoint CalculateWidgetDrawSize(UWorld* World);
	bool IsTextureSizeValid(FIntPoint Size) const;

	void RegisterHitTesterWithViewport(UWorld* World);
	void UnRegisterHitTesterWithViewport();

public:
	/**
	 * Post process material used to display the widget. 
	 * SlateUI [Texture]
	 * TintColorAndOpacity [Vector]
	 * OpacityFromTexture [Scalar]
	 */
	UPROPERTY(EditAnywhere, Category = PostProcess)
	UMaterialInterface* PostProcessMaterial;

	/** Tint color and opacity for this component. */
	UPROPERTY(EditAnywhere, Category = PostProcess)
	FLinearColor PostProcessTintColorAndOpacity;

	/** Sets the amount of opacity from the widget's UI texture to use when rendering the translucent or masked UI to the viewport (0.0-1.0). */
	UPROPERTY(EditAnywhere, Category = PostProcess, meta = (ClampMin = 0.0f, ClampMax = 1.0f))
	float PostProcessOpacityFromTexture;

	/** The size of the rendered widget. */
	UPROPERTY(EditAnywhere, Category = PostProcess, meta=(InlineEditConditionToggle))
	bool bWidgetDrawSize;

	/** The size of the rendered widget. */
	UPROPERTY(EditAnywhere, Category = PostProcess, meta=(EditCondition= bWidgetDrawSize))
	FIntPoint WidgetDrawSize;

	/** Is the virtual window created to host the widget focusable? */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = PostProcess)
	bool bWindowFocusable;

	/** The visibility of the virtual window created to host the widget. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = PostProcess)
	EWindowVisibility WindowVisibility;

	/** Register with the viewport for hardware input from the mouse and keyboard. It can and will steal focus from the viewport. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category= PostProcess)
	bool bReceiveHardwareInput;

	/** The background color of the render target */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = PostProcess)
	FLinearColor RenderTargetBackgroundColor;

	/** The blend mode for the widget. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = PostProcess)
	EWidgetBlendMode RenderTargetBlendMode;

private:
	/** Post process component used to add the material to the post process chain. */
	UPROPERTY(Transient)
	UPostProcessComponent* PostProcessComponent;

	/** The dynamic instance of the material that the render target is attached to. */
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* PostProcessMaterialInstance;

	/** The target to which the user widget is rendered. */
	UPROPERTY(Transient)
	UTextureRenderTarget2D* WidgetRenderTarget;

	/** The slate window that contains the user widget content. */
	TSharedPtr<SVirtualWindow> SlateWindow;

	/** The slate viewport we are registered to. */
	TWeakPtr<SViewport> ViewportWidget;

	/** Helper class for drawing widgets to a render target. */
	FWidgetRenderer* WidgetRenderer;

	/** The size of the rendered widget */
	FIntPoint CurrentWidgetDrawSize;

	/** Hit tester when we want the hardware input. */
	TSharedPtr<FVPWidgetPostProcessHitTester> CustomHitTestPath;
};

/**
 * Will set the Widgets on a viewport either by Widgets are first rendered to a render target, then that render target is displayed in the world.
 */
UCLASS(meta=(ShowOnlyInnerProperties))
class VPUTILITIES_API UVPFullScreenUserWidget : public UObject
{
	GENERATED_BODY()

public:
	UVPFullScreenUserWidget(const FObjectInitializer& ObjectInitializer);

	//~ Begin UObject interface
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface

	bool ShouldDisplay(UWorld* World) const;
	EVPWidgetDisplayType GetDisplayType(UWorld* World) const;
	bool IsDisplayed() const;

	virtual bool Display(UWorld* World);
	virtual void Hide();
	virtual void Tick(float DeltaTime);

	void SetDisplayTypes(EVPWidgetDisplayType InEditorDisplayType, EVPWidgetDisplayType InGameDisplayType, EVPWidgetDisplayType InPIEDisplayType);

protected:
	void InitWidget();
	void ReleaseWidget();

private:
	void OnLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld);

protected:
	/** The display type when the world is an editor world. */
	UPROPERTY(EditAnywhere, Category = "User Interface")
	EVPWidgetDisplayType EditorDisplayType;

	/** The display type when the world is a game world. */
	UPROPERTY(EditAnywhere, Category = "User Interface")
	EVPWidgetDisplayType GameDisplayType;

	/** The display type when the world is a PIE world. */
	UPROPERTY(EditAnywhere, Category = "User Interface", meta = (DisplayName = "PIE Display Type"))
	EVPWidgetDisplayType PIEDisplayType;

	/** Behavior when the widget should be display by the slate attached to the viewport. */
	UPROPERTY(EditAnywhere, Category = "Viewport", meta = (ShowOnlyInnerProperties))
	FVPFullScreenUserWidget_Viewport ViewportDisplayType;

public:
	/** The class of User Widget to create and display an instance of */
	UPROPERTY(EditAnywhere, Category = "User Interface")
	TSubclassOf<UUserWidget> WidgetClass;

	/** Behavior when the widget should be display by a post process. */
	UPROPERTY(EditAnywhere, Category = "Post Process", meta = (ShowOnlyInnerProperties))
	FVPFullScreenUserWidget_PostProcess PostProcessDisplayType;

private:
	/** The User Widget object displayed and managed by this component */
	UPROPERTY(Transient, DuplicateTransient)
	UUserWidget* Widget;

	/** The world the widget is attached to. */
	TWeakObjectPtr<UWorld> World;

	/** How we currently displaying the widget. */
	EVPWidgetDisplayType CurrentDisplayType;

	/** The user requested the widget to be displayed. It's possible that some setting are invalid and the widget will not be displayed. */
	bool bDisplayRequested;
};
