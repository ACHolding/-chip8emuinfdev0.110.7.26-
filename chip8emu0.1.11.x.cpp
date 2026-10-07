// chip8emulator 0.1.1 — mGBA-styled CHIP-8 emulator shell
#include <SDL.h>
#include <SDL_ttf.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <fstream>

static const int WIN_W = 600;
static const int WIN_H = 400;
static const int CHIP_W = 64;
static const int CHIP_H = 32;

// mGBA-inspired blue palette
static const SDL_Color COL_BG       = {0x1A, 0x3A, 0x5C, 255}; // deep blue bg
static const SDL_Color COL_PANEL    = {0x14, 0x2E, 0x4A, 255}; // darker panel
static const SDL_Color COL_TEXT     = {0x6E, 0xC6, 0xFF, 255}; // blue hue text
static const SDL_Color COL_TEXT_DIM = {0x4A, 0x8A, 0xB8, 255};
static const SDL_Color COL_BTN      = {0x00, 0x00, 0x00, 255}; // black buttons
static const SDL_Color COL_BTN_HOV  = {0x1A, 0x1A, 0x1A, 255};
static const SDL_Color COL_BORDER   = {0x3A, 0x7A, 0xB0, 255};
static const SDL_Color COL_SCREEN   = {0x0A, 0x1A, 0x2A, 255};
static const SDL_Color COL_PIXEL    = {0x70, 0xD0, 0xFF, 255};

struct Button {
    SDL_Rect rect;
    const char* label;
    bool hovered;
};

struct Chip8 {
    uint8_t  memory[4096]{};
    uint8_t  V[16]{};
    uint16_t I = 0;
    uint16_t pc = 0x200;
    uint16_t stack[16]{};
    uint8_t  sp = 0;
    uint8_t  delay_timer = 0;
    uint8_t  sound_timer = 0;
    uint8_t  gfx[CHIP_W * CHIP_H]{};
    uint8_t  keys[16]{};
    bool     draw_flag = false;
    bool     running = false;
    bool     rom_loaded = false;

    void reset() {
        std::memset(memory, 0, sizeof(memory));
        std::memset(V, 0, sizeof(V));
        std::memset(stack, 0, sizeof(stack));
        std::memset(gfx, 0, sizeof(gfx));
        std::memset(keys, 0, sizeof(keys));
        I = 0; pc = 0x200; sp = 0;
        delay_timer = sound_timer = 0;
        draw_flag = true; running = false; rom_loaded = false;

        static const uint8_t fontset[80] = {
            0xF0,0x90,0x90,0x90,0xF0, 0x20,0x60,0x20,0x20,0x70,
            0xF0,0x10,0xF0,0x80,0xF0, 0xF0,0x10,0xF0,0x10,0xF0,
            0x90,0x90,0xF0,0x10,0x10, 0xF0,0x80,0xF0,0x10,0xF0,
            0xF0,0x80,0xF0,0x90,0xF0, 0xF0,0x10,0x20,0x40,0x40,
            0xF0,0x90,0xF0,0x90,0xF0, 0xF0,0x90,0xF0,0x10,0xF0,
            0xF0,0x90,0x90,0x90,0x90, 0xE0,0x90,0xE0,0x90,0xE0,
            0xF0,0x80,0x80,0x80,0xF0, 0xE0,0x90,0x90,0x90,0xE0,
            0xF0,0x80,0xF0,0x80,0xF0, 0xF0,0x80,0xF0,0x80,0x80
        };
        std::memcpy(memory, fontset, 80);
    }

    bool load_rom(const char* path) {
        reset();
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return false;
        auto size = f.tellg();
        if (size <= 0 || size > 3584) return false;
        f.seekg(0, std::ios::beg);
        f.read(reinterpret_cast<char*>(memory + 0x200), size);
        rom_loaded = true;
        running = true;
        draw_flag = true;
        return true;
    }

    void cycle() {
        if (!running || !rom_loaded) return;
        uint16_t op = (memory[pc] << 8) | memory[pc + 1];
        pc += 2;
        uint8_t x = (op & 0x0F00) >> 8;
        uint8_t y = (op & 0x00F0) >> 4;
        uint8_t n = op & 0x000F;
        uint8_t kk = op & 0x00FF;
        uint16_t nnn = op & 0x0FFF;

        switch (op & 0xF000) {
        case 0x0000:
            if (op == 0x00E0) { std::memset(gfx, 0, sizeof(gfx)); draw_flag = true; }
            else if (op == 0x00EE) { if (sp > 0) pc = stack[--sp]; }
            break;
        case 0x1000: pc = nnn; break;
        case 0x2000: if (sp < 16) stack[sp++] = pc; pc = nnn; break;
        case 0x3000: if (V[x] == kk) pc += 2; break;
        case 0x4000: if (V[x] != kk) pc += 2; break;
        case 0x5000: if (V[x] == V[y]) pc += 2; break;
        case 0x6000: V[x] = kk; break;
        case 0x7000: V[x] += kk; break;
        case 0x8000:
            switch (n) {
            case 0x0: V[x] = V[y]; break;
            case 0x1: V[x] |= V[y]; break;
            case 0x2: V[x] &= V[y]; break;
            case 0x3: V[x] ^= V[y]; break;
            case 0x4: { uint16_t s = V[x] + V[y]; V[0xF] = s > 255; V[x] = (uint8_t)s; } break;
            case 0x5: V[0xF] = V[x] >= V[y]; V[x] -= V[y]; break;
            case 0x6: V[0xF] = V[x] & 1; V[x] >>= 1; break;
            case 0x7: V[0xF] = V[y] >= V[x]; V[x] = V[y] - V[x]; break;
            case 0xE: V[0xF] = (V[x] & 0x80) >> 7; V[x] <<= 1; break;
            }
            break;
        case 0x9000: if (V[x] != V[y]) pc += 2; break;
        case 0xA000: I = nnn; break;
        case 0xB000: pc = nnn + V[0]; break;
        case 0xC000: V[x] = (std::rand() % 256) & kk; break;
        case 0xD000: {
            uint8_t px = V[x] % CHIP_W, py = V[y] % CHIP_H;
            V[0xF] = 0;
            for (int row = 0; row < n; ++row) {
                if (py + row >= CHIP_H) break;
                uint8_t sprite = memory[I + row];
                for (int col = 0; col < 8; ++col) {
                    if (px + col >= CHIP_W) break;
                    if (sprite & (0x80 >> col)) {
                        int idx = (py + row) * CHIP_W + (px + col);
                        if (gfx[idx]) V[0xF] = 1;
                        gfx[idx] ^= 1;
                    }
                }
            }
            draw_flag = true;
        } break;
        case 0xE000: {
            uint8_t kx = V[x] & 0xF;
            if (kk == 0x9E) { if (keys[kx]) pc += 2; }
            else if (kk == 0xA1) { if (!keys[kx]) pc += 2; }
        } break;
        case 0xF000:
            switch (kk) {
            case 0x07: V[x] = delay_timer; break;
            case 0x0A: {
                bool pressed = false;
                for (int i = 0; i < 16; ++i) if (keys[i]) { V[x] = i; pressed = true; break; }
                if (!pressed) pc -= 2;
            } break;
            case 0x15: delay_timer = V[x]; break;
            case 0x18: sound_timer = V[x]; break;
            case 0x1E: { uint32_t ni = (uint32_t)I + V[x]; V[0xF] = ni > 0xFFF; I = (uint16_t)(ni & 0xFFF); } break;
            case 0x29: I = V[x] * 5; break;
            case 0x33: memory[I] = V[x]/100; memory[I+1] = (V[x]/10)%10; memory[I+2] = V[x]%10; break;
            case 0x55: for (int i = 0; i <= x; ++i) memory[I + i] = V[i]; break;
            case 0x65: for (int i = 0; i <= x; ++i) V[i] = memory[I + i]; break;
            }
            break;
        }
    }

    void tick_timers() {
        if (delay_timer > 0) --delay_timer;
        if (sound_timer > 0) --sound_timer;
    }
};

static void fill_rect(SDL_Renderer* r, const SDL_Rect& rect, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rect);
}

static void draw_rect(SDL_Renderer* r, const SDL_Rect& rect, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(r, &rect);
}

static void draw_text(SDL_Renderer* ren, TTF_Font* font, const char* text,
                      int x, int y, SDL_Color color) {
    if (!font || !text || !*text) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    if (tex) {
        SDL_Rect dst{x, y, surf->w, surf->h};
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

static void draw_text_centered(SDL_Renderer* ren, TTF_Font* font, const char* text,
                               const SDL_Rect& box, SDL_Color color) {
    if (!font || !text) return;
    int tw = 0, th = 0;
    TTF_SizeUTF8(font, text, &tw, &th);
    draw_text(ren, font, text, box.x + (box.w - tw) / 2, box.y + (box.h - th) / 2, color);
}

static bool point_in(const SDL_Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// CHIP-8 keypad -> QWERTY
static int map_key(SDL_Keycode k) {
    switch (k) {
    case SDLK_x: return 0x0; case SDLK_1: return 0x1; case SDLK_2: return 0x2;
    case SDLK_3: return 0x3; case SDLK_q: return 0x4; case SDLK_w: return 0x5;
    case SDLK_e: return 0x6; case SDLK_a: return 0x7; case SDLK_s: return 0x8;
    case SDLK_d: return 0x9; case SDLK_z: return 0xA; case SDLK_c: return 0xB;
    case SDLK_4: return 0xC; case SDLK_r: return 0xD; case SDLK_f: return 0xE;
    case SDLK_v: return 0xF; default: return -1;
    }
}

static TTF_Font* open_font(int size) {
    const char* paths[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/SFNSMono.ttf",
        nullptr
    };
    for (int i = 0; paths[i]; ++i) {
        TTF_Font* f = TTF_OpenFont(paths[i], size);
        if (f) return f;
    }
    return nullptr;
}

static std::string basename_of(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Implemented in macos_filedialog.mm (native NSOpenPanel). Returns malloc'd C string or null.
extern "C" const char* macos_pick_rom_path_c(void);

static std::string pick_rom_path() {
    const char* p = macos_pick_rom_path_c();
    if (!p) return {};
    std::string out(p);
    std::free(const_cast<char*>(p));
    return out;
}

static bool try_open_rom(Chip8& chip, std::string& rom_name, std::string& status) {
    status = "Opening file dialog…";
    std::string path = pick_rom_path();
    if (path.empty()) {
        status = "Open cancelled";
        return false;
    }
    if (chip.load_rom(path.c_str())) {
        rom_name = basename_of(path);
        status = "Running";
        return true;
    }
    status = "Failed to load: " + basename_of(path);
    return false;
}

int main(int argc, char* argv[]) {
    std::srand((unsigned)std::time(nullptr));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        std::fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Window* win = SDL_CreateWindow(
        "chip8emulator 0.1.1",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!win) {
        std::fprintf(stderr, "Window: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);

    TTF_Font* font_title = open_font(16);
    TTF_Font* font_ui    = open_font(13);
    TTF_Font* font_small = open_font(11);

    Chip8 chip;
    chip.reset();

    std::string status = "Drop a .ch8 ROM or use Open";
    std::string rom_name = "(no ROM)";

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    if (argc > 1) {
        if (chip.load_rom(argv[1])) {
            rom_name = basename_of(argv[1]);
            status = "Running";
        } else {
            status = "Failed to load ROM";
        }
    }

    // Layout: mGBA-ish — title bar, screen, sidebar buttons
    SDL_Rect screen_frame{12, 36, 384, 192}; // 64x32 * 6
    SDL_Rect screen_inner{screen_frame.x + 2, screen_frame.y + 2,
                          screen_frame.w - 4, screen_frame.h - 4};

    Button buttons[] = {
        {{420, 40, 160, 28},  "Open ROM",   false},
        {{420, 76, 160, 28},  "Pause/Play", false},
        {{420, 112, 160, 28}, "Reset",      false},
        {{420, 148, 160, 28}, "Step",       false},
        {{420, 184, 160, 28}, "Quit",       false},
    };
    const int nbtn = 5;

    SDL_Texture* fb = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_STREAMING, CHIP_W, CHIP_H);
    SDL_PixelFormat* pixfmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA32);
    const Uint32 px_on  = pixfmt ? SDL_MapRGBA(pixfmt, COL_PIXEL.r, COL_PIXEL.g, COL_PIXEL.b, 255) : 0xFF70D0FF;
    const Uint32 px_off = pixfmt ? SDL_MapRGBA(pixfmt, COL_SCREEN.r, COL_SCREEN.g, COL_SCREEN.b, 255) : 0xFF0A1A2A;

    bool quit = false;
    Uint32 last_timer = SDL_GetTicks();
    const int CYCLES_PER_FRAME = 10;

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                int k = map_key(e.key.keysym.sym);
                if (k >= 0) chip.keys[k] = (e.type == SDL_KEYDOWN);
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) quit = true;
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_SPACE)
                    chip.running = !chip.running;
            } else if (e.type == SDL_MOUSEMOTION) {
                int mx = e.motion.x, my = e.motion.y;
                int ww = 0, wh = 0;
                SDL_GetWindowSize(win, &ww, &wh);
                if (ww > 0 && wh > 0 && (ww != WIN_W || wh != WIN_H)) {
                    mx = mx * WIN_W / ww;
                    my = my * WIN_H / wh;
                }
                for (int i = 0; i < nbtn; ++i)
                    buttons[i].hovered = point_in(buttons[i].rect, mx, my);
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                // Use window coords (Retina-safe): scale from drawable if needed
                int mx = e.button.x, my = e.button.y;
                int ww = 0, wh = 0;
                SDL_GetWindowSize(win, &ww, &wh);
                if (ww > 0 && wh > 0 && (ww != WIN_W || wh != WIN_H)) {
                    mx = mx * WIN_W / ww;
                    my = my * WIN_H / wh;
                }
                for (int i = 0; i < nbtn; ++i) {
                    if (!point_in(buttons[i].rect, mx, my)) continue;
                    if (i == 0) {
                        status = "Choose a ROM…";
                        SDL_RenderPresent(ren); // flush status before modal dialog
                        std::string path = pick_rom_path();
                        SDL_RaiseWindow(win);
                        if (path.empty()) {
                            status = "Open cancelled";
                        } else if (chip.load_rom(path.c_str())) {
                            rom_name = basename_of(path);
                            status = "Running";
                        } else {
                            status = "Failed to load: " + basename_of(path);
                        }
                    } else if (i == 1) {
                        if (chip.rom_loaded) {
                            chip.running = !chip.running;
                            status = chip.running ? "Running" : "Paused";
                        }
                    } else if (i == 2) {
                        if (chip.rom_loaded) {
                            std::uint8_t backup[3584];
                            std::memcpy(backup, chip.memory + 0x200, 3584);
                            chip.reset();
                            std::memcpy(chip.memory + 0x200, backup, 3584);
                            chip.rom_loaded = true;
                            chip.running = true;
                            status = "Reset";
                        }
                    } else if (i == 3) {
                        if (chip.rom_loaded) {
                            chip.running = false;
                            chip.cycle();
                            status = "Stepped";
                        }
                    } else if (i == 4) {
                        quit = true;
                    }
                }
            } else if (e.type == SDL_DROPFILE) {
                char* file = e.drop.file;
                if (chip.load_rom(file)) {
                    rom_name = basename_of(file);
                    status = "Running";
                } else {
                    status = "Bad ROM";
                }
                SDL_free(file);
            }
        }

        if (chip.running) {
            for (int i = 0; i < CYCLES_PER_FRAME; ++i) chip.cycle();
        }

        Uint32 now = SDL_GetTicks();
        if (now - last_timer >= 16) {
            chip.tick_timers();
            last_timer = now;
        }

        // Upload framebuffer
        if (chip.draw_flag || true) {
            void* pixels; int pitch;
            if (SDL_LockTexture(fb, nullptr, &pixels, &pitch) == 0) {
                auto* row = static_cast<uint8_t*>(pixels);
                for (int y = 0; y < CHIP_H; ++y) {
                    auto* p = reinterpret_cast<uint32_t*>(row + y * pitch);
                    for (int x = 0; x < CHIP_W; ++x) {
                        p[x] = chip.gfx[y * CHIP_W + x] ? px_on : px_off;
                    }
                }
                SDL_UnlockTexture(fb);
            }
            chip.draw_flag = false;
        }

        // Draw UI
        SDL_SetRenderDrawColor(ren, COL_BG.r, COL_BG.g, COL_BG.b, 255);
        SDL_RenderClear(ren);

        // Title bar strip
        SDL_Rect titlebar{0, 0, WIN_W, 28};
        fill_rect(ren, titlebar, COL_PANEL);
        draw_rect(ren, titlebar, COL_BORDER);
        draw_text(ren, font_title, "chip8emulator 0.1.1", 10, 5, COL_TEXT);

        // Screen frame
        fill_rect(ren, screen_frame, COL_BORDER);
        fill_rect(ren, screen_inner, COL_SCREEN);
        SDL_RenderCopy(ren, fb, nullptr, &screen_inner);

        // Sidebar panel
        SDL_Rect side{408, 36, 180, 192};
        fill_rect(ren, side, COL_PANEL);
        draw_rect(ren, side, COL_BORDER);

        for (int i = 0; i < nbtn; ++i) {
            fill_rect(ren, buttons[i].rect, buttons[i].hovered ? COL_BTN_HOV : COL_BTN);
            draw_rect(ren, buttons[i].rect, COL_BORDER);
            draw_text_centered(ren, font_ui, buttons[i].label, buttons[i].rect, COL_TEXT);
        }

        // Info panel
        SDL_Rect info{12, 240, 576, 148};
        fill_rect(ren, info, COL_PANEL);
        draw_rect(ren, info, COL_BORDER);

        draw_text(ren, font_ui, "ROM:", 24, 252, COL_TEXT_DIM);
        draw_text(ren, font_ui, rom_name.c_str(), 70, 252, COL_TEXT);

        draw_text(ren, font_ui, "Status:", 24, 274, COL_TEXT_DIM);
        draw_text(ren, font_ui, status.c_str(), 90, 274, COL_TEXT);

        char regs[128];
        std::snprintf(regs, sizeof(regs), "PC:%03X  I:%03X  DT:%02X  ST:%02X",
                      chip.pc, chip.I, chip.delay_timer, chip.sound_timer);
        draw_text(ren, font_small, regs, 24, 300, COL_TEXT);

        draw_text(ren, font_small,
                  "Keys: 1 2 3 4   Q W E R   A S D F   Z X C V     Space=Pause  Esc=Quit",
                  24, 324, COL_TEXT_DIM);
        draw_text(ren, font_small,
                  "Drag & drop a .ch8 / .c8 ROM onto the window  |  mGBA blue theme",
                  24, 348, COL_TEXT_DIM);

        SDL_RenderPresent(ren);
    }

    if (pixfmt) SDL_FreeFormat(pixfmt);
    if (fb) SDL_DestroyTexture(fb);
    if (font_title) TTF_CloseFont(font_title);
    if (font_ui) TTF_CloseFont(font_ui);
    if (font_small) TTF_CloseFont(font_small);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
