//
// Created by Noah on 12/18/2024.
//

#include <cglm/cglm.h>
#include <engine/assets/AssetReader.h>
#include <engine/assets/TextureLoader.h>
#include <engine/graphics/vulkan/VulkanHelpers.h>
#include <engine/graphics/vulkan/VulkanResources.h>
#include <engine/helpers/MathEx.h>
#include <engine/structs/GlobalState.h>
#include <engine/structs/List.h>
#include <engine/subsystem/Error.h>
#include <engine/subsystem/threads/LodThread.h>
#include <luna/luna.h>
#include <luna/lunaBuffer.h>
#include <luna/lunaImage.h>
#include <luna/lunaTypes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

/**
 * A helper function which creates the underlying @c LunaBuffer for a given @c BufferRegion,
 *  with the size of the buffer given by the @c allocatedSize field on the buffer region
 * @param bufferRegion A pointer to the buffer region that should have a buffer created for it
 * @param usage The usage flags of the buffer to create
 * @return @c VK_SUCCESS if the buffer region was successfully created, or a meaningful result code otherwise
 */
static inline VkResult CreateBufferRegion(BufferRegion *bufferRegion, const VkBufferUsageFlags usage)
{
	const LunaBufferCreationInfo vertexBufferCreationInfo = {
		.size = bufferRegion->allocatedSize,
		.usage = usage,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&vertexBufferCreationInfo, &bufferRegion->buffer),
						   "Failed to create buffer region!");

	return VK_SUCCESS;
}

/**
 * A helper function which creates the underlying @c LunaBuffer and @c data pointer for a given @c BufferRegionWithData,
 *  with the size of the buffer and data pointer allocation given by the @c allocatedSize field on the buffer region
 * @param bufferRegion A pointer to the buffer region that should have a buffer and data pointer created for it
 * @param usage The usage flags of the buffer to create
 * @return @c VK_SUCCESS if the buffer region and data pointer were successfully created,
 *          or a meaningful result code otherwise
 */
static inline VkResult CreateBufferRegionWithData(BufferRegionWithData *bufferRegion, const VkBufferUsageFlags usage)
{
	const LunaBufferCreationInfo vertexBufferCreationInfo = {
		.size = bufferRegion->allocatedSize,
		.usage = usage,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&vertexBufferCreationInfo, &bufferRegion->buffer),
						   "Failed to create buffer region with data!");
	bufferRegion->data = malloc(bufferRegion->allocatedSize);
	CheckAlloc(bufferRegion->data);

	return VK_SUCCESS;
}

/**
 * A helper function which resizes a @c BufferRegion struct to fit the @c bytesUsed field.
 * @note Resizing a buffer is typically a slow operation, since most of the time the data will have to be copied from
 *        one place to another within VRAM, rather than just extending the current block.
 * @param bufferRegion A pointer to the buffer region that should be resized
 * @return @c VK_SUCCESS of the buffer region was successfully resized, or a meaningful result code otherwise
 */
static inline VkResult ResizeBufferRegion(BufferRegion *bufferRegion)
{
	if (bufferRegion->allocatedSize < bufferRegion->bytesUsed)
	{
		VulkanTestReturnResult(lunaResizeBuffer(&bufferRegion->buffer, bufferRegion->bytesUsed),
							   "Failed to resize buffer region!");
		bufferRegion->allocatedSize = bufferRegion->bytesUsed;
	}

	return VK_SUCCESS;
}

/**
 * A helper function which resizes a @c BufferRegionWithData struct to fit the @c bytesUsed field. This resizes both the
 *  buffer, as well as the @c data pointer.
 * @note Resizing a buffer is typically a slow operation, since most of the time the data will have to be copied from
 *        one place to another within VRAM, rather than just extending the current block.
 * @param bufferRegion A pointer to the buffer region that should be resized
 * @return @c VK_SUCCESS of the buffer region was successfully resized, or a meaningful result code otherwise
 */
static inline VkResult ResizeBufferRegionWithData(BufferRegionWithData *bufferRegion)
{
	if (bufferRegion->allocatedSize < bufferRegion->bytesUsed)
	{
		VulkanTestReturnResult(lunaResizeBuffer(&bufferRegion->buffer, bufferRegion->bytesUsed),
							   "Failed to resize buffer region with data!");
		bufferRegion->allocatedSize = bufferRegion->bytesUsed;
	}

	void *newData = realloc(bufferRegion->data, bufferRegion->allocatedSize);
	CheckAlloc(newData);
	bufferRegion->data = newData;

	return VK_SUCCESS;
}

VkResult CreateUiBuffers()
{
	VulkanTestReturnResult(CreateBufferRegionWithData(&buffers.ui.vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
						   "Failed to create UI vertex buffer!");
	VulkanTestReturnResult(CreateBufferRegionWithData(&buffers.ui.indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
						   "Failed to create UI index buffer!");

	return VK_SUCCESS;
}

VkResult CreateMapBuffers()
{
	VulkanTestReturnResult(CreateBufferRegion(&buffers.map.vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
						   "Failed to create map vertex buffer!");
	VulkanTestReturnResult(CreateBufferRegion(&buffers.map.perMaterialData, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
						   "Failed to create map per-material data buffer!");
	VulkanTestReturnResult(CreateBufferRegion(&buffers.map.indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
						   "Failed to create map index buffer!");
	VulkanTestReturnResult(CreateBufferRegion(&buffers.map.drawInfo, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT),
						   "Failed to create map draw info buffer!");

	return VK_SUCCESS;
}

VkResult CreateDebugDrawBuffers()
{
#ifdef JPH_DEBUG_RENDERER
	VulkanTestReturnResult(CreateBufferRegionWithData(&buffers.debugDrawLines.vertices,
													  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
						   "Failed to create debug draw lines buffer!");
	VulkanTestReturnResult(CreateBufferRegionWithData(&buffers.debugDrawTriangles.vertices,
													  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
						   "Failed to create debug draw triangles buffer!");
#endif

	return VK_SUCCESS;
}

VkResult ResizeMapBuffers()
{
	VulkanTestReturnResult(ResizeBufferRegion(&buffers.map.vertices), "Failed to resize map vertex buffer!");
	VulkanTestReturnResult(ResizeBufferRegion(&buffers.map.perMaterialData),
						   "Failed to resize map per-material data buffer!");
	VulkanTestReturnResult(ResizeBufferRegion(&buffers.map.indices), "Failed to resize map index buffer!");
	VulkanTestReturnResult(ResizeBufferRegion(&buffers.map.drawInfo), "Failed to resize map draw info buffer!");

	return VK_SUCCESS;
}

VkResult ResizeDebugDrawBuffers()
{
#ifdef JPH_DEBUG_RENDERER
	VulkanTestReturnResult(ResizeBufferRegionWithData(&buffers.debugDrawLines.vertices),
						   "Failed to resize debug draw lines buffer!");
	buffers.debugDrawLines.shouldResize = false;
	VulkanTestReturnResult(ResizeBufferRegionWithData(&buffers.debugDrawTriangles.vertices),
						   "Failed to resize debug draw triangles buffer!");
	buffers.debugDrawTriangles.shouldResize = false;
#endif

	return VK_SUCCESS;
}

bool LoadTexture(const Image *image)
{
	LockLodThreadMutex(); // TODO: This is not a great fix but it works ig
	const bool useMipmaps = GetState()->options.mipmaps && image->mipmaps;
	LunaSampler sampler = LUNA_NULL_HANDLE;
	if (image->filter && image->repeat)
	{
		sampler = useMipmaps ? textureSamplers.linearRepeatAnisotropy : textureSamplers.linearRepeatNoAnisotropy;
	}
	if (image->filter && !image->repeat)
	{
		sampler = useMipmaps ? textureSamplers.linearNoRepeatAnisotropy : textureSamplers.linearNoRepeatNoAnisotropy;
	}
	if (!image->filter && image->repeat)
	{
		sampler = useMipmaps ? textureSamplers.nearestRepeatAnisotropy : textureSamplers.nearestRepeatNoAnisotropy;
	}
	if (!image->filter && !image->repeat)
	{
		sampler = useMipmaps ? textureSamplers.nearestNoRepeatAnisotropy : textureSamplers.nearestNoRepeatNoAnisotropy;
	}
	const LunaSampledImageCreationInfo imageCreationInfo = {
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.width = image->width,
		.height = image->height,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT,
		.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.writeInfo.bytes = image->width * image->height * sizeof(uint32_t),
		.writeInfo.pixels = image->pixelData,
		.writeInfo.mipmapLevels = useMipmaps ? (uint8_t)log2(max(image->width, image->height)) + 1 : 1,
		.writeInfo.generateMipmaps = useMipmaps,
		.writeInfo.sourceStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		.writeInfo.destinationStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		.writeInfo.destinationAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.sampler = sampler,
	};
	LunaImage lunaImage = VK_NULL_HANDLE;
	const size_t index = textures.length;
	VulkanTest(lunaCreateImage(&imageCreationInfo, &lunaImage), "Failed to create texture!");
	imageAssetIdToIndexMap[image->id] = index;
	ListInsertAfter(textures, index - 1, lunaImage);

	const LunaDescriptorImageInfo imageInfo = {
		.image = lunaImage,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	LunaWriteDescriptorSet writeDescriptors[MAX_FRAMES_IN_FLIGHT];
	for (uint8_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		writeDescriptors[i] = (LunaWriteDescriptorSet){
			.descriptorSet = descriptorSets[i],
			.bindingName = "Textures",
			.descriptorArrayElement = index,
			.descriptorCount = 1,
			.imageInfo = &imageInfo,
		};
	}
	lunaWriteDescriptorSets(MAX_FRAMES_IN_FLIGHT, writeDescriptors);
	UnlockLodThreadMutex();

	return true;
}
