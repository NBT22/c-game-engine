//
// Created by Noah on 7/5/2024.
//

#include <assert.h>
#include <cglm/types.h>
#include <engine/assets/TextureLoader.h>
#include <engine/graphics/Drawing.h>
#include <engine/graphics/vulkan/Vulkan.h>
#include <engine/graphics/vulkan/VulkanHelpers.h>
#include <engine/graphics/vulkan/VulkanInternal.h>
#include <engine/structs/Camera.h>
#include <engine/structs/Color.h>
#include <engine/structs/List.h>
#include <engine/structs/Map.h>
#include <engine/structs/Viewmodel.h>
#include <engine/subsystem/Logging.h>
#include <engine/subsystem/threads/LodThread.h>
#include <joltc/Math/Vector3.h>
#include <luna/lunaBuffer.h>
#include <luna/lunaDevice.h>
#include <luna/lunaDrawing.h>
#include <luna/lunaInstance.h>
#include <luna/lunaTypes.h>
#include <math.h>
#include <SDL_error.h>
#include <SDL_video.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

#include "engine/assets/AssetReader.h"
#include "engine/graphics/vulkan/VulkanResources.h"
#ifdef JPH_DEBUG_RENDERER
#include <engine/debug/JoltDebugRenderer.h>
#include <engine/subsystem/Error.h>
#endif

// TODO: Can the concept of frames in flight be removed entirely in favor of simply letting Luna handle it?
//  I've started this process, so if it cannot it needs to be readded in several places

static const Map *loadedMap;
static size_t skyModelIndexCount;

static inline VkResult LoadSky(const ModelDefinition *model)
{
	if (model->skinCount > 1)
	{
		LogWarning("Discarding %d extra skins from sky model!\n", model->skinCount - 1);
	}
	if (model->materialCount > 1)
	{
		LogWarning("Discarding %d extra materials from sky model!\n", model->materialCount - 1);
	}
	if (model->materials->shader != SHADER_SKY)
	{
		LogWarning("Ignoring incorrect material shader type on sky model!\n");
	}
	if (model->lodCount > 1)
	{
		LogWarning("Discarding %d extra lods from sky model!\n", model->lodCount - 1);
	}

	ModelLod *lod = model->lods[0];

	SkyVertex vertices[lod->vertexCount];
	for (size_t i = 0; i < lod->vertexCount; i++)
	{
		memcpy(vertices + i, lod->vertexData + i, sizeof(SkyVertex));
	}
	assert(lunaGetBufferSize(buffers.sky.vertices) == sizeof(SkyVertex) * lod->vertexCount);
	const LunaBufferWriteInfo vertexBufferWriteInfo = {
		.bytes = sizeof(SkyVertex) * lod->vertexCount,
		.data = vertices,
		.stageFlags = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
	};
	VulkanTestReturnResult(lunaWriteDataToBuffer(buffers.sky.vertices, &vertexBufferWriteInfo),
						   "Failed to write sky model vertex data to buffer!");

	skyModelIndexCount = lod->indexCount[0];
	assert(lunaGetBufferSize(buffers.sky.indices) == sizeof(uint32_t) * skyModelIndexCount);
	const LunaBufferWriteInfo indexBufferWriteInfo = {
		.bytes = sizeof(uint32_t) * skyModelIndexCount,
		.data = lod->indexData[0],
		.stageFlags = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
	};
	VulkanTestReturnResult(lunaWriteDataToBuffer(buffers.sky.indices, &indexBufferWriteInfo),
						   "Failed to write sky model index data to buffer!");

	return VK_SUCCESS;
}

static inline VkResult LoadMapModelShader(const Map *map,
										  const ModelShader modelShader,
										  ModelBuffer *buffer,
										  const size_t vertexSize)
{
	size_t totalMaterialCount = 0;
	size_t totalVertexCount = 0;
	size_t totalIndexCount = 0;
	for (size_t i = 0; i < map->modelCount; i++)
	{
		const MapModel *model = map->models + i;
		if (model->material->shader == modelShader)
		{
			totalVertexCount += model->vertexCount;
			totalIndexCount += model->indexCount;
			totalMaterialCount++;
		}
	}
	const size_t vertexBufferSize = totalVertexCount * vertexSize;
	VulkanTestReturnResult(lunaResizeBuffer(&buffer->vertices, vertexBufferSize), "Failed to resize vertex buffer!");
	const size_t perMaterialBufferSize = totalMaterialCount * sizeof(uint32_t);
	VulkanTestReturnResult(lunaResizeBuffer(&buffer->perMaterial, perMaterialBufferSize),
						   "Failed to resize per material data buffer!");
	const size_t indexBufferSize = totalIndexCount * sizeof(uint32_t);
	VulkanTestReturnResult(lunaResizeBuffer(&buffer->indices, indexBufferSize), "Failed to resize index buffer!");
	const size_t drawInfoBufferSize = totalMaterialCount * sizeof(VkDrawIndexedIndirectCommand);
	VulkanTestReturnResult(lunaResizeBuffer(&buffer->drawInfo, drawInfoBufferSize),
						   "Failed to resize draw info buffer!");

	char vertices[totalVertexCount][vertexSize];
	VkDeviceSize vertexCount = 0;
	uint32_t textureIndices[totalMaterialCount];
	uint32_t indices[totalIndexCount];
	VkDeviceSize indexCount = 0;
	VkDrawIndexedIndirectCommand drawInfo[totalMaterialCount];
	size_t materialIndex = 0;
	for (size_t i = 0; i < map->modelCount; i++)
	{
		const MapModel *model = map->models + i;
		if (model->material->shader != modelShader)
		{
			continue;
		}
		textureIndices[materialIndex] = TextureIndex(model->material->texture);
		memcpy(indices + indexCount, model->indices, model->indexCount * sizeof(uint32_t));
		drawInfo[materialIndex].indexCount = model->indexCount;
		drawInfo[materialIndex].instanceCount = 1;
		drawInfo[materialIndex].firstIndex = indexCount;
		drawInfo[materialIndex].vertexOffset = (int32_t)vertexCount;
		drawInfo[materialIndex].firstInstance = materialIndex;
		for (size_t j = 0; j < model->vertexCount; j++, vertexCount++)
		{
			memcpy(vertices + vertexCount, model->vertices + j, vertexSize);
		}

		indexCount += model->indexCount;
		materialIndex++;
	}

	const LunaBufferWriteInfo vertexBufferWriteInfo = {
		.bytes = vertexBufferSize,
		.data = vertices,
		.stageFlags = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
	};
	VulkanTestReturnResult(lunaWriteDataToBuffer(buffer->vertices, &vertexBufferWriteInfo),
						   "Failed to write data to vertex buffer!");
	const LunaBufferWriteInfo perMaterialDataBufferWriteInfo = {
		.bytes = perMaterialBufferSize,
		.data = textureIndices,
		.stageFlags = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
	};
	VulkanTestReturnResult(lunaWriteDataToBuffer(buffer->perMaterial, &perMaterialDataBufferWriteInfo),
						   "Failed to write data to per-material data buffer!");
	const LunaBufferWriteInfo indexBufferWriteInfo = {
		.bytes = indexBufferSize,
		.data = indices,
		.stageFlags = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
	};
	VulkanTestReturnResult(lunaWriteDataToBuffer(buffer->indices, &indexBufferWriteInfo),
						   "Failed to write data to index buffer!");
	const LunaBufferWriteInfo drawInfoBufferWriteInfo = {
		.bytes = drawInfoBufferSize,
		.data = drawInfo,
		.stageFlags = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
	};
	VulkanTestReturnResult(lunaWriteDataToBuffer(buffer->drawInfo, &drawInfoBufferWriteInfo),
						   "Failed to write data to draw info buffer!");

	return VK_SUCCESS;
}

/**
 * Loads a map into VRAM. This function is responsible for
 *  1. Ensuring that the target buffers are large enough to hold the data, and resizing as needed
 *  2. Copying the data out of the Map struct and into VRAM, using temporary CPU-side buffers in order to combine
 *      all map models into one large vertex buffer and one large index buffer
 *  3. Copying any data that is only required once per material into the @c perMaterialData buffer
 *  4. Generating the @c VkDrawIndexedIndirectCommand structures that are stored in the @c drawInfo buffer
 *  5. Setting the initial state for any relevant descriptor sets or push constants
 * @todo This function should set the initial state for any descriptor sets and push constants
 * @param map The map to load
 * @return @c VK_SUCCESS if the map was successfully loaded, or a meaningful result code otherwise
 */
static inline VkResult LoadMap(const Map *map)
{
	VulkanTestReturnResult(LoadMapModelShader(map, SHADER_SHADED, &buffers.shadedMap, sizeof(MapVertex)),
						   "Failed to load shaded map!");
	VulkanTestReturnResult(LoadMapModelShader(map,
											  SHADER_UNSHADED,
											  &buffers.unshadedMap,
											  sizeof(MapVertex) - sizeof(Vector3)),
						   "Failed to load shaded map!");

	skyTextureIndex = TextureIndex(map->skyTexture);
	loadedMap = map;

	return VK_SUCCESS;
}

bool VK_Init(SDL_Window *window)
{
	LogDebug("Initializing Vulkan renderer...\n");
	// clang-format off
	if (CreateInstance(window) && CreateSurface() && CreateLogicalDevice() && CreateSwapchain() && CreateRenderPass() &&
		CreateDescriptorSetLayouts() && CreateGraphicsPipelines() && CreateTextureSamplers() && CreateBuffers() &&
		CreateDescriptorSet())
	{
		// clang-format on

		VkPhysicalDeviceProperties physicalDeviceProperties;
		lunaGetPhysicalDeviceProperties(&physicalDeviceProperties);
		char vendor[32] = {};
		switch (physicalDeviceProperties.vendorID)
		{
			case AMD:
				strncpy(vendor, "AMD", 32);
				break;
			case APPLE:
				strncpy(vendor, "Apple", 32);
				break;
			case ARM:
				strncpy(vendor, "ARM", 32);
				break;
			case IMG_TEC:
				strncpy(vendor, "ImgTec", 32);
				break;
			case INTEL:
				strncpy(vendor, "Intel", 32);
				break;
			case MESA:
				strncpy(vendor, "Mesa", 32);
				break;
			case MICROSOFT:
				strncpy(vendor, "Microsoft", 32);
				break;
			case NVIDIA:
				strncpy(vendor, "NVIDIA", 32);
				break;
			case QUALCOMM:
				strncpy(vendor, "Qualcomm", 32);
				break;
			default:
				strncpy(vendor, "Unknown", 32);
				break;
		}
		LogInfo("Vulkan Initialized\n");
		LogInfo("Vulkan Vendor: %s\n", vendor);
		LogInfo("Vulkan Device: %s\n", physicalDeviceProperties.deviceName);
		LogInfo("Vulkan Version: %u.%u.%u\n",
				VK_API_VERSION_MAJOR(physicalDeviceProperties.apiVersion),
				VK_API_VERSION_MINOR(physicalDeviceProperties.apiVersion),
				VK_API_VERSION_PATCH(physicalDeviceProperties.apiVersion));

		VulkanTest(LoadSky(LoadModel(MODEL("sky"))), "Failed to load sky model!");

		return true;
	}

	if (!VK_Cleanup())
	{
		VulkanLogError("Cleanup failed!");
	}

	return false;
}

bool VK_UpdateActors(const LockingList *actors, const bool shouldReloadActors)
{
	// TODO: Implement this

	(void)actors;
	(void)shouldReloadActors;
	return true;
}

VkResult VK_FrameStart()
{
	if (minimized)
	{
		return VK_NOT_READY;
	}

	if (LockLodThreadMutex() != 0)
	{
		LogError("Failed to lock LOD thread mutex with error: %s", SDL_GetError());
		return VK_ERROR_UNKNOWN;
	}

	VulkanTestResizeSwapchain(lunaBeginFrame(false), "Failed to begin frame!");
	const LunaRenderPassBeginInfo beginInfo = {
		.renderArea.extent = swapChainExtent,
		.depthAttachmentClearValue.depthStencil.depth = 1,
	};
	VulkanTest(lunaBeginRenderPass(renderPass, &beginInfo), "Failed to begin render pass!");

	if (UnlockLodThreadMutex() != 0)
	{
		LogError("Failed to unlock LOD thread mutex with error: %s", SDL_GetError());
		return VK_ERROR_UNKNOWN;
	}

	buffers.ui.freeQuads = buffers.ui.allocatedQuads;
#ifdef JPH_DEBUG_RENDERER
	buffers.debugDrawLines.vertexCount = 0;
	buffers.debugDrawLines.vertices.bytesUsed = 0;
	buffers.debugDrawTriangles.vertexCount = 0;
	buffers.debugDrawTriangles.vertices.bytesUsed = 0;
#endif

	return VK_SUCCESS;
}

VkResult VK_RenderMap(const Map *map, const Camera *camera)
{
	if (map != loadedMap)
	{
		VulkanTestReturnResult(LoadMap(map), "Failed to load map!");
	}

	float lighting[6]; // r, g, b, x, y, z
	lighting[0] = map->lightColor.r;
	lighting[1] = map->lightColor.g;
	lighting[2] = map->lightColor.b;
	lighting[3] = cosf(map->lightPitch) * sinf(map->lightYaw);
	lighting[4] = sinf(map->lightPitch);
	lighting[5] = -cosf(map->lightPitch) * cosf(map->lightYaw);
	const LunaBufferWriteInfo lightingBufferWriteInfo = {
		.bytes = sizeof(lighting),
		.data = lighting,
		.stageFlags = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
	};
	VulkanTestReturnResult(lunaWriteDataToBuffer(buffers.uniforms.lighting, &lightingBufferWriteInfo),
						   "Failed to update lighting data!");

	float fog[6]; // r, g, b, a, start, end
	fog[0] = map->fogColor.r;
	fog[1] = map->fogColor.g;
	fog[2] = map->fogColor.b;
	fog[3] = map->fogColor.a;
	fog[4] = map->fogStart;
	fog[5] = map->fogEnd;
	const LunaBufferWriteInfo fogBufferWriteInfo = {
		.bytes = sizeof(fog),
		.data = fog,
		.stageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	};
	VulkanTestReturnResult(lunaWriteDataToBuffer(buffers.uniforms.fog, &fogBufferWriteInfo),
						   "Failed to update fog data!");

	VulkanTestReturnResult(UpdateCameraUniform(camera), "Failed to update transform matrix!");


	const VkViewport viewport = {
		.width = (float)swapChainExtent.width,
		.height = (float)swapChainExtent.height,
		.maxDepth = 1,
	};
	const LunaViewportBindInfo viewportBindInfo = {
		.viewportCount = 1,
		.viewports = &viewport,
	};
	const VkRect2D scissor = {
		.extent = swapChainExtent,
	};
	const LunaScissorBindInfo scissorBindInfo = {
		.scissorCount = 1,
		.scissors = &scissor,
	};
	const LunaDynamicStateBindInfo dynamicStateBindInfos[] = {
		{
			.dynamicStateType = VK_DYNAMIC_STATE_VIEWPORT,
			.bindInfo.viewportBindInfo = &viewportBindInfo,
		},
		{
			.dynamicStateType = VK_DYNAMIC_STATE_SCISSOR,
			.bindInfo.scissorBindInfo = &scissorBindInfo,
		},
	};
	const LunaGraphicsPipelineBindInfo pipelineBindInfo = {
		.descriptorSetBindInfo.descriptorSetCount = 1,
		.descriptorSetBindInfo.descriptorSets = &descriptorSet,
		.dynamicStateCount = sizeof(dynamicStateBindInfos) / sizeof(*dynamicStateBindInfos),
		.dynamicStates = dynamicStateBindInfos,
	};

	VulkanTestReturnResult(lunaPushConstants(pipelines.sky), "Failed to push constants for sky pipeline!");
	const LunaDrawIndexedInfo skyDrawInfo = {
		.pipeline = pipelines.sky,
		.pipelineBindInfo = &pipelineBindInfo,
		.indexCount = skyModelIndexCount,
		.instanceCount = 1,
	};
	VulkanTestReturnResult(lunaDrawBufferIndexed(buffers.sky.vertices,
												 buffers.sky.indices,
												 VK_INDEX_TYPE_UINT32,
												 &skyDrawInfo),
						   "Failed to draw sky!");

	lunaBindVertexBuffers((LunaBuffer[]){buffers.shadedMap.vertices, buffers.shadedMap.perMaterial}, 0, 2);
	lunaBindIndexBuffer(buffers.shadedMap.indices, VK_INDEX_TYPE_UINT32);
	const LunaDrawIndexedIndirectInfo shadedDrawInfo = {
		.pipeline = pipelines.shadedMap,
		.pipelineBindInfo = &pipelineBindInfo,
		.buffer = buffers.shadedMap.drawInfo,
		.drawCount = lunaGetBufferSize(buffers.shadedMap.drawInfo) / sizeof(VkDrawIndexedIndirectCommand),
	};
	VulkanTestReturnResult(lunaDrawIndexedIndirect(&shadedDrawInfo), "Failed to draw shaded map!");

	lunaBindVertexBuffers((LunaBuffer[]){buffers.unshadedMap.vertices, buffers.unshadedMap.perMaterial}, 0, 2);
	lunaBindIndexBuffer(buffers.unshadedMap.indices, VK_INDEX_TYPE_UINT32);
	const LunaDrawIndexedIndirectInfo unshadedDrawInfo = {
		.pipeline = pipelines.unshadedMap,
		.pipelineBindInfo = &pipelineBindInfo,
		.buffer = buffers.unshadedMap.drawInfo,
		.drawCount = lunaGetBufferSize(buffers.unshadedMap.drawInfo) / sizeof(VkDrawIndexedIndirectCommand),
	};
	VulkanTestReturnResult(lunaDrawIndexedIndirect(&unshadedDrawInfo), "Failed to draw unshaded map!");

	return VK_SUCCESS;
}

VkResult VK_FrameEnd()
{
	if ((pendingTasks & PENDING_TASK_UI_BUFFERS_RESIZE_BIT) == PENDING_TASK_UI_BUFFERS_RESIZE_BIT)
	{
		VulkanTestReturnResult(lunaGrowBuffer(&buffers.ui.vertexBuffer,
											  buffers.ui.allocatedQuads * 4 * sizeof(UiVertex)),
							   "Failed to recreate UI vertex buffer!");
		VulkanTestReturnResult(lunaGrowBuffer(&buffers.ui.indexBuffer,
											  buffers.ui.allocatedQuads * 6 * sizeof(uint32_t)),
							   "Failed to recreate UI index buffer!");

		pendingTasks ^= PENDING_TASK_UI_BUFFERS_RESIZE_BIT;
	}
	if (buffers.ui.freeQuads != buffers.ui.allocatedQuads)
	{
		// TODO: This write is the cause of the glitching (and crash) when pausing the game
		const LunaBufferWriteInfo vertexBufferWriteInfo = {
			.bytes = (buffers.ui.allocatedQuads - buffers.ui.freeQuads) * 4 * sizeof(UiVertex),
			.data = buffers.ui.vertexData,
			.stageFlags = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		};
		const LunaBufferWriteInfo indexBufferWriteInfo = {
			.bytes = (buffers.ui.allocatedQuads - buffers.ui.freeQuads) * 6 * sizeof(uint32_t),
			.data = buffers.ui.indexData,
			.stageFlags = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		};
		VulkanTestReturnResult(lunaWriteDataToBuffer(buffers.ui.vertexBuffer, &vertexBufferWriteInfo),
							   "Failed to write UI vertex buffer!");
		VulkanTestReturnResult(lunaWriteDataToBuffer(buffers.ui.indexBuffer, &indexBufferWriteInfo),
							   "Failed to write UI index buffer!");
	}

	if (LockLodThreadMutex() != 0)
	{
		LogError("Failed to lock LOD thread mutex with error: %s", SDL_GetError());
		return VK_ERROR_UNKNOWN;
	}

	if (buffers.ui.freeQuads != buffers.ui.allocatedQuads)
	{
		const VkViewport viewport = {
			.width = (float)swapChainExtent.width,
			.height = (float)swapChainExtent.height,
			.maxDepth = 1,
		};
		const LunaViewportBindInfo viewportBindInfo = {
			.viewportCount = 1,
			.viewports = &viewport,
		};
		const VkRect2D scissor = {
			.extent = swapChainExtent,
		};
		const LunaScissorBindInfo scissorBindInfo = {
			.scissorCount = 1,
			.scissors = &scissor,
		};
		const LunaDynamicStateBindInfo dynamicStateBindInfos[] = {
			{
				.dynamicStateType = VK_DYNAMIC_STATE_VIEWPORT,
				.bindInfo.viewportBindInfo = &viewportBindInfo,
			},
			{
				.dynamicStateType = VK_DYNAMIC_STATE_SCISSOR,
				.bindInfo.scissorBindInfo = &scissorBindInfo,
			},
		};
		const LunaGraphicsPipelineBindInfo pipelineBindInfo = {
			.descriptorSetBindInfo.descriptorSetCount = 1,
			.descriptorSetBindInfo.descriptorSets = &descriptorSet,
			.dynamicStateCount = sizeof(dynamicStateBindInfos) / sizeof(*dynamicStateBindInfos),
			.dynamicStates = dynamicStateBindInfos,
		};
		const LunaDrawIndexedInfo drawInfo = {
			.pipeline = pipelines.ui,
			.pipelineBindInfo = &pipelineBindInfo,
			.indexCount = (buffers.ui.allocatedQuads - buffers.ui.freeQuads) * 6,
			.instanceCount = 1,
		};
		VulkanTestReturnResult(lunaDrawBufferIndexed(buffers.ui.vertexBuffer,
													 buffers.ui.indexBuffer,
													 VK_INDEX_TYPE_UINT32,
													 &drawInfo),
							   "Failed to draw UI!");
	}

	lunaEndRenderPass();

	VulkanTestResizeSwapchain(lunaEndFrame(), "Failed to present swapchain!");
	if (UnlockLodThreadMutex() != 0)
	{
		LogError("Failed to unlock LOD thread mutex with error: %s", SDL_GetError());
		return VK_ERROR_UNKNOWN;
	}

	return VK_SUCCESS;
}

bool VK_Cleanup()
{
	LogDebug("Cleaning up Vulkan renderer...\n");
	VulkanTest(lunaDestroyInstance(), "Cleanup failed!");
	free(buffers.ui.vertexData);
	free(buffers.ui.indexData);

	return true;
}

inline void VK_Minimize()
{
	minimized = true;
}

inline void VK_Restore()
{
	minimized = false;
}

void VK_DrawColoredQuad(const int32_t x, const int32_t y, const int32_t w, const int32_t h, const Color color)
{
	DrawRectInternal(VK_X_TO_NDC(x), VK_Y_TO_NDC(y), VK_X_TO_NDC(x + w), VK_Y_TO_NDC(y + h), 0, 0, 0, 0, &color, -1);
}

void VK_DrawColoredQuadsBatched(const float *vertices, const int32_t quadCount, const Color color)
{
	for (int32_t i = 0; i < quadCount; i++)
	{
		const uint32_t index = i * 8;
		const mat4 matrix = {
			{vertices[index + 0], vertices[index + 1], 0, 0},
			{vertices[index + 2], vertices[index + 3], 0, 0},
			{vertices[index + 4], vertices[index + 5], 0, 0},
			{vertices[index + 6], vertices[index + 7], 0, 0},
		};
		DrawQuadInternal(matrix, &color, -1);
	}
}

void VK_DrawTexturedQuad(const int32_t x, const int32_t y, const int32_t w, const int32_t h, const char *texture)
{
	DrawRectInternal(VK_X_TO_NDC(x),
					 VK_Y_TO_NDC(y),
					 VK_X_TO_NDC(x + w),
					 VK_Y_TO_NDC(y + h),
					 0,
					 0,
					 1,
					 1,
					 &COLOR_WHITE,
					 TextureIndex(texture));
}

void VK_DrawTexturedQuadMod(const int32_t x,
							const int32_t y,
							const int32_t w,
							const int32_t h,
							const char *texture,
							const Color *color)
{
	DrawRectInternal(VK_X_TO_NDC(x),
					 VK_Y_TO_NDC(y),
					 VK_X_TO_NDC(x + w),
					 VK_Y_TO_NDC(y + h),
					 0,
					 0,
					 1,
					 1,
					 color,
					 TextureIndex(texture));
}

void VK_DrawTexturedQuadRegion(const int32_t x,
							   const int32_t y,
							   const int32_t w,
							   const int32_t h,
							   const int32_t regionX,
							   const int32_t regionY,
							   const int32_t regionW,
							   const int32_t regionH,
							   const char *texture)
{
	const Image *image = LoadImage(texture);

	const float startU = (float)regionX / (float)image->width;
	const float startV = (float)regionY / (float)image->height;

	DrawRectInternal(VK_X_TO_NDC(x),
					 VK_Y_TO_NDC(y),
					 VK_X_TO_NDC(x + w),
					 VK_Y_TO_NDC(y + h),
					 startU,
					 startV,
					 startU + (float)regionW / (float)image->width,
					 startV + (float)regionH / (float)image->height,
					 &COLOR_WHITE,
					 ImageIndex(image));
}

void VK_DrawTexturedQuadRegionMod(const int32_t x,
								  const int32_t y,
								  const int32_t w,
								  const int32_t h,
								  const int32_t regionX,
								  const int32_t regionY,
								  const int32_t regionW,
								  const int32_t regionH,
								  const char *texture,
								  const Color color)
{
	const Image *image = LoadImage(texture);

	const float startU = (float)regionX / (float)image->width;
	const float startV = (float)regionY / (float)image->height;

	DrawRectInternal(VK_X_TO_NDC(x),
					 VK_Y_TO_NDC(y),
					 VK_X_TO_NDC(x + w),
					 VK_Y_TO_NDC(y + h),
					 startU,
					 startV,
					 startU + (float)regionW / (float)image->width,
					 startV + (float)regionH / (float)image->height,
					 &color,
					 ImageIndex(image));
}

void VK_DrawTexturedQuadsBatched(const float *vertices, const int32_t quadCount, const char *texture, const Color color)
{
	for (int32_t i = 0; i < quadCount; i++)
	{
		const uint32_t index = i * 16;
		const mat4 matrix = {
			{
				vertices[index + 0],
				vertices[index + 1],
				vertices[index + 2],
				vertices[index + 3],
			},
			{
				vertices[index + 4],
				vertices[index + 5],
				vertices[index + 6],
				vertices[index + 7],
			},
			{
				vertices[index + 8],
				vertices[index + 9],
				vertices[index + 10],
				vertices[index + 11],
			},
			{
				vertices[index + 12],
				vertices[index + 13],
				vertices[index + 14],
				vertices[index + 15],
			},
		};
		DrawQuadInternal(matrix, &color, TextureIndex(texture));
	}
}

void VK_DrawLine(const int32_t startX,
				 const int32_t startY,
				 const int32_t endX,
				 const int32_t endY,
				 const int32_t thickness,
				 const Color color)
{
	const float dx = (float)endX - (float)startX;
	const float dy = (float)endY - (float)startY;
	const float distance = 2.0f * sqrtf(dx * dx + dy * dy);

	const mat4 matrix = {
		{
			VK_X_TO_NDC(-thickness * dy / distance + (float)startX),
			VK_Y_TO_NDC(thickness * dx / distance + (float)startY),
			0,
			0,
		},
		{
			VK_X_TO_NDC(-thickness * dy / distance + (float)endX),
			VK_Y_TO_NDC(thickness * dx / distance + (float)endY),
			0,
			0,
		},
		{
			VK_X_TO_NDC(thickness * dy / distance + (float)endX),
			VK_Y_TO_NDC(-thickness * dx / distance + (float)endY),
			0,
			0,
		},
		{
			VK_X_TO_NDC(thickness * dy / distance + (float)startX),
			VK_Y_TO_NDC(-thickness * dx / distance + (float)startY),
			0,
			0,
		},
	};
	DrawQuadInternal(matrix, &color, -1);
}

void VK_DrawRectOutline(const int32_t x,
						const int32_t y,
						const int32_t w,
						const int32_t h,
						const int32_t thickness,
						const Color color)
{
	VK_DrawLine(x, y, x + w, y, thickness, color);
	VK_DrawLine(x + w, y, x + w, y + h, thickness, color);
	VK_DrawLine(x + w, y + h, x, y + h, thickness, color);
	VK_DrawLine(x, y + h, x, y, thickness, color);
}

void VK_DrawUiTriangles(const UiTriangleArray *triangleArray, const char *texture, const Color color)
{
	// Good enough for now
	const size_t quadCount = triangleArray->indexCount / 6;
	EnsureSpaceForUiElements(quadCount);

	const size_t vertexOffset = (buffers.ui.allocatedQuads - buffers.ui.freeQuads) * 4;
	UiVertex *vertices = buffers.ui.vertexData + vertexOffset;
	uint32_t *indices = buffers.ui.indexData + (buffers.ui.allocatedQuads - buffers.ui.freeQuads) * 6;

	for (size_t i = 0; i < triangleArray->vertexCount; i++)
	{
		memcpy(vertices + i, triangleArray->vertices[i], sizeof(*triangleArray->vertices));
		vertices[i].r = color.r;
		vertices[i].g = color.g;
		vertices[i].b = color.b;
		vertices[i].a = color.a;
		vertices[i].textureIndex = TextureIndex(texture);
	}
	for (size_t i = 0; i < triangleArray->indexCount; i++)
	{
		indices[i] = (*triangleArray->indices)[i] + vertexOffset;
	}

	buffers.ui.freeQuads -= quadCount;
}

void VK_DrawJoltDebugRendererLine(const Vector3 *from, const Vector3 *to, const uint32_t color)
{
#ifdef JPH_DEBUG_RENDERER
	if (buffers.debugDrawLines.vertices.allocatedSize <
		buffers.debugDrawLines.vertices.bytesUsed + sizeof(DebugDrawVertex) * 2)
	{
		buffers.debugDrawLines.vertices.allocatedSize += sizeof(DebugDrawVertex) * 2 * 16;
		buffers.debugDrawLines.shouldResize = true;

		DebugDrawVertex *newVertices = realloc(buffers.debugDrawLines.vertices.data,
											   buffers.debugDrawLines.vertices.allocatedSize);
		CheckAlloc(newVertices);
		buffers.debugDrawLines.vertices.data = newVertices;
	}

	const float a = 1;
	const float r = (float)(color >> 16 & 0xFF) / 255.0f;
	const float g = (float)(color >> 8 & 0xFF) / 255.0f;
	const float b = (float)(color & 0xFF) / 255.0f;

	DebugDrawVertex *bufferVertices = buffers.debugDrawLines.vertices.data + buffers.debugDrawLines.vertices.bytesUsed;

	bufferVertices[0] = (DebugDrawVertex){
		.position = *from,
		.color = {r, g, b, a},
	};
	bufferVertices[1] = (DebugDrawVertex){
		.position = *to,
		.color = {r, g, b, a},
	};

	buffers.debugDrawLines.vertexCount += 2;
	buffers.debugDrawLines.vertices.bytesUsed += sizeof(DebugDrawVertex) * 2;
#else
	(void)from;
	(void)to;
	(void)color;
#endif
}

void VK_DrawJoltDebugRendererTriangle(const Vector3 *vertices, const uint32_t color)
{
#ifdef JPH_DEBUG_RENDERER
	if (buffers.debugDrawTriangles.vertices.allocatedSize <
		buffers.debugDrawTriangles.vertices.bytesUsed + sizeof(DebugDrawVertex) * 3)
	{
		buffers.debugDrawTriangles.vertices.allocatedSize += sizeof(DebugDrawVertex) * 3 * 16;
		buffers.debugDrawTriangles.shouldResize = true;

		DebugDrawVertex *newVertices = realloc(buffers.debugDrawTriangles.vertices.data,
											   buffers.debugDrawTriangles.vertices.allocatedSize);
		CheckAlloc(newVertices);
		buffers.debugDrawTriangles.vertices.data = newVertices;
	}

	const float a = 1;
	const float r = (float)(color >> 16 & 0xFF) / 255.0f;
	const float g = (float)(color >> 8 & 0xFF) / 255.0f;
	const float b = (float)(color & 0xFF) / 255.0f;

	DebugDrawVertex *bufferVertices = buffers.debugDrawTriangles.vertices.data +
									  buffers.debugDrawTriangles.vertices.bytesUsed;

	bufferVertices[0] = (DebugDrawVertex){
		.position = vertices[0],
		.color = {r, g, b, a},
	};
	bufferVertices[1] = (DebugDrawVertex){
		.position = vertices[1],
		.color = {r, g, b, a},
	};
	bufferVertices[2] = (DebugDrawVertex){
		.position = vertices[2],
		.color = {r, g, b, a},
	};

	buffers.debugDrawTriangles.vertexCount += 3;
	buffers.debugDrawTriangles.vertices.bytesUsed += sizeof(DebugDrawVertex) * 3;
#else
	(void)vertices;
	(void)color;
#endif
}
