//  ---------------------------------------------------------------------------
//
//  @file       Sponge_sfml.cpp
//  @brief      Example that uses AntTweakBar with SFML3 and OpenGL. Draws a
//              Menger sponge, aka Sierpinski cube:
//              http://en.wikipedia.org/wiki/Menger_sponge .
//
//              Cubes shading is augmented with some simple ambient occlusion
//              applied by subdividing each cube face into a 3x3 grid.
//              AntTweakBar is used to add some interactive controls,
//              including an interactive quaternion rotation widget.
//
//              SFML3 port of examples/glfw/Sponge_glfw.c - see
//              docs/plans/sfml3-backend.md for the backend adapter notes.
//              The sponge mesh-generation math is unchanged; only the
//              windowing/event handling differs (SFML3 instead of GLFW3).
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              SFML:        https://www.sfml-dev.org
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <SFML/Window.hpp>
#include <AntTweakBar.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

// Unlike GLFW/SDL3, SFML exposes no window-content-scale/DPI query at all
// (checked the vendored Window/WindowBase headers directly - no such
// method exists), so there is no fontscaling adjustment here: AntTweakBar
// draws at its default fixed pixel size on every display, including
// Retina/HiDPI ones, unlike its GLFW3/SDL3 counterparts.

// SFML cursors are process-global-ish (sf::WindowBase::setMouseCursor()
// ties the cursor to no particular ownership model). sf::Cursor has no
// copy constructor (move-only), so the cache holds std::optional<sf::Cursor>
// populated via std::move.
static std::optional<sf::Cursor> g_StandardCursors[TW_CURSOR_CUSTOM];
static std::optional<sf::Cursor> g_LastCustomCursor;
static bool g_CursorHidden = false;

static sf::Cursor::Type SFMLStandardCursorShape(ETwCursor _Cursor)
{
    switch (_Cursor) {
    case TW_CURSOR_ARROW:        return sf::Cursor::Type::Arrow;
    case TW_CURSOR_MOVE:         return sf::Cursor::Type::SizeAll;
    case TW_CURSOR_RESIZE_WE:    return sf::Cursor::Type::SizeHorizontal;
    case TW_CURSOR_RESIZE_NS:    return sf::Cursor::Type::SizeVertical;
    case TW_CURSOR_RESIZE_NESW:  return sf::Cursor::Type::SizeBottomLeftTopRight;
    case TW_CURSOR_RESIZE_NWSE:  return sf::Cursor::Type::SizeTopLeftBottomRight;
    case TW_CURSOR_HAND:         return sf::Cursor::Type::Hand;
    case TW_CURSOR_CROSS:        return sf::Cursor::Type::Cross;
    case TW_CURSOR_IBEAM:        return sf::Cursor::Type::Text;
    case TW_CURSOR_NO:           return sf::Cursor::Type::NotAllowed;
    default:                     return sf::Cursor::Type::Arrow; // TW_CURSOR_HELP/UPARROW: no dedicated SFML shape
    }
}

// sf::Clipboard::getString() returns an sf::String by value - convert to a
// static std::string so the returned const char* stays valid for however
// long AntTweakBar needs it after this call returns.
static std::string g_ClipboardText;

static const char * TW_CALL ClipboardGetSFML(void *_ClientData)
{
    (void)_ClientData;
    g_ClipboardText = sf::Clipboard::getString().toAnsiString();
    return g_ClipboardText.c_str();
}

static void TW_CALL ClipboardSetSFML(const char *_Text, void *_ClientData)
{
    (void)_ClientData;
    sf::Clipboard::setString(_Text);
}

static void TW_CALL SFMLCursorCB(ETwCursor _Cursor, const unsigned char *_RGBA32x32, int _HotX, int _HotY, void *_ClientData)
{
    sf::WindowBase *window = static_cast<sf::WindowBase *>(_ClientData);
    // TW_CURSOR_HIDDEN is an input mode, not a cursor shape: the roto slider
    // hides the pointer while it is dragged. g_CursorHidden remembers that so
    // the mode is restored once, on the next request for a visible cursor.
    if (_Cursor == TW_CURSOR_HIDDEN) {
        window->setMouseCursorVisible(false);
        g_CursorHidden = true;
        return;
    }
    if (g_CursorHidden) {
        window->setMouseCursorVisible(true);
        g_CursorHidden = false;
    }
    if (_Cursor == TW_CURSOR_CUSTOM && _RGBA32x32 != NULL) {
        auto cursor = sf::Cursor::createFromPixels(_RGBA32x32, sf::Vector2u(32, 32),
                                                    sf::Vector2u((unsigned)_HotX, (unsigned)_HotY));
        if (cursor.has_value()) {
            window->setMouseCursor(*cursor);
            g_LastCustomCursor = std::move(cursor);
        }
        return;
    }
    if (!g_StandardCursors[_Cursor].has_value())
        g_StandardCursors[_Cursor] = sf::Cursor::createFromSystem(SFMLStandardCursorShape(_Cursor));
    if (g_StandardCursors[_Cursor].has_value())
        window->setMouseCursor(*g_StandardCursors[_Cursor]);
}

static void handleKeyPressed(const sf::Event::KeyPressed *_Event, bool *_Running)
{
    // Matches the GLFW original: Escape quits directly, not forwarded to
    // TwKeyPressed.
    if (_Event->code == sf::Keyboard::Key::Escape) {
        *_Running = false;
        return;
    }

    int twMod = 0;
    if (_Event->shift) twMod |= TW_KMOD_SHIFT;
    if (_Event->control) twMod |= TW_KMOD_CTRL;
    if (_Event->alt) twMod |= TW_KMOD_ALT;

    int twKey = 0;
    switch (_Event->code) {
    case sf::Keyboard::Key::Backspace: twKey = TW_KEY_BACKSPACE; break;
    case sf::Keyboard::Key::Tab: twKey = TW_KEY_TAB; break;
    case sf::Keyboard::Key::Enter: twKey = TW_KEY_RETURN; break;
    case sf::Keyboard::Key::Pause: twKey = TW_KEY_PAUSE; break;
    case sf::Keyboard::Key::Space: twKey = TW_KEY_SPACE; break;
    case sf::Keyboard::Key::Delete: twKey = TW_KEY_DELETE; break;
    case sf::Keyboard::Key::Up: twKey = TW_KEY_UP; break;
    case sf::Keyboard::Key::Down: twKey = TW_KEY_DOWN; break;
    case sf::Keyboard::Key::Right: twKey = TW_KEY_RIGHT; break;
    case sf::Keyboard::Key::Left: twKey = TW_KEY_LEFT; break;
    case sf::Keyboard::Key::Insert: twKey = TW_KEY_INSERT; break;
    case sf::Keyboard::Key::Home: twKey = TW_KEY_HOME; break;
    case sf::Keyboard::Key::End: twKey = TW_KEY_END; break;
    case sf::Keyboard::Key::PageUp: twKey = TW_KEY_PAGE_UP; break;
    case sf::Keyboard::Key::PageDown: twKey = TW_KEY_PAGE_DOWN; break;
    case sf::Keyboard::Key::F1: twKey = TW_KEY_F1; break;
    case sf::Keyboard::Key::F2: twKey = TW_KEY_F2; break;
    case sf::Keyboard::Key::F3: twKey = TW_KEY_F3; break;
    case sf::Keyboard::Key::F4: twKey = TW_KEY_F4; break;
    case sf::Keyboard::Key::F5: twKey = TW_KEY_F5; break;
    case sf::Keyboard::Key::F6: twKey = TW_KEY_F6; break;
    case sf::Keyboard::Key::F7: twKey = TW_KEY_F7; break;
    case sf::Keyboard::Key::F8: twKey = TW_KEY_F8; break;
    case sf::Keyboard::Key::F9: twKey = TW_KEY_F9; break;
    case sf::Keyboard::Key::F10: twKey = TW_KEY_F10; break;
    case sf::Keyboard::Key::F11: twKey = TW_KEY_F11; break;
    case sf::Keyboard::Key::F12: twKey = TW_KEY_F12; break;
    case sf::Keyboard::Key::F13: twKey = TW_KEY_F13; break;
    case sf::Keyboard::Key::F14: twKey = TW_KEY_F14; break;
    case sf::Keyboard::Key::F15: twKey = TW_KEY_F15; break;
    default: break;
    }
    // GLFW original also lets Ctrl-held alphanumeric keys through
    // unconditionally (raw ASCII key < 128) even without a special-case
    // match above. Unlike GLFW's key codes, sf::Keyboard::Key::A..Z/Num0..9
    // are small sequential enum values (A=0), NOT ASCII - map them to their
    // ASCII letter/digit explicitly rather than casting the enum directly.
    if (twKey == 0 && _Event->control) {
        using sf::Keyboard::Key;
        if (_Event->code >= Key::A && _Event->code <= Key::Z) {
            twKey = 'A' + (static_cast<int>(_Event->code) - static_cast<int>(Key::A));
        } else if (_Event->code >= Key::Num0 && _Event->code <= Key::Num9) {
            twKey = '0' + (static_cast<int>(_Event->code) - static_cast<int>(Key::Num0));
        }
    }
    if (twKey != 0) {
        TwKeyPressed(twKey, twMod);
    }
}

static void handleMouseButton(sf::Mouse::Button _Button, bool _Down)
{
    // sf::Mouse::Button::Left/Right/Middle are 0/1/2, NOT matching
    // TW_MOUSE_LEFT/MIDDLE/RIGHT (1/2/3) the way SDL3's numbering happened
    // to - needs its own explicit mapping (see Triangle_sfml.cpp).
    TwMouseButtonID twButton;
    switch (_Button) {
    case sf::Mouse::Button::Left:   twButton = TW_MOUSE_LEFT;   break;
    case sf::Mouse::Button::Right:  twButton = TW_MOUSE_RIGHT;  break;
    case sf::Mouse::Button::Middle: twButton = TW_MOUSE_MIDDLE; break;
    default: return;
    }
    TwMouseButton(_Down ? TW_MOUSE_PRESSED : TW_MOUSE_RELEASED, twButton);
}

// ----------------------------------------------------------------------
// Geometry data structures and portable math (unchanged from the GLFW3
// original, only C99 syntax converted to valid C++ where needed - the
// nested-brace aggregate initializers below are valid in both languages)
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
// int> for the sponge's vertex/index buffers (kept as a plain C-style array
// here too, matching the GLFW/C99 original's own structure exactly).
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


// Some math operators and functions (operator overloads become named functions, matching the C99 original).
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

// Applies ambient-occlusion edge flags for one recursive subdivision step.
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
    // directly here with no negation.
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
static void Anim(float _Time)
{
    static float s_PrevTime = 0;
    float dt = _Time - s_PrevTime;
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
    s_PrevTime = _Time;
}

// SFML has only one size concept (no window-point-size-vs-pixel-size split
// the way GLFW/SDL3 expose for HiDPI), so no mouse-coordinate scaling step
// is needed here.
static void handleResized(unsigned int _Width, unsigned int _Height)
{
    if (_Height == 0) _Height = 1;
    g_Width = (int)_Width;
    g_Height = (int)_Height;
    glViewport(0, 0, (GLsizei)_Width, (GLsizei)_Height);
    TwWindowSize((int)_Width, (int)_Height);
}

int main()
{
    // No version/profile hints: this AntTweakBar build only supports the
    // OpenGL compatibility profile for the fixed-function renderer used
    // here (TW_OPENGL_CORE is a different code path), so we let SFML
    // create its default (non-core) context.
    sf::ContextSettings settings;
    // depthBits defaults to 0 (no depth buffer) - unlike GLFW (default 24)
    // and SDL3 - so it must be requested explicitly for GL_DEPTH_TEST
    // below to have any effect; without it, overlapping cube faces draw
    // in call order instead of by distance (looks like inverted normals,
    // but isn't - the geometry/winding is fine).
    settings.depthBits = 24;

    sf::Window window(sf::VideoMode(sf::Vector2u((unsigned)g_Width, (unsigned)g_Height)),
                      "AntTweakBar + SFML3: Menger sponge", sf::Style::Default, sf::State::Windowed, settings);

    if (!window.setActive(true)) {
        fprintf(stderr, "Failed to set the SFML window as active\n");
        return 1;
    }

    if (!gladLoadGLLoader((GLADloadproc)sf::Context::getFunction)) {
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

    if (!TwInit(TW_OPENGL, NULL)) {
        fprintf(stderr, "AntTweakBar initialization failed: %s\n", TwGetLastError());
        return 1;
    }
    TwSetCursorCallback(SFMLCursorCB, &window);
    TwSetClipboardCallback(ClipboardGetSFML, ClipboardSetSFML, NULL);

    handleResized((unsigned)g_Width, (unsigned)g_Height);

    TwBar *bar = TwNewBar("TweakBar");
    {
        int barSize[2] = { 224, 320 };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with SFML3 and OpenGL, drawing a recursively-generated Menger sponge.' ");

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

    sf::Clock clock;
    bool running = true;
    while (running) {
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                running = false;
            } else if (const auto *keyPressed = event->getIf<sf::Event::KeyPressed>()) {
                handleKeyPressed(keyPressed, &running);
            } else if (const auto *textEntered = event->getIf<sf::Event::TextEntered>()) {
                TwKeyPressed((int)textEntered->unicode, 0);
            } else if (const auto *pressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                handleMouseButton(pressed->button, true);
            } else if (const auto *released = event->getIf<sf::Event::MouseButtonReleased>()) {
                handleMouseButton(released->button, false);
            } else if (const auto *moved = event->getIf<sf::Event::MouseMoved>()) {
                TwMouseMotion(moved->position.x, moved->position.y);
            } else if (const auto *resized = event->getIf<sf::Event::Resized>()) {
                handleResized(resized->size.x, resized->size.y);
            }
            // No mouse-wheel handling: the GLFW original registers no
            // scroll callback for this example either - behavior parity,
            // not superset parity.
        }

        Anim(clock.getElapsedTime().asSeconds());
        Render();
        window.display();
    }

    TwTerminate();

    free(g_Vertices.items);
    free(g_Indices.items);

    return 0;
}
