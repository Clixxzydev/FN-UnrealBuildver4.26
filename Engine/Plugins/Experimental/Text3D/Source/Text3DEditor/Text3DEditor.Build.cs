// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Text3DEditor : ModuleRules
{
	public Text3DEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Text3D",
			"UnrealEd",
		});
	}
}
