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
#include "atb_glfw.h"   // shared GLFW3 <-> AntTweakBar glue for these examples
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>



// Runs on every framebuffer resize, before TwWindowSize(). Size is in pixels.
static void resizeHook(GLFWwindow *window, int width, int height)
{
    (void)window;
    glViewport(0, 0, width, height);
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

    // Requested size is in "reference" (96 DPI) pixels; grow the actual
    // window to match the monitor's real pixel density on platforms where
    // window size and framebuffer size are otherwise always 1:1 (Windows,
    // X11) - a no-op on macOS, which already does this by definition (see
    // docs/plans/examples-hidpi-scaling.md).
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
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

    // AntTweakBar has no DPI awareness, so scale its font by the window content
    // scale to keep a comparable physical size. Must precede TwInit(), which
    // bakes the scale into the font atlases.
    atb_glfw_SetFontScaling(window);

    if (!TwInit(TW_OPENGL, NULL)) {
        const char* err = TwGetLastError();
        fprintf(stderr, "TwInit failed: %s\n", err ? err : "Unknown error");
        fflush(stderr);
        return -3;
    }
    // Registers the GLFW callbacks, gives GLFW3 authoritative cursor
    // ownership, routes the clipboard through it, and applies the current
    // framebuffer size - see atb_glfw.h.
    {
        atb_glfw_Hooks hooks = { 0 };
        hooks.resize = resizeHook;
        atb_glfw_Attach(window, &hooks);
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
        int barSize[2] = { (int)(370 * atb_glfw_ContentScaleX() + 0.5f), (int)(380 * atb_glfw_ContentScaleY() + 0.5f) };
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


    while (!glfwWindowShouldClose(window))
    {
        glClearColor(0.5f, 0.5f, 0.6f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        TwDraw();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    TwTerminate();
    atb_glfw_Detach(window);   // releases the cursors, after TwTerminate()
    glfwTerminate();

    return 0;
}
