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

static const uint32_t MAP_MAX_TRIANGLES_PER_MATERIAL_INIT = 512;

VkResult CreateUiBuffers()
{
	static const uint32_t MAX_UI_QUADS_INIT = 8192; // TODO: Ensure this is a good value for GGUI

	buffers.ui.allocatedQuads = 0;
	buffers.ui.freeQuads = MAX_UI_QUADS_INIT;

	const size_t vertexBufferAllocationSize = MAX_UI_QUADS_INIT * 4 * sizeof(UiVertex);
	const LunaBufferCreationInfo vertexBufferCreationInfo = {
		.size = vertexBufferAllocationSize,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&vertexBufferCreationInfo, &buffers.ui.vertexBuffer),
						   "Failed to create UI vertex buffer!");
	buffers.ui.vertexData = malloc(vertexBufferAllocationSize);
	CheckAlloc(buffers.ui.vertexData);

	const size_t indexBufferAllocationSize = MAX_UI_QUADS_INIT * 6 * sizeof(uint32_t);
	const LunaBufferCreationInfo indexBufferCreationInfo = {
		.size = indexBufferAllocationSize,
		.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&indexBufferCreationInfo, &buffers.ui.indexBuffer),
						   "Failed to create UI index buffer!");
	buffers.ui.indexData = malloc(indexBufferAllocationSize);
	CheckAlloc(buffers.ui.indexData);

	return VK_SUCCESS;
}

VkResult CreateUniformBuffers()
{
	const LunaBufferCreationInfo cameraUniformBufferCreationInfo = {
		.size = sizeof(CameraUniform),
		.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&cameraUniformBufferCreationInfo, &buffers.uniforms.camera),
						   "Failed to create camera uniform buffer!");
	const LunaBufferCreationInfo lightingBufferCreationInfo = {
		.size = sizeof(float) * 6, // r, g, b, x, y, z
		.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&lightingBufferCreationInfo, &buffers.uniforms.lighting),
						   "Failed to create lighting uniform buffer!");
	const LunaBufferCreationInfo fogBufferCreationInfo = {
		.size = sizeof(Color) + sizeof(float) * 2, // fogColor, fogStart, fogEnd
		.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&fogBufferCreationInfo, &buffers.uniforms.fog),
						   "Failed to create fog uniform buffer!");

	return VK_SUCCESS;
}

VkResult CreateShadedMapBuffers()
{
	static const uint32_t MAP_MAX_SHADED_MATERIALS_INIT = 16;

	const LunaBufferCreationInfo verticesBufferCreationInfo = {
		.size = sizeof(MapVertex) * 3 * MAP_MAX_TRIANGLES_PER_MATERIAL_INIT * MAP_MAX_SHADED_MATERIALS_INIT,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&verticesBufferCreationInfo, &buffers.shadedMap.vertices),
						   "Failed to create shaded map vertex buffer!");
	const LunaBufferCreationInfo perMaterialBufferCreationInfo = {
		.size = sizeof(uint32_t) * MAP_MAX_SHADED_MATERIALS_INIT,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&perMaterialBufferCreationInfo, &buffers.shadedMap.perMaterial),
						   "Failed to create shaded map per-material data buffer!");
	const LunaBufferCreationInfo indicesBufferCreationInfo = {
		.size = sizeof(uint32_t) * 3 * MAP_MAX_TRIANGLES_PER_MATERIAL_INIT * MAP_MAX_SHADED_MATERIALS_INIT,
		.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&indicesBufferCreationInfo, &buffers.shadedMap.indices),
						   "Failed to create shaded map index buffer!");
	const LunaBufferCreationInfo drawInfoBufferCreationInfo = {
		.size = sizeof(VkDrawIndexedIndirectCommand) * MAP_MAX_SHADED_MATERIALS_INIT,
		.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&drawInfoBufferCreationInfo, &buffers.shadedMap.drawInfo),
						   "Failed to create shaded map draw info buffer!");

	return VK_SUCCESS;
}

VkResult CreateUnshadedMapBuffers()
{
	static const size_t VERTEX_SIZE = sizeof(MapVertex) - sizeof(Vector3);
	static const uint32_t MAP_MAX_UNSHADED_MATERIALS_INIT = 8;

	const LunaBufferCreationInfo verticesBufferCreationInfo = {
		.size = VERTEX_SIZE * 3 * MAP_MAX_TRIANGLES_PER_MATERIAL_INIT * MAP_MAX_UNSHADED_MATERIALS_INIT,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&verticesBufferCreationInfo, &buffers.unshadedMap.vertices),
						   "Failed to create unshaded map vertex buffer!");
	const LunaBufferCreationInfo perMaterialBufferCreationInfo = {
		.size = sizeof(uint32_t) * MAP_MAX_UNSHADED_MATERIALS_INIT,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&perMaterialBufferCreationInfo, &buffers.unshadedMap.perMaterial),
						   "Failed to create unshaded map per-material data buffer!");
	const LunaBufferCreationInfo indicesBufferCreationInfo = {
		.size = sizeof(uint32_t) * 3 * MAP_MAX_TRIANGLES_PER_MATERIAL_INIT * MAP_MAX_UNSHADED_MATERIALS_INIT,
		.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&indicesBufferCreationInfo, &buffers.unshadedMap.indices),
						   "Failed to create unshaded map index buffer!");
	const LunaBufferCreationInfo drawInfoBufferCreationInfo = {
		.size = sizeof(VkDrawIndexedIndirectCommand) * MAP_MAX_UNSHADED_MATERIALS_INIT,
		.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&drawInfoBufferCreationInfo, &buffers.unshadedMap.drawInfo),
						   "Failed to create unshaded map draw info buffer!");

	return VK_SUCCESS;
}

VkResult CreateSkyBuffers()
{
	static const size_t SKY_MAX_VERTICES_INIT = 559;
	static const size_t SKY_MAX_INDICES_INIT = 2880;

	const LunaBufferCreationInfo verticesBufferCreationInfo = {
		.size = sizeof(SkyVertex) * SKY_MAX_VERTICES_INIT,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&verticesBufferCreationInfo, &buffers.sky.vertices),
						   "Failed to create sky vertex buffer!");
	const LunaBufferCreationInfo indicesBufferCreationInfo = {
		.size = sizeof(uint32_t) * SKY_MAX_INDICES_INIT,
		.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&indicesBufferCreationInfo, &buffers.sky.indices),
						   "Failed to create sky index buffer!");

	return VK_SUCCESS;
}

VkResult CreateShadedViewmodelBuffers()
{
	// TODO: Init sizes are directly based on eraser
	static const size_t VIEWMODEL_MAX_SHADED_VERTICES_INIT = 220;
	static const size_t VIEWMODEL_MAX_SHADED_INDICES_INIT = 900;
	static const size_t VIEWMODEL_MAX_SHADED_MATERIALS_INIT = 1;

	const LunaBufferCreationInfo verticesBufferCreationInfo = {
		.size = sizeof(ModelVertex) * VIEWMODEL_MAX_SHADED_VERTICES_INIT,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&verticesBufferCreationInfo, &buffers.shadedViewmodel.vertices),
						   "Failed to create shaded viewmodel vertex buffer!");
	const LunaBufferCreationInfo perMaterialBufferCreationInfo = {
		.size = sizeof(uint32_t) * VIEWMODEL_MAX_SHADED_MATERIALS_INIT,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&perMaterialBufferCreationInfo, &buffers.shadedViewmodel.perMaterial),
						   "Failed to create shaded viewmodel per-material data buffer!");
	const LunaBufferCreationInfo indicesBufferCreationInfo = {
		.size = sizeof(uint32_t) * VIEWMODEL_MAX_SHADED_INDICES_INIT,
		.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&indicesBufferCreationInfo, &buffers.shadedViewmodel.indices),
						   "Failed to create shaded viewmodel index buffer!");
	const LunaBufferCreationInfo drawInfoBufferCreationInfo = {
		.size = sizeof(VkDrawIndexedIndirectCommand) * VIEWMODEL_MAX_SHADED_MATERIALS_INIT,
		.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&drawInfoBufferCreationInfo, &buffers.shadedViewmodel.drawInfo),
						   "Failed to create shaded viewmodel draw info buffer!");

	return VK_SUCCESS;
}

VkResult CreateUnshadedViewmodelBuffers()
{
	static const size_t VERTEX_SIZE = sizeof(ModelVertex) - sizeof(Vector3);
	// TODO: Init sizes are directly based on eraser, with unshaded being as small as possible without being zero bytes
	static const size_t VIEWMODEL_MAX_UNSHADED_VERTICES_INIT = 1;
	static const size_t VIEWMODEL_MAX_UNSHADED_INDICES_INIT = 1;
	static const size_t VIEWMODEL_MAX_UNSHADED_MATERIALS_INIT = 1;

	const LunaBufferCreationInfo verticesBufferCreationInfo = {
		.size = VERTEX_SIZE * VIEWMODEL_MAX_UNSHADED_VERTICES_INIT,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&verticesBufferCreationInfo, &buffers.unshadedViewmodel.vertices),
						   "Failed to create unshaded viewmodel vertex buffer!");
	const LunaBufferCreationInfo perMaterialBufferCreationInfo = {
		.size = sizeof(uint32_t) * VIEWMODEL_MAX_UNSHADED_MATERIALS_INIT,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&perMaterialBufferCreationInfo, &buffers.unshadedViewmodel.perMaterial),
						   "Failed to create unshaded viewmodel per-material data buffer!");
	const LunaBufferCreationInfo indicesBufferCreationInfo = {
		.size = sizeof(uint32_t) * VIEWMODEL_MAX_UNSHADED_INDICES_INIT,
		.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&indicesBufferCreationInfo, &buffers.unshadedViewmodel.indices),
						   "Failed to create unshaded viewmodel index buffer!");
	const LunaBufferCreationInfo drawInfoBufferCreationInfo = {
		.size = sizeof(VkDrawIndexedIndirectCommand) * VIEWMODEL_MAX_UNSHADED_MATERIALS_INIT,
		.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&drawInfoBufferCreationInfo, &buffers.unshadedViewmodel.drawInfo),
						   "Failed to create unshaded viewmodel draw info buffer!");

	return VK_SUCCESS;
}

VkResult CreateDebugDrawBuffers()
{
#ifdef JPH_DEBUG_RENDERER
	const LunaBufferCreationInfo linesVertexBuffer = {
		.size = buffers.debugDrawLines.vertices.allocatedSize,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&linesVertexBuffer, &buffers.debugDrawLines.vertices.buffer),
						   "Failed to create debug draw lines buffer!");
	buffers.debugDrawLines.vertices.data = malloc(buffers.debugDrawLines.vertices.allocatedSize);
	CheckAlloc(buffers.debugDrawLines.vertices.data);

	const LunaBufferCreationInfo vertexVertexBuffer = {
		.size = buffers.debugDrawTriangles.vertices.allocatedSize,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};
	VulkanTestReturnResult(lunaCreateBuffer(&vertexVertexBuffer, &buffers.debugDrawTriangles.vertices.buffer),
						   "Failed to create debug draw triangles buffer!");
	buffers.debugDrawTriangles.vertices.data = malloc(buffers.debugDrawTriangles.vertices.allocatedSize);
	CheckAlloc(buffers.debugDrawTriangles.vertices.data);
#endif

	return VK_SUCCESS;
}

VkResult ResizeDebugDrawBuffers()
{
#ifdef JPH_DEBUG_RENDERER
	if (buffers.debugDrawLines.vertices.allocatedSize < buffers.debugDrawLines.vertices.bytesUsed)
	{
		VulkanTestReturnResult(lunaResizeBuffer(&buffers.debugDrawLines.vertices.buffer,
												buffers.debugDrawLines.vertices.bytesUsed),
							   "Failed to resize debug draw lines buffer!");
		buffers.debugDrawLines.vertices.allocatedSize = buffers.debugDrawLines.vertices.bytesUsed;
	}
	void *newData = realloc(buffers.debugDrawLines.vertices.data, buffers.debugDrawLines.vertices.allocatedSize);
	CheckAlloc(newData);
	buffers.debugDrawLines.vertices.data = newData;
	buffers.debugDrawLines.shouldResize = false;

	if (buffers.debugDrawTriangles.vertices.allocatedSize < buffers.debugDrawTriangles.vertices.bytesUsed)
	{
		VulkanTestReturnResult(lunaResizeBuffer(&buffers.debugDrawTriangles.vertices.buffer,
												buffers.debugDrawTriangles.vertices.bytesUsed),
							   "Failed to resize debug draw triangles buffer!");
		buffers.debugDrawTriangles.vertices.allocatedSize = buffers.debugDrawTriangles.vertices.bytesUsed;
	}

	newData = realloc(buffers.debugDrawTriangles.vertices.data, buffers.debugDrawTriangles.vertices.allocatedSize);
	CheckAlloc(newData);
	buffers.debugDrawTriangles.vertices.data = newData;
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
	LunaImage lunaImage = LUNA_NULL_HANDLE;
	const size_t index = textures.length;
	VulkanTest(lunaCreateImage(&imageCreationInfo, &lunaImage), "Failed to create texture!");
	imageAssetIdToIndexMap[image->id] = index;
	ListInsertAfter(textures, index - 1, lunaImage);

	const LunaDescriptorImageInfo imageInfo = {
		.image = lunaImage,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	const LunaWriteDescriptorSet writeDescriptor = {
		.descriptorSet = descriptorSet,
		.bindingName = "Textures",
		.descriptorArrayElement = index,
		.descriptorCount = 1,
		.imageInfo = &imageInfo,
	};
	lunaWriteDescriptorSets(1, &writeDescriptor);
	UnlockLodThreadMutex();

	return true;
}
