//  ---------------------------------------------------------------------------
//
//  @file       SimpleGL21.c
//  @brief      A simple example that uses AntTweakBar with
//              OpenGL 2.1 (compatibility profile) and the GLFW3 windowing
//              system.
//
//              Also demonstrates a temporary, self-contained dialog bar:
//              pressing [Esc] shows a "Quit the application?" bar with
//              Yes/No buttons, built at that moment with TwNewBar() and
//              torn down again with TwDeleteBar() - see ShowConfirmQuitBar()
//              below. Ported from the legacy GLFW2 example of (nearly) the
//              same name, which used the same dialog technique against
//              glfwOpenWindow()'s implicit-context API.
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              GLFW:        http://www.glfw.org
//
//  @author     Philippe Decaudin
//  @date       2006/05/20
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <AntTweakBar.h>
#include "atb_glfw.h"   // shared GLFW3 <-> AntTweakBar glue for these examples
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

float g_cameraPosX = 0.0f;
float g_cameraPosY = 0.0f;
float g_cameraPosZ = 5.0f;

bool g_cameraDragging = false;

double g_lastMouseX = 0.0;
double g_lastMouseY = 0.0;

char *g_userText = NULL; // Will be malloc'ed on first use

// Quit-confirmation dialog state (see ShowConfirmQuitBar() below).
static TwBar *g_ConfirmBar = NULL; // the "ConfirmQuit" bar, or NULL when not shown

static void CloseConfirmQuitBar(void);

void TW_CALL ConfirmQuitYesCB(void *clientData)
{
    glfwSetWindowShouldClose((GLFWwindow *)clientData, GLFW_TRUE);
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
static void ShowConfirmQuitBar(GLFWwindow *window)
{
    char def[160];
    int winWidth, winHeight, barWidth, barHeight, posX, posY;

    if( g_ConfirmBar != NULL )
        return; // already showing

    SetAllBarsVisible(0);

    glfwGetFramebufferSize(window, &winWidth, &winHeight);
    barWidth  = (int)(220 * atb_glfw_ContentScaleX() + 0.5f);
    barHeight = (int)(80 * atb_glfw_ContentScaleY() + 0.5f);
    posX = (winWidth  - barWidth)  / 2; if( posX < 0 ) posX = 0;
    posY = (winHeight - barHeight) / 2; if( posY < 0 ) posY = 0;

    g_ConfirmBar = TwNewBar("ConfirmQuit");
    snprintf(def, sizeof(def),
             " ConfirmQuit label='Confirm' size='%d %d' position='%d %d' "
             "resizable=false movable=false iconifiable=false ",
             barWidth, barHeight, posX, posY);
    TwDefine(def);

    TwAddButton(g_ConfirmBar, "Msg", NULL, NULL, " label='Quit the application?' ");
    TwAddButton(g_ConfirmBar, "Yes", ConfirmQuitYesCB, window, " label='Yes' ");
    TwAddButton(g_ConfirmBar, "No",  ConfirmQuitNoCB,  NULL,   " label='No' ");
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

// [Esc] opens the quit-confirmation dialog. The hook runs only for keys
// AntTweakBar did not consume, so an open popup or an active edit field
// still gets [Esc] first.
static void keyHook(GLFWwindow *window, int key, int scancode, int action, int mods)
{
  (void)scancode; (void)mods;
  if ((action == GLFW_PRESS || action == GLFW_REPEAT) && key == GLFW_KEY_ESCAPE)
  {
    if (g_ConfirmBar == NULL)
      ShowConfirmQuitBar(window);
  }
}

// Left-drag orbits the camera, right-click recenters it. Both hooks run only
// when the click/motion did not land on a tweak bar.
static void mouseButtonHook(GLFWwindow *window, int button, int action, int mods)
{
  (void)mods;
  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    if (action == GLFW_PRESS) {
      g_cameraDragging = true;
      glfwGetCursorPos(window, &g_lastMouseX, &g_lastMouseY);
    } else if (action == GLFW_RELEASE) {
      g_cameraDragging = false;
    }
  }

  if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
    g_cameraPosX = 0;
    g_cameraPosY = 0;
    g_cameraPosZ = 5.0f; // Reset camera position
  }
}

static void cursorPosHook(GLFWwindow *window, double xpos, double ypos)
{
  if (g_cameraDragging) {
    double dx = xpos - g_lastMouseX;
    double dy = ypos - g_lastMouseY;

    int width, height;
    glfwGetWindowSize(window, &width, &height);
    g_cameraPosX += (float)dx / width * 2.0f;  // Scale to screen
    g_cameraPosY -= (float)dy / height * 2.0f; // Inverted Y

    g_lastMouseX = xpos;
    g_lastMouseY = ypos;
  }
}

static void scrollHook(GLFWwindow *window, double xoffset, double yoffset)
{
  (void)window; (void)xoffset;
  g_cameraPosZ -= (float)yoffset * 0.05f; // Zoom sensitivity
  if (g_cameraPosZ < 1.0f) g_cameraPosZ = 1.0f; // Prevent too close
  if (g_cameraPosZ > 50.0f) g_cameraPosZ = 50.0f; // Prevent too far
}

// Runs on every framebuffer resize, before TwWindowSize(). Size is in pixels.
static void resizeHook(GLFWwindow *window, int width, int height)
{
  (void)window;
  if (height == 0) height = 1;
    float aspect = (float)width / (float)height;
    float near = 1.0f, far = 100.0f;
    float fov = 45.0f;
    float top = tan(fov * 0.01745329251f) * near;
    float bottom = -top;
    float right = top * aspect;
    float left = -right;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(left, right, bottom, top, near, far);
}

void TW_CALL ResetCubePosition(void *clientData)
{
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

void error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
    fflush(stderr);
}


// Main
int main(void)
{
  GLFWwindow* window; // GLFW3 window
  TwBar *bar;         // Pointer to a tweak bar

  double time = 0, dt;// Current time and enlapsed time
  double turn = 0;    // Model turn counter
  double speed = 0.3; // Model rotation speed
  int wire = 0;       // Draw model in wireframe?
  float bgColor[] = { 73.0/255, 25.0/255, 100.0/255 };         // Background color
  unsigned char cubeColor[] = { 255, 170, 0, 250 }; // Model color (32bits RGBA)


  // Set error callback
  glfwSetErrorCallback(error_callback);

  // Intialize GLFW
  if(!glfwInit())
  {
      fprintf(stderr, "GLFW initialization failed\n");
      return 1;
  }

  // Requested size is in "reference" (96 DPI) pixels; grow the actual
  // window to match the monitor's real pixel density on platforms where
  // window size and framebuffer size are otherwise always 1:1 (Windows,
  // X11) - a no-op on macOS, which already does this by definition (see
  // docs/plans/examples-hidpi-scaling.md).
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  window = glfwCreateWindow(800, 600, "AntTweakBar + GLFW3 (OpenGL 2.1)", NULL, NULL);
  if(!window)
  {
      fprintf(stderr, "Cannot open GLFW window\n");
      glfwTerminate();
      return -1;
  }

  glfwMakeContextCurrent(window);
  if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
  {
      fprintf(stderr, "Failed to initialize GLAD\n");
      return -2;
  }

  // AntTweakBar has no DPI awareness, so scale its font by the window content
  // scale to keep a comparable physical size. Must precede TwInit(), which
  // bakes the scale into the font atlases.
  atb_glfw_SetFontScaling(window);

  // Initialize AntTweakBar
  if (!TwInit(TW_OPENGL, NULL)) {
      const char* err = TwGetLastError();
      fprintf(stderr, "TwInit failed: %s\n", err ? err : "Unknown error");
      fflush(stderr);
      return -3;
  }
  // Registers the GLFW callbacks, gives GLFW3 authoritative cursor
  // ownership, routes the clipboard through it, and applies the current
  // framebuffer size - see atb_glfw.h.
  {
    atb_glfw_Hooks hooks = { 0 };
    hooks.key = keyHook;
    hooks.mouseButton = mouseButtonHook;
    hooks.cursorPos = cursorPosHook;
    hooks.scroll = scrollHook;
    hooks.resize = resizeHook;
    atb_glfw_Attach(window, &hooks);
  }
  TwCopyCDStringToClientFunc(CopyCDStringToClient);

  // Create a tweak bar
  bar = TwNewBar("TweakBar");
  TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with GLFW3 and OpenGL 2.1. Press [Esc] to quit (with a confirmation dialog).' "); // Message added to the help bar.
  TwDefine(" TweakBar color='100 100 50' alpha=200 ");
  {
      // Scaled by content scale so the panel keeps up with the
      // now-larger scaled contents.
      int barSize[2] = { (int)(220 * atb_glfw_ContentScaleX() + 0.5f),
                         (int)(530 * atb_glfw_ContentScaleY() + 0.5f) };
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
  time = glfwGetTime();

  // Main loop (repeated while window is not closed - [Esc] shows a
  // confirmation dialog instead of quitting immediately, see keyCallback())
  while (!glfwWindowShouldClose(window))
  {
    // Clear frame buffer
    glClearColor(bgColor[0], bgColor[1], bgColor[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Update rotation
    dt = glfwGetTime() - time;
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
    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  // Terminate AntTweakBar and GLFW
  TwTerminate();
  atb_glfw_Detach(window);   // releases the cursors, after TwTerminate()
  glfwTerminate();

  return 0;
}
