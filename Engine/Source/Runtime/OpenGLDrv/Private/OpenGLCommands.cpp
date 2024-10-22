// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	OpenGLCommands.cpp: OpenGL RHI commands implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "RHIDefinitions.h"
#include "RHI.h"
#include "EngineGlobals.h"
#include "RenderResource.h"
#include "OpenGLDrv.h"
#include "OpenGLDrvPrivate.h"
#include "RenderUtils.h"

/*
#define DECLARE_ISBOUNDSHADER(ShaderType) template <typename TShaderType> inline void ValidateBoundShader(TRefCountPtr<FOpenGLBoundShaderState> InBoundShaderState, FRHIGraphicsShader* GfxShader, TShaderType* ShaderTypeRHI) \
{ \
	FOpenGL##ShaderType* ShaderType = FOpenGLDynamicRHI::ResourceCast(static_cast<TShaderType*>(GfxShader)); \
	ensureMsgf(InBoundShaderState && ShaderType == InBoundShaderState->Get##ShaderType(), TEXT("Parameters are being set for a %s which is not currently bound"), TEXT(#ShaderType)); \
}

DECLARE_ISBOUNDSHADER(VertexShader)
DECLARE_ISBOUNDSHADER(PixelShader)
DECLARE_ISBOUNDSHADER(GeometryShader)
DECLARE_ISBOUNDSHADER(HullShader)
DECLARE_ISBOUNDSHADER(DomainShader)
*/

#if 0//DO_CHECK
	#define VALIDATE_BOUND_SHADER(s, t) ValidateBoundShader<t>(PendingState.BoundShaderState, s)
#else
	#define VALIDATE_BOUND_SHADER(s, t)
#endif

namespace OpenGLConsoleVariables
{
#if PLATFORM_WINDOWS || PLATFORM_LINUX
	int32 bUseMapBuffer = 0;
#else
	int32 bUseMapBuffer = 1;
#endif
	static FAutoConsoleVariableRef CVarUseMapBuffer(
		TEXT("OpenGL.UseMapBuffer"),
		bUseMapBuffer,
		TEXT("If true, use glMapBuffer otherwise use glBufferSubdata.")
		);

	int32 bSkipCompute = 0;
	static FAutoConsoleVariableRef CVarSkipCompute(
		TEXT("OpenGL.SkipCompute"),
		bSkipCompute,
		TEXT("If true, don't issue dispatch work.")
		);

	int32 bUseVAB = 1;
	static FAutoConsoleVariableRef CVarUseVAB(
		TEXT("OpenGL.UseVAB"),
		bUseVAB,
		TEXT("If true, use GL_VERTEX_ATTRIB_BINDING instead of traditional vertex array setup."),
		ECVF_ReadOnly
		);

#if PLATFORM_WINDOWS || PLATFORM_LINUX
	int32 MaxSubDataSize = 256*1024;
#else
	int32 MaxSubDataSize = 0;
#endif
	static FAutoConsoleVariableRef CVarMaxSubDataSize(
		TEXT("OpenGL.MaxSubDataSize"),
		MaxSubDataSize,
		TEXT("Maximum amount of data to send to glBufferSubData in one call"),
		ECVF_ReadOnly
		);

	int32 bBindlessTexture = 0;
	static FAutoConsoleVariableRef CVarBindlessTexture(
		TEXT("OpenGL.BindlessTexture"),
		bBindlessTexture,
		TEXT("If true, use GL_ARB_bindless_texture over traditional glBindTexture/glBindSampler."),
		ECVF_ReadOnly
		);
	
	int32 bRebindTextureBuffers = 0;
	static FAutoConsoleVariableRef CVarRebindTextureBuffers(
		TEXT("OpenGL.RebindTextureBuffers"),
		bRebindTextureBuffers,
		TEXT("If true, rebind GL_TEXTURE_BUFFER's to their GL_TEXTURE name whenever the buffer is modified.")
		);

	int32 bUseBufferDiscard = 1;
	static FAutoConsoleVariableRef CVarUseBufferDiscard(
		TEXT("OpenGL.UseBufferDiscard"),
		bUseBufferDiscard,
		TEXT("If true, use dynamic buffer orphaning hint.")
		);
	
	static TAutoConsoleVariable<int32> CVarUseSeparateShaderObjects(
		TEXT("OpenGL.UseSeparateShaderObjects"),
		0,
		TEXT("If set to 1, use OpenGL's separate shader objects to eliminate expensive program linking"),
		ECVF_ReadOnly|ECVF_RenderThreadSafe);
};

#if PLATFORM_64BITS
#define INDEX_TO_VOID(Index) (void*)((uint64)(Index))
#else
#define INDEX_TO_VOID(Index) (void*)((uint32)(Index))
#endif

enum EClearType
{
	CT_None				= 0x0,
	CT_Depth			= 0x1,
	CT_Stencil			= 0x2,
	CT_Color			= 0x4,
	CT_DepthStencil		= CT_Depth | CT_Stencil,
};

struct FPendingSamplerDataValue
{
	GLenum	Enum;
	GLint	Value;
};

struct FVertexBufferPair
{
	FOpenGLVertexBuffer*				Source;
	TRefCountPtr<FOpenGLVertexBuffer>	Dest;
};
static TArray<FVertexBufferPair> ZeroStrideExpandedBuffersList;


static int FindVertexBuffer(FOpenGLVertexBuffer* Source)
{
	for (int32 Index = 0; Index < ZeroStrideExpandedBuffersList.Num(); ++Index)
	{
		if (ZeroStrideExpandedBuffersList[Index].Source == Source)
		{
			return Index;
		}
	}
	return -1;
}

static FOpenGLVertexBuffer* FindExpandedZeroStrideBuffer(FOpenGLVertexBuffer* ZeroStrideVertexBuffer, uint32 Stride, uint32 NumVertices, const FOpenGLVertexElement& VertexElement)
{
	uint32 Size = NumVertices * Stride;
	int32 FoundExpandedVBIndex = FindVertexBuffer(ZeroStrideVertexBuffer);
	if (FoundExpandedVBIndex != -1)
	{
		// Check if the current size is big enough
		FOpenGLVertexBuffer* ExpandedVB = ZeroStrideExpandedBuffersList[FoundExpandedVBIndex].Dest;
		if (Size <= ExpandedVB->GetSize())
		{
			return ExpandedVB;
		}
	}
	else
	{
		FVertexBufferPair NewPair;
		NewPair.Source = ZeroStrideVertexBuffer;
		NewPair.Dest = NULL;
		FoundExpandedVBIndex = ZeroStrideExpandedBuffersList.Num();
		ZeroStrideExpandedBuffersList.Add(NewPair);
	}

	int32 VertexTypeSize = 0;
	switch( VertexElement.Type )
	{
	case GL_FLOAT:
	case GL_UNSIGNED_INT:
	case GL_INT:
		VertexTypeSize = 4;
		break;
	case GL_SHORT:
	case GL_UNSIGNED_SHORT:
	case GL_HALF_FLOAT:
		VertexTypeSize = 2;
		break;
	case GL_BYTE:
	case GL_UNSIGNED_BYTE:
		VertexTypeSize = 1;
		break;
	case GL_DOUBLE:
		VertexTypeSize = 8;
		break;
	default:
		check(0);
		break;
	}

	const int32 VertexElementSize = ( VertexElement.Size == GL_BGRA ) ? 4 : VertexElement.Size;
	const int32 SizeToFill = VertexElementSize * VertexTypeSize;
	void* RESTRICT SourceData = ZeroStrideVertexBuffer->GetZeroStrideBuffer();
	check(SourceData);
	TRefCountPtr<FOpenGLVertexBuffer> ExpandedVB = new FOpenGLVertexBuffer(0, Size, BUF_Static, NULL);
	uint8* RESTRICT Data = ExpandedVB->Lock(0, Size, false, true);

	switch (SizeToFill)
	{
	case 4:
		{
			uint32 Source = *(uint32*)SourceData;
			uint32* RESTRICT Dest = (uint32*)Data;
			for (uint32 Index = 0; Index < Size / sizeof(uint32); ++Index)
			{
				*Dest++ = Source;
			}
		}
		break;
	case 8:
		{
			uint64 Source = *(uint64*)SourceData;
			uint64* RESTRICT Dest = (uint64*)Data;
			for (uint32 Index = 0; Index < Size / sizeof(uint64); ++Index)
			{
				*Dest++ = Source;
			}
		}
		break;
	case 16:
		{
			uint64 SourceA = *(uint64*)SourceData;
			uint64 SourceB = *((uint64*)SourceData + 1);
			uint64* RESTRICT Dest = (uint64*)Data;
			for (uint32 Index = 0; Index < Size / (2 * sizeof(uint64)); ++Index)
			{
				*Dest++ = SourceA;
				*Dest++ = SourceB;
			}
		}
		break;
	default:
		check(0);
	}

	ExpandedVB->Unlock();

	ZeroStrideExpandedBuffersList[FoundExpandedVBIndex].Dest = ExpandedVB;

	return ExpandedVB;
}

static FORCEINLINE GLint ModifyFilterByMips(GLint Filter, bool bHasMips)
{
	if (!bHasMips)
	{
		switch (Filter)
		{
			case GL_LINEAR_MIPMAP_NEAREST:
			case GL_LINEAR_MIPMAP_LINEAR:
				return GL_LINEAR;

			case GL_NEAREST_MIPMAP_NEAREST:
			case GL_NEAREST_MIPMAP_LINEAR:
				return GL_NEAREST;

			default:
				break;
		}
	}

	return Filter;
}

static FORCEINLINE EShaderFrequency GetShaderFrequency(FRHIGraphicsShader* ShaderRHI)
{
	switch (ShaderRHI->GetFrequency())
	{
	case SF_Vertex:
		VALIDATE_BOUND_SHADER(ShaderRHI, Vertex);
		return SF_Vertex;
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	case SF_Hull:
		VALIDATE_BOUND_SHADER(ShaderRHI, Hull);
		return SF_Hull;
	case SF_Domain:
		VALIDATE_BOUND_SHADER(ShaderRHI, Domain);
		return SF_Domain;
#endif
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	case SF_Geometry:
		VALIDATE_BOUND_SHADER(ShaderRHI, Geometry);
		return SF_Geometry;
#endif
	case SF_Pixel:
		VALIDATE_BOUND_SHADER(ShaderRHI, Pixel);
		return SF_Pixel;
	default:
		checkf(0, TEXT("Undefined FRHIShader Type %d!"), (int32)ShaderRHI->GetFrequency());
	}

	return SF_NumFrequencies;
}

static FORCEINLINE CrossCompiler::EShaderStage GetShaderCrossCompilerStage(FRHIGraphicsShader* ShaderRHI)
{
	switch (ShaderRHI->GetFrequency())
	{
	case SF_Vertex:
		VALIDATE_BOUND_SHADER(ShaderRHI, Vertex);
		return CrossCompiler::SHADER_STAGE_VERTEX;
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	case SF_Hull:
		VALIDATE_BOUND_SHADER(ShaderRHI, Hull);
		return CrossCompiler::SHADER_STAGE_HULL;
	case SF_Domain:
		VALIDATE_BOUND_SHADER(ShaderRHI, Domain);
		return CrossCompiler::SHADER_STAGE_DOMAIN;
#endif
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	case SF_Geometry:
		VALIDATE_BOUND_SHADER(ShaderRHI, Geometry);
		return CrossCompiler::SHADER_STAGE_GEOMETRY;
#endif
	case SF_Pixel:
		VALIDATE_BOUND_SHADER(ShaderRHI, Pixel);
		return CrossCompiler::SHADER_STAGE_PIXEL;
	default:
		checkf(0, TEXT("Undefined FRHIShader Type %d!"), (int32)ShaderRHI->GetFrequency());
	}

	return CrossCompiler::NUM_SHADER_STAGES;
}

static FORCEINLINE void GetShaderStageIndexAndMaxUnits(FRHIGraphicsShader* ShaderRHI, GLint& OutIndex, GLint& OutMaxUnits)
{
	switch (ShaderRHI->GetFrequency())
	{
	case SF_Vertex:
		VALIDATE_BOUND_SHADER(ShaderRHI, Vertex);
		OutIndex = FOpenGL::GetFirstVertexTextureUnit();
		OutMaxUnits = FOpenGL::GetMaxVertexTextureImageUnits();
		break;
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	case SF_Hull:
		VALIDATE_BOUND_SHADER(ShaderRHI, Hull);
		OutIndex = FOpenGL::GetFirstHullTextureUnit();
		OutMaxUnits = FOpenGL::GetMaxHullTextureImageUnits();
		break;
	case SF_Domain:
		VALIDATE_BOUND_SHADER(ShaderRHI, Domain);
		OutIndex = FOpenGL::GetFirstDomainTextureUnit();
		OutMaxUnits = FOpenGL::GetMaxDomainTextureImageUnits();
		break;
#endif
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	case SF_Geometry:
		VALIDATE_BOUND_SHADER(ShaderRHI, Geometry);
		OutIndex = FOpenGL::GetFirstGeometryTextureUnit();
		OutMaxUnits = FOpenGL::GetMaxGeometryTextureImageUnits();
		break;
#endif
	case SF_Pixel:
		VALIDATE_BOUND_SHADER(ShaderRHI, Pixel);
		OutIndex = FOpenGL::GetFirstPixelTextureUnit();
		OutMaxUnits = FOpenGL::GetMaxTextureImageUnits();
		break;
	default:
		checkf(0, TEXT("Undefined FRHIShader Type %d!"), (int32)ShaderRHI->GetFrequency());
	}
}

// Vertex state.
void FOpenGLDynamicRHI::RHISetStreamSource(uint32 StreamIndex, FRHIVertexBuffer* VertexBufferRHI, uint32 Offset)
{
	VERIFY_GL_SCOPE();
	FOpenGLVertexBuffer* VertexBuffer = ResourceCast(VertexBufferRHI);
	PendingState.Streams[StreamIndex].VertexBuffer = VertexBuffer;
	PendingState.Streams[StreamIndex].Stride = PendingState.BoundShaderState ? PendingState.BoundShaderState->StreamStrides[StreamIndex] : 0;
	PendingState.Streams[StreamIndex].Offset = Offset;
}

// Rasterizer state.
void FOpenGLDynamicRHI::RHISetRasterizerState(FRHIRasterizerState* NewStateRHI)
{
	VERIFY_GL_SCOPE();
	FOpenGLRasterizerState* NewState = ResourceCast(NewStateRHI);
	PendingState.RasterizerState = NewState->Data;
}

void FOpenGLDynamicRHI::UpdateRasterizerStateInOpenGLContext( FOpenGLContextState& ContextState )
{
	VERIFY_GL_SCOPE();
	if (FOpenGL::SupportsPolygonMode() && ContextState.RasterizerState.FillMode != PendingState.RasterizerState.FillMode)
	{
		FOpenGL::PolygonMode(GL_FRONT_AND_BACK, PendingState.RasterizerState.FillMode);
		ContextState.RasterizerState.FillMode = PendingState.RasterizerState.FillMode;
	}

	if (ContextState.RasterizerState.CullMode != PendingState.RasterizerState.CullMode)
	{
		if (PendingState.RasterizerState.CullMode != GL_NONE)
		{
			// Only call glEnable if needed
			if (ContextState.RasterizerState.CullMode == GL_NONE)
			{
				glEnable(GL_CULL_FACE);
			}
			glCullFace(PendingState.RasterizerState.CullMode);
		}
		else
		{
			glDisable(GL_CULL_FACE);
		}
		ContextState.RasterizerState.CullMode = PendingState.RasterizerState.CullMode;
	}

	// Convert our platform independent depth bias into an OpenGL depth bias.
	const float BiasScale = float((1<<24)-1);	// Warning: this assumes depth bits == 24, and won't be correct with 32.
	float DepthBias = PendingState.RasterizerState.DepthBias * BiasScale;
	if (ContextState.RasterizerState.DepthBias != PendingState.RasterizerState.DepthBias
		|| ContextState.RasterizerState.SlopeScaleDepthBias != PendingState.RasterizerState.SlopeScaleDepthBias)
	{
		if ((DepthBias == 0.0f) && (PendingState.RasterizerState.SlopeScaleDepthBias == 0.0f))
		{
			// If we're here, both previous 2 'if' conditions are true, and this implies that cached state was not all zeroes, so we need to glDisable.
			glDisable(GL_POLYGON_OFFSET_FILL);
			if ( FOpenGL::SupportsPolygonMode() )
			{
				glDisable(GL_POLYGON_OFFSET_LINE);
				glDisable(GL_POLYGON_OFFSET_POINT);
			}
		}
		else
		{
			if (ContextState.RasterizerState.DepthBias == 0.0f && ContextState.RasterizerState.SlopeScaleDepthBias == 0.0f)
			{
				glEnable(GL_POLYGON_OFFSET_FILL);
				if ( FOpenGL::SupportsPolygonMode() )
				{
					glEnable(GL_POLYGON_OFFSET_LINE);
					glEnable(GL_POLYGON_OFFSET_POINT);
				}
			}
			glPolygonOffset(PendingState.RasterizerState.SlopeScaleDepthBias, DepthBias);
		}

		ContextState.RasterizerState.DepthBias = PendingState.RasterizerState.DepthBias;
		ContextState.RasterizerState.SlopeScaleDepthBias = PendingState.RasterizerState.SlopeScaleDepthBias;
	}
}

void FOpenGLDynamicRHI::UpdateViewportInOpenGLContext( FOpenGLContextState& ContextState )
{
	VERIFY_GL_SCOPE();
	if (ContextState.Viewport != PendingState.Viewport)
	{
		//@todo the viewport defined by glViewport does not clip, unlike the viewport in d3d
		// Set the scissor rect to the viewport unless it is explicitly set smaller to emulate d3d.
		glViewport(
			PendingState.Viewport.Min.X,
			PendingState.Viewport.Min.Y,
			PendingState.Viewport.Max.X - PendingState.Viewport.Min.X,
			PendingState.Viewport.Max.Y - PendingState.Viewport.Min.Y);

		ContextState.Viewport = PendingState.Viewport;
	}

	if (ContextState.DepthMinZ != PendingState.DepthMinZ || ContextState.DepthMaxZ != PendingState.DepthMaxZ)
	{
		FOpenGL::DepthRange(PendingState.DepthMinZ, PendingState.DepthMaxZ);
		ContextState.DepthMinZ = PendingState.DepthMinZ;
		ContextState.DepthMaxZ = PendingState.DepthMaxZ;
	}
}

void FOpenGLDynamicRHI::RHISetViewport(float MinX, float MinY,float MinZ, float MaxX, float MaxY,float MaxZ)
{
	VERIFY_GL_SCOPE();
	PendingState.Viewport.Min.X = (uint32)MinX;
	PendingState.Viewport.Min.Y = (uint32)MinY;
	PendingState.Viewport.Max.X = (uint32)MaxX;
	PendingState.Viewport.Max.Y = (uint32)MaxY;
	PendingState.DepthMinZ = MinZ;
	PendingState.DepthMaxZ = MaxZ;

	RHISetScissorRect(false, 0, 0, 0, 0);
}

void FOpenGLDynamicRHI::RHISetScissorRect(bool bEnable,uint32 MinX,uint32 MinY,uint32 MaxX,uint32 MaxY)
{
	VERIFY_GL_SCOPE();
	PendingState.bScissorEnabled = bEnable;
	PendingState.Scissor.Min.X = MinX;
	PendingState.Scissor.Min.Y = MinY;
	PendingState.Scissor.Max.X = MaxX;
	PendingState.Scissor.Max.Y = MaxY;
}

inline void FOpenGLDynamicRHI::UpdateScissorRectInOpenGLContext( FOpenGLContextState& ContextState )
{
	VERIFY_GL_SCOPE();
	if (ContextState.bScissorEnabled != PendingState.bScissorEnabled)
	{
		if (PendingState.bScissorEnabled)
		{
			glEnable(GL_SCISSOR_TEST);
		}
		else
		{
			glDisable(GL_SCISSOR_TEST);
		}
		ContextState.bScissorEnabled = PendingState.bScissorEnabled;
	}

	if( PendingState.bScissorEnabled &&
		ContextState.Scissor != PendingState.Scissor )
	{
		check(PendingState.Scissor.Min.X <= PendingState.Scissor.Max.X);
		check(PendingState.Scissor.Min.Y <= PendingState.Scissor.Max.Y);
		glScissor(PendingState.Scissor.Min.X, PendingState.Scissor.Min.Y, PendingState.Scissor.Max.X - PendingState.Scissor.Min.X, PendingState.Scissor.Max.Y - PendingState.Scissor.Min.Y);
		ContextState.Scissor = PendingState.Scissor;
	}
}

/**
* Set bound shader state. This will set the vertex decl/shader, and pixel shader
* @param BoundShaderState - state resource
*/
void FOpenGLDynamicRHI::RHISetBoundShaderState(FRHIBoundShaderState* BoundShaderStateRHI)
{
	VERIFY_GL_SCOPE();
	FOpenGLBoundShaderState* BoundShaderState = ResourceCast(BoundShaderStateRHI);
	PendingState.BoundShaderState = BoundShaderState;

	// Prevent transient bound shader states from being recreated for each use by keeping a history of the most recently used bound shader states.
	// The history keeps them alive, and the bound shader state cache allows them to be reused if needed.
	BoundShaderStateHistory.Add(BoundShaderState);
}

void FOpenGLDynamicRHI::RHISetUAVParameter(FRHIPixelShader* PixelShaderRHI, uint32 UAVIndex, FRHIUnorderedAccessView* UnorderedAccessViewRHI)
{
	checkNoEntry();//UAV-PS port: not yet implemented
}


void FOpenGLDynamicRHI::RHISetUAVParameter(FRHIComputeShader* ComputeShaderRHI,uint32 UAVIndex, FRHIUnorderedAccessView* UnorderedAccessViewRHI)
{
	check(FOpenGL::SupportsComputeShaders());
	
	VERIFY_GL_SCOPE();
	if(UnorderedAccessViewRHI)
	{
		FOpenGLUnorderedAccessView* UnorderedAccessView = ResourceCast(UnorderedAccessViewRHI);
		bool bLayered = UnorderedAccessView->IsLayered();
		GLint Layer = UnorderedAccessView->GetLayer();
		InternalSetShaderUAV(FOpenGL::GetFirstComputeUAVUnit() + UAVIndex, UnorderedAccessView->Format, UnorderedAccessView->Resource, bLayered, Layer, GL_READ_WRITE);
	}
	else
	{
		InternalSetShaderUAV(FOpenGL::GetFirstComputeUAVUnit() + UAVIndex, GL_R32F, 0, false, 0, GL_READ_WRITE);
	}
}

void FOpenGLDynamicRHI::RHISetUAVParameter(FRHIComputeShader* ComputeShaderRHI,uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI, uint32 InitialCount )
{
	// TODO: Implement for OpenGL
	check(0);
}

void FOpenGLDynamicRHI::InternalSetShaderTexture(FOpenGLTextureBase* Texture, FOpenGLShaderResourceView* SRV, GLint TextureIndex, GLenum Target, GLuint Resource, int NumMips, int LimitMip)
{
	auto& PendingTextureState = PendingState.Textures[TextureIndex];
	PendingTextureState.Texture = Texture;
	PendingTextureState.SRV = SRV;
	PendingTextureState.Target = Target;
	PendingTextureState.Resource = Resource;
	PendingTextureState.LimitMip = LimitMip;
	PendingTextureState.bHasMips = (NumMips == 0 || NumMips > 1);
	PendingTextureState.NumMips = NumMips;
}

void FOpenGLDynamicRHI::InternalSetSamplerStates(GLint TextureIndex, FOpenGLSamplerState* SamplerState)
{
	PendingState.SamplerStates[TextureIndex] = SamplerState;
}

void FOpenGLDynamicRHI::CachedSetupTextureStageInner(FOpenGLContextState& ContextState, GLint TextureIndex, GLenum Target, GLuint Resource, GLint LimitMip, GLint NumMips)
{
	DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_CachedSetupTextureStage);
	VERIFY_GL_SCOPE();
	FTextureStage& TextureState = ContextState.Textures[TextureIndex];

	// Something will have to be changed. Switch to the stage in question.
	if( ContextState.ActiveTexture != TextureIndex )
	{
		glActiveTexture( GL_TEXTURE0 + TextureIndex );
		ContextState.ActiveTexture = TextureIndex;
	}

	if (TextureState.Target == Target)
	{
		glBindTexture(Target, Resource);
	}
	else
	{
		if (TextureState.Target != GL_NONE)
		{
			// Unbind different texture target on the same stage, to avoid OpenGL keeping its data, and potential driver problems.
			glBindTexture(TextureState.Target, 0);
		}

		if (Target != GL_NONE)
		{
			glBindTexture(Target, Resource);
		}
	}
	
	// Use the texture SRV's LimitMip value to specify the mip available for sampling
	// This requires SupportsTextureBaseLevel & is a fallback for SupportsTextureView
	// which should be preferred.
	if(Target != GL_NONE && Target != GL_TEXTURE_BUFFER && Target != GL_TEXTURE_EXTERNAL_OES && !FOpenGL::SupportsTextureView())
	{
		TPair<GLenum, GLenum>* MipLimits;
		
		{
			DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_CachedSetupTextureStage_Find);
			MipLimits = TextureMipLimits.Find(Resource);
		}
		
		GLint BaseMip = LimitMip == -1 ? 0 : LimitMip;
		GLint MaxMip = LimitMip == -1 ? NumMips - 1 : LimitMip;
		
		const bool bSameLimitMip = MipLimits && MipLimits->Key == BaseMip;
		const bool bSameNumMips = MipLimits && MipLimits->Value == MaxMip;

		if (!bSameLimitMip || !bSameNumMips)
		{
			DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_CachedSetupTextureStage_TexParameter);
			if (FOpenGL::SupportsTextureBaseLevel() && !bSameLimitMip)
			{
				FOpenGL::TexParameter(Target, GL_TEXTURE_BASE_LEVEL, BaseMip);
			}
			if (FOpenGL::SupportsTextureMaxLevel() && !bSameNumMips)
			{
				FOpenGL::TexParameter(Target, GL_TEXTURE_MAX_LEVEL, MaxMip);
			}
			if (MipLimits)
			{
				MipLimits->Key = BaseMip;
				MipLimits->Value = MaxMip;
			}
			else
			{
				TextureMipLimits.Add(Resource, TPair<GLenum, GLenum>(BaseMip, MaxMip));
			}
		}
	}
	else
	{
		LimitMip = 0;
		NumMips = 0;
	}

	TextureState.LimitMip = LimitMip;
	TextureState.NumMips = NumMips;
	TextureState.Target = Target;
	TextureState.Resource = Resource;
}

inline void FOpenGLDynamicRHI::ApplyTextureStage(FOpenGLContextState& ContextState, GLint TextureIndex, const FTextureStage& TextureStage, FOpenGLSamplerState* SamplerState)
{
	GLenum Target = TextureStage.Target;
	VERIFY_GL_SCOPE();
	const bool bHasTexture = (TextureStage.Texture != NULL);
	if (!bHasTexture || TextureStage.Texture->SamplerState != SamplerState)
	{
		// Texture must be bound first
		if( ContextState.ActiveTexture != TextureIndex )
		{
			glActiveTexture(GL_TEXTURE0 + TextureIndex);
			ContextState.ActiveTexture = TextureIndex;
		}

		GLint WrapS = SamplerState->Data.WrapS;
		GLint WrapT = SamplerState->Data.WrapT;
		if (!FOpenGL::SupportsTextureNPOT() && bHasTexture)
		{
			if (!TextureStage.Texture->IsPowerOfTwo())
			{
				bool bChanged = false;
				if (WrapS != GL_CLAMP_TO_EDGE)
				{
					WrapS = GL_CLAMP_TO_EDGE;
					bChanged = true;
				}
				if (WrapT != GL_CLAMP_TO_EDGE)
				{
					WrapT = GL_CLAMP_TO_EDGE;
					bChanged = true;
				}
				if (bChanged)
				{
					ANSICHAR DebugName[128] = "";
					if (FOpenGL::GetLabelObject(GL_TEXTURE, TextureStage.Resource, sizeof(DebugName), DebugName) != 0)
					{
						UE_LOG(LogRHI, Warning, TEXT("Texture %s (Index %d, Resource %d) has a non-clamp mode; switching to clamp to avoid driver problems"), ANSI_TO_TCHAR(DebugName), TextureIndex, TextureStage.Resource);
					}
					else
					{
						UE_LOG(LogRHI, Warning, TEXT("Texture %d (Resource %d) has a non-clamp mode; switching to clamp to avoid driver problems"), TextureIndex, TextureStage.Resource);
					}
				}
			}
		}

		// Sets parameters of currently bound texture
		FOpenGL::TexParameter(Target, GL_TEXTURE_WRAP_S, WrapS);
		FOpenGL::TexParameter(Target, GL_TEXTURE_WRAP_T, WrapT);
		if( FOpenGL::SupportsTexture3D() )
		{
			FOpenGL::TexParameter(Target, GL_TEXTURE_WRAP_R, SamplerState->Data.WrapR);
		}

		if( FOpenGL::SupportsTextureLODBias() )
		{
			FOpenGL::TexParameter(Target, GL_TEXTURE_LOD_BIAS, SamplerState->Data.LODBias);
		}
		// Make sure we don't set mip filtering on if the texture has no mip levels, as that will cause a crash/black render on ES2.
		FOpenGL::TexParameter(Target, GL_TEXTURE_MIN_FILTER, ModifyFilterByMips(SamplerState->Data.MinFilter, TextureStage.bHasMips));
		FOpenGL::TexParameter(Target, GL_TEXTURE_MAG_FILTER, SamplerState->Data.MagFilter);
		if( FOpenGL::SupportsTextureFilterAnisotropic() )
		{
			// GL_EXT_texture_filter_anisotropic requires value to be at least 1
			GLint MaxAnisotropy = FMath::Max(1, SamplerState->Data.MaxAnisotropy);
			FOpenGL::TexParameter(Target, GL_TEXTURE_MAX_ANISOTROPY_EXT, MaxAnisotropy);
		}

		if( FOpenGL::SupportsTextureCompare() )
		{
			FOpenGL::TexParameter(Target, GL_TEXTURE_COMPARE_MODE, SamplerState->Data.CompareMode);
			FOpenGL::TexParameter(Target, GL_TEXTURE_COMPARE_FUNC, SamplerState->Data.CompareFunc);
		}

		if (bHasTexture)
		{
			TextureStage.Texture->SamplerState = SamplerState;
		}
	}
}

template <typename StateType>
void FOpenGLDynamicRHI::SetupTexturesForDraw( FOpenGLContextState& ContextState, const StateType& ShaderState, int32 MaxTexturesNeeded )
{
	VERIFY_GL_SCOPE();
	SCOPE_CYCLE_COUNTER_DETAILED(STAT_OpenGLTextureBindTime);
	
	// Skip texture setup when running bindless texture, it is done with program setup
	if (FOpenGL::SupportsBindlessTexture() && OpenGLConsoleVariables::bBindlessTexture)
	{
		return;
	}

	int32 MaxProgramTexture = 0;
	const TBitArray<>& NeededBits = ShaderState->GetTextureNeeds(MaxProgramTexture);

	for( int32 TextureStageIndex = 0; TextureStageIndex <= MaxProgramTexture; ++TextureStageIndex )
	{
		if (!NeededBits[TextureStageIndex])
		{
			// Current program doesn't make use of this texture stage. No matter what UE4 wants to have on in,
			// it won't be useful for this draw, so telling OpenGL we don't really need it to give the driver
			// more leeway in memory management, and avoid false alarms about same texture being set on
			// texture stage and in framebuffer.
			CachedSetupTextureStage( ContextState, TextureStageIndex, GL_NONE, 0, -1, 1 );
		}
		else
		{
			const FTextureStage& TextureStage = PendingState.Textures[TextureStageIndex];
			
#if UE_BUILD_DEBUG
			// Use the texture SRV's LimitMip value to specify the mip available for sampling
			// This requires SupportsTextureBaseLevel & is a fallback for SupportsTextureView
			// which should be preferred.
			if(!FOpenGL::SupportsTextureView())
			{
				// When trying to limit the mip available for sampling (as part of texture SRV)
				// ensure that the texture is bound to only one sampler, or that all samplers
				// share the same restriction.
				if(TextureStage.LimitMip != -1)
				{
					for( int32 TexIndex = 0; TexIndex <= MaxProgramTexture; ++TexIndex )
					{
						if(TexIndex != TextureStageIndex && ShaderState->NeedsTextureStage(TexIndex))
						{
							const FTextureStage& OtherStage = PendingState.Textures[TexIndex];
							const bool bSameResource = OtherStage.Resource == TextureStage.Resource;
							const bool bSameTarget = OtherStage.Target == TextureStage.Target;
							const GLint TextureStageBaseMip = TextureStage.LimitMip == -1 ? 0 : TextureStage.LimitMip;
							const GLint OtherStageBaseMip = OtherStage.LimitMip == -1 ? 0 : OtherStage.LimitMip;
							const bool bSameLimitMip = TextureStageBaseMip == OtherStageBaseMip;
							const GLint TextureStageMaxMip = TextureStage.LimitMip == -1 ? TextureStage.NumMips - 1 : TextureStage.LimitMip;
							const GLint OtherStageMaxMip = OtherStage.LimitMip == -1 ? OtherStage.NumMips - 1 : OtherStage.LimitMip;
							const bool bSameMaxMip = TextureStageMaxMip == OtherStageMaxMip;
							if( bSameTarget && bSameResource && !bSameLimitMip && !bSameMaxMip )
							{
								UE_LOG(LogRHI, Warning, TEXT("Texture SRV fallback requires that each texture SRV be bound with the same mip-range restrictions. Expect rendering errors."));
							}
						}
					}
				}
			}
#endif
			CachedSetupTextureStage( ContextState, TextureStageIndex, TextureStage.Target, TextureStage.Resource, TextureStage.LimitMip, TextureStage.NumMips );
			
			bool bExternalTexture = (TextureStage.Target == GL_TEXTURE_EXTERNAL_OES);

			if (FOpenGL::SupportsSamplerObjects() && !bExternalTexture)
			{
				FOpenGLSamplerState* PendingSampler = PendingState.SamplerStates[TextureStageIndex];
			
				if (ContextState.SamplerStates[TextureStageIndex] != PendingSampler)
				{
					FOpenGL::BindSampler(TextureStageIndex, PendingSampler ? PendingSampler->Resource : 0);
					ContextState.SamplerStates[TextureStageIndex] = PendingSampler;
				}
			}
			else if (TextureStage.Target != GL_TEXTURE_BUFFER)
			{
				if (FOpenGL::SupportsSamplerObjects())
				{
					FOpenGL::BindSampler(TextureStageIndex, 0);
					ContextState.SamplerStates[TextureStageIndex] = nullptr;
				}
				ApplyTextureStage( ContextState, TextureStageIndex, TextureStage, PendingState.SamplerStates[TextureStageIndex] );
			}
		}
	}

	// For now, continue to clear unused stages
	for( int32 TextureStageIndex = MaxProgramTexture + 1; TextureStageIndex < MaxTexturesNeeded; ++TextureStageIndex )
	{
		CachedSetupTextureStage( ContextState, TextureStageIndex, GL_NONE, 0, -1, 1 );
	}
}

void FOpenGLDynamicRHI::SetupTexturesForDraw( FOpenGLContextState& ContextState )
{
	SetupTexturesForDraw(ContextState, PendingState.BoundShaderState, FOpenGL::GetMaxCombinedTextureImageUnits());
}

void FOpenGLDynamicRHI::InternalSetShaderUAV(GLint UAVIndex, GLenum Format, GLuint Resource, bool bLayered, GLint Layer, GLenum Access)
{
	VERIFY_GL_SCOPE();
	PendingState.UAVs[UAVIndex].Format = Format;
	PendingState.UAVs[UAVIndex].Resource = Resource;
	PendingState.UAVs[UAVIndex].Layer = Layer;
	PendingState.UAVs[UAVIndex].bLayered = bLayered;
	PendingState.UAVs[UAVIndex].Access = Access;
}

void FOpenGLDynamicRHI::SetupUAVsForDraw(FOpenGLContextState& ContextState)
{
	int32 MaxUAVUnitUsed = 0;
	const TBitArray<>& NeededBits = PendingState.BoundShaderState->GetUAVNeeds(MaxUAVUnitUsed);
	SetupUAVsForProgram(ContextState, NeededBits, MaxUAVUnitUsed, FOpenGL::GetMaxPixelUAVUnits());
}

void FOpenGLDynamicRHI::SetupUAVsForCompute(FOpenGLContextState& ContextState, const FOpenGLComputeShader* ComputeShader)
{
	int32 MaxUAVUnitUsed = 0;
	const TBitArray<>& NeededBits = ComputeShader->GetUAVNeeds(MaxUAVUnitUsed);
	SetupUAVsForProgram(ContextState, NeededBits, MaxUAVUnitUsed, FOpenGL::GetMaxComputeUAVUnits());
}

void FOpenGLDynamicRHI::SetupUAVsForProgram(FOpenGLContextState& ContextState, const TBitArray<>& NeededBits, int32 MaxUAVUnitUsed, int32 MaxUAVUnits)
{
	if (MaxUAVUnitUsed < 0 && ContextState.ActiveUAVMask == 0)
	{
		// Quit early if program does not use UAVs and context has no active UAV units
		return;
	}

	for (int32 UAVStageIndex = 0; UAVStageIndex <= MaxUAVUnitUsed; ++UAVStageIndex)
	{
		if (!NeededBits[UAVStageIndex])
		{
			CachedSetupUAVStage(ContextState, UAVStageIndex, GL_R32F, 0, false, 0, GL_READ_WRITE);
		}
		else
		{
			const FUAVStage& UAVStage = PendingState.UAVs[UAVStageIndex];
			CachedSetupUAVStage(ContextState, UAVStageIndex, UAVStage.Format, UAVStage.Resource, UAVStage.bLayered, UAVStage.Layer, UAVStage.Access);
		}
	}

	// clear rest of the units
	for (int32 UAVStageIndex = (MaxUAVUnitUsed+1); UAVStageIndex < MaxUAVUnits; ++UAVStageIndex)
	{
		CachedSetupUAVStage(ContextState, UAVStageIndex, GL_R32F, 0, false, 0, GL_READ_WRITE);
	}
}


void FOpenGLDynamicRHI::CachedSetupUAVStage( FOpenGLContextState& ContextState, GLint UAVIndex, GLenum Format, GLuint Resource, bool bLayered, GLint Layer, GLenum Access)
{
	FUAVStage& UAVStage = ContextState.UAVs[UAVIndex];	

	if (UAVStage.Format == Format && 
		UAVStage.Resource == Resource &&
		UAVStage.Access == Access &&
		UAVStage.Layer == Layer &&
		UAVStage.bLayered == bLayered)
	{
		// Nothing's changed, no need to update
		return;
	}
		
	VERIFY_GL_SCOPE();
	
	FOpenGL::BindImageTexture(UAVIndex, Resource, 0, bLayered ? GL_TRUE : GL_FALSE, Layer, Access, Format);
	
	UAVStage.Format = Format;
	UAVStage.Resource = Resource;
	UAVStage.Access = Access;
	UAVStage.Layer = Layer;
	UAVStage.bLayered = bLayered;

	uint32 UAVBit = 1 << UAVIndex;
	if (Resource != 0)
	{
		ContextState.ActiveUAVMask|= UAVBit;
	}
	else
	{
		ContextState.ActiveUAVMask&= ~UAVBit;
	}
}

void FOpenGLDynamicRHI::UpdateSRV(FOpenGLShaderResourceView* SRV)
{
	check(SRV);
	// For Depth/Stencil textures whose Stencil component we wish to sample we must blit the stencil component out to an intermediate texture when we 'Store' the texture.
#if PLATFORM_DESKTOP || PLATFORM_LUMINGL4
	if (FOpenGL::GetFeatureLevel() >= ERHIFeatureLevel::SM5 && FOpenGL::SupportsPixelBuffers() && IsValidRef(SRV->Texture2D))
	{
		FOpenGLTexture2D* Texture2D = ResourceCast(SRV->Texture2D.GetReference());
		
		uint32 ArrayIndices = 0;
		uint32 MipmapLevels = 0;
		
		GLuint SourceFBO = GetOpenGLFramebuffer(0, nullptr, &ArrayIndices, &MipmapLevels, (FOpenGLTextureBase*)Texture2D);
		
		glBindFramebuffer(GL_FRAMEBUFFER, SourceFBO);
		
		uint32 SizeX = Texture2D->GetSizeX();
		uint32 SizeY = Texture2D->GetSizeY();
		
		uint32 MipBytes = SizeX * SizeY;
		TRefCountPtr<FOpenGLPixelBuffer> PixelBuffer = new FOpenGLPixelBuffer(0, MipBytes, BUF_Dynamic);
		
		glBindBuffer( GL_PIXEL_PACK_BUFFER, 0 );
		glBindBuffer( GL_PIXEL_PACK_BUFFER, PixelBuffer->Resource );
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, SizeX, SizeY, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, nullptr );
		glPixelStorei(GL_PACK_ALIGNMENT, 4);
		glBindBuffer( GL_PIXEL_PACK_BUFFER, 0 );
		
		FOpenGLContextState& ContextState = GetContextStateForCurrentContext();
		
		GLenum Target = SRV->Target;
		
		CachedSetupTextureStage(ContextState, FOpenGL::GetMaxCombinedTextureImageUnits() - 1, Target, SRV->Resource, -1, 1);
		
		CachedBindPixelUnpackBuffer(ContextState, PixelBuffer->Resource);
		
		glPixelStorei(GL_UNPACK_ROW_LENGTH, SizeX);
		
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexSubImage2D(Target, 0, 0, 0, SizeX, SizeY, GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		
		CachedBindPixelUnpackBuffer(ContextState, 0);
		
		glBindFramebuffer(GL_FRAMEBUFFER, ContextState.Framebuffer);
		ContextState.Framebuffer = -1;
	}
#endif
}

void FOpenGLDynamicRHI::RHISetShaderResourceViewParameter(FRHIGraphicsShader* ShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{
	VERIFY_GL_SCOPE();

#ifndef __EMSCRIPTEN__
	// TODO: On WebGL1/GLES2 builds, the following assert() always comes out false, however when simply ignoring this check,
	// everything seems to be working fine. That said, I don't know what should change here, shader resource views are a D3D abstraction,
	// but I think InternalSetShaderTexture() and RHISetShaderSampler() calls below need to occur even on GLES2.
	check(FOpenGL::SupportsResourceView());
#endif

	GLint Index = 0;
	GLint MaxUnits = 0;
	GetShaderStageIndexAndMaxUnits(ShaderRHI, Index, MaxUnits);

	ensureMsgf((int32)TextureIndex < MaxUnits, TEXT("Using more texture units (%d) than allowed (%d) on Frequency %d!"), TextureIndex, MaxUnits, (int32)ShaderRHI->GetFrequency());
	FOpenGLShaderResourceView* SRV = ResourceCast(SRVRHI);
	GLuint Resource = 0;
	GLenum Target = GL_TEXTURE_BUFFER;
	int32 LimitMip = -1;
	if( SRV )
	{
		Resource = SRV->Resource;
		Target = SRV->Target;
		LimitMip = SRV->LimitMip;
		UpdateSRV(SRV);
	}
	ensureMsgf((int32)TextureIndex < MaxUnits, TEXT("Using more textures (%d) than allowed (%d)!"), TextureIndex, MaxUnits);
	InternalSetShaderTexture(NULL, SRV, Index + TextureIndex, Target, Resource, 0, LimitMip);
	RHISetShaderSampler(ShaderRHI, TextureIndex, PointSamplerState);
}

void FOpenGLDynamicRHI::RHISetShaderResourceViewParameter(FRHIComputeShader* ComputeShaderRHI,uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{
	VERIFY_GL_SCOPE();
	check(FOpenGL::SupportsComputeShaders());
	check(FOpenGL::SupportsResourceView());
	FOpenGLShaderResourceView* SRV = ResourceCast(SRVRHI);
	GLuint Resource = 0;
	GLenum Target = GL_TEXTURE_BUFFER;
	int32 LimitMip = -1;
	if( SRV )
	{
		Resource = SRV->Resource;
		Target = SRV->Target;
		LimitMip = SRV->LimitMip;
		UpdateSRV(SRV);
	}
	ensureMsgf((int32)TextureIndex < FOpenGL::GetMaxComputeTextureImageUnits(), TEXT("Using more compute texture units (%d) than allowed (%d)!"), TextureIndex, FOpenGL::GetMaxComputeTextureImageUnits());
	InternalSetShaderTexture(NULL, SRV, FOpenGL::GetFirstComputeTextureUnit() + TextureIndex, Target, Resource, 0, LimitMip);
	RHISetShaderSampler(ComputeShaderRHI,TextureIndex,PointSamplerState);
}

void FOpenGLDynamicRHI::RHISetShaderTexture(FRHIGraphicsShader* ShaderRHI,uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
	VERIFY_GL_SCOPE();
	FOpenGLTextureBase* NewTexture = GetOpenGLTextureFromRHITexture(NewTextureRHI);

	GLint Index = 0;
	GLint MaxUnits = 0;
	GetShaderStageIndexAndMaxUnits(ShaderRHI, Index, MaxUnits);

	ensureMsgf((int32)TextureIndex < MaxUnits, TEXT("Using more texture units (%d) than allowed (%d) on Frequency %d!"), TextureIndex, MaxUnits, (int32)ShaderRHI->GetFrequency());
	if (NewTexture)
	{
		InternalSetShaderTexture(NewTexture, nullptr, Index + TextureIndex, NewTexture->Target, NewTexture->Resource, NewTextureRHI->GetNumMips(), -1);
	}
	else
	{
		InternalSetShaderTexture(nullptr, nullptr, Index + TextureIndex, 0, 0, 0, -1);
	}
}

void FOpenGLDynamicRHI::RHISetShaderSampler(FRHIGraphicsShader* ShaderRHI,uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	VERIFY_GL_SCOPE();
	FOpenGLSamplerState* NewState = ResourceCast(NewStateRHI);

	GLint Index = 0;
	GLint MaxUnits = 0;
	GetShaderStageIndexAndMaxUnits(ShaderRHI, Index, MaxUnits);

	InternalSetSamplerStates(Index + SamplerIndex, NewState);
}

void FOpenGLDynamicRHI::RHISetShaderTexture(FRHIComputeShader* ComputeShaderRHI,uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
	check(FOpenGL::SupportsComputeShaders());
	VERIFY_GL_SCOPE();
	FOpenGLTextureBase* NewTexture = GetOpenGLTextureFromRHITexture(NewTextureRHI);
	ensureMsgf((int32)TextureIndex < FOpenGL::GetMaxComputeTextureImageUnits(), TEXT("Using more compute texture units (%d) than allowed (%d)!"), TextureIndex, FOpenGL::GetMaxComputeTextureImageUnits());
	if (NewTexture)
	{
		InternalSetShaderTexture(NewTexture, nullptr, FOpenGL::GetFirstComputeTextureUnit() + TextureIndex, NewTexture->Target, NewTexture->Resource, NewTextureRHI->GetNumMips(), -1);
	}
	else
	{
		InternalSetShaderTexture(nullptr, nullptr, FOpenGL::GetFirstComputeTextureUnit() + TextureIndex, 0, 0, 0, -1);
	}
}

void FOpenGLDynamicRHI::RHISetGlobalUniformBuffers(const FUniformBufferStaticBindings& InUniformBuffers)
{
	FMemory::Memzero(GlobalUniformBuffers.GetData(), GlobalUniformBuffers.Num() * sizeof(FRHIUniformBuffer*));

	for (int32 Index = 0; Index < InUniformBuffers.GetUniformBufferCount(); ++Index)
	{
		GlobalUniformBuffers[InUniformBuffers.GetSlot(Index)] = InUniformBuffers.GetUniformBuffer(Index);
	}
}

void FOpenGLDynamicRHI::RHISetShaderUniformBuffer(FRHIGraphicsShader* ShaderRHI,uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	VERIFY_GL_SCOPE();
	EShaderFrequency Stage = GetShaderFrequency(ShaderRHI);
	if (Stage != SF_NumFrequencies)
	{
		PendingState.BoundUniformBuffers[Stage][BufferIndex] = BufferRHI;
		PendingState.DirtyUniformBuffers[Stage] |= 1 << BufferIndex;
		PendingState.bAnyDirtyGraphicsUniformBuffers = true;
	}
}

void FOpenGLDynamicRHI::RHISetShaderSampler(FRHIComputeShader* ComputeShaderRHI,uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	check(FOpenGL::SupportsComputeShaders());
	VERIFY_GL_SCOPE();
	FOpenGLSamplerState* NewState = ResourceCast(NewStateRHI);
	InternalSetSamplerStates(FOpenGL::GetFirstComputeTextureUnit() + SamplerIndex, NewState);
}

void FOpenGLDynamicRHI::RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShaderRHI,uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	VERIFY_GL_SCOPE();
	check(FOpenGL::SupportsComputeShaders());
	PendingState.BoundUniformBuffers[SF_Compute][BufferIndex] = BufferRHI;
	PendingState.DirtyUniformBuffers[SF_Compute] |= 1 << BufferIndex;
}

void FOpenGLDynamicRHI::RHISetShaderParameter(FRHIGraphicsShader* ShaderRHI,uint32 BufferIndex,uint32 BaseIndex,uint32 NumBytes,const void* NewValue)
{
	VERIFY_GL_SCOPE();
	CrossCompiler::EShaderStage Stage = GetShaderCrossCompilerStage(ShaderRHI);
	if (Stage != CrossCompiler::NUM_SHADER_STAGES)
	{
		PendingState.ShaderParameters[Stage].Set(BufferIndex, BaseIndex, NumBytes, NewValue);
		PendingState.LinkedProgramAndDirtyFlag = nullptr;
	}
}

void FOpenGLDynamicRHI::RHISetShaderParameter(FRHIComputeShader* ComputeShaderRHI,uint32 BufferIndex,uint32 BaseIndex,uint32 NumBytes,const void* NewValue)
{ 
	VERIFY_GL_SCOPE();
	check(FOpenGL::SupportsComputeShaders());
	PendingState.ShaderParameters[CrossCompiler::SHADER_STAGE_COMPUTE].Set(BufferIndex, BaseIndex, NumBytes, NewValue);
	PendingState.LinkedProgramAndDirtyFlag = nullptr;
}

void FOpenGLDynamicRHI::RHISetDepthStencilState(FRHIDepthStencilState* NewStateRHI,uint32 StencilRef)
{
	VERIFY_GL_SCOPE();
	FOpenGLDepthStencilState* NewState = ResourceCast(NewStateRHI);
	PendingState.DepthStencilState = NewState->Data;
	PendingState.StencilRef = StencilRef;
}

void FOpenGLDynamicRHI::RHISetStencilRef(uint32 StencilRef)
{
	VERIFY_GL_SCOPE();
	PendingState.StencilRef = StencilRef;
}

void FOpenGLDynamicRHI::UpdateDepthStencilStateInOpenGLContext( FOpenGLContextState& ContextState )
{
	VERIFY_GL_SCOPE();
	if (ContextState.DepthStencilState.bZEnable != PendingState.DepthStencilState.bZEnable)
	{
		if (PendingState.DepthStencilState.bZEnable)
		{
			glEnable(GL_DEPTH_TEST);
		}
		else
		{
			glDisable(GL_DEPTH_TEST);
		}
		ContextState.DepthStencilState.bZEnable = PendingState.DepthStencilState.bZEnable;
	}

	if (ContextState.DepthStencilState.bZWriteEnable != PendingState.DepthStencilState.bZWriteEnable)
	{
		glDepthMask(PendingState.DepthStencilState.bZWriteEnable);
		ContextState.DepthStencilState.bZWriteEnable = PendingState.DepthStencilState.bZWriteEnable;
	}

	if (PendingState.DepthStencilState.bZEnable)
	{
		if (ContextState.DepthStencilState.ZFunc != PendingState.DepthStencilState.ZFunc)
		{
			glDepthFunc(PendingState.DepthStencilState.ZFunc);
			ContextState.DepthStencilState.ZFunc = PendingState.DepthStencilState.ZFunc;
		}
	}

	if (ContextState.DepthStencilState.bStencilEnable != PendingState.DepthStencilState.bStencilEnable)
	{
		if (PendingState.DepthStencilState.bStencilEnable)
		{
			glEnable(GL_STENCIL_TEST);
		}
		else
		{
			glDisable(GL_STENCIL_TEST);
		}
		ContextState.DepthStencilState.bStencilEnable = PendingState.DepthStencilState.bStencilEnable;
	}

	// If only two-sided <-> one-sided stencil mode changes, and nothing else, we need to call full set of functions
	// to ensure all drivers handle this correctly - some of them might keep those states in different variables.
	if (ContextState.DepthStencilState.bTwoSidedStencilMode != PendingState.DepthStencilState.bTwoSidedStencilMode)
	{
		// Invalidate cache to enforce update of part of stencil state that needs to be set with different functions, when needed next
		// Values below are all invalid, but they'll never be used, only compared to new values to be set.
		ContextState.DepthStencilState.StencilFunc = 0xFFFF;
		ContextState.DepthStencilState.StencilFail = 0xFFFF;
		ContextState.DepthStencilState.StencilZFail = 0xFFFF;
		ContextState.DepthStencilState.StencilPass = 0xFFFF;
		ContextState.DepthStencilState.CCWStencilFunc = 0xFFFF;
		ContextState.DepthStencilState.CCWStencilFail = 0xFFFF;
		ContextState.DepthStencilState.CCWStencilZFail = 0xFFFF;
		ContextState.DepthStencilState.CCWStencilPass = 0xFFFF;
		ContextState.DepthStencilState.StencilReadMask = 0xFFFF;

		ContextState.DepthStencilState.bTwoSidedStencilMode = PendingState.DepthStencilState.bTwoSidedStencilMode;
	}

	if (PendingState.DepthStencilState.bStencilEnable)
	{
		if (PendingState.DepthStencilState.bTwoSidedStencilMode)
		{
			if (ContextState.DepthStencilState.StencilFunc != PendingState.DepthStencilState.StencilFunc
				|| ContextState.StencilRef != PendingState.StencilRef
				|| ContextState.DepthStencilState.StencilReadMask != PendingState.DepthStencilState.StencilReadMask)
			{
				glStencilFuncSeparate(GL_BACK, PendingState.DepthStencilState.StencilFunc, PendingState.StencilRef, PendingState.DepthStencilState.StencilReadMask);
				ContextState.DepthStencilState.StencilFunc = PendingState.DepthStencilState.StencilFunc;
			}

			if (ContextState.DepthStencilState.StencilFail != PendingState.DepthStencilState.StencilFail
				|| ContextState.DepthStencilState.StencilZFail != PendingState.DepthStencilState.StencilZFail
				|| ContextState.DepthStencilState.StencilPass != PendingState.DepthStencilState.StencilPass)
			{
				glStencilOpSeparate(GL_BACK, PendingState.DepthStencilState.StencilFail, PendingState.DepthStencilState.StencilZFail, PendingState.DepthStencilState.StencilPass);
				ContextState.DepthStencilState.StencilFail = PendingState.DepthStencilState.StencilFail;
				ContextState.DepthStencilState.StencilZFail = PendingState.DepthStencilState.StencilZFail;
				ContextState.DepthStencilState.StencilPass = PendingState.DepthStencilState.StencilPass;
			}

			if (ContextState.DepthStencilState.CCWStencilFunc != PendingState.DepthStencilState.CCWStencilFunc
				|| ContextState.StencilRef != PendingState.StencilRef
				|| ContextState.DepthStencilState.StencilReadMask != PendingState.DepthStencilState.StencilReadMask)
			{
				glStencilFuncSeparate(GL_FRONT, PendingState.DepthStencilState.CCWStencilFunc, PendingState.StencilRef, PendingState.DepthStencilState.StencilReadMask);
				ContextState.DepthStencilState.CCWStencilFunc = PendingState.DepthStencilState.CCWStencilFunc;
			}

			if (ContextState.DepthStencilState.CCWStencilFail != PendingState.DepthStencilState.CCWStencilFail
				|| ContextState.DepthStencilState.CCWStencilZFail != PendingState.DepthStencilState.CCWStencilZFail
				|| ContextState.DepthStencilState.CCWStencilPass != PendingState.DepthStencilState.CCWStencilPass)
			{
				glStencilOpSeparate(GL_FRONT, PendingState.DepthStencilState.CCWStencilFail, PendingState.DepthStencilState.CCWStencilZFail, PendingState.DepthStencilState.CCWStencilPass);
				ContextState.DepthStencilState.CCWStencilFail = PendingState.DepthStencilState.CCWStencilFail;
				ContextState.DepthStencilState.CCWStencilZFail = PendingState.DepthStencilState.CCWStencilZFail;
				ContextState.DepthStencilState.CCWStencilPass = PendingState.DepthStencilState.CCWStencilPass;
			}

			ContextState.DepthStencilState.StencilReadMask = PendingState.DepthStencilState.StencilReadMask;
			ContextState.StencilRef = PendingState.StencilRef;
		}
		else
		{
			if (ContextState.DepthStencilState.StencilFunc != PendingState.DepthStencilState.StencilFunc
				|| ContextState.StencilRef != PendingState.StencilRef
				|| ContextState.DepthStencilState.StencilReadMask != PendingState.DepthStencilState.StencilReadMask)
			{
				glStencilFunc(PendingState.DepthStencilState.StencilFunc, PendingState.StencilRef, PendingState.DepthStencilState.StencilReadMask);
				ContextState.DepthStencilState.StencilFunc = PendingState.DepthStencilState.StencilFunc;
				ContextState.DepthStencilState.StencilReadMask = PendingState.DepthStencilState.StencilReadMask;
				ContextState.StencilRef = PendingState.StencilRef;
			}

			if (ContextState.DepthStencilState.StencilFail != PendingState.DepthStencilState.StencilFail
				|| ContextState.DepthStencilState.StencilZFail != PendingState.DepthStencilState.StencilZFail
				|| ContextState.DepthStencilState.StencilPass != PendingState.DepthStencilState.StencilPass)
			{
				glStencilOp(PendingState.DepthStencilState.StencilFail, PendingState.DepthStencilState.StencilZFail, PendingState.DepthStencilState.StencilPass);
				ContextState.DepthStencilState.StencilFail = PendingState.DepthStencilState.StencilFail;
				ContextState.DepthStencilState.StencilZFail = PendingState.DepthStencilState.StencilZFail;
				ContextState.DepthStencilState.StencilPass = PendingState.DepthStencilState.StencilPass;
			}
		}

		if (ContextState.DepthStencilState.StencilWriteMask != PendingState.DepthStencilState.StencilWriteMask)
		{
			glStencilMask(PendingState.DepthStencilState.StencilWriteMask);
			ContextState.DepthStencilState.StencilWriteMask = PendingState.DepthStencilState.StencilWriteMask;
		}
	}
}

void FOpenGLDynamicRHI::SetPendingBlendStateForActiveRenderTargets( FOpenGLContextState& ContextState )
{
	VERIFY_GL_SCOPE();

	bool bABlendWasSet = false;

	//
	// Need to expand setting for glBlendFunction and glBlendEquation

	const uint32 NumRenderTargets = FOpenGL::SupportsMultipleRenderTargets() ? MaxSimultaneousRenderTargets : 1;

	for (uint32 RenderTargetIndex = 0;RenderTargetIndex < NumRenderTargets; ++RenderTargetIndex)
	{
		if(PendingState.RenderTargets[RenderTargetIndex] == 0)
		{
			// Even if on this stage blend states are incompatible with other stages, we can disregard it, as no render target is assigned to it.
			continue;
		}

		const FOpenGLBlendStateData::FRenderTarget& RenderTargetBlendState = PendingState.BlendState.RenderTargets[RenderTargetIndex];
		FOpenGLBlendStateData::FRenderTarget& CachedRenderTargetBlendState = ContextState.BlendState.RenderTargets[RenderTargetIndex];

		if (CachedRenderTargetBlendState.bAlphaBlendEnable != RenderTargetBlendState.bAlphaBlendEnable)
		{
			if (RenderTargetBlendState.bAlphaBlendEnable)
			{
				FOpenGL::EnableIndexed(GL_BLEND,RenderTargetIndex);
			}
			else
			{
				FOpenGL::DisableIndexed(GL_BLEND,RenderTargetIndex);
			}
			CachedRenderTargetBlendState.bAlphaBlendEnable = RenderTargetBlendState.bAlphaBlendEnable;
		}

		if (RenderTargetBlendState.bAlphaBlendEnable)
		{
			if ( FOpenGL::SupportsSeparateAlphaBlend() )
			{
				// Set current blend per stage
				if (RenderTargetBlendState.bSeparateAlphaBlendEnable)
				{
					if (CachedRenderTargetBlendState.ColorSourceBlendFactor != RenderTargetBlendState.ColorSourceBlendFactor
						|| CachedRenderTargetBlendState.ColorDestBlendFactor != RenderTargetBlendState.ColorDestBlendFactor
						|| CachedRenderTargetBlendState.AlphaSourceBlendFactor != RenderTargetBlendState.AlphaSourceBlendFactor
						|| CachedRenderTargetBlendState.AlphaDestBlendFactor != RenderTargetBlendState.AlphaDestBlendFactor)
					{
						FOpenGL::BlendFuncSeparatei(
							RenderTargetIndex, 
							RenderTargetBlendState.ColorSourceBlendFactor, RenderTargetBlendState.ColorDestBlendFactor,
							RenderTargetBlendState.AlphaSourceBlendFactor, RenderTargetBlendState.AlphaDestBlendFactor
							);
					}

					if (CachedRenderTargetBlendState.ColorBlendOperation != RenderTargetBlendState.ColorBlendOperation
						|| CachedRenderTargetBlendState.AlphaBlendOperation != RenderTargetBlendState.AlphaBlendOperation)
					{
						FOpenGL::BlendEquationSeparatei(
							RenderTargetIndex, 
							RenderTargetBlendState.ColorBlendOperation,
							RenderTargetBlendState.AlphaBlendOperation
							);
					}
				}
				else
				{
					if (CachedRenderTargetBlendState.ColorSourceBlendFactor != RenderTargetBlendState.ColorSourceBlendFactor
						|| CachedRenderTargetBlendState.ColorDestBlendFactor != RenderTargetBlendState.ColorDestBlendFactor
						|| CachedRenderTargetBlendState.AlphaSourceBlendFactor != RenderTargetBlendState.AlphaSourceBlendFactor
						|| CachedRenderTargetBlendState.AlphaDestBlendFactor != RenderTargetBlendState.AlphaDestBlendFactor)
					{
						FOpenGL::BlendFunci(RenderTargetIndex, RenderTargetBlendState.ColorSourceBlendFactor, RenderTargetBlendState.ColorDestBlendFactor);
					}

					if (CachedRenderTargetBlendState.ColorBlendOperation != RenderTargetBlendState.ColorBlendOperation)
					{
						FOpenGL::BlendEquationi(RenderTargetIndex, RenderTargetBlendState.ColorBlendOperation);
					}
				}

				CachedRenderTargetBlendState.bSeparateAlphaBlendEnable = RenderTargetBlendState.bSeparateAlphaBlendEnable;
				CachedRenderTargetBlendState.ColorBlendOperation = RenderTargetBlendState.ColorBlendOperation;
				CachedRenderTargetBlendState.ColorSourceBlendFactor = RenderTargetBlendState.ColorSourceBlendFactor;
				CachedRenderTargetBlendState.ColorDestBlendFactor = RenderTargetBlendState.ColorDestBlendFactor;
				if( RenderTargetBlendState.bSeparateAlphaBlendEnable )
				{ 
					CachedRenderTargetBlendState.AlphaSourceBlendFactor = RenderTargetBlendState.AlphaSourceBlendFactor;
					CachedRenderTargetBlendState.AlphaDestBlendFactor = RenderTargetBlendState.AlphaDestBlendFactor;
				}
				else
				{
					CachedRenderTargetBlendState.AlphaSourceBlendFactor = RenderTargetBlendState.ColorSourceBlendFactor;
					CachedRenderTargetBlendState.AlphaDestBlendFactor = RenderTargetBlendState.ColorDestBlendFactor;
				}
			}
			else
			{
				if( bABlendWasSet )
				{
					// Detect the case of subsequent render target needing different blend setup than one already set in this call.
					if( CachedRenderTargetBlendState.bSeparateAlphaBlendEnable != RenderTargetBlendState.bSeparateAlphaBlendEnable
						|| CachedRenderTargetBlendState.ColorBlendOperation != RenderTargetBlendState.ColorBlendOperation
						|| CachedRenderTargetBlendState.ColorSourceBlendFactor != RenderTargetBlendState.ColorSourceBlendFactor
						|| CachedRenderTargetBlendState.ColorDestBlendFactor != RenderTargetBlendState.ColorDestBlendFactor
						|| ( RenderTargetBlendState.bSeparateAlphaBlendEnable && 
							( CachedRenderTargetBlendState.AlphaSourceBlendFactor != RenderTargetBlendState.AlphaSourceBlendFactor
							|| CachedRenderTargetBlendState.AlphaDestBlendFactor != RenderTargetBlendState.AlphaDestBlendFactor
							)
							)
						)
						UE_LOG(LogRHI, Fatal, TEXT("OpenGL state on draw requires setting different blend operation or factors to different render targets. This is not supported on Mac OS X!"));
				}
				else
				{
					// Set current blend to all stages
					if (RenderTargetBlendState.bSeparateAlphaBlendEnable)
					{
						if (CachedRenderTargetBlendState.ColorSourceBlendFactor != RenderTargetBlendState.ColorSourceBlendFactor
							|| CachedRenderTargetBlendState.ColorDestBlendFactor != RenderTargetBlendState.ColorDestBlendFactor
							|| CachedRenderTargetBlendState.AlphaSourceBlendFactor != RenderTargetBlendState.AlphaSourceBlendFactor
							|| CachedRenderTargetBlendState.AlphaDestBlendFactor != RenderTargetBlendState.AlphaDestBlendFactor)
						{
							glBlendFuncSeparate(
								RenderTargetBlendState.ColorSourceBlendFactor, RenderTargetBlendState.ColorDestBlendFactor,
								RenderTargetBlendState.AlphaSourceBlendFactor, RenderTargetBlendState.AlphaDestBlendFactor
								);
						}

						if (CachedRenderTargetBlendState.ColorBlendOperation != RenderTargetBlendState.ColorBlendOperation
							|| CachedRenderTargetBlendState.AlphaBlendOperation != RenderTargetBlendState.AlphaBlendOperation)
						{
							glBlendEquationSeparate(
								RenderTargetBlendState.ColorBlendOperation,
								RenderTargetBlendState.AlphaBlendOperation
								);
						}
					}
					else
					{
						if (CachedRenderTargetBlendState.ColorSourceBlendFactor != RenderTargetBlendState.ColorSourceBlendFactor
							|| CachedRenderTargetBlendState.ColorDestBlendFactor != RenderTargetBlendState.ColorDestBlendFactor
							|| CachedRenderTargetBlendState.AlphaSourceBlendFactor != RenderTargetBlendState.AlphaSourceBlendFactor
							|| CachedRenderTargetBlendState.AlphaDestBlendFactor != RenderTargetBlendState.AlphaDestBlendFactor)
						{
							glBlendFunc(RenderTargetBlendState.ColorSourceBlendFactor, RenderTargetBlendState.ColorDestBlendFactor);
						}

						if (CachedRenderTargetBlendState.ColorBlendOperation != RenderTargetBlendState.ColorBlendOperation
							|| CachedRenderTargetBlendState.AlphaBlendOperation != RenderTargetBlendState.ColorBlendOperation)
						{
							glBlendEquation(RenderTargetBlendState.ColorBlendOperation);
						}
					}

					// Set cached values of all stages to what they were set by global calls, common to all stages
					for(uint32 RenderTargetIndex2 = 0; RenderTargetIndex2 < MaxSimultaneousRenderTargets; ++RenderTargetIndex2 )
					{
						FOpenGLBlendStateData::FRenderTarget& CachedRenderTargetBlendState2 = ContextState.BlendState.RenderTargets[RenderTargetIndex2];
						CachedRenderTargetBlendState2.bSeparateAlphaBlendEnable = RenderTargetBlendState.bSeparateAlphaBlendEnable;
						CachedRenderTargetBlendState2.ColorBlendOperation = RenderTargetBlendState.ColorBlendOperation;
						CachedRenderTargetBlendState2.ColorSourceBlendFactor = RenderTargetBlendState.ColorSourceBlendFactor;
						CachedRenderTargetBlendState2.ColorDestBlendFactor = RenderTargetBlendState.ColorDestBlendFactor;
						if( RenderTargetBlendState.bSeparateAlphaBlendEnable )
						{
							CachedRenderTargetBlendState2.AlphaBlendOperation = RenderTargetBlendState.AlphaBlendOperation;
							CachedRenderTargetBlendState2.AlphaSourceBlendFactor = RenderTargetBlendState.AlphaSourceBlendFactor;
							CachedRenderTargetBlendState2.AlphaDestBlendFactor = RenderTargetBlendState.AlphaDestBlendFactor;
							CachedRenderTargetBlendState2.AlphaBlendOperation = RenderTargetBlendState.AlphaBlendOperation;
						}
						else
						{
							CachedRenderTargetBlendState2.AlphaBlendOperation = RenderTargetBlendState.ColorBlendOperation;
							CachedRenderTargetBlendState2.AlphaSourceBlendFactor = RenderTargetBlendState.ColorSourceBlendFactor;
							CachedRenderTargetBlendState2.AlphaDestBlendFactor = RenderTargetBlendState.ColorDestBlendFactor;
							CachedRenderTargetBlendState2.AlphaBlendOperation = RenderTargetBlendState.ColorBlendOperation;
						}
					}

					bABlendWasSet = true;
				}
			}
		}

		CachedRenderTargetBlendState.bSeparateAlphaBlendEnable = RenderTargetBlendState.bSeparateAlphaBlendEnable;

		if(CachedRenderTargetBlendState.ColorWriteMaskR != RenderTargetBlendState.ColorWriteMaskR
			|| CachedRenderTargetBlendState.ColorWriteMaskG != RenderTargetBlendState.ColorWriteMaskG
			|| CachedRenderTargetBlendState.ColorWriteMaskB != RenderTargetBlendState.ColorWriteMaskB
			|| CachedRenderTargetBlendState.ColorWriteMaskA != RenderTargetBlendState.ColorWriteMaskA)
		{
			FOpenGL::ColorMaskIndexed(
				RenderTargetIndex,
				RenderTargetBlendState.ColorWriteMaskR,
				RenderTargetBlendState.ColorWriteMaskG,
				RenderTargetBlendState.ColorWriteMaskB,
				RenderTargetBlendState.ColorWriteMaskA
				);

			CachedRenderTargetBlendState.ColorWriteMaskR = RenderTargetBlendState.ColorWriteMaskR;
			CachedRenderTargetBlendState.ColorWriteMaskG = RenderTargetBlendState.ColorWriteMaskG;
			CachedRenderTargetBlendState.ColorWriteMaskB = RenderTargetBlendState.ColorWriteMaskB;
			CachedRenderTargetBlendState.ColorWriteMaskA = RenderTargetBlendState.ColorWriteMaskA;
		}
	}
}

void FOpenGLDynamicRHI::RHISetBlendState(FRHIBlendState* NewStateRHI,const FLinearColor& BlendFactor)
{
	VERIFY_GL_SCOPE();
	FOpenGLBlendState* NewState = ResourceCast(NewStateRHI);
	FMemory::Memcpy(&PendingState.BlendState,&(NewState->Data),sizeof(FOpenGLBlendStateData));
}

void FOpenGLDynamicRHI::SetRenderTargets(
	uint32 NumSimultaneousRenderTargets,
	const FRHIRenderTargetView* NewRenderTargetsRHI,
	const FRHIDepthRenderTargetView* NewDepthStencilTargetRHI)
{
	VERIFY_GL_SCOPE();
	check(NumSimultaneousRenderTargets <= MaxSimultaneousRenderTargets);

	FMemory::Memset(PendingState.RenderTargets,0,sizeof(PendingState.RenderTargets));
	FMemory::Memset(PendingState.RenderTargetMipmapLevels,0,sizeof(PendingState.RenderTargetMipmapLevels));
	FMemory::Memset(PendingState.RenderTargetArrayIndex,0,sizeof(PendingState.RenderTargetArrayIndex));
	PendingState.FirstNonzeroRenderTarget = -1;

	for( int32 RenderTargetIndex = NumSimultaneousRenderTargets - 1; RenderTargetIndex >= 0; --RenderTargetIndex )
	{
		PendingState.RenderTargets[RenderTargetIndex] = GetOpenGLTextureFromRHITexture(NewRenderTargetsRHI[RenderTargetIndex].Texture);
		PendingState.RenderTargetMipmapLevels[RenderTargetIndex] = NewRenderTargetsRHI[RenderTargetIndex].MipIndex;
		PendingState.RenderTargetArrayIndex[RenderTargetIndex] = NewRenderTargetsRHI[RenderTargetIndex].ArraySliceIndex;

		if( PendingState.RenderTargets[RenderTargetIndex] )
		{
			PendingState.FirstNonzeroRenderTarget = (int32)RenderTargetIndex;
		}
	}

	FOpenGLTextureBase* NewDepthStencilRT = GetOpenGLTextureFromRHITexture(NewDepthStencilTargetRHI ? NewDepthStencilTargetRHI->Texture : nullptr);
	
	PendingState.DepthStencil = NewDepthStencilRT;
	PendingState.StencilStoreAction = NewDepthStencilTargetRHI ? NewDepthStencilTargetRHI->GetStencilStoreAction() : ERenderTargetStoreAction::ENoAction;
	PendingState.DepthTargetWidth = NewDepthStencilTargetRHI ? GetOpenGLTextureSizeXFromRHITexture(NewDepthStencilTargetRHI->Texture) : 0u;
	PendingState.DepthTargetHeight = NewDepthStencilTargetRHI ? GetOpenGLTextureSizeYFromRHITexture(NewDepthStencilTargetRHI->Texture) : 0u;
	
	if (PendingState.FirstNonzeroRenderTarget == -1 && !PendingState.DepthStencil)
	{
		// Special case - invalid setup, but sometimes performed by the engine

		PendingState.Framebuffer = 0;
		PendingState.bFramebufferSetupInvalid = true;
		return;
	}

	PendingState.Framebuffer = GetOpenGLFramebuffer(NumSimultaneousRenderTargets, PendingState.RenderTargets, PendingState.RenderTargetArrayIndex, PendingState.RenderTargetMipmapLevels, PendingState.DepthStencil);
	PendingState.bFramebufferSetupInvalid = false;

	if (PendingState.FirstNonzeroRenderTarget != -1)
	{
		// Set viewport size to new render target size.
		PendingState.Viewport.Min.X = 0;
		PendingState.Viewport.Min.Y = 0;

		uint32 Width = 0;
		uint32 Height = 0;

		FOpenGLTexture2D* NewRenderTarget2D = (FOpenGLTexture2D*)NewRenderTargetsRHI[PendingState.FirstNonzeroRenderTarget].Texture->GetTexture2D();
		if(NewRenderTarget2D)
		{
			Width = NewRenderTarget2D->GetSizeX();
			Height = NewRenderTarget2D->GetSizeY();
		}
		else
		{
			FOpenGLTextureCube* NewRenderTargetCube = (FOpenGLTextureCube*)NewRenderTargetsRHI[PendingState.FirstNonzeroRenderTarget].Texture->GetTextureCube();
			if(NewRenderTargetCube)
			{
				Width = NewRenderTargetCube->GetSize();
				Height = NewRenderTargetCube->GetSize();
			}
			else
			{
				FOpenGLTexture3D* NewRenderTarget3D = (FOpenGLTexture3D*)NewRenderTargetsRHI[PendingState.FirstNonzeroRenderTarget].Texture->GetTexture3D();
				if(NewRenderTarget3D)
				{
					Width = NewRenderTarget3D->GetSizeX();
					Height = NewRenderTarget3D->GetSizeY();
				}
				else
				{
					FOpenGLTexture2DArray* NewRenderTarget2DArray = (FOpenGLTexture2DArray*)NewRenderTargetsRHI[PendingState.FirstNonzeroRenderTarget].Texture->GetTexture2DArray();
					if(NewRenderTarget2DArray)
					{
						Width = NewRenderTarget2DArray->GetSizeX();
						Height = NewRenderTarget2DArray->GetSizeY();
					}
					else
					{
						check(0);
					}
				}
			}
		}

		{
			uint32 MipIndex = NewRenderTargetsRHI[PendingState.FirstNonzeroRenderTarget].MipIndex;
			Width = FMath::Max<uint32>(1,(Width >> MipIndex));
			Height = FMath::Max<uint32>(1,(Height >> MipIndex));
		}

		PendingState.Viewport.Max.X = PendingState.RenderTargetWidth = Width;
		PendingState.Viewport.Max.Y = PendingState.RenderTargetHeight = Height;
	}
	else if( NewDepthStencilTargetRHI )
	{
		// Set viewport size to new depth target size.
		PendingState.Viewport.Min.X = 0;
		PendingState.Viewport.Min.Y = 0;
		PendingState.Viewport.Max.X = GetOpenGLTextureSizeXFromRHITexture(NewDepthStencilTargetRHI->Texture);
		PendingState.Viewport.Max.Y = GetOpenGLTextureSizeYFromRHITexture(NewDepthStencilTargetRHI->Texture);
	}
}

void FOpenGLDynamicRHI::RHIDiscardRenderTargets(bool Depth, bool Stencil, uint32 ColorBitMaskIn)
{
	if (FOpenGL::SupportsDiscardFrameBuffer())
	{
		VERIFY_GL_SCOPE();
		const bool bDefaultFramebuffer = (PendingState.Framebuffer == 0);
		uint32 ColorBitMask = ColorBitMaskIn;
		// 8 Color + Depth + Stencil = 10
		GLenum Attachments[MaxSimultaneousRenderTargets + 2];
		uint32 I = 0;
		if (Depth)
		{
			Attachments[I] = bDefaultFramebuffer ? GL_DEPTH : GL_DEPTH_ATTACHMENT;
			I++;
		}
		if (Stencil)
		{
			Attachments[I] = bDefaultFramebuffer ? GL_STENCIL : GL_STENCIL_ATTACHMENT;
			I++;
		}

		if (bDefaultFramebuffer)
		{
			if (ColorBitMask)
			{
				Attachments[I] = GL_COLOR;
				I++;
			}
		}
		else
		{
			ColorBitMask &= (1 << MaxSimultaneousRenderTargets) - 1;
			uint32 J = 0;
			while (ColorBitMask)
			{
				if (ColorBitMask & 1)
				{
					Attachments[I] = GL_COLOR_ATTACHMENT0 + J;
					I++;
				}

				ColorBitMask >>= 1;
				++J;
			}
		}

		FOpenGL::DiscardFramebufferEXT(GL_FRAMEBUFFER, I, Attachments);
	}
}

void FOpenGLDynamicRHI::SetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo)
{
	VERIFY_GL_SCOPE();
	this->SetRenderTargets(RenderTargetsInfo.NumColorRenderTargets,
		RenderTargetsInfo.ColorRenderTarget,
		&RenderTargetsInfo.DepthStencilRenderTarget);

	/**
	 * Convert all load action from NoAction to Clear for tiled GPU on OpenGL platform to avoid an unnecessary load action.
	 */

	bool bIsTiledGPU = RHIHasTiledGPU(GetFeatureLevelShaderPlatform(FOpenGL::GetFeatureLevel()));

	bool bClearColor = RenderTargetsInfo.bClearColor;
	bool bClearStencil = RenderTargetsInfo.bClearStencil;
	bool bClearDepth = RenderTargetsInfo.bClearDepth;

	FLinearColor ClearColors[MaxSimultaneousRenderTargets];
	float DepthClear = 0.0;
	uint32 StencilClear = 0;

	for (int32 i = 0; i < RenderTargetsInfo.NumColorRenderTargets; ++i)
	{
		if (RenderTargetsInfo.ColorRenderTarget[i].Texture != nullptr)
		{
			const FClearValueBinding& ClearValue = RenderTargetsInfo.ColorRenderTarget[i].Texture->GetClearBinding();

			if (bIsTiledGPU)
			{
				bClearColor |= RenderTargetsInfo.ColorRenderTarget[i].LoadAction == ERenderTargetLoadAction::ENoAction;

				ClearColors[i] = ClearValue.ColorBinding == EClearBinding::EColorBound ? ClearValue.GetClearColor() : FLinearColor::Black;
			}
			else if(bClearColor)
			{
				checkf(ClearValue.ColorBinding == EClearBinding::EColorBound, TEXT("Texture: %s does not have a color bound for fast clears"), *RenderTargetsInfo.ColorRenderTarget[i].Texture->GetName().GetPlainNameString());

				ClearColors[i] = ClearValue.GetClearColor();
			}
		}
	}

	if (RenderTargetsInfo.DepthStencilRenderTarget.Texture != nullptr)
	{
		const FClearValueBinding& ClearValue = RenderTargetsInfo.DepthStencilRenderTarget.Texture->GetClearBinding();

		if (bIsTiledGPU)
		{
			bClearStencil |= RenderTargetsInfo.DepthStencilRenderTarget.StencilLoadAction == ERenderTargetLoadAction::ENoAction;

			bClearDepth |= RenderTargetsInfo.DepthStencilRenderTarget.DepthLoadAction == ERenderTargetLoadAction::ENoAction;

			if (ClearValue.ColorBinding == EClearBinding::EDepthStencilBound)
			{
				ClearValue.GetDepthStencil(DepthClear, StencilClear);
			}
		}
		else if (bClearDepth || bClearStencil)
		{
			checkf(ClearValue.ColorBinding == EClearBinding::EDepthStencilBound, TEXT("Texture: %s does not have a DS value bound for fast clears"), *RenderTargetsInfo.DepthStencilRenderTarget.Texture->GetName().GetPlainNameString());

			ClearValue.GetDepthStencil(DepthClear, StencilClear);
		}
	}

	if (bClearColor || bClearStencil || bClearDepth)
	{
		this->RHIClearMRT(bClearColor, RenderTargetsInfo.NumColorRenderTargets, ClearColors, bClearDepth, DepthClear, bClearStencil, StencilClear);
	}
}

// Primitive drawing.

void FOpenGLDynamicRHI::EnableVertexElementCached(
	FOpenGLContextState& ContextState,
	GLuint AttributeIndex,
	const FOpenGLVertexElement &VertexElement,
	GLsizei Stride,
	void *Pointer,
	GLuint Buffer)
{
	VERIFY_GL_SCOPE();

	check( !(FOpenGL::SupportsVertexAttribBinding() && OpenGLConsoleVariables::bUseVAB));

	if (!ContextState.GetVertexAttrEnabled(AttributeIndex))
	{
		ContextState.SetVertexAttrEnabled(AttributeIndex, true);
		glEnableVertexAttribArray(AttributeIndex);
	}
	FOpenGLCachedAttr &Attr = ContextState.VertexAttrs[AttributeIndex];

	bool bAnyDifferent = //bitwise ors to get rid of the branches
		(Attr.Pointer != Pointer) |
		(Attr.Buffer != Buffer) |
		(Attr.Size != VertexElement.Size) |
		(Attr.Type != VertexElement.Type) |
		(Attr.bNormalized != VertexElement.bNormalized) |
		(Attr.Stride != Stride); 

	if (bAnyDifferent)
	{
		CachedBindArrayBuffer(ContextState, Buffer);
		if( !VertexElement.bShouldConvertToFloat )
		{
			FOpenGL::VertexAttribIPointer(
				AttributeIndex,
				VertexElement.Size,
				VertexElement.Type,
				Stride,
				Pointer
				);
		}
		else
		{
			FOpenGL::VertexAttribPointer(
				AttributeIndex,
				VertexElement.Size,
				VertexElement.Type,
				VertexElement.bNormalized,
				Stride,
				Pointer
				);
		}
		
		Attr.Pointer = Pointer;
		Attr.Buffer = Buffer;
		Attr.Size = VertexElement.Size;
		Attr.Type = VertexElement.Type;
		Attr.bNormalized = VertexElement.bNormalized;
		Attr.Stride = Stride;
	}

	if (Attr.Divisor != VertexElement.Divisor)
	{
		FOpenGL::VertexAttribDivisor(AttributeIndex, VertexElement.Divisor);
		Attr.Divisor = VertexElement.Divisor;
	}
}

FORCEINLINE void FOpenGLDynamicRHI::EnableVertexElementCachedZeroStride(FOpenGLContextState& ContextState, GLuint AttributeIndex, const FOpenGLVertexElement& VertexElement, uint32 NumVertices, FOpenGLVertexBuffer* ZeroStrideVertexBuffer)
{
	uint32 Stride = ZeroStrideVertexBuffer->GetSize();
	FOpenGLVertexBuffer* ExpandedVertexBuffer = FindExpandedZeroStrideBuffer(ZeroStrideVertexBuffer, Stride, NumVertices, VertexElement);
	EnableVertexElementCached(ContextState, AttributeIndex, VertexElement, Stride, 0, ExpandedVertexBuffer->Resource);
}

void FOpenGLDynamicRHI::FreeZeroStrideBuffers()
{
	// Forces releasing references to expanded zero stride vertex buffers
	ZeroStrideExpandedBuffersList.Empty();
}

void FOpenGLDynamicRHI::SetupVertexArrays(FOpenGLContextState& ContextState, uint32 BaseVertexIndex, FOpenGLStream* Streams, uint32 NumStreams, uint32 MaxVertices)
{
	SCOPE_CYCLE_COUNTER_DETAILED(STAT_OpenGLVBOSetupTime);
	if (FOpenGL::SupportsVertexAttribBinding() && OpenGLConsoleVariables::bUseVAB)
	{
		SetupVertexArraysVAB(ContextState, BaseVertexIndex, Streams, NumStreams, MaxVertices);
		return;
	}
	VERIFY_GL_SCOPE();
	
	uint32 UsedAttributes = 0;
	static_assert(NUM_OPENGL_VERTEX_STREAMS <= sizeof(UsedAttributes) * 8, "Not enough bits in UsedAttributes to store NUM_OPENGL_VERTEX_STREAMS");

	check(IsValidRef(PendingState.BoundShaderState));

	const FOpenGLShaderBindings& Bindings = PendingState.BoundShaderState->GetVertexShader()->Bindings;

	FOpenGLVertexDeclaration* VertexDeclaration = PendingState.BoundShaderState->VertexDeclaration;
	for (int32 ElementIndex = 0; ElementIndex < VertexDeclaration->VertexElements.Num(); ElementIndex++)
	{
		FOpenGLVertexElement& VertexElement = VertexDeclaration->VertexElements[ElementIndex];
		uint32 AttributeIndex = VertexElement.AttributeIndex;
		const bool bAttribInUse = (Bindings.InOutMask & (0x1 << AttributeIndex)) != 0;
		if (!bAttribInUse)
		{
			continue; // skip unused attributes.
		}

		AttributeIndex = RemapVertexAttrib(Bindings, AttributeIndex);

		if ( VertexElement.StreamIndex < NumStreams)
		{
			FOpenGLStream* Stream = &Streams[VertexElement.StreamIndex];
			uint32 Stride = Stream->Stride;

			if( Stream->VertexBuffer->GetUsage() & BUF_ZeroStride )
			{
				check(Stride == 0);
				check(Stream->Offset == 0);
				check(VertexElement.Offset == 0);
				check(Stream->VertexBuffer->GetZeroStrideBuffer());
				EnableVertexElementCachedZeroStride(
					ContextState,
					AttributeIndex,
					VertexElement,
					MaxVertices,
					Stream->VertexBuffer
					);
			}
			else
			{
				check( Stride > 0 );
				EnableVertexElementCached(
					ContextState,
					AttributeIndex,
					VertexElement,
					Stride,
					INDEX_TO_VOID(BaseVertexIndex * Stride + Stream->Offset + VertexElement.Offset),
					Stream->VertexBuffer->Resource
					);
			}

			UsedAttributes |= (1<<AttributeIndex);
		}
		else
		{
			//workaround attributes with no streams
			VERIFY_GL_SCOPE();

			if (ContextState.GetVertexAttrEnabled(AttributeIndex))
			{
				ContextState.SetVertexAttrEnabled(AttributeIndex, false);
				glDisableVertexAttribArray(AttributeIndex);
			}

			float data[4] = { 0.0f};

			glVertexAttrib4fv(AttributeIndex, data);
		}
	}

	uint32 NotUsedButEnabledMask = (ContextState.VertexAttrs_EnabledBits & ~(UsedAttributes));

	for (GLuint AttribIndex = 0; AttribIndex < NUM_OPENGL_VERTEX_STREAMS && NotUsedButEnabledMask; AttribIndex++)
	{
		if (NotUsedButEnabledMask & 1)
		{
			glDisableVertexAttribArray(AttribIndex);
			ContextState.SetVertexAttrEnabled(AttribIndex, false);
		}
		NotUsedButEnabledMask >>= 1;
	}

	// Disable remaining vertex arrays
}

void FOpenGLDynamicRHI::SetupVertexArraysVAB(FOpenGLContextState& ContextState, uint32 BaseVertexIndex, FOpenGLStream* Streams, uint32 NumStreams, uint32 MaxVertices)
{
	VERIFY_GL_SCOPE();
	bool KnowsDivisor[NUM_OPENGL_VERTEX_STREAMS] = { 0 };
	uint32 Divisor[NUM_OPENGL_VERTEX_STREAMS] = { 0 };
	uint32 LastMaxAttrib = ContextState.MaxActiveAttrib;
	bool UpdateDivisors = false;
	uint32 StreamMask = ContextState.ActiveStreamMask;

	check(IsValidRef(PendingState.BoundShaderState));
	FOpenGLVertexDeclaration* VertexDeclaration = PendingState.BoundShaderState->VertexDeclaration;
	uint32 AttributeMask = PendingState.BoundShaderState->GetVertexShader()->Bindings.InOutMask;
	if (FOpenGL::NeedsVertexAttribRemapTable())
	{
		AttributeMask = PendingState.BoundShaderState->GetVertexShader()->Bindings.VertexRemappedMask;
	}

	if (ContextState.VertexDecl != VertexDeclaration || AttributeMask != ContextState.ActiveAttribMask)
	{
		ContextState.MaxActiveAttrib = 0;
		StreamMask = 0;
		UpdateDivisors = true;

		check(VertexDeclaration->VertexElements.Num() <= 32);

		for (int32 ElementIndex = 0; ElementIndex < VertexDeclaration->VertexElements.Num(); ElementIndex++)
		{
			FOpenGLVertexElement& VertexElement = VertexDeclaration->VertexElements[ElementIndex];
			uint32 AttributeIndex = VertexElement.AttributeIndex;
			const bool bAttribInUse = (PendingState.BoundShaderState->GetVertexShader()->Bindings.InOutMask & (0x1 << AttributeIndex)) != 0;
			if (bAttribInUse)
			{
				AttributeIndex = RemapVertexAttrib(AttributeIndex);
			}

			const uint32 StreamIndex = VertexElement.StreamIndex;

			ContextState.MaxActiveAttrib = FMath::Max( ContextState.MaxActiveAttrib, AttributeIndex);

			//only setup/track attributes actually in use
			FOpenGLCachedAttr &Attr = ContextState.VertexAttrs[AttributeIndex];
			if (bAttribInUse)
			{
				if (VertexElement.StreamIndex < NumStreams)
				{

					// Track the actively used streams, to limit the updates to those in use
					StreamMask |= 0x1 << VertexElement.StreamIndex;

					// Verify that the Divisor is consistent across the stream
					check(!KnowsDivisor[StreamIndex] || Divisor[StreamIndex] == VertexElement.Divisor);
					KnowsDivisor[StreamIndex] = true;
					Divisor[StreamIndex] = VertexElement.Divisor;

					if (
						(Attr.StreamOffset != VertexElement.Offset) ||
						(Attr.Size != VertexElement.Size) ||
						(Attr.Type != VertexElement.Type) ||
						(Attr.bNormalized != VertexElement.bNormalized))
					{
						if (!VertexElement.bShouldConvertToFloat)
						{
							FOpenGL::VertexAttribIFormat(AttributeIndex, VertexElement.Size, VertexElement.Type, VertexElement.Offset);
						}
						else
						{
							FOpenGL::VertexAttribFormat(AttributeIndex, VertexElement.Size, VertexElement.Type, VertexElement.bNormalized, VertexElement.Offset);
						}

						Attr.StreamOffset = VertexElement.Offset;
						Attr.Size = VertexElement.Size;
						Attr.Type = VertexElement.Type;
						Attr.bNormalized = VertexElement.bNormalized;
					}

					if (Attr.StreamIndex != StreamIndex)
					{
						FOpenGL::VertexAttribBinding(AttributeIndex, VertexElement.StreamIndex);
						Attr.StreamIndex = StreamIndex;
					}
				}
				else
				{
					// bogus stream, make sure current value is zero to match D3D
					static float data[4] = { 0.0f };

					glVertexAttrib4fv(AttributeIndex, data);

					//Kill this attribute to make sure it isn't enabled
					AttributeMask &= ~(1 << AttributeIndex);
				}
			}
			else
			{
				if (Attr.StreamIndex != StreamIndex)
				{
					FOpenGL::VertexAttribBinding(AttributeIndex, VertexElement.StreamIndex);
					Attr.StreamIndex = StreamIndex;
				}
			}
		}
		ContextState.VertexDecl = VertexDeclaration;

		//Update the stream mask
		ContextState.ActiveStreamMask = StreamMask;
	}

	//setup streams
	for (uint32 StreamIndex = 0; StreamIndex < NumStreams; StreamIndex++, StreamMask >>= 1)
	{
		FOpenGLStream &CachedStream = ContextState.VertexStreams[StreamIndex];
		FOpenGLStream &Stream = Streams[StreamIndex];
		uint32 Offset = BaseVertexIndex * Stream.Stride + Stream.Offset;
		if ((StreamMask & 0x1) != 0 && Stream.VertexBuffer)
		{
			if ( CachedStream.VertexBuffer != Stream.VertexBuffer || CachedStream.Offset != Offset || CachedStream.Stride != Stream.Stride)
			{
				check(Stream.VertexBuffer->Resource != 0);
				FOpenGL::BindVertexBuffer( StreamIndex, Stream.VertexBuffer->Resource, Offset, Stream.Stride);
				CachedStream.VertexBuffer = Stream.VertexBuffer;
				CachedStream.Offset = Offset;
				CachedStream.Stride = Stream.Stride;
			}
			if (UpdateDivisors && CachedStream.Divisor != Divisor[StreamIndex])
			{
				FOpenGL::VertexBindingDivisor( StreamIndex, Divisor[StreamIndex]);
				CachedStream.Divisor = Divisor[StreamIndex];
			}
		}
		else
		{
			if (((StreamMask & 0x1) != 0) && (Stream.VertexBuffer == nullptr))
			{
				UE_LOG(LogRHI, Error, TEXT("Stream %d marked as in use, but vertex buffer provided is NULL (Mask = %x)"), StreamIndex, StreamMask);
			}
			if (CachedStream.VertexBuffer != Stream.VertexBuffer || CachedStream.Offset != Offset || CachedStream.Stride != Stream.Stride)
			{
				FOpenGL::BindVertexBuffer(StreamIndex, 0, 0, 0);
				CachedStream.VertexBuffer = nullptr;
				CachedStream.Offset = 0;
				CachedStream.Stride = 0;
			}
		}
	}

	//Ensure that all requested streams were set
	check(StreamMask == 0);

	// Set the enable/disable state on the arrays
	uint32 MaskDif = ContextState.ActiveAttribMask ^ AttributeMask;
	if (MaskDif)
	{
		ContextState.ActiveAttribMask = AttributeMask;
		uint32 MaxAttrib = FMath::Max( ContextState.MaxActiveAttrib, LastMaxAttrib);
	
		for (GLuint AttribIndex = 0; AttribIndex < NUM_OPENGL_VERTEX_STREAMS && AttribIndex <= MaxAttrib && MaskDif; AttribIndex++)
		{
			if ( MaskDif & 0x1)
			{
				if ( AttributeMask & 0x1)
				{
					glEnableVertexAttribArray(AttribIndex);
				}
				else
				{
					glDisableVertexAttribArray(AttribIndex);
				}
			}
			AttributeMask >>= 1;
			MaskDif >>= 1;
		}
		check( MaskDif == 0);
	}
}

// Used by default on ES2 for immediate mode rendering.
void FOpenGLDynamicRHI::SetupVertexArraysUP(FOpenGLContextState& ContextState, void* Buffer, uint32 Stride)
{
	VERIFY_GL_SCOPE();

	uint32 UsedAttributes = 0;
	static_assert(NUM_OPENGL_VERTEX_STREAMS <= sizeof(UsedAttributes) * 8, "Not enough bits in UsedAttributes to store NUM_OPENGL_VERTEX_STREAMS");

	check(IsValidRef(PendingState.BoundShaderState));
	FOpenGLVertexDeclaration* VertexDeclaration = PendingState.BoundShaderState->VertexDeclaration;

	const FOpenGLShaderBindings& Bindings = PendingState.BoundShaderState->GetVertexShader()->Bindings;

	for (int32 ElementIndex = 0; ElementIndex < VertexDeclaration->VertexElements.Num(); ElementIndex++)
	{
		FOpenGLVertexElement &VertexElement = VertexDeclaration->VertexElements[ElementIndex];
		check(VertexElement.StreamIndex < 1);

		uint32 AttributeIndex = VertexElement.AttributeIndex;
		const bool bAttribInUse = (Bindings.InOutMask & (0x1 << AttributeIndex)) != 0;
		if (bAttribInUse)
		{
			AttributeIndex = RemapVertexAttrib(Bindings, AttributeIndex);
			check(Stride > 0);
			EnableVertexElementCached(
				ContextState,
				AttributeIndex,
				VertexElement,
				Stride,
				(void*)(((char*)Buffer) + VertexElement.Offset),
				0
				);
			UsedAttributes |= (1 << AttributeIndex);
		}
	}

	uint32 NotUsedButEnabledMask = (ContextState.VertexAttrs_EnabledBits & ~(UsedAttributes));

	for (GLuint AttribIndex = 0; AttribIndex < NUM_OPENGL_VERTEX_STREAMS && NotUsedButEnabledMask; AttribIndex++)
	{
		if (NotUsedButEnabledMask & 1)
		{
			glDisableVertexAttribArray(AttribIndex);
			ContextState.SetVertexAttrEnabled(AttribIndex, false);
		}
		NotUsedButEnabledMask >>= 1;
	}
}

void FOpenGLDynamicRHI::OnProgramDeletion( GLint ProgramResource )
{
	VERIFY_GL_SCOPE();
	if( SharedContextState.Program == ProgramResource )
	{
		SharedContextState.Program = -1;
	}

	if( RenderingContextState.Program == ProgramResource )
	{
		RenderingContextState.Program = -1;
	}
}

void FOpenGLDynamicRHI::OnVertexBufferDeletion( GLuint VertexBufferResource )
{
	VERIFY_GL_SCOPE();
	if (SharedContextState.ArrayBufferBound == VertexBufferResource)
	{
		SharedContextState.ArrayBufferBound = -1;	// will force refresh
	}

	if (RenderingContextState.ArrayBufferBound == VertexBufferResource)
	{
		RenderingContextState.ArrayBufferBound = -1;	// will force refresh
	}

	for (GLuint AttribIndex = 0; AttribIndex < NUM_OPENGL_VERTEX_STREAMS; AttribIndex++)
	{
		if( SharedContextState.VertexAttrs[AttribIndex].Buffer == VertexBufferResource )
		{
			SharedContextState.VertexAttrs[AttribIndex].Pointer = FOpenGLCachedAttr_Invalid;	// that'll enforce state update on next cache test
		}

		if( RenderingContextState.VertexAttrs[AttribIndex].Buffer == VertexBufferResource )
		{
			RenderingContextState.VertexAttrs[AttribIndex].Pointer = FOpenGLCachedAttr_Invalid;	// that'll enforce state update on next cache test
		}
	}

	for (GLuint StreamIndex = 0; StreamIndex < NUM_OPENGL_VERTEX_STREAMS; StreamIndex++)
	{
		if (SharedContextState.VertexStreams[StreamIndex].VertexBuffer != nullptr && SharedContextState.VertexStreams[StreamIndex].VertexBuffer->Resource == VertexBufferResource)
		{
			FOpenGL::BindVertexBuffer(StreamIndex, 0, 0, 0); // brianh@nvidia: work around driver bug 1809000
			SharedContextState.VertexStreams[StreamIndex].VertexBuffer = nullptr;
		}

		if (RenderingContextState.VertexStreams[StreamIndex].VertexBuffer != nullptr && RenderingContextState.VertexStreams[StreamIndex].VertexBuffer->Resource == VertexBufferResource)
		{
			FOpenGL::BindVertexBuffer(StreamIndex, 0, 0, 0); // brianh@nvidia: work around driver bug 1809000
			RenderingContextState.VertexStreams[StreamIndex].VertexBuffer = nullptr;
		}
	}
}

void FOpenGLDynamicRHI::OnIndexBufferDeletion( GLuint IndexBufferResource )
{
	VERIFY_GL_SCOPE();
	if (SharedContextState.ElementArrayBufferBound == IndexBufferResource)
	{
		SharedContextState.ElementArrayBufferBound = -1;	// will force refresh
	}

	if (RenderingContextState.ElementArrayBufferBound == IndexBufferResource)
	{
		RenderingContextState.ElementArrayBufferBound = -1;	// will force refresh
	}
}

void FOpenGLDynamicRHI::OnPixelBufferDeletion( GLuint PixelBufferResource )
{
	VERIFY_GL_SCOPE();
	if (SharedContextState.PixelUnpackBufferBound == PixelBufferResource)
	{
		SharedContextState.PixelUnpackBufferBound = -1;	// will force refresh
	}

	if (RenderingContextState.PixelUnpackBufferBound == PixelBufferResource)
	{
		RenderingContextState.PixelUnpackBufferBound = -1;	// will force refresh
	}
}

void FOpenGLDynamicRHI::OnUniformBufferDeletion( GLuint UniformBufferResource, uint32 AllocatedSize, bool bStreamDraw )
{
	VERIFY_GL_SCOPE();
	if (SharedContextState.UniformBufferBound == UniformBufferResource)
	{
		SharedContextState.UniformBufferBound = -1;	// will force refresh
	}

	if (RenderingContextState.UniformBufferBound == UniformBufferResource)
	{
		RenderingContextState.UniformBufferBound = -1;	// will force refresh
	}

	for (GLuint UniformBufferIndex = 0; UniformBufferIndex < CrossCompiler::NUM_SHADER_STAGES*OGL_MAX_UNIFORM_BUFFER_BINDINGS; UniformBufferIndex++)
	{
		if( SharedContextState.UniformBuffers[UniformBufferIndex] == UniformBufferResource )
		{
			SharedContextState.UniformBuffers[UniformBufferIndex] = FOpenGLCachedUniformBuffer_Invalid;	// that'll enforce state update on next cache test
		}

		if( RenderingContextState.UniformBuffers[UniformBufferIndex] == UniformBufferResource )
		{
			RenderingContextState.UniformBuffers[UniformBufferIndex] = FOpenGLCachedUniformBuffer_Invalid;	// that'll enforce state update on next cache test
		}
	}
}

FORCEINLINE void FOpenGLDynamicRHI::CommitNonComputeShaderConstants()
{
	if (GUseEmulatedUniformBuffers)
	{
		CommitNonComputeShaderConstantsSlowPath();
	}
	else
	{
		FOpenGLLinkedProgram* LinkedProgram = PendingState.BoundShaderState->LinkedProgram;
		if (LinkedProgram == PendingState.LinkedProgramAndDirtyFlag)
		{
			return;
		}
		CommitNonComputeShaderConstantsFastPath(LinkedProgram);
		PendingState.LinkedProgramAndDirtyFlag = LinkedProgram;
	}
}


void FOpenGLDynamicRHI::CommitNonComputeShaderConstantsSlowPath()
{
	VERIFY_GL_SCOPE();
	PendingState.LinkedProgramAndDirtyFlag = nullptr;

	FOpenGLLinkedProgram* LinkedProgram = PendingState.BoundShaderState->LinkedProgram;
	if (GUseEmulatedUniformBuffers)
	{
		PendingState.ShaderParameters[CrossCompiler::SHADER_STAGE_VERTEX].CommitPackedUniformBuffers(LinkedProgram, CrossCompiler::SHADER_STAGE_VERTEX, PendingState.BoundUniformBuffers[SF_Vertex], PendingState.BoundShaderState->GetVertexShader()->UniformBuffersCopyInfo);
	}
	PendingState.ShaderParameters[CrossCompiler::SHADER_STAGE_VERTEX].CommitPackedGlobals(LinkedProgram, CrossCompiler::SHADER_STAGE_VERTEX);

	if (GUseEmulatedUniformBuffers)
	{
		PendingState.ShaderParameters[CrossCompiler::SHADER_STAGE_PIXEL].CommitPackedUniformBuffers(LinkedProgram, CrossCompiler::SHADER_STAGE_PIXEL, PendingState.BoundUniformBuffers[SF_Pixel], PendingState.BoundShaderState->GetPixelShader()->UniformBuffersCopyInfo);
	}
	PendingState.ShaderParameters[CrossCompiler::SHADER_STAGE_PIXEL].CommitPackedGlobals(LinkedProgram, CrossCompiler::SHADER_STAGE_PIXEL);

	if (PendingState.BoundShaderState->GetGeometryShader())
	{
		if (GUseEmulatedUniformBuffers)
		{
			PendingState.ShaderParameters[CrossCompiler::SHADER_STAGE_GEOMETRY].CommitPackedUniformBuffers(LinkedProgram, CrossCompiler::SHADER_STAGE_GEOMETRY, PendingState.BoundUniformBuffers[SF_Geometry], PendingState.BoundShaderState->GetGeometryShader()->UniformBuffersCopyInfo);
		}
		PendingState.ShaderParameters[CrossCompiler::SHADER_STAGE_GEOMETRY].CommitPackedGlobals(LinkedProgram, CrossCompiler::SHADER_STAGE_GEOMETRY);
	}
}

void FOpenGLDynamicRHI::CommitNonComputeShaderConstantsFastPath(FOpenGLLinkedProgram* LinkedProgram)
{
	VERIFY_GL_SCOPE();
	PendingState.ShaderParameters[CrossCompiler::SHADER_STAGE_VERTEX].CommitPackedGlobals(LinkedProgram, CrossCompiler::SHADER_STAGE_VERTEX);
	PendingState.ShaderParameters[CrossCompiler::SHADER_STAGE_PIXEL].CommitPackedGlobals(LinkedProgram, CrossCompiler::SHADER_STAGE_PIXEL);

	if (PendingState.BoundShaderState->GetGeometryShader())
	{
		PendingState.ShaderParameters[CrossCompiler::SHADER_STAGE_GEOMETRY].CommitPackedGlobals(LinkedProgram, CrossCompiler::SHADER_STAGE_GEOMETRY);
	}
}

void FOpenGLDynamicRHI::CommitComputeShaderConstants(FOpenGLComputeShader* ComputeShader)
{
	VERIFY_GL_SCOPE();
	check(FOpenGL::SupportsComputeShaders());

	const int32 Stage = CrossCompiler::SHADER_STAGE_COMPUTE;
	FOpenGLShaderParameterCache& StageShaderParameters = PendingState.ShaderParameters[Stage];

	if (GUseEmulatedUniformBuffers)
	{
		StageShaderParameters.CommitPackedUniformBuffers(ComputeShader->LinkedProgram, Stage, PendingState.BoundUniformBuffers[Stage], ComputeShader->UniformBuffersCopyInfo);
	}
	StageShaderParameters.CommitPackedGlobals(ComputeShader->LinkedProgram, Stage);
	PendingState.LinkedProgramAndDirtyFlag = nullptr;
}

template <EShaderFrequency Frequency>
uint32 GetFirstTextureUnit();

template <> FORCEINLINE uint32 GetFirstTextureUnit<SF_Vertex>() { return FOpenGL::GetFirstVertexTextureUnit(); }
template <> FORCEINLINE uint32 GetFirstTextureUnit<SF_Hull>() { return FOpenGL::GetFirstHullTextureUnit(); }
template <> FORCEINLINE uint32 GetFirstTextureUnit<SF_Domain>() { return FOpenGL::GetFirstDomainTextureUnit(); }
template <> FORCEINLINE uint32 GetFirstTextureUnit<SF_Pixel>() { return FOpenGL::GetFirstPixelTextureUnit(); }
template <> FORCEINLINE uint32 GetFirstTextureUnit<SF_Geometry>() { return FOpenGL::GetFirstGeometryTextureUnit(); }
template <> FORCEINLINE uint32 GetFirstTextureUnit<SF_Compute>() { return FOpenGL::GetFirstComputeTextureUnit(); }

template <EShaderFrequency Frequency>
uint32 GetNumTextureUnits();

template <> FORCEINLINE uint32 GetNumTextureUnits<SF_Vertex>() { return FOpenGL::GetMaxVertexTextureImageUnits(); }
template <> FORCEINLINE uint32 GetNumTextureUnits<SF_Hull>() { return FOpenGL::GetMaxHullTextureImageUnits(); }
template <> FORCEINLINE uint32 GetNumTextureUnits<SF_Domain>() { return FOpenGL::GetMaxDomainTextureImageUnits(); }
template <> FORCEINLINE uint32 GetNumTextureUnits<SF_Pixel>() { return FOpenGL::GetMaxTextureImageUnits(); }
template <> FORCEINLINE uint32 GetNumTextureUnits<SF_Geometry>() { return FOpenGL::GetMaxGeometryTextureImageUnits(); }
template <> FORCEINLINE uint32 GetNumTextureUnits<SF_Compute>() { return FOpenGL::GetMaxComputeTextureImageUnits(); }

template <EShaderFrequency Frequency>
uint32 GetFirstUAVUnit() { return 0; }
template <> FORCEINLINE uint32 GetFirstUAVUnit<SF_Pixel>() { return FOpenGL::GetFirstPixelUAVUnit(); }
template <> FORCEINLINE uint32 GetFirstUAVUnit<SF_Compute>() { return FOpenGL::GetFirstComputeUAVUnit(); }

template <EShaderFrequency Frequency>
uint32 GetNumUAVUnits() { return 0; }
template <> FORCEINLINE uint32 GetNumUAVUnits<SF_Compute>()	{ return FOpenGL::GetMaxComputeUAVUnits(); }
template <> FORCEINLINE uint32 GetNumUAVUnits<SF_Pixel>()	{ return FOpenGL::GetMaxPixelUAVUnits(); }

template <EShaderFrequency Frequency>
void SetResource(FOpenGLDynamicRHI* RESTRICT OpenGLRHI, uint32 BindIndex, FRHITexture* RESTRICT TextureRHI)
{
	FOpenGLTextureBase* Texture = GetOpenGLTextureFromRHITexture(TextureRHI);
#if !UE_BUILD_TEST && !UE_BUILD_SHIPPING
	ensureMsgf(BindIndex < GetNumTextureUnits<Frequency>(), TEXT("Using more %s texture units (%d) than allowed (%d) on a shader unit!"), GetShaderFrequencyString(Frequency, false), BindIndex, GetNumTextureUnits<Frequency>());
#endif
	if (Texture)
	{
		TextureRHI->SetLastRenderTime(FApp::GetCurrentTime());
		OpenGLRHI->InternalSetShaderTexture(Texture, nullptr, GetFirstTextureUnit<Frequency>() + BindIndex, Texture->Target, Texture->Resource, Texture->NumMips, -1);
	}
	else
	{
		OpenGLRHI->InternalSetShaderTexture(Texture, nullptr, GetFirstTextureUnit<Frequency>() + BindIndex, 0, 0, 0, -1);
	}
	// clear any previous sampler state
	OpenGLRHI->InternalSetSamplerStates(GetFirstTextureUnit<Frequency>() + BindIndex, nullptr);
}

template <EShaderFrequency Frequency>
FORCEINLINE void SetResource(FOpenGLDynamicRHI* RESTRICT OpenGLRHI, uint32 BindIndex, FOpenGLSamplerState* RESTRICT SamplerState)
{
	OpenGLRHI->InternalSetSamplerStates(GetFirstTextureUnit<Frequency>() + BindIndex, SamplerState);
}

template <EShaderFrequency Frequency>
FORCEINLINE void SetResource(FOpenGLDynamicRHI* RESTRICT OpenGLRHI, uint32 BindIndex, FRHIShaderResourceView* RESTRICT RHISRV)
{
#if !UE_BUILD_TEST && !UE_BUILD_SHIPPING
	ensureMsgf(BindIndex < GetNumTextureUnits<Frequency>(), TEXT("Using more %s texture units (%d) than allowed (%d) on a shader unit!"), GetShaderFrequencyString(Frequency, false), BindIndex, GetNumTextureUnits<Frequency>());	VERIFY_GL_SCOPE();
#endif
	auto SRV = FOpenGLDynamicRHI::ResourceCast(RHISRV);
	OpenGLRHI->InternalSetShaderTexture(NULL, SRV, GetFirstTextureUnit<Frequency>() + BindIndex, SRV->Target, SRV->Resource, 0, SRV->LimitMip);
	SetResource<Frequency>(OpenGLRHI,BindIndex,OpenGLRHI->GetPointSamplerState());
}

template <EShaderFrequency Frequency>
FORCEINLINE void SetResource(FOpenGLDynamicRHI* RESTRICT OpenGLRHI, uint32 BindIndex, FRHIUnorderedAccessView* RESTRICT RHIUAV)
{
#if !UE_BUILD_TEST && !UE_BUILD_SHIPPING
	ensureMsgf(BindIndex < GetNumUAVUnits<Frequency>(), TEXT("Using more %s image units (%d) than allowed (%d) on a shader unit!"), GetShaderFrequencyString(Frequency, false), BindIndex, GetNumUAVUnits<Frequency>());	VERIFY_GL_SCOPE();
#endif
	auto UAV = FOpenGLDynamicRHI::ResourceCast(RHIUAV);
	GLenum Access = (Frequency == SF_Compute) ? GL_READ_WRITE : GL_WRITE_ONLY;
	// TODO: This must be true for 3D textures
	bool bLayered = false;
	GLint Layer = 0;
	OpenGLRHI->InternalSetShaderUAV(GetFirstUAVUnit<Frequency>() + BindIndex, UAV->Format, UAV->Resource, bLayered, Layer, Access);
}

template <class GLResourceType, EShaderFrequency ShaderFrequency>
FORCEINLINE void SetShaderResourcesFromBuffer(FOpenGLDynamicRHI* RESTRICT OpenGLRHI, FOpenGLUniformBuffer* RESTRICT Buffer, const uint32* RESTRICT ResourceMap, int32 BufferIndex)
{
	uint32 BufferOffset = ResourceMap[BufferIndex];
	if (BufferOffset > 0)
	{
		const TRefCountPtr<FRHIResource>* RESTRICT Resources = Buffer->ResourceTable.GetData();
		const uint32* RESTRICT ResourceInfos = &ResourceMap[BufferOffset];
		uint32 ResourceInfo = *ResourceInfos++;
		do 
		{
			checkSlow(FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
			const uint16 ResourceIndex = FRHIResourceTableEntry::GetResourceIndex(ResourceInfo);
			const uint8 BindIndex = FRHIResourceTableEntry::GetBindIndex(ResourceInfo);

			GLResourceType* ResourcePtr = (GLResourceType*)Resources[ResourceIndex].GetReference();
			SetResource<ShaderFrequency>(OpenGLRHI, BindIndex, ResourcePtr);

			ResourceInfo = *ResourceInfos++;
		} while (FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
	}
}

template <class ShaderType>
FORCEINLINE void FOpenGLDynamicRHI::SetResourcesFromTables(const ShaderType* RESTRICT Shader)
{
	checkSlow(Shader);
	const FOpenGLShaderResourceTable* RESTRICT SRT = &Shader->Bindings.ShaderResourceTable;

	// Mask the dirty bits by those buffers from which the shader has bound resources.
	uint32 DirtyBits = SRT->ResourceTableBits & PendingState.DirtyUniformBuffers[ShaderType::StaticFrequency];
	int32 BufferIndex = 0;
	do
	{
		uint32 LowBit = (DirtyBits & 1);
		if (LowBit)
		{
			FOpenGLUniformBuffer* Buffer = (FOpenGLUniformBuffer*)PendingState.BoundUniformBuffers[ShaderType::StaticFrequency][BufferIndex].GetReference();
			check(Buffer);
			check(BufferIndex < SRT->ResourceTableLayoutHashes.Num());
			check(Buffer->GetLayout().GetHash() == SRT->ResourceTableLayoutHashes[BufferIndex]);

			// todo: could make this two pass: gather then set
			SetShaderResourcesFromBuffer<FRHITexture, (EShaderFrequency)ShaderType::StaticFrequency>(this, Buffer, SRT->TextureMap.GetData(), BufferIndex);
			SetShaderResourcesFromBuffer<FRHIShaderResourceView, (EShaderFrequency)ShaderType::StaticFrequency>(this, Buffer, SRT->ShaderResourceViewMap.GetData(), BufferIndex);
			SetShaderResourcesFromBuffer<FOpenGLSamplerState, (EShaderFrequency)ShaderType::StaticFrequency>(this, Buffer, SRT->SamplerMap.GetData(), BufferIndex);
			SetShaderResourcesFromBuffer<FRHIUnorderedAccessView, (EShaderFrequency)ShaderType::StaticFrequency>(this, Buffer, SRT->UnorderedAccessViewMap.GetData(), BufferIndex);
		}
		BufferIndex++;
		DirtyBits >>= 1;
	} while (DirtyBits);
}


void FOpenGLDynamicRHI::CommitGraphicsResourceTablesInner()
{
	VERIFY_GL_SCOPE();

	if (PendingState.DirtyUniformBuffers[SF_Vertex])
	{
		if (auto* Shader = PendingState.BoundShaderState->GetVertexShader())
		{
			SetResourcesFromTables(Shader);
		}
	}
	if (PendingState.DirtyUniformBuffers[SF_Pixel])
	{
		if (auto* Shader = PendingState.BoundShaderState->GetPixelShader())
		{
			SetResourcesFromTables(Shader);
		}
	}
	if (PendingState.DirtyUniformBuffers[SF_Hull])
	{
		if (auto* Shader = PendingState.BoundShaderState->GetHullShader())
		{
			SetResourcesFromTables(Shader);
		}
	}
	if (PendingState.DirtyUniformBuffers[SF_Domain])
	{
		if (auto* Shader = PendingState.BoundShaderState->GetDomainShader())
		{
			SetResourcesFromTables(Shader);
		}
	}
	if (PendingState.DirtyUniformBuffers[SF_Geometry])
	{
		if (auto* Shader = PendingState.BoundShaderState->GetGeometryShader())
		{
			SetResourcesFromTables(Shader);
		}
	}

	PendingState.bAnyDirtyGraphicsUniformBuffers = false;
	PendingState.DirtyUniformBuffers[SF_Vertex] = 0;
	PendingState.DirtyUniformBuffers[SF_Pixel] = 0;
	PendingState.DirtyUniformBuffers[SF_Hull] = 0;
	PendingState.DirtyUniformBuffers[SF_Domain] = 0;
	PendingState.DirtyUniformBuffers[SF_Geometry] = 0;
}


void FOpenGLDynamicRHI::CommitComputeResourceTables(FOpenGLComputeShader* ComputeShader)
{
	VERIFY_GL_SCOPE();

	check(ComputeShader);
	SetResourcesFromTables(ComputeShader);
	PendingState.DirtyUniformBuffers[SF_Compute] = 0;
}

#if DEBUG_GL_SHADERS
static void VerifyProgramPipeline()
{
	if (FOpenGL::SupportsSeparateShaderObjects())
	{
		VERIFY_GL_SCOPE();
		GLint ProgramPipeline = 0;
		glGetIntegerv(GL_PROGRAM_PIPELINE_BINDING, &ProgramPipeline);
		if(ProgramPipeline)
		{
			FOpenGL::ValidateProgramPipeline(ProgramPipeline);
			GLint LinkStatus = GL_FALSE;
			FOpenGL::GetProgramPipelineiv(ProgramPipeline, GL_VALIDATE_STATUS, &LinkStatus);
			if(LinkStatus == GL_FALSE)
			{
				GLint LogLength = 0;
				FOpenGL::GetProgramPipelineiv(ProgramPipeline, GL_INFO_LOG_LENGTH, &LogLength);
				ANSICHAR DefaultLog[] = "No log";
				ANSICHAR *CompileLog = DefaultLog;
				if (LogLength > 1)
				{
					CompileLog = (ANSICHAR *)FMemory::Malloc(LogLength);
					FOpenGL::GetProgramPipelineInfoLog(ProgramPipeline, LogLength, NULL, CompileLog);
				}
				
				UE_LOG(LogRHI,Error,TEXT("Failed to validate pipeline %d. Compile log:\n%s"), ProgramPipeline,
					   ANSI_TO_TCHAR(CompileLog));
				
				if (LogLength > 1)
				{
					FMemory::Free(CompileLog);
				}
			}
		}
	}
}
#endif

void FOpenGLDynamicRHI::RHIDrawPrimitive(uint32 BaseVertexIndex,uint32 NumPrimitives,uint32 NumInstances)
{
	SCOPE_CYCLE_COUNTER_DETAILED(STAT_OpenGLDrawPrimitiveTime);
	VERIFY_GL_SCOPE();
	RHI_DRAW_CALL_STATS(PrimitiveType,NumPrimitives*NumInstances);

	FOpenGLContextState& ContextState = GetContextStateForCurrentContext();
	BindPendingFramebuffer(ContextState);
	SetPendingBlendStateForActiveRenderTargets(ContextState);
	UpdateViewportInOpenGLContext(ContextState);
	UpdateScissorRectInOpenGLContext(ContextState);
	UpdateRasterizerStateInOpenGLContext(ContextState);
	UpdateDepthStencilStateInOpenGLContext(ContextState);
	BindPendingShaderState(ContextState);
	CommitGraphicsResourceTables();
	SetupTexturesForDraw(ContextState);
	SetupUAVsForDraw(ContextState);
	CommitNonComputeShaderConstants();
	CachedBindElementArrayBuffer(ContextState,0);
	uint32 VertexCount = GetVertexCountForPrimitiveCount(NumPrimitives,PrimitiveType);
	SetupVertexArrays(ContextState, BaseVertexIndex, PendingState.Streams, NUM_OPENGL_VERTEX_STREAMS, VertexCount);

	GLenum DrawMode = GL_TRIANGLES;
	GLsizei NumElements = 0;
	GLint PatchSize = 0;
	FindPrimitiveType(PrimitiveType, ContextState.bUsingTessellation, NumPrimitives, DrawMode, NumElements, PatchSize);

	if (FOpenGL::SupportsTessellation() && DrawMode == GL_PATCHES )
	{
		FOpenGL::PatchParameteri(GL_PATCH_VERTICES, PatchSize);
	}
	
#if DEBUG_GL_SHADERS
	VerifyProgramPipeline();
#endif

	GPUProfilingData.RegisterGPUWork(NumPrimitives * NumInstances, VertexCount * NumInstances);
	if (NumInstances == 1)
	{
		SCOPE_CYCLE_COUNTER_DETAILED(STAT_OpenGLDrawPrimitiveDriverTime);
		CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderFirstDrawTime, PendingState.BoundShaderState->RequiresDriverInstantiation());
		glDrawArrays(DrawMode, 0, NumElements);
		REPORT_GL_DRAW_ARRAYS_EVENT_FOR_FRAME_DUMP( DrawMode, 0, NumElements );
	}
	else
	{
		SCOPE_CYCLE_COUNTER_DETAILED(STAT_OpenGLDrawPrimitiveDriverTime);
		CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderFirstDrawTime, PendingState.BoundShaderState->RequiresDriverInstantiation());
		FOpenGL::DrawArraysInstanced(DrawMode, 0, NumElements, NumInstances);
		REPORT_GL_DRAW_ARRAYS_INSTANCED_EVENT_FOR_FRAME_DUMP( DrawMode, 0, NumElements, NumInstances );
	}
}

void FOpenGLDynamicRHI::RHIDrawPrimitiveIndirect(FRHIVertexBuffer* ArgumentBufferRHI,uint32 ArgumentOffset)
{
	if (FOpenGL::SupportsDrawIndirect())
	{
		VERIFY_GL_SCOPE();

		check(ArgumentBufferRHI);
	GPUProfilingData.RegisterGPUWork(0);

		FOpenGLContextState& ContextState = GetContextStateForCurrentContext();
		BindPendingFramebuffer(ContextState);
		SetPendingBlendStateForActiveRenderTargets(ContextState);
		UpdateViewportInOpenGLContext(ContextState);
		UpdateScissorRectInOpenGLContext(ContextState);
		UpdateRasterizerStateInOpenGLContext(ContextState);
		UpdateDepthStencilStateInOpenGLContext(ContextState);
		BindPendingShaderState(ContextState);
		CommitGraphicsResourceTables();
		SetupTexturesForDraw(ContextState);
		SetupUAVsForDraw(ContextState);
		CommitNonComputeShaderConstants();
		CachedBindElementArrayBuffer(ContextState,0);

		// Zero-stride buffer emulation won't work here, need to use VAB with proper zero strides
		SetupVertexArrays(ContextState, 0, PendingState.Streams, NUM_OPENGL_VERTEX_STREAMS, 1);

		GLenum DrawMode = GL_TRIANGLES;
		GLsizei NumElements = 0;
		GLint PatchSize = 0;
		FindPrimitiveType(PrimitiveType, ContextState.bUsingTessellation, 0, DrawMode, NumElements, PatchSize);

		if (FOpenGL::SupportsTessellation() && DrawMode == GL_PATCHES )
		{
			FOpenGL::PatchParameteri(GL_PATCH_VERTICES, PatchSize);
		} 

		FOpenGLVertexBuffer* ArgumentBuffer = ResourceCast(ArgumentBufferRHI);


		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, ArgumentBuffer->Resource);
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderFirstDrawTime, PendingState.BoundShaderState->RequiresDriverInstantiation());
			FOpenGL::DrawArraysIndirect( DrawMode, INDEX_TO_VOID(ArgumentOffset));
		}
		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0);
	}
	else
	{
		UE_LOG(LogRHI, Fatal,TEXT("OpenGL RHI does not yet support indirect draw calls."));
	}

}

void FOpenGLDynamicRHI::RHIDrawIndexedIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIStructuredBuffer* ArgumentsBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances)
{
	if (FOpenGL::SupportsDrawIndirect())
	{
		VERIFY_GL_SCOPE();

		FOpenGLIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
	GPUProfilingData.RegisterGPUWork(1);

		check(ArgumentsBufferRHI);

		//Draw indiect has to have a number of instances
		check(NumInstances > 1);

		FOpenGLContextState& ContextState = GetContextStateForCurrentContext();
		BindPendingFramebuffer(ContextState);
		SetPendingBlendStateForActiveRenderTargets(ContextState);
		UpdateViewportInOpenGLContext(ContextState);
		UpdateScissorRectInOpenGLContext(ContextState);
		UpdateRasterizerStateInOpenGLContext(ContextState);
		UpdateDepthStencilStateInOpenGLContext(ContextState);
		BindPendingShaderState(ContextState);
		CommitGraphicsResourceTables();
		SetupTexturesForDraw(ContextState);
		SetupUAVsForDraw(ContextState);
		CommitNonComputeShaderConstants();
		CachedBindElementArrayBuffer(ContextState,IndexBuffer->Resource);

		// Zero-stride buffer emulation won't work here, need to use VAB with proper zero strides
		SetupVertexArrays(ContextState, 0, PendingState.Streams, NUM_OPENGL_VERTEX_STREAMS, 1);

		GLenum DrawMode = GL_TRIANGLES;
		GLsizei NumElements = 0;
		GLint PatchSize = 0;
		FindPrimitiveType(PrimitiveType, ContextState.bUsingTessellation, 0, DrawMode, NumElements, PatchSize);

		if (FOpenGL::SupportsTessellation() && DrawMode == GL_PATCHES )
		{
			FOpenGL::PatchParameteri(GL_PATCH_VERTICES, PatchSize);
		} 

		GLenum IndexType = IndexBuffer->GetStride() == sizeof(uint32) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;

		FOpenGLStructuredBuffer* ArgumentsBuffer = ResourceCast(ArgumentsBufferRHI);


		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, ArgumentsBuffer->Resource);
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderFirstDrawTime, PendingState.BoundShaderState->RequiresDriverInstantiation());
	
			// Offset is based on an index into the list of structures
			FOpenGL::DrawElementsIndirect( DrawMode, IndexType, INDEX_TO_VOID(DrawArgumentsIndex * 5 *sizeof(uint32)));
		}
		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0);
	}
	else
	{
		UE_LOG(LogRHI, Fatal,TEXT("OpenGL RHI does not yet support indirect draw calls."));
	}
}

void FOpenGLDynamicRHI::RHIDrawIndexedPrimitive(FRHIIndexBuffer* IndexBufferRHI,int32 BaseVertexIndex,uint32 FirstInstance,uint32 NumVertices,uint32 StartIndex,uint32 NumPrimitives,uint32 NumInstances)
{
	SCOPE_CYCLE_COUNTER_DETAILED(STAT_OpenGLDrawPrimitiveTime);
	VERIFY_GL_SCOPE();

	FOpenGLIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);

	RHI_DRAW_CALL_STATS(PrimitiveType,NumPrimitives*NumInstances);

	FOpenGLContextState& ContextState = GetContextStateForCurrentContext();
	{
		DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_BindPendingFramebuffer);
		BindPendingFramebuffer(ContextState);
	}
	{
		DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_SetPendingBlendStateForActiveRenderTargets);
		SetPendingBlendStateForActiveRenderTargets(ContextState);
	}
	{
		DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateViewportInOpenGLContext);
		UpdateViewportInOpenGLContext(ContextState);
	}
	{
		DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateScissorRectInOpenGLContext);
		UpdateScissorRectInOpenGLContext(ContextState);
	}
	{
		DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateRasterizerStateInOpenGLContext);
		UpdateRasterizerStateInOpenGLContext(ContextState);
	}
	{
		DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateDepthStencilStateInOpenGLContext);
		UpdateDepthStencilStateInOpenGLContext(ContextState);
	}
	{
		DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_BindPendingShaderState);
		BindPendingShaderState(ContextState);
	}
	{
		DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_CommitGraphicsResourceTables);
		CommitGraphicsResourceTables();
	}
	{
		DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_SetupTexturesForDraw);
		SetupTexturesForDraw(ContextState);
	}
	{
		DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_SetupUAVsForDraw);
		SetupUAVsForDraw(ContextState);
	}
	{
		DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_CommitNonComputeShaderConstants);
		CommitNonComputeShaderConstants();
	}
	{
		DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_CachedBindElementArrayBuffer);
		CachedBindElementArrayBuffer(ContextState, IndexBuffer->Resource);
	}
	{
		DETAILED_QUICK_SCOPE_CYCLE_COUNTER(STAT_SetupVertexArrays);
		SetupVertexArrays(ContextState, BaseVertexIndex, PendingState.Streams, NUM_OPENGL_VERTEX_STREAMS, NumVertices + StartIndex);
	}

	GLenum DrawMode = GL_TRIANGLES;
	GLsizei NumElements = 0;
	GLint PatchSize = 0;
	FindPrimitiveType(PrimitiveType, ContextState.bUsingTessellation, NumPrimitives, DrawMode, NumElements, PatchSize);

	if (FOpenGL::SupportsTessellation() && DrawMode == GL_PATCHES )
	{
		FOpenGL::PatchParameteri(GL_PATCH_VERTICES, PatchSize);
	}

	GLenum IndexType = IndexBuffer->GetStride() == sizeof(uint32) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
	StartIndex *= IndexBuffer->GetStride() == sizeof(uint32) ? sizeof(uint32) : sizeof(uint16);

#if DEBUG_GL_SHADERS
	VerifyProgramPipeline();
#endif

	GPUProfilingData.RegisterGPUWork(NumPrimitives * NumInstances, NumElements * NumInstances);
	if (NumInstances > 1)
	{
		SCOPE_CYCLE_COUNTER_DETAILED(STAT_OpenGLDrawPrimitiveDriverTime);
		CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderFirstDrawTime, PendingState.BoundShaderState->RequiresDriverInstantiation());
		checkf(FirstInstance  == 0, TEXT("FirstInstance is currently unsupported on this RHI"));
		FOpenGL::DrawElementsInstanced(DrawMode, NumElements, IndexType, INDEX_TO_VOID(StartIndex), NumInstances);
		REPORT_GL_DRAW_ELEMENTS_INSTANCED_EVENT_FOR_FRAME_DUMP(DrawMode, NumElements, IndexType, (void *)StartIndex, NumInstances);
	}
	else
	{
		SCOPE_CYCLE_COUNTER_DETAILED(STAT_OpenGLDrawPrimitiveDriverTime);
		CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderFirstDrawTime, PendingState.BoundShaderState->RequiresDriverInstantiation());
		if ( FOpenGL::SupportsDrawIndexOffset() )
		{
			FOpenGL::DrawRangeElements(DrawMode, 0, NumVertices, NumElements, IndexType, INDEX_TO_VOID(StartIndex));
		}
		else
		{
			glDrawElements(DrawMode, NumElements, IndexType, INDEX_TO_VOID(StartIndex));
		}
		REPORT_GL_DRAW_RANGE_ELEMENTS_EVENT_FOR_FRAME_DUMP(DrawMode, MinIndex, MinIndex + NumVertices, NumElements, IndexType, (void *)StartIndex);
	}
}

void FOpenGLDynamicRHI::RHIDrawIndexedPrimitiveIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIVertexBuffer* ArgumentBufferRHI,uint32 ArgumentOffset)
{
	if (FOpenGL::SupportsDrawIndirect())
	{
		VERIFY_GL_SCOPE();

		FOpenGLIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);
		GPUProfilingData.RegisterGPUWork(1);

		check(ArgumentBufferRHI);

		FOpenGLContextState& ContextState = GetContextStateForCurrentContext();
		BindPendingFramebuffer(ContextState);
		SetPendingBlendStateForActiveRenderTargets(ContextState);
		UpdateViewportInOpenGLContext(ContextState);
		UpdateScissorRectInOpenGLContext(ContextState);
		UpdateRasterizerStateInOpenGLContext(ContextState);
		UpdateDepthStencilStateInOpenGLContext(ContextState);
		BindPendingShaderState(ContextState);
		CommitGraphicsResourceTables();
		SetupTexturesForDraw(ContextState);
		SetupUAVsForDraw(ContextState);
		CommitNonComputeShaderConstants();
		CachedBindElementArrayBuffer(ContextState,IndexBuffer->Resource);

		// @ToDo Zero-stride buffer emulation won't work here, need to use VAB with proper zero strides
		SetupVertexArrays(ContextState, 0, PendingState.Streams, NUM_OPENGL_VERTEX_STREAMS, 1);

		GLenum DrawMode = GL_TRIANGLES;
		GLsizei NumElements = 0;
		GLint PatchSize = 0;
		FindPrimitiveType(PrimitiveType, ContextState.bUsingTessellation, 0, DrawMode, NumElements, PatchSize);

		if (FOpenGL::SupportsTessellation() && DrawMode == GL_PATCHES )
		{
			FOpenGL::PatchParameteri(GL_PATCH_VERTICES, PatchSize);
		} 

		GLenum IndexType = IndexBuffer->GetStride() == sizeof(uint32) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;

		FOpenGLVertexBuffer* ArgumentBuffer = ResourceCast(ArgumentBufferRHI);


		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, ArgumentBuffer->Resource);
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_OpenGLShaderFirstDrawTime, PendingState.BoundShaderState->RequiresDriverInstantiation());
		
			// Offset is based on an index into the list of structures
			FOpenGL::DrawElementsIndirect( DrawMode, IndexType, INDEX_TO_VOID(ArgumentOffset));
		}
		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0);
	}
	else
	{
		UE_LOG(LogRHI, Fatal,TEXT("OpenGL RHI does not yet support indirect draw calls."));
	}
}

// Raster operations.
static inline void ClearCurrentDepthStencilWithCurrentScissor( int8 ClearType, float Depth, uint32 Stencil )
{
	switch (ClearType)
	{
	case CT_DepthStencil:	// Clear depth and stencil
		FOpenGL::ClearBufferfi(GL_DEPTH_STENCIL, 0, Depth, Stencil);
		break;

	case CT_Stencil:	// Clear stencil only
		FOpenGL::ClearBufferiv(GL_STENCIL, 0, (const GLint*)&Stencil);
		break;

	case CT_Depth:	// Clear depth only
		FOpenGL::ClearBufferfv(GL_DEPTH, 0, &Depth);
		break;

	default:
		break;	// impossible anyway
	}
}

void FOpenGLDynamicRHI::ClearCurrentFramebufferWithCurrentScissor(FOpenGLContextState& ContextState, int8 ClearType, int32 NumClearColors, const FLinearColor* ClearColorArray, float Depth, uint32 Stencil)
{
	VERIFY_GL_SCOPE();
		
	if ( FOpenGL::SupportsMultipleRenderTargets() )
	{
		// Clear color buffers
		if (ClearType & CT_Color)
		{
			for(int32 ColorIndex = 0; ColorIndex < NumClearColors; ++ColorIndex)
			{
				FOpenGL::ClearBufferfv( GL_COLOR, ColorIndex, (const GLfloat*)&ClearColorArray[ColorIndex] );
			}
		}

		if (ClearType & CT_DepthStencil)
		{
			ClearCurrentDepthStencilWithCurrentScissor(ClearType & CT_DepthStencil, Depth, Stencil);
		}
	}
	else
	{
		GLuint Mask = 0;
		if( ClearType & CT_Color && NumClearColors > 0 )
		{
			if (!ContextState.BlendState.RenderTargets[0].ColorWriteMaskR ||
				!ContextState.BlendState.RenderTargets[0].ColorWriteMaskG ||
				!ContextState.BlendState.RenderTargets[0].ColorWriteMaskB ||
				!ContextState.BlendState.RenderTargets[0].ColorWriteMaskA)
			{
				FOpenGL::ColorMaskIndexed(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				ContextState.BlendState.RenderTargets[0].ColorWriteMaskR = 1;
				ContextState.BlendState.RenderTargets[0].ColorWriteMaskG = 1;
				ContextState.BlendState.RenderTargets[0].ColorWriteMaskB = 1;
				ContextState.BlendState.RenderTargets[0].ColorWriteMaskA = 1;
			}

			if (ContextState.ClearColor != ClearColorArray[0])
			{
				glClearColor( ClearColorArray[0].R, ClearColorArray[0].G, ClearColorArray[0].B, ClearColorArray[0].A );
				ContextState.ClearColor = ClearColorArray[0];
			}
			Mask |= GL_COLOR_BUFFER_BIT;
		}
		if ( ClearType & CT_Depth )
		{
			if (!ContextState.DepthStencilState.bZWriteEnable)
			{
				glDepthMask(GL_TRUE);
				ContextState.DepthStencilState.bZWriteEnable = true;
			}
			if (ContextState.ClearDepth != Depth)
			{
				FOpenGL::ClearDepth( Depth );
				ContextState.ClearDepth = Depth;
			}
			Mask |= GL_DEPTH_BUFFER_BIT;
		}
		if ( ClearType & CT_Stencil )
		{
			if (ContextState.DepthStencilState.StencilWriteMask != 0xFFFFFFFF)
			{
				glStencilMask(0xFFFFFFFF);
				ContextState.DepthStencilState.StencilWriteMask = 0xFFFFFFFF;
			}

			if (ContextState.ClearStencil != Stencil)
			{
				glClearStencil( Stencil );
				ContextState.ClearStencil = Stencil;
			}
			Mask |= GL_STENCIL_BUFFER_BIT;
		}

		// do the clear
		glClear( Mask );
	}

	REPORT_GL_CLEAR_EVENT_FOR_FRAME_DUMP( ClearType, NumClearColors, (const float*)ClearColorArray, Depth, Stencil );
}

void FOpenGLDynamicRHI::RHIClearMRT(bool bClearColor,int32 NumClearColors,const FLinearColor* ClearColorArray,bool bClearDepth,float Depth,bool bClearStencil,uint32 Stencil)
{
	FIntRect ExcludeRect;
	VERIFY_GL_SCOPE();

	check((GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM5) || !PendingState.bFramebufferSetupInvalid);

	if (bClearColor)
	{
		// This is copied from DirectX11 code - apparently there's a silent assumption that there can be no valid render target set at index higher than an invalid one.
		int32 NumActiveRenderTargets = 0;
		for (int32 TargetIndex = 0; TargetIndex < MaxSimultaneousRenderTargets; TargetIndex++)
		{
			if (PendingState.RenderTargets[TargetIndex] != 0)
			{
				NumActiveRenderTargets++;
			}
			else
			{
				break;
			}
		}
		
		// Must specify enough clear colors for all active RTs
		check(NumClearColors >= NumActiveRenderTargets);
	}

	// Remember cached scissor state, and set one to cover viewport
	FIntRect PrevScissor = PendingState.Scissor;
	bool bPrevScissorEnabled = PendingState.bScissorEnabled;

	bool bScissorChanged = false;
	GPUProfilingData.RegisterGPUWork(0);
	FOpenGLContextState& ContextState = GetContextStateForCurrentContext();
	BindPendingFramebuffer(ContextState);

	if (bPrevScissorEnabled || PendingState.Viewport.Min.X != 0 || PendingState.Viewport.Min.Y != 0 || PendingState.Viewport.Max.X != PendingState.RenderTargetWidth || PendingState.Viewport.Max.Y != PendingState.RenderTargetHeight)
	{
		RHISetScissorRect(false, 0, 0, 0, 0);
		bScissorChanged = true;
	}

	// Always update in case there are uncommitted changes to disable scissor
	UpdateScissorRectInOpenGLContext(ContextState);

	int8 ClearType = CT_None;

	// Prepare color buffer masks, if applicable
	if (bClearColor)
	{
		ClearType |= CT_Color;

		for(int32 ColorIndex = 0; ColorIndex < NumClearColors; ++ColorIndex)
		{
			if( !ContextState.BlendState.RenderTargets[ColorIndex].ColorWriteMaskR ||
				!ContextState.BlendState.RenderTargets[ColorIndex].ColorWriteMaskG ||
				!ContextState.BlendState.RenderTargets[ColorIndex].ColorWriteMaskB ||
				!ContextState.BlendState.RenderTargets[ColorIndex].ColorWriteMaskA)
			{
				FOpenGL::ColorMaskIndexed(ColorIndex, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				ContextState.BlendState.RenderTargets[ColorIndex].ColorWriteMaskR = 1;
				ContextState.BlendState.RenderTargets[ColorIndex].ColorWriteMaskG = 1;
				ContextState.BlendState.RenderTargets[ColorIndex].ColorWriteMaskB = 1;
				ContextState.BlendState.RenderTargets[ColorIndex].ColorWriteMaskA = 1;
			}
		}
	}

	// Prepare depth mask, if applicable
	if (bClearDepth && PendingState.DepthStencil)
	{
		ClearType |= CT_Depth;

		if (!ContextState.DepthStencilState.bZWriteEnable)
		{
			glDepthMask(GL_TRUE);
			ContextState.DepthStencilState.bZWriteEnable = true;
		}
	}

	// Prepare stencil mask, if applicable
	if (bClearStencil && PendingState.DepthStencil)
	{
		ClearType |= CT_Stencil;

		if (ContextState.DepthStencilState.StencilWriteMask != 0xFFFFFFFF)
		{
			glStencilMask(0xFFFFFFFF);
			ContextState.DepthStencilState.StencilWriteMask = 0xFFFFFFFF;
		}
	}

	// Just one clear
	ClearCurrentFramebufferWithCurrentScissor(ContextState, ClearType, NumClearColors, ClearColorArray, Depth, Stencil);

	if (bScissorChanged)
	{
		// Change it back
		RHISetScissorRect(bPrevScissorEnabled,PrevScissor.Min.X, PrevScissor.Min.Y, PrevScissor.Max.X, PrevScissor.Max.Y);
	}
}

// Blocks the CPU until the GPU catches up and goes idle.
void FOpenGLDynamicRHI::RHIBlockUntilGPUIdle()
{
	// Not really supported
}

void FOpenGLDynamicRHI::RHISubmitCommandsAndFlushGPU()
{
	RunOnGLRenderContextThread([&]()
	{
		FOpenGL::Flush();
		RHIPollOcclusionQueries();
	});
}

/**
 * Returns the total GPU time taken to render the last frame. Same metric as FPlatformTime::Cycles().
 */
uint32 FOpenGLDynamicRHI::RHIGetGPUFrameCycles(uint32 GPUIndex)
{
	check(GPUIndex == 0);
	return GGPUFrameTime;
}

void FOpenGLDynamicRHI::RHISetComputeShader(FRHIComputeShader* ComputeShaderRHI)
{
	if (OpenGLConsoleVariables::bSkipCompute)
	{
		return;
	}

	if (FOpenGL::SupportsComputeShaders())
	{
		PendingState.CurrentComputeShader = ComputeShaderRHI;
	}
	else
	{
		UE_LOG(LogRHI,Fatal,TEXT("Platform doesn't support SM5 for OpenGL but set feature level to SM5"));
	}

	ApplyGlobalUniformBuffers(ComputeShaderRHI, ResourceCast(ComputeShaderRHI));
}

void FOpenGLDynamicRHI::RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ)
{ 
	if (OpenGLConsoleVariables::bSkipCompute)
	{
		return;
	}

	if (FOpenGL::SupportsComputeShaders())
	{
		VERIFY_GL_SCOPE();
		
		FRHIComputeShader* ComputeShaderRHI = PendingState.CurrentComputeShader;
		check(ComputeShaderRHI);

		FOpenGLComputeShader* ComputeShader = ResourceCast(ComputeShaderRHI);

		if (ComputeShader->LinkedProgram == nullptr)
		{
			ComputeShader->LinkedProgram = GetLinkedComputeProgram(ComputeShaderRHI);
		}
		FOpenGLContextState& ContextState = GetContextStateForCurrentContext();

		GPUProfilingData.RegisterGPUDispatch(FIntVector(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ));	

		BindPendingComputeShaderState(ContextState, ComputeShader);
		CommitComputeResourceTables(ComputeShader);
		SetupTexturesForDraw(ContextState, ComputeShader, FOpenGL::GetMaxComputeTextureImageUnits());
		SetupUAVsForCompute(ContextState, ComputeShader);
		CommitComputeShaderConstants(ComputeShader);
	
		FOpenGL::MemoryBarrier(GL_ALL_BARRIER_BITS);
		FOpenGL::DispatchCompute(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
		FOpenGL::MemoryBarrier(GL_ALL_BARRIER_BITS);
	}
	else
	{
		UE_LOG(LogRHI,Fatal,TEXT("Platform doesn't support SM5 for OpenGL but set feature level to SM5"));
	}
}

void FOpenGLDynamicRHI::RHIDispatchIndirectComputeShader(FRHIVertexBuffer* ArgumentBufferRHI,uint32 ArgumentOffset)
{
	if (FOpenGL::SupportsComputeShaders())
	{
		VERIFY_GL_SCOPE();
		
		FRHIComputeShader* ComputeShaderRHI = PendingState.CurrentComputeShader;
		check(ComputeShaderRHI);

		FOpenGLComputeShader* ComputeShader = ResourceCast(ComputeShaderRHI);
		if (ComputeShader->LinkedProgram == nullptr)
		{
			ComputeShader->LinkedProgram = GetLinkedComputeProgram(ComputeShaderRHI);
		}

		FOpenGLVertexBuffer* ArgumentBuffer = ResourceCast(ArgumentBufferRHI);

		FOpenGLContextState& ContextState = GetContextStateForCurrentContext();

		GPUProfilingData.RegisterGPUDispatch(FIntVector(1, 1, 1));	

		BindPendingComputeShaderState(ContextState, ComputeShader);

		SetupTexturesForDraw(ContextState, ComputeShader, FOpenGL::GetMaxComputeTextureImageUnits());

		SetupUAVsForCompute(ContextState, ComputeShader);
	
		CommitComputeShaderConstants(ComputeShader);
	
		FOpenGL::MemoryBarrier(GL_ALL_BARRIER_BITS);

		glBindBuffer( GL_DISPATCH_INDIRECT_BUFFER, ArgumentBuffer->Resource);
	
		FOpenGL::DispatchComputeIndirect(ArgumentOffset);

		glBindBuffer( GL_DISPATCH_INDIRECT_BUFFER, 0);
	
		FOpenGL::MemoryBarrier(GL_ALL_BARRIER_BITS);

	}
	else
	{
		UE_LOG(LogRHI,Fatal,TEXT("Platform doesn't support SM5 for OpenGL but set feature level to SM5"));
	}
}

void FOpenGLDynamicRHI::RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data)
{
	UE_LOG(LogRHI, Fatal,TEXT("OpenGL Render path does not support multiple Viewports!"));
}

void FOpenGLDynamicRHI::RHIExecuteCommandList(FRHICommandList*)
{
	check(0);
}

void FOpenGLDynamicRHI::RHIEnableDepthBoundsTest(bool bEnable)
{
	if (FOpenGL::SupportsDepthBoundsTest())
	{
		if(bEnable)
		{
			glEnable(GL_DEPTH_BOUNDS_TEST_EXT);
		}
		else
		{
			glDisable(GL_DEPTH_BOUNDS_TEST_EXT);
		}
	}
}

void FOpenGLDynamicRHI::RHISetDepthBounds(float MinDepth, float MaxDepth)
{
	if (FOpenGL::SupportsDepthBoundsTest())
	{
		FOpenGL::DepthBounds(MinDepth, MaxDepth);
	}
}

void FOpenGLDynamicRHI::RHISubmitCommandsHint()
{
	FOpenGL::Flush();
}

IRHICommandContext* FOpenGLDynamicRHI::RHIGetDefaultContext()
{
	return this;
}

IRHICommandContextContainer* FOpenGLDynamicRHI::RHIGetCommandContextContainer(int32 Index, int32 Num)
{
	return nullptr;
}

void FOpenGLDynamicRHI::RHIInvalidateCachedState()
{
	RenderingContextState = FOpenGLContextState();
	SharedContextState = FOpenGLContextState();

	RenderingContextState.InitializeResources(FOpenGL::GetMaxCombinedTextureImageUnits(), FOpenGL::GetMaxCombinedUAVUnits());
	SharedContextState.InitializeResources(FOpenGL::GetMaxCombinedTextureImageUnits(), FOpenGL::GetMaxCombinedUAVUnits());
}

void FOpenGLDynamicRHI::RHICopyToStagingBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIStagingBuffer* DestinationStagingBufferRHI, uint32 InOffset, uint32 InNumBytes)
{
	VERIFY_GL_SCOPE();
	FOpenGLVertexBuffer* SourceBuffer = ResourceCast(SourceBufferRHI);
	FOpenGLStagingBuffer* DestinationBuffer = ResourceCast(DestinationStagingBufferRHI);

	check(DestinationBuffer->ShadowBuffer != 0);
	glBindBuffer(GL_COPY_WRITE_BUFFER, DestinationBuffer->ShadowBuffer);
	if (DestinationBuffer->ShadowSize < InNumBytes)
	{
		// Orphan the existing buffer.
		glBufferData(GL_COPY_WRITE_BUFFER, InNumBytes, NULL, GL_STREAM_READ);
		DestinationBuffer->ShadowSize = InNumBytes;
	}

	glBindBuffer(GL_COPY_READ_BUFFER, SourceBuffer->Resource);
	FOpenGL::CopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, InOffset, 0, InNumBytes);
	glBindBuffer(GL_COPY_READ_BUFFER, 0);
	glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

void FOpenGLDynamicRHI::RHIWriteGPUFence(FRHIGPUFence* FenceRHI)
{
	check(FenceRHI);
	FOpenGLGPUFence* CopyFence = ResourceCast(FenceRHI);
	CopyFence->WriteInternal();
}


#if PLATFORM_USES_FIXED_RHI_CLASS
#define INTERNAL_DECORATOR(Method) ((FOpenGLDynamicRHI&)CmdList.GetContext()).FOpenGLDynamicRHI::Method
#include "RHICommandListCommandExecutes.inl"
#endif

