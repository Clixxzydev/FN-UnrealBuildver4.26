// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class NavigationSystem : ModuleRules
    {
        public NavigationSystem(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicIncludePaths.AddRange(
                new string[] {
                    "Runtime/NavigationSystem/Public",
                }
                );

            PrivateIncludePaths.AddRange(
                new string[] {
                    "Runtime/NavigationSystem/Private",
                    "Runtime/Engine/Private",
                    "Developer/DerivedDataCache/Public",
                }
                );

            PublicDependencyModuleNames.AddRange(
                new string[] {
                    "Core",
                    "CoreUObject",
                    "Engine",
                }
                );

            PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "RHI",
                    "RenderCore",
                }
                );

			PrivateIncludePathModuleNames.AddRange(
				new string[]
				{
					"TargetPlatform",
				}
				);
            
            if (!Target.bBuildRequiresCookedData && Target.bCompileAgainstEngine)
            {
                DynamicallyLoadedModuleNames.Add("DerivedDataCache");
            }

            SetupModulePhysicsSupport(Target);

			if (Target.bCompileChaos || Target.bUseChaos)
            {
                PublicDependencyModuleNames.AddRange(
                    new string[] {
                        "GeometryCollectionCore",
                        "GeometryCollectionEngine",
                    }
					);
            }

            if (Target.bCompileRecast)
            {
                PrivateDependencyModuleNames.Add("Navmesh");
                PublicDefinitions.Add("WITH_RECAST=1");
                if (Target.bCompileNavmeshSegmentLinks)
                {
                    PublicDefinitions.Add("WITH_NAVMESH_SEGMENT_LINKS=1");
                }
                else
                {
                    PublicDefinitions.Add("WITH_NAVMESH_SEGMENT_LINKS=0");
                }

                if (Target.bCompileNavmeshClusterLinks)
                {
                    PublicDefinitions.Add("WITH_NAVMESH_CLUSTER_LINKS=1");
                }
                else
                {
                    PublicDefinitions.Add("WITH_NAVMESH_CLUSTER_LINKS=0");
                }
            }
            else
            {
                // Because we test WITH_RECAST in public Engine header files, we need to make sure that modules
                // that import us also have this definition set appropriately.  Recast is a private dependency
                // module, so it's definitions won't propagate to modules that import Engine.
                PublicDefinitions.Add("WITH_RECAST=0");
                PublicDefinitions.Add("WITH_NAVMESH_CLUSTER_LINKS=0");
                PublicDefinitions.Add("WITH_NAVMESH_SEGMENT_LINKS=0");
            }

            if (Target.bBuildEditor == true)
            {
                // @todo api: Only public because of WITH_EDITOR and UNREALED_API
                PublicDependencyModuleNames.Add("UnrealEd");
                CircularlyReferencedDependentModules.Add("UnrealEd");
            }
        }
    }
}
