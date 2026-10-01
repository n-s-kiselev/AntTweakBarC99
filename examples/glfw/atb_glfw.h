//  ---------------------------------------------------------------------------
//
//  @file       atb_glfw.h
//  @brief      Header-only glue between GLFW3 and AntTweakBar, shared by the
//              examples in this folder.
//
//              Every example used to repeat the same ~180 lines: the
//              GLFW_KEY_* -> TW_KEY_* map, the modifier mask, character input,
//              mouse button/motion/wheel forwarding, the framebuffer-size ->
//              TwWindowSize plumbing with its HiDPI mouse scale, the clipboard
//              callbacks and the standard-cursor cache. All of that lives here
//              now, so a fix lands once instead of thirteen times.
//
//              This is example scaffolding, NOT part of the AntTweakBar API.
//              An application is free to copy it, but nothing in the library
//              depends on it.
//
//  usage:      Create the window and GL context yourself, call
//              atb_glfw_SetFontScaling() BEFORE TwInit(), then:
//
//                  TwInit(TW_OPENGL, NULL);
//                  atb_glfw_Attach(window, &hooks);   // registers callbacks
//                  ... your main loop, ending with TwDraw() ...
//                  TwTerminate();
//                  atb_glfw_Detach(window);           // after TwTerminate()
//
//              Window creation, the GL context, the main loop and TwDraw()
//              stay in the example on purpose - they are the part worth
//              reading.
//
//  multiple
//  windows:    atb_glfw_AttachWindow(window, twWindowID, &hooks) registers one
//              more window against its own AntTweakBar manager; every callback
//              then selects that manager with TwSetCurrentWindow() before
//              forwarding. atb_glfw_Attach() is exactly AttachWindow() with ID
//              0, the manager TwInit() creates. See MultiWindow_glfw.c.
//
//  hooks:      Every member of atb_glfw_Hooks is optional (NULL = ignore) and
//              runs only for events AntTweakBar did not consume, except
//              `resize`, which always runs (the example still owns its own
//              viewport and projection).
//
//  note:       All state below is file-scope `static`, so each translation
//              unit including this header gets its own copy. That is correct
//              for these single-file examples; do not include it twice in one
//              program and expect shared state.
//
//  ---------------------------------------------------------------------------

#ifndef ATB_GLFW_H_INCLUDED
#define ATB_GLFW_H_INCLUDED

#include <GLFW/glfw3.h>
#include <AntTweakBar.h>
#include <stdio.h>

// Raise before including this header if an example needs more windows.
#ifndef ATB_GLFW_MAX_WINDOWS
#define ATB_GLFW_MAX_WINDOWS 4
#endif

//  ---------------------------------------------------------------------------
//  Application hooks
//  ---------------------------------------------------------------------------

typedef struct atb_glfw_Hooks
{
    // Called only when AntTweakBar did not consume the event.
    void (*key)(GLFWwindow *window, int key, int scancode, int action, int mods);
    void (*mouseButton)(GLFWwindow *window, int button, int action, int mods);
    void (*cursorPos)(GLFWwindow *window, double xpos, double ypos);
    void (*scroll)(GLFWwindow *window, double xoffset, double yoffset);
    // Always called, before TwWindowSize(): the example owns its viewport and
    // projection. Size is in framebuffer pixels.
    void (*resize)(GLFWwindow *window, int fbWidth, int fbHeight);
    void *userData;
} atb_glfw_Hooks;

//  ---------------------------------------------------------------------------
//  State (per translation unit - see the note above)
//  ---------------------------------------------------------------------------

// One entry per attached window. Everything here is per-window because
// AntTweakBar keeps one manager (bars, hit state, canvas size) per window ID,
// and each window has its own point/pixel dimensions.
typedef struct atb_glfw_WindowState
{
    GLFWwindow    *window;
    int            twWindowID;
    // GLFW reports the cursor position in window points, but TwWindowSize() is
    // fed framebuffer pixels, so mouse events are scaled by this
    // window/framebuffer ratio before reaching AntTweakBar - otherwise its
    // pixel-space hit testing would misread a point-space cursor
    // (docs/plans/examples-hidpi-scaling.md).
    double         mouseScaleX, mouseScaleY;
    double         wheelPos;   // TwMouseWheel() wants an absolute position
    atb_glfw_Hooks hooks;
} atb_glfw_WindowState;

static atb_glfw_WindowState atb_glfw_g_Windows[ATB_GLFW_MAX_WINDOWS];
static int atb_glfw_g_WindowCount = 0;

// The window the pointer was last seen over. AntTweakBar's cursor callback is a
// single, process-wide hook with no idea which of our windows it means, so we
// track that ourselves.
static GLFWwindow *atb_glfw_g_ActiveWindow = NULL;

// The window content scale, as measured by atb_glfw_SetFontScaling(). Not
// per-window: the fonts are a process-wide resource shared by every manager, so
// there is only ever one fontscaling value in play.
static float atb_glfw_g_ContentScaleX = 1.0f, atb_glfw_g_ContentScaleY = 1.0f;

// GLFW3 cursor binding (docs/glfw3-cursor-integration.md): AntTweakBar predates
// cursor-ownership models like GLFW3's, and toolkits that reassert their own
// cursor on every mouse move (macOS's Cocoa backend, for one) would silently
// overwrite a natively set cursor. Routing every change through glfwSetCursor()
// gives GLFW3 authoritative ownership.
static GLFWcursor *atb_glfw_g_StandardCursors[TW_CURSOR_CUSTOM] = { NULL };
static GLFWcursor *atb_glfw_g_LastCustomCursor = NULL;
static int atb_glfw_g_CursorHidden = 0;

//  ---------------------------------------------------------------------------
//  Window registry
//  ---------------------------------------------------------------------------

static inline atb_glfw_WindowState * atb_glfw_Find(GLFWwindow *window)
{
    int i;
    for (i = 0; i < atb_glfw_g_WindowCount; ++i)
        if (atb_glfw_g_Windows[i].window == window)
            return &atb_glfw_g_Windows[i];
    return NULL;
}

// Resolves the window an event belongs to, makes its AntTweakBar manager the
// current one, and remembers it as the window the cursor callback should act
// on. Returns NULL for a window this header does not know.
static inline atb_glfw_WindowState * atb_glfw_Select(GLFWwindow *window)
{
    atb_glfw_WindowState *w = atb_glfw_Find(window);
    if (w == NULL)
        return NULL;
    atb_glfw_g_ActiveWindow = window;
    TwSetCurrentWindow(w->twWindowID);
    return w;
}

//  ---------------------------------------------------------------------------
//  Accessors
//  ---------------------------------------------------------------------------

static inline double atb_glfw_MouseScaleX(void)
{
    atb_glfw_WindowState *w = atb_glfw_Find(atb_glfw_g_ActiveWindow);
    return (w != NULL) ? w->mouseScaleX : 1.0;
}

static inline double atb_glfw_MouseScaleY(void)
{
    atb_glfw_WindowState *w = atb_glfw_Find(atb_glfw_g_ActiveWindow);
    return (w != NULL) ? w->mouseScaleY : 1.0;
}

// Valid after atb_glfw_SetFontScaling(). Examples use these to scale their bar
// so it keeps up with the now-larger scaled contents.
static inline float atb_glfw_ContentScaleX(void) { return atb_glfw_g_ContentScaleX; }
static inline float atb_glfw_ContentScaleY(void) { return atb_glfw_g_ContentScaleY; }

//  ---------------------------------------------------------------------------
//  Clipboard and cursor
//  ---------------------------------------------------------------------------

static inline const char * TW_CALL atb_glfw_ClipboardGet(void *_ClientData)
{
    (void)_ClientData;
    return glfwGetClipboardString(NULL);
}

static inline void TW_CALL atb_glfw_ClipboardSet(const char *_Text, void *_ClientData)
{
    (void)_ClientData;
    glfwSetClipboardString(NULL, _Text);
}

static inline int atb_glfw_StandardCursorShape(ETwCursor _Cursor)
{
    switch (_Cursor) {
    case TW_CURSOR_ARROW:        return GLFW_ARROW_CURSOR;
    case TW_CURSOR_MOVE:         return GLFW_RESIZE_ALL_CURSOR;
    case TW_CURSOR_RESIZE_WE:    return GLFW_RESIZE_EW_CURSOR;
    case TW_CURSOR_RESIZE_NS:    return GLFW_RESIZE_NS_CURSOR;
    case TW_CURSOR_RESIZE_NESW:  return GLFW_RESIZE_NESW_CURSOR;
    case TW_CURSOR_RESIZE_NWSE:  return GLFW_RESIZE_NWSE_CURSOR;
    case TW_CURSOR_HAND:         return GLFW_POINTING_HAND_CURSOR;
    case TW_CURSOR_CROSS:        return GLFW_CROSSHAIR_CURSOR;
    case TW_CURSOR_IBEAM:        return GLFW_IBEAM_CURSOR;
    case TW_CURSOR_NO:           return GLFW_NOT_ALLOWED_CURSOR;
    default:                     return GLFW_ARROW_CURSOR; // TW_CURSOR_HELP/UPARROW: no dedicated GLFW shape
    }
}

static inline void TW_CALL atb_glfw_CursorCB(ETwCursor _Cursor, const unsigned char *_RGBA32x32, int _HotX, int _HotY, void *_ClientData)
{
    // The window the pointer is over, not the one that happened to install this
    // callback - with several windows those differ.
    GLFWwindow *window = atb_glfw_g_ActiveWindow;
    (void)_ClientData;
    if (window == NULL)
        return;

    // TW_CURSOR_HIDDEN is an input mode, not a cursor shape: the RotoSlider
    // hides the pointer while it is dragged. The flag remembers that, so the
    // mode is restored once, on the next request for a visible cursor.
    if (_Cursor == TW_CURSOR_HIDDEN) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        atb_glfw_g_CursorHidden = 1;
        return;
    }
    if (atb_glfw_g_CursorHidden) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        atb_glfw_g_CursorHidden = 0;
    }

    if (_Cursor == TW_CURSOR_CUSTOM && _RGBA32x32 != NULL) {
        GLFWimage img;
        GLFWcursor *cur;
        img.width = 32; img.height = 32;
        img.pixels = (unsigned char *)_RGBA32x32; // glfwCreateCursor only reads it
        cur = glfwCreateCursor(&img, _HotX, _HotY);
        if (cur != NULL) {
            // Set the new cursor before destroying the old one: destroying a
            // cursor still current for a window resets that window to the
            // default arrow, which would undo this if done first.
            glfwSetCursor(window, cur);
            if (atb_glfw_g_LastCustomCursor != NULL)
                glfwDestroyCursor(atb_glfw_g_LastCustomCursor);
            atb_glfw_g_LastCustomCursor = cur;
        }
        return;
    }

    if ((int)_Cursor < 0 || (int)_Cursor >= TW_CURSOR_CUSTOM)
        return; // not a standard shape; nothing sensible to set
    if (atb_glfw_g_StandardCursors[_Cursor] == NULL)
        atb_glfw_g_StandardCursors[_Cursor] = glfwCreateStandardCursor(atb_glfw_StandardCursorShape(_Cursor));
    if (atb_glfw_g_StandardCursors[_Cursor] != NULL)
        glfwSetCursor(window, atb_glfw_g_StandardCursors[_Cursor]);
}

static inline void atb_glfw_DestroyCursorCache(void)
{
    int i;
    for (i = 0; i < TW_CURSOR_CUSTOM; ++i) {
        if (atb_glfw_g_StandardCursors[i] != NULL) {
            glfwDestroyCursor(atb_glfw_g_StandardCursors[i]);
            atb_glfw_g_StandardCursors[i] = NULL;
        }
    }
    if (atb_glfw_g_LastCustomCursor != NULL) {
        glfwDestroyCursor(atb_glfw_g_LastCustomCursor);
        atb_glfw_g_LastCustomCursor = NULL;
    }
}

//  ---------------------------------------------------------------------------
//  Key translation
//  ---------------------------------------------------------------------------

// AntTweakBar's key codes come from SDL 1.2 and do not match GLFW3's, so
// TwEventKeyGLFW()/TwEventCharGLFW() (built against GLFW2 constants) must not
// be used here - see the wiki's Integration-Guide-GLFW3. This is the mapping.
// Returns the TW_KEY_* code, or 0 when the key has no AntTweakBar equivalent.
static inline int atb_glfw_TranslateKey(int key, int mods)
{
    int ctrl = (mods & GLFW_MOD_CONTROL) != 0;

    switch (key) {
    case GLFW_KEY_BACKSPACE: return TW_KEY_BACKSPACE;
    case GLFW_KEY_TAB:       return TW_KEY_TAB;
    case GLFW_KEY_ENTER:     return TW_KEY_RETURN;
    case GLFW_KEY_PAUSE:     return TW_KEY_PAUSE;
    case GLFW_KEY_SPACE:     return TW_KEY_SPACE;
    case GLFW_KEY_DELETE:    return TW_KEY_DELETE;
    case GLFW_KEY_UP:        return TW_KEY_UP;
    case GLFW_KEY_DOWN:      return TW_KEY_DOWN;
    case GLFW_KEY_RIGHT:     return TW_KEY_RIGHT;
    case GLFW_KEY_LEFT:      return TW_KEY_LEFT;
    case GLFW_KEY_INSERT:    return TW_KEY_INSERT;
    case GLFW_KEY_HOME:      return TW_KEY_HOME;
    case GLFW_KEY_END:       return TW_KEY_END;
    case GLFW_KEY_PAGE_UP:   return TW_KEY_PAGE_UP;
    case GLFW_KEY_PAGE_DOWN: return TW_KEY_PAGE_DOWN;
    case GLFW_KEY_F1:  return TW_KEY_F1;
    case GLFW_KEY_F2:  return TW_KEY_F2;
    case GLFW_KEY_F3:  return TW_KEY_F3;
    case GLFW_KEY_F4:  return TW_KEY_F4;
    case GLFW_KEY_F5:  return TW_KEY_F5;
    case GLFW_KEY_F6:  return TW_KEY_F6;
    case GLFW_KEY_F7:  return TW_KEY_F7;
    case GLFW_KEY_F8:  return TW_KEY_F8;
    case GLFW_KEY_F9:  return TW_KEY_F9;
    case GLFW_KEY_F10: return TW_KEY_F10;
    case GLFW_KEY_F11: return TW_KEY_F11;
    case GLFW_KEY_F12: return TW_KEY_F12;
    case GLFW_KEY_F13: return TW_KEY_F13;
    case GLFW_KEY_F14: return TW_KEY_F14;
    case GLFW_KEY_F15: return TW_KEY_F15;
    default: break;
    }

    // Ctrl+letter shortcuts arrive here, not through the character callback.
    if (ctrl && key < 128)
        return key;
    return 0;
}

static inline int atb_glfw_TranslateModifiers(int mods)
{
    int twMod = 0;
    if (mods & GLFW_MOD_SHIFT)   twMod |= TW_KMOD_SHIFT;
    if (mods & GLFW_MOD_CONTROL) twMod |= TW_KMOD_CTRL;
    if (mods & GLFW_MOD_ALT)     twMod |= TW_KMOD_ALT;
    return twMod;
}

//  ---------------------------------------------------------------------------
//  GLFW callbacks
//  ---------------------------------------------------------------------------

static inline void atb_glfw_KeyCB(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    atb_glfw_WindowState *w = atb_glfw_Select(window);
    if (w == NULL)
        return;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        int twKey = atb_glfw_TranslateKey(key, mods);
        if (twKey != 0 && TwKeyPressed(twKey, atb_glfw_TranslateModifiers(mods)))
            return;
    }
    if (w->hooks.key != NULL)
        w->hooks.key(window, key, scancode, action, mods);
}

static inline void atb_glfw_CharCB(GLFWwindow *window, unsigned int codepoint)
{
    if (atb_glfw_Select(window) == NULL)
        return;
    TwKeyPressed((int)codepoint, 0);
}

static inline void atb_glfw_MouseButtonCB(GLFWwindow *window, int button, int action, int mods)
{
    atb_glfw_WindowState *w = atb_glfw_Select(window);
    if (w == NULL)
        return;
    // Safe with GLFW3: its button and action constants happen to match the
    // GLFW2-era values this helper was built against.
    if (TwEventMouseButtonGLFW(button, action))
        return;
    if (w->hooks.mouseButton != NULL)
        w->hooks.mouseButton(window, button, action, mods);
}

static inline void atb_glfw_CursorPosCB(GLFWwindow *window, double xpos, double ypos)
{
    atb_glfw_WindowState *w = atb_glfw_Select(window);
    if (w == NULL)
        return;
    if (TwMouseMotion((int)(xpos * w->mouseScaleX), (int)(ypos * w->mouseScaleY)))
        return;
    if (w->hooks.cursorPos != NULL)
        w->hooks.cursorPos(window, xpos, ypos);
}

static inline void atb_glfw_ScrollCB(GLFWwindow *window, double xoffset, double yoffset)
{
    atb_glfw_WindowState *w = atb_glfw_Select(window);
    if (w == NULL)
        return;
    // TwMouseWheel() takes an absolute accumulated position, not a delta.
    w->wheelPos += yoffset;
    if (TwMouseWheel((int)w->wheelPos))
        return;
    if (w->hooks.scroll != NULL)
        w->hooks.scroll(window, xoffset, yoffset);
}

// Registered as the FRAMEBUFFER size callback, not the window size callback:
// GLFW reports this one in actual pixels, matching glViewport and TwWindowSize.
static inline void atb_glfw_FramebufferSizeCB(GLFWwindow *window, int width, int height)
{
    int winWidth = width, winHeight = height;
    atb_glfw_WindowState *w = atb_glfw_Find(window);
    if (w == NULL)
        return;
    if (height == 0) height = 1;

    TwSetCurrentWindow(w->twWindowID);
    if (w->hooks.resize != NULL)
        w->hooks.resize(window, width, height);
    TwWindowSize(width, height);

    glfwGetWindowSize(window, &winWidth, &winHeight);
    w->mouseScaleX = (winWidth  > 0) ? (double)width  / winWidth  : 1.0;
    w->mouseScaleY = (winHeight > 0) ? (double)height / winHeight : 1.0;
}

//  ---------------------------------------------------------------------------
//  Setup and teardown
//  ---------------------------------------------------------------------------

// Scales AntTweakBar's font by the window's content scale, so its fixed-pixel
// widgets keep a comparable physical size on a HiDPI display. Returns that
// scale, which examples also use to size their bar.
//
// MUST be called BEFORE TwInit(): "fontscaling" is honoured only while the
// library is uninitialised, because the scale is baked into the font atlases
// when they are generated.
static inline float atb_glfw_SetFontScaling(GLFWwindow *window)
{
    char def[64];
    glfwGetWindowContentScale(window, &atb_glfw_g_ContentScaleX, &atb_glfw_g_ContentScaleY);
    snprintf(def, sizeof(def), "GLOBAL fontscaling=%g", (double)atb_glfw_g_ContentScaleX);
    TwDefine(def);
    return atb_glfw_g_ContentScaleX;
}

// Registers every GLFW callback for `window`, binds it to AntTweakBar's manager
// `twWindowID`, and applies the current framebuffer size. Call after TwInit()
// (and, for an ID other than 0, after TwSetCurrentWindow() has created that
// manager). `hooks` may be NULL; it is copied, so it need not outlive this call.
static inline void atb_glfw_AttachWindow(GLFWwindow *window, int twWindowID, const atb_glfw_Hooks *hooks)
{
    atb_glfw_WindowState *w;
    int fbWidth = 0, fbHeight = 0;

    w = atb_glfw_Find(window);
    if (w == NULL) {
        if (atb_glfw_g_WindowCount >= ATB_GLFW_MAX_WINDOWS) {
            fprintf(stderr, "atb_glfw: more than %d windows attached - raise ATB_GLFW_MAX_WINDOWS\n",
                    ATB_GLFW_MAX_WINDOWS);
            return;
        }
        w = &atb_glfw_g_Windows[atb_glfw_g_WindowCount++];
    }

    w->window      = window;
    w->twWindowID  = twWindowID;
    w->mouseScaleX = 1.0;
    w->mouseScaleY = 1.0;
    w->wheelPos    = 0.0;
    if (hooks != NULL) {
        w->hooks = *hooks;
    } else {
        atb_glfw_Hooks empty = { 0 };
        w->hooks = empty;
    }
    if (atb_glfw_g_ActiveWindow == NULL)
        atb_glfw_g_ActiveWindow = window;

    // Give GLFW3 authoritative cursor ownership, and route the clipboard
    // through it so the library needs no toolkit linkage of its own. Both are
    // process-wide in AntTweakBar, so repeating them per window is harmless.
    TwSetCursorCallback(atb_glfw_CursorCB, NULL);
    TwSetClipboardCallback(atb_glfw_ClipboardGet, atb_glfw_ClipboardSet, NULL);

    glfwSetKeyCallback(window, atb_glfw_KeyCB);
    glfwSetCharCallback(window, atb_glfw_CharCB);
    glfwSetMouseButtonCallback(window, atb_glfw_MouseButtonCB);
    glfwSetCursorPosCallback(window, atb_glfw_CursorPosCB);
    glfwSetScrollCallback(window, atb_glfw_ScrollCB);
    glfwSetFramebufferSizeCallback(window, atb_glfw_FramebufferSizeCB);

    // Size the bars before the first frame.
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    atb_glfw_FramebufferSizeCB(window, fbWidth, fbHeight);
}

// Single-window shorthand: AntTweakBar's manager 0, the one TwInit() creates.
static inline void atb_glfw_Attach(GLFWwindow *window, const atb_glfw_Hooks *hooks)
{
    atb_glfw_AttachWindow(window, 0, hooks);
}

// Unregisters `window` and, once the last one is gone, releases the cursors
// this header created. Call AFTER TwTerminate(), so the library is no longer
// asking for cursor changes, and before glfwTerminate().
static inline void atb_glfw_Detach(GLFWwindow *window)
{
    atb_glfw_WindowState *w = atb_glfw_Find(window);
    if (w != NULL) {
        *w = atb_glfw_g_Windows[--atb_glfw_g_WindowCount];
        if (atb_glfw_g_ActiveWindow == window)
            atb_glfw_g_ActiveWindow = NULL;
    }
    if (atb_glfw_g_WindowCount == 0)
        atb_glfw_DestroyCursorCache();
}

#endif // ATB_GLFW_H_INCLUDED
