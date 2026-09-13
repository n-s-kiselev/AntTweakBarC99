//  ---------------------------------------------------------------------------
//
//  @file       Shapes_sfml.cpp
//  @brief      An example that uses AntTweakBar with OpenGL and SFML3
//              to display one of several 3D shapes, orientable with an
//              interactive TW_TYPE_QUAT4F rotation widget or auto-rotated,
//              lit with an adjustable light direction, and colored via
//              grouped TW_TYPE_COLOR3F material variables. Also
//              demonstrates a TwType enum variable (to pick the current
//              shape) and a callback-bound variable (TwAddVarCB, for the
//              auto-rotate toggle).
//              SFML3 port of examples/glfw/Shapes_glfw.c - see
//              docs/plans/sfml3-backend.md for the backend adapter notes.
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              SFML:        https://www.sfml-dev.org
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <SFML/Window.hpp>
#include <AntTweakBar.h>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <optional>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Unlike GLFW/SDL3, SFML exposes no window-content-scale/DPI query at all
// (checked the vendored Window/WindowBase headers directly - no such
// method exists), so there is no fontscaling adjustment here.

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

// This example displays one of the following shapes.
// (SHAPE_SPHERE replaces the original GLUT example's teapot - see the
// original examples/glfw/Shapes_glfw.c's file-level comment for why.)
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

static sf::Clock g_Clock;

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


// Procedural replacement for glutSolidTeapot(): a UV sphere.
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
        float angle = (float)(g_Clock.getElapsedTime().asSeconds() - g_RotateTime);
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


// SFML has only one size concept (no window-point-size-vs-pixel-size split
// the way GLFW/SDL3 expose for HiDPI), so no mouse-coordinate scaling step
// is needed here.
static void handleResized(unsigned int _Width, unsigned int _Height)
{
    if (_Height == 0) _Height = 1;
    float aspect = (float)_Width / (float)_Height;
    float znear = 1.0f;
    float zfar = 100.0f;
    float fov = 45.0f;
    float top = tanf(fov * 0.01745329251f) * znear;
    float bottom = -top;
    float right = top * aspect;
    float left = -right;

    glViewport(0, 0, (GLsizei)_Width, (GLsizei)_Height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(left, right, bottom, top, znear, zfar);

    // Set up the modelview (camera) matrix - manual equivalent of the
    // original's gluLookAt(0,0,5, 0,0,0, 0,1,0) plus its extra offset.
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -5.0f);
    glTranslatef(0.0f, 0.6f, -1.0f);

    // Send the new window size to AntTweakBar
    TwWindowSize((int)_Width, (int)_Height);
}


//  Callback function called when the 'AutoRotate' variable value of the tweak bar has changed
void TW_CALL SetAutoRotateCB(const void *value, void *clientData)
{
    (void)clientData; // unused

    g_AutoRotate = *(const int *)value; // copy value to g_AutoRotate
    if( g_AutoRotate!=0 )
    {
        // init rotation
        g_RotateTime = g_Clock.getElapsedTime().asSeconds();
        g_RotateStart[0] = g_Rotation[0];
        g_RotateStart[1] = g_Rotation[1];
        g_RotateStart[2] = g_Rotation[2];
        g_RotateStart[3] = g_Rotation[3];

        // make Rotation variable read-only
        TwDefine(" TweakBar/ObjRotation readonly ");
    }
    else
        // make Rotation variable read-write
        TwDefine(" TweakBar/ObjRotation readwrite ");
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

// Escape is fed through to TwKeyPressed(TW_KEY_ESCAPE, ...) like any other
// key, matching the GLFW original's keyCallback - it does NOT quit the
// example (the window only closes via the OS close button/sf::Event::Closed).
static void handleKeyPressed(const sf::Event::KeyPressed *_Event)
{
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
    case sf::Keyboard::Key::Escape: twKey = TW_KEY_ESCAPE; break;
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
    if (twKey == 0 && _Event->control) {
        // Ctrl+letter/digit shortcuts (e.g. Ctrl+C/Ctrl+V in text-editable
        // widgets) - sf::Keyboard::Key::A..Z/Num0..Num9 are sequential enum
        // values starting at 0, not ASCII, so map them explicitly rather
        // than casting the enum directly. Mirrors the GLFW original's own
        // `if (twKey==0 && ctrl && key<128) twKey=key;` fallback - needed
        // because SFML, like GLFW/SDL3, delivers no TextEntered event for
        // Ctrl-held key combinations.
        if (_Event->code >= sf::Keyboard::Key::A && _Event->code <= sf::Keyboard::Key::Z)
            twKey = 'A' + ((int)_Event->code - (int)sf::Keyboard::Key::A);
        else if (_Event->code >= sf::Keyboard::Key::Num0 && _Event->code <= sf::Keyboard::Key::Num9)
            twKey = '0' + ((int)_Event->code - (int)sf::Keyboard::Key::Num0);
    }
    if (twKey != 0) {
        TwKeyPressed(twKey, twMod);
    }
}

static void handleMouseButton(sf::Mouse::Button _Button, bool _Down)
{
    // sf::Mouse::Button::Left/Right/Middle are 0/1/2, NOT matching
    // TW_MOUSE_LEFT/MIDDLE/RIGHT (1/2/3) - needs its own explicit mapping.
    TwMouseButtonID twButton;
    switch (_Button) {
    case sf::Mouse::Button::Left:   twButton = TW_MOUSE_LEFT;   break;
    case sf::Mouse::Button::Right:  twButton = TW_MOUSE_RIGHT;  break;
    case sf::Mouse::Button::Middle: twButton = TW_MOUSE_MIDDLE; break;
    default: return;
    }
    TwMouseButton(_Down ? TW_MOUSE_PRESSED : TW_MOUSE_RELEASED, twButton);
}

// Builds the three shape display lists once (replaces the original's
// glutSolidTeapot/glutSolidTorus/glutSolidCone calls).
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
int main()
{
    float axis[] = { 0.7f, 0.7f, 0.0f }; // initial model rotation
    float angle = 0.8f;

    sf::ContextSettings settings;
    // depthBits defaults to 0 (no depth buffer) - unlike GLFW (default 24)
    // and SDL3 - so it must be requested explicitly for GL_DEPTH_TEST
    // below to have any effect; without it, overlapping geometry draws in
    // call order instead of by distance (looks like inverted normals, but
    // isn't - the geometry/winding is fine).
    settings.depthBits = 24;
    settings.majorVersion = 2;
    settings.minorVersion = 1;

    sf::Window window(sf::VideoMode(sf::Vector2u(640, 480)),
                      "AntTweakBar + SFML3 (Shapes)", sf::Style::Default, sf::State::Windowed, settings);

    if (!window.setActive(true)) {
        fprintf(stderr, "Failed to set the SFML window as active\n");
        return -1;
    }

    if (!gladLoadGLLoader((GLADloadproc)sf::Context::getFunction)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        return -2;
    }

    if (!TwInit(TW_OPENGL, NULL)) {
        const char* err = TwGetLastError();
        fprintf(stderr, "TwInit failed: %s\n", err ? err : "Unknown error");
        return -3;
    }
    TwSetCursorCallback(SFMLCursorCB, &window);
    TwSetClipboardCallback(ClipboardGetSFML, ClipboardSetSFML, NULL);

    handleResized(640, 480);

    // Create the shape display lists
    BuildShapeDisplayLists();

    // Create a tweak bar
    TwBar *bar = TwNewBar("TweakBar");
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with SFML3 and OpenGL.' ");
    TwDefine(" TweakBar color='96 216 224' ");
    {
        int barSize[2] = { 200, 400 };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }

    TwAddVarRW(bar, "Zoom", TW_TYPE_FLOAT, &g_Zoom,
               " min=0.01 max=2.5 step=0.01 keyIncr=z keyDecr=Z help='Scale the object (1=original size).' ");

    TwAddVarRW(bar, "ObjRotation", TW_TYPE_QUAT4F, &g_Rotation,
               " label='Object rotation' opened=true help='Change the object orientation.' ");

    TwAddVarCB(bar, "AutoRotate", TW_TYPE_BOOL32, SetAutoRotateCB, GetAutoRotateCB, NULL,
               " label='Auto-rotate' key=space help='Toggle auto-rotate mode.' ");

    TwAddVarRW(bar, "Multiplier", TW_TYPE_FLOAT, &g_LightMultiplier,
               " label='Light booster' min=0.1 max=4 step=0.02 keyIncr='+' keyDecr='-' help='Increase/decrease the light power.' ");

    TwAddVarRW(bar, "LightDir", TW_TYPE_DIR3F, &g_LightDirection,
               " label='Light direction' opened=true help='Change the light direction.' ");

    TwAddVarRW(bar, "Ambient", TW_TYPE_COLOR3F, &g_MatAmbient, " group='Material' ");

    TwAddVarRW(bar, "Diffuse", TW_TYPE_COLOR3F, &g_MatDiffuse, " group='Material' ");

    {
        TwEnumVal shapeEV[NUM_SHAPES] = { {SHAPE_SPHERE, "Sphere"}, {SHAPE_TORUS, "Torus"}, {SHAPE_CONE, "Cone"} };
        TwType shapeType = TwDefineEnum("ShapeType", shapeEV, NUM_SHAPES);
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
    g_RotateTime = g_Clock.getElapsedTime().asSeconds();
    // Init rotation
    SetQuaternionFromAxisAngle(axis, angle, g_Rotation);
    SetQuaternionFromAxisAngle(axis, angle, g_RotateStart);

    // Main loop (repeated while window is not closed)
    bool running = true;
    while (running) {
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                running = false;
            } else if (const auto *keyPressed = event->getIf<sf::Event::KeyPressed>()) {
                handleKeyPressed(keyPressed);
            } else if (const auto *textEntered = event->getIf<sf::Event::TextEntered>()) {
                TwKeyPressed((int)textEntered->unicode, 0);
            } else if (const auto *pressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                handleMouseButton(pressed->button, true);
            } else if (const auto *released = event->getIf<sf::Event::MouseButtonReleased>()) {
                handleMouseButton(released->button, false);
            } else if (const auto *moved = event->getIf<sf::Event::MouseMoved>()) {
                TwMouseMotion(moved->position.x, moved->position.y);
            } else if (const auto *wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
                static double wheelPos = 0;
                wheelPos += wheel->delta;
                TwMouseWheel((int)wheelPos);
            } else if (const auto *resized = event->getIf<sf::Event::Resized>()) {
                handleResized(resized->size.x, resized->size.y);
            }
        }

        Display();

        window.display();
    }

    glDeleteLists(SHAPE_SPHERE, NUM_SHAPES);

    TwTerminate();

    return 0;
}
