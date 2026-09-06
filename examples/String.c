//  ---------------------------------------------------------------------------
//
//  @file       String.c
//  @brief      This example illustrates the use of AntTweakBar's C-compatible
//              string variable types: a dynamically-allocated C string
//              (TW_TYPE_CDSTRING) and a fixed-size C string
//              (TW_TYPE_CSSTRING(n)).
//
//              Ported from the legacy GLUT/C++ example String.cpp, which
//              also demonstrated a third type, TW_TYPE_STDSTRING (bound to a
//              C++ std::string). That type cannot be named in a C99
//              translation unit at all, so its section of the original demo
//              (creating a new tweak bar with a std::string-edited title)
//              was dropped rather than ported - this is a permanent,
//              intentional omission for the C99 port, not a bug. Whether
//              TW_TYPE_STDSTRING itself survives anywhere in the C99
//              library's public API is tracked separately as an open
//              question in docs/plans/c99-rewrite.md.
//
//              The graphic window is created by GLFW3 (the original used
//              GLUT); this example has no 3D content of its own, so the
//              GLFW3/AntTweakBar integration boilerplate below is the same
//              minimal shape used by every other GLFW3 example in this
//              folder (see e.g. SimpleGL21.c).
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              GLFW:        http://www.glfw.org
//
//  @author     Philippe Decaudin
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <AntTweakBar.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// GLFW3 cursor binding (see docs/glfw3-cursor-integration.md and
// SimpleGL21.c): routes every AntTweakBar cursor change through
// glfwSetCursor() instead of AntTweakBar setting the system cursor natively.
static GLFWcursor* g_StandardCursors[TW_CURSOR_CUSTOM] = { NULL };
static GLFWcursor* g_LastCustomCursor = NULL;
static int g_CursorHidden = 0;

static int GLFWStandardCursorShape(ETwCursor _Cursor)
{
    switch (_Cursor) {
    case TW_CURSOR_ARROW:        return GLFW_ARROW_CURSOR;
    case TW_CURSOR_MOVE:         return GLFW_RESIZE_ALL_CURSOR;
    case TW_CURSOR_RESIZE_WE:    return GLFW_RESIZE_EW_CURSOR;
    case TW_CURSOR_RESIZE_NS:    return GLFW_RESIZE_NS_CURSOR;
    case TW_CURSOR_RESIZE_NESW:  return GLFW_RESIZE_NESW_CURSOR;
    case TW_CURSOR_RESIZE_NWSE:  return GLFW_RESIZE_NWSE_CURSOR;
    case TW_CURSOR_HAND:         return GLFW_POINTING_HAND_CURSOR;
    case TW_CURSOR_CROSS:        return GLFW_CROSSHAIR_CURSOR;
    case TW_CURSOR_IBEAM:        return GLFW_IBEAM_CURSOR;
    case TW_CURSOR_NO:           return GLFW_NOT_ALLOWED_CURSOR;
    default:                     return GLFW_ARROW_CURSOR; // TW_CURSOR_HELP/UPARROW: no dedicated GLFW shape
    }
}

static void TW_CALL GLFWCursorCB(ETwCursor _Cursor, const unsigned char *_RGBA32x32, int _HotX, int _HotY, void *_ClientData)
{
    GLFWwindow *window = (GLFWwindow *)_ClientData;
    // TW_CURSOR_HIDDEN is an input mode, not a cursor shape: the roto slider
    // hides the pointer while it is dragged. g_CursorHidden remembers that so
    // the mode is restored once, on the next request for a visible cursor.
    if (_Cursor == TW_CURSOR_HIDDEN) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        g_CursorHidden = 1;
        return;
    }
    if (g_CursorHidden) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        g_CursorHidden = 0;
    }
    if (_Cursor == TW_CURSOR_CUSTOM && _RGBA32x32 != NULL) {
        GLFWimage img;
        img.width = 32; img.height = 32;
        img.pixels = (unsigned char *)_RGBA32x32; // glfwCreateCursor only reads it
        GLFWcursor *cur = glfwCreateCursor(&img, _HotX, _HotY);
        if (cur != NULL) {
            // Set the new cursor before destroying the old one: destroying
            // a cursor still current for a window resets that window to
            // the default arrow, which would undo this if done first.
            glfwSetCursor(window, cur);
            if (g_LastCustomCursor != NULL)
                glfwDestroyCursor(g_LastCustomCursor);
            g_LastCustomCursor = cur;
        }
        return;
    }
    if (g_StandardCursors[_Cursor] == NULL)
        g_StandardCursors[_Cursor] = glfwCreateStandardCursor(GLFWStandardCursorShape(_Cursor));
    if (g_StandardCursors[_Cursor] != NULL)
        glfwSetCursor(window, g_StandardCursors[_Cursor]);
}

static void DestroyGLFWCursorCache(void)
{
    for (int i = 0; i < TW_CURSOR_CUSTOM; ++i) {
        if (g_StandardCursors[i] != NULL) {
            glfwDestroyCursor(g_StandardCursors[i]);
            g_StandardCursors[i] = NULL;
        }
    }
    if (g_LastCustomCursor != NULL) {
        glfwDestroyCursor(g_LastCustomCursor);
        g_LastCustomCursor = NULL;
    }
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)window; (void)scancode;
    if (action == GLFW_PRESS || action == GLFW_REPEAT)
    {
        int twMod = 0;
        bool ctrl;
        if (mods & GLFW_MOD_SHIFT) twMod |= TW_KMOD_SHIFT;
        if ((ctrl = (mods & GLFW_MOD_CONTROL))) twMod |= TW_KMOD_CTRL;
        if (mods & GLFW_MOD_ALT) twMod |= TW_KMOD_ALT;

        int twKey = 0;
        switch (key)
        {
        case GLFW_KEY_BACKSPACE: twKey = TW_KEY_BACKSPACE; break;
        case GLFW_KEY_TAB: twKey = TW_KEY_TAB; break;
        case GLFW_KEY_ENTER: twKey = TW_KEY_RETURN; break;
        case GLFW_KEY_PAUSE: twKey = TW_KEY_PAUSE; break;
        case GLFW_KEY_ESCAPE: twKey = TW_KEY_ESCAPE; break;
        case GLFW_KEY_SPACE: twKey = TW_KEY_SPACE; break;
        case GLFW_KEY_DELETE: twKey = TW_KEY_DELETE; break;
        case GLFW_KEY_UP: twKey = TW_KEY_UP; break;
        case GLFW_KEY_DOWN: twKey = TW_KEY_DOWN; break;
        case GLFW_KEY_RIGHT: twKey = TW_KEY_RIGHT; break;
        case GLFW_KEY_LEFT: twKey = TW_KEY_LEFT; break;
        case GLFW_KEY_INSERT: twKey = TW_KEY_INSERT; break;
        case GLFW_KEY_HOME: twKey = TW_KEY_HOME; break;
        case GLFW_KEY_END: twKey = TW_KEY_END; break;
        case GLFW_KEY_PAGE_UP: twKey = TW_KEY_PAGE_UP; break;
        case GLFW_KEY_PAGE_DOWN: twKey = TW_KEY_PAGE_DOWN; break;
        case GLFW_KEY_F1: twKey = TW_KEY_F1; break;
        case GLFW_KEY_F2: twKey = TW_KEY_F2; break;
        case GLFW_KEY_F3: twKey = TW_KEY_F3; break;
        case GLFW_KEY_F4: twKey = TW_KEY_F4; break;
        case GLFW_KEY_F5: twKey = TW_KEY_F5; break;
        case GLFW_KEY_F6: twKey = TW_KEY_F6; break;
        case GLFW_KEY_F7: twKey = TW_KEY_F7; break;
        case GLFW_KEY_F8: twKey = TW_KEY_F8; break;
        case GLFW_KEY_F9: twKey = TW_KEY_F9; break;
        case GLFW_KEY_F10: twKey = TW_KEY_F10; break;
        case GLFW_KEY_F11: twKey = TW_KEY_F11; break;
        case GLFW_KEY_F12: twKey = TW_KEY_F12; break;
        case GLFW_KEY_F13: twKey = TW_KEY_F13; break;
        case GLFW_KEY_F14: twKey = TW_KEY_F14; break;
        case GLFW_KEY_F15: twKey = TW_KEY_F15; break;
        }
        if (twKey == 0 && ctrl && key < 128)
            twKey = key;
        if (twKey != 0)
            TwKeyPressed(twKey, twMod);
    }
}

static void charCallback(GLFWwindow* window, unsigned int key)
{
    (void)window;
    TwKeyPressed(key, 0);
}

static void mousebuttonCallback(GLFWwindow* _window, int _button, int _action, int _mods)
{
    (void)_window; (void)_mods;
    TwEventMouseButtonGLFW(_button, _action);
}

// GLFW always reports cursor position in window points, but TwWindowSize()
// is now fed framebuffer pixels (see windowSizeCallback below), so mouse
// events must be scaled by this window/framebuffer ratio before reaching
// AntTweakBar, or its hit-testing/drawing (now in pixel space) would
// misread a point-space cursor position - see docs/plans/examples-hidpi-scaling.md.
static double g_MouseScaleX = 1.0, g_MouseScaleY = 1.0;

static void mousePosCallback(GLFWwindow* _window, double _xpos, double _ypos)
{
    (void)_window;
    TwEventMousePosGLFW((int)(_xpos * g_MouseScaleX), (int)(_ypos * g_MouseScaleY));
}

static void mouseScrollCallback(GLFWwindow* _window, double _xoffset, double _yoffset)
{
    (void)_window; (void)_xoffset;
    static double pos = 0;
    pos += _yoffset;
    TwEventMouseWheelGLFW((int)pos);
}

// Registered as the FRAMEBUFFER size callback (not the window size
// callback): GLFW reports this in actual pixels, matching
// glViewport/TwWindowSize.
static void windowSizeCallback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
    TwWindowSize(width, height);

    int winWidth = width, winHeight = height;
    glfwGetWindowSize(window, &winWidth, &winHeight);
    g_MouseScaleX = (winWidth > 0) ? (double)width / winWidth : 1.0;
    g_MouseScaleY = (winHeight > 0) ? (double)height / winHeight : 1.0;
}

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
    fflush(stderr);
}


// ---------------------------------------------------------------------------
// 1) Callback functions for C-Dynamic string variables
// ---------------------------------------------------------------------------

// Function called to copy the content of a C-Dynamic String (src) handled by
// the AntTweakBar library to a C-Dynamic string (*destPtr) handled by our application
void TW_CALL CopyCDStringToClient(char **destPtr, const char *src)
{
    size_t srcLen = (src!=NULL) ? strlen(src) : 0;
    size_t destLen = (*destPtr!=NULL) ? strlen(*destPtr) : 0;

    // Alloc or realloc dest memory block if needed
    if( *destPtr==NULL )
        *destPtr = (char *)malloc(srcLen+1);
    else if( srcLen>destLen )
        *destPtr = (char *)realloc(*destPtr, srcLen+1);

    // Copy src
    if( srcLen>0 )
        strncpy(*destPtr, src, srcLen);
    (*destPtr)[srcLen] = '\0'; // null-terminated string
}

// Callback function called by AntTweakBar to set the "TextLine" CDString variable
void TW_CALL SetTextLineCB(const void *value, void *clientData)
{
    const char *src = *(const char **)value;
    char **destPtr = (char **)clientData;

    // Copies src to *destPtr (destPtr might be reallocated)
    CopyCDStringToClient(destPtr, src);

    // Change the label of the "Echo" inactive button
    size_t srcLen = strlen(src);
    if( srcLen>0 )
    {
        char *def = (char *)malloc(128+srcLen);
        snprintf(def, 128+srcLen, " Main/Echo label=`%s` ", src);
        TwDefine(def);
        free(def);
    }
    else
        TwDefine(" Main/Echo label=` ` ");
}

// Callback function called by AntTweakBar to get the "TextLine" CDString variable
void TW_CALL GetTextLineCB(void *value, void *clientData)
{
    char **destPtr = (char **)value;
    char *src = *(char **)clientData;

    // Do not assign destPtr directly:
    // Use TwCopyCDStringToLibrary to copy TextLine to AntTweakBar
    TwCopyCDStringToLibrary(destPtr, src);
}


// ---------------------------------------------------------------------------
// 2) Callback functions for C-Static sized string variables
// ---------------------------------------------------------------------------

// A static sized string
char g_CapStr[17] = "16 chars max"; // 17 = 16 + the null termination char

// A utility function: Convert a C string to lower or upper case
void CaseCopy(char *dest, const char *src, size_t maxLength, int capCase)
{
    size_t i;
    if( capCase==0 ) // lower case
        for( i=0; i<maxLength-1 && src[i]!='\0'; ++i )
            dest[i] = (char)tolower((unsigned char)src[i]);
    else // upper case
        for( i=0; i<maxLength-1 && src[i]!='\0'; ++i )
            dest[i] = (char)toupper((unsigned char)src[i]);
    dest[i] = '\0'; // ensure that dest is null-terminated
}

// Callback function called by AntTweakBar to set the "CapStr" CSString variable
void TW_CALL SetCapStrCB(const void *value, void *clientData)
{
    const char *src = (const char *)value;
    int capCase = *(int *)clientData;
    CaseCopy(g_CapStr, src, sizeof(g_CapStr), capCase);
}

// Callback function called by AntTweakBar to get the "CapStr" CSString variable
void TW_CALL GetCapStrCB(void *value, void *clientData)
{
    char *dest = (char *)value;
    int capCase = *(int *)clientData;
    CaseCopy(dest, g_CapStr, sizeof(g_CapStr), capCase);
}


// ---------------------------------------------------------------------------
// Main function (application based on GLFW3)
// ---------------------------------------------------------------------------

int main(void)
{
    GLFWwindow *window;

    glfwSetErrorCallback(error_callback);

    if (!glfwInit())
    {
        fprintf(stderr, "GLFW initialization failed\n");
        return 1;
    }

    window = glfwCreateWindow(640, 480, "AntTweakBar + GLFW3 (String Types)", NULL, NULL);
    if (!window)
    {
        fprintf(stderr, "Cannot open GLFW window\n");
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        fprintf(stderr, "Failed to initialize GLAD\n");
        return -2;
    }

    // AntTweakBar draws every widget at a fixed pixel size with no DPI
    // awareness, so on a HiDPI/Retina display it looks too large/blurry
    // relative to a standard display (see docs/plans/examples-hidpi-scaling.md).
    // Scaling "fontscaling" (set via TwDefine, before TwInit) by the
    // window's content scale keeps it a comparable physical size; on a
    // standard display the content scale is 1.0, so this is a no-op there.
    float contentScaleX = 1.0f, contentScaleY = 1.0f;
    glfwGetWindowContentScale(window, &contentScaleX, &contentScaleY);
    {
        char fontScalingDef[64];
        snprintf(fontScalingDef, sizeof(fontScalingDef), "GLOBAL fontscaling=%g", (double)contentScaleX);
        TwDefine(fontScalingDef);
    }

    if (!TwInit(TW_OPENGL, NULL)) {
        const char* err = TwGetLastError();
        fprintf(stderr, "TwInit failed: %s\n", err ? err : "Unknown error");
        fflush(stderr);
        return -3;
    }
    // Give GLFW3 authoritative cursor ownership (see GLFWCursorCB above).
    TwSetCursorCallback(GLFWCursorCB, window);
    {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        windowSizeCallback(window, width, height);
    }

    // Create a tweak bar
    TwBar *bar = TwNewBar("Main");
    TwDefine(" Main label='~ String variable examples ~' fontSize=3 position='180 16' valuesWidth=100 ");
    {
        // Scaled by content scale so the panel keeps up with the
        // now-larger scaled contents.
        int barSize[2] = { (int)(270 * contentScaleX + 0.5f), (int)(320 * contentScaleY + 0.5f) };
        TwSetParam(bar, NULL, "size", TW_PARAM_INT32, 2, barSize);
    }


    //
    // 1) C-Dynamic string variable example
    //

    TwAddButton(bar, "Info2.1", NULL, NULL, "label='1) This example uses' ");
    TwAddButton(bar, "Info2.2", NULL, NULL, "label='C-Dynamic string variables' ");

    // Define the required callback function to copy a CDString (see TwCopyCDStringToClientFunc documentation)
    TwCopyCDStringToClientFunc(CopyCDStringToClient);

    // Add a CDString variable
    char *someText = NULL;
    TwAddVarRW(bar, "Input", TW_TYPE_CDSTRING, &someText,
               " label='Text input' group=CDString help=`The text to be copied to 'Text output'.` ");
    TwAddVarRO(bar, "Output", TW_TYPE_CDSTRING, &someText,
               " label='Text output' group=CDString help=`Carbon copy of the text entered in 'Text input'.` ");

    // Add a line of text (we will use the label of a inactive button)
    #define TEXTLINE "a line of text"
    TwAddButton(bar, "Echo", NULL, NULL,
                " label=`" TEXTLINE "` group=CDString help='Echo of the text entered in the next field' ");

    // Add a CDString variable accessed through callbacks
    char *textLine = (char *)malloc(sizeof(TEXTLINE)+1);
    strncpy(textLine, TEXTLINE, sizeof(TEXTLINE));
    TwAddVarCB(bar, "TextLine", TW_TYPE_CDSTRING, SetTextLineCB, GetTextLineCB, &textLine,
               " label='Change text above' group=CDString help='The text to be echoed.' ");

    // Set the group label & separator
    TwDefine(" Main/CDString label='Echo some text' help='This example demonstates different use of C-Dynamic string variables.' ");
    TwAddSeparator(bar, "Sep2", "");
    TwAddButton(bar, "Blank2", NULL, NULL, " label=' ' ");


    //
    // 2) C-Static string variable example
    //

    TwAddButton(bar, "Info3.1", NULL, NULL, "label='2) This example uses' ");
    TwAddButton(bar, "Info3.2", NULL, NULL, "label='C strings of fixed size' ");

    // Add a CSString
    char tenStr[] = "0123456789"; // 10 characters + null_termination_char -> size = 11
    TwAddVarRW(bar, "Ten", TW_TYPE_CSSTRING(sizeof(tenStr)), tenStr,
               " label='10 chars max' group=CSString help='A string with a length of 10 characters max.' ");

    // Add a CSString accessed through callbacks. The callbacks will convert the string characters to upper or lower case
    int capCase = 1; // O: lower-case, 1: upper-case
    TwAddVarCB(bar, "Capitalize", TW_TYPE_CSSTRING(sizeof(g_CapStr)), SetCapStrCB, GetCapStrCB, &capCase,
               " group=CSString help='A string of fixed size to be converted to upper or lower case.' ");

    // Add a bool variable
    TwAddVarRW(bar, "Case", TW_TYPE_BOOL32, &capCase,
               " false=lower true=UPPER group=CSString key=Space help=`Changes the characters case of the 'Capitalize' string.` ");

    // Set the group label & separator
    TwDefine(" Main/CSString label='Character capitalization' help='This example demonstates different use of C-Static sized variables.' ");
    TwAddSeparator(bar, "Sep3", "");

    glfwSetKeyCallback(window, keyCallback);
    glfwSetCharCallback(window, charCallback);
    glfwSetMouseButtonCallback(window, mousebuttonCallback);
    glfwSetCursorPosCallback(window, mousePosCallback);
    glfwSetScrollCallback(window, mouseScrollCallback);
    glfwSetFramebufferSizeCallback(window, windowSizeCallback);

    while (!glfwWindowShouldClose(window))
    {
        glClearColor(0.5f, 0.5f, 0.6f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        TwDraw();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    TwTerminate();
    DestroyGLFWCursorCache();
    glfwTerminate();

    return 0;
}
