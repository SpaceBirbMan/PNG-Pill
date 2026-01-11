#ifndef SOCKETS_H
#define SOCKETS_H

#include <libwebsockets.h>
#include <webp/encode.h>
#include <cstdint>
#include <cstddef>

/**
 * @brief sendWebP Отправляет изображение в формате WebP по WebSocket
 * @param wsi Указатель на WebSocket-соединение (из libwebsockets)
 * @param pixels Пиксели в формате RGBA (4 байта на пиксель)
 * @param width Ширина изображения
 * @param height Высота изображения
 * @return true при успехе
 */

bool sendWebP(struct lws* wsi, const uint8_t* pixels, int width, int height) {
    if (!wsi || !pixels || width <= 0 || height <= 0) return false;
    uint8_t* webp_data = nullptr;
    size_t output_size = WebPEncodeRGBA(pixels, width, height, width * 4, 90.0f, &webp_data);
    if (output_size == 0 || webp_data == nullptr) {
        return false;
    }

    unsigned char* buf = new unsigned char[LWS_PRE + output_size];
    std::memcpy(buf + LWS_PRE, webp_data, output_size);
    WebPFree(webp_data);

    int sent = lws_write(wsi, buf + LWS_PRE, output_size, LWS_WRITE_BINARY);
    delete[] buf;

    return (sent == static_cast<int>(output_size));
}

static int callback_http(struct lws* wsi, enum lws_callback_reasons reason,
                         void* user, void* in, size_t len) {
    return 0;
}

#endif // SOCKETS_H
