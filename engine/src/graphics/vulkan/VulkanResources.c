//
// Created by Noah on 12/18/2024.
//

#include <engine/assets/ModelLoader.h>
#include <engine/assets/TextureLoader.h>
#include <engine/graphics/vulkan/VulkanHelpers.h>
#include <engine/graphics/vulkan/VulkanResources.h>
#include <engine/helpers/MathEx.h>
#include <engine/helpers/Realloc.h>
#include <engine/structs/GlobalState.h>
#include <engine/structs/List.h>
#include <engine/structs/Viewmodel.h>
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

VkResult CreateUiBuffers()
{
	const LunaBufferCreationInfo vertexBufferCreationInfo = {
		.size = buffers.ui.vertices.allocatedSize,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&vertexBufferCreationInfo, &buffers.ui.vertices.buffer),
						   "Failed to create UI vertex buffer!");
	buffers.ui.vertices.data = malloc(buffers.ui.vertices.allocatedSize);
	CheckAlloc(buffers.ui.vertices.data);

	const LunaBufferCreationInfo indexBufferCreationInfo = {
		.size = buffers.ui.indices.allocatedSize,
		.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&indexBufferCreationInfo, &buffers.ui.indices.buffer),
						   "Failed to create UI index buffer!");
	buffers.ui.indices.data = malloc(buffers.ui.indices.allocatedSize);
	CheckAlloc(buffers.ui.indices.data);

	return VK_SUCCESS;
}

VkResult CreateDebugDrawBuffers()
{
#ifdef JPH_DEBUG_RENDERER
	const LunaBufferCreationInfo linesBufferCreationInfo = {
		.size = buffers.debugDrawLines.vertices.allocatedSize,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&linesBufferCreationInfo, &buffers.debugDrawLines.vertices.buffer),
						   "Failed to create debug draw lines buffer!");
	buffers.debugDrawLines.vertices.data = malloc(buffers.debugDrawLines.vertices.allocatedSize);
	CheckAlloc(buffers.debugDrawLines.vertices.data);

	const LunaBufferCreationInfo trianglesBufferCreationInfo = {
		.size = buffers.debugDrawTriangles.vertices.allocatedSize,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&trianglesBufferCreationInfo, &buffers.debugDrawTriangles.vertices.buffer),
						   "Failed to create debug draw triangles buffer!");
	buffers.debugDrawTriangles.vertices.data = malloc(buffers.debugDrawTriangles.vertices.allocatedSize);
	CheckAlloc(buffers.debugDrawTriangles.vertices.data);
#endif

	return VK_SUCCESS;
}

VkResult ResizeDebugDrawBuffers()
{
#ifdef JPH_DEBUG_RENDERER
	lunaDestroyBuffer(buffers.debugDrawLines.vertices.buffer);
	lunaDestroyBuffer(buffers.debugDrawTriangles.vertices.buffer);


	const LunaBufferCreationInfo linesBufferCreationInfo = {
		.size = buffers.debugDrawLines.vertices.allocatedSize,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	const LunaBufferCreationInfo trianglesBufferCreationInfo = {
		.size = buffers.debugDrawTriangles.vertices.allocatedSize,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&linesBufferCreationInfo, &buffers.debugDrawLines.vertices.buffer),
						   "Failed to recreate debug draw lines buffer!");
	VulkanTestReturnResult(lunaCreateBuffer(&trianglesBufferCreationInfo, &buffers.debugDrawTriangles.vertices.buffer),
						   "Failed to recreate debug draw triangles buffer!");

	buffers.debugDrawLines.shouldResize = false;
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
