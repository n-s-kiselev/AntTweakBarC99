//  ---------------------------------------------------------------------------
//
//  @file       Strip.c
//  @brief      A simple example that uses AntTweakBar with GLFW3 and OpenGL.
//              Draws an animated color-gradient triangle strip.
//              Ported from the legacy GLFW2 example TwStripGLFW.c (itself
//              ported from TwSimpleDX9.cpp, originally Direct3D9-based).
//              Also demonstrates TwSetParam() as an alternative to TwDefine()
//              for setting a bar attribute (here, the tweak bar's size).
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              GLFW:        http://www.glfw.org
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <AntTweakBar.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

// GLFW3 cursor binding (see docs/glfw3-cursor-integration.md): AntTweakBar
// predates cursor-ownership models like GLFW3's and sets the system cursor
// directly, which GLFW3 toolkits that reassert their own cursor on every
// mouse move (e.g. macOS's Cocoa backend) silently overwrite. Installing
// this as AntTweakBar's cursor callback (TwSetCursorCallback, below) routes
// every cursor change through glfwSetCursor() instead, so GLFW3 owns it.
static GLFWcursor* g_StandardCursors[TW_CURSOR_CUSTOM] = { NULL };
static GLFWcursor* g_LastCustomCursor = NULL;
static int g_CursorHidden = 0;

static int GLFWStandardCursorShape(ETwCursor _Cursor)
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

static void TW_CALL GLFWCursorCB(ETwCursor _Cursor, const unsigned char *_RGBA32x32, int _HotX, int _HotY, void *_ClientData)
{
    GLFWwindow *window = (GLFWwindow *)_ClientData;
    // TW_CURSOR_HIDDEN is an input mode, not a cursor shape: the roto slider
    // hides the pointer while it is dragged. g_CursorHidden remembers that so
    // the mode is restored once, on the next request for a visible cursor.
    if (_Cursor == TW_CURSOR_HIDDEN) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        g_CursorHidden = 1;
        return;
    }
    if (g_CursorHidden) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        g_CursorHidden = 0;
    }
    if (_Cursor == TW_CURSOR_CUSTOM && _RGBA32x32 != NULL) {
        GLFWimage img;
        img.width = 32; img.height = 32;
        img.pixels = (unsigned char *)_RGBA32x32; // glfwCreateCursor only reads it
        GLFWcursor *cur = glfwCreateCursor(&img, _HotX, _HotY);
        if (cur != NULL) {
            // Set the new cursor before destroying the old one: destroying
            // a cursor still current for a window resets that window to
            // the default arrow, which would undo this if done first.
            glfwSetCursor(window, cur);
            if (g_LastCustomCursor != NULL)
                glfwDestroyCursor(g_LastCustomCursor);
            g_LastCustomCursor = cur;
        }
        return;
    }
    if (g_StandardCursors[_Cursor] == NULL)
        g_StandardCursors[_Cursor] = glfwCreateStandardCursor(GLFWStandardCursorShape(_Cursor));
    if (g_StandardCursors[_Cursor] != NULL)
        glfwSetCursor(window, g_StandardCursors[_Cursor]);
}

static void DestroyGLFWCursorCache(void)
{
    for (int i = 0; i < TW_CURSOR_CUSTOM; ++i) {
        if (g_StandardCursors[i] != NULL) {
            glfwDestroyCursor(g_StandardCursors[i]);
            g_StandardCursors[i] = NULL;
        }
    }
    if (g_LastCustomCursor != NULL) {
        glfwDestroyCursor(g_LastCustomCursor);
        g_LastCustomCursor = NULL;
    }
}

static int g_Width = 640, g_Height = 480;

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
  if (action == GLFW_PRESS || action == GLFW_REPEAT)
  {
    if (key == GLFW_KEY_ESCAPE)
    {
      glfwSetWindowShouldClose(window, GLFW_TRUE);
      return;
    }

    int twMod = 0;
    bool ctrl;
    if (mods & GLFW_MOD_SHIFT) twMod |= TW_KMOD_SHIFT;
    if ((ctrl = (mods & GLFW_MOD_CONTROL))) twMod |= TW_KMOD_CTRL;
    if (mods & GLFW_MOD_ALT) twMod |= TW_KMOD_ALT;

    int twKey = 0;
    switch (key)
    {
    case GLFW_KEY_BACKSPACE: twKey = TW_KEY_BACKSPACE; break;
    case GLFW_KEY_TAB: twKey = TW_KEY_TAB; break;
    case GLFW_KEY_ENTER: twKey = TW_KEY_RETURN; break;
    case GLFW_KEY_PAUSE: twKey = TW_KEY_PAUSE; break;
    case GLFW_KEY_SPACE: twKey = TW_KEY_SPACE; break;
    case GLFW_KEY_DELETE: twKey = TW_KEY_DELETE; break;
    case GLFW_KEY_UP: twKey = TW_KEY_UP; break;
    case GLFW_KEY_DOWN: twKey = TW_KEY_DOWN; break;
    case GLFW_KEY_RIGHT: twKey = TW_KEY_RIGHT; break;
    case GLFW_KEY_LEFT: twKey = TW_KEY_LEFT; break;
    case GLFW_KEY_INSERT: twKey = TW_KEY_INSERT; break;
    case GLFW_KEY_HOME: twKey = TW_KEY_HOME; break;
    case GLFW_KEY_END: twKey = TW_KEY_END; break;
    case GLFW_KEY_PAGE_UP: twKey = TW_KEY_PAGE_UP; break;
    case GLFW_KEY_PAGE_DOWN: twKey = TW_KEY_PAGE_DOWN; break;
    case GLFW_KEY_F1: twKey = TW_KEY_F1; break;
    case GLFW_KEY_F2: twKey = TW_KEY_F2; break;
    case GLFW_KEY_F3: twKey = TW_KEY_F3; break;
    case GLFW_KEY_F4: twKey = TW_KEY_F4; break;
    case GLFW_KEY_F5: twKey = TW_KEY_F5; break;
    case GLFW_KEY_F6: twKey = TW_KEY_F6; break;
    case GLFW_KEY_F7: twKey = TW_KEY_F7; break;
    case GLFW_KEY_F8: twKey = TW_KEY_F8; break;
    case GLFW_KEY_F9: twKey = TW_KEY_F9; break;
    case GLFW_KEY_F10: twKey = TW_KEY_F10; break;
    case GLFW_KEY_F11: twKey = TW_KEY_F11; break;
    case GLFW_KEY_F12: twKey = TW_KEY_F12; break;
    case GLFW_KEY_F13: twKey = TW_KEY_F13; break;
    case GLFW_KEY_F14: twKey = TW_KEY_F14; break;
    case GLFW_KEY_F15: twKey = TW_KEY_F15; break;
    }
    if (twKey == 0 && ctrl && key < 128)
    {
      twKey = key;
    }
    if (twKey != 0)
    {
      if (TwKeyPressed(twKey, twMod)) return;
    }
  }
}

static void charCallback(GLFWwindow* window, unsigned int key)
{
  if (TwKeyPressed(key, 0)) return;
}

static void mousebuttonCallback(GLFWwindow* _window, int _button, int _action, int _mods)
{
    if (TwEventMouseButtonGLFW(_button, _action)) return;
}

static void mousePosCallback(GLFWwindow* _window, double _xpos, double _ypos)
{
    if (TwEventMousePosGLFW((int)_xpos, (int)_ypos)) return;
}

static void mouseScrollCallback(GLFWwindow* _window, double _xoffset, double _yoffset)
{
    static double pos = 0;
    pos += _yoffset;
    if (TwEventMouseWheelGLFW((int)pos)) return;
}

static void windowSizeCallback(GLFWwindow* window, int width, int height)
{
    if (height == 0) height = 1;
    g_Width = width;
    g_Height = height;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if (width >= height) {
        double aspect = (double)width / height;
        glOrtho(-aspect, aspect, -1.0, 1.0, -1.0, 1.0);
    } else {
        double aspect = (double)height / width;
        glOrtho(-1.0, 1.0, -aspect, aspect, -1.0, 1.0);
    }
    glMatrixMode(GL_MODELVIEW);
    TwWindowSize(width, height);
}

void error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
    fflush(stderr);
}

int main(void)
{
    GLFWwindow* window; // GLFW3 window

    int numSec = 100;             // number of strip sections
    float color[] = { 1, 0, 0 };  // strip color
    unsigned char bgColor[] = { 128, 196, 196, 255 }; // background color (32bits RGBA: R,G,B,A)

    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        fprintf(stderr, "GLFW initialization failed\n");
        return 1;
    }

    // Disable Retina scaling for now
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
    window = glfwCreateWindow(g_Width, g_Height, "AntTweakBar + GLFW3 (Triangle Strip)", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Cannot open GLFW window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        return 1;
    }

    if (!TwInit(TW_OPENGL, NULL)) {
        fprintf(stderr, "AntTweakBar initialization failed: %s\n", TwGetLastError());
        return 1;
    }
    // Give GLFW3 authoritative cursor ownership (see GLFWCursorCB above).
    TwSetCursorCallback(GLFWCursorCB, window);

    {
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        windowSizeCallback(window, width, height);
    }

    TwBar *bar = TwNewBar("TweakBar");
    {
        // Demonstrates TwSetParam() as an alternative to TwDefine() for
        // setting a single bar attribute (here, "size").
        int barSize[2] = { 200, 320 };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with GLFW3 and OpenGL.' ");
    TwDefine(" TweakBar color='128 224 160' text=dark ");

    TwAddVarRW(bar, "NumSec", TW_TYPE_INT32, &numSec,
               " label='Strip length' min=1 max=1000 keyIncr=s keyDecr=S help='Number of segments of the strip.' ");
    TwAddVarRW(bar, "Color", TW_TYPE_COLOR3F, &color, " label='Strip color' ");
    TwAddVarRW(bar, "BgColor", TW_TYPE_COLOR32, &bgColor, " label='Background color' ");
    TwAddVarRO(bar, "Width", TW_TYPE_INT32, &g_Width, " label='wnd width' help='Current graphics window width.' ");
    TwAddVarRO(bar, "Height", TW_TYPE_INT32, &g_Height, " label='wnd height' help='Current graphics window height.' ");

    glfwSetKeyCallback(window, keyCallback);
    glfwSetCharCallback(window, charCallback);
    glfwSetMouseButtonCallback(window, mousebuttonCallback);
    glfwSetCursorPosCallback(window, mousePosCallback);
    glfwSetScrollCallback(window, mouseScrollCallback);
    glfwSetWindowSizeCallback(window, windowSizeCallback);

    while (!glfwWindowShouldClose(window)) {
        glClearColor(bgColor[2] / 255.0f, bgColor[1] / 255.0f, bgColor[0] / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float t = (float)glfwGetTime();
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
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    TwTerminate();
    DestroyGLFWCursorCache();
    glfwTerminate();
    return 0;
}
