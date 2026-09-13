//  ---------------------------------------------------------------------------
//
//  @file       String_sfml.cpp
//  @brief      This example illustrates the use of AntTweakBar's C-compatible
//              string variable types: a dynamically-allocated C string
//              (TW_TYPE_CDSTRING) and a fixed-size C string
//              (TW_TYPE_CSSTRING(n)).
//              SFML3 port of examples/glfw/String_glfw.c - see
//              docs/plans/sfml3-backend.md for the backend adapter notes.
//              See String_glfw.c's own header comment for why
//              TW_TYPE_STDSTRING's section of the original demo was
//              dropped rather than ported (a permanent, intentional
//              omission, not specific to this backend).
//
//              AntTweakBar: http://anttweakbar.sourceforge.net/doc
//              OpenGL:      http://www.opengl.org
//              SFML:        https://www.sfml-dev.org
//
//  @author     Philippe Decaudin
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>
#include <SFML/Window.hpp>
#include <AntTweakBar.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cctype>
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

// Matches the GLFW original: Escape is just fed through to TwKeyPressed()
// like any other key - no direct quit binding anywhere in this example,
// the window only closes via the OS close button/sf::Event::Closed.
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

static void handleResized(unsigned int _Width, unsigned int _Height)
{
    glViewport(0, 0, (GLsizei)_Width, (GLsizei)_Height);
    TwWindowSize((int)_Width, (int)_Height);
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
// Main function (application based on SFML3)
// ---------------------------------------------------------------------------

int main()
{
    sf::ContextSettings settings;
    settings.majorVersion = 2;
    settings.minorVersion = 1;

    sf::Window window(sf::VideoMode(sf::Vector2u(640, 480)),
                      "AntTweakBar + SFML3 (String Types)", sf::Style::Default, sf::State::Windowed, settings);

    if (!window.setActive(true)) {
        fprintf(stderr, "Failed to set the SFML window as active\n");
        return -1;
    }

    if (!gladLoadGLLoader((GLADloadproc)sf::Context::getFunction)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        return -2;
    }

    // SFML exposes no direct window-content-scale/DPI query (unlike GLFW's
    // glfwGetWindowContentScale/SDL3's SDL_GetWindowDisplayScale), but
    // window.getSize() now correctly reports real native pixel dimensions
    // on HiDPI/Retina displays (see vendor/sfml/src/SFML/Window/macOS/
    // SFWindowController.mm's own highDpi fix) - the ratio between that
    // and the logical 640-wide size requested above IS the content scale
    // factor, computed manually here.
    float contentScale = (float)window.getSize().x / 640.0f;
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
    TwSetCursorCallback(SFMLCursorCB, &window);
    TwSetClipboardCallback(ClipboardGetSFML, ClipboardSetSFML, NULL);
    handleResized(window.getSize().x, window.getSize().y);

    // Create a tweak bar
    TwBar *bar = TwNewBar("Main");
    // valuesWidth/size widened relative to the original demo so the "Multiline" text
    // variable below has room to wrap legibly (the narrower column also truncated the
    // other, short string values with "...").
    TwDefine(" Main label='~ String variable examples ~' fontSize=3 position='180 16' valuesWidth=200 ");
    {
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
                static double wheelPos = 0;
                wheelPos += wheel->delta;
                TwMouseWheel((int)wheelPos);
            } else if (const auto *resized = event->getIf<sf::Event::Resized>()) {
                handleResized(resized->size.x, resized->size.y);
            }
        }

        glClearColor(0.5f, 0.5f, 0.6f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        TwDraw();

        window.display();
    }

    TwTerminate();

    return 0;
}
