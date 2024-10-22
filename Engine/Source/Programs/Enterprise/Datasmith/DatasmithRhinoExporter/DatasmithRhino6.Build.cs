// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	[SupportedPlatforms("Win64")]
	public class DatasmithRhino6 : DatasmithRhinoBase
	{
		public DatasmithRhino6(ReadOnlyTargetRules Target)
			: base(Target)
		{
		}

		public override string GetRhinoVersion()
		{
			return "6";
		}
	}
}
