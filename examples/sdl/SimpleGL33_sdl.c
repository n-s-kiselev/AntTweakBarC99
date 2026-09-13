// g++ -std=c++11 MiniExample.cpp glad/src/glad.c -Iglad/include -I/path/to/glm -lSDL3 -ldl -lGL -o cube
// gcc -std=c99 MiniExample3.c glad/src/glad.c -Iglad/include -lSDL3 -framework Cocoa -framework OpenGL -framework IOKit -o cube
#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <AntTweakBar.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

// SDL3 cursors are process-global (SDL_SetCursor() takes no window
// argument, unlike glfwSetCursor()) - there is no per-window cursor-
// ownership fight to route around the way examples/glfw/SimpleGL33.c's own
// comment describes, so this is a plain cache, not a GLFW3-style shim.
static SDL_Cursor *g_StandardCursors[TW_CURSOR_CUSTOM] = { NULL };
static SDL_Cursor *g_LastCustomCursor = NULL;
static bool g_CursorHidden = false;

static SDL_SystemCursor SDLStandardCursorShape(ETwCursor _Cursor)
{
    switch (_Cursor) {
    case TW_CURSOR_ARROW:        return SDL_SYSTEM_CURSOR_DEFAULT;
    case TW_CURSOR_MOVE:         return SDL_SYSTEM_CURSOR_MOVE;
    case TW_CURSOR_RESIZE_WE:    return SDL_SYSTEM_CURSOR_EW_RESIZE;
    case TW_CURSOR_RESIZE_NS:    return SDL_SYSTEM_CURSOR_NS_RESIZE;
    case TW_CURSOR_RESIZE_NESW:  return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    case TW_CURSOR_RESIZE_NWSE:  return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    case TW_CURSOR_HAND:         return SDL_SYSTEM_CURSOR_POINTER;
    case TW_CURSOR_CROSS:        return SDL_SYSTEM_CURSOR_CROSSHAIR;
    case TW_CURSOR_IBEAM:        return SDL_SYSTEM_CURSOR_TEXT;
    case TW_CURSOR_NO:           return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    default:                     return SDL_SYSTEM_CURSOR_DEFAULT; // TW_CURSOR_HELP/UPARROW: no dedicated SDL shape
    }
}

// SDL_GetClipboardText() allocates and must be freed (SDL_free) - unlike
// GLFW's glfwGetClipboardString(), which owns its own buffer. Freeing the
// *previous* call's result (not the one just returned) keeps the pointer
// valid for however long AntTweakBar needs it after this call returns.
static char *g_ClipboardText = NULL;

static const char * TW_CALL ClipboardGetSDL(void *_ClientData)
{
    (void)_ClientData;
    if (g_ClipboardText != NULL) SDL_free(g_ClipboardText);
    g_ClipboardText = SDL_GetClipboardText();
    return g_ClipboardText;
}

static void TW_CALL ClipboardSetSDL(const char *_Text, void *_ClientData)
{
    (void)_ClientData;
    SDL_SetClipboardText(_Text);
}

static void TW_CALL SDLCursorCB(ETwCursor _Cursor, const unsigned char *_RGBA32x32, int _HotX, int _HotY, void *_ClientData)
{
    (void)_ClientData;
    // TW_CURSOR_HIDDEN is an input mode, not a cursor shape: the roto slider
    // hides the pointer while it is dragged. g_CursorHidden remembers that so
    // the mode is restored once, on the next request for a visible cursor.
    if (_Cursor == TW_CURSOR_HIDDEN) {
        SDL_HideCursor();
        g_CursorHidden = true;
        return;
    }
    if (g_CursorHidden) {
        SDL_ShowCursor();
        g_CursorHidden = false;
    }
    if (_Cursor == TW_CURSOR_CUSTOM && _RGBA32x32 != NULL) {
        // SDL_PIXELFORMAT_RGBA32 is the byte-order-independent alias for
        // 8-bit-per-channel RGBA (see SDL_pixels.h) - matches _RGBA32x32's
        // own byte layout exactly, no channel reordering needed.
        SDL_Surface *surface = SDL_CreateSurfaceFrom(32, 32, SDL_PIXELFORMAT_RGBA32,
                                                      (void *)_RGBA32x32, 32 * 4);
        if (surface != NULL) {
            SDL_Cursor *cur = SDL_CreateColorCursor(surface, _HotX, _HotY);
            SDL_DestroySurface(surface); // SDL_CreateColorCursor copies the pixels
            if (cur != NULL) {
                // Set the new cursor before destroying the old one: some
                // platforms reset to the default arrow when the current
                // cursor is destroyed - same precaution as the GLFW3
                // example's own GLFWCursorCB.
                SDL_SetCursor(cur);
                if (g_LastCustomCursor != NULL) SDL_DestroyCursor(g_LastCustomCursor);
                g_LastCustomCursor = cur;
            }
        }
        return;
    }
    if (g_StandardCursors[_Cursor] == NULL)
        g_StandardCursors[_Cursor] = SDL_CreateSystemCursor(SDLStandardCursorShape(_Cursor));
    if (g_StandardCursors[_Cursor] != NULL)
        SDL_SetCursor(g_StandardCursors[_Cursor]);
}

static void DestroySDLCursorCache(void)
{
    for (int i = 0; i < TW_CURSOR_CUSTOM; ++i) {
        if (g_StandardCursors[i] != NULL) {
            SDL_DestroyCursor(g_StandardCursors[i]);
            g_StandardCursors[i] = NULL;
        }
    }
    if (g_LastCustomCursor != NULL) {
        SDL_DestroyCursor(g_LastCustomCursor);
        g_LastCustomCursor = NULL;
    }
    if (g_ClipboardText != NULL) {
        SDL_free(g_ClipboardText);
        g_ClipboardText = NULL;
    }
}

static void handleKeyDown(const SDL_KeyboardEvent *_Event, bool *_Running)
{
    if (_Event->key == SDLK_ESCAPE) {
        *_Running = false;
        return;
    }

    int twMod = 0;
    bool ctrl;
    if (_Event->mod & SDL_KMOD_SHIFT) twMod |= TW_KMOD_SHIFT;
    if ((ctrl = (_Event->mod & SDL_KMOD_CTRL) != 0)) twMod |= TW_KMOD_CTRL;
    if (_Event->mod & SDL_KMOD_ALT) twMod |= TW_KMOD_ALT;

    int twKey = 0;
    switch (_Event->key) {
    case SDLK_BACKSPACE: twKey = TW_KEY_BACKSPACE; break;
    case SDLK_TAB: twKey = TW_KEY_TAB; break;
    case SDLK_RETURN: twKey = TW_KEY_RETURN; break;
    case SDLK_PAUSE: twKey = TW_KEY_PAUSE; break;
    case SDLK_SPACE: twKey = TW_KEY_SPACE; break;
    case SDLK_DELETE: twKey = TW_KEY_DELETE; break;
    case SDLK_UP: twKey = TW_KEY_UP; break;
    case SDLK_DOWN: twKey = TW_KEY_DOWN; break;
    case SDLK_RIGHT: twKey = TW_KEY_RIGHT; break;
    case SDLK_LEFT: twKey = TW_KEY_LEFT; break;
    case SDLK_INSERT: twKey = TW_KEY_INSERT; break;
    case SDLK_HOME: twKey = TW_KEY_HOME; break;
    case SDLK_END: twKey = TW_KEY_END; break;
    case SDLK_PAGEUP: twKey = TW_KEY_PAGE_UP; break;
    case SDLK_PAGEDOWN: twKey = TW_KEY_PAGE_DOWN; break;
    case SDLK_F1: twKey = TW_KEY_F1; break;
    case SDLK_F2: twKey = TW_KEY_F2; break;
    case SDLK_F3: twKey = TW_KEY_F3; break;
    case SDLK_F4: twKey = TW_KEY_F4; break;
    case SDLK_F5: twKey = TW_KEY_F5; break;
    case SDLK_F6: twKey = TW_KEY_F6; break;
    case SDLK_F7: twKey = TW_KEY_F7; break;
    case SDLK_F8: twKey = TW_KEY_F8; break;
    case SDLK_F9: twKey = TW_KEY_F9; break;
    case SDLK_F10: twKey = TW_KEY_F10; break;
    case SDLK_F11: twKey = TW_KEY_F11; break;
    case SDLK_F12: twKey = TW_KEY_F12; break;
    case SDLK_F13: twKey = TW_KEY_F13; break;
    case SDLK_F14: twKey = TW_KEY_F14; break;
    case SDLK_F15: twKey = TW_KEY_F15; break;
    }
    if (twKey == 0 && ctrl && _Event->key < 128) {
        twKey = (int)_Event->key;
    }
    if (twKey != 0) {
        TwKeyPressed(twKey, twMod);
    }
}

// SDL3 delivers typed text as a UTF-8 string per SDL_EVENT_TEXT_INPUT event
// (opt-in via SDL_StartTextInput()), unlike GLFW's one-codepoint-per-call
// char callback - decode it a codepoint at a time and feed each to
// TwKeyPressed(), same as examples/glfw/SimpleGL33.c's charCallback does
// per GLFW callback invocation. Only handles well-formed UTF-8 (1-3 byte
// sequences cover Latin/European scripts, the practical case for this
// library's text-entry widgets); malformed input is skipped rather than
// misinterpreted.
static void handleTextInput(const char *_Utf8Text)
{
    const unsigned char *s = (const unsigned char *)_Utf8Text;
    while (*s != '\0') {
        unsigned int cp = 0;
        int extra = 0;
        if ((*s & 0x80) == 0x00) { cp = *s; extra = 0; }
        else if ((*s & 0xE0) == 0xC0) { cp = *s & 0x1F; extra = 1; }
        else if ((*s & 0xF0) == 0xE0) { cp = *s & 0x0F; extra = 2; }
        else if ((*s & 0xF8) == 0xF0) { cp = *s & 0x07; extra = 3; }
        else { ++s; continue; } // invalid leading byte, skip it
        ++s;
        bool valid = true;
        for (int i = 0; i < extra; ++i) {
            if ((*s & 0xC0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (*s & 0x3F);
            ++s;
        }
        if (valid) TwKeyPressed((int)cp, 0);
    }
}

static void handleMouseButton(const SDL_MouseButtonEvent *_Event)
{
    if (_Event->button == SDL_BUTTON_LEFT || _Event->button == SDL_BUTTON_MIDDLE || _Event->button == SDL_BUTTON_RIGHT) {
        // AntTweakBar.h's TW_MOUSE_LEFT/MIDDLE/RIGHT are deliberately the
        // same values as SDL_BUTTON_LEFT/MIDDLE/RIGHT (1/2/3) - no mapping
        // table needed, unlike the key/cursor cases above.
        TwMouseButton(_Event->down ? TW_MOUSE_PRESSED : TW_MOUSE_RELEASED, (TwMouseButtonID)_Event->button);
    }
}

// GLFW always reports cursor position in window points, but TwWindowSize()
// is now fed framebuffer pixels (see windowSizeCallback below), so mouse
// events must be scaled by this window/framebuffer ratio before reaching
// AntTweakBar, or its hit-testing/drawing (now in pixel space) would
// misread a point-space cursor position - see docs/plans/examples-hidpi-scaling.md.
static double g_MouseScaleX = 1.0, g_MouseScaleY = 1.0;

static void updateMouseScale(SDL_Window *_Window, int _PixelWidth, int _PixelHeight)
{
    int pointWidth = _PixelWidth, pointHeight = _PixelHeight;
    SDL_GetWindowSize(_Window, &pointWidth, &pointHeight);
    g_MouseScaleX = (pointWidth > 0) ? (double)_PixelWidth / pointWidth : 1.0;
    g_MouseScaleY = (pointHeight > 0) ? (double)_PixelHeight / pointHeight : 1.0;
}

// Called once at startup (with the framebuffer's actual initial pixel
// size) and again on every SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED - mirrors
// examples/glfw/SimpleGL33.c's windowSizeCallback (registered as GLFW's
// FRAMEBUFFER size callback there for the same pixel-vs-point reason).
static void handleWindowPixelSizeChanged(SDL_Window *_Window, int _Width, int _Height)
{
    if (_Height == 0) _Height = 1;
    float aspect = (float)_Width / (float)_Height;
    float near = 1.0f, far = 100.0f;
    float fov = 45.0f;
    float top = tanf(fov * 0.01745329251f) * near;
    float bottom = -top;
    float right = top * aspect;
    float left = -right;
    (void)left; (void)right; (void)bottom; (void)top; (void)near; (void)far; // used only in the GL21/frustum variant; kept for parity of structure

    glViewport(0, 0, _Width, _Height);

    TwWindowSize(_Width, _Height);
    updateMouseScale(_Window, _Width, _Height);
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

int main(void)
{
    TwBar *bar;         // Pointer to a tweak bar

    double time = 0, dt;// Current time and enlapsed time
    double speed = 0.3; // Model rotation speed
    double angle = 0.0;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return -1;
    }

    // OpenGL 3.3 Core Profile, forward-compatible - same profile
    // examples/glfw/SimpleGL33.c targets.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

    // Requested size is in "reference" (96 DPI) pixels; grow the actual
    // window to match the monitor's real pixel density on platforms where
    // window size and framebuffer size are otherwise always 1:1 (Windows,
    // X11) - a no-op on macOS, which already does this by definition (see
    // docs/plans/examples-hidpi-scaling.md).
    SDL_Window *window = SDL_CreateWindow("AntTweakBar + SDL3 (OpenGL 3.3 Core)", 800, 600,
                                           SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == NULL) {
        fprintf(stderr, "Failed to create SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (ctx == NULL) {
        fprintf(stderr, "Cannot create GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // Load OpenGL functions
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        printf("Failed to initialize GLAD\n");
        return -1;
    }

    printf("OpenGL Version: %s\n", glGetString(GL_VERSION));

    // Build and compile shaders
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

    // Set up vertex data and buffers
    unsigned int VBO, VAO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Color attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Enable depth testing
    glEnable(GL_DEPTH_TEST);

    // AntTweakBar draws every widget at a fixed pixel size with no DPI
    // awareness, so on a HiDPI/Retina display it looks too large/blurry
    // relative to a standard display (see docs/plans/examples-hidpi-scaling.md).
    // Scaling "fontscaling" (set via TwDefine, before TwInit) by the
    // window's content scale keeps it a comparable physical size; on a
    // standard display the content scale is 1.0, so this is a no-op there.
    float contentScale = SDL_GetWindowDisplayScale(window);
    if (contentScale <= 0.0f) contentScale = 1.0f;
    {
        char fontScalingDef[64];
        snprintf(fontScalingDef, sizeof(fontScalingDef), "GLOBAL fontscaling=%g", (double)contentScale);
        TwDefine(fontScalingDef);
    }

    // Initialize AntTweakBar
    if (!TwInit(TW_OPENGL_CORE, NULL)) {
        const char* err = TwGetLastError();
        fprintf(stderr, "TwInit failed: %s\n", err ? err : "Unknown error");
        fflush(stderr);
        return 1;
    }
    TwSetCursorCallback(SDLCursorCB, NULL); // SDL cursors are process-global, no window needed
    TwSetClipboardCallback(ClipboardGetSDL, ClipboardSetSDL, NULL);
    SDL_StartTextInput(window);
    {
        int width, height;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        handleWindowPixelSizeChanged(window, width, height);
    }

    // Create a tweak bar
    bar = TwNewBar("TweakBar");
    TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with SDL3 and OpenGL.' "); // Message added to the help bar.
    {
        // TwBar's fixed 200x320 default doesn't grow with the now-larger
        // scaled contents above, so it must be scaled explicitly too.
        int barSize[2] = { (int)(200 * contentScale + 0.5f), (int)(320 * contentScale + 0.5f) };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }

    // Add 'speed' to 'bar': it is a modifable (RW) variable of type TW_TYPE_DOUBLE. Its key shortcuts are [s] and [S].
    TwAddVarRW(bar, "speed", TW_TYPE_DOUBLE, &speed,
                " label='Rot speed' min=0 max=2 step=0.01 keyIncr=s keyDecr=S help='Rotation speed (turns/second)' ");

    TwAddSeparator(bar, NULL, "");
    TwAddButton(bar, "FullWidthDemoMoreLines", FullWidthLinesCB, bar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");

    // Initialize time
    time = (double)SDL_GetTicksNS() / 1e9;

    bool running = true;
    static double wheelPos = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                handleKeyDown(&event.key, &running);
                break;
            case SDL_EVENT_TEXT_INPUT:
                handleTextInput(event.text.text);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                handleMouseButton(&event.button);
                break;
            case SDL_EVENT_MOUSE_MOTION:
                TwMouseMotion((int)(event.motion.x * g_MouseScaleX), (int)(event.motion.y * g_MouseScaleY));
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                wheelPos += event.wheel.y;
                TwMouseWheel((int)wheelPos);
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                handleWindowPixelSizeChanged(window, event.window.data1, event.window.data2);
                break;
            default:
                break;
            }
        }

        // Also honor a plain, per-frame Escape check the same way
        // examples/glfw/SimpleGL33.c's glfwGetKey(window, GLFW_KEY_ESCAPE)
        // poll did, on top of the key-down event above.
        if (SDL_GetKeyboardState(NULL)[SDL_SCANCODE_ESCAPE]) {
            running = false;
        }

        // Clear screen
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Activate shader
        glUseProgram(shaderProgram);

        // Create transformations
        dt = (double)SDL_GetTicksNS() / 1e9 - time;
        if (dt < 0) dt = 0;
        time += dt;
        angle += speed * dt;

        // Model matrix
        float model[16] = {
            cosf((float)angle), 0.0f, sinf((float)angle), 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            -sinf((float)angle), 0.0f, cosf((float)angle), 0.0f,
            0.0f, 0.0f, -3.0f, 1.0f
        };

        // Projection matrix
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

        // Set uniforms
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, model);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, normals);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, projection);

        // Render cube
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        // Draw tweak bar only
        TwDraw();
        // Swap buffers
        SDL_GL_SwapWindow(window);
    }

    TwTerminate();
    DestroySDLCursorCache();
    // Cleanup
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteProgram(shaderProgram);

    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
