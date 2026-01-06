//
// Created by Noah on 11/23/2024.
//

#include <assert.h>
#include <cglm/cglm.h>
#include <cglm/clipspace/persp_lh_zo.h>
#include <engine/assets/ShaderLoader.h>
#include <engine/assets/TextureLoader.h>
#include <engine/graphics/RenderingHelpers.h>
#include <engine/graphics/vulkan/VulkanHelpers.h>
#include <engine/graphics/vulkan/VulkanResources.h>
#include <engine/physics/Physics.h>
#include <engine/structs/Camera.h>
#include <engine/structs/Color.h>
#include <engine/structs/List.h>
#include <engine/structs/Viewmodel.h>
#include <engine/subsystem/Error.h>
#include <joltc/Math/Quat.h>
#include <joltc/Math/Vector3.h>
#include <luna/luna.h>
#include <luna/lunaTypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

#pragma region variables
bool minimized = false;
VkExtent2D swapChainExtent = {0};
VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
LunaRenderPass renderPass = LUNA_NULL_HANDLE;
uint32_t imageAssetIdToIndexMap[MAX_TEXTURES];
TextureSamplers textureSamplers = {
	.linearRepeatAnisotropy = LUNA_NULL_HANDLE,
	.nearestRepeatAnisotropy = LUNA_NULL_HANDLE,
	.linearNoRepeatAnisotropy = LUNA_NULL_HANDLE,
	.nearestNoRepeatAnisotropy = LUNA_NULL_HANDLE,
	.linearRepeatNoAnisotropy = LUNA_NULL_HANDLE,
	.nearestRepeatNoAnisotropy = LUNA_NULL_HANDLE,
	.linearNoRepeatNoAnisotropy = LUNA_NULL_HANDLE,
	.nearestNoRepeatNoAnisotropy = LUNA_NULL_HANDLE,
};
LockingList textures = {0};
LunaDescriptorSetLayout descriptorSetLayout = LUNA_NULL_HANDLE;
LunaDescriptorSet descriptorSets[MAX_FRAMES_IN_FLIGHT];
PushConstants pushConstants = {0};
Pipelines pipelines = {
	.ui = LUNA_NULL_HANDLE,
#ifdef JPH_DEBUG_RENDERER
	.debugDrawLines = LUNA_NULL_HANDLE,
	.debugDrawTriangles = LUNA_NULL_HANDLE,
#endif
};
#define MAX_UI_QUADS_INIT 8192 // TODO: Ensure this is a good value for GGUI
#define MAX_MAP_TRIANGLES_PER_MAT_INIT 1024
#define MAX_MAP_MATERIALS_INIT 16
Buffers buffers = {
	.ui.vertices.allocatedSize = sizeof(UiVertex) * 4 * MAX_UI_QUADS_INIT,
	.ui.indices.allocatedSize = sizeof(uint32_t) * 6 * MAX_UI_QUADS_INIT,

	.map.vertices.allocatedSize = sizeof(MapVertex) * 3 *  MAX_MAP_TRIANGLES_PER_MAT_INIT * MAX_MAP_MATERIALS_INIT,
	.map.perMaterialData.allocatedSize = sizeof(uint32_t) * MAX_MAP_MATERIALS,
	.map.indices.allocatedSize = sizeof(uint32_t) * 3 *  MAX_MAP_TRIANGLES_PER_MAT_INIT * MAX_MAP_MATERIALS_INIT,
	.map.drawInfo.allocatedSize = sizeof(VkDrawIndexedIndirectCommand) * MAX_MAP_MATERIALS,

#ifdef JPH_DEBUG_RENDERER
	.debugDrawLines.vertices.allocatedSize = sizeof(DebugDrawVertex) * MAX_DEBUG_DRAW_VERTICES_INIT,
	.debugDrawTriangles.vertices.allocatedSize = sizeof(DebugDrawVertex) * MAX_DEBUG_DRAW_VERTICES_INIT,
#endif
};
#pragma endregion variables

VkResult CreateShaderModule(const char *path, const ShaderType shaderType, LunaShaderModule *shaderModule)
{
	Shader *shader = LoadShader(path);
	if (!shader)
	{
		return VK_ERROR_UNKNOWN;
	}
	assert(shader->platform == PLATFORM_VULKAN);
	assert(shader->type == shaderType);
	(void)shaderType;

	const LunaShaderModuleCreationInfo shaderModuleCreationInfo = {
		.creationInfoType = LUNA_SHADER_MODULE_CREATION_INFO_TYPE_SPIRV,
		.creationInfoUnion.spirv.size = sizeof(uint32_t) * shader->spirvLength,
		.creationInfoUnion.spirv.spirv = shader->spirv,
	};
	VulkanTestReturnResult(lunaCreateShaderModule(&shaderModuleCreationInfo, shaderModule),
						   "Failed to create shader module!");

	FreeShader(shader);
	return VK_SUCCESS;
}

inline uint32_t TextureIndex(const char *texture)
{
	return ImageIndex(LoadImage(texture));
}

inline uint32_t ImageIndex(const Image *image)
{
	const uint32_t index = imageAssetIdToIndexMap[image->id];
	if (index == -1u)
	{
		if (!LoadTexture(image))
		{
			// TODO: If loading a texture fails it can't fall back to OpenGL.
			//  There is no easy way to fix this with the current system, since the return value of this function is not
			//  checked but instead is just assumed to be valid. That rules out returning something like -1 on error.
			Error("Failed to load texture!");
		}
		return imageAssetIdToIndexMap[image->id];
	}
	return index;
}

// TODO: Make sure this doesn't need changes
void UpdateTransformMatrix(const Camera *camera)
{
	mat4 perspectiveMatrix;
	glm_perspective_lh_zo(glm_rad(camera->fov),
						  (float)swapChainExtent.width / (float)swapChainExtent.height,
						  NEAR_Z,
						  FAR_Z,
						  perspectiveMatrix);

	versor rotationQuat;
	QUAT_TO_VERSOR(camera->transform.rotation, rotationQuat);
	versor rotationOffset;
	glm_quatv(rotationOffset, GLM_PIf, GLM_XUP);
	glm_quat_mul(rotationQuat, rotationOffset, rotationQuat);

	vec3 cameraPosition = {camera->transform.position.x, camera->transform.position.y, camera->transform.position.z};
	mat4 viewMatrix;
	glm_quat_look(cameraPosition, rotationQuat, viewMatrix);

	glm_mat4_mul(perspectiveMatrix, viewMatrix, pushConstants.transformMatrix);
}

// TODO: Update this
void UpdateViewModelMatrix(const Viewmodel *viewmodel)
{
	mat4 perspectiveMatrix;
	glm_perspective_lh_zo(glm_rad(VIEWMODEL_FOV),
						  (float)swapChainExtent.width / (float)swapChainExtent.height,
						  NEAR_Z,
						  FAR_Z,
						  perspectiveMatrix);

	mat4 translationMatrix = GLM_MAT4_IDENTITY_INIT;
	glm_translate(translationMatrix,
				  (vec3){viewmodel->transform.position.x,
						 -viewmodel->transform.position.y,
						 viewmodel->transform.position.z});

	// TODO rotation other than yaw
	mat4 rotationMatrix = GLM_MAT4_IDENTITY_INIT;
	glm_rotate(rotationMatrix,
			   JPH_Quat_GetRotationAngle(&viewmodel->transform.rotation, &Vector3_AxisY),
			   (vec3){0.0f, -1.0f, 0.0f});

	mat4 viewModelMatrix;
	glm_mat4_mul(translationMatrix, rotationMatrix, translationMatrix);
	glm_mat4_mul(perspectiveMatrix, translationMatrix, viewModelMatrix);

	// for (uint32_t i = 0; i < buffers.viewModel.drawCount; i++)
	// {
	// 	memcpy(buffers.viewModel.instanceDatas[i].transform, viewModelMatrix, sizeof(mat4));
	// }
	// const LunaBufferWriteInfo instanceDataBufferWriteInfo = {
	// 	.bytes = sizeof(ModelInstanceData) * buffers.viewModel.drawCount,
	// 	.data = buffers.viewModel.instanceDatas,
	// 	.stageFlags = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
	// };
	// lunaWriteDataToBuffer(buffers.viewModel.instanceDataBuffer, &instanceDataBufferWriteInfo);
}

void EnsureSpaceForUiElements(const size_t vertexCount, const size_t indexCount)
{
	if (buffers.ui.vertices.allocatedSize < buffers.ui.vertices.bytesUsed + sizeof(UiVertex) * vertexCount ||
		buffers.ui.indices.allocatedSize < buffers.ui.indices.bytesUsed + sizeof(uint32_t) * indexCount)
	{
		buffers.ui.vertices.allocatedSize += sizeof(UiVertex) * vertexCount * 16;
		buffers.ui.indices.allocatedSize += sizeof(uint32_t) * indexCount * 16;
		buffers.ui.shouldResize = true;

		UiVertex *newVertices = realloc(buffers.ui.vertices.data, buffers.ui.vertices.allocatedSize);
		CheckAlloc(newVertices);
		buffers.ui.vertices.data = newVertices;

		uint32_t *newIndices = realloc(buffers.ui.indices.data, buffers.ui.indices.allocatedSize);
		CheckAlloc(newIndices);
		buffers.ui.indices.data = newIndices;
	}
}

void DrawRectInternal(const float ndcStartX,
					  const float ndcStartY,
					  const float ndcEndX,
					  const float ndcEndY,
					  const float startU,
					  const float startV,
					  const float endU,
					  const float endV,
					  const Color *color,
					  const uint32_t textureIndex)
{
	const mat4 vertices = {
		{ndcEndX, ndcStartY, endU, startV},
		{ndcStartX, ndcStartY, startU, startV},
		{ndcStartX, ndcEndY, startU, endV},
		{ndcEndX, ndcEndY, endU, endV},
	};
	DrawQuadInternal(vertices, color, textureIndex);
}

void DrawQuadInternal(const mat4 vertices_posXY_uvZW, const Color *color, const uint32_t textureIndex)
{
	EnsureSpaceForUiElements(4, 6);

	UiVertex *vertices = buffers.ui.vertices.data + buffers.ui.vertices.bytesUsed;
	uint32_t *indices = buffers.ui.indices.data + buffers.ui.indices.bytesUsed;

	vertices[0] = (UiVertex){
		.x = vertices_posXY_uvZW[0][0],
		.y = vertices_posXY_uvZW[0][1],
		.u = vertices_posXY_uvZW[0][2],
		.v = vertices_posXY_uvZW[0][3],
		.r = color->r,
		.g = color->g,
		.b = color->b,
		.a = color->a,
		.textureIndex = textureIndex,
	};
	vertices[1] = (UiVertex){
		.x = vertices_posXY_uvZW[1][0],
		.y = vertices_posXY_uvZW[1][1],
		.u = vertices_posXY_uvZW[1][2],
		.v = vertices_posXY_uvZW[1][3],
		.r = color->r,
		.g = color->g,
		.b = color->b,
		.a = color->a,
		.textureIndex = textureIndex,
	};
	vertices[2] = (UiVertex){
		.x = vertices_posXY_uvZW[2][0],
		.y = vertices_posXY_uvZW[2][1],
		.u = vertices_posXY_uvZW[2][2],
		.v = vertices_posXY_uvZW[2][3],
		.r = color->r,
		.g = color->g,
		.b = color->b,
		.a = color->a,
		.textureIndex = textureIndex,
	};
	vertices[3] = (UiVertex){
		.x = vertices_posXY_uvZW[3][0],
		.y = vertices_posXY_uvZW[3][1],
		.u = vertices_posXY_uvZW[3][2],
		.v = vertices_posXY_uvZW[3][3],
		.r = color->r,
		.g = color->g,
		.b = color->b,
		.a = color->a,
		.textureIndex = textureIndex,
	};

	const size_t vertexOffset = buffers.ui.vertices.bytesUsed / sizeof(UiVertex);
	indices[0] = vertexOffset;
	indices[1] = vertexOffset + 1;
	indices[2] = vertexOffset + 2;
	indices[3] = vertexOffset;
	indices[4] = vertexOffset + 2;
	indices[5] = vertexOffset + 3;

	buffers.ui.vertices.bytesUsed += sizeof(UiVertex) * 4;
	buffers.ui.indices.bytesUsed += sizeof(uint32_t) * 6;
}
