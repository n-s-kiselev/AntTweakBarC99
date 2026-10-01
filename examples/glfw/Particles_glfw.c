//  ---------------------------------------------------------------------------
//
//  @file       Particles.c
//  @brief      An example that uses AntTweakBar with GLFW3 and OpenGL to draw
//              moving cubic particles, with interactive control over their
//              generation (birth rate, speed, direction, color).
//
//              Ported from the legacy GLFW2 example TwParticlesGLFW.c
//              (itself ported from TwSimpleSFML.cpp, originally SFML-based)
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


#define MAX_PARTICLES 2000

typedef struct {
    float Size;
    float Position[3];
    float Speed[3];
    float RotationAxis[3];
    float RotationAngle;  // in degrees
    float RotationSpeed;
    float Color[3];
    float Age;
    int Alive;
} Particle;

static Particle g_Particles[MAX_PARTICLES];
static int g_Width = 800, g_Height = 600;


static float Random(void)
{
    return 2.0f * ((float)rand() / (double)RAND_MAX) - 1.0f;
}

static void SpawnParticle(Particle *p, float size, const float speedDir[3], float speedNorm, const float color[3])
{
    p->Size = size * (1.0f + 0.2f * Random());
    p->Position[0] = p->Position[1] = p->Position[2] = 0;
    p->Speed[0] = speedNorm * (speedDir[0] + 0.1f * Random());
    p->Speed[1] = speedNorm * (speedDir[1] + 0.1f * Random());
    p->Speed[2] = speedNorm * (speedDir[2] + 0.1f * Random());
    p->RotationAxis[0] = Random();
    p->RotationAxis[1] = Random();
    p->RotationAxis[2] = Random();
    p->RotationAngle = 360.0f * Random();
    p->RotationSpeed = 360.0f * Random();
    p->Color[0] = color[0] + 0.2f * Random();
    p->Color[1] = color[1] + 0.2f * Random();
    p->Color[2] = color[2] + 0.2f * Random();
    p->Age = 0;
    p->Alive = 1;
}

static void UpdateParticle(Particle *p, float dt)
{
    p->Position[0] += dt * p->Speed[0];
    p->Position[1] += dt * p->Speed[1];
    p->Position[2] += dt * p->Speed[2];
    p->Speed[1] -= dt * 9.81f; // gravity
    p->RotationAngle += dt * p->RotationSpeed;
    p->Age += dt;
}

static void setProjection(int width, int height)
{
    float near = 1.0f, far = 500.0f;
    float fovy = 90.0f * 0.01745329251f;
    float aspect = (float)width / (float)height;
    float top = tanf(fovy * 0.5f) * near;
    float right = top * aspect;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right, right, -top, top, near, far);
    glMatrixMode(GL_MODELVIEW);
}

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
    float birthCount = 0;
    float birthRate = 20;               // number of particles generated per second
    float maxAge = 3.0f;                // particles life time
    float speedDir[3] = {0, 1, 0};      // initial particles speed direction
    float speedNorm = 7.0f;             // initial particles speed amplitude
    float size = 0.1f;                  // particles size
    float color[3] = {0.8f, 0.6f, 0};   // particles color
    float bgColor[3] = {0, 0.6f, 0.6f}; // background color
    double time;

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
    window = glfwCreateWindow(g_Width, g_Height, "AntTweakBar + GLFW3 (Particles)", NULL, NULL);
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
    glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);

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

    bar = TwNewBar("Particles");
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with GLFW3 and OpenGL.' ");
    TwDefine(" Particles position='16 240' ");
    {
        // Scaled by content scale so the panel keeps up with the
        // now-larger scaled contents.
        int barSize[2] = { (int)(200 * atb_glfw_ContentScaleX() + 0.5f), (int)(320 * atb_glfw_ContentScaleY() + 0.5f) };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }

    TwAddVarRW(bar, "Birth rate", TW_TYPE_FLOAT, &birthRate, " min=0.1 max=100 step=0.1 keyIncr='+' keyDecr='-' ");
    TwAddVarRW(bar, "Speed", TW_TYPE_FLOAT, &speedNorm, " min=0.1 max=10 step=0.1 keyIncr='s' keyDecr='S' ");
    TwAddVarRW(bar, "Direction", TW_TYPE_DIR3F, &speedDir, " opened=true showval=false ");
    TwAddVarRW(bar, "Color", TW_TYPE_COLOR3F, &color, " colorMode=hls opened=true ");
    TwAddVarRW(bar, "Background color", TW_TYPE_COLOR3F, &bgColor, " colorMode=hls opened=true ");

    TwAddSeparator(bar, NULL, "");
    TwAddButton(bar, "FullWidthDemoMoreLines", FullWidthLinesCB, bar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");


    time = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float dt = (float)(now - time);
        if (dt < 0) dt = 0;
        time = now;

        for (int i = 0; i < MAX_PARTICLES; ++i) {
            if (!g_Particles[i].Alive) continue;
            UpdateParticle(&g_Particles[i], dt);
            if (g_Particles[i].Age >= maxAge) g_Particles[i].Alive = 0;
        }

        birthCount += dt * birthRate;
        while (birthCount >= 1.0f) {
            for (int i = 0; i < MAX_PARTICLES; ++i) {
                if (!g_Particles[i].Alive) {
                    SpawnParticle(&g_Particles[i], size, speedDir, speedNorm, color);
                    break;
                }
            }
            birthCount -= 1.0f;
        }

        glClearColor(bgColor[0], bgColor[1], bgColor[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        for (int i = 0; i < MAX_PARTICLES; ++i) {
            if (!g_Particles[i].Alive) continue;
            Particle *p = &g_Particles[i];

            glColor3fv(p->Color);
            glLoadIdentity();
            glTranslatef(0.0f, -1.0f, -3.0f); // camera position
            glTranslatef(p->Position[0], p->Position[1], p->Position[2]);
            glScalef(p->Size, p->Size, p->Size);
            glRotatef(p->RotationAngle, p->RotationAxis[0], p->RotationAxis[1], p->RotationAxis[2]);

            glBegin(GL_QUADS);
                glNormal3f(0,0,-1); glVertex3f(0,0,0); glVertex3f(0,1,0); glVertex3f(1,1,0); glVertex3f(1,0,0);
                glNormal3f(0,0,+1); glVertex3f(0,0,1); glVertex3f(1,0,1); glVertex3f(1,1,1); glVertex3f(0,1,1);
                glNormal3f(-1,0,0); glVertex3f(0,0,0); glVertex3f(0,0,1); glVertex3f(0,1,1); glVertex3f(0,1,0);
                glNormal3f(+1,0,0); glVertex3f(1,0,0); glVertex3f(1,1,0); glVertex3f(1,1,1); glVertex3f(1,0,1);
                glNormal3f(0,-1,0); glVertex3f(0,0,0); glVertex3f(1,0,0); glVertex3f(1,0,1); glVertex3f(0,0,1);
                glNormal3f(0,+1,0); glVertex3f(0,1,0); glVertex3f(0,1,1); glVertex3f(1,1,1); glVertex3f(1,1,0);
            glEnd();
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
