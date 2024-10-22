// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GeometryCollectionCore : ModuleRules
	{
        public GeometryCollectionCore(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("Runtime/Experimental/GeometryCollectionCore/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                    "Chaos",
                    "Voronoi"
                }
            );

			PrivateDefinitions.Add("CHAOS_INCLUDE_LEVEL_1=1");
		}
    }
}
