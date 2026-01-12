//
// Created by Noah on 11/23/2024.
//

#ifndef VULKANHELPERS_H
#define VULKANHELPERS_H

#include <cglm/types.h>
#include <engine/assets/ModelLoader.h>
#include <engine/assets/ShaderLoader.h>
#include <engine/assets/TextureLoader.h>
#include <engine/structs/Camera.h>
#include <engine/structs/Color.h>
#include <engine/structs/List.h>
#include <engine/structs/Map.h>
#include <engine/structs/Viewmodel.h>
#include <engine/subsystem/Logging.h>
#include <joltc/Math/Vector3.h>
#include <luna/lunaTypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

#pragma region macros
#ifdef JPH_DEBUG_RENDERER
#define MAX_DEBUG_DRAW_VERTICES_INIT 1024
#endif

#define VulkanLogError(...) LogInternal("VULKAN", 31, true, __VA_ARGS__)
// TODO Use LogInternal
#define VulkanTestInternal(function, returnValue, ...) \
	{ \
		const VkResult result = function; \
		if (result != VK_SUCCESS) \
		{ \
			LogInternal("VULKAN", 31, false, __VA_ARGS__); \
			LogInternal(NULL, 0, true, "Error code: %d\n", result); \
			if (result == VK_ERROR_DEVICE_LOST) \
			{ \
				LogInfo("See https://starflight.dev/media/VK_ERROR_DEVICE_LOST.webp for more information\n"); \
			} \
			return returnValue; \
		} \
	}
#define VulkanTestReturnResult(function, ...) VulkanTestInternal(function, result, __VA_ARGS__)
#define VulkanTest(function, ...) VulkanTestInternal(function, false, __VA_ARGS__)
#define VulkanTestResizeSwapchain(function, ...) \
	{ \
		const VkResult resizeCheckResult = function; \
		if (resizeCheckResult != VK_SUCCESS) \
		{ \
			if (resizeCheckResult == VK_ERROR_OUT_OF_DATE_KHR || resizeCheckResult == VK_SUBOPTIMAL_KHR) \
			{ \
				const LunaRenderPassResizeInfo renderPassResizeInfo = { \
					.renderPass = renderPass, \
					.width = LUNA_RENDER_PASS_WIDTH_SWAPCHAIN_WIDTH, \
					.height = LUNA_RENDER_PASS_HEIGHT_SWAPCHAIN_HEIGHT, \
				}; \
				VulkanTestReturnResult(lunaResizeSwapchain(1, &renderPassResizeInfo, NULL, &swapChainExtent), \
									   "Failed to resize swapchain!"); \
				if (UnlockLodThreadMutex() != 0) \
				{ \
					LogError("Failed to unlock LOD thread mutex with error: %s", SDL_GetError()); \
					return VK_ERROR_UNKNOWN; \
				} \
				return resizeCheckResult; \
			} \
			VulkanTestReturnResult(resizeCheckResult, __VA_ARGS__); \
		} \
	}
#pragma endregion macros

#pragma region typedefs
enum VendorIDs
{
	AMD = 0x1002,
	APPLE = 0x106B,
	ARM = 0x13B5,
	IMG_TEC = 0x1010,
	INTEL = 0x8086,
	MESA = VK_VENDOR_ID_MESA,
	MICROSOFT = 0x1414,
	NVIDIA = 0x10DE,
	QUALCOMM = 0x5143,
};

enum PendingTasksBitFlags
{
	PENDING_TASK_UI_BUFFERS_RESIZE_BIT = 1 << 0,
};

typedef struct UiVertex
{
	float x;
	float y;

	float u;
	float v;

	float r;
	float g;
	float b;
	float a;

	uint32_t textureIndex;
} UiVertex;

typedef struct DebugDrawVertex
{
	Vector3 position;
	Color color;
} DebugDrawVertex;

typedef struct UniformBuffers
{
	LunaBuffer transformMatrix;
	LunaBuffer lighting;
	LunaBuffer fog;
} UniformBuffers;

typedef struct UiBuffer
{
	LunaBuffer vertexBuffer;
	LunaBuffer indexBuffer;
	uint32_t allocatedQuads;
	uint32_t freeQuads;
	UiVertex *vertexData;
	uint32_t *indexData;
} UiBuffer;

/// Contains all the information needed to keep track of the required buffers for the map models
typedef struct MapBuffer
{
	/// A buffer containing per-vertex data
	LunaBuffer vertices;
	/// A buffer containing data that only needs to exist once per-material
	LunaBuffer perMaterial;
	/// A buffer containing the index data to use along-side the per-vertex data
	LunaBuffer indices;
	/// A buffer containing the VkDrawIndexedIndirectCommand structures required for the indirect draw call
	LunaBuffer drawInfo;
} MapBuffer;

#ifdef JPH_DEBUG_RENDERER
// TODO: Clean up both this and the whole system
typedef struct DebugDrawBuffer
{
	struct
	{
		LunaBuffer buffer;
		VkDeviceSize bytesUsed;
		VkDeviceSize allocatedSize;
		void *data;
	} vertices;
	uint32_t vertexCount;
	bool shouldResize;
} DebugDrawBuffer;
#endif

typedef struct Buffers
{
	UiBuffer ui;
	MapBuffer map;
	UniformBuffers uniforms;
#ifdef JPH_DEBUG_RENDERER
	DebugDrawBuffer debugDrawLines;
	DebugDrawBuffer debugDrawTriangles;
#endif
} Buffers;

typedef struct Pipelines
{
	LunaGraphicsPipeline ui;
	LunaGraphicsPipeline map;
#ifdef JPH_DEBUG_RENDERER
	LunaGraphicsPipeline debugDrawLines;
	LunaGraphicsPipeline debugDrawTriangles;
#endif
} Pipelines;

typedef struct TextureSamplers
{
	LunaSampler linearRepeatAnisotropy;
	LunaSampler nearestRepeatAnisotropy;
	LunaSampler linearNoRepeatAnisotropy;
	LunaSampler nearestNoRepeatAnisotropy;
	LunaSampler linearRepeatNoAnisotropy;
	LunaSampler nearestRepeatNoAnisotropy;
	LunaSampler linearNoRepeatNoAnisotropy;
	LunaSampler nearestNoRepeatNoAnisotropy;
} TextureSamplers;

typedef struct DescriptorSetLayouts
{
	LunaDescriptorSetLayout transform;
	LunaDescriptorSetLayout all;
	LunaDescriptorSetLayout globalLighting;
	LunaDescriptorSetLayout fog;
} DescriptorSetLayouts;
#pragma endregion typedefs

#pragma region variables
// TODO: Make sure these are all needed and are all as they should be

extern bool minimized;
extern VkExtent2D swapChainExtent;
extern VkSampleCountFlagBits msaaSamples;
extern LunaRenderPass renderPass;
extern uint32_t imageAssetIdToIndexMap[MAX_TEXTURES];
extern TextureSamplers textureSamplers;
extern LockingList textures;
extern LunaDescriptorSetLayout descriptorSetLayout;
extern LunaDescriptorSet descriptorSet;

extern Buffers buffers;
extern Pipelines pipelines;
extern uint32_t pendingTasks; // Bits set with PendingTasksBitFlags
#pragma endregion variables

VkResult CreateShaderModule(const char *path, ShaderType shaderType, LunaShaderModule *shaderModule);

uint32_t TextureIndex(const char *texture);

uint32_t ImageIndex(const Image *image);

VkResult UpdateTransformMatrix(const Camera *camera);

void UpdateViewModelMatrix(const Viewmodel *viewmodel);

void EnsureSpaceForUiElements(size_t quadCount);

void DrawRectInternal(float ndcStartX,
					  float ndcStartY,
					  float ndcEndX,
					  float ndcEndY,
					  float startU,
					  float startV,
					  float endU,
					  float endV,
					  const Color *color,
					  uint32_t textureIndex);

void DrawQuadInternal(const mat4 vertices_posXY_uvZW, const Color *color, uint32_t textureIndex);

#endif //VULKANHELPERS_H
