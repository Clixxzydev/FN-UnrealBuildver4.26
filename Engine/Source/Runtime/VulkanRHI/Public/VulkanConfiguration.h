// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VulkanConfiguration.h: Control compilation of the runtime RHI.
=============================================================================*/

// Compiled with 1.1.82.1

#pragma once

#include "VulkanCommon.h"

// API version we want to target.
#ifndef UE_VK_API_VERSION
	#define UE_VK_API_VERSION									VK_API_VERSION_1_0
#endif

// by default, we enable debugging in Development builds, unless the platform says not to
#ifndef VULKAN_SHOULD_DEBUG_IN_DEVELOPMENT
	#define VULKAN_SHOULD_DEBUG_IN_DEVELOPMENT 1
#endif

#define VULKAN_HAS_DEBUGGING_ENABLED							(UE_BUILD_DEBUG || (UE_BUILD_DEVELOPMENT && VULKAN_SHOULD_DEBUG_IN_DEVELOPMENT))

// Enables the VK_LAYER_LUNARG_api_dump layer and the report VK_DEBUG_REPORT_INFORMATION_BIT_EXT flag
#define VULKAN_ENABLE_API_DUMP									0

#ifndef VULKAN_SHOULD_ENABLE_DRAW_MARKERS
	#define VULKAN_SHOULD_ENABLE_DRAW_MARKERS					0
#endif

// Enables logging wrappers per Vulkan call
#ifndef VULKAN_ENABLE_DUMP_LAYER
	#define VULKAN_ENABLE_DUMP_LAYER							0
#endif

#define VULKAN_ENABLE_DRAW_MARKERS								VULKAN_SHOULD_ENABLE_DRAW_MARKERS

#ifndef VULKAN_ENABLE_IMAGE_TRACKING_LAYER
	#define VULKAN_ENABLE_IMAGE_TRACKING_LAYER					0
#endif

#ifndef VULKAN_ENABLE_BUFFER_TRACKING_LAYER
	#define VULKAN_ENABLE_BUFFER_TRACKING_LAYER					0
#endif

#define VULKAN_ENABLE_TRACKING_LAYER							(VULKAN_ENABLE_BUFFER_TRACKING_LAYER || VULKAN_ENABLE_IMAGE_TRACKING_LAYER)
#define VULKAN_ENABLE_WRAP_LAYER								(VULKAN_ENABLE_DUMP_LAYER || VULKAN_ENABLE_TRACKING_LAYER)

#define VULKAN_HASH_POOLS_WITH_TYPES_USAGE_ID					1

#define VULKAN_SINGLE_ALLOCATION_PER_RESOURCE					0

#ifndef VULKAN_FREEPAGE_FOR_TYPE
	#define VULKAN_FREEPAGE_FOR_TYPE							0
#endif

#ifndef VULKAN_USE_NEW_QUERIES
	#define VULKAN_USE_NEW_QUERIES								1
#endif

#ifndef VULKAN_SHOULD_USE_LLM
	#define VULKAN_SHOULD_USE_LLM								0
#endif

#ifndef VULKAN_USE_LLM
	#define VULKAN_USE_LLM										((ENABLE_LOW_LEVEL_MEM_TRACKER) && VULKAN_SHOULD_USE_LLM)
#endif

#ifndef VULKAN_CUSTOM_MEMORY_MANAGER_ENABLED
	#define VULKAN_CUSTOM_MEMORY_MANAGER_ENABLED				VULKAN_USE_LLM
#endif

#ifndef VULKAN_SHOULD_USE_COMMANDWRAPPERS
	#define VULKAN_SHOULD_USE_COMMANDWRAPPERS					VULKAN_ENABLE_WRAP_LAYER
#endif

#ifndef VULKAN_COMMANDWRAPPERS_ENABLE							
	#define VULKAN_COMMANDWRAPPERS_ENABLE						VULKAN_SHOULD_USE_COMMANDWRAPPERS
#endif

#ifndef VULKAN_USE_IMAGE_ACQUIRE_FENCES
	#define VULKAN_USE_IMAGE_ACQUIRE_FENCES						1
#endif

#define VULKAN_ENABLE_AGGRESSIVE_STATS							0

#define VULKAN_REUSE_FENCES										1

#ifndef VULKAN_QUERY_CALLSTACK
	#define VULKAN_QUERY_CALLSTACK								0
#endif

#ifndef VULKAN_ENABLE_DESKTOP_HMD_SUPPORT
	#define VULKAN_ENABLE_DESKTOP_HMD_SUPPORT					0
#endif

#ifndef VULKAN_SIGNAL_UNIMPLEMENTED
	#define VULKAN_SIGNAL_UNIMPLEMENTED()
#endif

#ifndef VULKAN_ENABLE_LRU_CACHE
	#define VULKAN_ENABLE_LRU_CACHE								0
#endif

#ifdef VK_KHR_maintenance1
	#define VULKAN_SUPPORTS_MAINTENANCE_LAYER1					1
#else
	#define VULKAN_SUPPORTS_MAINTENANCE_LAYER1					0
#endif

#ifdef VK_KHR_maintenance2
	#define VULKAN_SUPPORTS_MAINTENANCE_LAYER2					1
#else
	#define VULKAN_SUPPORTS_MAINTENANCE_LAYER2					0
#endif

#ifdef VK_EXT_validation_cache
	#define VULKAN_SUPPORTS_VALIDATION_CACHE					1
#else
	#define VULKAN_SUPPORTS_VALIDATION_CACHE					0
#endif

#ifdef VK_EXT_validation_features
	#define VULKAN_HAS_VALIDATION_FEATURES						1
#else
	#define VULKAN_HAS_VALIDATION_FEATURES						0
#endif

#ifndef VULKAN_SUPPORTS_DEDICATED_ALLOCATION
	#ifdef VK_KHR_dedicated_allocation
		// Disable this for now as it is causing a large memory leak
		#define VULKAN_SUPPORTS_DEDICATED_ALLOCATION			0
	#else
		#define VULKAN_SUPPORTS_DEDICATED_ALLOCATION			0
	#endif
#endif

#ifndef VULKAN_SUPPORTS_GOOGLE_DISPLAY_TIMING
	#define VULKAN_SUPPORTS_GOOGLE_DISPLAY_TIMING				0
#endif

#ifndef VULKAN_USE_CREATE_ANDROID_SURFACE
	#define VULKAN_USE_CREATE_ANDROID_SURFACE					0
#endif

#ifndef VULKAN_USE_CREATE_WIN32_SURFACE
	#define VULKAN_USE_CREATE_WIN32_SURFACE						0
#endif

#ifndef VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY
	#define VULKAN_USE_REAL_RENDERPASS_COMPATIBILITY			1
#endif

#ifndef VULKAN_USE_DIFFERENT_POOL_CMDBUFFERS
	#define VULKAN_USE_DIFFERENT_POOL_CMDBUFFERS				1
#endif

#ifndef VULKAN_DELETE_STALE_CMDBUFFERS
	#define VULKAN_DELETE_STALE_CMDBUFFERS						1
#endif

#ifndef VULKAN_SUPPORTS_COLOR_CONVERSIONS
	#define VULKAN_SUPPORTS_COLOR_CONVERSIONS					0
#endif

#ifndef VULKAN_SUPPORTS_AMD_BUFFER_MARKER
	#define VULKAN_SUPPORTS_AMD_BUFFER_MARKER					0
#endif

#ifndef VULKAN_SUPPORTS_NV_DIAGNOSTIC_CHECKPOINT
	#define VULKAN_SUPPORTS_NV_DIAGNOSTIC_CHECKPOINT			0
#endif

#ifndef VULKAN_SUPPORTS_GPU_CRASH_DUMPS
	#define VULKAN_SUPPORTS_GPU_CRASH_DUMPS						(VULKAN_SUPPORTS_AMD_BUFFER_MARKER || VULKAN_SUPPORTS_NV_DIAGNOSTIC_CHECKPOINT)
#endif

#ifndef VULKAN_SUPPORTS_DEBUG_UTILS
	#ifdef VK_EXT_debug_utils
		#define VULKAN_SUPPORTS_DEBUG_UTILS						1
	#else
		#define VULKAN_SUPPORTS_DEBUG_UTILS						0
	#endif
#endif

#ifndef VULKAN_SUPPORTS_MEMORY_PRIORITY
	#ifdef VK_EXT_memory_priority
		#define VULKAN_SUPPORTS_MEMORY_PRIORITY					1
	#else
		#define VULKAN_SUPPORTS_MEMORY_PRIORITY					0
	#endif
#endif

#ifndef VULKAN_SUPPORTS_PHYSICAL_DEVICE_PROPERTIES2
	#ifdef VK_KHR_get_physical_device_properties2
		#define VULKAN_SUPPORTS_PHYSICAL_DEVICE_PROPERTIES2		1
	#else
		#define VULKAN_SUPPORTS_PHYSICAL_DEVICE_PROPERTIES2		0
	#endif
#endif

#ifndef VULKAN_SUPPORTS_EXTERNAL_MEMORY
	#ifdef VK_KHR_external_memory_capabilities
		#define VULKAN_SUPPORTS_EXTERNAL_MEMORY					(VULKAN_SUPPORTS_PHYSICAL_DEVICE_PROPERTIES2)	// Requirement
	#else
		#define VULKAN_SUPPORTS_EXTERNAL_MEMORY					0
	#endif
#endif

#ifndef VULKAN_SUPPORTS_DRIVER_PROPERTIES
	#ifdef VK_KHR_driver_properties
		#define VULKAN_SUPPORTS_DRIVER_PROPERTIES				1
	#else
		#define VULKAN_SUPPORTS_DRIVER_PROPERTIES				0
	#endif
#endif

/* VK_QCOM_render_pass_transform */
#ifndef VK_QCOM_render_pass_transform
#define VK_QCOM_render_pass_transform 1
#define VK_QCOM_RENDER_PASS_TRANSFORM_SPEC_VERSION 1
#define VK_QCOM_RENDER_PASS_TRANSFORM_EXTENSION_NAME "VK_QCOM_render_pass_transform"
#define VK_STRUCTURE_TYPE_RENDER_PASS_TRANSFORM_BEGIN_INFO_QCOM 1000282000
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDER_PASS_TRANSFORM_INFO_QCOM 1000282001
#define VK_RENDER_PASS_CREATE_TRANSFORM_BIT_QCOM 0x00000002
typedef struct VkRenderPassTransformBeginInfoQCOM {
	VkStructureType sType;
	void* pNext;
	VkSurfaceTransformFlagBitsKHR transform;
} VkRenderPassTransformBeginInfoQCOM;

typedef struct VkCommandBufferInheritanceRenderPassTransformInfoQCOM {
	VkStructureType sType;
	void* pNext;
	VkSurfaceTransformFlagBitsKHR transform;
	VkRect2D renderArea;
} VkCommandBufferInheritanceRenderPassTransformInfoQCOM;

#endif //VK_QCOM_render_pass_transform

#ifndef VULKAN_SUPPORTS_FULLSCREEN_EXCLUSIVE
	#ifdef VK_EXT_full_screen_exclusive
		#define VULKAN_SUPPORTS_FULLSCREEN_EXCLUSIVE			1
	#else
		#define VULKAN_SUPPORTS_FULLSCREEN_EXCLUSIVE			0
	#endif
#endif

#ifndef VULKAN_SUPPORTS_ASTC_DECODE_MODE
	#ifdef VK_EXT_astc_decode_mode
		#define VULKAN_SUPPORTS_ASTC_DECODE_MODE				(VULKAN_SUPPORTS_PHYSICAL_DEVICE_PROPERTIES2)	// Requirement
	#else
		#define VULKAN_SUPPORTS_ASTC_DECODE_MODE				0
	#endif
#endif

#ifndef VULKAN_OBJECT_TRACKING 
#define VULKAN_OBJECT_TRACKING 0 //Track objects created and memory used. use r.vulkan.dumpmemory to dump to console
#endif



VULKANRHI_API DECLARE_LOG_CATEGORY_EXTERN(LogVulkanRHI, Log, All);

#if VULKAN_CUSTOM_MEMORY_MANAGER_ENABLED
	#define VULKAN_CPU_ALLOCATOR								VulkanRHI::GetMemoryAllocator(nullptr)
#else
	#define VULKAN_CPU_ALLOCATOR								nullptr
#endif

#ifndef VULKAN_PURGE_SHADER_MODULES
	#define VULKAN_PURGE_SHADER_MODULES							0
#endif

namespace VulkanRHI
{
	static FORCEINLINE const VkAllocationCallbacks* GetMemoryAllocator(const VkAllocationCallbacks* Allocator)
	{
#if VULKAN_CUSTOM_MEMORY_MANAGER_ENABLED
		extern VkAllocationCallbacks GAllocationCallbacks;
		return Allocator ? Allocator : &GAllocationCallbacks;
#else
		return Allocator;
#endif
	}
}
