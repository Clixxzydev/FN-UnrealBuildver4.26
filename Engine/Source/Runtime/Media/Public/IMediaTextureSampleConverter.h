// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "RHIResources.h"

/**
 * Interface class to implement custom sample conversion
 */
class IMediaTextureSampleConverter
{
public:
	enum EConverterInfoFlags
	{
		ConverterInfoFlags_Default = 0,
		ConverterInfoFlags_WillCreateOutputTexture = 1 << 0,
		ConverterInfoFlags_PreprocessOnly = 1 << 1,
	};

	struct FConversionHints
	{
		bool bOutputSRGB;
		uint8 NumMips;
	};

	virtual uint32 GetConverterInfoFlags() const
	{
		return ConverterInfoFlags_Default;
	}

	virtual bool Convert(FTexture2DRHIRef & InDstTexture, const FConversionHints & Hints) = 0;
};
