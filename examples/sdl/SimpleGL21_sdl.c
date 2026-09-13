//  ---------------------------------------------------------------------------
//
//  @file       SimpleGL21.c
//  @brief      A simple example that uses AntTweakBar with
//              OpenGL 2.1 (compatibility profile) and the SDL3 windowing
//              system.
//
//              Also demonstrates a temporary, self-contained dialog bar:
//              pressing [Esc] shows a "Quit the application?" bar with
//              Yes/No buttons, built at that moment with TwNewBar() and
//              torn down again with TwDeleteBar() - see ShowConfirmQuitBar()
//              below. SDL3 port of examples/glfw/SimpleGL21.c - see
//              docs/plans/sdl3-backend.md for the backend adapter notes.
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              SDL:         https://www.libsdl.org
//
//  @author     Philippe Decaudin
//  @date       2006/05/20
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <AntTweakBar.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

// SDL3 cursors are process-global (SDL_SetCursor() takes no window
// argument, unlike glfwSetCursor()) - there is no per-window cursor-
// ownership fight to route around the way examples/glfw/SimpleGL21.c's own
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

float g_cameraPosX = 0.0f;
float g_cameraPosY = 0.0f;
float g_cameraPosZ = 5.0f;

bool g_cameraDragging = false;

float g_lastMouseX = 0.0f;
float g_lastMouseY = 0.0f;

char *g_userText = NULL; // Will be malloc'ed on first use

// Quit-confirmation dialog state (see ShowConfirmQuitBar() below).
static TwBar *g_ConfirmBar = NULL; // the "ConfirmQuit" bar, or NULL when not shown

// Window content scale (see fontscaling comment near TwInit() in main()),
// stashed here so ShowConfirmQuitBar() - which runs later, with no access
// to main()'s locals - can scale its own bar's size the same way.
float g_ContentScale = 1.0f;

// SDL always reports mouse position in window points, but TwWindowSize()
// is now fed the pixel size (see handleWindowPixelSizeChanged), so mouse
// events must be scaled by this window/pixel ratio before reaching
// AntTweakBar, or its hit-testing/drawing (now in pixel space) would
// misread a point-space cursor position - see docs/plans/examples-hidpi-scaling.md.
static double g_MouseScaleX = 1.0, g_MouseScaleY = 1.0;

static void CloseConfirmQuitBar(void);

// clientData is a pointer to the main loop's own `running` flag - SDL has
// no per-window "should close" query/flag the way glfwSetWindowShouldClose/
// glfwWindowShouldClose does, so the confirm-quit dialog signals the main
// loop directly instead.
void TW_CALL ConfirmQuitYesCB(void *clientData)
{
    *(bool *)clientData = false;
}

void TW_CALL ConfirmQuitNoCB(void *clientData)
{
    (void)clientData;
    CloseConfirmQuitBar();
}

// Hides (or shows again) every bar that currently exists - including the
// built-in help bar, which is only ever minimized by default and would
// otherwise still be reachable while the confirmation dialog is up - so
// nothing but the dialog itself can react to input.
static void SetAllBarsVisible(int visible)
{
    int i, barCount = TwGetBarCount();
    char def[128];
    for( i=0; i<barCount; ++i )
    {
        TwBar *b = TwGetBarByIndex(i);
        if( b == g_ConfirmBar )
            continue;
        snprintf(def, sizeof(def), " %s visible=%s ", TwGetBarName(b), visible ? "true" : "false");
        TwDefine(def);
    }
}

// Creates a small centered "Quit the application?" bar with Yes/No buttons,
// and hides every other bar so none of their widgets can react to input
// while the question is pending (AntTweakBar has no built-in modal dialog -
// hiding the other bars is how this demo approximates one).
static void ShowConfirmQuitBar(SDL_Window *window, bool *runningFlag)
{
    char def[160];
    int winWidth, winHeight, barWidth, barHeight, posX, posY;

    if( g_ConfirmBar != NULL )
        return; // already showing

    SetAllBarsVisible(0);

    SDL_GetWindowSizeInPixels(window, &winWidth, &winHeight);
    barWidth  = (int)(220 * g_ContentScale + 0.5f);
    barHeight = (int)(80 * g_ContentScale + 0.5f);
    posX = (winWidth  - barWidth)  / 2; if( posX < 0 ) posX = 0;
    posY = (winHeight - barHeight) / 2; if( posY < 0 ) posY = 0;

    g_ConfirmBar = TwNewBar("ConfirmQuit");
    snprintf(def, sizeof(def),
             " ConfirmQuit label='Confirm' size='%d %d' position='%d %d' "
             "resizable=false movable=false iconifiable=false ",
             barWidth, barHeight, posX, posY);
    TwDefine(def);

    TwAddButton(g_ConfirmBar, "Msg", NULL, NULL, " label='Quit the application?' ");
    TwAddButton(g_ConfirmBar, "Yes", ConfirmQuitYesCB, runningFlag, " label='Yes' ");
    TwAddButton(g_ConfirmBar, "No",  ConfirmQuitNoCB,  NULL,        " label='No' ");
    TwSetTopBar(g_ConfirmBar);
}

static void CloseConfirmQuitBar(void)
{
    if( g_ConfirmBar == NULL )
        return;
    TwDeleteBar(g_ConfirmBar);
    g_ConfirmBar = NULL;
    SetAllBarsVisible(1);
}

static void handleKeyDown(const SDL_KeyboardEvent *_Event, SDL_Window *window, bool *runningFlag)
{
    if (_Event->key == SDLK_ESCAPE) {
        if (g_ConfirmBar == NULL)
            ShowConfirmQuitBar(window, runningFlag);
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
// TwKeyPressed(), same as examples/glfw/SimpleGL21.c's charCallback does
// per GLFW callback invocation. Only handles well-formed UTF-8 (1-3 byte
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

    if (_Event->button == SDL_BUTTON_LEFT) {
        if (_Event->down) {
            g_cameraDragging = true;
            g_lastMouseX = _Event->x;
            g_lastMouseY = _Event->y;
        } else {
            g_cameraDragging = false;
        }
    }

    if (_Event->button == SDL_BUTTON_RIGHT && _Event->down) {
        g_cameraPosX = 0;
        g_cameraPosY = 0;
        g_cameraPosZ = 5.0f; // Reset camera position
    }
}

static void handleMouseMotion(SDL_Window *window, const SDL_MouseMotionEvent *_Event)
{
    TwMouseMotion((int)(_Event->x * g_MouseScaleX), (int)(_Event->y * g_MouseScaleY));

    if (g_cameraDragging) {
        float dx = _Event->x - g_lastMouseX;
        float dy = _Event->y - g_lastMouseY;

        int width, height;
        SDL_GetWindowSize(window, &width, &height);
        g_cameraPosX += dx / width * 2.0f;  // Scale to screen
        g_cameraPosY -= dy / height * 2.0f; // Inverted Y

        g_lastMouseX = _Event->x;
        g_lastMouseY = _Event->y;
    }
}

static void handleMouseWheel(const SDL_MouseWheelEvent *_Event)
{
    static double pos = 0;
    pos += _Event->y;
    g_cameraPosZ -= (float)_Event->y * 0.05f; // Zoom sensitivity
    if (g_cameraPosZ < 1.0f) g_cameraPosZ = 1.0f; // Prevent too close
    if (g_cameraPosZ > 50.0f) g_cameraPosZ = 50.0f; // Prevent too far

    TwMouseWheel((int)pos);
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
// examples/glfw/SimpleGL21.c's windowSizeCallback (registered as GLFW's
// FRAMEBUFFER size callback there for the same pixel-vs-point reason).
static void handleWindowPixelSizeChanged(SDL_Window *_Window, int _Width, int _Height)
{
    if (_Height == 0) _Height = 1;
    float aspect = (float)_Width / (float)_Height;
    float near = 1.0f, far = 100.0f;
    float fov = 45.0f;
    float top = tanf(fov * 0.01745329251f) * near;
    float bottom = -top;
    float right = top * aspect;
    float left = -right;

    glViewport(0, 0, _Width, _Height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(left, right, bottom, top, near, far);

    TwWindowSize(_Width, _Height);
    updateMouseScale(_Window, _Width, _Height);
}

void TW_CALL ResetCubePosition(void *clientData)
{
    (void)clientData;
    g_cameraPosX = 0;
    g_cameraPosY = 0;
    g_cameraPosZ = 5.0f; // Reset camera position
}

// Copy function for CDSTRING (required by AntTweakBar)
void TW_CALL CopyCDStringToClient(char **destPtr, const char *src)
{
    size_t len = src ? strlen(src) : 0;
    *destPtr = (char*)realloc(*destPtr, len + 1);
    if (*destPtr) {
        strcpy(*destPtr, src);
    }
}

void TW_CALL PrintTextCallback(void *clientData)
{
    (void)clientData;
    printf("User text: %s\n", g_userText ? g_userText : "(null)");
    fflush(stdout);
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

// This example program draws a possibly transparent cube
void DrawModel(int _wireframe)
{
  int pass, numPass;
  // Enable OpenGL transparency and light (could have been done once at init)
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_LIGHT0);    // use default light diffuse and position
  glEnable(GL_NORMALIZE);
  glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);
  glEnable(GL_COLOR_MATERIAL);
  glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);
  glEnable(GL_LINE_SMOOTH);
  glLineWidth(3.0);

  if( _wireframe )
  {
      glDisable(GL_CULL_FACE);
      glDisable(GL_LIGHTING);
      glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
      numPass = 1;
  }else{
      glEnable(GL_CULL_FACE);
      glFrontFace(GL_CCW);
      glEnable(GL_LIGHTING);
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      numPass = 2;
  }

  for(pass = 0; pass < numPass; ++pass)
  {
    // Since the material could be transparent, we draw the convex model in 2 passes:
    // first its back faces, and second its front faces.
    glCullFace( (pass==0) ? GL_FRONT : GL_BACK );

    // Draw the model (a cube)
    glBegin(GL_QUADS);
      // Front face (z = +0.5)
      glNormal3f(0, 0, 1);
      glVertex3f(-0.5f, -0.5f,  0.5f);
      glVertex3f( 0.5f, -0.5f,  0.5f);
      glVertex3f( 0.5f,  0.5f,  0.5f);
      glVertex3f(-0.5f,  0.5f,  0.5f);

      // Back face (z = -0.5)
      glNormal3f(0, 0, -1);
      glVertex3f( 0.5f, -0.5f, -0.5f);
      glVertex3f(-0.5f, -0.5f, -0.5f);
      glVertex3f(-0.5f,  0.5f, -0.5f);
      glVertex3f( 0.5f,  0.5f, -0.5f);

      // Left face (x = -0.5)
      glNormal3f(-1, 0, 0);
      glVertex3f(-0.5f, -0.5f, -0.5f);
      glVertex3f(-0.5f, -0.5f,  0.5f);
      glVertex3f(-0.5f,  0.5f,  0.5f);
      glVertex3f(-0.5f,  0.5f, -0.5f);

      // Right face (x = +0.5)
      glNormal3f(1, 0, 0);
      glVertex3f( 0.5f, -0.5f,  0.5f);
      glVertex3f( 0.5f, -0.5f, -0.5f);
      glVertex3f( 0.5f,  0.5f, -0.5f);
      glVertex3f( 0.5f,  0.5f,  0.5f);

      // Bottom face (y = -0.5)
      glNormal3f(0, -1, 0);
      glVertex3f(-0.5f, -0.5f, -0.5f);
      glVertex3f( 0.5f, -0.5f, -0.5f);
      glVertex3f( 0.5f, -0.5f,  0.5f);
      glVertex3f(-0.5f, -0.5f,  0.5f);

      // Top face (y = +0.5)
      glNormal3f(0, 1, 0);
      glVertex3f(-0.5f,  0.5f,  0.5f);
      glVertex3f( 0.5f,  0.5f,  0.5f);
      glVertex3f( 0.5f,  0.5f, -0.5f);
      glVertex3f(-0.5f,  0.5f, -0.5f);
    glEnd();
  }
}

// Main
int main(void)
{
  TwBar *bar;         // Pointer to a tweak bar

  double time = 0, dt;// Current time and enlapsed time
  double turn = 0;    // Model turn counter
  double speed = 0.3; // Model rotation speed
  int wire = 0;       // Draw model in wireframe?
  float bgColor[] = { 73.0/255, 25.0/255, 100.0/255 };         // Background color
  unsigned char cubeColor[] = { 255, 170, 0, 250 }; // Model color (32bits RGBA)

  if (!SDL_Init(SDL_INIT_VIDEO)) {
      fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
      return 1;
  }

  // Fixed-function GL (glBegin/glEnd below) + AntTweakBar's TW_OPENGL
  // (compatibility, not Core Profile) renderer - request a plain 2.1
  // compatibility context, the exact version validated by this backend's
  // compile spike (see docs/plans/sdl3-backend.md Step 1).
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

  // Requested size is in "reference" (96 DPI) pixels; grow the actual
  // window to match the monitor's real pixel density on platforms where
  // window size and framebuffer size are otherwise always 1:1 (Windows,
  // X11) - a no-op on macOS, which already does this by definition (see
  // docs/plans/examples-hidpi-scaling.md).
  SDL_Window *window = SDL_CreateWindow("AntTweakBar + SDL3 (OpenGL 2.1)", 800, 600,
                                         SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (window == NULL)
  {
      fprintf(stderr, "Cannot open SDL window: %s\n", SDL_GetError());
      SDL_Quit();
      return -1;
  }

  SDL_GLContext ctx = SDL_GL_CreateContext(window);
  if (ctx == NULL)
  {
      fprintf(stderr, "Cannot create GL context: %s\n", SDL_GetError());
      SDL_DestroyWindow(window);
      SDL_Quit();
      return -1;
  }

  if(!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
  {
      fprintf(stderr, "Failed to initialize GLAD\n");
      return -2;
  }

  // AntTweakBar draws every widget at a fixed pixel size with no DPI
  // awareness, so on a HiDPI/Retina display it looks too large/blurry
  // relative to a standard display (see docs/plans/examples-hidpi-scaling.md).
  // Scaling "fontscaling" (set via TwDefine, before TwInit) by the
  // window's content scale keeps it a comparable physical size; on a
  // standard display the content scale is 1.0, so this is a no-op there.
  g_ContentScale = SDL_GetWindowDisplayScale(window);
  if (g_ContentScale <= 0.0f) g_ContentScale = 1.0f;
  {
      char fontScalingDef[64];
      snprintf(fontScalingDef, sizeof(fontScalingDef), "GLOBAL fontscaling=%g", (double)g_ContentScale);
      TwDefine(fontScalingDef);
  }

  // Initialize AntTweakBar
  if (!TwInit(TW_OPENGL, NULL)) {
      const char* err = TwGetLastError();
      fprintf(stderr, "TwInit failed: %s\n", err ? err : "Unknown error");
      fflush(stderr);
      return -3;
  }
  TwSetCursorCallback(SDLCursorCB, NULL); // SDL cursors are process-global, no window needed
  TwSetClipboardCallback(ClipboardGetSDL, ClipboardSetSDL, NULL);
  SDL_StartTextInput(window);
  {
    int width, height;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    handleWindowPixelSizeChanged(window, width, height);
  }
  TwCopyCDStringToClientFunc(CopyCDStringToClient);

  // Create a tweak bar
  bar = TwNewBar("TweakBar");
  TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with SDL3 and OpenGL 2.1. Press [Esc] to quit (with a confirmation dialog).' "); // Message added to the help bar.
  TwDefine(" TweakBar color='100 100 50' alpha=200 ");
  {
      // Scaled by content scale so the panel keeps up with the
      // now-larger scaled contents.
      int barSize[2] = { (int)(220 * g_ContentScale + 0.5f), (int)(530 * g_ContentScale + 0.5f) };
      TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
  }
  // Add 'speed' to 'bar': it is a modifable (RW) variable of type TW_TYPE_DOUBLE. Its key shortcuts are [s] and [S].
  TwAddVarRW(bar, "speed", TW_TYPE_DOUBLE, &speed,
              " label='Rot speed' min=0 max=2 step=0.01 keyIncr=s keyDecr=S help='Rotation speed (turns/second)' ");

  // Add 'wire' to 'bar': it is a modifable variable of type TW_TYPE_BOOL32 (32 bits boolean). Its key shortcut is [w].
  TwAddVarRW(bar, "wire", TW_TYPE_BOOL32, &wire,
              " label='Wireframe mode' key=w help='Toggle wireframe display mode.' ");

  // Add 'time' to 'bar': it is a read-only (RO) variable of type TW_TYPE_DOUBLE, with 1 precision digit
  TwAddVarRO(bar, "time", TW_TYPE_DOUBLE, &time, " label='Time' precision=1 help='Time (in seconds).' ");

  // Add 'bgColor' to 'bar': it is a modifable variable of type TW_TYPE_COLOR3F (3 floats color)
  TwAddVarRW(bar, "bgColor", TW_TYPE_COLOR3F, &bgColor, " label='Background color' ");

  // Add 'cubeColor' to 'bar': it is a modifable variable of type TW_TYPE_COLOR32 (32 bits color) with alpha
  TwAddVarRW(bar, "cubeColor", TW_TYPE_COLOR32, &cubeColor,
              " label='Cube color' alpha help='Color and transparency of the cube.' ");

  // Add a button to reset the cube position
  TwAddButton(bar, "Reset Position", ResetCubePosition, NULL,
            " label='Reset Cube Position' key=r help='Reset pan and zoom.' ");

  // Add an editable text field to the tweak bar
  TwAddVarRW(bar, "Text", TW_TYPE_CDSTRING, &g_userText,
            " label='Input Text' help='Editable dynamic string.' ");

  TwAddButton(bar, "PrintText", PrintTextCallback, NULL,
              " label='Print Text' help='Prints the text to stdout' ");

  TwAddSeparator(bar, NULL, "");
  TwAddButton(bar, "FullWidthDemoMoreLines", FullWidthLinesCB, bar,
              " label='More lines' full_width=true "
              "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
  TwAddVarRW(bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
             " label='Full-width text' full_width=true lines=2 "
             "help='A full-width, wrapped multiline text field.' ");

  // Initialize time
  time = (double)SDL_GetTicksNS() / 1e9;

  bool running = true;
  // Main loop (repeated while window is not closed - [Esc] shows a
  // confirmation dialog instead of quitting immediately, see handleKeyDown())
  while (running)
  {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            running = false;
            break;
        case SDL_EVENT_KEY_DOWN:
            handleKeyDown(&event.key, window, &running);
            break;
        case SDL_EVENT_TEXT_INPUT:
            handleTextInput(event.text.text);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            handleMouseButton(&event.button);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            handleMouseMotion(window, &event.motion);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            handleMouseWheel(&event.wheel);
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            handleWindowPixelSizeChanged(window, event.window.data1, event.window.data2);
            break;
        default:
            break;
        }
    }

    // Clear frame buffer
    glClearColor(bgColor[0], bgColor[1], bgColor[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Update rotation
    dt = (double)SDL_GetTicksNS() / 1e9 - time;
    if (dt < 0) dt = 0;
    time += dt;
    turn += speed * dt;

    // Setup MODELVIEW matrix (projection is already set once)
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    GLfloat light_pos[] = { 1.0f, 1.0f, 5.0f, 1.0f }; // w=1.0 = positional light
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);

    glTranslated(g_cameraPosX, g_cameraPosY, -g_cameraPosZ);
    glRotated(360.0 * turn, 0.4, 1, 0.2);

    // Draw model
    glColor4ubv(cubeColor);
    DrawModel(wire);

    // Draw tweak bars
    TwDraw();

    // Swap buffers
    SDL_GL_SwapWindow(window);
  }

  // Terminate AntTweakBar and SDL
  TwTerminate();
  DestroySDLCursorCache();
  SDL_GL_DestroyContext(ctx);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
