//
// Created by droc101 on 9/30/2024.
//

#include "GLHelper.h"
#include <cglm/cglm.h>
#include "../../../Structs/GlobalState.h"
#include "../../../Structs/Vector2.h"
#include "../../CommonAssets.h"
#include "../../Core/AssetReader.h"
#include "../../Core/Error.h"
#include "../../Core/Logging.h"
#include "../RenderingHelpers.h"
#include "GLInternal.h"

typedef struct __attribute__((aligned(16))) GL_SharedUniforms
{
	mat4 worldViewMatrix;
	vec3 fogColor;
	float fogStart;
	float fogEnd;
	float cameraYaw;
} GL_SharedUniforms;

SDL_GLContext ctx;

GL_Shader *uiTexturedShader;
GL_Shader *uiColoredShader;
GL_Shader *wallShader;
GL_Shader *floorAndCeilingShader;
GL_Shader *shadowShader;
GL_Shader *skyShader;
GL_Shader *modelUnshadedShader;
GL_Shader *modelShadedShader;

GL_Buffer *glBuffer;

GLuint glTextures[GL_MAX_TEXTURE_SLOTS];
int glNextFreeSlot = 0;
int glAssetTextureMap[MAX_TEXTURES];
char glLastError[512];

GL_ModelBuffers *glModels[MAX_MODELS];

GLuint sharedUniformBuffer;

#pragma region Shader Variable Locations

GLint floorTextureLoc;
GLint floorShadeLoc;
GLint floorHeightLoc;
GLint floorSharedUniformsLoc;

GLint hudColoredColorLoc; // TODO: confusing name -- location of the color uniform in the colored shader

GLint hudTexturedTextureLoc; // TODO: confusing name -- location of the texture uniform in the textured shader
GLint hudTexturedColorLoc;
GLint hudTexturedRegionLoc;

GLint shadowTextureLoc;
GLint shadowModelViewMatrixLoc;
GLint shadowSharedUniformsLoc;

GLint wallTextureLoc;
GLint wallModelWorldMatrixLoc;
GLint wallAngleLoc;
GLint wallSharedUniformsLoc;

#pragma endregion

void LoadShaderLocations()
{
	floorTextureLoc = glGetUniformLocation(floorAndCeilingShader->program, "alb");
	floorShadeLoc = glGetUniformLocation(floorAndCeilingShader->program, "shade");
	floorHeightLoc = glGetUniformLocation(floorAndCeilingShader->program, "height");
	floorSharedUniformsLoc = glGetUniformBlockIndex(floorAndCeilingShader->program, "SharedUniforms");

	hudColoredColorLoc = glGetUniformLocation(uiColoredShader->program, "col");

	hudTexturedTextureLoc = glGetUniformLocation(uiTexturedShader->program, "alb");
	hudTexturedColorLoc = glGetUniformLocation(uiTexturedShader->program, "col");
	hudTexturedRegionLoc = glGetUniformLocation(uiTexturedShader->program, "region");

	shadowTextureLoc = glGetUniformLocation(shadowShader->program, "alb");
	shadowModelViewMatrixLoc = glGetUniformLocation(shadowShader->program, "MODEL_WORLD_MATRIX");
	shadowSharedUniformsLoc = glGetUniformBlockIndex(shadowShader->program, "SharedUniforms");

	wallTextureLoc = glGetUniformLocation(wallShader->program, "alb");
	wallModelWorldMatrixLoc = glGetUniformLocation(wallShader->program, "MODEL_WORLD_MATRIX");
	wallAngleLoc = glGetUniformLocation(wallShader->program, "wall_angle");
	wallSharedUniformsLoc = glGetUniformBlockIndex(wallShader->program, "SharedUniforms");
}

void GL_Error(const char *error)
{
	LogError("OpenGL Error: %s\n", error);
	strcpy(glLastError, error);
}

bool GL_PreInit()
{
	const bool msaaEnabled = GetState()->options.msaa != MSAA_NONE;
	if (SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, msaaEnabled) != 0)
	{
		LogError("Failed to set MSAA buffers attribute: %s\n", SDL_GetError());
	}
	if (msaaEnabled)
	{
		int mssaValue = 0;
		switch (GetState()->options.msaa)
		{
			case MSAA_2X:
				mssaValue = 2;
				break;
			case MSAA_4X:
				mssaValue = 4;
				break;
			case MSAA_8X:
				mssaValue = 8;
				break;
			default:
				GL_Error("Invalid MSAA value!");
				return false;
		}
		if (SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, mssaValue) != 0)
		{
			LogError("Failed to set MSAA samples attribute: %s\n", SDL_GetError());
		}
	}
	TestSDLFunction(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3),
					"Failed to set OpenGL major version",
					"Failed to start OpenGL");
	TestSDLFunction(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3),
					"Failed to set OpenGL minor version",
					"Failed to start OpenGL");
	TestSDLFunction(SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1),
					"Failed to set OpenGL accelerated visual",
					"Failed to start OpenGL");
	TestSDLFunction(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE),
					"Failed to set OpenGL profile",
					"Failed to start OpenGL");
	TestSDLFunction(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1),
					"Failed to set OpenGL double buffer",
					"Failed to start OpenGL");

	memset(glAssetTextureMap, -1, MAX_TEXTURES * sizeof(int));
	memset(glTextures, 0, sizeof(glTextures));

	return true;
}

bool GL_Init(SDL_Window *wnd)
{
	LogInfo("Initializing OpenGL\n");

	ctx = SDL_GL_CreateContext(wnd);
	if (ctx == NULL)
	{
		LogError("SDL_GL_CreateContext Error: %s\n", SDL_GetError());
		GL_Error("Failed to create OpenGL context");
		return false;
	}

	TestSDLFunction_NonFatal(SDL_GL_SetSwapInterval(GetState()->options.vsync ? 1 : 0), "Failed to set VSync");

	// ReSharper disable once CppJoinDeclarationAndAssignment
	GLenum err;
	glewExperimental = GL_TRUE; // Please expose OpenGL 3.x+ interfaces
	err = glewInit();
	if (err != GLEW_OK)
	{
		SDL_GL_DeleteContext(ctx);
		GL_Error("Failed to start OpenGL. Your GPU or drivers may not support OpenGL 3.3.");
		return false;
	}

	// Ensure we have GL 3.3 or higher
	if (!GLEW_VERSION_3_3)
	{
		SDL_GL_DeleteContext(ctx);
		GL_Error("Failed to start OpenGL. Your GPU or drivers may not support OpenGL 3.3.");
		return false;
	}


#ifdef BUILDSTYLE_DEBUG
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(GL_DebugMessageCallback, NULL);
#endif

	uiTexturedShader = GL_ConstructShaderFromAssets(OGL_SHADER("GL_hud_textured_f"), OGL_SHADER("GL_hud_textured_v"));
	uiColoredShader = GL_ConstructShaderFromAssets(OGL_SHADER("GL_hud_color_f"), OGL_SHADER("GL_hud_color_v"));
	wallShader = GL_ConstructShaderFromAssets(OGL_SHADER("GL_wall_f"), OGL_SHADER("GL_wall_v"));
	floorAndCeilingShader = GL_ConstructShaderFromAssets(OGL_SHADER("GL_floor_f"), OGL_SHADER("GL_floor_v"));
	shadowShader = GL_ConstructShaderFromAssets(OGL_SHADER("GL_shadow_f"), OGL_SHADER("GL_shadow_v"));
	skyShader = GL_ConstructShaderFromAssets(OGL_SHADER("GL_sky_f"), OGL_SHADER("GL_sky_v"));
	modelShadedShader = GL_ConstructShaderFromAssets(OGL_SHADER("GL_model_shaded_f"), OGL_SHADER("GL_model_shaded_v"));
	modelUnshadedShader = GL_ConstructShaderFromAssets(OGL_SHADER("GL_model_unshaded_f"),
													   OGL_SHADER("GL_model_unshaded_v"));

	if (!uiTexturedShader ||
		!uiColoredShader ||
		!wallShader ||
		!floorAndCeilingShader ||
		!shadowShader ||
		!skyShader ||
		!modelShadedShader ||
		!modelUnshadedShader)
	{
		GL_Error("Failed to compile shaders");
		return false;
	}

	LoadShaderLocations();

	glBuffer = GL_ConstructBuffer();

	glGenBuffers(1, &sharedUniformBuffer);
	glBindBuffer(GL_UNIFORM_BUFFER, sharedUniformBuffer);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(GL_SharedUniforms), NULL, GL_STREAM_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);

	char *vendor = (char *)glGetString(GL_VENDOR);
	char *renderer = (char *)glGetString(GL_RENDERER);
	char *version = (char *)glGetString(GL_VERSION);
	char *shadingLanguage = (char *)glGetString(GL_SHADING_LANGUAGE_VERSION);

	LogInfo("OpenGL Initialized\n");
	LogInfo("OpenGL Vendor: %s\n", vendor);
	LogInfo("OpenGL Renderer: %s\n", renderer);
	LogInfo("OpenGL Version: %s\n", version);
	LogInfo("GLSL: %s\n", shadingLanguage);

	fflush(stdout);

	GL_Disable3D();

	return true;
}

GL_Shader *GL_ConstructShaderFromAssets(const char *fsh, const char *vsh)
{
	const Asset *fragmentSource = DecompressAsset(fsh);
	const Asset *vertexSource = DecompressAsset(vsh);
	if (fragmentSource == NULL || vertexSource == NULL)
	{
		Error("Failed to load shaders!");
	}
	return GL_ConstructShader((char *)fragmentSource->data, (char *)vertexSource->data);
}

GL_Shader *GL_ConstructShader(const char *fsh, const char *vsh)
{
	GLint status;
	char errorBuffer[512];

	GL_Shader *shader = malloc(sizeof(GL_Shader));
	CheckAlloc(shader);

	shader->vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(shader->vertexShader, 1, (const GLchar *const *)&vsh, NULL);
	glCompileShader(shader->vertexShader);
	glGetShaderiv(shader->vertexShader, GL_COMPILE_STATUS, &status);
	if (status != GL_TRUE)
	{
		glGetShaderInfoLog(shader->vertexShader, sizeof(errorBuffer), NULL, errorBuffer);
		errorBuffer[sizeof(errorBuffer) - 1] = '\0';
		Error(errorBuffer);
	}

	shader->fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(shader->fragmentShader, 1, (const GLchar *const *)&fsh, NULL);
	glCompileShader(shader->fragmentShader);
	glGetShaderiv(shader->fragmentShader, GL_COMPILE_STATUS, &status);
	if (status != GL_TRUE)
	{
		glGetShaderInfoLog(shader->fragmentShader, sizeof(errorBuffer), NULL, errorBuffer);
		errorBuffer[sizeof(errorBuffer) - 1] = '\0';
		LogError(errorBuffer);
		free(shader);
		return NULL;
	}

	shader->program = glCreateProgram();
	glAttachShader(shader->program, shader->vertexShader);
	glAttachShader(shader->program, shader->fragmentShader);
	glBindFragDataLocation(shader->program, 0, "COLOR");
	glLinkProgram(shader->program);

	glGetProgramiv(shader->program, GL_LINK_STATUS, &status);
	if (status != GL_TRUE)
	{
		glGetProgramInfoLog(shader->program, sizeof(errorBuffer), NULL, errorBuffer);
		errorBuffer[sizeof(errorBuffer) - 1] = '\0';
		LogError(errorBuffer);
		free(shader);
		return NULL;
	}

	return shader;
}

void GL_DestroyShader(GL_Shader *shd)
{
	glDeleteShader(shd->vertexShader);
	glDeleteShader(shd->fragmentShader);
	glDeleteProgram(shd->program);
	free(shd);
	shd = NULL;
}

GL_Buffer *GL_ConstructBuffer()
{
	GL_Buffer *buffer = malloc(sizeof(GL_Buffer));
	CheckAlloc(buffer);

	glGenVertexArrays(1, &buffer->vertexArrayObject);
	glGenBuffers(1, &buffer->vertexBufferObject);
	glGenBuffers(1, &buffer->elementBufferObject);

	return buffer;
}

void GL_DestroyBuffer(GL_Buffer *buffer)
{
	glDeleteVertexArrays(1, &buffer->vertexArrayObject);
	glDeleteBuffers(1, &buffer->vertexBufferObject);
	glDeleteBuffers(1, &buffer->elementBufferObject);
	free(buffer);
}

inline void GL_ClearScreen()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GL_ClearColor(const Color color)
{
	glClearColor(color.r, color.g, color.b, color.a);

	GL_ClearScreen();
}

inline void GL_ClearDepthOnly()
{
	glClear(GL_DEPTH_BUFFER_BIT);
}

inline void GL_Swap()
{
	SDL_GL_SwapWindow(GetGameWindow());
}

void GL_DestroyGL()
{
	GL_DestroyShader(uiTexturedShader);
	GL_DestroyShader(uiColoredShader);
	GL_DestroyShader(wallShader);
	GL_DestroyShader(floorAndCeilingShader);
	GL_DestroyShader(shadowShader);
	GL_DestroyShader(skyShader);
	GL_DestroyShader(modelShadedShader);
	GL_DestroyShader(modelUnshadedShader);
	glUseProgram(0);
	glDisableVertexAttribArray(0);
	GL_DestroyBuffer(glBuffer);
	glDeleteBuffers(1, &sharedUniformBuffer);
	for (int i = 0; i < MAX_TEXTURES; i++)
	{
		if (glTextures[i] != 0)
		{
			glDeleteTextures(1, &glTextures[i]);
		}
	}
	for (int i = 0; i < MAX_MODELS; i++)
	{
		if (glModels[i] != NULL)
		{
			for (int j = 0; j < glModels[i]->lodCount; j++)
			{
				GL_DestroyBuffer(glModels[i]->buffers[j]);
			}
			free(glModels[i]->buffers);
			free(glModels[i]);
		}
	}
	SDL_GL_DeleteContext(ctx);
}

void GL_DrawRect(const Vector2 pos, const Vector2 size, const Color color)
{
	glUseProgram(uiColoredShader->program);

	glUniform4fv(hudColoredColorLoc, 1, COLOR_TO_ARR(color));

	const Vector2 ndcStartPos = v2(GL_X_TO_NDC(pos.x), GL_Y_TO_NDC(pos.y));
	const Vector2 ncdEndPos = v2(GL_X_TO_NDC(pos.x + size.x), GL_Y_TO_NDC(pos.y + size.y));


	const float vertices[4][2] = {
		{(float)ndcStartPos.x, (float)ndcStartPos.y},
		{(float)ncdEndPos.x, (float)ndcStartPos.y},
		{(float)ncdEndPos.x, (float)ncdEndPos.y},
		{(float)ndcStartPos.x, (float)ncdEndPos.y},
	};

	const uint indices[] = {0, 1, 2, 0, 2, 3};

	glBindVertexArray(glBuffer->vertexArrayObject);

	glBindBuffer(GL_ARRAY_BUFFER, glBuffer->vertexBufferObject);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer->elementBufferObject);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STREAM_DRAW);

	const GLint posAttrLoc = glGetAttribLocation(uiColoredShader->program, "VERTEX");
	glVertexAttribPointer(posAttrLoc, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(posAttrLoc);

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
}

void GL_DrawRectOutline(const Vector2 pos, const Vector2 size, const Color color, const float thickness)
{
	if (thickness < 1.0f)
	{
		glEnable(GL_LINE_SMOOTH);
	} else
	{
		glDisable(GL_LINE_SMOOTH);
	}

	glLineWidth(thickness);

	glUseProgram(uiColoredShader->program);

	glUniform4fv(hudColoredColorLoc, 1, COLOR_TO_ARR(color));

	const Vector2 ndcStartPos = v2(GL_X_TO_NDC(pos.x), GL_Y_TO_NDC(pos.y));
	const Vector2 ndcEndPos = v2(GL_X_TO_NDC(pos.x + size.x), GL_Y_TO_NDC(pos.y + size.y));


	const float vertices[4][2] = {
		{(float)ndcStartPos.x, (float)ndcStartPos.y},
		{(float)ndcEndPos.x, (float)ndcStartPos.y},
		{(float)ndcEndPos.x, (float)ndcEndPos.y},
		{(float)ndcStartPos.x, (float)ndcEndPos.y},
	};

	const uint indices[] = {0, 1, 2, 3};

	glBindVertexArray(glBuffer->vertexArrayObject);

	glBindBuffer(GL_ARRAY_BUFFER, glBuffer->vertexBufferObject);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer->elementBufferObject);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STREAM_DRAW);

	const GLint posAttrLoc = glGetAttribLocation(uiColoredShader->program, "VERTEX");
	glVertexAttribPointer(posAttrLoc, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(posAttrLoc);

	glDrawElements(GL_LINE_LOOP, 4, GL_UNSIGNED_INT, NULL);
}

void GL_LoadTextureFromAsset(const char *texture)
{
	const Image *image = LoadImage(texture);

	// if the texture is already loaded, don't load it again
	if (glAssetTextureMap[image->id] != -1)
	{
		if (glIsTexture(glTextures[glAssetTextureMap[image->id]]))
		{
			glBindTexture(GL_TEXTURE_2D, glTextures[glAssetTextureMap[image->id]]);
			return;
		}
	}

	const int slot = GL_RegisterTexture(image->pixelData, (int)image->width, (int)image->height);

	glAssetTextureMap[image->id] = slot;
}

int GL_RegisterTexture(const byte *pixelData, const int width, const int height)
{
	const int slot = glNextFreeSlot;

	glGenTextures(1, &glTextures[slot]);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, glTextures[slot]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixelData);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, -1.5f);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	if (GetState()->options.mipmaps)
	{
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	} else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, pixelData);

	glNextFreeSlot++;

	return slot;
}

void GL_SetTexParams(const char *texture, const bool linear, const bool repeat)
{
	GL_LoadTextureFromAsset(texture); // make sure the texture is loaded

	const Image *image = LoadImage(texture);

	const GLuint glTextureID = glTextures[glAssetTextureMap[image->id]];

	glBindTexture(GL_TEXTURE_2D, glTextureID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);

	if (GetState()->options.mipmaps)
	{
		glTexParameteri(GL_TEXTURE_2D,
						GL_TEXTURE_MIN_FILTER,
						linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
	} else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
	}
}

void GL_DrawTexture_Internal(const Vector2 pos,
							 const Vector2 size,
							 const char *texture,
							 const Color color,
							 const Vector2 regionStart,
							 const Vector2 regionEnd)
{
	glUseProgram(uiTexturedShader->program);

	GL_LoadTextureFromAsset(texture);

	glUniform4fv(hudTexturedColorLoc, 1, COLOR_TO_ARR(color));

	glUniform4f(hudTexturedRegionLoc,
				(GLfloat)regionStart.x,
				(GLfloat)regionStart.y,
				(GLfloat)regionEnd.x,
				(GLfloat)regionEnd.y);

	const Vector2 ndcStartPos = v2(GL_X_TO_NDC(pos.x), GL_Y_TO_NDC(pos.y));
	const Vector2 ndcEndPos = v2(GL_X_TO_NDC(pos.x + size.x), GL_Y_TO_NDC(pos.y + size.y));


	const float vertices[4][4] = {
		{(float)ndcStartPos.x, (float)ndcStartPos.y, 0.0f, 0.0f},
		{(float)ndcEndPos.x, (float)ndcStartPos.y, 1.0f, 0.0f},
		{(float)ndcEndPos.x, (float)ndcEndPos.y, 1.0f, 1.0f},
		{(float)ndcStartPos.x, (float)ndcEndPos.y, 0.0f, 1.0f},
	};

	const uint indices[] = {0, 1, 2, 0, 2, 3};

	glBindVertexArray(glBuffer->vertexArrayObject);

	glBindBuffer(GL_ARRAY_BUFFER, glBuffer->vertexBufferObject);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer->elementBufferObject);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STREAM_DRAW);

	const GLint posAttrLoc = glGetAttribLocation(uiTexturedShader->program, "VERTEX");
	glVertexAttribPointer(posAttrLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(posAttrLoc);

	const GLint texAttrLoc = glGetAttribLocation(uiTexturedShader->program, "VERTEX_UV");
	glVertexAttribPointer(texAttrLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
	glEnableVertexAttribArray(texAttrLoc);

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
}

inline void GL_DrawTexture(const Vector2 pos, const Vector2 size, const char *texture)
{
	GL_DrawTexture_Internal(pos, size, texture, COLOR_WHITE, v2(-1, 0), v2s(0));
}

inline void GL_DrawTextureMod(const Vector2 pos, const Vector2 size, const char *texture, const Color color)
{
	GL_DrawTexture_Internal(pos, size, texture, color, v2(-1, 0), v2s(0));
}

inline void GL_DrawTextureRegion(const Vector2 pos,
								 const Vector2 size,
								 const char *texture,
								 const Vector2 regionStart,
								 const Vector2 regionEnd)
{
	GL_DrawTexture_Internal(pos, size, texture, COLOR_WHITE, regionStart, regionEnd);
}

inline void GL_DrawTextureRegionMod(const Vector2 pos,
									const Vector2 size,
									const char *texture,
									const Vector2 regionStart,
									const Vector2 regionEnd,
									const Color color)
{
	GL_DrawTexture_Internal(pos, size, texture, color, regionStart, regionEnd);
}

void GL_DrawLine(const Vector2 start, const Vector2 end, const Color color, const float thickness)
{
	if (thickness < 1.0f)
	{
		glEnable(GL_LINE_SMOOTH);
	} else
	{
		glDisable(GL_LINE_SMOOTH);
	}

	glUseProgram(uiColoredShader->program);

	glUniform4fv(hudColoredColorLoc, 1, COLOR_TO_ARR(color));

	const Vector2 ndcStartPos = v2(GL_X_TO_NDC(start.x), GL_Y_TO_NDC(start.y));
	const Vector2 ndcEndPos = v2(GL_X_TO_NDC(end.x), GL_Y_TO_NDC(end.y));

	// Calculate the 2 corner vertices of each point for a thick line
	const float vertices[2][2] = {
		{(float)ndcStartPos.x, (float)ndcStartPos.y},
		{(float)ndcEndPos.x, (float)ndcEndPos.y},
	};

	const uint indices[] = {0, 1};

	glBindVertexArray(glBuffer->vertexArrayObject);

	glBindBuffer(GL_ARRAY_BUFFER, glBuffer->vertexBufferObject);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer->elementBufferObject);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STREAM_DRAW);

	const GLint posAttrLoc = glGetAttribLocation(uiColoredShader->program, "VERTEX");
	glVertexAttribPointer(posAttrLoc, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(posAttrLoc);

	glLineWidth(thickness);
	glDrawElements(GL_LINES, 2, GL_UNSIGNED_INT, NULL);
}

void GL_SetLevelParams(mat4 *mvp, const Level *l)
{
	GL_SharedUniforms uniforms;
	glm_mat4_copy(mvp[0], uniforms.worldViewMatrix);
	glm_vec3_copy((vec3){(float)(l->fogColor >> 16 & 0xFF) / 255.0f,
						 (float)(l->fogColor >> 8 & 0xFF) / 255.0f,
						 (float)(l->fogColor & 0xFF) / 255.0f},
				  uniforms.fogColor);
	uniforms.cameraYaw = GetState()->cam->yaw;
	uniforms.fogStart = (float)l->fogStart;
	uniforms.fogEnd = (float)l->fogEnd;

	glBindBuffer(GL_UNIFORM_BUFFER, sharedUniformBuffer);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(GL_SharedUniforms), &uniforms, GL_STREAM_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void GL_DrawWall(const Wall *w)
{
	glUseProgram(wallShader->program);

	glBindBufferBase(GL_UNIFORM_BUFFER, wallSharedUniformsLoc, sharedUniformBuffer);

	GL_LoadTextureFromAsset(w->tex);

	glUniform1f(wallAngleLoc, (float)w->angle);

	float vertices[4][5] = {
		// X Y Z U V
		{(float)w->a.x, 0.5f * w->height, (float)w->a.y, 0.0f, 0.0f},
		{(float)w->b.x, 0.5f * w->height, (float)w->b.y, (float)w->length, 0.0f},
		{(float)w->b.x, -0.5f * w->height, (float)w->b.y, (float)w->length, 1.0f},
		{(float)w->a.x, -0.5f * w->height, (float)w->a.y, 0.0f, 1.0f},
	};

	const float uvOffset = w->uvOffset;
	const float uvScale = w->uvScale;
	for (int i = 0; i < 4; i++)
	{
		vertices[i][3] = vertices[i][3] * uvScale + uvOffset;
	}

	const uint indices[] = {0, 1, 2, 0, 2, 3};

	glBindVertexArray(glBuffer->vertexArrayObject);

	glBindBuffer(GL_ARRAY_BUFFER, glBuffer->vertexBufferObject);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer->elementBufferObject);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STREAM_DRAW);

	const GLint posAttrLoc = glGetAttribLocation(wallShader->program, "VERTEX");
	glVertexAttribPointer(posAttrLoc, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(posAttrLoc);

	const GLint texAttrLoc = glGetAttribLocation(wallShader->program, "VERTEX_UV");
	glVertexAttribPointer(texAttrLoc, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void *)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(texAttrLoc);

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
}

void GL_DrawActorWall(const Actor *actor)
{
	const Wall *wall = actor->actorWall;

	glUseProgram(wallShader->program);

	glBindBufferBase(GL_UNIFORM_BUFFER, wallSharedUniformsLoc, sharedUniformBuffer);

	GL_LoadTextureFromAsset(wall->tex);

	glUniform1f(wallAngleLoc, wall->angle);


	const float halfHeight = wall->height / 2.0f;
	const Vector2 startVertex = v2(actor->position.x + wall->a.x, actor->position.y + wall->a.y);
	const Vector2 endVertex = v2(actor->position.x + wall->b.x, actor->position.y + wall->b.y);
	const Vector2 startUV = v2(wall->uvOffset, 0);
	const Vector2 endUV = v2(wall->uvScale * wall->length + wall->uvOffset, 1);
	const float vertices[4][5] = {
		// X Y Z U V
		{
			startVertex.x,
			actor->yPosition + halfHeight,
			startVertex.y,
			startUV.x,
			startUV.y,
		},
		{
			endVertex.x,
			actor->yPosition + halfHeight,
			endVertex.y,
			endUV.x,
			startUV.y,
		},
		{
			endVertex.x,
			actor->yPosition - halfHeight,
			endVertex.y,
			endUV.x,
			endUV.y,
		},
		{
			startVertex.x,
			actor->yPosition - halfHeight,
			startVertex.y,
			startUV.x,
			endUV.y,
		},
	};

	const uint indices[] = {0, 1, 2, 0, 2, 3};

	glBindVertexArray(glBuffer->vertexArrayObject);

	glBindBuffer(GL_ARRAY_BUFFER, glBuffer->vertexBufferObject);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer->elementBufferObject);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STREAM_DRAW);

	const GLint posAttrLoc = glGetAttribLocation(wallShader->program, "VERTEX");
	glVertexAttribPointer(posAttrLoc, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(posAttrLoc);

	const GLint texAttrLoc = glGetAttribLocation(wallShader->program, "VERTEX_UV");
	glVertexAttribPointer(texAttrLoc, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void *)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(texAttrLoc);

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
}

void GL_DrawFloor(const Vector2 vp1, const Vector2 vp2, const char *texture, const float height, const float shade)
{
	glUseProgram(floorAndCeilingShader->program);

	glBindBufferBase(GL_UNIFORM_BUFFER, floorSharedUniformsLoc, sharedUniformBuffer);

	GL_LoadTextureFromAsset(texture);

	glUniform1f(floorHeightLoc, height);
	glUniform1f(floorShadeLoc, shade);

	const float vertices[4][2] = {
		// X Z
		{(float)vp1.x, (float)vp1.y},
		{(float)vp2.x, (float)vp1.y},
		{(float)vp2.x, (float)vp2.y},
		{(float)vp1.x, (float)vp2.y},
	};

	const uint indices[] = {0, 1, 2, 0, 2, 3};

	glBindVertexArray(glBuffer->vertexArrayObject);

	glBindBuffer(GL_ARRAY_BUFFER, glBuffer->vertexBufferObject);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer->elementBufferObject);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STREAM_DRAW);

	const GLint posAttrLoc = glGetAttribLocation(floorAndCeilingShader->program, "VERTEX");
	glVertexAttribPointer(posAttrLoc, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(posAttrLoc);

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
}

void GL_DrawShadow(const Vector2 vp1, const Vector2 vp2, const mat4 mdl)
{
	glUseProgram(shadowShader->program);

	GL_LoadTextureFromAsset(TEXTURE("vfx_shadow"));

	glBindBufferBase(GL_UNIFORM_BUFFER, shadowSharedUniformsLoc, sharedUniformBuffer);

	// model -> world
	glUniformMatrix4fv(shadowModelViewMatrixLoc, 1, GL_FALSE, mdl[0]);

	const float vertices[4][2] = {
		// X Z
		{(float)vp1.x, (float)vp1.y},
		{(float)vp2.x, (float)vp1.y},
		{(float)vp2.x, (float)vp2.y},
		{(float)vp1.x, (float)vp2.y},
	};

	const uint indices[] = {0, 1, 2, 0, 2, 3};

	glBindVertexArray(glBuffer->vertexArrayObject);

	glBindBuffer(GL_ARRAY_BUFFER, glBuffer->vertexBufferObject);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer->elementBufferObject);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STREAM_DRAW);

	const GLint posAttrLoc = glGetAttribLocation(shadowShader->program, "VERTEX");
	glVertexAttribPointer(posAttrLoc, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(posAttrLoc);

	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
}

inline void GL_Enable3D()
{
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_MULTISAMPLE);
	glClear(GL_DEPTH_BUFFER_BIT);
}

inline void GL_Disable3D()
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_MULTISAMPLE);
	glClear(GL_DEPTH_BUFFER_BIT);
}

inline void GL_UpdateViewportSize()
{
	int vpWidth;
	int vpHeight;
	SDL_GL_GetDrawableSize(GetGameWindow(), &vpWidth, &vpHeight);
	glViewport(0, 0, vpWidth, vpHeight);
}

void GL_DrawColoredArrays(const float *vertices, const uint *indices, const uint quadCount, const Color color)
{
	glUseProgram(uiColoredShader->program);

	glUniform4fv(hudColoredColorLoc, 1, COLOR_TO_ARR(color));

	glBindVertexArray(glBuffer->vertexArrayObject);

	glBindBuffer(GL_ARRAY_BUFFER, glBuffer->vertexBufferObject);
	glBufferData(GL_ARRAY_BUFFER, (long)(quadCount * 16 * sizeof(float)), vertices, GL_STREAM_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer->elementBufferObject);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (long)(quadCount * 6 * sizeof(uint)), indices, GL_STREAM_DRAW);

	const GLint posAttrLoc = glGetAttribLocation(uiColoredShader->program, "VERTEX");
	glVertexAttribPointer(posAttrLoc, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(posAttrLoc);

	glDrawElements(GL_TRIANGLES, (int)(quadCount * 6), GL_UNSIGNED_INT, NULL);
}

void GL_DrawTexturedArrays(const float *vertices,
						   const uint *indices,
						   const int quadCount,
						   const char *texture,
						   const Color color)
{
	glUseProgram(uiTexturedShader->program);

	GL_LoadTextureFromAsset(texture);

	glUniform4fv(hudTexturedColorLoc, 1, COLOR_TO_ARR(color));

	glUniform4f(hudTexturedRegionLoc, -1, 0, 0, 0);

	glBindVertexArray(glBuffer->vertexArrayObject);

	glBindBuffer(GL_ARRAY_BUFFER, glBuffer->vertexBufferObject);
	glBufferData(GL_ARRAY_BUFFER, (long)(quadCount * 16 * sizeof(float)), vertices, GL_STREAM_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glBuffer->elementBufferObject);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (long)(quadCount * 6 * sizeof(uint)), indices, GL_STREAM_DRAW);

	const GLint posAttrLoc = glGetAttribLocation(uiTexturedShader->program, "VERTEX");
	glVertexAttribPointer(posAttrLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(posAttrLoc);

	const GLint texAttrLoc = glGetAttribLocation(uiTexturedShader->program, "VERTEX_UV");
	glVertexAttribPointer(texAttrLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
	glEnableVertexAttribArray(texAttrLoc);

	glDrawElements(GL_TRIANGLES, quadCount * 6, GL_UNSIGNED_INT, NULL);
}

mat4 *GL_GetMatrix(const Camera *cam)
{
	vec3 cameraPosition = {cam->x, cam->y, cam->z};
	const float aspectRatio = WindowWidthFloat() / WindowHeightFloat();

	mat4 identityMatrix = GLM_MAT4_IDENTITY_INIT;
	mat4 perspectiveMatrix = GLM_MAT4_ZERO_INIT;
	glm_perspective(glm_rad(cam->fov), aspectRatio, NEAR_Z, FAR_Z, perspectiveMatrix);

	vec3 lookAtPosition = {cosf(cam->yaw), 0, sinf(cam->yaw)};
	vec3 upVector = {0, 1, 0};

	// TODO: roll and pitch are messed up

	glm_vec3_rotate(lookAtPosition, cam->roll, (vec3){0, 0, 1}); // Roll
	glm_vec3_rotate(lookAtPosition, cam->pitch, (vec3){1, 0, 0}); // Pitch

	lookAtPosition[0] += cameraPosition[0];
	lookAtPosition[1] += cameraPosition[1];
	lookAtPosition[2] += cameraPosition[2];

	mat4 viewMatrix = GLM_MAT4_ZERO_INIT;
	glm_lookat(cameraPosition, lookAtPosition, upVector, viewMatrix);

	mat4 modelViewMatrix = GLM_MAT4_ZERO_INIT;
	glm_mat4_mul(viewMatrix, identityMatrix, modelViewMatrix);

	mat4 *modelViewProjectionMatrix = malloc(sizeof(mat4));
	CheckAlloc(modelViewProjectionMatrix);
	glm_mat4_mul(perspectiveMatrix, modelViewMatrix, *modelViewProjectionMatrix);

	return modelViewProjectionMatrix;
}

void GL_GetViewModelMatrix(mat4 *out)
{
	mat4 perspectiveMatrix = GLM_MAT4_ZERO_INIT;

	const float aspectRatio = WindowWidthFloat() / WindowHeightFloat();
	glm_mat4_identity(perspectiveMatrix);
	glm_perspective(glm_rad(70), aspectRatio, NEAR_Z, FAR_Z, perspectiveMatrix);

	mat4 translationMatrix = GLM_MAT4_IDENTITY_INIT;
	glm_translate(translationMatrix, (vec3){0.5f, -0.35f + ((float)GetState()->cameraY * 0.2f), 0});

	mat4 rotationMatrix = GLM_MAT4_IDENTITY_INIT;
	glm_rotate(rotationMatrix, glm_rad(5), (vec3){0, 1, 0});

	glm_mat4_mul(translationMatrix, rotationMatrix, translationMatrix);

	glm_mat4_mul(perspectiveMatrix, translationMatrix, *out);
}

void GL_RenderLevel(const Level *l, const Camera *cam)
{
	GL_Enable3D();

	//glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
	//glLineWidth(2);

	mat4 *worldViewMatrix = GL_GetMatrix(cam);
	mat4 skyModelWorldMatrix = GLM_MAT4_IDENTITY_INIT;
	glm_translated(skyModelWorldMatrix, (vec3){(float)l->player.pos.x, 0, (float)l->player.pos.y});

	const Vector2 floorStart = v2(l->player.pos.x - 100, l->player.pos.y - 100);
	const Vector2 floorEnd = v2(l->player.pos.x + 100, l->player.pos.y + 100);

	GL_SetLevelParams(worldViewMatrix, l);

	if (l->hasCeiling)
	{
		GL_DrawFloor(floorStart, floorEnd, l->ceilOrSkyTex, 0.5f, 0.8f);
	} else
	{
		GL_RenderModel(skyModel, skyModelWorldMatrix, 0);
		GL_ClearDepthOnly(); // prevent sky from clipping into walls
	}

	GL_DrawFloor(floorStart, floorEnd, l->floorTex, -0.5f, 1.0f);

	glDisable(GL_DEPTH_TEST);
	for (int i = 0; i < l->actors.length; i++)
	{
		const Actor *actor = ListGet(l->actors, i);
		if (actor->showShadow)
		{
			mat4 actorXfm = GLM_MAT4_IDENTITY_INIT;
			ActorTransformMatrix(actor, &actorXfm);
			// remove the rotation and y position from the actor matrix so the shadow draws correctly
			glm_rotate(actorXfm, (float)actor->rotation, (vec3){0, 1, 0});
			glm_translate(actorXfm, (vec3){0, -actor->yPosition, 0});
			GL_DrawShadow(v2s(-0.5f * actor->shadowSize), v2s(0.5f * actor->shadowSize), actorXfm);
		}
	}
	glEnable(GL_DEPTH_TEST);

	for (int i = 0; i < l->walls.length; i++)
	{
		GL_DrawWall(ListGet(l->walls, i));
	}

	for (int i = 0; i < l->actors.length; i++)
	{
		const Actor *actor = ListGet(l->actors, i);
		mat4 actorXfm = GLM_MAT4_IDENTITY_INIT;
		ActorTransformMatrix(actor, &actorXfm);
		if (actor->actorModel == NULL)
		{
			if (actor->actorWall == NULL)
			{
				continue;
			}
			GL_DrawActorWall(actor);
		} else
		{
			GL_RenderModel(actor->actorModel, actorXfm, actor->actorModelSkin);
		}
	}

	free(worldViewMatrix);

	glClear(GL_DEPTH_BUFFER_BIT);

	mat4 viewModelMatrix;
	GL_GetViewModelMatrix(&viewModelMatrix);

	GL_SharedUniforms uniforms;
	glm_mat4_copy(viewModelMatrix, uniforms.worldViewMatrix);
	uniforms.cameraYaw = 0;
	uniforms.fogStart = (float)1000;
	uniforms.fogEnd = (float)1001;

	glBindBuffer(GL_UNIFORM_BUFFER, sharedUniformBuffer);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(GL_SharedUniforms), &uniforms, GL_STREAM_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	GL_RenderModel(LoadModel(MODEL("model_eraser")), GLM_MAT4_IDENTITY, 0);

	//glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	GL_Disable3D();
}

void GL_LoadModel(const ModelDefinition *model, const int lod, const int material)
{
	if (glModels[model->id] != NULL)
	{
		const GL_ModelBuffers *modelBuffer = glModels[model->id];
		const GL_Buffer *lodBuffer = modelBuffer->buffers[lod];
		const GL_Buffer materialBuffer = lodBuffer[material];
		glBindVertexArray(materialBuffer.vertexArrayObject);
		glBindBuffer(GL_ARRAY_BUFFER, materialBuffer.vertexBufferObject);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, materialBuffer.elementBufferObject);
		return;
	}
	GL_ModelBuffers *buf = malloc(sizeof(GL_ModelBuffers));
	CheckAlloc(buf);
	buf->lodCount = model->lodCount;
	buf->materialCount = model->materialCount;
	buf->buffers = malloc(sizeof(void*) * model->lodCount);
	CheckAlloc(buf->buffers);

	for (int l = 0; l < buf->lodCount; l++)
	{
		buf->buffers[l] = malloc(sizeof(GL_Buffer) * model->materialCount);
		CheckAlloc(buf->buffers[l]);

		for (int m = 0; m < buf->materialCount; m++)
		{
			GL_Buffer *modelBuffer = &buf->buffers[l][m];
			glGenVertexArrays(1, &modelBuffer->vertexArrayObject);
			glGenBuffers(1, &modelBuffer->vertexBufferObject);
			glGenBuffers(1, &modelBuffer->elementBufferObject);

			glBindVertexArray(modelBuffer->vertexArrayObject);

			glBindBuffer(GL_ARRAY_BUFFER, modelBuffer->vertexBufferObject);
			glBufferData(GL_ARRAY_BUFFER, (long)(model->lods[l]->vertexCount * sizeof(float) * 8), model->lods[l]->vertexData, GL_STATIC_DRAW);

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, modelBuffer->elementBufferObject);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, (long)(model->lods[l]->indexCount[m] * sizeof(uint)), model->lods[l]->indexData[m], GL_STATIC_DRAW);
		}
	}

	glModels[model->id] = buf;
}

void GL_RenderModelPart(const ModelDefinition *model,
						const mat4 modelWorldMatrix,
						const int lod,
						const int material,
						const int skin)
{
	Material* skinMats = model->skins[skin];

	const ModelShader shader = skinMats[material].shader;

	GL_Shader *glShader;
	switch (shader)
	{
		case SHADER_SKY:
			glShader = skyShader;
			break;
		case SHADER_SHADED:
			glShader = modelShadedShader;
			break;
		case SHADER_UNSHADED:
			glShader = modelUnshadedShader;
			break;
		default:
			Error("Invalid shader for model drawing");
	}

	glUseProgram(glShader->program);

	if (shader == SHADER_SKY)
	{
		GL_LoadTextureFromAsset(GetState()->level->ceilOrSkyTex);
	} else
	{
		GL_LoadTextureFromAsset(skinMats[material].texture);
	}


	glBindBufferBase(GL_UNIFORM_BUFFER,
					 glGetUniformBlockIndex(glShader->program, "SharedUniforms"),
					 sharedUniformBuffer);

	glUniformMatrix4fv(glGetUniformLocation(glShader->program, "MODEL_WORLD_MATRIX"),
					   1,
					   GL_FALSE,
					   modelWorldMatrix[0]); // model -> world

	GL_LoadModel(model, lod, material);

	const GLint posAttrLoc = glGetAttribLocation(glShader->program, "VERTEX");
	glVertexAttribPointer(posAttrLoc, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(posAttrLoc);

	const GLint texAttrLoc = glGetAttribLocation(glShader->program, "VERTEX_UV");
	glVertexAttribPointer(texAttrLoc, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void *)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(texAttrLoc);

	if (shader == SHADER_SHADED) // other shaders do not take normals
	{
		const GLint normAttrLoc = glGetAttribLocation(glShader->program, "VERTEX_NORMAL");
		glVertexAttribPointer(normAttrLoc, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void *)(5 * sizeof(GLfloat)));
		glEnableVertexAttribArray(normAttrLoc);
	}

	if (shader != SHADER_SKY)
	{
		const GLint colUniformLocation = glGetUniformLocation(glShader->program, "albColor");
		glUniform4fv(colUniformLocation, 1, COLOR_TO_ARR(skinMats[material].color));
	}

	glDrawElements(GL_TRIANGLES, (int)model->lods[lod]->indexCount[material], GL_UNSIGNED_INT, NULL);
}

void GL_RenderModel(const ModelDefinition *model, const mat4 modelWorldMatrix, const int skin)
{
	int lod = 0;
	if (model->lodCount > 1)
	{
		const float distanceToCamera = glm_vec3_distance((vec3){modelWorldMatrix[3][0], modelWorldMatrix[3][1], modelWorldMatrix[3][2]},
												 (vec3){GetState()->cam->x, GetState()->cam->y, GetState()->cam->z});

		for (int i = model->lodCount - 1; i >= 0; i--)
		{
			if (distanceToCamera > model->lods[i]->distance)
			{
				lod = i;
				break;
			}
		}
	}

	for (int m = 0; m < model->materialCount; m++)
	{
		GL_RenderModelPart(model, modelWorldMatrix, lod, m, skin);
	}
}
