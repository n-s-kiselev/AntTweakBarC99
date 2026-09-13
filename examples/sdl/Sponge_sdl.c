//  ---------------------------------------------------------------------------
//
//  @file       Sponge.c
//
//  @brief      Example that uses AntTweakBar with SDL3 and OpenGL. SDL3 port
//              of examples/glfw/Sponge.c - see docs/plans/sdl3-backend.md
//              for the backend adapter notes.
//
//              It draws a Menger sponge, aka Sierpinski cube:
//              http://en.wikipedia.org/wiki/Menger_sponge .
//
//              Cubes shading is augmented with some simple ambient occlusion
//              applied by subdividing each cube face into a 3x3 grid.
//              AntTweakBar is used to add some interactive controls,
//              including an interactive quaternion rotation widget.
//
//              The sponge mesh-generation math is unchanged from the GLFW3
//              example - only the windowing/event integration was ported to
//              SDL3, using the same pattern established in
//              examples/sdl/Triangle.c.
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              SDL:         https://www.libsdl.org
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <AntTweakBar.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// ----------------------------------------------------------------------
// SDL3 cursor binding (see examples/sdl/Triangle.c for the full pattern
// this mirrors): SDL3 cursors are process-global (SDL_SetCursor() takes no
// window argument, unlike glfwSetCursor()), so there is no per-window
// cursor-ownership fight to route around the way the GLFW3 example's own
// comment describes - this is a plain cache, not a GLFW3-style shim.
// ----------------------------------------------------------------------

static SDL_Cursor *g_StandardCursors[TW_CURSOR_CUSTOM] = { NULL };
static SDL_Cursor *g_LastCustomCursor = NULL;
static bool g_CursorHidden = false;

static SDL_SystemCursor SDLStandardCursorShape(ETwCursor _Cursor)
{
    switch (_Cursor) {
    case TW_CURSOR_ARROW:        return SDL_SYSTEM_CURSOR_DEFAULT;
    case TW_CURSOR_MOVE:         return SDL_SYSTEM_CURSOR_MOVE;
    case TW_CURSOR_RESIZE_WE:    return SDL_SYSTEM_CURSOR_EW_RESIZE;
    case TW_CURSOR_RESIZE_NS:    return SDL_SYSTEM_CURSOR_NS_RESIZE;
    case TW_CURSOR_RESIZE_NESW:  return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    case TW_CURSOR_RESIZE_NWSE:  return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    case TW_CURSOR_HAND:         return SDL_SYSTEM_CURSOR_POINTER;
    case TW_CURSOR_CROSS:        return SDL_SYSTEM_CURSOR_CROSSHAIR;
    case TW_CURSOR_IBEAM:        return SDL_SYSTEM_CURSOR_TEXT;
    case TW_CURSOR_NO:           return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    default:                     return SDL_SYSTEM_CURSOR_DEFAULT; // TW_CURSOR_HELP/UPARROW: no dedicated SDL shape
    }
}

// SDL_GetClipboardText() allocates and must be freed (SDL_free) - unlike
// GLFW's glfwGetClipboardString(), which owns its own buffer. Freeing the
// *previous* call's result (not the one just returned) keeps the pointer
// valid for however long AntTweakBar needs it after this call returns.
static char *g_ClipboardText = NULL;

static const char * TW_CALL ClipboardGetSDL(void *_ClientData)
{
    (void)_ClientData;
    if (g_ClipboardText != NULL) SDL_free(g_ClipboardText);
    g_ClipboardText = SDL_GetClipboardText();
    return g_ClipboardText;
}

static void TW_CALL ClipboardSetSDL(const char *_Text, void *_ClientData)
{
    (void)_ClientData;
    SDL_SetClipboardText(_Text);
}

static void TW_CALL SDLCursorCB(ETwCursor _Cursor, const unsigned char *_RGBA32x32, int _HotX, int _HotY, void *_ClientData)
{
    (void)_ClientData;
    // TW_CURSOR_HIDDEN is an input mode, not a cursor shape: the roto slider
    // hides the pointer while it is dragged. g_CursorHidden remembers that so
    // the mode is restored once, on the next request for a visible cursor.
    if (_Cursor == TW_CURSOR_HIDDEN) {
        SDL_HideCursor();
        g_CursorHidden = true;
        return;
    }
    if (g_CursorHidden) {
        SDL_ShowCursor();
        g_CursorHidden = false;
    }
    if (_Cursor == TW_CURSOR_CUSTOM && _RGBA32x32 != NULL) {
        // SDL_PIXELFORMAT_RGBA32 is the byte-order-independent alias for
        // 8-bit-per-channel RGBA (see SDL_pixels.h) - matches _RGBA32x32's
        // own byte layout exactly, no channel reordering needed.
        SDL_Surface *surface = SDL_CreateSurfaceFrom(32, 32, SDL_PIXELFORMAT_RGBA32,
                                                      (void *)_RGBA32x32, 32 * 4);
        if (surface != NULL) {
            SDL_Cursor *cur = SDL_CreateColorCursor(surface, _HotX, _HotY);
            SDL_DestroySurface(surface); // SDL_CreateColorCursor copies the pixels
            if (cur != NULL) {
                // Set the new cursor before destroying the old one: some
                // platforms reset to the default arrow when the current
                // cursor is destroyed - same precaution as the GLFW3
                // example's own GLFWCursorCB.
                SDL_SetCursor(cur);
                if (g_LastCustomCursor != NULL) SDL_DestroyCursor(g_LastCustomCursor);
                g_LastCustomCursor = cur;
            }
        }
        return;
    }
    if (g_StandardCursors[_Cursor] == NULL)
        g_StandardCursors[_Cursor] = SDL_CreateSystemCursor(SDLStandardCursorShape(_Cursor));
    if (g_StandardCursors[_Cursor] != NULL)
        SDL_SetCursor(g_StandardCursors[_Cursor]);
}

static void DestroySDLCursorCache(void)
{
    for (int i = 0; i < TW_CURSOR_CUSTOM; ++i) {
        if (g_StandardCursors[i] != NULL) {
            SDL_DestroyCursor(g_StandardCursors[i]);
            g_StandardCursors[i] = NULL;
        }
    }
    if (g_LastCustomCursor != NULL) {
        SDL_DestroyCursor(g_LastCustomCursor);
        g_LastCustomCursor = NULL;
    }
    if (g_ClipboardText != NULL) {
        SDL_free(g_ClipboardText);
        g_ClipboardText = NULL;
    }
}

// ----------------------------------------------------------------------
// Geometry data structures and portable math (unchanged from the GLFW3
// example - this is the actual sponge demo and has nothing to do with the
// windowing backend)
// ----------------------------------------------------------------------

typedef struct { float v[3]; } Vector3;
static const Vector3 VECTOR3_ZERO = { { 0, 0, 0 } };

typedef struct { float m[4][4]; } Matrix4x4;
static const Matrix4x4 MATRIX4X4_IDENTITY = { { {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1} } };

typedef struct { float q[4]; } Quaternion;

static const float FLOAT_PI = 3.14159265f;

typedef struct
{
    Vector3 Position;
    Vector3 Normal;
    unsigned int AmbientColor; // R,G,B,A bytes at increasing addresses (matches DXGI_FORMAT_R8G8B8A8_UNORM)
} Vertex;

// A small growable array, replacing std::vector<Vertex>/std::vector<unsigned
// int> for the sponge's vertex/index buffers.
typedef struct { Vertex *items; size_t count, capacity; } VertexArray;
typedef struct { unsigned int *items; size_t count, capacity; } IndexArray;

static void VertexArray_Push(VertexArray *arr, Vertex v)
{
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity ? arr->capacity * 2 : 256;
        arr->items = (Vertex *)realloc(arr->items, arr->capacity * sizeof(Vertex));
    }
    arr->items[arr->count++] = v;
}

static void IndexArray_Push(IndexArray *arr, unsigned int i)
{
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity ? arr->capacity * 2 : 256;
        arr->items = (unsigned int *)realloc(arr->items, arr->capacity * sizeof(unsigned int));
    }
    arr->items[arr->count++] = i;
}

// Each cube face is split into a 3x3 grid
static const int CUBE_FACE_TRIANGLE_COUNT = 2 * 3 * 3; // 18 triangles to be drawn for each face
// Faces color of the sponge wrt to recursion level
static const unsigned int COLORS[] = { 0xffffffff, 0xff007fff, 0xff7fff00, 0xffff007f, 0xff0000ff, 0xff00ff00, 0xffff0000 };


// Scene globals
static Quaternion g_SpongeRotation;          // model rotation, set in main()
static int g_SpongeLevel = 2;                // number of recursions
static int g_SpongeAO = 1;                   // apply ambient occlusion (TW_TYPE_BOOL32-bound: must be int, not bool)
static unsigned int g_SpongeIndicesCount = 0;// set by BuildSponge
static Vector3 g_LightDir = { { -0.5f, -0.2f, 1 } }; // direction from the sponge to the light source
static float g_CamDistance = 0.7f;           // camera distance
static float g_BackgroundColor[] = {0, 0, 0.5f, 1}; // background color
static int g_Animate = 1;                    // enable animation (TW_TYPE_BOOL32-bound: must be int, not bool)
static float g_AnimationSpeed = 0.2f;        // animation speed


// Some math operators and functions (operator overloads become named functions in C99).
static Vector3 Vector3_Add(Vector3 a, Vector3 b)
{
    Vector3 out;
    out.v[0] = a.v[0] + b.v[0];
    out.v[1] = a.v[1] + b.v[1];
    out.v[2] = a.v[2] + b.v[2];
    return out;
}

static Vector3 Vector3_Scale(float s, Vector3 a)
{
    Vector3 out;
    out.v[0] = s * a.v[0];
    out.v[1] = s * a.v[1];
    out.v[2] = s * a.v[2];
    return out;
}

static float Length(Vector3 a)
{
    return (float)sqrt(a.v[0]*a.v[0] + a.v[1]*a.v[1] + a.v[2]*a.v[2]);
}

static Matrix4x4 Translation(Vector3 t)
{
    Matrix4x4 out = MATRIX4X4_IDENTITY;
    out.m[3][0] = t.v[0];
    out.m[3][1] = t.v[1];
    out.m[3][2] = t.v[2];
    return out;
}

static Matrix4x4 Scale(float s)
{
    Matrix4x4 out = MATRIX4X4_IDENTITY;
    out.m[0][0] = out.m[1][1] = out.m[2][2] = s;
    return out;
}

static Matrix4x4 Matrix4x4_Mul(Matrix4x4 a, Matrix4x4 b)
{
    Matrix4x4 out = MATRIX4X4_IDENTITY;
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            out.m[i][j] = a.m[i][0]*b.m[0][j] + a.m[i][1]*b.m[1][j] + a.m[i][2]*b.m[2][j] + a.m[i][3]*b.m[3][j];
    return out;
}

static Vector3 Vector3_Transform(Vector3 p, Matrix4x4 a)
{
    Vector3 out;
    float rw = 1.f / (p.v[0]*a.m[0][3] + p.v[1]*a.m[1][3] + p.v[2]*a.m[2][3] + a.m[3][3]);
    out.v[0] = rw  * (p.v[0]*a.m[0][0] + p.v[1]*a.m[1][0] + p.v[2]*a.m[2][0] + a.m[3][0]);
    out.v[1] = rw  * (p.v[0]*a.m[0][1] + p.v[1]*a.m[1][1] + p.v[2]*a.m[2][1] + a.m[3][1]);
    out.v[2] = rw  * (p.v[0]*a.m[0][2] + p.v[1]*a.m[1][2] + p.v[2]*a.m[2][2] + a.m[3][2]);
    return out;
}

static Quaternion RotationFromAxisAngle(Vector3 axis, float angle)
{
    Quaternion out;
    float norm = Length(axis);
    float sina2 = (float)sin(0.5f * angle);
    out.q[0] = sina2 * axis.v[0] / norm;
    out.q[1] = sina2 * axis.v[1] / norm;
    out.q[2] = sina2 * axis.v[2] / norm;
    out.q[3] = (float)cos(0.5f * angle);
    return out;
}

static void AxisAngleFromRotation(Vector3 *outAxis, float *outAngle, Quaternion quat)
{
    float sina2 = (float)sqrt(quat.q[0]*quat.q[0] + quat.q[1]*quat.q[1] + quat.q[2]*quat.q[2]);
    *outAngle = 2.0f * (float)atan2(sina2, quat.q[3]);
    float r = (sina2 > 0) ? (1.0f / sina2) : 0;
    outAxis->v[0] = r * quat.q[0];
    outAxis->v[1] = r * quat.q[1];
    outAxis->v[2] = r * quat.q[2];
}

// DARKEN scales one 8-bit color channel by s (clamped); DARKEN_COLOR does
// the same to all three channels of an opaque 0xAARRGGBB color. Used below
// to shade the ambient-occluded parts of a cube face.
#define DARKEN(r, s) ( (unsigned int)((float)(r)*(s)) > 255 ? 255 : (unsigned int)((float)(r)*(s)) )
#define DARKEN_COLOR(c, s) ( 0xff000000 | (DARKEN(((c)>>16)&0xff, s)<<16) | (DARKEN(((c)>>8)&0xff, s)<<8) | DARKEN((c)&0xff, s) )

// Append vertices and indices of a cube to the index and vertex buffers.
// The cube has gradient ambient-occlusion defined per edge.
static void AppendCubeToBuffers(VertexArray *vertices, IndexArray *indices,
                                 Matrix4x4 xform, float aoRatio, const bool aoEdges[12],
                                 const unsigned int faceColors[6])
{
    unsigned int indicesOffset = (unsigned int)vertices->count;

    const float R = 0.5f; // unit cube radius
    const Vector3 A[6] = { {{-R, -R, -R}}, {{+R, -R, -R}}, {{+R, -R, +R}}, {{-R, -R, +R}}, {{-R, +R, -R}}, {{-R, -R, -R}} };
    const Vector3 B[6] = { {{+R, -R, -R}}, {{+R, -R, +R}}, {{-R, -R, +R}}, {{-R, -R, -R}}, {{+R, +R, -R}}, {{+R, -R, -R}} };
    const Vector3 C[6] = { {{-R, +R, -R}}, {{+R, +R, -R}}, {{+R, +R, +R}}, {{-R, +R, +R}}, {{-R, +R, +R}}, {{-R, -R, +R}} };
    const Vector3 D[6] = { {{+R, +R, -R}}, {{+R, +R, +R}}, {{-R, +R, +R}}, {{-R, +R, -R}}, {{+R, +R, +R}}, {{+R, -R, +R}} };
    const Vector3 N[6] = { {{ 0,  0, -1}}, {{+1,  0,  0}}, {{ 0,  0, +1}}, {{-1,  0,  0}}, {{ 0, +1,  0}}, {{ 0, -1,  0}} };
    const int E[6][4] = { {0, 1, 2, 3}, {8, 7, 9, 1}, {4, 5, 6, 7}, {11, 3, 10, 5}, {2, 9, 6, 10}, {0, 8, 4, 11} };

    int face, i, j;
    float u, v;
    bool ao;
    Vertex vertex;
    for (face = 0; face < 6; face++)
        for (j = 0; j < 4; j++)
        {
            v = (j == 1) ? aoRatio : ((j == 2) ? 1.0f - aoRatio : j/3.0f);
            for (i = 0; i < 4; i++)
            {
                u = (i == 1) ? aoRatio : ((i == 2) ? 1.0f - aoRatio : i/3.0f);

                vertex.Position = Vector3_Add(
                    Vector3_Scale(1.0f - v, Vector3_Add(Vector3_Scale(1.0f - u, A[face]), Vector3_Scale(u, B[face]))),
                    Vector3_Scale(v, Vector3_Add(Vector3_Scale(1.0f - u, C[face]), Vector3_Scale(u, D[face]))));
                vertex.Position = Vector3_Transform(vertex.Position, xform);

                vertex.Normal = N[face];

                ao  = (j == 0) && aoEdges[E[face][0]];
                ao |= (i == 3) && aoEdges[E[face][1]];
                ao |= (j == 3) && aoEdges[E[face][2]];
                ao |= (i == 0) && aoEdges[E[face][3]];

                vertex.AmbientColor = ao ? DARKEN_COLOR(faceColors[face], 0.75f) : faceColors[face];

                VertexArray_Push(vertices, vertex);
            }
        }

    const unsigned short I[/*CUBE_FACE_TRIANGLE_COUNT*/18][3] =
    {
        {0, 5, 4}, {0, 1, 5},  {1, 6, 5}, {1, 2, 6},  {3, 6, 2}, {3, 7, 6},
        {4, 9, 8}, {4, 5, 9},  {5, 10, 9}, {5, 6, 10},  {6, 11, 10}, {6, 7, 11},
        {8, 9, 12}, {9, 13, 12},  {9, 14, 13}, {9, 10, 14},  {10, 15, 14}, {10, 11, 15}
    };
    int tri;
    for (face = 0; face < 6; face++)
        for (tri = 0; tri < CUBE_FACE_TRIANGLE_COUNT; tri++)
            for (i = 0; i < 3; i++)
                IndexArray_Push(indices, indicesOffset + I[tri][i] + 16*face); // 16 vertices per face
}

// Replaces the original's local "struct Local { static void ApplyAO(...) }"
// (a local class with a static method has no C99 equivalent) with a plain
// file-scope static function, taking pointers instead of C++ references.
static void ApplyAO(int i, int j, bool *e0, bool *e1, bool *e2, bool *e3)
{
    if (i == -1 && j == 0) *e0 = *e1 = true;
    if (i == +1 && j <= 0) *e1 = false;
    if (i == +1 && j >= 0) *e0 = false;

    if (i == +1 && j == 0) *e2 = *e3 = true;
    if (i == -1 && j <= 0) *e2 = false;
    if (i == -1 && j >= 0) *e3 = false;

    if (j == -1 && i == 0) *e1 = *e2 = true;
    if (j == +1 && i <= 0) *e1 = false;
    if (j == +1 && i >= 0) *e2 = false;

    if (j == +1 && i == 0) *e0 = *e3 = true;
    if (j == -1 && i <= 0) *e0 = false;
    if (j == -1 && i >= 0) *e3 = false;
}

// Recursive function called to fill the vertex and index buffers with the cubes forming the Menger sponge.
static void FillSpongeBuffers(int level, int levelMax, VertexArray *vertices, IndexArray *indices,
                               Vector3 center, bool aoEnabled, const bool aoEdges[12], const unsigned int faceColors[6])
{
    float scale = (float)pow(1.0f/3.0f, level);

    if (level == levelMax)
    {
        float aoRatio = (float)pow(3.0f, level) * 0.02f;
        if (aoRatio > 0.4999f)
            aoRatio = 0.4999f;
        Matrix4x4 xform = Matrix4x4_Mul(Scale(scale), Translation(center));
        AppendCubeToBuffers(vertices, indices, xform, aoRatio, aoEdges, faceColors);
    }
    else
    {
        bool aoEdgesCopy[12];
        unsigned int faceColorsCopy[6];
        int i, j, k, l;
        for (i = -1; i <= 1; i++)
            for (j = -1; j <= 1; j++)
                for (k = -1; k <= 1; k++)
                    if ( !( (i == 0 && j == 0) || (i == 0 && k == 0) || (j == 0 && k == 0) ) )
                    {
                        float s = 1.0f/3.0f * scale;
                        Vector3 t = { { center.v[0] + s * i, center.v[1] + s * j, center.v[2] + s * k } };

                        for (l = 0; l < 12; l++)
                            aoEdgesCopy[l] = aoEdges[l];
                        if (aoEnabled)
                        {
                            ApplyAO( i, j, &aoEdgesCopy[8], &aoEdgesCopy[9], &aoEdgesCopy[10], &aoEdgesCopy[11]); // z direction
                            ApplyAO( i, k, &aoEdgesCopy[1], &aoEdgesCopy[7], &aoEdgesCopy[5],  &aoEdgesCopy[3] ); // y direction
                            ApplyAO(-k, j, &aoEdgesCopy[0], &aoEdgesCopy[2], &aoEdgesCopy[6],  &aoEdgesCopy[4] ); // x direction
                        }

                        for (l = 0; l < 6; l++)
                            faceColorsCopy[l] = faceColors[l];
                        if (k == +1) faceColorsCopy[0] = COLORS[level+1];
                        if (i == -1) faceColorsCopy[1] = COLORS[level+1];
                        if (k == -1) faceColorsCopy[2] = COLORS[level+1];
                        if (i == +1) faceColorsCopy[3] = COLORS[level+1];
                        if (j == -1) faceColorsCopy[4] = COLORS[level+1];
                        if (j == +1) faceColorsCopy[5] = COLORS[level+1];

                        FillSpongeBuffers(level + 1, levelMax, vertices, indices, t, aoEnabled, aoEdgesCopy, faceColorsCopy);
                    }
    }
}


// ----------------------------------------------------------------------
// OpenGL rendering (fixed-function pipeline - this example's AntTweakBar
// build does not support the OpenGL Core Profile)
// ----------------------------------------------------------------------

static int g_Width = 640, g_Height = 480;

// SDL always reports mouse position in window points, but TwWindowSize()
// is fed the pixel size (see handleWindowPixelSizeChanged), so mouse
// events must be scaled by this window/pixel ratio before reaching
// AntTweakBar - see docs/plans/examples-hidpi-scaling.md.
static double g_MouseScaleX = 1.0, g_MouseScaleY = 1.0;
static VertexArray g_Vertices = {0};
static IndexArray g_Indices = {0};

static void InitRenderStates(void)
{
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    // Mimic the original shader's "(1-lightCoeff) + lightCoeff*NdotL" mix
    // (lightCoeff=0.85) with fixed-function lighting: a small constant
    // ambient term plus a dominant per-vertex diffuse term.
    GLfloat globalAmbient[] = { 0.15f, 0.15f, 0.15f, 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, globalAmbient);
    GLfloat lightAmbient[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    GLfloat lightDiffuse[] = { 0.85f, 0.85f, 0.85f, 1.0f };
    glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmbient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiffuse);
}

// Build sponge vertices/indices (plain client-side arrays, since this
// AntTweakBar build doesn't support the OpenGL Core Profile buffer/shader
// pipeline). Reuses previously-allocated array storage across rebuilds.
static void BuildSponge(int levelMax, bool aoEnabled)
{
    g_Vertices.count = 0;
    g_Indices.count = 0;
    bool aoEdges[12] = { false, false, false, false, false, false, false, false, false, false, false, false };
    unsigned int faceColors[6] = { COLORS[0], COLORS[0], COLORS[0], COLORS[0], COLORS[0], COLORS[0] };
    FillSpongeBuffers(0, levelMax, &g_Vertices, &g_Indices, VECTOR3_ZERO, aoEnabled, aoEdges, faceColors);

    g_SpongeIndicesCount = (unsigned int)g_Indices.count;
}

static void DrawSponge(void)
{
    if (g_SpongeIndicesCount == 0) return;

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &g_Vertices.items[0].Position);
    glNormalPointer(GL_FLOAT, sizeof(Vertex), &g_Vertices.items[0].Normal);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &g_Vertices.items[0].AmbientColor);

    glDrawElements(GL_TRIANGLES, (GLsizei)g_SpongeIndicesCount, GL_UNSIGNED_INT, g_Indices.items);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
}

// Callback functions called by AntTweakBar to set/get the sponge recursion level and AO flag
void TW_CALL SetSpongeLevelCB(const void *value, void *clientData)
{
    (void)clientData;
    g_SpongeLevel = *(const int *)value;
    BuildSponge(g_SpongeLevel, g_SpongeAO);
}
void TW_CALL GetSpongeLevelCB(void *value, void *clientData)
{
    (void)clientData;
    *(int *)value = g_SpongeLevel;
}
void TW_CALL SetSpongeAOCB(const void *value, void *clientData)
{
    (void)clientData;
    g_SpongeAO = *(const int *)value;
    BuildSponge(g_SpongeLevel, g_SpongeAO);
}
void TW_CALL GetSpongeAOCB(void *value, void *clientData)
{
    (void)clientData;
    *(int *)value = g_SpongeAO;
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

static void Render(void)
{
    glClearColor(g_BackgroundColor[0], g_BackgroundColor[1], g_BackgroundColor[2], g_BackgroundColor[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspectRatio = (float)g_Width / (float)g_Height;
    float near = 0.1f, far = 100.0f;
    float top = (float)tan(FLOAT_PI/8.0f) * near; // half of a FLOAT_PI/4 (45 degree) vertical FOV
    float right = top * aspectRatio;
    glFrustum(-right, right, -top, top, near, far);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // The original Direct3D projection is left-handed and looks toward +Z.
    // OpenGL's compatibility projection looks toward -Z, so preserve the
    // original horizontal offset but reverse its depth translation.
    float dist = g_CamDistance + 0.4f;
    Vector3 camPosInv = { { dist * 0.3f, dist * 0.0f, dist * 2.0f } };
    glTranslatef(camPosInv.v[0], camPosInv.v[1], -camPosInv.v[2]);

    // Light direction is fixed in view space (doesn't rotate with the sponge),
    // so it's set before applying the sponge's own rotation below. g_LightDir
    // is the direction TO the light source (matching GL_LIGHT0's own
    // w=0 "direction to light" convention for GL_POSITION), so it's used
    // directly here with no negation - unlike the original TwSimpleDX11.cpp
    // shader, whose LightDir instead means "direction the light travels".
    Vector3 lightDirNorm = Vector3_Scale(1.0f / Length(g_LightDir), g_LightDir);
    GLfloat lightPos[4] = { lightDirNorm.v[0], lightDirNorm.v[1], lightDirNorm.v[2], 0.0f }; // directional light
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);

    Vector3 axis = VECTOR3_ZERO;
    float angle = 0;
    AxisAngleFromRotation(&axis, &angle, g_SpongeRotation);
    glRotatef(angle * 180.0f / FLOAT_PI, axis.v[0], axis.v[1], axis.v[2]);

    DrawSponge();

    TwDraw();
}

// Rotating sponge. Replaces glfwGetTime() with SDL_GetTicksNS()/1e9 - same
// contract (monotonically increasing seconds, arbitrary epoch, only
// differences matter).
static void Anim(void)
{
    static double s_PrevTime = 0;
    double time = (double)SDL_GetTicksNS() / 1e9;
    float dt = (float)(time - s_PrevTime);
    if (g_Animate && dt > 0 && dt < 0.2f)
    {
        Vector3 axis = VECTOR3_ZERO;
        float angle = 0;
        AxisAngleFromRotation(&axis, &angle, g_SpongeRotation);
        if (Length(axis) < 1.0e-6f)
            axis.v[1] = 1;
        angle += g_AnimationSpeed * dt;
        if (angle >= 2.0f*FLOAT_PI)
            angle -= 2.0f*FLOAT_PI;
        else if (angle <= 0)
            angle += 2.0f*FLOAT_PI;
        g_SpongeRotation = RotationFromAxisAngle(axis, angle);
    }
    s_PrevTime = time;
}

// ----------------------------------------------------------------------
// SDL3 event handling (same pattern as examples/sdl/Triangle.c)
// ----------------------------------------------------------------------

static void handleKeyDown(const SDL_KeyboardEvent *_Event, bool *_Running)
{
    if (_Event->key == SDLK_ESCAPE) {
        *_Running = false;
        return;
    }

    int twMod = 0;
    if (_Event->mod & SDL_KMOD_SHIFT) twMod |= TW_KMOD_SHIFT;
    bool ctrl = (_Event->mod & SDL_KMOD_CTRL) != 0;
    if (ctrl) twMod |= TW_KMOD_CTRL;
    if (_Event->mod & SDL_KMOD_ALT) twMod |= TW_KMOD_ALT;

    int twKey = 0;
    switch (_Event->key) {
    case SDLK_BACKSPACE: twKey = TW_KEY_BACKSPACE; break;
    case SDLK_TAB: twKey = TW_KEY_TAB; break;
    case SDLK_RETURN: twKey = TW_KEY_RETURN; break;
    case SDLK_PAUSE: twKey = TW_KEY_PAUSE; break;
    case SDLK_SPACE: twKey = TW_KEY_SPACE; break;
    case SDLK_DELETE: twKey = TW_KEY_DELETE; break;
    case SDLK_UP: twKey = TW_KEY_UP; break;
    case SDLK_DOWN: twKey = TW_KEY_DOWN; break;
    case SDLK_RIGHT: twKey = TW_KEY_RIGHT; break;
    case SDLK_LEFT: twKey = TW_KEY_LEFT; break;
    case SDLK_INSERT: twKey = TW_KEY_INSERT; break;
    case SDLK_HOME: twKey = TW_KEY_HOME; break;
    case SDLK_END: twKey = TW_KEY_END; break;
    case SDLK_PAGEUP: twKey = TW_KEY_PAGE_UP; break;
    case SDLK_PAGEDOWN: twKey = TW_KEY_PAGE_DOWN; break;
    case SDLK_F1: twKey = TW_KEY_F1; break;
    case SDLK_F2: twKey = TW_KEY_F2; break;
    case SDLK_F3: twKey = TW_KEY_F3; break;
    case SDLK_F4: twKey = TW_KEY_F4; break;
    case SDLK_F5: twKey = TW_KEY_F5; break;
    case SDLK_F6: twKey = TW_KEY_F6; break;
    case SDLK_F7: twKey = TW_KEY_F7; break;
    case SDLK_F8: twKey = TW_KEY_F8; break;
    case SDLK_F9: twKey = TW_KEY_F9; break;
    case SDLK_F10: twKey = TW_KEY_F10; break;
    case SDLK_F11: twKey = TW_KEY_F11; break;
    case SDLK_F12: twKey = TW_KEY_F12; break;
    case SDLK_F13: twKey = TW_KEY_F13; break;
    case SDLK_F14: twKey = TW_KEY_F14; break;
    case SDLK_F15: twKey = TW_KEY_F15; break;
    }
    if (twKey == 0 && ctrl && _Event->key < 128) {
        twKey = (int)_Event->key;
    }
    if (twKey != 0) {
        TwKeyPressed(twKey, twMod);
    }
}

// SDL3 delivers typed text as a UTF-8 string per SDL_EVENT_TEXT_INPUT event
// (opt-in via SDL_StartTextInput()), unlike GLFW's one-codepoint-per-call
// char callback - decode it a codepoint at a time and feed each to
// TwKeyPressed(), same as examples/sdl/Triangle.c's handleTextInput.
static void handleTextInput(const char *_Utf8Text)
{
    const unsigned char *s = (const unsigned char *)_Utf8Text;
    while (*s != '\0') {
        unsigned int cp = 0;
        int extra = 0;
        if ((*s & 0x80) == 0x00) { cp = *s; extra = 0; }
        else if ((*s & 0xE0) == 0xC0) { cp = *s & 0x1F; extra = 1; }
        else if ((*s & 0xF0) == 0xE0) { cp = *s & 0x0F; extra = 2; }
        else if ((*s & 0xF8) == 0xF0) { cp = *s & 0x07; extra = 3; }
        else { ++s; continue; } // invalid leading byte, skip it
        ++s;
        bool valid = true;
        for (int i = 0; i < extra; ++i) {
            if ((*s & 0xC0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (*s & 0x3F);
            ++s;
        }
        if (valid) TwKeyPressed((int)cp, 0);
    }
}

static void handleMouseButton(const SDL_MouseButtonEvent *_Event)
{
    if (_Event->button == SDL_BUTTON_LEFT || _Event->button == SDL_BUTTON_MIDDLE || _Event->button == SDL_BUTTON_RIGHT) {
        // AntTweakBar.h's TW_MOUSE_LEFT/MIDDLE/RIGHT are deliberately the
        // same values as SDL_BUTTON_LEFT/MIDDLE/RIGHT (1/2/3) - no mapping
        // table needed.
        TwMouseButton(_Event->down ? TW_MOUSE_PRESSED : TW_MOUSE_RELEASED, (TwMouseButtonID)_Event->button);
    }
}

static void updateMouseScale(SDL_Window *_Window, int _PixelWidth, int _PixelHeight)
{
    int pointWidth = _PixelWidth, pointHeight = _PixelHeight;
    SDL_GetWindowSize(_Window, &pointWidth, &pointHeight);
    g_MouseScaleX = (pointWidth > 0) ? (double)_PixelWidth / pointWidth : 1.0;
    g_MouseScaleY = (pointHeight > 0) ? (double)_PixelHeight / pointHeight : 1.0;
}

// Called once at startup (with the window's actual initial pixel size) and
// again on every SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED. Unlike
// examples/sdl/Triangle.c's counterpart, this only updates the viewport/
// TwWindowSize/mouse-scale - Render() sets up the projection matrix itself
// every frame via glFrustum, matching the GLFW3 example's own
// windowSizeCallback (which likewise didn't touch GL_PROJECTION).
static void handleWindowPixelSizeChanged(SDL_Window *_Window, int _Width, int _Height)
{
    if (_Height == 0) _Height = 1;
    g_Width = _Width;
    g_Height = _Height;
    glViewport(0, 0, _Width, _Height);
    TwWindowSize(_Width, _Height);
    updateMouseScale(_Window, _Width, _Height);
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    // Fixed-function GL (glBegin/glEnd-era client-side arrays below) +
    // AntTweakBar's TW_OPENGL (compatibility, not Core Profile) renderer -
    // request a plain 2.1 compatibility context, the same profile
    // examples/sdl/Triangle.c targets and the exact version validated by
    // this backend's compile spike (see docs/plans/sdl3-backend.md Step 1).
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    SDL_Window *window = SDL_CreateWindow("AntTweakBar + SDL3: Menger sponge", g_Width, g_Height,
                                           SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == NULL) {
        fprintf(stderr, "Cannot open SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (ctx == NULL) {
        fprintf(stderr, "Cannot create GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        return 1;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    InitRenderStates();

    Vector3 axis = { { -1, 1, 0 } };
    g_SpongeRotation = RotationFromAxisAngle(axis, FLOAT_PI/4);
    BuildSponge(g_SpongeLevel, g_SpongeAO);

    // AntTweakBar draws every widget at a fixed pixel size with no DPI
    // awareness - scale "fontscaling" by the window's display scale before
    // TwInit, same reasoning (and the same no-op-on-a-standard-display
    // behavior) as examples/sdl/Triangle.c's own contentScale handling.
    float contentScale = SDL_GetWindowDisplayScale(window);
    if (contentScale <= 0.0f) contentScale = 1.0f;
    {
        char fontScalingDef[64];
        snprintf(fontScalingDef, sizeof(fontScalingDef), "GLOBAL fontscaling=%g", (double)contentScale);
        TwDefine(fontScalingDef);
    }

    if (!TwInit(TW_OPENGL, NULL)) {
        fprintf(stderr, "AntTweakBar initialization failed: %s\n", TwGetLastError());
        return 1;
    }
    TwSetCursorCallback(SDLCursorCB, NULL); // SDL cursors are process-global, no window needed
    TwSetClipboardCallback(ClipboardGetSDL, ClipboardSetSDL, NULL);
    SDL_StartTextInput(window);

    {
        int pixelWidth, pixelHeight;
        SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
        handleWindowPixelSizeChanged(window, pixelWidth, pixelHeight);
    }

    TwBar *bar = TwNewBar("TweakBar");
    {
        // Scaled by content scale so the panel keeps up with the
        // now-larger scaled contents.
        int barSize[2] = { (int)(224 * contentScale + 0.5f), (int)(320 * contentScale + 0.5f) };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with SDL3 and OpenGL, drawing a recursively-generated Menger sponge.' ");

    TwAddVarCB(bar, "Level", TW_TYPE_INT32, SetSpongeLevelCB, GetSpongeLevelCB, NULL, "min=0 max=3 group=Sponge keyincr=l keydecr=L");
    TwAddVarCB(bar, "Ambient Occlusion", TW_TYPE_BOOL32, SetSpongeAOCB, GetSpongeAOCB, NULL, "group=Sponge key=o");
    TwAddVarRW(bar, "Rotation", TW_TYPE_QUAT4F, &g_SpongeRotation, "opened=true axisz=-z group=Sponge");
    TwAddVarRW(bar, "Animation", TW_TYPE_BOOL32, &g_Animate, "group=Sponge key=a");
    TwAddVarRW(bar, "Animation speed", TW_TYPE_FLOAT, &g_AnimationSpeed, "min=-10 max=10 step=0.1 group=Sponge keyincr=+ keydecr=-");
    // No axisz=-z here (unlike "Rotation" above): g_LightDir is fed straight
    // to glLightfv with no negation (see Render()), so leaving this widget's
    // axes unpermuted makes its arrow a literal view of that same vector -
    // it always points exactly toward the light source.
    TwAddVarRW(bar, "Light direction", TW_TYPE_DIR3F, &g_LightDir, "opened=true showval=false");
    TwAddVarRW(bar, "Camera distance", TW_TYPE_FLOAT, &g_CamDistance, "min=0 max=4 step=0.01 keyincr=PGUP keydecr=PGDOWN");
    TwAddVarRW(bar, "Background", TW_TYPE_COLOR4F, &g_BackgroundColor, "colormode=hls");

    TwAddSeparator(bar, NULL, "");
    TwAddButton(bar, "FullWidthDemoMoreLines", FullWidthLinesCB, bar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                handleKeyDown(&event.key, &running);
                break;
            case SDL_EVENT_TEXT_INPUT:
                handleTextInput(event.text.text);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                handleMouseButton(&event.button);
                break;
            case SDL_EVENT_MOUSE_MOTION:
                TwMouseMotion((int)(event.motion.x * g_MouseScaleX), (int)(event.motion.y * g_MouseScaleY));
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                handleWindowPixelSizeChanged(window, event.window.data1, event.window.data2);
                break;
            default:
                break;
            }
        }

        Anim();
        Render();
        SDL_GL_SwapWindow(window);
    }

    TwTerminate();
    DestroySDLCursorCache();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();

    free(g_Vertices.items);
    free(g_Indices.items);

    return 0;
}
