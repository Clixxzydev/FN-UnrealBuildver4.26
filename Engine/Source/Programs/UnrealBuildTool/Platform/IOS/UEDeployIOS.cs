// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Xml;
using System.IO;
using System.Diagnostics;
using System.Xml.Linq;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	class UEDeployIOS : UEBuildDeploy
	{
		public UEDeployIOS()
		{
		}

		protected UnrealPluginLanguage UPL = null;
		protected delegate bool FilenameFilter(string InFilename);

		public bool ForDistribution
		{
			get { return bForDistribution; }
			set { bForDistribution = value; }
		}
		bool bForDistribution = false;

		protected class VersionUtilities
		{
			public static string BuildDirectory
			{
				get;
			set;
			}
			public static string GameName
			{
				get;
				set;
			}

			public static bool bCustomLaunchscreenStoryboard = false;			

			static string RunningVersionFilename
			{
				get { return Path.Combine(BuildDirectory, GameName + ".PackageVersionCounter"); }
			}

			/// <summary>
			/// Reads the GameName.PackageVersionCounter from disk and bumps the minor version number in it
			/// </summary>
			/// <returns></returns>
			public static string ReadRunningVersion()
			{
				string CurrentVersion = "0.0";
				if (File.Exists(RunningVersionFilename))
				{
					CurrentVersion = File.ReadAllText(RunningVersionFilename);
				}

				return CurrentVersion;
			}

			/// <summary>
			/// Pulls apart a version string of one of the two following formats:
			///	  "7301.15 11-01 10:28"   (Major.Minor Date Time)
			///	  "7486.0"  (Major.Minor)
			/// </summary>
			/// <param name="CFBundleVersion"></param>
			/// <param name="VersionMajor"></param>
			/// <param name="VersionMinor"></param>
			/// <param name="TimeStamp"></param>
			public static void PullApartVersion(string CFBundleVersion, out int VersionMajor, out int VersionMinor, out string TimeStamp)
			{
				// Expecting source to be like "7301.15 11-01 10:28" or "7486.0"
				string[] Parts = CFBundleVersion.Split(new char[] { ' ' });

				// Parse the version string
				string[] VersionParts = Parts[0].Split(new char[] { '.' });

				if (!int.TryParse(VersionParts[0], out VersionMajor))
				{
					VersionMajor = 0;
				}

				if ((VersionParts.Length < 2) || (!int.TryParse(VersionParts[1], out VersionMinor)))
				{
					VersionMinor = 0;
				}

				TimeStamp = "";
				if (Parts.Length > 1)
				{
					TimeStamp = String.Join(" ", Parts, 1, Parts.Length - 1);
				}
			}

			public static string ConstructVersion(int MajorVersion, int MinorVersion)
			{
				return String.Format("{0}.{1}", MajorVersion, MinorVersion);
			}

			/// <summary>
			/// Parses the version string (expected to be of the form major.minor or major)
			/// Also parses the major.minor from the running version file and increments it's minor by 1.
			///
			/// If the running version major matches and the running version minor is newer, then the bundle version is updated.
			///
			/// In either case, the running version is set to the current bundle version number and written back out.
			/// </summary>
			/// <returns>The (possibly updated) bundle version</returns>
			public static string CalculateUpdatedMinorVersionString(string CFBundleVersion)
			{
				// Read the running version and bump it
				int RunningMajorVersion;
				int RunningMinorVersion;

				string DummyDate;
				string RunningVersion = ReadRunningVersion();
				PullApartVersion(RunningVersion, out RunningMajorVersion, out RunningMinorVersion, out DummyDate);
				RunningMinorVersion++;

				// Read the passed in version and bump it
				int MajorVersion;
				int MinorVersion;
				PullApartVersion(CFBundleVersion, out MajorVersion, out MinorVersion, out DummyDate);
				MinorVersion++;

				// Combine them if the stub time is older
				if ((RunningMajorVersion == MajorVersion) && (RunningMinorVersion > MinorVersion))
				{
					// A subsequent cook on the same sync, the only time that we stomp on the stub version
					MinorVersion = RunningMinorVersion;
				}

				// Combine them together
				string ResultVersionString = ConstructVersion(MajorVersion, MinorVersion);

				// Update the running version file
				Directory.CreateDirectory(Path.GetDirectoryName(RunningVersionFilename));
				File.WriteAllText(RunningVersionFilename, ResultVersionString);

				return ResultVersionString;
			}

			/// <summary>
			/// Updates the minor version in the CFBundleVersion key of the specified PList if this is a new package.
			/// Also updates the key EpicAppVersion with the bundle version and the current date/time (no year)
			/// </summary>
			public static string UpdateBundleVersion(string OldPList, string EngineDirectory)
			{
				string CFBundleVersion = "-1";
				if (Environment.GetEnvironmentVariable("IsBuildMachine") != "1")
				{
					int Index = OldPList.IndexOf("CFBundleVersion");
					if (Index != -1)
					{
						int Start = OldPList.IndexOf("<string>", Index) + ("<string>").Length;
						CFBundleVersion = OldPList.Substring(Start, OldPList.IndexOf("</string>", Index) - Start);
						CFBundleVersion = CalculateUpdatedMinorVersionString(CFBundleVersion);
					}
					else
					{
						CFBundleVersion = "0.0";
					}
				}
				else
				{
                    // get the changelist
                    CFBundleVersion = ReadOnlyBuildVersion.Current.Changelist.ToString();

				}

				return CFBundleVersion;
			}
		}

		protected virtual string GetTargetPlatformName()
		{
			return "IOS";
		}

		public static string EncodeBundleName(string PlistValue, string ProjectName)
		{
			string result = PlistValue.Replace("[PROJECT_NAME]", ProjectName).Replace("_", "");
			result = result.Replace("&", "&amp;");
			result = result.Replace("\"", "&quot;");
			result = result.Replace("\'", "&apos;");
			result = result.Replace("<", "&lt;");
			result = result.Replace(">", "&gt;");

			return result;
		}

		public static bool GenerateIOSPList(FileReference ProjectFile, UnrealTargetConfiguration Config, string ProjectDirectory, bool bIsUE4Game, string GameName, bool bIsClient, string ProjectName, string InEngineDir, string AppDirectory, VersionNumber SdkVersion, UnrealPluginLanguage UPL, string BundleID, bool bBuildAsFramework, out bool bSupportsPortrait, out bool bSupportsLandscape, out bool bSkipIcons)
		{
			// generate the Info.plist for future use
			string BuildDirectory = ProjectDirectory + "/Build/IOS";
			string IntermediateDirectory = (bIsUE4Game ? InEngineDir : ProjectDirectory) + "/Intermediate/IOS";
			string PListFile = IntermediateDirectory + "/" + GameName + "-Info.plist";
			ProjectName = !String.IsNullOrEmpty(ProjectName) ? ProjectName : GameName;
			VersionUtilities.BuildDirectory = BuildDirectory;
			VersionUtilities.GameName = GameName;

			// read the old file
			string OldPListData = File.Exists(PListFile) ? File.ReadAllText(PListFile) : "";

			// get the settings from the ini file
			// plist replacements
			DirectoryReference DirRef = bIsUE4Game ? (!string.IsNullOrEmpty(UnrealBuildTool.GetRemoteIniPath()) ? new DirectoryReference(UnrealBuildTool.GetRemoteIniPath()) : null) : new DirectoryReference(ProjectDirectory);
			ConfigHierarchy Ini = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, DirRef, UnrealTargetPlatform.IOS);

			// orientations
			string InterfaceOrientation = "";
			string PreferredLandscapeOrientation = "";
			Ini.GetString("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "PreferredLandscapeOrientation", out PreferredLandscapeOrientation);

			string SupportedOrientations = "";
			bool bSupported = true;
			Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bSupportsPortraitOrientation", out bSupported);
			SupportedOrientations += bSupported ? "\t\t<string>UIInterfaceOrientationPortrait</string>\n" : "";
			bSupportsPortrait = bSupported;

			Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bSupportsUpsideDownOrientation", out bSupported);
			SupportedOrientations += bSupported ? "\t\t<string>UIInterfaceOrientationPortraitUpsideDown</string>\n" : "";
			bSupportsPortrait |= bSupported;

			bool bSupportsLandscapeLeft = false;
			Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bSupportsLandscapeLeftOrientation", out bSupportsLandscapeLeft);
			bool bSupportsLandscapeRight = false;
			Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bSupportsLandscapeRightOrientation", out bSupportsLandscapeRight);
			bSupportsLandscape = bSupportsLandscapeLeft || bSupportsLandscapeRight;

			if (bSupportsLandscapeLeft && bSupportsLandscapeRight)
			{
				// if both landscape orientations are present, set the UIInterfaceOrientation key
				// in the orientation list, the preferred orientation should be first
				if (PreferredLandscapeOrientation == "LandscapeLeft")
				{
					InterfaceOrientation = "\t<key>UIInterfaceOrientation</key>\n\t<string>UIInterfaceOrientationLandscapeLeft</string>\n";
					SupportedOrientations += "\t\t<string>UIInterfaceOrientationLandscapeLeft</string>\n\t\t<string>UIInterfaceOrientationLandscapeRight</string>\n";
				}
				else
				{
					// by default, landscape right is the preferred orientation - Apple's UI guidlines
					InterfaceOrientation = "\t<key>UIInterfaceOrientation</key>\n\t<string>UIInterfaceOrientationLandscapeRight</string>\n";
					SupportedOrientations += "\t\t<string>UIInterfaceOrientationLandscapeRight</string>\n\t\t<string>UIInterfaceOrientationLandscapeLeft</string>\n";
				}
			}
			else
			{
				// max one landscape orientation is supported
				SupportedOrientations += bSupportsLandscapeRight? "\t\t<string>UIInterfaceOrientationLandscapeRight</string>\n": "";
				SupportedOrientations += bSupportsLandscapeLeft? "\t\t<string>UIInterfaceOrientationLandscapeLeft</string>\n" : "";
			}

			// ITunes file sharing
			bool bSupportsITunesFileSharing = false;
			Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bSupportsITunesFileSharing", out bSupportsITunesFileSharing);
			bool bSupportsFilesApp = false;
			Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bSupportsFilesApp", out bSupportsFilesApp);

			// bundle display name
			string BundleDisplayName;
			Ini.GetString("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "BundleDisplayName", out BundleDisplayName);

			// bundle identifier
			string BundleIdentifier;
			Ini.GetString("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "BundleIdentifier", out BundleIdentifier);
			if (!string.IsNullOrEmpty(BundleID))
			{
				BundleIdentifier = BundleID; // overriding bundle ID
			}

			// bundle name
			string BundleName;
			Ini.GetString("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "BundleName", out BundleName);

			// disable https requirement
			bool bDisableHTTPS;
			Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bDisableHTTPS", out bDisableHTTPS);

			// short version string
			string BundleShortVersion;
			Ini.GetString("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "VersionInfo", out BundleShortVersion);

			// required capabilities
			string RequiredCaps = "";

			IOSProjectSettings ProjectSettings = ((IOSPlatform)UEBuildPlatform.GetBuildPlatform(UnrealTargetPlatform.IOS)).ReadProjectSettings(ProjectFile);

			List<string> Arches = ((Config == UnrealTargetConfiguration.Shipping) ? ProjectSettings.ShippingArchitectures : ProjectSettings.NonShippingArchitectures).ToList();
			if (Arches.Count > 1)
			{
				RequiredCaps += "\t\t<string>armv7</string>\n";
			}
			else
			{
				RequiredCaps += "\t\t<string>" + Arches[0] + "</string>\n";
			}

			//ConfigHierarchy GameIni = ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, DirRef, UnrealTargetPlatform.IOS);

			Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bSupportsMetal", out bSupported);
			RequiredCaps += bSupported ? "\t\t<string>metal</string>\n" : "";

			// minimum iOS version
			string MinVersion;
			if (Ini.GetString("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "MinimumiOSVersion", out MinVersion))
			{
				switch (MinVersion)
				{
				case "IOS_61":
					Log.TraceWarning("IOS 6 is no longer supported in UE4 as of 4.11");
					MinVersion = "7.0";
					break;
				case "IOS_7":
					Log.TraceWarning("IOS 7 is no longer supported in UE4 as of 4.15");
					MinVersion = "7.0";
					break;
				case "IOS_8":
					Log.TraceWarning("IOS 8 is no longer supported in UE4 as of 4.18");
					MinVersion = "8.0";
					break;
				case "IOS_9":
					Log.TraceWarning("IOS 9 is no longer supported in UE4 as of 4.21");
					MinVersion = "9.0";
					break;
				case "IOS_10":
					Log.TraceWarning("IOS 10 is no longer supported in UE4 as of 4.23");
					MinVersion = "10.0";
					break;
				case "IOS_11":
					MinVersion = "11.0";
					break;
				case "IOS_12":
					MinVersion = "12.0";
					break;
				case "IOS_13":
					MinVersion = "13.0";
					break;
				}
			}
			else
			{
				MinVersion = "11.0";
			}

			// Get Facebook Support details
			bool bEnableFacebookSupport = true;
			Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bEnableFacebookSupport", out bEnableFacebookSupport);
			bool bEnableAutomaticLogging = true;
			Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bEnableAutomaticLogging", out bEnableAutomaticLogging);
			bool bEnableAdvertisingId = true;
			Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bEnableAdvertisingId", out bEnableAdvertisingId);

			// Write the Facebook App ID if we need it.
			string FacebookAppID = "";
			Ini.GetString("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "FacebookAppID", out FacebookAppID);
			bEnableFacebookSupport = bEnableFacebookSupport && !string.IsNullOrWhiteSpace(FacebookAppID);
			string FacebookDisplayName = "";
			Ini.GetString("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "BundleDisplayName", out FacebookDisplayName);

			// Get Google Support details
			bool bEnableGoogleSupport = true;
			Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bEnableGoogleSupport", out bEnableGoogleSupport);

			// Write the Google iOS URL Scheme if we need it.
			string GoogleReversedClientId = "";
			Ini.GetString("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "GoogleReversedClientId", out GoogleReversedClientId);
			bEnableGoogleSupport = bEnableGoogleSupport && !string.IsNullOrWhiteSpace(GoogleReversedClientId);

			// Add remote-notifications as background mode
			bool bRemoteNotificationsSupported = false;
			Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bEnableRemoteNotificationsSupport", out bRemoteNotificationsSupported);

            // Add background fetch as background mode
            bool bBackgroundFetch = false;
            Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bEnableBackgroundFetch", out bBackgroundFetch);

			// Get any Location Services permission descriptions added
			string LocationAlwaysUsageDescription = "";
			string LocationWhenInUseDescription = "";
			Ini.GetString("/Script/LocationServicesIOSEditor.LocationServicesIOSSettings", "LocationAlwaysUsageDescription", out LocationAlwaysUsageDescription);
			Ini.GetString("/Script/LocationServicesIOSEditor.LocationServicesIOSSettings", "LocationWhenInUseDescription", out LocationWhenInUseDescription);

			// extra plist data
			string ExtraData = "";
			Ini.GetString("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "AdditionalPlistData", out ExtraData);

			Ini.GetBool("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "bCustomLaunchscreenStoryboard", out VersionUtilities.bCustomLaunchscreenStoryboard);

			// generate the plist file
			StringBuilder Text = new StringBuilder();
			Text.AppendLine("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
			Text.AppendLine("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">");
			Text.AppendLine("<plist version=\"1.0\">");
			Text.AppendLine("<dict>");
			Text.AppendLine("\t<key>CFBundleURLTypes</key>");
			Text.AppendLine("\t<array>");
			Text.AppendLine("\t\t<dict>");
			Text.AppendLine("\t\t\t<key>CFBundleURLName</key>");
			Text.AppendLine("\t\t\t<string>com.Epic.Unreal</string>");
			Text.AppendLine("\t\t\t<key>CFBundleURLSchemes</key>");
			Text.AppendLine("\t\t\t<array>");
			Text.AppendLine(string.Format("\t\t\t\t<string>{0}</string>", bIsUE4Game ? "UE4Game" : GameName));
			if (bEnableFacebookSupport)
			{
				// This is needed for facebook login to redirect back to the app after completion.
				Text.AppendLine(string.Format("\t\t\t\t<string>fb{0}</string>", FacebookAppID));
			}
			if (bEnableGoogleSupport)
			{
				Text.AppendLine(string.Format("\t\t\t\t<string>{0}</string>", GoogleReversedClientId));
			}
			Text.AppendLine("\t\t\t</array>");
			Text.AppendLine("\t\t</dict>");
			Text.AppendLine("\t</array>");
			Text.AppendLine("\t<key>CFBundleDevelopmentRegion</key>");
			Text.AppendLine("\t<string>English</string>");
			Text.AppendLine("\t<key>CFBundleDisplayName</key>");
			Text.AppendLine(string.Format("\t<string>{0}</string>", EncodeBundleName(BundleDisplayName, ProjectName)));
			Text.AppendLine("\t<key>CFBundleExecutable</key>");
			string BundleExecutable = bIsUE4Game ?
				(bIsClient ? "UE4Client" : "UE4Game") :
				(bIsClient ? GameName + "Client" : GameName);
			Text.AppendLine(string.Format("\t<string>{0}</string>", BundleExecutable));
			Text.AppendLine("\t<key>CFBundleIdentifier</key>");
			Text.AppendLine(string.Format("\t<string>{0}</string>", BundleIdentifier.Replace("[PROJECT_NAME]", ProjectName).Replace("_", "")));
			Text.AppendLine("\t<key>CFBundleInfoDictionaryVersion</key>");
			Text.AppendLine("\t<string>6.0</string>");
			Text.AppendLine("\t<key>CFBundleName</key>");
			Text.AppendLine(string.Format("\t<string>{0}</string>", EncodeBundleName(BundleName, ProjectName)));
			Text.AppendLine("\t<key>CFBundlePackageType</key>");
			Text.AppendLine("\t<string>APPL</string>");
			Text.AppendLine("\t<key>CFBundleSignature</key>");
			Text.AppendLine("\t<string>????</string>");
			Text.AppendLine("\t<key>CFBundleVersion</key>");
			Text.AppendLine(string.Format("\t<string>{0}</string>", VersionUtilities.UpdateBundleVersion(OldPListData, InEngineDir)));
			Text.AppendLine("\t<key>CFBundleShortVersionString</key>");
			Text.AppendLine(string.Format("\t<string>{0}</string>", BundleShortVersion));
			Text.AppendLine("\t<key>LSRequiresIPhoneOS</key>");
			Text.AppendLine("\t<true/>");
			Text.AppendLine("\t<key>UIStatusBarHidden</key>");
			Text.AppendLine("\t<true/>");
			Text.AppendLine("\t<key>UIFileSharingEnabled</key>");
			Text.AppendLine(string.Format("\t<{0}/>", bSupportsITunesFileSharing ? "true" : "false"));
			if (bSupportsFilesApp)
			{
				Text.AppendLine("\t<key>LSSupportsOpeningDocumentsInPlace</key>");
				Text.AppendLine("\t<true/>");
			}
			Text.AppendLine("\t<key>UIRequiresFullScreen</key>");
			Text.AppendLine("\t<true/>");
			Text.AppendLine("\t<key>UIViewControllerBasedStatusBarAppearance</key>");
			Text.AppendLine("\t<false/>");
			if (InterfaceOrientation != "")
			{ 
				Text.AppendLine(InterfaceOrientation);
			}
			Text.AppendLine("\t<key>UISupportedInterfaceOrientations</key>");
			Text.AppendLine("\t<array>");
			foreach(string Line in SupportedOrientations.Split("\r\n".ToCharArray()))
			{
				if (!string.IsNullOrWhiteSpace(Line))
				{
					Text.AppendLine(Line);
				}
			}
			Text.AppendLine("\t</array>");
			Text.AppendLine("\t<key>UIRequiredDeviceCapabilities</key>");
			Text.AppendLine("\t<array>");
			foreach(string Line in RequiredCaps.Split("\r\n".ToCharArray()))
			{
				if (!string.IsNullOrWhiteSpace(Line))
				{
					Text.AppendLine(Line);
				}
			}
			Text.AppendLine("\t</array>");

			if (SupportsIconCatalog(SdkVersion))
			{
				bSkipIcons = true;
				Text.AppendLine("\t<key>CFBundleIcons</key>");
				Text.AppendLine("\t<dict>");
				Text.AppendLine("\t\t<key>CFBundlePrimaryIcon</key>");
				Text.AppendLine("\t\t<dict>");
				Text.AppendLine("\t\t\t<key>CFBundleIconFiles</key>");
				Text.AppendLine("\t\t\t<array>");
				Text.AppendLine("\t\t\t\t<string>AppIcon20x20</string>");
				Text.AppendLine("\t\t\t\t<string>AppIcon29x29</string>");
				Text.AppendLine("\t\t\t\t<string>AppIcon40x40</string>");
				Text.AppendLine("\t\t\t\t<string>AppIcon60x60</string>");
				Text.AppendLine("\t\t\t</array>");
				Text.AppendLine("\t\t\t<key>CFBundleIconName</key>");
				Text.AppendLine("\t\t\t<string>AppIcon</string>");
				Text.AppendLine("\t\t\t<key>UIPrerenderedIcon</key>");
				Text.AppendLine("\t\t\t<true/>");
				Text.AppendLine("\t\t</dict>");
				Text.AppendLine("\t</dict>");
				Text.AppendLine("\t<key>CFBundleIcons~ipad</key>");
				Text.AppendLine("\t<dict>");
				Text.AppendLine("\t\t<key>CFBundlePrimaryIcon</key>");
				Text.AppendLine("\t\t<dict>");
				Text.AppendLine("\t\t\t<key>CFBundleIconFiles</key>");
				Text.AppendLine("\t\t\t<array>");
				Text.AppendLine("\t\t\t\t<string>AppIcon20x20</string>");
				Text.AppendLine("\t\t\t\t<string>AppIcon29x29</string>");
				Text.AppendLine("\t\t\t\t<string>AppIcon40x40</string>");
				Text.AppendLine("\t\t\t\t<string>AppIcon60x60</string>");
				Text.AppendLine("\t\t\t\t<string>AppIcon76x76</string>");
				Text.AppendLine("\t\t\t\t<string>AppIcon83.5x83.5</string>");
				Text.AppendLine("\t\t\t</array>");
				Text.AppendLine("\t\t\t<key>CFBundleIconName</key>");
				Text.AppendLine("\t\t\t<string>AppIcon</string>");
				Text.AppendLine("\t\t\t<key>UIPrerenderedIcon</key>");
				Text.AppendLine("\t\t\t<true/>");
				Text.AppendLine("\t\t</dict>");
				Text.AppendLine("\t</dict>");
			}
			else
			{
				bSkipIcons = false;
				Text.AppendLine("\t<key>CFBundleIcons</key>");
				Text.AppendLine("\t<dict>");
				Text.AppendLine("\t\t<key>CFBundlePrimaryIcon</key>");
				Text.AppendLine("\t\t<dict>");
				Text.AppendLine("\t\t\t<key>CFBundleIconFiles</key>");
				Text.AppendLine("\t\t\t<array>");
				Text.AppendLine("\t\t\t\t<string>Icon29.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon29@2x.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon40.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon40@2x.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon57.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon57@2x.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon60@2x.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon60@3x.png</string>");
				Text.AppendLine("\t\t\t</array>");
				Text.AppendLine("\t\t\t<key>UIPrerenderedIcon</key>");
				Text.AppendLine("\t\t\t<true/>");
				Text.AppendLine("\t\t</dict>");
				Text.AppendLine("\t</dict>");
				Text.AppendLine("\t<key>CFBundleIcons~ipad</key>");
				Text.AppendLine("\t<dict>");
				Text.AppendLine("\t\t<key>CFBundlePrimaryIcon</key>");
				Text.AppendLine("\t\t<dict>");
				Text.AppendLine("\t\t\t<key>CFBundleIconFiles</key>");
				Text.AppendLine("\t\t\t<array>");
				Text.AppendLine("\t\t\t\t<string>Icon29.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon29@2x.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon40.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon40@2x.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon50.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon50@2x.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon72.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon72@2x.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon76.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon76@2x.png</string>");
				Text.AppendLine("\t\t\t\t<string>Icon83.5@2x.png</string>");
				Text.AppendLine("\t\t\t</array>");
				Text.AppendLine("\t\t\t<key>UIPrerenderedIcon</key>");
				Text.AppendLine("\t\t\t<true/>");
				Text.AppendLine("\t\t</dict>");
				Text.AppendLine("\t</dict>");
			}

			Text.AppendLine("\t<key>UILaunchStoryboardName</key>");
			Text.AppendLine("\t<string>LaunchScreen</string>");

			if (File.Exists(DirectoryReference.FromFile(ProjectFile) + "/Build/IOS/Resources/Interface/LaunchScreen.storyboard") && VersionUtilities.bCustomLaunchscreenStoryboard)
			{
				string LaunchStoryboard = DirectoryReference.FromFile(ProjectFile) + "/Build/IOS/Resources/Interface/LaunchScreen.storyboard";

				if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac)
				{
					string outputStoryboard = LaunchStoryboard + "c";
					string argsStoryboard = "--compile " + outputStoryboard + " " + LaunchStoryboard;
					string stdOutLaunchScreen = Utils.RunLocalProcessAndReturnStdOut("ibtool", argsStoryboard);

					Log.TraceInformation("LaunchScreen Storyboard compilation results : " + stdOutLaunchScreen);
				}
				else
				{
					Log.TraceWarning("Custom Launchscreen compilation storyboard only compatible on Mac for now");
				}
			}

			Text.AppendLine("\t<key>CFBundleSupportedPlatforms</key>");
			Text.AppendLine("\t<array>");
			Text.AppendLine("\t\t<string>iPhoneOS</string>");
			Text.AppendLine("\t</array>");
			Text.AppendLine("\t<key>MinimumOSVersion</key>");
			Text.AppendLine(string.Format("\t<string>{0}</string>", MinVersion));
			// disable exempt encryption
			Text.AppendLine("\t<key>ITSAppUsesNonExemptEncryption</key>");
			Text.AppendLine("\t<false/>");
			// add location services descriptions if used
			Text.AppendLine("\t<key>NSLocationAlwaysAndWhenInUseUsageDescription</key>");
			Text.AppendLine(string.Format("\t<string>{0}</string>", LocationAlwaysUsageDescription));
			Text.AppendLine("\t<key>NSLocationWhenInUseUsageDescription</key>");
			Text.AppendLine(string.Format("\t<string>{0}></string>", LocationWhenInUseDescription));

			// disable HTTPS requirement
			if (bDisableHTTPS)
			{
				Text.AppendLine("\t<key>NSAppTransportSecurity</key>");
				Text.AppendLine("\t\t<dict>");
				Text.AppendLine("\t\t\t<key>NSAllowsArbitraryLoads</key><true/>");
				Text.AppendLine("\t\t</dict>");
			}

			if (bEnableFacebookSupport)
			{
				Text.AppendLine("\t<key>FacebookAppID</key>");
				Text.AppendLine(string.Format("\t<string>{0}</string>", FacebookAppID));
				Text.AppendLine("\t<key>FacebookDisplayName</key>");
				Text.AppendLine(string.Format("\t<string>{0}</string>", FacebookDisplayName));
				
				if (!bEnableAutomaticLogging)
				{
					Text.AppendLine("<key>FacebookAutoLogAppEventsEnabled</key><false/>");
				}
				
				if (!bEnableAdvertisingId)
				{
					Text.AppendLine("<key>FacebookAdvertiserIDCollectionEnabled</key><false/>");
				}

				Text.AppendLine("\t<key>LSApplicationQueriesSchemes</key>");
				Text.AppendLine("\t<array>");
				Text.AppendLine("\t\t<string>fbapi</string>");
				Text.AppendLine("\t\t<string>fb-messenger-api</string>");
				Text.AppendLine("\t\t<string>fb-messenger-share-api</string>");
				Text.AppendLine("\t\t<string>fbauth2</string>");
				Text.AppendLine("\t\t<string>fbshareextension</string>");
				Text.AppendLine("\t</array>");
			}

			if (!string.IsNullOrEmpty(ExtraData))
			{
				ExtraData = ExtraData.Replace("\\n", "\n");
				foreach(string Line in ExtraData.Split("\r\n".ToCharArray()))
				{
					if (!string.IsNullOrWhiteSpace(Line))
					{
						Text.AppendLine("\t" + Line);
					}
				}
			}

			// Add remote-notifications as background mode
			if (bRemoteNotificationsSupported || bBackgroundFetch)
			{
				Text.AppendLine("\t<key>UIBackgroundModes</key>");
				Text.AppendLine("\t<array>");
                if (bRemoteNotificationsSupported)
                {
				    Text.AppendLine("\t\t<string>remote-notification</string>");
                }
                if (bBackgroundFetch)
                {
                    Text.AppendLine("\t\t<string>fetch</string>");
                }
				Text.AppendLine("\t</array>");
			}

			// write the iCloud container identifier, if present in the old file
			if (!string.IsNullOrEmpty(OldPListData))
			{
				int index = OldPListData.IndexOf("ICloudContainerIdentifier");
				if (index > 0)
				{
					index = OldPListData.IndexOf("<string>", index) + 8;
					int length = OldPListData.IndexOf("</string>", index) - index;
					string ICloudContainerIdentifier = OldPListData.Substring(index, length);
					Text.AppendLine("\t<key>ICloudContainerIdentifier</key>");
					Text.AppendLine(string.Format("\t<string>{0}</string>", ICloudContainerIdentifier));
				}
			}

			Text.AppendLine("</dict>");
			Text.AppendLine("</plist>");

			// Create the intermediate directory if needed
			if (!Directory.Exists(IntermediateDirectory))
			{
				Directory.CreateDirectory(IntermediateDirectory);
			}

			if (UPL != null)
			{
				// Allow UPL to modify the plist here
				XDocument XDoc;
				try
				{
					XDoc = XDocument.Parse(Text.ToString());
				}
				catch (Exception e)
				{
					throw new BuildException("plist is invalid {0}\n{1}", e, Text.ToString());
				}

				XDoc.DocumentType.InternalSubset = "";
				UPL.ProcessPluginNode("None", "iosPListUpdates", "", ref XDoc);
				string result = XDoc.Declaration.ToString() + "\n" + XDoc.ToString().Replace("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"[]>", "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">");
				File.WriteAllText(PListFile, result);

				Text = new StringBuilder(result);
			}

			File.WriteAllText(PListFile, Text.ToString());

            if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac && !bBuildAsFramework)
			{
				if (!Directory.Exists(AppDirectory))
				{
					Directory.CreateDirectory(AppDirectory);
				}
				File.WriteAllText(AppDirectory + "/Info.plist", Text.ToString());
			}

			return true;
		}

        static bool SupportsIconCatalog(VersionNumber SdkVersion)
        {
			return SdkVersion != null && SdkVersion >= new VersionNumber(11);
		}

        public static VersionNumber GetSdkVersion(TargetReceipt Receipt)
        {
			VersionNumber SdkVersion = null;
            if (Receipt != null)
            {
                ReceiptProperty SdkVersionProperty = Receipt.AdditionalProperties.FirstOrDefault(x => x.Name == "SDK");
				if(SdkVersionProperty != null)
				{
					VersionNumber.TryParse(SdkVersionProperty.Value, out SdkVersion);
				}
			}
			return SdkVersion;
        }

		public static bool GetCompileAsDll(TargetReceipt Receipt)
		{
			if (Receipt != null)
			{
				ReceiptProperty CompileAsDllProperty = Receipt.AdditionalProperties.FirstOrDefault(x => x.Name == "CompileAsDll");
				if (CompileAsDllProperty != null && CompileAsDllProperty.Value == "true")
				{
					return true;
				}
			}
			return false;
		}

 		public bool GeneratePList(FileReference ProjectFile, UnrealTargetConfiguration Config, string ProjectDirectory, bool bIsUE4Game, string GameName, bool bIsClient, string ProjectName, string InEngineDir, string AppDirectory, TargetReceipt Receipt, out bool bSupportsPortrait, out bool bSupportsLandscape, out bool bSkipIcons)
		{
			List<string> UPLScripts = CollectPluginDataPaths(Receipt.AdditionalProperties);
			VersionNumber SdkVersion = GetSdkVersion(Receipt);
			bool bBuildAsFramework = GetCompileAsDll(Receipt);
			return GeneratePList(ProjectFile, Config, ProjectDirectory, bIsUE4Game, GameName, bIsClient, ProjectName, InEngineDir, AppDirectory, UPLScripts, SdkVersion, "", bBuildAsFramework, out bSupportsPortrait, out bSupportsLandscape, out bSkipIcons);
		}

		public virtual bool GeneratePList(FileReference ProjectFile, UnrealTargetConfiguration Config, string ProjectDirectory, bool bIsUE4Game, string GameName, bool bIsClient, string ProjectName, string InEngineDir, string AppDirectory, List<string> UPLScripts, VersionNumber SdkVersion, string BundleID, bool bBuildAsFramework, out bool bSupportsPortrait, out bool bSupportsLandscape, out bool bSkipIcons)
		{
			// remember name with -IOS-Shipping, etc
			// string ExeName = GameName;

			// strip out the markup
			GameName = GameName.Split("-".ToCharArray())[0];

			List<string> ProjectArches = new List<string>();
			ProjectArches.Add("None");

			string BundlePath;

			// get the receipt
			if (bIsUE4Game)
			{
				//               ReceiptFilename = TargetReceipt.GetDefaultPath(UnrealBuildTool.EngineDirectory, "UE4Game", UnrealTargetPlatform.IOS, Config, "");
				BundlePath = Path.Combine(UnrealBuildTool.EngineDirectory.ToString(), "Intermediate", "IOS-Deploy", "UE4Game", Config.ToString(), "Payload", "UE4Game.app");
			}
			else
			{
				//                ReceiptFilename = TargetReceipt.GetDefaultPath(new DirectoryReference(ProjectDirectory), GameName, UnrealTargetPlatform.IOS, Config, "");
				BundlePath = AppDirectory;//Path.Combine(ProjectDirectory, "Binaries", "IOS", "Payload", ProjectName + ".app");
			}

			string RelativeEnginePath = UnrealBuildTool.EngineDirectory.MakeRelativeTo(DirectoryReference.GetCurrentDirectory());

			UnrealPluginLanguage UPL = new UnrealPluginLanguage(ProjectFile, UPLScripts, ProjectArches, "", "", UnrealTargetPlatform.IOS);

			// Passing in true for distribution is not ideal here but given the way that ios packaging happens and this call chain it seems unavoidable for now, maybe there is a way to correctly pass it in that I can't find?
			UPL.Init(ProjectArches, true, RelativeEnginePath, BundlePath, ProjectDirectory, Config.ToString(), false);

			return GenerateIOSPList(ProjectFile, Config, ProjectDirectory, bIsUE4Game, GameName, bIsClient, ProjectName, InEngineDir, AppDirectory, SdkVersion, UPL, BundleID, bBuildAsFramework, out bSupportsPortrait, out bSupportsLandscape, out bSkipIcons);
		}

		protected virtual void CopyCloudResources(string InEngineDir, string AppDirectory)
		{
			CopyFiles(InEngineDir + "/Build/IOS/Cloud", AppDirectory, "*.*", true);
		}

		protected virtual void CopyCustomLaunchScreenResources(string InEngineDir, string AppDirectory, string BuildDirectory)
		{
			if (Directory.Exists(BuildDirectory + "/Resources/Interface/LaunchScreen.storyboardc"))
			{
				CopyFolder(BuildDirectory + "/Resources/Interface/LaunchScreen.storyboardc", AppDirectory + "/LaunchScreen.storyboardc", true);
				CopyFiles(BuildDirectory + "/Resources/Interface/Assets", AppDirectory, "*", true);
			}
			else
			{
				Log.TraceWarning("Custom LaunchScreen Storyboard is checked but no compiled Storyboard could be found. Custom Storyboard compilation is only Mac compatible for now. Fallback to default Launchscreen");
				CopyStandardLaunchScreenResources(InEngineDir, AppDirectory, BuildDirectory);
			}
		}

		protected virtual void CopyStandardLaunchScreenResources(string InEngineDir, string AppDirectory, string BuildDirectory)
		{
			CopyFolder(InEngineDir + "/Build/IOS/Resources/Interface/LaunchScreen.storyboardc", AppDirectory + "/LaunchScreen.storyboardc", true);

			if (File.Exists(BuildDirectory + "/Resources/Graphics/LaunchScreenIOS.png"))
			{
				CopyFiles(BuildDirectory + "/Resources/Graphics", AppDirectory, "LaunchScreenIOS.png", true);
			}
			else
			{
				CopyFiles(InEngineDir + "/Build/IOS/Resources/Graphics", AppDirectory, "LaunchScreenIOS.png", true);
			}

		}

		protected virtual void CopyLaunchScreenResources(string InEngineDir, string AppDirectory, string BuildDirectory)
		{
			if (VersionUtilities.bCustomLaunchscreenStoryboard)
			{
				CopyCustomLaunchScreenResources(InEngineDir, AppDirectory, BuildDirectory);
			}
			else
			{
				CopyStandardLaunchScreenResources(InEngineDir, AppDirectory, BuildDirectory);
			}

			if (!File.Exists(AppDirectory + "/LaunchScreen.storyboardc/LaunchScreen.nib"))
			{
				Log.TraceError("Launchscreen.storyboard ViewController needs an ID named LaunchScreen");
			}
		}

		protected virtual void CopyIconResources(string InEngineDir, string AppDirectory, string BuildDirectory)
		{
			CopyFiles(InEngineDir + "/Build/IOS/Resources/Graphics", AppDirectory, "Icon*.png", true);
	
			// merge game assets on top
			// @todo tvos: Do we want to copy IOS and TVOS both in? (Engine/IOS -> Game/IOS -> Game/TVOS)?
			if (Directory.Exists(BuildDirectory + "/Resources/Graphics"))
			{
					CopyFiles(BuildDirectory + "/Resources/Graphics", AppDirectory, "Icon*.png", true);
			}
		}

		protected virtual void CopyLocalizationsResources(string InEngineDir, string AppDirectory, string BuildDirectory, string IntermediateDir)
		{
			string LocalizationsPath = BuildDirectory + "/Resources/Localizations";
			if (Directory.Exists(LocalizationsPath))
			{
				Log.TraceInformation("Copy localizations from Resources {0}, {1}", LocalizationsPath, AppDirectory);
				CopyFolder(BuildDirectory + "/Resources/Localizations", AppDirectory, true, delegate(string InFilename)
				{
					if (Path.GetFileName(InFilename).Equals(".DS_Store")) return false;
					return true;
				});
			}
		}

		public bool PrepForUATPackageOrDeploy(UnrealTargetConfiguration Config, FileReference ProjectFile, string InProjectName, string InProjectDirectory, string InExecutablePath, string InEngineDir, bool bForDistribution, string CookFlavor, bool bIsDataDeploy, bool bCreateStubIPA, TargetReceipt Receipt)
		{
			List<string> UPLScripts = CollectPluginDataPaths(Receipt.AdditionalProperties);
			VersionNumber SdkVersion = GetSdkVersion(Receipt);
			bool bBuildAsFramework = GetCompileAsDll(Receipt);
			return PrepForUATPackageOrDeploy(Config, ProjectFile, InProjectName, InProjectDirectory, InExecutablePath, InEngineDir, bForDistribution, CookFlavor, bIsDataDeploy, bCreateStubIPA, UPLScripts, SdkVersion, "", bBuildAsFramework);
		}

		public bool PrepForUATPackageOrDeploy(UnrealTargetConfiguration Config, FileReference ProjectFile, string InProjectName, string InProjectDirectory, string InExecutablePath, string InEngineDir, bool bForDistribution, string CookFlavor, bool bIsDataDeploy, bool bCreateStubIPA, List<string> UPLScripts, VersionNumber SdkVersion, string BundleID, bool bBuildAsFramework)
		{
			if (BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Mac)
			{
				throw new BuildException("UEDeployIOS.PrepForUATPackageOrDeploy only supports running on the Mac");
			}

			// If we are building as a framework, we don't need to do all of this.
			if (bBuildAsFramework)
			{
				return false;
			}

			string SubDir = GetTargetPlatformName();

			bool bIsUE4Game = InExecutablePath.Contains("UE4Game");
			string BinaryPath = Path.GetDirectoryName(InExecutablePath);
			string GameExeName = Path.GetFileName(InExecutablePath);
			string GameName = bIsUE4Game ? "UE4Game" : GameExeName.Split("-".ToCharArray())[0];
			string PayloadDirectory = BinaryPath + "/Payload";
			string AppDirectory = PayloadDirectory + "/" + GameName + ".app";
			string CookedContentDirectory = AppDirectory + "/cookeddata";
			string BuildDirectory = InProjectDirectory + "/Build/" + SubDir;
			string BuildDirectory_NFL = InProjectDirectory + "/Restricted/NotForLicensees/Build/" + SubDir;
			string IntermediateDirectory = (bIsUE4Game ? InEngineDir : InProjectDirectory) + "/Intermediate/" + SubDir;

			Directory.CreateDirectory(BinaryPath);
			Directory.CreateDirectory(PayloadDirectory);
			Directory.CreateDirectory(AppDirectory);
			Directory.CreateDirectory(BuildDirectory);

			// create the entitlements file

			// delete some old files if they exist
			if (Directory.Exists(AppDirectory + "/_CodeSignature"))
			{
				Directory.Delete(AppDirectory + "/_CodeSignature", true);
			}
			if (File.Exists(AppDirectory + "/CustomResourceRules.plist"))
			{
				File.Delete(AppDirectory + "/CustomResourceRules.plist");
			}
			if (File.Exists(AppDirectory + "/embedded.mobileprovision"))
			{
				File.Delete(AppDirectory + "/embedded.mobileprovision");
			}
			if (File.Exists(AppDirectory + "/PkgInfo"))
			{
				File.Delete(AppDirectory + "/PkgInfo");
			}

			// install the provision
			FileInfo DestFileInfo;
			// always look for provisions in the IOS dir, even for TVOS
			string ProvisionWithPrefix = InEngineDir + "/Build/IOS/UE4Game.mobileprovision";

			string ProjectProvision = InProjectName + ".mobileprovision";
			if (File.Exists(Path.Combine(BuildDirectory, ProjectProvision)))
			{
				ProvisionWithPrefix = Path.Combine(BuildDirectory, ProjectProvision);
			}
			else
			{
				if (File.Exists(Path.Combine(BuildDirectory_NFL, ProjectProvision)))
				{
					ProvisionWithPrefix = Path.Combine(BuildDirectory_NFL, BuildDirectory, ProjectProvision);
				}
				else if (!File.Exists(ProvisionWithPrefix))
				{
					ProvisionWithPrefix = Path.Combine(InEngineDir, "Restricted/NotForLicensees/Build", SubDir, "UE4Game.mobileprovision");
				}
			}
			if (File.Exists(ProvisionWithPrefix))
			{
				Directory.CreateDirectory(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/");
				if (File.Exists(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + ProjectProvision))
				{
					DestFileInfo = new FileInfo(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + ProjectProvision);
					DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
				}
				File.Copy(ProvisionWithPrefix, Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + ProjectProvision, true);
				DestFileInfo = new FileInfo(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + ProjectProvision);
				DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
			}
			if (!File.Exists(ProvisionWithPrefix) || Environment.GetEnvironmentVariable("IsBuildMachine") == "1")
			{
				// copy all provisions from the game directory, the engine directory, and the notforlicensees directory
				// copy all of the provisions from the game directory to the library
				{
					if (Directory.Exists(BuildDirectory))
					{
						foreach(string Provision in Directory.EnumerateFiles(BuildDirectory, "*.mobileprovision", SearchOption.AllDirectories))
						{
							if (!File.Exists(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + Path.GetFileName(Provision)) || File.GetLastWriteTime(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + Path.GetFileName(Provision)) < File.GetLastWriteTime(Provision))
							{
								if (File.Exists(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + Path.GetFileName(Provision)))
								{
									DestFileInfo = new FileInfo(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + Path.GetFileName(Provision));
									DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
								}
								File.Copy(Provision, Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + Path.GetFileName(Provision), true);
								DestFileInfo = new FileInfo(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + Path.GetFileName(Provision));
								DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
							}
						}
					}
				}

				// copy all of the provisions from the engine directory to the library
				{
					// always look for provisions in the IOS dir, even for TVOS
					if (Directory.Exists(InEngineDir + "/Build/IOS"))
					{
						foreach(string Provision in Directory.EnumerateFiles(InEngineDir + "/Build/IOS", "*.mobileprovision", SearchOption.AllDirectories))
						{
							if (!File.Exists(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + Path.GetFileName(Provision)) || File.GetLastWriteTime(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + Path.GetFileName(Provision)) < File.GetLastWriteTime(Provision))
							{
								if (File.Exists(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + Path.GetFileName(Provision)))
								{
									DestFileInfo = new FileInfo(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + Path.GetFileName(Provision));
									DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
								}
								File.Copy(Provision, Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + Path.GetFileName(Provision), true);
								DestFileInfo = new FileInfo(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + Path.GetFileName(Provision));
								DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
							}
						}
					}
				}
			}

			// install the distribution provision
			ProvisionWithPrefix = InEngineDir + "/Build/IOS/UE4Game_Distro.mobileprovision";
			string ProjectDistroProvision = InProjectName + "_Distro.mobileprovision";
			if (File.Exists(Path.Combine(BuildDirectory, ProjectDistroProvision )))
			{
				ProvisionWithPrefix = Path.Combine(BuildDirectory, ProjectDistroProvision);
			}
			else
			{
				if (File.Exists(Path.Combine(BuildDirectory_NFL, ProjectDistroProvision)))
				{
					ProvisionWithPrefix = Path.Combine(BuildDirectory_NFL, ProjectDistroProvision);
				}
				else if (!File.Exists(ProvisionWithPrefix))
				{
					ProvisionWithPrefix = Path.Combine(InEngineDir, "Restricted/NotForLicensees/Build", SubDir, "UE4Game_Distro.mobileprovision");
				}
			}
			if (File.Exists(ProvisionWithPrefix))
			{
				if (File.Exists(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + InProjectName + "_Distro.mobileprovision"))
				{
					DestFileInfo = new FileInfo(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + InProjectName + "_Distro.mobileprovision");
					DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
				}
				File.Copy(ProvisionWithPrefix, Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + InProjectName + "_Distro.mobileprovision", true);
				DestFileInfo = new FileInfo(Environment.GetEnvironmentVariable("HOME") + "/Library/MobileDevice/Provisioning Profiles/" + InProjectName + "_Distro.mobileprovision");
				DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
			}

			bool bSupportsPortrait = true;
			bool bSupportsLandscape = false;
			bool bSkipIcons = false;
			GeneratePList(ProjectFile, Config, InProjectDirectory, bIsUE4Game, GameExeName, false, InProjectName, InEngineDir, AppDirectory, UPLScripts, SdkVersion, BundleID, bBuildAsFramework, out bSupportsPortrait, out bSupportsLandscape, out bSkipIcons);

			// ensure the destination is writable
			if (File.Exists(AppDirectory + "/" + GameName))
			{
				FileInfo GameFileInfo = new FileInfo(AppDirectory + "/" + GameName);
				GameFileInfo.Attributes = GameFileInfo.Attributes & ~FileAttributes.ReadOnly;
			}

			// copy the GameName binary
			File.Copy(BinaryPath + "/" + GameExeName, AppDirectory + "/" + GameName, true);

			//tvos support
			if (SubDir == GetTargetPlatformName())
			{
				string BuildDirectoryFortvOS = InProjectDirectory + "/Build/IOS";
				CopyLaunchScreenResources(InEngineDir, AppDirectory, BuildDirectoryFortvOS);
			}
			else
			{
				CopyLaunchScreenResources(InEngineDir, AppDirectory, BuildDirectory);
			}

			if (!bCreateStubIPA)
			{
				if (!bSkipIcons)
				{
					CopyIconResources(InEngineDir, AppDirectory, BuildDirectory);
				}
				CopyLocalizationsResources(InEngineDir, AppDirectory, BuildDirectory, IntermediateDirectory);

				CopyCloudResources(InProjectDirectory, AppDirectory);

				// copy additional engine framework assets in
				// @todo tvos: TVOS probably needs its own assets?
				string FrameworkAssetsPath = InEngineDir + "/Intermediate/IOS/FrameworkAssets";

				// Let project override assets if they exist
				if (Directory.Exists(InProjectDirectory + "/Intermediate/IOS/FrameworkAssets"))
				{
					FrameworkAssetsPath = InProjectDirectory + "/Intermediate/IOS/FrameworkAssets";
				}

				if (Directory.Exists(FrameworkAssetsPath))
				{
					CopyFolder(FrameworkAssetsPath, AppDirectory, true);
				}

				Directory.CreateDirectory(CookedContentDirectory);
			}

			return true;
		}

		public override bool PrepTargetForDeployment(TargetReceipt Receipt)
		{
			List<string> UPLScripts = CollectPluginDataPaths(Receipt.AdditionalProperties);
			VersionNumber SdkVersion = GetSdkVersion(Receipt);
			bool bBuildAsFramework = GetCompileAsDll(Receipt);
			return PrepTargetForDeployment(Receipt.ProjectFile, Receipt.TargetName, Receipt.Platform, Receipt.Configuration, UPLScripts, SdkVersion, false, "", bBuildAsFramework);
		}

		public bool PrepTargetForDeployment(FileReference ProjectFile, string TargetName, UnrealTargetPlatform Platform, UnrealTargetConfiguration Configuration, List<string> UPLScripts, VersionNumber SdkVersion, bool bCreateStubIPA, string BundleID, bool bBuildAsFramework)
		{
			string SubDir = GetTargetPlatformName();

			string GameName = TargetName;
			string ProjectDirectory = (DirectoryReference.FromFile(ProjectFile) ?? UnrealBuildTool.EngineDirectory).FullName;
			string BuildPath = (GameName == "UE4Game" ? "../../Engine" : ProjectDirectory) + "/Binaries/" + SubDir;
			bool bIsUE4Game = GameName.Contains("UE4Game");

			Console.WriteLine("1 GameName: {0}, ProjectName: {1}", GameName, (ProjectFile == null) ? "" : Path.GetFileNameWithoutExtension(ProjectFile.FullName));

			string DecoratedGameName;
			if (Configuration == UnrealTargetConfiguration.Development)
			{
				DecoratedGameName = GameName;
			}
			else
			{
				DecoratedGameName = String.Format("{0}-{1}-{2}", GameName, Platform.ToString(), Configuration.ToString());
			}

			if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac && Environment.GetEnvironmentVariable("UBT_NO_POST_DEPLOY") != "true")
			{
				return PrepForUATPackageOrDeploy(Configuration, ProjectFile, GameName, ProjectDirectory, BuildPath + "/" + DecoratedGameName, "../../Engine", bForDistribution, "", false, bCreateStubIPA, UPLScripts, SdkVersion, BundleID, bBuildAsFramework);
			}
			else
			{
				// @todo tvos merge: This used to copy the bundle back - where did that code go? It needs to be fixed up for TVOS directories
				bool bSupportPortrait, bSupportLandscape, bSkipIcons;
				GeneratePList(ProjectFile, Configuration, ProjectDirectory, bIsUE4Game, GameName, false, (ProjectFile == null) ? "" : Path.GetFileNameWithoutExtension(ProjectFile.FullName), "../../Engine", "", UPLScripts, SdkVersion, BundleID, bBuildAsFramework, out bSupportPortrait, out bSupportLandscape, out bSkipIcons);
			}
			return true;
		}

		public static List<string> CollectPluginDataPaths(List<ReceiptProperty> ReceiptProperties)
		{
			List<string> PluginExtras = new List<string>();
			if (ReceiptProperties == null)
			{
				Log.TraceInformation("Receipt is NULL");
				//Log.TraceInformation("Receipt is NULL");
				return PluginExtras;
			}

			// collect plugin extra data paths from target receipt
			IEnumerable<ReceiptProperty> Results = ReceiptProperties.Where(x => x.Name == "IOSPlugin");
			foreach(ReceiptProperty Property in Results)
			{
				// Keep only unique paths
				string PluginPath = Property.Value;
				if (PluginExtras.FirstOrDefault(x => x == PluginPath) == null)
				{
					PluginExtras.Add(PluginPath);
					Log.TraceInformation("IOSPlugin: {0}", PluginPath);
				}
			}
			return PluginExtras;
		}

		public static void SafeFileCopy(FileInfo SourceFile, string DestinationPath, bool bOverwrite)
		{
			FileInfo DI = new FileInfo(DestinationPath);
			if (DI.Exists && bOverwrite)
			{
				DI.IsReadOnly = false;
				DI.Delete();
			}

			Directory.CreateDirectory(Path.GetDirectoryName(DestinationPath));
			SourceFile.CopyTo(DestinationPath, bOverwrite);

			FileInfo DI2 = new FileInfo(DestinationPath);
			if (DI2.Exists)
			{
				DI2.IsReadOnly = false;
			}
		}

		protected void CopyFiles(string SourceDirectory, string DestinationDirectory, string TargetFiles, bool bOverwrite = false)
		{
			DirectoryInfo SourceFolderInfo = new DirectoryInfo(SourceDirectory);
			if (SourceFolderInfo.Exists)
			{
				FileInfo[] SourceFiles = SourceFolderInfo.GetFiles(TargetFiles);
				foreach(FileInfo SourceFile in SourceFiles)
				{
					string DestinationPath = Path.Combine(DestinationDirectory, SourceFile.Name);
					SafeFileCopy(SourceFile, DestinationPath, bOverwrite);
				}
			}
		}

		protected void CopyFolder(string SourceDirectory, string DestinationDirectory, bool bOverwrite = false, FilenameFilter Filter = null)
		{
			Directory.CreateDirectory(DestinationDirectory);
			RecursiveFolderCopy(new DirectoryInfo(SourceDirectory), new DirectoryInfo(DestinationDirectory), bOverwrite, Filter);
		}

		static private void RecursiveFolderCopy(DirectoryInfo SourceFolderInfo, DirectoryInfo DestFolderInfo, bool bOverwrite = false, FilenameFilter Filter = null)
		{
			foreach(FileInfo SourceFileInfo in SourceFolderInfo.GetFiles())
			{
				string DestinationPath = Path.Combine(DestFolderInfo.FullName, SourceFileInfo.Name);
				if (Filter != null && !Filter(DestinationPath))
				{
					continue;
				}
				SafeFileCopy(SourceFileInfo, DestinationPath, bOverwrite);
			}

			foreach(DirectoryInfo SourceSubFolderInfo in SourceFolderInfo.GetDirectories())
			{
				string DestFolderName = Path.Combine(DestFolderInfo.FullName, SourceSubFolderInfo.Name);
				Directory.CreateDirectory(DestFolderName);
				RecursiveFolderCopy(SourceSubFolderInfo, new DirectoryInfo(DestFolderName), bOverwrite);
			}
		}
	}
}
