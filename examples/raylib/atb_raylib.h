//  ---------------------------------------------------------------------------
//
//  @file       atb_raylib.h
//  @brief      Header-only glue between raylib and AntTweakBar, shared by the
//              examples in this folder.
//
//              This is example scaffolding, NOT part of the AntTweakBar API.
//              An application is free to copy it, but nothing in the library
//              depends on it.
//
//  shape:      Unlike the GLFW3/SDL3/SFML3 glue headers, this one is POLLING
//              based, because raylib is. raylib owns the event loop and
//              exposes input only as per-frame state (IsKeyPressed(),
//              GetMouseX(), GetCharPressed(), ...), so there are no callbacks
//              to install and no hook struct: atb_raylib_Update() reads that
//              state once a frame and synthesizes the AntTweakBar events from
//              it. It returns whether AntTweakBar took the input, which is the
//              raylib-idiomatic equivalent of the other backends' hooks - the
//              example simply skips its own input handling when it did.
//
//  usage:          InitWindow(w, h, "...");
//                  atb_raylib_SetFontScaling();   // BEFORE TwInit()
//                  TwInit(TW_OPENGL_CORE, NULL);  // CORE: raylib is GL 3.3
//                  atb_raylib_Attach();
//                  while (!WindowShouldClose()) {
//                      bool guiTookInput = atb_raylib_Update();
//                      if (!guiTookInput) { ... your own input ... }
//                      BeginDrawing();
//                          ClearBackground(...);
//                          ... your raylib drawing ...
//                          atb_raylib_Draw();     // last, inside BeginDrawing
//                      EndDrawing();
//                  }
//                  TwTerminate();
//                  atb_raylib_Detach();
//                  CloseWindow();
//
//  note:       TwInit() must be given TW_OPENGL_CORE, not TW_OPENGL: raylib
//              builds against the OpenGL 3.3 core profile, where the
//              fixed-function pipeline the TW_OPENGL renderer uses does not
//              exist.
//
//  note:       All state below is file-scope `static`, so each translation
//              unit including this header gets its own copy. That is correct
//              for these single-file examples; do not include it twice in one
//              program and expect shared state.
//
//  ---------------------------------------------------------------------------

#ifndef ATB_RAYLIB_H_INCLUDED
#define ATB_RAYLIB_H_INCLUDED

#include "raylib.h"
#include "rlgl.h"       // rlDrawRenderBatchActive(), to flush raylib's batch
#include <AntTweakBar.h>
#include <stdio.h>

//  ---------------------------------------------------------------------------
//  State
//  ---------------------------------------------------------------------------

// raylib reports the mouse in logical screen points, but TwWindowSize() is fed
// render (framebuffer) pixels, so mouse positions are scaled by this ratio
// before reaching AntTweakBar. It is 1.0 unless the window was created with
// FLAG_WINDOW_HIGHDPI.
static float atb_raylib_g_MouseScaleX = 1.0f, atb_raylib_g_MouseScaleY = 1.0f;

static float atb_raylib_g_WheelPos = 0.0f;

// The window content scale, as measured by atb_raylib_SetFontScaling().
static float atb_raylib_g_ContentScaleX = 1.0f, atb_raylib_g_ContentScaleY = 1.0f;

static int atb_raylib_g_CursorHidden = 0;

static inline float atb_raylib_ContentScaleX(void) { return atb_raylib_g_ContentScaleX; }
static inline float atb_raylib_ContentScaleY(void) { return atb_raylib_g_ContentScaleY; }

//  ---------------------------------------------------------------------------
//  Clipboard and cursor
//  ---------------------------------------------------------------------------

static inline const char * TW_CALL atb_raylib_ClipboardGet(void *_ClientData)
{
    (void)_ClientData;
    return GetClipboardText();
}

static inline void TW_CALL atb_raylib_ClipboardSet(const char *_Text, void *_ClientData)
{
    (void)_ClientData;
    SetClipboardText(_Text);
}

static inline int atb_raylib_CursorShape(ETwCursor _Cursor)
{
    switch (_Cursor) {
    case TW_CURSOR_ARROW:       return MOUSE_CURSOR_ARROW;
    case TW_CURSOR_MOVE:        return MOUSE_CURSOR_RESIZE_ALL;
    case TW_CURSOR_RESIZE_WE:   return MOUSE_CURSOR_RESIZE_EW;
    case TW_CURSOR_RESIZE_NS:   return MOUSE_CURSOR_RESIZE_NS;
    case TW_CURSOR_RESIZE_NESW: return MOUSE_CURSOR_RESIZE_NESW;
    case TW_CURSOR_RESIZE_NWSE: return MOUSE_CURSOR_RESIZE_NWSE;
    case TW_CURSOR_HAND:        return MOUSE_CURSOR_POINTING_HAND;
    case TW_CURSOR_CROSS:       return MOUSE_CURSOR_CROSSHAIR;
    case TW_CURSOR_IBEAM:       return MOUSE_CURSOR_IBEAM;
    case TW_CURSOR_NO:          return MOUSE_CURSOR_NOT_ALLOWED;
    // TW_CURSOR_HELP/UPARROW, and TW_CURSOR_CUSTOM: raylib exposes only the
    // ten standard shapes above and no custom-cursor API at all, so an
    // AntTweakBar custom cursor image cannot be honoured - fall back to the
    // arrow rather than leave whatever shape was set last.
    default:                    return MOUSE_CURSOR_ARROW;
    }
}

static inline void TW_CALL atb_raylib_CursorCB(ETwCursor _Cursor, const unsigned char *_RGBA32x32, int _HotX, int _HotY, void *_ClientData)
{
    (void)_RGBA32x32; (void)_HotX; (void)_HotY; (void)_ClientData;

    // TW_CURSOR_HIDDEN is an input mode, not a cursor shape: the RotoSlider
    // hides the pointer while it is dragged. The flag remembers that, so the
    // cursor is shown again once, on the next request for a visible one.
    if (_Cursor == TW_CURSOR_HIDDEN) {
        if (!atb_raylib_g_CursorHidden) {
            HideCursor();
            atb_raylib_g_CursorHidden = 1;
        }
        return;
    }
    if (atb_raylib_g_CursorHidden) {
        ShowCursor();
        atb_raylib_g_CursorHidden = 0;
    }
    SetMouseCursor(atb_raylib_CursorShape(_Cursor));
}

//  ---------------------------------------------------------------------------
//  Key translation
//  ---------------------------------------------------------------------------

// AntTweakBar's key codes come from SDL 1.2 and do not match raylib's (which
// are GLFW3's), so this is the mapping. Returns the TW_KEY_* code, or 0 when
// the key has no AntTweakBar equivalent.
static inline int atb_raylib_TranslateKey(int key)
{
    switch (key) {
    case KEY_BACKSPACE: return TW_KEY_BACKSPACE;
    case KEY_TAB:       return TW_KEY_TAB;
    case KEY_ENTER:     return TW_KEY_RETURN;
    case KEY_KP_ENTER:  return TW_KEY_RETURN;
    case KEY_PAUSE:     return TW_KEY_PAUSE;
    case KEY_ESCAPE:    return TW_KEY_ESCAPE;
    case KEY_DELETE:    return TW_KEY_DELETE;
    case KEY_UP:        return TW_KEY_UP;
    case KEY_DOWN:      return TW_KEY_DOWN;
    case KEY_RIGHT:     return TW_KEY_RIGHT;
    case KEY_LEFT:      return TW_KEY_LEFT;
    case KEY_INSERT:    return TW_KEY_INSERT;
    case KEY_HOME:      return TW_KEY_HOME;
    case KEY_END:       return TW_KEY_END;
    case KEY_PAGE_UP:   return TW_KEY_PAGE_UP;
    case KEY_PAGE_DOWN: return TW_KEY_PAGE_DOWN;
    case KEY_F1:  return TW_KEY_F1;
    case KEY_F2:  return TW_KEY_F2;
    case KEY_F3:  return TW_KEY_F3;
    case KEY_F4:  return TW_KEY_F4;
    case KEY_F5:  return TW_KEY_F5;
    case KEY_F6:  return TW_KEY_F6;
    case KEY_F7:  return TW_KEY_F7;
    case KEY_F8:  return TW_KEY_F8;
    case KEY_F9:  return TW_KEY_F9;
    case KEY_F10: return TW_KEY_F10;
    case KEY_F11: return TW_KEY_F11;
    case KEY_F12: return TW_KEY_F12;
    default:      return 0;
    }
}

static inline int atb_raylib_Modifiers(void)
{
    int twMod = 0;
    if (IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT))   twMod |= TW_KMOD_SHIFT;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) twMod |= TW_KMOD_CTRL;
    if (IsKeyDown(KEY_LEFT_ALT)     || IsKeyDown(KEY_RIGHT_ALT))     twMod |= TW_KMOD_ALT;
    return twMod;
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
static inline float atb_raylib_SetFontScaling(void)
{
    Vector2 dpi = GetWindowScaleDPI();
    char def[64];
    atb_raylib_g_ContentScaleX = (dpi.x > 0.0f) ? dpi.x : 1.0f;
    atb_raylib_g_ContentScaleY = (dpi.y > 0.0f) ? dpi.y : 1.0f;
    snprintf(def, sizeof(def), "GLOBAL fontscaling=%g", (double)atb_raylib_g_ContentScaleX);
    TwDefine(def);
    return atb_raylib_g_ContentScaleX;
}

// Recomputes the point->pixel mouse scale and hands AntTweakBar the current
// render size. Called by Attach() and whenever the window resizes.
static inline void atb_raylib_UpdateSize(void)
{
    int renderW = GetRenderWidth(),  renderH = GetRenderHeight();
    int screenW = GetScreenWidth(),  screenH = GetScreenHeight();
    atb_raylib_g_MouseScaleX = (screenW > 0) ? (float)renderW / screenW : 1.0f;
    atb_raylib_g_MouseScaleY = (screenH > 0) ? (float)renderH / screenH : 1.0f;
    TwWindowSize(renderW, renderH);
}

// Installs AntTweakBar's cursor and clipboard callbacks and applies the
// current window size. Call after TwInit().
static inline void atb_raylib_Attach(void)
{
    // Give raylib authoritative cursor ownership, and route the clipboard
    // through it so the library needs no toolkit linkage of its own.
    TwSetCursorCallback(atb_raylib_CursorCB, NULL);
    TwSetClipboardCallback(atb_raylib_ClipboardGet, atb_raylib_ClipboardSet, NULL);
    atb_raylib_UpdateSize();
}

// Restores anything this header changed about raylib's state. Call AFTER
// TwTerminate() and before CloseWindow().
static inline void atb_raylib_Detach(void)
{
    if (atb_raylib_g_CursorHidden) {
        ShowCursor();
        atb_raylib_g_CursorHidden = 0;
    }
    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
}

//  ---------------------------------------------------------------------------
//  Per-frame input
//  ---------------------------------------------------------------------------

// Polls raylib's input state and forwards it to AntTweakBar. Call once per
// frame, before your own input handling and before atb_raylib_Draw().
//
// Returns true when AntTweakBar consumed something - the cursor is over a bar,
// a widget is being dragged, an edit field has focus - which is the signal for
// the caller to leave that frame's input alone.
static inline bool atb_raylib_Update(void)
{
    bool handled = false;
    int twMod, key, codepoint, i;
    float wheel;
    static const int buttons[3] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_MIDDLE, MOUSE_BUTTON_RIGHT };
    static const TwMouseButtonID twButtons[3] = { TW_MOUSE_LEFT, TW_MOUSE_MIDDLE, TW_MOUSE_RIGHT };

    if (IsWindowResized()) atb_raylib_UpdateSize();

    if (TwMouseMotion((int)(GetMouseX() * atb_raylib_g_MouseScaleX),
                      (int)(GetMouseY() * atb_raylib_g_MouseScaleY)))
        handled = true;

    for (i = 0; i < 3; ++i) {
        if (IsMouseButtonPressed(buttons[i])
            && TwMouseButton(TW_MOUSE_PRESSED, twButtons[i])) handled = true;
        // A release is always forwarded, even when AntTweakBar says it did not
        // want it: the press that started a drag may have been over a bar that
        // has since scrolled away, and the bar still has to let go.
        if (IsMouseButtonReleased(buttons[i]))
            TwMouseButton(TW_MOUSE_RELEASED, twButtons[i]);
    }

    // TwMouseWheel() takes an absolute accumulated position, not a delta.
    wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        atb_raylib_g_WheelPos += wheel;
        if (TwMouseWheel((int)atb_raylib_g_WheelPos)) handled = true;
    }

    twMod = atb_raylib_Modifiers();

    // Printable input, already composed by raylib into Unicode code points.
    while ((codepoint = GetCharPressed()) != 0) {
        if (TwKeyPressed(codepoint, 0)) handled = true;
    }

    // Everything the character queue does not report: navigation, editing and
    // function keys, plus the Ctrl+letter shortcuts (which produce no
    // character). GetKeyPressed() drains raylib's queue of this frame's key
    // presses; auto-repeat is not in that queue, so held keys are checked
    // separately below.
    while ((key = GetKeyPressed()) != 0) {
        int twKey = atb_raylib_TranslateKey(key);
        if (twKey == 0 && (twMod & TW_KMOD_CTRL) && key >= KEY_A && key <= KEY_Z)
            twKey = key - KEY_A + 'a'; // AntTweakBar expects the lowercase letter
        if (twKey != 0 && TwKeyPressed(twKey, twMod)) handled = true;
    }

    // Auto-repeat for the keys that need it while held - the arrows,
    // backspace and delete, so holding one keeps moving the caret or deleting.
    {
        static const int repeatable[] = {
            KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_BACKSPACE, KEY_DELETE,
        };
        for (i = 0; i < (int)(sizeof(repeatable) / sizeof(repeatable[0])); ++i) {
            if (IsKeyPressedRepeat(repeatable[i])
                && TwKeyPressed(atb_raylib_TranslateKey(repeatable[i]), twMod)) handled = true;
        }
    }

    return handled;
}

//  ---------------------------------------------------------------------------
//  Drawing
//  ---------------------------------------------------------------------------

// Draws every tweak bar. Call inside BeginDrawing()/EndDrawing(), after all of
// your own raylib drawing, so the bars end up on top.
//
// rlDrawRenderBatchActive() first: raylib batches its 2D/3D geometry and
// flushes it lazily, so anything still queued has to reach the GPU before
// AntTweakBar binds its own shader and vertex arrays - otherwise raylib's
// pending triangles would be drawn later, with AntTweakBar's state, on top of
// the bars. raylib re-binds what it needs on its next batch, so nothing has to
// be restored afterwards.
static inline void atb_raylib_Draw(void)
{
    rlDrawRenderBatchActive();
    TwDraw();
}

#endif // ATB_RAYLIB_H_INCLUDED
