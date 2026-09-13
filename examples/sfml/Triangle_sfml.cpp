//  ---------------------------------------------------------------------------
//
//  @file       Triangle_sfml.cpp
//  @brief      A simple example that uses AntTweakBar with SFML3 and OpenGL.
//              Draws a triangle and allows the user to tweak its vertex
//              positions and colors, using a custom TwDefineStruct'd 2D point.
//              SFML3 port of examples/glfw/Triangle_glfw.c - see
//              docs/plans/sfml3-backend.md for the backend adapter notes.
//              SFML has no C API at all, so - unlike the SDL3 port, mostly
//              plain C99 - this is real C++.
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              SFML:        https://www.sfml-dev.org
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <SFML/Window.hpp>
#include <AntTweakBar.h>
#include <cstddef>
#include <cstdio>
#include <cmath>
#include <optional>
#include <string>

#define NB_VERTS 3

typedef struct { float X, Y; } Point;

static int g_Angle = 0;
static float g_Scale = 1;
static Point g_Positions[NB_VERTS] = { {0.0f, 0.5f}, {0.5f, -0.5f}, {-0.5f, -0.5f} };
static float g_Colors[NB_VERTS][4] = { {0, 1, 1, 1}, {1, 0, 1, 1}, {1, 1, 0, 1} };
static int g_Width = 640, g_Height = 480;

// Unlike GLFW/SDL3, SFML exposes no window-content-scale/DPI query at all
// (checked the vendored Window/WindowBase headers directly - no such
// method exists), so there is no equivalent of the other two backends'
// examples/*/Triangle_*.c fontscaling adjustment here: AntTweakBar draws
// at its default fixed pixel size on every display, including Retina/HiDPI
// ones, unlike its GLFW3/SDL3 counterparts.

// SFML cursors are process-global (sf::WindowBase::setMouseCursor() takes
// the cursor as a value tied to no particular window ownership model - no
// per-window cursor-ownership fight to route around here, similar to the
// SDL3 port's own finding). sf::Cursor has no copy constructor (move-only),
// so the cache holds std::optional<sf::Cursor> populated via std::move.
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

// sf::Clipboard::getString() returns an sf::String by value (not an owning
// pointer AntTweakBar can hold onto) - convert to a static std::string so
// the returned const char* stays valid for however long AntTweakBar needs
// it after this call returns, same ownership pattern as the SDL3 port's
// own g_ClipboardText (there, freeing an SDL-allocated buffer; here, just
// overwriting a std::string that owns its own storage).
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
            // Set the new cursor before letting the old one's optional be
            // replaced (and destroyed): matches the same precaution as the
            // GLFW3/SDL3 examples' own cursor callbacks.
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
    if (twKey != 0) {
        TwKeyPressed(twKey, twMod);
    }
}

static void handleMouseButton(sf::Mouse::Button _Button, bool _Down)
{
    // Unlike SDL3 (whose TW_MOUSE_LEFT/MIDDLE/RIGHT values were
    // deliberately numbered to match SDL_BUTTON_LEFT/MIDDLE/RIGHT), SFML's
    // sf::Mouse::Button enum (Left=0, Right=1, Middle=2) does NOT line up
    // with AntTweakBar.h's TW_MOUSE_LEFT=1/MIDDLE=2/RIGHT=3 - checked the
    // vendored Mouse.hpp directly rather than assuming the SDL3 coincidence
    // would repeat, so this needs its own explicit mapping.
    TwMouseButtonID twButton;
    switch (_Button) {
    case sf::Mouse::Button::Left:   twButton = TW_MOUSE_LEFT;   break;
    case sf::Mouse::Button::Right:  twButton = TW_MOUSE_RIGHT;  break;
    case sf::Mouse::Button::Middle: twButton = TW_MOUSE_MIDDLE; break;
    default: return;
    }
    TwMouseButton(_Down ? TW_MOUSE_PRESSED : TW_MOUSE_RELEASED, twButton);
}

// Called once at startup and again on every sf::Event::Resized - mirrors
// examples/glfw/Triangle_glfw.c's windowSizeCallback/examples/sdl/
// Triangle_sdl.c's handleWindowPixelSizeChanged, but SFML reports only one
// size (no separate window-point-size-vs-framebuffer-pixel-size split the
// way GLFW/SDL3 expose for HiDPI displays - see the fontscaling comment
// above), so there is no mouse-coordinate scaling step needed here: mouse
// event positions and window.getSize() are already in the same units.
static void handleResized(unsigned int _Width, unsigned int _Height)
{
    if (_Height == 0) _Height = 1;
    g_Width = (int)_Width;
    g_Height = (int)_Height;
    glViewport(0, 0, (GLsizei)_Width, (GLsizei)_Height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if (_Width >= _Height) {
        double aspect = (double)_Width / _Height;
        glOrtho(-aspect, aspect, -1.0, 1.0, -1.0, 1.0);
    } else {
        double aspect = (double)_Height / _Width;
        glOrtho(-1.0, 1.0, -aspect, aspect, -1.0, 1.0);
    }
    glMatrixMode(GL_MODELVIEW);
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
    // Fixed-function GL (glBegin/glEnd below) + AntTweakBar's TW_OPENGL
    // (compatibility, not Core Profile) renderer - request a plain 2.1
    // compatibility context, the same profile examples/glfw/SimpleGL21_glfw.c
    // and examples/sdl/Triangle_sdl.c target.
    sf::ContextSettings settings;
    settings.majorVersion = 2;
    settings.minorVersion = 1;

    sf::Window window(sf::VideoMode(sf::Vector2u((unsigned)g_Width, (unsigned)g_Height)),
                      "AntTweakBar + SFML3 (Triangle)", sf::Style::Default, sf::State::Windowed, settings);

    if (!window.setActive(true)) {
        fprintf(stderr, "Failed to set the SFML window as active\n");
        return 1;
    }

    if (!gladLoadGLLoader((GLADloadproc)sf::Context::getFunction)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        return 1;
    }

    if (!TwInit(TW_OPENGL, NULL)) {
        fprintf(stderr, "AntTweakBar initialization failed: %s\n", TwGetLastError());
        return 1;
    }
    TwSetCursorCallback(SFMLCursorCB, &window);
    TwSetClipboardCallback(ClipboardGetSFML, ClipboardSetSFML, NULL);

    handleResized((unsigned)g_Width, (unsigned)g_Height);

    TwBar *bar = TwNewBar("TweakBar");
    {
        int barSize[2] = { 200, 320 };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with SFML3 and OpenGL.' ");

    TwAddVarRW(bar, "Rotation", TW_TYPE_INT32, &g_Angle,
               " KeyIncr=r KeyDecr=R Help='Rotates the triangle (angle in degree).' ");
    TwAddVarRW(bar, "Scale", TW_TYPE_FLOAT, &g_Scale,
               " Min=-2 Max=2 Step=0.01 KeyIncr=s KeyDecr=S Help='Scales the triangle (1=original size).' ");

    TwStructMember pointMembers[] = {
        { "X", TW_TYPE_FLOAT, offsetof(Point, X), " Min=-1 Max=1 Step=0.01 " },
        { "Y", TW_TYPE_FLOAT, offsetof(Point, Y), " Min=-1 Max=1 Step=0.01 " }
    };
    TwType pointType = TwDefineStruct("POINT", pointMembers, 2, sizeof(Point), NULL, NULL);

    TwAddVarRW(bar, "Color0", TW_TYPE_COLOR4F, &g_Colors[0], " Alpha HLS Group='Vertex 0' Label=Color ");
    TwAddVarRW(bar, "Pos0", pointType, &g_Positions[0], " Group='Vertex 0' Label='Position' ");
    TwAddVarRW(bar, "Color1", TW_TYPE_COLOR4F, &g_Colors[1], " Alpha HLS Group='Vertex 1' Label=Color ");
    TwAddVarRW(bar, "Pos1", pointType, &g_Positions[1], " Group='Vertex 1' Label='Position' ");
    TwAddVarRW(bar, "Color2", TW_TYPE_COLOR4F, &g_Colors[2], " Alpha HLS Group='Vertex 2' Label=Color ");
    TwAddVarRW(bar, "Pos2", pointType, &g_Positions[2], " Group='Vertex 2' Label='Position' ");

    TwAddSeparator(bar, NULL, "");
    TwAddButton(bar, "FullWidthDemoMoreLines", FullWidthLinesCB, bar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");

    bool running = true;
    static double wheelPos = 0;
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
                wheelPos += wheel->delta;
                TwMouseWheel((int)wheelPos);
            } else if (const auto *resized = event->getIf<sf::Event::Resized>()) {
                handleResized(resized->size.x, resized->size.y);
            }
        }

        glClearColor(0.125f, 0.125f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float a = (float)g_Angle * (3.14159265358979f / 180.0f);
        float ca = cosf(a), sa = sinf(a);
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < NB_VERTS; ++i) {
            float x = g_Scale * (ca * g_Positions[i].X - sa * g_Positions[i].Y);
            float y = g_Scale * (sa * g_Positions[i].X + ca * g_Positions[i].Y);
            glColor4fv(g_Colors[i]);
            glVertex2f(x, y);
        }
        glEnd();

        TwDraw();
        window.display();
    }

    TwTerminate();
    return 0;
}
