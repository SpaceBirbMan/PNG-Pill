#include <string>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <thread>
#include <cstring>
#include <functional>
#include "sockets.h"


constexpr double PI = 3.141592653589793;

struct SpriteList {
    SDL_Texture* tex = nullptr;
    SDL_Surface* surface = nullptr;
    int w = 0, h = 0;
    std::string name;
    float baseOffsetX[4] = { 0,0,0,0 };
    float baseOffsetY[4] = { 0,0,0,0 };
};

enum class SpriteAlignment{
    AsIs,       // как есть (без изменений)
    Centered    // центрировать по bounding box
};

struct AppConfig {
    bool debugMode = true;
    uint32_t bgColor = 0x000000;
    int windowWidth = 800;
    int windowHeight = 600;
    std::string micName = "default";
    float micThreshold = 0.0075f;
    float micGain = 1.0f;
    std::string spriteDir;
    bool enableBreathing = true;
    float breathingAmp = 1.0f;
    float breathingFreq = 1.0f;
    bool enableShaking = true;
    float shakingAmp = 1.0f;
    float shakingFreq = 1.0f;
    int fps = 60; // vsync?
    bool globalHookingAcceptable = false;
    bool useCpuRendering = false;
    SpriteAlignment alignment = SpriteAlignment::Centered;
    bool usebilinearinterpolationoncpu = true;
    int numberOfThreadsForCpuRender = -1; // то есть автоматическое определение (должно быть (потом))
    int webSocket = 8080;
};

struct ContextMenuItem {
    std::string label;
    std::function<void()> action;
};

struct RawPixels {
    uint8_t* pixels = nullptr;
    size_t size = 0;
};

struct MainLoopState {
    bool running = true;
    bool debug = false;
    bool speak = false;
    bool prevSpeak = false;
    bool blink = false;
    bool isBreathing = false;
    bool webDisplaying = false;

    int currentSpriteIndex = 0;
    int prevFrameIndex = -1;

    double dt = 0.0;

    double globalTime = 0.0;
    double breathPhase = 0.0;
    float breathScale = 1.0f;

    RawPixels currentFrameRawPixels;

    Uint32 lastBlink = 0;
    Uint32 blinkStart = 0;
    Uint64 perfStart = 0;
    double perfFreq = 0.0;

    bool showContextMenu = false;
    int contextMenuX = 0;
    int contextMenuY = 0;

    bool dragging = false;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float baseOffsetX = 0.0f;
    float baseOffsetY = 0.0f;
    float scale = 1.0f;
    float baseScale = 1.0f;
    int dragStartX = 0, dragStartY = 0;

    Uint32 lastLeftClickTime = 0;
    int lastLeftClickX = -1;
    int lastLeftClickY = -1;
    const Uint32 DOUBLE_CLICK_THRESHOLD_MS = 500;
    const int DOUBLE_CLICK_THRESHOLD_PX = 5;

    uint8_t** rawFrame = nullptr;

    std::vector<SDL_Texture*> contextMenuTextures;
    TTF_Font* menuFont = nullptr;

};

struct AppContext {
    SDL_Window* win;
    SDL_Renderer* ren;
    SDL_Surface* winSurface = nullptr;
    SDL_AudioStream* stream;
    std::vector<SpriteList> sprites;
    std::unordered_map<SDL_Keycode, size_t> keymap;
    AppConfig cfg;
    unsigned int nThreads;
    std::vector<ContextMenuItem> contextMenuItems;
    MainLoopState* state;
};

static uint32_t hexStringToUint32(const std::string& hexStr) {
    if (hexStr.size() != 7 || hexStr[0] != '#') {
        return 0x000000; // fallback
    }
    unsigned int r = 0, g = 0, b = 0;
    std::sscanf(hexStr.c_str() + 1, "%2x%2x%2x", &r, &g, &b);
    return (r << 16) | (g << 8) | b;
}

static std::string trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start))) ++start;
    auto end = s.end();
    do --end; while (std::distance(start, end) > 0 && std::isspace(static_cast<unsigned char>(*end)));
    return std::string(start, end + 1);
}

static void hexToRgb(const std::string& hex, Uint8& r, Uint8& g, Uint8& b) {
    unsigned int ri = 0, gi = 0, bi = 0;
    if (hex.size() == 7 && hex[0] == '#') {
        std::sscanf(hex.c_str() + 1, "%2x%2x%2x", &ri, &gi, &bi);
    }
    r = static_cast<Uint8>(ri);
    g = static_cast<Uint8>(gi);
    b = static_cast<Uint8>(bi);
}

static bool parseBool(const std::string& s) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower == "true";
}

struct RenderGeometry {
    float dstX, dstY;
    float dstW, dstH;
    int srcX, srcY, srcW, srcH;
};

RenderGeometry computeRenderGeometry(
    int spriteW, int spriteH,
    int winW, int winH,
    const AppContext& ctx,
    int frameIndex,
    float shakingAmp,
    float shakingFreq,
    float baseOffsetX[] = {0}, float baseOffsetY[] = {0}
) {
    int quadW = spriteW / 2;
    int quadH = spriteH / 2;

    int srcX = (frameIndex % 2) * quadW;
    int srcY = (frameIndex / 2) * quadH;

    float ibaseOffsetX = baseOffsetX[frameIndex];
    float ibaseOffsetY = baseOffsetY[frameIndex];

    float aspect = static_cast<float>(quadW) / quadH;
    int dstW = std::min(winW, static_cast<int>(winH * aspect));
    int dstH = std::min(winH, static_cast<int>(winW / aspect));
    float baseW = static_cast<float>(dstW) * ctx.state->breathScale;
    float baseH = static_cast<float>(dstH) * ctx.state->breathScale;
    float finalW = baseW * ctx.state->scale;
    float finalH = baseH * ctx.state->scale;

    float dstX = (static_cast<float>(winW) - finalW) / 2.0f + ctx.state->offsetX + ibaseOffsetX * ctx.state->scale;
    float dstY = (static_cast<float>(winH) - finalH) / 2.0f + ctx.state->offsetY + ibaseOffsetY * ctx.state->scale;

    if ((shakingAmp > 0.0f) && (shakingFreq > 0.0f) && ctx.state->speak) {
        float ox = static_cast<float>(std::sin(ctx.state->globalTime * (50.0f * shakingFreq) * PI) * 2.0f * (shakingAmp / 2.0f));
        float oy = static_cast<float>(std::cos(ctx.state->globalTime * (36.0f * shakingFreq) * PI) * 1.0f * (shakingAmp / 2.0f));
        dstX += ox;
        dstY += oy;
    }

    return { dstX, dstY, finalW, finalH, srcX, srcY, quadW, quadH };
}

void updateContextMenuTextures(AppContext& ctx) {
    for (auto& tex : ctx.state->contextMenuTextures) {
        SDL_DestroyTexture(tex);
    }
    ctx.state->contextMenuTextures.clear();

    for (const auto& item : ctx.contextMenuItems) {
        SDL_Color color = { 240, 240, 240, 255 };
        SDL_Surface* surf = TTF_RenderText_Blended(ctx.state->menuFont, item.label.c_str(), item.label.length(), color);
        if (!surf) {
            SDL_Log("TTF_RenderUTF8_Blended failed: %s", SDL_GetError());
            ctx.state->contextMenuTextures.push_back(nullptr);
            continue;
        }

        SDL_Texture* tex = SDL_CreateTextureFromSurface(ctx.ren, surf);
        SDL_DestroySurface(surf);
        ctx.state->contextMenuTextures.push_back(tex);
    }
}

static void renderFrameGpu(AppContext& ctx, int frameIndex) {
    SpriteList& sp = ctx.sprites[ctx.state->currentSpriteIndex];
    int winW, winH;
    SDL_GetWindowSize(ctx.win, &winW, &winH);

    RenderGeometry geom = computeRenderGeometry(
        sp.w, sp.h,
        winW, winH,
        ctx, frameIndex,
        ctx.cfg.shakingAmp,
        ctx.cfg.shakingFreq,
        sp.baseOffsetX,
        sp.baseOffsetY
    );

    SDL_FRect src{ static_cast<float>(geom.srcX), static_cast<float>(geom.srcY),
                   static_cast<float>(geom.srcW), static_cast<float>(geom.srcH) };
    SDL_FRect dst{ geom.dstX, geom.dstY, geom.dstW, geom.dstH };


    Uint8 r = (ctx.cfg.bgColor >> 16) & 0xFF;
    Uint8 g = (ctx.cfg.bgColor >> 8) & 0xFF;
    Uint8 b = (ctx.cfg.bgColor >> 0) & 0xFF;
    SDL_SetRenderDrawColor(ctx.ren, r, g, b, 255);
    SDL_RenderClear(ctx.ren);
    SDL_RenderTexture(ctx.ren, sp.tex, &src, &dst);
    if (ctx.state->webDisplaying) {

        SDL_Surface* surf = SDL_RenderReadPixels(ctx.ren, nullptr);
        if (surf) {
            size_t size = surf->h * surf->pitch;
            memcpy(ctx.state->currentFrameRawPixels.pixels, surf->pixels, size);
            ctx.state->currentFrameRawPixels.size = size;
            WebPEncodeRGBA(ctx.state->currentFrameRawPixels.pixels, surf->w, surf->h, surf->w+4, 80, ctx.state->rawFrame);
            SDL_DestroySurface(surf);
        }
        //downloadPixelsFromGPUTexture(?, ctx.state->currentFrameRawPixels.pixels, ctx.state->currentFrameRawPixels.size, ctx);
        // тут требуется переход на уровень рендера ниже, придётся писать шейдеры и работать с видюхой прямо

        try {
        WS::send(ctx.state->rawFrame);
        } catch (...) {
            std::cerr << "WS send failed" << std::endl;
        }
    }
    if (ctx.state->showContextMenu) {
        const int menuWidth = 220;
        const int itemHeight = 24;
        const int padding = 8;
        const int menuHeight = static_cast<int>(ctx.contextMenuItems.size()) * itemHeight;

        int menuX = ctx.state->contextMenuX;
        int menuY = ctx.state->contextMenuY;

        int winW, winH;
        SDL_GetWindowSize(ctx.win, &winW, &winH);
        if (menuX + menuWidth > winW) menuX = winW - menuWidth;
        if (menuY + menuHeight > winH) menuY = winH - menuHeight;
        menuX = std::max(0, menuX);
        menuY = std::max(0, menuY);

        SDL_SetRenderDrawColor(ctx.ren, 40, 40, 40, 255);
        SDL_FRect bg{ static_cast<float>(menuX), static_cast<float>(menuY),
                     static_cast<float>(menuWidth), static_cast<float>(menuHeight) };
        SDL_RenderFillRect(ctx.ren, &bg);

        SDL_SetRenderDrawColor(ctx.ren, 200, 200, 200, 255);
        SDL_RenderRect(ctx.ren, &bg);
        for (size_t i = 0; i < ctx.contextMenuItems.size(); ++i) {
            if (ctx.state->contextMenuTextures[i]) {
                int itemY = menuY + static_cast<int>(i) * itemHeight;
                int itemW, itemH;
                SDL_PropertiesID props = SDL_GetTextureProperties(ctx.state->contextMenuTextures[i]);
                itemW = static_cast<int>(SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_WIDTH_NUMBER, 0));
                itemH = static_cast<int>(SDL_GetNumberProperty(props, SDL_PROP_TEXTURE_HEIGHT_NUMBER, 0));

                SDL_FRect dst{
                    static_cast<float>(menuX + padding),
                    static_cast<float>(itemY + (itemHeight - itemH) / 2),
                    static_cast<float>(itemW),
                    static_cast<float>(itemH)
                };
                SDL_RenderTexture(ctx.ren, ctx.state->contextMenuTextures[i], nullptr, &dst);
            }
        }
    }
    if (ctx.state->debug) {
        static Uint32 lastTime = 0;
        static int frameCount = 0;
        static int displayedFps = 0;
        static int targetFps = ctx.cfg.fps;

        Uint32 currentTime = SDL_GetTicks();
        frameCount++;

        if (currentTime - lastTime >= 1000) {
            displayedFps = frameCount;
            frameCount = 0;
            lastTime = currentTime;
            targetFps = ctx.cfg.fps;
        }

        char fpsText[32];
        snprintf(fpsText, sizeof(fpsText), "%d/%d", displayedFps, targetFps);

        if (ctx.state->menuFont) {
            SDL_Color color = {255, 50, 50, 255};
            SDL_Surface* surf = TTF_RenderText_Solid(ctx.state->menuFont, fpsText, 8, color);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(ctx.ren, surf);
                if (tex) {
                    SDL_FRect dst{10.0f, 10.0f, static_cast<float>(surf->w), static_cast<float>(surf->h)};
                    SDL_RenderTexture(ctx.ren, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
                SDL_DestroySurface(surf);
            }
        }
    }
    SDL_RenderPresent(ctx.ren);

}

using Fixed = int32_t;
constexpr int FP_SHIFT = 16;
constexpr Fixed FP_ONE = 1 << FP_SHIFT;

inline Fixed toFixed(float v) {
    return static_cast<Fixed>(v * FP_ONE + (v >= 0 ? 0.5f : -0.5f));
}

inline float toFloat(Fixed v) {
    return static_cast<float>(v) / FP_ONE;
}

static Uint32 sampleBilinear(SDL_Surface* src, float u, float v) {
    if (!src) return 0;

    const SDL_PixelFormatDetails* fmtDetails = SDL_GetPixelFormatDetails(src->format);
    if (!fmtDetails) return 0;

    int x0 = static_cast<int>(std::floor(u));
    int y0 = static_cast<int>(std::floor(v));
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    x0 = std::clamp(x0, 0, src->w - 1);
    x1 = std::clamp(x1, 0, src->w - 1);
    y0 = std::clamp(y0, 0, src->h - 1);
    y1 = std::clamp(y1, 0, src->h - 1);

    float fx = u - x0;
    float fy = v - y0;

    auto getPixel = [&](int x, int y) -> Uint32 {
        return static_cast<Uint32*>(src->pixels)[y * src->w + x];
        };

    Uint8 r00, g00, b00, a00;
    Uint8 r10, g10, b10, a10;
    Uint8 r01, g01, b01, a01;
    Uint8 r11, g11, b11, a11;

    SDL_GetRGBA(getPixel(x0, y0), fmtDetails, nullptr, &r00, &g00, &b00, &a00);
    SDL_GetRGBA(getPixel(x1, y0), fmtDetails, nullptr, &r10, &g10, &b10, &a10);
    SDL_GetRGBA(getPixel(x0, y1), fmtDetails, nullptr, &r01, &g01, &b01, &a01);
    SDL_GetRGBA(getPixel(x1, y1), fmtDetails, nullptr, &r11, &g11, &b11, &a11);

    auto lerpUint8 = [&](Uint8 a, Uint8 b, float t) -> Uint8 {
        return static_cast<Uint8>(a + t * (b - a) + 0.5f);
        };

    Uint8 r_top = lerpUint8(r00, r10, fx);
    Uint8 g_top = lerpUint8(g00, g10, fx);
    Uint8 b_top = lerpUint8(b00, b10, fx);
    Uint8 a_top = lerpUint8(a00, a10, fx);

    Uint8 r_bot = lerpUint8(r01, r11, fx);
    Uint8 g_bot = lerpUint8(g01, g11, fx);
    Uint8 b_bot = lerpUint8(b01, b11, fx);
    Uint8 a_bot = lerpUint8(a01, a11, fx);

    Uint8 r = lerpUint8(r_top, r_bot, fy);
    Uint8 g = lerpUint8(g_top, g_bot, fy);
    Uint8 b = lerpUint8(b_top, b_bot, fy);
    Uint8 a = lerpUint8(a_top, a_bot, fy);

    return SDL_MapRGBA(fmtDetails, nullptr, r, g, b, a);
}

static void renderFrameCpu(AppContext& ctx, int frameIndex) {
    SDL_Surface* winSurface = SDL_GetWindowSurface(ctx.win);
    if (!winSurface) return;

    SpriteList& sp = ctx.sprites[ctx.state->currentSpriteIndex];
    if (!sp.surface) return;

    int winW = winSurface->w;
    int winH = winSurface->h;

    RenderGeometry geom = computeRenderGeometry(
        sp.surface->w, sp.surface->h,
        winW, winH,
        ctx, frameIndex,
        ctx.cfg.shakingAmp,
        ctx.cfg.shakingFreq,
        sp.baseOffsetX,
        sp.baseOffsetY
    );

    float dstX = geom.dstX;
    float dstY = geom.dstY;
    float dstW = geom.dstW;
    float dstH = geom.dstH;
    int srcX = geom.srcX;
    int srcY = geom.srcY;
    int srcW = geom.srcW;
    int srcH = geom.srcH;

    if (dstW <= 0 || dstH <= 0 || srcW <= 0 || srcH <= 0) return;

    const SDL_PixelFormatDetails* dstFmt = SDL_GetPixelFormatDetails(winSurface->format);
    if (!dstFmt) return;

    Uint8 bgR = (ctx.cfg.bgColor >> 16) & 0xFF;
    Uint8 bgG = (ctx.cfg.bgColor >> 8) & 0xFF;
    Uint8 bgB = ctx.cfg.bgColor & 0xFF;
    Uint32 bg = SDL_MapRGB(dstFmt, nullptr, bgR, bgG, bgB);

    std::vector<Uint32> frameBuffer(winW * winH, bg);

    float invDstW = 1.0f / dstW;
    float invDstH = 1.0f / dstH;

    const SDL_PixelFormatDetails* srcFmt = SDL_GetPixelFormatDetails(sp.surface->format);
    if (!srcFmt) return;

    int dstLeft = std::max(0, static_cast<int>(std::floor(dstX)));
    int dstTop = std::max(0, static_cast<int>(std::floor(dstY)));
    int dstRight = std::min(winW, static_cast<int>(std::ceil(dstX + dstW)));
    int dstBottom = std::min(winH, static_cast<int>(std::ceil(dstY + dstH)));

    if (dstLeft >= dstRight || dstTop >= dstBottom) return;

    const int height = dstBottom - dstTop;
    const int rowsPerThread = std::max(1, height / static_cast<int>(ctx.nThreads)); 
    std::vector<std::thread> workers;

    for (int t = 0; t < static_cast<int>(ctx.nThreads); ++t) {
        const int yStart = dstTop + t * rowsPerThread;
        const int yEnd = (t == static_cast<int>(ctx.nThreads) - 1) ? dstBottom : std::min(yStart + rowsPerThread, dstBottom);

        if (yStart >= yEnd) continue;

        workers.emplace_back([&, yStart, yEnd]() {
            for (int y = yStart; y < yEnd; ++y) {
                for (int x = dstLeft; x < dstRight; ++x) {
                    float fx = (x + 0.5f - dstX) * invDstW;
                    float fy = (y + 0.5f - dstY) * invDstH;
                    if (fx < 0.0f || fx >= 1.0f || fy < 0.0f || fy >= 1.0f) continue;

                    float srcU = srcX + fx * srcW;
                    float srcV = srcY + fy * srcH;

                    Uint32 srcPixel = sampleBilinear(sp.surface, srcU, srcV);

                    Uint8 sr, sg, sb, sa;
                    SDL_GetRGBA(srcPixel, srcFmt, nullptr, &sr, &sg, &sb, &sa);

                    if (sa == 0) continue;

                    Uint32 dstPixel = frameBuffer[y * winW + x];
                    Uint8 dr, dg, db, da;
                    SDL_GetRGBA(dstPixel, dstFmt, nullptr, &dr, &dg, &db, &da);

                    float a = sa / 255.0f;
                    Uint8 r = static_cast<Uint8>(sr * a + dr * (1.0f - a) + 0.5f);
                    Uint8 g = static_cast<Uint8>(sg * a + dg * (1.0f - a) + 0.5f);
                    Uint8 b = static_cast<Uint8>(sb * a + db * (1.0f - a) + 0.5f);

                    frameBuffer[y * winW + x] = SDL_MapRGB(dstFmt, nullptr, r, g, b);
                }
            }
            });
    }

    for (auto& th : workers) {
        th.join();
    }

    if (ctx.state->showContextMenu) {
        int menuX = ctx.state->contextMenuX;
        int menuY = ctx.state->contextMenuY;
        int menuW = 150;
        int menuH = 80;

        menuX = std::clamp(menuX, 0, winW - menuW);
        menuY = std::clamp(menuY, 0, winH - menuH);

        Uint32 menuBg = SDL_MapRGB(dstFmt, nullptr, 40, 40, 40);
        Uint32 menuBorder = SDL_MapRGB(dstFmt, nullptr, 200, 200, 200);

        for (int y = menuY; y < menuY + menuH; ++y) {
            for (int x = menuX; x < menuX + menuW; ++x) {
                if (y >= 0 && y < winH && x >= 0 && x < winW) {
                    frameBuffer[y * winW + x] = menuBg;
                }
            }
        }

        for (int x = menuX; x < menuX + menuW && x < winW; ++x) {
            if (menuY >= 0 && menuY < winH) frameBuffer[menuY * winW + x] = menuBorder;
            if (menuY + menuH - 1 < winH) frameBuffer[(menuY + menuH - 1) * winW + x] = menuBorder;
        }
        for (int y = menuY; y < menuY + menuH && y < winH; ++y) {
            if (menuX >= 0) frameBuffer[y * winW + menuX] = menuBorder;
            if (menuX + menuW - 1 < winW) frameBuffer[y * winW + (menuX + menuW - 1)] = menuBorder;
        }
    }

    Uint32* dstPixels = static_cast<Uint32*>(winSurface->pixels);
    std::memcpy(dstPixels, frameBuffer.data(), frameBuffer.size() * sizeof(Uint32));

    SDL_UpdateWindowSurface(ctx.win);
}

void downloadPixelsFromGPUTexture(SDL_GPUTexture* gpu_texture, uint8_t** out_pixels, size_t* out_size, AppContext& ctx) {
    int width = ctx.cfg.windowWidth;
    int height = ctx.cfg.windowHeight;
    size_t pixel_size = 4;
    size_t buffer_size = width * height * pixel_size;

    SDL_GPUDevice* device = SDL_GetGPURendererDevice(ctx.ren);
    if (!device) {
        std::cout << "gpu device trouble" << std::endl;
        return;
    }

    SDL_GPUTransferBufferCreateInfo tbci = {};
    tbci.size = buffer_size;
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(device, &tbci);

    SDL_GPUCommandBuffer* cmd_buf = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd_buf);

    SDL_GPUTextureRegion src_region = {};
    src_region.texture = gpu_texture;
    src_region.w = width;
    src_region.h = height;
    src_region.d = 1;

    SDL_GPUTextureTransferInfo dst_info = {};
    dst_info.transfer_buffer = transfer_buffer;
    dst_info.offset = 0;

    SDL_DownloadFromGPUTexture(copy_pass, &src_region, &dst_info);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd_buf);

    SDL_WaitForGPUFences(device, true, &fence, 1);

    void* mapped = SDL_MapGPUTransferBuffer(device, transfer_buffer, false);
    if (mapped) {
        *out_size = buffer_size;
        *out_pixels = (uint8_t*)malloc(buffer_size);
        memcpy(*out_pixels, mapped, buffer_size);
        SDL_UnmapGPUTransferBuffer(device, transfer_buffer);
    }

    SDL_ReleaseGPUFence(device, fence);
    SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
}
