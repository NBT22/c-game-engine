//
// Created by droc101 on 4/22/2024.
//

#include "GMenuState.h"
#include <stdio.h>
#include "../../../Structs/Vector2.h"
#include "../Helpers/CommonAssets.h"
#include "../Helpers/Core/AssetReader.h"
#include "../Helpers/Graphics/Drawing.h"
#include "../Helpers/Graphics/Font.h"
#include "../Helpers/Graphics/RenderingHelpers.h"
#include "../Structs/GlobalState.h"
#include "../Structs/UI/Controls/Button.h"
#include "../Structs/UI/Controls/TextBox.h"
#include "../Structs/UI/UiStack.h"
#include "GLevelSelectState.h"
#include "GOptionsState.h"

UiStack *menuStack = NULL;

void StartGame()
{
#ifdef USE_LEVEL_SELECT
	GLevelSelectStateSet();
#else
	GMainStateSet();
#endif
}

void QuitGame()
{
	GetState()->requestExit = true;
}

void OpenOptions()
{
	GOptionsStateSet(false);
}

void GMenuStateUpdate(GlobalState * /*State*/) {}

void GMenuStateRender(GlobalState * /*State*/)
{
	RenderMenuBackground();

	// draw the logo
	SDL_Rect logoRect;
	logoRect.x = (WindowWidth() - 480) / 2;
	logoRect.y = 32;
	logoRect.w = 480;
	logoRect.h = 320;
	DrawTexture(v2((float)logoRect.x, (float)logoRect.y),
				v2((float)logoRect.w, (float)logoRect.h),
				TEXTURE("interface_menu_logo"));

#ifdef BUILDSTYLE_DEBUG
	FontDrawString(v2(20, 200), "DEBUG BUILD", 16, 0xFF00FF00, smallFont);
#endif

	// draw version and copyright info
	char buffer[256];
	sprintf(buffer, "Engine %s\n%s", VERSION, COPYRIGHT);
	DrawTextAligned(buffer,
					16,
					0xFF000000,
					v2(WindowWidthFloat() - 208, WindowHeightFloat() - 208),
					v2(200, 200),
					FONT_HALIGN_RIGHT,
					FONT_VALIGN_BOTTOM,
					smallFont);
	DrawTextAligned(buffer,
					16,
					0xFFa0a0a0,
					v2(WindowWidthFloat() - 210, WindowHeightFloat() - 210),
					v2(200, 200),
					FONT_HALIGN_RIGHT,
					FONT_VALIGN_BOTTOM,
					smallFont);

	ProcessUiStack(menuStack);
	DrawUiStack(menuStack);
}

void GMenuStateSet()
{
	if (menuStack == NULL)
	{
		menuStack = CreateUiStack();
		float opY = 80;
		const float opSpacing = 50;

		UiStackPush(menuStack, CreateButtonControl(v2(0, opY), v2(480, 40), "Start", StartGame, MIDDLE_CENTER));
		opY += opSpacing;
		UiStackPush(menuStack, CreateButtonControl(v2(0, opY), v2(480, 40), "Options", OpenOptions, MIDDLE_CENTER));
		opY += opSpacing;
		UiStackPush(menuStack, CreateButtonControl(v2(0, opY), v2(480, 40), "Quit", QuitGame, MIDDLE_CENTER));
		opY += opSpacing;

		Control *t = CreateTextBoxControl("test", v2(0, opY), v2(480, 40), MIDDLE_CENTER, 32, NULL);
		UiStackPush(menuStack, t);
	}
	UiStackResetFocus(menuStack);
	StopMusic();

	SetStateCallbacks(GMenuStateUpdate,
					  NULL,
					  MENU_STATE,
					  GMenuStateRender); // Fixed update is not needed for this state
}

void GMenuStateDestroy()
{
	if (menuStack != NULL)
	{
		DestroyUiStack(menuStack);
		menuStack = NULL;
	}
}
