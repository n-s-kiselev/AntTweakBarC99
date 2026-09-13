//  ---------------------------------------------------------------------------
//
//  @file       SimpleGL21_sfml.cpp
//  @brief      A simple example that uses AntTweakBar with
//              OpenGL 2.1 (compatibility profile) and SFML3.
//
//              Also demonstrates a temporary, self-contained dialog bar:
//              pressing [Esc] shows a "Quit the application?" bar with
//              Yes/No buttons, built at that moment with TwNewBar() and
//              torn down again with TwDeleteBar() - see ShowConfirmQuitBar()
//              below. SFML3 port of examples/glfw/SimpleGL21_glfw.c - see
//              docs/plans/sfml3-backend.md for the backend adapter notes.
//              SFML has no per-window "should close" flag the way GLFW's
//              glfwSetWindowShouldClose()/glfwWindowShouldClose() do, so the
//              confirm-quit dialog's "Yes" button flips a plain bool (a
//              pointer to the main loop's own `running` flag) instead of a
//              window handle.
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
#include <cstdlib>
#include <cmath>
#include <optional>
#include <string>

// Unlike GLFW/SDL3, SFML exposes no window-content-scale/DPI query at all
// (checked the vendored Window/WindowBase headers directly - no such
// method exists), so there is no fontscaling adjustment here.

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

static float g_cameraPosX = 0.0f;
static float g_cameraPosY = 0.0f;
static float g_cameraPosZ = 5.0f;

static bool g_cameraDragging = false;

static int g_lastMouseX = 0;
static int g_lastMouseY = 0;

static char *g_userText = NULL; // Will be malloc'ed on first use

// Quit-confirmation dialog state (see ShowConfirmQuitBar() below).
static TwBar *g_ConfirmBar = NULL; // the "ConfirmQuit" bar, or NULL when not shown

static int g_Width = 800, g_Height = 600;

static void CloseConfirmQuitBar(void);

void TW_CALL ConfirmQuitYesCB(void *clientData)
{
    *static_cast<bool *>(clientData) = false;
}

void TW_CALL ConfirmQuitNoCB(void *clientData)
{
    (void)clientData;
    CloseConfirmQuitBar();
}

// Hides (or shows again) every bar that currently exists - including the
// built-in help bar, which is only ever minimized by default and would
// otherwise still be reachable while the confirmation dialog is up - so
// nothing but the dialog itself can react to input.
static void SetAllBarsVisible(int visible)
{
    int i, barCount = TwGetBarCount();
    char def[128];
    for( i=0; i<barCount; ++i )
    {
        TwBar *b = TwGetBarByIndex(i);
        if( b == g_ConfirmBar )
            continue;
        snprintf(def, sizeof(def), " %s visible=%s ", TwGetBarName(b), visible ? "true" : "false");
        TwDefine(def);
    }
}

// Creates a small centered "Quit the application?" bar with Yes/No buttons,
// and hides every other bar so none of their widgets can react to input
// while the question is pending (AntTweakBar has no built-in modal dialog -
// hiding the other bars is how this demo approximates one).
static void ShowConfirmQuitBar(bool *runningFlag)
{
    char def[160];
    int barWidth, barHeight, posX, posY;

    if( g_ConfirmBar != NULL )
        return; // already showing

    SetAllBarsVisible(0);

    barWidth  = 220;
    barHeight = 80;
    posX = (g_Width  - barWidth)  / 2; if( posX < 0 ) posX = 0;
    posY = (g_Height - barHeight) / 2; if( posY < 0 ) posY = 0;

    g_ConfirmBar = TwNewBar("ConfirmQuit");
    snprintf(def, sizeof(def),
             " ConfirmQuit label='Confirm' size='%d %d' position='%d %d' "
             "resizable=false movable=false iconifiable=false ",
             barWidth, barHeight, posX, posY);
    TwDefine(def);

    TwAddButton(g_ConfirmBar, "Msg", NULL, NULL, " label='Quit the application?' ");
    TwAddButton(g_ConfirmBar, "Yes", ConfirmQuitYesCB, runningFlag, " label='Yes' ");
    TwAddButton(g_ConfirmBar, "No",  ConfirmQuitNoCB,  NULL,        " label='No' ");
    TwSetTopBar(g_ConfirmBar);
}

static void CloseConfirmQuitBar(void)
{
    if( g_ConfirmBar == NULL )
        return;
    TwDeleteBar(g_ConfirmBar);
    g_ConfirmBar = NULL;
    SetAllBarsVisible(1);
}

static void handleKeyPressed(const sf::Event::KeyPressed *_Event, bool *_Running)
{
    if (_Event->code == sf::Keyboard::Key::Escape) {
        if (g_ConfirmBar == NULL)
            ShowConfirmQuitBar(_Running);
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

static void handleMouseButtonPressed(const sf::Event::MouseButtonPressed *_Event)
{
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
        // SFML's mouse-button event already carries the cursor position -
        // no separate glfwGetCursorPos()-style query needed (see
        // docs/plans/sfml3-backend.md).
        g_lastMouseX = _Event->position.x;
        g_lastMouseY = _Event->position.y;
    }
    if (_Event->button == sf::Mouse::Button::Right) {
        g_cameraPosX = 0;
        g_cameraPosY = 0;
        g_cameraPosZ = 5.0f; // Reset camera position
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
    TwMouseButton(TW_MOUSE_RELEASED, twButton);
    if (_Event->button == sf::Mouse::Button::Left)
        g_cameraDragging = false;
}

static void handleMouseMoved(const sf::Event::MouseMoved *_Event)
{
    if (TwMouseMotion(_Event->position.x, _Event->position.y)) return;

    if (g_cameraDragging) {
        int dx = _Event->position.x - g_lastMouseX;
        int dy = _Event->position.y - g_lastMouseY;

        g_cameraPosX += (float)dx / g_Width * 2.0f;  // Scale to screen
        g_cameraPosY -= (float)dy / g_Height * 2.0f; // Inverted Y

        g_lastMouseX = _Event->position.x;
        g_lastMouseY = _Event->position.y;
    }
}

static void handleMouseWheelScrolled(const sf::Event::MouseWheelScrolled *_Event)
{
    static double pos = 0;
    pos += _Event->delta;
    g_cameraPosZ -= _Event->delta * 0.05f; // Zoom sensitivity
    if (g_cameraPosZ < 1.0f) g_cameraPosZ = 1.0f; // Prevent too close
    if (g_cameraPosZ > 50.0f) g_cameraPosZ = 50.0f; // Prevent too far

    TwMouseWheel((int)pos);
}

// Registered on sf::Event::Resized - mirrors the GLFW original's
// FRAMEBUFFER-size callback; SFML has only one size concept, so no
// mouse-coordinate scaling step is needed here.
static void handleResized(unsigned int _Width, unsigned int _Height)
{
    if (_Height == 0) _Height = 1;
    g_Width = (int)_Width;
    g_Height = (int)_Height;
    float aspect = (float)_Width / (float)_Height;
    float near = 1.0f, far = 100.0f;
    float fov = 45.0f;
    float top = tanf(fov * 0.01745329251f) * near;
    float bottom = -top;
    float right = top * aspect;
    float left = -right;

    glViewport(0, 0, (GLsizei)_Width, (GLsizei)_Height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(left, right, bottom, top, near, far);

    TwWindowSize((int)_Width, (int)_Height);
}

void TW_CALL ResetCubePosition(void *clientData)
{
    (void)clientData;
    g_cameraPosX = 0;
    g_cameraPosY = 0;
    g_cameraPosZ = 5.0f; // Reset camera position
}

// Copy function for CDSTRING (required by AntTweakBar)
void TW_CALL CopyCDStringToClient(char **destPtr, const char *src)
{
    size_t len = src ? strlen(src) : 0;
    *destPtr = (char*)realloc(*destPtr, len + 1);
    if (*destPtr) {
        strcpy(*destPtr, src);
    }
}

void TW_CALL PrintTextCallback(void *clientData)
{
    (void)clientData;
    printf("User text: %s\n", g_userText ? g_userText : "(null)");
    fflush(stdout);
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

// This example program draws a possibly transparent cube
void DrawModel(int _wireframe)
{
  int pass, numPass;
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_LIGHT0);
  glEnable(GL_NORMALIZE);
  glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);
  glEnable(GL_COLOR_MATERIAL);
  glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);
  glEnable(GL_LINE_SMOOTH);
  glLineWidth(3.0);

  if( _wireframe )
  {
      glDisable(GL_CULL_FACE);
      glDisable(GL_LIGHTING);
      glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
      numPass = 1;
  }else{
      glEnable(GL_CULL_FACE);
      glFrontFace(GL_CCW);
      glEnable(GL_LIGHTING);
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      numPass = 2;
  }

  for(pass = 0; pass < numPass; ++pass)
  {
    glCullFace( (pass==0) ? GL_FRONT : GL_BACK );

    glBegin(GL_QUADS);
      glNormal3f(0, 0, 1);
      glVertex3f(-0.5f, -0.5f,  0.5f);
      glVertex3f( 0.5f, -0.5f,  0.5f);
      glVertex3f( 0.5f,  0.5f,  0.5f);
      glVertex3f(-0.5f,  0.5f,  0.5f);

      glNormal3f(0, 0, -1);
      glVertex3f( 0.5f, -0.5f, -0.5f);
      glVertex3f(-0.5f, -0.5f, -0.5f);
      glVertex3f(-0.5f,  0.5f, -0.5f);
      glVertex3f( 0.5f,  0.5f, -0.5f);

      glNormal3f(-1, 0, 0);
      glVertex3f(-0.5f, -0.5f, -0.5f);
      glVertex3f(-0.5f, -0.5f,  0.5f);
      glVertex3f(-0.5f,  0.5f,  0.5f);
      glVertex3f(-0.5f,  0.5f, -0.5f);

      glNormal3f(1, 0, 0);
      glVertex3f( 0.5f, -0.5f,  0.5f);
      glVertex3f( 0.5f, -0.5f, -0.5f);
      glVertex3f( 0.5f,  0.5f, -0.5f);
      glVertex3f( 0.5f,  0.5f,  0.5f);

      glNormal3f(0, -1, 0);
      glVertex3f(-0.5f, -0.5f, -0.5f);
      glVertex3f( 0.5f, -0.5f, -0.5f);
      glVertex3f( 0.5f, -0.5f,  0.5f);
      glVertex3f(-0.5f, -0.5f,  0.5f);

      glNormal3f(0, 1, 0);
      glVertex3f(-0.5f,  0.5f,  0.5f);
      glVertex3f( 0.5f,  0.5f,  0.5f);
      glVertex3f( 0.5f,  0.5f, -0.5f);
      glVertex3f(-0.5f,  0.5f, -0.5f);
    glEnd();
  }
}

int main()
{
  double time = 0, dt;
  double turn = 0;
  double speed = 0.3;
  int wire = 0;
  float bgColor[] = { 73.0f/255, 25.0f/255, 100.0f/255 };
  unsigned char cubeColor[] = { 255, 170, 0, 250 };

  sf::ContextSettings settings;
  // depthBits defaults to 0 (no depth buffer) - unlike GLFW (default 24)
  // and SDL3 - so it must be requested explicitly for GL_DEPTH_TEST below
  // to have any effect; without it, overlapping geometry draws in call
  // order instead of by distance (looks like inverted normals, but isn't
  // - the geometry/winding is fine).
  settings.depthBits = 24;
  settings.majorVersion = 2;
  settings.minorVersion = 1;

  sf::Window window(sf::VideoMode(sf::Vector2u((unsigned)g_Width, (unsigned)g_Height)),
                    "AntTweakBar + SFML3 (OpenGL 2.1)", sf::Style::Default, sf::State::Windowed, settings);

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
      fflush(stderr);
      return -3;
  }
  TwSetCursorCallback(SFMLCursorCB, &window);
  TwSetClipboardCallback(ClipboardGetSFML, ClipboardSetSFML, NULL);
  handleResized((unsigned)g_Width, (unsigned)g_Height);
  TwCopyCDStringToClientFunc(CopyCDStringToClient);

  TwBar *bar = TwNewBar("TweakBar");
  TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with SFML3 and OpenGL 2.1. Press [Esc] to quit (with a confirmation dialog).' ");
  TwDefine(" TweakBar color='100 100 50' alpha=200 ");
  {
      int barSize[2] = { 220, 530 };
      TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
  }
  TwAddVarRW(bar, "speed", TW_TYPE_DOUBLE, &speed,
              " label='Rot speed' min=0 max=2 step=0.01 keyIncr=s keyDecr=S help='Rotation speed (turns/second)' ");

  TwAddVarRW(bar, "wire", TW_TYPE_BOOL32, &wire,
              " label='Wireframe mode' key=w help='Toggle wireframe display mode.' ");

  TwAddVarRO(bar, "time", TW_TYPE_DOUBLE, &time, " label='Time' precision=1 help='Time (in seconds).' ");

  TwAddVarRW(bar, "bgColor", TW_TYPE_COLOR3F, &bgColor, " label='Background color' ");

  TwAddVarRW(bar, "cubeColor", TW_TYPE_COLOR32, &cubeColor,
              " label='Cube color' alpha help='Color and transparency of the cube.' ");

  TwAddButton(bar, "Reset Position", ResetCubePosition, NULL,
            " label='Reset Cube Position' key=r help='Reset pan and zoom.' ");

  TwAddVarRW(bar, "Text", TW_TYPE_CDSTRING, &g_userText,
            " label='Input Text' help='Editable dynamic string.' ");

  TwAddButton(bar, "PrintText", PrintTextCallback, NULL,
              " label='Print Text' help='Prints the text to stdout' ");

  TwAddSeparator(bar, NULL, "");
  TwAddButton(bar, "FullWidthDemoMoreLines", FullWidthLinesCB, bar,
              " label='More lines' full_width=true "
              "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
  TwAddVarRW(bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
             " label='Full-width text' full_width=true lines=2 "
             "help='A full-width, wrapped multiline text field.' ");

  sf::Clock clock;
  time = clock.getElapsedTime().asSeconds();

  bool running = true;

  // Main loop (repeated while window is not closed - [Esc] shows a
  // confirmation dialog instead of quitting immediately, see handleKeyPressed())
  while (running)
  {
    while (const std::optional event = window.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            running = false;
        } else if (const auto *keyPressed = event->getIf<sf::Event::KeyPressed>()) {
            handleKeyPressed(keyPressed, &running);
        } else if (const auto *textEntered = event->getIf<sf::Event::TextEntered>()) {
            TwKeyPressed((int)textEntered->unicode, 0);
        } else if (const auto *pressed = event->getIf<sf::Event::MouseButtonPressed>()) {
            handleMouseButtonPressed(pressed);
        } else if (const auto *released = event->getIf<sf::Event::MouseButtonReleased>()) {
            handleMouseButtonReleased(released);
        } else if (const auto *moved = event->getIf<sf::Event::MouseMoved>()) {
            handleMouseMoved(moved);
        } else if (const auto *wheel = event->getIf<sf::Event::MouseWheelScrolled>()) {
            handleMouseWheelScrolled(wheel);
        } else if (const auto *resized = event->getIf<sf::Event::Resized>()) {
            handleResized(resized->size.x, resized->size.y);
        }
    }

    glClearColor(bgColor[0], bgColor[1], bgColor[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    double now = clock.getElapsedTime().asSeconds();
    dt = now - time;
    if (dt < 0) dt = 0;
    time = now;
    turn += speed * dt;

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    GLfloat light_pos[] = { 1.0f, 1.0f, 5.0f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);

    glTranslated(g_cameraPosX, g_cameraPosY, -g_cameraPosZ);
    glRotated(360.0 * turn, 0.4, 1, 0.2);

    glColor4ubv(cubeColor);
    DrawModel(wire);

    TwDraw();
    window.display();
  }

  TwTerminate();

  return 0;
}
