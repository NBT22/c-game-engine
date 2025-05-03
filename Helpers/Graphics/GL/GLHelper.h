//
// Created by droc101 on 9/30/2024.
//

#ifndef GAME_GLHELPER_H
#define GAME_GLHELPER_H

#include <cglm/cglm.h>
#include "../Drawing.h"
#include "SDL.h"

/**
 * Set SDL_GL flags (this must be done before the SDL window is created)
 */
bool GL_PreInit();

/**
 * Initialize OpenGL
 */
bool GL_Init(SDL_Window *wnd);

/**
 * Clear the screen
 */
void GL_ClearScreen();

/**
 * Clear only the depth buffer
 */
void GL_ClearDepthOnly();

/**
 * Swap the buffers
 */
void GL_Swap();

/**
 * Destroy the GL state
 */
void GL_DestroyGL();

/**
 * Set the filter/repeat parameters for a texture
 * @param texture The texture name
 * @param linear Whether to use linear filtering
 * @param repeat Whether to repeat the texture
 */
void GL_SetTexParams(const char *texture, bool linear, bool repeat);

/**
 * Draw a rectangle
 * @param pos The position in pixels
 * @param size The size in pixels
 * @param color The color to draw
 */
void GL_DrawRect(Vector2 pos, Vector2 size, Color color);

/**
 * Draw a rectangle outline
 * @param pos The position in pixels
 * @param size The size in pixels
 * @param color The color to draw
 * @param thickness The thickness of the outline
 */
void GL_DrawRectOutline(Vector2 pos, Vector2 size, Color color, float thickness);

/**
 * Draw a line
 * @param start The start position in pixels
 * @param end The end position in pixels
 * @param color The color to draw
 * @param thickness The thickness of the line
 */
void GL_DrawLine(Vector2 start, Vector2 end, Color color, float thickness);

/**
 * Draw a texture in 2D
 * @param pos The position in pixels
 * @param size The size in pixels
 * @param texture The texture name
 */
void GL_DrawTexture(Vector2 pos, Vector2 size, const char *texture);

/**
 * Draw a texture in 2D with a color mod
 * @param pos The position in pixels
 * @param size The size in pixels
 * @param texture The texture name
 * @param color The modulate color
 */
void GL_DrawTextureMod(Vector2 pos, Vector2 size, const char *texture, Color color);

/**
 * Draw a texture region in 2D
 * @param pos The position in pixels
 * @param size The size in pixels
 * @param texture The texture name
 * @param region_start The start of the region in pixels
 * @param region_end The end of the region in pixels
 */
void GL_DrawTextureRegion(Vector2 pos, Vector2 size, const char *texture, Vector2 region_start, Vector2 region_end);

/**
 * Draw a texture region in 2D with a color mod
 * @param pos The position in pixels
 * @param size The size in pixels
 * @param texture The texture name
 * @param region_start The start of the region in pixels
 * @param region_end The end of the region in pixels
 * @param color The modulate color
 */
void GL_DrawTextureRegionMod(Vector2 pos,
							 Vector2 size,
							 const char *texture,
							 Vector2 region_start,
							 Vector2 region_end,
							 Color color);

/**
 * Set the clear color
 * @param color The color to clear the screen with
 * @note This does in fact clear the screen
 */
void GL_ClearColor(Color color);

/**
 * Draw a wall in 3D
 * @param w The wall to draw
 * @note This expects 3D mode to be enabled
 */
void GL_DrawWall(const Wall *w);

/**
 * Draw the floor in 3D
 * @param vp1 The start of the floor
 * @param vp2 The end of the floor
 * @param texture The texture name
 * @param height The height of the floor
 * @param shade The shade of the floor
 */
void GL_DrawFloor(Vector2 vp1, Vector2 vp2, const char *texture, float height, float shade);

/**
 * Draw a shadow sprite
 * @param vp1 The first vertex
 * @param vp2 The second vertex
 * @param mdl The model -> world matrix
 */
void GL_DrawShadow(Vector2 vp1, Vector2 vp2, const mat4 mdl);

/**
 * Update the viewport size
 */
void GL_UpdateViewportSize();

/**
 * Draw arrays using the ui_textured shader
 * @param vertices Vertex data [x, y, u, v] with UVs in NDC
 * @param indices Index data
 * @param quad_count The number of quads to draw
 * @param texture The texture name
 * @param color The modulate color
 */
void GL_DrawTexturedArrays(const float *vertices,
						   const uint *indices,
						   int quad_count,
						   const char *texture,
						   Color color);

/**
 * Draw arrays using the ui_colored shader
 * @param vertices Vertex data [x, y] with positions in NDC
 * @param indices Index data
 * @param quad_count The number of quads to draw
 * @param color The color to draw
 */
void GL_DrawColoredArrays(const float *vertices, const uint *indices, uint quad_count, Color color);

/**
 * Convert screen X to NDC
 * @param x X position in pixels
 * @return The NDC position
 */
#define GL_X_TO_NDC(x) ((float)(x) / WindowWidth() * 2.0f - 1.0f)

/**
 * Convert screen Y to NDC
 * @param y Y position in pixels
 * @return The NDC position
 */
#define GL_Y_TO_NDC(y) (1.0f - (float)(y) / WindowHeight() * 2.0f)

/**
 * Get the transformation matrix for a camera
 * @param cam The camera
 * @return A mat4 MODEL_VIEW_PROJECTION matrix of the camera (World space to screen space)
 */
mat4 *GL_GetMatrix(const Camera *cam);

/**
 * Get the transform matrix for the viewmodel/held item
 * @param out The destination matrix
 */
void GL_GetViewModelMatrix(mat4 *out);

/**
 * OpenGL code to render the 3D portion of a level
 * @param l The level to render
 * @param cam The camera to render with
 * @note - This does not render the sky
 * @note - This destroys the contents of the depth buffer
 */
void GL_RenderLevel(const Level *l, const Camera *cam);

/**
 * Render a 3D model
 * @param model The model to render
 * @param modelWorldMatrix The model -> world matrix
 * @param skin
 */
void GL_RenderModel(const ModelDefinition *model, const mat4 modelWorldMatrix, int skin);

#endif //GAME_GLHELPER_H
