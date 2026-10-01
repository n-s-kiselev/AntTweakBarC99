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
//              bars) the call applies to. For input events that is what
//              atb_glfw_AttachWindow() takes care of: it binds a GLFWwindow
//              to a window ID, and the shared callbacks in atb_glfw.h select
//              that manager before forwarding anything.
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
#include "atb_glfw.h"   // shared GLFW3 <-> AntTweakBar glue for these examples
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
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
    char        fullWidthText[300]; // full_width=true multiline demo text - see SetupWindow()
} DemoWindow;

static DemoWindow g_Windows[NUM_WINDOWS];

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

// Runs on every framebuffer resize, before TwWindowSize(), with that window's
// AntTweakBar manager already current. Size is in pixels.
static void resizeHook(GLFWwindow *window, int width, int height)
{
    glfwMakeContextCurrent(window); // glViewport()/projection below are per-context state
    float aspect = (float)width / (float)height;
    float znear = 1.0f, zfar = 100.0f, fov = 45.0f;
    float top = tanf(fov * 0.01745329251f) * znear;
    float bottom = -top, right = top * aspect, left = -right;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(left, right, bottom, top, znear, zfar);
}

static void error_callback(int error, const char *description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
    fflush(stderr);
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

// Creates one GLFW3 window, assigns it an AntTweakBar window ID (creating
// that manager immediately - see the file header comment above), and adds
// its tweak bar. windowIndex 0 must be called after TwInit() (its manager
// is the master one, ID 0, implicitly tied to whatever context is current
// when TwInit() runs); windowIndex 1 (and beyond, if this were extended)
// calls TwSetCurrentWindow() with a fresh ID to lazily create its manager.
static bool SetupWindow(int windowIndex, GLFWwindow *shareWith, const char *title, float r, float g, float b)
{
    DemoWindow *dw = &g_Windows[windowIndex];

    // Requested size is in "reference" (96 DPI) pixels; grow the actual
    // window to match the monitor's real pixel density on platforms where
    // window size and framebuffer size are otherwise always 1:1 (Windows,
    // X11) - a no-op on macOS, which already does this by definition (see
    // docs/plans/examples-hidpi-scaling.md).
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    dw->window = glfwCreateWindow(500, 500, title, NULL, shareWith);
    if (dw->window == NULL) {
        fprintf(stderr, "Cannot open GLFW window '%s'\n", title);
        return false;
    }
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
        // AntTweakBar has no DPI awareness, so scale its font by the window
        // content scale to keep a comparable physical size. Must precede
        // TwInit(), which bakes the scale into the font atlases. Done once
        // here (not per window): fonts are a process-wide resource shared by
        // every window's manager.
        atb_glfw_SetFontScaling(dw->window);
        if (!TwInit(TW_OPENGL, NULL)) {
            fprintf(stderr, "TwInit failed: %s\n", TwGetLastError());
            return false;
        }
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

    // Registers the GLFW callbacks, binds this window to its own AntTweakBar
    // manager (so every event selects it with TwSetCurrentWindow() before being
    // forwarded), and applies the current framebuffer size - see atb_glfw.h.
    {
        atb_glfw_Hooks hooks = { 0 };
        hooks.resize = resizeHook;
        atb_glfw_AttachWindow(dw->window, dw->twWindowID, &hooks);
    }

    dw->bar = TwNewBar("TweakBar");
    TwDefine(" GLOBAL help='Two independent AntTweakBar-managed GLFW3 windows in one process.' ");
    {
        // Scaled by content scale so the panel keeps up with the
        // now-larger scaled contents.
        int barSize[2] = { (int)(200 * atb_glfw_ContentScaleX() + 0.5f),
                           (int)(150 * atb_glfw_ContentScaleY() + 0.5f) };
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
    atb_glfw_Detach(g_Windows[1].window);   // releases the cursors, after TwTerminate()
    atb_glfw_Detach(g_Windows[0].window);

    glfwDestroyWindow(g_Windows[1].window);
    glfwDestroyWindow(g_Windows[0].window);
    glfwTerminate();

    return 0;
}
