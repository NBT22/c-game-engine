//
// Created by droc101 on 4/13/25.
//

#include "Trigger.h"
#include <box2d/box2d.h>
#include <box2d/types.h>

#include "../../Helpers/Collision.h"
#include "../../Helpers/Core/Error.h"
#include "../../Structs/Actor.h"
#include "../../Structs/GlobalState.h"
#include "../../Structs/Level.h"

#define TRIGGER_INPUT_FORCE_TRIGGER 1
#define TRIGGER_OUTPUT_TRIGGERED 2

bool TriggerSignalHandler(Actor *self, const Actor *sender, byte signal, const Param *param)
{
	if (DefaultSignalHandler(self, sender, signal, param))
	{
		return true;
	}
	if (signal == TRIGGER_INPUT_FORCE_TRIGGER)
	{
		ActorFireOutput(self, TRIGGER_OUTPUT_TRIGGERED, PARAM_NONE);
		return true;
	}
	return false;
}

void CreateTriggerSensor(Actor *trigger, const Vector2 position, const float rotation, const b2WorldId worldId)
{
	b2BodyDef sensorBodyDef = b2DefaultBodyDef();
	sensorBodyDef.type = b2_staticBody;
	sensorBodyDef.position = position;
	const b2BodyId bodyId = b2CreateBody(worldId, &sensorBodyDef);
	const b2Polygon sensorShape = b2MakeOffsetBox((float)trigger->paramA * 0.5f,
												  (float)trigger->paramB * 0.5f,
												  (Vector2){0, 0},
												  rotation);
	b2ShapeDef sensorShapeDef = b2DefaultShapeDef();
	sensorShapeDef.isSensor = true;
	sensorShapeDef.filter.categoryBits = COLLISION_GROUP_TRIGGER;
	sensorShapeDef.filter.maskBits = COLLISION_GROUP_PLAYER;
	*(b2ShapeId *)(trigger->extraData) = b2CreatePolygonShape(bodyId, &sensorShapeDef, &sensorShape);
	trigger->bodyId = bodyId;
}

void TriggerInit(Actor *this, const b2WorldId worldId)
{
	this->showShadow = false;
	this->extraData = malloc(sizeof(b2ShapeId));
	this->SignalHandler = TriggerSignalHandler;
	CheckAlloc(this->extraData);
	CreateTriggerSensor(this, this->position, this->rotation, worldId);
}

void TriggerUpdate(Actor *this, double /*delta*/)
{
	if (GetSensorState(GetState()->level->worldId, ((b2ShapeId *)this->extraData)->index1, false))
	{
		ActorFireOutput(this, TRIGGER_OUTPUT_TRIGGERED, PARAM_NONE); // 2 = trigger
		RemoveActor(this); // for now they are ALL one shot
	}
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
void TriggerDestroy(Actor *this)
{
	b2DestroyBody(this->bodyId);
	free(this->actorWall);
	free(this->extraData);
}
