// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class VulkanShaderFormat : ModuleRules
{
	public VulkanShaderFormat(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.Add("TargetPlatform");

		// Do not link the module (as that would require the vulkan dll), only the include paths
		PublicIncludePaths.Add("Runtime/VulkanRHI/Public");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"RenderCore",
				"ShaderCompilerCommon",
				"ShaderPreprocessor",
				"RHI", // @todo platplug: This would not be needed if we could move FDataDriveShaderPlatformInfo (and ERHIFeatureLevel) into RenderCore or maybe its own module?
			}
			);

		AddEngineThirdPartyPrivateStaticDependencies(Target, "HLSLCC");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "GlsLang");

		if (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.Win64)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"ShaderConductor",
				"SPIRVReflect"
			);

			string BinaryFolder = Target.UEThirdPartyBinariesDirectory + "ShaderConductor/Win64";
			RuntimeDependencies.Add(BinaryFolder + "/dxcompiler.dll");
			RuntimeDependencies.Add(BinaryFolder + "/ShaderConductor.dll");
		}

		if (Target.Platform != UnrealTargetPlatform.Win64 &&
			Target.Platform != UnrealTargetPlatform.Win32 &&
			Target.Platform != UnrealTargetPlatform.Android &&
			!Target.IsInPlatformGroup(UnrealPlatformGroup.Linux) &&
			Target.Platform != UnrealTargetPlatform.Mac)
		{
			PrecompileForTargets = PrecompileTargetsType.None;
		}

		AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");
	}
}
