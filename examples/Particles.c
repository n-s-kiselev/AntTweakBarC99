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

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
  (void)scancode;
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
    return;
  }

  if (action == GLFW_PRESS || action == GLFW_REPEAT)
  {
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
  (void)window;
  if (TwKeyPressed(key, 0)) return;
}

static void mousebuttonCallback(GLFWwindow* _window, int _button, int _action, int _mods)
{
  (void)_window; (void)_mods;
  TwEventMouseButtonGLFW(_button, _action);
}

static void mousePosCallback(GLFWwindow* _window, double _xpos, double _ypos)
{
  (void)_window;
  TwEventMousePosGLFW((int)_xpos, (int)_ypos);
}

static void mouseScrollCallback(GLFWwindow* _window, double _xoffset, double _yoffset)
{
  (void)_window; (void)_xoffset;
  static double pos = 0;
  pos += _yoffset;
  TwEventMouseWheelGLFW((int)pos);
}

static void windowSizeCallback(GLFWwindow* window, int width, int height)
{
  (void)window;
  if (height == 0) height = 1;
  g_Width = width;
  g_Height = height;
  setProjection(width, height);
  TwWindowSize(width, height);
}

void error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
    fflush(stderr);
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

    // Disable Retina scaling for now (matches this fork's other GLFW3
    // examples): keeps window-coordinate and framebuffer-pixel units equal,
    // so no manual HiDPI mouse/window-size scaling is needed.
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
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

    bar = TwNewBar("Particles");
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with GLFW3 and OpenGL.' ");
    TwDefine(" Particles size='200 320' position='16 240' ");

    TwAddVarRW(bar, "Birth rate", TW_TYPE_FLOAT, &birthRate, " min=0.1 max=100 step=0.1 keyIncr='+' keyDecr='-' ");
    TwAddVarRW(bar, "Speed", TW_TYPE_FLOAT, &speedNorm, " min=0.1 max=10 step=0.1 keyIncr='s' keyDecr='S' ");
    TwAddVarRW(bar, "Direction", TW_TYPE_DIR3F, &speedDir, " opened=true showval=false ");
    TwAddVarRW(bar, "Color", TW_TYPE_COLOR3F, &color, " colorMode=hls opened=true ");
    TwAddVarRW(bar, "Background color", TW_TYPE_COLOR3F, &bgColor, " colorMode=hls opened=true ");

    glfwSetKeyCallback(window, keyCallback);
    glfwSetCharCallback(window, charCallback);
    glfwSetMouseButtonCallback(window, mousebuttonCallback);
    glfwSetCursorPosCallback(window, mousePosCallback);
    glfwSetScrollCallback(window, mouseScrollCallback);
    glfwSetWindowSizeCallback(window, windowSizeCallback);

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
    DestroyGLFWCursorCache();
    glfwTerminate();
    return 0;
}
