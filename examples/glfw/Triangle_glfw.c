//  ---------------------------------------------------------------------------
//
//  @file       Triangle.c
//  @brief      A simple example that uses AntTweakBar with GLFW3 and OpenGL.
//              Draws a triangle and allows the user to tweak its vertex
//              positions and colors, using a custom TwDefineStruct'd 2D point.
//              Ported from the legacy GLFW2 example TwTriangleGLFW.c (itself
//              ported from TwSimpleDX10.cpp, originally Direct3D10-based).
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
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

#define NB_VERTS 3

typedef struct { float X, Y; } Point;

static int g_Angle = 0;
static float g_Scale = 1;
static Point g_Positions[NB_VERTS] = { {0.0f, 0.5f}, {0.5f, -0.5f}, {-0.5f, -0.5f} };
static float g_Colors[NB_VERTS][4] = { {0, 1, 1, 1}, {1, 0, 1, 1}, {1, 1, 0, 1} };
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
    window = glfwCreateWindow(g_Width, g_Height, "AntTweakBar + GLFW3 (Triangle)", NULL, NULL);
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
        hooks.key = keyHook;
        hooks.resize = resizeHook;
        atb_glfw_Attach(window, &hooks);
    }

    TwBar *bar = TwNewBar("TweakBar");
    {
        // Demonstrates TwSetParam() as an alternative to TwDefine() for
        // setting a single bar attribute (here, "size"). Scaled by content
        // scale so the panel keeps up with the now-larger scaled contents.
        int barSize[2] = { (int)(200 * atb_glfw_ContentScaleX() + 0.5f),
                           (int)(320 * atb_glfw_ContentScaleY() + 0.5f) };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with GLFW3 and OpenGL.' ");

    TwAddVarRW(bar, "Rotation", TW_TYPE_INT32, &g_Angle,
               " KeyIncr=r KeyDecr=R Help='Rotates the triangle (angle in degree).' ");
    TwAddVarRW(bar, "Scale", TW_TYPE_FLOAT, &g_Scale,
               " Min=-2 Max=2 Step=0.01 KeyIncr=s KeyDecr=S Help='Scales the triangle (1=original size).' ");

    TwStructMember pointMembers[] = {
        { "X", TW_TYPE_FLOAT, offsetof(Point, X), " Min=-1 Max=1 Step=0.01 " },
        { "Y", TW_TYPE_FLOAT, offsetof(Point, Y), " Min=-1 Max=1 Step=0.01 " }
    };
    TwType pointType = TwDefineStruct("POINT", pointMembers, 2, sizeof(Point), NULL, NULL);

    TwAddVarRW(bar, "Color0", TW_TYPE_COLOR4F, &g_Colors[0], " coloralpha=true colormode=hls Group='Vertex 0' Label=Color ");
    TwAddVarRW(bar, "Pos0", pointType, &g_Positions[0], " Group='Vertex 0' Label='Position' ");
    TwAddVarRW(bar, "Color1", TW_TYPE_COLOR4F, &g_Colors[1], " coloralpha=true colormode=hls Group='Vertex 1' Label=Color ");
    TwAddVarRW(bar, "Pos1", pointType, &g_Positions[1], " Group='Vertex 1' Label='Position' ");
    TwAddVarRW(bar, "Color2", TW_TYPE_COLOR4F, &g_Colors[2], " coloralpha=true colormode=hls Group='Vertex 2' Label=Color ");
    TwAddVarRW(bar, "Pos2", pointType, &g_Positions[2], " Group='Vertex 2' Label='Position' ");

    TwAddSeparator(bar, NULL, "");
    TwAddButton(bar, "FullWidthDemoMoreLines", FullWidthLinesCB, bar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.125f, 0.125f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float a = (float)g_Angle * (3.14159265358979f / 180.0f);
        float ca = cosf(a), sa = sinf(a);
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < NB_VERTS; ++i) {
            float x = g_Scale * (ca * g_Positions[i].X - sa * g_Positions[i].Y);
            float y = g_Scale * (sa * g_Positions[i].X + ca * g_Positions[i].Y);
            glColor4fv(g_Colors[i]);
            glVertex2f(x, y);
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
