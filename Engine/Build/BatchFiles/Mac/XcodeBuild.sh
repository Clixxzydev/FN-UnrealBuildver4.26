#!/bin/sh

# This script gets called every time Xcode does a build or clean operation. It is similar to Build.sh
# (and can take the same arguments) but performs some interpretation of arguments that come from Xcode
# Values for $ACTION: "" = building, "clean" = cleaning

# Setup Environment & Mono
source Engine/Build/BatchFiles/Mac/SetupEnvironment.sh -mono Engine/Build/BatchFiles/Mac

# First make sure that the UnrealBuildTool is up-to-date
if ! xbuild /property:Configuration=Development /verbosity:quiet /nologo /p:NoWarn=1591 Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj; then
  echo "Failed to build to build tool (UnrealBuildTool)"
  exit 1
fi

#echo "Raw Args: $*"

case $1 in 
	"clean")
		ACTION="clean"
	;;

	"install")
		ACTION="install"
	;;	
esac

if [ ["$ACTION"] == [""] ]; then
	ACTION="build"
	TARGET=$1
	PLATFORM=$2
	CONFIGURATION=$3
	TRAILINGARGS=${@:4}
else
	# non build actions are all shifted by one
	TARGET=$2
	PLATFORM=$3
	CONFIGURATION=$4
	TRAILINGARGS=${@:5}
fi

# Convert platform to UBT terms
case $PLATFORM in
	"iphoneos"|"IOS"|"iphonesimulator")
		PLATFORM="IOS"
	;;
	"appletvos")
		PLATFORM="TVOS"
	;;
	"macosx")
		PLATFORM="Mac"
	;;
esac

echo "Processing $ACTION for Target=$TARGET Platform=$PLATFORM Configuration=$CONFIGURATION $TRAILINGARGS"

# Add additional flags based on actions, arguments, and env properties
AdditionalFlags=""

if [ "$ACTION" == "build" ]; then

	# flags based on platform
	case $PLATFORM in 
		"IOS")
			AdditionalFlags += " -deploy"
		;;

		"TVOS")
			AdditionalFlags += " -deploy"
		;;
	esac

	case $CLANG_STATIC_ANALYZER_MODE in
		"deep")
			AdditionalFlags+="-SkipActionHistory"
			;;
		"shallow")
			AdditionalFlags+="-SkipActionHistory"
			;;
	esac

	case $ENABLE_THREAD_SANITIZER in
		"YES"|"1")
			# Disable TSAN atomic->non-atomic race reporting as we aren't C++11 memory-model conformant so UHT will fail
			export TSAN_OPTIONS="suppress_equal_stacks=true suppress_equal_addresses=true report_atomic_races=false"
		;;
	esac

	# Build SCW if this is an editor target
	if [[ "$TARGET" == *"Editor" ]]; then
		mono Engine/Binaries/DotNET/UnrealBuildTool.exe ShaderCompileWorker Mac Development
	fi

elif [ $ACTION == "clean" ]; then
	AdditionalFlags="-clean"
fi

echo Running Engine/Binaries/DotNET/UnrealBuildTool.exe $TARGET $PLATFORM $CONFIGURATION $TRAILINGARGS $AdditionalFlags
mono Engine/Binaries/DotNET/UnrealBuildTool.exe $TARGET $PLATFORM $CONFIGURATION $TRAILINGARGS $AdditionalFlags

ExitCode=$?
if [ $ExitCode -eq 254 ] || [ $ExitCode -eq 255 ] || [ $ExitCode -eq 2 ]; then
	exit 0
fi
exit $ExitCode
