#include <time.h>
#include "handlers.h"
#include "../listening/phd/play.h"
#include "../io/chat/translation.h"
#include "../io/logger/logger.h"
#include "../motor.h"

bool job_handle_keep_alive(job_payload_t* payload) {

	struct timespec time;
	clock_gettime(CLOCK_REALTIME, &time);
	const int64_t out_ms = time.tv_sec * 1000 + time.tv_nsec / 0xF4240;

	if (out_ms - payload->client->last_recv >= 30000) {
		ltg_disconnect(payload->client);
		return false;
	} else {
		phd_send_keep_alive(payload->client, out_ms);
		return true;
	}

}

bool job_handle_global_chat_message(job_payload_t* payload) {

	log_info("<%s> %s", UTL_STRTOCSTR(payload->global_chat_message.client->username), UTL_STRTOCSTR(payload->global_chat_message.message));

	cht_translation_t translation = cht_translation_new;
	translation.translate = cht_translation_chat_type_text;
	cht_component_t name = cht_new;
	name.text = payload->global_chat_message.client->username;
	cht_component_t message = cht_new;
	message.text = payload->global_chat_message.message;
	cht_add_with(&translation, &name);
	cht_add_with(&translation, &message);

	char out[1536];
	const size_t out_len = cht_write_translation(&translation, out);
	// lock client vector
	with_lock (&sky_main.listener.online.lock) {
		utl_dll_iterator_t iterator = UTL_DLL_ITERATOR_INITIALIZER(&sky_main.listener.online.list);
		ltg_client_t* client = utl_dll_iterator_next(&iterator);
		while (client != NULL) {
			phd_send_chat_message(client, out, out_len, payload->global_chat_message.client->uuid);
			client = utl_dll_iterator_next(&iterator);
		}
	}
	cht_term_translation(&translation);

	free(payload->global_chat_message.message.value);
	return true;

}

bool job_handle_player_join(job_payload_t* payload) {

	log_info("%s joined the game", UTL_STRTOCSTR(payload->client->username));

	cht_translation_t translation = cht_translation_new;
	translation.translate = cht_translation_multiplayer_player_joined;
	translation.color = cht_yellow;
	cht_component_t name = cht_new;
	name.text = payload->client->username;
	cht_add_with(&translation, &name);

	char out[128];
	const size_t out_len = cht_write_translation(&translation, out);
	// lock client vector
	with_lock (&sky_main.listener.online.lock) {
		utl_dll_iterator_t iterator = UTL_DLL_ITERATOR_INITIALIZER(&sky_main.listener.online.list);
		ltg_client_t* client = utl_dll_iterator_next(&iterator);
		while (client != NULL) {
			phd_send_player_info_add_player(client, payload->client);
			phd_send_system_chat_message(client, out, out_len);
			client = utl_dll_iterator_next(&iterator);
		}
	}

	cht_term_translation(&translation);

	return true;

}

bool job_handle_player_leave(job_payload_t* payload) {

	log_info("%s left the game", payload->player_leave.username);

	cht_translation_t translation = cht_translation_new;
	translation.translate = cht_translation_multiplayer_player_left;
	translation.color = cht_yellow;
	cht_component_t name = cht_new;
	name.text = (string_t) {
		.value = payload->player_leave.username,
		.length = payload->player_leave.username_length
	};
	cht_add_with(&translation, &name);

	char out[128];
	const size_t out_len = cht_write_translation(&translation, out);
	
	with_lock (&sky_main.listener.online.lock) {
		utl_dll_iterator_t iterator = UTL_DLL_ITERATOR_INITIALIZER(&sky_main.listener.online.list);
		ltg_client_t* client = utl_dll_iterator_next(&iterator);
		while (client != NULL) {
			phd_send_player_info_remove_player(client, payload->player_leave.uuid);
			phd_send_system_chat_message(client, out, out_len);
			client = utl_dll_iterator_next(&iterator);
		}
	}

	cht_term_translation(&translation);

	return true;

}

bool job_handle_send_update_pings(__attribute__((unused)) job_payload_t* payload) {

	with_lock (&sky_main.listener.online.lock) {
		utl_dll_iterator_t iterator = UTL_DLL_ITERATOR_INITIALIZER(&sky_main.listener.online.list);
		ltg_client_t* client = utl_dll_iterator_next(&iterator);
		while (client != NULL) {
			phd_send_player_info_update_latency(client);
			client = utl_dll_iterator_next(&iterator);
		}
	}

	return true;

}

bool job_handle_tick_region(job_payload_t* payload) {
	
	for (uint32_t i = 0; i < 32 * 32; ++i) {

		const wld_chunk_t* chunk = payload->region->chunks[i];

		if (chunk != NULL) {
			if (chunk->ticket <= WLD_TICKET_TICK_ENTITIES) {
				// entities and chunk ticks
			}

			if (chunk->ticket <= WLD_TICKET_TICK) {
				// tick
			}

			if (chunk->ticket <= WLD_TICKET_BORDER) {
				// border
			}
		}
	}

	return true;

}

bool job_handle_unload_region(job_payload_t* payload) {

	if (payload->region->loaded_chunks == 0) {
		wld_unload_region(payload->region);
		return true;
	}

	return false;

}