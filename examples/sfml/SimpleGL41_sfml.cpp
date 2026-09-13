//  ---------------------------------------------------------------------------
//
//  @file       SimpleGL41_sfml.cpp
//  @brief      A simple example that uses AntTweakBar with SFML3 and OpenGL
//              3.3 Core Profile (a textured/lit spinning cube via shaders).
//              SFML3 port of examples/glfw/SimpleGL41_glfw.c - see
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
#include <cstdio>
#include <cmath>
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

// This example maps Escape to TW_KEY_ESCAPE like any other key (matching
// the GLFW original's keyCallback) - quitting is driven instead by a
// per-frame sf::Keyboard::isKeyPressed(Escape) poll in the main loop,
// mirroring the GLFW original's own per-frame glfwGetKey(GLFW_KEY_ESCAPE)
// check (see main()).
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
    TwMouseButtonID twButton;
    switch (_Button) {
    case sf::Mouse::Button::Left:   twButton = TW_MOUSE_LEFT;   break;
    case sf::Mouse::Button::Right:  twButton = TW_MOUSE_RIGHT;  break;
    case sf::Mouse::Button::Middle: twButton = TW_MOUSE_MIDDLE; break;
    default: return;
    }
    TwMouseButton(_Down ? TW_MOUSE_PRESSED : TW_MOUSE_RELEASED, twButton);
}

static int g_Width = 800, g_Height = 600;

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

const char* vertexShaderSource = "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec3 aColor;\n"
    "out vec3 ourColor;\n"
    "uniform mat4 model;\n"
    "uniform mat4 view;\n"
    "uniform mat4 projection;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = projection * view * model * vec4(aPos, 1.0);\n"
    "   ourColor = aColor;\n"
    "}\0";

const char* fragmentShaderSource = "#version 330 core\n"
    "in vec3 ourColor;\n"
    "out vec4 FragColor;\n"
    "void main()\n"
    "{\n"
    "   FragColor = vec4(ourColor, 1.0);\n"
    "}\n\0";

float vertices[] = {
    -0.5f,-0.5f,-0.5f, 1.0f,0.0f,0.0f,
     0.5f,-0.5f,-0.5f, 0.0f,1.0f,0.0f,
     0.5f, 0.5f,-0.5f, 0.0f,0.0f,1.0f,
    -0.5f, 0.5f,-0.5f, 1.0f,1.0f,0.0f,

    -0.5f,-0.5f, 0.5f, 0.0f,1.0f,1.0f,
     0.5f,-0.5f, 0.5f, 1.0f,0.0f,1.0f,
     0.5f, 0.5f, 0.5f, 0.5f,0.5f,0.5f,
    -0.5f, 0.5f, 0.5f, 1.0f,1.0f,1.0f
};

unsigned int indices[] = {
    0,1,2, 2,3,0,
    4,5,6, 6,7,4,
    1,5,6, 6,2,1,
    0,4,7, 7,3,0,
    3,2,6, 6,7,3,
    0,1,5, 5,4,0
};

float normals [] = {
            1,0,0,0,
            0,1,0,0,
            0,0,1,0,
            0,0,0,1
        };

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
    double time = 0, dt;
    double speed = 0.3;
    double angle = 0.0;

    // OpenGL 4.1 Core Profile - AntTweakBar's Core Profile renderer
    // (TW_OPENGL_CORE) is required to draw over a Core context.
    sf::ContextSettings settings;
    settings.majorVersion = 4;
    settings.minorVersion = 1;
    settings.attributeFlags = sf::ContextSettings::Core;

    sf::Window window(sf::VideoMode(sf::Vector2u((unsigned)g_Width, (unsigned)g_Height)),
                      "AntTweakBar + SFML3 (OpenGL 4.1 Core)", sf::Style::Default, sf::State::Windowed, settings);

    if (!window.setActive(true)) {
        printf("Failed to set the SFML window as active\n");
        return -1;
    }

    if (!gladLoadGLLoader((GLADloadproc)sf::Context::getFunction)) {
        printf("Failed to initialize GLAD\n");
        return -1;
    }

    printf("OpenGL Version: %s\n", glGetString(GL_VERSION));

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    unsigned int VBO, VAO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glEnable(GL_DEPTH_TEST);

    if (!TwInit(TW_OPENGL_CORE, NULL)) {
        const char* err = TwGetLastError();
        fprintf(stderr, "TwInit failed: %s\n", err ? err : "Unknown error");
        fflush(stderr);
        return 1;
    }
    TwSetCursorCallback(SFMLCursorCB, &window);
    TwSetClipboardCallback(ClipboardGetSFML, ClipboardSetSFML, NULL);
    handleResized((unsigned)g_Width, (unsigned)g_Height);

    TwBar *bar = TwNewBar("TweakBar");
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with SFML3 and OpenGL.' ");
    {
        int barSize[2] = { 200, 320 };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }

    TwAddVarRW(bar, "speed", TW_TYPE_DOUBLE, &speed,
                " label='Rot speed' min=0 max=2 step=0.01 keyIncr=s keyDecr=S help='Rotation speed (turns/second)' ");

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
                static double pos = 0;
                pos += wheel->delta;
                TwMouseWheel((int)pos);
            } else if (const auto *resized = event->getIf<sf::Event::Resized>()) {
                handleResized(resized->size.x, resized->size.y);
            }
        }

        // Extra, redundant per-frame Escape check - matches the GLFW
        // original's own glfwGetKey(GLFW_KEY_ESCAPE) poll (the key-pressed
        // event handler above only forwards Escape to TwKeyPressed(), same
        // as the GLFW original's keyCallback, it does not quit by itself).
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Escape))
            running = false;

        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        double now = clock.getElapsedTime().asSeconds();
        dt = now - time;
        if (dt < 0) dt = 0;
        time = now;
        angle += speed * dt;

        float model[16] = {
            cosf((float)angle), 0.0f, sinf((float)angle), 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            -sinf((float)angle), 0.0f, cosf((float)angle), 0.0f,
            0.0f, 0.0f, -3.0f, 1.0f
        };

        float fov = 45.0f;
        float aspect = 800.0f / 600.0f;
        float near = 0.1f;
        float far = 100.0f;
        float f = 1.0f / tanf(fov * 3.14159f / 360.0f);
        float projection[16] = {
            f/aspect, 0.0f, 0.0f, 0.0f,
            0.0f, f, 0.0f, 0.0f,
            0.0f, 0.0f, (far+near)/(near-far), -1.0f,
            0.0f, 0.0f, (2.0f*far*near)/(near-far), 0.0f
        };

        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, model);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, normals);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, projection);

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        TwDraw();
        window.display();
    }

    TwTerminate();
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteProgram(shaderProgram);

    return 0;
}
