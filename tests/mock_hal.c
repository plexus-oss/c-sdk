/**
 * @file mock_hal.c
 * @brief Mock HAL implementation for host-side testing
 *
 * Provides stub implementations of all HAL functions so tests
 * can compile and run on macOS/Linux without real hardware.
 */

#include "plexus.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Simulated tick counter */
static uint32_t s_tick_ms = 0;
static uint64_t s_time_ms = 1700000000000ULL; /* ~Nov 2023 in ms */

/* Track last HTTP POST for test assertions */
static char s_last_post_url[256] = {0};
static char s_last_post_body[PLEXUS_JSON_BUFFER_SIZE] = {0};
static size_t s_last_post_body_len = 0;
static char s_last_user_agent[128] = {0};
static plexus_err_t s_next_post_result = PLEXUS_OK;
static int s_post_call_count = 0;

/* Track delay calls for backoff verification */
static uint32_t s_delay_calls[16] = {0};
static int s_delay_call_count = 0;

/* ---- Test helpers (not part of HAL) ---- */

void mock_hal_reset(void) {
    s_tick_ms = 0;
    s_time_ms = 1700000000000ULL;
    s_last_post_url[0] = '\0';
    s_last_post_body[0] = '\0';
    s_last_post_body_len = 0;
    s_last_user_agent[0] = '\0';
    s_next_post_result = PLEXUS_OK;
    s_post_call_count = 0;
    s_delay_call_count = 0;
    memset(s_delay_calls, 0, sizeof(s_delay_calls));
}

void mock_hal_set_tick(uint32_t tick_ms) {
    s_tick_ms = tick_ms;
}

void mock_hal_advance_tick(uint32_t delta_ms) {
    s_tick_ms += delta_ms;
}

void mock_hal_set_next_post_result(plexus_err_t err) {
    s_next_post_result = err;
}

const char* mock_hal_last_post_body(void) {
    return s_last_post_body;
}

size_t mock_hal_last_post_body_len(void) {
    return s_last_post_body_len;
}

int mock_hal_post_call_count(void) {
    return s_post_call_count;
}

const char* mock_hal_last_user_agent(void) {
    return s_last_user_agent;
}

const char* mock_hal_last_post_url(void) {
    return s_last_post_url;
}

int mock_hal_delay_call_count(void) {
    return s_delay_call_count;
}

uint32_t mock_hal_delay_call_ms(int index) {
    if (index >= 0 && index < 16) {
        return s_delay_calls[index];
    }
    return 0;
}

/* ---- HAL function implementations ---- */

plexus_err_t plexus_hal_http_post(const char* url, const char* api_key,
                                   const char* user_agent,
                                   const char* body, size_t body_len) {
    (void)api_key;
    s_post_call_count++;

    if (url) {
        strncpy(s_last_post_url, url, sizeof(s_last_post_url) - 1);
    }
    if (user_agent) {
        strncpy(s_last_user_agent, user_agent, sizeof(s_last_user_agent) - 1);
    }
    if (body && body_len > 0 && body_len < sizeof(s_last_post_body)) {
        memcpy(s_last_post_body, body, body_len);
        s_last_post_body[body_len] = '\0';
        s_last_post_body_len = body_len;
    }

    return s_next_post_result;
}

#if PLEXUS_ENABLE_COMMANDS
plexus_err_t plexus_hal_http_get(const char* url, const char* api_key,
                                  const char* user_agent,
                                  char* response_buf, size_t buf_size,
                                  size_t* response_len) {
    (void)url;
    (void)api_key;
    (void)user_agent;
    if (response_buf && buf_size > 0) {
        const char* empty = "{\"commands\":[]}";
        size_t len = strlen(empty);
        if (len < buf_size) {
            memcpy(response_buf, empty, len + 1);
            if (response_len) *response_len = len;
        }
    }
    return PLEXUS_OK;
}
#endif

uint64_t plexus_hal_get_time_ms(void) {
    return s_time_ms++;
}

uint32_t plexus_hal_get_tick_ms(void) {
    return s_tick_ms;
}

void plexus_hal_delay_ms(uint32_t ms) {
    /* Record delay calls for backoff testing */
    if (s_delay_call_count < 16) {
        s_delay_calls[s_delay_call_count] = ms;
    }
    s_delay_call_count++;
    /* Advance tick to simulate time passing */
    s_tick_ms += ms;
}

void plexus_hal_log(const char* fmt, ...) {
    (void)fmt;
}

/* ========================================================================= */
/* Persistent storage mock — key-value map                                   */
/* ========================================================================= */

#if PLEXUS_ENABLE_PERSISTENT_BUFFER

#define MOCK_STORAGE_SLOTS 16
#define MOCK_STORAGE_KEY_LEN 32

static struct {
    char key[MOCK_STORAGE_KEY_LEN];
    char data[PLEXUS_JSON_BUFFER_SIZE];
    size_t len;
    bool used;
} s_storage[MOCK_STORAGE_SLOTS];

static int storage_find(const char* key) {
    for (int i = 0; i < MOCK_STORAGE_SLOTS; i++) {
        if (s_storage[i].used && strcmp(s_storage[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

static int storage_alloc(const char* key) {
    /* Try to find existing entry first */
    int idx = storage_find(key);
    if (idx >= 0) return idx;

    /* Find a free slot */
    for (int i = 0; i < MOCK_STORAGE_SLOTS; i++) {
        if (!s_storage[i].used) {
            strncpy(s_storage[i].key, key, MOCK_STORAGE_KEY_LEN - 1);
            s_storage[i].key[MOCK_STORAGE_KEY_LEN - 1] = '\0';
            s_storage[i].used = true;
            s_storage[i].len = 0;
            return i;
        }
    }
    return -1;
}

void mock_hal_storage_reset(void) {
    memset(s_storage, 0, sizeof(s_storage));
}

plexus_err_t plexus_hal_storage_write(const char* key, const void* data, size_t len) {
    int idx = storage_alloc(key);
    if (idx < 0) return PLEXUS_ERR_HAL;
    if (len > sizeof(s_storage[idx].data)) return PLEXUS_ERR_HAL;
    memcpy(s_storage[idx].data, data, len);
    s_storage[idx].len = len;
    return PLEXUS_OK;
}

plexus_err_t plexus_hal_storage_read(const char* key, void* data, size_t max_len, size_t* out_len) {
    int idx = storage_find(key);
    if (idx < 0) {
        if (out_len) *out_len = 0;
        return PLEXUS_OK;  /* Key not found is not an error */
    }
    size_t copy_len = s_storage[idx].len < max_len ? s_storage[idx].len : max_len;
    memcpy(data, s_storage[idx].data, copy_len);
    if (out_len) *out_len = copy_len;
    return PLEXUS_OK;
}

plexus_err_t plexus_hal_storage_clear(const char* key) {
    int idx = storage_find(key);
    if (idx >= 0) {
        s_storage[idx].used = false;
        s_storage[idx].len = 0;
    }
    return PLEXUS_OK;
}

#endif /* PLEXUS_ENABLE_PERSISTENT_BUFFER */

/* ========================================================================= */
/* Thread safety mock                                                        */
/* ========================================================================= */

#if PLEXUS_ENABLE_THREAD_SAFE

static int s_mutex_lock_count = 0;
static int s_mutex_unlock_count = 0;

int mock_hal_mutex_lock_count(void) { return s_mutex_lock_count; }
int mock_hal_mutex_unlock_count(void) { return s_mutex_unlock_count; }

void mock_hal_mutex_reset(void) {
    s_mutex_lock_count = 0;
    s_mutex_unlock_count = 0;
}

void* plexus_hal_mutex_create(void) {
    return (void*)1; /* Non-NULL sentinel */
}

void plexus_hal_mutex_lock(void* mutex) {
    (void)mutex;
    s_mutex_lock_count++;
}

void plexus_hal_mutex_unlock(void* mutex) {
    (void)mutex;
    s_mutex_unlock_count++;
}

void plexus_hal_mutex_destroy(void* mutex) {
    (void)mutex;
}

#endif /* PLEXUS_ENABLE_THREAD_SAFE */

/* ========================================================================= */
/* MQTT mock                                                                 */
/* ========================================================================= */

#if PLEXUS_ENABLE_MQTT

static bool s_mqtt_connected = false;
static int s_mqtt_publish_count = 0;
static char s_mqtt_last_topic[256] = {0};
static char s_mqtt_last_payload[PLEXUS_JSON_BUFFER_SIZE] = {0};
static size_t s_mqtt_last_payload_len = 0;
static plexus_err_t s_mqtt_next_publish_result = PLEXUS_OK;
static plexus_err_t s_mqtt_next_connect_result = PLEXUS_OK;

void mock_hal_mqtt_reset(void) {
    s_mqtt_connected = false;
    s_mqtt_publish_count = 0;
    s_mqtt_last_topic[0] = '\0';
    s_mqtt_last_payload[0] = '\0';
    s_mqtt_last_payload_len = 0;
    s_mqtt_next_publish_result = PLEXUS_OK;
    s_mqtt_next_connect_result = PLEXUS_OK;
}

void mock_hal_mqtt_set_connected(bool connected) {
    s_mqtt_connected = connected;
}

void mock_hal_mqtt_set_next_connect_result(plexus_err_t err) {
    s_mqtt_next_connect_result = err;
}

void mock_hal_mqtt_set_next_publish_result(plexus_err_t err) {
    s_mqtt_next_publish_result = err;
}

int mock_hal_mqtt_publish_count(void) {
    return s_mqtt_publish_count;
}

const char* mock_hal_mqtt_last_topic(void) {
    return s_mqtt_last_topic;
}

const char* mock_hal_mqtt_last_payload(void) {
    return s_mqtt_last_payload;
}

plexus_err_t plexus_hal_mqtt_connect(const char* broker_uri, const char* api_key,
                                      const char* source_id) {
    (void)broker_uri;
    (void)api_key;
    (void)source_id;
    if (s_mqtt_next_connect_result == PLEXUS_OK) {
        s_mqtt_connected = true;
    }
    return s_mqtt_next_connect_result;
}

plexus_err_t plexus_hal_mqtt_publish(const char* topic, const char* payload,
                                      size_t payload_len, int qos) {
    (void)qos;
    s_mqtt_publish_count++;
    if (topic) {
        strncpy(s_mqtt_last_topic, topic, sizeof(s_mqtt_last_topic) - 1);
    }
    if (payload && payload_len > 0 && payload_len < sizeof(s_mqtt_last_payload)) {
        memcpy(s_mqtt_last_payload, payload, payload_len);
        s_mqtt_last_payload[payload_len] = '\0';
        s_mqtt_last_payload_len = payload_len;
    }
    return s_mqtt_next_publish_result;
}

bool plexus_hal_mqtt_is_connected(void) {
    return s_mqtt_connected;
}

void plexus_hal_mqtt_disconnect(void) {
    s_mqtt_connected = false;
}

#if PLEXUS_ENABLE_COMMANDS
plexus_err_t plexus_hal_mqtt_subscribe(const char* topic, int qos) {
    (void)topic;
    (void)qos;
    return PLEXUS_OK;
}

plexus_err_t plexus_hal_mqtt_receive(char* buf, size_t buf_size, size_t* msg_len) {
    (void)buf;
    (void)buf_size;
    if (msg_len) *msg_len = 0;
    return PLEXUS_OK;
}
#endif /* PLEXUS_ENABLE_COMMANDS */

#endif /* PLEXUS_ENABLE_MQTT */
