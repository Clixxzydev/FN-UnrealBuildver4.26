#!/bin/sh

PATH="/Applications/CMake.app/Contents/bin":"$PATH"

CUR_DIR="`dirname "$0"`"

cd $CUR_DIR

if [ ! -d ninja ]; then
	git clone git://github.com/ninja-build/ninja.git && cd ninja
	git checkout release

	./configure.py --bootstrap

	cd ..
fi

PATH="$CUR_DIR/ninja":"$PATH"
export PATH="$CUR_DIR/ninja":"$PATH"

cd ShaderConductor

# p4 edit $THIRD_PARTY_CHANGELIST lib/Mac/...

# Compile for Mac
SRC_DIR="Build"
DST_DIR="../../../../Binaries/ThirdParty/ShaderConductor/Mac"

if [ "$#" -eq 1 ] && [ "$1" == "-debug" ]; then
	# Debug
	python3 BuildAll.py ninja clang x64 Debug
	SRC_DIR="$SRC_DIR/ninja-osx-clang-x64-Debug/Lib"
else
	# Release
	python3 BuildAll.py ninja clang x64 RelWithDebInfo
	SRC_DIR="$SRC_DIR/ninja-osx-clang-x64-RelWithDebInfo/Lib"
fi

# Copy binary files from source to destination
cp -f Build/ninja-osx-clang-x64-RelWithDebInfo/Lib/libdxcompiler.dylib "$DST_DIR/libdxcompiler.dylib"
cp -f Build/ninja-osx-clang-x64-RelWithDebInfo/Lib/libShaderConductor.dylib "$DST_DIR/libShaderConductor.dylib"

# Replace dummy RPATH value, so ShaderConductor can manually load libdxcompiler.dylib via the 'dlopen' API
install_name_tool -rpath RPATH_DUMMY ../ThirdParty/ShaderConductor/Mac "$DST_DIR/libShaderConductor.dylib"
install_name_tool -rpath RPATH_DUMMY ../ThirdParty/ShaderConductor/Mac "$DST_DIR/libdxcompiler.dylib"

# Link DWARF debug symbols
dsymutil "$DST_DIR/libdxcompiler.dylib"
dsymutil "$DST_DIR/libShaderConductor.dylib"

