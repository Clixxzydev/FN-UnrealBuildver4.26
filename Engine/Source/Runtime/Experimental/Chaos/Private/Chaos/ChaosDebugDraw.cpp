// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/Evolution/SimulationSpace.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"

//#pragma optimize ("", off)

namespace Chaos
{
	namespace DebugDraw
	{
		float ArrowSize = 1.5f;
		float BodyAxisLen = 4.0f;
		float ContactLen = 4.0f;
		float ContactWidth = 2.0f;
		float ContactPhiWidth = 0.3f;
		float ContactOwnerWidth = 0.0f;
		float ConstraintAxisLen = 5.0f;
		float JointComSize = 2.0f;
		float LineThickness = 0.15f;
		float DrawScale = 1.0f;
		float FontHeight = 10.0f;
		float FontScale = 1.5f;
		float ShapeThicknesScale = 1.0f;
		float PointSize = 2.0f;
		float VelScale = 0.0f;
		float AngVelScale = 0.0f;
		int DrawPriority = 10.0f;

		FAutoConsoleVariableRef CVarArrowSize(TEXT("p.Chaos.DebugDrawArrowSize"), ArrowSize, TEXT("ArrowSize."));
		FAutoConsoleVariableRef CVarBodyAxisLen(TEXT("p.Chaos.DebugDrawBodyAxisLen"), BodyAxisLen, TEXT("BodyAxisLen."));
		FAutoConsoleVariableRef CVarContactLen(TEXT("p.Chaos.DebugDrawContactLen"), ContactLen, TEXT("ContactLen."));
		FAutoConsoleVariableRef CVarContactWidth(TEXT("p.Chaos.DebugDrawContactWidth"), ContactWidth, TEXT("ContactWidth."));
		FAutoConsoleVariableRef CVarContactPhiWidth(TEXT("p.Chaos.DebugDrawContactPhiWidth"), ContactPhiWidth, TEXT("ContactPhiWidth."));
		FAutoConsoleVariableRef CVarContactOwnerWidth(TEXT("p.Chaos.DebugDrawContactOwnerWidth"), ContactOwnerWidth, TEXT("ContactOwnerWidth."));
		FAutoConsoleVariableRef CVarConstraintAxisLen(TEXT("p.Chaos.DebugDrawConstraintAxisLen"), ConstraintAxisLen, TEXT("ConstraintAxisLen."));
		FAutoConsoleVariableRef CVarLineThickness(TEXT("p.Chaos.DebugDrawLineThickness"), LineThickness, TEXT("LineThickness."));
		FAutoConsoleVariableRef CVarLineShapeThickness(TEXT("p.Chaos.DebugDrawShapeLineThicknessScale"), ShapeThicknesScale, TEXT("Shape lineThickness multiplier."));
		FAutoConsoleVariableRef CVarVelScale(TEXT("p.Chaos.DebugDrawVelScale"), VelScale, TEXT("If >0 show velocity when drawing particle transforms."));
		FAutoConsoleVariableRef CVarAngVelScale(TEXT("p.Chaos.DebugDrawAngVelScale"), AngVelScale, TEXT("If >0 show angular velocity when drawing particle transforms."));
		FAutoConsoleVariableRef CVarScale(TEXT("p.Chaos.DebugDrawScale"), DrawScale, TEXT("Scale applied to all Chaos Debug Draw line lengths etc."));

		//
		//
		//

#if CHAOS_DEBUG_DRAW

		void DrawShapesImpl(const FRigidTransform3& ShapeTransform, const FImplicitObject* Shape, const FColor& Color);

		void DrawShape(const FRigidTransform3& ShapeTransform, const FImplicitObject* Shape, const FColor& Color)
		{
			DrawShapesImpl(ShapeTransform, Shape, Color);
		}

		template <bool bInstanced>
		void DrawShapesScaledImpl(const FRigidTransform3& ShapeTransform, const FImplicitObject* Shape, const FColor& Color)
		{
			const EImplicitObjectType PackedType = Shape->GetType();
			const EImplicitObjectType InnerType = GetInnerType(PackedType);
			CHAOS_CHECK(IsScaled(PackedType));
			CHAOS_CHECK(IsInstanced(PackedType) == bInstanced);

			FRigidTransform3 ScaleTM = FRigidTransform3::Identity;
			switch (InnerType)
			{
			case ImplicitObjectType::Sphere:
				break;
			case ImplicitObjectType::Box:
				break;
			case ImplicitObjectType::Plane:
				break;
			case ImplicitObjectType::Capsule:
				break;
			case ImplicitObjectType::Transformed:
				break;
			case ImplicitObjectType::Union:
				break;
			case ImplicitObjectType::LevelSet:
				break;
			case ImplicitObjectType::Unknown:
				break;
			case ImplicitObjectType::Convex:
			{
				const TImplicitObjectScaled<FConvex, bInstanced>* Scaled = Shape->template GetObject<TImplicitObjectScaled<FConvex, bInstanced>>();
				ScaleTM.SetScale3D(Scaled->GetScale());
				DrawShapesImpl(ShapeTransform * ScaleTM, Scaled->GetUnscaledObject(), Color);
				break;
			}
			case ImplicitObjectType::TaperedCylinder:
				break;
			case ImplicitObjectType::Cylinder:
				break;
			case ImplicitObjectType::TriangleMesh:
				break;
			case ImplicitObjectType::HeightField:
				break;
			default:
				break;
			}
		}

		void DrawShapesInstancedImpl(const FRigidTransform3& ShapeTransform, const FImplicitObject* Shape, const FColor& Color)
		{
			const EImplicitObjectType PackedType = Shape->GetType();
			const EImplicitObjectType InnerType = GetInnerType(PackedType);
			CHAOS_CHECK(IsScaled(PackedType) == false);
			CHAOS_CHECK(IsInstanced(PackedType));

			switch (InnerType)
			{
			case ImplicitObjectType::Sphere:
				break;
			case ImplicitObjectType::Box:
				break;
			case ImplicitObjectType::Plane:
				break;
			case ImplicitObjectType::Capsule:
				break;
			case ImplicitObjectType::Transformed:
				break;
			case ImplicitObjectType::Union:
				break;
			case ImplicitObjectType::LevelSet:
				break;
			case ImplicitObjectType::Unknown:
				break;
			case ImplicitObjectType::Convex:
			{
				const TImplicitObjectInstanced<FConvex>* Instanced = Shape->template GetObject<TImplicitObjectInstanced<FConvex>>();
				DrawShapesImpl(ShapeTransform, Instanced->GetInstancedObject(), Color);
				break;
			}
			case ImplicitObjectType::TaperedCylinder:
				break;
			case ImplicitObjectType::Cylinder:
				break;
			case ImplicitObjectType::TriangleMesh:
				break;
			case ImplicitObjectType::HeightField:
				break;
			default:
				break;
			}
		}

		void DrawShapesImpl(const FRigidTransform3& ShapeTransform, const FImplicitObject* Shape, const FColor& Color)
		{
			const EImplicitObjectType PackedType = Shape->GetType(); // Type includes scaling and instancing data
			const EImplicitObjectType InnerType = GetInnerType(Shape->GetType());

			// For scaled shapes, we must unpack the scaled type first
			if (IsScaled(PackedType))
			{
				if (IsInstanced(PackedType))
				{
					DrawShapesScaledImpl<true>(ShapeTransform, Shape, Color);
				}
				else
				{
					DrawShapesScaledImpl<false>(ShapeTransform, Shape, Color);
				}
				return;
			}
			else if (IsInstanced(PackedType))
			{
				DrawShapesInstancedImpl(ShapeTransform, Shape, Color);
			}

			// @todo(ccaulfield): handle scale throughout
			switch (InnerType)
			{
			case ImplicitObjectType::Sphere:
			{
				const TSphere<FReal, 3>* Sphere = Shape->template GetObject<TSphere<FReal, 3>>();
				const FVec3 P = ShapeTransform.TransformPosition(Sphere->GetCenter());
				FDebugDrawQueue::GetInstance().DrawDebugSphere(P, Sphere->GetRadius(), 20, Color, false, KINDA_SMALL_NUMBER, DrawPriority, ShapeThicknesScale * LineThickness);
				break;
			}
			case ImplicitObjectType::Box:
			{
				const TBox<FReal, 3>* Box = Shape->template GetObject<TBox<FReal, 3>>();
				const FVec3 P = ShapeTransform.TransformPosition(Box->GetCenter());
				FDebugDrawQueue::GetInstance().DrawDebugBox(P, (FReal)0.5 * Box->Extents(), ShapeTransform.GetRotation(), Color, false, KINDA_SMALL_NUMBER, DrawPriority, ShapeThicknesScale * LineThickness);
				break;
			}
			case ImplicitObjectType::Plane:
				break;
			case ImplicitObjectType::Capsule:
			{
				const TCapsule<FReal>* Capsule = Shape->template GetObject<TCapsule<FReal>>();
				const FVec3 P = ShapeTransform.TransformPosition(Capsule->GetCenter());
				const FRotation3 Q = ShapeTransform.GetRotation() * FRotationMatrix::MakeFromZ(Capsule->GetAxis());
				FDebugDrawQueue::GetInstance().DrawDebugCapsule(P, (FReal)0.5 * Capsule->GetHeight() + Capsule->GetRadius(), Capsule->GetRadius(), Q, Color, false, KINDA_SMALL_NUMBER, DrawPriority, ShapeThicknesScale * LineThickness);
				break;
			}
			case ImplicitObjectType::Transformed:
			{
				const TImplicitObjectTransformed<FReal, 3>* Transformed = Shape->template GetObject<TImplicitObjectTransformed<FReal, 3>>();
				FRigidTransform3 TransformedTransform = FRigidTransform3(ShapeTransform.TransformPosition(Transformed->GetTransform().GetLocation()), ShapeTransform.GetRotation() * Transformed->GetTransform().GetRotation());
				DrawShapesImpl(TransformedTransform, Transformed->GetTransformedObject(), Color);
				break;
			}
			case ImplicitObjectType::Union:
			{
				const FImplicitObjectUnion* Union = Shape->template GetObject<FImplicitObjectUnion>();
				for (auto& UnionShape : Union->GetObjects())
				{
					DrawShapesImpl(ShapeTransform, UnionShape.Get(), Color);
				}
				break;
			}
			case ImplicitObjectType::LevelSet:
				break;
			case ImplicitObjectType::Unknown:
				break;
			case ImplicitObjectType::Convex:
			{
				if (const FConvex* Convex = Shape->template GetObject<FConvex>())
				{
					// TODO: This is horrendously slow. Figure out a way to cache
					// the generated trimeshes on the debug draw queue instance.
					const TParticles<FReal, 3>& Particles = Convex->GetSurfaceParticles();
					TTriangleMesh<FReal> Triangles = TTriangleMesh<FReal>::GetConvexHullFromParticles(Particles);
					for (const TVector<int32, 3>& Elem : Triangles.GetElements())
					{
						const FVec3 P0 = ShapeTransform.TransformPosition(Particles.X(Elem[0]));
						const FVec3 P1 = ShapeTransform.TransformPosition(Particles.X(Elem[1]));
						const FVec3 P2 = ShapeTransform.TransformPosition(Particles.X(Elem[2]));
						FDebugDrawQueue::GetInstance().DrawDebugLine(P0, P1, Color, false, -1.f, 0, ShapeThicknesScale * LineThickness);
						FDebugDrawQueue::GetInstance().DrawDebugLine(P1, P2, Color, false, -1.f, 0, ShapeThicknesScale * LineThickness);
						FDebugDrawQueue::GetInstance().DrawDebugLine(P2, P0, Color, false, -1.f, 0, ShapeThicknesScale * LineThickness);
					}
				}
				break;
			}
			case ImplicitObjectType::TaperedCylinder:
				break;
			case ImplicitObjectType::Cylinder:
				break;
			case ImplicitObjectType::TriangleMesh:
				break;
			case ImplicitObjectType::HeightField:
				break;
			default:
				break;
			}
		}

		void DrawParticleShapesImpl(const FRigidTransform3& SpaceTransform, const TGeometryParticleHandle<FReal, 3>* Particle, const FColor& Color)
		{
			FVec3 P = SpaceTransform.TransformPosition(Particle->ObjectState() == EObjectStateType::Dynamic ? Particle->CastToRigidParticle()->P() : Particle->X());
			FRotation3 Q = SpaceTransform.GetRotation() * (Particle->ObjectState() == EObjectStateType::Dynamic ? Particle->CastToRigidParticle()->Q() : Particle->R());

			DrawShapesImpl(FRigidTransform3(P, Q), Particle->Geometry().Get(), Color);
		}

		void DrawParticleShapesImpl(const FRigidTransform3& SpaceTransform, const TGeometryParticle<FReal, 3>* Particle, const FColor& Color)
		{
			FVec3 P = SpaceTransform.TransformPosition(Particle->X());
			FRotation3 Q = SpaceTransform.GetRotation() * (Particle->R());

			DrawShapesImpl(FRigidTransform3(P, Q), Particle->Geometry().Get(), Color);
		}

		void DrawParticleBoundsImpl(const FRigidTransform3& SpaceTransform, const TGeometryParticleHandle<FReal, 3>* InParticle, const FColor& Color)
		{
			TAABB<FReal, 3> Box = InParticle->WorldSpaceInflatedBounds();
			FVec3 P = SpaceTransform.TransformPosition(Box.GetCenter());
			FRotation3 Q = SpaceTransform.GetRotation();
			FMatrix33 Qm = Q.ToMatrix();
			FDebugDrawQueue::GetInstance().DrawDebugBox(P, 0.5f * Box.Extents(), Q, Color, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
		}

		void DrawParticleTransformImpl(const FRigidTransform3& SpaceTransform, const TGeometryParticleHandle<FReal, 3>* InParticle, int32 Index, FReal ColorScale)
		{
			FColor Red = (ColorScale * FColor::Red).ToFColor(false);
			FColor Green = (ColorScale * FColor::Green).ToFColor(false);
			FColor Blue = (ColorScale * FColor::Blue).ToFColor(false);

			TConstGenericParticleHandle<FReal, 3> Particle(InParticle);
			FVec3 PCOM = SpaceTransform.TransformPosition(FParticleUtilities::GetCoMWorldPosition(Particle));
			FRotation3 QCOM = SpaceTransform.GetRotation() * FParticleUtilities::GetCoMWorldRotation(Particle);
			FMatrix33 QCOMm = QCOM.ToMatrix();
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(PCOM, PCOM + DrawScale * BodyAxisLen * QCOMm.GetAxis(0), DrawScale * ArrowSize, Red, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(PCOM, PCOM + DrawScale * BodyAxisLen * QCOMm.GetAxis(1), DrawScale * ArrowSize, Green, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(PCOM, PCOM + DrawScale * BodyAxisLen * QCOMm.GetAxis(2), DrawScale * ArrowSize, Blue, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);

			FColor Black = FColor::Black;
			FColor Grey = (ColorScale * FColor(64, 64, 64)).ToFColor(false);
			FVec3 PActor = SpaceTransform.TransformPosition(FParticleUtilities::GetActorWorldTransform(Particle).GetTranslation());
			FDebugDrawQueue::GetInstance().DrawDebugPoint(PActor, Black, false, KINDA_SMALL_NUMBER, DrawPriority, DrawScale * PointSize);
			FDebugDrawQueue::GetInstance().DrawDebugLine(PCOM, PActor, Grey, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
		
			if (Index >= 0)
			{
				//FDebugDrawQueue::GetInstance().DrawDebugString(PCOM + FontHeight * FVec3(0, 0, 1), FString::Format(TEXT("{0}{1}"), { Particle->IsKinematic()? TEXT("K"): TEXT("D"), Index }), nullptr, FColor::White, KINDA_SMALL_NUMBER, false, FontScale);
			}

			if ((VelScale > 0.0f) && (Particle->V().Size() > KINDA_SMALL_NUMBER))
			{
				FDebugDrawQueue::GetInstance().DrawDebugLine(PCOM, PCOM + Particle->V() * VelScale, Red, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			}
			if ((AngVelScale > 0.0f) && (Particle->W().Size() > KINDA_SMALL_NUMBER))
			{
				FDebugDrawQueue::GetInstance().DrawDebugLine(PCOM, PCOM + Particle->W() * AngVelScale, Green, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			}
		}

		void DrawCollisionImpl(const FRigidTransform3& SpaceTransform, const FCollisionConstraintBase& Contact, float ColorScale)
		{
			FVec3 Location = SpaceTransform.TransformPosition(Contact.GetLocation());
			FVec3 Normal = SpaceTransform.TransformVector(Contact.GetNormal());

			if (ContactWidth > 0)
			{
				bool bIsManifold = (Contact.GetType() == FCollisionConstraintBase::FType::MultiPoint) && Contact.As<FRigidBodyMultiPointContactConstraint>()->IsManifoldValid();
				if (!bIsManifold)
				{
					FColor C0 = (ColorScale * FColor(200, 0, 0)).ToFColor(false);
					FMatrix Axes = FRotationMatrix::MakeFromX(Normal);
					FDebugDrawQueue::GetInstance().DrawDebugCircle(Location, DrawScale * ContactWidth, 12, C0, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness, Axes.GetUnitAxis(EAxis::Y), Axes.GetUnitAxis(EAxis::Z), false);
				}
				else
				{
					const FRigidBodyMultiPointContactConstraint& MultiPointConstraint = *Contact.As<FRigidBodyMultiPointContactConstraint>();
					int32 PlaneOwnerIndex = MultiPointConstraint.GetManifoldPlaneOwnerIndex();
					int32 PointsOwnerIndex = 1 - PlaneOwnerIndex;
					FColor C0 = (PlaneOwnerIndex == 0) ? (ColorScale * FColor(0, 200, 0)).ToFColor(false) : (ColorScale * FColor(0, 0, 200)).ToFColor(false);
					
					FMatrix Axes = FRotationMatrix::MakeFromX(Normal);
					FDebugDrawQueue::GetInstance().DrawDebugCircle(Location, DrawScale * ContactWidth, 12, C0, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness, Axes.GetUnitAxis(EAxis::Y), Axes.GetUnitAxis(EAxis::Z), false);
					
					TConstGenericParticleHandle<FReal, 3> PointsParticle = MultiPointConstraint.Particle[PointsOwnerIndex];
					FRigidTransform3 PointsImplicitTransform = MultiPointConstraint.ImplicitTransform[PointsOwnerIndex];
					FRigidTransform3 PointsTransform = PointsImplicitTransform * FParticleUtilities::GetActorWorldTransform(PointsParticle) * SpaceTransform;
					for (int32 SampleIndex = 1; SampleIndex < MultiPointConstraint.NumManifoldPoints(); ++SampleIndex)
					{
						FVec3 S0 = PointsTransform.TransformPosition(MultiPointConstraint.GetManifoldPoint(SampleIndex - 1));
						FVec3 S1 = PointsTransform.TransformPosition(MultiPointConstraint.GetManifoldPoint(SampleIndex));
						FDebugDrawQueue::GetInstance().DrawDebugLine(S0, S1, FColor::Orange, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
					}
				}
			}
			if (ContactLen > 0)
			{
				FColor C1 = (ColorScale * FColor(255, 0, 0)).ToFColor(false);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Location, Location + DrawScale * ContactLen * Normal, C1, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			}
			if (ContactPhiWidth > 0 && Contact.GetPhi() < FLT_MAX)
			{
				FColor C2 = (ColorScale * FColor(128, 128, 0)).ToFColor(false);
				FMatrix Axes = FRotationMatrix::MakeFromX(Normal);
				FDebugDrawQueue::GetInstance().DrawDebugCircle(Location - Contact.GetPhi() * Normal, DrawScale * ContactPhiWidth, 12, C2, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness, Axes.GetUnitAxis(EAxis::Y), Axes.GetUnitAxis(EAxis::Z), false);
			}
			if (ContactOwnerWidth > 0)
			{
				FColor C3 = (ColorScale * FColor(128, 128, 128)).ToFColor(false);
				FMatrix Axes = FRotationMatrix::MakeFromX(Normal);
				FVec3 P0 = SpaceTransform.TransformPosition(Contact.Particle[0]->X());
				FVec3 P1 = SpaceTransform.TransformPosition(Contact.Particle[1]->X());
				FDebugDrawQueue::GetInstance().DrawDebugLine(Location, P0, C3, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness * 0.5f);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Location, P1, C3, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness * 0.5f);
			}

			// Draw the particle (mass frame) coordinates
			{
				//DrawParticleTransformImpl(FRigidTransform3::Identity, Contact.Particle[0], INDEX_NONE, 1.0f);
				//DrawParticleTransformImpl(FRigidTransform3::Identity, Contact.Particle[1], INDEX_NONE, 1.0f);
				//DebugDraw::DrawParticleShapes(FRigidTransform3(), Contact.Particle[0], FColor::Green);
				//DebugDraw::DrawParticleShapes(FRigidTransform3(), Contact.Particle[1], FColor::Green);
			}
		}
		
		void DrawCollisionImpl(const FRigidTransform3& SpaceTransform, const FPBDCollisionConstraintHandle* ConstraintHandle, float ColorScale)
		{
			DrawCollisionImpl(SpaceTransform, ConstraintHandle->GetContact(), ColorScale);
		}

		void DrawJointConstraintImpl(const FRigidTransform3& SpaceTransform, const FVec3& InPa, const FVec3& InCa, const FVec3& InXa, const FMatrix33& Ra, const FVec3& InPb, const FVec3& InCb, const FVec3& InXb, const FMatrix33& Rb, int32 IslandIndex, int32 LevelIndex, int32 ColorIndex, int32 BatchIndex, int32 Index, FReal ColorScale, uint32 FeatureMask)
		{
			using namespace Chaos::DebugDraw;
			FColor R = (ColorScale * FColor::Red).ToFColor(false);
			FColor G = (ColorScale * FColor::Green).ToFColor(false);
			FColor B = (ColorScale * FColor::Blue).ToFColor(false);
			FColor C = (ColorScale * FColor::Cyan).ToFColor(false);
			FColor M = (ColorScale * FColor::Magenta).ToFColor(false);
			FColor Y = (ColorScale * FColor::Yellow).ToFColor(false);
			FVec3 Pa = SpaceTransform.TransformPosition(InPa);
			FVec3 Pb = SpaceTransform.TransformPosition(InPb);
			FVec3 Ca = SpaceTransform.TransformPosition(InCa);
			FVec3 Cb = SpaceTransform.TransformPosition(InCb);
			FVec3 Xa = SpaceTransform.TransformPosition(InXa);
			FVec3 Xb = SpaceTransform.TransformPosition(InXb);

			if (FeatureMask & (uint32)EDebugDrawJointFeature::ActorConnector)
			{
				const FReal ConnectorThickness = 1.5f * LineThickness;
				const FReal CoMSize = DrawScale * JointComSize;
				// Leave a gap around the actor position so we can see where the center is
				FVec3 Sa = Pa;
				const FReal Lena = (Xa - Pa).Size();
				if (Lena > KINDA_SMALL_NUMBER)
				{
					Sa = FMath::Lerp(Pa, Xa, FMath::Clamp(CoMSize / Lena, 0.0f, 1.0f));
				}
				FVec3 Sb = Pb;
				const FReal Lenb = (Xb - Pb).Size();
				if (Lenb > KINDA_SMALL_NUMBER)
				{
					Sb = FMath::Lerp(Pb, Xb, FMath::Clamp(CoMSize / Lena, 0.0f, 1.0f));
				}
				FDebugDrawQueue::GetInstance().DrawDebugLine(Pa, Sa, FColor::White, false, KINDA_SMALL_NUMBER, DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Pb, Sb, FColor::White, false, KINDA_SMALL_NUMBER, DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Sa, Xa, R, false, KINDA_SMALL_NUMBER, DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Sb, Xb, C, false, KINDA_SMALL_NUMBER, DrawPriority, ConnectorThickness);
			}
			if (FeatureMask & (uint32)EDebugDrawJointFeature::CoMConnector)
			{
				const FReal ConnectorThickness = 1.5f * LineThickness;
				const FReal CoMSize = DrawScale * JointComSize;
				// Leave a gap around the body position so we can see where the center is
				FVec3 Sa = Ca;
				const FReal Lena = (Xa - Ca).Size();
				if (Lena > KINDA_SMALL_NUMBER)
				{
					Sa = FMath::Lerp(Ca, Xa, FMath::Clamp(CoMSize / Lena, 0.0f, 1.0f));
				}
				FVec3 Sb = Cb;
				const FReal Lenb = (Xb - Cb).Size();
				if (Lenb > KINDA_SMALL_NUMBER)
				{
					Sb = FMath::Lerp(Cb, Xb, FMath::Clamp(CoMSize / Lena, 0.0f, 1.0f));
				}
				FDebugDrawQueue::GetInstance().DrawDebugLine(Ca, Sa, FColor::Black, false, KINDA_SMALL_NUMBER, DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Cb, Sb, FColor::Black, false, KINDA_SMALL_NUMBER, DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Sa, Xa, R, false, KINDA_SMALL_NUMBER, DrawPriority, ConnectorThickness);
				FDebugDrawQueue::GetInstance().DrawDebugLine(Sb, Xb, C, false, KINDA_SMALL_NUMBER, DrawPriority, ConnectorThickness);
			}
			if (FeatureMask & (uint32)EDebugDrawJointFeature::Stretch)
			{
				const FReal StretchThickness = 3.0f * LineThickness;
				FDebugDrawQueue::GetInstance().DrawDebugLine(Xa, Xb, M, false, KINDA_SMALL_NUMBER, DrawPriority, StretchThickness);
			}
			if (FeatureMask & (uint32)EDebugDrawJointFeature::Axes)
			{
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xa, Xa + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Ra.GetAxis(0)), DrawScale * ArrowSize, R, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xa, Xa + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Ra.GetAxis(1)), DrawScale * ArrowSize, G, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xa, Xa + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Ra.GetAxis(2)), DrawScale * ArrowSize, B, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xb, Xb + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Rb.GetAxis(0)), DrawScale * ArrowSize, C, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xb, Xb + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Rb.GetAxis(1)), DrawScale * ArrowSize, M, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
				FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Xb, Xb + DrawScale * ConstraintAxisLen * SpaceTransform.TransformVector(Rb.GetAxis(2)), DrawScale * ArrowSize, Y, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			}
			FVec3 TextPos = Xb;
			if ((FeatureMask & (uint32)EDebugDrawJointFeature::Level) && (LevelIndex >= 0))
			{
				FDebugDrawQueue::GetInstance().DrawDebugString(TextPos, FString::Format(TEXT("{0}"), { LevelIndex }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, FontScale);
				TextPos += FontHeight * FVec3(0, 0, 1);
			}
			if ((FeatureMask & (uint32)EDebugDrawJointFeature::Index) && (Index >= 0))
			{
				FDebugDrawQueue::GetInstance().DrawDebugString(TextPos, FString::Format(TEXT("{0}"), { Index }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, FontScale);
				TextPos += FontHeight * FVec3(0, 0, 1);
			}
			if ((FeatureMask & (uint32)EDebugDrawJointFeature::Color) && (ColorIndex >= 0))
			{
				FDebugDrawQueue::GetInstance().DrawDebugString(TextPos, FString::Format(TEXT("{0}"), { ColorIndex }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, FontScale);
				TextPos += FontHeight * FVec3(0, 0, 1);
			}
			if ((FeatureMask & (uint32)EDebugDrawJointFeature::Batch) && (BatchIndex >= 0))
			{
				FDebugDrawQueue::GetInstance().DrawDebugString(TextPos, FString::Format(TEXT("{0}"), { BatchIndex }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, FontScale);
				TextPos += FontHeight * FVec3(0, 0, 1);
			}
			if ((FeatureMask & (uint32)EDebugDrawJointFeature::Island) && (IslandIndex >= 0))
			{
				FDebugDrawQueue::GetInstance().DrawDebugString(TextPos, FString::Format(TEXT("{0}"), { IslandIndex }), nullptr, FColor::Red, KINDA_SMALL_NUMBER, false, FontScale);
				TextPos += FontHeight * FVec3(0, 0, 1);
			}
		}

		void DrawJointConstraintImpl(const FRigidTransform3& SpaceTransform, const FPBDJointConstraintHandle* ConstraintHandle, FReal ColorScale, uint32 FeatureMask)
		{
			TVector<TGeometryParticleHandle<FReal, 3>*, 2> ConstrainedParticles = ConstraintHandle->GetConstrainedParticles();
			auto RigidParticle0 = ConstrainedParticles[0]->CastToRigidParticle();
			auto RigidParticle1 = ConstrainedParticles[1]->CastToRigidParticle();
			if ((RigidParticle0 && RigidParticle0->ObjectState() == EObjectStateType::Dynamic) || (RigidParticle1 && RigidParticle1->ObjectState() == EObjectStateType::Dynamic))
			{
				FVec3 Pa = FParticleUtilities::GetActorWorldTransform(TConstGenericParticleHandle<FReal, 3>(ConstraintHandle->GetConstrainedParticles()[1])).GetTranslation();
				FVec3 Pb = FParticleUtilities::GetActorWorldTransform(TConstGenericParticleHandle<FReal, 3>(ConstraintHandle->GetConstrainedParticles()[0])).GetTranslation();
				FVec3 Ca = FParticleUtilities::GetCoMWorldPosition(TConstGenericParticleHandle<FReal, 3>(ConstraintHandle->GetConstrainedParticles()[1]));
				FVec3 Cb = FParticleUtilities::GetCoMWorldPosition(TConstGenericParticleHandle<FReal, 3>(ConstraintHandle->GetConstrainedParticles()[0]));
				FVec3 Xa, Xb;
				FMatrix33 Ra, Rb;
				ConstraintHandle->CalculateConstraintSpace(Xa, Ra, Xb, Rb);
				DrawJointConstraintImpl(SpaceTransform, Pa, Ca, Xa, Ra, Pb, Cb, Xb, Rb, ConstraintHandle->GetConstraintIsland(), ConstraintHandle->GetConstraintLevel(), ConstraintHandle->GetConstraintColor(), ConstraintHandle->GetConstraintBatch(), ConstraintHandle->GetConstraintIndex(), ColorScale, FeatureMask);
			}
		}

		void DrawSimulationSpaceImpl(const FSimulationSpace& SimSpace)
		{
			const FVec3 Pos = SimSpace.Transform.GetLocation();
			const FRotation3& Rot = SimSpace.Transform.GetRotation();
			const FMatrix33 Rotm = Rot.ToMatrix();
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Pos, Pos + DrawScale * BodyAxisLen * Rotm.GetAxis(0), DrawScale * ArrowSize, FColor::Red, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Pos, Pos + DrawScale * BodyAxisLen * Rotm.GetAxis(1), DrawScale * ArrowSize, FColor::Green, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Pos, Pos + DrawScale * BodyAxisLen * Rotm.GetAxis(2), DrawScale * ArrowSize, FColor::Blue, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);

			FDebugDrawQueue::GetInstance().DrawDebugLine(Pos, Pos + VelScale * SimSpace.LinearVelocity, FColor::Cyan, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugLine(Pos, Pos + AngVelScale * SimSpace.AngularVelocity, FColor::Cyan, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugLine(Pos, Pos + 0.01f * VelScale * SimSpace.LinearAcceleration, FColor::Yellow, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
			FDebugDrawQueue::GetInstance().DrawDebugLine(Pos, Pos + 0.01f * AngVelScale * SimSpace.AngularAcceleration, FColor::Orange, false, KINDA_SMALL_NUMBER, DrawPriority, LineThickness);
		}

#endif

		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<TGeometryParticles<float, 3>>& ParticlesView, const FColor& Color)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleShapesImpl(SpaceTransform, GetHandleHelper(&Particle), Color);
				}
			}
#endif
		}

		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<TKinematicGeometryParticles<float, 3>>& ParticlesView, const FColor& Color)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleShapesImpl(SpaceTransform, GetHandleHelper(&Particle), Color);
				}
			}
#endif
		}

		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<TPBDRigidParticles<float, 3>>& ParticlesView, const FColor& Color)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleShapesImpl(SpaceTransform, GetHandleHelper(&Particle), Color);
				}
			}
#endif
		}

		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TGeometryParticleHandle<float, 3>* Particle, const FColor& Color)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				DrawParticleShapesImpl(SpaceTransform, Particle, Color);
			}
#endif
		}

		void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TGeometryParticle<float, 3>* Particle, const FColor& Color)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				DrawParticleShapesImpl(SpaceTransform, Particle, Color);
			}
#endif
		}

		void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<TGeometryParticles<float, 3>>& ParticlesView, const FColor& Color)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleBoundsImpl(SpaceTransform, GetHandleHelper(&Particle), Color);
				}
			}
#endif
		}

		void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<TKinematicGeometryParticles<float, 3>>& ParticlesView, const FColor& Color)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleBoundsImpl(SpaceTransform, GetHandleHelper(&Particle), Color);
				}
			}
#endif
		}

		void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<TPBDRigidParticles<float, 3>>& ParticlesView, const FColor& Color)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (auto& Particle : ParticlesView)
				{
					DrawParticleBoundsImpl(SpaceTransform, GetHandleHelper(&Particle), Color);
				}
			}
#endif
		}

		void DrawParticleTransforms(const FRigidTransform3& SpaceTransform, const TParticleView<TGeometryParticles<float, 3>>& ParticlesView)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				int32 Index = 0;
				for (auto& Particle : ParticlesView)
				{
					DrawParticleTransformImpl(SpaceTransform, GetHandleHelper(&Particle), Index++, 1.0f);
				}
			}
#endif
		}

		void DrawParticleTransforms(const FRigidTransform3& SpaceTransform, const TParticleView<TKinematicGeometryParticles<float, 3>>& ParticlesView)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				int32 Index = 0;
				for (auto& Particle : ParticlesView)
				{
					DrawParticleTransformImpl(SpaceTransform, GetHandleHelper(&Particle), Index++, 1.0f);
				}
			}
#endif
		}

		void DrawParticleTransforms(const FRigidTransform3& SpaceTransform, const TParticleView<TPBDRigidParticles<float, 3>>& ParticlesView)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				int32 Index = 0;
				for (auto& Particle : ParticlesView)
				{
					DrawParticleTransformImpl(SpaceTransform, GetHandleHelper(&Particle), Index++, 1.0f);
				}
			}
#endif
		}

		void DrawParticleCollisions(const FRigidTransform3& SpaceTransform, const TGeometryParticleHandle<float, 3>* Particle, const FPBDCollisionConstraints& Collisions)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (const Chaos::FPBDCollisionConstraintHandle * ConstraintHandle : Collisions.GetConstConstraintHandles())
				{
					TVector<const TGeometryParticleHandle<float, 3>*, 2> ConstrainedParticles = ConstraintHandle->GetConstrainedParticles();
					if ((ConstrainedParticles[0] == Particle) || (ConstrainedParticles[1] == Particle))
					{
						DrawCollisionImpl(SpaceTransform, ConstraintHandle, 1.0f);
					}
				}
			}
#endif
		}

		void DrawCollisions(const FRigidTransform3& SpaceTransform, const FPBDCollisionConstraints& Collisions, float ColorScale)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (int32 ConstraintIndex = 0; ConstraintIndex < Collisions.NumConstraints(); ++ConstraintIndex)
				{
					DrawCollisionImpl(SpaceTransform, Collisions.GetConstraint(ConstraintIndex), ColorScale);
				}
			}
#endif
		}

		void DrawCollisions(const FRigidTransform3& SpaceTransform, const TArray<FPBDCollisionConstraintHandle*>& ConstraintHandles, float ColorScale)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (const FPBDCollisionConstraintHandle* ConstraintHandle : ConstraintHandles)
				{
					DrawCollisionImpl(SpaceTransform, ConstraintHandle, ColorScale);
				}
			}
#endif
		}


		void DrawJointConstraints(const FRigidTransform3& SpaceTransform, const TArray<FPBDJointConstraintHandle*>& ConstraintHandles, float ColorScale, uint32 FeatureMask)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (const FPBDJointConstraintHandle* ConstraintHandle : ConstraintHandles)
				{
					DrawJointConstraintImpl(SpaceTransform, ConstraintHandle, ColorScale, FeatureMask);
				}
			}
#endif
		}

		void DrawJointConstraints(const FRigidTransform3& SpaceTransform, const FPBDJointConstraints& Constraints, float ColorScale, uint32 FeatureMask)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.NumConstraints(); ++ConstraintIndex)
				{
					DrawJointConstraintImpl(SpaceTransform, Constraints.GetConstraintHandle(ConstraintIndex), ColorScale, FeatureMask);
				}
			}
#endif
		}

		void DrawSimulationSpace(const FSimulationSpace& SimSpace)
		{
#if CHAOS_DEBUG_DRAW
			if (FDebugDrawQueue::IsDebugDrawingEnabled())
			{
				DrawSimulationSpaceImpl(SimSpace);
			}
#endif
		}
	}
}
