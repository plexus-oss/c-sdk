/**
 * @file plexus.hpp
 * @brief C++ wrapper for Plexus C SDK (Arduino / ESP-IDF C++ projects)
 *
 * Provides an idiomatic C++ class:
 *   PlexusClient px("plx_xxx", "device-001");
 *   px.send("temperature", 72.5);
 *   px.tick();
 */

#ifndef PLEXUS_HPP
#define PLEXUS_HPP

#include "plexus.h"

class PlexusClient {
public:
    PlexusClient(const char* apiKey, const char* sourceId)
        : _client(plexus_init(apiKey, sourceId)) {}

    ~PlexusClient() {
        if (_client) {
            plexus_free(_client);
        }
    }

    /* Non-copyable */
    PlexusClient(const PlexusClient&) = delete;
    PlexusClient& operator=(const PlexusClient&) = delete;

    bool isValid() const { return _client != 0; }

    plexus_err_t send(const char* metric, double value) {
        return plexus_send_number(_client, metric, value);
    }

    plexus_err_t sendNumber(const char* metric, double value) {
        return plexus_send_number(_client, metric, value);
    }

    plexus_err_t sendNumberTs(const char* metric, double value, uint64_t timestamp_ms) {
        return plexus_send_number_ts(_client, metric, value, timestamp_ms);
    }

#if PLEXUS_ENABLE_STRING_VALUES
    plexus_err_t sendString(const char* metric, const char* value) {
        return plexus_send_string(_client, metric, value);
    }
#endif

#if PLEXUS_ENABLE_BOOL_VALUES
    plexus_err_t sendBool(const char* metric, bool value) {
        return plexus_send_bool(_client, metric, value);
    }
#endif

    plexus_err_t flush() { return plexus_flush(_client); }
    plexus_err_t tick() { return plexus_tick(_client); }
    uint16_t pendingCount() const { return plexus_pending_count(_client); }
    void clear() { plexus_clear(_client); }

    plexus_err_t setEndpoint(const char* endpoint) {
        return plexus_set_endpoint(_client, endpoint);
    }

    plexus_err_t setFlushInterval(uint32_t interval_ms) {
        return plexus_set_flush_interval(_client, interval_ms);
    }

    plexus_err_t setFlushCount(uint16_t count) {
        return plexus_set_flush_count(_client, count);
    }

#if PLEXUS_ENABLE_STATUS_CALLBACK
    plexus_err_t onStatusChange(plexus_status_callback_t cb, void* userData) {
        return plexus_on_status_change(_client, cb, userData);
    }

    plexus_conn_status_t getStatus() const {
        return plexus_get_status(_client);
    }
#endif

    plexus_client_t* handle() { return _client; }

private:
    plexus_client_t* _client;
};

#endif /* PLEXUS_HPP */
