#pragma once
#include <pthread.h>
#include "../../main.h"
#include "../../io/chat/chat.h"
#include "../../util/lock_util.h"
#include "../positions.h"

typedef enum {

	ent_standing,
	ent_fall_flying,
	ent_sleeping,
	ent_spin_attack,
	ent_sneaking,
	ent_dying

} ent_pose_t;

typedef enum {

	ent_player

} ent_type_t;

typedef struct {

	pthread_mutex_t lock;

	wld_position_t position;
	wld_chunk_t* chunk;
	utl_doubly_linked_node_t* chunk_node;

	uint32_t id;

	cht_component_t custom_name;
	uint16_t air_ticks;
	ent_pose_t pose : 5;
	ent_type_t type : 1;
	bool_t on_fire : 1;
	bool_t crouching : 1;
	bool_t sprinting : 1;
	bool_t swimming : 1;
	bool_t invisible : 1;
	bool_t glowing : 1;
	bool_t flying_with_elytra : 1;
	bool_t custom_name_visible : 1;
	bool_t silent : 1;
	bool_t no_gravity : 1;
	bool_t on_ground : 1;
	uint8_t powder_snow_ticks;

} ent_entity_t;

typedef struct {

	ent_entity_t entity;

	wld_rotation_t rotation;

	float32_t health;

	uint8_t potion_effect_color;
	uint8_t arrows;
	uint8_t bee_stings;

	wld_block_position_t sleeping_bed;

	uint8_t hand : 1;
	bool_t hand_active : 1;
	bool_t riptide_spin_attack : 1;
	bool_t potion_effect_ambient : 1;
	bool_t sleeping_in_bed : 1;

} ent_living_entity_t;

typedef enum {

	ent_survival = 0,
	ent_creative = 1,
	ent_adventure = 2,
	ent_spectator = 3

} ent_gamemode_t;

typedef struct {

	ent_living_entity_t living_entity;
	float32_t additional_hearts;
	int32_t score;

	uint8_t held_item : 4;

	byte_t displayed_skin_parts;
	byte_t main_hand : 1;

	ent_gamemode_t gamemode : 2;

	// for parrots
	ent_living_entity_t* left_shoulder;
	ent_living_entity_t* right_shoulder;

} ent_player_t;

extern uint32_t ent_register_entity(ent_entity_t* entity);
extern ent_entity_t* ent_get_entity_by_id(uint32_t id);

/*
LOCK ENTITY BEFORE USING THIS FUNCTION
UNLOCK CHUNK BEFORE USING THIS FUNCTION
*/
static inline void ent_remove_chunk_l(ent_entity_t* entity) {

	if (entity->chunk != NULL) {

		with_lock (&entity->chunk->lock) {
			utl_dllist_remove_by_reference(&entity->chunk->entities, entity->chunk_node);
		}
		entity->chunk_node = NULL;

		entity->chunk = NULL;

	}

}

static inline void ent_remove_chunk(ent_entity_t* entity) {

	with_lock (&entity->lock) {
		
		ent_remove_chunk_l(entity);

	}

}

/*
LOCK ENTITY BEFORE USING THIS FUNCTION
UNLOCK CHUNK BEFORE USING THIS FUNCTION
*/
extern void ent_set_chunk_l(ent_entity_t* entity);

static inline void ent_set_chunk(ent_entity_t* entity) {
	
	with_lock(&entity->lock) {

		ent_set_chunk_l(entity);

	}

}

static inline void ent_move_l(ent_entity_t* entity, float64_t x, float64_t y, float64_t z, bool_t on_ground) {

	if (((uint64_t) entity->position.x >> 4) != ((uint64_t) x >> 4) || ((uint64_t) entity->position.z >> 4) != ((uint64_t) z >> 4)) {

		entity->position.x = x;
		entity->position.y = y;
		entity->position.z = z;
		ent_set_chunk_l(entity);

	} else {

		entity->position.x = x;
		entity->position.y = y;
		entity->position.z = z;

	}

	entity->on_ground = on_ground;

}

static inline void ent_move(ent_entity_t* entity, float64_t x, float64_t y, float64_t z, bool_t on_ground) {

	with_lock (&entity->lock) {
		ent_move_l(entity, x, y, z, on_ground);
	}

}

static inline void ent_move_look_l(ent_living_entity_t* entity, float64_t x, float64_t y, float64_t z, float32_t yaw, float32_t pitch, bool_t on_ground) {

	if (((uint64_t) entity->entity.position.x >> 4) != ((uint64_t) x >> 4) || ((uint64_t) entity->entity.position.z >> 4) != ((uint64_t) z >> 4)) {

		entity->entity.position.x = x;
		entity->entity.position.y = y;
		entity->entity.position.z = z;
		ent_set_chunk_l(&entity->entity);

	} else {

		entity->entity.position.x = x;
		entity->entity.position.y = y;
		entity->entity.position.z = z;

	}

	entity->entity.on_ground = on_ground;
	entity->rotation.pitch = pitch;
	entity->rotation.yaw = yaw;

}

static inline void ent_move_look(ent_living_entity_t* entity, float64_t x, float64_t y, float64_t z, float32_t yaw, float32_t pitch, bool_t on_ground) {
	
	with_lock (&entity->entity.lock) {

		ent_move_look_l(entity, x, y, z, yaw, pitch, on_ground);

	}

}

static inline void ent_teleport(ent_entity_t* entity, wld_world_t* world, float64_t x, float64_t y, float64_t z) {

	with_lock (&entity->lock) {

		entity->position.world = world;
		entity->position.x = x;
		entity->position.y = y;
		entity->position.z = z;
		
		ent_set_chunk(entity);
	
	}

}

static inline void ent_teleport_look(ent_living_entity_t* entity, wld_world_t* world, float64_t x, float64_t y, float64_t z, float32_t yaw, float32_t pitch) {

	with_lock(&entity->entity.lock) {

		entity->entity.position.world = world;
		entity->entity.position.x = x;
		entity->entity.position.y = y;
		entity->entity.position.z = z;
		entity->rotation.pitch = pitch;
		entity->rotation.yaw = yaw;

		ent_set_chunk(&entity->entity);

	}

}

extern void ent_free_entity(ent_entity_t* entity);
