//
// Created by droc101 on 11/7/2024.
//

#include "Door.h"
#include <box2d/box2d.h>
#include <box2d/types.h>

#include "../Helpers/Collision.h"
#include "../Helpers/Core/AssetReader.h"
#include "../Helpers/Core/Error.h"
#include "../Helpers/Core/Logging.h"
#include "../Structs/GlobalState.h"
#include "../Structs/Vector2.h"
#include "../Structs/Wall.h"

typedef enum
{
	DOOR_CLOSED,
	DOOR_OPENING,
	DOOR_OPEN,
	DOOR_CLOSING
} DoorState;

#define DOOR_OUTPUT_CLOSING 2
#define DOOR_OUTPUT_OPENING 3
#define DOOR_OUTPUT_FULLY_CLOSED 4
#define DOOR_OUTPUT_FULLY_OPEN 5

#define DOOR_INPUT_OPEN 1
#define DOOR_INPUT_CLOSE 2

typedef struct DoorData
{
	DoorState state;
	bool playerColliding;
	double animationTime;
	b2ShapeId sensorId;
	Vector2 spawnPosition;
} DoorData;

void DoorSetState(const Actor *door, const DoorState state)
{
	DoorData *data = door->extra_data;
	data->state = state;
	data->animationTime = 0;
	if (state == DOOR_OPENING)
	{
		ActorFireOutput(door, DOOR_OUTPUT_OPENING, PARAM_NONE);
	} else if (state == DOOR_CLOSING)
	{
		ActorFireOutput(door, DOOR_OUTPUT_CLOSING, PARAM_NONE);
	} else if (state == DOOR_OPEN)
	{
		ActorFireOutput(door, DOOR_OUTPUT_FULLY_OPEN, PARAM_NONE);
	} else if (state == DOOR_CLOSED)
	{
		ActorFireOutput(door, DOOR_OUTPUT_FULLY_CLOSED, PARAM_NONE);
	}
}

void CreateDoorCollider(Actor *this, const b2WorldId worldId, const Vector2 wallEnd)
{
	b2BodyDef doorBodyDef = b2DefaultBodyDef();
	doorBodyDef.type = b2_kinematicBody;
	doorBodyDef.position = this->position;
	this->bodyId = b2CreateBody(worldId, &doorBodyDef);
	this->actorWall->bodyId = this->bodyId;
	const b2Segment doorShape = {
		.point2 = wallEnd,
	};
	b2ShapeDef doorShapeDef = b2DefaultShapeDef();
	doorShapeDef.friction = 0;
	doorShapeDef.filter.categoryBits = COLLISION_GROUP_ACTOR;
	b2CreateSegmentShape(this->bodyId, &doorShapeDef, &doorShape);
}

void CreateDoorSensor(Actor *this, const b2WorldId worldId)
{
	this->extra_data = calloc(1, sizeof(DoorData));
	CheckAlloc(this->extra_data);
	DoorData *data = this->extra_data;

	b2BodyDef sensorBodyDef = b2DefaultBodyDef();
	sensorBodyDef.type = b2_staticBody;
	sensorBodyDef.position = this->position;
	const b2BodyId sensorBody = b2CreateBody(worldId, &sensorBodyDef);
	const b2Circle sensorShape = {
		.radius = 1,
	};
	b2ShapeDef sensorShapeDef = b2DefaultShapeDef();
	sensorShapeDef.isSensor = true;
	sensorShapeDef.filter.categoryBits = COLLISION_GROUP_ACTOR;
	sensorShapeDef.filter.maskBits = COLLISION_GROUP_PLAYER;
	data->sensorId = b2CreateCircleShape(sensorBody, &sensorShapeDef, &sensorShape);
}

bool DoorSignalHandler(Actor *self, const Actor *sender, byte signal, const Param *param);

void DoorInit(Actor *this, const b2WorldId worldId)
{
	const Vector2 wallEnd = Vector2Normalize(Vector2FromAngle(this->rotation));
	this->actorWall = CreateWall((Vector2){0, 0}, wallEnd, TEXTURE("actor_door"), 1.0f, 0.0f);
	WallBake(this->actorWall);

	CreateDoorCollider(this, worldId, wallEnd);
	CreateDoorSensor(this, worldId);
	this->SignalHandler = DoorSignalHandler;
	this->showShadow = false;

	DoorData *data = this->extra_data; // Allocated in CreateDoorSensor
	data->state = DOOR_CLOSED;
	data->animationTime = 0;
	data->spawnPosition = this->position;
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
void DoorUpdate(Actor *this, const double delta)
{
	this->position = b2Body_GetPosition(this->bodyId);
	DoorData *data = this->extra_data;
	data->playerColliding = GetSensorState(GetState()->level->worldId, data->sensorId.index1, data->playerColliding);
	if (this->paramA)
	{
		data->playerColliding = false;
	}
	switch (data->state)
	{
		case DOOR_CLOSED:
			if (data->playerColliding)
			{
				b2Body_SetLinearVelocity(this->bodyId,
										 Vector2Normalize(Vector2Scale(Vector2FromAngle(this->rotation), -1)));
				DoorSetState(this, DOOR_OPENING);
			}
			break;
		case DOOR_OPEN:
			if (data->animationTime >= 1 && !data->playerColliding && !this->paramB)
			{
				b2Body_SetLinearVelocity(this->bodyId, Vector2Normalize(Vector2FromAngle(this->rotation)));
				DoorSetState(this, DOOR_CLOSING);
			}
			break;
		case DOOR_OPENING:
			if (data->animationTime >= 1)
			{
				b2Body_SetLinearVelocity(this->bodyId, v2s(0));
				b2Body_SetTransform(this->bodyId, Vector2Sub(data->spawnPosition, this->actorWall->b), b2MakeRot(0));
				DoorSetState(this, DOOR_OPEN);
			}
			break;
		case DOOR_CLOSING:
			if (data->playerColliding)
			{
				b2Body_SetLinearVelocity(this->bodyId,
										 Vector2Normalize(Vector2Scale(Vector2FromAngle(this->rotation), -1)));
				data->state = DOOR_OPENING; // Set manually in order to not reset data->animationTime
				data->animationTime = 1 - data->animationTime;
			} else if (data->animationTime >= 1)
			{
				b2Body_SetLinearVelocity(this->bodyId, v2s(0));
				b2Body_SetTransform(this->bodyId, data->spawnPosition, b2MakeRot(0));
				DoorSetState(this, DOOR_CLOSED);
			}
			break;
		default:
			LogWarning("Invalid door state: %d", data->state);
			break;
	}
	data->animationTime += delta / PHYSICS_TARGET_TPS;
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
void DoorDestroy(Actor *this)
{
	b2DestroyBody(this->bodyId);
	b2DestroyBody(b2Shape_GetBody(((DoorData *)this->extra_data)->sensorId));
	free(this->extra_data);
	free(this->actorWall);
}

bool DoorSignalHandler(Actor *self, const Actor *sender, byte signal, const Param *param)
{
	if (DefaultSignalHandler(self, sender, signal, param))
	{
		return true;
	}
	DoorData *data = self->extra_data;
	if (signal == DOOR_INPUT_OPEN && data->state == DOOR_CLOSED)
	{
		b2Body_SetLinearVelocity(self->bodyId, Vector2Normalize(Vector2Scale(Vector2FromAngle(self->rotation), -1)));
		DoorSetState(self, DOOR_OPENING);
		return true;
	}
	if (signal == DOOR_INPUT_CLOSE && data->state == DOOR_OPEN)
	{
		b2Body_SetLinearVelocity(self->bodyId, Vector2Normalize(Vector2FromAngle(self->rotation)));
		DoorSetState(self, DOOR_CLOSING);
		return true;
	}
	return false;
}
