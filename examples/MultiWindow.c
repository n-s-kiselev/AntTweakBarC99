//  ---------------------------------------------------------------------------
//
//  @file       MultiWindow.c
//  @brief      Demonstrates running two independent AntTweakBar-managed
//              windows in one process, each with its own tweak bar, using
//              GLFW3 (real separate windows) instead of GLUT sub-windows.
//
//              Replaces the legacy GLUT-based TwDualGLUT.c, which achieved
//              the same thing with a single top-level GLUT window holding
//              two GLUT sub-windows and per-callback TwSetCurrentWindow()
//              routing. The multi-window mechanism this example exists to
//              show is unchanged: AntTweakBar has no built-in notion of
//              "windows" beyond a caller-assigned integer ID per manager -
//              TwInit() creates one manager for whatever GL context is
//              current at the time (window ID 0, the "master" manager);
//              TwSetCurrentWindow() with a not-yet-seen ID lazily creates
//              one more manager, tied to whatever GL context is current at
//              that moment. Every subsequent Tw* call for a given window
//              must be preceded by TwSetCurrentWindow(idForThatWindow) so
//              AntTweakBar knows which manager (and which window's tweak
//              bars) the call applies to.
//
//              The rendered scene in each window is deliberately minimal
//              (a simple spinning cube, reusing SimpleGL21.c's DrawModel())
//              - the point of this example is the two-window architecture,
//              not the geometry. See Shapes.c for the richer
//              quaternion/enum/lighting demo this one intentionally does not
//              duplicate.
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              GLFW:        http://www.glfw.org
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <AntTweakBar.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

#define NUM_WINDOWS 2

typedef struct
{
    GLFWwindow *window;
    int         twWindowID;   // AntTweakBar's per-window manager ID (0 and 1)
    TwBar      *bar;
    double      speed;        // rotation speed (turns/second)
    double      turn;         // current rotation, in turns
    int         wire;         // wireframe toggle
    float       bgColor[3];
    // GLFW always reports cursor position in window points, but
    // TwWindowSize() is now fed framebuffer pixels (see windowSizeCallback
    // below), so mouse events must be scaled by this window/framebuffer
    // ratio before reaching AntTweakBar - per-window, unlike fontscaling,
    // since each window has its own point/pixel dimensions.
    double      mouseScaleX, mouseScaleY;
} DemoWindow;

static DemoWindow g_Windows[NUM_WINDOWS];

// Window content scale (see fontscaling comment in SetupWindow() below),
// queried once for window 0 and reused for window 1's bar size too: fonts
// are a process-wide resource (one shared g_FontScaling/set of default
// fonts for every CTwMgr), so there is only ever one fontscaling value to
// match, regardless of how many windows/managers exist.
static float g_ContentScaleX = 1.0f, g_ContentScaleY = 1.0f;

// AntTweakBar's cursor callback (TwSetCursorCallback, installed once below)
// is a single, process-wide hook - it is not aware of which of our two
// windows the pointer is currently over. g_ActiveWindow tracks that
// ourselves (updated by every per-window mouse/key callback below) so
// GLFWCursorCB knows which GLFWwindow to call glfwSetCursor() on. A real
// multi-window application built on top of this library would need the
// same tracking; AntTweakBar has no per-manager cursor-callback hook.
static GLFWwindow *g_ActiveWindow = NULL;

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
    GLFWwindow *window = g_ActiveWindow;
    (void)_ClientData;
    if (window == NULL) return;

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

static DemoWindow *DemoWindowFor(GLFWwindow *window)
{
    return (DemoWindow *)glfwGetWindowUserPointer(window);
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

// Registered as the FRAMEBUFFER size callback (not the window size
// callback): GLFW reports this in actual pixels, matching
// glViewport/TwWindowSize.
static void windowSizeCallback(GLFWwindow *window, int width, int height)
{
    DemoWindow *dw = DemoWindowFor(window);
    if (dw == NULL) return;
    if (height == 0) height = 1;

    glfwMakeContextCurrent(window); // glViewport()/projection below are per-context state
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

    int winWidth = width, winHeight = height;
    glfwGetWindowSize(window, &winWidth, &winHeight);
    dw->mouseScaleX = (winWidth > 0) ? (double)width / winWidth : 1.0;
    dw->mouseScaleY = (winHeight > 0) ? (double)height / winHeight : 1.0;
}

// Shared key/mouse callback bodies: each just resolves which DemoWindow the
// event belongs to, selects that window's AntTweakBar manager, and forwards
// the event - the same TwSetCurrentWindow(id)-before-TwEventXxx pattern
// TwDualGLUT.c used per GLUT callback (there, id came from glutGetWindow()).

static void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    DemoWindow *dw = DemoWindowFor(window);
    if (dw == NULL) return;
    g_ActiveWindow = window;
    TwSetCurrentWindow(dw->twWindowID);

    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    int twMod = 0;
    if (mods & GLFW_MOD_SHIFT) twMod |= TW_KMOD_SHIFT;
    bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;
    if (ctrl) twMod |= TW_KMOD_CTRL;
    if (mods & GLFW_MOD_ALT) twMod |= TW_KMOD_ALT;

    int twKey = 0;
    switch (key) {
    case GLFW_KEY_BACKSPACE: twKey = TW_KEY_BACKSPACE; break;
    case GLFW_KEY_TAB: twKey = TW_KEY_TAB; break;
    case GLFW_KEY_ENTER: twKey = TW_KEY_RETURN; break;
    case GLFW_KEY_ESCAPE: twKey = TW_KEY_ESCAPE; break;
    case GLFW_KEY_SPACE: twKey = TW_KEY_SPACE; break;
    case GLFW_KEY_DELETE: twKey = TW_KEY_DELETE; break;
    case GLFW_KEY_UP: twKey = TW_KEY_UP; break;
    case GLFW_KEY_DOWN: twKey = TW_KEY_DOWN; break;
    case GLFW_KEY_RIGHT: twKey = TW_KEY_RIGHT; break;
    case GLFW_KEY_LEFT: twKey = TW_KEY_LEFT; break;
    case GLFW_KEY_HOME: twKey = TW_KEY_HOME; break;
    case GLFW_KEY_END: twKey = TW_KEY_END; break;
    case GLFW_KEY_PAGE_UP: twKey = TW_KEY_PAGE_UP; break;
    case GLFW_KEY_PAGE_DOWN: twKey = TW_KEY_PAGE_DOWN; break;
    }
    if (twKey == 0 && ctrl && key < 128) twKey = key;
    if (twKey != 0) TwKeyPressed(twKey, twMod);
}

static void charCallback(GLFWwindow *window, unsigned int codepoint)
{
    DemoWindow *dw = DemoWindowFor(window);
    if (dw == NULL) return;
    g_ActiveWindow = window;
    TwSetCurrentWindow(dw->twWindowID);
    TwKeyPressed(codepoint, 0);
}

static void mousebuttonCallback(GLFWwindow *window, int button, int action, int mods)
{
    (void)mods;
    DemoWindow *dw = DemoWindowFor(window);
    if (dw == NULL) return;
    g_ActiveWindow = window;
    TwSetCurrentWindow(dw->twWindowID);
    TwEventMouseButtonGLFW(button, action);
}

static void mousePosCallback(GLFWwindow *window, double xpos, double ypos)
{
    DemoWindow *dw = DemoWindowFor(window);
    if (dw == NULL) return;
    g_ActiveWindow = window;
    TwSetCurrentWindow(dw->twWindowID);
    TwEventMousePosGLFW((int)(xpos * dw->mouseScaleX), (int)(ypos * dw->mouseScaleY));
}

static void mouseScrollCallback(GLFWwindow *window, double xoffset, double yoffset)
{
    static double pos[NUM_WINDOWS] = { 0 };
    (void)xoffset;
    DemoWindow *dw = DemoWindowFor(window);
    if (dw == NULL) return;
    g_ActiveWindow = window;
    TwSetCurrentWindow(dw->twWindowID);
    pos[dw->twWindowID] += yoffset;
    TwEventMouseWheelGLFW((int)pos[dw->twWindowID]);
}

static void error_callback(int error, const char *description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
    fflush(stderr);
}

// Creates one GLFW3 window, assigns it an AntTweakBar window ID (creating
// that manager immediately - see the file header comment above), and adds
// its tweak bar. windowIndex 0 must be called after TwInit() (its manager
// is the master one, ID 0, implicitly tied to whatever context is current
// when TwInit() runs); windowIndex 1 (and beyond, if this were extended)
// calls TwSetCurrentWindow() with a fresh ID to lazily create its manager.
static bool SetupWindow(int windowIndex, GLFWwindow *shareWith, const char *title, float r, float g, float b)
{
    DemoWindow *dw = &g_Windows[windowIndex];

    dw->window = glfwCreateWindow(500, 500, title, NULL, shareWith);
    if (dw->window == NULL) {
        fprintf(stderr, "Cannot open GLFW window '%s'\n", title);
        return false;
    }
    glfwSetWindowUserPointer(dw->window, dw);
    dw->twWindowID = windowIndex; // arbitrary but must be unique and stable
    dw->speed = 0.2 + 0.15 * windowIndex;
    dw->turn = 0.0;
    dw->wire = 0;
    dw->bgColor[0] = r; dw->bgColor[1] = g; dw->bgColor[2] = b;

    glfwMakeContextCurrent(dw->window);

    if (windowIndex == 0) {
        // First window: load GLAD once (function pointers are valid across
        // every context sharing this one's object namespace - see
        // shareWith below) and initialize AntTweakBar's master manager.
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            fprintf(stderr, "Failed to initialize GLAD\n");
            return false;
        }
        // AntTweakBar draws every widget at a fixed pixel size with no DPI
        // awareness, so on a HiDPI/Retina display it looks too large/blurry
        // relative to a standard display (see docs/plans/examples-hidpi-scaling.md).
        // Scaling "fontscaling" (set via TwDefine, before TwInit) by the
        // window's content scale keeps it a comparable physical size; on a
        // standard display the content scale is 1.0, so this is a no-op
        // there. Done once here (not per window): fonts are a process-wide
        // resource shared by every window's manager.
        glfwGetWindowContentScale(dw->window, &g_ContentScaleX, &g_ContentScaleY);
        {
            char fontScalingDef[64];
            snprintf(fontScalingDef, sizeof(fontScalingDef), "GLOBAL fontscaling=%g", (double)g_ContentScaleX);
            TwDefine(fontScalingDef);
        }
        if (!TwInit(TW_OPENGL, NULL)) {
            fprintf(stderr, "TwInit failed: %s\n", TwGetLastError());
            return false;
        }
        TwSetCursorCallback(GLFWCursorCB, NULL);
    } else {
        // Later windows: their context shares object namespace with window
        // 0 (required so a single TwTerminate() call, made with only one
        // context current, can validly delete every window's GL objects -
        // see the file header comment). TwSetCurrentWindow() with a fresh
        // ID lazily creates this window's own CTwMgr/renderer, tied to the
        // context made current just above.
        if (!TwSetCurrentWindow(dw->twWindowID)) {
            fprintf(stderr, "TwSetCurrentWindow(%d) failed to create a manager\n", dw->twWindowID);
            return false;
        }
    }

    {
        int width, height;
        glfwGetFramebufferSize(dw->window, &width, &height);
        windowSizeCallback(dw->window, width, height);
    }

    dw->bar = TwNewBar("TweakBar");
    TwDefine(" GLOBAL help='Two independent AntTweakBar-managed GLFW3 windows in one process.' ");
    {
        // Scaled by content scale so the panel keeps up with the
        // now-larger scaled contents.
        int barSize[2] = { (int)(200 * g_ContentScaleX + 0.5f), (int)(150 * g_ContentScaleY + 0.5f) };
        TwSetParam(dw->bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }
    TwAddVarRW(dw->bar, "speed", TW_TYPE_DOUBLE, &dw->speed,
               " label='Rot speed' min=0 max=2 step=0.01 help='Rotation speed (turns/second)' ");
    TwAddVarRW(dw->bar, "wire", TW_TYPE_BOOL32, &dw->wire,
               " label='Wireframe' help='Toggle wireframe display mode.' ");
    TwAddVarRW(dw->bar, "bgColor", TW_TYPE_COLOR3F, &dw->bgColor,
               " label='Background color' ");

    glfwSetKeyCallback(dw->window, keyCallback);
    glfwSetCharCallback(dw->window, charCallback);
    glfwSetMouseButtonCallback(dw->window, mousebuttonCallback);
    glfwSetCursorPosCallback(dw->window, mousePosCallback);
    glfwSetScrollCallback(dw->window, mouseScrollCallback);
    glfwSetFramebufferSizeCallback(dw->window, windowSizeCallback);

    return true;
}

int main(void)
{
    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) {
        fprintf(stderr, "GLFW initialization failed\n");
        return 1;
    }

    if (!SetupWindow(0, NULL, "MultiWindow - Window A", 0.15f, 0.15f, 0.35f))
        return 1;
    // Window B shares window A's context object namespace (see SetupWindow()
    // comment) - this is what makes a single, single-current-context
    // TwTerminate() call able to clean up both windows' GL resources.
    if (!SetupWindow(1, g_Windows[0].window, "MultiWindow - Window B", 0.35f, 0.15f, 0.15f))
        return 1;

    g_ActiveWindow = g_Windows[0].window;
    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(g_Windows[0].window) && !glfwWindowShouldClose(g_Windows[1].window))
    {
        glfwPollEvents();

        double now = glfwGetTime();
        double dt = now - lastTime;
        if (dt < 0) dt = 0;
        lastTime = now;

        for (int i = 0; i < NUM_WINDOWS; ++i) {
            DemoWindow *dw = &g_Windows[i];
            dw->turn += dw->speed * dt;

            glfwMakeContextCurrent(dw->window);
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
            glfwSwapBuffers(dw->window);
        }
    }

    // A shared context must be current for TwTerminate()'s internal loop
    // over every window's manager to validly delete their (shared) GL
    // objects - see the file header comment.
    glfwMakeContextCurrent(g_Windows[0].window);
    TwTerminate();
    DestroyGLFWCursorCache();

    glfwDestroyWindow(g_Windows[1].window);
    glfwDestroyWindow(g_Windows[0].window);
    glfwTerminate();

    return 0;
}
