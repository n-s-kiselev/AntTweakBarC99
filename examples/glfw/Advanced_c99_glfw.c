//  ---------------------------------------------------------------------------
//
//  @file       Advanced_c99.c
//  @brief      An example showing many features of AntTweakBar,
//              including variables accessed by callbacks and
//              the definition of a custom structure type.
//              It also uses OpenGL and GLFW windowing system
//              but could be easily adapted to other frameworks.
//
//              This is the strict-C99 sibling of Advanced_cpp.cpp: same
//              scene, same AntTweakBar usage, same behavior - only the
//              language changed. The C++ `class Scene` (constructor/
//              destructor/methods) became a plain `struct Scene` plus
//              free `Scene_*` functions taking a `Scene *` first
//              parameter; `Light`'s and `Scene`'s nested C++ enums
//              (`Light::AnimMode`, `Scene::RotMode`) became file-scope
//              `typedef enum`s; `new[]`/`delete[]` became `malloc`/`free`;
//              `static_cast<T>` became a plain C-style cast; and
//              TW_TYPE_BOOLCPP - a C++-only type sized to match a real
//              C++ `bool` - became TW_TYPE_BOOL32, the plain 32-bit
//              boolean type, with its two bound variables (`Light::Active`,
//              `Scene::Wireframe`) widened from `bool` to `int` to match
//              TW_TYPE_BOOL32's expected storage size (the same fix
//              already made in examples/Sponge.c).
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              GLFW:        http://www.glfw.org
//
//              This example draws a simple scene that can be re-tesselated
//              interactively, and illuminated dynamically by an adjustable
//              number of moving lights.
//
//  @author     Philippe Decaudin
//  @date       2006/05/20
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <AntTweakBar.h>
#include "atb_glfw.h"   // shared GLFW3 <-> AntTweakBar glue for these examples

#include <stdbool.h>
#include <stddef.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#if !defined(_WIN32) && !defined(_WIN64)
#   define _snprintf snprintf
#endif

// M_PI is a common extension, not part of strict C99 - some libcs (e.g.
// glibc under -std=c99 with no _DEFAULT_SOURCE) hide it. Defined here so
// this file builds the same way on every supported platform.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

float g_cameraPosX = 0.0f;
float g_cameraPosY = 0.0f;
float g_cameraPosZ = 0.0f;

bool g_cameraDragging = false;

double g_lastMouseX = 0.0;
double g_lastMouseY = 0.0;

const char* title = "AntTweakBar example: Advanced (C99)";

// Light animation mode. A C++ nested `enum Light::AnimMode` in the original;
// hoisted to file scope here since C has no nested-type/qualified-name
// concept for enums declared inside a struct.
typedef enum { ANIM_FIXED, ANIM_BOUNCE, ANIM_ROTATE, ANIM_COMBINED } LightAnimMode;

// Light structure: embeds light parameters
typedef struct Light
{
    int     Active;     // light On or Off (TW_TYPE_BOOL32-bound: must be int, not bool)
    float   Pos[4];     // light position (in homogeneous coordinates, ie. Pos[4]=1)
    float   Color[4];   // light color (no alpha, ie. Color[4]=1)
    float   Radius;     // radius of the light influence area
    float   Dist0, Angle0, Height0, Speed0; // light initial cylindrical coordinates and speed
    char    Name[4];    // light short name (will be named "1", "2", "3",...)
    LightAnimMode Animation; // light animation mode
} Light;

// Scene rotation mode. A C++ nested `enum Scene::RotMode` in the original;
// hoisted to file scope for the same reason as LightAnimMode above.
typedef enum { ROT_OFF, ROT_CW, ROT_CCW } SceneRotMode;

// Background color space mode - a small demo-only enum (not part of the
// original Advanced example) used below to exercise the align_right/
// align_left label parameters on an enum widget alongside plain floats.
typedef enum { COLORSPACE_LINEAR, COLORSPACE_SRGB } SceneColorSpace;

// Structure that describes the scene. A plain C struct: the original C++
// `class Scene` had no invariant that required hiding its fields, so every
// field is public here too - only its methods became free functions
// (Scene_*, below), each taking a `Scene *` (or `const Scene *`) first
// parameter in place of the implicit `this`.
typedef struct Scene
{
    int     Wireframe;  // draw scene in wireframe or filled (TW_TYPE_BOOL32-bound: must be int, not bool)
    int     Subdiv;     // number of subdivisions used to tessellate the scene
    int     NumLights;  // number of dynamic lights
    float   BgColor0[3], BgColor1[3]; // top and bottom background colors
    float   Ambient;    // scene ambient factor
    float   Reflection; // ground plane reflection factor (0=no reflection, 1=full reflection)
    double  RotYAngle;  // rotation angle of the scene around its Y axis (in degree)
    float   RotSpeed;   // rotation speed, in degree/second
    SceneRotMode Rotation; // scene rotation mode (off, clockwise, counter-clockwise)
    SceneColorSpace ColorSpace; // demo-only: exercises align_right on an enum widget

    GLuint  objList, groundList, haloList;  // OpenGL display list IDs
    int     maxLights;                      // maximum number of dynamic lights allowed by the graphic card
    Light * lights;                         // array of lights (malloc'ed in Scene_Init, freed in Scene_Destruct)
    TwBar * lightsBar;                      // pointer to the tweak bar for lights created by Scene_CreateBar()
} Scene;

// Forward declarations (the C++ class body served this purpose in
// Advanced_cpp.cpp; C has no equivalent, so every Scene_* function used
// before its own definition further down needs one here).
static void Scene_Construct(Scene *scene);
static void Scene_Destruct(Scene *scene);
static void Scene_Init(Scene *scene, bool changeLights);
static void Scene_Draw(const Scene *scene);
static void Scene_Update(Scene *scene, double time);
static void Scene_CreateBar(Scene *scene);
static void Scene_DrawHalos(const Scene *scene, bool reflected);
// These three drawing subroutines never touch Scene state (verified by
// reading Advanced_cpp.cpp's own DrawSubdivPlaneY/DrawSubdivCylinderY/
// DrawSubdivHaloZ bodies) - they were private methods only for namespacing,
// so they become plain file-scope functions with no Scene* parameter,
// rather than carrying an unused one.
static void DrawSubdivPlaneY(float xMin, float xMax, float y, float zMin, float zMax, int xSubdiv, int zSubdiv);
static void DrawSubdivCylinderY(float xCenter, float yBottom, float zCenter, float height, float radiusBottom, float radiusTop, int sideSubdiv, int ySubdiv);
static void DrawSubdivHaloZ(float x, float y, float z, float radius, int subdiv);

// Constructor
static void Scene_Construct(Scene *scene)
{
    // Set scene members.
    // The scene will be created by Scene_Init()
    scene->Wireframe = 0;
    scene->Subdiv = 20;
    scene->NumLights = 0;
    scene->BgColor0[0] = 0.9f;
    scene->BgColor0[1] = 0.0f;
    scene->BgColor0[2] = 0.0f;
    scene->BgColor1[0] = 0.3f;
    scene->BgColor1[1] = 0.0f;
    scene->BgColor1[2] = 0.0f;
    scene->Ambient = 0.2f;
    scene->Reflection = 0.5f;
    scene->RotYAngle = 0;
    scene->RotSpeed = 5.0f;
    scene->Rotation = ROT_CCW;
    scene->ColorSpace = COLORSPACE_LINEAR;
    scene->objList = 0;
    scene->groundList = 0;
    scene->haloList = 0;
    scene->maxLights = 0;
    scene->lights = NULL;
    scene->lightsBar = NULL;
}

// Destructor
static void Scene_Destruct(Scene *scene)
{
    // free all lights
    if (scene->lights)
        free(scene->lights);
}

// Create the scene, and (re)initialize lights if changeLights is true
static void Scene_Init(Scene *scene, bool changeLights)
{
    // Get the max number of lights allowed by the graphic card
    glGetIntegerv(GL_MAX_LIGHTS, &scene->maxLights);
    if (scene->maxLights > 16)
        scene->maxLights = 16;

    // Create the lights array
    if (scene->lights == NULL && scene->maxLights > 0)
    {
        scene->lights = (Light *)malloc((size_t)scene->maxLights * sizeof(Light));
        scene->NumLights = 3;               // default number of lights
        if (scene->NumLights > scene->maxLights)
            scene->NumLights = scene->maxLights;
        changeLights = true;         // force lights initialization

        // Create a tweak bar for lights
        Scene_CreateBar(scene);
    }

    // (Re)initialize lights if needed (uses random values)
    if (changeLights)
        for (int i = 0; i < scene->maxLights; ++i)
        {
            scene->lights[i].Dist0     = 0.5f*(float)rand()/(float)RAND_MAX + 0.55f;
            scene->lights[i].Angle0    = 2*M_PI*((float)rand()/(float)RAND_MAX);
            scene->lights[i].Height0   = 2*M_PI*(float)rand()/(float)RAND_MAX;
            scene->lights[i].Speed0    = 4.0f*(float)rand()/(float)RAND_MAX - 2.0f;
            scene->lights[i].Animation = (LightAnimMode)(ANIM_BOUNCE + (rand()%3));
            scene->lights[i].Radius    = (float)rand()/(float)RAND_MAX+0.05f;
            scene->lights[i].Color[0]  = (float)rand()/(float)RAND_MAX;
            scene->lights[i].Color[1]  = (float)rand()/(float)RAND_MAX;
            scene->lights[i].Color[2]  = (scene->lights[i].Color[0]>scene->lights[i].Color[1]) ? 1.0f-scene->lights[i].Color[1] : 1.0f-scene->lights[i].Color[0];
            scene->lights[i].Color[3]  = 1;
            scene->lights[i].Active    = 1;
        }

    // Initialize some OpenGL states
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LIGHTING);
    glEnable(GL_CULL_FACE);
    glEnable(GL_NORMALIZE);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);

    // Create objects display list using the current Subdiv parameter to control the tesselation
    if (scene->objList > 0)
        glDeleteLists(scene->objList, 1);      // delete previously created display list
    scene->objList = glGenLists(1);
    glNewList(scene->objList, GL_COMPILE);
    DrawSubdivCylinderY(-0.9f, 0, -0.9f, 1.4f, 0.15f, 0.12f, scene->Subdiv/2+8, scene->Subdiv);
    DrawSubdivCylinderY(+0.9f, 0, -0.9f, 1.4f, 0.15f, 0.12f, scene->Subdiv/2+8, scene->Subdiv);
    DrawSubdivCylinderY(+0.9f, 0, +0.9f, 1.4f, 0.15f, 0.12f, scene->Subdiv/2+8, scene->Subdiv);
    DrawSubdivCylinderY(-0.9f, 0, +0.9f, 1.4f, 0.15f, 0.12f, scene->Subdiv/2+8, scene->Subdiv);
    DrawSubdivCylinderY(0, 0, 0, 0.4f, 0.5f, 0.3f, scene->Subdiv+16, scene->Subdiv/8+1);
    DrawSubdivCylinderY(0, 0.4f, 0, 0.05f, 0.3f, 0.0f, scene->Subdiv+16, scene->Subdiv/16+1);
    glEndList();

    // Create ground display list
    if (scene->groundList > 0)
        glDeleteLists(scene->groundList, 1);   // delete previously created display list
    scene->groundList = glGenLists(1);
    glNewList(scene->groundList, GL_COMPILE);
    DrawSubdivPlaneY(-1.2f, 1.2f, 0, -1.2f, 1.2f, (3*scene->Subdiv)/2, (3*scene->Subdiv)/2);
    glEndList();

    // Create display list to draw light halos
    if (scene->haloList > 0)
        glDeleteLists(scene->haloList, 1);     // delete previously created display list
    scene->haloList = glGenLists(1);
    glNewList(scene->haloList, GL_COMPILE);
    DrawSubdivHaloZ(0, 0, 0, 1, 32);
    glEndList();
}


// Left-drag orbits the camera, right-click recenters it, the wheel zooms.
// The hooks run only for events AntTweakBar did not consume, so a click or
// drag that lands on a tweak bar never moves the camera.
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
        g_cameraPosZ = 0; // Reset camera position
    }
}

// The camera-drag deltas deliberately stay in window/screen coordinates:
// normalizing by the window size makes them resolution independent.
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
    g_cameraPosZ -= (float)yoffset * 0.1f; // Zoom sensitivity
    if (g_cameraPosZ <-50.0f) g_cameraPosZ =-50.00f; // Prevent too close
    if (g_cameraPosZ > 50.0f) g_cameraPosZ = 50.0f; // Prevent too far
}

// Runs on every framebuffer resize, before TwWindowSize(). Size is in pixels.
static void resizeHook(GLFWwindow *window, int width, int height)
{
    (void)window;
    if (height == 0) height = 1;
    float aspect = (float)width / (float)height;

    // Projection matrix setup (equivalent to gluPerspective(40, aspect, 1, 10))
    float fovY = 40.0f;
    float near = 1.0f;
    float far = 10.0f;
    float top = tan(fovY * M_PI / 360.0f) * near;
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

// Callback function associated to the 'Change lights' button of the lights tweak bar.
void TW_CALL ReinitCB(void *clientData)
{
    Scene *scene = (Scene *)clientData; // scene pointer is stored in clientData
    Scene_Init(scene, true);            // re-initialize the scene
}


// Create a tweak bar for lights.
// New enum type and struct type are defined and used by this bar.
static void Scene_CreateBar(Scene *scene)
{
    // Create a new tweak bar and change its label, position and transparency
    scene->lightsBar = TwNewBar("Lights");
    TwDefine(" Lights label='Lights TweakBar' position='580 16' alpha=0 help='Use this bar to edit the lights in the scene.' ");
    // This bar has no explicit size='...' either, so - like 'Main' above -
    // its panel needs the same explicit HiDPI scaling of TwBar's fixed
    // 200x320 default (see the fontscaling comment in main()).
    {
        int lightsBarSize[2] = { (int)(200 * atb_glfw_ContentScaleX() + 0.5f), (int)(320 * atb_glfw_ContentScaleY() + 0.5f) };
        TwSetParam(scene->lightsBar, NULL, "size", TW_PARAM_INT32, 2, lightsBarSize);
    }

    // Add a variable of type int to control the number of lights
    TwAddVarRW(scene->lightsBar, "NumLights", TW_TYPE_INT32, &scene->NumLights,
               " label='Number of lights' keyIncr=l keyDecr=L help='Changes the number of lights in the scene.' ");

    // Set the NumLights min value (=0) and max value (depends on the user graphic card)
    int zero = 0;
    TwSetParam(scene->lightsBar, "NumLights", "min", TW_PARAM_INT32, 1, &zero);
    TwSetParam(scene->lightsBar, "NumLights", "max", TW_PARAM_INT32, 1, &scene->maxLights);
    // Note, TwDefine could also have been used for that pupose like this:
    //   char def[256];
    //   _snprintf(def, 255, "Lights/NumLights min=0 max=%d", scene->maxLights);
    //   TwDefine(def); // min and max are defined using a definition string


    // Add a button to re-initialize the lights; this button calls the ReinitCB callback function
    TwAddButton(scene->lightsBar, "Reinit", ReinitCB, scene,
                " label='Change lights' key=c help='Random changes of lights parameters.' ");

    // Define a new enum type for the tweak bar
    TwEnumVal modeEV[] = // array used to describe the LightAnimMode enum values
    {
        { ANIM_FIXED,    "Fixed"     },
        { ANIM_BOUNCE,   "Bounce"    },
        { ANIM_ROTATE,   "Rotate"    },
        { ANIM_COMBINED, "Combined"  }
    };
    TwType modeType = TwDefineEnum("Mode", modeEV, 4);  // create a new TwType associated to the enum defined by the modeEV array

    // Define a new struct type: light variables are embedded in this structure
    TwStructMember lightMembers[] = // array used to describe tweakable variables of the Light structure
    {
        { "Active",    TW_TYPE_BOOL32,  offsetof(Light, Active),    " help='Enable/disable the light.' " },   // Light::Active is bound as a plain 32-bit boolean
        { "Color",     TW_TYPE_COLOR4F, offsetof(Light, Color),     " coloralpha=false help='Light color.' " }, // Light::Color is represented by 4 floats, but alpha channel should be ignored
        { "Radius",    TW_TYPE_FLOAT,   offsetof(Light, Radius),    " min=0 max=4 step=0.02 help='Light radius.' " },
        { "Animation", modeType,        offsetof(Light, Animation), " help='Change the animation mode.' " },  // use the enum 'modeType' created before to tweak the Light::Animation variable
        { "Speed",     TW_TYPE_FLOAT,   offsetof(Light, Speed0),    " readonly=true help='Light moving speed.' " } // Light::Speed is made read-only
    };
    TwType lightType = TwDefineStruct("Light", lightMembers, 5, sizeof(Light), NULL, NULL);  // create a new TwType associated to the struct defined by the lightMembers array

    // Use the newly created 'lightType' to add variables associated with lights
    for (int i = 0; i < scene->maxLights; ++i)  // Add 'maxLights' variables of type lightType;
    {                               // unused lights variables (over NumLights) will hidden by Scene_Update()
        _snprintf(scene->lights[i].Name, sizeof(scene->lights[i].Name), "%d", i+1); // Create a name for each light ("1", "2", "3",...)
        TwAddVarRW(scene->lightsBar, scene->lights[i].Name, lightType, &scene->lights[i], " group='Edit lights' "); // Add a lightType variable and group it into the 'Edit lights' group

        // Set 'label' and 'help' parameters of the light
        char paramValue[64];
        _snprintf(paramValue, sizeof(paramValue), "Light #%d", i+1);
        TwSetParam(scene->lightsBar, scene->lights[i].Name, "label", TW_PARAM_CSTRING, 1, paramValue); // Set label
        _snprintf(paramValue, sizeof(paramValue), "Parameters of the light #%d", i+1);
        TwSetParam(scene->lightsBar, scene->lights[i].Name, "help", TW_PARAM_CSTRING, 1, paramValue);  // Set help

        // Note, parameters could also have been set using the define string of TwAddVarRW like this:
        //   char def[256];
        //   _snprintf(def, sizeof(def), "group='Edit lights' label='Light #%d' help='Parameters of the light #%d' ", i+1, i+1);
        //   TwAddVarRW(scene->lightsBar, scene->lights[i].Name, lightType, &scene->lights[i], def); // Add a lightType variable, group it into the 'Edit lights' group, and name it 'Light #n'
    }
}


// Move lights
static void Scene_Update(Scene *scene, double time)
{
    float horizSpeed, vertSpeed;
    for (int i = 0; i < scene->NumLights; ++i)
    {
        // Change light position according to its current animation mode

        if (scene->lights[i].Animation==ANIM_ROTATE || scene->lights[i].Animation==ANIM_COMBINED)
            horizSpeed = scene->lights[i].Speed0;
        else
            horizSpeed = 0;

        if (scene->lights[i].Animation==ANIM_BOUNCE || scene->lights[i].Animation==ANIM_COMBINED)
            vertSpeed = 1;
        else
            vertSpeed = 0;

        scene->lights[i].Pos[0] = scene->lights[i].Dist0 * (float)cos(horizSpeed*time + scene->lights[i].Angle0);
        scene->lights[i].Pos[1] = (float)fabs(cos(vertSpeed*time + scene->lights[i].Height0));
        scene->lights[i].Pos[2] = scene->lights[i].Dist0 * (float)sin(horizSpeed*time + scene->lights[i].Angle0);
        scene->lights[i].Pos[3] = 1;
    }
}


// Activate OpenGL lights; hide unused lights in the Lights tweak bar;
// and draw the scene. The scene is reflected by the ground plane, so it is
// drawn two times: first reflected, and second normal (unreflected).
static void Scene_Draw(const Scene *scene)
{
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    GLdouble eyeX = -0.3, eyeY = 1.5, eyeZ = 3;
    GLdouble centerX = 0.0, centerY = 0.0, centerZ = 0.0;
    GLdouble upX = 0.0, upY = 1.0, upZ = 0.0;

    GLdouble f[3] = {
        centerX - eyeX,
        centerY - eyeY,
        centerZ - eyeZ
    };
    GLdouble f_len = sqrt(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    f[0] /= f_len; f[1] /= f_len; f[2] /= f_len;

    GLdouble up[3] = { upX, upY, upZ };
    GLdouble up_len = sqrt(up[0]*up[0] + up[1]*up[1] + up[2]*up[2]);
    up[0] /= up_len; up[1] /= up_len; up[2] /= up_len;

    GLdouble s[3] = {
        f[1]*up[2] - f[2]*up[1],
        f[2]*up[0] - f[0]*up[2],
        f[0]*up[1] - f[1]*up[0]
    };
    GLdouble s_len = sqrt(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    s[0] /= s_len; s[1] /= s_len; s[2] /= s_len;

    GLdouble u[3] = {
        s[1]*f[2] - s[2]*f[1],
        s[2]*f[0] - s[0]*f[2],
        s[0]*f[1] - s[1]*f[0]
    };

    GLdouble m[16] = {
        s[0],  u[0], -f[0], 0.0,
        s[1],  u[1], -f[1], 0.0,
        s[2],  u[2], -f[2], 0.0,
        0.0,   0.0,   0.0, 1.0
    };
    glMultMatrixd(m);
    glTranslated(-eyeX, -eyeY, -eyeZ);
    glTranslated(g_cameraPosX, g_cameraPosY, -g_cameraPosZ);

    // Rotate the scene
    glRotated(scene->RotYAngle, 0, 1, 0);

    // Hide/active lights
    int i, lightVisible;
    for (i = 0; i < scene->maxLights; ++i)
    {
        if (i < scene->NumLights)
        {
            // Lights under NumLights are shown in the Lights tweak bar
            lightVisible = 1;

            // Tell OpenGL to enable or disable the light
            if (scene->lights[i].Active)
                glEnable(GL_LIGHT0+i);
            else
                glDisable(GL_LIGHT0+i);

            // Update OpenGL light parameters (for the reflected scene)
            float reflectPos[4] = { scene->lights[i].Pos[0], -scene->lights[i].Pos[1], scene->lights[i].Pos[2], scene->lights[i].Pos[3] };
            glLightfv(GL_LIGHT0+i, GL_POSITION, reflectPos);
            glLightfv(GL_LIGHT0+i, GL_DIFFUSE, scene->lights[i].Color);
            glLightf(GL_LIGHT0+i, GL_CONSTANT_ATTENUATION, 1);
            glLightf(GL_LIGHT0+i, GL_LINEAR_ATTENUATION, 0);
            glLightf(GL_LIGHT0+i, GL_QUADRATIC_ATTENUATION, 1.0f/(scene->lights[i].Radius*scene->lights[i].Radius));
        }
        else
        {
            // Lights over NumLights are hidden in the Lights tweak bar
            lightVisible = 0;

            // Disable the OpenGL light
            glDisable(GL_LIGHT0+i);

        }

        // Show or hide the light variable in the Lights tweak bar
        TwSetParam(scene->lightsBar, scene->lights[i].Name, "visible", TW_PARAM_INT32, 1, &lightVisible);
    }

    // Set global ambient and clear screen and depth buffer
    float ambient[4] = { scene->Ambient*(scene->BgColor0[0]+scene->BgColor1[0])/2, scene->Ambient*(scene->BgColor0[1]+scene->BgColor1[1])/2,
                         scene->Ambient*(scene->BgColor0[2]+scene->BgColor1[2])/2, 1 };
    glClearColor(ambient[0], ambient[1], ambient[2], 1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);

    // Draw the reflected scene
    glPolygonMode(GL_FRONT_AND_BACK, (scene->Wireframe ? GL_LINE : GL_FILL));
    glCullFace(GL_FRONT);
    glPushMatrix();
    glScalef(1, -1, 1);
    glColor3f(1, 1, 1);
    glCallList(scene->objList);
    Scene_DrawHalos(scene, true);
    glPopMatrix();
    glCullFace(GL_BACK);

    // clear depth buffer again
    glClear(GL_DEPTH_BUFFER_BIT);

    // Draw the ground plane (using the Reflection parameter as transparency)
    glColor4f(1, 1, 1, 1.0f-scene->Reflection);
    glCallList(scene->groundList);

    // Draw the gradient background (requires to switch to screen-space normalized coordinates)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glBegin(GL_QUADS);
        glColor3f(scene->BgColor0[0], scene->BgColor0[1], scene->BgColor0[2]);
        glVertex3f(-1, -1, 0.9f);
        glVertex3f(1, -1, 0.9f);
        glColor3f(scene->BgColor1[0], scene->BgColor1[1], scene->BgColor1[2]);
        glVertex3f(1, 1, 0.9f);
        glVertex3f(-1, 1, 0.9f);
    glEnd();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glEnable(GL_LIGHTING);

    // Update light positions for unreflected scene
    for (i = 0; i < scene->NumLights; ++i)
        glLightfv(GL_LIGHT0+i, GL_POSITION, scene->lights[i].Pos);

    // Draw the unreflected scene
    glPolygonMode(GL_FRONT_AND_BACK, (scene->Wireframe ? GL_LINE : GL_FILL));
    glColor3f(1, 1, 1);
    glCallList(scene->objList);
    Scene_DrawHalos(scene, false);
}


// Subroutine used to draw halos around light positions
static void Scene_DrawHalos(const Scene *scene, bool reflected)
{
    //glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    float prevAmbient[4];
    glGetFloatv(GL_LIGHT_MODEL_AMBIENT, prevAmbient);
    glPushMatrix();
    glLoadIdentity();
    if (reflected)
        glScalef(1, -1 ,1);
    float black[4] = {0, 0, 0, 1};
    float cr = (float)cos(2*M_PI*scene->RotYAngle/360.0f);
    float sr = (float)sin(2*M_PI*scene->RotYAngle/360.0f);
    for (int i = 0; i < scene->NumLights; ++i)
    {
        if (scene->lights[i].Active)
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, scene->lights[i].Color);
        else
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, black);
        glPushMatrix();
        glTranslatef(cr*scene->lights[i].Pos[0]+sr*scene->lights[i].Pos[2], scene->lights[i].Pos[1], -sr*scene->lights[i].Pos[0]+cr*scene->lights[i].Pos[2]);
        //glScalef(0.5f*lights[i].Radius, 0.5f*lights[i].Radius, 1);
        glScalef(0.05f, 0.05f, 1);
        glCallList(scene->haloList);
        glPopMatrix();
    }
    glPopMatrix();
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, prevAmbient);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}


// Subroutine used to build the ground plane display list (mesh subdivision is adjustable)
static void DrawSubdivPlaneY(float xMin, float xMax, float y, float zMin, float zMax, int xSubdiv, int zSubdiv)
{
    const float FLOAT_EPS = 1.0e-5f;
    float dx = (xMax-xMin)/xSubdiv;
    float dz = (zMax-zMin)/zSubdiv;
    glBegin(GL_QUADS);
    glNormal3f(0, -1, 0);
    for (float z=zMin; z<zMax-FLOAT_EPS; z+=dz)
        for (float x=xMin; x<xMax-FLOAT_EPS; x+=dx)
        {
            glVertex3f(x, y, z);
            glVertex3f(x, y, z+dz);
            glVertex3f(x+dx, y, z+dz);
            glVertex3f(x+dx, y, z);
        }
    glEnd();
}


// Subroutine used to build objects display list (mesh subdivision is adjustable)
static void DrawSubdivCylinderY(float xCenter, float yBottom, float zCenter, float height, float radiusBottom, float radiusTop, int sideSubdiv, int ySubdiv)
{
    float h0, h1, y0, y1, r0, r1, a0, a1, cosa0, sina0, cosa1, sina1;
    glBegin(GL_QUADS);
    glNormal3f(0, 1, 0);
    for (int j=0; j<ySubdiv; ++j)
        for (int i=0; i<sideSubdiv; ++i)
        {
            h0 = (float)j/ySubdiv;
            h1 = (float)(j+1)/ySubdiv;
            y0 = yBottom + h0*height;
            y1 = yBottom + h1*height;
            r0 = radiusBottom + h0*(radiusTop-radiusBottom);
            r1 = radiusBottom + h1*(radiusTop-radiusBottom);
            a0 = 2*M_PI*(float)i/sideSubdiv;
            a1 = 2*M_PI*(float)(i+1)/sideSubdiv;
            cosa0 = (float)cos(a0);
            sina0 = (float)sin(a0);
            cosa1 = (float)cos(a1);
            sina1 = (float)sin(a1);
            glNormal3f(cosa0, 0, sina0);
            glVertex3f(xCenter+r0*cosa0, y0, zCenter+r0*sina0);
            glNormal3f(cosa0, 0, sina0);
            glVertex3f(xCenter+r1*cosa0, y1, zCenter+r1*sina0);
            glNormal3f(cosa1, 0, sina1);
            glVertex3f(xCenter+r1*cosa1, y1, zCenter+r1*sina1);
            glNormal3f(cosa1, 0, sina1);
            glVertex3f(xCenter+r0*cosa1, y0, zCenter+r0*sina1);
        }
    glEnd();
}


// Subroutine used to build halo display list
static void DrawSubdivHaloZ(float x, float y, float z, float radius, int subdiv)
{
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0, 0, 0);
    glColor4f(1, 1, 1, 1);
    glVertex3f(x, y, z);
    for (int i=0; i<=subdiv; ++i)
    {
        glColor4f(1, 1, 1, 0);
        glVertex3f(x+radius*(float)cos(2*M_PI*(float)i/subdiv), x+radius*(float)sin(2*M_PI*(float)i/subdiv), z);
    }
    glEnd();
}


// Callback function called when the 'Subdiv' variable value of the main tweak bar has changed.
void TW_CALL SetSubdivCB(const void *value, void *clientData)
{
    Scene *scene = (Scene *)clientData;       // scene pointer is stored in clientData
    scene->Subdiv = *(const int *)value;      // copy value to scene->Subdiv
    Scene_Init(scene, false);                 // re-init scene with the new Subdiv parameter
}


// Callback function called by the main tweak bar to get the 'Subdiv' value
void TW_CALL GetSubdivCB(void *value, void *clientData)
{
    Scene *scene = (Scene *)clientData;  // scene pointer is stored in clientData
    *(int *)value = scene->Subdiv;       // copy scene->Subdiv to value
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


// Main function
int main(void)
{
    GLFWwindow* window; // GLFW3 window
    // Intialize GLFW
    if (!glfwInit()) {
        fprintf(stderr, "GLFW initialization failed\n");
        return 1;
    }

    // glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    // glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    // glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required on macOS

    // Create a window
    // Requested size is in "reference" (96 DPI) pixels; grow the actual
    // window to match the monitor's real pixel density on platforms where
    // window size and framebuffer size are otherwise always 1:1 (Windows,
    // X11) - a no-op on macOS, which already does this by definition (see
    // docs/plans/examples-hidpi-scaling.md).
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    window = glfwCreateWindow(800, 600, title, NULL, NULL);
    if (!window)
    {
        fprintf(stderr, "Cannot open GLFW window\n");
        glfwTerminate();
        return 1;
    }
    glfwSetWindowTitle(window, title);
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        return -1;
    }
    printf("OpenGL Version: %s\n", glGetString(GL_VERSION));
    glfwSwapInterval(0);

    // AntTweakBar has no DPI awareness, so scale its font by the window content
    // scale to keep a comparable physical size - that one parameter drives all
    // of its layout math (row height, button/slider size, ...). Must precede
    // TwInit(), which bakes the scale into the font atlases.
    //
    // Every bar's own panel size is a separate matter: TwBar's 200x320 default
    // is a fixed pixel constant that does NOT derive from font metrics, so it
    // has to be scaled explicitly too, via TwSetParam after each TwNewBar()
    // (see "Main" below and Scene_CreateBar()'s "Lights"), or the bigger
    // post-fontscaling rows/labels would get clipped by an unchanged panel.
    atb_glfw_SetFontScaling(window);

    // if (!TwInit(TW_OPENGL_CORE, NULL)) {
    if (!TwInit(TW_OPENGL, NULL)) {
        const char* err = TwGetLastError();
        fprintf(stderr, "TwInit failed: %s\n", err ? err : "Unknown error");
        fflush(stderr);
        return 1;
    }
    // Registers the GLFW callbacks, gives GLFW3 authoritative cursor
    // ownership, routes the clipboard through it, and applies the current
    // framebuffer size - see atb_glfw.h.
    {
        atb_glfw_Hooks hooks = { 0 };
        hooks.mouseButton = mouseButtonHook;
        hooks.cursorPos = cursorPosHook;
        hooks.scroll = scrollHook;
        hooks.resize = resizeHook;
        atb_glfw_Attach(window, &hooks);
    }
    // Change the font size, and add a global message to the Help bar.
    TwDefine(" GLOBAL fontSize=3 help='This example illustrates the definition of custom structure type as well as many other features.' ");

    // Initialize the 3D scene
    Scene scene;
    Scene_Construct(&scene);
    Scene_Init(&scene, true);

    // Create a tweak bar called 'Main' and change its refresh rate, position, size and transparency
    TwBar *mainBar = TwNewBar("Main");
    TwDefine(" Main label='Main TweakBar' refresh=0.5 position='16 16' alpha=0");
    // The bar's declared size must be scaled the same way fontscaling already
    // scaled its contents above, or the panel and its (now larger) widgets
    // would mismatch again on a HiDPI/Retina display - hence TwSetParam
    // instead of a literal size='260 320' in the TwDefine string above.
    {
        int mainBarSize[2] = { (int)(260 * atb_glfw_ContentScaleX() + 0.5f), (int)(320 * atb_glfw_ContentScaleY() + 0.5f) };
        TwSetParam(mainBar, NULL, "size", TW_PARAM_INT32, 2, mainBarSize);
    }

    // Add some variables to the Main tweak bar
    TwAddVarRW(mainBar, "Wireframe", TW_TYPE_BOOL32, &scene.Wireframe,
               " group='Display' key=w help='Toggle wireframe display mode.' "); // 'Wireframe' is put in the group 'Display' (which is then created)
    TwAddVarRW(mainBar, "BgTop", TW_TYPE_COLOR3F, &scene.BgColor1,
               " group='Background' help='Change the top background color.' ");  // 'BgTop' and 'BgBottom' are put in the group 'Background' (which is then created)
    TwAddVarRW(mainBar, "BgBottom", TW_TYPE_COLOR3F, &scene.BgColor0,
               " group='Background' help='Change the bottom background color.' ");
    // align_right demo: Red/Green/Blue (plain floats sharing 'BgTop' storage
    // above) and Mode (an enum) hug the right edge of the label column, unlike
    // every other widget in this bar, which keeps the default left alignment.
    // Mode's label is long on purpose, to exercise the "..." truncation path
    // that preserves the end of a right-aligned label instead of its start.
    TwAddVarRW(mainBar, "Red", TW_TYPE_FLOAT, &scene.BgColor1[0],
               " group='Background' align_right=true min=0 max=1 step=0.01 help='Top background color, red channel (right-aligned label demo).' ");
    TwAddVarRW(mainBar, "Green", TW_TYPE_FLOAT, &scene.BgColor1[1],
               " group='Background' align_right=true min=0 max=1 step=0.01 help='Top background color, green channel (right-aligned label demo).' ");
    TwAddVarRW(mainBar, "Blue", TW_TYPE_FLOAT, &scene.BgColor1[2],
               " group='Background' align_right=true min=0 max=1 step=0.01 help='Top background color, blue channel (right-aligned label demo).' ");
    TwEnumVal colorSpaceEV[] = { { COLORSPACE_LINEAR, "Linear"}, { COLORSPACE_SRGB, "sRGB" } };
    TwType colorSpaceType = TwDefineEnum("Color Space", colorSpaceEV, 2);
    TwAddVarRW(mainBar, "Mode", colorSpaceType, &scene.ColorSpace,
               " group='Background' align_right=true "
               " label='Background Color Space Rendering Mode' "
               " help='Background color space (right-aligned, deliberately long label to test left-side ellipsis truncation).' ");
    TwDefine(" Main/Background group='Display' ");  // The group 'Background' of bar 'Main' is put in the group 'Display'
    TwAddVarCB(mainBar, "Subdiv", TW_TYPE_INT32, SetSubdivCB, GetSubdivCB, &scene,
               " group='Scene' label='Meshes subdivision' min=1 max=50 keyincr=s keyDecr=S help='Subdivide the meshes more or less (switch to wireframe to see the effect).' ");
    TwAddVarRW(mainBar, "Ambient", TW_TYPE_FLOAT, &scene.Ambient,
               " label='Ambient factor' group='Scene' min=0 max=1 step=0.001 keyIncr=a keyDecr=A help='Change scene ambient.' ");
    TwAddVarRW(mainBar, "Reflection", TW_TYPE_FLOAT, &scene.Reflection,
               " label='Reflection factor' group='Scene' min=0 max=1 step=0.001 keyIncr=r keyDecr=R help='Change ground reflection.' ");

    // Create a new TwType called rotationType associated with the SceneRotMode enum, and use it
    TwEnumVal rotationEV[] = { { ROT_OFF, "Stopped"},
                               { ROT_CW,  "Clockwise" },
                               { ROT_CCW, "Counter-clockwise" } };
    TwType rotationType = TwDefineEnum( "Rotation Mode", rotationEV, 3 );
    TwAddVarRW(mainBar, "Rotation", rotationType, &scene.Rotation,
               " group='Scene' keyIncr=Backspace keyDecr=SHIFT+Backspace help='Stop or change the rotation mode.' ");
    TwAddVarRW(mainBar, "RotSpeed", TW_TYPE_FLOAT, &scene.RotSpeed,
               " label='Rot speed' group='Scene' min=0 max=90 step=1 help='Scene rotation speed, in degree/second.' ");

    // Add a read-only float variable; its precision is 0 which means that the fractionnal part of the float value will not be displayed
    TwAddVarRO(mainBar, "RotYAngle", TW_TYPE_DOUBLE, &scene.RotYAngle,
               " group='Scene' label='Rot angle (degree)' precision=0 help='Animated rotation angle' ");

    TwAddSeparator(mainBar, NULL, "");
    TwAddButton(mainBar, "FullWidthDemoMoreLines", FullWidthLinesCB, mainBar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(mainBar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");

    // Initialize time
    double time = glfwGetTime(), dt = 0;            // Current time and elapsed time
    double frameDTime = 0, frameCount = 0, fps = 0; // Framerate

    while (!glfwWindowShouldClose(window))
    {
        // Get elapsed time
        dt = glfwGetTime() - time;
        if (dt < 0) dt = 0;
        time += dt;

        // Rotate scene
        if (scene.Rotation==ROT_CW)
            scene.RotYAngle -= scene.RotSpeed*dt;
        else if (scene.Rotation==ROT_CCW)
            scene.RotYAngle += scene.RotSpeed*dt;

        // Move lights
        Scene_Update(&scene, time);

        // Draw scene
        Scene_Draw(&scene);

        // Draw tweak bar only
        TwDraw();

        glfwSwapBuffers(window);
        glfwPollEvents();

        // Estimate framerate
        frameCount++;
        frameDTime += dt;
        if (frameDTime>1.0)
        {
            fps = frameCount/frameDTime;
            char newTitle[128];
            _snprintf(newTitle, sizeof(newTitle), "%s (%.1f fps)", title, fps);
            //glfwSetWindowTitle(newTitle); // uncomment to display framerate
            frameCount = frameDTime = 0;
        }
    }

    // Terminate the scene, AntTweakBar, and GLFW
    Scene_Destruct(&scene);
    TwTerminate();
    atb_glfw_Detach(window);   // releases the cursors, after TwTerminate()
    glfwTerminate();

    return 0;
}
