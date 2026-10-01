//  ---------------------------------------------------------------------------
//
//  @file       Shapes.c
//  @brief      An example that uses AntTweakBar with OpenGL and the GLFW3
//              windowing system to display one of several 3D shapes,
//              orientable with an interactive TW_TYPE_QUAT4F rotation
//              widget or auto-rotated, lit with an adjustable light
//              direction, and colored via grouped TW_TYPE_COLOR3F material
//              variables. Also demonstrates a TwType enum variable (to pick
//              the current shape) and a callback-bound variable
//              (TwAddVarCB, for the auto-rotate toggle).
//
//              Ported from the legacy GLUT example TwSimpleGLUT.c: GLUT's
//              callback-registration model and glutMainLoop() are replaced
//              by an explicit main loop plus atb_glfw.h, the GLFW3 <->
//              AntTweakBar glue (cursor, clipboard, key/mouse/scroll/resize)
//              shared by every example in this folder. The three
//              GLUT solid-shape helpers the original used
//              (glutSolidTeapot/glutSolidTorus/glutSolidCone) have no GLFW3
//              or GLAD equivalent - GLFW is strictly a windowing/input
//              library with no shape-drawing utilities of its own - so this
//              port replaces them with small procedural tessellations
//              built once into display lists: a UV sphere (replacing the
//              teapot, which would otherwise require a large hardcoded
//              patch dataset), a torus, and a cone. The shape-switching
//              enum, quaternion widget, auto-rotate callback, light
//              direction, and grouped material colors are otherwise
//              unchanged from the original.
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
#include <stdbool.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


// This example displays one of the following shapes.
// (SHAPE_SPHERE replaces the original GLUT example's teapot - see the
// file-level comment above for why.)
typedef enum { SHAPE_SPHERE=1, SHAPE_TORUS, SHAPE_CONE } Shape;
#define NUM_SHAPES 3
Shape g_CurrentShape = SHAPE_TORUS;
// Shapes scale
float g_Zoom = 1.0f;
// Shape orientation (stored as a quaternion)
float g_Rotation[] = { 0.0f, 0.0f, 0.0f, 1.0f };
// Auto rotate
int g_AutoRotate = 0;
double g_RotateTime = 0;
float g_RotateStart[] = { 0.0f, 0.0f, 0.0f, 1.0f };
// Shapes material
float g_MatAmbient[] = { 0.5f, 0.0f, 0.0f, 1.0f };
float g_MatDiffuse[] = { 1.0f, 1.0f, 0.0f, 1.0f };
// Light parameter
float g_LightMultiplier = 1.0f;
float g_LightDirection[] = { -0.57735f, -0.57735f, -0.57735f };


// Routine to set a quaternion from a rotation axis and angle
// ( input axis = float[3] angle = float  output: quat = float[4] )
void SetQuaternionFromAxisAngle(const float *axis, float angle, float *quat)
{
    float sina2, norm;
    sina2 = (float)sin(0.5f * angle);
    norm = (float)sqrt(axis[0]*axis[0] + axis[1]*axis[1] + axis[2]*axis[2]);
    quat[0] = sina2 * axis[0] / norm;
    quat[1] = sina2 * axis[1] / norm;
    quat[2] = sina2 * axis[2] / norm;
    quat[3] = (float)cos(0.5f * angle);
}


// Routine to convert a quaternion to a 4x4 matrix
// ( input: quat = float[4]  output: mat = float[4*4] )
void ConvertQuaternionToMatrix(const float *quat, float *mat)
{
    float yy2 = 2.0f * quat[1] * quat[1];
    float xy2 = 2.0f * quat[0] * quat[1];
    float xz2 = 2.0f * quat[0] * quat[2];
    float yz2 = 2.0f * quat[1] * quat[2];
    float zz2 = 2.0f * quat[2] * quat[2];
    float wz2 = 2.0f * quat[3] * quat[2];
    float wy2 = 2.0f * quat[3] * quat[1];
    float wx2 = 2.0f * quat[3] * quat[0];
    float xx2 = 2.0f * quat[0] * quat[0];
    mat[0*4+0] = - yy2 - zz2 + 1.0f;
    mat[0*4+1] = xy2 + wz2;
    mat[0*4+2] = xz2 - wy2;
    mat[0*4+3] = 0;
    mat[1*4+0] = xy2 - wz2;
    mat[1*4+1] = - xx2 - zz2 + 1.0f;
    mat[1*4+2] = yz2 + wx2;
    mat[1*4+3] = 0;
    mat[2*4+0] = xz2 + wy2;
    mat[2*4+1] = yz2 - wx2;
    mat[2*4+2] = - xx2 - yy2 + 1.0f;
    mat[2*4+3] = 0;
    mat[3*4+0] = mat[3*4+1] = mat[3*4+2] = 0;
    mat[3*4+3] = 1;
}


// Routine to multiply 2 quaternions (ie, compose rotations)
// ( input q1 = float[4] q2 = float[4]  output: qout = float[4] )
void MultiplyQuaternions(const float *q1, const float *q2, float *qout)
{
    float qr[4];
    qr[0] = q1[3]*q2[0] + q1[0]*q2[3] + q1[1]*q2[2] - q1[2]*q2[1];
    qr[1] = q1[3]*q2[1] + q1[1]*q2[3] + q1[2]*q2[0] - q1[0]*q2[2];
    qr[2] = q1[3]*q2[2] + q1[2]*q2[3] + q1[0]*q2[1] - q1[1]*q2[0];
    qr[3] = q1[3]*q2[3] - (q1[0]*q2[0] + q1[1]*q2[1] + q1[2]*q2[2]);
    qout[0] = qr[0]; qout[1] = qr[1]; qout[2] = qr[2]; qout[3] = qr[3];
}


// Procedural replacement for glutSolidTorus(innerRadius, outerRadius, sides, rings):
// builds a torus of the given minor/major radius, immediate-mode GL_QUADS
// with per-vertex normals, matching this example's fixed-function/compat-
// profile rendering style.
static void DrawTorus(float minorRadius, float majorRadius, int sides, int rings)
{
    for (int ring = 0; ring < rings; ++ring) {
        float theta0 = (float)(2.0 * M_PI * ring / rings);
        float theta1 = (float)(2.0 * M_PI * (ring + 1) / rings);
        glBegin(GL_QUAD_STRIP);
        for (int side = 0; side <= sides; ++side) {
            float phi = (float)(2.0 * M_PI * side / sides);
            float cosPhi = cosf(phi), sinPhi = sinf(phi);
            for (int t = 0; t < 2; ++t) {
                float theta = (t == 0) ? theta0 : theta1;
                float cosTheta = cosf(theta), sinTheta = sinf(theta);
                float nx = cosTheta * cosPhi, ny = sinTheta * cosPhi, nz = sinPhi;
                float x = cosTheta * (majorRadius + minorRadius * cosPhi);
                float y = sinTheta * (majorRadius + minorRadius * cosPhi);
                float z = minorRadius * sinPhi;
                glNormal3f(nx, ny, nz);
                glVertex3f(x, y, z);
            }
        }
        glEnd();
    }
}


// Procedural replacement for glutSolidCone(baseRadius, height, slices, stacks):
// a capped cone along +Z, immediate-mode GL_TRIANGLE_FAN for the side and
// base, matching this example's fixed-function/compat-profile style.
static void DrawCone(float baseRadius, float height, int slices)
{
    float nz = baseRadius / sqrtf(baseRadius*baseRadius + height*height);
    float nxy = height / sqrtf(baseRadius*baseRadius + height*height);

    // Side
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0.0f, 0.0f, 1.0f);
    glVertex3f(0.0f, 0.0f, height);
    for (int i = 0; i <= slices; ++i) {
        float a = (float)(2.0 * M_PI * i / slices);
        float x = cosf(a), y = sinf(a);
        glNormal3f(x * nxy, y * nxy, nz);
        glVertex3f(x * baseRadius, y * baseRadius, 0.0f);
    }
    glEnd();

    // Base cap
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0.0f, 0.0f, -1.0f);
    glVertex3f(0.0f, 0.0f, 0.0f);
    for (int i = slices; i >= 0; --i) {
        float a = (float)(2.0 * M_PI * i / slices);
        glVertex3f(cosf(a) * baseRadius, sinf(a) * baseRadius, 0.0f);
    }
    glEnd();
}


// Procedural replacement for glutSolidTeapot(): a UV sphere (see the
// file-level comment above for why the teapot itself could not be ported).
static void DrawSphere(float radius, int slices, int stacks)
{
    for (int i = 0; i < stacks; ++i) {
        float lat0 = (float)(M_PI * (-0.5 + (double)i / stacks));
        float lat1 = (float)(M_PI * (-0.5 + (double)(i + 1) / stacks));
        float z0 = sinf(lat0), zr0 = cosf(lat0);
        float z1 = sinf(lat1), zr1 = cosf(lat1);

        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; ++j) {
            float lng = (float)(2.0 * M_PI * j / slices);
            float x = cosf(lng), y = sinf(lng);

            glNormal3f(x * zr0, y * zr0, z0);
            glVertex3f(radius * x * zr0, radius * y * zr0, radius * z0);
            glNormal3f(x * zr1, y * zr1, z1);
            glVertex3f(radius * x * zr1, radius * y * zr1, radius * z1);
        }
        glEnd();
    }
}


// Render Display() draws whichever shape is selected. Display lists are
// built once in main() (see BuildShapeDisplayLists() below).
void Display(void)
{
    float v[4]; // will be used to set light parameters
    float mat[4*4]; // rotation matrix

    // Clear frame buffer
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_NORMALIZE);

    // Set light
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    v[0] = v[1] = v[2] = g_LightMultiplier*0.4f; v[3] = 1.0f;
    glLightfv(GL_LIGHT0, GL_AMBIENT, v);
    v[0] = v[1] = v[2] = g_LightMultiplier*0.8f; v[3] = 1.0f;
    glLightfv(GL_LIGHT0, GL_DIFFUSE, v);
    v[0] = -g_LightDirection[0]; v[1] = -g_LightDirection[1]; v[2] = -g_LightDirection[2]; v[3] = 0.0f;
    glLightfv(GL_LIGHT0, GL_POSITION, v);

    // Set material
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, g_MatAmbient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, g_MatDiffuse);

    // Rotate and draw shape
    glPushMatrix();
    glTranslatef(0.5f, -0.3f, 0.0f);
    if( g_AutoRotate )
    {
        float axis[3] = { 0, 1, 0 };
        float angle = (float)(glfwGetTime() - g_RotateTime);
        float quat[4];
        SetQuaternionFromAxisAngle(axis, angle, quat);
        MultiplyQuaternions(g_RotateStart, quat, g_Rotation);
    }
    ConvertQuaternionToMatrix(g_Rotation, mat);
    glMultMatrixf(mat);
    glScalef(g_Zoom, g_Zoom, g_Zoom);
    glCallList(g_CurrentShape);
    glPopMatrix();

    // Draw tweak bars
    TwDraw();
}



// Runs on every framebuffer resize, before TwWindowSize(). Size is in pixels.
static void resizeHook(GLFWwindow *window, int width, int height)
{
    (void)window;
    if (height == 0) height = 1;
    float aspect = (float)width / (float)height;
    float znear = 1.0f;
    float zfar = 100.0f;
    float fov = 45.0f;
    float top = tanf(fov * 0.01745329251f) * znear;
    float bottom = -top;
    float right = top * aspect;
    float left = -right;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(left, right, bottom, top, znear, zfar);

    // Set up the modelview (camera) matrix - manual equivalent of the
    // original's gluLookAt(0,0,5, 0,0,0, 0,1,0) plus its extra offset.
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -5.0f);
    glTranslatef(0.0f, 0.6f, -1.0f);
}


//  Callback function called when the 'AutoRotate' variable value of the tweak bar has changed
void TW_CALL SetAutoRotateCB(const void *value, void *clientData)
{
    (void)clientData; // unused

    g_AutoRotate = *(const int *)value; // copy value to g_AutoRotate
    if( g_AutoRotate!=0 )
    {
        // init rotation
        g_RotateTime = glfwGetTime();
        g_RotateStart[0] = g_Rotation[0];
        g_RotateStart[1] = g_Rotation[1];
        g_RotateStart[2] = g_Rotation[2];
        g_RotateStart[3] = g_Rotation[3];

        // make Rotation variable read-only
        TwDefine(" TweakBar/ObjRotation readonly ");
    }
    else
        // make Rotation variable read-write
        TwDefine(" TweakBar/ObjRotation readonly=false ");
}


//  Callback function called by the tweak bar to get the 'AutoRotate' value
void TW_CALL GetAutoRotateCB(void *value, void *clientData)
{
    (void)clientData; // unused
    *(int *)value = g_AutoRotate; // copy g_AutoRotate to value
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


void error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
    fflush(stderr);
}

// Builds the three shape display lists once (replaces the original's
// glutSolidTeapot/glutSolidTorus/glutSolidCone calls - see file-level
// comment).
static void BuildShapeDisplayLists(void)
{
    glNewList(SHAPE_SPHERE, GL_COMPILE);
    DrawSphere(1.0f, 32, 16);
    glEndList();
    glNewList(SHAPE_TORUS, GL_COMPILE);
    DrawTorus(0.3f, 1.0f, 16, 32);
    glEndList();
    glNewList(SHAPE_CONE, GL_COMPILE);
    DrawCone(1.0f, 1.5f, 64);
    glEndList();
}


// Main
int main(void)
{
    GLFWwindow *window;
    TwBar *bar; // Pointer to the tweak bar
    float axis[] = { 0.7f, 0.7f, 0.0f }; // initial model rotation
    float angle = 0.8f;

    // Set error callback
    glfwSetErrorCallback(error_callback);

    // Initialize GLFW
    if (!glfwInit())
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
    window = glfwCreateWindow(640, 480, "AntTweakBar + GLFW3 (Shapes)", NULL, NULL);
    if (!window)
    {
        fprintf(stderr, "Cannot open GLFW window\n");
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
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
        hooks.resize = resizeHook;
        atb_glfw_Attach(window, &hooks);
    }

    // Create the shape display lists
    BuildShapeDisplayLists();

    // Create a tweak bar
    bar = TwNewBar("TweakBar");
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with GLFW3 and OpenGL.' "); // Message added to the help bar.
    TwDefine(" TweakBar color='96 216 224' "); // change default tweak bar color
    {
        // Scaled by content scale so the panel keeps up with the
        // now-larger scaled contents.
        int barSize[2] = { (int)(200 * atb_glfw_ContentScaleX() + 0.5f), (int)(400 * atb_glfw_ContentScaleY() + 0.5f) };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }

    // Add 'g_Zoom' to 'bar': this is a modifable (RW) variable of type TW_TYPE_FLOAT. Its key shortcuts are [z] and [Z].
    TwAddVarRW(bar, "Zoom", TW_TYPE_FLOAT, &g_Zoom,
               " min=0.01 max=2.5 step=0.01 keyIncr=z keyDecr=Z help='Scale the object (1=original size).' ");

    // Add 'g_Rotation' to 'bar': this is a variable of type TW_TYPE_QUAT4F which defines the object's orientation
    TwAddVarRW(bar, "ObjRotation", TW_TYPE_QUAT4F, &g_Rotation,
               " label='Object rotation' opened=true help='Change the object orientation.' ");

    // Add callback to toggle auto-rotate mode (callback functions are defined above).
    TwAddVarCB(bar, "AutoRotate", TW_TYPE_BOOL32, SetAutoRotateCB, GetAutoRotateCB, NULL,
               " label='Auto-rotate' key=space help='Toggle auto-rotate mode.' ");

    // Add 'g_LightMultiplier' to 'bar': this is a variable of type TW_TYPE_FLOAT. Its key shortcuts are [+] and [-].
    TwAddVarRW(bar, "Multiplier", TW_TYPE_FLOAT, &g_LightMultiplier,
               " label='Light booster' min=0.1 max=4 step=0.02 keyIncr='+' keyDecr='-' help='Increase/decrease the light power.' ");

    // Add 'g_LightDirection' to 'bar': this is a variable of type TW_TYPE_DIR3F which defines the light direction
    TwAddVarRW(bar, "LightDir", TW_TYPE_DIR3F, &g_LightDirection,
               " label='Light direction' opened=true help='Change the light direction.' ");

    // Add 'g_MatAmbient' to 'bar': this is a variable of type TW_TYPE_COLOR3F (3 floats color, alpha is ignored)
    // and is inserted into a group named 'Material'.
    TwAddVarRW(bar, "Ambient", TW_TYPE_COLOR3F, &g_MatAmbient, " group='Material' ");

    // Add 'g_MatDiffuse' to 'bar': this is a variable of type TW_TYPE_COLOR3F (3 floats color, alpha is ignored)
    // and is inserted into group 'Material'.
    TwAddVarRW(bar, "Diffuse", TW_TYPE_COLOR3F, &g_MatDiffuse, " group='Material' ");

    // Add the enum variable 'g_CurrentShape' to 'bar'
    // (before adding an enum variable, its enum type must be declared to AntTweakBar as follow)
    {
        // shapeEV associates Shape enum values with labels that will be displayed instead of enum values
        TwEnumVal shapeEV[NUM_SHAPES] = { {SHAPE_SPHERE, "Sphere"}, {SHAPE_TORUS, "Torus"}, {SHAPE_CONE, "Cone"} };
        // Create a type for the enum shapeEV
        TwType shapeType = TwDefineEnum("ShapeType", shapeEV, NUM_SHAPES);
        // add 'g_CurrentShape' to 'bar': this is a variable of type ShapeType. Its key shortcuts are [<] and [>].
        TwAddVarRW(bar, "Shape", shapeType, &g_CurrentShape, " keyIncr='<' keyDecr='>' help='Change object shape.' ");
    }

    TwAddSeparator(bar, NULL, "");
    TwAddButton(bar, "FullWidthDemoMoreLines", FullWidthLinesCB, bar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");


    // Store time
    g_RotateTime = glfwGetTime();
    // Init rotation
    SetQuaternionFromAxisAngle(axis, angle, g_Rotation);
    SetQuaternionFromAxisAngle(axis, angle, g_RotateStart);

    // Main loop (repeated while window is not closed and [ESC] is not pressed)
    while (!glfwWindowShouldClose(window))
    {
        Display();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteLists(SHAPE_SPHERE, NUM_SHAPES);

    // Terminate AntTweakBar and GLFW
    TwTerminate();
    atb_glfw_Detach(window);   // releases the cursors, after TwTerminate()
    glfwTerminate();

    return 0;
}
