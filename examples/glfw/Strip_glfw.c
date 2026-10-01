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
#include "atb_glfw.h"   // shared GLFW3 <-> AntTweakBar glue for these examples
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>


static int g_Width = 640, g_Height = 480;


// Escape quits this demo. The hook runs only for keys AntTweakBar did not
// consume, so an open popup or an active edit field still gets Escape first.
static void keyHook(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void)scancode; (void)mods;
    if ((action == GLFW_PRESS || action == GLFW_REPEAT) && key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

// Runs on every framebuffer resize, before TwWindowSize(). Size is in pixels.
static void resizeHook(GLFWwindow *window, int width, int height)
{
    (void)window;
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
    GLFWwindow* window; // GLFW3 window

    int numSec = 100;             // number of strip sections
    float color[] = { 1, 0, 0 };  // strip color
    unsigned char bgColor[] = { 128, 196, 196, 255 }; // background color (32bits RGBA: R,G,B,A)

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

    // AntTweakBar has no DPI awareness, so scale its font by the window content
    // scale to keep a comparable physical size. Must precede TwInit(), which
    // bakes the scale into the font atlases.
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
        hooks.key = keyHook;
        hooks.resize = resizeHook;
        atb_glfw_Attach(window, &hooks);
    }

    TwBar *bar = TwNewBar("TweakBar");
    {
        // Demonstrates TwSetParam() as an alternative to TwDefine() for
        // setting a single bar attribute (here, "size"). Scaled by content
        // scale so the panel keeps up with the now-larger scaled contents.
        int barSize[2] = { (int)(200 * atb_glfw_ContentScaleX() + 0.5f), (int)(320 * atb_glfw_ContentScaleY() + 0.5f) };
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

    TwAddSeparator(bar, NULL, "");
    TwAddButton(bar, "FullWidthDemoMoreLines", FullWidthLinesCB, bar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");


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
    atb_glfw_Detach(window);   // releases the cursors, after TwTerminate()
    glfwTerminate();
    return 0;
}
