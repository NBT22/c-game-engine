//
// Created by droc101 on 4/21/2024.
//

#include "Font.h"
#include <ctype.h>
#include <string.h>
#include "../../Structs/Vector2.h"
#include "../Core/Error.h"
#include "../Core/MathEx.h"
#include "RenderingHelpers.h"

Vector2 FontDrawString(const Vector2 pos, const char *str, const uint size, const uint color, const Font *font)
{
	const ulong stringLength = strlen(str);
	float *verts = calloc(stringLength, sizeof(float[4][4]));
	CheckAlloc(verts);
	uint *indices = calloc(stringLength, sizeof(uint[6]));
	CheckAlloc(indices);

	int x = (int)pos.x;
	int y = (int)pos.y;
	int i = 0;
	const double sizeMultiplier = (double)size / font->default_size;
	const int width = (int)(font->width * sizeMultiplier);
	const int quadHeight = (int)(font->texture_height * sizeMultiplier);
	const int baselineHeight = (int)(font->baseline * sizeMultiplier);
	const double uvPixel = 1.0 / font->image->width;
	while (str[i] != '\0')
	{
		const int fSize = (int)((font->char_widths[(int)str[i]] + font->char_spacing) * sizeMultiplier);

		if (str[i] == ' ')
		{
			i++;
			x += (int)((font->space_width + font->char_spacing) * sizeMultiplier);
			continue;
		}

		if (str[i] == '\n')
		{
			i++;
			x = (int)pos.x;
			y += (int)((baselineHeight + font->line_spacing) * sizeMultiplier);
			continue;
		}

		const Vector2 ndcPos = v2(X_TO_NDC((float)x), Y_TO_NDC((float)y));
		const Vector2 ndcPosEnd = v2(X_TO_NDC((float)(x + width)), Y_TO_NDC((float)(y + quadHeight)));
		const double charUVStart = (double)font->indices[(int)str[i]] / font->char_count;
		const double charUVEnd = (font->indices[(int)str[i]] + 1.0) / font->char_count - uvPixel;

		const mat4 quad = {
			{(float)ndcPos.x, (float)ndcPos.y, (float)charUVStart, 0},
			{(float)ndcPos.x, (float)ndcPosEnd.y, (float)charUVStart, 1},
			{(float)ndcPosEnd.x, (float)ndcPosEnd.y, (float)charUVEnd, 1},
			{(float)ndcPosEnd.x, (float)ndcPos.y, (float)charUVEnd, 0},
		};

		memcpy(verts + i * 16, quad, sizeof(quad));

		uint quadIndices[6] = {0, 1, 2, 0, 2, 3};
		for (int j = 0; j < 6; j++)
		{
			quadIndices[j] += i * 4;
		}

		memcpy(indices + i * 6, quadIndices, sizeof(quadIndices));

		x += fSize;
		i++;
	}

	BatchedQuadArray quads;
	quads.verts = verts;
	quads.indices = indices;
	quads.quad_count = (int)stringLength;
	DrawBatchedQuadsTextured(&quads, font->texture, color);

	free(verts);
	free(indices);

	return v2((float)(x + width), (float)(y + size)); // Return the bottom right corner of the text
}

inline Vector2 MeasureText(const char *str, const uint size, const Font *font)
{
	return MeasureTextNChars(str, size, font, strlen(str));
}

Vector2 MeasureTextNChars(const char *str, const uint size, const Font *font, const size_t n)
{
	int textWidth = 0;
	int textHeight = (int)size;
	int tempWidth = 0;
	const double sizeMultiplier = (double)size / font->default_size;
	for (int j = 0; j < n; j++)
	{
		const int fSize = (int)((font->char_widths[(int)str[j]] + font->char_spacing) * sizeMultiplier);
		tempWidth += fSize;
		if (str[j] == ' ')
		{
			tempWidth -= fSize;
			tempWidth += (int)((font->space_width + font->char_spacing) * sizeMultiplier);
		} else if (str[j + 1] == '\0')
		{
			tempWidth -= (int)(font->char_spacing * sizeMultiplier); // fix extra spacing at the end of the string
		} else if (str[j] == '\n')
		{
			tempWidth -= fSize;
			textWidth = max(textWidth, tempWidth);
			tempWidth = 0;
			textHeight += (int)((size + font->line_spacing) * sizeMultiplier);
		}
	}

	textWidth = max(textWidth, tempWidth);

	return v2((float)textWidth, (float)textHeight);
}

int StringLineCount(const char *str)
{
	int count = 1;
	for (int i = 0; i < strlen(str); i++)
	{
		if (str[i] == '\n')
		{
			count++;
		}
	}
	return count;
}

int MeasureLine(const char *str, const int line)
{
	int i = 0;
	int count = 0;
	while (str[i] != '\0')
	{
		if (str[i] == '\n')
		{
			count++;
		}
		if (count == line)
		{
			return i;
		}
		i++;
	}
	return i;
}

void TextGetLine(const char *str, const int line, char *out, size_t outBufferSize)
{
	int start = MeasureLine(str, line);

	// if the start is a newline, skip it
	if (str[start] == '\n')
	{
		start++;
	}

	const int end = MeasureLine(str, line + 1);

	// ensure start is less than end
	if (start > end)
	{
		out[0] = '\0';
		return;
	}

	size_t copySize = end - start;
	if (copySize > outBufferSize)
	{
		copySize = outBufferSize;
	}

	strncpy(out, str + start, copySize);
	out[end - start] = '\0';
}

void DrawTextAligned(const char *str,
					 const uint size,
					 const uint color,
					 const Vector2 rect_pos,
					 const Vector2 rect_size,
					 const FontHorizontalAlign h_align,
					 const FontVerticalAlign v_align,
					 const Font *font)
{
	const size_t stringLength = strlen(str);
	float *verts = calloc(stringLength, sizeof(float[4][4]));
	CheckAlloc(verts);
	uint *indices = calloc(stringLength, sizeof(uint[6]));
	CheckAlloc(indices);

	const double sizeMultiplier = (double)size / font->default_size;
	const int width = (int)(font->width * sizeMultiplier);
	const int quadHeight = (int)(font->texture_height * sizeMultiplier);
	const int baselineHeight = (int)(font->baseline * sizeMultiplier);
	const double uvPixel = 1.0 / font->image->width;
	int c = 0;

	const int lines = StringLineCount(str);
	int x;
	int y = (int)rect_pos.y;
	if (v_align == FONT_VALIGN_MIDDLE)
	{
		y += ((int)rect_size.y - lines * (int)size) / 2;
	} else if (v_align == FONT_VALIGN_BOTTOM)
	{
		y += (int)rect_size.y - lines * (int)size;
	}

	for (int i = 0; i < lines; i++)
	{
		char line[256];
		TextGetLine(str, i, line, 256);
		const Vector2 textSize = MeasureText(line, size, font);
		if (h_align == FONT_HALIGN_CENTER)
		{
			x = (int)(rect_pos.x + (rect_size.x - textSize.x) / 2);
		} else if (h_align == FONT_HALIGN_RIGHT)
		{
			x = (int)(rect_pos.x + rect_size.x - textSize.x);
		} else
		{
			x = (int)rect_pos.x;
		}
		int lx = x;
		int ly = y;
		int j = 0;
		while (line[j] != '\0')
		{
			const int fSize = (int)((font->char_widths[(int)line[j]] + font->char_spacing) * sizeMultiplier);

			if (line[j] == ' ')
			{
				j++;
				lx += (int)((font->space_width + font->char_spacing) * sizeMultiplier);
				continue;
			}

			const Vector2 ndcPos = v2(X_TO_NDC((float)lx), Y_TO_NDC((float)ly));
			const Vector2 ndcPosEnd = v2(X_TO_NDC((float)(lx + width)), Y_TO_NDC((float)(ly + quadHeight)));
			const double charUVStart = (double)font->indices[(int)line[j]] / font->char_count;
			const double charUVEnd = (font->indices[(int)line[j]] + 1.0) / font->char_count - uvPixel;

			const mat4 quad = {
				{(float)ndcPos.x, (float)ndcPos.y, (float)charUVStart, 0},
				{(float)ndcPos.x, (float)ndcPosEnd.y, (float)charUVStart, 1},
				{(float)ndcPosEnd.x, (float)ndcPosEnd.y, (float)charUVEnd, 1},
				{(float)ndcPosEnd.x, (float)ndcPos.y, (float)charUVEnd, 0},
			};

			memcpy(verts + (c+j) * 16, quad, sizeof(quad));

			uint quadIndices[6] = {0, 1, 2, 0, 2, 3};
			for (int k = 0; k < 6; k++)
			{
				quadIndices[k] += (c+j) * 4;
			}

			memcpy(indices + (c+j) * 6, quadIndices, sizeof(quadIndices));

			lx += fSize;
			j++;
		}
		c += (int)strlen(line);
		y += (int)(size + font->line_spacing);
	}

	BatchedQuadArray quads;
	quads.verts = verts;
	quads.indices = indices;
	quads.quad_count = (int)stringLength;
	DrawBatchedQuadsTextured(&quads, font->texture, color);

	free(verts);
	free(indices);
}
