//  ---------------------------------------------------------------------------
//
//  @file       MultiWindow_sfml.cpp
//  @brief      Demonstrates running two independent AntTweakBar-managed
//              windows in one process, each with its own tweak bar, using
//              SFML3.
//              SFML3 port of examples/glfw/MultiWindow_glfw.c - see
//              docs/plans/sfml3-backend.md for the backend adapter notes.
//
//              Unlike the GLFW3 (explicit `share` window param) and SDL3
//              (SDL_GL_SHARE_WITH_CURRENT_CONTEXT attribute) ports, SFML
//              needs no explicit "share this context with that one" step
//              at all: its own internal GlResource/GlContext machinery
//              (see vendor/sfml/include/SFML/Window/GlResource.hpp and
//              vendor/sfml/src/SFML/Window/GlContext.hpp's
//              getSharedContext()) automatically shares GL resources
//              across every context created in the same process. Also
//              unlike GLFW/SDL3, sf::Window::pollEvent() is a per-window
//              member function (each window has its own event queue), not
//              one global queue keyed by a window-user-pointer/window-ID -
//              so the main loop below polls each DemoWindow's own window
//              directly, with no DemoWindowFor()-style lookup needed at
//              all (see the GLFW original's own DemoWindowFor(), now
//              unnecessary here).
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              SFML:        https://www.sfml-dev.org
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <SFML/Window.hpp>
#include <AntTweakBar.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <optional>
#include <string>

#define NUM_WINDOWS 2

typedef struct
{
    sf::Window *window;
    int         twWindowID;   // AntTweakBar's per-window manager ID (0 and 1)
    TwBar      *bar;
    double      speed;        // rotation speed (turns/second)
    double      turn;         // current rotation, in turns
    int         wire;         // wireframe toggle
    float       bgColor[3];
    char        fullWidthText[300]; // full_width=true multiline demo text - see SetupWindow()
} DemoWindow;

static DemoWindow g_Windows[NUM_WINDOWS];

// Window content scale: SFML has no content-scale/DPI query at all (unlike
// GLFW/SDL3 - checked the vendored headers directly), so unlike the GLFW
// original there is no g_ContentScaleX/Y here and no fontscaling TwDefine()
// call - a known, permanent, documented limitation of this backend.

// AntTweakBar's cursor callback (TwSetCursorCallback, installed once below)
// is a single, process-wide hook - it is not aware of which of our two
// windows the pointer is currently over. g_ActiveWindow tracks that
// ourselves (updated by whichever window's event loop is currently being
// polled) so SFMLCursorCB knows which sf::Window to call setMouseCursor()
// on - same reasoning, and same tracking need, as the GLFW original's own
// g_ActiveWindow.
static sf::Window *g_ActiveWindow = NULL;

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
    (void)_ClientData;
    sf::Window *window = g_ActiveWindow;
    if (window == NULL) return;

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

// This example program draws a possibly transparent cube (identical to
// SimpleGL21.c's DrawModel() - kept self-contained here rather than
// shared across example files).
static void DrawCube(int wireframe)
{
    int pass, numPass;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);

    if (wireframe) {
        glDisable(GL_CULL_FACE);
        glDisable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        numPass = 1;
    } else {
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CCW);
        glEnable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        numPass = 2;
    }

    for (pass = 0; pass < numPass; ++pass) {
        glCullFace((pass == 0) ? GL_FRONT : GL_BACK);
        glBegin(GL_QUADS);
            glNormal3f(0, 0, 1);
            glVertex3f(-0.5f, -0.5f,  0.5f); glVertex3f( 0.5f, -0.5f,  0.5f);
            glVertex3f( 0.5f,  0.5f,  0.5f); glVertex3f(-0.5f,  0.5f,  0.5f);
            glNormal3f(0, 0, -1);
            glVertex3f( 0.5f, -0.5f, -0.5f); glVertex3f(-0.5f, -0.5f, -0.5f);
            glVertex3f(-0.5f,  0.5f, -0.5f); glVertex3f( 0.5f,  0.5f, -0.5f);
            glNormal3f(-1, 0, 0);
            glVertex3f(-0.5f, -0.5f, -0.5f); glVertex3f(-0.5f, -0.5f,  0.5f);
            glVertex3f(-0.5f,  0.5f,  0.5f); glVertex3f(-0.5f,  0.5f, -0.5f);
            glNormal3f(1, 0, 0);
            glVertex3f( 0.5f, -0.5f,  0.5f); glVertex3f( 0.5f, -0.5f, -0.5f);
            glVertex3f( 0.5f,  0.5f, -0.5f); glVertex3f( 0.5f,  0.5f,  0.5f);
            glNormal3f(0, -1, 0);
            glVertex3f(-0.5f, -0.5f, -0.5f); glVertex3f( 0.5f, -0.5f, -0.5f);
            glVertex3f( 0.5f, -0.5f,  0.5f); glVertex3f(-0.5f, -0.5f,  0.5f);
            glNormal3f(0, 1, 0);
            glVertex3f(-0.5f,  0.5f,  0.5f); glVertex3f( 0.5f,  0.5f,  0.5f);
            glVertex3f( 0.5f,  0.5f, -0.5f); glVertex3f(-0.5f,  0.5f, -0.5f);
        glEnd();
    }
}

static void handleResized(DemoWindow *dw, unsigned int _Width, unsigned int _Height)
{
    if (_Height == 0) _Height = 1;

    (void)dw->window->setActive(true); // glViewport()/projection below are per-context state
    float aspect = (float)_Width / (float)_Height;
    float znear = 1.0f, zfar = 100.0f, fov = 45.0f;
    float top = tanf(fov * 0.01745329251f) * znear;
    float bottom = -top, right = top * aspect, left = -right;

    glViewport(0, 0, (GLsizei)_Width, (GLsizei)_Height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(left, right, bottom, top, znear, zfar);

    TwSetCurrentWindow(dw->twWindowID);
    TwWindowSize((int)_Width, (int)_Height);
}

static void handleKeyPressed(DemoWindow *dw, const sf::Event::KeyPressed *_Event)
{
    g_ActiveWindow = dw->window;
    TwSetCurrentWindow(dw->twWindowID);

    int twMod = 0;
    if (_Event->shift) twMod |= TW_KMOD_SHIFT;
    if (_Event->control) twMod |= TW_KMOD_CTRL;
    if (_Event->alt) twMod |= TW_KMOD_ALT;

    int twKey = 0;
    switch (_Event->code) {
    case sf::Keyboard::Key::Backspace: twKey = TW_KEY_BACKSPACE; break;
    case sf::Keyboard::Key::Tab: twKey = TW_KEY_TAB; break;
    case sf::Keyboard::Key::Enter: twKey = TW_KEY_RETURN; break;
    case sf::Keyboard::Key::Escape: twKey = TW_KEY_ESCAPE; break;
    case sf::Keyboard::Key::Space: twKey = TW_KEY_SPACE; break;
    case sf::Keyboard::Key::Delete: twKey = TW_KEY_DELETE; break;
    case sf::Keyboard::Key::Up: twKey = TW_KEY_UP; break;
    case sf::Keyboard::Key::Down: twKey = TW_KEY_DOWN; break;
    case sf::Keyboard::Key::Right: twKey = TW_KEY_RIGHT; break;
    case sf::Keyboard::Key::Left: twKey = TW_KEY_LEFT; break;
    case sf::Keyboard::Key::Home: twKey = TW_KEY_HOME; break;
    case sf::Keyboard::Key::End: twKey = TW_KEY_END; break;
    case sf::Keyboard::Key::PageUp: twKey = TW_KEY_PAGE_UP; break;
    case sf::Keyboard::Key::PageDown: twKey = TW_KEY_PAGE_DOWN; break;
    default: break;
    }
    // Ctrl+letter/digit shortcuts - sf::Keyboard::Key::A..Z/Num0..Num9 are
    // sequential enum values starting at 0, not ASCII, so map them
    // explicitly. Mirrors the GLFW original's own ctrl-fallback (SFML, like
    // GLFW/SDL3, delivers no TextEntered event for Ctrl-held combinations).
    if (twKey == 0 && _Event->control) {
        if (_Event->code >= sf::Keyboard::Key::A && _Event->code <= sf::Keyboard::Key::Z)
            twKey = 'A' + ((int)_Event->code - (int)sf::Keyboard::Key::A);
        else if (_Event->code >= sf::Keyboard::Key::Num0 && _Event->code <= sf::Keyboard::Key::Num9)
            twKey = '0' + ((int)_Event->code - (int)sf::Keyboard::Key::Num0);
    }
    if (twKey != 0) TwKeyPressed(twKey, twMod);
}

static void handleMouseButton(DemoWindow *dw, sf::Mouse::Button _Button, bool _Down)
{
    g_ActiveWindow = dw->window;
    TwSetCurrentWindow(dw->twWindowID);

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

// full_width=true demo: a multiline text widget spanning the whole row, and a button
// below it that cycles the text widget's "lines=" value 2->3->4->5->6->2->..., changing
// the existing widget's attribute at runtime via TwDefine rather than recreating it.
// clientData is the specific window's own bar, so each of the two windows in this
// example cycles its own widget independently.
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

// Creates one SFML window, assigns it an AntTweakBar window ID (creating
// that manager immediately), and adds its tweak bar. windowIndex 0 must be
// called after nothing special (its manager is the master one, ID 0,
// implicitly tied to whatever context is current when TwInit() runs);
// windowIndex 1 calls TwSetCurrentWindow() with a fresh ID to lazily
// create its manager. Both windows automatically share one GL object
// namespace - see the file header comment - so no explicit "share" step
// is needed here, unlike the GLFW/SDL3 ports of this same example.
static bool SetupWindow(int windowIndex, sf::Window *window, const char *title, float r, float g, float b)
{
    DemoWindow *dw = &g_Windows[windowIndex];

    sf::ContextSettings settings;
    settings.majorVersion = 2;
    settings.minorVersion = 1;
    window->create(sf::VideoMode(sf::Vector2u(500, 500)), title, sf::Style::Default, sf::State::Windowed, settings);

    dw->window = window;
    dw->twWindowID = windowIndex; // arbitrary but must be unique and stable
    dw->speed = 0.2 + 0.15 * windowIndex;
    dw->turn = 0.0;
    dw->wire = 0;
    dw->bgColor[0] = r; dw->bgColor[1] = g; dw->bgColor[2] = b;

    if (!window->setActive(true)) {
        fprintf(stderr, "Failed to activate SFML window '%s'\n", title);
        return false;
    }

    if (windowIndex == 0) {
        // First window: load GLAD once (function pointers are valid across
        // every context sharing this one's object namespace) and
        // initialize AntTweakBar's master manager.
        if (!gladLoadGLLoader((GLADloadproc)sf::Context::getFunction)) {
            fprintf(stderr, "Failed to initialize GLAD\n");
            return false;
        }
        if (!TwInit(TW_OPENGL, NULL)) {
            fprintf(stderr, "TwInit failed: %s\n", TwGetLastError());
            return false;
        }
        TwSetCursorCallback(SFMLCursorCB, NULL);
        TwSetClipboardCallback(ClipboardGetSFML, ClipboardSetSFML, NULL);
    } else {
        // Later windows: TwSetCurrentWindow() with a fresh ID lazily
        // creates this window's own CTwMgr/renderer, tied to the context
        // made current just above.
        if (!TwSetCurrentWindow(dw->twWindowID)) {
            fprintf(stderr, "TwSetCurrentWindow(%d) failed to create a manager\n", dw->twWindowID);
            return false;
        }
    }

    handleResized(dw, 500, 500);

    dw->bar = TwNewBar("TweakBar");
    TwDefine(" GLOBAL help='Two independent AntTweakBar-managed SFML3 windows in one process.' ");
    {
        int barSize[2] = { 200, 150 };
        TwSetParam(dw->bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }
    TwAddVarRW(dw->bar, "speed", TW_TYPE_DOUBLE, &dw->speed,
               " label='Rot speed' min=0 max=2 step=0.01 help='Rotation speed (turns/second)' ");
    TwAddVarRW(dw->bar, "wire", TW_TYPE_BOOL32, &dw->wire,
               " label='Wireframe' help='Toggle wireframe display mode.' ");
    TwAddVarRW(dw->bar, "bgColor", TW_TYPE_COLOR3F, &dw->bgColor,
               " label='Background color' ");

    strcpy(dw->fullWidthText,
        "This is a full-width widget. You can enter long text that spans multiple lines. "
        "The text is automatically wrapped to fit the available width. Clicking the "
        "full-width button above increases the number of visible lines up to 6, then "
        "resets it back to 2.");
    TwAddSeparator(dw->bar, NULL, "");
    TwAddButton(dw->bar, "FullWidthDemoMoreLines", FullWidthLinesCB, dw->bar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(dw->bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(dw->fullWidthText)), dw->fullWidthText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");

    return true;
}

int main()
{
    // sf::Window default-constructs without opening a window; SetupWindow()
    // opens each one via create() (matches the GLFW original's two-call
    // SetupWindow(0, ...)/SetupWindow(1, ...) shape).
    sf::Window windowAStorage, windowBStorage;
    sf::Window *windowA = &windowAStorage;
    sf::Window *windowB = &windowBStorage;

    if (!SetupWindow(0, windowA, "MultiWindow - Window A", 0.15f, 0.15f, 0.35f))
        return 1;
    // Window B automatically shares window A's context object namespace
    // (see the file header comment) - no explicit share step needed.
    if (!SetupWindow(1, windowB, "MultiWindow - Window B", 0.35f, 0.15f, 0.15f))
        return 1;

    g_ActiveWindow = windowA;
    sf::Clock clock;
    double lastTime = clock.getElapsedTime().asSeconds();

    while (windowA->isOpen() && windowB->isOpen())
    {
        for (int i = 0; i < NUM_WINDOWS; ++i) {
            DemoWindow *dw = &g_Windows[i];
            while (const std::optional event = dw->window->pollEvent()) {
                if (event->is<sf::Event::Closed>()) {
                    dw->window->close();
                } else if (const auto *keyPressed = event->getIf<sf::Event::KeyPressed>()) {
                    handleKeyPressed(dw, keyPressed);
                } else if (const auto *textEntered = event->getIf<sf::Event::TextEntered>()) {
                    g_ActiveWindow = dw->window;
                    TwSetCurrentWindow(dw->twWindowID);
                    TwKeyPressed((int)textEntered->unicode, 0);
                } else if (const auto *pressed = event->getIf<sf::Event::MouseButtonPressed>()) {
                    handleMouseButton(dw, pressed->button, true);
                } else if (const auto *released = event->getIf<sf::Event::MouseButtonReleased>()) {
                    handleMouseButton(dw, released->button, false);
                } else if (const auto *moved = event->getIf<sf::Event::MouseMoved>()) {
                    g_ActiveWindow = dw->window;
                    TwSetCurrentWindow(dw->twWindowID);
                    TwMouseMotion(moved->position.x, moved->position.y);
                } else if (const auto *wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
                    static double pos[NUM_WINDOWS] = { 0 };
                    g_ActiveWindow = dw->window;
                    TwSetCurrentWindow(dw->twWindowID);
                    pos[dw->twWindowID] += wheel->delta;
                    TwMouseWheel((int)pos[dw->twWindowID]);
                } else if (const auto *resized = event->getIf<sf::Event::Resized>()) {
                    handleResized(dw, resized->size.x, resized->size.y);
                }
            }
        }

        double now = clock.getElapsedTime().asSeconds();
        double dt = now - lastTime;
        if (dt < 0) dt = 0;
        lastTime = now;

        for (int i = 0; i < NUM_WINDOWS; ++i) {
            DemoWindow *dw = &g_Windows[i];
            dw->turn += dw->speed * dt;

            (void)dw->window->setActive(true);
            TwSetCurrentWindow(dw->twWindowID);

            glClearColor(dw->bgColor[0], dw->bgColor[1], dw->bgColor[2], 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            GLfloat lightPos[] = { 1.0f, 1.0f, 5.0f, 1.0f };
            glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
            glTranslated(0, 0, -3.0);
            glRotated(360.0 * dw->turn, 0.4, 1, 0.2);
            glColor3f(0.9f, 0.7f, 0.1f);
            DrawCube(dw->wire);

            TwDraw();
            dw->window->display();
        }
    }

    // A shared context must be current for TwTerminate()'s internal loop
    // over every window's manager to validly delete their (shared) GL
    // objects.
    (void)windowA->setActive(true);
    TwTerminate();

    windowB->close();
    windowA->close();

    return 0;
}
