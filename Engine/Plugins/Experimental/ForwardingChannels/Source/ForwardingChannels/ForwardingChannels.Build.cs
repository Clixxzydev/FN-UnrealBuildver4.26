// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ForwardingChannels : ModuleRules
{
	public ForwardingChannels(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
			}
		);
	}
}
