#include <sysutil/sysutil.h>
#include "DxLib.h"
#include <unistd.h>


// Variables externas de control del sistema
extern int mainZ;

// Objetos del renderizador RSX acelerado por hardware de SDL
static SDL_Window *ps3_window = NULL;
static SDL_Renderer *ps3_renderer = NULL;
static SDL_Texture *ps3_texture = NULL;
SDL_Surface *screen = NULL;
bool fullscreen_stretch = false;
bool sound = true;

static bool btn_start_held = false;
static bool btn_select_held = false;

SDL_Joystick* joystick;
bool keysHeld[1024];

static inline int normalizeKey(int k) {
    if (k & 0x40000000) {
        return 256 + (k & 0x1FF); // Mapea scancodes en el rango 256-767
    }
    if (k >= 0 && k < 256) {
        return k; // Teclas ASCII normales
    }
    return 0;
}

static inline void setKeyHeld(int k, bool state) {
    int idx = normalizeKey(k);
    if (idx >= 0 && idx < 1024) {
        keysHeld[idx] = state;
    }
}

static inline bool getKeyHeld(int k) {
    int idx = normalizeKey(k);
    if (idx >= 0 && idx < 1024) {
        return keysHeld[idx];
    }
    return false;
}

void deinit();

static std::string fixPath(const char* p) {
    if (!p) return "";
    if (p[0] == '/') return std::string(p);
    return std::string("/dev_hdd0/game/SYOB00001/USRDIR/") + p;
}

int DxLib_Init()
{
    atexit(deinit);
    setlocale(LC_CTYPE, "ja_JP.UTF-8");

    // 1. Inicializar SDL completo (Video RSX, Audio y Joystick)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "Unable to init SDL: %s\n", SDL_GetError());
        return -1;
    }

    // 2. Crear ventana nativa PS3 (1280x720)
    ps3_window = SDL_CreateWindow("Syobon Action",
                                  SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  1280, 720,
                                  SDL_WINDOW_SHOWN);
    if (!ps3_window) {
        fprintf(stderr, "Unable to create SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    // 3. Crear renderizador RSX con aceleración por hardware y VSync
    ps3_renderer = SDL_CreateRenderer(ps3_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ps3_renderer) {
        fprintf(stderr, "Unable to create RSX renderer: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    // 4. Crear textura streaming nativa de Cat Mario (480x420) en VRAM de RSX
    ps3_texture = SDL_CreateTexture(ps3_renderer,
                                    SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    480, 420);
    if (!ps3_texture) {
        fprintf(stderr, "Unable to create RSX streaming texture: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    // 4. Crear lienzo virtual en RAM con la resolución exacta de Cat Mario (480x420)
    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
    screen = SDL_CreateRGBSurface(SDL_SWSURFACE, 480, 420, 32,
                                 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000);
    #else
    screen = SDL_CreateRGBSurface(SDL_SWSURFACE, 480, 420, 32,
                                 0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000);
    #endif

    if (!screen) {
        fprintf(stderr, "Unable to create virtual screen: %s\n", SDL_GetError());
        return -1;
    }

    if (IMG_Init(IMG_INIT_PNG) != IMG_INIT_PNG) {
        fprintf(stderr, "Unable to init SDL_img: %s\n", IMG_GetError());
        return -1;
    }

    // Inicializar fuente TTF
    if (TTF_Init() == -1) {
        fprintf(stderr, "Unable to init SDL_ttf: %s\n", TTF_GetError());
        return -1;
    }

    // Inicializar Audio
#define AUDIO_CHANNELS 2
    if (sound && Mix_OpenAudio(22050, AUDIO_S16SYS, AUDIO_CHANNELS, 4096)) {
        fprintf(stderr, "Unable to init SDL_mixer: %s\n", Mix_GetError());
        sound = false;
    }

    // Conectar mando
    joystick = SDL_JoystickOpen(0);

    for (int i = 0; i < 1024; i++)
        keysHeld[i] = false;
    for (int i = 0; i < FONT_MAX; i++)
        font[i] = NULL;
    srand(time(NULL));

    return 0;
}

//Fonts
byte fontsize = 0;
TTF_Font *font[FONT_MAX];

//Strings
void SetFontSize(byte size)
{
    fontsize = size;
    if (font[size] == NULL) {
        font[size] = TTF_OpenFont(fixPath("res/sazanami-gothic.ttf").c_str(), size);
        if (font[size] == NULL) {
            printf("Unable to load font: %s\n", TTF_GetError());
            exit(1);
        }
    }
}

byte fontType = DX_FONTTYPE_NORMAL;
void ChangeFontType(byte type)
{
    fontType = type;
}

void DrawString(int a, int b, const char *x, Uint32 c)
{
    SDL_Color color = { c >> 16, c >> 8, c };
    SDL_Surface *rendered = TTF_RenderUTF8_Solid(font[fontsize], x, color);
    if (fontType == DX_FONTTYPE_EDGE) {
	SDL_Color blk = { 0, 0, 0 };
	SDL_Surface *shadow = TTF_RenderUTF8_Solid(font[fontsize], x, blk);
	DrawGraphZ(a - 1, b - 1, shadow);
	DrawGraphZ(a, b - 1, shadow);
	DrawGraphZ(a + 1, b - 1, shadow);
	DrawGraphZ(a - 1, b, shadow);
	DrawGraphZ(a + 1, b, shadow);
	DrawGraphZ(a - 1, b + 1, shadow);
	DrawGraphZ(a, b + 1, shadow);
	DrawGraphZ(a + 1, b + 1, shadow);
	SDL_FreeSurface(shadow);
    }
    DrawGraphZ(a, b, rendered);
    SDL_FreeSurface(rendered);
}

void DrawFormatString(int a, int b, Uint32 color, const char *str, ...)
{
    va_list args;
    char *newstr = new char[strlen(str) + 16];
    va_start(args, str);
    vsprintf(newstr, str, args);
    va_end(args);
    DrawString(a, b, newstr, color);
    delete newstr;
}

//void DrawFormatString(int a, int b, int c

//Key Aliases
#define KEY_INPUT_ESCAPE SDLK_ESCAPE

bool ex = false;

void UpdateKeys()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_KEYDOWN:
            setKeyHeld(event.key.keysym.sym, true);
            break;
        case SDL_KEYUP:
            setKeyHeld(event.key.keysym.sym, false);
            break;
        case SDL_JOYAXISMOTION:
            if(event.jaxis.which == 0)
            {
                if(event.jaxis.axis == JOYSTICK_XAXIS)
                {
                    if(event.jaxis.value < -16000) setKeyHeld(SDLK_LEFT, true);
                    else if(event.jaxis.value > 16000) setKeyHeld(SDLK_RIGHT, true);
                    else {
                        setKeyHeld(SDLK_LEFT, false);
                        setKeyHeld(SDLK_RIGHT, false);
                    }
                }
                else if(event.jaxis.axis == JOYSTICK_YAXIS)
                {
                    if(event.jaxis.value < -16000) setKeyHeld(SDLK_UP, true);
                    else if(event.jaxis.value > 16000) setKeyHeld(SDLK_DOWN, true);
                    else {
                        setKeyHeld(SDLK_UP, false);
                        setKeyHeld(SDLK_DOWN, false);
                    }
                }
            }
            break;
case SDL_JOYBUTTONDOWN:
            switch(event.jbutton.button) {
                case 0: setKeyHeld(SDLK_LEFT, true); break;
                case 1: setKeyHeld(SDLK_DOWN, true); break;
                case 2: setKeyHeld(SDLK_RIGHT, true); break;
                case 3: setKeyHeld(SDLK_UP, true); break;
                case 4: // START
                    btn_start_held = true;
                    if (btn_select_held) {
                        // COMBO SELECT + START -> SALIR AL XMB
                        Mix_HaltMusic();
                        ex = true;
                        break;
                    }
                    if (mainZ == 100) {
                        setKeyHeld(SDLK_1, true);
                        setKeyHeld(SDLK_RETURN, true);
                    }
                    break;
                case 5: // R3 (CLICK ANALÓGICO DERECHO)
                    if (mainZ != 100) {
                        // VOLVER AL TÍTULO (Equivalente a tecla F1)
                        setKeyHeld(SDLK_F1, true);
                        Mix_HaltMusic();
                    }
                    break;
                case 6: // L3 (CLICK ANALÓGICO IZQUIERDO)
                    if (mainZ == 100) {
                        setKeyHeld(SDLK_0, true);
                        setKeyHeld(SDLK_RETURN, true); // Nivel 0 (Mystery Dungeon)
                    }
                    break;
                case 7: // SELECT
                    btn_select_held = true;
                    if (btn_start_held) {
                        // COMBO SELECT + START -> SALIR AL XMB
                        Mix_HaltMusic();
                        ex = true;
                        break;
                    }
                    fullscreen_stretch = !fullscreen_stretch; // Alternar 16:9 y 4:3
                    break;
                case 8: // CUADRADO
                    if (mainZ == 100) {
                        setKeyHeld(SDLK_1, true);
                        setKeyHeld(SDLK_RETURN, true); // Nivel 1
                    } else {
                        setKeyHeld(SDLK_SPACE, true); // Turbo Puro (60 FPS)
                    }
                    break;
                case 9: // EQUIS (CRUZ)
                    if (mainZ == 100) {
                        setKeyHeld(SDLK_4, true);
                        setKeyHeld(SDLK_RETURN, true); // Nivel 4
                    } else {
                        setKeyHeld(SDLK_z, true); // Salto Puro
                    }
                    break;
                case 10: // CIRCULO
                    if (mainZ == 100) {
                        setKeyHeld(SDLK_3, true);
                        setKeyHeld(SDLK_RETURN, true); // Nivel 3
                    } else {
                        setKeyHeld(SDLK_RETURN, true);
                    }
                    break;
                case 11: // TRIANGULO
                    if (mainZ == 100) {
                        setKeyHeld(SDLK_2, true);
                        setKeyHeld(SDLK_RETURN, true); // Nivel 2
                    } else {
                        setKeyHeld(SDLK_o, true); // Suicidio / Reiniciar sala
                    }
                    break;
                case 12: // R1
                    if (mainZ == 100) {
                        setKeyHeld(SDLK_7, true);
                        setKeyHeld(SDLK_RETURN, true); // Nivel 7 Secreto
                    }
                    break;
                case 13: // L1
                    if (mainZ == 100) {
                        setKeyHeld(SDLK_5, true);
                        setKeyHeld(SDLK_RETURN, true); // Nivel 5 Secreto
                    }
                    break;
                case 14: // R2
                    if (mainZ == 100) {
                        setKeyHeld(SDLK_8, true);
                        setKeyHeld(SDLK_RETURN, true); // Nivel 8 Secreto
                    }
                    break;
                case 15: // L2
                    if (mainZ == 100) {
                        setKeyHeld(SDLK_6, true);
                        setKeyHeld(SDLK_RETURN, true); // Nivel 6 Secreto
                    }
                    break;
            }
            break;
        case SDL_JOYBUTTONUP:
            switch(event.jbutton.button) {
                case 0: setKeyHeld(SDLK_LEFT, false); break;
                case 1: setKeyHeld(SDLK_DOWN, false); break;
                case 2: setKeyHeld(SDLK_RIGHT, false); break;
                case 3: setKeyHeld(SDLK_UP, false); break;
                case 4: 
                    btn_start_held = false;
                    setKeyHeld(SDLK_RETURN, false);
                    setKeyHeld(SDLK_1, false);
                    break;
                case 5: // R3
                    setKeyHeld(SDLK_F1, false);
                    break;
                case 6: // L3
                    setKeyHeld(SDLK_0, false);
                    setKeyHeld(SDLK_RETURN, false);
                    break;
                case 7: 
                    btn_select_held = false;
                    break;
                case 8: // CUADRADO
                    setKeyHeld(SDLK_1, false);
                    setKeyHeld(SDLK_RETURN, false);
                    setKeyHeld(SDLK_SPACE, false);
                    break;
                case 9: // EQUIS (CRUZ)
                    setKeyHeld(SDLK_4, false);
                    setKeyHeld(SDLK_RETURN, false);
                    setKeyHeld(SDLK_z, false);
                    break;
                case 10: // CIRCULO
                    setKeyHeld(SDLK_3, false);
                    setKeyHeld(SDLK_RETURN, false);
                    break;
                case 11: // TRIANGULO
                    setKeyHeld(SDLK_2, false);
                    setKeyHeld(SDLK_RETURN, false);
                    setKeyHeld(SDLK_o, false);
                    break;
                case 12: // R1
                    setKeyHeld(SDLK_7, false);
                    setKeyHeld(SDLK_RETURN, false);
                    break;
                case 13: // L1
                    setKeyHeld(SDLK_5, false);
                    setKeyHeld(SDLK_RETURN, false);
                    break;
                case 14: // R2
                    setKeyHeld(SDLK_8, false);
                    setKeyHeld(SDLK_RETURN, false);
                    break;
                case 15: // L2
                    setKeyHeld(SDLK_6, false);
                    setKeyHeld(SDLK_RETURN, false);
                    break;
            }
            break;
        case SDL_QUIT:
            ex = true;
            break;
        }
    }
}

byte ProcessMessage()
{
    return ex;
}

byte CheckHitKey(int key)
{
    return getKeyHeld(key);
}

byte WaitKey()
{
    SDL_Event event;
    while (true) {
        while (SDL_PollEvent(&event)) {
            // Permitir que el botón PS funcione mientras se lee el texto
            sysUtilCheckCallback();

            // Salida por teclado (emulador / PC)
            if (event.type == SDL_KEYDOWN) {
                return (byte)event.key.keysym.sym;
            }

            // Salida por mando DualShock 3 (PS3 física / RPCS3)
            if (event.type == SDL_JOYBUTTONDOWN) {
                // Si presionan el combo SELECT + START, salir limpiamente al XMB
                if (event.jbutton.button == 4 && btn_select_held) {
                    Mix_HaltMusic();
                    exit(0);
                }
                if (event.jbutton.button == 5) {
                    btn_select_held = true;
                }
                // Cualquier botón de acción (Cruz, Círculo, Cuadrado, etc.) descarta el mensaje
                return 1;
            }
            if (event.type == SDL_JOYBUTTONUP) {
                if (event.jbutton.button == 5) {
                    btn_select_held = false;
                }
            }
        }
        sysUtilCheckCallback();
        usleep(10000); // 10 ms para no saturar la CPU
    }
}

/*Uint32 GetColor(byte r, byte g, byte b)
{
    return r << 8 * 3 | g << 8 * 2 | b << 8 | 0xFF;
}*/

void DrawGraphZ(int a, int b, SDL_Surface * mx)
{
    if(mx)
    {
        SDL_Rect offset;
        offset.x = a;
        offset.y = b;
        SDL_BlitSurface(mx, NULL, screen, &offset);
    }
}

void DrawTurnGraphZ(int a, int b, SDL_Surface * mx)
{
    if (!mx || !screen) return;

    SDL_Surface *flipped = SDL_CreateRGBSurface(SDL_SWSURFACE, mx->w, mx->h,
                                               mx->format->BitsPerPixel,
                                               mx->format->Rmask, mx->format->Gmask,
                                               mx->format->Bmask, mx->format->Amask);
    if (!flipped) return;

    // Copia invertida horizontalmente directa de fila por fila
    int bpp = mx->format->BytesPerPixel;
    for (int y = 0; y < mx->h; y++) {
        Uint8 *src_row = (Uint8*)mx->pixels + y * mx->pitch;
        Uint8 *dst_row = (Uint8*)flipped->pixels + y * flipped->pitch;
        for (int x = 0; x < mx->w; x++) {
            memcpy(dst_row + (mx->w - 1 - x) * bpp, src_row + x * bpp, bpp);
        }
    }

    SDL_SetColorKey(flipped, SDL_SRCCOLORKEY,
                    SDL_MapRGB(flipped->format, 9 * 16 + 9, 255, 255));

    SDL_Rect offset;
    offset.x = a;
    offset.y = b;
    SDL_BlitSurface(flipped, NULL, screen, &offset);
    SDL_FreeSurface(flipped);
}

void DrawVertTurnGraph(int a, int b, SDL_Surface * mx)
{
    if (!mx || !screen) return;

    SDL_Surface *flipped = SDL_CreateRGBSurface(SDL_SWSURFACE, mx->w, mx->h,
                                               mx->format->BitsPerPixel,
                                               mx->format->Rmask, mx->format->Gmask,
                                               mx->format->Bmask, mx->format->Amask);
    if (!flipped) return;

    
    int bpp = mx->format->BytesPerPixel;
    for (int y = 0; y < mx->h; y++) {
        Uint8 *src_row = (Uint8*)mx->pixels + y * mx->pitch;
        Uint8 *dst_row = (Uint8*)flipped->pixels + (mx->h - 1 - y) * flipped->pitch;
        memcpy(dst_row, src_row, mx->w * bpp);
    }

    SDL_SetColorKey(flipped, SDL_SRCCOLORKEY,
                    SDL_MapRGB(flipped->format, 9 * 16 + 9, 255, 255));

    SDL_Rect offset;
    offset.x = a - mx->w / 2;
    offset.y = b - mx->h / 2;
    SDL_BlitSurface(flipped, NULL, screen, &offset);
    SDL_FreeSurface(flipped);
}

SDL_Surface *DerivationGraph(int srcx, int srcy, int width, int height,
                             SDL_Surface * src)
{
    if (!src || !screen) return NULL;

    // Crear la superficie usando directamente el formato nativo de la pantalla
    SDL_Surface *img =
        SDL_CreateRGBSurface(SDL_SWSURFACE, width, height,
                             screen->format->BitsPerPixel,
                             screen->format->Rmask, screen->format->Gmask,
                             screen->format->Bmask, screen->format->Amask);

    if (!img) {
        // Fallback estándar Big Endian para Cell PPU
        #if SDL_BYTEORDER == SDL_BIG_ENDIAN
        img = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 32,
                                   0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000);
        #else
        img = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 32,
                                   0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000);
        #endif
    }

    if (!img) {
        printf("[ERROR] DerivationGraph fallo total: %s\n", SDL_GetError());
        return NULL;
    }

    SDL_Rect offset;
    offset.x = srcx;
    offset.y = srcy;
    offset.w = width;
    offset.h = height;

    SDL_BlitSurface(src, &offset, img, NULL);
    SDL_SetColorKey(img, SDL_SRCCOLORKEY,
                    SDL_MapRGB(img->format, 9 * 16 + 9, 255, 255));
    return img;
}

//Noticably different than the original
SDL_Surface *LoadGraph(const char *filename)
{
    printf("[DEBUG] Cargando imagen: %s\n", filename);
    std::string full = fixPath(filename);
    SDL_Surface *image = IMG_Load(full.c_str());

    if (image) {
        printf("[DEBUG] Imagen cargada OK (%p): %s\n", image, filename);
        return image;
    }
    fprintf(stderr, "Error: Unable to load %s: %s\n", filename, IMG_GetError());
    printf("[ERROR] No se pudo cargar %s: %s\n", full.c_str(), IMG_GetError());
    exit(1);
}

void PlaySoundMem(Mix_Chunk* s, int l)
{
    if(sound) Mix_PlayChannel(-1, s, l);
}

Mix_Chunk* LoadSoundMem(const char* f)
{
    if(!sound) return NULL;

    Mix_Chunk* s = Mix_LoadWAV(fixPath(f).c_str());
    if(s) return s;
    fprintf(stderr, "Error: Unable to load sound %s: %s\n", f, Mix_GetError());
    return NULL;
}

Mix_Music* LoadMusicMem(const char* f)
{
    if(!sound) return NULL;

    std::string path = fixPath(f);
    FILE *fp = fopen(path.c_str(), "rb");
    if(!fp) {
        fprintf(stderr, "Error: Unable to open music %s\n", path.c_str());
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if(sz <= 0) {
        fclose(fp);
        return NULL;
    }

    void *buf = malloc(sz);
    if(!buf) {
        fclose(fp);
        return NULL;
    }

    if(fread(buf, 1, sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    SDL_RWops *rw = SDL_RWFromMem(buf, sz);
    if(!rw) {
        free(buf);
        return NULL;
    }

    Mix_Music *m = Mix_LoadMUS_RW(rw);
    if(m) return m;

    fprintf(stderr, "Error: Unable to load music from mem %s: %s\n", f, Mix_GetError());
    free(buf);
    return NULL;
}

void PS3_ScreenFlip()
{
    if (!screen || !ps3_renderer || !ps3_texture) return;

    // 1. Subir únicamente los 806 KB del lienzo nativo a la VRAM (toma < 0.4 ms)
    SDL_UpdateTexture(ps3_texture, NULL, screen->pixels, screen->pitch);

    // 2. Limpiar todo el render target a negro puro con la GPU RSX
    // (Elimina instantáneamente cualquier residuo del Menú HOME / XMB de Sony)
    SDL_SetRenderDrawColor(ps3_renderer, 0, 0, 0, 255);
    SDL_RenderClear(ps3_renderer);

    // 3. Reescalado por silicio en GPU RSX (0.0 ms CPU)
    if (!fullscreen_stretch) {
        // Modo 4:3 Centrado clásico (Pillarbox con franjas negras laterales limpias)
        // En 1280x720, 4:3 corresponde a 960x720 centrado (160 px de margen a cada lado)
        SDL_Rect dst_rect = { 160, 0, 960, 720 };
        SDL_RenderCopy(ps3_renderer, ps3_texture, NULL, &dst_rect);
    } else {
        // Modo 16:9 Pantalla Completa estirada a todo el televisor
        SDL_RenderCopy(ps3_renderer, ps3_texture, NULL, NULL);
    }

    // 4. Conmutar el buffer de pantalla con VSync por hardware
    SDL_RenderPresent(ps3_renderer);
}

void DxLib_CleanupVideo()
{
    if (ps3_texture) {
        SDL_DestroyTexture(ps3_texture);
        ps3_texture = NULL;
    }
    if (ps3_renderer) {
        SDL_DestroyRenderer(ps3_renderer);
        ps3_renderer = NULL;
    }
    if (ps3_window) {
        SDL_DestroyWindow(ps3_window);
        ps3_window = NULL;
    }
}
