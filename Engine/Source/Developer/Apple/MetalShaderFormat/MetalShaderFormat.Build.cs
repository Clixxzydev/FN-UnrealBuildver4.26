// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetalShaderFormat : ModuleRules
{
	public MetalShaderFormat(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.Add("TargetPlatform");
		PublicIncludePaths.Add("Runtime/Apple/MetalRHI/Public");

		PrivateIncludePaths.AddRange(
			new string[] {
				"Developer/DerivedDataCache/Public",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"RenderCore",
				"ShaderCompilerCommon",
				"ShaderPreprocessor",
				"FileUtilities"
			}
			);

		AddEngineThirdPartyPrivateStaticDependencies(Target, 
			"HLSLCC"
			);

		if (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.Win64)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"ShaderConductor",
				"SPIRVReflect"
			);
		}

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{ 
				"DerivedDataCache",
			}
			);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDelayLoadDLLs.Add("dxcompiler.dll");
			PublicDelayLoadDLLs.Add("ShaderConductor.dll");

			string BinaryFolder = Target.UEThirdPartyBinariesDirectory + "ShaderConductor/Win64";
			RuntimeDependencies.Add(BinaryFolder + "/dxcompiler.dll");
			RuntimeDependencies.Add(BinaryFolder + "/ShaderConductor.dll");
		}
	}
}
