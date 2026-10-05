#pragma once

// Host stand-in for src/Log.h: everything to stderr.

#include <cstdio>

#define RENDER_HOST_LOG(level, tag, ...) \
    do { \
        fprintf(stderr, "%s %s: ", level, tag); \
        fprintf(stderr, __VA_ARGS__); \
        fputc('\n', stderr); \
    } while (0)

#define ESP_LOGE(tag, ...) RENDER_HOST_LOG("E", tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) RENDER_HOST_LOG("W", tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) RENDER_HOST_LOG("I", tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) RENDER_HOST_LOG("D", tag, __VA_ARGS__)
