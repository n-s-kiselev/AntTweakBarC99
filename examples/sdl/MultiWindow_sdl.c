//  ---------------------------------------------------------------------------
//
//  @file       MultiWindow.c
//  @brief      Demonstrates running two independent AntTweakBar-managed
//              windows in one process, each with its own tweak bar, using
//              SDL3 (real separate windows).
//
//              SDL3 port of examples/glfw/MultiWindow_glfw.c - see
//              docs/plans/sdl3-backend.md for the backend adapter notes.
//              The multi-window mechanism this example exists to show is
//              unchanged: AntTweakBar has no built-in notion of "windows"
//              beyond a caller-assigned integer ID per manager - TwInit()
//              creates one manager for whatever GL context is current at
//              the time (window ID 0, the "master" manager);
//              TwSetCurrentWindow() with a not-yet-seen ID lazily creates
//              one more manager, tied to whatever GL context is current at
//              that moment. Every subsequent Tw* call for a given window
//              must be preceded by TwSetCurrentWindow(idForThatWindow) so
//              AntTweakBar knows which manager (and which window's tweak
//              bars) the call applies to.
//
//              Two SDL3-specific differences from the GLFW3 original:
//              - GLFW ties one implicit GL context to each GLFWwindow and
//                shares object namespaces via glfwCreateWindow()'s 4th
//                parameter; SDL3 separates window and context entirely, so
//                each DemoWindow below stores its own SDL_GLContext, and
//                sharing is requested via the SDL_GL_SHARE_WITH_CURRENT_CONTEXT
//                attribute (set to 1 right before creating window B's
//                context, with window A's context still current - SDL3
//                shares with whatever context is current at the moment
//                SDL_GL_CreateContext() is called).
//              - GLFW's per-window user pointer
//                (glfwSetWindowUserPointer/glfwGetWindowUserPointer) has no
//                direct SDL3 counterpart; SDL3 windows instead carry a
//                property set (SDL_GetWindowProperties() + SDL_SetPointerProperty()/
//                SDL_GetPointerProperty()), used the same way here.
//              - GLFW dispatches input through one callback per window
//                (window is an explicit parameter), so the GLFW original
//                tracked g_ActiveWindow itself just to know which GLFWwindow
//                its single process-wide GLFWCursorCB should call
//                glfwSetCursor() on. SDL3's SDL_SetCursor() takes no window
//                argument at all (cursors are process-global - see
//                examples/sdl/Triangle_sdl.c's own comment on this), so that
//                tracking has no SDL3 purpose and is dropped entirely, not
//                ported. SDL3 delivers one shared event queue for every
//                window instead of per-window callbacks; each event carries
//                a windowID (event.<member>.windowID) resolved back to a
//                DemoWindow* via the property lookup above.
//
//              The rendered scene in each window is deliberately minimal
//              (a simple spinning cube) - the point of this example is the
//              two-window architecture, not the geometry.
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              SDL:         https://www.libsdl.org
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <AntTweakBar.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#define NUM_WINDOWS 2

// SDL_GetWindowProperties()'s property name used to look a DemoWindow* back
// up from its SDL_Window* (see DemoWindowFor() below).
#define DEMO_WINDOW_PROPERTY "atb.demo_window"

typedef struct
{
    SDL_Window   *window;
    SDL_GLContext ctx;
    int           twWindowID;   // AntTweakBar's per-window manager ID (0 and 1)
    TwBar        *bar;
    double        speed;        // rotation speed (turns/second)
    double        turn;         // current rotation, in turns
    int           wire;         // wireframe toggle
    float         bgColor[3];
    char          fullWidthText[300]; // full_width=true multiline demo text - see SetupWindow()
    bool          shouldClose;
    // SDL always reports mouse position in window points, but
    // TwWindowSize() is fed pixel size (see handleWindowPixelSizeChanged
    // below), so mouse events must be scaled by this window/pixel ratio
    // before reaching AntTweakBar - per-window, unlike fontscaling, since
    // each window has its own point/pixel dimensions.
    double        mouseScaleX, mouseScaleY;
} DemoWindow;

static DemoWindow g_Windows[NUM_WINDOWS];

// Window content scale (see fontscaling comment in SetupWindow() below),
// queried once for window 0 and reused for window 1's bar size too: fonts
// are a process-wide resource (one shared g_FontScaling/set of default
// fonts for every CTwMgr), so there is only ever one fontscaling value to
// match, regardless of how many windows/managers exist.
static float g_ContentScale = 1.0f;

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

// Process-global, unlike the GLFW3 original's GLFWCursorCB - see the file
// header comment on why no window/g_ActiveWindow tracking is needed here.
static void TW_CALL SDLCursorCB(ETwCursor _Cursor, const unsigned char *_RGBA32x32, int _HotX, int _HotY, void *_ClientData)
{
    (void)_ClientData;
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
        SDL_Surface *surface = SDL_CreateSurfaceFrom(32, 32, SDL_PIXELFORMAT_RGBA32,
                                                      (void *)_RGBA32x32, 32 * 4);
        if (surface != NULL) {
            SDL_Cursor *cur = SDL_CreateColorCursor(surface, _HotX, _HotY);
            SDL_DestroySurface(surface);
            if (cur != NULL) {
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

static DemoWindow *DemoWindowFor(SDL_Window *window)
{
    if (window == NULL) return NULL;
    return (DemoWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), DEMO_WINDOW_PROPERTY, NULL);
}

static DemoWindow *DemoWindowForID(SDL_WindowID windowID)
{
    return DemoWindowFor(SDL_GetWindowFromID(windowID));
}

// This example program draws a possibly transparent cube (identical to
// SimpleGL21.c's DrawModel() - kept self-contained here rather than
// shared across example files).
static void DrawCube(int wireframe)
{
    int pass, numPass;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);

    if (wireframe) {
        glDisable(GL_CULL_FACE);
        glDisable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        numPass = 1;
    } else {
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CCW);
        glEnable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        numPass = 2;
    }

    for (pass = 0; pass < numPass; ++pass) {
        glCullFace((pass == 0) ? GL_FRONT : GL_BACK);
        glBegin(GL_QUADS);
            glNormal3f(0, 0, 1);
            glVertex3f(-0.5f, -0.5f,  0.5f); glVertex3f( 0.5f, -0.5f,  0.5f);
            glVertex3f( 0.5f,  0.5f,  0.5f); glVertex3f(-0.5f,  0.5f,  0.5f);
            glNormal3f(0, 0, -1);
            glVertex3f( 0.5f, -0.5f, -0.5f); glVertex3f(-0.5f, -0.5f, -0.5f);
            glVertex3f(-0.5f,  0.5f, -0.5f); glVertex3f( 0.5f,  0.5f, -0.5f);
            glNormal3f(-1, 0, 0);
            glVertex3f(-0.5f, -0.5f, -0.5f); glVertex3f(-0.5f, -0.5f,  0.5f);
            glVertex3f(-0.5f,  0.5f,  0.5f); glVertex3f(-0.5f,  0.5f, -0.5f);
            glNormal3f(1, 0, 0);
            glVertex3f( 0.5f, -0.5f,  0.5f); glVertex3f( 0.5f, -0.5f, -0.5f);
            glVertex3f( 0.5f,  0.5f, -0.5f); glVertex3f( 0.5f,  0.5f,  0.5f);
            glNormal3f(0, -1, 0);
            glVertex3f(-0.5f, -0.5f, -0.5f); glVertex3f( 0.5f, -0.5f, -0.5f);
            glVertex3f( 0.5f, -0.5f,  0.5f); glVertex3f(-0.5f, -0.5f,  0.5f);
            glNormal3f(0, 1, 0);
            glVertex3f(-0.5f,  0.5f,  0.5f); glVertex3f( 0.5f,  0.5f,  0.5f);
            glVertex3f( 0.5f,  0.5f, -0.5f); glVertex3f(-0.5f,  0.5f, -0.5f);
        glEnd();
    }
}

static void updateMouseScale(DemoWindow *dw, int pixelWidth, int pixelHeight)
{
    int winWidth = pixelWidth, winHeight = pixelHeight;
    SDL_GetWindowSize(dw->window, &winWidth, &winHeight);
    dw->mouseScaleX = (winWidth > 0) ? (double)pixelWidth / winWidth : 1.0;
    dw->mouseScaleY = (winHeight > 0) ? (double)pixelHeight / winHeight : 1.0;
}

// Called once per window at startup and again on every
// SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED for that window.
static void handleWindowPixelSizeChanged(DemoWindow *dw, int width, int height)
{
    if (dw == NULL) return;
    if (height == 0) height = 1;

    SDL_GL_MakeCurrent(dw->window, dw->ctx); // glViewport()/projection below are per-context state
    float aspect = (float)width / (float)height;
    float znear = 1.0f, zfar = 100.0f, fov = 45.0f;
    float top = tanf(fov * 0.01745329251f) * znear;
    float bottom = -top, right = top * aspect, left = -right;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(left, right, bottom, top, znear, zfar);

    TwSetCurrentWindow(dw->twWindowID);
    TwWindowSize(width, height);
    updateMouseScale(dw, width, height);
}

// full_width=true demo: a multiline text widget spanning the whole row, and a button
// below it that cycles the text widget's "lines=" value 2->3->4->5->6->2->..., changing
// the existing widget's attribute at runtime via TwDefine rather than recreating it.
// clientData is the specific window's own bar (see SetupWindow()), so each of the two
// windows in this example cycles its own widget independently.
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

static void handleKeyDown(DemoWindow *dw, const SDL_KeyboardEvent *_Event)
{
    TwSetCurrentWindow(dw->twWindowID);

    int twMod = 0;
    if (_Event->mod & SDL_KMOD_SHIFT) twMod |= TW_KMOD_SHIFT;
    bool ctrl = (_Event->mod & SDL_KMOD_CTRL) != 0;
    if (ctrl) twMod |= TW_KMOD_CTRL;
    if (_Event->mod & SDL_KMOD_ALT) twMod |= TW_KMOD_ALT;

    int twKey = 0;
    switch (_Event->key) {
    case SDLK_BACKSPACE: twKey = TW_KEY_BACKSPACE; break;
    case SDLK_TAB: twKey = TW_KEY_TAB; break;
    case SDLK_RETURN: twKey = TW_KEY_RETURN; break;
    case SDLK_ESCAPE: twKey = TW_KEY_ESCAPE; break;
    case SDLK_SPACE: twKey = TW_KEY_SPACE; break;
    case SDLK_DELETE: twKey = TW_KEY_DELETE; break;
    case SDLK_UP: twKey = TW_KEY_UP; break;
    case SDLK_DOWN: twKey = TW_KEY_DOWN; break;
    case SDLK_RIGHT: twKey = TW_KEY_RIGHT; break;
    case SDLK_LEFT: twKey = TW_KEY_LEFT; break;
    case SDLK_HOME: twKey = TW_KEY_HOME; break;
    case SDLK_END: twKey = TW_KEY_END; break;
    case SDLK_PAGEUP: twKey = TW_KEY_PAGE_UP; break;
    case SDLK_PAGEDOWN: twKey = TW_KEY_PAGE_DOWN; break;
    }
    if (twKey == 0 && ctrl && _Event->key < 128) twKey = (int)_Event->key;
    if (twKey != 0) TwKeyPressed(twKey, twMod);
}

static void handleTextInput(DemoWindow *dw, const char *_Utf8Text)
{
    TwSetCurrentWindow(dw->twWindowID);
    const unsigned char *s = (const unsigned char *)_Utf8Text;
    while (*s != '\0') {
        unsigned int cp = 0;
        int extra = 0;
        if ((*s & 0x80) == 0x00) { cp = *s; extra = 0; }
        else if ((*s & 0xE0) == 0xC0) { cp = *s & 0x1F; extra = 1; }
        else if ((*s & 0xF0) == 0xE0) { cp = *s & 0x0F; extra = 2; }
        else if ((*s & 0xF8) == 0xF0) { cp = *s & 0x07; extra = 3; }
        else { ++s; continue; }
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

static void handleMouseButton(DemoWindow *dw, const SDL_MouseButtonEvent *_Event)
{
    TwSetCurrentWindow(dw->twWindowID);
    if (_Event->button == SDL_BUTTON_LEFT || _Event->button == SDL_BUTTON_MIDDLE || _Event->button == SDL_BUTTON_RIGHT) {
        TwMouseButton(_Event->down ? TW_MOUSE_PRESSED : TW_MOUSE_RELEASED, (TwMouseButtonID)_Event->button);
    }
}

// Creates one SDL3 window+context, assigns it an AntTweakBar window ID
// (creating that manager immediately - see the file header comment above),
// and adds its tweak bar. windowIndex 0 must be called first (its manager
// is the master one, ID 0, implicitly tied to whatever context is current
// when TwInit() runs); windowIndex 1 (and beyond, if this were extended)
// calls TwSetCurrentWindow() with a fresh ID to lazily create its manager.
static bool SetupWindow(int windowIndex, const char *title, float r, float g, float b)
{
    DemoWindow *dw = &g_Windows[windowIndex];

    dw->window = SDL_CreateWindow(title, 500, 500,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (dw->window == NULL) {
        fprintf(stderr, "Cannot open SDL window '%s': %s\n", title, SDL_GetError());
        return false;
    }
    SDL_SetPointerProperty(SDL_GetWindowProperties(dw->window), DEMO_WINDOW_PROPERTY, dw);
    dw->twWindowID = windowIndex; // arbitrary but must be unique and stable
    dw->speed = 0.2 + 0.15 * windowIndex;
    dw->turn = 0.0;
    dw->wire = 0;
    dw->shouldClose = false;
    dw->bgColor[0] = r; dw->bgColor[1] = g; dw->bgColor[2] = b;

    if (windowIndex == 1) {
        // Window B's context shares window A's object namespace (required
        // so a single TwTerminate() call, made with only one context
        // current, can validly delete every window's GL objects - see the
        // file header comment). This attribute shares with whatever
        // context is current *at context-creation time* below, so it must
        // be set here, right before creating window B's context, with
        // window A's context still current from SetupWindow(0)'s own
        // SDL_GL_CreateContext() call.
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    }
    dw->ctx = SDL_GL_CreateContext(dw->window);
    if (dw->ctx == NULL) {
        fprintf(stderr, "Cannot create GL context for '%s': %s\n", title, SDL_GetError());
        return false;
    }

    if (windowIndex == 0) {
        // First window: load GLAD once (function pointers are valid across
        // every context sharing this one's object namespace) and
        // initialize AntTweakBar's master manager.
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            fprintf(stderr, "Failed to initialize GLAD\n");
            return false;
        }
        // AntTweakBar draws every widget at a fixed pixel size with no DPI
        // awareness - scale "fontscaling" (set via TwDefine, before
        // TwInit) by the window's display scale. Done once here (not per
        // window): fonts are a process-wide resource shared by every
        // window's manager.
        g_ContentScale = SDL_GetWindowDisplayScale(dw->window);
        if (g_ContentScale <= 0.0f) g_ContentScale = 1.0f;
        {
            char fontScalingDef[64];
            snprintf(fontScalingDef, sizeof(fontScalingDef), "GLOBAL fontscaling=%g", (double)g_ContentScale);
            TwDefine(fontScalingDef);
        }
        if (!TwInit(TW_OPENGL, NULL)) {
            fprintf(stderr, "TwInit failed: %s\n", TwGetLastError());
            return false;
        }
        TwSetCursorCallback(SDLCursorCB, NULL);
        TwSetClipboardCallback(ClipboardGetSDL, ClipboardSetSDL, NULL);
    } else {
        // Later windows: TwSetCurrentWindow() with a fresh ID lazily
        // creates this window's own CTwMgr/renderer, tied to the context
        // made current just above (SDL_GL_CreateContext() makes the new
        // context current for its window immediately).
        if (!TwSetCurrentWindow(dw->twWindowID)) {
            fprintf(stderr, "TwSetCurrentWindow(%d) failed to create a manager\n", dw->twWindowID);
            return false;
        }
    }

    SDL_StartTextInput(dw->window);

    {
        int width, height;
        SDL_GetWindowSizeInPixels(dw->window, &width, &height);
        handleWindowPixelSizeChanged(dw, width, height);
    }

    dw->bar = TwNewBar("TweakBar");
    TwDefine(" GLOBAL help='Two independent AntTweakBar-managed SDL3 windows in one process.' ");
    {
        // Scaled by content scale so the panel keeps up with the
        // now-larger scaled contents.
        int barSize[2] = { (int)(200 * g_ContentScale + 0.5f), (int)(150 * g_ContentScale + 0.5f) };
        TwSetParam(dw->bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }
    TwAddVarRW(dw->bar, "speed", TW_TYPE_DOUBLE, &dw->speed,
               " label='Rot speed' min=0 max=2 step=0.01 help='Rotation speed (turns/second)' ");
    TwAddVarRW(dw->bar, "wire", TW_TYPE_BOOL32, &dw->wire,
               " label='Wireframe' help='Toggle wireframe display mode.' ");
    TwAddVarRW(dw->bar, "bgColor", TW_TYPE_COLOR3F, &dw->bgColor,
               " label='Background color' ");

    strcpy(dw->fullWidthText,
        "This is a full-width widget. You can enter long text that spans multiple lines. "
        "The text is automatically wrapped to fit the available width. Clicking the "
        "full-width button above increases the number of visible lines up to 6, then "
        "resets it back to 2.");
    TwAddSeparator(dw->bar, NULL, "");
    TwAddButton(dw->bar, "FullWidthDemoMoreLines", FullWidthLinesCB, dw->bar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(dw->bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(dw->fullWidthText)), dw->fullWidthText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");

    return true;
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    if (!SetupWindow(0, "MultiWindow - Window A", 0.15f, 0.15f, 0.35f))
        return 1;
    if (!SetupWindow(1, "MultiWindow - Window B", 0.35f, 0.15f, 0.15f))
        return 1;

    double lastTime = (double)SDL_GetTicksNS() / 1e9;

    while (!g_Windows[0].shouldClose && !g_Windows[1].shouldClose)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                g_Windows[0].shouldClose = true;
                g_Windows[1].shouldClose = true;
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                DemoWindow *dw = DemoWindowForID(event.window.windowID);
                if (dw != NULL) dw->shouldClose = true;
                break;
            }
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                DemoWindow *dw = DemoWindowForID(event.window.windowID);
                handleWindowPixelSizeChanged(dw, event.window.data1, event.window.data2);
                break;
            }
            case SDL_EVENT_KEY_DOWN: {
                DemoWindow *dw = DemoWindowForID(event.key.windowID);
                if (dw != NULL) handleKeyDown(dw, &event.key);
                break;
            }
            case SDL_EVENT_TEXT_INPUT: {
                DemoWindow *dw = DemoWindowForID(event.text.windowID);
                if (dw != NULL) handleTextInput(dw, event.text.text);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                DemoWindow *dw = DemoWindowForID(event.button.windowID);
                if (dw != NULL) handleMouseButton(dw, &event.button);
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                DemoWindow *dw = DemoWindowForID(event.motion.windowID);
                if (dw != NULL) {
                    TwSetCurrentWindow(dw->twWindowID);
                    TwMouseMotion((int)(event.motion.x * dw->mouseScaleX), (int)(event.motion.y * dw->mouseScaleY));
                }
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                static double pos[NUM_WINDOWS] = { 0 };
                DemoWindow *dw = DemoWindowForID(event.wheel.windowID);
                if (dw != NULL) {
                    TwSetCurrentWindow(dw->twWindowID);
                    pos[dw->twWindowID] += event.wheel.y;
                    TwMouseWheel((int)pos[dw->twWindowID]);
                }
                break;
            }
            default:
                break;
            }
        }

        double now = (double)SDL_GetTicksNS() / 1e9;
        double dt = now - lastTime;
        if (dt < 0) dt = 0;
        lastTime = now;

        for (int i = 0; i < NUM_WINDOWS; ++i) {
            DemoWindow *dw = &g_Windows[i];
            dw->turn += dw->speed * dt;

            SDL_GL_MakeCurrent(dw->window, dw->ctx);
            TwSetCurrentWindow(dw->twWindowID);

            glClearColor(dw->bgColor[0], dw->bgColor[1], dw->bgColor[2], 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            GLfloat lightPos[] = { 1.0f, 1.0f, 5.0f, 1.0f };
            glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
            glTranslated(0, 0, -3.0);
            glRotated(360.0 * dw->turn, 0.4, 1, 0.2);
            glColor3f(0.9f, 0.7f, 0.1f);
            DrawCube(dw->wire);

            TwDraw();
            SDL_GL_SwapWindow(dw->window);
        }
    }

    // A shared context must be current for TwTerminate()'s internal loop
    // over every window's manager to validly delete their (shared) GL
    // objects - see the file header comment.
    SDL_GL_MakeCurrent(g_Windows[0].window, g_Windows[0].ctx);
    TwTerminate();
    DestroySDLCursorCache();

    SDL_GL_DestroyContext(g_Windows[1].ctx);
    SDL_GL_DestroyContext(g_Windows[0].ctx);
    SDL_DestroyWindow(g_Windows[1].window);
    SDL_DestroyWindow(g_Windows[0].window);
    SDL_Quit();

    return 0;
}
