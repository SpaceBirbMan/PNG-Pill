#include "app.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
const std::string APP_NAME = "PNGPill";


// todo: hot reload
static void createDefaultConfig(const fs::path& path) {
    std::ofstream file(path);
    if (!file.is_open()) return;
    file << "debugMode = false\n"
        << "bgColor = #000000\n"
        << "windowWidth = 800\n"
        << "windowHeight = 600\n"
        << "micName = default\n"
        << "micThreshold = 0.0075\n"
        << "micGain = 1.0\n"
        << "spriteDir = \n"
        << "enableBreathing = true\n"
        << "enableShaking = true\n"
        << "shakingAmplitude = 1.0\n"
        << "shakingFrequency = 1.0\n"
        << "fps = 60\n";
}

static AppConfig loadConfig(const fs::path& dir) {
    auto cfgPath = dir / "config.ini";
    if (!fs::exists(cfgPath)) {
        createDefaultConfig(cfgPath);
    }

    AppConfig cfg;
    std::ifstream f(cfgPath);
    std::string line;

    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "debugMode") cfg.debugMode = parseBool(val);
        if (key == "bgColor") cfg.bgColor = hexStringToUint32(val);
        else if (key == "windowWidth")  cfg.windowWidth = std::stoi(val);
        else if (key == "windowHeight") cfg.windowHeight = std::stoi(val);
        else if (key == "micName") cfg.micName = val;
        else if (key == "micThreshold") cfg.micThreshold = std::stof(val);
        else if (key == "micGain")      cfg.micGain = std::stof(val);
        else if (key == "spriteDir")    cfg.spriteDir = val;
        else if (key == "enableBreathing") cfg.enableBreathing = parseBool(val);
        else if (key == "breathingAmplitude") cfg.breathingAmp = stof(val);
        else if (key == "breathingFrequency") cfg.breathingFreq = stof(val);
        else if (key == "enableShaking")    cfg.enableShaking = parseBool(val);
        else if (key == "shakingAmplitude") cfg.shakingAmp = std::stof(val);
        else if (key == "shakingFrequency") cfg.shakingFreq = std::stof(val);
        else if (key == "fps")          cfg.fps = std::stoi(val);
    }
    return cfg;
}


static SDL_AudioDeviceID findMicByName(const std::string& name) {
    int num = 0;
    SDL_AudioDeviceID* devices = SDL_GetAudioRecordingDevices(&num);
    if (!devices || num == 0) return 0;

    SDL_AudioDeviceID fallback = devices[0];
    std::string lname = name;
    std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);

    for (int i = 0; i < num; ++i) {
        const char* devName = SDL_GetAudioDeviceName(devices[i]);
        if (!devName) continue;
        std::string d = devName;
        std::transform(d.begin(), d.end(), d.begin(), ::tolower);
        if (d.find(lname) != std::string::npos) {
            SDL_free(devices);
            return devices[i];
        }
    }
    SDL_free(devices);
    return fallback;
}


void loadSpritesCpu(
    std::vector<SpriteList>& sprites,
    std::unordered_map<SDL_Keycode, size_t>& keymap,
    const std::string& dirPath,
    AppContext ctx,
    SpriteAlignment alignment = SpriteAlignment::AsIs
) {
    fs::path dir = dirPath.empty() ? fs::current_path() : fs::path(dirPath);
    if (!fs::exists(dir)) {
        fs::create_directory(dir);
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension();
        if (ext != ".png" && ext != ".PNG") continue;

        SDL_Surface* surf = IMG_Load(entry.path().string().c_str());
        if (!surf) {
            std::cerr << "Failed to load " << entry.path().string() << '\n';
            continue;
        }

        SpriteList s;
        s.surface = surf;
        s.tex = nullptr;
        s.w = surf->w;
        s.h = surf->h;
        s.name = entry.path().stem().string();

        if (alignment == SpriteAlignment::Centered) {
            SDL_PixelFormat targetFormat = SDL_PIXELFORMAT_RGBA8888;
            SDL_Surface* rgbaSurf = SDL_ConvertSurface(surf, targetFormat);
            if (!rgbaSurf) {
                std::cerr << "Failed to convert surface to RGBA8888\n";
            }
            else {
                SDL_DestroySurface(surf);
                surf = rgbaSurf;
                s.surface = surf;
                s.w = surf->w;
                s.h = surf->h;
            }

            int min_x = surf->w, max_x = -1;
            int min_y = surf->h, max_y = -1;
            uint32_t* pixels = static_cast<uint32_t*>(surf->pixels);
            int pitch = surf->pitch / sizeof(uint32_t);

            int quadW = surf->w / 2;
            int quadH = surf->h / 2;

            for (int fy = 0; fy < 2; ++fy) {
                for (int fx = 0; fx < 2; ++fx) {
                    int idx = fy * 2 + fx;

                    int min_x = quadW, max_x = -1;
                    int min_y = quadH, max_y = -1;

                    for (int y = 0; y < quadH; ++y) {
                        for (int x = 0; x < quadW; ++x) {
                            int gx = fx * quadW + x;
                            int gy = fy * quadH + y;
                            uint32_t pixel = pixels[gy * pitch + gx];
                            uint8_t alpha = (pixel >> 24) & 0xFF;
                            if (alpha > 0) {
                                if (x < min_x) min_x = x;
                                if (x > max_x) max_x = x;
                                if (y < min_y) min_y = y;
                                if (y > max_y) max_y = y;
                            }
                        }
                    }

                    if (min_x <= max_x && min_y <= max_y) {
                        float current_center_x = (min_x + max_x) * 0.5f;
                        float current_center_y = (min_y + max_y) * 0.5f;
                        float quad_center_x = quadW * 0.5f;
                        float quad_center_y = quadH * 0.5f;

                        s.baseOffsetX[idx] = quad_center_x - current_center_x;
                        s.baseOffsetY[idx] = quad_center_y - current_center_y;
                    }
                }
            }
        }

        size_t idx = sprites.size();
        sprites.push_back(s);

        SDL_Keycode kc = SDL_GetKeyFromName(s.name.c_str());
        if (kc != SDLK_UNKNOWN) {
            keymap[kc] = idx;
        }
    }

    if (sprites.empty()) {
        std::cerr << "No sprites found.\n";
    }
}

static void loadSprites(
    SDL_Renderer* renderer,
    const std::string& dirPath,
    std::vector<SpriteList>& sprites,
    std::unordered_map<SDL_Keycode, size_t>& keymap,
    SpriteAlignment alignment = SpriteAlignment::AsIs
) {
    std::vector<SpriteList> cpuSprites;
    std::unordered_map<SDL_Keycode, size_t> cpuKeymap;
    AppContext dummyCtx{ nullptr, renderer };
    loadSpritesCpu(cpuSprites, cpuKeymap, dirPath, dummyCtx, alignment);

    for (auto& s : cpuSprites) {
        if (s.surface) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, s.surface);
            SDL_DestroySurface(s.surface);
            s.surface = nullptr;
            s.tex = tex;
            if (tex) {
                sprites.push_back(s);
                if (cpuKeymap.count(SDL_GetKeyFromName(s.name.c_str()))) {
                    keymap[SDL_GetKeyFromName(s.name.c_str())] = sprites.size() - 1;
                }
            }
            std::cout << s.baseOffsetX << " " << s.baseOffsetY << std::endl;
        }
    }
}


static void initializeMainLoopState(AppContext &ctx) {
    ctx.state->perfStart = SDL_GetPerformanceCounter();
    ctx.state->perfFreq = static_cast<double>(SDL_GetPerformanceFrequency());
    ctx.state->lastBlink = SDL_GetTicks();
}

static void handleEvents(AppContext& ctx) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            ctx.state->running = false;
            break;

        case SDL_EVENT_KEY_DOWN: {
            if (ev.key.key == SDLK_ESCAPE) {
                ctx.state->running = false;
            }
            else {
                auto it = ctx.keymap.find(ev.key.key);
                if (it != ctx.keymap.end()) {
                    ctx.state->currentSpriteIndex = static_cast<int>(it->second);
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            switch (ev.button.button) {
            case SDL_BUTTON_LEFT: {
                Uint32 currentTime = SDL_GetTicks();
                int dx = ev.button.x - ctx.state->lastLeftClickX;
                int dy = ev.button.y - ctx.state->lastLeftClickY;
                bool isDoubleClick = (currentTime - ctx.state->lastLeftClickTime <= ctx.state->DOUBLE_CLICK_THRESHOLD_MS) &&
                    (dx * dx + dy * dy <= ctx.state->DOUBLE_CLICK_THRESHOLD_PX * ctx.state->DOUBLE_CLICK_THRESHOLD_PX);

                if (ev.button.button == SDL_BUTTON_LEFT && ctx.state->showContextMenu) {
                    const int menuWidth = 180;
                    const int itemHeight = 24;
                    int menuX = ctx.state->contextMenuX;
                    int menuY = ctx.state->contextMenuY;

                    int winW, winH;
                    SDL_GetWindowSize(ctx.win, &winW, &winH);
                    if (menuX + menuWidth > winW) menuX = winW - menuWidth;
                    if (menuY + static_cast<int>(ctx.contextMenuItems.size()) * itemHeight > winH)
                        menuY = winH - static_cast<int>(ctx.contextMenuItems.size()) * itemHeight;
                    menuX = std::max(0, menuX);
                    menuY = std::max(0, menuY);

                    int localY = ev.button.y - menuY;
                    size_t index = static_cast<size_t>(localY / itemHeight);

                    if (index < ctx.contextMenuItems.size() &&
                        ev.button.x >= menuX &&
                        ev.button.x < menuX + menuWidth) {
                        ctx.contextMenuItems[index].action();
                        ctx.state->showContextMenu = false;
                    } else {
                        ctx.state->showContextMenu = false;
                    }
                    break;
                }

                if (isDoubleClick) {
                    ctx.state->lastLeftClickTime = 0; // сброс, чтобы не срабатывал трижды
                    ctx.state->offsetX = ctx.state->baseOffsetX; // хардкожно айайай
                    ctx.state->offsetY = ctx.state->baseOffsetY;
                    ctx.state->scale = ctx.state->baseScale;
                }
                else {
                ctx.state->dragging = true;
                ctx.state->dragStartX = ev.button.x;
                ctx.state->dragStartY = ev.button.y;
                }
                ctx.state->lastLeftClickTime = currentTime;
                ctx.state->lastLeftClickX = ev.button.x;
                ctx.state->lastLeftClickY = ev.button.y;
                break;
            }
            case SDL_BUTTON_RIGHT: {

                ctx.state->showContextMenu = !ctx.state->showContextMenu;
                ctx.state->contextMenuX = ev.button.x;
                ctx.state->contextMenuY = ev.button.y;
                break;
            }
            }
            break;

            if (ev.button.button == SDL_BUTTON_LEFT && ctx.state->showContextMenu) {
                const int menuWidth = 180;
                const int itemHeight = 24;
                int menuX = ctx.state->contextMenuX;
                int menuY = ctx.state->contextMenuY;

                int winW, winH;
                SDL_GetWindowSize(ctx.win, &winW, &winH);
                if (menuX + menuWidth > winW) menuX = winW - menuWidth;
                if (menuY + static_cast<int>(ctx.contextMenuItems.size()) * itemHeight > winH)
                    menuY = winH - static_cast<int>(ctx.contextMenuItems.size()) * itemHeight;
                menuX = std::max(0, menuX);
                menuY = std::max(0, menuY);

                int localY = ev.button.y - menuY;
                size_t index = static_cast<size_t>(localY / itemHeight);

                if (index < ctx.contextMenuItems.size() &&
                    ev.button.x >= menuX &&
                    ev.button.x < menuX + menuWidth) {
                    ctx.contextMenuItems[index].action();
                    ctx.state->showContextMenu = false;
                } else {
                    ctx.state->showContextMenu = false;
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (ev.button.button == SDL_BUTTON_LEFT) {
                ctx.state->dragging = false;
            }
            break;
        }

        case SDL_EVENT_MOUSE_MOTION: {
            if (ctx.state->dragging) {
                int dx = ev.motion.x - ctx.state->dragStartX;
                int dy = ev.motion.y - ctx.state->dragStartY;
                ctx.state->offsetX += dx;
                ctx.state->offsetY += dy;
                ctx.state->dragStartX = ev.motion.x;
                ctx.state->dragStartY = ev.motion.y;
            }
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL: {
            const float zoomFactor = 1.1f;
            if (ev.wheel.y > 0) {
                ctx.state->scale *= zoomFactor;
            }
            else if (ev.wheel.y < 0) {
                ctx.state->scale /= zoomFactor;
            }
            ctx.state->scale = std::clamp(ctx.state->scale, 0.1f, 5.0f);
            break;
        }

        }
    }

}

static void updateTiming(AppContext &ctx) {
    Uint64 now = SDL_GetPerformanceCounter();
    ctx.state->dt = (now - ctx.state->perfStart) / ctx.state->perfFreq;
    ctx.state->perfStart = now;
    ctx.state->globalTime += ctx.state->dt;
}

static void updateAudioState(AppContext& ctx) {
    ctx.state->prevSpeak = ctx.state->speak;
    ctx.state->speak = false;

    if (!ctx.stream) return;

    int avail = SDL_GetAudioStreamAvailable(ctx.stream);
    if (avail <= 0) return;

    int toRead = std::min(avail, 4096);
    std::vector<float> buffer(toRead / sizeof(float));
    int got = SDL_GetAudioStreamData(ctx.stream, buffer.data(), toRead);
    if (got <= 0) return;

    int samples = got / sizeof(float);
    double sum = 0.0;
    for (int i = 0; i < samples; ++i) {
        double v = buffer[i] * 2.0 * (1.0f + ctx.cfg.micGain);
        sum += v * v;
    }
    double rms = std::sqrt(sum / samples);
    ctx.state->speak = (rms > ctx.cfg.micThreshold);
}

// todo: Доделать дыхание (чтобы был разговор на выдохе)
// fixme: Дыхание не учитывает смещение спрайтов при центровке

static void updateBreathing(const AppContext& ctx) {
    if (!ctx.cfg.enableBreathing) {
        ctx.state->isBreathing = false;
        ctx.state->breathScale = 1.0f;
        return;
    }

    ctx.state->isBreathing = true;

    const double BREATH_IDLE_SPEED = 2.0 * PI / 6.0;
    const double BREATH_INHALE_SPEED = 2.0 * PI / 2.0;
    const double BREATH_EXHALE_SPEED = 2.0 * PI / 3.0;

    if (ctx.state->speak) {
        ctx.state->isBreathing = true;
        ctx.state->breathPhase += ctx.state->dt * BREATH_EXHALE_SPEED;
    }
    else if (!ctx.state->speak && ctx.state->prevSpeak) {
        ctx.state->isBreathing = true;

    }
    else if (!ctx.state->speak && ctx.state->isBreathing) {

        ctx.state->breathPhase -= ctx.state->dt * BREATH_IDLE_SPEED;

    }

    // state.breathPhase = fmod(state.breathPhase, 2.0 * PI);

    ctx.state->breathScale = ctx.state->isBreathing ? (1.0f + 0.03f * std::sin(ctx.state->breathPhase)) : 1.0f;
}

static void updateBlinking(AppContext &ctx) {
    const Uint32 BLINK_INTERVAL = 3000;
    const Uint32 BLINK_DURATION = 200;
    Uint32 nowMs = SDL_GetTicks();

    if (!ctx.state->blink && nowMs - ctx.state->lastBlink >= BLINK_INTERVAL) {
        ctx.state->blink = true;
        ctx.state->blinkStart = nowMs;
        ctx.state->lastBlink = nowMs;
    }
    if (ctx.state->blink && nowMs - ctx.state->blinkStart >= BLINK_DURATION) {
        ctx.state->blink = false;
    }
}

static void renderFrame(AppContext& ctx, int frameIndex) {
    switch (ctx.cfg.useCpuRendering) {
    case true:
        renderFrameCpu(ctx, frameIndex);
        break;
    default:
        renderFrameGpu(ctx, frameIndex);
    }
}

static void maybeRender(AppContext& ctx) {
    int frameIndex = (ctx.state->speak ? 1 : 0) + (ctx.state->blink ? 2 : 0);
    bool needsRender = ctx.state->speak || ctx.state->isBreathing || ctx.state->blink || (ctx.state->prevFrameIndex != frameIndex);

    if (!needsRender) return;
    ctx.state->prevFrameIndex = frameIndex;

    renderFrame(ctx, frameIndex);
}

static void runMainLoop(AppContext& ctx) {
    initializeMainLoopState(ctx);
    
    renderFrame(ctx, 0);

    while (ctx.state->running) {
        Uint32 frameStart = SDL_GetTicks();
        handleEvents(ctx);
        updateTiming(ctx);
        updateAudioState(ctx);
        updateBreathing(ctx);
        updateBlinking(ctx);
        maybeRender(ctx);
        Uint32 frameTime = SDL_GetTicks() - frameStart;
        Uint32 target = 1000 / ctx.cfg.fps;
        if (frameTime < target) {
            SDL_Delay(target - frameTime);
        }
        frameStart = SDL_GetTicks();

        if (!g_globalRunning) {
            ctx.state->running = false;
        }

        if (ctx.state->showContextMenu) {
            SDL_SetRenderDrawColor(ctx.ren, 50, 50, 50, 255);
            SDL_FRect menuRect = { static_cast<float>(ctx.state->contextMenuX), static_cast<float>(ctx.state->contextMenuY), 150.0f, 100.0f };
            SDL_RenderFillRect(ctx.ren, &menuRect);        
        }
    }
}


SDL_Surface* loadAndConvert(const char* path, SDL_PixelFormat& targetFormat) {
    SDL_Surface* src = IMG_Load(path);
    if (!src) return nullptr;
    SDL_Surface* dst = SDL_ConvertSurface(src, targetFormat);
    SDL_DestroySurface(src);
    return dst;
}

static bool initSDL(AppContext& ctx, const AppConfig& cfg) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return false;
    }

    TTF_Init();

    Uint8 r = (ctx.cfg.bgColor >> 16) & 0xFF;
    Uint8 g = (ctx.cfg.bgColor >> 8) & 0xFF;
    Uint8 b = (ctx.cfg.bgColor >> 0) & 0xFF;

    if (cfg.useCpuRendering) {
        ctx.win = SDL_CreateWindow(APP_NAME.c_str(), cfg.windowWidth, cfg.windowHeight, SDL_WINDOW_RESIZABLE & SDL_WINDOWPOS_CENTERED);
        if (!ctx.win) { /* ошибка */ }

        ctx.winSurface = SDL_GetWindowSurface(ctx.win);
        SDL_Surface* sprite = loadAndConvert("avatar.png", ctx.winSurface->format);
        if (!ctx.winSurface) {
            std::cerr << "Failed to get window surface for CPU rendering\n";
            return false;
        }

        loadSpritesCpu(ctx.sprites, ctx.keymap, cfg.spriteDir, ctx, cfg.alignment);
    }
    else {

        ctx.win = SDL_CreateWindow(APP_NAME.c_str(), cfg.windowWidth, cfg.windowHeight, SDL_WINDOW_RESIZABLE & SDL_WINDOWPOS_CENTERED);
        if (!ctx.win) {
            std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
            SDL_Quit();
            return false;
        }

        SDL_SetWindowPosition(ctx.win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        ctx.ren = SDL_CreateRenderer(ctx.win, nullptr);
        if (!ctx.ren) {
            std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
            SDL_DestroyWindow(ctx.win);
            SDL_Quit();
            return false;
        }

        SDL_Surface* icon = IMG_Load("icon.ico");
        if (icon) {
            SDL_SetWindowIcon(ctx.win, icon);
            SDL_DestroySurface(icon);
        }

        //// Загрузочный экран
        //SDL_Texture* loading = IMG_LoadTexture(ctx.ren, "loading.png");
        //if (loading) {
        //    for (int i = 0; i < 60; ++i) { // о боже
        //        SDL_SetRenderDrawColor(ctx.ren, r, g, b, 255);
        //        SDL_RenderClear(ctx.ren);
        //        float w, h;
        //        SDL_GetTextureSize(loading, &w, &h);
        //        SDL_FRect dst = { (cfg.windowWidth - w) / 2, (cfg.windowHeight - h) / 2, w, h };
        //        SDL_RenderTexture(ctx.ren, loading, nullptr, &dst);
        //        SDL_RenderPresent(ctx.ren);
        //        SDL_Delay(16);
        //    }
        //    SDL_DestroyTexture(loading);
        //}

        loadSprites(ctx.ren, cfg.spriteDir, ctx.sprites, ctx.keymap, cfg.alignment);
        if (ctx.sprites.empty()) {
            std::cerr << "No PNG sprites found.\n";
            SDL_DestroyRenderer(ctx.ren);
            SDL_DestroyWindow(ctx.win);
            SDL_Quit();
            return false;
        }

        ctx.state->menuFont = TTF_OpenFont("C:\\Windows\\Fonts\\consola.ttf", 16);
        updateContextMenuTextures(ctx);
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq = 8000;
    SDL_AudioDeviceID dev = findMicByName(cfg.micName);
    ctx.stream = SDL_OpenAudioDeviceStream(dev, &spec, nullptr, nullptr);
    if (ctx.stream) {
        if (SDL_ResumeAudioStreamDevice(ctx.stream) < 0) {
            std::cerr << "Failed to start audio stream: " << SDL_GetError() << '\n';
            ctx.stream = nullptr;
        }
        else {
            std::cout << "Audio capture started\n";
        }
    }

    return true;
}


int main(int argc, char** argv) {
    (void)argc; (void)argv;
    fs::path exeDir = fs::current_path();
    AppConfig cfg = loadConfig(exeDir);

    AppContext ctx;
    ctx.nThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency() / 2) + 1);
    ctx.cfg = cfg;

    MainLoopState* state = new MainLoopState();
    ctx.state = state;

    WS::openWebSocket(ctx.cfg.webSocket);

    if (cfg.globalHookingAcceptable) {
        InstallGlobalKeyboardHook(); // загружается та версия хука, которая нужна платформе (точнее будет, как сделаю)
    }

    ctx.contextMenuItems = {
        { "Перечитать конфиг", [&ctx]() {
             ctx.state->showContextMenu = false;
         }},
        { "Дебаг-режим", [&ctx]() {
             ctx.state->showContextMenu = false;
             ctx.state->debug = !ctx.state->debug;
         }},
        { "Поставить тут PatPat", [&ctx]() {
             ctx.state->showContextMenu = false;
             // todo: Прикрутить твич
         }},
        { "Использовать WebRender", [&ctx]() {
             ctx.state->showContextMenu = false;
             // todo: Прикрутить веб-рендер и сокеты (сделать веб-рендер)
         }}
    };
     

    if (!initSDL(ctx, cfg)) {
        UninstallGlobalKeyboardHook();
        return 1;
    }

    g_globalRunning = true;

    runMainLoop(ctx);

    UninstallGlobalKeyboardHook();

    if (ctx.stream) SDL_DestroyAudioStream(ctx.stream);
    for (auto& s : ctx.sprites) {
        if (s.tex) SDL_DestroyTexture(s.tex);
    }
    SDL_DestroyRenderer(ctx.ren);
    SDL_DestroyWindow(ctx.win);
    WS::closeWebSocket(ctx.cfg.webSocket);
    SDL_Quit();
    return 0;
}
