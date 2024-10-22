// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_CHAOS

#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Physics/Experimental/ChaosInterfaceUtils.h"
#include "Physics/PhysicsInterfaceTypes.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "Templates/UniquePtr.h"

#include "PhysicsSolver.h"
#include "Chaos/Box.h"
#include "Chaos/Cylinder.h"
#include "Chaos/TaperedCylinder.h"
#include "Chaos/Capsule.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Levelset.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Sphere.h"
#include "Chaos/Matrix.h"
#include "Chaos/MassProperties.h"
#include "ChaosSolversModule.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/Convex.h"
#include "Chaos/GeometryQueries.h"
#include "Chaos/Plane.h"
#include "ChaosCheck.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDJointConstraints.h"

#include "Async/ParallelFor.h"
#include "Collision/CollisionConversions.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Math/UnrealMathUtility.h"
#include "PBDRigidsSolver.h"
#include "Physics/PhysicsFiltering.h"
#include "PhysicsInterfaceUtilsCore.h"
#include "PhysicalMaterials/PhysicalMaterialMask.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

#if PHYSICS_INTERFACE_PHYSX
#include "geometry/PxConvexMesh.h"
#include "geometry/PxTriangleMesh.h"
#include "foundation/PxVec3.h"
#include "extensions/PxMassProperties.h"
#include "Containers/ArrayView.h"
#endif

DEFINE_STAT(STAT_TotalPhysicsTime);
DEFINE_STAT(STAT_NumCloths);
DEFINE_STAT(STAT_NumClothVerts);

DECLARE_CYCLE_STAT(TEXT("Start Physics Time (sync)"), STAT_PhysicsKickOffDynamicsTime, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("Fetch Results Time (sync)"), STAT_PhysicsFetchDynamicsTime, STATGROUP_Physics);

DECLARE_CYCLE_STAT(TEXT("Start Physics Time (async)"), STAT_PhysicsKickOffDynamicsTime_Async, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("Fetch Results Time (async)"), STAT_PhysicsFetchDynamicsTime_Async, STATGROUP_Physics);

DECLARE_CYCLE_STAT(TEXT("Update Kinematics On Deferred SkelMeshes"), STAT_UpdateKinematicsOnDeferredSkelMeshes, STATGROUP_Physics);

DECLARE_CYCLE_STAT(TEXT("Phys Events Time"), STAT_PhysicsEventTime, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("SyncComponentsToBodies (sync)"), STAT_SyncComponentsToBodies, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("SyncComponentsToBodies (async)"), STAT_SyncComponentsToBodies_Async, STATGROUP_Physics);
DECLARE_CYCLE_STAT(TEXT("Query PhysicalMaterialMask Hit"), STAT_QueryPhysicalMaterialMaskHit, STATGROUP_Physics);

DECLARE_DWORD_COUNTER_STAT(TEXT("Broadphase Adds"), STAT_NumBroadphaseAdds, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Broadphase Removes"), STAT_NumBroadphaseRemoves, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Active Constraints"), STAT_NumActiveConstraints, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Active Simulated Bodies"), STAT_NumActiveSimulatedBodies, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Active Kinematic Bodies"), STAT_NumActiveKinematicBodies, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Mobile Bodies"), STAT_NumMobileBodies, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Static Bodies"), STAT_NumStaticBodies, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("Shapes"), STAT_NumShapes, STATGROUP_Physics);

DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Broadphase Adds"), STAT_NumBroadphaseAddsAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Broadphase Removes"), STAT_NumBroadphaseRemovesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Active Constraints"), STAT_NumActiveConstraintsAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Active Simulated Bodies"), STAT_NumActiveSimulatedBodiesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Active Kinematic Bodies"), STAT_NumActiveKinematicBodiesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Mobile Bodies"), STAT_NumMobileBodiesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Static Bodies"), STAT_NumStaticBodiesAsync, STATGROUP_Physics);
DECLARE_DWORD_COUNTER_STAT(TEXT("(ASync) Shapes"), STAT_NumShapesAsync, STATGROUP_Physics);

ECollisionShapeType GetGeometryType(const Chaos::FPerShapeData& Shape)
{
	return GetType(*Shape.GetGeometry());
}

Chaos::FChaosPhysicsMaterial* GetMaterialFromInternalFaceIndex(const FPhysicsShape& Shape, const FPhysicsActor& Actor, uint32 InternalFaceIndex)
{
	const auto& Materials = Shape.GetMaterials();
	if(Materials.Num() > 0 && Actor.GetProxy())
	{
		Chaos::FPBDRigidsSolver* Solver = Actor.GetProxy()->GetSolver<Chaos::FPBDRigidsSolver>();

		if(ensure(Solver))
		{
			if(Materials.Num() == 1)
			{
				Chaos::TSolverQueryMaterialScope<Chaos::ELockType::Read> Scope(Solver);
				return Solver->GetQueryMaterials().Get(Materials[0].InnerHandle);
			}

			uint8 Index = Shape.GetGeometry()->GetMaterialIndex(InternalFaceIndex);

			if(Materials.IsValidIndex(Index))
			{
				Chaos::TSolverQueryMaterialScope<Chaos::ELockType::Read> Scope(Solver);
				return Solver->GetQueryMaterials().Get(Materials[Index].InnerHandle);
			}
		}
	}

	return nullptr;
}

Chaos::FChaosPhysicsMaterial* GetMaterialFromInternalFaceIndexAndHitLocation(const FPhysicsShape& Shape, const FPhysicsActor& Actor, uint32 InternalFaceIndex, const FVector& HitLocation)
{
	{
		SCOPE_CYCLE_COUNTER(STAT_QueryPhysicalMaterialMaskHit);

		if (Shape.GetMaterials().Num() > 0 && Actor.GetProxy())
		{
			Chaos::FPBDRigidsSolver* Solver = Actor.GetProxy()->GetSolver<Chaos::FPBDRigidsSolver>();

			if (ensure(Solver))
			{
				if (Shape.GetMaterialMasks().Num() > 0)
				{
					UBodySetup* BodySetup = nullptr;

					if (const FBodyInstance* BodyInst = GetUserData(Actor))
					{
						BodyInst = FPhysicsInterface::ShapeToOriginalBodyInstance(BodyInst, &Shape);
						BodySetup = BodyInst->GetBodySetup();	//this data should be immutable at runtime so ok to check from worker thread.
						ECollisionShapeType GeomType = GetGeometryType(Shape);

						if (BodySetup && BodySetup->bSupportUVsAndFaceRemap && GetGeometryType(Shape) == ECollisionShapeType::Trimesh)
						{
							FVector Scale(1.0f, 1.0f, 1.0f);
							const Chaos::FImplicitObject* Geometry = Shape.GetGeometry().Get();
							if (const Chaos::TImplicitObjectScaled<Chaos::FTriangleMeshImplicitObject>* ScaledTrimesh = Chaos::TImplicitObjectScaled<Chaos::FTriangleMeshImplicitObject>::AsScaled(*Geometry))
							{
								Scale = ScaledTrimesh->GetScale();
							}

							// Convert hit location to local
							Chaos::FRigidTransform3 ActorToWorld(Actor.X(), Actor.R(), Scale);
							const FVector LocalHitPos = ActorToWorld.InverseTransformPosition(HitLocation);

							uint8 Index = Shape.GetGeometry()->GetMaterialIndex(InternalFaceIndex);
							if (Shape.GetMaterialMasks().IsValidIndex(Index))
							{
								Chaos::FChaosPhysicsMaterialMask* Mask = nullptr;
								{
									Chaos::TSolverQueryMaterialScope<Chaos::ELockType::Read> Scope(Solver);
									Mask = Solver->GetQueryMaterialMasks().Get(Shape.GetMaterialMasks()[Index].InnerHandle);
								}

								if (Mask && InternalFaceIndex < (uint32)BodySetup->FaceRemap.Num())
								{
									int32 RemappedFaceIndex = BodySetup->FaceRemap[InternalFaceIndex];
									FVector2D UV;


									if (BodySetup->CalcUVAtLocation(LocalHitPos, RemappedFaceIndex, Mask->UVChannelIndex, UV))
									{
										uint32 MapIdx = UPhysicalMaterialMask::GetPhysMatIndex(Mask->MaskData, Mask->SizeX, Mask->SizeY, Mask->AddressX, Mask->AddressY, UV.X, UV.Y);
										uint32 AdjustedMapIdx = Index * EPhysicalMaterialMaskColor::MAX + MapIdx;
										if (Shape.GetMaterialMaskMaps().IsValidIndex(AdjustedMapIdx))
										{
											uint32 MaterialIdx = Shape.GetMaterialMaskMaps()[AdjustedMapIdx];
											if (Shape.GetMaterialMaskMapMaterials().IsValidIndex(MaterialIdx))
											{
												Chaos::TSolverQueryMaterialScope<Chaos::ELockType::Read> Scope(Solver);
												return Solver->GetQueryMaterials().Get(Shape.GetMaterialMaskMapMaterials()[MaterialIdx].InnerHandle);
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	return GetMaterialFromInternalFaceIndex(Shape, Actor, InternalFaceIndex);
}

FPhysInterface_Chaos::FPhysInterface_Chaos(const AWorldSettings* Settings) 
{

}

FPhysInterface_Chaos::~FPhysInterface_Chaos()
{
}

FPhysicsMaterialMaskHandle FPhysInterface_Chaos::CreateMaterialMask(const UPhysicalMaterialMask* InMaterialMask)
{
	Chaos::FMaterialMaskHandle NewHandle = Chaos::FPhysicalMaterialManager::Get().CreateMask();
	FPhysInterface_Chaos::UpdateMaterialMask(NewHandle, InMaterialMask);
	return NewHandle;
}

void FPhysInterface_Chaos::UpdateMaterialMask(FPhysicsMaterialMaskHandle& InHandle, const UPhysicalMaterialMask* InMaterialMask)
{
	if (Chaos::FChaosPhysicsMaterialMask* MaterialMask = InHandle.Get())
	{
		InMaterialMask->GenerateMaskData(MaterialMask->MaskData, MaterialMask->SizeX, MaterialMask->SizeY);
		MaterialMask->UVChannelIndex = InMaterialMask->UVChannelIndex;
		MaterialMask->AddressX = static_cast<int32>(InMaterialMask->AddressX);
		MaterialMask->AddressY = static_cast<int32>(InMaterialMask->AddressY);
	}

	Chaos::FPhysicalMaterialManager::Get().UpdateMaterialMask(InHandle);
}

bool FPhysInterface_Chaos::IsInScene(const FPhysicsActorHandle& InActorReference)
{
	return (GetCurrentScene(InActorReference) != nullptr);
}

void FPhysInterface_Chaos::FlushScene(FPhysScene* InScene)
{
	FPhysicsCommand::ExecuteWrite(InScene, [&]()
	{
		InScene->Flush_AssumesLocked();
	});
}

Chaos::EJointMotionType ConvertMotionType(ELinearConstraintMotion InEngineType)
{
	if (InEngineType == ELinearConstraintMotion::LCM_Free)
		return Chaos::EJointMotionType::Free;
	else if (InEngineType == ELinearConstraintMotion::LCM_Limited)
		return Chaos::EJointMotionType::Limited;
	else if (InEngineType == ELinearConstraintMotion::LCM_Locked)
		return Chaos::EJointMotionType::Locked;
	else
		ensure(false);
	return Chaos::EJointMotionType::Locked;
};

void FPhysInterface_Chaos::SetLinearMotionLimitType_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef, PhysicsInterfaceTypes::ELimitAxis InAxis, ELinearConstraintMotion InMotion)
{
	if (InConstraintRef.IsValid())
	{
		if (Chaos::FJointConstraint* Constraint = InConstraintRef.Constraint)
		{
			switch (InAxis)
			{
			case PhysicsInterfaceTypes::ELimitAxis::X:
				Constraint->SetLinearMotionTypesX(ConvertMotionType(InMotion));
				break;

			case PhysicsInterfaceTypes::ELimitAxis::Y:
				Constraint->SetLinearMotionTypesY(ConvertMotionType(InMotion));
				break;

			case PhysicsInterfaceTypes::ELimitAxis::Z:
				Constraint->SetLinearMotionTypesZ(ConvertMotionType(InMotion));
				break;
			default:
				ensure(false);
			}
		}
	}
}

Chaos::EJointMotionType ConvertMotionType(EAngularConstraintMotion InEngineType)
{
	if (InEngineType == EAngularConstraintMotion::ACM_Free)
		return Chaos::EJointMotionType::Free;
	else if (InEngineType == EAngularConstraintMotion::ACM_Limited)
		return Chaos::EJointMotionType::Limited;
	else if (InEngineType == EAngularConstraintMotion::ACM_Locked)
		return Chaos::EJointMotionType::Locked;
	else
		ensure(false);
	return Chaos::EJointMotionType::Locked;
};


void FPhysInterface_Chaos::SetAngularMotionLimitType_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef, PhysicsInterfaceTypes::ELimitAxis InAxis, EAngularConstraintMotion InMotion)
{
	if (InConstraintRef.IsValid())
	{
		if (Chaos::FJointConstraint* Constraint = InConstraintRef.Constraint)
		{
			switch (InAxis)
			{
			case PhysicsInterfaceTypes::ELimitAxis::Twist:
				Constraint->SetAngularMotionTypesX(ConvertMotionType(InMotion));
				break;

			case PhysicsInterfaceTypes::ELimitAxis::Swing1:
				Constraint->SetAngularMotionTypesY(ConvertMotionType(InMotion));
				break;

			case PhysicsInterfaceTypes::ELimitAxis::Swing2:
				Constraint->SetAngularMotionTypesZ(ConvertMotionType(InMotion));
				break;
			default:
				ensure(false);
			}
		}
	}
}

void FPhysInterface_Chaos::UpdateLinearLimitParams_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef, float InLimit, float InAverageMass, const FLinearConstraint& InParams)
{
	if (InConstraintRef.IsValid())
	{
		if (Chaos::FJointConstraint* Constraint = InConstraintRef.Constraint)
		{
			Constraint->SetLinearLimit(InLimit); 

			Constraint->SetSoftLinearLimitsEnabled(InParams.bSoftConstraint);
			Constraint->SetSoftLinearStiffness(InParams.Stiffness);
			Constraint->SetSoftLinearDamping(InParams.Damping);
			Constraint->SetLinearContactDistance(InParams.ContactDistance);
			Constraint->SetLinearRestitution(InParams.Restitution);
			//Constraint->SetAngularSoftForceMode( InParams.NOT_QUITE_SURE ); // @todo(chaos) 
		}
	}
}

void FPhysInterface_Chaos::UpdateConeLimitParams_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef, float InAverageMass, const FConeConstraint& InParams)
{
	if (InConstraintRef.IsValid())
	{
		if (Chaos::FJointConstraint* Constraint = InConstraintRef.Constraint)
		{
			Chaos::FVec3 Limit = Constraint->GetAngularLimits();
			Limit[(int32)Chaos::EJointAngularConstraintIndex::Swing1] = FMath::DegreesToRadians(InParams.Swing1LimitDegrees);
			Limit[(int32)Chaos::EJointAngularConstraintIndex::Swing2] = FMath::DegreesToRadians(InParams.Swing2LimitDegrees);
			Constraint->SetAngularLimits(Limit);

			Constraint->SetSoftSwingLimitsEnabled(InParams.bSoftConstraint);
			Constraint->SetSoftSwingStiffness(InParams.Stiffness);
			Constraint->SetSoftSwingDamping(InParams.Damping);
			Constraint->SetSwingContactDistance(InParams.ContactDistance);
			Constraint->SetSwingRestitution(InParams.Restitution);
			//Constraint->SetAngularSoftForceMode( InParams.NOT_QUITE_SURE ); // @todo(chaos) 
		}
	}
}

void FPhysInterface_Chaos::UpdateTwistLimitParams_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef, float InAverageMass, const FTwistConstraint& InParams)
{
	if (InConstraintRef.IsValid())
	{
		if (Chaos::FJointConstraint* Constraint = InConstraintRef.Constraint)
		{
			Chaos::FVec3 Limit = Constraint->GetAngularLimits();
			Limit[(int32)Chaos::EJointAngularConstraintIndex::Twist] = FMath::DegreesToRadians(InParams.TwistLimitDegrees);
			Constraint->SetAngularLimits(Limit);

			Constraint->SetSoftTwistLimitsEnabled(InParams.bSoftConstraint);
			Constraint->SetSoftTwistStiffness(InParams.Stiffness);
			Constraint->SetSoftTwistDamping(InParams.Damping);
			Constraint->SetTwistContactDistance(InParams.ContactDistance);
			Constraint->SetTwistRestitution(InParams.Restitution);
			//Constraint->SetAngularSoftForceMode( InParams.NOT_QUITE_SURE ); // @todo(chaos) 

		}
	}
}

void FPhysInterface_Chaos::UpdateLinearDrive_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef, const FLinearDriveConstraint& InDriveParams)
{
	if (InConstraintRef.IsValid())
	{
		if (Chaos::FJointConstraint* Constraint = InConstraintRef.Constraint)
		{
			Constraint->SetLinearPositionDriveXEnabled(false);
			Constraint->SetLinearPositionDriveYEnabled(false);
			Constraint->SetLinearPositionDriveZEnabled(false);

			Constraint->SetLinearVelocityDriveXEnabled(false);
			Constraint->SetLinearVelocityDriveYEnabled(false);
			Constraint->SetLinearVelocityDriveZEnabled(false);

			bool bPositionDriveEnabled = InDriveParams.IsPositionDriveEnabled();
			if (bPositionDriveEnabled)
			{
				Constraint->SetLinearPositionDriveXEnabled(InDriveParams.XDrive.bEnablePositionDrive);
				Constraint->SetLinearPositionDriveYEnabled(InDriveParams.YDrive.bEnablePositionDrive);
				Constraint->SetLinearPositionDriveZEnabled(InDriveParams.ZDrive.bEnablePositionDrive);
				Constraint->SetLinearDrivePositionTarget(InDriveParams.PositionTarget);
			}

			bool bVelocityDriveEnabled = InDriveParams.IsVelocityDriveEnabled();
			if (bVelocityDriveEnabled)
			{
				Constraint->SetLinearVelocityDriveXEnabled(InDriveParams.XDrive.bEnableVelocityDrive);
				Constraint->SetLinearVelocityDriveYEnabled(InDriveParams.YDrive.bEnableVelocityDrive);
				Constraint->SetLinearVelocityDriveZEnabled(InDriveParams.ZDrive.bEnableVelocityDrive);
				Constraint->SetLinearDriveVelocityTarget(InDriveParams.VelocityTarget);
			}

			Constraint->SetLinearDriveForceMode(Chaos::EJointForceMode::Acceleration);
			Constraint->SetLinearDriveStiffness(FMath::Max3(InDriveParams.XDrive.Stiffness, InDriveParams.YDrive.Stiffness, InDriveParams.ZDrive.Stiffness));
			Constraint->SetLinearDriveDamping(FMath::Max3(InDriveParams.XDrive.Damping, InDriveParams.YDrive.Damping, InDriveParams.ZDrive.Damping));
		}
	}
}

void FPhysInterface_Chaos::UpdateAngularDrive_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef, const FAngularDriveConstraint& InDriveParams)
{
	if (InConstraintRef.IsValid())
	{
		if (Chaos::FJointConstraint* Constraint = InConstraintRef.Constraint)
		{
			Constraint->SetAngularSLerpPositionDriveEnabled(false);
			Constraint->SetAngularTwistPositionDriveEnabled(false);
			Constraint->SetAngularSwingPositionDriveEnabled(false);

			Constraint->SetAngularSLerpVelocityDriveEnabled(false);
			Constraint->SetAngularTwistVelocityDriveEnabled(false);
			Constraint->SetAngularSwingVelocityDriveEnabled(false);

			bool bPositionDriveEnabled = InDriveParams.IsOrientationDriveEnabled();
			if (bPositionDriveEnabled)
			{
				if (InDriveParams.AngularDriveMode == EAngularDriveMode::TwistAndSwing)
				{
					Constraint->SetAngularTwistPositionDriveEnabled(InDriveParams.TwistDrive.bEnablePositionDrive);
					Constraint->SetAngularSwingPositionDriveEnabled(InDriveParams.SwingDrive.bEnablePositionDrive);
				}
				else
				{
					Constraint->SetAngularSLerpPositionDriveEnabled(InDriveParams.SlerpDrive.bEnablePositionDrive);
				}

				Constraint->SetAngularDrivePositionTarget(Chaos::FRotation3(InDriveParams.OrientationTarget.Quaternion()));
			}

			bool bVelocityDriveEnabled = InDriveParams.IsVelocityDriveEnabled();
			if (bVelocityDriveEnabled)
			{
				if (InDriveParams.AngularDriveMode == EAngularDriveMode::TwistAndSwing)
				{
					Constraint->SetAngularTwistVelocityDriveEnabled(InDriveParams.TwistDrive.bEnableVelocityDrive);
					Constraint->SetAngularSwingVelocityDriveEnabled(InDriveParams.SwingDrive.bEnableVelocityDrive);
				}
				else
				{
					Constraint->SetAngularSLerpVelocityDriveEnabled(InDriveParams.SlerpDrive.bEnableVelocityDrive);
				}

				Constraint->SetAngularDriveVelocityTarget(InDriveParams.AngularVelocityTarget);
			}

			Constraint->SetAngularDriveForceMode(Chaos::EJointForceMode::Acceleration);
			Constraint->SetAngularDriveStiffness(FMath::Max3(InDriveParams.SlerpDrive.Stiffness, InDriveParams.TwistDrive.Stiffness, InDriveParams.SwingDrive.Stiffness));
			Constraint->SetAngularDriveDamping(FMath::Max3(InDriveParams.SlerpDrive.Damping, InDriveParams.TwistDrive.Damping, InDriveParams.SwingDrive.Damping));
		}
	}
}

void FPhysInterface_Chaos::UpdateDriveTarget_AssumesLocked(const FPhysicsConstraintHandle& InConstraintRef, const FLinearDriveConstraint& InLinDrive, const FAngularDriveConstraint& InAngDrive)
{
	if (InConstraintRef.IsValid())
	{
		UpdateLinearDrive_AssumesLocked(InConstraintRef, InLinDrive);
		UpdateAngularDrive_AssumesLocked(InConstraintRef, InAngDrive);
	}
}



enum class EPhysicsInterfaceScopedLockType : uint8
{
	Read,
	Write
};

struct FScopedSceneLock_Chaos
{
	FScopedSceneLock_Chaos(FPhysicsActorHandle const * InActorHandle, EPhysicsInterfaceScopedLockType InLockType)
		: LockType(InLockType)
	{
		Scene = GetSceneForActor(InActorHandle);
		LockScene();
	}

	FScopedSceneLock_Chaos(FPhysicsActorHandle const * InActorHandleA, FPhysicsActorHandle const * InActorHandleB, EPhysicsInterfaceScopedLockType InLockType)
		: LockType(InLockType)
	{
		FPhysScene_Chaos* SceneA = GetSceneForActor(InActorHandleA);
		FPhysScene_Chaos* SceneB = GetSceneForActor(InActorHandleB);

		if(SceneA == SceneB)
		{
			Scene = SceneA;
		}
		else if(!SceneA || !SceneB)
		{
			Scene = SceneA ? SceneA : SceneB;
		}
		else
		{
			UE_LOG(LogPhysics, Warning, TEXT("Attempted to aquire a physics scene lock for two paired actors that were not in the same scene. Skipping lock"));
		}

		LockScene();
	}

	FScopedSceneLock_Chaos(FPhysicsConstraintHandle const * InHandle, EPhysicsInterfaceScopedLockType InLockType)
		: Scene(nullptr)
		, LockType(InLockType)
	{
		UE_LOG(LogPhysics, Warning, TEXT("Constraint instance attempted scene lock, Constraints currently unimplemented"));
	}

	FScopedSceneLock_Chaos(USkeletalMeshComponent* InSkelMeshComp, EPhysicsInterfaceScopedLockType InLockType)
		: LockType(InLockType)
	{
		Scene = nullptr;

		if(InSkelMeshComp)
		{
			for(FBodyInstance* BI : InSkelMeshComp->Bodies)
			{
				Scene = GetSceneForActor(&BI->GetPhysicsActorHandle());
				if(Scene)
				{
					break;
				}
			}
		}

		LockScene();
	}

	FScopedSceneLock_Chaos(FPhysScene_Chaos* InScene, EPhysicsInterfaceScopedLockType InLockType)
		: Scene(InScene)
		, LockType(InLockType)
	{
		LockScene();
	}

	~FScopedSceneLock_Chaos()
	{
		UnlockScene();
	}

private:

	void LockScene()
	{
		if(!Scene)
		{
			return;
		}

		switch(LockType)
		{
		case EPhysicsInterfaceScopedLockType::Read:
			Scene->ExternalDataLock.ReadLock();
			break;
		case EPhysicsInterfaceScopedLockType::Write:
			Scene->ExternalDataLock.WriteLock();
			break;
		}
	}

	void UnlockScene()
	{
		if(!Scene)
		{
			return;
		}

		switch(LockType)
		{
		case EPhysicsInterfaceScopedLockType::Read:
			Scene->ExternalDataLock.ReadUnlock();
			break;
		case EPhysicsInterfaceScopedLockType::Write:
			Scene->ExternalDataLock.WriteUnlock();
			break;
		}
	}

	FPhysScene_Chaos* GetSceneForActor(FPhysicsActorHandle const * InActorHandle)
	{
		FBodyInstance* ActorInstance = (*InActorHandle) ? FPhysicsUserData_Chaos::Get<FBodyInstance>((*InActorHandle)->UserData()) : nullptr;

		if(ActorInstance)
		{
			return ActorInstance->GetPhysicsScene();
		}

		return nullptr;
	}

	FPhysScene_Chaos* Scene;
	EPhysicsInterfaceScopedLockType LockType;
};

bool FPhysInterface_Chaos::ExecuteOnUnbrokenConstraintReadOnly(const FPhysicsConstraintHandle& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintHandle&)> Func)
{
    if (!IsBroken(InConstraintRef))
    {
		FScopedSceneLock_Chaos SceneLock(&InConstraintRef, EPhysicsInterfaceScopedLockType::Read);
        Func(InConstraintRef);
        return true;
    }
    return false;
}

bool FPhysInterface_Chaos::ExecuteOnUnbrokenConstraintReadWrite(const FPhysicsConstraintHandle& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintHandle&)> Func)
{
    if (!IsBroken(InConstraintRef))
    {
		FScopedSceneLock_Chaos SceneLock(&InConstraintRef, EPhysicsInterfaceScopedLockType::Write);
        Func(InConstraintRef);
        return true;
    }
    return false;
}

bool FPhysInterface_Chaos::ExecuteRead(const FPhysicsActorHandle& InActorReference, TFunctionRef<void(const FPhysicsActorHandle& Actor)> InCallable)
{
	if(InActorReference)
	{
		FScopedSceneLock_Chaos SceneLock(&InActorReference, EPhysicsInterfaceScopedLockType::Read);

		InCallable(InActorReference);
		return true;
	}
	return false;
}

bool FPhysInterface_Chaos::ExecuteRead(USkeletalMeshComponent* InMeshComponent, TFunctionRef<void()> InCallable)
{
	FScopedSceneLock_Chaos SceneLock(InMeshComponent, EPhysicsInterfaceScopedLockType::Read);
	InCallable();
	return true;
}

bool FPhysInterface_Chaos::ExecuteRead(const FPhysicsActorHandle& InActorReferenceA, const FPhysicsActorHandle& InActorReferenceB, TFunctionRef<void(const FPhysicsActorHandle& ActorA, const FPhysicsActorHandle& ActorB)> InCallable)
{
	FScopedSceneLock_Chaos SceneLock(&InActorReferenceA, &InActorReferenceB, EPhysicsInterfaceScopedLockType::Read);
	InCallable(InActorReferenceA, InActorReferenceB);
	return true;
}

bool FPhysInterface_Chaos::ExecuteRead(const FPhysicsConstraintHandle& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintHandle& Constraint)> InCallable)
{
	if(InConstraintRef.IsValid())
	{
		FScopedSceneLock_Chaos SceneLock(&InConstraintRef, EPhysicsInterfaceScopedLockType::Read);
		InCallable(InConstraintRef);
		return true;
	}

	return false;
}

bool FPhysInterface_Chaos::ExecuteRead(FPhysScene* InScene, TFunctionRef<void()> InCallable)
{
	if(InScene)
	{
		FScopedSceneLock_Chaos SceneLock(InScene, EPhysicsInterfaceScopedLockType::Read);
		InCallable();
		return true;
	}

	return false;
}

bool FPhysInterface_Chaos::ExecuteWrite(const FPhysicsActorHandle& InActorReference, TFunctionRef<void(const FPhysicsActorHandle& Actor)> InCallable)
{
	//why do we have a write that takes in a const handle?
	if(InActorReference)
	{
		FScopedSceneLock_Chaos SceneLock(&InActorReference, EPhysicsInterfaceScopedLockType::Write);
		InCallable(InActorReference);
		return true;
	}
	return false;
}

bool FPhysInterface_Chaos::ExecuteWrite(FPhysicsActorHandle& InActorReference, TFunctionRef<void(FPhysicsActorHandle& Actor)> InCallable)
{
	if(InActorReference)
	{
		FScopedSceneLock_Chaos SceneLock(&InActorReference, EPhysicsInterfaceScopedLockType::Write);
		InCallable(InActorReference);
		return true;
	}

	return false;
}

bool FPhysInterface_Chaos::ExecuteWrite(USkeletalMeshComponent* InMeshComponent, TFunctionRef<void()> InCallable)
{
	FScopedSceneLock_Chaos SceneLock(InMeshComponent, EPhysicsInterfaceScopedLockType::Write);
	InCallable();
	return true;
}

bool FPhysInterface_Chaos::ExecuteWrite(const FPhysicsActorHandle& InActorReferenceA, const FPhysicsActorHandle& InActorReferenceB, TFunctionRef<void(const FPhysicsActorHandle& ActorA, const FPhysicsActorHandle& ActorB)> InCallable)
{
	FScopedSceneLock_Chaos SceneLock(&InActorReferenceA, &InActorReferenceB, EPhysicsInterfaceScopedLockType::Write);
	InCallable(InActorReferenceA, InActorReferenceB);
	return true;
}

bool FPhysInterface_Chaos::ExecuteWrite(const FPhysicsConstraintHandle& InConstraintRef, TFunctionRef<void(const FPhysicsConstraintHandle& Constraint)> InCallable)
{
	if(InConstraintRef.IsValid())
	{
		FScopedSceneLock_Chaos SceneLock(&InConstraintRef, EPhysicsInterfaceScopedLockType::Write);
		InCallable(InConstraintRef);
		return true;
	}

	return false;
}

bool FPhysInterface_Chaos::ExecuteWrite(FPhysScene* InScene, TFunctionRef<void()> InCallable)
{
	if(InScene)
	{
		FScopedSceneLock_Chaos SceneLock(InScene, EPhysicsInterfaceScopedLockType::Write);
		InCallable();
		return true;
	}

	return false;
}

void FPhysInterface_Chaos::ExecuteShapeWrite(FBodyInstance* InInstance, FPhysicsShapeHandle& InShape, TFunctionRef<void(FPhysicsShapeHandle& InShape)> InCallable)
{
	if(InInstance && InShape.IsValid())
	{
		FScopedSceneLock_Chaos SceneLock(&InInstance->GetPhysicsActorHandle(), EPhysicsInterfaceScopedLockType::Write);
		InCallable(InShape);
	}
}


FPhysicsShapeHandle FPhysInterface_Chaos::CreateShape(physx::PxGeometry* InGeom, bool bSimulation, bool bQuery, UPhysicalMaterial* InSimpleMaterial, TArray<UPhysicalMaterial*>* InComplexMaterials)
{
	// #todo : Implement
	// @todo(mlentine): Should we be doing anything with the InGeom here?
    FPhysicsActorHandle NewActor = nullptr;
	return { nullptr, NewActor };
}

const FBodyInstance* FPhysInterface_Chaos::ShapeToOriginalBodyInstance(const FBodyInstance* InCurrentInstance, const Chaos::FPerShapeData* InShape)
{
	//question: this is identical to physx version, should it be in body instance?
	check(InCurrentInstance);
	check(InShape);

	const FBodyInstance* TargetInstance = InCurrentInstance->WeldParent ? InCurrentInstance->WeldParent : InCurrentInstance;
	const FBodyInstance* OutInstance = TargetInstance;

	if (const TMap<FPhysicsShapeHandle, FBodyInstance::FWeldInfo>* WeldInfo = InCurrentInstance->GetCurrentWeldInfo())
	{
		for (const TPair<FPhysicsShapeHandle, FBodyInstance::FWeldInfo>& Pair : *WeldInfo)
		{
			if (Pair.Key.Shape == InShape)
			{
				TargetInstance = Pair.Value.ChildBI;
			}
		}
	}

	return TargetInstance;
}



void FPhysInterface_Chaos::AddGeometry(FPhysicsActorHandle& InActor, const FGeometryAddParams& InParams, TArray<FPhysicsShapeHandle>* OutOptShapes)
{
	LLM_SCOPE(ELLMTag::ChaosGeometry);
	TArray<TUniquePtr<Chaos::FImplicitObject>> Geoms;
	Chaos::FShapesArray Shapes;
	ChaosInterface::CreateGeometry(InParams, Geoms, Shapes);

#if WITH_CHAOS
	if (InActor && Geoms.Num())
	{
		for (TUniquePtr<Chaos::FPerShapeData>& Shape : Shapes)
		{
			FPhysicsShapeHandle NewHandle(Shape.Get(), InActor);
			if (OutOptShapes)
			{
				OutOptShapes->Add(NewHandle);
			}

			FBodyInstance::ApplyMaterialToShape_AssumesLocked(NewHandle, InParams.SimpleMaterial, InParams.ComplexMaterials, &InParams.ComplexMaterialMasks);

			//TArrayView<UPhysicalMaterial*> SimpleView = MakeArrayView(&(const_cast<UPhysicalMaterial*>(InParams.SimpleMaterial)), 1);
			//FPhysInterface_Chaos::SetMaterials(NewHandle, InParams.ComplexMaterials.Num() > 0 ? InParams.ComplexMaterials : SimpleView);
		}

		//todo: we should not be creating unique geometry per actor
		if(Geoms.Num() > 1)
		{
			InActor->SetGeometry(MakeUnique<Chaos::FImplicitObjectUnion>(MoveTemp(Geoms)));
		}
		else
		{
			InActor->SetGeometry(MoveTemp(Geoms[0]));
		}
		InActor->SetShapesArray(MoveTemp(Shapes));
	}
#endif
}

void FPhysInterface_Chaos::SetMaterials(const FPhysicsShapeHandle& InShape, const TArrayView<UPhysicalMaterial*> InMaterials)
{
	// Build a list of handles to store on the shape
	TArray<Chaos::FMaterialHandle> NewMaterialHandles;
	NewMaterialHandles.Reserve(InMaterials.Num());

	for(UPhysicalMaterial* UnrealMaterial : InMaterials)
	{
		NewMaterialHandles.Add(UnrealMaterial->GetPhysicsMaterial());
	}

	InShape.Shape->SetMaterials(NewMaterialHandles);
}

void FPhysInterface_Chaos::SetMaterials(const FPhysicsShapeHandle& InShape, const TArrayView<UPhysicalMaterial*> InMaterials, const TArrayView<FPhysicalMaterialMaskParams>& InMaterialMasks)
{
	SetMaterials(InShape, InMaterials);

	if (InMaterialMasks.Num() > 0)
	{
		// Build a list of handles to store on the shape
		TArray<Chaos::FMaterialMaskHandle> NewMaterialMaskHandles;
		TArray<uint32> NewMaterialMaskMaps;
		TArray<Chaos::FMaterialHandle> NewMaterialMaskMaterialHandles;

		NewMaterialMaskHandles.Reserve(InMaterialMasks.Num());

		int MaskMapMatIdx = 0;

		InShape.Shape->ModifyMaterialMaskMaps([&](auto& MaterialMaskMaps)
		{
			for(FPhysicalMaterialMaskParams& MaterialMaskData : InMaterialMasks)
		{
				if(MaterialMaskData.PhysicalMaterialMask && ensure(MaterialMaskData.PhysicalMaterialMap))
			{
				NewMaterialMaskHandles.Add(MaterialMaskData.PhysicalMaterialMask->GetPhysicsMaterialMask());
					for(int i = 0; i < EPhysicalMaterialMaskColor::MAX; i++)
				{
						if(UPhysicalMaterial* MapMat = MaterialMaskData.PhysicalMaterialMap->GetPhysicalMaterialFromMap(i))
					{
							MaterialMaskMaps.Emplace(MaskMapMatIdx);
						MaskMapMatIdx++;
						} else
					{
							MaterialMaskMaps.Emplace(INDEX_NONE);
				}
			}
				} else
			{
				NewMaterialMaskHandles.Add(Chaos::FMaterialMaskHandle());
					for(int i = 0; i < EPhysicalMaterialMaskColor::MAX; i++)
				{
						MaterialMaskMaps.Emplace(INDEX_NONE);
				}
			}
		}

		});
		

		
		if (MaskMapMatIdx > 0)
		{
			NewMaterialMaskMaterialHandles.Reserve(MaskMapMatIdx);

			uint32 Offset = 0;

			for (FPhysicalMaterialMaskParams& MaterialMaskData : InMaterialMasks)
			{
				if (MaterialMaskData.PhysicalMaterialMask)
				{
					for (int i = 0; i < EPhysicalMaterialMaskColor::MAX; i++)
					{
						if (UPhysicalMaterial* MapMat = MaterialMaskData.PhysicalMaterialMap->GetPhysicalMaterialFromMap(i))
						{
							NewMaterialMaskMaterialHandles.Add(MapMat->GetPhysicsMaterial());
						}
					}
				}
			}
		}

		InShape.Shape->SetMaterialMasks(NewMaterialMaskHandles);
		InShape.Shape->SetMaterialMaskMapMaterials(NewMaterialMaskMaterialHandles);
	}
}

void FinishSceneStat()
{
}

bool FPhysInterface_Chaos::LineTrace_Geom(FHitResult& OutHit, const FBodyInstance* InInstance, const FVector& WorldStart, const FVector& WorldEnd, bool bTraceComplex, bool bExtractPhysMaterial)
{
	// Need an instance to trace against
	check(InInstance);

	OutHit.TraceStart = WorldStart;
	OutHit.TraceEnd = WorldEnd;

	bool bHitSomething = false;

	const FVector Delta = WorldEnd - WorldStart;
	const float DeltaMag = Delta.Size();
	if (DeltaMag > KINDA_SMALL_NUMBER)
	{
		{
			// #PHYS2 Really need a concept for "multi" locks here - as we're locking ActorRef but not TargetInstance->ActorRef
			FPhysicsCommand::ExecuteRead(InInstance->ActorHandle, [&](const FPhysicsActorHandle& Actor)
			{
				// If we're welded then the target instance is actually our parent
				const FBodyInstance* TargetInstance = InInstance->WeldParent ? InInstance->WeldParent : InInstance;
				if(const Chaos::TGeometryParticle<float, 3>* RigidBody = TargetInstance->ActorHandle)
				{
					FRaycastHit BestHit;
					BestHit.Distance = FLT_MAX;

					// Get all the shapes from the actor
					PhysicsInterfaceTypes::FInlineShapeArray Shapes;
					const int32 NumShapes = FillInlineShapeArray_AssumesLocked(Shapes, Actor);

					const FTransform WorldTM(RigidBody->R(), RigidBody->X());
					const FVector LocalStart = WorldTM.InverseTransformPositionNoScale(WorldStart);
					const FVector LocalDelta = WorldTM.InverseTransformVectorNoScale(Delta);

					// Iterate over each shape
					for (int32 ShapeIdx = 0; ShapeIdx < NumShapes; ShapeIdx++)
					{
						// #PHYS2 - SHAPES - Resolve this single cast case
						FPhysicsShapeReference_Chaos& ShapeRef = Shapes[ShapeIdx];
						Chaos::FPerShapeData* Shape = ShapeRef.Shape;
						check(Shape);

						if (TargetInstance->IsShapeBoundToBody(ShapeRef) == false)
						{
							continue;
						}

						// Filter so we trace against the right kind of collision
						FCollisionFilterData ShapeFilter = Shape->GetQueryData();
						const bool bShapeIsComplex = (ShapeFilter.Word3 & EPDF_ComplexCollision) != 0;
						const bool bShapeIsSimple = (ShapeFilter.Word3 & EPDF_SimpleCollision) != 0;
						if ((bTraceComplex && bShapeIsComplex) || (!bTraceComplex && bShapeIsSimple))
						{

							float Distance;
							Chaos::TVector<float, 3> LocalPosition;
							Chaos::TVector<float, 3> LocalNormal;

							int32 FaceIndex;
							if (Shape->GetGeometry()->Raycast(LocalStart, LocalDelta / DeltaMag, DeltaMag, 0, Distance, LocalPosition, LocalNormal, FaceIndex))
							{
								if (Distance < BestHit.Distance)
								{
									BestHit.Distance = Distance;
									BestHit.WorldNormal = LocalNormal;	//will convert to world when best is chosen
									BestHit.WorldPosition = LocalPosition;
									BestHit.Shape = Shape;
									BestHit.Actor = Actor;
									BestHit.FaceIndex = FaceIndex;
								}
							}
						}
					}

					if (BestHit.Distance < FLT_MAX)
					{
						BestHit.WorldNormal = WorldTM.TransformVectorNoScale(BestHit.WorldNormal);
						BestHit.WorldPosition = WorldTM.TransformPositionNoScale(BestHit.WorldPosition);
						SetFlags(BestHit, EHitFlags::Distance | EHitFlags::Normal | EHitFlags::Position);

						// we just like to make sure if the hit is made, set to test touch
						FCollisionFilterData QueryFilter;
						QueryFilter.Word2 = 0xFFFFF;

						FTransform StartTM(WorldStart);
						const UPrimitiveComponent* OwnerComponentInst = InInstance->OwnerComponent.Get();
						ConvertQueryImpactHit(OwnerComponentInst ? OwnerComponentInst->GetWorld() : nullptr, BestHit, OutHit, DeltaMag, QueryFilter, WorldStart, WorldEnd, nullptr, StartTM, true, bExtractPhysMaterial);
						bHitSomething = true;
					}
				}
			});
		}
	}

	return bHitSomething;
}

bool FPhysInterface_Chaos::Sweep_Geom(FHitResult& OutHit, const FBodyInstance* InInstance, const FVector& InStart, const FVector& InEnd, const FQuat& InShapeRotation, const FCollisionShape& InShape, bool bSweepComplex)
{
	bool bSweepHit = false;

	if (InShape.IsNearlyZero())
	{
		bSweepHit = LineTrace_Geom(OutHit, InInstance, InStart, InEnd, bSweepComplex);
	}
	else
	{
		OutHit.TraceStart = InStart;
		OutHit.TraceEnd = InEnd;

		const FBodyInstance* TargetInstance = InInstance->WeldParent ? InInstance->WeldParent : InInstance;

		FPhysicsCommand::ExecuteRead(TargetInstance->ActorHandle, [&](const FPhysicsActorHandle& Actor)
		{
			const Chaos::TGeometryParticle<float, 3>* RigidBody = Actor;

			if (RigidBody && InInstance->OwnerComponent.Get())
			{
				FPhysicsShapeAdapter ShapeAdapter(InShapeRotation, InShape);

				const FVector Delta = InEnd - InStart;
				const float DeltaMag = Delta.Size();
				if (DeltaMag > KINDA_SMALL_NUMBER)
				{
					const FTransform ActorTM(RigidBody->R(), RigidBody->X());

					UPrimitiveComponent* OwnerComponentInst = InInstance->OwnerComponent.Get();
					FTransform StartTM(ShapeAdapter.GetGeomOrientation(), InStart);
					FTransform CompTM(OwnerComponentInst->GetComponentTransform());

					Chaos::TVector<float,3> Dir = Delta / DeltaMag;

					FSweepHit Hit;

					// Get all the shapes from the actor
					PhysicsInterfaceTypes::FInlineShapeArray Shapes;
					// #PHYS2 - SHAPES - Resolve this function to not use px stuff
					const int32 NumShapes = FillInlineShapeArray_AssumesLocked(Shapes, Actor); // #PHYS2 - Need a lock/execute here?

					// Iterate over each shape
					for (int32 ShapeIdx = 0; ShapeIdx < NumShapes; ShapeIdx++)
					{
						FPhysicsShapeReference_Chaos& ShapeRef = Shapes[ShapeIdx];
						Chaos::FPerShapeData* Shape = ShapeRef.Shape;
						check(Shape);

						// Skip shapes not bound to this instance
						if (!TargetInstance->IsShapeBoundToBody(ShapeRef))
						{
							continue;
						}

						// Filter so we trace against the right kind of collision
						FCollisionFilterData ShapeFilter = Shape->GetQueryData();
						const bool bShapeIsComplex = (ShapeFilter.Word3 & EPDF_ComplexCollision) != 0;
						const bool bShapeIsSimple = (ShapeFilter.Word3 & EPDF_SimpleCollision) != 0;
						if ((bSweepComplex && bShapeIsComplex) || (!bSweepComplex && bShapeIsSimple))
						{
							//question: this is returning first result, is that valid? Keeping it the same as physx for now
							Chaos::TVector<float, 3> WorldPosition;
							Chaos::TVector<float, 3> WorldNormal;
							int32 FaceIdx;
							if (Chaos::Utilities::CastHelper(ShapeAdapter.GetGeometry(), ActorTM, [&](const auto& Downcast, const auto& FullActorTM) { return Chaos::SweepQuery(*Shape->GetGeometry(), FullActorTM, Downcast, StartTM, Dir, DeltaMag, Hit.Distance, WorldPosition, WorldNormal, FaceIdx, 0.f, false); }))
							{
								// we just like to make sure if the hit is made
								FCollisionFilterData QueryFilter;
								QueryFilter.Word2 = 0xFFFFF;

								// we don't get Shape information when we access via PShape, so I filled it up
								Hit.Shape = Shape;
								Hit.Actor = ShapeRef.ActorRef;
								Hit.WorldPosition = WorldPosition;
								Hit.WorldNormal = WorldNormal;
								Hit.FaceIndex = FaceIdx;
								if (!HadInitialOverlap(Hit))
								{
									Hit.FaceIndex = FindFaceIndex(Hit, Dir);
								}
								SetFlags(Hit, EHitFlags::Distance | EHitFlags::Normal | EHitFlags::Position | EHitFlags::FaceIndex);

								FTransform StartTransform(InStart);
								ConvertQueryImpactHit(OwnerComponentInst->GetWorld(), Hit, OutHit, DeltaMag, QueryFilter, InStart, InEnd, nullptr, StartTransform, false, false);
								bSweepHit = true;
							}
						}
					}
				}
			}
		});
	}

	return bSweepHit;
}

bool Overlap_GeomInternal(const FBodyInstance* InInstance, const Chaos::FImplicitObject& InGeom, const FTransform& GeomTransform, FMTDResult* OutOptResult)
{
	const FBodyInstance* TargetInstance = InInstance->WeldParent ? InInstance->WeldParent : InInstance;
	Chaos::TGeometryParticle<float, 3>* RigidBody = TargetInstance->ActorHandle;

	if (RigidBody == nullptr)
	{
		return false;
	}

	// Get all the shapes from the actor
	PhysicsInterfaceTypes::FInlineShapeArray Shapes;
	const int32 NumShapes = FillInlineShapeArray_AssumesLocked(Shapes, RigidBody);

	const FTransform ActorTM(RigidBody->R(), RigidBody->X());

	// Iterate over each shape
	for (int32 ShapeIdx = 0; ShapeIdx < NumShapes; ++ShapeIdx)
	{
		FPhysicsShapeReference_Chaos& ShapeRef = Shapes[ShapeIdx];
		const Chaos::FPerShapeData* Shape = ShapeRef.Shape;
		check(Shape);

		if (TargetInstance->IsShapeBoundToBody(ShapeRef))
		{
			if (OutOptResult)
			{
				Chaos::FMTDInfo MTDInfo;
				if (Chaos::Utilities::CastHelper(InGeom, ActorTM, [&](const auto& Downcast, const auto& FullActorTM) { return Chaos::OverlapQuery(*Shape->GetGeometry(), FullActorTM, Downcast, GeomTransform, /*Thickness=*/0, &MTDInfo); }))
				{
					OutOptResult->Distance = MTDInfo.Penetration;
					OutOptResult->Direction = MTDInfo.Normal;
					return true;	//question: should we take most shallow penetration?
				}
			}
			else	//question: why do we even allow user to not pass in MTD info?
			{
				if (Chaos::Utilities::CastHelper(InGeom, ActorTM, [&](const auto& Downcast, const auto& FullActorTM) { return Chaos::OverlapQuery(*Shape->GetGeometry(), FullActorTM, Downcast, GeomTransform); }))
				{
					return true;
				}
			}

		}
	}

	return false;
}

bool FPhysInterface_Chaos::Overlap_Geom(const FBodyInstance* InBodyInstance, const FPhysicsGeometryCollection& InGeometry, const FTransform& InShapeTransform, FMTDResult* OutOptResult)
{
	return Overlap_GeomInternal(InBodyInstance, InGeometry.GetGeometry(), InShapeTransform, OutOptResult);
}

bool FPhysInterface_Chaos::Overlap_Geom(const FBodyInstance* InBodyInstance, const FCollisionShape& InCollisionShape, const FQuat& InShapeRotation, const FTransform& InShapeTransform, FMTDResult* OutOptResult)
{
	FPhysicsShapeAdapter Adaptor(InShapeRotation, InCollisionShape);
	return Overlap_GeomInternal(InBodyInstance, Adaptor.GetGeometry(), Adaptor.GetGeomPose(InShapeTransform.GetTranslation()), OutOptResult);
}

bool FPhysInterface_Chaos::GetSquaredDistanceToBody(const FBodyInstance* InInstance, const FVector& InPoint, float& OutDistanceSquared, FVector* OutOptPointOnBody)
{
	if (OutOptPointOnBody)
	{
		*OutOptPointOnBody = InPoint;
		OutDistanceSquared = 0.f;
	}

	float ReturnDistance = -1.f;
	float MinPhi = BIG_NUMBER;
	bool bFoundValidBody = false;
	bool bEarlyOut = true;

	const FBodyInstance* UseBI = InInstance->WeldParent ? InInstance->WeldParent : InInstance;
	const FTransform BodyTM = UseBI->GetUnrealWorldTransform();
	const FVector LocalPoint = BodyTM.InverseTransformPositionNoScale(InPoint);

	FPhysicsCommand::ExecuteRead(UseBI->ActorHandle, [&](const FPhysicsActorHandle& Actor)
	{

		bEarlyOut = false;

		TArray<FPhysicsShapeReference_Chaos> Shapes;
		InInstance->GetAllShapes_AssumesLocked(Shapes);
		for (const FPhysicsShapeReference_Chaos& Shape : Shapes)
		{
			if (UseBI->IsShapeBoundToBody(Shape) == false)	//skip welded shapes that do not belong to us
			{
				continue;
			}

			ECollisionShapeType GeomType = FPhysicsInterface::GetShapeType(Shape);

			if (!Shape.GetGeometry().IsConvex())
			{
				// Type unsupported for this function, but some other shapes will probably work. 
				continue;
			}

			bFoundValidBody = true;

			Chaos::TVector<float, 3> Normal;
			const float Phi = Shape.Shape->GetGeometry()->PhiWithNormal(LocalPoint, Normal);
			if (Phi <= 0)
			{
				OutDistanceSquared = 0;
				if (OutOptPointOnBody)
				{
					*OutOptPointOnBody = InPoint;
				}
				break;
			}
			else if (Phi < MinPhi)
			{
				MinPhi = Phi;
				OutDistanceSquared = Phi * Phi;
				if (OutOptPointOnBody)
				{
					const Chaos::TVector<float, 3> LocalClosestPoint = LocalPoint - Phi * Normal;
					*OutOptPointOnBody = BodyTM.TransformPositionNoScale(LocalClosestPoint);
				}
			}
		}
	});

	if (!bFoundValidBody && !bEarlyOut)
	{
		UE_LOG(LogPhysics, Verbose, TEXT("GetDistanceToBody: Component (%s) has no simple collision and cannot be queried for closest point."), InInstance->OwnerComponent.Get() ? *(InInstance->OwnerComponent->GetPathName()) : TEXT("NONE"));
	}

	return bFoundValidBody;
}

uint32 GetTriangleMeshExternalFaceIndex(const FPhysicsShape& Shape, uint32 InternalFaceIndex)
{
	using namespace Chaos;
	uint8 OuterType = Shape.GetGeometry()->GetType();
	uint8 InnerType = GetInnerType(OuterType);
	if (ensure(InnerType == ImplicitObjectType::TriangleMesh))
	{
		const FTriangleMeshImplicitObject* TriangleMesh = nullptr;

		if (IsScaled(OuterType))
		{
			const TImplicitObjectScaled<FTriangleMeshImplicitObject>& ScaledTriangleMesh = Shape.GetGeometry()->GetObjectChecked<TImplicitObjectScaled<FTriangleMeshImplicitObject>>();
			TriangleMesh = ScaledTriangleMesh.GetUnscaledObject();
		}
		else if(IsInstanced(OuterType))
		{
			TriangleMesh = Shape.GetGeometry()->GetObjectChecked<TImplicitObjectInstanced<FTriangleMeshImplicitObject>>().GetInstancedObject();
		}
		else
		{
			TriangleMesh = &Shape.GetGeometry()->GetObjectChecked<FTriangleMeshImplicitObject>();
		}

		return TriangleMesh->GetExternalFaceIndexFromInternal(InternalFaceIndex);
	}

	return -1;
}

void FPhysInterface_Chaos::CalculateMassPropertiesFromShapeCollection(Chaos::TMassProperties<float,3>& OutProperties,const TArray<FPhysicsShapeHandle>& InShapes,float InDensityKGPerCM)
{
	ChaosInterface::CalculateMassPropertiesFromShapeCollection(OutProperties,InShapes,InDensityKGPerCM);
}

#endif
