// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Animation/AnimTypes.h"
#include "Engine/PoseWatch.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Animation/AnimStateMachineTypes.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/BlendSpaceBase.h"
#include "AnimBlueprintClassSubsystem.h"

#include "AnimBlueprintGeneratedClass.generated.h"

class UAnimGraphNode_Base;
class UAnimGraphNode_StateMachineBase;
class UAnimInstance;
class UAnimStateNode;
class UAnimStateTransitionNode;
class UEdGraph;
class USkeleton;

// Represents the debugging information for a single state within a state machine
USTRUCT()
struct FStateMachineStateDebugData
{
	GENERATED_BODY()

public:
	FStateMachineStateDebugData()
		: StateMachineIndex(INDEX_NONE)
		, StateIndex(INDEX_NONE)
		, Weight(0.0f)
		, ElapsedTime(0.0f)
	{
	}

	FStateMachineStateDebugData(int32 InStateMachineIndex, int32 InStateIndex, float InWeight, float InElapsedTime)
		: StateMachineIndex(InStateMachineIndex)
		, StateIndex(InStateIndex)
		, Weight(InWeight)
		, ElapsedTime(InElapsedTime)
	{}

	// The index of the state machine
	int32 StateMachineIndex;

	// The index of the state
	int32 StateIndex;

	// The last recorded weight for this state
	float Weight;

	// The time that this state has been active (only valid if this is the current state)
	float ElapsedTime;
};

// This structure represents debugging information for a single state machine
USTRUCT()
struct FStateMachineDebugData
{
	GENERATED_BODY()

public:
	FStateMachineDebugData()
		: MachineIndex(INDEX_NONE)
	{}

	// Map from state nodes to their state entry in a state machine
	TMap<TWeakObjectPtr<UEdGraphNode>, int32> NodeToStateIndex;
	TMap<TWeakObjectPtr<UEdGraphNode>, int32> NodeToTransitionIndex;

	// The animation node that leads into this state machine (A3 only)
	TWeakObjectPtr<UAnimGraphNode_StateMachineBase> MachineInstanceNode;

	// Index of this machine in the StateMachines array
	int32 MachineIndex;

public:
	ENGINE_API UEdGraphNode* FindNodeFromStateIndex(int32 StateIndex) const;
	ENGINE_API UEdGraphNode* FindNodeFromTransitionIndex(int32 TransitionIndex) const;
};

// This structure represents debugging information for a frame snapshot
USTRUCT()
struct FAnimationFrameSnapshot
{
	GENERATED_USTRUCT_BODY()

	FAnimationFrameSnapshot()
#if WITH_EDITORONLY_DATA
		: TimeStamp(0.0)
#endif
	{
	}
#if WITH_EDITORONLY_DATA
public:
	// The snapshot of data saved from the animation
	TArray<uint8> SerializedData;

	// The time stamp for when this snapshot was taken (relative to the life timer of the object being recorded)
	double TimeStamp;

public:
	void InitializeFromInstance(UAnimInstance* Instance);
	ENGINE_API void CopyToInstance(UAnimInstance* Instance);
#endif
};

// This structure represents animation-related debugging information for an entire AnimBlueprint
// (general debug information for the event graph, etc... is still contained in a FBlueprintDebugData structure)
USTRUCT()
struct ENGINE_API FAnimBlueprintDebugData
{
	GENERATED_USTRUCT_BODY()

	FAnimBlueprintDebugData()
#if WITH_EDITORONLY_DATA
		: SnapshotBuffer(NULL)
		, SnapshotIndex(INDEX_NONE)
#endif
	{
	}

#if WITH_EDITORONLY_DATA
public:
	// Map from state machine graphs to their corresponding debug data
	TMap<TWeakObjectPtr<const UEdGraph>, FStateMachineDebugData> StateMachineDebugData;

	// Map from state graphs to their node
	TMap<TWeakObjectPtr<const UEdGraph>, TWeakObjectPtr<UAnimStateNode> > StateGraphToNodeMap;

	// Map from transition graphs to their node
	TMap<TWeakObjectPtr<const UEdGraph>, TWeakObjectPtr<UAnimStateTransitionNode> > TransitionGraphToNodeMap;

	// Map from custom transition blend graphs to their node
	TMap<TWeakObjectPtr<const UEdGraph>, TWeakObjectPtr<UAnimStateTransitionNode> > TransitionBlendGraphToNodeMap;

	// Map from animation node to their property index
	TMap<TWeakObjectPtr<const UAnimGraphNode_Base>, int32> NodePropertyToIndexMap;

	// Map from node property index to source editor node
	TMap<int32, TWeakObjectPtr<const UEdGraphNode> > NodePropertyIndexToNodeMap;

	// Map from animation node GUID to property index
	TMap<FGuid, int32> NodeGuidToIndexMap;

	// The debug data for each state machine state
	TArray<FStateMachineStateDebugData> StateData;	
	
	// History of snapshots of animation data
	TSimpleRingBuffer<FAnimationFrameSnapshot>* SnapshotBuffer;

	// Node visit structure
	struct FNodeVisit
	{
		int32 SourceID;
		int32 TargetID;
		float Weight;

		FNodeVisit(int32 InSourceID, int32 InTargetID, float InWeight)
			: SourceID(InSourceID)
			, TargetID(InTargetID)
			, Weight(InWeight)
		{
		}
	};

	// History of activated nodes
	TArray<FNodeVisit> UpdatedNodesThisFrame;

	// Values output by nodes
	struct FNodeValue
	{
		FString Text;
		int32 NodeID;

		FNodeValue(const FString& InText, int32 InNodeID)
			: Text(InText)
			, NodeID(InNodeID)
		{}
	};

	// Values output by nodes
	TArray<FNodeValue> NodeValuesThisFrame;

	// Record of a sequence player's state
	struct FSequencePlayerRecord
	{
		FSequencePlayerRecord(int32 InNodeID, float InPosition, float InLength, int32 InFrameCount)
			: NodeID(InNodeID)
			, Position(InPosition)
			, Length(InLength) 
			, FrameCount(InFrameCount)
		{}

		int32 NodeID;
		float Position;
		float Length;
		int32 FrameCount;
	};

	// All sequence player records this frame
	TArray<FSequencePlayerRecord> SequencePlayerRecordsThisFrame;

	// Record of a blend space player's state
	struct FBlendSpacePlayerRecord
	{
		FBlendSpacePlayerRecord(int32 InNodeID, UBlendSpaceBase* InBlendSpace, float InPositionX, float InPositionY, float InPositionZ)
			: NodeID(InNodeID)
			, BlendSpace(InBlendSpace)
			, PositionX(InPositionX)
			, PositionY(InPositionY) 
			, PositionZ(InPositionY)
		{}

		int32 NodeID;
		TWeakObjectPtr<const UBlendSpaceBase> BlendSpace;
		float PositionX;
		float PositionY;
		float PositionZ;
	};

	// All blend space player records this frame
	TArray<FBlendSpacePlayerRecord> BlendSpacePlayerRecordsThisFrame;

	// Active pose watches to track
	TArray<FAnimNodePoseWatch> AnimNodePoseWatch;

	// Index of snapshot
	int32 SnapshotIndex;
public:

	~FAnimBlueprintDebugData()
	{
		if (SnapshotBuffer != NULL)
		{
			delete SnapshotBuffer;
		}
		SnapshotBuffer = NULL;
	}



	bool IsReplayingSnapshot() const { return SnapshotIndex != INDEX_NONE; }
	void TakeSnapshot(UAnimInstance* Instance);
	float GetSnapshotLengthInSeconds();
	int32 GetSnapshotLengthInFrames();
	void SetSnapshotIndexByTime(UAnimInstance* Instance, double TargetTime);
	void SetSnapshotIndex(UAnimInstance* Instance, int32 NewIndex);
	void ResetSnapshotBuffer();

	void ResetNodeVisitSites();
	void RecordNodeVisit(int32 TargetNodeIndex, int32 SourceNodeIndex, float BlendWeight);
	void RecordNodeVisitArray(const TArray<FNodeVisit>& Nodes);
	void RecordStateData(int32 StateMachineIndex, int32 StateIndex, float Weight, float ElapsedTime);
	void RecordNodeValue(int32 InNodeID, const FString& InText);
	void RecordSequencePlayer(int32 InNodeID, float InPosition, float InLength, int32 InFrameCount);
	void RecordBlendSpacePlayer(int32 InNodeID, UBlendSpaceBase* InBlendSpace, float InPositionX, float InPositionY, float InPositionZ);

	void AddPoseWatch(int32 NodeID, FColor Color);
	void RemovePoseWatch(int32 NodeID);
	void UpdatePoseWatchColour(int32 NodeID, FColor Color);
#endif
};

#if WITH_EDITORONLY_DATA
namespace EPropertySearchMode
{
	enum Type
	{
		OnlyThis,
		Hierarchy
	};
}
#endif

UCLASS()
class ENGINE_API UAnimBlueprintGeneratedClass : public UBlueprintGeneratedClass, public IAnimClassInterface
{
	GENERATED_UCLASS_BODY()

	friend class UAnimBlueprintClassSubsystem;
	friend class FAnimBlueprintCompilerContext;

	// List of state machines present in this blueprint class
	UPROPERTY()
	TArray<FBakedAnimationStateMachine> BakedStateMachines;

	/** Target skeleton for this blueprint class */
	UPROPERTY()
	USkeleton* TargetSkeleton;

	/** A list of anim notifies that state machines (or anything else) may reference */
	UPROPERTY()
	TArray<FAnimNotifyEvent> AnimNotifies;

	// Indices for each of the saved pose nodes that require updating, in the order they need to get updates, per layer
	UPROPERTY()
	TMap<FName, FCachedPoseIndices> OrderedSavedPoseIndicesMap;

	// The various anim functions that this class holds (created during GenerateAnimationBlueprintFunctions)
	TArray<FAnimBlueprintFunction> AnimBlueprintFunctions;

	// The arrays of anim nodes; this is transient generated data (created during Link)
	TArray<FStructProperty*> AnimNodeProperties;
	TArray<FStructProperty*> LinkedAnimGraphNodeProperties;
	TArray<FStructProperty*> LinkedAnimLayerNodeProperties;
	TArray<FStructProperty*> PreUpdateNodeProperties;
	TArray<FStructProperty*> DynamicResetNodeProperties;
	TArray<FStructProperty*> StateMachineNodeProperties;
	TArray<FStructProperty*> InitializationNodeProperties;

	// Array of sync group names in the order that they are requested during compile
	UPROPERTY()
	TArray<FName> SyncGroupNames;

	// The default handler for graph-exposed inputs
	UPROPERTY()
	TArray<FExposedValueHandler> EvaluateGraphExposedInputs;

	// Indices for any Asset Player found within a specific (named) Anim Layer Graph, or implemented Anim Interface Graph
	UPROPERTY()
	TMap<FName, FGraphAssetPlayerInformation> GraphAssetPlayerInformation;

	// Per layer graph blending options
	UPROPERTY()
	TMap<FName, FAnimGraphBlendOptions> GraphBlendOptions;

	/** Data for each subsystem */
	UPROPERTY()
	TArray<UAnimBlueprintClassSubsystem*> Subsystems;

	/** Map of class->subsystem */
	TMap<TSubclassOf<UAnimBlueprintClassSubsystem>, UAnimBlueprintClassSubsystem*> SubsystemMap;
	TMap<TSubclassOf<UInterface>, UAnimBlueprintClassSubsystem*> SubsystemInterfaceMap;

	/** Subsystem properties */
	TArray<FStructProperty*> SubsystemProperties;

public:
	// IAnimClassInterface interface
	virtual const TArray<FBakedAnimationStateMachine>& GetBakedStateMachines() const override { return GetRootClass()->BakedStateMachines; }
	virtual USkeleton* GetTargetSkeleton() const override { return TargetSkeleton; }
	virtual const TArray<FAnimNotifyEvent>& GetAnimNotifies() const override { return GetRootClass()->AnimNotifies; }
	virtual const TArray<FStructProperty*>& GetAnimNodeProperties() const override { return AnimNodeProperties; }
	virtual const TArray<FStructProperty*>& GetLinkedAnimGraphNodeProperties() const override { return LinkedAnimGraphNodeProperties; }
	virtual const TArray<FStructProperty*>& GetLinkedAnimLayerNodeProperties() const override { return LinkedAnimLayerNodeProperties; }
	virtual const TArray<FStructProperty*>& GetPreUpdateNodeProperties() const override { return PreUpdateNodeProperties; }
	virtual const TArray<FStructProperty*>& GetDynamicResetNodeProperties() const override { return DynamicResetNodeProperties; }
	virtual const TArray<FStructProperty*>& GetStateMachineNodeProperties() const override { return StateMachineNodeProperties; }
	virtual const TArray<FStructProperty*>& GetInitializationNodeProperties() const override { return InitializationNodeProperties; }
	virtual const TArray<FName>& GetSyncGroupNames() const override { return GetRootClass()->SyncGroupNames; }
	virtual const TMap<FName, FCachedPoseIndices>& GetOrderedSavedPoseNodeIndicesMap() const override { return GetRootClass()->OrderedSavedPoseIndicesMap; }
	virtual int32 GetSyncGroupIndex(FName SyncGroupName) const override { return GetSyncGroupNames().IndexOfByKey(SyncGroupName); }
	virtual const TArray<FExposedValueHandler>& GetExposedValueHandlers() const { return EvaluateGraphExposedInputs; }
	virtual const TArray<FAnimBlueprintFunction>& GetAnimBlueprintFunctions() const override { return AnimBlueprintFunctions; }
	virtual const TMap<FName, FGraphAssetPlayerInformation>& GetGraphAssetPlayerInformation() const override { return GetRootClass()->GraphAssetPlayerInformation; }
	virtual const TMap<FName, FAnimGraphBlendOptions>& GetGraphBlendOptions() const override { return GetRootClass()->GraphBlendOptions; }
	virtual const TArray<UAnimBlueprintClassSubsystem*>& GetSubsystems() const override { return GetRootClass()->Subsystems; }
	virtual UAnimBlueprintClassSubsystem* GetSubsystem(TSubclassOf<UAnimBlueprintClassSubsystem> InClass) const override;
	virtual UAnimBlueprintClassSubsystem* FindSubsystemWithInterface(TSubclassOf<UInterface> InClassInterface) const override;
	virtual const TArray<FStructProperty*>& GetSubsystemProperties() const override { return SubsystemProperties; }

public:
	// Get the root anim BP class (i.e. if this is a derived class).
	// Some properties that are derived from the compiled anim graph are routed to the 'Root' class
	// as child classes don't get fully compiled. Instead they just override various asset players leaving the
	// full compilation up to the base class. 
	// Previously we copied over all the parent class data in Link(), but as Link() can be called on the async 
	// loading thread we cant do any object-duplication operations (e.g. with subsystems).
	const UAnimBlueprintGeneratedClass* GetRootClass() const;

#if WITH_EDITORONLY_DATA
	FAnimBlueprintDebugData AnimBlueprintDebugData;

	FAnimBlueprintDebugData& GetAnimBlueprintDebugData()
	{
		return AnimBlueprintDebugData;
	}

	template<typename StructType>
	const int32* GetNodePropertyIndexFromHierarchy(UAnimGraphNode_Base* Node)
	{
		TArray<const UBlueprintGeneratedClass*> BlueprintHierarchy;
		GetGeneratedClassesHierarchy(this, BlueprintHierarchy);

		for (const UBlueprintGeneratedClass* Blueprint : BlueprintHierarchy)
		{
			if (const UAnimBlueprintGeneratedClass* AnimBlueprintClass = Cast<UAnimBlueprintGeneratedClass>(Blueprint))
			{
				const int32* SearchIndex = AnimBlueprintClass->AnimBlueprintDebugData.NodePropertyToIndexMap.Find(Node);
				if (SearchIndex)
				{
					return SearchIndex;
				}
			}

		}
		return NULL;
	}

	template<typename StructType>
	const int32* GetNodePropertyIndex(UAnimGraphNode_Base* Node, EPropertySearchMode::Type SearchMode = EPropertySearchMode::OnlyThis)
	{
		return (SearchMode == EPropertySearchMode::OnlyThis) ? AnimBlueprintDebugData.NodePropertyToIndexMap.Find(Node) : GetNodePropertyIndexFromHierarchy<StructType>(Node);
	}

	template<typename StructType>
	int32 GetLinkIDForNode(UAnimGraphNode_Base* Node, EPropertySearchMode::Type SearchMode = EPropertySearchMode::OnlyThis)
	{
		const int32* pIndex = GetNodePropertyIndex<StructType>(Node, SearchMode);
		if (pIndex)
		{
			return (AnimNodeProperties.Num() - 1 - *pIndex); //@TODO: Crazysauce
		}
		return -1;
	}

	template<typename StructType>
	FStructProperty* GetPropertyForNode(UAnimGraphNode_Base* Node, EPropertySearchMode::Type SearchMode = EPropertySearchMode::OnlyThis)
	{
		const int32* pIndex = GetNodePropertyIndex<StructType>(Node, SearchMode);
		if (pIndex)
		{
			if (FStructProperty* AnimationProperty = AnimNodeProperties[AnimNodeProperties.Num() - 1 - *pIndex])
			{
				if (AnimationProperty->Struct->IsChildOf(StructType::StaticStruct()))
				{
					return AnimationProperty;
				}
			}
		}

		return NULL;
	}

	template<typename StructType>
	StructType* GetPropertyInstance(UObject* Object, UAnimGraphNode_Base* Node, EPropertySearchMode::Type SearchMode = EPropertySearchMode::OnlyThis)
	{
		FStructProperty* AnimationProperty = GetPropertyForNode<StructType>(Node);
		if (AnimationProperty)
		{
			return AnimationProperty->ContainerPtrToValuePtr<StructType>((void*)Object);
		}

		return NULL;
	}

	template<typename StructType>
	StructType* GetPropertyInstance(UObject* Object, FGuid NodeGuid, EPropertySearchMode::Type SearchMode = EPropertySearchMode::OnlyThis)
	{
		const int32* pIndex = GetNodePropertyIndexFromGuid(NodeGuid, SearchMode);
		if (pIndex)
		{
			if (FStructProperty* AnimProperty = AnimNodeProperties[AnimNodeProperties.Num() - 1 - *pIndex])
			{
				if (AnimProperty->Struct->IsChildOf(StructType::StaticStruct()))
				{
					return AnimProperty->ContainerPtrToValuePtr<StructType>((void*)Object);
				}
			}
		}

		return NULL;
	}

	template<typename StructType>
	StructType& GetPropertyInstanceChecked(UObject* Object, UAnimGraphNode_Base* Node, EPropertySearchMode::Type SearchMode = EPropertySearchMode::OnlyThis)
	{
		const int32 Index = AnimBlueprintDebugData.NodePropertyToIndexMap.FindChecked(Node);
		FStructProperty* AnimationProperty = AnimNodeProperties[AnimNodeProperties.Num() - 1 - Index];
		check(AnimationProperty);
		check(AnimationProperty->Struct->IsChildOf(StructType::StaticStruct()));
		return AnimationProperty->ContainerPtrToValuePtr<StructType>((void*)Object);
	}

	const int32* GetNodePropertyIndexFromGuid(FGuid Guid, EPropertySearchMode::Type SearchMode = EPropertySearchMode::OnlyThis);

	const UEdGraphNode* GetVisualNodeFromNodePropertyIndex(int32 PropertyIndex) const;
#endif

	// Called after Link to patch up references to the nodes in the CDO
	void LinkFunctionsToDefaultObjectNodes(UObject* DefaultObject);

	// Populates AnimBlueprintFunctions according to the UFunction(s) on this class
	void GenerateAnimationBlueprintFunctions();

	// Rebuild subsystem & subsystem interface maps from Subsystems array
	void RebuildSubsystemMaps();

	// UObject interface
	virtual void Serialize(FArchive& Ar) override;
	// End of UObject interface

	// UStruct interface
	virtual void Link(FArchive& Ar, bool bRelinkExistingProperties) override;
	// End of UStruct interface

	// UClass interface
	virtual void PurgeClass(bool bRecompilingOnLoad) override;
	virtual uint8* GetPersistentUberGraphFrame(UObject* Obj, UFunction* FuncToCheck) const override;
	virtual void PostLoadDefaultObject(UObject* Object) override;
	virtual void PostLoad() override;
	// End of UClass interface
};

template<typename NodeType>
NodeType* GetNodeFromPropertyIndex(UObject* AnimInstanceObject, const IAnimClassInterface* AnimBlueprintClass, int32 PropertyIndex)
{
	if (PropertyIndex != INDEX_NONE)
	{
		FStructProperty* NodeProperty = AnimBlueprintClass->GetAnimNodeProperties()[AnimBlueprintClass->GetAnimNodeProperties().Num() - 1 - PropertyIndex]; //@TODO: Crazysauce
		check(NodeProperty->Struct == NodeType::StaticStruct());
		return NodeProperty->ContainerPtrToValuePtr<NodeType>(AnimInstanceObject);
	}

	return NULL;
}