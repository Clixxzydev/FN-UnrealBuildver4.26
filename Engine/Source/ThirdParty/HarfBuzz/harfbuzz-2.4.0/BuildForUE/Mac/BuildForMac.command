#!/bin/sh

#!/bin/sh
# Copyright Epic Games, Inc. All Rights Reserved.

## 
## Most of the following script is intended to be consistent for building all Mac 
## third-party source. The sequence of steps are -
## 1) Set up constants, create temp dir, checkout files, save file info
## 2) lib-specific build steps
## 3) Check files were updated

##
## Lib specific constants

# Name of lib
LIB_NAME="HarfBuzz"
# Drops from the location of this script to where libfiles are relative to
#  e.g.
#  {DROP_TO_LIBROOT}/README
#  {DROP_TO_LIBROOT}/include)
#  ${DROP_TO_LIBROOT}/$LIBFILES[0])
DROP_TO_LIBROOT=../..
# Drops from the location of LIBROOT to Engine/Source/ThirdParrty
DROP_TO_THIRDPARTY=../..

# Path to libs from libroot
LIB_PATH=lib/Mac

# files we build
LIBFILES=( 
    "${LIB_PATH}/libharfbuzz.a"
    "${LIB_PATH}/libharfbuzzd.a"
)

##
## Common setup steps

# Build script will be in <lib>/Build/Mac so get that path and drop two folders to leave us
# in the actual lib folder
pushd . > /dev/null
SCRIPT_DIR="`dirname "${BASH_SOURCE[0]}"`"
cd ${SCRIPT_DIR}/${DROP_TO_LIBROOT}
LIB_ROOT_DIR=${PWD}
echo Changed to ${LIB_ROOT_DIR}

# We should be in ThirdParty/LibName and we want to pull in some common things from
# ThirdParty/BuildScripts/Mac/Common
source ${DROP_TO_THIRDPARTY}/BuildScripts/Mac/Common/Common.sh

echo Rebuilding ${LIB_NAME}

# create a tempdir and save it (note the tmpdir variable is used by the functions that 
# check file state)
TMPDIR="/tmp/${LIB_NAME}-$$"
mkdir -p ${TMPDIR} > /dev/null 2>&1

# checkout the library list and save their state
checkoutFiles ${LIBFILES[@]}
saveFileStates ${LIBFILES[@]}
# Set this for other arch if not on x86_64
TEMP_DIR_RELEASE="${TMPDIR}/release"
TEMP_DIR_DEBUG="${TMPDIR}/debug"

CXXFLAGS="-std=c++11"
ARCHFLAGS="x86_64"

if [ "$BUILD_UNIVERSAL" = true ] ; then
    ARCHFLAGS="${ARCHFLAGS};arm64"
fi

mkdir -p $TEMP_DIR_RELEASE && cd $TEMP_DIR_RELEASE
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS="$CXXFLAGS" -DCMAKE_OSX_ARCHITECTURES="$ARCHFLAGS" "${LIB_ROOT_DIR}/BuildForUE"
make clean && make -j$(get_core_count)
cp -v ../libharfbuzz.a "${LIB_ROOT_DIR}/${LIB_PATH}/libharfbuzz.a"
rm ../libharfbuzz.a

mkdir -p $TEMP_DIR_DEBUG && cd $TEMP_DIR_DEBUG
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS="$CXXFLAGS" -DCMAKE_OSX_ARCHITECTURES="$ARCHFLAGS" "${LIB_ROOT_DIR}/BuildForUE"
make clean && make -j$(get_core_count)
cp -v ../libharfbuzz.a "${LIB_ROOT_DIR}/${LIB_PATH}/libharfbuzzd.a"
rm ../libharfbuzz.a

# back to where our libs are relative to
cd ${LIB_ROOT_DIR}

# check the files were all touched
checkFilesWereUpdated ${LIBFILES[@]}

checkFilesAreFatBinaries ${LIBFILES[@]}

echo The following files were rebuilt: ${LIBFILES[@]}

popd > /dev/null
