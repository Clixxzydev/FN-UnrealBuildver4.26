// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsEngine/ConstraintInstance.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "UObject/FrameworkObjectVersion.h"
#include "UObject/AnimPhysObjectVersion.h"
#include "HAL/IConsoleManager.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsPublic.h"
#include "Physics/PhysicsInterfaceTypes.h"

#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "HAL/LowLevelMemTracker.h"

#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

using namespace PhysicsInterfaceTypes;

#define LOCTEXT_NAMESPACE "ConstraintInstance"

TAutoConsoleVariable<float> CVarConstraintLinearDampingScale(
	TEXT("p.ConstraintLinearDampingScale"),
	1.f,
	TEXT("The multiplier of constraint linear damping in simulation. Default: 1"),
	ECVF_ReadOnly);

TAutoConsoleVariable<float> CVarConstraintLinearStiffnessScale(
	TEXT("p.ConstraintLinearStiffnessScale"),
	1.f,
	TEXT("The multiplier of constraint linear stiffness in simulation. Default: 1"),
	ECVF_ReadOnly);

TAutoConsoleVariable<float> CVarConstraintAngularDampingScale(
	TEXT("p.ConstraintAngularDampingScale"),
	100000.f,
	TEXT("The multiplier of constraint angular damping in simulation. Default: 100000"),
	ECVF_ReadOnly);

TAutoConsoleVariable<float> CVarConstraintAngularStiffnessScale(
	TEXT("p.ConstraintAngularStiffnessScale"),
	100000.f,
	TEXT("The multiplier of constraint angular stiffness in simulation. Default: 100000"),
	ECVF_ReadOnly);

/** Handy macro for setting BIT of VAR based on the bool CONDITION */
#define SET_DRIVE_PARAM(VAR, CONDITION, BIT)   (VAR) = (CONDITION) ? ((VAR) | (BIT)) : ((VAR) & ~(BIT))

float RevolutionsToRads(const float Revolutions)
{
	return Revolutions * 2.f * PI;
}

FVector RevolutionsToRads(const FVector Revolutions)
{
	return Revolutions * 2.f * PI;
}

#if WITH_EDITOR
void FConstraintProfileProperties::SyncChangedConstraintProperties(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	static const FName StiffnessProperty = GET_MEMBER_NAME_CHECKED(FConstraintDrive, Stiffness);
	static const FName MaxForceName = GET_MEMBER_NAME_CHECKED(FConstraintDrive, MaxForce);
	static const FName DampingName = GET_MEMBER_NAME_CHECKED(FConstraintDrive, Damping);

	if (TDoubleLinkedList<FProperty*>::TDoubleLinkedListNode* PropertyNode = PropertyChangedEvent.PropertyChain.GetTail())
	{
		if (TDoubleLinkedList<FProperty*>::TDoubleLinkedListNode* ParentProeprtyNode = PropertyNode->GetPrevNode())
		{
			if (FProperty* Property = PropertyNode->GetValue())
			{
				if (FProperty* ParentProperty = ParentProeprtyNode->GetValue())
				{
					const FName PropertyName = Property->GetFName();
					const FName ParentPropertyName = ParentProperty->GetFName();

					if (ParentPropertyName == GET_MEMBER_NAME_CHECKED(FLinearDriveConstraint, XDrive))
					{
						if (StiffnessProperty == PropertyName)
						{
							LinearDrive.YDrive.Stiffness = LinearDrive.XDrive.Stiffness;
							LinearDrive.ZDrive.Stiffness = LinearDrive.XDrive.Stiffness;
						}
						else if (MaxForceName == PropertyName)
						{
							LinearDrive.YDrive.MaxForce = LinearDrive.XDrive.MaxForce;
							LinearDrive.ZDrive.MaxForce = LinearDrive.XDrive.MaxForce;
						}
						else if (DampingName == PropertyName)
						{
							LinearDrive.YDrive.Damping = LinearDrive.XDrive.Damping;
							LinearDrive.ZDrive.Damping = LinearDrive.XDrive.Damping;
						}
					}
					else if (ParentPropertyName == GET_MEMBER_NAME_CHECKED(FAngularDriveConstraint, SlerpDrive))
					{
						if (StiffnessProperty == PropertyName)
						{
							AngularDrive.SwingDrive.Stiffness = AngularDrive.SlerpDrive.Stiffness;
							AngularDrive.TwistDrive.Stiffness = AngularDrive.SlerpDrive.Stiffness;
						}
						else if (MaxForceName == PropertyName)
						{
							AngularDrive.SwingDrive.MaxForce = AngularDrive.SlerpDrive.MaxForce;
							AngularDrive.TwistDrive.MaxForce = AngularDrive.SlerpDrive.MaxForce;
						}
						else if (DampingName == PropertyName)
						{
							AngularDrive.SwingDrive.Damping = AngularDrive.SlerpDrive.Damping;
							AngularDrive.TwistDrive.Damping = AngularDrive.SlerpDrive.Damping;
						}
					}
				}
			}
		}
	}
}
#endif

FConstraintProfileProperties::FConstraintProfileProperties()
	: ProjectionLinearTolerance(5.f)
	, ProjectionAngularTolerance(180.f)
	, ProjectionLinearAlpha(1.0f)
	, ProjectionAngularAlpha(0.0f)
	, LinearBreakThreshold(300.f)
	, AngularBreakThreshold(500.f)
	, bDisableCollision(false)
	, bParentDominates(false)
	, bEnableProjection(true)
	, bEnableSoftProjection(false)
	, bAngularBreakable(false)
	, bLinearBreakable(false)
{
}

void FConstraintInstance::UpdateLinearLimit()
{
	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		ProfileInstance.LinearLimit.UpdateLinearLimit_AssumesLocked(InUnbrokenConstraint, AverageMass, bScaleLinearLimits ? LastKnownScale : 1.0f);
	});
}

void FConstraintInstance::UpdateAngularLimit()
{
	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		ProfileInstance.ConeLimit.UpdateConeLimit_AssumesLocked(InUnbrokenConstraint, AverageMass);
		ProfileInstance.TwistLimit.UpdateTwistLimit_AssumesLocked(InUnbrokenConstraint, AverageMass);
	});
}

void FConstraintInstance::UpdateBreakable()
{
	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		ProfileInstance.UpdateBreakable_AssumesLocked(InUnbrokenConstraint);
	});
}

void FConstraintProfileProperties::UpdateBreakable_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef) const
{
	const float LinearBreakForce = bLinearBreakable ? LinearBreakThreshold : MAX_FLT;
	const float AngularBreakForce = bAngularBreakable ? AngularBreakThreshold : MAX_FLT;

	FPhysicsInterface::SetBreakForces_AssumesLocked(InConstraintRef, LinearBreakForce, AngularBreakForce);
}

void FConstraintInstance::UpdateDriveTarget()
{
	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FPhysicsInterface::UpdateDriveTarget_AssumesLocked(InUnbrokenConstraint, ProfileInstance.LinearDrive, ProfileInstance.AngularDrive);
	});
}

/** Constructor **/
FConstraintInstance::FConstraintInstance()
	: ConstraintIndex(0)
	, PhysScene(nullptr)
	, AngularRotationOffset(ForceInitToZero)
	, bScaleLinearLimits(true)
	, AverageMass(0.f)
	, UserData(this)
	, LastKnownScale(1.f)
#if WITH_EDITORONLY_DATA
	, bDisableCollision_DEPRECATED(false)
	, bEnableProjection_DEPRECATED(true)
	, ProjectionLinearTolerance_DEPRECATED(5.f)
	, ProjectionAngularTolerance_DEPRECATED(180.f)
	, LinearXMotion_DEPRECATED(ACM_Locked)
	, LinearYMotion_DEPRECATED(ACM_Locked)
	, LinearZMotion_DEPRECATED(ACM_Locked)
	, LinearLimitSize_DEPRECATED(0.f)
	, bLinearLimitSoft_DEPRECATED(false)
	, LinearLimitStiffness_DEPRECATED(0.f)
	, LinearLimitDamping_DEPRECATED(0.f)
	, bLinearBreakable_DEPRECATED(false)
	, LinearBreakThreshold_DEPRECATED(300.f)
	, AngularSwing1Motion_DEPRECATED(ACM_Free)
	, AngularTwistMotion_DEPRECATED(ACM_Free)
	, AngularSwing2Motion_DEPRECATED(ACM_Free)
	, bSwingLimitSoft_DEPRECATED(true)
	, bTwistLimitSoft_DEPRECATED(true)
	, Swing1LimitAngle_DEPRECATED(45)
	, TwistLimitAngle_DEPRECATED(45)
	, Swing2LimitAngle_DEPRECATED(45)
	, SwingLimitStiffness_DEPRECATED(50)
	, SwingLimitDamping_DEPRECATED(5)
	, TwistLimitStiffness_DEPRECATED(50)
	, TwistLimitDamping_DEPRECATED(5)
	, bAngularBreakable_DEPRECATED(false)
	, AngularBreakThreshold_DEPRECATED(500.f)
	, bLinearXPositionDrive_DEPRECATED(false)
	, bLinearXVelocityDrive_DEPRECATED(false)
	, bLinearYPositionDrive_DEPRECATED(false)
	, bLinearYVelocityDrive_DEPRECATED(false)
	, bLinearZPositionDrive_DEPRECATED(false)
	, bLinearZVelocityDrive_DEPRECATED(false)
	, bLinearPositionDrive_DEPRECATED(false)
	, bLinearVelocityDrive_DEPRECATED(false)
	, LinearPositionTarget_DEPRECATED(ForceInit)
	, LinearVelocityTarget_DEPRECATED(ForceInit)
	, LinearDriveSpring_DEPRECATED(50.0f)
	, LinearDriveDamping_DEPRECATED(1.0f)
	, LinearDriveForceLimit_DEPRECATED(0)
	, bSwingPositionDrive_DEPRECATED(false)
	, bSwingVelocityDrive_DEPRECATED(false)
	, bTwistPositionDrive_DEPRECATED(false)
	, bTwistVelocityDrive_DEPRECATED(false)
	, bAngularSlerpDrive_DEPRECATED(false)
	, bAngularOrientationDrive_DEPRECATED(false)
	, bEnableSwingDrive_DEPRECATED(true)
	, bEnableTwistDrive_DEPRECATED(true)
	, bAngularVelocityDrive_DEPRECATED(false)
	, AngularPositionTarget_DEPRECATED(ForceInit)
	, AngularDriveMode_DEPRECATED(EAngularDriveMode::SLERP)
	, AngularOrientationTarget_DEPRECATED(ForceInit)
	, AngularVelocityTarget_DEPRECATED(ForceInit)
	, AngularDriveSpring_DEPRECATED(50.0f)
	, AngularDriveDamping_DEPRECATED(1.0f)
	, AngularDriveForceLimit_DEPRECATED(0)
#endif	//EDITOR_ONLY_DATA
{
	Pos1 = FVector(0.0f, 0.0f, 0.0f);
	PriAxis1 = FVector(1.0f, 0.0f, 0.0f);
	SecAxis1 = FVector(0.0f, 1.0f, 0.0f);

	Pos2 = FVector(0.0f, 0.0f, 0.0f);
	PriAxis2 = FVector(1.0f, 0.0f, 0.0f);
	SecAxis2 = FVector(0.0f, 1.0f, 0.0f);
}

void FConstraintInstance::SetDisableCollision(bool InDisableCollision)
{
	ProfileInstance.bDisableCollision = InDisableCollision;

	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FPhysicsInterface::SetCollisionEnabled(InUnbrokenConstraint, !InDisableCollision);
	});
}

float ComputeAverageMass_AssumesLocked(const FPhysicsActorHandle& InActor1, const FPhysicsActorHandle& InActor2)
{
	float AverageMass = 0;

	float TotalMass = 0;
	int NumDynamic = 0;

	if (FPhysicsInterface::IsValid(InActor1) && FPhysicsInterface::IsRigidBody(InActor1))
	{
		TotalMass += FPhysicsInterface::GetMass_AssumesLocked(InActor1);
		++NumDynamic;
	}

	if(FPhysicsInterface::IsValid(InActor2) && FPhysicsInterface::IsRigidBody(InActor2))
	{
		TotalMass += FPhysicsInterface::GetMass_AssumesLocked(InActor2);
		++NumDynamic;
	}

	check(NumDynamic);

	if(NumDynamic > 0) // Some builds not taking the assumption from the check above and warn of zero divide
	{
		AverageMass = TotalMass / NumDynamic; //-V609
	}

	return AverageMass;
}

bool GetActorRefs(FBodyInstance* Body1, FBodyInstance* Body2, FPhysicsActorHandle& OutActorRef1, FPhysicsActorHandle& OutActorRef2, UObject* DebugOwner)
{
	FPhysicsActorHandle ActorRef1 = Body1 ? Body1->ActorHandle : FPhysicsActorHandle();
	FPhysicsActorHandle ActorRef2 = Body2 ? Body2->ActorHandle : FPhysicsActorHandle();

	// Do not create joint unless you have two actors
	// Do not create joint unless one of the actors is dynamic
	if((!FPhysicsInterface::IsValid(ActorRef1) || !FPhysicsInterface::IsRigidBody(ActorRef1)) && (!FPhysicsInterface::IsValid(ActorRef2) || !FPhysicsInterface::IsRigidBody(ActorRef2)))
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		FMessageLog("PIE").Warning()
			->AddToken(FTextToken::Create(LOCTEXT("TwoStaticBodiesWarningStart", "Constraint in")))
			->AddToken(FTextToken::Create(FText::Format(LOCTEXT("TwoStaticBodiesWarningOwner", "'{0}'"), FText::FromString(GetPathNameSafe(DebugOwner)))))
			->AddToken(FTextToken::Create(LOCTEXT("TwoStaticBodiesWarningEnd", "attempting to create a joint between objects that are both static.  No joint created.")));
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		return false;
	}

	if(FPhysicsInterface::IsValid(ActorRef1) && FPhysicsInterface::IsValid(ActorRef2) && ActorRef1 == ActorRef2)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		const UPrimitiveComponent* PrimComp = Body1 ? Body1->OwnerComponent.Get() : nullptr;
		FMessageLog("PIE").Warning()
			->AddToken(FTextToken::Create(LOCTEXT("SameBodyWarningStart", "Constraint in")))
			->AddToken(FTextToken::Create(FText::Format(LOCTEXT("SameBodyWarningOwner", "'{0}'"), FText::FromString(GetPathNameSafe(DebugOwner)))))
			->AddToken(FTextToken::Create(LOCTEXT("SameBodyWarningMid", "attempting to create a joint to the same body")))
			->AddToken(FUObjectToken::Create(PrimComp));
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		return false;
	}

	// Ensure that actors are either invalid (ie 'world') or valid to simulate.
	bool bActor1ValidToSim = false;
	bool bActor2ValidToSim = false;
	FPhysicsCommand::ExecuteRead(ActorRef1, ActorRef2, [&](const FPhysicsActorHandle& ActorA, const FPhysicsActorHandle& ActorB)
	{
		bActor1ValidToSim = !FPhysicsInterface::IsValid(ActorRef1) || FPhysicsInterface::CanSimulate_AssumesLocked(ActorRef1);
		bActor2ValidToSim = !FPhysicsInterface::IsValid(ActorRef2) || FPhysicsInterface::CanSimulate_AssumesLocked(ActorRef2);
	});

	if(!bActor1ValidToSim || !bActor2ValidToSim)
	{
		OutActorRef1 = FPhysicsActorHandle();
		OutActorRef2 = FPhysicsActorHandle();

		return false;
	}

	OutActorRef1 = ActorRef1;
	OutActorRef2 = ActorRef2;

	return true;
}

bool FConstraintInstance::CreateJoint_AssumesLocked(const FPhysicsActorHandle& InActorRef1, const FPhysicsActorHandle& InActorRef2)
{
#if WITH_CHAOS
	LLM_SCOPE(ELLMTag::Chaos);
#else
	LLM_SCOPE(ELLMTag::PhysX);
#endif

	FTransform Local1 = GetRefFrame(EConstraintFrame::Frame1);
	if(FPhysicsInterface::IsValid(InActorRef1))
	{
		Local1.ScaleTranslation(FVector(LastKnownScale));
	}

	checkf(Local1.IsValid() && !Local1.ContainsNaN(), TEXT("%s"), *Local1.ToString());

	FTransform Local2 = GetRefFrame(EConstraintFrame::Frame2);
	if(FPhysicsInterface::IsValid(InActorRef2))
	{
		Local2.ScaleTranslation(FVector(LastKnownScale));
	}
	
	checkf(Local2.IsValid() && !Local2.ContainsNaN(), TEXT("%s"), *Local2.ToString());

	ConstraintHandle = FPhysicsInterface::CreateConstraint(InActorRef1, InActorRef2, Local1, Local2);

	if(!ConstraintHandle.IsValid())
	{
		UE_LOG(LogPhysics, Log, TEXT("FConstraintInstance::CreatePxJoint_AssumesLocked - Invalid 6DOF joint (%s)"), *JointName.ToString());
		return false;
	}

	FPhysicsInterface::SetConstraintUserData(ConstraintHandle, &UserData);

	return true;
}

void FConstraintProfileProperties::UpdateConstraintFlags_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef) const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	FPhysicsInterface::SetCanVisualize(InConstraintRef, true);
#endif

	FPhysicsInterface::SetCollisionEnabled(InConstraintRef, !bDisableCollision);
#if WITH_CHAOS
	FPhysicsInterface::SetProjectionEnabled_AssumesLocked(InConstraintRef, bEnableProjection, ProjectionLinearAlpha, ProjectionAngularAlpha);
#else
	FPhysicsInterface::SetProjectionEnabled_AssumesLocked(InConstraintRef, bEnableProjection, ProjectionLinearTolerance, ProjectionAngularTolerance);
#endif
	FPhysicsInterface::SetParentDominates_AssumesLocked(InConstraintRef, bParentDominates);
}


void FConstraintInstance::UpdateAverageMass_AssumesLocked(const FPhysicsActorHandle& InActorRef1, const FPhysicsActorHandle& InActorRef2)
{
	AverageMass = ComputeAverageMass_AssumesLocked(InActorRef1, InActorRef2);
}

void EnsureSleepingActorsStaySleeping_AssumesLocked(const FPhysicsActorHandle& InActorRef1, const FPhysicsActorHandle& InActorRef2)
{
	const bool bActor1Asleep = FPhysicsInterface::IsSleeping(InActorRef1);
	const bool bActor2Asleep = FPhysicsInterface::IsSleeping(InActorRef2);

	// creation of joints wakes up rigid bodies, so we put them to sleep again if both were initially asleep
	if (bActor1Asleep && bActor2Asleep)
	{
		if(FPhysicsInterface::IsValid(InActorRef1) && !FPhysicsInterface::IsKinematic_AssumesLocked(InActorRef1))
		{
			FPhysicsInterface::PutToSleep_AssumesLocked(InActorRef1);
		}

		if(FPhysicsInterface::IsValid(InActorRef2) && !FPhysicsInterface::IsKinematic_AssumesLocked(InActorRef2))
		{
			FPhysicsInterface::PutToSleep_AssumesLocked(InActorRef2);
		}
	}
}

/** 
 *	Create physics engine constraint.
 */
void FConstraintInstance::InitConstraint(FBodyInstance* Body1, FBodyInstance* Body2, float InScale, UObject* DebugOwner, FOnConstraintBroken InConstraintBrokenDelegate)
{
	FPhysicsActorHandle Actor1;
	FPhysicsActorHandle Actor2;

	{
		const bool bValidActors = GetActorRefs(Body1, Body2, Actor1, Actor2, DebugOwner);
		if (!bValidActors)
		{
			return;
		}

		FPhysicsCommand::ExecuteWrite(Actor1, Actor2, [&](const FPhysicsActorHandle& ActorA, const FPhysicsActorHandle& ActorB)
		{
			InitConstraint_AssumesLocked(ActorA, ActorB, InScale, InConstraintBrokenDelegate);
		});
	}

}

void FConstraintInstance::InitConstraint_AssumesLocked(const FPhysicsActorHandle& ActorRef1, const FPhysicsActorHandle& ActorRef2, float InScale, FOnConstraintBroken InConstraintBrokenDelegate)
{
	OnConstraintBrokenDelegate = InConstraintBrokenDelegate;
	LastKnownScale = InScale;

	UserData = FChaosUserData(this);

	// if there's already a constraint, get rid of it first
	if (ConstraintHandle.IsValid())
	{
		TermConstraint();
	}

	if (!CreateJoint_AssumesLocked(ActorRef1, ActorRef2))
	{
		return;
	}
	
	// update mass
	UpdateAverageMass_AssumesLocked(ActorRef1, ActorRef2);

	ProfileInstance.Update_AssumesLocked(ConstraintHandle, AverageMass, bScaleLinearLimits ? LastKnownScale : 1.f);
	EnsureSleepingActorsStaySleeping_AssumesLocked(ActorRef1, ActorRef2);
}

void FConstraintInstance::SetConstraintBrokenDelegate(FOnConstraintBroken InConstraintBrokenDelegate)
{
	OnConstraintBrokenDelegate = InConstraintBrokenDelegate;
}

void FConstraintProfileProperties::Update_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef, float AverageMass, float UseScale) const
{
	// flags and projection settings
	UpdateConstraintFlags_AssumesLocked(InConstraintRef);

	//limits
	LinearLimit.UpdateLinearLimit_AssumesLocked(InConstraintRef, AverageMass, UseScale);
	ConeLimit.UpdateConeLimit_AssumesLocked(InConstraintRef, AverageMass);
	TwistLimit.UpdateTwistLimit_AssumesLocked(InConstraintRef, AverageMass);

	UpdateBreakable_AssumesLocked(InConstraintRef);

	// Motors
	FPhysicsInterface::UpdateLinearDrive_AssumesLocked(InConstraintRef, LinearDrive);
	FPhysicsInterface::UpdateAngularDrive_AssumesLocked(InConstraintRef, AngularDrive);

	// Target
	FPhysicsInterface::UpdateDriveTarget_AssumesLocked(InConstraintRef, LinearDrive, AngularDrive);
}

void FConstraintInstance::TermConstraint()
{
	if (!ConstraintHandle.IsValid())
	{
		return;
	}

	FPhysicsConstraintHandle PhysConstraint = GetPhysicsConstraintRef();

	FPhysicsCommand::ExecuteWrite(PhysConstraint, [&](const FPhysicsConstraintHandle& Constraint)
	{
		FPhysicsInterface::ReleaseConstraint(ConstraintHandle);
	});
}

bool FConstraintInstance::IsTerminated() const
{
	return !ConstraintHandle.IsValid();
}

bool FConstraintInstance::IsValidConstraintInstance() const
{
	return ConstraintHandle.IsValid();
}

void FConstraintInstance::CopyProfilePropertiesFrom(const FConstraintProfileProperties& FromProperties)
{
	ProfileInstance = FromProperties;

	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		ProfileInstance.Update_AssumesLocked(ConstraintHandle, AverageMass, bScaleLinearLimits ? LastKnownScale : 1.0f);
	});
}

const FPhysicsConstraintHandle& FConstraintInstance::GetPhysicsConstraintRef() const
{
	return ConstraintHandle;
}


void FConstraintInstance::CopyConstraintGeometryFrom(const FConstraintInstance* FromInstance)
{
	Pos1 = FromInstance->Pos1;
	PriAxis1 = FromInstance->PriAxis1;
	SecAxis1 = FromInstance->SecAxis1;

	Pos2 = FromInstance->Pos2;
	PriAxis2 = FromInstance->PriAxis2;
	SecAxis2 = FromInstance->SecAxis2;
}

void FConstraintInstance::CopyConstraintParamsFrom(const FConstraintInstance* FromInstance)
{
	check(FromInstance->IsTerminated());
	check(IsTerminated());
	check(FromInstance->PhysScene == nullptr);

	*this = *FromInstance;
}

FTransform FConstraintInstance::GetRefFrame(EConstraintFrame::Type Frame) const
{
	FTransform Result;

	if(Frame == EConstraintFrame::Frame1)
	{
		Result = FTransform(PriAxis1, SecAxis1, PriAxis1 ^ SecAxis1, Pos1);
	}
	else
	{
		Result = FTransform(PriAxis2, SecAxis2, PriAxis2 ^ SecAxis2, Pos2);
	}

	float Error = FMath::Abs( Result.GetDeterminant() - 1.0f );
	if(Error > 0.01f)
	{
		UE_LOG(LogPhysics, Warning,  TEXT("FConstraintInstance::GetRefFrame : Contained scale."));
	}

	return Result;
}

void FConstraintInstance::SetRefFrame(EConstraintFrame::Type Frame, const FTransform& RefFrame)
{
	if(Frame == EConstraintFrame::Frame1)
	{
		Pos1 = RefFrame.GetTranslation();
		PriAxis1 = RefFrame.GetUnitAxis( EAxis::X );
		SecAxis1 = RefFrame.GetUnitAxis( EAxis::Y );
	}
	else
	{
		Pos2 = RefFrame.GetTranslation();
		PriAxis2 = RefFrame.GetUnitAxis( EAxis::X );
		SecAxis2 = RefFrame.GetUnitAxis( EAxis::Y );
	}

	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FPhysicsInterface::SetLocalPose(InUnbrokenConstraint, RefFrame, Frame);
	});
}

void FConstraintInstance::SetRefPosition(EConstraintFrame::Type Frame, const FVector& RefPosition)
{
	if (Frame == EConstraintFrame::Frame1)
	{
		Pos1 = RefPosition;
	}
	else
	{
		Pos2 = RefPosition;
	}

	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FTransform LocalPose = FPhysicsInterface::GetLocalPose(InUnbrokenConstraint, Frame);
		LocalPose.SetLocation(RefPosition);
		FPhysicsInterface::SetLocalPose(InUnbrokenConstraint, LocalPose, Frame);
	});
}

void FConstraintInstance::SetRefOrientation(EConstraintFrame::Type Frame, const FVector& PriAxis, const FVector& SecAxis)
{
	FVector RefPos;
		
	if (Frame == EConstraintFrame::Frame1)
	{
		RefPos = Pos1;
		PriAxis1 = PriAxis;
		SecAxis1 = SecAxis;
	}
	else
	{
		RefPos = Pos2;
		PriAxis2 = PriAxis;
		SecAxis2 = SecAxis;
	}

	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FTransform URefTransform = FTransform(PriAxis, SecAxis, PriAxis ^ SecAxis, RefPos);
		FPhysicsInterface::SetLocalPose(InUnbrokenConstraint, URefTransform, Frame);
	});
}

/** Get the position of this constraint in world space. */
FVector FConstraintInstance::GetConstraintLocation()
{
	return FPhysicsInterface::GetLocation(ConstraintHandle);
}

void FConstraintInstance::GetConstraintForce(FVector& OutLinearForce, FVector& OutAngularForce)
{
	OutLinearForce = FVector::ZeroVector;
	OutAngularForce = FVector::ZeroVector;
	FPhysicsInterface::GetForce(ConstraintHandle, OutLinearForce, OutAngularForce);
}

bool FConstraintInstance::IsBroken()
{
	return FPhysicsInterface::IsBroken(ConstraintHandle);
}

/** Function for turning linear position drive on and off. */
void FConstraintInstance::SetLinearPositionDrive(bool bEnableXDrive, bool bEnableYDrive, bool bEnableZDrive)
{
	ProfileInstance.LinearDrive.SetLinearPositionDrive(bEnableXDrive, bEnableYDrive, bEnableZDrive);

	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [this](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FPhysicsInterface::UpdateLinearDrive_AssumesLocked(InUnbrokenConstraint, ProfileInstance.LinearDrive);
	});
}

/** Function for turning linear velocity drive on and off. */
void FConstraintInstance::SetLinearVelocityDrive(bool bEnableXDrive, bool bEnableYDrive, bool bEnableZDrive)
{
	ProfileInstance.LinearDrive.SetLinearVelocityDrive(bEnableXDrive, bEnableYDrive, bEnableZDrive);

	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [this](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FPhysicsInterface::UpdateLinearDrive_AssumesLocked(InUnbrokenConstraint, ProfileInstance.LinearDrive);
	});
}

void FConstraintInstance::SetOrientationDriveTwistAndSwing(bool InEnableTwistDrive, bool InEnableSwingDrive)
{
	ProfileInstance.AngularDrive.SetOrientationDriveTwistAndSwing(InEnableTwistDrive, InEnableSwingDrive);

	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [this](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FPhysicsInterface::UpdateAngularDrive_AssumesLocked(InUnbrokenConstraint, ProfileInstance.AngularDrive);
	});
}

void FConstraintInstance::SetOrientationDriveSLERP(bool InEnableSLERP)
{
	ProfileInstance.AngularDrive.SetOrientationDriveSLERP(InEnableSLERP);

	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [this](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FPhysicsInterface::UpdateAngularDrive_AssumesLocked(InUnbrokenConstraint, ProfileInstance.AngularDrive);
	});
}

/** Set which twist and swing angular velocity drives are enabled. Only applicable when Twist And Swing drive mode is used */
void FConstraintInstance::SetAngularVelocityDriveTwistAndSwing(bool bInEnableTwistDrive, bool bInEnableSwingDrive)
{
	ProfileInstance.AngularDrive.SetAngularVelocityDriveTwistAndSwing(bInEnableTwistDrive, bInEnableSwingDrive);

	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [this](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FPhysicsInterface::UpdateAngularDrive_AssumesLocked(InUnbrokenConstraint, ProfileInstance.AngularDrive);
	});
}

/** Set whether the SLERP angular velocity drive is enabled. Only applicable when SLERP drive mode is used */
void FConstraintInstance::SetAngularVelocityDriveSLERP(bool bInEnableSLERP)
{
	ProfileInstance.AngularDrive.SetAngularVelocityDriveSLERP(bInEnableSLERP);

	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [this](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FPhysicsInterface::UpdateAngularDrive_AssumesLocked(InUnbrokenConstraint, ProfileInstance.AngularDrive);
	});
}

/** Set the angular drive mode */
void FConstraintInstance::SetAngularDriveMode(EAngularDriveMode::Type DriveMode)
{
	ProfileInstance.AngularDrive.SetAngularDriveMode(DriveMode);

	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [this](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FPhysicsInterface::UpdateAngularDrive_AssumesLocked(InUnbrokenConstraint, ProfileInstance.AngularDrive);
	});
}

/** Function for setting linear position target. */
void FConstraintInstance::SetLinearPositionTarget(const FVector& InPosTarget)
{
	// If settings are the same, don't do anything.
	if( ProfileInstance.LinearDrive.PositionTarget == InPosTarget )
	{
		return;
	}

	ProfileInstance.LinearDrive.PositionTarget = InPosTarget;
	FPhysicsInterface::SetDrivePosition(ConstraintHandle, InPosTarget);
}

/** Function for setting linear velocity target. */
void FConstraintInstance::SetLinearVelocityTarget(const FVector& InVelTarget)
{
	// If settings are the same, don't do anything.
	if (ProfileInstance.LinearDrive.VelocityTarget == InVelTarget)
	{
		return;
	}

	ProfileInstance.LinearDrive.VelocityTarget = InVelTarget;
	FPhysicsInterface::SetDriveLinearVelocity(ConstraintHandle, InVelTarget);
}

/** Function for setting linear motor parameters. */
void FConstraintInstance::SetLinearDriveParams(float InSpring, float InDamping, float InForceLimit)
{
	ProfileInstance.LinearDrive.SetDriveParams(InSpring, InDamping, InForceLimit);

	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [this](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FPhysicsInterface::UpdateLinearDrive_AssumesLocked(InUnbrokenConstraint, ProfileInstance.LinearDrive);
	});
}

/** Function for setting target angular position. */
void FConstraintInstance::SetAngularOrientationTarget(const FQuat& InOrientationTarget)
{
	FRotator OrientationTargetRot(InOrientationTarget);

	// If settings are the same, don't do anything.
	if( ProfileInstance.AngularDrive.OrientationTarget == OrientationTargetRot )
	{
		return;
	}

	ProfileInstance.AngularDrive.OrientationTarget = OrientationTargetRot;
	FPhysicsInterface::SetDriveOrientation(ConstraintHandle, InOrientationTarget);
}

float FConstraintInstance::GetCurrentSwing1() const
{
	return FPhysicsInterface::GetCurrentSwing1(ConstraintHandle);
}

float FConstraintInstance::GetCurrentSwing2() const
{
	return FPhysicsInterface::GetCurrentSwing2(ConstraintHandle);
}

float FConstraintInstance::GetCurrentTwist() const
{
	return FPhysicsInterface::GetCurrentTwist(ConstraintHandle);
}


/** Function for setting target angular velocity. */
void FConstraintInstance::SetAngularVelocityTarget(const FVector& InVelTarget)
{
	// If settings are the same, don't do anything.
	if( ProfileInstance.AngularDrive.AngularVelocityTarget == InVelTarget )
	{
		return;
	}

	ProfileInstance.AngularDrive.AngularVelocityTarget = InVelTarget;
	FPhysicsInterface::SetDriveAngularVelocity(ConstraintHandle, RevolutionsToRads(InVelTarget));
}

/** Function for setting angular motor parameters. */
void FConstraintInstance::SetAngularDriveParams(float InSpring, float InDamping, float InForceLimit)
{
	ProfileInstance.AngularDrive.SetDriveParams(InSpring, InDamping, InForceLimit);

	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FPhysicsInterface::UpdateAngularDrive_AssumesLocked(InUnbrokenConstraint, ProfileInstance.AngularDrive);
	});
}

/** Scale Angular Limit Constraints (as defined in RB_ConstraintSetup) */
void FConstraintInstance::SetAngularDOFLimitScale(float InSwing1LimitScale, float InSwing2LimitScale, float InTwistLimitScale)
{
	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		if ( ProfileInstance.ConeLimit.Swing1Motion == ACM_Limited || ProfileInstance.ConeLimit.Swing2Motion == ACM_Limited )
		{
			// PhysX swing directions are different from Unreal's - so change here.
			if (ProfileInstance.ConeLimit.Swing1Motion == ACM_Limited)
			{
				FPhysicsInterface::SetAngularMotionLimitType_AssumesLocked(InUnbrokenConstraint, ELimitAxis::Swing2, ACM_Limited);
			}

			if (ProfileInstance.ConeLimit.Swing2Motion == ACM_Limited)
			{
				FPhysicsInterface::SetAngularMotionLimitType_AssumesLocked(InUnbrokenConstraint, ELimitAxis::Swing1, ACM_Limited);
			}
		
			//The limit values need to be clamped so it will be valid in PhysX
			float ZLimitAngle = FMath::ClampAngle(ProfileInstance.ConeLimit.Swing1LimitDegrees * InSwing1LimitScale, KINDA_SMALL_NUMBER, 179.9999f) * (PI/180.0f);
			float YLimitAngle = FMath::ClampAngle(ProfileInstance.ConeLimit.Swing2LimitDegrees * InSwing2LimitScale, KINDA_SMALL_NUMBER, 179.9999f) * (PI/180.0f);
			float LimitContactDistance =  FMath::DegreesToRadians(FMath::Max(1.f, ProfileInstance.ConeLimit.ContactDistance * FMath::Min(InSwing1LimitScale, InSwing2LimitScale)));

			FPhysicsInterface::SetSwingLimit(ConstraintHandle, YLimitAngle, ZLimitAngle, LimitContactDistance);
		}

		if ( ProfileInstance.ConeLimit.Swing1Motion  == ACM_Locked )
		{
			FPhysicsInterface::SetAngularMotionLimitType_AssumesLocked(InUnbrokenConstraint, ELimitAxis::Swing2, ACM_Locked);
		}

		if ( ProfileInstance.ConeLimit.Swing2Motion  == ACM_Locked )
		{
			FPhysicsInterface::SetAngularMotionLimitType_AssumesLocked(InUnbrokenConstraint, ELimitAxis::Swing1, ACM_Locked);
		}

		if ( ProfileInstance.TwistLimit.TwistMotion == ACM_Limited )
		{
			FPhysicsInterface::SetAngularMotionLimitType_AssumesLocked(InUnbrokenConstraint, ELimitAxis::Twist, ACM_Limited);

			const float TwistLimitRad	= ProfileInstance.TwistLimit.TwistLimitDegrees * InTwistLimitScale * (PI/180.0f);
			float LimitContactDistance = FMath::DegreesToRadians(FMath::Max(1.f, ProfileInstance.ConeLimit.ContactDistance * InTwistLimitScale));

			FPhysicsInterface::SetTwistLimit(ConstraintHandle, -TwistLimitRad, TwistLimitRad, LimitContactDistance);
		}
		else if ( ProfileInstance.TwistLimit.TwistMotion == ACM_Locked )
		{
			FPhysicsInterface::SetAngularMotionLimitType_AssumesLocked(InUnbrokenConstraint, ELimitAxis::Twist, ACM_Locked);
		}
	});
}

/** Allows you to dynamically change the size of the linear limit 'sphere'. */
void FConstraintInstance::SetLinearLimitSize(float NewLimitSize)
{
	//TODO: Is this supposed to be scaling the linear limit? The code just sets it directly.
#if WITH_PHYSX
	FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& InUnbrokenConstraint)
	{
		FPhysicsInterface::SetLinearLimit(ConstraintHandle, NewLimitSize);
	});
#endif
}

bool FConstraintInstance::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);
	Ar.UsingCustomVersion(FAnimPhysObjectVersion::GUID);
	return false;	//We only have this function to mark custom GUID. Still want serialize tagged properties
}

#if WITH_EDITORONLY_DATA
void FConstraintInstance::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_FIXUP_STIFFNESS_AND_DAMPING_SCALE)
	{
		LinearLimitStiffness_DEPRECATED		/= CVarConstraintAngularStiffnessScale.GetValueOnGameThread();
		SwingLimitStiffness_DEPRECATED		/= CVarConstraintAngularStiffnessScale.GetValueOnGameThread();
		TwistLimitStiffness_DEPRECATED		/= CVarConstraintAngularStiffnessScale.GetValueOnGameThread();
		LinearLimitDamping_DEPRECATED		/=  CVarConstraintAngularDampingScale.GetValueOnGameThread();
		SwingLimitDamping_DEPRECATED		/=  CVarConstraintAngularDampingScale.GetValueOnGameThread();
		TwistLimitDamping_DEPRECATED		/=  CVarConstraintAngularDampingScale.GetValueOnGameThread();
	}

	if (Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_FIXUP_MOTOR_UNITS)
	{
		AngularVelocityTarget_DEPRECATED *= 1.f / (2.f * PI);	//we want to use revolutions per second - old system was using radians directly
	}

	if (Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_CONSTRAINT_INSTANCE_MOTOR_FLAGS)
	{
		bLinearXVelocityDrive_DEPRECATED = LinearVelocityTarget_DEPRECATED.X != 0.f;
		bLinearYVelocityDrive_DEPRECATED = LinearVelocityTarget_DEPRECATED.Y != 0.f;
		bLinearZVelocityDrive_DEPRECATED = LinearVelocityTarget_DEPRECATED.Z != 0.f;
	}
	
	if (Ar.IsLoading() && Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::ConstraintInstanceBehaviorParameters)
	{
		//Need to move all the deprecated properties into the new profile struct
		ProfileInstance.bDisableCollision = bDisableCollision_DEPRECATED;
		ProfileInstance.bEnableProjection = bEnableProjection_DEPRECATED;
		ProfileInstance.ProjectionLinearTolerance = ProjectionLinearTolerance_DEPRECATED;
		ProfileInstance.ProjectionAngularTolerance = ProjectionAngularTolerance_DEPRECATED;
		ProfileInstance.LinearLimit.XMotion = LinearXMotion_DEPRECATED;
		ProfileInstance.LinearLimit.YMotion = LinearYMotion_DEPRECATED;
		ProfileInstance.LinearLimit.ZMotion = LinearZMotion_DEPRECATED;
		ProfileInstance.LinearLimit.Limit = LinearLimitSize_DEPRECATED;
		ProfileInstance.LinearLimit.bSoftConstraint = bLinearLimitSoft_DEPRECATED;
		ProfileInstance.LinearLimit.Stiffness = LinearLimitStiffness_DEPRECATED;
		ProfileInstance.LinearLimit.Damping = LinearLimitDamping_DEPRECATED;
		ProfileInstance.bLinearBreakable = bLinearBreakable_DEPRECATED;
		ProfileInstance.LinearBreakThreshold = LinearBreakThreshold_DEPRECATED;
		ProfileInstance.ConeLimit.Swing1Motion = AngularSwing1Motion_DEPRECATED;
		ProfileInstance.TwistLimit.TwistMotion = AngularTwistMotion_DEPRECATED;
		ProfileInstance.ConeLimit.Swing2Motion = AngularSwing2Motion_DEPRECATED;
		ProfileInstance.ConeLimit.bSoftConstraint = bSwingLimitSoft_DEPRECATED;
		ProfileInstance.TwistLimit.bSoftConstraint = bTwistLimitSoft_DEPRECATED;
		ProfileInstance.ConeLimit.Swing1LimitDegrees = Swing1LimitAngle_DEPRECATED;
		ProfileInstance.TwistLimit.TwistLimitDegrees = TwistLimitAngle_DEPRECATED;
		ProfileInstance.ConeLimit.Swing2LimitDegrees = Swing2LimitAngle_DEPRECATED;
		ProfileInstance.ConeLimit.Stiffness = SwingLimitStiffness_DEPRECATED;
		ProfileInstance.ConeLimit.Damping = SwingLimitDamping_DEPRECATED;
		ProfileInstance.TwistLimit.Stiffness = TwistLimitStiffness_DEPRECATED;
		ProfileInstance.TwistLimit.Damping = TwistLimitDamping_DEPRECATED;
		ProfileInstance.bAngularBreakable = bAngularBreakable_DEPRECATED;
		ProfileInstance.AngularBreakThreshold = AngularBreakThreshold_DEPRECATED;

		//we no longer have a single control for all linear axes. If it was off we ensure all individual drives are off. If it's on we just leave things alone.
		//This loses a bit of info, but the ability to toggle drives on and off at runtime was very obfuscated so hopefuly this doesn't hurt too many people. They can still toggle individual drives on and off
		ProfileInstance.LinearDrive.XDrive.bEnablePositionDrive = bLinearXPositionDrive_DEPRECATED && bLinearPositionDrive_DEPRECATED;
		ProfileInstance.LinearDrive.XDrive.bEnableVelocityDrive = bLinearXVelocityDrive_DEPRECATED && bLinearVelocityDrive_DEPRECATED;
		ProfileInstance.LinearDrive.YDrive.bEnablePositionDrive = bLinearYPositionDrive_DEPRECATED && bLinearPositionDrive_DEPRECATED;
		ProfileInstance.LinearDrive.YDrive.bEnableVelocityDrive = bLinearYVelocityDrive_DEPRECATED && bLinearVelocityDrive_DEPRECATED;
		ProfileInstance.LinearDrive.ZDrive.bEnablePositionDrive = bLinearZPositionDrive_DEPRECATED && bLinearPositionDrive_DEPRECATED;
		ProfileInstance.LinearDrive.ZDrive.bEnableVelocityDrive = bLinearZVelocityDrive_DEPRECATED && bLinearVelocityDrive_DEPRECATED;
		
		ProfileInstance.LinearDrive.PositionTarget = LinearPositionTarget_DEPRECATED;
		ProfileInstance.LinearDrive.VelocityTarget = LinearVelocityTarget_DEPRECATED;

		//Linear drives now set settings per axis so duplicate old data
		ProfileInstance.LinearDrive.XDrive.Stiffness = LinearDriveSpring_DEPRECATED;
		ProfileInstance.LinearDrive.YDrive.Stiffness = LinearDriveSpring_DEPRECATED;
		ProfileInstance.LinearDrive.ZDrive.Stiffness = LinearDriveSpring_DEPRECATED;
		ProfileInstance.LinearDrive.XDrive.Damping = LinearDriveDamping_DEPRECATED;
		ProfileInstance.LinearDrive.YDrive.Damping = LinearDriveDamping_DEPRECATED;
		ProfileInstance.LinearDrive.ZDrive.Damping = LinearDriveDamping_DEPRECATED;
		ProfileInstance.LinearDrive.XDrive.MaxForce = LinearDriveForceLimit_DEPRECATED;
		ProfileInstance.LinearDrive.YDrive.MaxForce = LinearDriveForceLimit_DEPRECATED;
		ProfileInstance.LinearDrive.ZDrive.MaxForce = LinearDriveForceLimit_DEPRECATED;

		//We now expose twist swing and slerp drive directly. In the old system you had a single switch, but then there was also special switches for disabling twist and swing
		//Technically someone COULD disable these, but they are not exposed in editor so it seems very unlikely. So if they are true and angular orientation is false we override it
		ProfileInstance.AngularDrive.SwingDrive.bEnablePositionDrive = bEnableSwingDrive_DEPRECATED && bAngularOrientationDrive_DEPRECATED;
		ProfileInstance.AngularDrive.SwingDrive.bEnableVelocityDrive = bEnableSwingDrive_DEPRECATED && bAngularVelocityDrive_DEPRECATED;
		ProfileInstance.AngularDrive.TwistDrive.bEnablePositionDrive = bEnableTwistDrive_DEPRECATED && bAngularOrientationDrive_DEPRECATED;
		ProfileInstance.AngularDrive.TwistDrive.bEnableVelocityDrive = bEnableTwistDrive_DEPRECATED && bAngularVelocityDrive_DEPRECATED;
		ProfileInstance.AngularDrive.SlerpDrive.bEnablePositionDrive = bAngularOrientationDrive_DEPRECATED;
		ProfileInstance.AngularDrive.SlerpDrive.bEnableVelocityDrive = bAngularVelocityDrive_DEPRECATED;

		ProfileInstance.AngularDrive.AngularDriveMode = AngularDriveMode_DEPRECATED;
		ProfileInstance.AngularDrive.OrientationTarget = AngularOrientationTarget_DEPRECATED;
		ProfileInstance.AngularDrive.AngularVelocityTarget = AngularVelocityTarget_DEPRECATED;

		//duplicate drive spring data into all 3 drives
		ProfileInstance.AngularDrive.SwingDrive.Stiffness = AngularDriveSpring_DEPRECATED;
		ProfileInstance.AngularDrive.TwistDrive.Stiffness = AngularDriveSpring_DEPRECATED;
		ProfileInstance.AngularDrive.SlerpDrive.Stiffness = AngularDriveSpring_DEPRECATED;
		ProfileInstance.AngularDrive.SwingDrive.Damping = AngularDriveDamping_DEPRECATED;
		ProfileInstance.AngularDrive.TwistDrive.Damping = AngularDriveDamping_DEPRECATED;
		ProfileInstance.AngularDrive.SlerpDrive.Damping = AngularDriveDamping_DEPRECATED;
		ProfileInstance.AngularDrive.SwingDrive.MaxForce = AngularDriveForceLimit_DEPRECATED;
		ProfileInstance.AngularDrive.TwistDrive.MaxForce = AngularDriveForceLimit_DEPRECATED;
		ProfileInstance.AngularDrive.SlerpDrive.MaxForce = AngularDriveForceLimit_DEPRECATED;

	}

	if (Ar.IsLoading() && Ar.CustomVer(FAnimPhysObjectVersion::GUID) < FAnimPhysObjectVersion::TuneSoftLimitStiffnessAndDamping)
	{
		//Handle the fact that 0,0 used to mean hard limit, but now means free
		if(ProfileInstance.LinearLimit.Stiffness == 0.f && ProfileInstance.LinearLimit.Damping == 0.f)
		{
			ProfileInstance.LinearLimit.bSoftConstraint = false;
		}

		if (ProfileInstance.ConeLimit.Stiffness == 0.f && ProfileInstance.ConeLimit.Damping == 0.f)
		{
			ProfileInstance.ConeLimit.bSoftConstraint = false;
		}

		if (ProfileInstance.TwistLimit.Stiffness == 0.f && ProfileInstance.TwistLimit.Damping == 0.f)
		{
			ProfileInstance.TwistLimit.bSoftConstraint = false;
		}

		//Now handle the new linear spring stiffness and damping coefficient
		if(CVarConstraintAngularStiffnessScale.GetValueOnGameThread() > 0.f)
		{
			ProfileInstance.LinearLimit.Stiffness *= CVarConstraintAngularStiffnessScale.GetValueOnGameThread() / CVarConstraintLinearStiffnessScale.GetValueOnGameThread();
		}

		if (CVarConstraintAngularDampingScale.GetValueOnGameThread() > 0.f)
		{
			ProfileInstance.LinearLimit.Damping *= CVarConstraintAngularDampingScale.GetValueOnGameThread() / CVarConstraintLinearDampingScale.GetValueOnGameThread();
		}
	}
}
#endif

//Hacks to easily get zeroed memory for special case when we don't use GC
void FConstraintInstance::Free(FConstraintInstance * Ptr)
{
	Ptr->~FConstraintInstance();
	FMemory::Free(Ptr);
}
FConstraintInstance * FConstraintInstance::Alloc()
{
	void* Memory = FMemory::Malloc(sizeof(FConstraintInstance));
	FMemory::Memzero(Memory, sizeof(FConstraintInstance));
	return new (Memory)FConstraintInstance();
}

void FConstraintInstance::EnableProjection()
{
	ProfileInstance.bEnableProjection = true;
	
	FPhysicsCommand::ExecuteWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& Constraint)
	{
		FPhysicsInterface::SetProjectionEnabled_AssumesLocked(Constraint, true, ProfileInstance.ProjectionLinearTolerance, ProfileInstance.ProjectionAngularTolerance);
	});
}

void FConstraintInstance::DisableProjection()
{
	ProfileInstance.bEnableProjection = false;
	
	FPhysicsCommand::ExecuteWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& Constraint)
	{
		FPhysicsInterface::SetProjectionEnabled_AssumesLocked(Constraint, false);
	});
}

void FConstraintInstance::EnableParentDominates()
{
	ProfileInstance.bParentDominates = true;
	
	FPhysicsCommand::ExecuteWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& Constraint)
	{
		FPhysicsInterface::SetParentDominates_AssumesLocked(Constraint, true);
	});
}

void FConstraintInstance::DisableParentDominates()
{
	ProfileInstance.bParentDominates = false;
	
	FPhysicsCommand::ExecuteWrite(ConstraintHandle, [&](const FPhysicsConstraintHandle& Constraint)
	{
		FPhysicsInterface::SetParentDominates_AssumesLocked(Constraint, false);
	});
}


#undef LOCTEXT_NAMESPACE
