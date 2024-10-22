// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "GenericPlatform/GenericPlatformMath.h"

// This implementation is used by both Windows and XBoxOne
#if PLATFORM_WINDOWS
#include <intrin.h>
#include <smmintrin.h>
#endif

#include "Math/UnrealPlatformMathSSE4.h"

/**
* Windows implementation of the Math OS functions
**/
struct FWindowsPlatformMath : public TUnrealPlatformMathSSE4Base<FGenericPlatformMath>
{
#if PLATFORM_XBOXONE
	static FORCEINLINE void WideVectorLoadHalf(float* RESTRICT Dst, const uint16* RESTRICT Src)
	{
		_mm256_storeu_ps(Dst, _mm256_cvtph_ps(_mm_loadu_si128((__m128i*)Src)));
	}

	static FORCEINLINE void WideVectorStoreHalf(uint16* RESTRICT Dst, const float* RESTRICT Src)
	{
		_mm_storeu_si128((__m128i*)Dst, _mm256_cvtps_ph(_mm256_loadu_ps(Src), _MM_FROUND_TO_NEAREST_INT));
	}

	static FORCEINLINE void VectorStoreHalf(uint16* RESTRICT Dst, const float* RESTRICT Src)
	{
		_mm_storeu_si64((__m128i*)Dst, _mm_cvtps_ph(_mm_loadu_ps(Src), _MM_FROUND_TO_NEAREST_INT));
	}

	static FORCEINLINE void VectorLoadHalf(float* RESTRICT Dst, const uint16* RESTRICT Src)
	{
		_mm_storeu_ps(Dst, _mm_cvtph_ps(_mm_loadu_si64((__m128i*)Src)));
	}

	static FORCEINLINE float LoadHalf(const uint16* Ptr)
	{
		float Value;
		_mm_store_ss(&Value, _mm_cvtph_ps(_mm_loadu_si16(Ptr)));
		return Value;
	}

	static FORCEINLINE void StoreHalf(uint16* Ptr, float Value)
	{
		_mm_storeu_si16(Ptr, _mm_cvtps_ph(_mm_load_ss(&Value), _MM_FROUND_TO_NEAREST_INT));
	}
#endif

#if PLATFORM_ENABLE_VECTORINTRINSICS
	static FORCEINLINE bool IsNaN( float A ) { return _isnan(A) != 0; }
	static FORCEINLINE bool IsNaN(double A) { return _isnan(A) != 0; }
	static FORCEINLINE bool IsFinite( float A ) { return _finite(A) != 0; }
	static FORCEINLINE bool IsFinite(double A) { return _finite(A) != 0; }

	#pragma intrinsic( _BitScanReverse )
	static FORCEINLINE uint32 FloorLog2(uint32 Value) 
	{
		// Use BSR to return the log2 of the integer
		unsigned long Log2;
		if(_BitScanReverse(&Log2, Value) != 0)
		{
			return Log2;
		}

		return 0;
	}
	static FORCEINLINE uint32 CountLeadingZeros(uint32 Value)
	{
		// Use BSR to return the log2 of the integer
		unsigned long Log2;
		if(_BitScanReverse(&Log2, Value) != 0)
		{
			return 31 - Log2;
		}

		return 32;
	}
	static FORCEINLINE uint32 CountTrailingZeros(uint32 Value)
	{
		if (Value == 0)
		{
			return 32;
		}
		unsigned long BitIndex;	// 0-based, where the LSB is 0 and MSB is 31
		_BitScanForward( &BitIndex, Value );	// Scans from LSB to MSB
		return BitIndex;
	}
	static FORCEINLINE uint32 CeilLogTwo( uint32 Arg )
	{
		int32 Bitmask = ((int32)(CountLeadingZeros(Arg) << 26)) >> 31;
		return (32 - CountLeadingZeros(Arg - 1)) & (~Bitmask);
	}
	static FORCEINLINE uint32 RoundUpToPowerOfTwo(uint32 Arg)
	{
		return 1 << CeilLogTwo(Arg);
	}
	static FORCEINLINE uint64 RoundUpToPowerOfTwo64(uint64 Arg)
	{
		return uint64(1) << CeilLogTwo64(Arg);
	}
#if PLATFORM_64BITS
	static FORCEINLINE uint64 CeilLogTwo64(uint64 Arg)
	{
		int64 Bitmask = ((int64)(CountLeadingZeros64(Arg) << 57)) >> 63;
		return (64 - CountLeadingZeros64(Arg - 1)) & (~Bitmask);
	}
	static FORCEINLINE uint64 CountLeadingZeros64(uint64 Value)
	{
		// Use BSR to return the log2 of the integer
		unsigned long Log2;
		if (_BitScanReverse64(&Log2, Value) != 0)
		{
			return 63 - Log2;
		}

		return 64;
	}
	static FORCEINLINE uint64 CountTrailingZeros64(uint64 Value)
	{
		if (Value == 0)
		{
			return 64;
		}
		unsigned long BitIndex;	// 0-based, where the LSB is 0 and MSB is 31
		_BitScanForward64( &BitIndex, Value );	// Scans from LSB to MSB
		return BitIndex;
	}

#endif

#if PLATFORM_ENABLE_POPCNT_INTRINSIC
	static FORCEINLINE int32 CountBits(uint64 Bits)
	{
		return _mm_popcnt_u64(Bits);
	}
#endif

#endif
};

typedef FWindowsPlatformMath FPlatformMath;
