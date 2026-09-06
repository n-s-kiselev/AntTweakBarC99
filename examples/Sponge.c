//  ---------------------------------------------------------------------------
//
//  @file       Sponge.c
//
//  @brief      Example that uses AntTweakBar with GLFW3 and OpenGL. Ported
//              to strict C99 from the legacy GLFW2 C++ example
//              TwSpongeGLFW.cpp (itself ported from TwSimpleDX11.cpp+.hlsl,
//              originally Direct3D11-based).
//
//              It draws a Menger sponge, aka Sierpinski cube:
//              http://en.wikipedia.org/wiki/Menger_sponge .
//
//              Cubes shading is augmented with some simple ambient occlusion
//              applied by subdividing each cube face into a 3x3 grid.
//              AntTweakBar is used to add some interactive controls,
//              including an interactive quaternion rotation widget.
//
//              The sponge mesh-generation math is unchanged from the
//              original C++ example - only its syntax was converted to C99
//              (operator overloads became named functions, references
//              became pointers, std::vector became a small growable array,
//              a local class-with-a-static-method became a plain file-scope
//              static function, and TW_TYPE_BOOLCPP - a C++-only type sized
//              to match a real C++ bool - became TW_TYPE_BOOL32, the plain
//              32-bit boolean type, with its bound variables widened from
//              bool to int to match TW_TYPE_BOOL32's expected storage size).
//              Windowing/event handling was ported from GLFW2's
//              implicit-context API to GLFW3, using the same integration
//              pattern (cursor callback, key/mouse/scroll/resize callbacks,
//              main loop) established in SimpleGL21.c.
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              GLFW:        http://www.glfw.org
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <AntTweakBar.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// ----------------------------------------------------------------------
// GLFW3 cursor binding (see docs/glfw3-cursor-integration.md and
// SimpleGL21.c): AntTweakBar predates cursor-ownership models like
// GLFW3's and sets the system cursor directly, which GLFW3 toolkits that
// reassert their own cursor on every mouse move (e.g. macOS's Cocoa
// backend) silently overwrite. Installing this as AntTweakBar's cursor
// callback (TwSetCursorCallback, below) routes every cursor change through
// glfwSetCursor() instead, so GLFW3 owns it.
// ----------------------------------------------------------------------

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

// ----------------------------------------------------------------------
// Geometry data structures and portable math (unchanged from the original
// TwSimpleDX11.cpp/TwSpongeGLFW.cpp, only C++ syntax converted to C99)
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

// GLFW always reports cursor position in window points, but TwWindowSize()
// is now fed framebuffer pixels (see windowSizeCallback), so mouse events
// must be scaled by this window/framebuffer ratio before reaching
// AntTweakBar, or its hit-testing/drawing (now in pixel space) would
// misread a point-space cursor position - see docs/plans/examples-hidpi-scaling.md.
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

// Rotating sponge
static void Anim(void)
{
    static double s_PrevTime = 0;
    double time = glfwGetTime();
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
// GLFW3 callbacks (same shape as SimpleGL21.c's GLFW3 integration pattern)
// ----------------------------------------------------------------------

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
  (void)scancode;
  if (action == GLFW_PRESS || action == GLFW_REPEAT)
  {
    if (key == GLFW_KEY_ESCAPE)
    {
      glfwSetWindowShouldClose(window, GLFW_TRUE);
      return;
    }

    int twMod = 0;
    if (mods & GLFW_MOD_SHIFT) twMod |= TW_KMOD_SHIFT;
    if (mods & GLFW_MOD_CONTROL) twMod |= TW_KMOD_CTRL;
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
    if (twKey == 0 && (mods & GLFW_MOD_CONTROL) && key < 128)
      twKey = key;
    if (twKey != 0)
      if (TwKeyPressed(twKey, twMod)) return;
  }
}

static void charCallback(GLFWwindow* window, unsigned int key)
{
  (void)window;
  if (TwKeyPressed(key, 0)) return;
}

static void mousebuttonCallback(GLFWwindow* window, int button, int action, int mods)
{
  (void)window; (void)mods;
  TwEventMouseButtonGLFW(button, action);
}

static void mousePosCallback(GLFWwindow* window, double xpos, double ypos)
{
  (void)window;
  TwEventMousePosGLFW((int)(xpos * g_MouseScaleX), (int)(ypos * g_MouseScaleY));
}

// Registered as the FRAMEBUFFER size callback (not the window size
// callback): GLFW reports this in actual pixels, matching
// glViewport/TwWindowSize.
static void windowSizeCallback(GLFWwindow* window, int width, int height)
{
  if (height == 0) height = 1;
  g_Width = width;
  g_Height = height;
  glViewport(0, 0, width, height);
  TwWindowSize(width, height);

  int winWidth = width, winHeight = height;
  glfwGetWindowSize(window, &winWidth, &winHeight);
  g_MouseScaleX = (winWidth > 0) ? (double)width / winWidth : 1.0;
  g_MouseScaleY = (winHeight > 0) ? (double)height / winHeight : 1.0;
}

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
    fflush(stderr);
}

int main(void)
{
    GLFWwindow *window;

    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        fprintf(stderr, "GLFW initialization failed\n");
        return 1;
    }

    // No version/profile hints: this AntTweakBar build only supports the
    // OpenGL compatibility profile for the fixed-function renderer used
    // here (TW_OPENGL_CORE is a different code path), so we let GLFW3
    // create its default (non-core) context.
    window = glfwCreateWindow(g_Width, g_Height, "AntTweakBar + GLFW3: Menger sponge", NULL, NULL);
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
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    InitRenderStates();

    Vector3 axis = { { -1, 1, 0 } };
    g_SpongeRotation = RotationFromAxisAngle(axis, FLOAT_PI/4);
    BuildSponge(g_SpongeLevel, g_SpongeAO);

    // AntTweakBar draws every widget at a fixed pixel size with no DPI
    // awareness, so on a HiDPI/Retina display it looks too large/blurry
    // relative to a standard display (see docs/plans/examples-hidpi-scaling.md).
    // Scaling "fontscaling" (set via TwDefine, before TwInit) by the
    // window's content scale keeps it a comparable physical size; on a
    // standard display the content scale is 1.0, so this is a no-op there.
    float contentScaleX = 1.0f, contentScaleY = 1.0f;
    glfwGetWindowContentScale(window, &contentScaleX, &contentScaleY);
    {
        char fontScalingDef[64];
        snprintf(fontScalingDef, sizeof(fontScalingDef), "GLOBAL fontscaling=%g", (double)contentScaleX);
        TwDefine(fontScalingDef);
    }

    if (!TwInit(TW_OPENGL, NULL)) {
        fprintf(stderr, "AntTweakBar initialization failed: %s\n", TwGetLastError());
        return 1;
    }
    // Give GLFW3 authoritative cursor ownership (see GLFWCursorCB above).
    TwSetCursorCallback(GLFWCursorCB, window);

    {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        windowSizeCallback(window, width, height);
    }

    TwBar *bar = TwNewBar("TweakBar");
    {
        // Scaled by content scale so the panel keeps up with the
        // now-larger scaled contents.
        int barSize[2] = { (int)(224 * contentScaleX + 0.5f), (int)(320 * contentScaleY + 0.5f) };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with GLFW3 and OpenGL, drawing a recursively-generated Menger sponge.' ");

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

    glfwSetKeyCallback(window, keyCallback);
    glfwSetCharCallback(window, charCallback);
    glfwSetMouseButtonCallback(window, mousebuttonCallback);
    glfwSetCursorPosCallback(window, mousePosCallback);
    glfwSetFramebufferSizeCallback(window, windowSizeCallback);

    while (!glfwWindowShouldClose(window)) {
        Anim();
        Render();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    TwTerminate();
    DestroyGLFWCursorCache();
    glfwTerminate();

    free(g_Vertices.items);
    free(g_Indices.items);

    return 0;
}
