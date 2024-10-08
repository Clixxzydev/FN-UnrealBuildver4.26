// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Hierarchy/RigUnit_AddBoneTransform.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_AddBoneTransform::GetUnitLabel() const
{
	return FString::Printf(TEXT("Offset Transform %s"), *Bone.ToString());
}

FRigUnit_AddBoneTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigBoneHierarchy* Hierarchy = ExecuteContext.GetBones();
	if (Hierarchy)
	{
		switch (Context.State)
		{
			case EControlRigState::Init:
			{
				CachedBoneIndex = Hierarchy->GetIndex(Bone);
				if (CachedBoneIndex == INDEX_NONE)
				{
					UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Bone is not set."));
				}
				break;
			}
			case EControlRigState::Update:
			{
				if (CachedBoneIndex != INDEX_NONE)
				{
					FTransform TargetTransform;
					const FTransform PreviousTransform = Hierarchy->GetGlobalTransform(CachedBoneIndex);

					if (bPostMultiply)
					{
						TargetTransform = PreviousTransform * Transform;
					}
					else
					{
						TargetTransform = Transform * PreviousTransform;
					}

					if (!FMath::IsNearlyEqual(Weight, 1.f))
					{
						float T = FMath::Clamp<float>(Weight, 0.f, 1.f);
						TargetTransform = FControlRigMathLibrary::LerpTransform(PreviousTransform, TargetTransform, T);
					}

					Hierarchy->SetGlobalTransform(CachedBoneIndex, TargetTransform, bPropagateToChildren);
				}
			}
			default:
			{
				break;
			}
		}
	}
}

