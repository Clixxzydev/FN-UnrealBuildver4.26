// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GroupTopology.h"
#include "Spatial/GeometrySet3.h"
#include "DynamicMeshAABBTree3.h"
#include "RayTypes.h"

class FToolDataVisualizer;
struct FViewCameraState;

/**
 * FGroupTopologySelector implements selection behavior for a FGroupTopology mesh.
 * Groups, Group Edges, and Corners can be selected dependings on the settings passed in.
 *
 * Internally a FGeometrySet3 is constructed to support ray-hit testing against the edges and corners.
 * 
 * Note that to hit-test against the mesh you have to provide a your own FDynamicMeshAABBTree3.
 * You do this by providing a callback via SetSpatialSource(). The reason for this is that
 * (1) owners of FGroupTopologySelector likely already have a BVTree and (2) if the use case
 * is deformation, we need to make sure the owner has recomputed the BVTree before we call functions 
 * on it. The callback you provide should do that!
 * 
 * DrawSelection() can be used to visualize a selection via line/circle drawing.
 * 
 * @todo optionally have an internal mesh AABBTree that can be used when owner does not provide one?
 */
class MODELINGCOMPONENTS_API FGroupTopologySelector
{
public:

	//
	// Configuration variables
	// 

	/**
	 * Determines the behavior of a FindSelectedElement() call.
	 */
	struct FSelectionSettings
	{
		bool bEnableFaceHits = true;
		bool bEnableEdgeHits = true;
		bool bEnableCornerHits = true;
		
		// The following are mainly useful for ortho viewport selection:

		// Prefer an edge projected to a point rather than the point, and a face projected to an edge
		// rather than the edge.
		bool bPreferProjectedElement = false;

		// If the first element is valid, select all elements behind it that are aligned with it.
		bool bSelectDownRay = false;

		//Do not check whether the closest element is occluded.
		bool bIgnoreOcclusion = false;
	};

	/** 
	 * This is the function we use to determine if a point on a corner/edge is close enough
	 * to the hit-test ray to treat as a "hit". By default this is Euclidean distance with
	 * a tolerance of 1.0. You probably need to replace this with your own function.
	 */
	TFunction<bool(const FVector3d&, const FVector3d&)> PointsWithinToleranceTest;

public:
	FGroupTopologySelector();

	/**
	 * Initialize the selector with the given Mesh and Topology.
	 * This does not create the internal data structures, this happens lazily on GetGeometrySet()
	 */
	void Initialize(const FDynamicMesh3* Mesh, const FGroupTopology* Topology);

	/**
	 * Provide a function that will return an AABBTree for the Mesh.
	 * See class comment for why this is necessary.
	 */
	void SetSpatialSource(TFunction<FDynamicMeshAABBTree3*(void)> GetSpatialFunc)
	{
		GetSpatial = GetSpatialFunc;
	}

	/**
	 * Notify the Selector that the mesh has changed. 
	 * @param bTopologyDeformed if this is true, the mesh vertices have been moved so we need to update bounding boxes/etc
	 * @param bTopologyModified if this is true, topology has changed and we need to rebuild spatial data structures from scratch
	 */
	void Invalidate(bool bTopologyDeformed, bool bTopologyModified);

	/**
	 * @return the internal GeometrySet. This does lazy updating of the GeometrySet, so this function may take some time.
	 */
	const FGeometrySet3& GetGeometrySet();

	/**
	 * Find which element was selected for a given ray.
	 *
	 * @param Settings settings that determine what can be selected.
	 * @param Ray hit-test ray
	 * @param ResultOut resulting selection. Will not be cleared before use. At most one of the Groups/Corners/Edges
	 *  members will be added to.
	 * @param SelectedPositionOut The point on the ray nearest to the selected element
	 * @param SelectedNormalOut the normal at that point if ResultOut contains a selected face, otherwise uninitialized
	 * @param EdgeSegmentIdOut When not null, if the selected element is a group edge, the segment id of the segment
	 *   that was actually clicked within the edge polyline will be stored here.
	 *
	 * @return true if something was selected
	 */
	bool FindSelectedElement(const FSelectionSettings& Settings, const FRay3d& Ray, FGroupTopologySelection& ResultOut,
		FVector3d& SelectedPositionOut, FVector3d& SelectedNormalOut, int32* EdgeSegmentIdOut = nullptr);

	/**
	 * Using the edges in the given selection as starting points, add any "edge loops" containing the edges. An 
	 * edge loop is a sequence of edges that passes through valence-4 corners through the opposite edge, and may
	 * not actually form a complete loop if they hit a non-valence-4 corner.
	 *
	 * @param Selection Selection to expand.
	 * @param bool true if selection was modified (i.e., were the already selected edges part of any edge loops whose
	 *  member edges were not yet all selected).
	 */
	bool ExpandSelectionByEdgeLoops(FGroupTopologySelection& Selection);

	/**
	 * Render the given selection with the default settings of the FToolDataVisualizer.
	 * Selected edges are drawn as lines, and selected corners are drawn as small view-facing circles.
	 * (Currently seleced faces are not draw)
	 */
	void DrawSelection(const FGroupTopologySelection& Selection, FToolDataVisualizer* Renderer, const FViewCameraState* CameraState);


public:
	// internal rendering parameters
	float VisualAngleSnapThreshold = 0.5;


protected:
	const FDynamicMesh3* Mesh = nullptr;
	const FGroupTopology* Topology = nullptr;

	TFunction<FDynamicMeshAABBTree3*(void)> GetSpatial = nullptr;

	bool bGeometryInitialized = false;
	bool bGeometryUpToDate = false;
	FGeometrySet3 GeometrySet;

	bool DoCornerBasedSelection(const FSelectionSettings& Settings, const FRay3d& Ray,
		FDynamicMeshAABBTree3* Spatial, const FGeometrySet3& TopoSpatial, 
		FGroupTopologySelection& ResultOut, FVector3d& SelectedPositionOut, int32 *EdgeSegmentIdOut) const;
	bool DoEdgeBasedSelection(const FSelectionSettings& Settings, const FRay3d& Ray,
		FDynamicMeshAABBTree3* Spatial, const FGeometrySet3& TopoSpatial,
		FGroupTopologySelection& ResultOut, FVector3d& SelectedPositionOut, int32 *EdgeSegmentIdOut) const;
};