//  ---------------------------------------------------------------------------
//
//  @file       String.c
//  @brief      This example illustrates the use of AntTweakBar's C-compatible
//              string variable types: a dynamically-allocated C string
//              (TW_TYPE_CDSTRING) and a fixed-size C string
//              (TW_TYPE_CSSTRING(n)).
//
//              SDL3 port of examples/glfw/String.c - see
//              docs/plans/sdl3-backend.md for the backend adapter notes.
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
//              The graphic window is created by SDL3 (the original used
//              GLUT); this example has no 3D content of its own, so the
//              SDL3/AntTweakBar integration boilerplate below is the same
//              minimal shape used by every other SDL3 example in this
//              folder (see e.g. SimpleGL21.c).
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              SDL:         https://www.libsdl.org
//
//  @author     Philippe Decaudin
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <AntTweakBar.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// SDL3 cursors are process-global (SDL_SetCursor() takes no window
// argument, unlike glfwSetCursor()) - there is no per-window cursor-
// ownership fight to route around the way examples/glfw/String.c's own
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

// Unlike most other ported examples, this one does NOT quit on Escape at
// all (there is no bound "Quit" variable, nor a direct close call) -
// Escape is simply fed through to TwKeyPressed() like any other named key,
// preserving examples/glfw/String.c's own keyCallback behavior exactly.
// The window only closes via SDL_EVENT_WINDOW_CLOSE_REQUESTED/QUIT.
static void handleKeyDown(const SDL_KeyboardEvent *_Event)
{
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
    case SDLK_ESCAPE: twKey = TW_KEY_ESCAPE; break;
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
    if (twKey == 0 && ctrl && _Event->key < 128)
        twKey = (int)_Event->key;
    if (twKey != 0)
        TwKeyPressed(twKey, twMod);
}

// SDL3 delivers typed text as a UTF-8 string per SDL_EVENT_TEXT_INPUT event
// (opt-in via SDL_StartTextInput()), unlike GLFW's one-codepoint-per-call
// char callback - decode it a codepoint at a time and feed each to
// TwKeyPressed(), same as examples/glfw/String.c's charCallback does per
// GLFW callback invocation. Only handles well-formed UTF-8 (1-3 byte
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
// is now fed pixel size (see handleWindowPixelSizeChanged below), so mouse
// events must be scaled by this window/pixel ratio before reaching
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
// examples/glfw/String.c's windowSizeCallback (registered as GLFW's
// FRAMEBUFFER size callback there for the same pixel-vs-point reason).
static void handleWindowPixelSizeChanged(SDL_Window *_Window, int _Width, int _Height)
{
    glViewport(0, 0, _Width, _Height);
    TwWindowSize(_Width, _Height);
    updateMouseScale(_Window, _Width, _Height);
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

    // Copy src (memcpy, not strncpy: the buffer is sized srcLen+1 and the
    // terminator is set explicitly right below, so strncpy's own null-
    // padding/truncation behavior is neither needed nor wanted here).
    if( srcLen>0 )
        memcpy(*destPtr, src, srcLen);
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

// Gives the "WideButton" demo below a real callback, so it draws an actual clickable
// rectangle: a callback-less button draws only its label, which full_width suppresses.
void TW_CALL WideButtonCB(void *clientData)
{
    (void)clientData;
    printf("WideButton clicked (full_width=true button).\n");
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
// Main function (application based on SDL3)
// ---------------------------------------------------------------------------

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    // Fixed-function GL + AntTweakBar's TW_OPENGL (compatibility, not Core
    // Profile) renderer - request a plain 2.1 compatibility context, the
    // same profile examples/glfw/SimpleGL21.c targets, and the exact
    // version validated by this backend's compile spike (see
    // docs/plans/sdl3-backend.md Step 1).
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    SDL_Window *window = SDL_CreateWindow("AntTweakBar + SDL3 (String Types)", 640, 480,
                                           SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window)
    {
        fprintf(stderr, "Cannot open SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (ctx == NULL)
    {
        fprintf(stderr, "Cannot create GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        fprintf(stderr, "Failed to initialize GLAD\n");
        return -2;
    }

    // AntTweakBar draws every widget at a fixed pixel size with no DPI
    // awareness - scale "fontscaling" by the window's display scale before
    // TwInit, same reasoning (and the same no-op-on-a-standard-display
    // behavior) as examples/glfw/String.c's own contentScaleX handling.
    float contentScale = SDL_GetWindowDisplayScale(window);
    if (contentScale <= 0.0f) contentScale = 1.0f;
    {
        char fontScalingDef[64];
        snprintf(fontScalingDef, sizeof(fontScalingDef), "GLOBAL fontscaling=%g", (double)contentScale);
        TwDefine(fontScalingDef);
    }

    if (!TwInit(TW_OPENGL, NULL)) {
        const char* err = TwGetLastError();
        fprintf(stderr, "TwInit failed: %s\n", err ? err : "Unknown error");
        fflush(stderr);
        return -3;
    }
    TwSetCursorCallback(SDLCursorCB, NULL); // SDL cursors are process-global, no window needed
    TwSetClipboardCallback(ClipboardGetSDL, ClipboardSetSDL, NULL);
    SDL_StartTextInput(window);
    {
        int pixelWidth, pixelHeight;
        SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
        handleWindowPixelSizeChanged(window, pixelWidth, pixelHeight);
    }

    // Create a tweak bar
    TwBar *bar = TwNewBar("Main");
    // valuesWidth/size widened relative to the original demo so the "Multiline" text
    // variable below has room to wrap legibly (the narrower column also truncated the
    // other, short string values with "...").
    TwDefine(" Main label='~ String variable examples ~' fontSize=3 position='180 16' valuesWidth=200 ");
    {
        // Scaled by content scale so the panel keeps up with the
        // now-larger scaled contents.
        int barSize[2] = { (int)(370 * contentScale + 0.5f), (int)(380 * contentScale + 0.5f) };
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

    // Add a CDString variable accessed through callbacks. TEXTLINE is a
    // fixed string literal (not attacker-controlled), so a plain strcpy
    // into this generously-sized (sizeof(TEXTLINE)+1) buffer is safe -
    // strncpy(dst, TEXTLINE, sizeof(TEXTLINE)) triggers
    // -Wsizeof-pointer-memaccess because that's also the exact shape of
    // the classic strncpy(dst, src, sizeof(src)) bug when src is a
    // pointer variable instead of a literal.
    char *textLine = (char *)malloc(sizeof(TEXTLINE)+1);
    strcpy(textLine, TEXTLINE);
    TwAddVarCB(bar, "TextLine", TW_TYPE_CDSTRING, SetTextLineCB, GetTextLineCB, &textLine,
               " label='Change text above' group=CDString help='The text to be echoed.' ");

    // Add a multiline-text variable exercising the "lines" param: a CDString whose value is
    // wrapped over a fixed number of visible lines, with its own scrollbar when it overflows.
    char *multilineText = NULL;
    CopyCDStringToClient(&multilineText,
        "This description is long enough that it needs to wrap across "
        "several lines and will not fit in the four lines configured "
        "below, so the widget's own scrollbar should appear on the right "
        "to reach the rest of the text.");

    TwAddSeparator(bar, NULL, "group=CDString");
    TwAddVarRW(bar, "Multiline", TW_TYPE_CDSTRING, &multilineText,
               " label='Multiline text' group=CDString lines=4 help='Demonstrates the new lines= param (a multiline text widget with its own scrollbar).' ");

    TwAddSeparator(bar, NULL, "group=CDString");

    // The same "lines=N" widget with "full_width=true" added - compare against "Multiline
    // text" above: no label, and the text box spans the label column as well.
    char *wideMultilineText = NULL;
    CopyCDStringToClient(&wideMultilineText,
        "This text field occupies the entire width of the bar. This text is long enough "
        "that it needs to wrap across several lines and will not fit in the four lines "
        "configured here, so a scrollbar should appear on the right to reach the rest of "
        "the text.");
    TwAddVarRW(bar, "WideMultiline", TW_TYPE_CDSTRING, &wideMultilineText,
               " label='Wide multiline text' group=CDString lines=4 full_width=true help='Demonstrates full_width=true: no label, the widget spans the full row width.' ");

    TwAddSeparator(bar, NULL, "group=CDString");

    // "full_width" is generic, not multiline-specific: on a plain button it widens the
    // clickable rect itself across the label column.
    TwAddButton(bar, "WideButton", WideButtonCB, NULL,
                " label='Wide button' group=CDString full_width=true help='Demonstrates full_width=true on a TW_TYPE_BUTTON: no label, the clickable rect spans the full row width.' ");

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

    TwAddSeparator(bar, NULL, "");
    TwAddButton(bar, "FullWidthDemoMoreLines", FullWidthLinesCB, bar,
                " label='More lines' full_width=true "
                "help='Cycles the text field below through 2, 3, 4, 5, 6 visible lines, then back to 2.' ");
    TwAddVarRW(bar, "FullWidthDemoText", TW_TYPE_CSSTRING(sizeof(g_FullWidthDemoText)), g_FullWidthDemoText,
               " label='Full-width text' full_width=true lines=2 "
               "help='A full-width, wrapped multiline text field.' ");

    bool running = true;
    static double wheelPos = 0;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                handleKeyDown(&event.key);
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

        glClearColor(0.5f, 0.5f, 0.6f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        TwDraw();

        SDL_GL_SwapWindow(window);
    }

    TwTerminate();
    DestroySDLCursorCache();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
