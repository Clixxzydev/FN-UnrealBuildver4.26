// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "Components/LightComponent.h"

#include "DirectionalLightComponent.generated.h"

class FLightSceneProxy;

/**
 * A light component that has parallel rays. Will provide a uniform lighting across any affected surface (eg. The Sun). This will affect all objects in the defined light-mass importance volume.
 */
UCLASS(Blueprintable, ClassGroup=Lights, hidecategories=(Object, LightProfiles), editinlinenew, meta=(BlueprintSpawnableComponent))
class ENGINE_API UDirectionalLightComponent : public ULightComponent
{
	GENERATED_UCLASS_BODY()

	/**
	* Controls the depth bias scaling across cascades. This allows to mitigage the shadow acne difference on shadow cascades transition.
	* A value of 1 scales shadow bias based on each cascade size (Default).
	* A value of 0 scales shadow bias uniformly accross all cacascade.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Light, AdvancedDisplay, meta = (UIMin = "0", UIMax = "1"))
	float ShadowCascadeBiasDistribution;

	/** Whether to occlude fog and atmosphere inscattering with screenspace blurred occlusion from this light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=LightShafts, meta=(DisplayName = "Light Shaft Occlusion"))
	uint32 bEnableLightShaftOcclusion:1;

	/** 
	 * Controls how dark the occlusion masking is, a value of 1 results in no darkening term.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=LightShafts, meta=(UIMin = "0", UIMax = "1"))
	float OcclusionMaskDarkness;

	/** Everything closer to the camera than this distance will occlude light shafts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=LightShafts, meta=(UIMin = "0", UIMax = "500000"))
	float OcclusionDepthRange;

	/** 
	 * Can be used to make light shafts come from somewhere other than the light's actual direction. 
	 * This will only be used when non-zero.  It does not have to be normalized.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=LightShafts)
	FVector LightShaftOverrideDirection;

	UPROPERTY()
	float WholeSceneDynamicShadowRadius_DEPRECATED;

	/** 
	 * How far Cascaded Shadow Map dynamic shadows will cover for a movable light, measured from the camera.
	 * A value of 0 disables the dynamic shadow.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=CascadedShadowMaps, meta=(UIMin = "0", UIMax = "20000", DisplayName = "Dynamic Shadow Distance MovableLight"))
	float DynamicShadowDistanceMovableLight;

	/** 
	 * How far Cascaded Shadow Map dynamic shadows will cover for a stationary light, measured from the camera.
	 * A value of 0 disables the dynamic shadow.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=CascadedShadowMaps, meta=(UIMin = "0", UIMax = "20000", DisplayName = "Dynamic Shadow Distance StationaryLight"))
	float DynamicShadowDistanceStationaryLight;

	/** 
	 * Number of cascades to split the view frustum into for the whole scene dynamic shadow.  
	 * More cascades result in better shadow resolution, but adds significant rendering cost.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=CascadedShadowMaps, meta=(UIMin = "0", UIMax = "4", DisplayName = "Num Dynamic Shadow Cascades"))
	int32 DynamicShadowCascades;

	/** 
	 * Controls whether the cascades are distributed closer to the camera (larger exponent) or further from the camera (smaller exponent).
	 * An exponent of 1 means that cascade transitions will happen at a distance proportional to their resolution.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=CascadedShadowMaps, meta=(UIMin = "1", UIMax = "4", DisplayName = "Distribution Exponent"))
	float CascadeDistributionExponent;

	/** 
	 * Proportion of the fade region between cascades.
	 * Pixels within the fade region of two cascades have their shadows blended to avoid hard transitions between quality levels.
	 * A value of zero eliminates the fade region, creating hard transitions.
	 * Higher values increase the size of the fade region, creating a more gradual transition between cascades.
	 * The value is expressed as a percentage proportion (i.e. 0.1 = 10% overlap).
	 * Ideal values are the smallest possible which still hide the transition.
	 * An increased fade region size causes an increase in shadow rendering cost.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=CascadedShadowMaps, meta=(UIMin = "0", UIMax = "0.3", DisplayName = "Transition Fraction"))
	float CascadeTransitionFraction;

	/** 
	 * Controls the size of the fade out region at the far extent of the dynamic shadow's influence.  
	 * This is specified as a fraction of DynamicShadowDistance. 
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=CascadedShadowMaps, meta=(UIMin = "0", UIMax = "1.0", DisplayName = "Distance Fadeout Fraction"))
	float ShadowDistanceFadeoutFraction;

	/** 
	 * Stationary lights only: Whether to use per-object inset shadows for movable components, even though cascaded shadow maps are enabled.
	 * This allows dynamic objects to have a shadow even when they are outside of the cascaded shadow map, which is important when DynamicShadowDistanceStationaryLight is small.
	 * If DynamicShadowDistanceStationaryLight is large (currently > 8000), this will be forced off.
	 * Disabling this can reduce shadowing cost significantly with many movable objects.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=CascadedShadowMaps, DisplayName = "Inset Shadows For Movable Objects")
	uint32 bUseInsetShadowsForMovableObjects : 1;

	/** 0: no DistantShadowCascades, otherwise the count of cascades between WholeSceneDynamicShadowRadius and DistantShadowDistance that are covered by distant shadow cascades. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=CascadedShadowMaps, meta=(UIMin = "0", UIMax = "4"), DisplayName = "Far Shadow Cascade Count")
	int32 FarShadowCascadeCount;

	/** 
	 * Distance at which the far shadow cascade should end.  Far shadows will cover the range between 'Dynamic Shadow Distance' and this distance. 
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=CascadedShadowMaps, meta=(UIMin = "0", UIMax = "800000"), DisplayName = "Far Shadow Distance")
	float FarShadowDistance;

	/** 
	 * Distance at which the ray traced shadow cascade should end.  Distance field shadows will cover the range between 'Dynamic Shadow Distance' this distance. 
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=DistanceFieldShadows, meta=(UIMin = "0", UIMax = "100000"), DisplayName = "DistanceField Shadow Distance")
	float DistanceFieldShadowDistance;

	/** 
	 * Angle subtended by light source in degrees (also known as angular diameter).
	 * Defaults to 0.5357 which is the angle for our sun.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Light, meta=(UIMin = "0", UIMax = "5"), DisplayName = "Source Angle")
	float LightSourceAngle;

	/** 
	 * Angle subtended by soft light source in degrees.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Light, meta=(UIMin = "0", UIMax = "5"), DisplayName = "Source Soft Angle")
	float LightSourceSoftAngle;

	/**
	 * Shadow source angle factor, relative to the light source angle.
	 * Defaults to 1.0 to coincide with light source angle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = RayTracing, meta = (UIMin = "0", UIMax = "5"), DisplayName = "Shadow Source Angle Factor")
	float ShadowSourceAngleFactor;

	/** Determines how far shadows can be cast, in world units.  Larger values increase the shadowing cost. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=DistanceFieldShadows, meta=(UIMin = "1000", UIMax = "100000"), DisplayName = "DistanceField Trace Distance")
	float TraceDistance;
	
	/**
	 * Whether the directional light can interact with the atmosphere, cloud and generate a visual disk. All of wwhich compse the visual sky.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category= AtmosphereAndCloud, meta=(DisplayName = "Atmosphere Sun Light"))
	uint32 bUsedAsAtmosphereSunLight : 1;

	/**
	 * Two atmosphere lights are supported. For instance: a sun and a moon, or two suns.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = AtmosphereAndCloud, meta = (DisplayName = "Atmosphere Sun Light Index", UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax= "1"))
	int32 AtmosphereSunLightIndex;

	/**
	 * A color multiplied with the sun disk luminance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AtmosphereAndCloud, AdvancedDisplay, meta = (DisplayName = "Atmosphere Sun Disk Color Scale"))
	FLinearColor AtmosphereSunDiskColorScale;

	/**
	 * Wether to apply atmosphere transmittance per pixel on opaque meshes, instead of using the light global transmittance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AtmosphereAndCloud, AdvancedDisplay)
	uint32 bPerPixelAtmosphereTransmittance : 1;

	/**
	 * Whether the light should cast any shadows from opaque meshes onto clouds. This is disabled for AtmosphereLight1.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AtmosphereAndCloud)
	uint32 bCastShadowsOnClouds : 1;
	/**
	 * Whether the light should cast any shadows from opaque meshes onto the atmosphere.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AtmosphereAndCloud)
	uint32 bCastShadowsOnAtmosphere : 1;
	/**
	 * Whether the light should cast any shadows from clouds onto the atmosphere and other scene elements.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AtmosphereAndCloud)
	uint32 bCastCloudShadows : 1;
	/**
	 * The strength of the shadow, higher value will block more light.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AtmosphereAndCloud, AdvancedDisplay, meta = (UIMin = "0", UIMax = "1", ClampMin = "0", SliderExponent = 3.0))
	float CloudShadowStrength;
	/**
	 * The world space radius of the cloud shadow map around the camera in kilometers.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AtmosphereAndCloud, AdvancedDisplay, meta = (UIMin = "1", ClampMin = "1"))
	float CloudShadowExtent;
	/**
	 * Scale the cloud shadow map resolution. The resolution is still clamped to 'r.VolumetricCloud.ShadowMap.MaxResolution'.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AtmosphereAndCloud, AdvancedDisplay, meta = (UIMin = "0.25", UIMax = "8", ClampMin = "0.25", SliderExponent = 3.0))
	float CloudShadowMapResolutionScale;

	/**
	 * Scales the lights contribution when scattered in cloud participating media. This can help counter balance the fact that our multiple scattering solution is only an approximation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category = AtmosphereAndCloud, meta = (HideAlphaChannel))
	FLinearColor CloudScatteredLuminanceScale;

	/** The Lightmass settings for this object. */
	UPROPERTY(EditAnywhere, Category=Light, meta=(ShowOnlyInnerProperties))
	struct FLightmassDirectionalLightSettings LightmassSettings;

	/**
	* Whether the light should cast modulated shadows from dynamic objects (mobile only).  Also requires Cast Shadows to be set to True.
	**/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Light, AdvancedDisplay)
	uint32 bCastModulatedShadows : 1;

	/**
	* Color to modulate against the scene color when rendering modulated shadows. (mobile only)
	**/
	UPROPERTY(BlueprintReadOnly, interp, Category = Light, meta = (HideAlphaChannel), AdvancedDisplay)
	FColor ModulatedShadowColor;
	
	/**
	 * Control the amount of shadow occlusion. A value of 0 means no occlusion, thus no shadow.
	 */
	UPROPERTY(BlueprintReadOnly, interp, Category = Light, meta = (HideAlphaChannel, UIMin = "0", UIMax = "1", ClampMin = "0", ClampMax = "1"), AdvancedDisplay)
	float ShadowAmount;

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetDynamicShadowDistanceMovableLight(float NewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetDynamicShadowDistanceStationaryLight(float NewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetDynamicShadowCascades(int32 NewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetCascadeDistributionExponent(float NewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetCascadeTransitionFraction(float NewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetShadowDistanceFadeoutFraction(float NewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetEnableLightShaftOcclusion(bool bNewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetOcclusionMaskDarkness(float NewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetLightShaftOverrideDirection(FVector NewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetShadowAmount(float NewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|Lighting")
	void SetAtmosphereSunLight(bool bNewValue);

	UFUNCTION(BlueprintCallable, Category = "Rendering|Lighting")
	void SetAtmosphereSunLightIndex(int32 NewValue);

	//~ Begin ULightComponent Interface
	virtual FVector4 GetLightPosition() const override;
	virtual ELightComponentType GetLightType() const override;
	virtual FLightmassLightSettings GetLightmassSettings() const override
	{
		return LightmassSettings;
	}

	virtual float GetUniformPenumbraSize() const override;

	virtual FLightSceneProxy* CreateSceneProxy() const override;
	virtual bool IsUsedAsAtmosphereSunLight() const override
	{
		return bUsedAsAtmosphereSunLight;
	}
	virtual uint8 GetAtmosphereSunLightIndex() const override
	{
		return AtmosphereSunLightIndex;
	}
	virtual FLinearColor GetAtmosphereSunDiskColorScale() const override
	{
		return AtmosphereSunDiskColorScale;
	}
	//~ End ULightComponent Interface

	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif // WITH_EDITOR
	virtual void Serialize(FArchive& Ar) override;
	//~ Begin UObject Interface

	virtual void InvalidateLightingCacheDetailed(bool bInvalidateBuildEnqueuedLighting, bool bTranslationOnly) override;
};



