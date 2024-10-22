// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraComponent.h"
#include "VectorVM.h"
#include "NiagaraRenderer.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstance.h"
#include "MeshBatch.h"
#include "NiagaraConstants.h"
#include "NiagaraStats.h"
#include "NiagaraCommon.h"
#include "NiagaraDataInterface.h"
#include "UObject/NameTypes.h"
#include "NiagaraWorldManager.h"
#include "EngineUtils.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Engine/CollisionProfile.h"
#include "PrimitiveSceneInfo.h"
#include "NiagaraCrashReporterHandler.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraComponentSettings.h"
#include "NiagaraCustomVersion.h"
#include "Materials/MaterialInstanceDynamic.h"

DECLARE_CYCLE_STAT(TEXT("Sceneproxy create (GT)"), STAT_NiagaraCreateSceneProxy, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Component Tick (GT)"), STAT_NiagaraComponentTick, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Activate (GT)"), STAT_NiagaraComponentActivate, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Deactivate (GT)"), STAT_NiagaraComponentDeactivate, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Send Render Data (GT)"), STAT_NiagaraComponentSendRenderData, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Set Dynamic Data (RT)"), STAT_NiagaraSetDynamicData, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Get Dynamic Mesh Elements (RT)"), STAT_NiagaraComponentGetDynamicMeshElements, STATGROUP_Niagara);

DEFINE_LOG_CATEGORY(LogNiagara);

static int GNiagaraSoloTickEarly = 1;
static FAutoConsoleVariableRef CVarNiagaraSoloTickEarly(
	TEXT("fx.Niagara.Solo.TickEarly"),
	GNiagaraSoloTickEarly,
	TEXT("When enabled will tick kin the first available tick group."),
	ECVF_Default
);

static int GNiagaraSoloAllowAsyncWorkToEndOfFrame = 1;
static FAutoConsoleVariableRef CVarNiagaraSoloAllowAsyncWorkToEndOfFrame(
	TEXT("fx.Niagara.Solo.AllowAsyncWorkToEndOfFrame"),
	GNiagaraSoloAllowAsyncWorkToEndOfFrame,
	TEXT("Allow async work to continue until the end of the frame for solo Niagara instances, if false it will complete within the tick group it started in."),
	ECVF_Default
);

static int32 GbSuppressNiagaraSystems = 0;
static FAutoConsoleVariableRef CVarSuppressNiagaraSystems(
	TEXT("fx.SuppressNiagaraSystems"),
	GbSuppressNiagaraSystems,
	TEXT("If > 0 Niagara particle systems will not be activated. \n"),
	ECVF_Default
);

static int32 GNiagaraComponentWarnNullAsset = 0;
static FAutoConsoleVariableRef CVarNiagaraComponentWarnNullAsset(
	TEXT("fx.Niagara.ComponentWarnNullAsset"),
	GNiagaraComponentWarnNullAsset,
	TEXT("When enabled we will warn if a NiagaraComponent is activate with a null asset.  This is sometimes useful for tracking down components that can be removed."),
	ECVF_Default
);

static int32 GNiagaraComponentWarnAsleepCullReaction = 1;
static FAutoConsoleVariableRef CVarNiagaraComponentWarnAsleepCullReaction(
	TEXT("fx.Niagara.ComponentWarnAsleepCullReaction"),
	GNiagaraComponentWarnAsleepCullReaction,
	TEXT("When enabled we will warn if a NiagaraComponent completes naturally but has Asleep mode set for cullreaction."),
	ECVF_Default
);

static int32 GNiagaraUseFastSetUserParametersToDefaultValues = 1;
static FAutoConsoleVariableRef CVarNiagaraUseFastSetUserParametersToDefaultValues(
	TEXT("fx.Niagara.UseFastSetUserParametersToDefaultValues"),
	GNiagaraUseFastSetUserParametersToDefaultValues,
	TEXT("When a component is activated we will check the surpession list."),
	ECVF_Default
);

static int GNiagaraForceWaitForCompilationOnActivate = 0;
static FAutoConsoleVariableRef CVarNiagaraForceWaitForCompilationOnActivate(
	TEXT("fx.Niagara.ForceWaitForCompilationOnActivate"),
	GNiagaraForceWaitForCompilationOnActivate,
	TEXT("When a component is activated it will stall waiting for any pending shader compilation."),
	ECVF_Default
);

void DumpNiagaraComponents(UWorld* World)
{
	for (TActorIterator<AActor> ActorItr(World); ActorItr; ++ActorItr)
	{
		TArray<UNiagaraComponent*> Components;
		ActorItr->GetComponents<UNiagaraComponent>(Components, true);
		if (Components.Num() != 0)
		{
			UE_LOG(LogNiagara, Log, TEXT("Actor: \"%s\" ... %d Components"), *ActorItr->GetName(), Components.Num());
		}

		for (UNiagaraComponent* Component : Components)
		{
			if (Component != nullptr)
			{
				UNiagaraSystem* Sys = Component->GetAsset();
				FNiagaraSystemInstance* SysInst = Component->GetSystemInstance();
				if (!Sys)
				{
					UE_LOG(LogNiagara, Log, TEXT("Component: \"%s\" ... no system"), *Component->GetName());

				}
				else if (Sys && !SysInst)
				{
					UE_LOG(LogNiagara, Log, TEXT("Component: \"%s\" System: \"%s\" ... no instance"), *Component->GetName(), *Sys->GetName());

				}
				else
				{
					UE_LOG(LogNiagara, Log, TEXT("Component: \"%s\" System: \"%s\" | ReqExecState: %d | ExecState: %d | bIsActive: %d"), *Component->GetName(), *Sys->GetName(),
						(int32)SysInst->GetRequestedExecutionState(), (int32)SysInst->GetActualExecutionState(), Component->IsActive());

					if (!SysInst->IsComplete())
					{
						for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Emitter : SysInst->GetEmitters())
						{
							UE_LOG(LogNiagara, Log, TEXT("    Emitter: \"%s\" | ExecState: %d | NumParticles: %d | CPUTime: %f"), *Emitter->GetEmitterHandle().GetUniqueInstanceName(),
								(int32)Emitter->GetExecutionState(), Emitter->GetNumParticles(), Emitter->GetTotalCPUTimeMS());
						}
					}
				}
			}
		}
	}
}

FAutoConsoleCommandWithWorld DumpNiagaraComponentsCommand(
	TEXT("DumpNiagaraComponents"),
	TEXT("Dump Existing Niagara Components"),
	FConsoleCommandWithWorldDelegate::CreateStatic(&DumpNiagaraComponents)
);


FNiagaraSceneProxy::FNiagaraSceneProxy(const UNiagaraComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent, InComponent->GetAsset() ? InComponent->GetAsset()->GetFName() : FName())
		, bRenderingEnabled(true)
		, RuntimeCycleCount(nullptr)
#if WITH_PARTICLE_PERF_STATS
		, PerfAsset(InComponent->GetAsset())
#endif
{
	FNiagaraSystemInstance* SystemInst = InComponent->GetSystemInstance();
	if (SystemInst)
	{
		//UE_LOG(LogNiagara, Warning, TEXT("FNiagaraSceneProxy %p"), this);
		CreateRenderers(InComponent);
		Batcher = SystemInst->GetBatcher();

#if STATS
		SystemStatID = InComponent->GetAsset()->GetStatID(false, false);
#endif

		RuntimeCycleCount = InComponent->GetAsset()->GetCycleCounter(false, false);
	}
}

SIZE_T FNiagaraSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FNiagaraSceneProxy::ReleaseRenderers()
{
	if (EmitterRenderers.Num() > 0)
	{
		//Renderers must be freed on the render thread.
		ENQUEUE_RENDER_COMMAND(ReleaseRenderersCommand)(
			[ToDeleteEmitterRenderers = MoveTemp(EmitterRenderers)](FRHICommandListImmediate& RHICmdList)
		{
			for (FNiagaraRenderer* EmitterRenderer : ToDeleteEmitterRenderers)
			{
				if (EmitterRenderer)
				{
					EmitterRenderer->ReleaseRenderThreadResources();
					delete EmitterRenderer;
				}
			}
		});
		EmitterRenderers.Empty();
	}
	RendererDrawOrder.Empty();
}

void FNiagaraSceneProxy::CreateRenderers(const UNiagaraComponent* Component)
{
	LLM_SCOPE(ELLMTag::Niagara);

	check(Component);
	check(Component->GetSystemInstance());

	UNiagaraSystem* System = Component->GetAsset();
	check(System);

	bAlwaysHasVelocity = false;

	ReleaseRenderers();

	RendererDrawOrder = System->GetRendererDrawOrder();
	EmitterRenderers.Reserve(RendererDrawOrder.Num());

	ERHIFeatureLevel::Type FeatureLevel = GetScene().GetFeatureLevel();
	for(TSharedRef<const FNiagaraEmitterInstance, ESPMode::ThreadSafe> EmitterInst : Component->GetSystemInstance()->GetEmitters())
	{
		if (UNiagaraEmitter* Emitter = EmitterInst->GetCachedEmitter())
		{
			Emitter->ForEachEnabledRenderer(
				[&](UNiagaraRendererProperties* Properties)
				{
					//We can skip creation of the renderer if the current quality level doesn't support it. If the quality level changes all systems are fully reinitialized.
					FNiagaraRenderer* NewRenderer = nullptr;
					if (Properties->GetIsActive() && EmitterInst->GetData().IsInitialized() && !EmitterInst->IsDisabled())
					{
						NewRenderer = Properties->CreateEmitterRenderer(FeatureLevel, &EmitterInst.Get(), Component);
						bAlwaysHasVelocity |= Properties->bMotionBlurEnabled;
					}
					EmitterRenderers.Add(NewRenderer);
				}
			);
		}
	}

	// If we have renderers then the draw order on the system should match, when compiling the number of renderers can be zero
	checkf((EmitterRenderers.Num() == 0) || (EmitterRenderers.Num() == RendererDrawOrder.Num()), TEXT("EmitterRenderers Num %d does not match System DrawOrder %d"), EmitterRenderers.Num(), RendererDrawOrder.Num());
}

FNiagaraSceneProxy::~FNiagaraSceneProxy()
{
	Batcher = nullptr;

	//UE_LOG(LogNiagara, Warning, TEXT("~FNiagaraSceneProxy %p"), this);
	check(IsInRenderingThread());
	for (FNiagaraRenderer* EmitterRenderer : EmitterRenderers)
	{
		if (EmitterRenderer)
		{
			EmitterRenderer->ReleaseRenderThreadResources();
			delete EmitterRenderer;
		}
	}
	UniformBufferNoVelocity.ReleaseResource();
	EmitterRenderers.Empty();
}

void FNiagaraSceneProxy::ReleaseRenderThreadResources()
{
	for (FNiagaraRenderer* Renderer : EmitterRenderers)
	{
		if (Renderer)
		{
			Renderer->ReleaseRenderThreadResources();
		}
	}
	UniformBufferNoVelocity.ReleaseResource();
}

// FPrimitiveSceneProxy interface.
void FNiagaraSceneProxy::CreateRenderThreadResources()
{
	LLM_SCOPE(ELLMTag::Niagara);
	for (FNiagaraRenderer* Renderer : EmitterRenderers)
	{
		if (Renderer)
		{
			Renderer->CreateRenderThreadResources(Batcher);
		}
	}
	return;
}

void FNiagaraSceneProxy::OnTransformChanged()
{
	LocalToWorldInverse = GetLocalToWorld().Inverse();
	UniformBufferNoVelocity.ReleaseResource();
}

FPrimitiveViewRelevance FNiagaraSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Relevance;

	if (bRenderingEnabled == false || !FNiagaraUtilities::SupportsNiagaraRendering(View->GetFeatureLevel()))
	{
		return Relevance;
	}
	Relevance.bDynamicRelevance = true;

	for (FNiagaraRenderer* Renderer : EmitterRenderers)
	{
		if (Renderer)
		{
			Relevance |= Renderer->GetViewRelevance(View, this);
		}
	}

	Relevance.bVelocityRelevance = IsMovable() && Relevance.bOpaque && Relevance.bRenderInMainPass;

	return Relevance;
}

FRHIUniformBuffer* FNiagaraSceneProxy::GetUniformBufferNoVelocity() const
{
	if ( !UniformBufferNoVelocity.IsInitialized() )
	{
		bool bHasPrecomputedVolumetricLightmap;
		FMatrix PreviousLocalToWorld;
		int32 SingleCaptureIndex;
		bool bOutputVelocity;
		FPrimitiveSceneInfo* LocalPrimitiveSceneInfo = GetPrimitiveSceneInfo();
		GetScene().GetPrimitiveUniformShaderParameters_RenderThread(LocalPrimitiveSceneInfo, bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

		UniformBufferNoVelocity.SetContents(
			GetPrimitiveUniformShaderParameters(
				GetLocalToWorld(),
				PreviousLocalToWorld,
				GetActorPosition(),
				GetBounds(),
				GetLocalBounds(),
				GetLocalBounds(),
				ReceivesDecals(),
				HasDistanceFieldRepresentation(),
				HasDynamicIndirectShadowCasterRepresentation(),
				UseSingleSampleShadowFromStationaryLights(),
				bHasPrecomputedVolumetricLightmap,
				DrawsVelocity(),
				GetLightingChannelMask(),
				LpvBiasMultiplier,
				LocalPrimitiveSceneInfo ? LocalPrimitiveSceneInfo->GetLightmapDataOffset() : 0,
				SingleCaptureIndex,
				false,
				GetCustomPrimitiveData()
			)
		);
		UniformBufferNoVelocity.InitResource();
	}
	return UniformBufferNoVelocity.GetUniformBufferRHI();
}

uint32 FNiagaraSceneProxy::GetMemoryFootprint() const
{ 
	return (sizeof(*this) + GetAllocatedSize()); 
}

uint32 FNiagaraSceneProxy::GetAllocatedSize() const
{ 
	uint32 DynamicDataSize = 0;
	for (FNiagaraRenderer* Renderer : EmitterRenderers)
	{
		if (Renderer)
		{
			DynamicDataSize += Renderer->GetDynamicDataSize();
		}
	}
	return FPrimitiveSceneProxy::GetAllocatedSize() + DynamicDataSize;
}

bool FNiagaraSceneProxy::GetRenderingEnabled() const
{
	return bRenderingEnabled;
}

void FNiagaraSceneProxy::SetRenderingEnabled(bool bInRenderingEnabled)
{
	bRenderingEnabled = bInRenderingEnabled;
}

void FNiagaraSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_RT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentGetDynamicMeshElements);

#if STATS
	FScopeCycleCounter SystemStatCounter(SystemStatID);
#endif

	FNiagaraScopedRuntimeCycleCounter RuntimeScope(RuntimeCycleCount);

	for (int32 RendererIdx : RendererDrawOrder)
	{
		FNiagaraRenderer* Renderer = EmitterRenderers[RendererIdx];
		if (Renderer && (Renderer->GetSimTarget() != ENiagaraSimTarget::GPUComputeSim || FNiagaraUtilities::AllowGPUParticles(ViewFamily.GetShaderPlatform())))
		{
			Renderer->GetDynamicMeshElements(Views, ViewFamily, VisibilityMap, Collector, this);
		}
	}

	if (ViewFamily.EngineShowFlags.Particles)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
				if (HasCustomOcclusionBounds())
				{
					RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetCustomOcclusionBounds(), IsSelected());
				}
			}
		}
	}
}

#if RHI_RAYTRACING
void FNiagaraSceneProxy::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances)
{
	FNiagaraScopedRuntimeCycleCounter RuntimeScope(RuntimeCycleCount);
	for (FNiagaraRenderer* Renderer : EmitterRenderers)
	{
		if (Renderer)
		{
			Renderer->GetDynamicRayTracingInstances(Context, OutRayTracingInstances, this);
		}
	}
}
#endif

void FNiagaraSceneProxy::GatherSimpleLights(const FSceneViewFamily& ViewFamily, FSimpleLightArray& OutParticleLights) const
{
	FNiagaraScopedRuntimeCycleCounter RuntimeScope(RuntimeCycleCount);
	for (int32 Idx = 0; Idx < EmitterRenderers.Num(); Idx++)
	{
		FNiagaraRenderer *Renderer = EmitterRenderers[Idx];
		if (Renderer && Renderer->HasLights())
		{
			Renderer->GatherSimpleLights(OutParticleLights);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

UNiagaraComponent::UNiagaraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bForceSolo(false)
	, AgeUpdateMode(ENiagaraAgeUpdateMode::TickDeltaTime)
	, DesiredAge(0.0f)
	, LastHandledDesiredAge(0.0f)
	, bCanRenderWhileSeeking(true)
	, SeekDelta(1 / 30.0f)
	, MaxSimTime(33.0f / 1000.0f)
	, bIsSeeking(false)
	, bAutoDestroy(false)
	, MaxTimeBeforeForceUpdateTransform(5.0f)
#if WITH_EDITOR
	, PreviewLODDistance(0.0f)
	, bEnablePreviewLODDistance(false)
	, bWaitForCompilationOnActivate(false)
#endif
	, bAwaitingActivationDueToNotReady(false)
	, bActivateShouldResetWhenReady(false)
	, bDidAutoAttach(false)
	, bAllowScalability(true)
	, bIsCulledByScalability(false)
	, bDuringUpdateContextReset(false)
	//, bIsChangingAutoAttachment(false)
	, ScalabilityManagerHandle(INDEX_NONE)
	, ForceUpdateTransformTime(0.0f)
	, CurrLocalBounds(ForceInit)
{
	OverrideParameters.SetOwner(this);

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = GNiagaraSoloTickEarly ? TG_PrePhysics : TG_DuringPhysics;
	PrimaryComponentTick.EndTickGroup = GNiagaraSoloAllowAsyncWorkToEndOfFrame ? TG_LastDemotable : ETickingGroup(PrimaryComponentTick.TickGroup);
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.SetTickFunctionEnable(false);
	bTickInEditor = true;
	bAutoActivate = true;
	bRenderingEnabled = true;
	SavedAutoAttachRelativeScale3D = FVector(1.f, 1.f, 1.f);

	SetGenerateOverlapEvents(false);
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
}

/********* UFXSystemComponent *********/
void UNiagaraComponent::SetBoolParameter(FName Parametername, bool Param)
{
	SetVariableBool(Parametername, Param);
}

void UNiagaraComponent::SetIntParameter(FName ParameterName, int Param)
{
	SetVariableInt(ParameterName, Param);
}

void UNiagaraComponent::SetFloatParameter(FName ParameterName, float Param)
{
	SetVariableFloat(ParameterName, Param);
}

void UNiagaraComponent::SetVectorParameter(FName ParameterName, FVector Param)
{
	SetVariableVec3(ParameterName, Param);
}

void UNiagaraComponent::SetColorParameter(FName ParameterName, FLinearColor Param)
{
	SetVariableLinearColor(ParameterName, Param);
}

void UNiagaraComponent::SetActorParameter(FName ParameterName, class AActor* Param)
{
	SetVariableActor(ParameterName, Param);
}

UFXSystemAsset* UNiagaraComponent::GetFXSystemAsset() const
{
	return Asset;
}

void UNiagaraComponent::SetEmitterEnable(FName EmitterName, bool bNewEnableState)
{
	if (!SystemInstance)
	{
		return;
	}
	if (SystemInstance.Get() && !SystemInstance->IsComplete())
	{
		SystemInstance->SetEmitterEnable(EmitterName, bNewEnableState);
	}
}

void UNiagaraComponent::ReleaseToPool()
{
	if (PoolingMethod != ENCPoolMethod::ManualRelease)
	{		
		if (UNiagaraComponentPool::Enabled())//Only emit this warning if pooling is enabled. If it's not, all components will have PoolingMethod none.
		{	
			UE_LOG(LogNiagara, Warning, TEXT("Manually releasing a PSC to the pool that was not spawned with ENCPoolMethod::ManualRelease. Asset=%s Component=%s"),
				Asset ? *Asset->GetPathName() : TEXT("NULL"), *GetPathName()
			);
		}
		return;
	}

	if (!IsActive())
	{
		UnregisterWithScalabilityManager();

		//If we're already complete then release to the pool straight away.
		UWorld* World = GetWorld();
		check(World);
		if( FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(World))
		{
			WorldMan->GetComponentPool()->ReclaimWorldParticleSystem(this);
		}
		else
		{
			DestroyComponent();
		}
	}
	else
	{
		//If we haven't completed, deactivate and defer release to pool.
		PoolingMethod = ENCPoolMethod::ManualRelease_OnComplete;
		Deactivate();
	}
}

uint32 UNiagaraComponent::GetApproxMemoryUsage() const
{
	// TODO: implement memory usage for the component pool statistics
	return 1;
}

void UNiagaraComponent::ActivateSystem(bool bFlagAsJustAttached)
{
	// Attachment is handled different in niagara so the bFlagAsJustAttached is ignored here.
	if (IsActive())
	{
		// If the system is already active then activate with reset to reset the system simulation but
		// leave the emitter simulations active.
		bool bResetSystem = true;
		bool bIsFromScalability = false;
		ActivateInternal(bResetSystem, bIsFromScalability);
	}
	else
	{
		// Otherwise just follow the standard activate path.
		Activate();
	}
}

/********* UFXSystemComponent *********/


void UNiagaraComponent::TickComponent(float DeltaSeconds, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	LLM_SCOPE(ELLMTag::Niagara);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentTick);

	FScopeCycleCounter SystemStatCounter(Asset ? Asset->GetStatID(true, false) : TStatId());

	if (bAwaitingActivationDueToNotReady)
	{
		Activate(bActivateShouldResetWhenReady);
		return;
	}

	if (!SystemInstance)
	{
		return;
	}

	if (!IsActive() && bAutoActivate && SystemInstance.Get() && SystemInstance->GetAreDataInterfacesInitialized())
	{
		Activate();
	}

	check(SystemInstance->IsSolo());
	if (IsActive() && SystemInstance.Get() && !SystemInstance->IsComplete())
	{
		check(Asset != nullptr);
		Asset->AddToInstanceCountStat(1, true);
		INC_DWORD_STAT_BY(STAT_TotalNiagaraSystemInstances, 1);
		INC_DWORD_STAT_BY(STAT_TotalNiagaraSystemInstancesSolo, 1);

		// If the interfaces have changed in a meaningful way, we need to potentially rebind and update the values.
		if (OverrideParameters.GetInterfacesDirty())
		{
			SystemInstance->Reset(FNiagaraSystemInstance::EResetMode::ReInit);
		}

		if (AgeUpdateMode == ENiagaraAgeUpdateMode::TickDeltaTime)
		{
			SystemInstance->ManualTick(DeltaSeconds, ThisTickFunction->IsCompletionHandleValid() ? ThisTickFunction->GetCompletionHandle() : nullptr);
		}
		else if(AgeUpdateMode == ENiagaraAgeUpdateMode::DesiredAge)
		{
			float AgeDiff = FMath::Max(DesiredAge, 0.0f) - SystemInstance->GetAge();
			int32 TicksToProcess = 0;
			if (FMath::Abs(AgeDiff) < KINDA_SMALL_NUMBER)
			{
				AgeDiff = 0.0f;
			}
			else
			{
				if (AgeDiff < 0.0f)
				{
					SystemInstance->Reset(FNiagaraSystemInstance::EResetMode::ResetAll);
					AgeDiff = DesiredAge - SystemInstance->GetAge();
				}

				if (AgeDiff > 0.0f)
				{
					FNiagaraSystemSimulation* SystemSim = GetSystemSimulation().Get();
					if (SystemSim)
					{
						double StartTime = FPlatformTime::Seconds();
						double CurrentTime = StartTime;

						TicksToProcess = FMath::FloorToInt(AgeDiff / SeekDelta);
						for (; TicksToProcess > 0 && CurrentTime - StartTime < MaxSimTime; --TicksToProcess)
						{
							//Cannot do multiple tick off the game thread here without additional work. So we pass in null for the completion event which will force GT execution.
							//If this becomes a perf problem I can add a new path for the tick code to handle multiple ticks.
							SystemInstance->ManualTick(SeekDelta, nullptr);
							CurrentTime = FPlatformTime::Seconds();
						}
					}
				}
			}

			if (TicksToProcess == 0)
			{
				bIsSeeking = false;
			}
		}
		else if (AgeUpdateMode == ENiagaraAgeUpdateMode::DesiredAgeNoSeek)
		{
			int32 MaxForwardFrames = 5; // HACK - for some reason sequencer jumps forwards by multiple frames on pause, so this is being added to allow for FX to stay alive when being controlled by sequencer in the editor.  This should be lowered once that issue is fixed.
			float AgeDiff = DesiredAge - LastHandledDesiredAge;
			if (AgeDiff < 0)
			{
				if (FMath::Abs(AgeDiff) >= SeekDelta)
				{
					// When going back in time for a frame or more, reset and simulate a single frame.  We ignore small negative changes to delta
					// time which can happen when controlling time with the timeline and the time snaps to a previous time when paused.
					SystemInstance->Reset(FNiagaraSystemInstance::EResetMode::ResetAll);
					SystemInstance->ManualTick(SeekDelta, nullptr);
				}
			}
			else if (AgeDiff < MaxForwardFrames * SeekDelta)
			{
				// Allow ticks between 0 and MaxForwardFrames, but don't ever send more then 2 x the seek delta.
				SystemInstance->ManualTick(FMath::Min(AgeDiff, 2 * SeekDelta), nullptr);
			}
			else
			{
				// When going forward in time for more than MaxForwardFrames, reset and simulate a single frame.
				SystemInstance->Reset(FNiagaraSystemInstance::EResetMode::ResetAll);
				SystemInstance->ManualTick(SeekDelta, nullptr);
			}
			LastHandledDesiredAge = DesiredAge;
		}

		if (SceneProxy != nullptr)
		{
			FNiagaraSceneProxy* NiagaraProxy = static_cast<FNiagaraSceneProxy*>(SceneProxy);
			NiagaraProxy->SetRenderingEnabled(bRenderingEnabled && (bCanRenderWhileSeeking || bIsSeeking == false));
		}
	}
}

const UObject* UNiagaraComponent::AdditionalStatObject() const
{
	return Asset;
}

void UNiagaraComponent::ResetSystem()
{
	Activate(true);
}

void UNiagaraComponent::ReinitializeSystem()
{
	DestroyInstance();
	Activate(true);
}

bool UNiagaraComponent::GetRenderingEnabled() const
{
	return bRenderingEnabled;
}

void UNiagaraComponent::SetRenderingEnabled(bool bInRenderingEnabled)
{
	bRenderingEnabled = bInRenderingEnabled;
}

void UNiagaraComponent::AdvanceSimulation(int32 TickCount, float TickDeltaSeconds)
{
	if (SystemInstance.IsValid() && TickDeltaSeconds > SMALL_NUMBER)
	{
		SystemInstance->AdvanceSimulation(TickCount, TickDeltaSeconds);
	}
}

void UNiagaraComponent::AdvanceSimulationByTime(float SimulateTime, float TickDeltaSeconds)
{
	if (SystemInstance.IsValid() && TickDeltaSeconds > SMALL_NUMBER)
	{
		int32 TickCount = SimulateTime / TickDeltaSeconds;
		SystemInstance->AdvanceSimulation(TickCount, TickDeltaSeconds);
	}
}

void UNiagaraComponent::SetPaused(bool bInPaused)
{
	if (SystemInstance.IsValid())
	{
		SystemInstance->SetPaused(bInPaused);
	}
}

bool UNiagaraComponent::IsPaused()const
{
	if (SystemInstance.IsValid())
	{
		return SystemInstance->IsPaused();
	}
	return false;
}

UNiagaraDataInterface * UNiagaraComponent::GetDataInterface(const FString &Name)
{

	// @todo-threadsafety Think of a better way to do this!
	if (!SystemInstance || SystemInstance->GetEmitters().Num() == 0 || !SystemInstance->GetEmitters()[0]->GetGPUContext())
	{
		return nullptr;
	}
	
	FNiagaraComputeExecutionContext* GPUContext = SystemInstance->GetEmitters()[0]->GetGPUContext();
	const TArray<FNiagaraScriptDataInterfaceCompileInfo> &DataInterfaceInfo = GPUContext->GPUScript->GetVMExecutableData().DataInterfaceInfo;
	const TArray<UNiagaraDataInterface*>& DataInterfaces = GPUContext->CombinedParamStore.GetDataInterfaces();

	int Index = 0;
	for (UNiagaraDataInterface* Interface : DataInterfaces)
	{
		if (DataInterfaceInfo[Index].Name.GetPlainNameString() == Name)
		{			
			return Interface;
		}	
		++Index;
	}
	return nullptr;
}

bool UNiagaraComponent::IsWorldReadyToRun() const
{
	// The niagara system instance assumes that a batcher exists when it is created. We need to wait until this has happened before successfully activating this system.
	bool FXSystemExists = false;
	bool WorldManagerExists = false;
	UWorld* World = GetWorld();
	if (World)
	{
		if (World->Scene)
		{
			FFXSystemInterface*  FXSystemInterface = World->Scene->GetFXSystem();
			if (FXSystemInterface)
			{
				NiagaraEmitterInstanceBatcher* FoundBatcher = static_cast<NiagaraEmitterInstanceBatcher*>(FXSystemInterface->GetInterface(NiagaraEmitterInstanceBatcher::Name));
				if (FoundBatcher != nullptr)
				{
					FXSystemExists = true;
				}
			}
		}

		FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World);
		if (WorldManager)
		{
			WorldManagerExists = true;
		}
	}

	return WorldManagerExists && FXSystemExists;
}

bool UNiagaraComponent::InitializeSystem()
{
	if (SystemInstance.IsValid() == false)
	{
		LLM_SCOPE(ELLMTag::Niagara);
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
		
		UWorld* World = GetWorld();
		check(World);
		check(Asset);

		const bool bPooled = PoolingMethod != ENCPoolMethod::None;
		FNiagaraSystemInstance::AllocateSystemInstance(SystemInstance, *World, *Asset, &OverrideParameters, this, TickBehavior, bPooled);
		//UE_LOG(LogNiagara, Log, TEXT("Create System: %p | %s\n"), SystemInstance.Get(), *GetAsset()->GetFullName());
#if WITH_EDITORONLY_DATA
		OnSystemInstanceChangedDelegate.Broadcast();
#endif
		SystemInstance->Init(bForceSolo);
		SystemInstance->SetOnPostTick(FNiagaraSystemInstance::FOnPostTick::CreateUObject(this, &UNiagaraComponent::PostSystemTick_GameThread));
		MarkRenderStateDirty();
		return true;
	}
	return false;
}

void UNiagaraComponent::Activate(bool bReset /* = false */)
{
	ActivateInternal(bReset, false);
}

void UNiagaraComponent::ActivateInternal(bool bReset /* = false */, bool bIsScalabilityCull)
{
	bAwaitingActivationDueToNotReady = false;

	// Reset our local bounds on reset
	if (bReset)
	{
		CurrLocalBounds.Init();
	}

	if (GbSuppressNiagaraSystems != 0)
	{
		UnregisterWithScalabilityManager();
		OnSystemComplete();
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentActivate);
	if (Asset == nullptr)
	{
		DestroyInstance();
		if (GNiagaraComponentWarnNullAsset && !HasAnyFlags(RF_DefaultSubObject | RF_ArchetypeObject | RF_ClassDefaultObject))
		{
			UE_LOG(LogNiagara, Warning, TEXT("Failed to activate Niagara Component due to missing or invalid asset! (%s)"), *GetFullName());
		}
		SetComponentTickEnabled(false);
		return;
	}

	UWorld* World = GetWorld();
	// If the particle system can't ever render (ie on dedicated server or in a commandlet) than do not activate...
	if (!FApp::CanEverRender() || !World || World->IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	if (!IsRegistered())
	{
		return;
	}

	// Should we force activation to fail?
	if (UNiagaraComponentSettings::ShouldSuppressActivation(Asset))
	{
		return;
	}


	// On the off chance that the user changed the asset, we need to clear out the existing data.
	if (SystemInstance.IsValid() && SystemInstance->GetSystem() != Asset)
	{
		UnregisterWithScalabilityManager();
		OnSystemComplete();
	}

#if WITH_EDITOR
	// In case we're not yet ready to run due to compilation requests, go ahead and keep polling there..
	if (Asset->HasOutstandingCompilationRequests())
	{
		if (bWaitForCompilationOnActivate || GNiagaraForceWaitForCompilationOnActivate)
		{
			Asset->WaitForCompilationComplete(true);
		}
		Asset->PollForCompilationComplete();
	}
#endif
	
	if (!Asset->IsReadyToRun() || !IsWorldReadyToRun())
	{
		bAwaitingActivationDueToNotReady = true;
		bActivateShouldResetWhenReady = bReset;
		SetComponentTickEnabled(true);
		return;
	}

	if (bReset)
	{
		UnregisterWithScalabilityManager();
	}

	if (!bIsScalabilityCull && bIsCulledByScalability && ScalabilityManagerHandle != INDEX_NONE)
	{
		//If this is a non scalability activate call and we're still registered with the manager.
		//If we reach this point then we must have been previously culled by scalability so bail here.
		return;
	}

	bIsCulledByScalability = false; 
	if (ShouldPreCull())
	{
		//We have decided to pre cull the system.
		OnSystemComplete();
		return;
	}

	Super::Activate(bReset);

	// Early out if we're not forcing a reset, and both the component and system instance are already active.
	if (bReset == false &&
		IsActive() &&
		SystemInstance != nullptr &&
		SystemInstance->GetRequestedExecutionState() == ENiagaraExecutionState::Active &&
		SystemInstance->GetActualExecutionState() == ENiagaraExecutionState::Active)
	{
		return;
	}

	//UE_LOG(LogNiagara, Log, TEXT("Activate: %p - %s - %s"), this, *Asset->GetName(), bIsScalabilityCull ? TEXT("Scalability") : TEXT(""));
	
	// Auto attach if requested
	const bool bWasAutoAttached = bDidAutoAttach;
	bDidAutoAttach = false;
	if (bAutoManageAttachment)
	{
		USceneComponent* NewParent = AutoAttachParent.Get();
		if (NewParent)
		{
			const bool bAlreadyAttached = GetAttachParent() && (GetAttachParent() == NewParent) && (GetAttachSocketName() == AutoAttachSocketName) && GetAttachParent()->GetAttachChildren().Contains(this);
			if (!bAlreadyAttached)
			{
				bDidAutoAttach = bWasAutoAttached;
				CancelAutoAttachment(true);
				SavedAutoAttachRelativeLocation = GetRelativeLocation();
				SavedAutoAttachRelativeRotation = GetRelativeRotation();
				SavedAutoAttachRelativeScale3D = GetRelativeScale3D();
				//bIsChangingAutoAttachment = true;
				AttachToComponent(NewParent, FAttachmentTransformRules(AutoAttachLocationRule, AutoAttachRotationRule, AutoAttachScaleRule, bAutoAttachWeldSimulatedBodies), AutoAttachSocketName);
				//bIsChangingAutoAttachment = false;
			}

			bDidAutoAttach = true;
			//bFlagAsJustAttached = true;
		}
		else
		{
			CancelAutoAttachment(true);
		}
	}
	
#if WITH_EDITOR
	//TODO: Do this else where. I get needing to ensure params are correct from the component but these are stomping over runtime changes to the params in editor builds.
	//For now we can bypass the worst of the impact by disallowing in game worlds.
	if(!World->IsGameWorld())
	{
		ApplyOverridesToParameterStore();
	}
#endif

	FNiagaraSystemInstance::EResetMode ResetMode = FNiagaraSystemInstance::EResetMode::ResetSystem;
	if (InitializeSystem())
	{
		ResetMode = FNiagaraSystemInstance::EResetMode::None;//Already done a reinit sete
	}

	if (!SystemInstance)
	{
		OnSystemComplete();
		return;
	}

	RegisterWithScalabilityManager();

	// NOTE: This call can cause SystemInstance itself to get destroyed with auto destroy systems
	SystemInstance->Activate(ResetMode);
	
	if (SystemInstance && SystemInstance->IsSolo())
	{
		const ETickingGroup SoloTickGroup = SystemInstance->CalculateTickGroup();
		PrimaryComponentTick.TickGroup = FMath::Max(GNiagaraSoloTickEarly ? TG_PrePhysics : TG_DuringPhysics, SoloTickGroup);
		PrimaryComponentTick.EndTickGroup = GNiagaraSoloAllowAsyncWorkToEndOfFrame ? TG_LastDemotable : ETickingGroup(PrimaryComponentTick.TickGroup);

		/** We only need to tick the component if we require solo mode. */
		SetComponentTickEnabled(true);
	}
	else
	{
		SetComponentTickEnabled(false);
	}
}

void UNiagaraComponent::Deactivate()
{
	DeactivateInternal(false);
}

void UNiagaraComponent::DeactivateInternal(bool bIsScalabilityCull /* = false */)
{
	bool bWasCulledByScalabiltiy = bIsCulledByScalability;

	if (bIsScalabilityCull)
	{
		bIsCulledByScalability = true;
	}
	else
	{
		// Unregister with the scalability manager if this is a genuine deactivation from outside.
		// The scalability manager itself can call this function when culling systems.
		UnregisterWithScalabilityManager();
	}

	if (IsActive() && SystemInstance)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentDeactivate);
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);

		//UE_LOG(LogNiagara, Log, TEXT("Deactivate: %p - %s - %s"), this, *Asset->GetName(), bIsScalabilityCull ? TEXT("Scalability") : TEXT(""));

		// Don't deactivate in solo mode as we are not ticked by the world but rather the component
		// Deactivating will cause the system to never Complete
		if (SystemInstance->IsSolo() == false)
		{
			Super::Deactivate();
		}

		SystemInstance->Deactivate();

		// We are considered active until we are complete
		// Note: Deactivate call can finalize -> complete the system -> release to pool -> unregister which will result in a nullptr for the SystemInstance
		SetActiveFlag(SystemInstance ? !SystemInstance->IsComplete() : false);
	}
	else
	{
		Super::Deactivate();

		if(bWasCulledByScalabiltiy && !bIsCulledByScalability)//We were culled by scalability but no longer, ensure we've handled completion correctly. E.g. returned to the pool etc.
		{
			OnSystemComplete();
		}
		SetActiveFlag(false);
	}
}

void UNiagaraComponent::DeactivateImmediate()
{
	DeactivateImmediateInternal(false);
}

void UNiagaraComponent::DeactivateImmediateInternal(bool bIsScalabilityCull)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentDeactivate);
	Super::Deactivate();

	bool bWasCulledByScalability = bIsCulledByScalability;

	//UE_LOG(LogNiagara, Log, TEXT("DeactivateImmediate: %p - %s - %s"), this, *Asset->GetName(), bIsScalabilityCull ? TEXT("Scalability") : TEXT(""));

	//UE_LOG(LogNiagara, Log, TEXT("Deactivate %s"), *GetName());

	//Unregister with the scalability manager if this is a genuine deactivation from outside.
	//The scalability manager itself can call this function when culling systems.
	if (bIsScalabilityCull)
	{
		bIsCulledByScalability = true;
	}
	else
	{
		UnregisterWithScalabilityManager();
	}

	SetActiveFlag(false);

	if (SystemInstance)
	{
		SystemInstance->Deactivate(true);
	}
	else if (bWasCulledByScalability && !bIsCulledByScalability)//We were culled by scalability but no longer, ensure we've handled completion correctly. E.g. returned to the pool etc.
	{
		OnSystemComplete();
	}
}

bool UNiagaraComponent::ShouldPreCull()
{
	if (bAllowScalability)
	{
		if (UNiagaraSystem* System = GetAsset())
		{
			if (UNiagaraEffectType* EffectType = System->GetEffectType())
			{
				if (FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(GetWorld()))
				{
					if (EffectType->UpdateFrequency == ENiagaraScalabilityUpdateFrequency::SpawnOnly)
					{
						//If we're just set to check on spawn then check for precull here.
						return WorldMan->ShouldPreCull(GetAsset(), this);
					}
				}
			}
		}
	}

	return false;
}

void UNiagaraComponent::RegisterWithScalabilityManager()
{
	if (ScalabilityManagerHandle == INDEX_NONE && bAllowScalability)
	{
		if (UNiagaraSystem* System = GetAsset())
		{
			if (UNiagaraEffectType* EffectType = System->GetEffectType())
			{
				if (FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(GetWorld()))
				{
					WorldMan->RegisterWithScalabilityManager(this);
				}
			}
		}
	}
}

void UNiagaraComponent::UnregisterWithScalabilityManager()
{
	if (ScalabilityManagerHandle != INDEX_NONE)
	{
		if (FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(GetWorld()))
		{
			WorldMan->UnregisterWithScalabilityManager(this);
		}
	}
	bIsCulledByScalability = false;
	ScalabilityManagerHandle = INDEX_NONE;//Just to be sure our state is unregistered.
}

void UNiagaraComponent::PostSystemTick_GameThread()
{
	check(SystemInstance.IsValid()); // sanity

#if WITH_EDITOR
	if (SystemInstance->HandleNeedsUIResync())
	{
		OnSynchronizedWithAssetParametersDelegate.Broadcast();
	}
#endif

	// Check if the system got completed
	if (IsActive() && SystemInstance->IsComplete())
	{
		OnSystemComplete();
		return;
	}

	// NOTE: Since this is happening before scene visibility calculation, it's likely going to be off by a frame
	SystemInstance->SetLastRenderTime(GetLastRenderTime());

	MarkRenderDynamicDataDirty();

	// Check to force update our transform based on a timer or bounds expanding beyond their previous local boundaries
	const FBox NewLocalBounds = SystemInstance->GetLocalBounds();
	ForceUpdateTransformTime += GetWorld()->GetDeltaSeconds();
	if (!CurrLocalBounds.IsValid ||
		!CurrLocalBounds.IsInsideOrOn(NewLocalBounds.Min) ||
		!CurrLocalBounds.IsInsideOrOn(NewLocalBounds.Max) ||
		(ForceUpdateTransformTime > MaxTimeBeforeForceUpdateTransform))
	{
		CurrLocalBounds = NewLocalBounds;
		ForceUpdateTransformTime = 0.0f;
		UpdateComponentToWorld();
	}
}

void UNiagaraComponent::OnSystemComplete()
{
	//UE_LOG(LogNiagara, Log, TEXT("OnSystemComplete: %p - %s"), SystemInstance.Get(), *Asset->GetName());

	SetComponentTickEnabled(false);
	SetActiveFlag(false);

	MarkRenderDynamicDataDirty();
	//TODO: Mark the render state dirty?


	//Don't really complete if we're being culled by scalability.
	//We want to stop ticking but not be reclaimed by the pools etc.
	//We also want to skip this work if we're destroying during and update context reset.
	if (bIsCulledByScalability == false && bDuringUpdateContextReset == false)
	{		
		//UE_LOG(LogNiagara, Log, TEXT("OnSystemFinished.Broadcast(this);: { %p -  %p - %s"), this, SystemInstance.Get(), *Asset->GetName());
		OnSystemFinished.Broadcast(this);
		//UE_LOG(LogNiagara, Log, TEXT("OnSystemFinished.Broadcast(this);: } %p - %s"), SystemInstance.Get(), *Asset->GetName());

		if (PoolingMethod == ENCPoolMethod::AutoRelease)//Don't release back to the pool if we're completing due to scalability culling.
		{
			FNiagaraWorldManager::Get(GetWorld())->GetComponentPool()->ReclaimWorldParticleSystem(this);
		}
		else if (PoolingMethod == ENCPoolMethod::ManualRelease_OnComplete)
		{
			PoolingMethod = ENCPoolMethod::ManualRelease;
			FNiagaraWorldManager::Get(GetWorld())->GetComponentPool()->ReclaimWorldParticleSystem(this);
		}
		else if (bAutoDestroy)
		{
			//UE_LOG(LogNiagara, Log, TEXT("OnSystemComplete DestroyComponent();: { %p - %s"), SystemInstance.Get(), *Asset->GetName());
			DestroyComponent();
			//UE_LOG(LogNiagara, Log, TEXT("OnSystemComplete DestroyComponent();: } %p - %s"), SystemInstance.Get(), *Asset->GetName());
		}
		else if (bAutoManageAttachment)
		{
			CancelAutoAttachment(/*bDetachFromParent=*/ true);
		}

		if (IsRegisteredWithScalabilityManager())
		{
			//Can we be sure this isn't going to spam erroneously?
			if (UNiagaraEffectType* EffectType = GetAsset()->GetEffectType())
			{
				//Only trigger warning if we're not being deactivated/completed from the outside and this is a natural completion by the system itself.
				if (EffectType->CullReaction == ENiagaraCullReaction::DeactivateImmediateResume || EffectType->CullReaction == ENiagaraCullReaction::DeactivateResume)
				{
					if (GNiagaraComponentWarnAsleepCullReaction == 1)
					{
						//If we're completing naturally, i.e. we're a burst/non-looping system then we shouldn't be using a mode reactivates the effect.
						UE_LOG(LogNiagara, Warning, TEXT("Niagara Effect has completed naturally but has an effect type with the \"Asleep\" cull reaction. If an effect like this is culled before it can complete then it could leak into the scalability manager and be reactivated incorrectly. Please verify this is using the correct EffectType.\nComponent:%s\nSystem:%s")
							, *GetFullName(), *GetAsset()->GetFullName());
					}
				}
			}
			//We've completed naturally so unregister with the scalability manager.
			UnregisterWithScalabilityManager();
		}
	}
}

void UNiagaraComponent::DestroyInstance()
{
	//UE_LOG(LogNiagara, Log, TEXT("UNiagaraComponent::DestroyInstance: %p - %p  %s\n"), this, SystemInstance.Get(), *GetAsset()->GetFullName());
	//UE_LOG(LogNiagara, Log, TEXT("DestroyInstance: %p - %s"), this, *Asset->GetName());
	SetActiveFlag(false);

	// Before we can destory the instance, we need to deactivate it.
	if (SystemInstance)
	{
		SystemInstance->Deactivate(true);
	}
	UnregisterWithScalabilityManager();
	
	// Rather than setting the unique ptr to null here, we allow it to transition ownership to the system's deferred deletion queue. This allows us to safely
	// get rid of the system interface should we be doing this in response to a callback invoked during the system interface's lifetime completion cycle.
	FNiagaraSystemInstance::DeallocateSystemInstance(SystemInstance); // System Instance will be nullptr after this.
	check(SystemInstance.Get() == nullptr);


#if WITH_EDITORONLY_DATA
	OnSystemInstanceChangedDelegate.Broadcast();
#endif
	MarkRenderStateDirty();
}

void UNiagaraComponent::OnPooledReuse(UWorld* NewWorld)
{
	check(!IsPendingKill());
	SetUserParametersToDefaultValues();

	//Need to reset the component's visibility in case it's returned to the pool while marked invisible.
	SetVisibility(true);

	if (GetWorld() != NewWorld)
	{
		// Rename the NC to move it into the current PersistentLevel - it may have been spawned in one
		// level but is now needed in another level.
		// Use the REN_ForceNoResetLoaders flag to prevent the rename from potentially calling FlushAsyncLoading.
		Rename(nullptr, NewWorld, REN_ForceNoResetLoaders);
	}

	if (SystemInstance != nullptr)
	{
		SystemInstance->OnPooledReuse(*NewWorld);
	}
}

void UNiagaraComponent::OnRegister()
{
	if (IsActive() && SystemInstance.IsValid() == false)
	{
		// If we're active but don't have an active system instance clear the active flag so that the component
		// gets activated.
		SetActiveFlag(false);
	}

	if (bAutoManageAttachment && !IsActive())
	{
		// Detach from current parent, we are supposed to wait for activation.
		if (GetAttachParent())
		{
			// If no auto attach parent override, use the current parent when we activate
			if (!AutoAttachParent.IsValid())
			{
				AutoAttachParent = GetAttachParent();
			}
			// If no auto attach socket override, use current socket when we activate
			if (AutoAttachSocketName == NAME_None)
			{
				AutoAttachSocketName = GetAttachSocketName();
			}

			// Prevent attachment before Super::OnRegister() tries to attach us, since we only attach when activated.
			if (GetAttachParent()->GetAttachChildren().Contains(this))
			{
				// Only detach if we are not about to auto attach to the same target, that would be wasteful.
				if (!bAutoActivate || (AutoAttachLocationRule != EAttachmentRule::KeepRelative && AutoAttachRotationRule != EAttachmentRule::KeepRelative && AutoAttachScaleRule != EAttachmentRule::KeepRelative) || (AutoAttachSocketName != GetAttachSocketName()) || (AutoAttachParent != GetAttachParent()))
				{
					//bIsChangingAutoAttachment = true;
					DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepRelative, /*bCallModify=*/ false));
					//bIsChangingAutoAttachment = false;
				}
			}
			else
			{
				SetupAttachment(nullptr, NAME_None);
			}
		}

		SavedAutoAttachRelativeLocation = GetRelativeLocation();
		SavedAutoAttachRelativeRotation = GetRelativeRotation();
		SavedAutoAttachRelativeScale3D = GetRelativeScale3D();
	}
	Super::OnRegister();
}

bool UNiagaraComponent::IsReadyForOwnerToAutoDestroy() const
{
	return !IsActive();
}

void UNiagaraComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	//UE_LOG(LogNiagara, Log, TEXT("OnComponentDestroyed %p %p"), this, SystemInstance.Get());
	//DestroyInstance();//Can't do this here as we can call this from inside the system instance currently during completion 

	if (PoolingMethod != ENCPoolMethod::None)
	{
		if (UWorld* World = GetWorld())
		{
			UE_LOG(LogNiagara, Warning, TEXT("UNiagaraComponent::OnComponentDestroyed: Component (%p - %s) Asset (%s) is still pooled (%d) while destroying!\n"), this, *GetFullNameSafe(this), *GetFullNameSafe(Asset), PoolingMethod);
			if (FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World))
			{
				if (UNiagaraComponentPool* ComponentPool = WorldManager->GetComponentPool())
				{
					ComponentPool->PooledComponentDestroyed(this);
				}
			}
		}
		else
		{
			UE_LOG(LogNiagara, Warning, TEXT("UNiagaraComponent::OnComponentDestroyed: Component (%p - %s) Asset (%s) is still pooled (%d) while destroying and world it nullptr!\n"), this, *GetFullNameSafe(this), *GetFullNameSafe(Asset), PoolingMethod);
		}

		// Set pooling method to none as we are destroyed and can not go into the pool after this point
		PoolingMethod = ENCPoolMethod::None;
	}

	UnregisterWithScalabilityManager();

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UNiagaraComponent::OnUnregister()
{
	Super::OnUnregister();

	SetActiveFlag(false);

	UnregisterWithScalabilityManager();

	if (SystemInstance)
	{
		//UE_LOG(LogNiagara, Log, TEXT("UNiagaraComponent::OnUnregister: %p  %s\n"), SystemInstance.Get(), *GetAsset()->GetFullName());

		SystemInstance->Deactivate(true);

		if (PoolingMethod == ENCPoolMethod::None)
		{
			// Rather than setting the unique ptr to null here, we allow it to transition ownership to the system's deferred deletion queue. This allows us to safely
			// get rid of the system interface should we be doing this in response to a callback invoked during the system interface's lifetime completion cycle.
			FNiagaraSystemInstance::DeallocateSystemInstance(SystemInstance); // System Instance will be nullptr after this.
			check(SystemInstance.Get() == nullptr);
#if WITH_EDITORONLY_DATA
			OnSystemInstanceChangedDelegate.Broadcast();
#endif
		}
	}
}

void UNiagaraComponent::BeginDestroy()
{
	//UE_LOG(LogNiagara, Log, TEXT("UNiagaraComponent::BeginDestroy(): %p - %d - %s\n"), this, ScalabilityManagerHandle, *GetAsset()->GetFullName());

	if (PoolingMethod != ENCPoolMethod::None)
	{
		if (UWorld* World = GetWorld())
		{
			UE_LOG(LogNiagara, Warning, TEXT("UNiagaraComponent::BeginDestroy: Component (%p - %s) Asset (%s) is still pooled (%d) while destroying!\n"), this, *GetFullNameSafe(this), *GetFullNameSafe(Asset), PoolingMethod);
			if (FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World))
			{
				if (UNiagaraComponentPool* ComponentPool = WorldManager->GetComponentPool())
				{
					ComponentPool->PooledComponentDestroyed(this);
				}
			}
		}
		else
		{
			UE_LOG(LogNiagara, Warning, TEXT("UNiagaraComponent::BeginDestroy: Component (%p - %s) Asset (%s) is still pooled (%d) while destroying and world it nullptr!\n"), this, *GetFullNameSafe(this), *GetFullNameSafe(Asset), PoolingMethod);
		}

		// Set pooling method to none as we are destroyed and can not go into the pool after this point
		PoolingMethod = ENCPoolMethod::None;
	}

	//By now we will have already unregisted with the scalability manger. Either directly in OnComponentDestroyed, or via the post GC callbacks in the manager it's self in the case of someone calling MarkPendingKill() directly on a component.
	ScalabilityManagerHandle = INDEX_NONE;

	DestroyInstance();

	Super::BeginDestroy();
}

TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> UNiagaraComponent::GetSystemSimulation()
{
	if (SystemInstance)
	{
		return SystemInstance->GetSystemSimulation();
	}

	return nullptr;
}

void UNiagaraComponent::OnEndOfFrameUpdateDuringTick()
{
	Super::OnEndOfFrameUpdateDuringTick();
	if ( SystemInstance )
	{
		SystemInstance->WaitForAsyncTickAndFinalize();
	}
}

void UNiagaraComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);
	// The emitter instance may not tick again next frame so we send the dynamic data here so that the current state
	// renders.  This can happen when while editing, or any time the age update mode is set to desired age.
	SendRenderDynamicData_Concurrent();
}

void UNiagaraComponent::SendRenderDynamicData_Concurrent()
{
	LLM_SCOPE(ELLMTag::Niagara);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentSendRenderData);
	PARTICLE_PERF_STAT_CYCLES(Asset, EndOfFrame);

	Super::SendRenderDynamicData_Concurrent();

	if (SystemInstance.IsValid() && SceneProxy)
	{
		FNiagaraCrashReporterScope CRScope(SystemInstance.Get());

#if STATS
		TStatId SystemStatID = GetAsset() ? GetAsset()->GetStatID(true, true) : TStatId();
		FScopeCycleCounter SystemStatCounter(SystemStatID);
#endif

		FNiagaraScopedRuntimeCycleCounter RuntimeScope(GetAsset(), true, false);

		FNiagaraSceneProxy* NiagaraProxy = static_cast<FNiagaraSceneProxy*>(SceneProxy);
		const TArray<FNiagaraRenderer*>& EmitterRenderers = NiagaraProxy->GetEmitterRenderers();
		const int32 NumEmitterRenderers = EmitterRenderers.Num();

		if (NumEmitterRenderers == 0)
		{
			// Early out if we have no renderers
			return;
		}

		typedef TArray<FNiagaraDynamicDataBase*, TInlineAllocator<8>> TDynamicDataArray;
		TDynamicDataArray NewDynamicData;
		NewDynamicData.Reserve(NumEmitterRenderers);

		int32 RendererIndex = 0;
		for (int32 i = 0; i < SystemInstance->GetEmitters().Num(); i++)
		{
			FNiagaraEmitterInstance* EmitterInst = &SystemInstance->GetEmitters()[i].Get();
			UNiagaraEmitter* Emitter = EmitterInst->GetCachedEmitter();

			if(Emitter == nullptr)
			{
				continue;
			}

#if STATS
			TStatId EmitterStatID = Emitter->GetStatID(true, true);
			FScopeCycleCounter EmitterStatCounter(EmitterStatID);
#endif

			Emitter->ForEachEnabledRenderer(
				[&](UNiagaraRendererProperties* Properties)
				{
					FNiagaraRenderer* Renderer = EmitterRenderers[RendererIndex++];
					FNiagaraDynamicDataBase* NewData = nullptr;
				
					if (Renderer && Properties->GetIsActive())
					{
						bool bRendererEditorEnabled = true;
#if WITH_EDITORONLY_DATA
						const FNiagaraEmitterHandle& Handle = Asset->GetEmitterHandle(i);
						bRendererEditorEnabled = (!SystemInstance->GetIsolateEnabled() || Handle.IsIsolated());
#endif
						if (bRendererEditorEnabled && !EmitterInst->IsComplete() && !SystemInstance->IsComplete())
						{
							NewData = Renderer->GenerateDynamicData(NiagaraProxy, Properties, EmitterInst);
						}
					}

					NewDynamicData.Add(NewData);
				}
			);
		}

#if WITH_EDITOR
		if (NumEmitterRenderers != NewDynamicData.Num())
		{
			// This can happen in the editor when modifying the number or renderers while the system is running and the render thread is already processing the data.
			// in this case we just skip drawing this frame since the system will be reinitialized.
			return;
		}
#endif
		
		ENQUEUE_RENDER_COMMAND(NiagaraSetDynamicData)(
			[NiagaraProxy, DynamicData = MoveTemp(NewDynamicData), PerfAsset=Asset](FRHICommandListImmediate& RHICmdList)
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraSetDynamicData);
			PARTICLE_PERF_STAT_CYCLES(PerfAsset, RenderUpdate);

			const TArray<FNiagaraRenderer*>& EmitterRenderers_RT = NiagaraProxy->GetEmitterRenderers();
			for (int32 i = 0; i < EmitterRenderers_RT.Num(); ++i)
			{
				if (FNiagaraRenderer* Renderer = EmitterRenderers_RT[i])
				{
					Renderer->SetDynamicData_RenderThread(DynamicData[i]);
				}
			}
		});
	}
}

int32 UNiagaraComponent::GetNumMaterials() const
{
	TArray<UMaterialInterface*> UsedMaterials;
	if (SystemInstance)
	{
		for (int32 i = 0; i < SystemInstance->GetEmitters().Num(); i++)
		{
			FNiagaraEmitterInstance* EmitterInst = &SystemInstance->GetEmitters()[i].Get();
			if ( UNiagaraEmitter* Emitter = EmitterInst->GetCachedEmitter() )
			{
				Emitter->ForEachEnabledRenderer(
					[&](UNiagaraRendererProperties* Properties)
					{
						Properties->GetUsedMaterials(EmitterInst, UsedMaterials);
					}
				);
			}
		}
	}

	return UsedMaterials.Num();
}


FBoxSphereBounds UNiagaraComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	const USceneComponent* UseAutoParent = (bAutoManageAttachment && GetAttachParent() == nullptr) ? AutoAttachParent.Get() : nullptr;
	if (UseAutoParent)
	{
		// We use auto attachment but have detached, don't use our own bogus bounds (we're off near 0,0,0), use the usual parent's bounds.
		return UseAutoParent->Bounds;
	}

	FBoxSphereBounds SystemBounds;
	if (CurrLocalBounds.IsValid)
	{
		SystemBounds = CurrLocalBounds;
		SystemBounds.BoxExtent *= BoundsScale;
		SystemBounds.SphereRadius *= BoundsScale;
	}
	else
	{
		FBox SimBounds(ForceInit);
		SystemBounds = FBoxSphereBounds(SimBounds);
	}

	return SystemBounds.TransformBy(LocalToWorld);
}

void UNiagaraComponent::UpdateEmitterMaterials()
{
	TArray<FNiagaraMaterialOverride> NewEmitterMaterials;
	
	if (SystemInstance)
	{
		for (int32 i = 0; i < SystemInstance->GetEmitters().Num(); i++)
		{
			FNiagaraEmitterInstance* EmitterInst = &SystemInstance->GetEmitters()[i].Get();
			if (UNiagaraEmitter* Emitter = EmitterInst->GetCachedEmitter())
			{

				Emitter->ForEachEnabledRenderer(
					[&](UNiagaraRendererProperties* Properties)
					{
						TArray<UMaterialInterface*> UsedMaterials;
						Properties->GetUsedMaterials(EmitterInst, UsedMaterials);
						bool bCreateMidsForUsedMaterials = Properties->NeedsMIDsForMaterials();

						uint32 Index = 0;
						for (UMaterialInterface*& Mat : UsedMaterials)
						{
							if (Mat && bCreateMidsForUsedMaterials && !Mat->IsA<UMaterialInstanceDynamic>())
							{
								bool bFoundMatch = false;
								for (int32 i = 0; i < EmitterMaterials.Num(); i++)
								{
									if (EmitterMaterials[i].EmitterRendererProperty == Properties && EmitterMaterials[i].Material )
									{
										UMaterialInstanceDynamic* MatDyn = Cast< UMaterialInstanceDynamic>(EmitterMaterials[i].Material);
										if (MatDyn && MatDyn->Parent == Mat)
										{
											bFoundMatch = true;
											Mat = MatDyn;
											NewEmitterMaterials.Add(EmitterMaterials[i]);
											break;
										}
									}
								}

								if (!bFoundMatch)
								{
									UE_LOG(LogNiagara, Log, TEXT("Create Dynamic Material for component %s"), *GetPathName());
									Mat = UMaterialInstanceDynamic::Create(Mat, this);
									FNiagaraMaterialOverride Override;
									Override.Material = Mat;
									Override.EmitterRendererProperty = Properties;
									Override.MaterialSubIndex = Index;

									NewEmitterMaterials.Add(Override);
								}
							}
							Index++;
						}
					}
				);				
			}
		}
	}

	EmitterMaterials = NewEmitterMaterials;
}

FPrimitiveSceneProxy* UNiagaraComponent::CreateSceneProxy()
{
	LLM_SCOPE(ELLMTag::Niagara);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraCreateSceneProxy);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);

	UpdateEmitterMaterials();
	
	// The constructor will set up the System renderers from the component.
	FNiagaraSceneProxy* Proxy = new FNiagaraSceneProxy(this);

	return Proxy;
}

void UNiagaraComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (!SystemInstance.IsValid())
	{
		return;
	}

	for (int32 EmitterIdx = 0; EmitterIdx < SystemInstance->GetEmitters().Num(); ++EmitterIdx)
	{
		TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Sim = SystemInstance->GetEmitters()[EmitterIdx];

		if (UNiagaraEmitter* Emitter = Sim->GetEmitterHandle().GetInstance())
		{
			Emitter->ForEachEnabledRenderer(
				[&](UNiagaraRendererProperties* Properties)
				{

					bool bCreateMidsForUsedMaterials = Properties->NeedsMIDsForMaterials();
					TArray<UMaterialInterface*> Mats;
					Properties->GetUsedMaterials(&Sim.Get(), Mats);

					if (bCreateMidsForUsedMaterials)
					{
						for (const FNiagaraMaterialOverride& Override : EmitterMaterials)
						{
							if (Override.EmitterRendererProperty == Properties)
							{
								for (int32 i = 0; i < Mats.Num(); i++)
								{
									if (i == Override.MaterialSubIndex)
									{
										Mats[i] = Override.Material;
										continue;
									}
								}
							}
						}
					}

					OutMaterials.Append(Mats);
				}
			);
		}
	}

}

void UNiagaraComponent::SetComponentTickEnabled(bool bEnabled)
{
	Super::SetComponentTickEnabled(bEnabled);
	if (SystemInstance.IsValid())
	{
		SystemInstance->UpdatePrereqs();
	}
}

void UNiagaraComponent::OnAttachmentChanged()
{
	// Uncertain about this. 
	// 	if (bIsActive && !bIsChangingAutoAttachment && !GetOwner()->IsPendingKillPending())
	// 	{
	// 		ResetSystem();
	// 	}

	Super::OnAttachmentChanged();
	if ( SystemInstance.IsValid() )
	{
		SystemInstance->UpdatePrereqs();
	}
}

void UNiagaraComponent::OnChildAttached(USceneComponent* ChildComponent)
{
	Super::OnChildAttached(ChildComponent);
	if (SystemInstance.IsValid())
	{
		SystemInstance->UpdatePrereqs();
	}
}

void UNiagaraComponent::OnChildDetached(USceneComponent* ChildComponent)
{
	Super::OnChildDetached(ChildComponent);
	if (SystemInstance.IsValid())
	{
		SystemInstance->UpdatePrereqs();
	}
}

FNiagaraSystemInstance* UNiagaraComponent::GetSystemInstance() const
{
	return SystemInstance.Get();
}

void UNiagaraComponent::SetTickBehavior(ENiagaraTickBehavior NewTickBehavior)
{
	TickBehavior = NewTickBehavior;
	if (SystemInstance.IsValid())
	{
		SystemInstance->SetTickBehavior(TickBehavior);
	}
}

void UNiagaraComponent::SetVariableLinearColor(FName InVariableName, const FLinearColor& InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetColorDef(), InVariableName);
	OverrideParameters.SetParameterValue(InValue, VariableDesc, true);
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&InValue, sizeof(FLinearColor)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableLinearColor(const FString& InVariableName, const FLinearColor& InValue)
{
	SetVariableLinearColor(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableQuat(FName InVariableName, const FQuat& InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetQuatDef(), InVariableName);
	OverrideParameters.SetParameterValue(InValue, VariableDesc, true);
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&InValue, sizeof(FQuat)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableQuat(const FString& InVariableName, const FQuat& InValue)
{
	SetVariableQuat(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableVec4(FName InVariableName, const FVector4& InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetVec4Def(), InVariableName);
	OverrideParameters.SetParameterValue(InValue, VariableDesc, true);
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&InValue, sizeof(FVector4)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableVec4(const FString& InVariableName, const FVector4& InValue)
{
	SetVariableVec4(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableVec3(FName InVariableName, FVector InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetVec3Def(), InVariableName);
	OverrideParameters.SetParameterValue(InValue, VariableDesc, true);
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&InValue, sizeof(FVector)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableVec3(const FString& InVariableName, FVector InValue)
{
	SetVariableVec3(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableVec2(FName InVariableName, FVector2D InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetVec2Def(), InVariableName);
	OverrideParameters.SetParameterValue(InValue, VariableDesc, true);
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&InValue, sizeof(FVector2D)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableVec2(const FString& InVariableName, FVector2D InValue)
{
	SetVariableVec2(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableFloat(FName InVariableName, float InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetFloatDef(), InVariableName);
	OverrideParameters.SetParameterValue(InValue, VariableDesc, true);
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&InValue, sizeof(float)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableFloat(const FString& InVariableName, float InValue)
{
	SetVariableFloat(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableInt(FName InVariableName, int32 InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetIntDef(), InVariableName);
	OverrideParameters.SetParameterValue(InValue, VariableDesc, true);
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&InValue, sizeof(int32)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableInt(const FString& InVariableName, int32 InValue)
{
	SetVariableInt(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableBool(FName InVariableName, bool InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetBoolDef(), InVariableName);
	const FNiagaraBool BoolValue(InValue);
	OverrideParameters.SetParameterValue(BoolValue, VariableDesc, true);
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&BoolValue, sizeof(FNiagaraBool)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableBool(const FString& InVariableName, bool InValue)
{
	SetVariableBool(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableActor(FName InVariableName, AActor* InValue)
{
	SetVariableObject(InVariableName, InValue);
}

void UNiagaraComponent::SetNiagaraVariableActor(const FString& InVariableName, AActor* InValue)
{
	SetNiagaraVariableObject(InVariableName, InValue);
}

void UNiagaraComponent::SetVariableObject(FName InVariableName, UObject* InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetUObjectDef(), InVariableName);
	OverrideParameters.SetUObject(InValue, VariableDesc);
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(InValue));
#endif
}

void UNiagaraComponent::SetNiagaraVariableObject(const FString& InVariableName, UObject* InValue)
{
	SetVariableObject(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableMaterial(FName InVariableName, UMaterialInterface* InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetUMaterialDef(), InVariableName);
	OverrideParameters.SetUObject(InValue, VariableDesc);
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(InValue));
#endif
	MarkRenderStateDirty(); // Materials might be using this on the system, so invalidate the render state to re-gather them.
}

TArray<FVector> UNiagaraComponent::GetNiagaraParticlePositions_DebugOnly(const FString& InEmitterName)
{
	return GetNiagaraParticleValueVec3_DebugOnly(InEmitterName, TEXT("Position"));
}

TArray<FVector> UNiagaraComponent::GetNiagaraParticleValueVec3_DebugOnly(const FString& InEmitterName, const FString& InValueName)
{
	TArray<FVector> Positions;
	FName EmitterName = FName(*InEmitterName);
	if (SystemInstance.IsValid())
	{
		for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& Sim : SystemInstance->GetEmitters())
		{
			if (Sim->GetEmitterHandle().GetName() == EmitterName)
			{
				FNiagaraDataBuffer& ParticleData = Sim->GetData().GetCurrentDataChecked();
				int32 NumParticles = ParticleData.GetNumInstances();
				Positions.SetNum(NumParticles);

				const auto PosData = FNiagaraDataSetAccessor<FVector>::CreateReader(Sim->GetData(), *InValueName);
				if (!PosData.IsValid())
				{
					UE_LOG(LogNiagara, Warning, TEXT("Unable to find variable %s on %s per-particle data. Returning zeroes."), *InValueName, *GetPathName());
				}

				for (int32 i = 0; i < NumParticles; ++i)
				{
					Positions[i] = PosData.GetSafe(i, FVector::ZeroVector);
				}
			}
		}
	}
	return Positions;

}

TArray<float> UNiagaraComponent::GetNiagaraParticleValues_DebugOnly(const FString& InEmitterName, const FString& InValueName)
{
	TArray<float> Values;
	FName EmitterName = FName(*InEmitterName);
	if (SystemInstance.IsValid())
	{
		for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& Sim : SystemInstance->GetEmitters())
		{
			if (Sim->GetEmitterHandle().GetName() == EmitterName)
			{
				FNiagaraDataBuffer& ParticleData = Sim->GetData().GetCurrentDataChecked();
				int32 NumParticles = ParticleData.GetNumInstances();
				Values.SetNum(NumParticles);

				const auto ValueData = FNiagaraDataSetAccessor<float>::CreateReader(Sim->GetData(), *InValueName);
				for (int32 i=0; i < NumParticles; ++i)
				{
					Values[i] = ValueData.GetSafe(i, 0.0f);
				}
			}
		}
	}
	return Values;
}

void FixInvalidUserParameters(FNiagaraUserRedirectionParameterStore& ParameterStore)
{
	static const FString UserPrefix = FNiagaraConstants::UserNamespace.ToString() + TEXT(".");

	TArray<FNiagaraVariable> Parameters;
	ParameterStore.GetParameters(Parameters);
	TArray<FNiagaraVariable> IncorrectlyNamedParameters;
	for (FNiagaraVariable& Parameter : Parameters)
	{
		if (Parameter.GetName().ToString().StartsWith(UserPrefix) == false)
		{
			IncorrectlyNamedParameters.Add(Parameter);
		}
	}

	bool bParameterRenamed = false;
	for(FNiagaraVariable& IncorrectlyNamedParameter : IncorrectlyNamedParameters)
	{
		FString FixedNameString = UserPrefix + IncorrectlyNamedParameter.GetName().ToString();
		FName FixedName = *FixedNameString;
		FNiagaraVariable FixedParameter(IncorrectlyNamedParameter.GetType(), FixedName);
		if (Parameters.Contains(FixedParameter))
		{
			// If the correctly named parameter is also in the collection then both parameters need to be removed and the 
			// correct one re-added.  First we need to cache the value of the parameter so that it's not lost on removal.
			UNiagaraDataInterface* DataInterfaceValue = nullptr;
			UObject* ObjectValue = nullptr;
			TArray<uint8> DataValue;
			int32 ValueIndex = ParameterStore.IndexOf(IncorrectlyNamedParameter);
			if (IncorrectlyNamedParameter.IsDataInterface())
			{
				DataInterfaceValue = ParameterStore.GetDataInterface(IncorrectlyNamedParameter);
			}
			else if (IncorrectlyNamedParameter.IsUObject())
			{
				ObjectValue = ParameterStore.GetUObject(IncorrectlyNamedParameter);
			}
			else
			{
				const uint8* DataValuePtr = ParameterStore.GetParameterData(IncorrectlyNamedParameter);
				if (DataValuePtr != nullptr)
				{
					DataValue.AddUninitialized(IncorrectlyNamedParameter.GetSizeInBytes());
					FMemory::Memcpy(DataValue.GetData(), DataValuePtr, IncorrectlyNamedParameter.GetSizeInBytes());
				}
			}

			// Next we remove the parameter twice because the first removal of the incorrect parameter will actually remove
			// the correct version due to the user redirection table.
			ParameterStore.RemoveParameter(IncorrectlyNamedParameter);
			ParameterStore.RemoveParameter(IncorrectlyNamedParameter);

			// Last we add back the fixed parameter and set the value.
			ParameterStore.AddParameter(FixedParameter, false);
			if (DataInterfaceValue != nullptr)
			{
				ParameterStore.SetDataInterface(DataInterfaceValue, FixedParameter);
			}
			else if (ObjectValue != nullptr)
			{
				ParameterStore.SetUObject(ObjectValue, FixedParameter);
			}
			else
			{
				if (DataValue.Num() == FixedParameter.GetSizeInBytes())
				{
					ParameterStore.SetParameterData(DataValue.GetData(), FixedParameter);
				}
			}
		}
		else 
		{
			// If the correctly named parameter was not in the collection already we can just rename the incorrect parameter
			// to the correct one.
			ParameterStore.RenameParameter(IncorrectlyNamedParameter, FixedName);
			bParameterRenamed = true;
		}
	}

	if (bParameterRenamed)
	{
		ParameterStore.RecreateRedirections();
	}
}

void UNiagaraComponent::PostLoad()
{
	Super::PostLoad();

	OverrideParameters.PostLoad();

#if WITH_EDITOR
	if (Asset != nullptr)
	{
		Asset->ConditionalPostLoad();

		FixInvalidUserParameters(OverrideParameters);
		
		UpgradeDeprecatedParameterOverrides();

#if WITH_EDITORONLY_DATA
		const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);
		if (NiagaraVer < FNiagaraCustomVersion::ComponentsOnlyHaveUserVariables)
		{
			{
				TArray<FNiagaraVariableBase> ToRemoveNonUser;
				TArray<FNiagaraVariableBase> ToAddNonUser;

				auto InstanceKeyIter = InstanceParameterOverrides.CreateConstIterator();
				while (InstanceKeyIter)
				{
					FName KeyName = InstanceKeyIter.Key().GetName();
					FNiagaraVariableBase VarBase = InstanceKeyIter.Key();
					FNiagaraUserRedirectionParameterStore::MakeUserVariable(VarBase);
					FName UserKeyName = VarBase.GetName();
					if (KeyName != UserKeyName)
					{
						UE_LOG(LogNiagara, Log, TEXT("InstanceParameterOverrides for %s has non-user keys in it! %s. Updating in PostLoad to User key."), *GetPathName(), *KeyName.ToString());
						const FNiagaraVariant* FoundVar = InstanceParameterOverrides.Find(VarBase);
						if (FoundVar != nullptr)
						{
							UE_LOG(LogNiagara, Warning, TEXT("InstanceParameterOverrides for %s has values for both keys in it! %s and %s. PostLoad keeping User version."), *GetPathName(), *KeyName.ToString(), *UserKeyName.ToString());
						}
						else
						{
							ToAddNonUser.Add(InstanceKeyIter.Key());
						}
						ToRemoveNonUser.Add(InstanceKeyIter.Key());
					}
					++InstanceKeyIter;
				}

				for (const FNiagaraVariableBase& Var : ToAddNonUser)
				{
					const FNiagaraVariant FoundVar = InstanceParameterOverrides.FindRef(Var);
					FNiagaraVariableBase UserVar = Var;
					FNiagaraUserRedirectionParameterStore::MakeUserVariable(UserVar);
					InstanceParameterOverrides.Emplace(UserVar, FoundVar);
				}

				for (const FNiagaraVariableBase& Var : ToRemoveNonUser)
				{
					InstanceParameterOverrides.Remove(Var);
				}
			}

			{
				TArray<FNiagaraVariableBase> ToRemoveNonUser;
				TArray<FNiagaraVariableBase> ToAddNonUser;

				auto TemplateKeyIter = TemplateParameterOverrides.CreateConstIterator();
				while (TemplateKeyIter)
				{
					FName KeyName = TemplateKeyIter.Key().GetName();
					FNiagaraVariableBase VarBase = TemplateKeyIter.Key();
					FNiagaraUserRedirectionParameterStore::MakeUserVariable(VarBase);
					FName UserKeyName = VarBase.GetName();
					if (KeyName != UserKeyName)
					{
						UE_LOG(LogNiagara, Log, TEXT("TemplateParameterOverrides for %s has non-user keys in it! %s. Updating in PostLoad to User key."), *GetPathName(), *KeyName.ToString());
						const FNiagaraVariant* FoundVar = TemplateParameterOverrides.Find(VarBase);
						if (FoundVar != nullptr)
						{
							UE_LOG(LogNiagara, Warning, TEXT("TemplateParameterOverrides for %s has values for both keys in it! %s and %s.  PostLoad keeping User version."), *GetPathName(), *KeyName.ToString(), *UserKeyName.ToString());
						}
						else
						{
							ToAddNonUser.Add(TemplateKeyIter.Key());
						}
						ToRemoveNonUser.Add(TemplateKeyIter.Key());
					}
					++TemplateKeyIter;
				}

				for (const FNiagaraVariableBase& Var : ToAddNonUser)
				{
					const FNiagaraVariant* FoundVar = TemplateParameterOverrides.Find(Var);
					FNiagaraVariableBase UserVar = Var;
					FNiagaraUserRedirectionParameterStore::MakeUserVariable(UserVar);
					TemplateParameterOverrides.Add(UserVar) = *FoundVar;
				}

				for (const FNiagaraVariableBase& Var : ToRemoveNonUser)
				{
					TemplateParameterOverrides.Remove(Var);
				}
			}
		}
#endif
		SynchronizeWithSourceSystem();

		AssetExposedParametersChangedHandle = Asset->GetExposedParameters().AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraComponent::AssetExposedParametersChanged));
	}
#endif
}

static FNiagaraVariant GetParameterValueFromStore(const FNiagaraVariableBase& Var, const FNiagaraParameterStore& Store)
{
	if (Var.IsDataInterface())
	{
		const int32 Index = Store.IndexOf(Var);
		if (Index != INDEX_NONE)
		{
			return FNiagaraVariant(Store.GetDataInterfaces()[Index]);
		}
	}
	else if (Var.IsUObject())
	{
		const int32 Index = Store.IndexOf(Var);
		if (Index != INDEX_NONE)
		{
			return FNiagaraVariant(Store.GetUObjects()[Index]);
		}
	}

	const uint8* ParameterData = Store.GetParameterData(Var);
	if (ParameterData == nullptr)
	{
		return FNiagaraVariant();
	}

	return FNiagaraVariant(ParameterData, Var.GetSizeInBytes());
}

#if WITH_EDITOR

void UNiagaraComponent::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange != nullptr && 
		PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraComponent, Asset) && 
		Asset != nullptr)
	{
		Asset->GetExposedParameters().RemoveOnChangedHandler(AssetExposedParametersChangedHandle);
		DestroyInstance();
	}
}

void UNiagaraComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FName PropertyName;
	if (PropertyChangedEvent.Property)
	{
		PropertyName = PropertyChangedEvent.Property->GetFName();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraComponent, Asset))
	{
		SynchronizeWithSourceSystem();
		if (Asset != nullptr)
		{
			AssetExposedParametersChangedHandle = Asset->GetExposedParameters().AddOnChangedHandler(
				FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraComponent::AssetExposedParametersChanged));
		}
	}
	else if(PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraComponent, OverrideParameters))
	{
		SynchronizeWithSourceSystem();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraComponent, TemplateParameterOverrides) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraComponent, InstanceParameterOverrides))
	{
		ApplyOverridesToParameterStore();
	}

	ReinitializeSystem();

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

void UNiagaraComponent::SetUserParametersToDefaultValues()
{
#if WITH_EDITORONLY_DATA
	EditorOverridesValue_DEPRECATED.Empty();
	TemplateParameterOverrides.Empty();
	InstanceParameterOverrides.Empty();
#endif
	
	if (Asset == nullptr)
	{
		OverrideParameters.Empty(false);
		return;
	}

	if (GNiagaraUseFastSetUserParametersToDefaultValues)
	{
		const FNiagaraUserRedirectionParameterStore& SourceUserParameterStore = Asset->GetExposedParameters();
		TArrayView<const FNiagaraVariableWithOffset> DestParameters = OverrideParameters.ReadParameterVariables();

		TArray<FNiagaraVariableBase, TInlineAllocator<8>> ParametersToRemove;
		bool bInterfacesChanged = false;

		for (int32 i=0; i < DestParameters.Num(); ++i)
		{
			const FNiagaraVariableWithOffset& DestParameter = DestParameters[i];
			const int32 DestIndex = DestParameter.Offset;
			const int32 SourceIndex = SourceUserParameterStore.IndexOf(DestParameter);
			if (SourceIndex != INDEX_NONE)
			{
				if (DestParameter.IsDataInterface())
				{
					check(OverrideParameters.GetDataInterface(DestIndex) != nullptr);
					SourceUserParameterStore.GetDataInterface(SourceIndex)->CopyTo(OverrideParameters.GetDataInterface(DestIndex));
					bInterfacesChanged = true;
				}
				else if (DestParameter.IsUObject())
				{
					OverrideParameters.SetUObject(SourceUserParameterStore.GetUObject(SourceIndex), DestIndex);
				}
				else
				{
					OverrideParameters.SetParameterData(SourceUserParameterStore.GetParameterData(SourceIndex), DestIndex, DestParameter.GetSizeInBytes());
				}
			}
			else
			{
				ParametersToRemove.Add(DestParameter);
			}
		}

		for (const FNiagaraVariableBase& ParameterToRemove : ParametersToRemove)
		{
			OverrideParameters.RemoveParameter(ParameterToRemove);
		}

		if (bInterfacesChanged)
		{
			OverrideParameters.OnInterfaceChange();
		}
	}
	else
	{
		OverrideParameters.Empty(false);
		CopyParametersFromAsset();
	}

	OverrideParameters.Rebind();
}

#if WITH_EDITOR

void UNiagaraComponent::UpgradeDeprecatedParameterOverrides()
{
	OverrideParameters.SanityCheckData();
	PostLoadNormalizeOverrideNames();
	
	TArray<FNiagaraVariable> UserParameters;
	OverrideParameters.GetUserParameters(UserParameters);

	for (const TPair<FName, bool>& Pair : EditorOverridesValue_DEPRECATED)
	{
		FNiagaraVariable* Found = UserParameters.FindByPredicate([Pair](const FNiagaraVariable& Var)
		{
			return Var.GetName() == Pair.Key;
		});

		if (Found != nullptr)
		{
			FNiagaraVariant StoredValue = GetParameterValueFromStore(*Found, OverrideParameters);

			if (StoredValue.IsValid())
			{
				SetParameterOverride(*Found, StoredValue);
			}
		}
	}

	EditorOverridesValue_DEPRECATED.Empty();
}

void UNiagaraComponent::EnsureOverrideParametersConsistent() const
{
	if (Asset == nullptr)
	{
		return;
	}
	
	TArray<FNiagaraVariable> UserParameters;
	Asset->GetExposedParameters().GetUserParameters(UserParameters);
	
	for (const FNiagaraVariable& Key : UserParameters)
	{
		FNiagaraVariant OverrideValue = FindParameterOverride(Key);
		if (OverrideValue.IsValid())
		{
			if (Key.IsDataInterface())
			{
				const UNiagaraDataInterface* ActualDI = OverrideParameters.GetDataInterface(Key);
				if (ActualDI != nullptr)
				{
					ensureAlways(OverrideValue.GetDataInterface()->Equals(ActualDI));
				}
			}
			else if (Key.IsUObject())
			{
				const UObject* ActualObj = OverrideParameters.GetUObject(Key);
				if (ActualObj != nullptr)
				{
					ensureAlways(OverrideValue.GetUObject() == ActualObj);
				}
			}
			else
			{
				const uint8* ActualData = OverrideParameters.GetParameterData(Key);
				if (ActualData != nullptr)
				{
					ensureAlways(FMemory::Memcmp(ActualData, OverrideValue.GetBytes(), Key.GetSizeInBytes()) == 0);
				}
			}
		}
	}
}

void UNiagaraComponent::ApplyOverridesToParameterStore()
{
	if (!IsTemplate())
	{
		if (UNiagaraComponent* Archetype = Cast<UNiagaraComponent>(GetArchetype()))
		{
			TemplateParameterOverrides = Archetype->TemplateParameterOverrides;
		}
	}

	for (const auto& Pair : TemplateParameterOverrides)
	{ 
		if (!FNiagaraUserRedirectionParameterStore::IsUserParameter(Pair.Key))
		{
			continue;
		}

		const int32* ExistingParam = OverrideParameters.FindParameterOffset(Pair.Key);
		if (ExistingParam != nullptr)
		{
			SetOverrideParameterStoreValue(Pair.Key, Pair.Value);
		}
	}

	if (!IsTemplate())
	{
		for (const auto& Pair : InstanceParameterOverrides)
		{ 
			if (!FNiagaraUserRedirectionParameterStore::IsUserParameter(Pair.Key))
			{
				continue;
			}

			const int32* ExistingParam = OverrideParameters.FindParameterOffset(Pair.Key);
			if (ExistingParam != nullptr)
			{
				SetOverrideParameterStoreValue(Pair.Key, Pair.Value);
			}
		}
	}

	EnsureOverrideParametersConsistent();
}

#endif

void UNiagaraComponent::CopyParametersFromAsset()
{
	TArray<FNiagaraVariable> SourceVars;
	Asset->GetExposedParameters().GetParameters(SourceVars);
	for (FNiagaraVariable& Param : SourceVars)
	{
		OverrideParameters.AddParameter(Param, true);
	}

	TArray<FNiagaraVariable> ExistingVars;
	OverrideParameters.GetParameters(ExistingVars);

	for (FNiagaraVariable ExistingVar : ExistingVars)
	{
		if (SourceVars.Contains(ExistingVar))
		{
			Asset->GetExposedParameters().CopyParameterData(OverrideParameters, ExistingVar);
		}
		else
		{
			OverrideParameters.RemoveParameter(ExistingVar);
		}
	}
}

void UNiagaraComponent::SynchronizeWithSourceSystem()
{
	// Synchronizing parameters will create new data interface objects and if the old data
	// interface objects are currently being used by a simulation they may be destroyed due to garbage
	// collection, so preemptively kill the instance here.
	DestroyInstance();

	//TODO: Look through params in system in "Owner" namespace and add to our parameters.
	if (Asset == nullptr)
	{
#if WITH_EDITORONLY_DATA
		OverrideParameters.Empty();
		EditorOverridesValue_DEPRECATED.Empty();
		
		OnSynchronizedWithAssetParametersDelegate.Broadcast();
#endif
		return;
	}

#if WITH_EDITOR
	CopyParametersFromAsset();
	ApplyOverridesToParameterStore();
#endif

	OverrideParameters.Rebind();

#if WITH_EDITORONLY_DATA
	OnSynchronizedWithAssetParametersDelegate.Broadcast();
#endif
}

void UNiagaraComponent::AssetExposedParametersChanged()
{
	SynchronizeWithSourceSystem();
	ReinitializeSystem();
}

#if WITH_EDITOR

bool UNiagaraComponent::HasParameterOverride(const FNiagaraVariableBase& InKey) const 
{
	FNiagaraVariableBase UserVariable = InKey;

	if (Asset)
	{
		if (!Asset->GetExposedParameters().RedirectUserVariable(UserVariable))
		{
			return false;
		}
	}
	else if (!FNiagaraUserRedirectionParameterStore::IsUserParameter(UserVariable))
	{
		return false;
	}

	if (IsTemplate())
	{
		const FNiagaraVariant* ThisValue = TemplateParameterOverrides.Find(UserVariable);

		const FNiagaraVariant* ArchetypeValue = nullptr;
		if (const UNiagaraComponent* Archetype = Cast<UNiagaraComponent>(GetArchetype()))
		{
			ArchetypeValue = Archetype->TemplateParameterOverrides.Find(UserVariable);
		}

		if (ThisValue != nullptr && ArchetypeValue != nullptr)
		{
			// exists in both, check values
			return *ThisValue != *ArchetypeValue;
		}
		else if (ThisValue != nullptr || ArchetypeValue != nullptr)
		{
			// either added or removed in this
			return true;
		}
	}
	else
	{
		if (InstanceParameterOverrides.Contains(UserVariable))
		{
			return true;
		}
	}

	return false;
}

FNiagaraVariant UNiagaraComponent::FindParameterOverride(const FNiagaraVariableBase& InKey) const
{
	if (Asset == nullptr)
	{
		return FNiagaraVariant();
	}

	FNiagaraVariableBase UserVariable = InKey;

	const FNiagaraUserRedirectionParameterStore& ParameterStore = Asset->GetExposedParameters();

	if (!ParameterStore.RedirectUserVariable(UserVariable))
	{
		return FNiagaraVariant();
	}

	if (ParameterStore.FindParameterOffset(UserVariable) == nullptr)
	{
		return FNiagaraVariant();
	}

	if (!IsTemplate())
	{
		const FNiagaraVariant* Value = InstanceParameterOverrides.Find(UserVariable);
		if (Value != nullptr)
		{
			return *Value;
		}
	}

	{
		const FNiagaraVariant* Value = TemplateParameterOverrides.Find(UserVariable);
		if (Value != nullptr)
		{
			return *Value;
		}
	}

	return FNiagaraVariant();
}

void UNiagaraComponent::SetOverrideParameterStoreValue(const FNiagaraVariableBase& InKey, const FNiagaraVariant& InValue)
{
	if (InKey.IsDataInterface())
	{
		UNiagaraDataInterface* DuplicatedDI = DuplicateObject(InValue.GetDataInterface(), this);
		OverrideParameters.SetDataInterface(DuplicatedDI, InKey);
	}
	else if (InKey.IsUObject())
	{
		OverrideParameters.SetUObject(InValue.GetUObject(), InKey);
	}
	else
	{
		OverrideParameters.SetParameterData(InValue.GetBytes(), InKey, true);
	}
}

void UNiagaraComponent::SetParameterOverride(const FNiagaraVariableBase& InKey, const FNiagaraVariant& InValue)
{
	if (!ensure(InValue.IsValid()))
	{
		return;
	}

	// we want to be sure we're storing data based on the fully qualified key name (i.e. taking the user redirection into account)
	FNiagaraVariableBase UserVariable = InKey;
	if (!OverrideParameters.RedirectUserVariable(UserVariable))
	{
		return;
	}

	if (IsTemplate())
	{
		TemplateParameterOverrides.Add(UserVariable, InValue);
	}
	else
	{
		InstanceParameterOverrides.Add(UserVariable, InValue);
	}

	SetOverrideParameterStoreValue(UserVariable, InValue);
}

void UNiagaraComponent::RemoveParameterOverride(const FNiagaraVariableBase& InKey)
{
	// we want to be sure we're storing data based on the fully qualified key name (i.e. taking the user redirection into account)
	FNiagaraVariableBase UserVariable = InKey;
	if (!OverrideParameters.RedirectUserVariable(UserVariable))
	{
		return;
	}

	if (!IsTemplate())
	{
		InstanceParameterOverrides.Remove(UserVariable);
	}
	else
	{
		TemplateParameterOverrides.Remove(UserVariable);

		// we are an archetype, but check if we have an archetype and inherit the value from there
		const UNiagaraComponent* Archetype = Cast<UNiagaraComponent>(GetArchetype());
		if (Archetype != nullptr)
		{
			FNiagaraVariant ArchetypeValue = Archetype->FindParameterOverride(UserVariable);
			if (ArchetypeValue.IsValid())
			{
				// defined in archetype, reset value to that
				if (UserVariable.IsDataInterface())
				{
					UNiagaraDataInterface* DataInterface = DuplicateObject(ArchetypeValue.GetDataInterface(), this);
					TemplateParameterOverrides.Add(UserVariable, FNiagaraVariant(DataInterface));
				}
				else
				{
					TemplateParameterOverrides.Add(UserVariable, ArchetypeValue);
				}
			}
		}
	}

	SynchronizeWithSourceSystem();
}

#endif

ENiagaraAgeUpdateMode UNiagaraComponent::GetAgeUpdateMode() const
{
	return AgeUpdateMode;
}

void UNiagaraComponent::SetAgeUpdateMode(ENiagaraAgeUpdateMode InAgeUpdateMode)
{
	AgeUpdateMode = InAgeUpdateMode;
}

float UNiagaraComponent::GetDesiredAge() const
{
	return DesiredAge;
}

void UNiagaraComponent::SetDesiredAge(float InDesiredAge)
{
	DesiredAge = InDesiredAge;
	bIsSeeking = false;
}

void UNiagaraComponent::SeekToDesiredAge(float InDesiredAge)
{
	DesiredAge = InDesiredAge;
	bIsSeeking = true;
}

void UNiagaraComponent::SetCanRenderWhileSeeking(bool bInCanRenderWhileSeeking)
{
	bCanRenderWhileSeeking = bInCanRenderWhileSeeking;
}

float UNiagaraComponent::GetSeekDelta() const
{
	return SeekDelta;
}

void UNiagaraComponent::SetSeekDelta(float InSeekDelta)
{
	SeekDelta = InSeekDelta;
}

float UNiagaraComponent::GetMaxSimTime() const
{
	return MaxSimTime;
}

void UNiagaraComponent::SetMaxSimTime(float InMaxTime)
{
	MaxSimTime = InMaxTime;
}

void UNiagaraComponent::SetAutoDestroy(bool bInAutoDestroy)
{
	if (ensureMsgf(!bInAutoDestroy || (PoolingMethod == ENCPoolMethod::None), TEXT("Attempting to set AutoDestroy on a pooled component!  Component(%s) Asset(%s)"), *GetFullName(), GetAsset() != nullptr ? *GetAsset()->GetPathName() : TEXT("None")))
	{
		bAutoDestroy = bInAutoDestroy;
	}
}

#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
void UNiagaraComponent::SetPreviewLODDistance(bool bInEnablePreviewLODDistance, float InPreviewLODDistance)
{
	bEnablePreviewLODDistance = bInEnablePreviewLODDistance;
	PreviewLODDistance = InPreviewLODDistance;
}
#else
void UNiagaraComponent::SetPreviewLODDistance(bool bInEnablePreviewLODDistance, float InPreviewLODDistance){}
#endif

void UNiagaraComponent::SetAllowScalability(bool bAllow)
{
	bAllowScalability = bAllow; 
	if (!bAllow)
	{
		UnregisterWithScalabilityManager();
	}
}

#if WITH_EDITOR

void UNiagaraComponent::PostLoadNormalizeOverrideNames()
{
	TMap<FName, bool> ValueMap;
	for (TPair<FName, bool> Pair : EditorOverridesValue_DEPRECATED)
	{
		FString ValueNameString = Pair.Key.ToString();
		if (ValueNameString.StartsWith(TEXT("User.")))
		{
			 ValueNameString = ValueNameString.RightChop(5);
		}

		ValueMap.Add(FName(*ValueNameString), Pair.Value);
	}
	EditorOverridesValue_DEPRECATED = ValueMap;
}

#endif // WITH_EDITOR

void UNiagaraComponent::SetAsset(UNiagaraSystem* InAsset)
{
	if (Asset == InAsset)
	{
		return;
	}

#if WITH_EDITOR
	if (Asset != nullptr)
	{
		Asset->GetExposedParameters().RemoveOnChangedHandler(AssetExposedParametersChangedHandle);
	}
#endif

	UnregisterWithScalabilityManager();

	Asset = InAsset;

#if WITH_EDITOR
	SynchronizeWithSourceSystem();
	if (Asset != nullptr)
	{
		AssetExposedParametersChangedHandle = Asset->GetExposedParameters().AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraComponent::AssetExposedParametersChanged));
	}
	else
	{
		AssetExposedParametersChangedHandle.Reset();
	}
#else
	CopyParametersFromAsset();
	OverrideParameters.Rebind();
#endif

	bool bWasActive = SystemInstance && SystemInstance->GetRequestedExecutionState() == ENiagaraExecutionState::Active;

	//Force a reinit.
	DestroyInstance();

	if (Asset && IsRegistered())
	{
		if (bAutoActivate || bWasActive)
		{
			Activate();
		}
	}
}

void UNiagaraComponent::SetForceSolo(bool bInForceSolo) 
{ 
	if (bForceSolo != bInForceSolo)
	{
		bForceSolo = bInForceSolo;
		DestroyInstance();
		SetComponentTickEnabled(bInForceSolo);
	}
}

void UNiagaraComponent::SetAutoAttachmentParameters(USceneComponent* Parent, FName SocketName, EAttachmentRule LocationRule, EAttachmentRule RotationRule, EAttachmentRule ScaleRule)
{
	AutoAttachParent = Parent;
	AutoAttachSocketName = SocketName;
	AutoAttachLocationRule = LocationRule;
	AutoAttachRotationRule = RotationRule;
	AutoAttachScaleRule = ScaleRule;
}


void UNiagaraComponent::CancelAutoAttachment(bool bDetachFromParent)
{
	if (bAutoManageAttachment)
	{
		if (bDidAutoAttach)
		{
			// Restore relative transform from before attachment. Actual transform will be updated as part of DetachFromParent().
			SetRelativeLocation_Direct(SavedAutoAttachRelativeLocation);
			SetRelativeRotation_Direct(SavedAutoAttachRelativeRotation);
			SetRelativeScale3D_Direct(SavedAutoAttachRelativeScale3D);
			bDidAutoAttach = false;
		}

		if (bDetachFromParent)
		{
			//bIsChangingAutoAttachment = true;
			DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
			//bIsChangingAutoAttachment = false;
		}
	}
}
