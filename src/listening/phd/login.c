#include <curl/curl.h>
#include "login.h"
#include "play.h"
#include "../../util/util.h"
#include "../../motor.h"
#include "../../io/logger/logger.h"
#include "../../io/chat/chat.h"
#include "../../io/chat/translation.h"
#include "../../crypt/random.h"

#include <yyjson.h>

struct {

	CURL* curl;
	pthread_mutex_t lock;

} phd_authRequest = {
	.curl = NULL,
	.lock = PTHREAD_MUTEX_INITIALIZER
};

size_t phd_auth_response_write(void* ptr, size_t size, size_t nmemb, phd_auth_response_t* r) {
	
	const size_t new_len = r->len + size * nmemb;
	r->ptr = realloc(r->ptr, new_len + 1);

	memcpy(r->ptr + r->len, ptr, size * nmemb);
	r->ptr[new_len] = '\0';
	r->len = new_len;

	return size*nmemb;

}

bool_t phd_login(ltg_client_t* client, pck_packet_t* packet) {

	__attribute__((unused)) const int32_t length = pck_read_var_int(packet); // packet length
	const int32_t id = pck_read_var_int(packet);

	switch (id) {
	case 0x00:
		return phd_handle_login_start(client, packet);
	case 0x01:
		return phd_handle_encryption_response(client, packet);
	case 0x02:
		return phd_handle_login_plugin_response(client, packet);
	default:
		log_warn("Received unknown packet %x in login state!", id);
		return false;
	}

}

bool_t phd_handle_login_start(ltg_client_t* client, pck_packet_t* packet) {

	client->username.length = pck_read_var_int(packet);
	pck_read_bytes(packet, (byte_t*) client->username.value, client->username.length);

	if (client->protocol != sky_main.protocol) {

		if (client->protocol < sky_main.protocol) {

			cht_translation_t translation = cht_translation_new;
			translation.translate = cht_translation_multiplayer_disconnect_outdated_client;
			cht_component_t version = cht_new;
			version.text = __MC_VER__;
			
			cht_add_with(&translation, &version);

			char message[128];
			const size_t message_len = cht_write_translation(&translation, message);

			phd_send_disconnect_login(client, message, message_len);

			cht_term_translation(&translation);

		} else {

			cht_translation_t translation = cht_translation_new;
			translation.translate = cht_translation_multiplayer_disconnect_outdated_server;
			cht_component_t version = cht_new;
			version.text = __MC_VER__;
			
			cht_add_with(&translation, &version);

			char message[128];
			const size_t message_len = cht_write_translation(&translation, message);

			phd_send_disconnect_login(client, message, message_len);

			cht_term_translation(&translation);

		}

		return false;
	}

	phd_send_encryption_request(client);

	return true;

}

bool_t phd_handle_encryption_response(ltg_client_t* client, pck_packet_t* packet) {

	struct {
		int32_t length;
		byte_t bytes[128];
	} secret;

	// get shared secret
	secret.length = pck_read_var_int(packet);
	pck_read_bytes(packet, secret.bytes, secret.length);

	// decrypt shared secret
	cry_rsa_decript(secret.bytes, secret.bytes, secret.length, &sky_main.listener.keypair);
	utl_reverse_bytes(secret.bytes, secret.bytes, LTG_AES_KEY_LENGTH);
	
	// start encryption cypher
	const int enc_res = cfb8_start(0, secret.bytes, secret.bytes, LTG_AES_KEY_LENGTH, 0, &client->encryption.encrypt);
	if (enc_res != CRYPT_OK) {
		log_error("Could not start encryption cipher! Error code: %d", enc_res);
		return false;
	}
	const int dec_res = cfb8_start(0, secret.bytes, secret.bytes, LTG_AES_KEY_LENGTH, 0, &client->encryption.decrypt);
	if (dec_res != CRYPT_OK) {
		log_error("Could not start decryption cipher! Error code: %d", dec_res);
		return false;
	}
	client->encryption.enabled = true;

	struct {
		int32_t length;
		union {
			byte_t bytes[128];
			uint32_t key;
		};
	} verify;

	// get verify
	verify.length = pck_read_var_int(packet);
	pck_read_bytes(packet, verify.bytes, verify.length);

	// decrypt and check verify
	cry_rsa_decript(verify.bytes, verify.bytes, verify.length, &sky_main.listener.keypair);
	if (verify.key != client->verify) {

		return false;

	}

	if (sky_main.listener.online_mode) {
		// auth with Mojang's servers...

		pthread_mutex_lock(&phd_authRequest.lock);

		if (phd_authRequest.curl == NULL) {
			phd_authRequest.curl = curl_easy_init();
			curl_easy_setopt(phd_authRequest.curl, CURLOPT_TCP_FASTOPEN, 1);
			curl_easy_setopt(phd_authRequest.curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
			curl_easy_setopt(phd_authRequest.curl, CURLOPT_WRITEFUNCTION, phd_auth_response_write);
		}
		CURLcode res;

		if (!phd_authRequest.curl) {
			pthread_mutex_unlock(&phd_authRequest.lock);
			log_error("Failed to initialize cURL");
			return false;
		}

		// create server_id hash
		byte_t server_id_hash[sha1_desc.hashsize];
		hash_state md;
		sha1_init(&md);
		sha1_process(&md, (byte_t*) "", 0);
		sha1_process(&md, secret.bytes, LTG_AES_KEY_LENGTH);
		sha1_process(&md, sky_main.listener.keypair.ASN1.bytes, sky_main.listener.keypair.ASN1.length);
		sha1_done(&md, server_id_hash);

		// create server_id string
		char server_id[(sha1_desc.hashsize << 1) + 2];
		utl_to_minecraft_hex(server_id, server_id_hash, sha1_desc.hashsize);

		char request[157];
		sprintf(request, "https://sessionserver.mojang.com/session/minecraft/hasJoined?username=%s&serverId=%s", client->username.value, server_id);
		curl_easy_setopt(phd_authRequest.curl, CURLOPT_URL, request);

		// prepare response string
		phd_auth_response_t response;
		response.len = 0;
		response.ptr = malloc(1);
		response.ptr[0] = '\0';

		curl_easy_setopt(phd_authRequest.curl, CURLOPT_WRITEDATA, &response);

		res = curl_easy_perform(phd_authRequest.curl);
		if (res != CURLE_OK) {
			
			pthread_mutex_unlock(&phd_authRequest.lock);

			log_error("Could not authenticate client: %s", curl_easy_strerror(res));

			if (response.ptr != NULL)
				free(response.ptr);
			
			return false;

		}

		long http_code;
		curl_easy_getinfo(phd_authRequest.curl, CURLINFO_RESPONSE_CODE, &http_code);

		pthread_mutex_unlock(&phd_authRequest.lock);

		if (http_code != 200) {
			
			return false;

		}

		yyjson_doc* auth = yyjson_read(response.ptr, response.len, 0);

		yyjson_val* auth_obj = yyjson_doc_get_root(auth);
		size_t i, i_max;
		yyjson_val *auth_key, *auth_val;
		yyjson_obj_foreach(auth_obj, i, i_max, auth_key, auth_val) {
			switch (i) {
				case 0: { // id
					utl_read_hex_bytes(client->uuid, yyjson_get_str(auth_val), 16);
					break;
				}
				case 1: { // username
					const char* auth_username = yyjson_get_str(auth_val);
					if (strcmp(client->username.value, auth_username) != 0) {
						// free old username
						free(client->username.value);

						// copy new username
						client->username.length = yyjson_get_len(auth_val);
						memcpy(client->username.value, auth_username, client->username.length);
					}
					break;
				}
				case 2: { // properties
					size_t j, j_max;
					yyjson_val* property_obj;
					yyjson_arr_foreach(auth_val, j, j_max, property_obj) {
						
						enum {
							none,
							textures
						} property_type = none;

						size_t k, k_max;
						yyjson_val *property_key, *property_val;
						yyjson_obj_foreach(property_obj, k, k_max, property_key, property_val) {
							switch (k) {
								case 0: { // name
									if (yyjson_equals_str(property_val, "textures")) {
										property_type = textures;
									}
									break;
								}
								case 1: { // value
									switch (property_type) {
										case none: {
											log_error("Property type has not been set, is the json response from the auth server curropted?");
											
											yyjson_doc_free(auth);
											free(response.ptr);
											return false;
										}
										case textures: {
											client->textures.value.length = yyjson_get_len(property_val);
											client->textures.value.value = malloc(client->textures.value.length);
											memcpy(client->textures.value.value, yyjson_get_str(property_val), client->textures.value.length);
											break;
										}
									}
									break;
								}
								case 2: { // signature
									switch (property_type) {
										case none: {
											log_error("Property type has not been set, is the json response from the auth server curropted?");
											
											yyjson_doc_free(auth);
											free(response.ptr);
											return false;
										}
										case textures: {
											client->textures.signature.length = yyjson_get_len(property_val);
											client->textures.signature.value = malloc(client->textures.signature.length);
											memcpy(client->textures.signature.value, yyjson_get_str(property_val), client->textures.signature.length);
											break;
										}
									}
									break;
								}
							}
						}

					}
					break;
				}
			}
		}

		// free auth response and auth json doc
		yyjson_doc_free(auth);
		free(response.ptr);
	}

	// send login success packet
	phd_send_login_success(client);

	// switch to play state and join game
	client->state = ltg_play;
	phd_send_join_game(client);

	return true;

}

bool_t phd_handle_login_plugin_response(ltg_client_t* client, pck_packet_t* packet) {

	if ((uint32_t) pck_read_var_int(packet) != client->verify) {
		return false;
	}

	if (pck_read_int8(packet)) {
		// successful
	} else {
		// unsuccessful
	}

	return true;

}

void phd_send_disconnect_login(ltg_client_t* client, const char* message, size_t message_len) {

	PCK_INLINE(packet, 1 + message_len, io_big_endian);

	pck_write_var_int(packet, 0x00);

	pck_write_string(packet, message, message_len);

	ltg_send(client, packet);

}

void phd_send_encryption_request(ltg_client_t* client) {

	PCK_INLINE(response, 256, io_big_endian);

	// packet type 0x01
	pck_write_var_int(response, 0x01);

	// server id
	if (sky_main.listener.online_mode) {
		pck_write_string(response, "", 0);
	} else {
		pck_write_string(response, "-", 1);
	}

	// the public auth_key
	pck_write_var_int(response, sky_main.listener.keypair.ASN1.length);
	pck_write_bytes(response, sky_main.listener.keypair.ASN1.bytes, sky_main.listener.keypair.ASN1.length);

	// our verify token
	cry_random_bytes((uint8_t*) &client->verify, 4);
	pck_write_var_int(response, 4);
	pck_write_int32(response, client->verify);

	ltg_send(client, response);

}

void phd_send_login_success(ltg_client_t* client) {

	PCK_INLINE(response, 32, io_big_endian);

	pck_write_var_int(response, 0x02);
	pck_write_bytes(response, client->uuid, 16);
	pck_write_string(response, client->username.value, client->username.length);

	ltg_send(client, response);

}

void phd_send_set_compression(ltg_client_t* client) {

	PCK_INLINE(packet, 15, io_big_endian);

	pck_write_var_int(packet, 0x03);
	pck_write_var_int(packet, sky_main.listener.network_compression_threshold);

	ltg_send(client, packet);

}

void phd_send_login_plugin_request(ltg_client_t* client, const char* identifier, size_t identifier_length, const byte_t* data, size_t data_length) {

	PCK_INLINE(packet, identifier_length + data_length + 20, io_big_endian);

	pck_write_var_int(packet, 0x04);
	pck_write_var_int(packet, client->verify);
	pck_write_string(packet, identifier, identifier_length);

	pck_write_bytes(packet, data, data_length);

	ltg_send(client, packet);

}
