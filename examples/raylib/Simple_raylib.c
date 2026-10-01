//  ---------------------------------------------------------------------------
//
//  @file       Simple_raylib.c
//  @brief      A simple example that uses AntTweakBar with raylib.
//
//              Draws a spinning, tweakable cube in a raylib window with one
//              AntTweakBar panel controlling it, and demonstrates the shape of
//              the raylib integration: raylib owns the window and the event
//              loop, atb_raylib.h polls its input state once a frame and feeds
//              AntTweakBar, and atb_raylib_Draw() puts the bars on top of the
//              raylib scene.
//
//              Note that raylib is an OpenGL 3.3 core-profile renderer, so
//              TwInit() is given TW_OPENGL_CORE here, not TW_OPENGL.
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              raylib:      https://www.raylib.com
//
//  ---------------------------------------------------------------------------

#include "raylib.h"
#include <AntTweakBar.h>
#include "atb_raylib.h"   // shared raylib <-> AntTweakBar glue for these examples
#include <stdio.h>

int main(void)
{
    // Tweakable state, all bound into the bar below.
    // The TW_TYPE_BOOL* variants differ only in the storage size they expect,
    // so a C99 `bool` (one byte) pairs with TW_TYPE_BOOL8. TW_TYPE_BOOLCPP is
    // declared only for C++ and is not an option here.
    float     rotationSpeed = 40.0f;         // degrees per second
    bool      wireframe     = false;
    Color     cubeColor     = { 255, 170, 0, 255 };
    float     cubeSize      = 2.0f;
    float     bgColor[3]    = { 0.11f, 0.11f, 0.17f };
    float     angle         = 0.0f;
    bool      autoRotate    = true;

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

    TwBar *bar = TwNewBar("TweakBar");
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with raylib.' ");
    {
        // TwBar's fixed 200x320 default does not grow with the now-larger
        // scaled contents, so it must be scaled explicitly too.
        int barSize[2] = { (int)(220 * atb_raylib_ContentScaleX() + 0.5f),
                           (int)(320 * atb_raylib_ContentScaleY() + 0.5f) };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }

    TwAddVarRW(bar, "autoRotate", TW_TYPE_BOOL8, &autoRotate,
               " label='Auto rotate' key=space help='Start/stop the cube spinning.' ");
    TwAddVarRW(bar, "speed", TW_TYPE_FLOAT, &rotationSpeed,
               " label='Rot speed' min=0 max=360 step=1 keyIncr=s keyDecr=S "
               "help='Rotation speed (degrees/second).' ");
    TwAddVarRW(bar, "size", TW_TYPE_FLOAT, &cubeSize,
               " label='Cube size' min=0.5 max=5 step=0.1 ");
    TwAddVarRW(bar, "wire", TW_TYPE_BOOL8, &wireframe,
               " label='Wireframe' key=w help='Toggle wireframe display mode.' ");

    TwAddSeparator(bar, NULL, "");
    TwAddVarRW(bar, "cubeColor", TW_TYPE_COLOR32, &cubeColor,
               " label='Cube color' coloralpha=true help='Color of the cube.' ");
    TwAddVarRW(bar, "bgColor", TW_TYPE_COLOR3F, bgColor,
               " label='Background' colormode=hls ");
    TwAddVarRO(bar, "angle", TW_TYPE_FLOAT, &angle,
               " label='Angle' precision=1 help='Current rotation, in degrees.' ");

    Camera3D camera = { 0 };
    camera.position   = (Vector3){ 6.0f, 5.0f, 6.0f };
    camera.target     = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up         = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy       = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        // Feed AntTweakBar first; it reports whether it took this frame's
        // input, which is when the example leaves its own handling alone.
        bool guiTookInput = atb_raylib_Update();
        if (!guiTookInput && IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            UpdateCamera(&camera, CAMERA_THIRD_PERSON);

        if (autoRotate) angle += rotationSpeed * GetFrameTime();
        if (angle >= 360.0f) angle -= 360.0f;

        BeginDrawing();
            ClearBackground((Color){ (unsigned char)(bgColor[0] * 255),
                                     (unsigned char)(bgColor[1] * 255),
                                     (unsigned char)(bgColor[2] * 255), 255 });

            BeginMode3D(camera);
                rlPushMatrix();
                rlRotatef(angle, 0.4f, 1.0f, 0.2f);
                if (wireframe) DrawCubeWires((Vector3){ 0, 0, 0 }, cubeSize, cubeSize, cubeSize, cubeColor);
                else           DrawCube((Vector3){ 0, 0, 0 }, cubeSize, cubeSize, cubeSize, cubeColor);
                rlPopMatrix();
                DrawGrid(10, 1.0f);
            EndMode3D();

            DrawText("Right-drag to orbit the camera", 10, GetScreenHeight() - 28, 20, RAYWHITE);

            // Last, so the bars land on top of the scene.
            atb_raylib_Draw();
        EndDrawing();
    }

    TwTerminate();
    atb_raylib_Detach();
    CloseWindow();
    return 0;
}
