//  ---------------------------------------------------------------------------
//
//  @file       Simple_raylib.c
//  @brief      A simple example that uses AntTweakBar with raylib.
//
//              Draws a lit, tweakable cube in a raylib window with one
//              AntTweakBar panel controlling it, and demonstrates the shape of
//              the raylib integration: raylib owns the window and the event
//              loop, atb_raylib.h polls its input state once a frame and feeds
//              AntTweakBar, and atb_raylib_Draw() puts the bars on top of the
//              raylib scene.
//
//              raylib's built-in shader is unlit, so every face of a solid
//              DrawCube() comes out the same flat colour and the shape reads
//              as a silhouette. This example loads a small directional-light
//              shader instead (see lightingVS/lightingFS below, compiled from
//              memory - no asset files), so the faces are shaded apart, and
//              overlays the edges as wires. Lighting is the one place raylib
//              expects you to bring your own shader.
//
//              Left-drag orbits the camera; the wheel zooms. Both are ignored
//              while the pointer is working a tweak bar.
//
//              Note that raylib is an OpenGL 3.3 core-profile renderer, so
//              TwInit() is given TW_OPENGL_CORE here, not TW_OPENGL.
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              raylib:      https://www.raylib.com
//
//  ---------------------------------------------------------------------------

#include "raylib.h"
#include "rlgl.h"
#include <AntTweakBar.h>
#include "atb_raylib.h"   // shared raylib <-> AntTweakBar glue for these examples
#include <math.h>
#include <stdio.h>

// GLSL 330: nob.c always builds raylib with -DGRAPHICS_API_OPENGL_33.
//
// raylib binds `vertexPosition`/`vertexNormal`/`vertexColor` and the `mvp`,
// `matModel` and `matNormal` uniforms by name on its own; `colDiffuse` carries
// the tint passed to DrawModelEx(). Only lightDir/viewPos/ambient/shininess
// below are this example's own, set through SetShaderValue().
static const char *lightingVS =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 matNormal;\n"
    "out vec3 fragPosition;\n"
    "out vec3 fragNormal;\n"
    "out vec4 fragColor;\n"
    "void main()\n"
    "{\n"
    "    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));\n"
    "    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 1.0)));\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0);\n"
    "}\n";

static const char *lightingFS =
    "#version 330\n"
    "in vec3 fragPosition;\n"
    "in vec3 fragNormal;\n"
    "in vec4 fragColor;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 lightDir;\n"
    "uniform vec3 viewPos;\n"
    "uniform float ambient;\n"
    "uniform float shininess;\n"
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    vec4 base = colDiffuse*fragColor;\n"
    "    vec3 n = normalize(fragNormal);\n"
    "    vec3 l = normalize(-lightDir);\n"
    "    float diff = max(dot(n, l), 0.0);\n"
    "    vec3 v = normalize(viewPos - fragPosition);\n"
    "    vec3 h = normalize(l + v);\n"
    "    float spec = (diff > 0.0) ? pow(max(dot(n, h), 0.0), 64.0)*shininess : 0.0;\n"
    "    vec3 lit = base.rgb*(ambient + (1.0 - ambient)*diff) + vec3(spec);\n"
    "    finalColor = vec4(lit, base.a);\n"
    "}\n";

// Draws a cube's 12 edges as thin boxes rather than as GL lines.
//
// DrawCubeWires() would be the obvious choice, but its thickness comes from
// glLineWidth, and an OpenGL 3.3 CORE profile is only required to support a
// width of 1.0 - macOS clamps there, so the control would silently do nothing
// on the very platform raylib puts you in a core context on. Boxes are a few
// more triangles and behave identically everywhere.
//
// These go through rlgl's immediate batch, so they are drawn with raylib's own
// unlit shader and stay one solid colour, instead of being shaded along with
// the faces the way DrawModelWiresEx() would.
static void DrawCubeEdges(float side, float thickness, Color color)
{
    const float h = side*0.5f;
    const float t = thickness;
    const float l = side + t;          // overshoot so the corners close up
    int i;

    for (i = 0; i < 4; ++i) {
        // The two signs pick one of the four parallel edges on each axis.
        float a = (i & 1) ? h : -h;
        float b = (i & 2) ? h : -h;
        DrawCubeV((Vector3){ 0, a, b }, (Vector3){ l, t, t }, color);  // along X
        DrawCubeV((Vector3){ a, 0, b }, (Vector3){ t, l, t }, color);  // along Y
        DrawCubeV((Vector3){ a, b, 0 }, (Vector3){ t, t, l }, color);  // along Z
    }
}

int main(void)
{
    // Tweakable state, all bound into the bar below.
    // The TW_TYPE_BOOL* variants differ only in the storage size they expect,
    // so a C99 `bool` (one byte) pairs with TW_TYPE_BOOL8. TW_TYPE_BOOLCPP is
    // declared only for C++ and is not an option here.
    bool      showFaces     = true;
    bool      showEdges     = true;
    float     rotationSpeed = 40.0f;         // degrees per second
    bool      autoRotate    = true;
    float     angle         = 0.0f;
    float     cubeSize      = 2.0f;
    Color     cubeColor     = { 255, 170, 0, 255 };
    Color     edgeColor     = { 20, 20, 28, 255 };
    float     edgeWidth     = 0.025f;   // fraction of the cube's side
    float     bgColor[3]    = { 0.11f, 0.11f, 0.17f };
    float     lightDir[3]   = { -0.5f, -0.8f, -0.4f };  // TW_TYPE_DIR3F keeps this normalized
    float     ambient       = 0.35f;  // keeps the faces turned away from the
                                      // light light enough for dark edges to read
    float     shininess     = 0.35f;

    // Camera orbit, driven by the mouse below.
    float camYaw = 35.0f, camPitch = 25.0f, camDist = 7.0f;
    bool  orbiting = false;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(900, 600, "AntTweakBar + raylib (Simple)");
    SetTargetFPS(60);

    // Must precede TwInit(), which bakes the scale into the font atlases.
    atb_raylib_SetFontScaling();

    // TW_OPENGL_CORE, not TW_OPENGL: raylib builds against the GL 3.3 core
    // profile, which has no fixed-function pipeline.
    if (!TwInit(TW_OPENGL_CORE, NULL)) {
        fprintf(stderr, "AntTweakBar initialization failed: %s\n", TwGetLastError());
        CloseWindow();
        return 1;
    }

    // Installs the cursor and clipboard callbacks and applies the current
    // render size - see atb_raylib.h.
    atb_raylib_Attach();

    Model  cube    = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    Shader shading = LoadShaderFromMemory(lightingVS, lightingFS);
    // raylib updates whichever location is stored in SHADER_LOC_VECTOR_VIEW
    // with the camera position every frame, so viewPos needs no manual update.
    shading.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shading, "viewPos");
    int locLightDir  = GetShaderLocation(shading, "lightDir");
    int locAmbient   = GetShaderLocation(shading, "ambient");
    int locShininess = GetShaderLocation(shading, "shininess");
    cube.materials[0].shader = shading;

    TwBar *bar = TwNewBar("TweakBar");
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with raylib.' ");
    {
        // TwBar's fixed 200x320 default does not grow with the now-larger
        // scaled contents, so it must be scaled explicitly too.
        int barSize[2] = { (int)(230 * atb_raylib_ContentScaleX() + 0.5f),
                           (int)(420 * atb_raylib_ContentScaleY() + 0.5f) };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }

    TwAddVarRW(bar, "faces", TW_TYPE_BOOL8, &showFaces,
               " label='Show faces' group=Cube key=f ");
    TwAddVarRW(bar, "edges", TW_TYPE_BOOL8, &showEdges,
               " label='Show edges' group=Cube key=e ");
    TwAddVarRW(bar, "size", TW_TYPE_FLOAT, &cubeSize,
               " label='Size' group=Cube min=0.5 max=5 step=0.1 ");
    TwAddVarRW(bar, "cubeColor", TW_TYPE_COLOR32, &cubeColor,
               " label='Face color' group=Cube coloralpha=true ");
    TwAddVarRW(bar, "edgeColor", TW_TYPE_COLOR32, &edgeColor,
               " label='Edge color' group=Cube ");
    TwAddVarRW(bar, "edgeWidth", TW_TYPE_FLOAT, &edgeWidth,
               " label='Edge width' group=Cube min=0 max=0.12 step=0.005 precision=3 "
               "help='Edge thickness, as a fraction of the cube side.' ");

    TwAddVarRW(bar, "autoRotate", TW_TYPE_BOOL8, &autoRotate,
               " label='Auto rotate' group=Motion key=space ");
    TwAddVarRW(bar, "speed", TW_TYPE_FLOAT, &rotationSpeed,
               " label='Speed' group=Motion min=0 max=360 step=1 keyIncr=s keyDecr=S "
               "help='Rotation speed (degrees/second).' ");
    TwAddVarRO(bar, "angle", TW_TYPE_FLOAT, &angle,
               " label='Angle' group=Motion precision=1 ");

    // TW_TYPE_DIR3F draws an arrow the light direction can be dragged around,
    // and keeps the bound vector normalized - exactly what the shader wants.
    TwAddVarRW(bar, "lightDir", TW_TYPE_DIR3F, lightDir,
               " label='Light dir' group=Lighting help='Drag the arrow to move the light.' ");
    TwAddVarRW(bar, "ambient", TW_TYPE_FLOAT, &ambient,
               " label='Ambient' group=Lighting min=0 max=1 step=0.01 "
               "help='How much the unlit faces still show.' ");
    TwAddVarRW(bar, "shininess", TW_TYPE_FLOAT, &shininess,
               " label='Specular' group=Lighting min=0 max=1 step=0.01 ");
    TwAddVarRW(bar, "bgColor", TW_TYPE_COLOR3F, bgColor,
               " label='Background' group=Lighting colormode=hls ");

    Camera3D camera = { 0 };
    camera.target     = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up         = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy       = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        // Feed AntTweakBar first; it reports whether it took this frame's
        // input, which is when the example leaves its own handling alone.
        bool guiTookInput = atb_raylib_Update();

        // Orbit the camera on a left-drag that did NOT start on a bar. The
        // flag latches the gesture: once a drag owns the camera it keeps it
        // until the button is released, so sweeping the pointer across a bar
        // mid-drag does not abandon the rotation half way.
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !guiTookInput) orbiting = true;
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) orbiting = false;
        if (orbiting) {
            Vector2 drag = GetMouseDelta();
            camYaw -= drag.x*0.4f;
            camPitch += drag.y*0.4f;
            if (camPitch >  89.0f) camPitch =  89.0f;  // never cross the pole,
            if (camPitch < -89.0f) camPitch = -89.0f;  // where `up` would flip
        }
        if (!guiTookInput) {
            camDist -= GetMouseWheelMove()*0.6f;
            if (camDist <  2.0f) camDist =  2.0f;
            if (camDist > 30.0f) camDist = 30.0f;
        }

        float yaw = camYaw*DEG2RAD, pitch = camPitch*DEG2RAD;
        camera.position = (Vector3){ camDist*cosf(pitch)*sinf(yaw),
                                     camDist*sinf(pitch),
                                     camDist*cosf(pitch)*cosf(yaw) };

        if (autoRotate) angle += rotationSpeed*GetFrameTime();
        if (angle >= 360.0f) angle -= 360.0f;

        SetShaderValue(shading, locLightDir,  lightDir,   SHADER_UNIFORM_VEC3);
        SetShaderValue(shading, locAmbient,   &ambient,   SHADER_UNIFORM_FLOAT);
        SetShaderValue(shading, locShininess, &shininess, SHADER_UNIFORM_FLOAT);

        BeginDrawing();
            ClearBackground((Color){ (unsigned char)(bgColor[0]*255),
                                     (unsigned char)(bgColor[1]*255),
                                     (unsigned char)(bgColor[2]*255), 255 });

            BeginMode3D(camera);
                Vector3 axis = { 0.4f, 1.0f, 0.2f };
                if (showFaces)
                    DrawModelEx(cube, (Vector3){ 0, 0, 0 }, axis, angle,
                                (Vector3){ cubeSize, cubeSize, cubeSize }, cubeColor);
                if (showEdges) {
                    // Drawn through rlgl's own (unlit) batch rather than
                    // DrawModelWiresEx, so the edges stay one solid colour
                    // instead of being shaded along with the faces. The 1.004
                    // margin lifts them clear of the faces they sit on, which
                    // would otherwise z-fight.
                    rlPushMatrix();
                        rlRotatef(angle, axis.x, axis.y, axis.z);
                        DrawCubeEdges(cubeSize, cubeSize*edgeWidth, edgeColor);
                    rlPopMatrix();
                }
                // DrawGrid() is fixed at y=0, which would slice through a cube
                // centred on the origin - push it down to read as a floor.
                rlPushMatrix();
                    rlTranslatef(0.0f, -3.0f, 0.0f);
                    DrawGrid(20, 0.5f);
                rlPopMatrix();
            EndMode3D();

            DrawText("Left-drag to orbit  -  wheel to zoom", 10,
                     GetScreenHeight() - 28, 20, RAYWHITE);

            // Last, so the bars land on top of the scene.
            atb_raylib_Draw();
        EndDrawing();
    }

    UnloadShader(shading);
    UnloadModel(cube);
    TwTerminate();
    atb_raylib_Detach();
    CloseWindow();
    return 0;
}
