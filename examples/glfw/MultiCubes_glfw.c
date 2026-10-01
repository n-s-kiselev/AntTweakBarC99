//  ---------------------------------------------------------------------------
//
//  @file       MultiCubes.c
//  @brief      An example that uses AntTweakBar with GLFW3 and OpenGL to draw
//              many cubes moving along independently-tunable paths, with
//              colors interpolated between two tweakable endpoints.
//
//              Ported from the legacy GLFW2 example TwMultiCubesGLFW.c
//              (itself ported from TwSimpleSDL.c, originally SDL 1.2-based)
//              to GLFW3, replacing its manual HiDPI mouse/window-size
//              scaling with the GLFW_COCOA_RETINA_FRAMEBUFFER window hint
//              used by this fork's other GLFW3 examples.
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
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

static int g_Width = 640, g_Height = 480;

static void setProjection(int width, int height)
{
    float near = 1.0f, far = 10.0f;
    float fovy = 40.0f * 0.01745329251f; // 40 degrees, in radians
    float aspect = (float)width / (float)height;
    float top = tanf(fovy * 0.5f) * near;
    float right = top * aspect;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right, right, -top, top, near, far);
    glMatrixMode(GL_MODELVIEW);
}

// Runs on every framebuffer resize, before TwWindowSize(). Size is in pixels.
static void resizeHook(GLFWwindow *window, int width, int height)
{
  (void)window;
  g_Width = width;
  g_Height = height;
  setProjection(width, height);
}

void error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
    fflush(stderr);
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
    GLFWwindow *window;
    TwBar *bar;
    int n, numCubes = 30;
    float color0[] = { 1.0f, 0.5f, 0.0f };
    float color1[] = { 0.5f, 1.0f, 0.0f };
    double ka = 5.3, kb = 1.7, kc = 4.1;
    int quit = 0;

    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        fprintf(stderr, "GLFW initialization failed\n");
        return 1;
    }

    // Requested size is in "reference" (96 DPI) pixels; grow the actual
    // window to match the monitor's real pixel density on platforms where
    // window size and framebuffer size are otherwise always 1:1 (Windows,
    // X11) - a no-op on macOS, which already does this by definition (see
    // docs/plans/examples-hidpi-scaling.md).
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    window = glfwCreateWindow(g_Width, g_Height, "AntTweakBar + GLFW3 (Multi Cubes)", NULL, NULL);
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

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glEnable(GL_COLOR_MATERIAL);
    glDisable(GL_CULL_FACE);
    glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);

    // AntTweakBar draws every widget at a fixed pixel size with no DPI
    // awareness, so on a HiDPI/Retina display it looks too large/blurry
    // relative to a standard display (see docs/plans/examples-hidpi-scaling.md).
    // Scaling the font by the window's content scale keeps it a comparable
    // physical size; on a standard display that scale is 1.0, so this is a
    // no-op there. Must precede TwInit(), which bakes the scale into the fonts.
    atb_glfw_SetFontScaling(window);

    if (!TwInit(TW_OPENGL, NULL)) {
        fprintf(stderr, "AntTweakBar initialization failed: %s\n", TwGetLastError());
        return 1;
    }

    // Registers the GLFW callbacks, gives GLFW3 authoritative cursor
    // ownership, routes the clipboard through it, and applies the current
    // framebuffer size - see atb_glfw.h.
    {
        atb_glfw_Hooks hooks = { 0 };
        hooks.resize = resizeHook;
        atb_glfw_Attach(window, &hooks);
    }

    bar = TwNewBar("TweakBar");
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with GLFW3 and OpenGL.' ");
    {
        // Scaled by content scale so the panel keeps up with the
        // now-larger scaled contents.
        int barSize[2] = { (int)(200 * atb_glfw_ContentScaleX() + 0.5f),
                           (int)(320 * atb_glfw_ContentScaleY() + 0.5f) };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }

    TwAddVarRO(bar, "Width", TW_TYPE_INT32, &g_Width,
               " label='Wnd width' help='Width of the graphics window (in pixels)' ");
    TwAddVarRO(bar, "Height", TW_TYPE_INT32, &g_Height,
               " label='Wnd height' help='Height of the graphics window (in pixels)' ");
    TwAddVarRW(bar, "NumCubes", TW_TYPE_INT32, &numCubes,
               " label='Number of cubes' min=1 max=100 keyIncr=c keyDecr=C help='Defines the number of cubes in the scene.' ");
    TwAddVarRW(bar, "ka", TW_TYPE_DOUBLE, &ka,
               " label='X path coeff' keyIncr=1 keyDecr=CTRL+1 min=-10 max=10 step=0.01 ");
    TwAddVarRW(bar, "kb", TW_TYPE_DOUBLE, &kb,
               " label='Y path coeff' keyIncr=2 keyDecr=CTRL+2 min=-10 max=10 step=0.01 ");
    TwAddVarRW(bar, "kc", TW_TYPE_DOUBLE, &kc,
               " label='Z path coeff' keyIncr=3 keyDecr=CTRL+3 min=-10 max=10 step=0.01 ");
    TwAddVarRW(bar, "color0", TW_TYPE_COLOR3F, &color0,
               " label='Start color' help='Color of the first cube.' ");
    TwAddVarRW(bar, "color1", TW_TYPE_COLOR3F, &color1,
               " label='End color' help='Color of the last cube. Cube colors are interpolated between the Start and End colors.' ");
    TwAddVarRW(bar, "Quit", TW_TYPE_BOOL32, &quit,
               " label='Quit?' true='+' false='-' key='ESC' help='Quit program.' ");

    TwAddSeparator(bar, NULL, "");
    TwAddButton(bar, "FullWidthDemoMoreLines", FullWidthLinesCB, bar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");

    while (!quit && !glfwWindowShouldClose(window)) {
        glClearColor(0.5f, 0.75f, 0.8f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslated(0, 0, -3); // camera at (0,0,3) looking at the origin (gluLookAt equivalent)

        for (n = 0; n < numCubes; ++n) {
            double t = 0.05 * n - glfwGetTime() / 2.0;
            double r = 5.0 * n + glfwGetTime() * 100.0;
            float c = (float)n / numCubes;

            glPushMatrix();
            glTranslated(0.6 * cos(ka * t), 0.6 * cos(kb * t), 0.6 * sin(kc * t));
            glRotated(r, 0.2, 0.7, 0.2);
            glScaled(0.1, 0.1, 0.1);
            glTranslated(-0.5, -0.5, -0.5);

            glColor3f((1.0f - c) * color0[0] + c * color1[0],
                      (1.0f - c) * color0[1] + c * color1[1],
                      (1.0f - c) * color0[2] + c * color1[2]);

            glBegin(GL_QUADS);
                glNormal3f(0,0,-1); glVertex3f(0,0,0); glVertex3f(0,1,0); glVertex3f(1,1,0); glVertex3f(1,0,0);
                glNormal3f(0,0,+1); glVertex3f(0,0,1); glVertex3f(1,0,1); glVertex3f(1,1,1); glVertex3f(0,1,1);
                glNormal3f(-1,0,0); glVertex3f(0,0,0); glVertex3f(0,0,1); glVertex3f(0,1,1); glVertex3f(0,1,0);
                glNormal3f(+1,0,0); glVertex3f(1,0,0); glVertex3f(1,1,0); glVertex3f(1,1,1); glVertex3f(1,0,1);
                glNormal3f(0,-1,0); glVertex3f(0,0,0); glVertex3f(1,0,0); glVertex3f(1,0,1); glVertex3f(0,0,1);
                glNormal3f(0,+1,0); glVertex3f(0,1,0); glVertex3f(0,1,1); glVertex3f(1,1,1); glVertex3f(1,1,0);
            glEnd();

            glPopMatrix();
        }

        TwDraw();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    TwTerminate();
    atb_glfw_Detach(window);   // releases the cursors, after TwTerminate()
    glfwTerminate();
    return 0;
}
