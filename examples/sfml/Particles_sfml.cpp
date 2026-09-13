//  ---------------------------------------------------------------------------
//
//  @file       Particles_sfml.cpp
//  @brief      An example that uses AntTweakBar with SFML3 and OpenGL to draw
//              moving cubic particles, with interactive control over their
//              generation (birth rate, speed, direction, color).
//              SFML3 port of examples/glfw/Particles_glfw.c - see
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

// SFML exposes no direct window-content-scale/DPI query (unlike GLFW's
// glfwGetWindowContentScale/SDL3's SDL_GetWindowDisplayScale) - main()
// below derives the equivalent scale factor manually instead, from the
// ratio between window.getSize() and the logical size requested (see
// vendor/sfml/src/SFML/Window/macOS/SFWindowController.mm's own highDpi
// fix, which makes getSize() report real native pixel dimensions).

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
    default:                     return sf::Cursor::Type::Arrow;
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

#define MAX_PARTICLES 2000

typedef struct {
    float Size;
    float Position[3];
    float Speed[3];
    float RotationAxis[3];
    float RotationAngle;  // in degrees
    float RotationSpeed;
    float Color[3];
    float Age;
    int Alive;
} Particle;

static Particle g_Particles[MAX_PARTICLES];
static int g_Width = 800, g_Height = 600;

static float Random(void)
{
    return 2.0f * ((float)rand() / (double)RAND_MAX) - 1.0f;
}

static void SpawnParticle(Particle *p, float size, const float speedDir[3], float speedNorm, const float color[3])
{
    p->Size = size * (1.0f + 0.2f * Random());
    p->Position[0] = p->Position[1] = p->Position[2] = 0;
    p->Speed[0] = speedNorm * (speedDir[0] + 0.1f * Random());
    p->Speed[1] = speedNorm * (speedDir[1] + 0.1f * Random());
    p->Speed[2] = speedNorm * (speedDir[2] + 0.1f * Random());
    p->RotationAxis[0] = Random();
    p->RotationAxis[1] = Random();
    p->RotationAxis[2] = Random();
    p->RotationAngle = 360.0f * Random();
    p->RotationSpeed = 360.0f * Random();
    p->Color[0] = color[0] + 0.2f * Random();
    p->Color[1] = color[1] + 0.2f * Random();
    p->Color[2] = color[2] + 0.2f * Random();
    p->Age = 0;
    p->Alive = 1;
}

static void UpdateParticle(Particle *p, float dt)
{
    p->Position[0] += dt * p->Speed[0];
    p->Position[1] += dt * p->Speed[1];
    p->Position[2] += dt * p->Speed[2];
    p->Speed[1] -= dt * 9.81f; // gravity
    p->RotationAngle += dt * p->RotationSpeed;
    p->Age += dt;
}

static void setProjection(int width, int height)
{
    float nearPlane = 1.0f, farPlane = 500.0f;
    float fovy = 90.0f * 0.01745329251f;
    float aspect = (float)width / (float)height;
    float top = tanf(fovy * 0.5f) * nearPlane;
    float right = top * aspect;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right, right, -top, top, nearPlane, farPlane);
    glMatrixMode(GL_MODELVIEW);
}

// Matches the GLFW original: Escape quits directly, not forwarded to
// TwKeyPressed.
static void handleKeyPressed(const sf::Event::KeyPressed *_Event, bool *_Running)
{
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
    TwMouseButtonID twButton;
    switch (_Button) {
    case sf::Mouse::Button::Left:   twButton = TW_MOUSE_LEFT;   break;
    case sf::Mouse::Button::Right:  twButton = TW_MOUSE_RIGHT;  break;
    case sf::Mouse::Button::Middle: twButton = TW_MOUSE_MIDDLE; break;
    default: return;
    }
    TwMouseButton(_Down ? TW_MOUSE_PRESSED : TW_MOUSE_RELEASED, twButton);
}

static void handleResized(unsigned int _Width, unsigned int _Height)
{
    if (_Height == 0) _Height = 1;
    g_Width = (int)_Width;
    g_Height = (int)_Height;
    setProjection((int)_Width, (int)_Height);
    TwWindowSize((int)_Width, (int)_Height);
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

int main()
{
    float birthCount = 0;
    float birthRate = 20;
    float maxAge = 3.0f;
    float speedDir[3] = {0, 1, 0};
    float speedNorm = 7.0f;
    float size = 0.1f;
    float color[3] = {0.8f, 0.6f, 0};
    float bgColor[3] = {0, 0.6f, 0.6f};

    sf::ContextSettings settings;
    // depthBits defaults to 0 (no depth buffer) - unlike GLFW (default 24)
    // and SDL3 - so it must be requested explicitly for GL_DEPTH_TEST
    // below to have any effect; without it, overlapping geometry draws in
    // call order instead of by distance (looks like inverted normals, but
    // isn't - the geometry/winding is fine).
    settings.depthBits = 24;
    settings.majorVersion = 2;
    settings.minorVersion = 1;

    sf::Window window(sf::VideoMode(sf::Vector2u((unsigned)g_Width, (unsigned)g_Height)),
                      "AntTweakBar + SFML3 (Particles)", sf::Style::Default, sf::State::Windowed, settings);

    if (!window.setActive(true)) {
        fprintf(stderr, "Failed to set the SFML window as active\n");
        return 1;
    }

    if (!gladLoadGLLoader((GLADloadproc)sf::Context::getFunction)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        return 1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);

    // SFML exposes no direct content-scale/DPI query (unlike GLFW's
    // glfwGetWindowContentScale/SDL3's SDL_GetWindowDisplayScale), but
    // window.getSize() now correctly reports real native pixel dimensions
    // on HiDPI/Retina displays (see vendor/sfml/src/SFML/Window/macOS/
    // SFWindowController.mm's own highDpi fix) - the ratio between that
    // and the logical size we requested IS the content scale factor,
    // computed manually here. AntTweakBar draws every widget at a fixed
    // pixel size with no DPI awareness of its own, so without this the
    // panel would render at half the size of its GLFW3/SDL3 counterparts
    // once the window's actual drawable is native resolution.
    float contentScale = (float)window.getSize().x / (float)g_Width;
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
    TwSetCursorCallback(SFMLCursorCB, &window);
    TwSetClipboardCallback(ClipboardGetSFML, ClipboardSetSFML, NULL);

    handleResized(window.getSize().x, window.getSize().y);

    TwBar *bar = TwNewBar("Particles");
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with SFML3 and OpenGL.' ");
    TwDefine(" Particles position='16 240' ");
    {
        int barSize[2] = { (int)(200 * contentScale + 0.5f), (int)(320 * contentScale + 0.5f) };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }

    TwAddVarRW(bar, "Birth rate", TW_TYPE_FLOAT, &birthRate, " min=0.1 max=100 step=0.1 keyIncr='+' keyDecr='-' ");
    TwAddVarRW(bar, "Speed", TW_TYPE_FLOAT, &speedNorm, " min=0.1 max=10 step=0.1 keyIncr='s' keyDecr='S' ");
    TwAddVarRW(bar, "Direction", TW_TYPE_DIR3F, &speedDir, " opened=true showval=false ");
    TwAddVarRW(bar, "Color", TW_TYPE_COLOR3F, &color, " colorMode=hls opened=true ");
    TwAddVarRW(bar, "Background color", TW_TYPE_COLOR3F, &bgColor, " colorMode=hls opened=true ");

    TwAddSeparator(bar, NULL, "");
    TwAddButton(bar, "FullWidthDemoMoreLines", FullWidthLinesCB, bar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");

    sf::Clock clock;
    float time = 0.0f;
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
            } else if (const auto *wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
                static double wheelPos = 0;
                wheelPos += wheel->delta;
                TwMouseWheel((int)wheelPos);
            } else if (const auto *resized = event->getIf<sf::Event::Resized>()) {
                handleResized(resized->size.x, resized->size.y);
            }
        }

        float now = clock.getElapsedTime().asSeconds();
        float dt = now - time;
        if (dt < 0) dt = 0;
        time = now;

        for (int i = 0; i < MAX_PARTICLES; ++i) {
            if (!g_Particles[i].Alive) continue;
            UpdateParticle(&g_Particles[i], dt);
            if (g_Particles[i].Age >= maxAge) g_Particles[i].Alive = 0;
        }

        birthCount += dt * birthRate;
        while (birthCount >= 1.0f) {
            for (int i = 0; i < MAX_PARTICLES; ++i) {
                if (!g_Particles[i].Alive) {
                    SpawnParticle(&g_Particles[i], size, speedDir, speedNorm, color);
                    break;
                }
            }
            birthCount -= 1.0f;
        }

        glClearColor(bgColor[0], bgColor[1], bgColor[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        for (int i = 0; i < MAX_PARTICLES; ++i) {
            if (!g_Particles[i].Alive) continue;
            Particle *p = &g_Particles[i];

            glColor3fv(p->Color);
            glLoadIdentity();
            glTranslatef(0.0f, -1.0f, -3.0f); // camera position
            glTranslatef(p->Position[0], p->Position[1], p->Position[2]);
            glScalef(p->Size, p->Size, p->Size);
            glRotatef(p->RotationAngle, p->RotationAxis[0], p->RotationAxis[1], p->RotationAxis[2]);

            glBegin(GL_QUADS);
                glNormal3f(0,0,-1); glVertex3f(0,0,0); glVertex3f(0,1,0); glVertex3f(1,1,0); glVertex3f(1,0,0);
                glNormal3f(0,0,+1); glVertex3f(0,0,1); glVertex3f(1,0,1); glVertex3f(1,1,1); glVertex3f(0,1,1);
                glNormal3f(-1,0,0); glVertex3f(0,0,0); glVertex3f(0,0,1); glVertex3f(0,1,1); glVertex3f(0,1,0);
                glNormal3f(+1,0,0); glVertex3f(1,0,0); glVertex3f(1,1,0); glVertex3f(1,1,1); glVertex3f(1,0,1);
                glNormal3f(0,-1,0); glVertex3f(0,0,0); glVertex3f(1,0,0); glVertex3f(1,0,1); glVertex3f(0,0,1);
                glNormal3f(0,+1,0); glVertex3f(0,1,0); glVertex3f(0,1,1); glVertex3f(1,1,1); glVertex3f(1,1,0);
            glEnd();
        }

        TwDraw();
        window.display();
    }

    TwTerminate();
    return 0;
}
