//
// Created by droc101 on 4/22/2024.
//

#include <engine/assets/AssetReader.h>
#include <engine/assets/MapLoader.h>
#include <engine/graphics/RenderingHelpers.h>
#include <engine/helpers/Arguments.h>
#include <engine/physics/Physics.h>
#include <engine/structs/Camera.h>
#include <engine/structs/GlobalState.h>
#include <engine/structs/Item.h>
#include <engine/structs/List.h>
#include <engine/structs/Map.h>
#include <engine/structs/Options.h>
#include <engine/subsystem/Discord.h>
#include <engine/subsystem/Error.h>
#include <engine/subsystem/Logging.h>
#include <engine/subsystem/threads/PhysicsThread.h>
#include <SDL_mouse.h>
#include <SDL_stdinc.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_HEALTH 100
#define MAX_MAP_PATH_LENGTH 80

static GlobalState state;

void InitOptions()
{
	LogDebug("Loading options...\n");
	LoadOptions(&state.options);
}

void InitState()
{
	LogDebug("Initializing global state...\n");
	state.saveData = calloc(1, sizeof(SaveData));
	CheckAlloc(state.saveData);
	state.saveData->hp = 100;
	state.map = CreateMap(); // empty map so we don't segfault
	state.camera = calloc(1, sizeof(Camera));
	CheckAlloc(state.camera);
	state.camera->fov = GetState()->options.fov;
	state.rpcState = IN_MENUS;
	ListInit(state.saveData->items, Item);
}

inline GlobalState *GetState()
{
	return &state;
}

Item *GetItem()
{
	if (state.saveData->items.length != 0 && state.saveData->currentItem < state.saveData->items.length)
	{
		const Item asd = ListGet(state.saveData->items, state.saveData->currentItem, Item);
		return &ListGet(state.saveData->items, state.saveData->currentItem, Item);
	}
	return NULL;
}

void GiveItem(const ItemDefinition *definition, const bool switchToItem)
{
	for (size_t i = 0; i < state.saveData->items.length; i++)
	{
		if (ListGet(state.saveData->items, i, Item).definition == definition)
		{
			if (switchToItem)
			{
				SwitchToItem(definition);
			}
			break;
		}
	}
	Item item = {
		.definition = definition,
	};
	definition->Construct(&item);
	ListAdd(state.saveData->items, item);
	if (switchToItem)
	{
		SwitchToItem(definition);
	}
}

void SwitchToItem(const ItemDefinition *definition)
{
	for (size_t i = 0; i < state.saveData->items.length; i++)
	{
		Item *item = &ListGet(state.saveData->items, i, Item);
		if (item->definition == definition)
		{
			Item *previousItem = GetItem();
			if (previousItem)
			{
				previousItem->definition->SwitchFrom(previousItem, &state.map->viewmodel);
			}
			state.saveData->currentItem = i;
			if (state.map)
			{
				definition->SwitchTo(item, &state.map->viewmodel);
			}
			return;
		}
	}
	LogWarning("Was instructed to switch to an item that the player does not have!\n");
}

void NextItem()
{
	if (state.saveData->currentItem < state.saveData->items.length - 1)
	{
		SwitchToItem(ListGet(state.saveData->items, state.saveData->currentItem + 1, Item).definition);
	}
}

void PreviousItem()
{
	if (state.saveData->currentItem > 0)
	{
		SwitchToItem(ListGet(state.saveData->items, state.saveData->currentItem - 1, Item).definition);
	}
}

void SetStateCallbacks(const FrameUpdateFunction UpdateGame,
					   const FixedUpdateFunction FixedUpdateGame,
					   const GameStateId currentState,
					   const FrameRenderFunction RenderGame,
					   const SDL_bool enableRelativeMouseMode)
{
	state.UpdateGame = UpdateGame;
	state.currentState = currentState;
	state.RenderGame = RenderGame;
	PhysicsThreadSetFunction(FixedUpdateGame);
	DiscordUpdateRPC();
	if (!HasCliArg("--no-mouse-capture"))
	{
		SDL_SetRelativeMouseMode(enableRelativeMouseMode);
	}
}

void ChangeMap(Map *map)
{
	if (!map)
	{
		LogError("Cannot change to a NULL map. Something might have gone wrong while loading it.\n");
		return;
	}
	PhysicsThreadLockTickMutex();
	if (state.map)
	{
		DestroyMap(state.map);
	}
	state.map = map;
	LoadMapModels(map);
	// if (strncmp(map->music, "none", 4) != 0)
	// {
	// 	char musicPath[80];
	// 	snprintf(musicPath, sizeof(musicPath), SOUND("%s"), map->music);
	// 	ChangeMusic(musicPath);
	// } else
	// {
	// 	StopMusic();
	// }

	PhysicsThreadUnlockTickMutex();
}

void DestroyGlobalState()
{
	LogDebug("Cleaning up GlobalState...\n");
	SaveOptions(&state.options);
	DestroyMap(state.map);
	state.map = NULL;
	free(state.saveData);
	free(state.camera);

	LogDebug("Cleaning up game states...\n");
	PhysicsDestroyGlobal(&state);
}

bool ChangeMapByName(const char *name)
{
	LogInfo("Loading map \"%s\"\n", name);

	char mapPath[MAX_MAP_PATH_LENGTH];
	if (snprintf(mapPath, MAX_MAP_PATH_LENGTH, MAP("%s"), name) > MAX_MAP_PATH_LENGTH)
	{
		LogError("Failed to load map due to map name %s being too long\n", name);
		return false;
	}
	GetState()->saveData->blueCoins = 0;
	ChangeMap(LoadMap(mapPath));
	DiscordUpdateRPC();
	return true;
}
