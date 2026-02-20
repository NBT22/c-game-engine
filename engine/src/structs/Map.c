//
// Created by droc101 on 4/21/2024.
//

#include <engine/debug/JoltDebugRenderer.h>
#include <engine/graphics/Drawing.h>
#include <engine/physics/Physics.h>
#include <engine/structs/Actor.h>
#include <engine/structs/ActorWall.h>
#include <engine/structs/Camera.h>
#include <engine/structs/Color.h>
#include <engine/structs/GlobalState.h>
#include <engine/structs/Item.h>
#include <engine/structs/List.h>
#include <engine/structs/Map.h>
#include <engine/structs/Player.h>
#include <engine/structs/Vector2.h>
#include <engine/subsystem/Error.h>
#include <joltc/Physics/Body/BodyID.h>
#include <joltc/joltc.h>
#include <joltc/Physics/Body/BodyInterface.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Map *CreateMap(void)
{
	Map *map = calloc(1, sizeof(Map));
	CheckAlloc(map);
	ListInit(map->actors, Actor *);
	PhysicsInitMap(map);
	CreatePlayer(&map->player, map->physicsSystem);
	map->fogColor = COLOR(0xff000000);
	map->fogStart = 2000;
	map->fogEnd = 2500;
	map->lightColor = COLOR_WHITE;
	map->physicsTick = 0;
	map->changeFlags = 0;
	ListInit(map->namedActorNames, const char *);
	ListInit(map->namedActorPointers, Actor *);
	ListInit(map->joltBodies, JPH_BodyID);

	Item *item = GetItem();
	if (item)
	{
		item->definition->SwitchTo(item, &map->viewmodel);
	} else
	{
		map->viewmodel.enabled = false;
	}

	return map;
}

void DestroyMap(Map *map)
{
	for (size_t i = 0; i < map->actors.length; i++)
	{
		FreeActor(ListGet(map->actors, i, Actor *));
	}

	for (size_t i = 0; i < map->modelCount; i++)
	{
		const MapModel *model = map->models + i;
		free(model->vertices);
		free(model->indices);
	}
	free(map->models);
	map->models = NULL;

	free(map->skyTexture);
	free(map->discordRpcIcon);
	free(map->discordRpcName);

	JPH_BodyInterface *bodyInterface = JPH_PhysicsSystem_GetBodyInterface(map->physicsSystem);

	for (size_t i = 0; i < map->joltBodies.length; i++)
	{
		JPH_BodyInterface_RemoveAndDestroyBody(bodyInterface, ListGet(map->joltBodies, i, uint32_t));
	}
	ListFree(map->joltBodies);

	PhysicsDestroyMap(map, bodyInterface);

	ListAndContentsFree(map->namedActorNames);
	ListFree(map->namedActorPointers);
	ListFree(map->actors);
	free(map);
}

void AddActor(Actor *actor)
{
	ListAdd(GetState()->map->actors, actor);
}

void RemoveActor(Actor *actor)
{
	Map *map = GetState()->map;
	ActorFireOutput(actor, ACTOR_OUTPUT_KILLED, PARAM_NONE);

	// Remove the actor from the named actor lists if it's there
	const size_t nameIdx = ListFind(map->namedActorPointers, actor);
	if (nameIdx != SIZE_MAX)
	{
		free(ListGet(map->namedActorNames, nameIdx, char *));
		ListRemoveAt(map->namedActorNames, nameIdx);
		ListRemoveAt(map->namedActorPointers, nameIdx);
	}

	const size_t idx = ListFind(map->actors, actor);
	if (idx == SIZE_MAX)
	{
		return;
	}
	ListRemoveAt(map->actors, idx);
	FreeActor(actor);
}

void NameActor(Actor *actor, const char *name, Map *map)
{
	const char *tempName = strdup(name);
	ListAdd(map->namedActorNames, tempName);
	ListAdd(map->namedActorPointers, actor);
}

Actor *GetActorByName(const char *name, const Map *map)
{
	ListLock(map->namedActorNames);
	for (size_t i = 0; i < map->namedActorNames.length; i++)
	{
		if (strcmp(ListGet(map->namedActorNames, i, const char *), name) == 0)
		{
			Actor *actor = ListGet(map->namedActorPointers, i, Actor *);
			ListUnlock(map->namedActorNames);
			return actor;
		}
	}
	ListUnlock(map->namedActorNames);
	return NULL;
}

void GetActorsByName(const char *name, const Map *map, List *actors)
{
	ListInit(*actors, Actor *);
	ListLock(map->namedActorNames);
	for (size_t i = 0; i < map->namedActorNames.length; i++)
	{
		if (strcmp(ListGet(map->namedActorNames, i, const char *), name) == 0)
		{
			ListAdd(*actors, ListGet(map->namedActorPointers, i, Actor *));
		}
	}
	ListUnlock(map->namedActorNames);
}

void RenderMap(const Map *map, const Camera *camera)
{
	JoltDebugRendererDrawBodies(map->physicsSystem);
	RenderMap3D(map, camera);

	ListLock(map->actors);
	for (size_t i = 0; i < map->actors.length; i++)
	{
		Actor *actor = ListGet(map->actors, i, Actor *);
		actor->definition->RenderUi(actor);
	}
	ListUnlock(map->actors);
}
