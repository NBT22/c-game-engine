//
// Created by Noah on 7/5/2024.
//

#include "Vulkan.h"
#include <string.h>
#include "../../../Structs/GlobalState.h"
#include "../../CommonAssets.h"
#include "../../Core/Error.h"
#include "../../Core/Logging.h"
#include "../../Core/MathEx.h"
#include "VulkanHelpers.h"
#include "VulkanInternal.h"
#include "VulkanMemory.h"
#include "VulkanResources.h"

const Level *loadedLevel = NULL;

bool VK_Init(SDL_Window *window)
{
	vk_window = window;
	// clang-format off
	if (CreateInstance() && CreateSurface() && PickPhysicalDevice() && CreateLogicalDevice() && CreateSwapChain() &&
		CreateImageViews() && CreateRenderPass() && CreateDescriptorSetLayouts() && CreateGraphicsPipelineCache() &&
		CreateGraphicsPipelines() && CreateCommandPools() && CreateColorImage() && CreateDepthImage() &&
		CreateFramebuffers() && InitTextures() && CreateTexturesImageView() && CreateTextureSampler() &&
		CreateBuffers() && AllocateMemoryPools() && CreateDescriptorPool() && CreateDescriptorSets() &&
		CreateCommandBuffers() && CreateSyncObjects())
	{
		// clang-format on

		char *vendor = calloc(32, sizeof(char));
		CheckAlloc(vendor);
		switch (physicalDevice.properties.vendorID)
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
		LogInfo("Vulkan Device: %s\n", physicalDevice.properties.deviceName);
		LogInfo("Vulkan Version: %u.%u.%u\n",
				VK_API_VERSION_MAJOR(physicalDevice.properties.apiVersion),
				VK_API_VERSION_MINOR(physicalDevice.properties.apiVersion),
				VK_API_VERSION_PATCH(physicalDevice.properties.apiVersion));

		free(vendor);

		return true;
	}

	if (!VK_Cleanup())
	{
		VulkanLogError("Cleanup failed!");
	}

	return false;
}

VkResult VK_FrameStart()
{
	if (minimized)
	{
		return VK_NOT_READY;
	}

	if (textureCacheMiss && swapchainImageIndex != -1)
	{
		textureCacheMiss = false;
	} else
	{
		VulkanTestReturnResult(vkWaitForFences(device, MAX_FRAMES_IN_FLIGHT, inFlightFences, VK_TRUE, UINT64_MAX),
							   "Failed to wait for Vulkan fences!");

		VulkanTestReturnResult(vkResetFences(device, 1, &inFlightFences[currentFrame]),
							   "Failed to reset Vulkan fences!");

		const VkResult acquireNextImageResult = vkAcquireNextImageKHR(device,
																	  swapChain,
																	  UINT64_MAX,
																	  imageAvailableSemaphores[currentFrame],
																	  VK_NULL_HANDLE,
																	  &swapchainImageIndex);
		if (acquireNextImageResult == VK_ERROR_OUT_OF_DATE_KHR || acquireNextImageResult == VK_SUBOPTIMAL_KHR)
		{
			if (RecreateSwapChain())
			{
				return acquireNextImageResult;
			}
		}
		VulkanTestReturnResult(acquireNextImageResult, "Failed to acquire next Vulkan image index!");
	}

	VulkanTestReturnResult(vkResetCommandBuffer(commandBuffers[currentFrame], 0),
						   "Failed to reset Vulkan command buffer!");

	VulkanTestReturnResult(BeginRenderPass(commandBuffers[currentFrame], swapchainImageIndex),
						   "Failed to begin render pass!");

	vkCmdBindDescriptorSets(commandBuffers[currentFrame],
							VK_PIPELINE_BIND_POINT_GRAPHICS,
							pipelineLayout,
							0,
							1,
							&descriptorSets[currentFrame],
							0,
							NULL);

	if (!BeginCommandBuffer(&transferCommandBuffer, transferCommandPool))
	{
		return VK_ERROR_UNKNOWN;
	}

	buffers.ui.quadCount = 0;

	return VK_SUCCESS;
}

VkResult VK_FrameEnd()
{
	if (buffers.ui.shouldResize)
	{
		if (currentFrame == 0)
		{
			VulkanTestReturnResult(vkWaitForFences(device,
												   1,
												   &inFlightFences[MAX_FRAMES_IN_FLIGHT - 1],
												   VK_TRUE,
												   UINT64_MAX),
								   "Failed to wait for Vulkan fences!");
		} else
		{
			VulkanTestReturnResult(vkWaitForFences(device, 1, &inFlightFences[currentFrame - 1], VK_TRUE, UINT64_MAX),
								   "Failed to wait for Vulkan fences!");
		}

		VulkanTestReturnResult(ResizeUiBuffer(), "Failed to resize UI buffer!");

		buffers.ui.shouldResize = false;
	}

	if (buffers.ui.quadCount > 0)
	{
		memcpy(buffers.ui.vertexStaging, buffers.ui.vertices, sizeof(UiVertex) * buffers.ui.quadCount * 4);
		memcpy(buffers.ui.indexStaging, buffers.ui.indices, sizeof(uint32_t) * buffers.ui.quadCount * 6);
		vkCmdCopyBuffer(transferCommandBuffer,
						buffers.ui.stagingBufferInfo->buffer,
						buffers.ui.bufferInfo->buffer,
						2,
						(VkBufferCopy[]){
							{
								.srcOffset = buffers.ui.vertexStagingOffset,
								.dstOffset = buffers.ui.vertexOffset,
								.size = sizeof(UiVertex) * buffers.ui.quadCount * 4,
							},
							{
								.srcOffset = buffers.ui.indexStagingOffset,
								.dstOffset = buffers.ui.indexOffset,
								.size = sizeof(uint32_t) * buffers.ui.quadCount * 6,
							},
						});
	}

	if (!EndCommandBuffer(transferCommandBuffer, transferCommandPool, transferQueue))
	{
		return VK_ERROR_UNKNOWN;
	}

	if (textureCacheMiss)
	{
		return VK_INCOMPLETE;
	}

	vkCmdNextSubpass(commandBuffers[currentFrame], VK_SUBPASS_CONTENTS_INLINE);
	if (buffers.ui.quadCount > 0)
	{
		vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ui);

		vkCmdBindVertexBuffers(commandBuffers[currentFrame],
							   0,
							   1,
							   &buffers.ui.bufferInfo->buffer,
							   &buffers.ui.vertexOffset);

		vkCmdBindIndexBuffer(commandBuffers[currentFrame],
							 buffers.ui.bufferInfo->buffer,
							 buffers.ui.indexOffset,
							 VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(commandBuffers[currentFrame], buffers.ui.quadCount * 6, 1, 0, 0, 0);
	}

	VulkanTestReturnResult(EndRenderPass(commandBuffers[currentFrame]), "Failed to end render pass!");

	const VkSubmitInfo submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = NULL,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &imageAvailableSemaphores[currentFrame],
		.pWaitDstStageMask = (VkPipelineStageFlags[]){VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
		.commandBufferCount = 1,
		.pCommandBuffers = &commandBuffers[currentFrame],
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &renderFinishedSemaphores[currentFrame],
	};
	VulkanTestReturnResult(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]),
						   "Failed to submit Vulkan draw command buffer!");

	const VkSwapchainKHR swapChains[] = {swapChain};
	const VkPresentInfoKHR presentInfo = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pNext = NULL,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &renderFinishedSemaphores[currentFrame],
		.swapchainCount = 1,
		.pSwapchains = swapChains,
		.pImageIndices = &swapchainImageIndex,
		.pResults = NULL,
	};
	const VkResult queuePresentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
	if (queuePresentResult == VK_ERROR_OUT_OF_DATE_KHR || queuePresentResult == VK_SUBOPTIMAL_KHR)
	{
		if (RecreateSwapChain())
		{
			return queuePresentResult;
		}
	}
	VulkanTestReturnResult(queuePresentResult, "Failed to queue frame for presentation!");

	currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

	return VK_SUCCESS;
}

VkResult VK_RenderLevel(const Level *level, const Camera *camera)
{
	if (loadedLevel != level)
	{
		if (!VK_LoadLevelWalls(level))
		{
			return VK_ERROR_UNKNOWN;
		}
	}
	pushConstants.position[0] = (float)loadedLevel->player.pos.x;
	pushConstants.position[1] = (float)loadedLevel->player.pos.y;
	pushConstants.yaw = camera->yaw + 1.5f * PIf;
	UpdateTranslationMatrix(camera);

	VulkanTestReturnResult(CopyBuffers(loadedLevel), "Failed to copy buffers!");

	if (textureCacheMiss)
	{
		return VK_INCOMPLETE;
	}

	vkCmdPushConstants(commandBuffers[currentFrame],
					   pipelineLayout,
					   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					   0,
					   sizeof(PushConstants),
					   &pushConstants);

	if (buffers.walls.wallCount || buffers.walls.shadowCount)
	{
		vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.walls);

		vkCmdBindVertexBuffers(commandBuffers[currentFrame],
							   0,
							   2,
							   (VkBuffer[]){buffers.walls.bufferInfo->buffer, buffers.walls.bufferInfo->buffer},
							   (VkDeviceSize[]){buffers.walls.shadowOffset, buffers.walls.vertexOffset});
	}
	if (buffers.walls.wallCount)
	{
		vkCmdBindIndexBuffer(commandBuffers[currentFrame],
							 buffers.walls.bufferInfo->buffer,
							 buffers.walls.indexOffset,
							 VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(commandBuffers[currentFrame],
						 6 + (loadedLevel->hasCeiling ? 6 : buffers.walls.skyIndexCount),
						 1,
						 0,
						 0,
						 0);
	}
	if (buffers.walls.shadowCount)
	{
		vkCmdBindIndexBuffer(commandBuffers[currentFrame],
							 buffers.walls.bufferInfo->buffer,
							 buffers.walls.shadowOffset + sizeof(ShadowVertex) * buffers.walls.shadowCount * 4,
							 VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(commandBuffers[currentFrame],
						 buffers.walls.shadowCount * 6,
						 1,
						 0,
						 0,
						 0x53484457); // 0x53484457 is "SHDW", to encode that we are drawing the shadows
	}
	if (buffers.walls.wallCount > loadedLevel->hasCeiling + 1)
	{
		vkCmdBindIndexBuffer(commandBuffers[currentFrame],
							 buffers.walls.bufferInfo->buffer,
							 buffers.walls.indexOffset +
									 sizeof(uint32_t) *
											 (6 + (loadedLevel->hasCeiling ? 6 : buffers.walls.skyIndexCount)),
							 VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(commandBuffers[currentFrame],
						 buffers.walls.wallCount * 6 - (loadedLevel->hasCeiling ? 12 : 6),
						 1,
						 0,
						 0,
						 0x57414C4C); // 0x57414C4C is "WALL", to encode that we are drawing the walls
	}

	if (buffers.actors.drawInfoSize)
	{
		vkCmdBindPipeline(commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.actors);

		vkCmdBindVertexBuffers(commandBuffers[currentFrame],
							   0,
							   2,
							   (VkBuffer[]){buffers.actors.bufferInfo->buffer, buffers.actors.bufferInfo->buffer},
							   (VkDeviceSize[]){buffers.actors.vertexOffset, buffers.actors.instanceDataOffset});

		vkCmdBindIndexBuffer(commandBuffers[currentFrame],
							 buffers.actors.bufferInfo->buffer,
							 buffers.actors.indexOffset,
							 VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexedIndirect(commandBuffers[currentFrame],
								 buffers.actors.bufferInfo->buffer,
								 buffers.actors.drawInfoOffset,
								 buffers.actors.models.loadedModelIds.length + buffers.actors.walls.count,
								 sizeof(VkDrawIndexedIndirectCommand));
	}

	return VK_SUCCESS;
}

bool VK_Cleanup()
{
	if (device)
	{
		VulkanTest(vkDeviceWaitIdle(device), "Failed to wait for device to become idle!");

		CleanupSwapChain();

		vkDestroySampler(device, textureSamplers.linearRepeat, NULL);
		vkDestroySampler(device, textureSamplers.nearestRepeat, NULL);
		vkDestroySampler(device, textureSamplers.linearNoRepeat, NULL);
		vkDestroySampler(device, textureSamplers.nearestNoRepeat, NULL);
		for (size_t textureIndex = 0; textureIndex < textures.length; textureIndex++)
		{
			vkDestroyImageView(device, *(VkImageView *)ListGet(texturesImageView, textureIndex), NULL);
			vkDestroyImage(device, ((Texture *)ListGet(textures, textureIndex))->image, NULL);
		}
		ListAndContentsFree(&texturesImageView, false);
		ListAndContentsFree(&textures, false);
		vkFreeMemory(device, textureMemory.memory, NULL);

		CleanupColorImage();
		CleanupDepthImage();

		vkDestroyPipelineCache(device, pipelineCache, NULL);
		CleanupPipeline();

		vkDestroyRenderPass(device, renderPass, NULL);

		vkDestroyDescriptorPool(device, descriptorPool, NULL);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, NULL);

		if (!DestroyBuffer(&buffers.local))
		{
			return false;
		}
		if (!DestroyBuffer(&buffers.shared))
		{
			return false;
		}

		CleanupSyncObjects();

		vkDestroyCommandPool(device, graphicsCommandPool, NULL);
		vkDestroyCommandPool(device, transferCommandPool, NULL);
	}

	vkDestroyDevice(device, NULL);

	if (instance)
	{
		vkDestroySurfaceKHR(instance, surface, NULL);
	}

	vkDestroyInstance(instance, NULL);

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

inline uint8_t VK_GetSampleCountFlags()
{
	return physicalDevice.properties.limits.framebufferColorSampleCounts &
		   physicalDevice.properties.limits.framebufferDepthSampleCounts &
		   0xF;
}

bool VK_LoadLevelWalls(const Level *level)
{
	void *data;
	uint32_t skyVertexCount = 0;
	const Model *_skyModel = !skyModel ? LoadModel(MODEL("model_sky")) : skyModel;
	if (level->hasCeiling)
	{
		buffers.walls.skyIndexCount = 0;
		buffers.walls.wallCount = level->walls.length + 2;
	} else
	{
		skyVertexCount = _skyModel->vertexCount;
		buffers.walls.skyIndexCount = _skyModel->indexCount;
		buffers.walls.wallCount = level->walls.length + 1;
	}

	if (buffers.walls.wallCount > buffers.walls.maxWallCount)
	{
		const VkDeviceSize wallVertexSize = sizeof(WallVertex) *
											(buffers.walls.maxWallCount * 4 + _skyModel->vertexCount);
		const VkDeviceSize wallIndexSize = sizeof(uint32_t) * (buffers.walls.maxWallCount * 6 + _skyModel->indexCount);

		buffers.walls.maxWallCount = buffers.walls.wallCount;

		if (buffers.walls.vertexOffset <= buffers.walls.indexOffset &&
			buffers.walls.indexOffset == buffers.walls.vertexOffset + wallVertexSize)
		{
			if (!ResizeBufferRegion(&buffers.local,
									buffers.walls.vertexOffset,
									wallVertexSize + wallIndexSize,
									sizeof(WallVertex) * buffers.walls.maxWallCount * 4 +
											sizeof(uint32_t) * buffers.walls.maxWallCount * 6 +
											sizeof(WallVertex) * _skyModel->vertexCount +
											sizeof(uint32_t) * _skyModel->indexCount,
									true))
			{
				return false;
			}
		} else if (buffers.walls.indexOffset < buffers.walls.vertexOffset &&
				   buffers.walls.vertexOffset == buffers.walls.indexOffset + wallIndexSize)
		{
			if (!ResizeBufferRegion(&buffers.local,
									buffers.walls.indexOffset,
									wallIndexSize + wallVertexSize,
									sizeof(WallVertex) * buffers.walls.maxWallCount * 4 +
											sizeof(uint32_t) * buffers.walls.maxWallCount * 6 +
											sizeof(WallVertex) * _skyModel->vertexCount +
											sizeof(uint32_t) * _skyModel->indexCount,
									true))
			{
				return false;
			}
		} else
		{
			if (!ResizeBufferRegion(&buffers.local,
									buffers.walls.vertexOffset,
									wallVertexSize,
									sizeof(WallVertex) * buffers.walls.maxWallCount * 4 +
											sizeof(WallVertex) * _skyModel->vertexCount,
									true))
			{
				return false;
			}
			if (!ResizeBufferRegion(&buffers.local,
									buffers.walls.indexOffset,
									wallIndexSize,
									sizeof(uint32_t) * buffers.walls.maxWallCount * 6 +
											sizeof(uint32_t) * _skyModel->indexCount,
									true))
			{
				return false;
			}
		}

		buffers.walls.shadowSize = sizeof(ShadowVertex) * buffers.walls.shadowCount * 4 +
								   sizeof(uint32_t) * buffers.walls.shadowCount * 6;

		buffers.ui.vertexSize = sizeof(UiVertex) * buffers.ui.maxQuads * 4;
		buffers.ui.indexSize = sizeof(uint32_t) * buffers.ui.maxQuads * 6;

		buffers.actors.instanceDataSize = sizeof(ActorInstanceData) * buffers.actors.walls.count;
		if (buffers.actors.models.modelCounts.length && buffers.actors.models.loadedModelIds.length)
		{
			for (size_t i = 0; i < buffers.actors.models.loadedModelIds.length; i++)
			{
				buffers.actors.instanceDataSize += sizeof(ActorInstanceData) *
												   (size_t)ListGet(buffers.actors.models.modelCounts, i);
			}
		}
		buffers.actors.drawInfoSize = sizeof(VkDrawIndexedIndirectCommand) *
											  buffers.actors.models.loadedModelIds.length +
									  sizeof(VkDrawIndexedIndirectCommand) * buffers.actors.walls.count;

		buffers.actors.models.vertexSize = sizeof(ActorVertex) * buffers.actors.models.vertexCount;
		buffers.actors.models.indexSize = sizeof(uint32_t) * buffers.actors.models.indexCount;
		buffers.actors.walls.vertexSize = sizeof(ActorVertex) * buffers.actors.walls.count * 4;
		buffers.actors.walls.indexSize = sizeof(uint32_t) * buffers.actors.walls.count * 6;
		SetLocalBufferAliasingInfo();
	}

	ListClear(&buffers.actors.models.loadedModelIds);
	ListClear(&buffers.actors.models.modelCounts);
	buffers.walls.shadowCount = 0;
	memset(&buffers.actors.models, 0, sizeof(ModelActorBuffer));
	memset(&buffers.actors.walls, 0, sizeof(WallActorBuffer));
	ListLock(level->actors);
	for (size_t i = 0; i < level->actors.length; i++)
	{
		const Actor *actor = ListGet(level->actors, i);
		if (!actor->actorModel)
		{
			if (!actor->actorWall)
			{
				continue;
			}
			buffers.actors.walls.count++;
		} else
		{
			size_t index = ListFind(buffers.actors.models.loadedModelIds, (void *)actor->actorModel->id);
			if (index == -1)
			{
				index = buffers.actors.models.loadedModelIds.length;
				ListAdd(&buffers.actors.models.loadedModelIds, (void *)actor->actorModel->id);
				buffers.actors.models.vertexCount += actor->actorModel->vertexCount;
				buffers.actors.models.indexCount += actor->actorModel->indexCount;
			}
			if (index < buffers.actors.models.modelCounts.length)
			{
				buffers.actors.models.modelCounts.data[index]++;
			} else
			{
				ListAdd(&buffers.actors.models.modelCounts, (void *)1);
			}
		}
		if (actor->showShadow)
		{
			buffers.walls.shadowCount++;
		}
	}
	ListUnlock(level->actors);

	if (sizeof(ActorVertex) * buffers.actors.models.vertexCount > buffers.actors.models.vertexSize ||
		sizeof(uint32_t) * buffers.actors.models.indexCount > buffers.actors.models.indexSize ||
		sizeof(ActorVertex) * buffers.actors.walls.count * 4 > buffers.actors.walls.vertexSize ||
		sizeof(uint32_t) * buffers.actors.walls.count * 6 > buffers.actors.walls.indexSize)
	{
		if (currentFrame == 0)
		{
			VulkanTest(vkWaitForFences(device, 1, &inFlightFences[MAX_FRAMES_IN_FLIGHT - 1], VK_TRUE, UINT64_MAX),
					   "Failed to wait for Vulkan fences!");
		} else
		{
			VulkanTest(vkWaitForFences(device, 1, &inFlightFences[currentFrame - 1], VK_TRUE, UINT64_MAX),
					   "Failed to wait for Vulkan fences!");
		}
		if (!ResizeActorBuffer())
		{
			return false;
		}
	}

	if (level->hasCeiling)
	{
		pushConstants.skyVertexCount = 0;
		pushConstants.skyTextureIndex = MAX_TEXTURES;
	} else
	{
		pushConstants.skyVertexCount = skyModel->vertexCount;
		pushConstants.skyTextureIndex = TextureIndex(level->ceilOrSkyTex);
	}
	pushConstants.shadowTextureIndex = TextureIndex(TEXTURE("vfx_shadow"));
	pushConstants.fogStart = (float)level->fogStart;
	pushConstants.fogEnd = (float)level->fogEnd;
	pushConstants.fogColor = level->fogColor;

	WallVertex *wallVertices = calloc(buffers.walls.wallCount * 4 + skyVertexCount, sizeof(WallVertex));
	CheckAlloc(wallVertices);
	uint32_t *wallIndices = calloc(buffers.walls.wallCount * 6 + buffers.walls.skyIndexCount, sizeof(uint32_t));
	CheckAlloc(wallIndices);

	ActorVertex *actorVertices = calloc(buffers.actors.models.vertexCount, sizeof(ActorVertex));
	CheckAlloc(actorVertices);
	uint32_t *actorIndices = calloc(buffers.actors.models.indexCount, sizeof(uint32_t));
	CheckAlloc(actorIndices);

	LoadWalls(level, _skyModel, wallVertices, wallIndices, skyVertexCount);
	LoadActorModels(level, actorVertices, actorIndices);

	const VkDeviceSize wallVertexSize = sizeof(WallVertex) * (buffers.walls.maxWallCount * 4 + _skyModel->vertexCount);
	const VkDeviceSize wallIndexSize = sizeof(uint32_t) * (buffers.walls.maxWallCount * 6 + _skyModel->indexCount);

	MemoryInfo memoryInfo = {
		.type = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	};
	const MemoryAllocationInfo allocationInfo = {
		.memoryInfo = &memoryInfo,
		.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	};
	Buffer stagingBuffer = {
		.memoryAllocationInfo = allocationInfo,
		.size = wallVertexSize + wallIndexSize + buffers.actors.models.vertexSize + buffers.actors.models.indexSize,
	};
	if (!CreateBuffer(&stagingBuffer, true))
	{
		free(wallVertices);
		free(wallIndices);
		free(actorVertices);
		free(actorIndices);

		return false;
	}

	VulkanTest(vkMapMemory(device, memoryInfo.memory, 0, stagingBuffer.size, 0, &data),
			   "Failed to map staging buffer memory!");

	memcpy(data, wallVertices, sizeof(WallVertex) * (buffers.walls.wallCount * 4 + skyVertexCount));
	memcpy(data + wallVertexSize,
		   wallIndices,
		   sizeof(uint32_t) * (buffers.walls.wallCount * 6 + buffers.walls.skyIndexCount));
	memcpy(data + wallVertexSize + wallIndexSize,
		   actorVertices,
		   sizeof(ActorVertex) * buffers.actors.models.vertexCount);
	memcpy(data + wallVertexSize + wallIndexSize + buffers.actors.models.vertexSize,
		   actorIndices,
		   sizeof(uint32_t) * buffers.actors.models.indexCount);
	vkUnmapMemory(device, memoryInfo.memory);

	const VkBufferCopy regions[] = {
		{
			.srcOffset = 0,
			.dstOffset = buffers.walls.vertexOffset,
			.size = wallVertexSize,
		},
		{
			.srcOffset = wallVertexSize,
			.dstOffset = buffers.walls.indexOffset,
			.size = wallIndexSize,
		},
		{
			.srcOffset = wallVertexSize + wallIndexSize,
			.dstOffset = buffers.actors.vertexOffset,
			.size = buffers.actors.models.vertexSize,
		},
		{
			.srcOffset = wallVertexSize + wallIndexSize + buffers.actors.models.vertexSize,
			.dstOffset = buffers.actors.indexOffset,
			.size = buffers.actors.models.indexSize,
		},
	};
	if (!CopyBuffer(stagingBuffer.buffer, buffers.local.buffer, !buffers.actors.models.vertexSize ? 2 : 4, regions))
	{
		return false;
	}

	loadedLevel = level;

	free(wallVertices);
	free(wallIndices);
	free(actorVertices);
	free(actorIndices);

	return DestroyBuffer(&stagingBuffer);
}

bool VK_LoadNewActor()
{
	const Actor *actor = ListGet(loadedLevel->actors, loadedLevel->actors.length - 1);
	if (!actor->actorModel)
	{
		if (!actor->actorWall)
		{
			return true;
		}
		buffers.actors.walls.count++;
	} else
	{
		size_t index = ListFind(buffers.actors.models.loadedModelIds, (void *)actor->actorModel->id);
		if (index == -1)
		{
			index = buffers.actors.models.loadedModelIds.length;
			ListAdd(&buffers.actors.models.loadedModelIds, (void *)actor->actorModel->id);
			buffers.actors.models.vertexCount += actor->actorModel->vertexCount;
			buffers.actors.models.indexCount += actor->actorModel->indexCount;
		}
		if (index < buffers.actors.models.modelCounts.length)
		{
			buffers.actors.models.modelCounts.data[index]++;
		} else
		{
			ListAdd(&buffers.actors.models.modelCounts, (void *)1);
		}
	}
	if (actor->showShadow)
	{
		buffers.walls.shadowCount++;
	}

	if (currentFrame == 0)
	{
		VulkanTest(vkWaitForFences(device, 1, &inFlightFences[MAX_FRAMES_IN_FLIGHT - 1], VK_TRUE, UINT64_MAX),
				   "Failed to wait for Vulkan fences!");
	} else
	{
		VulkanTest(vkWaitForFences(device, 1, &inFlightFences[currentFrame - 1], VK_TRUE, UINT64_MAX),
				   "Failed to wait for Vulkan fences!");
	}
	if (!ResizeActorBuffer())
	{
		return false;
	}

	ActorVertex *actorVertices = calloc(buffers.actors.models.vertexCount, sizeof(ActorVertex));
	CheckAlloc(actorVertices);
	uint32_t *actorIndices = calloc(buffers.actors.models.indexCount, sizeof(uint32_t));
	CheckAlloc(actorIndices);
	LoadActorModels(loadedLevel, actorVertices, actorIndices);

	MemoryInfo memoryInfo = {
		.type = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	};
	const MemoryAllocationInfo allocationInfo = {
		.memoryInfo = &memoryInfo,
		.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	};
	Buffer stagingBuffer = {
		.memoryAllocationInfo = allocationInfo,
		.size = buffers.actors.models.vertexSize + buffers.actors.models.indexSize,
	};
	if (!CreateBuffer(&stagingBuffer, true))
	{
		free(actorVertices);
		free(actorIndices);

		return false;
	}

	void *data;
	VulkanTest(vkMapMemory(device, memoryInfo.memory, 0, stagingBuffer.size, 0, &data),
			   "Failed to map actor model staging buffer memory!");

	memcpy(data, actorVertices, sizeof(ActorVertex) * buffers.actors.models.vertexCount);
	memcpy(data + buffers.actors.models.vertexSize, actorIndices, sizeof(uint32_t) * buffers.actors.models.indexCount);
	vkUnmapMemory(device, memoryInfo.memory);

	const VkBufferCopy regions[] = {
		{
			.srcOffset = 0,
			.dstOffset = buffers.actors.vertexOffset,
			.size = buffers.actors.models.vertexSize,
		},
		{
			.srcOffset = buffers.actors.models.vertexSize,
			.dstOffset = buffers.actors.indexOffset,
			.size = buffers.actors.models.indexSize,
		},
	};
	if (!CopyBuffer(stagingBuffer.buffer, buffers.local.buffer, 2, regions))
	{
		return false;
	}

	free(actorVertices);
	free(actorIndices);

	return DestroyBuffer(&stagingBuffer);
}

void VK_DrawColoredQuad(const int32_t x, const int32_t y, const int32_t w, const int32_t h, const Color color)
{
	DrawRectInternal(VK_X_TO_NDC(x), VK_Y_TO_NDC(y), VK_X_TO_NDC(x + w), VK_Y_TO_NDC(y + h), 0, 0, 0, 0, color, -1);
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
		DrawQuadInternal(matrix, color, -1);
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
					 COLOR_WHITE,
					 TextureIndex(texture));
}

void VK_DrawTexturedQuadMod(const int32_t x,
							const int32_t y,
							const int32_t w,
							const int32_t h,
							const char *texture,
							const Color color)
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
					 COLOR_WHITE,
					 imageAssetIdToIndexMap[image->id]);
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
					 color,
					 imageAssetIdToIndexMap[image->id]);
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
		DrawQuadInternal(matrix, color, TextureIndex(texture));
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
	DrawQuadInternal(matrix, color, -1);
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

void VK_ClearColor(const Color color)
{

	clearColor = (VkClearColorValue){{color.r, color.g, color.b, color.a}};
}

void VK_SetTexParams(const char *texture, const bool linear, const bool repeat)
{
	const uint32_t textureIndex = TextureIndex(texture);
	for (uint8_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		VkDescriptorImageInfo imageInfo = {
			.sampler = textureSamplers.nearestNoRepeat,
			.imageView = *(VkImageView *)ListGet(texturesImageView, textureIndex),
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
		if (linear && repeat)
		{
			imageInfo.sampler = textureSamplers.linearRepeat;
		} else if (linear)
		{
			imageInfo.sampler = textureSamplers.linearNoRepeat;
		} else if (repeat)
		{
			imageInfo.sampler = textureSamplers.nearestRepeat;
		}

		const VkWriteDescriptorSet writeDescriptor = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = NULL,
			.dstSet = descriptorSets[i],
			.dstBinding = 0,
			.dstArrayElement = textureIndex,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &imageInfo,
			.pBufferInfo = NULL,
			.pTexelBufferView = NULL,
		};
		vkUpdateDescriptorSets(device, 1, &writeDescriptor, 0, NULL);
	}
}
