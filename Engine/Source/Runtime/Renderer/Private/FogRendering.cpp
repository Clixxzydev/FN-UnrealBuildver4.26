// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	FogRendering.cpp: Fog rendering implementation.
=============================================================================*/

#include "FogRendering.h"
#include "DeferredShadingRenderer.h"
#include "AtmosphereRendering.h"
#include "ScenePrivate.h"
#include "Engine/TextureCube.h"
#include "PipelineStateCache.h"
#include "SingleLayerWaterRendering.h"

DECLARE_GPU_STAT(Fog);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
static TAutoConsoleVariable<float> CVarFogStartDistance(
	TEXT("r.FogStartDistance"),
	-1.0f,
	TEXT("Allows to override the FogStartDistance setting (needs ExponentialFog in the level).\n")
	TEXT(" <0: use default settings (default: -1)\n")
	TEXT(">=0: override settings by the given value (in world units)"),
	ECVF_Cheat | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarFogDensity(
	TEXT("r.FogDensity"),
	-1.0f,
	TEXT("Allows to override the FogDensity setting (needs ExponentialFog in the level).\n")
	TEXT("Using a strong value allows to quickly see which pixel are affected by fog.\n")
	TEXT("Using a start distance allows to cull pixels are can speed up rendering.\n")
	TEXT(" <0: use default settings (default: -1)\n")
	TEXT(">=0: override settings by the given value (0:off, 1=very dense fog)"),
	ECVF_Cheat | ECVF_RenderThreadSafe);
#endif

static TAutoConsoleVariable<int32> CVarFog(
	TEXT("r.Fog"),
	1,
	TEXT(" 0: disabled\n")
	TEXT(" 1: enabled (default)"),
	ECVF_RenderThreadSafe | ECVF_Scalability);


IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FFogUniformParameters, "FogStruct");

struct FHeightFogRenderingParameters
{
	const FLightShaftsOutput& LightShaftsOutput;
	FTextureRHIRef LinearDepthTextureRHI;
	FIntRect ViewRect;
	float LinearDepthReadScale;
	FVector4 LinearDepthMinMaxUV;
};

void SetupFogUniformParameters(const FViewInfo& View, FFogUniformParameters& OutParameters)
{
	// Exponential Height Fog
	{
		const FTexture* Cubemap = GWhiteTextureCube;

		if (View.FogInscatteringColorCubemap)
		{
			Cubemap = View.FogInscatteringColorCubemap->Resource;
		}

		OutParameters.ExponentialFogParameters = View.ExponentialFogParameters;
		OutParameters.ExponentialFogColorParameter = FVector4(View.ExponentialFogColor, 1.0f - View.FogMaxOpacity);
		OutParameters.ExponentialFogParameters2 = View.ExponentialFogParameters2;
		OutParameters.ExponentialFogParameters3 = View.ExponentialFogParameters3;
		OutParameters.SinCosInscatteringColorCubemapRotation = View.SinCosInscatteringColorCubemapRotation;
		OutParameters.FogInscatteringTextureParameters = View.FogInscatteringTextureParameters;
		OutParameters.InscatteringLightDirection = View.InscatteringLightDirection;
		OutParameters.InscatteringLightDirection.W = View.bUseDirectionalInscattering ? FMath::Max(0.f, View.DirectionalInscatteringStartDistance) : -1.f;
		OutParameters.DirectionalInscatteringColor = FVector4(FVector(View.DirectionalInscatteringColor), FMath::Clamp(View.DirectionalInscatteringExponent, 0.000001f, 1000.0f));
		OutParameters.FogInscatteringColorCubemap = Cubemap->TextureRHI;
		OutParameters.FogInscatteringColorSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}

	// Volumetric Fog
	{
		FRHITexture* IntegratedLightScatteringTexture = nullptr;
		if (View.VolumetricFogResources.IntegratedLightScattering)
		{
			IntegratedLightScatteringTexture = View.VolumetricFogResources.IntegratedLightScattering->GetRenderTargetItem().ShaderResourceTexture;
		}
		SetBlackAlpha13DIfNull(IntegratedLightScatteringTexture);

		const bool bApplyVolumetricFog = View.VolumetricFogResources.IntegratedLightScattering != NULL;
		OutParameters.ApplyVolumetricFog = bApplyVolumetricFog ? 1.0f : 0.0f;
		OutParameters.IntegratedLightScattering = IntegratedLightScatteringTexture;
		OutParameters.IntegratedLightScatteringSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	}
}

TUniformBufferRef<FFogUniformParameters> CreateFogUniformBuffer(const class FViewInfo& View, EUniformBufferUsage Usage)
{
	FFogUniformParameters FogStruct;
	SetupFogUniformParameters(View, FogStruct);
	return CreateUniformBufferImmediate(FogStruct, Usage);
}

/** A vertex shader for rendering height fog. */
class FHeightFogVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FHeightFogVS,Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FHeightFogVS( )	{ }
	FHeightFogVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		FogStartZ.Bind(Initializer.ParameterMap,TEXT("FogStartZ"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundVertexShader(), View.ViewUniformBuffer);

		{
			// The fog can be set to start at a certain euclidean distance.
			// clamp the value to be behind the near plane z
			float FogStartDistance = FMath::Max(30.0f, View.ExponentialFogParameters.W);

			// Here we compute the nearest z value the fog can start
			// to render the quad at this z value with depth test enabled.
			// This means with a bigger distance specified more pixels are
			// are culled and don't need to be rendered. This is faster if
			// there is opaque content nearer than the computed z.

			FMatrix InvProjectionMatrix = View.ViewMatrices.GetInvProjectionMatrix();

			FVector ViewSpaceCorner = InvProjectionMatrix.TransformFVector4(FVector4(1, 1, 1, 1));

			float Ratio = ViewSpaceCorner.Z / ViewSpaceCorner.Size();

			FVector ViewSpaceStartFogPoint(0.0f, 0.0f, FogStartDistance * Ratio);
			FVector4 ClipSpaceMaxDistance = View.ViewMatrices.GetProjectionMatrix().TransformPosition(ViewSpaceStartFogPoint);

			float FogClipSpaceZ = ClipSpaceMaxDistance.Z / ClipSpaceMaxDistance.W;

			SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(),FogStartZ, FogClipSpaceZ);
		}
	}

private:
	LAYOUT_FIELD(FShaderParameter, FogStartZ);
};

IMPLEMENT_SHADER_TYPE(,FHeightFogVS,TEXT("/Engine/Private/HeightFogVertexShader.usf"),TEXT("Main"),SF_Vertex);

enum class EHeightFogFeature
{
	HeightFog,
	InscatteringTexture,
	DirectionalLightInscattering,
	HeightFogAndVolumetricFog,
	InscatteringTextureAndVolumetricFog,
	DirectionalLightInscatteringAndVolumetricFog
};

/** A pixel shader for rendering exponential height fog. */
template<EHeightFogFeature HeightFogFeature>
class TExponentialHeightFogPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TExponentialHeightFogPS,Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("SUPPORT_FOG_INSCATTERING_TEXTURE"), HeightFogFeature == EHeightFogFeature::InscatteringTexture || HeightFogFeature == EHeightFogFeature::InscatteringTextureAndVolumetricFog);
		OutEnvironment.SetDefine(TEXT("SUPPORT_FOG_DIRECTIONAL_LIGHT_INSCATTERING"), HeightFogFeature == EHeightFogFeature::DirectionalLightInscattering || HeightFogFeature == EHeightFogFeature::DirectionalLightInscatteringAndVolumetricFog);
		OutEnvironment.SetDefine(TEXT("SUPPORT_VOLUMETRIC_FOG"), HeightFogFeature == EHeightFogFeature::HeightFogAndVolumetricFog || HeightFogFeature == EHeightFogFeature::InscatteringTextureAndVolumetricFog || HeightFogFeature == EHeightFogFeature::DirectionalLightInscatteringAndVolumetricFog);
	}

	TExponentialHeightFogPS( )	{ }
	TExponentialHeightFogPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		OcclusionTexture.Bind(Initializer.ParameterMap, TEXT("OcclusionTexture"));
		OcclusionSampler.Bind(Initializer.ParameterMap, TEXT("OcclusionSampler"));
		LinearDepthTexture.Bind(Initializer.ParameterMap, TEXT("LinearDepthTexture"));
		LinearDepthSampler.Bind(Initializer.ParameterMap, TEXT("LinearDepthSampler"));
		bOnlyOnRenderedOpaque.Bind(Initializer.ParameterMap, TEXT("bOnlyOnRenderedOpaque"));
		bUseLinearDepthTexture.Bind(Initializer.ParameterMap, TEXT("bUseLinearDepthTexture"));
		LinearDepthTextureMinMaxUV.Bind(Initializer.ParameterMap, TEXT("LinearDepthTextureMinMaxUV"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FHeightFogRenderingParameters& Params)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);
		
		FFogUniformParameters FogUniformParameters;
		SetupFogUniformParameters(View, FogUniformParameters);
		SetUniformBufferParameterImmediate(RHICmdList, RHICmdList.GetBoundPixelShader(), GetUniformBufferParameter<FFogUniformParameters>(), FogUniformParameters);

		FTextureRHIRef TextureRHI = Params.LightShaftsOutput.LightShaftOcclusion ?
			Params.LightShaftsOutput.LightShaftOcclusion->GetRenderTargetItem().ShaderResourceTexture :
			GWhiteTexture->TextureRHI;
		SetTextureParameter(
			RHICmdList, 
			RHICmdList.GetBoundPixelShader(),
			OcclusionTexture, OcclusionSampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			TextureRHI
			);

		const bool bUseLinearDepthTextureEnabled = Params.LinearDepthTextureRHI != nullptr;
		FTextureRHIRef LinearDepthTextureRHI = bUseLinearDepthTextureEnabled ? Params.LinearDepthTextureRHI : GSystemTextures.DepthDummy->GetRenderTargetItem().ShaderResourceTexture;
		SetTextureParameter(
			RHICmdList, 
			RHICmdList.GetBoundPixelShader(),
			LinearDepthTexture, LinearDepthSampler,
			TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			LinearDepthTextureRHI
			);

		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), bOnlyOnRenderedOpaque, View.bFogOnlyOnRenderedOpaque ? 1.0f : 0.0f);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), bUseLinearDepthTexture, bUseLinearDepthTextureEnabled ? Params.LinearDepthReadScale : 0.0f);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), LinearDepthTextureMinMaxUV, Params.LinearDepthMinMaxUV);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, OcclusionTexture)
	LAYOUT_FIELD(FShaderResourceParameter, OcclusionSampler)
	LAYOUT_FIELD(FShaderResourceParameter, LinearDepthTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LinearDepthSampler);
	LAYOUT_FIELD(FShaderParameter, bOnlyOnRenderedOpaque)
	LAYOUT_FIELD(FShaderParameter, bUseLinearDepthTexture);
	LAYOUT_FIELD(FShaderParameter, LinearDepthTextureMinMaxUV);
};

IMPLEMENT_SHADER_TYPE(template<>,TExponentialHeightFogPS<EHeightFogFeature::HeightFog>,TEXT("/Engine/Private/HeightFogPixelShader.usf"), TEXT("ExponentialPixelMain"),SF_Pixel)
IMPLEMENT_SHADER_TYPE(template<>,TExponentialHeightFogPS<EHeightFogFeature::InscatteringTexture>,TEXT("/Engine/Private/HeightFogPixelShader.usf"), TEXT("ExponentialPixelMain"),SF_Pixel)
IMPLEMENT_SHADER_TYPE(template<>,TExponentialHeightFogPS<EHeightFogFeature::DirectionalLightInscattering>,TEXT("/Engine/Private/HeightFogPixelShader.usf"), TEXT("ExponentialPixelMain"),SF_Pixel)
IMPLEMENT_SHADER_TYPE(template<>,TExponentialHeightFogPS<EHeightFogFeature::HeightFogAndVolumetricFog>,TEXT("/Engine/Private/HeightFogPixelShader.usf"), TEXT("ExponentialPixelMain"),SF_Pixel)
IMPLEMENT_SHADER_TYPE(template<>,TExponentialHeightFogPS<EHeightFogFeature::InscatteringTextureAndVolumetricFog>,TEXT("/Engine/Private/HeightFogPixelShader.usf"), TEXT("ExponentialPixelMain"),SF_Pixel)
IMPLEMENT_SHADER_TYPE(template<>,TExponentialHeightFogPS<EHeightFogFeature::DirectionalLightInscatteringAndVolumetricFog>,TEXT("/Engine/Private/HeightFogPixelShader.usf"), TEXT("ExponentialPixelMain"),SF_Pixel)

/** The fog vertex declaration resource type. */
class FFogVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	// Destructor
	virtual ~FFogVertexDeclaration() {}

	virtual void InitRHI() override
	{
		FVertexDeclarationElementList Elements;
		Elements.Add(FVertexElement(0, 0, VET_Float2, 0, sizeof(FVector2D)));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI() override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

/** Vertex declaration for the light function fullscreen 2D quad. */
TGlobalResource<FFogVertexDeclaration> GFogVertexDeclaration;

void FSceneRenderer::InitFogConstants()
{
	// console command override
	float FogDensityOverride = -1.0f;
	float FogStartDistanceOverride = -1.0f;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	{
		// console variable overrides
		FogDensityOverride = CVarFogDensity.GetValueOnAnyThread();
		FogStartDistanceOverride = CVarFogStartDistance.GetValueOnAnyThread();
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		InitAtmosphereConstantsInView(View);
		// set fog consts based on height fog components
		if(ShouldRenderFog(*View.Family))
		{
			if (Scene->ExponentialFogs.Num() > 0)
			{
				const FExponentialHeightFogSceneInfo& FogInfo = Scene->ExponentialFogs[0];
				float CollapsedFogParameter[FExponentialHeightFogSceneInfo::NumFogs];
				static constexpr float MaxObserverHeightDifference = 65536.0f;
				float MaxObserverHeight = FLT_MAX;
				for (int i = 0; i < FExponentialHeightFogSceneInfo::NumFogs; i++)
				{
					// Only limit the observer height to fog if it has any density
					if (FogInfo.FogData[i].Density > 0.0f)
					{
						MaxObserverHeight = FMath::Min(MaxObserverHeight, FogInfo.FogData[i].Height + MaxObserverHeightDifference);
					}
				}
				
				// Clamping the observer height to avoid numerical precision issues in the height fog equation. The max observer height is relative to the fog height.
				const float ObserverHeight = FMath::Min(View.ViewMatrices.GetViewOrigin().Z, MaxObserverHeight);

				for (int i = 0; i < FExponentialHeightFogSceneInfo::NumFogs; i++)
				{
					const float CollapsedFogParameterPower = FMath::Clamp(
						-FogInfo.FogData[i].HeightFalloff * (ObserverHeight - FogInfo.FogData[i].Height),
						-126.f + 1.f, // min and max exponent values for IEEE floating points (http://en.wikipedia.org/wiki/IEEE_floating_point)
						+127.f - 1.f
					);

					CollapsedFogParameter[i] = FogInfo.FogData[i].Density * FMath::Pow(2.0f, CollapsedFogParameterPower);
				}

				View.ExponentialFogParameters = FVector4(CollapsedFogParameter[0], FogInfo.FogData[0].HeightFalloff, MaxObserverHeight, FogInfo.StartDistance);
				View.ExponentialFogParameters2 = FVector4(CollapsedFogParameter[1], FogInfo.FogData[1].HeightFalloff, FogInfo.FogData[1].Density, FogInfo.FogData[1].Height);
				View.ExponentialFogColor = FVector(FogInfo.FogColor.R, FogInfo.FogColor.G, FogInfo.FogColor.B);
				View.FogMaxOpacity = FogInfo.FogMaxOpacity;
				View.ExponentialFogParameters3 = FVector4(FogInfo.FogData[0].Density, FogInfo.FogData[0].Height, FogInfo.InscatteringColorCubemap ? 1.0f : 0.0f, FogInfo.FogCutoffDistance);
				View.SinCosInscatteringColorCubemapRotation = FVector2D(FMath::Sin(FogInfo.InscatteringColorCubemapAngle), FMath::Cos(FogInfo.InscatteringColorCubemapAngle));
				View.FogInscatteringColorCubemap = FogInfo.InscatteringColorCubemap;
				const float InvRange = 1.0f / FMath::Max(FogInfo.FullyDirectionalInscatteringColorDistance - FogInfo.NonDirectionalInscatteringColorDistance, .00001f);
				float NumMips = 1.0f;

				if (FogInfo.InscatteringColorCubemap)
				{
					NumMips = FogInfo.InscatteringColorCubemap->GetNumMips();
				}

				View.FogInscatteringTextureParameters = FVector(InvRange, -FogInfo.NonDirectionalInscatteringColorDistance * InvRange, NumMips);

				View.DirectionalInscatteringExponent = FogInfo.DirectionalInscatteringExponent;
				View.DirectionalInscatteringStartDistance = FogInfo.DirectionalInscatteringStartDistance;
				View.InscatteringLightDirection = FVector(0);
				FLightSceneInfo* SunLight = Scene->AtmosphereLights[0];	// Fog only takes into account a single atmosphere light with index 0.
				if (SunLight)
				{
					View.InscatteringLightDirection = -SunLight->Proxy->GetDirection();
					View.DirectionalInscatteringColor = FogInfo.DirectionalInscatteringColor * SunLight->Proxy->GetColor().ComputeLuminance();
				}
				View.bUseDirectionalInscattering = SunLight != nullptr;
			}
		}
	}
}

/** Sets the bound shader state for either the per-pixel or per-sample fog pass. */
void SetFogShaders(FRHICommandList& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit, FScene* Scene, const FViewInfo& View, bool bShouldRenderVolumetricFog, const FHeightFogRenderingParameters& Params)
{
	if (Scene->ExponentialFogs.Num() > 0)
	{
		TShaderMapRef<FHeightFogVS> VertexShader(View.ShaderMap);
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFogVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		
		if (bShouldRenderVolumetricFog)
		{
			if (View.FogInscatteringColorCubemap)
			{
				TShaderMapRef<TExponentialHeightFogPS<EHeightFogFeature::InscatteringTextureAndVolumetricFog> > ExponentialHeightFogPixelShader(View.ShaderMap);

				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ExponentialHeightFogPixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				VertexShader->SetParameters(RHICmdList, View);
				ExponentialHeightFogPixelShader->SetParameters(RHICmdList, View, Params);
			}
			else if (View.bUseDirectionalInscattering)
			{
				TShaderMapRef<TExponentialHeightFogPS<EHeightFogFeature::DirectionalLightInscatteringAndVolumetricFog> > ExponentialHeightFogPixelShader(View.ShaderMap);

				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ExponentialHeightFogPixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				VertexShader->SetParameters(RHICmdList, View);
				ExponentialHeightFogPixelShader->SetParameters(RHICmdList, View, Params);
			}
			else
			{
				TShaderMapRef<TExponentialHeightFogPS<EHeightFogFeature::HeightFogAndVolumetricFog> > ExponentialHeightFogPixelShader(View.ShaderMap);

				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ExponentialHeightFogPixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				VertexShader->SetParameters(RHICmdList, View);
				ExponentialHeightFogPixelShader->SetParameters(RHICmdList, View, Params);
			}
		}
		else
		{
			if (View.FogInscatteringColorCubemap)
			{
				TShaderMapRef<TExponentialHeightFogPS<EHeightFogFeature::InscatteringTexture> > ExponentialHeightFogPixelShader(View.ShaderMap);

				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ExponentialHeightFogPixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				VertexShader->SetParameters(RHICmdList, View);
				ExponentialHeightFogPixelShader->SetParameters(RHICmdList, View, Params);
			}
			else if (View.bUseDirectionalInscattering)
			{
				TShaderMapRef<TExponentialHeightFogPS<EHeightFogFeature::DirectionalLightInscattering> > ExponentialHeightFogPixelShader(View.ShaderMap);

				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ExponentialHeightFogPixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				VertexShader->SetParameters(RHICmdList, View);
				ExponentialHeightFogPixelShader->SetParameters(RHICmdList, View, Params);
			}
			else
			{
				TShaderMapRef<TExponentialHeightFogPS<EHeightFogFeature::HeightFog> > ExponentialHeightFogPixelShader(View.ShaderMap);

				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ExponentialHeightFogPixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				VertexShader->SetParameters(RHICmdList, View);
				ExponentialHeightFogPixelShader->SetParameters(RHICmdList, View, Params);
			}
		}
	}
}

void FDeferredShadingSceneRenderer::RenderViewFog(FRHICommandList& RHICmdList, const FViewInfo& View, const FHeightFogRenderingParameters& Params)
{
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	SCOPED_DRAW_EVENTF(RHICmdList, Fog, TEXT("ExponentialHeightFog %dx%d"), Params.ViewRect.Width(), Params.ViewRect.Height());
	SCOPED_GPU_STAT(RHICmdList, Fog);

	// Set the device viewport for the view.
	RHICmdList.SetViewport(Params.ViewRect.Min.X, Params.ViewRect.Min.Y, 0.0f, Params.ViewRect.Max.X, Params.ViewRect.Max.Y, 1.0f);
			
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			
	// disable alpha writes in order to preserve scene depth values on PC
	GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI();

	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	SetFogShaders(RHICmdList, GraphicsPSOInit, Scene, View, ShouldRenderVolumetricFog(), Params);

	// Draw a quad covering the view.
	RHICmdList.SetStreamSource(0, GScreenSpaceVertexBuffer.VertexBufferRHI, 0);
	RHICmdList.DrawIndexedPrimitive(GTwoTrianglesIndexBuffer.IndexBufferRHI, 0, 0, 4, 0, 2, 1);
}

bool FDeferredShadingSceneRenderer::RenderFog(FRHICommandListImmediate& RHICmdList, const FLightShaftsOutput& LightShaftsOutput)
{
	check(RHICmdList.IsOutsideRenderPass());

	if (Scene->ExponentialFogs.Num() > 0 
		// Fog must be done in the base pass for MSAA to work
		&& !IsForwardShadingEnabled(ShaderPlatform))
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		FUniformBufferRHIRef PassUniformBuffer = CreateSceneTextureUniformBufferDependentOnShadingPath(SceneContext, SceneContext.GetCurrentFeatureLevel(), ESceneTextureSetupMode::All, UniformBuffer_SingleFrame);

		SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite, true);

		FHeightFogRenderingParameters Parameters = { LightShaftsOutput, nullptr, FIntRect(), 1.0f, FVector4() };

		for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			if (View.IsPerspectiveProjection())
			{
				SCOPED_GPU_MASK(RHICmdList, View.GPUMask);

				FUniformBufferStaticBindings GlobalUniformBuffers(PassUniformBuffer);
				SCOPED_UNIFORM_BUFFER_GLOBAL_BINDINGS(RHICmdList, GlobalUniformBuffers);

				Parameters.ViewRect = View.ViewRect;
				RenderViewFog(RHICmdList, View, Parameters);
			}
		}

		SceneContext.FinishRenderingSceneColor(RHICmdList);

		return true;
	}

	return false;
}

void FDeferredShadingSceneRenderer::RenderUnderWaterFog(FRHICommandListImmediate& RHICmdList, FSingleLayerWaterPassData& PassData)
{
	check(RHICmdList.IsOutsideRenderPass());

	if (Scene->ExponentialFogs.Num() > 0
		// Fog must be done in the base pass for MSAA to work
		&& !IsForwardShadingEnabled(ShaderPlatform))
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		FUniformBufferRHIRef PassUniformBuffer = CreateSceneTextureUniformBufferDependentOnShadingPath(SceneContext, SceneContext.GetCurrentFeatureLevel(), ESceneTextureSetupMode::All, UniformBuffer_SingleFrame);

		FSceneRenderTargetItem& RT = PassData.SceneColorWithoutSingleLayerWater->GetRenderTargetItem();

		RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, RT.TargetableTexture.GetReference());
		FRHIRenderPassInfo RPInfo(RT.TargetableTexture, MakeRenderTargetActions(ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore));
		RHICmdList.BeginRenderPass(RPInfo, TEXT("BeginRenderingSceneColor"));

		FLightShaftsOutput LightShaftsOutput;
		LightShaftsOutput.LightShaftOcclusion = nullptr;
		FTextureRHIRef LinearDepthTextureRHI = PassData.SceneDepthWithoutSingleLayerWater->GetRenderTargetItem().ShaderResourceTexture;
		const float SINGLE_LAYER_WATER_DEPTH_SCALE = 100.0f; // This must match SINGLE_LAYER_WATER_DEPTH_SCALE from SingleLayerWaterCommon.ush and SingleLayerWaterComposite.usf. TODO deduplicate
		FHeightFogRenderingParameters Parameters = { LightShaftsOutput, LinearDepthTextureRHI, FIntRect(), SINGLE_LAYER_WATER_DEPTH_SCALE, FVector4() };

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			if (View.IsPerspectiveProjection())
			{
				SCOPED_GPU_MASK(RHICmdList, View.GPUMask);

				FUniformBufferStaticBindings GlobalUniformBuffers(PassUniformBuffer);
				SCOPED_UNIFORM_BUFFER_GLOBAL_BINDINGS(RHICmdList, GlobalUniformBuffers);

				// Specify the low resolution view rect
				Parameters.ViewRect = PassData.ViewData[ViewIndex].SceneWithoutSingleLayerWaterViewRect;
				Parameters.LinearDepthMinMaxUV = PassData.ViewData[ViewIndex].SceneWithoutSingleLayerWaterMinMaxUV;

				RenderViewFog(RHICmdList, View, Parameters);
			}
		}

		RHICmdList.EndRenderPass();

		RHICmdList.CopyToResolveTarget(RT.TargetableTexture, RT.ShaderResourceTexture, FResolveParams());
	}
}

bool ShouldRenderFog(const FSceneViewFamily& Family)
{
	const FEngineShowFlags EngineShowFlags = Family.EngineShowFlags;

	return EngineShowFlags.Fog
		&& EngineShowFlags.Materials 
		&& !Family.UseDebugViewPS()
		&& CVarFog.GetValueOnRenderThread() == 1
		&& !EngineShowFlags.StationaryLightOverlap 
		&& !EngineShowFlags.LightMapDensity;
}
