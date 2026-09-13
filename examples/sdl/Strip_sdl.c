//  ---------------------------------------------------------------------------
//
//  @file       Strip.c
//  @brief      A simple example that uses AntTweakBar with SDL3 and OpenGL.
//              Draws an animated color-gradient triangle strip.
//              SDL3 port of examples/glfw/Strip.c - see
//              docs/plans/sdl3-backend.md for the backend adapter notes.
//              Also demonstrates TwSetParam() as an alternative to TwDefine()
//              for setting a bar attribute (here, the tweak bar's size).
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              SDL:         https://www.libsdl.org
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <AntTweakBar.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

// SDL3 cursors are process-global (SDL_SetCursor() takes no window
// argument, unlike glfwSetCursor()) - there is no per-window cursor-
// ownership fight to route around the way examples/glfw/Strip.c's own
// comment describes, so this is a plain cache, not a GLFW3-style shim.
static SDL_Cursor *g_StandardCursors[TW_CURSOR_CUSTOM] = { NULL };
static SDL_Cursor *g_LastCustomCursor = NULL;
static bool g_CursorHidden = false;

static SDL_SystemCursor SDLStandardCursorShape(ETwCursor _Cursor)
{
    switch (_Cursor) {
    case TW_CURSOR_ARROW:        return SDL_SYSTEM_CURSOR_DEFAULT;
    case TW_CURSOR_MOVE:         return SDL_SYSTEM_CURSOR_MOVE;
    case TW_CURSOR_RESIZE_WE:    return SDL_SYSTEM_CURSOR_EW_RESIZE;
    case TW_CURSOR_RESIZE_NS:    return SDL_SYSTEM_CURSOR_NS_RESIZE;
    case TW_CURSOR_RESIZE_NESW:  return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    case TW_CURSOR_RESIZE_NWSE:  return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    case TW_CURSOR_HAND:         return SDL_SYSTEM_CURSOR_POINTER;
    case TW_CURSOR_CROSS:        return SDL_SYSTEM_CURSOR_CROSSHAIR;
    case TW_CURSOR_IBEAM:        return SDL_SYSTEM_CURSOR_TEXT;
    case TW_CURSOR_NO:           return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    default:                     return SDL_SYSTEM_CURSOR_DEFAULT; // TW_CURSOR_HELP/UPARROW: no dedicated SDL shape
    }
}

// SDL_GetClipboardText() allocates and must be freed (SDL_free) - unlike
// GLFW's glfwGetClipboardString(), which owns its own buffer. Freeing the
// *previous* call's result (not the one just returned) keeps the pointer
// valid for however long AntTweakBar needs it after this call returns.
static char *g_ClipboardText = NULL;

static const char * TW_CALL ClipboardGetSDL(void *_ClientData)
{
    (void)_ClientData;
    if (g_ClipboardText != NULL) SDL_free(g_ClipboardText);
    g_ClipboardText = SDL_GetClipboardText();
    return g_ClipboardText;
}

static void TW_CALL ClipboardSetSDL(const char *_Text, void *_ClientData)
{
    (void)_ClientData;
    SDL_SetClipboardText(_Text);
}

static void TW_CALL SDLCursorCB(ETwCursor _Cursor, const unsigned char *_RGBA32x32, int _HotX, int _HotY, void *_ClientData)
{
    (void)_ClientData;
    // TW_CURSOR_HIDDEN is an input mode, not a cursor shape: the roto slider
    // hides the pointer while it is dragged. g_CursorHidden remembers that so
    // the mode is restored once, on the next request for a visible cursor.
    if (_Cursor == TW_CURSOR_HIDDEN) {
        SDL_HideCursor();
        g_CursorHidden = true;
        return;
    }
    if (g_CursorHidden) {
        SDL_ShowCursor();
        g_CursorHidden = false;
    }
    if (_Cursor == TW_CURSOR_CUSTOM && _RGBA32x32 != NULL) {
        // SDL_PIXELFORMAT_RGBA32 is the byte-order-independent alias for
        // 8-bit-per-channel RGBA (see SDL_pixels.h) - matches _RGBA32x32's
        // own byte layout exactly, no channel reordering needed.
        SDL_Surface *surface = SDL_CreateSurfaceFrom(32, 32, SDL_PIXELFORMAT_RGBA32,
                                                      (void *)_RGBA32x32, 32 * 4);
        if (surface != NULL) {
            SDL_Cursor *cur = SDL_CreateColorCursor(surface, _HotX, _HotY);
            SDL_DestroySurface(surface); // SDL_CreateColorCursor copies the pixels
            if (cur != NULL) {
                // Set the new cursor before destroying the old one: some
                // platforms reset to the default arrow when the current
                // cursor is destroyed - same precaution as the GLFW3
                // example's own GLFWCursorCB.
                SDL_SetCursor(cur);
                if (g_LastCustomCursor != NULL) SDL_DestroyCursor(g_LastCustomCursor);
                g_LastCustomCursor = cur;
            }
        }
        return;
    }
    if (g_StandardCursors[_Cursor] == NULL)
        g_StandardCursors[_Cursor] = SDL_CreateSystemCursor(SDLStandardCursorShape(_Cursor));
    if (g_StandardCursors[_Cursor] != NULL)
        SDL_SetCursor(g_StandardCursors[_Cursor]);
}

static void DestroySDLCursorCache(void)
{
    for (int i = 0; i < TW_CURSOR_CUSTOM; ++i) {
        if (g_StandardCursors[i] != NULL) {
            SDL_DestroyCursor(g_StandardCursors[i]);
            g_StandardCursors[i] = NULL;
        }
    }
    if (g_LastCustomCursor != NULL) {
        SDL_DestroyCursor(g_LastCustomCursor);
        g_LastCustomCursor = NULL;
    }
    if (g_ClipboardText != NULL) {
        SDL_free(g_ClipboardText);
        g_ClipboardText = NULL;
    }
}

static int g_Width = 640, g_Height = 480;

// SDL always reports mouse position in window points, but TwWindowSize() is
// fed the pixel size (see handleWindowPixelSizeChanged), so mouse events
// must be scaled by this window/pixel ratio before reaching AntTweakBar -
// same reasoning as examples/glfw/Strip.c's own g_MouseScaleX/Y, see
// docs/plans/examples-hidpi-scaling.md.
static double g_MouseScaleX = 1.0, g_MouseScaleY = 1.0;

static void handleKeyDown(const SDL_KeyboardEvent *_Event, bool *_Running)
{
    if (_Event->key == SDLK_ESCAPE) {
        *_Running = false;
        return;
    }

    int twMod = 0;
    bool ctrl;
    if (_Event->mod & SDL_KMOD_SHIFT) twMod |= TW_KMOD_SHIFT;
    if ((ctrl = (_Event->mod & SDL_KMOD_CTRL) != 0)) twMod |= TW_KMOD_CTRL;
    if (_Event->mod & SDL_KMOD_ALT) twMod |= TW_KMOD_ALT;

    int twKey = 0;
    switch (_Event->key) {
    case SDLK_BACKSPACE: twKey = TW_KEY_BACKSPACE; break;
    case SDLK_TAB: twKey = TW_KEY_TAB; break;
    case SDLK_RETURN: twKey = TW_KEY_RETURN; break;
    case SDLK_PAUSE: twKey = TW_KEY_PAUSE; break;
    case SDLK_SPACE: twKey = TW_KEY_SPACE; break;
    case SDLK_DELETE: twKey = TW_KEY_DELETE; break;
    case SDLK_UP: twKey = TW_KEY_UP; break;
    case SDLK_DOWN: twKey = TW_KEY_DOWN; break;
    case SDLK_RIGHT: twKey = TW_KEY_RIGHT; break;
    case SDLK_LEFT: twKey = TW_KEY_LEFT; break;
    case SDLK_INSERT: twKey = TW_KEY_INSERT; break;
    case SDLK_HOME: twKey = TW_KEY_HOME; break;
    case SDLK_END: twKey = TW_KEY_END; break;
    case SDLK_PAGEUP: twKey = TW_KEY_PAGE_UP; break;
    case SDLK_PAGEDOWN: twKey = TW_KEY_PAGE_DOWN; break;
    case SDLK_F1: twKey = TW_KEY_F1; break;
    case SDLK_F2: twKey = TW_KEY_F2; break;
    case SDLK_F3: twKey = TW_KEY_F3; break;
    case SDLK_F4: twKey = TW_KEY_F4; break;
    case SDLK_F5: twKey = TW_KEY_F5; break;
    case SDLK_F6: twKey = TW_KEY_F6; break;
    case SDLK_F7: twKey = TW_KEY_F7; break;
    case SDLK_F8: twKey = TW_KEY_F8; break;
    case SDLK_F9: twKey = TW_KEY_F9; break;
    case SDLK_F10: twKey = TW_KEY_F10; break;
    case SDLK_F11: twKey = TW_KEY_F11; break;
    case SDLK_F12: twKey = TW_KEY_F12; break;
    case SDLK_F13: twKey = TW_KEY_F13; break;
    case SDLK_F14: twKey = TW_KEY_F14; break;
    case SDLK_F15: twKey = TW_KEY_F15; break;
    }
    if (twKey == 0 && ctrl && _Event->key < 128) {
        twKey = (int)_Event->key;
    }
    if (twKey != 0) {
        TwKeyPressed(twKey, twMod);
    }
}

// SDL3 delivers typed text as a UTF-8 string per SDL_EVENT_TEXT_INPUT event
// (opt-in via SDL_StartTextInput()), unlike GLFW's one-codepoint-per-call
// char callback - decode it a codepoint at a time and feed each to
// TwKeyPressed(), same as examples/glfw/Strip.c's charCallback does per
// GLFW callback invocation. Only handles well-formed UTF-8 (1-3 byte
// sequences cover Latin/European scripts, the practical case for this
// library's text-entry widgets); malformed input is skipped rather than
// misinterpreted.
static void handleTextInput(const char *_Utf8Text)
{
    const unsigned char *s = (const unsigned char *)_Utf8Text;
    while (*s != '\0') {
        unsigned int cp = 0;
        int extra = 0;
        if ((*s & 0x80) == 0x00) { cp = *s; extra = 0; }
        else if ((*s & 0xE0) == 0xC0) { cp = *s & 0x1F; extra = 1; }
        else if ((*s & 0xF0) == 0xE0) { cp = *s & 0x0F; extra = 2; }
        else if ((*s & 0xF8) == 0xF0) { cp = *s & 0x07; extra = 3; }
        else { ++s; continue; } // invalid leading byte, skip it
        ++s;
        bool valid = true;
        for (int i = 0; i < extra; ++i) {
            if ((*s & 0xC0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (*s & 0x3F);
            ++s;
        }
        if (valid) TwKeyPressed((int)cp, 0);
    }
}

static void handleMouseButton(const SDL_MouseButtonEvent *_Event)
{
    if (_Event->button == SDL_BUTTON_LEFT || _Event->button == SDL_BUTTON_MIDDLE || _Event->button == SDL_BUTTON_RIGHT) {
        // AntTweakBar.h's TW_MOUSE_LEFT/MIDDLE/RIGHT are deliberately the
        // same values as SDL_BUTTON_LEFT/MIDDLE/RIGHT (1/2/3) - no mapping
        // table needed, unlike the key/cursor cases above.
        TwMouseButton(_Event->down ? TW_MOUSE_PRESSED : TW_MOUSE_RELEASED, (TwMouseButtonID)_Event->button);
    }
}

static void updateMouseScale(SDL_Window *_Window, int _PixelWidth, int _PixelHeight)
{
    int pointWidth = _PixelWidth, pointHeight = _PixelHeight;
    SDL_GetWindowSize(_Window, &pointWidth, &pointHeight);
    g_MouseScaleX = (pointWidth > 0) ? (double)_PixelWidth / pointWidth : 1.0;
    g_MouseScaleY = (pointHeight > 0) ? (double)_PixelHeight / pointHeight : 1.0;
}

// Called once at startup (with the framebuffer's actual initial pixel
// size) and again on every SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED - mirrors
// examples/glfw/Strip.c's windowSizeCallback (registered as GLFW's
// FRAMEBUFFER size callback there for the same pixel-vs-point reason).
static void handleWindowPixelSizeChanged(SDL_Window *_Window, int _Width, int _Height)
{
    if (_Height == 0) _Height = 1;
    g_Width = _Width;
    g_Height = _Height;
    glViewport(0, 0, _Width, _Height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if (_Width >= _Height) {
        double aspect = (double)_Width / _Height;
        glOrtho(-aspect, aspect, -1.0, 1.0, -1.0, 1.0);
    } else {
        double aspect = (double)_Height / _Width;
        glOrtho(-1.0, 1.0, -aspect, aspect, -1.0, 1.0);
    }
    glMatrixMode(GL_MODELVIEW);
    TwWindowSize(_Width, _Height);
    updateMouseScale(_Window, _Width, _Height);
}

// full_width=true demo: a multiline text widget spanning the whole row, and a button
// below it that cycles the text widget's "lines=" value 2->3->4->5->6->2->..., changing
// the existing widget's attribute at runtime via TwDefine rather than recreating it.
static char g_FullWidthDemoText[300] =
    "This is a full-width widget. You can enter long text that spans multiple lines. "
    "The text is automatically wrapped to fit the available width. Clicking the "
    "full-width button above increases the number of visible lines up to 6, then "
    "resets it back to 2.";

void TW_CALL FullWidthLinesCB(void *clientData)
{
    TwBar *bar = (TwBar *)clientData;
    int lines = 2;
    TwGetParam(bar, "FullWidthDemoText", "lines", TW_PARAM_INT32, 1, &lines);
    lines = (lines>=6) ? 2 : lines+1;
    char def[96];
    snprintf(def, sizeof(def), " %s/FullWidthDemoText lines=%d ", TwGetBarName(bar), lines);
    TwDefine(def);
}

int main(void)
{
    int numSec = 100;             // number of strip sections
    float color[] = { 1, 0, 0 };  // strip color
    unsigned char bgColor[] = { 128, 196, 196, 255 }; // background color (32bits RGBA: R,G,B,A)

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    // Fixed-function GL (glBegin/glEnd below) + AntTweakBar's TW_OPENGL
    // (compatibility, not Core Profile) renderer - request a plain 2.1
    // compatibility context, the same profile examples/glfw/SimpleGL21.c
    // targets, and the exact version validated by this backend's compile
    // spike (see docs/plans/sdl3-backend.md Step 1).
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    SDL_Window *window = SDL_CreateWindow("AntTweakBar + SDL3 (Triangle Strip)", g_Width, g_Height,
                                           SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == NULL) {
        fprintf(stderr, "Cannot open SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (ctx == NULL) {
        fprintf(stderr, "Cannot create GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        return 1;
    }

    // AntTweakBar draws every widget at a fixed pixel size with no DPI
    // awareness - scale "fontscaling" by the window's display scale before
    // TwInit, same reasoning (and the same no-op-on-a-standard-display
    // behavior) as examples/glfw/Strip.c's own contentScaleX handling.
    float contentScale = SDL_GetWindowDisplayScale(window);
    if (contentScale <= 0.0f) contentScale = 1.0f;
    {
        char fontScalingDef[64];
        snprintf(fontScalingDef, sizeof(fontScalingDef), "GLOBAL fontscaling=%g", (double)contentScale);
        TwDefine(fontScalingDef);
    }

    if (!TwInit(TW_OPENGL, NULL)) {
        fprintf(stderr, "AntTweakBar initialization failed: %s\n", TwGetLastError());
        return 1;
    }
    TwSetCursorCallback(SDLCursorCB, NULL); // SDL cursors are process-global, no window needed
    TwSetClipboardCallback(ClipboardGetSDL, ClipboardSetSDL, NULL);
    SDL_StartTextInput(window);

    {
        int pixelWidth, pixelHeight;
        SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
        handleWindowPixelSizeChanged(window, pixelWidth, pixelHeight);
    }

    TwBar *bar = TwNewBar("TweakBar");
    {
        // Demonstrates TwSetParam() as an alternative to TwDefine() for
        // setting a single bar attribute (here, "size"). Scaled by content
        // scale so the panel keeps up with the now-larger scaled contents.
        int barSize[2] = { (int)(200 * contentScale + 0.5f), (int)(320 * contentScale + 0.5f) };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with SDL3 and OpenGL.' ");
    TwDefine(" TweakBar color='128 224 160' text=dark ");

    TwAddVarRW(bar, "NumSec", TW_TYPE_INT32, &numSec,
               " label='Strip length' min=1 max=1000 keyIncr=s keyDecr=S help='Number of segments of the strip.' ");
    TwAddVarRW(bar, "Color", TW_TYPE_COLOR3F, &color, " label='Strip color' ");
    TwAddVarRW(bar, "BgColor", TW_TYPE_COLOR32, &bgColor, " label='Background color' ");
    TwAddVarRO(bar, "Width", TW_TYPE_INT32, &g_Width, " label='wnd width' help='Current graphics window width.' ");
    TwAddVarRO(bar, "Height", TW_TYPE_INT32, &g_Height, " label='wnd height' help='Current graphics window height.' ");

    TwAddSeparator(bar, NULL, "");
    TwAddButton(bar, "FullWidthDemoMoreLines", FullWidthLinesCB, bar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");

    bool running = true;
    static double wheelPos = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                handleKeyDown(&event.key, &running);
                break;
            case SDL_EVENT_TEXT_INPUT:
                handleTextInput(event.text.text);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                handleMouseButton(&event.button);
                break;
            case SDL_EVENT_MOUSE_MOTION:
                TwMouseMotion((int)(event.motion.x * g_MouseScaleX), (int)(event.motion.y * g_MouseScaleY));
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                wheelPos += event.wheel.y;
                TwMouseWheel((int)wheelPos);
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                handleWindowPixelSizeChanged(window, event.window.data1, event.window.data2);
                break;
            default:
                break;
            }
        }

        glClearColor(bgColor[2] / 255.0f, bgColor[1] / 255.0f, bgColor[0] / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float t = (float)((double)SDL_GetTicksNS() / 1e9);
        glBegin(GL_TRIANGLE_STRIP);
        for (int i = 0; i <= numSec; ++i) {
            float s = (float)i / 100.0f;
            float x0 = 0.05f + 0.7f * cosf(2.0f * s + 5.0f * t);
            float x1 = x0 + (0.25f + 0.1f * cosf(s + t));
            float y = 0.7f * (0.7f + 0.3f * sinf(s + t)) * sinf(1.5f * s + 3.0f * t);
            float sc = (float)i / numSec;

            glColor3f(color[0] * sc, color[1] * sc, color[2] * sc);
            glVertex2f(x0, y);
            glVertex2f(x1, y);
        }
        glEnd();

        TwDraw();
        SDL_GL_SwapWindow(window);
    }

    TwTerminate();
    DestroySDLCursorCache();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
