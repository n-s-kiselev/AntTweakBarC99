//  ---------------------------------------------------------------------------
//
//  @file       Advanced_c99_sfml.cpp
//  @brief      An example showing many features of AntTweakBar,
//              including variables accessed by callbacks and
//              the definition of a custom structure type.
//              It also uses OpenGL and SFML3 windowing system
//              but could be easily adapted to other frameworks.
//
//              SFML3 port of examples/glfw/Advanced_c99_glfw.c - see
//              docs/plans/sfml3-backend.md for the backend adapter notes.
//              SFML has no C API at all, so - unlike the GLFW/SDL3
//              originals, plain C99 - this is real C++; the Scene/Light
//              structs and every Scene_* function carry over unchanged
//              (valid C is valid C++ here), only the window/context/
//              event/cursor/clipboard/timing glue changed.
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              SFML:        https://www.sfml-dev.org
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
#include <SFML/Window.hpp>
#include <AntTweakBar.h>

#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <optional>
#include <string>

#if !defined(_WIN32) && !defined(_WIN64)
#   define _snprintf snprintf
#endif

// M_PI is a common extension, not part of strict C99/C++ - some libcs hide
// it under strict standards modes. Defined here so this file builds the
// same way on every supported platform (matches the GLFW/SDL3 originals).
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

float g_cameraPosX = 0.0f;
float g_cameraPosY = 0.0f;
float g_cameraPosZ = 0.0f;

bool g_cameraDragging = false;

// Unlike the GLFW original (which queries glfwGetCursorPos() separately on
// button-press), SFML's sf::Event::MouseButtonPressed already carries the
// press position directly on the event, so no separate "last mouse
// position" query is needed at button-press time - only a running "last
// position" updated from every MouseMoved event, for drag-delta tracking.
double g_lastMouseX = 0.0;
double g_lastMouseY = 0.0;

// SFML exposes no direct window-content-scale/DPI query (unlike GLFW's
// glfwGetWindowContentScale/SDL3's SDL_GetWindowDisplayScale) - main()
// below sets both to the equivalent scale factor computed manually
// instead, from the ratio between window.getSize() and the logical size
// requested (see vendor/sfml/src/SFML/Window/macOS/SFWindowController.mm's
// own highDpi fix, which makes getSize() report real native pixel
// dimensions). Scene_CreateBar() below references them by name for its
// own bar-size scaling, matching the GLFW/SDL3 originals' structure.
float g_ContentScaleX = 1.0f, g_ContentScaleY = 1.0f;

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

const char* title = "AntTweakBar example: Advanced (C99, SFML3)";

// Light animation mode.
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

// Scene rotation mode.
typedef enum { ROT_OFF, ROT_CW, ROT_CCW } SceneRotMode;

// Structure that describes the scene.
typedef struct Scene
{
    int     Wireframe;  // draw scene in wireframe or filled (TW_TYPE_BOOL32-bound: must be int, not bool)
    int     Subdiv;     // number of subdivisions used to tessellate the scene
    int     NumLights;  // number of dynamic lights
    float   BgColor0[3], BgColor1[3]; // top and bottom background colors
    float   Ambient;    // scene ambient factor
    float   Reflection; // ground plane reflection factor (0=no reflection, 1=full reflection)
    double  RotYAngle;  // rotation angle of the scene around its Y axis (in degree)
    SceneRotMode Rotation; // scene rotation mode (off, clockwise, counter-clockwise)

    GLuint  objList, groundList, haloList;  // OpenGL display list IDs
    int     maxLights;                      // maximum number of dynamic lights allowed by the graphic card
    Light * lights;                         // array of lights (malloc'ed in Scene_Init, freed in Scene_Destruct)
    TwBar * lightsBar;                      // pointer to the tweak bar for lights created by Scene_CreateBar()
} Scene;

// Forward declarations.
static void Scene_Construct(Scene *scene);
static void Scene_Destruct(Scene *scene);
static void Scene_Init(Scene *scene, bool changeLights);
static void Scene_Draw(const Scene *scene);
static void Scene_Update(Scene *scene, double time);
static void Scene_CreateBar(Scene *scene);
static void Scene_DrawHalos(const Scene *scene, bool reflected);
static void DrawSubdivPlaneY(float xMin, float xMax, float y, float zMin, float zMax, int xSubdiv, int zSubdiv);
static void DrawSubdivCylinderY(float xCenter, float yBottom, float zCenter, float height, float radiusBottom, float radiusTop, int sideSubdiv, int ySubdiv);
static void DrawSubdivHaloZ(float x, float y, float z, float radius, int subdiv);

// Constructor
static void Scene_Construct(Scene *scene)
{
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
    scene->Rotation = ROT_CCW;
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
    if (scene->lights)
        free(scene->lights);
}

// Create the scene, and (re)initialize lights if changeLights is true
static void Scene_Init(Scene *scene, bool changeLights)
{
    glGetIntegerv(GL_MAX_LIGHTS, &scene->maxLights);
    if (scene->maxLights > 16)
        scene->maxLights = 16;

    if (scene->lights == NULL && scene->maxLights > 0)
    {
        scene->lights = (Light *)malloc((size_t)scene->maxLights * sizeof(Light));
        scene->NumLights = 3;
        if (scene->NumLights > scene->maxLights)
            scene->NumLights = scene->maxLights;
        changeLights = true;

        Scene_CreateBar(scene);
    }

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

    if (scene->objList > 0)
        glDeleteLists(scene->objList, 1);
    scene->objList = glGenLists(1);
    glNewList(scene->objList, GL_COMPILE);
    DrawSubdivCylinderY(-0.9f, 0, -0.9f, 1.4f, 0.15f, 0.12f, scene->Subdiv/2+8, scene->Subdiv);
    DrawSubdivCylinderY(+0.9f, 0, -0.9f, 1.4f, 0.15f, 0.12f, scene->Subdiv/2+8, scene->Subdiv);
    DrawSubdivCylinderY(+0.9f, 0, +0.9f, 1.4f, 0.15f, 0.12f, scene->Subdiv/2+8, scene->Subdiv);
    DrawSubdivCylinderY(-0.9f, 0, +0.9f, 1.4f, 0.15f, 0.12f, scene->Subdiv/2+8, scene->Subdiv);
    DrawSubdivCylinderY(0, 0, 0, 0.4f, 0.5f, 0.3f, scene->Subdiv+16, scene->Subdiv/8+1);
    DrawSubdivCylinderY(0, 0.4f, 0, 0.05f, 0.3f, 0.0f, scene->Subdiv+16, scene->Subdiv/16+1);
    glEndList();

    if (scene->groundList > 0)
        glDeleteLists(scene->groundList, 1);
    scene->groundList = glGenLists(1);
    glNewList(scene->groundList, GL_COMPILE);
    DrawSubdivPlaneY(-1.2f, 1.2f, 0, -1.2f, 1.2f, (3*scene->Subdiv)/2, (3*scene->Subdiv)/2);
    glEndList();

    if (scene->haloList > 0)
        glDeleteLists(scene->haloList, 1);
    scene->haloList = glGenLists(1);
    glNewList(scene->haloList, GL_COMPILE);
    DrawSubdivHaloZ(0, 0, 0, 1, 32);
    glEndList();
}

// Maps sf::Keyboard::Key::A..Z/Num0..Num9 to their ASCII codes for
// Ctrl-modified shortcuts - mirrors the GLFW original's
// "if (twKey == 0 && ctrl && key < 128) twKey = key;" fallback, which
// relied on GLFW3's own key constants already being ASCII-valued for
// printable keys. SFML's sf::Keyboard::Key enum is NOT ASCII-valued
// (Key::A == 0, not 'A' == 65 - checked the vendored header directly), so
// this needs an explicit table. Only letters/digits are covered (this
// project's own keyboard shortcuts are all letter-based, e.g. KeyIncr=s);
// punctuation Ctrl-shortcuts are not mapped, a minor known gap versus the
// GLFW/SDL3 originals.
static int SFMLKeyToAsciiForCtrl(sf::Keyboard::Key key)
{
    if (key >= sf::Keyboard::Key::A && key <= sf::Keyboard::Key::Z)
        return 'A' + ((int)key - (int)sf::Keyboard::Key::A);
    if (key >= sf::Keyboard::Key::Num0 && key <= sf::Keyboard::Key::Num9)
        return '0' + ((int)key - (int)sf::Keyboard::Key::Num0);
    return 0;
}

static void handleKeyPressed(const sf::Event::KeyPressed *_Event)
{
    // Unlike Triangle_sfml.cpp/Strip_sfml.cpp, Escape is NOT wired to quit
    // here - it's forwarded to TwKeyPressed like any other key, matching
    // the GLFW original exactly (no glfwSetWindowShouldClose call anywhere
    // in Advanced_c99_glfw.c's keyCallback - the window only closes via the
    // OS close button).
    int twMod = 0;
    bool ctrl;
    if (_Event->shift) twMod |= TW_KMOD_SHIFT;
    if ((ctrl = _Event->control)) twMod |= TW_KMOD_CTRL;
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
    if (twKey == 0 && ctrl) {
        twKey = SFMLKeyToAsciiForCtrl(_Event->code);
    }
    if (twKey != 0) {
        TwKeyPressed(twKey, twMod);
    }
}

static void handleMouseButton(sf::Mouse::Button _Button, sf::Vector2i _Position, bool _Down)
{
    // sf::Mouse::Button::Left/Right/Middle are 0/1/2, NOT matching
    // TW_MOUSE_LEFT/MIDDLE/RIGHT (1/2/3) - needs its own explicit mapping
    // (see Triangle_sfml.cpp).
    TwMouseButtonID twButton;
    switch (_Button) {
    case sf::Mouse::Button::Left:   twButton = TW_MOUSE_LEFT;   break;
    case sf::Mouse::Button::Right:  twButton = TW_MOUSE_RIGHT;  break;
    case sf::Mouse::Button::Middle: twButton = TW_MOUSE_MIDDLE; break;
    default: return;
    }
    if (TwMouseButton(_Down ? TW_MOUSE_PRESSED : TW_MOUSE_RELEASED, twButton)) return;

    if (_Button == sf::Mouse::Button::Left) {
        if (_Down) {
            g_cameraDragging = true;
            g_lastMouseX = _Position.x;
            g_lastMouseY = _Position.y;
        } else {
            g_cameraDragging = false;
        }
    }

    if (_Button == sf::Mouse::Button::Right) {
        if (_Down) {
            g_cameraPosX = 0;
            g_cameraPosY = 0;
            g_cameraPosZ = 0; // Reset camera position
        }
    }
}

static void handleMouseMoved(sf::Window &_Window, sf::Vector2i _Position)
{
    if (TwMouseMotion(_Position.x, _Position.y)) return;

    if (g_cameraDragging) {
        double dx = _Position.x - g_lastMouseX;
        double dy = _Position.y - g_lastMouseY;

        sf::Vector2u size = _Window.getSize();
        g_cameraPosX += (float)dx / size.x * 2.0f;  // Scale to screen
        g_cameraPosY -= (float)dy / size.y * 2.0f;  // Inverted Y

        g_lastMouseX = _Position.x;
        g_lastMouseY = _Position.y;
    }
}

static void handleMouseWheel(float _Delta)
{
    static double pos = 0;
    pos += _Delta;
    g_cameraPosZ -= _Delta * 0.1f; // Zoom sensitivity
    if (g_cameraPosZ <-50.0f) g_cameraPosZ =-50.00f; // Prevent too close
    if (g_cameraPosZ > 50.0f) g_cameraPosZ = 50.0f; // Prevent too far

    if (TwMouseWheel((int)pos)) return;
}

// SFML has only one size concept (no window-point-size-vs-pixel-size split
// the way GLFW/SDL3 expose for HiDPI), so no mouse-coordinate scaling step
// is needed here, unlike the GLFW original's g_MouseScaleX/Y.
static void handleResized(unsigned int _Width, unsigned int _Height)
{
    if (_Height == 0) _Height = 1;
    float aspect = (float)_Width / (float)_Height;

    float fovY = 40.0f;
    float near_ = 1.0f;
    float far_ = 10.0f;
    float top = tanf(fovY * (float)M_PI / 360.0f) * near_;
    float bottom = -top;
    float right = top * aspect;
    float left = -right;

    glViewport(0, 0, (GLsizei)_Width, (GLsizei)_Height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(left, right, bottom, top, near_, far_);

    TwWindowSize((int)_Width, (int)_Height);
}

void TW_CALL ResetCubePosition(void *clientData)
{
    (void)clientData;
    g_cameraPosX = 0;
    g_cameraPosY = 0;
    g_cameraPosZ = 5.0f; // Reset camera position
}

// Callback function associated to the 'Change lights' button of the lights tweak bar.
void TW_CALL ReinitCB(void *clientData)
{
    Scene *scene = (Scene *)clientData;
    Scene_Init(scene, true);
}

// Create a tweak bar for lights.
static void Scene_CreateBar(Scene *scene)
{
    scene->lightsBar = TwNewBar("Lights");
    TwDefine(" Lights label='Lights TweakBar' position='580 16' alpha=0 help='Use this bar to edit the lights in the scene.' ");
    {
        int lightsBarSize[2] = { (int)(200 * g_ContentScaleX + 0.5f), (int)(320 * g_ContentScaleY + 0.5f) };
        TwSetParam(scene->lightsBar, NULL, "size", TW_PARAM_INT32, 2, lightsBarSize);
    }

    TwAddVarRW(scene->lightsBar, "NumLights", TW_TYPE_INT32, &scene->NumLights,
               " label='Number of lights' keyIncr=l keyDecr=L help='Changes the number of lights in the scene.' ");

    int zero = 0;
    TwSetParam(scene->lightsBar, "NumLights", "min", TW_PARAM_INT32, 1, &zero);
    TwSetParam(scene->lightsBar, "NumLights", "max", TW_PARAM_INT32, 1, &scene->maxLights);

    TwAddButton(scene->lightsBar, "Reinit", ReinitCB, scene,
                " label='Change lights' key=c help='Random changes of lights parameters.' ");

    TwEnumVal modeEV[] =
    {
        { ANIM_FIXED,    "Fixed"     },
        { ANIM_BOUNCE,   "Bounce"    },
        { ANIM_ROTATE,   "Rotate"    },
        { ANIM_COMBINED, "Combined"  }
    };
    TwType modeType = TwDefineEnum("Mode", modeEV, 4);

    TwStructMember lightMembers[] =
    {
        { "Active",    TW_TYPE_BOOL32,  offsetof(Light, Active),    " help='Enable/disable the light.' " },
        { "Color",     TW_TYPE_COLOR4F, offsetof(Light, Color),     " noalpha help='Light color.' " },
        { "Radius",    TW_TYPE_FLOAT,   offsetof(Light, Radius),    " min=0 max=4 step=0.02 help='Light radius.' " },
        { "Animation", modeType,        offsetof(Light, Animation), " help='Change the animation mode.' " },
        { "Speed",     TW_TYPE_FLOAT,   offsetof(Light, Speed0),    " readonly=true help='Light moving speed.' " }
    };
    TwType lightType = TwDefineStruct("Light", lightMembers, 5, sizeof(Light), NULL, NULL);

    for (int i = 0; i < scene->maxLights; ++i)
    {
        _snprintf(scene->lights[i].Name, sizeof(scene->lights[i].Name), "%d", i+1);
        TwAddVarRW(scene->lightsBar, scene->lights[i].Name, lightType, &scene->lights[i], " group='Edit lights' ");

        char paramValue[64];
        _snprintf(paramValue, sizeof(paramValue), "Light #%d", i+1);
        TwSetParam(scene->lightsBar, scene->lights[i].Name, "label", TW_PARAM_CSTRING, 1, paramValue);
        _snprintf(paramValue, sizeof(paramValue), "Parameters of the light #%d", i+1);
        TwSetParam(scene->lightsBar, scene->lights[i].Name, "help", TW_PARAM_CSTRING, 1, paramValue);
    }
}

// Move lights
static void Scene_Update(Scene *scene, double time)
{
    float horizSpeed, vertSpeed;
    for (int i = 0; i < scene->NumLights; ++i)
    {
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

// Activate OpenGL lights; hide unused lights in the Lights tweak bar; and draw the scene.
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

    glRotated(scene->RotYAngle, 0, 1, 0);

    int i, lightVisible;
    for (i = 0; i < scene->maxLights; ++i)
    {
        if (i < scene->NumLights)
        {
            lightVisible = 1;

            if (scene->lights[i].Active)
                glEnable(GL_LIGHT0+i);
            else
                glDisable(GL_LIGHT0+i);

            float reflectPos[4] = { scene->lights[i].Pos[0], -scene->lights[i].Pos[1], scene->lights[i].Pos[2], scene->lights[i].Pos[3] };
            glLightfv(GL_LIGHT0+i, GL_POSITION, reflectPos);
            glLightfv(GL_LIGHT0+i, GL_DIFFUSE, scene->lights[i].Color);
            glLightf(GL_LIGHT0+i, GL_CONSTANT_ATTENUATION, 1);
            glLightf(GL_LIGHT0+i, GL_LINEAR_ATTENUATION, 0);
            glLightf(GL_LIGHT0+i, GL_QUADRATIC_ATTENUATION, 1.0f/(scene->lights[i].Radius*scene->lights[i].Radius));
        }
        else
        {
            lightVisible = 0;
            glDisable(GL_LIGHT0+i);
        }

        TwSetParam(scene->lightsBar, scene->lights[i].Name, "visible", TW_PARAM_INT32, 1, &lightVisible);
    }

    float ambient[4] = { scene->Ambient*(scene->BgColor0[0]+scene->BgColor1[0])/2, scene->Ambient*(scene->BgColor0[1]+scene->BgColor1[1])/2,
                         scene->Ambient*(scene->BgColor0[2]+scene->BgColor1[2])/2, 1 };
    glClearColor(ambient[0], ambient[1], ambient[2], 1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);

    glPolygonMode(GL_FRONT_AND_BACK, (scene->Wireframe ? GL_LINE : GL_FILL));
    glCullFace(GL_FRONT);
    glPushMatrix();
    glScalef(1, -1, 1);
    glColor3f(1, 1, 1);
    glCallList(scene->objList);
    Scene_DrawHalos(scene, true);
    glPopMatrix();
    glCullFace(GL_BACK);

    glClear(GL_DEPTH_BUFFER_BIT);

    glColor4f(1, 1, 1, 1.0f-scene->Reflection);
    glCallList(scene->groundList);

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

    for (i = 0; i < scene->NumLights; ++i)
        glLightfv(GL_LIGHT0+i, GL_POSITION, scene->lights[i].Pos);

    glPolygonMode(GL_FRONT_AND_BACK, (scene->Wireframe ? GL_LINE : GL_FILL));
    glColor3f(1, 1, 1);
    glCallList(scene->objList);
    Scene_DrawHalos(scene, false);
}

// Subroutine used to draw halos around light positions
static void Scene_DrawHalos(const Scene *scene, bool reflected)
{
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
    Scene *scene = (Scene *)clientData;
    scene->Subdiv = *(const int *)value;
    Scene_Init(scene, false);
}

// Callback function called by the main tweak bar to get the 'Subdiv' value
void TW_CALL GetSubdivCB(void *value, void *clientData)
{
    Scene *scene = (Scene *)clientData;
    *(int *)value = scene->Subdiv;
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
int main()
{
    sf::ContextSettings settings;
    // sf::ContextSettings::depthBits defaults to 0 (no depth buffer) -
    // unlike GLFW (which defaults to 24) and SDL3, so it must be requested
    // explicitly here for glEnable(GL_DEPTH_TEST) below to have any effect;
    // without it, depth testing is a silent no-op and overlapping geometry
    // draws in call order instead of by distance (looks like inverted
    // normals/backface artifacts, but isn't - the geometry/winding is fine).
    settings.depthBits = 24;
    settings.majorVersion = 2;
    settings.minorVersion = 1;

    sf::Window window(sf::VideoMode(sf::Vector2u(800, 600)), title, sf::Style::Default, sf::State::Windowed, settings);
    window.setTitle(title);

    if (!window.setActive(true)) {
        fprintf(stderr, "Failed to set the SFML window as active\n");
        return 1;
    }

    if (!gladLoadGLLoader((GLADloadproc)sf::Context::getFunction)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        return -1;
    }
    printf("OpenGL Version: %s\n", glGetString(GL_VERSION));
    window.setVerticalSyncEnabled(false); // matches the GLFW original's glfwSwapInterval(0)

    sf::Vector2u size = window.getSize();
    handleResized(size.x, size.y);

    // The ratio between the actual (real-pixel, post-highDpi-fix) size and
    // the logical 800-wide size requested above IS the content scale
    // factor - see g_ContentScaleX/Y's own comment near the top of this
    // file for why SFML needs this computed manually.
    g_ContentScaleX = g_ContentScaleY = (float)size.x / 800.0f;
    if (g_ContentScaleX <= 0.0f) g_ContentScaleX = g_ContentScaleY = 1.0f;
    {
        char fontScalingDef[64];
        snprintf(fontScalingDef, sizeof(fontScalingDef), "GLOBAL fontscaling=%g", (double)g_ContentScaleX);
        TwDefine(fontScalingDef);
    }

    if (!TwInit(TW_OPENGL, NULL)) {
        const char* err = TwGetLastError();
        fprintf(stderr, "TwInit failed: %s\n", err ? err : "Unknown error");
        fflush(stderr);
        return 1;
    }
    TwSetCursorCallback(SFMLCursorCB, &window);
    TwSetClipboardCallback(ClipboardGetSFML, ClipboardSetSFML, NULL);
    TwDefine(" GLOBAL fontSize=3 help='This example illustrates the definition of custom structure type as well as many other features.' ");

    Scene scene;
    Scene_Construct(&scene);
    Scene_Init(&scene, true);

    TwBar *mainBar = TwNewBar("Main");
    TwDefine(" Main label='Main TweakBar' refresh=0.5 position='16 16' alpha=0");
    {
        int mainBarSize[2] = { (int)(260 * g_ContentScaleX + 0.5f), (int)(320 * g_ContentScaleY + 0.5f) };
        TwSetParam(mainBar, NULL, "size", TW_PARAM_INT32, 2, mainBarSize);
    }

    TwAddVarRW(mainBar, "Wireframe", TW_TYPE_BOOL32, &scene.Wireframe,
               " group='Display' key=w help='Toggle wireframe display mode.' ");
    TwAddVarRW(mainBar, "BgTop", TW_TYPE_COLOR3F, &scene.BgColor1,
               " group='Background' help='Change the top background color.' ");
    TwAddVarRW(mainBar, "BgBottom", TW_TYPE_COLOR3F, &scene.BgColor0,
               " group='Background' help='Change the bottom background color.' ");
    TwDefine(" Main/Background group='Display' ");
    TwAddVarCB(mainBar, "Subdiv", TW_TYPE_INT32, SetSubdivCB, GetSubdivCB, &scene,
               " group='Scene' label='Meshes subdivision' min=1 max=50 keyincr=s keyDecr=S help='Subdivide the meshes more or less (switch to wireframe to see the effect).' ");
    TwAddVarRW(mainBar, "Ambient", TW_TYPE_FLOAT, &scene.Ambient,
               " label='Ambient factor' group='Scene' min=0 max=1 step=0.001 keyIncr=a keyDecr=A help='Change scene ambient.' ");
    TwAddVarRW(mainBar, "Reflection", TW_TYPE_FLOAT, &scene.Reflection,
               " label='Reflection factor' group='Scene' min=0 max=1 step=0.001 keyIncr=r keyDecr=R help='Change ground reflection.' ");

    TwEnumVal rotationEV[] = { { ROT_OFF, "Stopped"},
                               { ROT_CW,  "Clockwise" },
                               { ROT_CCW, "Counter-clockwise" } };
    TwType rotationType = TwDefineEnum( "Rotation Mode", rotationEV, 3 );
    TwAddVarRW(mainBar, "Rotation", rotationType, &scene.Rotation,
               " group='Scene' keyIncr=Backspace keyDecr=SHIFT+Backspace help='Stop or change the rotation mode.' ");

    TwAddVarRO(mainBar, "RotYAngle", TW_TYPE_DOUBLE, &scene.RotYAngle,
               " group='Scene' label='Rot angle (degree)' precision=0 help='Animated rotation angle' ");

    TwAddSeparator(mainBar, NULL, "");
    TwAddButton(mainBar, "FullWidthDemoMoreLines", FullWidthLinesCB, mainBar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(mainBar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");

    sf::Clock clock;
    double time = clock.getElapsedTime().asSeconds(), dt = 0;
    double frameDTime = 0, frameCount = 0, fps = 0;

    while (window.isOpen())
    {
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
            } else if (const auto *keyPressed = event->getIf<sf::Event::KeyPressed>()) {
                handleKeyPressed(keyPressed);
            } else if (const auto *textEntered = event->getIf<sf::Event::TextEntered>()) {
                TwKeyPressed((int)textEntered->unicode, 0);
            } else if (const auto *pressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                handleMouseButton(pressed->button, pressed->position, true);
            } else if (const auto *released = event->getIf<sf::Event::MouseButtonReleased>()) {
                handleMouseButton(released->button, released->position, false);
            } else if (const auto *moved = event->getIf<sf::Event::MouseMoved>()) {
                handleMouseMoved(window, moved->position);
            } else if (const auto *wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
                handleMouseWheel(wheel->delta);
            } else if (const auto *resized = event->getIf<sf::Event::Resized>()) {
                handleResized(resized->size.x, resized->size.y);
            }
        }

        double now = clock.getElapsedTime().asSeconds();
        dt = now - time;
        if (dt < 0) dt = 0;
        time += dt;

        if (scene.Rotation==ROT_CW)
            scene.RotYAngle -= 5.0*dt;
        else if (scene.Rotation==ROT_CCW)
            scene.RotYAngle += 5.0*dt;

        Scene_Update(&scene, time);

        Scene_Draw(&scene);

        TwDraw();

        window.display();

        frameCount++;
        frameDTime += dt;
        if (frameDTime>1.0)
        {
            fps = frameCount/frameDTime;
            char newTitle[128];
            _snprintf(newTitle, sizeof(newTitle), "%s (%.1f fps)", title, fps);
            //window.setTitle(newTitle); // uncomment to display framerate
            frameCount = frameDTime = 0;
        }
    }

    Scene_Destruct(&scene);
    TwTerminate();

    return 0;
}
