// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterConfigTypes.h"


/**
 * Public config manager interface
 */
class IDisplayClusterConfigManager
{
public:
	virtual ~IDisplayClusterConfigManager() = 0
	{ }

	// Returns amount of cluster nodes
	virtual int32 GetClusterNodesAmount() const = 0;
	// 
	virtual TArray<FDisplayClusterConfigClusterNode> GetClusterNodes() const = 0;
	virtual bool GetClusterNode(const FString& ClusterNodeId, FDisplayClusterConfigClusterNode& CfgClusterNode) const = 0;
	virtual bool GetMasterClusterNode(FDisplayClusterConfigClusterNode& cnode) const = 0;

	virtual int32 GetWindowsAmount() const = 0;
	virtual TArray<FDisplayClusterConfigWindow> GetWindows() const = 0;
	virtual bool GetWindow(const FString& WindowID, FDisplayClusterConfigWindow& CfgWindow) const = 0;
	virtual bool GetMasterWindow(FDisplayClusterConfigWindow& CfgWindow) const = 0;

	virtual int32 GetScreensAmount() const = 0;
	virtual TArray<FDisplayClusterConfigScreen> GetScreens() const = 0;
	virtual bool GetScreen(const FString& ScreenID, FDisplayClusterConfigScreen& CfgScreen) const = 0;

	virtual int32 GetCamerasAmount() const = 0;
	virtual TArray<FDisplayClusterConfigCamera> GetCameras() const = 0;
	virtual bool GetCamera(const FString& CameraID, FDisplayClusterConfigCamera& CfgCamera) const = 0;

	virtual int32 GetViewportsAmount() const = 0;
	virtual TArray<FDisplayClusterConfigViewport> GetViewports() const = 0;
	virtual bool GetViewport(const FString& ViewportID, FDisplayClusterConfigViewport& CfgViewport) const = 0;

	virtual int32 GetPostprocessAmount() const = 0;
	virtual TArray<FDisplayClusterConfigPostprocess> GetPostprocess() const = 0;
	virtual bool GetPostprocess(const FString& PostprocessID, FDisplayClusterConfigPostprocess& CfgPostprocess) const = 0;

	virtual int32 GetSceneNodesAmount() const = 0;
	virtual TArray<FDisplayClusterConfigSceneNode> GetSceneNodes() const = 0;
	virtual bool GetSceneNode(const FString& SceneNodeID, FDisplayClusterConfigSceneNode& CfgSceneNode) const = 0;

	virtual int32 GetInputDevicesAmount() const = 0;
	virtual TArray<FDisplayClusterConfigInput> GetInputDevices() const = 0;
	virtual bool GetInputDevice(const FString& InputDeviceID, FDisplayClusterConfigInput& CfgInputDevice) const = 0;

	virtual TArray<FDisplayClusterConfigInputSetup> GetInputSetupRecords() const = 0;
	virtual bool GetInputSetupRecord(const FString& InputSetupID, FDisplayClusterConfigInputSetup& CfgInputSetup) const = 0;

	virtual TArray<FDisplayClusterConfigProjection> GetProjections() const = 0;
	virtual bool GetProjection(const FString& ProjectionID, FDisplayClusterConfigProjection& CfgProjection) const = 0;

	virtual FDisplayClusterConfigGeneral GetConfigGeneral() const = 0;
	virtual FDisplayClusterConfigStereo  GetConfigStereo()  const = 0;
	virtual FDisplayClusterConfigRender  GetConfigRender()  const = 0;
	virtual FDisplayClusterConfigNvidia  GetConfigNvidia()  const = 0;
	virtual FDisplayClusterConfigNetwork GetConfigNetwork() const = 0;
	virtual FDisplayClusterConfigDebug   GetConfigDebug()   const = 0;
	virtual FDisplayClusterConfigCustom  GetConfigCustom()  const = 0;

	virtual FString GetFullPathToFile(const FString& FileName) const = 0;
	virtual FString GetFullPathToNewFile(const FString& FileName) const = 0;
};
