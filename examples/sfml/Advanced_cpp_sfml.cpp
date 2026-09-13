//  ---------------------------------------------------------------------------
//
//  @file       Advanced_cpp_sfml.cpp
//  @brief      An example showing many features of AntTweakBar,
//              including variable accessed by callbacks and
//              the definition of a custom structure type.
//              It also uses OpenGL and SFML3 windowing system
//              but could be easily adapted to other frameworks.
//
//              This is the deliberately C++ version of this example,
//              demonstrating that the library also works from C++ client
//              code. See Advanced_c99_sfml.cpp for the equivalent example
//              ported to strict C99. SFML3 port of
//              examples/glfw/Advanced_cpp_glfw.c - see
//              docs/plans/sfml3-backend.md for the backend adapter notes.
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              SFML:        https://www.sfml-dev.org
//
//              This example draws a simple scene that can be re-tesselated
//              interactively, and illuminated dynamically by an adjustable
//              number of moving lights.
//
//
//  @author     Philippe Decaudin
//  @date       2006/05/20
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <SFML/Window.hpp>
#include <AntTweakBar.h>

#include <cmath>
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <optional>
#include <string>
#if !defined(_WIN32) && !defined(_WIN64)
#   define _snprintf snprintf
#endif

float g_cameraPosX = 0.0f;
float g_cameraPosY = 0.0f;
float g_cameraPosZ = 0.0f;

bool g_cameraDragging = false;

int g_lastMouseX = 0;
int g_lastMouseY = 0;

// SFML exposes no direct window-content-scale/DPI query (unlike GLFW's
// glfwGetWindowContentScale/SDL3's SDL_GetWindowDisplayScale) - main()
// below sets both to the equivalent scale factor computed manually
// instead, from the ratio between window.getSize() and the logical size
// requested (see vendor/sfml/src/SFML/Window/macOS/SFWindowController.mm's
// own highDpi fix, which makes getSize() report real native pixel
// dimensions). Scene::CreateBar()'s and main()'s bar-sizing code below
// stays structurally identical to the GLFW original either way.
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

const char* title = "AntTweakBar example: Advanced (C++, SFML3)";

// Light structure: embeds light parameters
struct Light
{
    bool    Active;     // light On or Off
    float   Pos[4];     // light position (in homogeneous coordinates, ie. Pos[4]=1)
    float   Color[4];   // light color (no alpha, ie. Color[4]=1)
    float   Radius;     // radius of the light influence area
    float   Dist0, Angle0, Height0, Speed0; // light initial cylindrical coordinates and speed
    char    Name[4];    // light short name (will be named "1", "2", "3",...)
    enum    AnimMode { ANIM_FIXED, ANIM_BOUNCE, ANIM_ROTATE, ANIM_COMBINED };
    AnimMode Animation; // light animation mode
};


// Class that describes the scene and its methods
class Scene
{
public:
    bool    Wireframe;  // draw scene in wireframe or filled
    int     Subdiv;     // number of subdivisions used to tessellate the scene
    int     NumLights;  // number of dynamic lights
    float   BgColor0[3], BgColor1[3]; // top and bottom background colors
    float   Ambient;    // scene ambient factor
    float   Reflection; // ground plane reflection factor (0=no reflection, 1=full reflection)
    double  RotYAngle;  // rotation angle of the scene around its Y axis (in degree)
    enum    RotMode { ROT_OFF, ROT_CW, ROT_CCW };
    RotMode Rotation;   // scene rotation mode (off, clockwise, counter-clockwise)

            Scene();                        // constructor
            ~Scene();                       // destructor
    void    Init(bool changeLightPos);      // (re)initialize the scene
    void    Draw() const;                   // draw scene
    void    Update(double time);            // move lights

private:
    void    CreateBar();                    // create a tweak bar for lights

    // Some drawing subroutines
    void    DrawSubdivPlaneY(float xMin, float xMax, float y, float zMin, float zMax, int xSubdiv, int zSubdiv) const;
    void    DrawSubdivCylinderY(float xCenter, float yBottom, float zCenter, float height, float radiusBottom, float radiusTop, int sideSubdiv, int ySubdiv) const;
    void    DrawSubdivHaloZ(float x, float y, float z, float radius, int subdiv) const;
    void    DrawHalos(bool reflected) const;

    GLuint  objList, groundList, haloList;  // OpenGL display list IDs
    int     maxLights;                      // maximum number of dynamic lights allowed by the graphic card
    Light * lights;                         // array of lights
    TwBar * lightsBar;                      // pointer to the tweak bar for lights created by CreateBar()
};


// Constructor
Scene::Scene()
{
    // Set scene members.
    // The scene will be created by Scene::Init( )
    Wireframe = false;
    Subdiv = 20;
    NumLights = 0;
    BgColor0[0] = 0.9f;
    BgColor0[1] = 0.0f;
    BgColor0[2] = 0.0f;
    BgColor1[0] = 0.3f;
    BgColor1[1] = 0.0f;
    BgColor1[2] = 0.0f;
    Ambient = 0.2f;
    Reflection = 0.5f;
    RotYAngle = 0;
    Rotation = ROT_CCW;
    objList = 0;
    groundList = 0;
    haloList = 0;
    maxLights = 0;
    lights = NULL;
    lightsBar = NULL;
}


// Destructor
Scene::~Scene()
{
    // delete all lights
    if( lights )
        delete[] lights;
}


// Create the scene, and (re)initialize lights if changeLights is true
void Scene::Init(bool changeLights)
{
    // Get the max number of lights allowed by the graphic card
    glGetIntegerv(GL_MAX_LIGHTS, &maxLights);
    if( maxLights>16 )
        maxLights = 16;

    // Create the lights array
    if( lights==NULL && maxLights>0 )
    {
        lights = new Light[maxLights];
        NumLights = 3;               // default number of lights
        if( NumLights>maxLights )
            NumLights = maxLights;
        changeLights = true;         // force lights initialization

        // Create a tweak bar for lights
        CreateBar();
    }

    // (Re)initialize lights if needed (uses random values)
    if( changeLights )
        for(int i=0; i<maxLights; ++i)
        {
            lights[i].Dist0     = 0.5f*(float)rand()/(float)RAND_MAX + 0.55f;
            lights[i].Angle0    = 2*M_PI*((float)rand()/(float)RAND_MAX);
            lights[i].Height0   = 2*M_PI*(float)rand()/(float)RAND_MAX;
            lights[i].Speed0    = 4.0f*(float)rand()/(float)RAND_MAX - 2.0f;
            lights[i].Animation = (Light::AnimMode)(Light::ANIM_BOUNCE + (rand()%3));
            lights[i].Radius    = (float)rand()/(float)RAND_MAX+0.05f;
            lights[i].Color[0]  = (float)rand()/(float)RAND_MAX;
            lights[i].Color[1]  = (float)rand()/(float)RAND_MAX;
            lights[i].Color[2]  = (lights[i].Color[0]>lights[i].Color[1]) ? 1.0f-lights[i].Color[1] : 1.0f-lights[i].Color[0];
            lights[i].Color[3]  = 1;
            lights[i].Active    = true;
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
    if( objList>0 )
        glDeleteLists(objList, 1);      // delete previously created display list
    objList = glGenLists(1);
    glNewList(objList, GL_COMPILE);
    DrawSubdivCylinderY(-0.9f, 0, -0.9f, 1.4f, 0.15f, 0.12f, Subdiv/2+8, Subdiv);
    DrawSubdivCylinderY(+0.9f, 0, -0.9f, 1.4f, 0.15f, 0.12f, Subdiv/2+8, Subdiv);
    DrawSubdivCylinderY(+0.9f, 0, +0.9f, 1.4f, 0.15f, 0.12f, Subdiv/2+8, Subdiv);
    DrawSubdivCylinderY(-0.9f, 0, +0.9f, 1.4f, 0.15f, 0.12f, Subdiv/2+8, Subdiv);
    DrawSubdivCylinderY(0, 0, 0, 0.4f, 0.5f, 0.3f, Subdiv+16, Subdiv/8+1);
    DrawSubdivCylinderY(0, 0.4f, 0, 0.05f, 0.3f, 0.0f, Subdiv+16, Subdiv/16+1);
    glEndList();

    // Create ground display list
    if( groundList>0 )
        glDeleteLists(groundList, 1);   // delete previously created display list
    groundList = glGenLists(1);
    glNewList(groundList, GL_COMPILE);
    DrawSubdivPlaneY(-1.2f, 1.2f, 0, -1.2f, 1.2f, (3*Subdiv)/2, (3*Subdiv)/2);
    glEndList();

    // Create display list to draw light halos
    if( haloList>0 )
        glDeleteLists(haloList, 1);     // delete previously created display list
    haloList = glGenLists(1);
    glNewList(haloList, GL_COMPILE);
    DrawSubdivHaloZ(0, 0, 0, 1, 32);
    glEndList();
}


static void handleKeyPressed(const sf::Event::KeyPressed *_Event)
{
    int twMod = 0;
    if (_Event->shift) twMod |= TW_KMOD_SHIFT;
    if (_Event->control) twMod |= TW_KMOD_CTRL;
    if (_Event->alt) twMod |= TW_KMOD_ALT;

    int twKey = 0;
    switch (_Event->code)
    {
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
    if (twKey == 0 && _Event->control)
    {
        // Ctrl+letter/digit shortcuts - sf::Keyboard::Key::A..Z/Num0..Num9
        // are sequential enum values starting at 0, not ASCII, so map them
        // explicitly. Mirrors the GLFW original's own ctrl-fallback (SFML,
        // like GLFW/SDL3, delivers no TextEntered event for Ctrl-held
        // combinations).
        if (_Event->code >= sf::Keyboard::Key::A && _Event->code <= sf::Keyboard::Key::Z)
            twKey = 'A' + ((int)_Event->code - (int)sf::Keyboard::Key::A);
        else if (_Event->code >= sf::Keyboard::Key::Num0 && _Event->code <= sf::Keyboard::Key::Num9)
            twKey = '0' + ((int)_Event->code - (int)sf::Keyboard::Key::Num0);
    }
    if (twKey != 0)
    {
        TwKeyPressed(twKey, twMod);
    }
}

static void handleMouseButtonPressed(const sf::Event::MouseButtonPressed *_Event)
{
    // sf::Mouse::Button::Left/Right/Middle are 0/1/2, NOT matching
    // TW_MOUSE_LEFT/MIDDLE/RIGHT (1/2/3) the way SDL3's numbering happened
    // to - needs its own explicit mapping (see examples/sfml/Triangle_sfml.cpp).
    TwMouseButtonID twButton;
    switch (_Event->button) {
    case sf::Mouse::Button::Left:   twButton = TW_MOUSE_LEFT;   break;
    case sf::Mouse::Button::Right:  twButton = TW_MOUSE_RIGHT;  break;
    case sf::Mouse::Button::Middle: twButton = TW_MOUSE_MIDDLE; break;
    default: return;
    }
    if (TwMouseButton(TW_MOUSE_PRESSED, twButton)) return;

    if (_Event->button == sf::Mouse::Button::Left) {
        g_cameraDragging = true;
        // sf::Event::MouseButtonPressed already carries the cursor position
        // directly - no separate glfwGetCursorPos()-style query needed.
        g_lastMouseX = _Event->position.x;
        g_lastMouseY = _Event->position.y;
    }

    if (_Event->button == sf::Mouse::Button::Right) {
        g_cameraPosX = 0;
        g_cameraPosY = 0;
        g_cameraPosZ = 0; // Reset camera position
    }
}

static void handleMouseButtonReleased(const sf::Event::MouseButtonReleased *_Event)
{
    TwMouseButtonID twButton;
    switch (_Event->button) {
    case sf::Mouse::Button::Left:   twButton = TW_MOUSE_LEFT;   break;
    case sf::Mouse::Button::Right:  twButton = TW_MOUSE_RIGHT;  break;
    case sf::Mouse::Button::Middle: twButton = TW_MOUSE_MIDDLE; break;
    default: return;
    }
    if (TwMouseButton(TW_MOUSE_RELEASED, twButton)) return;

    if (_Event->button == sf::Mouse::Button::Left) {
        g_cameraDragging = false;
    }
}

static void handleMouseMoved(const sf::Window &_Window, const sf::Event::MouseMoved *_Event)
{
    if (TwMouseMotion(_Event->position.x, _Event->position.y)) return;

    if (g_cameraDragging) {
        int dx = _Event->position.x - g_lastMouseX;
        int dy = _Event->position.y - g_lastMouseY;

        // SFML has only one size concept (no separate window-point-size vs.
        // framebuffer-pixel-size split the way GLFW/SDL3 expose for HiDPI),
        // so window.getSize() is directly usable here with no scaling step.
        sf::Vector2u size = _Window.getSize();
        g_cameraPosX += (float)dx / (float)size.x * 2.0f;  // Scale to screen
        g_cameraPosY -= (float)dy / (float)size.y * 2.0f;  // Inverted Y

        g_lastMouseX = _Event->position.x;
        g_lastMouseY = _Event->position.y;
    }
}

static void handleMouseWheelScrolled(const sf::Event::MouseWheelScrolled *_Event)
{
    static double pos = 0;
    pos += _Event->delta;
    g_cameraPosZ -= _Event->delta * 0.1f; // Zoom sensitivity
    if (g_cameraPosZ < -50.0f) g_cameraPosZ = -50.00f; // Prevent too close
    if (g_cameraPosZ > 50.0f) g_cameraPosZ = 50.0f; // Prevent too far

    if (TwMouseWheel((int)pos)) return;
}

// Called once at startup and again on every sf::Event::Resized - mirrors the
// GLFW original's framebuffer-size callback. SFML has only one size concept
// (no HiDPI point-vs-pixel split), so this always operates in the same
// units the window reports.
static void handleResized(unsigned int _Width, unsigned int _Height)
{
    if (_Height == 0) _Height = 1;
    float aspect = (float)_Width / (float)_Height;

    // Projection matrix setup (equivalent to gluPerspective(40, aspect, 1, 10))
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

    // Notify AntTweakBar of the window size
    TwWindowSize((int)_Width, (int)_Height);
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
    Scene *scene = static_cast<Scene *>(clientData); // scene pointer is stored in clientData
    scene->Init(true);                               // re-initialize the scene
}


// Create a tweak bar for lights.
// New enum type and struct type are defined and used by this bar.
void Scene::CreateBar()
{
    // Create a new tweak bar and change its label, position and transparency
    lightsBar = TwNewBar("Lights");
    TwDefine(" Lights label='Lights TweakBar' position='580 16' alpha=0 help='Use this bar to edit the lights in the scene.' ");
    // This bar has no explicit size='...' either, so - like 'Main' above -
    // its panel needs the same explicit scaling of TwBar's fixed 200x320
    // default by g_ContentScaleX/Y (set in main(), see its own comment).
    {
        int lightsBarSize[2] = { (int)(200 * g_ContentScaleX + 0.5f), (int)(320 * g_ContentScaleY + 0.5f) };
        TwSetParam(lightsBar, NULL, "size", TW_PARAM_INT32, 2, lightsBarSize);
    }

    // Add a variable of type int to control the number of lights
    TwAddVarRW(lightsBar, "NumLights", TW_TYPE_INT32, &NumLights,
               " label='Number of lights' keyIncr=l keyDecr=L help='Changes the number of lights in the scene.' ");

    // Set the NumLights min value (=0) and max value (depends on the user graphic card)
    int zero = 0;
    TwSetParam(lightsBar, "NumLights", "min", TW_PARAM_INT32, 1, &zero);
    TwSetParam(lightsBar, "NumLights", "max", TW_PARAM_INT32, 1, &maxLights);
    // Note, TwDefine could also have been used for that pupose like this:
    //   char def[256];
    //   _snprintf(def, 255, "Lights/NumLights min=0 max=%d", maxLights);
    //   TwDefine(def); // min and max are defined using a definition string


    // Add a button to re-initialize the lights; this button calls the ReinitCB callback function
    TwAddButton(lightsBar, "Reinit", ReinitCB, this,
                " label='Change lights' key=c help='Random changes of lights parameters.' ");

    // Define a new enum type for the tweak bar
    TwEnumVal modeEV[] = // array used to describe the Scene::AnimMode enum values
    {
        { Light::ANIM_FIXED,    "Fixed"     },
        { Light::ANIM_BOUNCE,   "Bounce"    },
        { Light::ANIM_ROTATE,   "Rotate"    },
        { Light::ANIM_COMBINED, "Combined"  }
    };
    TwType modeType = TwDefineEnum("Mode", modeEV, 4);  // create a new TwType associated to the enum defined by the modeEV array

    // Define a new struct type: light variables are embedded in this structure
    TwStructMember lightMembers[] = // array used to describe tweakable variables of the Light structure
    {
        { "Active",    TW_TYPE_BOOLCPP, offsetof(Light, Active),    " help='Enable/disable the light.' " },   // Light::Active is a C++ boolean value
        { "Color",     TW_TYPE_COLOR4F, offsetof(Light, Color),     " noalpha help='Light color.' " },        // Light::Color is represented by 4 floats, but alpha channel should be ignored
        { "Radius",    TW_TYPE_FLOAT,   offsetof(Light, Radius),    " min=0 max=4 step=0.02 help='Light radius.' " },
        { "Animation", modeType,        offsetof(Light, Animation), " help='Change the animation mode.' " },  // use the enum 'modeType' created before to tweak the Light::Animation variable
        { "Speed",     TW_TYPE_FLOAT,   offsetof(Light, Speed0),    " readonly=true help='Light moving speed.' " } // Light::Speed is made read-only
    };
    TwType lightType = TwDefineStruct("Light", lightMembers, 5, sizeof(Light), NULL, NULL);  // create a new TwType associated to the struct defined by the lightMembers array

    // Use the newly created 'lightType' to add variables associated with lights
    for(int i=0; i<maxLights; ++i)  // Add 'maxLights' variables of type lightType;
    {                               // unused lights variables (over NumLights) will hidden by Scene::Update( )
        _snprintf(lights[i].Name, sizeof(lights[i].Name), "%d", i+1); // Create a name for each light ("1", "2", "3",...)
        TwAddVarRW(lightsBar, lights[i].Name, lightType, &lights[i], " group='Edit lights' "); // Add a lightType variable and group it into the 'Edit lights' group

        // Set 'label' and 'help' parameters of the light
        char paramValue[64];
        _snprintf(paramValue, sizeof(paramValue), "Light #%d", i+1);
        TwSetParam(lightsBar, lights[i].Name, "label", TW_PARAM_CSTRING, 1, paramValue); // Set label
        _snprintf(paramValue, sizeof(paramValue), "Parameters of the light #%d", i+1);
        TwSetParam(lightsBar, lights[i].Name, "help", TW_PARAM_CSTRING, 1, paramValue);  // Set help

        // Note, parameters could also have been set using the define string of TwAddVarRW like this:
        //   char def[256];
        //   _snprintf(def, sizeof(def), "group='Edit lights' label='Light #%d' help='Parameters of the light #%d' ", i+1, i+1);
        //   TwAddVarRW(lightsBar, lights[i].Name, lightType, &lights[i], def); // Add a lightType variable, group it into the 'Edit lights' group, and name it 'Light #n'
    }
}


// Move lights
void Scene::Update(double time)
{
    float horizSpeed, vertSpeed;
    for(int i=0; i<NumLights; ++i)
    {
        // Change light position according to its current animation mode

        if( lights[i].Animation==Light::ANIM_ROTATE || lights[i].Animation==Light::ANIM_COMBINED )
            horizSpeed = lights[i].Speed0;
        else
            horizSpeed = 0;

        if( lights[i].Animation==Light::ANIM_BOUNCE || lights[i].Animation==Light::ANIM_COMBINED )
            vertSpeed = 1;
        else
            vertSpeed = 0;

        lights[i].Pos[0] = lights[i].Dist0 * (float)cos(horizSpeed*time + lights[i].Angle0);
        lights[i].Pos[1] = (float)fabs(cos(vertSpeed*time + lights[i].Height0));
        lights[i].Pos[2] = lights[i].Dist0 * (float)sin(horizSpeed*time + lights[i].Angle0);
        lights[i].Pos[3] = 1;
    }
}


// Activate OpenGL lights; hide unused lights in the Lights tweak bar;
// and draw the scene. The scene is reflected by the ground plane, so it is
// drawn two times: first reflected, and second normal (unreflected).
void Scene::Draw() const
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
    glRotated(RotYAngle, 0, 1, 0);

    // Hide/active lights
    int i, lightVisible;
    for(i=0; i<maxLights; ++i)
    {
        if( i<NumLights )
        {
            // Lights under NumLights are shown in the Lights tweak bar
            lightVisible = 1;

            // Tell OpenGL to enable or disable the light
            if( lights[i].Active )
                glEnable(GL_LIGHT0+i);
            else
                glDisable(GL_LIGHT0+i);

            // Update OpenGL light parameters (for the reflected scene)
            float reflectPos[4] = { lights[i].Pos[0], -lights[i].Pos[1], lights[i].Pos[2], lights[i].Pos[3] };
            glLightfv(GL_LIGHT0+i, GL_POSITION, reflectPos);
            glLightfv(GL_LIGHT0+i, GL_DIFFUSE, lights[i].Color);
            glLightf(GL_LIGHT0+i, GL_CONSTANT_ATTENUATION, 1);
            glLightf(GL_LIGHT0+i, GL_LINEAR_ATTENUATION, 0);
            glLightf(GL_LIGHT0+i, GL_QUADRATIC_ATTENUATION, 1.0f/(lights[i].Radius*lights[i].Radius));
        }
        else
        {
            // Lights over NumLights are hidden in the Lights tweak bar
            lightVisible = 0;

            // Disable the OpenGL light
            glDisable(GL_LIGHT0+i);

        }

        // Show or hide the light variable in the Lights tweak bar
        TwSetParam(lightsBar, lights[i].Name, "visible", TW_PARAM_INT32, 1, &lightVisible);
    }

    // Set global ambient and clear screen and depth buffer
    float ambient[4] = { Ambient*(BgColor0[0]+BgColor1[0])/2, Ambient*(BgColor0[1]+BgColor1[1])/2,
                         Ambient*(BgColor0[2]+BgColor1[2])/2, 1 };
    glClearColor(ambient[0], ambient[1], ambient[2], 1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);

    // Draw the reflected scene
    glPolygonMode(GL_FRONT_AND_BACK, (Wireframe ? GL_LINE : GL_FILL));
    glCullFace(GL_FRONT);
    glPushMatrix();
    glScalef(1, -1, 1);
    glColor3f(1, 1, 1);
    glCallList(objList);
    DrawHalos(true);
    glPopMatrix();
    glCullFace(GL_BACK);

    // clear depth buffer again
    glClear(GL_DEPTH_BUFFER_BIT);

    // Draw the ground plane (using the Reflection parameter as transparency)
    glColor4f(1, 1, 1, 1.0f-Reflection);
    glCallList(groundList);

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
        glColor3f(BgColor0[0], BgColor0[1], BgColor0[2]);
        glVertex3f(-1, -1, 0.9f);
        glVertex3f(1, -1, 0.9f);
        glColor3f(BgColor1[0], BgColor1[1], BgColor1[2]);
        glVertex3f(1, 1, 0.9f);
        glVertex3f(-1, 1, 0.9f);
    glEnd();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glEnable(GL_LIGHTING);

    // Update light positions for unreflected scene
    for(i=0; i<NumLights; ++i)
        glLightfv(GL_LIGHT0+i, GL_POSITION, lights[i].Pos);

    // Draw the unreflected scene
    glPolygonMode(GL_FRONT_AND_BACK, (Wireframe ? GL_LINE : GL_FILL));
    glColor3f(1, 1, 1);
    glCallList(objList);
    DrawHalos(false);
}


// Subroutine used to draw halos around light positions
void Scene::DrawHalos(bool reflected) const
{
    //glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    float prevAmbient[4];
    glGetFloatv(GL_LIGHT_MODEL_AMBIENT, prevAmbient);
    glPushMatrix();
    glLoadIdentity();
    if( reflected )
        glScalef(1, -1 ,1);
    float black[4] = {0, 0, 0, 1};
    float cr = (float)cos(2*M_PI*RotYAngle/360.0f);
    float sr = (float)sin(2*M_PI*RotYAngle/360.0f);
    for(int i=0; i<NumLights; ++i)
    {
        if( lights[i].Active )
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, lights[i].Color);
        else
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, black);
        glPushMatrix();
        glTranslatef(cr*lights[i].Pos[0]+sr*lights[i].Pos[2], lights[i].Pos[1], -sr*lights[i].Pos[0]+cr*lights[i].Pos[2]);
        //glScalef(0.5f*lights[i].Radius, 0.5f*lights[i].Radius, 1);
        glScalef(0.05f, 0.05f, 1);
        glCallList(haloList);
        glPopMatrix();
    }
    glPopMatrix();
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, prevAmbient);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}


// Subroutine used to build the ground plane display list (mesh subdivision is adjustable)
void Scene::DrawSubdivPlaneY(float xMin, float xMax, float y, float zMin, float zMax, int xSubdiv, int zSubdiv) const
{
    const float FLOAT_EPS = 1.0e-5f;
    float dx = (xMax-xMin)/xSubdiv;
    float dz = (zMax-zMin)/zSubdiv;
    glBegin(GL_QUADS);
    glNormal3f(0, -1, 0);
    for( float z=zMin; z<zMax-FLOAT_EPS; z+=dz )
        for( float x=xMin; x<xMax-FLOAT_EPS; x+=dx )
        {
            glVertex3f(x, y, z);
            glVertex3f(x, y, z+dz);
            glVertex3f(x+dx, y, z+dz);
            glVertex3f(x+dx, y, z);
        }
    glEnd();
}


// Subroutine used to build objects display list (mesh subdivision is adjustable)
void Scene::DrawSubdivCylinderY(float xCenter, float yBottom, float zCenter, float height, float radiusBottom, float radiusTop, int sideSubdiv, int ySubdiv) const
{
    float h0, h1, y0, y1, r0, r1, a0, a1, cosa0, sina0, cosa1, sina1;
    glBegin(GL_QUADS);
    glNormal3f(0, 1, 0);
    for( int j=0; j<ySubdiv; ++j )
        for( int i=0; i<sideSubdiv; ++i )
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
void Scene::DrawSubdivHaloZ(float x, float y, float z, float radius, int subdiv) const
{
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0, 0, 0);
    glColor4f(1, 1, 1, 1);
    glVertex3f(x, y, z);
    for( int i=0; i<=subdiv; ++i )
    {
        glColor4f(1, 1, 1, 0);
        glVertex3f(x+radius*(float)cos(2*M_PI*(float)i/subdiv), x+radius*(float)sin(2*M_PI*(float)i/subdiv), z);
    }
    glEnd();
}


// Callback function called when the 'Subdiv' variable value of the main tweak bar has changed.
void TW_CALL SetSubdivCB(const void *value, void *clientData)
{
    Scene *scene = static_cast<Scene *>(clientData);    // scene pointer is stored in clientData
    scene->Subdiv = *static_cast<const int *>(value);   // copy value to scene->Subdiv
    scene->Init(false);                                 // re-init scene with the new Subdiv parameter
}


// Callback function called by the main tweak bar to get the 'Subdiv' value
void TW_CALL GetSubdivCB(void *value, void *clientData)
{
    Scene *scene = static_cast<Scene *>(clientData);    // scene pointer is stored in clientData
    *static_cast<int *>(value) = scene->Subdiv;         // copy scene->Subdiv to value
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
    TwBar *bar = static_cast<TwBar *>(clientData);
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
    // glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    // glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    // glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required on macOS
    sf::ContextSettings settings;
    // depthBits defaults to 0 (no depth buffer) - unlike GLFW (default 24)
    // and SDL3 - so it must be requested explicitly for GL_DEPTH_TEST
    // below to have any effect; without it, overlapping geometry draws in
    // call order instead of by distance (looks like inverted normals, but
    // isn't - the geometry/winding is fine).
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
    window.setVerticalSyncEnabled(false);

    // Initialize AntTweakBar
    sf::Vector2u initialSize = window.getSize();
    handleResized(initialSize.x, initialSize.y);

    // AntTweakBar draws every widget (buttons, sliders, panel, swatches) at a
    // fixed number of pixels with no DPI awareness; GLFW/SDL3 counterparts of
    // this example scale AntTweakBar's "fontscaling" global parameter by the
    // window's content scale to compensate on HiDPI/Retina displays. SFML
    // exposes no such content-scale query directly (checked the vendored
    // Window/WindowBase headers), but window.getSize() now correctly reports
    // real native pixel dimensions on HiDPI/Retina displays (see vendor/sfml/
    // src/SFML/Window/macOS/SFWindowController.mm's own highDpi fix) - the
    // ratio between that and the logical 800-wide size requested above IS
    // the content scale factor, computed manually here.
    g_ContentScaleX = g_ContentScaleY = (float)initialSize.x / 800.0f;
    if (g_ContentScaleX <= 0.0f) g_ContentScaleX = g_ContentScaleY = 1.0f;
    {
        char fontScalingDef[64];
        snprintf(fontScalingDef, sizeof(fontScalingDef), "GLOBAL fontscaling=%g", (double)g_ContentScaleX);
        TwDefine(fontScalingDef);
    }

    // if (!TwInit(TW_OPENGL_CORE, NULL)) {
    if (!TwInit(TW_OPENGL, NULL)) {
        const char* err = TwGetLastError();
        fprintf(stderr, "TwInit failed: %s\n", err ? err : "Unknown error");
        fflush(stderr);
        return 1;
    }
    TwSetCursorCallback(SFMLCursorCB, &window);
    TwSetClipboardCallback(ClipboardGetSFML, ClipboardSetSFML, NULL);
    // Change the font size, and add a global message to the Help bar.
    TwDefine(" GLOBAL fontSize=3 help='This example illustrates the definition of custom structure type as well as many other features.' ");

    // Initialize the 3D scene
    Scene scene;
    scene.Init(true);

    // Create a tweak bar called 'Main' and change its refresh rate, position, size and transparency
    TwBar *mainBar = TwNewBar("Main");
    TwDefine(" Main label='Main TweakBar' refresh=0.5 position='16 16' alpha=0");
    // The bar's declared size must be scaled the same way fontscaling already
    // scaled its contents above, or the panel and its (now larger) widgets
    // would mismatch again on a HiDPI/Retina display - hence TwSetParam
    // instead of a literal size='260 320' in the TwDefine string above.
    {
        int mainBarSize[2] = { (int)(260 * g_ContentScaleX + 0.5f), (int)(320 * g_ContentScaleY + 0.5f) };
        TwSetParam(mainBar, NULL, "size", TW_PARAM_INT32, 2, mainBarSize);
    }

    // Add some variables to the Main tweak bar
    TwAddVarRW(mainBar, "Wireframe", TW_TYPE_BOOLCPP, &scene.Wireframe,
               " group='Display' key=w help='Toggle wireframe display mode.' "); // 'Wireframe' is put in the group 'Display' (which is then created)
    TwAddVarRW(mainBar, "BgTop", TW_TYPE_COLOR3F, &scene.BgColor1,
               " group='Background' help='Change the top background color.' ");  // 'BgTop' and 'BgBottom' are put in the group 'Background' (which is then created)
    TwAddVarRW(mainBar, "BgBottom", TW_TYPE_COLOR3F, &scene.BgColor0,
               " group='Background' help='Change the bottom background color.' ");
    TwDefine(" Main/Background group='Display' ");  // The group 'Background' of bar 'Main' is put in the group 'Display'
    TwAddVarCB(mainBar, "Subdiv", TW_TYPE_INT32, SetSubdivCB, GetSubdivCB, &scene,
               " group='Scene' label='Meshes subdivision' min=1 max=50 keyincr=s keyDecr=S help='Subdivide the meshes more or less (switch to wireframe to see the effect).' ");
    TwAddVarRW(mainBar, "Ambient", TW_TYPE_FLOAT, &scene.Ambient,
               " label='Ambient factor' group='Scene' min=0 max=1 step=0.001 keyIncr=a keyDecr=A help='Change scene ambient.' ");
    TwAddVarRW(mainBar, "Reflection", TW_TYPE_FLOAT, &scene.Reflection,
               " label='Reflection factor' group='Scene' min=0 max=1 step=0.001 keyIncr=r keyDecr=R help='Change ground reflection.' ");

    // Create a new TwType called rotationType associated with the Scene::RotMode enum, and use it
    TwEnumVal rotationEV[] = { { Scene::ROT_OFF, "Stopped"},
                               { Scene::ROT_CW,  "Clockwise" },
                               { Scene::ROT_CCW, "Counter-clockwise" } };
    TwType rotationType = TwDefineEnum( "Rotation Mode", rotationEV, 3 );
    TwAddVarRW(mainBar, "Rotation", rotationType, &scene.Rotation,
               " group='Scene' keyIncr=Backspace keyDecr=SHIFT+Backspace help='Stop or change the rotation mode.' ");

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
    sf::Clock clock;
    double time = clock.getElapsedTime().asSeconds(), dt = 0; // Current time and elapsed time
    double frameDTime = 0, frameCount = 0, fps = 0;           // Framerate

    bool running = true;
    while (running)
    {
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                running = false;
            } else if (const auto *keyPressed = event->getIf<sf::Event::KeyPressed>()) {
                handleKeyPressed(keyPressed);
            } else if (const auto *textEntered = event->getIf<sf::Event::TextEntered>()) {
                TwKeyPressed((int)textEntered->unicode, 0);
            } else if (const auto *pressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                handleMouseButtonPressed(pressed);
            } else if (const auto *released = event->getIf<sf::Event::MouseButtonReleased>()) {
                handleMouseButtonReleased(released);
            } else if (const auto *moved = event->getIf<sf::Event::MouseMoved>()) {
                handleMouseMoved(window, moved);
            } else if (const auto *wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
                handleMouseWheelScrolled(wheel);
            } else if (const auto *resized = event->getIf<sf::Event::Resized>()) {
                handleResized(resized->size.x, resized->size.y);
            }
        }

        // Get elapsed time
        double now = clock.getElapsedTime().asSeconds();
        dt = now - time;
        if (dt < 0) dt = 0;
        time = now;

        // Rotate scene
        if( scene.Rotation==Scene::ROT_CW )
            scene.RotYAngle -= 5.0*dt;
        else if( scene.Rotation==Scene::ROT_CCW )
            scene.RotYAngle += 5.0*dt;

        // Move lights
        scene.Update(time);

        // Draw scene
        scene.Draw();

        // Draw tweak bar only
        TwDraw();

        window.display();

        // Estimate framerate
        frameCount++;
        frameDTime += dt;
        if( frameDTime>1.0 )
        {
            fps = frameCount/frameDTime;
            char newTitle[128];
            _snprintf(newTitle, sizeof(newTitle), "%s (%.1f fps)", title, fps);
            //window.setTitle(newTitle); // uncomment to display framerate
            frameCount = frameDTime = 0;
        }
    }

    // Terminate AntTweakBar
    TwTerminate();

    return 0;
}
