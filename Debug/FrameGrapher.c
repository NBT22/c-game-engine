//
// Created by droc101 on 4/24/24.
//

#include "FrameGrapher.h"
#include <limits.h>
#include <stdio.h>
#include "../Helpers/CommonAssets.h"
#include "../Helpers/Core/MathEx.h"
#include "../Helpers/Core/Timing.h"
#include "../Helpers/Graphics/Drawing.h"
#include "../Helpers/Graphics/Font.h"
#include "../Structs/Vector2.h"

double framerates[FRAMEGRAPH_HISTORY_SIZE] = {0};
long framegraphLastUpdateTime = LONG_MIN;

void FG_PushIntoArray(const double value)
{
	for (int i = 0; i < FRAMEGRAPH_HISTORY_SIZE - 1; i++)
	{
		framerates[i] = framerates[i + 1];
	}
	framerates[FRAMEGRAPH_HISTORY_SIZE - 1] = value;
}

void FrameGraphUpdate(const ulong ns)
{
	// If it's not time to update the graph, return
	if (GetTimeMs() - framegraphLastUpdateTime < FRAMEGRAPH_INTERVAL)
	{
		return;
	}

	FG_PushIntoArray(ns == 0 ? 1 : (double)ns);
	framegraphLastUpdateTime = (long)GetTimeMs();
}

void FrameGraphDraw()
{
#ifdef FRAMEGRAPH_ENABLE
#ifndef FRAMEGRAPH_FPS_ONLY
	// Draw a background for the graph
	DrawRect(0,
			 WindowHeight() - FRAMEGRAPH_THRESHOLD_GOOD * 2 * FRAMEGRAPH_V_SCALE - 20,
			 FRAMEGRAPH_H_SCALE * FRAMEGRAPH_HISTORY_SIZE + 10,
			 WindowHeight() - 10,
			 COLOR(0x80000000));

	// Draw a line at the bottom of the graph
	DrawLine(v2(10, WindowHeightFloat() - 10),
			 v2(FRAMEGRAPH_H_SCALE * FRAMEGRAPH_HISTORY_SIZE, WindowHeightFloat() - 10),
			 2,
			 COLOR(0x80808080));

	// Draw a line at the target framerate
	DrawLine(v2(10, WindowHeightFloat() - 10 - FRAMEGRAPH_THRESHOLD_GOOD * FRAMEGRAPH_V_SCALE),
			 v2(FRAMEGRAPH_H_SCALE * FRAMEGRAPH_HISTORY_SIZE,
				WindowHeightFloat() - 10 - FRAMEGRAPH_THRESHOLD_GOOD * FRAMEGRAPH_V_SCALE),
			 2,
			 COLOR(0x80808080));
	FontDrawString(v2(10, WindowHeightFloat() - 10 - FRAMEGRAPH_THRESHOLD_GOOD * FRAMEGRAPH_V_SCALE - 6),
				   "Target",
				   12,
				   COLOR(0xff00ffff),
				   smallFont);

	Color lineColor = COLOR(0);
	// Draw a line graph of all the frame rates/times
	for (int i = FRAMEGRAPH_HISTORY_SIZE - 2; i >= 0; i--)
	{
		if (framerates[i] == 0)
		{
			break;
		}

		const double ns = framerates[i];
		const double nextNs = framerates[i + 1];

		double f = 1000000000.0 / ns;
		double nextF = 1000000000.0 / nextNs;

#ifdef FRAMEGRAPH_SHOW_LINEAR_TIME_GRAPH
		double nsRemapped = remap(ns, 0, FRAMEGRAPH_NSPF, 0, FRAMEGRAPH_THRESHOLD_GOOD);
		double nextNsRemapped = remap(nextNs, 0, FRAMEGRAPH_NSPF, 0, FRAMEGRAPH_THRESHOLD_GOOD);
#endif


#ifdef FRAMEGRAPH_ENABLE_CAPPING
		if (f > FRAMEGRAPH_THRESHOLD_GOOD * 2)
		{
			f = FRAMEGRAPH_THRESHOLD_GOOD * 2;
		}
		if (nextF > FRAMEGRAPH_THRESHOLD_GOOD * 2)
		{
			nextF = FRAMEGRAPH_THRESHOLD_GOOD * 2;
		}
#ifdef FRAMEGRAPH_SHOW_LINEAR_TIME_GRAPH
		if (nsRemapped > FRAMEGRAPH_THRESHOLD_GOOD * 2)
		{
			nsRemapped = FRAMEGRAPH_THRESHOLD_GOOD * 2;
		}
		if (nextNsRemapped > FRAMEGRAPH_THRESHOLD_GOOD * 2)
		{
			nextNsRemapped = FRAMEGRAPH_THRESHOLD_GOOD * 2;
		}
#endif
#endif


		// first line for fps
		const double x1 = i * FRAMEGRAPH_H_SCALE + 10;
		double y1 = WindowHeight() - f * FRAMEGRAPH_V_SCALE - 10;
		const double x2 = (i + 1) * FRAMEGRAPH_H_SCALE + 10;
		double y2 = WindowHeight() - nextF * FRAMEGRAPH_V_SCALE - 10;

		if (f > FRAMEGRAPH_THRESHOLD_GOOD)
		{
			lineColor = COLOR(0xff00ff00);
		} else if (f < FRAMEGRAPH_THRESHOLD_BAD)
		{
			lineColor = COLOR(0xffff0000);
		} else
		{
			lineColor = COLOR(0xffff8000);
		}
		DrawLine(v2((float)x1, (float)y1), v2((float)x2, (float)y2), 2, lineColor);
#ifdef FRAMEGRAPH_SHOW_LINEAR_TIME_GRAPH
		// 2nd line for frame time
		y1 = (double)WindowHeight() - nsRemapped * FRAMEGRAPH_V_SCALE - 10;
		y2 = (double)WindowHeight() - nextNsRemapped * FRAMEGRAPH_V_SCALE - 10;
		lineColor.a = 0.5f;
		DrawLine(v2((float)x1, (float)y1), v2((float)x2, (float)y2), 2, lineColor);
#endif
	}
#else
	uint lineColor;
#endif
	const double currentNs = framerates[FRAMEGRAPH_HISTORY_SIZE - 1];
	const double currentF = 1000000000.0 / currentNs;
	const double currentMs = currentNs / 1000000.0;

	if (currentF > FRAMEGRAPH_THRESHOLD_GOOD)
	{
		lineColor = COLOR(0xff00ff00);
	} else if (currentF < FRAMEGRAPH_THRESHOLD_BAD)
	{
		lineColor = COLOR(0xffff0000);
	} else
	{
		lineColor = COLOR(0xffff8000);
	}

	// Draw the current framerate
	char fps[40];
	sprintf(fps, "FPS: %.2f\nMS: %2.2f", currentF, currentMs);
	FontDrawString(v2(12, WindowHeightFloat() - 8 - 38), fps, 16, COLOR_BLACK, smallFont);
	FontDrawString(v2(10, WindowHeightFloat() - 10 - 38), fps, 16, lineColor, smallFont);

#endif
}
