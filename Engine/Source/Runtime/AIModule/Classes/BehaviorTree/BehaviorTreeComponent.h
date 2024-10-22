// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EngineDefines.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "GameplayTagContainer.h"
#include "AITypes.h"
#include "BrainComponent.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "BehaviorTreeComponent.generated.h"

class FBehaviorTreeDebugger;
class UBehaviorTree;
class UBTAuxiliaryNode;
class UBTCompositeNode;
class UBTDecorator;
class UBTNode;
class UBTTask_RunBehavior;
class UBTTask_RunBehaviorDynamic;
class UBTTaskNode;
struct FScopedBehaviorTreeLock;

struct FBTNodeExecutionInfo
{
	/** index of first task allowed to be executed */
	FBTNodeIndex SearchStart;

	/** index of last task allowed to be executed */
	FBTNodeIndex SearchEnd;

	/** node to be executed */
	UBTCompositeNode* ExecuteNode;

	/** subtree index */
	uint16 ExecuteInstanceIdx;

	/** result used for resuming execution */
	TEnumAsByte<EBTNodeResult::Type> ContinueWithResult;

	/** if set, tree will try to execute next child of composite instead of forcing branch containing SearchStart */
	uint8 bTryNextChild : 1;

	/** if set, request was not instigated by finishing task/initialization but is a restart (e.g. decorator) */
	uint8 bIsRestart : 1;

	FBTNodeExecutionInfo() : ExecuteNode(NULL), bTryNextChild(false), bIsRestart(false) { }
};

struct FBTPendingExecutionInfo
{
	/** next task to execute */
	UBTTaskNode* NextTask;

	/** if set, tree ran out of nodes */
	uint32 bOutOfNodes : 1;

	/** if set, request can't be executed */
	uint32 bLocked : 1;

	FBTPendingExecutionInfo() : NextTask(NULL), bOutOfNodes(false), bLocked(false) {}
	bool IsSet() const { return (NextTask || bOutOfNodes) && !bLocked; }

	void Lock() { bLocked = true; }
	void Unlock() { bLocked = false; }
};

struct FBTPendingAuxNodesUnregisterInfo
{
	/** list of node index ranges pending aux nodes unregistration */
	TArray<FBTNodeIndexRange> Ranges;
};

struct FBTTreeStartInfo
{
	UBehaviorTree* Asset;
	EBTExecutionMode::Type ExecuteMode;
	uint8 bPendingInitialize : 1;

	FBTTreeStartInfo() : Asset(nullptr), ExecuteMode(EBTExecutionMode::Looped), bPendingInitialize(0) {}
	bool IsSet() const { return Asset != nullptr; }
	bool HasPendingInitialize() const { return bPendingInitialize && IsSet(); }
};

UCLASS(ClassGroup = AI, meta = (BlueprintSpawnableComponent))
class AIMODULE_API UBehaviorTreeComponent : public UBrainComponent
{
	GENERATED_UCLASS_BODY()

	// UActorComponent overrides
	virtual void RegisterComponentTickFunctions(bool bRegister) override;
	virtual void SetComponentTickEnabled(bool bEnabled) override;

	// Begin UBrainComponent overrides
	virtual void StartLogic() override;
	virtual void RestartLogic() override;
	virtual void StopLogic(const FString& Reason) override;
	virtual void PauseLogic(const FString& Reason) override;
	virtual EAILogicResuming::Type ResumeLogic(const FString& Reason) override;

	/** indicates instance has been initialized to work with specific BT asset */
	bool TreeHasBeenStarted() const;

public:
	/** DO NOT USE. This constructor is for internal usage only for hot-reload purposes. */
	UBehaviorTreeComponent(FVTableHelper& Helper);

	virtual bool IsRunning() const override;
	virtual bool IsPaused() const override;
	virtual void Cleanup() override;
	virtual void HandleMessage(const FAIMessage& Message) override;
	// End UBrainComponent overrides

	// Begin UActorComponent overrides
	virtual void UninitializeComponent() override;
	// End UActorComponent overrides

	/** starts execution from root */
	void StartTree(UBehaviorTree& Asset, EBTExecutionMode::Type ExecuteMode = EBTExecutionMode::Looped);

	/** stops execution */
	void StopTree(EBTStopMode::Type StopMode = EBTStopMode::Safe);

	/** restarts execution from root */
	void RestartTree();

	/** request execution change */
	void RequestExecution(UBTCompositeNode* RequestedOn, int32 InstanceIdx, 
		const UBTNode* RequestedBy, int32 RequestedByChildIndex,
		EBTNodeResult::Type ContinueWithResult, bool bStoreForDebugger = true);

	/** request execution change: helpers for decorator nodes */
	void RequestExecution(const UBTDecorator* RequestedBy);

	/** request execution change: helpers for task nodes */
	void RequestExecution(EBTNodeResult::Type ContinueWithResult);

	/** request unregistration of aux nodes in the specified branch */
	void RequestUnregisterAuxNodesInBranch(const UBTCompositeNode* Node);

	/** finish latent execution or abort */
	void OnTaskFinished(const UBTTaskNode* TaskNode, EBTNodeResult::Type TaskResult);

	/** setup message observer for given task */
	void RegisterMessageObserver(const UBTTaskNode* TaskNode, FName MessageType);
	void RegisterMessageObserver(const UBTTaskNode* TaskNode, FName MessageType, FAIRequestID MessageID);
	
	/** remove message observers registered with task */
	void UnregisterMessageObserversFrom(const UBTTaskNode* TaskNode);
	void UnregisterMessageObserversFrom(const FBTNodeIndex& TaskIdx);

	/** add active parallel task */
	void RegisterParallelTask(const UBTTaskNode* TaskNode);

	/** remove parallel task */
	void UnregisterParallelTask(const UBTTaskNode* TaskNode, uint16 InstanceIdx);

	/** unregister all aux nodes less important than given index */
	void UnregisterAuxNodesUpTo(const FBTNodeIndex& Index);

	/** unregister all aux nodes between given execution index range: FromIndex < AuxIndex < ToIndex */
	void UnregisterAuxNodesInRange(const FBTNodeIndex& FromIndex, const FBTNodeIndex& ToIndex);

	/** unregister all aux nodes in branch of tree */
	UE_DEPRECATED(4.26, "This function is deprecated. Please use RequestUnregisterAuxNodesInBranch instead.")
	void UnregisterAuxNodesInBranch(const UBTCompositeNode* Node, bool bApplyImmediately = true);

	/** BEGIN UActorComponent overrides */
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	/** END UActorComponent overrides */

	/** Schedule when will be the next tick, 0.0f means next frame, FLT_MAX means never */
	void ScheduleNextTick(float NextDeltaTime);

	/** process execution flow */
	void ProcessExecutionRequest();

	/** schedule execution flow update in next tick */
	void ScheduleExecutionUpdate();

	/** tries to find behavior tree instance in context */
	int32 FindInstanceContainingNode(const UBTNode* Node) const;

	/** tries to find template node for given instanced node */
	UBTNode* FindTemplateNode(const UBTNode* Node) const;

	/** @return current tree */
	UBehaviorTree* GetCurrentTree() const;

	/** @return tree from top of instance stack */
	UBehaviorTree* GetRootTree() const;

	/** @return active node */
	const UBTNode* GetActiveNode() const;
	
	/** get index of active instance on stack */
	uint16 GetActiveInstanceIdx() const;

	/** @return node memory */
	uint8* GetNodeMemory(UBTNode* Node, int32 InstanceIdx) const;

	/** @return true if ExecutionRequest is switching to higher priority node */
	bool IsRestartPending() const;

	/** @return true if waiting for abort to finish */
	bool IsAbortPending() const;

	/** @return true if active node is one of child nodes of given one */
	bool IsExecutingBranch(const UBTNode* Node, int32 ChildIndex = -1) const;

	/** @return true if aux node is currently active */
	bool IsAuxNodeActive(const UBTAuxiliaryNode* AuxNode) const;
	bool IsAuxNodeActive(const UBTAuxiliaryNode* AuxNodeTemplate, int32 InstanceIdx) const;

	/** Returns true if InstanceStack contains any BT runtime instances */
	bool IsInstanceStackEmpty() const { return (InstanceStack.Num() == 0); }

	/** @return status of speficied task */
	EBTTaskStatus::Type GetTaskStatus(const UBTTaskNode* TaskNode) const;

	virtual FString GetDebugInfoString() const override;
	virtual FString DescribeActiveTasks() const;
	virtual FString DescribeActiveTrees() const;

	/** @return the cooldown tag end time, 0.0f if CooldownTag is not found */
	UFUNCTION(BlueprintCallable, Category = "AI|Logic")
	float GetTagCooldownEndTime(FGameplayTag CooldownTag) const;

	/** add to the cooldown tag's duration */
	UFUNCTION(BlueprintCallable, Category = "AI|Logic")
	void AddCooldownTagDuration(FGameplayTag CooldownTag, float CooldownDuration, bool bAddToExistingDuration);

	/** assign subtree to RunBehaviorDynamic task specified by tag */
	UFUNCTION(BlueprintCallable, Category="AI|Logic")
	virtual void SetDynamicSubtree(FGameplayTag InjectTag, UBehaviorTree* BehaviorAsset);

// Code for timing BT Search for FramePro
#if !UE_BUILD_SHIPPING
	static void EndFrame();
#endif

#if ENABLE_VISUAL_LOG
	virtual void DescribeSelfToVisLog(struct FVisualLogEntry* Snapshot) const override;
#endif

#if CSV_PROFILER
	/** Set a custom CSV tick stat name, must point to a static string */
	void SetCSVTickStatName(const char* InCSVTickStatName) { CSVTickStatName = InCSVTickStatName; }
#endif

protected:
	/** stack of behavior tree instances */
	TArray<FBehaviorTreeInstance> InstanceStack;

	/** list of known subtree instances */
	TArray<FBehaviorTreeInstanceId> KnownInstances;

	/** instanced nodes */
	UPROPERTY(transient)
	TArray<UBTNode*> NodeInstances;

	/** search data being currently used */
	FBehaviorTreeSearchData SearchData;

	/** execution request, search will be performed when current task finish execution/aborting */
	FBTNodeExecutionInfo ExecutionRequest;

	/** result of ExecutionRequest, will be applied when current task finish aborting */
	FBTPendingExecutionInfo PendingExecution;

	/** list of all pending aux nodes unregistration requests */
	FBTPendingAuxNodesUnregisterInfo PendingUnregisterAuxNodesRequests;

	/** stored data for starting new tree, waits until previously running finishes aborting */
	FBTTreeStartInfo TreeStartInfo;

	/** message observers mapped by instance & execution index */
	TMultiMap<FBTNodeIndex,FAIMessageObserverHandle> TaskMessageObservers;

	/** behavior cooldowns mapped by tag to last time it was set */
	TMap<FGameplayTag, float> CooldownTagsMap;

#if USE_BEHAVIORTREE_DEBUGGER
	/** search flow for debugger */
	mutable TArray<TArray<FBehaviorTreeDebuggerInstance::FNodeFlowData> > CurrentSearchFlow;
	mutable TArray<TArray<FBehaviorTreeDebuggerInstance::FNodeFlowData> > CurrentRestarts;
	mutable TMap<FName, FString> SearchStartBlackboard;
	mutable TArray<FBehaviorTreeDebuggerInstance> RemovedInstances;

	/** debugger's recorded data */
	mutable TArray<FBehaviorTreeExecutionStep> DebuggerSteps;

	/** set when at least one debugger window is opened */
	static int32 ActiveDebuggerCounter;
#endif

	// Code for timing BT Search for FramePro
#if !UE_BUILD_SHIPPING
	static bool bAddedEndFrameCallback;
	static double FrameSearchTime;
	static int32 NumSearchTimeCalls;
#endif

	/** index of last active instance on stack */
	uint16 ActiveInstanceIdx;

	/** if set, StopTree calls will be deferred */
	uint8 StopTreeLock;

	/** if set, StopTree will be called at the end of tick */
	uint8 bDeferredStopTree : 1;

	/** loops tree execution */
	uint8 bLoopExecution : 1;

	/** set when execution is waiting for tasks to abort (current or parallel's main) */
	uint8 bWaitingForAbortingTasks : 1;

	/** set when execution update is scheduled for next tick */
	uint8 bRequestedFlowUpdate : 1;

	/** set when tree stop was called */
	uint8 bRequestedStop : 1;

	/** if set, tree execution is allowed */
	uint8 bIsRunning : 1;

	/** if set, execution requests will be postponed */
	uint8 bIsPaused : 1;

	/** push behavior tree instance on execution stack
	 *	@NOTE: should never be called out-side of BT execution, meaning only BT tasks can push another BT instance! */
	bool PushInstance(UBehaviorTree& TreeAsset);

	/** add unique Id of newly created subtree to KnownInstances list and return its index */
	uint8 UpdateInstanceId(UBehaviorTree* TreeAsset, const UBTNode* OriginNode, int32 OriginInstanceIdx);

	/** remove instanced nodes, known subtree instances and safely clears their persistent memory */
	void RemoveAllInstances();

	/** copy memory block from running instances to persistent memory */
	void CopyInstanceMemoryToPersistent();

	/** copy memory block from persistent memory to running instances (rollback) */
	void CopyInstanceMemoryFromPersistent();

	/** find next task to execute */
	UBTTaskNode* FindNextTask(UBTCompositeNode* ParentNode, uint16 ParentInstanceIdx, EBTNodeResult::Type LastResult);

	/** called when tree runs out of nodes to execute */
	void OnTreeFinished();

	/** apply pending node updates from SearchData */
	void ApplySearchData(UBTNode* NewActiveNode);

	/** apply pending node updates required for discarded search */
	void ApplyDiscardedSearch();

	/** apply updates from specific list */
	void ApplySearchUpdates(const TArray<FBehaviorTreeSearchUpdate>& UpdateList, int32 NewNodeExecutionIndex, bool bPostUpdate = false);

	/** abort currently executed task */
	void AbortCurrentTask();

	/** execute new task */
	void ExecuteTask(UBTTaskNode* TaskNode);

	/** deactivate all nodes up to requested one */
	bool DeactivateUpTo(UBTCompositeNode* Node, uint16 NodeInstanceIdx, EBTNodeResult::Type& NodeResult, int32& OutLastDeactivatedChildIndex);

	/** update state of aborting tasks */
	void UpdateAbortingTasks();

	/** apply pending execution from last task search */
	void ProcessPendingExecution();

	/** apply pending tree initialization */
	void ProcessPendingInitialize();

	/**
	 * apply pending unregister aux nodes requests
	 * @return true if some request were processed, false otherwise
	 */
	bool ProcessPendingUnregister();

	/** restore state of tree to state before search */
	void RollbackSearchChanges();

	/** make a snapshot for debugger */
	void StoreDebuggerExecutionStep(EBTExecutionSnap::Type SnapType);

	/** make a snapshot for debugger from given subtree instance */
	void StoreDebuggerInstance(FBehaviorTreeDebuggerInstance& InstanceInfo, uint16 InstanceIdx, EBTExecutionSnap::Type SnapType) const;
	void StoreDebuggerRemovedInstance(uint16 InstanceIdx) const;

	/** store search step for debugger */
	void StoreDebuggerSearchStep(const UBTNode* Node, uint16 InstanceIdx, EBTNodeResult::Type NodeResult) const;
	void StoreDebuggerSearchStep(const UBTNode* Node, uint16 InstanceIdx, bool bPassed) const;

	/** store restarting node for debugger */
	void StoreDebuggerRestart(const UBTNode* Node, uint16 InstanceIdx, bool bAllowed);

	/** describe blackboard's key values */
	void StoreDebuggerBlackboard(TMap<FName, FString>& BlackboardValueDesc) const;

	/** gather nodes runtime descriptions */
	void StoreDebuggerRuntimeValues(TArray<FString>& RuntimeDescriptions, UBTNode* RootNode, uint16 InstanceIdx) const;

	/** update runtime description of given task node in latest debugger's snapshot */
	void UpdateDebuggerAfterExecution(const UBTTaskNode* TaskNode, uint16 InstanceIdx) const;

	/** check if debugger is currently running and can gather data */
	static bool IsDebuggerActive();

	/** Return NodeA's relative priority in regards to NodeB */
	EBTNodeRelativePriority CalculateRelativePriority(const UBTNode* NodeA, const UBTNode* NodeB) const;

	friend UBTNode;
	friend UBTCompositeNode;
	friend UBTTaskNode;
	friend UBTTask_RunBehavior;
	friend UBTTask_RunBehaviorDynamic;
	friend FBehaviorTreeDebugger;
	friend FBehaviorTreeInstance;
	friend FScopedBehaviorTreeLock;

protected:
	/** data asset defining the tree */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = AI)
	UBehaviorTree* DefaultBehaviorTreeAsset;

	/** Used to tell tickmanager that we want interval ticking */
	bool bTickedOnce = false;
	/** Predicted next DeltaTime*/
	float NextTickDeltaTime = 0.0f;
	/** Accumulated DeltaTime if ticked more than predicted next delta time */
	float AccumulatedTickDeltaTime = 0.0f;
	/** GameTime of the last DeltaTime request, used for debugging to output warnings about ticking */
	float LastRequestedDeltaTimeGameTime = 0;

#if CSV_PROFILER
	/** CSV tick stat name. Can be changed but must point to a static string */
	const char* CSVTickStatName = "BehaviorTreeTick";
#endif
};

//////////////////////////////////////////////////////////////////////////
// Inlines

FORCEINLINE UBehaviorTree* UBehaviorTreeComponent::GetCurrentTree() const
{
	return InstanceStack.Num() ? KnownInstances[InstanceStack[ActiveInstanceIdx].InstanceIdIndex].TreeAsset : NULL;
}

FORCEINLINE UBehaviorTree* UBehaviorTreeComponent::GetRootTree() const
{
	return InstanceStack.Num() ? KnownInstances[InstanceStack[0].InstanceIdIndex].TreeAsset : NULL;
}

FORCEINLINE const UBTNode* UBehaviorTreeComponent::GetActiveNode() const
{
	return InstanceStack.Num() ? InstanceStack[ActiveInstanceIdx].ActiveNode : NULL;
}

FORCEINLINE uint16 UBehaviorTreeComponent::GetActiveInstanceIdx() const
{
	return ActiveInstanceIdx;
}

FORCEINLINE bool UBehaviorTreeComponent::IsRestartPending() const
{
	return ExecutionRequest.ExecuteNode && !ExecutionRequest.bTryNextChild;
}

FORCEINLINE bool UBehaviorTreeComponent::IsAbortPending() const
{
	return bWaitingForAbortingTasks || PendingExecution.IsSet();
}
