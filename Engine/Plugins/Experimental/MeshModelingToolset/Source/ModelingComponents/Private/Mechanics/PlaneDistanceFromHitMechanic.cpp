// Copyright Epic Games, Inc. All Rights Reserved.

#include "Mechanics/PlaneDistanceFromHitMechanic.h"
#include "ToolSceneQueriesUtil.h"
#include "MeshQueries.h"
#include "SceneManagement.h"
#include "MeshTransforms.h"
#include "Distance/DistLine3Ray3.h"





void UPlaneDistanceFromHitMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	UInteractionMechanic::Setup(ParentToolIn);

	//Properties = NewObject<UPolygonSelectionMechanicProperties>(this);
	//AddToolPropertySource(Properties);

	//// set up visualizers
	//PolyEdgesRenderer.LineColor = FLinearColor::Red;
	//PolyEdgesRenderer.LineThickness = 2.0;
	//HilightRenderer.LineColor = FLinearColor::Green;
	//HilightRenderer.LineThickness = 4.0f;
	//SelectionRenderer.LineColor = LinearColors::Gold3f<FLinearColor>();
	//SelectionRenderer.LineThickness = 4.0f;
}


void UPlaneDistanceFromHitMechanic::Initialize(FDynamicMesh3&& HitTargetMesh, const FFrame3d& PlaneFrameWorld, bool bMeshInWorldCoords)
{
	PreviewHeightFrame = PlaneFrameWorld;

	PreviewHeightTarget = MoveTemp(HitTargetMesh);
	if (bMeshInWorldCoords)
	{
		MeshTransforms::WorldToFrameCoords(PreviewHeightTarget, PreviewHeightFrame);
	}

	PreviewHeightTargetAABB.SetMesh(&PreviewHeightTarget);
}



void UPlaneDistanceFromHitMechanic::UpdateCurrentDistance(const FRay& WorldRay)
{
	float NearestHitDist = TNumericLimits<float>::Max();
	float NearestHitHeight = 1.0f;
	FFrame3d NearestHitFrameWorld;
	bool bFoundHit = false;

	// cast ray at target object
	FRay3d LocalRay = PreviewHeightFrame.ToFrame(WorldRay);
	int HitTID = PreviewHeightTargetAABB.FindNearestHitTriangle(LocalRay);
	if (HitTID >= 0)
	{
		FIntrRay3Triangle3d IntrQuery =
			TMeshQueries<FDynamicMesh3>::TriangleIntersection(PreviewHeightTarget, HitTID, LocalRay);
		FVector3d HitPosLocal = LocalRay.PointAt(IntrQuery.RayParameter);
		FVector3d HitNormalLocal = PreviewHeightTarget.GetTriNormal(HitTID);

		NearestHitFrameWorld = FFrame3d(
			PreviewHeightFrame.FromFramePoint(HitPosLocal),
			PreviewHeightFrame.FromFrameVector(HitNormalLocal));
		NearestHitHeight = HitPosLocal.Z;
		NearestHitDist = WorldRay.GetParameter((FVector)NearestHitFrameWorld.Origin);

		bFoundHit = true;
	}


	// cast ray into scene
	if (WorldHitQueryFunc)
	{
		FHitResult WorldHitResult;
		bool bHitWorld = WorldHitQueryFunc(WorldRay, WorldHitResult);
		if (bHitWorld)
		{
			float WorldHitDist = WorldRay.GetParameter(WorldHitResult.ImpactPoint);
			if (WorldHitDist < NearestHitDist)
			{
				NearestHitFrameWorld = FFrame3d(WorldHitResult.ImpactPoint, WorldHitResult.ImpactNormal);
				FVector3d HitPosLocal = PreviewHeightFrame.ToFramePoint(FVector3d(WorldHitResult.ImpactPoint));
				NearestHitHeight = HitPosLocal.Z;
				NearestHitDist = WorldHitDist;
				bFoundHit = true;
			}
		}
	}

	if (bFoundHit == false && bFallbackToLineAxisPoint)
	{
		FDistLine3Ray3d Distance(FLine3d(PreviewHeightFrame.Origin, PreviewHeightFrame.Z()), FRay3d(WorldRay));
		float DistSqr = Distance.GetSquared();
		float WorldHitDist = WorldRay.GetParameter( (FVector)Distance.RayClosestPoint );
		NearestHitFrameWorld = FFrame3d(Distance.RayClosestPoint, (Distance.RayClosestPoint - Distance.LineClosestPoint).Normalized());
		FVector3d HitPosLocal = PreviewHeightFrame.ToFramePoint(Distance.RayClosestPoint);
		NearestHitHeight = HitPosLocal.Z;
		NearestHitDist = WorldHitDist;
		bFoundHit = true;
	}

	if (bFoundHit)
	{
		if ( WorldPointSnapFunc)
		{
			FVector3d SnapPosWorld;
			if ( WorldPointSnapFunc(NearestHitFrameWorld.Origin, SnapPosWorld) )
			{
				NearestHitFrameWorld.Origin = SnapPosWorld;
				FVector3d LocalPos = PreviewHeightFrame.ToFramePoint((FVector3d)NearestHitFrameWorld.Origin);
				NearestHitHeight = (float)LocalPos.Z;
			}
		}

		CurrentHitPosFrameWorld = NearestHitFrameWorld;
		CurrentHeight = NearestHitHeight;
	}
	else
	{
		//CurrentHeight = DefaultHeight; 
	}

}



void UPlaneDistanceFromHitMechanic::Render(IToolsContextRenderAPI* RenderAPI)
{
	FViewCameraState CameraState = RenderAPI->GetCameraState();
	float PDIScale = CameraState.GetPDIScalingFactor();
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

	float Length = 10*PDIScale; float Thickness = 2*PDIScale;
	FColor HitFrameColor(0, 128, 128);
	PDI->DrawLine(
		(FVector)CurrentHitPosFrameWorld.PointAt(-Length, -Length, 0), (FVector)CurrentHitPosFrameWorld.PointAt(Length, Length, 0),
		HitFrameColor, 1, Thickness, 0.0f, true);
	PDI->DrawLine(
		(FVector)CurrentHitPosFrameWorld.PointAt(-Length, Length, 0), (FVector)CurrentHitPosFrameWorld.PointAt(Length, -Length, 0),
		HitFrameColor, 1, Thickness, 0.0f, true);

	FVector PreviewOrigin = (FVector)PreviewHeightFrame.Origin;

	FVector DrawPlaneNormal(PreviewHeightFrame.Z()); // DrawPlaneOrientation.GetAxisZ();

	FColor AxisColor(128, 128, 0);
	PDI->DrawLine(
		PreviewOrigin - 1000 * DrawPlaneNormal, PreviewOrigin + 1000 * DrawPlaneNormal,
		AxisColor, 1, 1.0f*PDIScale, 0.0f, true);

	FColor HeightPosColor(128, 0, 128);
	PDI->DrawLine(
		PreviewOrigin + CurrentHeight * DrawPlaneNormal, (FVector)CurrentHitPosFrameWorld.Origin,
		HeightPosColor, 1, 1.0f*PDIScale, 0.0f, true);


}