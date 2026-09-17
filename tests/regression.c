#include "../src/TwPrecomp.h"
#include "../src/TwMgr.h"
#include "../src/TwBar.h"
#include "record_graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition); \
    exit(EXIT_FAILURE); \
} } while (0)

static int checks;
static bool expecting_error;
static int reported_errors;

static void TW_CALL error_callback(const char *message)
{
    if (!expecting_error) {
        fprintf(stderr, "unexpected library error: %s\n", message);
        exit(EXIT_FAILURE);
    }
    ++reported_errors;
}

static void reject_definition(const char *definition)
{
    int previous_errors = reported_errors;
    expecting_error = true;
    CHECK(TwDefine(definition) == 0);
    expecting_error = false;
    CHECK(reported_errors > previous_errors);
    CHECK(TwGetLastError() != NULL);
    ++checks;
}

static void expect_param(TwBar *bar, const char *variable, const char *parameter, int expected)
{
    int actual = -999;
    CHECK(TwGetParam(bar, variable, parameter, TW_PARAM_INT32, 1, &actual) == 1);
    if (actual != expected) {
        fprintf(stderr, "%s/%s: expected %d, got %d\n", variable ? variable : "bar", parameter, expected, actual);
        exit(EXIT_FAILURE);
    }
    ++checks;
}

static void set_int(TwBar *bar, const char *variable, const char *parameter, int value)
{
    CHECK(TwSetParam(bar, variable, parameter, TW_PARAM_INT32, 1, &value));
    expect_param(bar, variable, parameter, value);
}

static void TW_CALL button_callback(void *data) { ++*(int *)data; }

static void test_parameters(void)
{
    CHECK(TwInit(TW_OPENGL_CORE, NULL));
    CHECK(TwWindowSize(800, 600));
    TwBar *bar = TwNewBar("Params");
    CHECK(bar);
    int number = 7, clicks = 0, selected = 1;
    char text[128] = "alpha beta gamma";
    const TwEnumVal options[] = {{0, "Zero"}, {1, "One"}, {2, "Two"}};
    TwType enumeration = TwDefineEnum("Options", options, 3);
    CHECK(enumeration != TW_TYPE_UNDEF);
    CHECK(TwAddVarRW(bar, "number", TW_TYPE_INT32, &number, "min=0 max=10 step=1"));
    CHECK(TwAddVarRW(bar, "text", TW_TYPE_CSSTRING(sizeof(text)), text, "group=Nested lines=3"));
    CHECK(TwAddVarRW(bar, "choice", enumeration, &selected, NULL));
    CHECK(TwAddButton(bar, "action", button_callback, &clicks, NULL));
    CHECK(TwAddSeparator(bar, "separator", NULL));
    expect_param(bar, "number", "full_width", 0);
    expect_param(bar, "number", "align_right", 0);
    expect_param(bar, "number", "align_left", 1);
    expect_param(bar, "text", "lines", 3);
    expect_param(bar, "Nested", "opened", 1);
    expect_param(bar, NULL, "visible", 1);
    CHECK(TwDefine("Params/number full_width=true align_right=true"));
    expect_param(bar, "number", "full_width", 1);
    expect_param(bar, "number", "align_left", 0);
    set_int(bar, "number", "align_left", 1);
    expect_param(bar, "number", "align_right", 0);
    set_int(bar, "number", "full_width", 0);
    set_int(bar, "text", "lines", 4);
    set_int(bar, "Nested", "opened", 0);
    set_int(bar, "Nested", "opened", 1);
    set_int(bar, "choice", "full_width", 1);
    set_int(bar, "action", "full_width", 1);
    set_int(bar, "number", "readonly", 1);
    CHECK(TwDefine("Params/number readwrite"));
    expect_param(bar, "number", "readonly", 0);
    reject_definition("Params/text lines=1");
    expect_param(bar, "text", "lines", 4);
    reject_definition("Params/number lines=3");
    reject_definition("Params/number full_width=invalid");
    expect_param(bar, "number", "full_width", 0);
    CHECK(TwDraw());
    CHECK(TwRemoveVar(bar, "text"));
    CHECK(TwRemoveAllVars(bar));
    CHECK(TwDeleteBar(bar));
    CHECK(TwTerminate());
    CHECK(TestGraph_LiveTextObjects() == 0);
}

static int row_index(CTwBar *bar, const char *name, int subrow)
{
    for (size_t i = 0; i < bar->m_HierTags.count; ++i)
        if (!strcmp(bar->m_HierTags.items[i].m_Var->m_Name, name) && bar->m_HierTags.items[i].m_SubLine == subrow)
            return (int)i;
    fprintf(stderr, "visible row missing: %s, subrow %d\n", name, subrow);
    exit(EXIT_FAILURE);
}

static int row_y(CTwBar *bar, const char *name, int subrow)
{
    return bar->m_PosY + bar->m_VarY0 + row_index(bar, name, subrow) * (bar->m_Font->m_CharHeight + bar->m_LineSep)
           + bar->m_Font->m_CharHeight / 2;
}

static void click(int x, int y)
{
    CHECK(TwMouseMotion(x, y));
    CHECK(TwMouseButton(TW_MOUSE_PRESSED, TW_MOUSE_LEFT));
    CHECK(TwMouseButton(TW_MOUSE_RELEASED, TW_MOUSE_LEFT));
}

static void capture(FILE *output, const char *name)
{
    CHECK(fprintf(output, "\nSCENE %s\n", name) > 0);
    /* Settle layout before recording: rebuild scheduling is not rendered appearance. */
    CHECK(TwDraw());
    TestGraph_RecordTo(output);
    CHECK(TwDraw());
    TestGraph_RecordTo(NULL);
}

static void test_scene(FILE *output, int font_size, int scale)
{
    char definition[256];
    snprintf(definition, sizeof(definition), "GLOBAL fontscaling=%d", scale);
    CHECK(TwDefine(definition));
    CHECK(TwInit(TW_OPENGL_CORE, NULL));
    CHECK(TwWindowSize(1200, 1000));
    CHECK(TwDefine("TW_HELP visible=false"));
    CTwBar *bar = TwNewBar("Baseline");
    CHECK(bar);
    snprintf(definition, sizeof(definition),
             "Baseline fontsize=%d position='40 40' size='700 880' valueswidth=310 color='70 100 130'", font_size);
    CHECK(TwDefine(definition));
    int number = 3, enabled = 0, selected = 0, clicks = 0;
    float accent_color[3] = {0.25f, 0.50f, 0.75f};
    float orientation[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    char text[256] = "First line wraps with enough words to cross the column boundary.\nSecond line.\nThird line.\nFourth line.\nFifth line.";
    const TwEnumVal options[] = {{0, "Short"}, {1, "WWWW a reasonably wide popup option"}, {2, "Last"}};
    TwType enumeration = TwDefineEnum("SceneOptions", options, 3);
    CHECK(TwAddVarRW(bar, "number", TW_TYPE_INT32, &number, "min=0 max=10 step=1 label='Aligned number with a deliberately long label that exceeds the available label column' align_right=true"));
    CHECK(TwAddVarRW(bar, "enabled", TW_TYPE_BOOL32, &enabled, "label=Enabled"));
    CHECK(TwAddVarRW(bar, "choice", enumeration, &selected, "label=Choice"));
    CHECK(TwAddButton(bar, "action", button_callback, &clicks, "label='Run action' full_width=true"));
    CHECK(TwAddSeparator(bar, "separator", NULL));
    CHECK(TwAddVarRW(bar, "text", TW_TYPE_CSSTRING(sizeof(text)), text, "group=Inner lines=3"));
    CHECK(TwDefine("Baseline/Inner group=Outer"));
    CHECK(TwAddVarRO(bar, "wide", TW_TYPE_CSSTRING(sizeof(text)), text, "full_width=true lines=2"));
    CHECK(TwAddVarRW(bar, "accent", TW_TYPE_COLOR3F, accent_color, "label='Accent color' group=Appearance"));
    CHECK(TwAddVarRW(bar, "orientation", TW_TYPE_QUAT4F, orientation, "label='Orientation' group=Appearance"));
    CHECK(TwDraw());
    CHECK(row_index(bar, "accent", 0) >= 0);
    CHECK(row_index(bar, "orientation", 0) >= 0);
    CHECK(fprintf(output, "\nCASE font=%d scale=%d\n", font_size, scale) > 0);
    capture(output, "unfocused");

    int value_x = bar->m_PosX + bar->m_VarX1 + 12;
    int bool_y = row_y(bar, "enabled", 0);
    int bool_top = bool_y - bar->m_Font->m_CharHeight / 2;
    CHECK(TwMouseMotion(value_x, bool_top - 1));
    CHECK(bar->m_HighlightedLine == row_index(bar, "number", 0));
    CHECK(TwMouseMotion(value_x, bool_top));
    CHECK(bar->m_HighlightedLine == row_index(bar, "enabled", 0));
    CHECK(TwMouseMotion(value_x, bool_top + bar->m_Font->m_CharHeight + bar->m_LineSep));
    CHECK(bar->m_HighlightedLine == row_index(bar, "choice", 0));
    CHECK(TwMouseMotion(value_x, bool_y));
    CHECK(bar->m_HighlightedLine == row_index(bar, "enabled", 0));
    capture(output, "focused");
    click(value_x, bool_y);
    CHECK(enabled == 1);
    CHECK(TwDraw());
    click(bar->m_PosX + bar->m_VarX0 + 16, row_y(bar, "action", 0));
    CHECK(clicks == 1);
    CHECK(TwDraw());

    int text_row = row_index(bar, "text", 0);
    CHECK(row_index(bar, "text", 2) == text_row + 2);
    CHECK(bar->m_HierTags.items[text_row].m_Level == 2);
    CTwVarAtom *atom = (CTwVarAtom *)bar->m_HierTags.items[text_row].m_Var;
    CHECK(atom->m_Val.m_Multiline.m_NbTextLines > 3);
    CHECK(TwMouseMotion(value_x, row_y(bar, "text", 1)));
    CHECK(TwMouseWheel(-1));
    CHECK(atom->m_Val.m_Multiline.m_FirstTextLine > 0);
    capture(output, "multiline-scrolled");

    click(value_x, row_y(bar, "choice", 0));
    CHECK(g_TwMgr->m_PopupBar != NULL);
    capture(output, "enum-popup");
    CTwBar *popup = g_TwMgr->m_PopupBar;
    CHECK(!popup->m_Resizable);
    click(popup->m_PosX + popup->m_VarX0 + 12, row_y(popup, "1", 0));
    CHECK(selected == 1);
    CHECK(g_TwMgr->m_PopupBar == NULL);

    CHECK(TwDraw());
    int roto_x = bar->m_PosX + bar->m_VarX2 - 1;
    CHECK(TwMouseMotion(value_x, row_y(bar, "number", 0)));
    CHECK(TwDraw());
    CHECK(TwMouseMotion(roto_x, row_y(bar, "number", 0)));
    CHECK(bar->m_HighlightRotoBtn);
    CHECK(TwMouseButton(TW_MOUSE_PRESSED, TW_MOUSE_LEFT));
    CHECK(bar->m_Roto.m_Active);
    /* Wall-clock autorepeat is outside these deterministic input sequences. */
    g_TwMgr->m_CanRepeatMousePressed = false;
    CHECK(TwMouseMotion(bar->m_Roto.m_Origin.x + 50, bar->m_Roto.m_Origin.y));
    capture(output, "roto-active");
    CHECK(TwMouseButton(TW_MOUSE_RELEASED, TW_MOUSE_LEFT));
    CHECK(!bar->m_Roto.m_Active);
    CHECK(number >= 0 && number <= 10);

    CHECK(TwDraw());
    int title_x = bar->m_PosX + bar->m_Width / 2;
    int title_y = bar->m_PosY + bar->m_Font->m_CharHeight / 2;
    CHECK(TwMouseMotion(title_x, title_y));
    CHECK(TwMouseButton(TW_MOUSE_PRESSED, TW_MOUSE_LEFT));
    CHECK(TwMouseMotion(title_x - 60, title_y));
    CHECK(TwMouseButton(TW_MOUSE_RELEASED, TW_MOUSE_LEFT));
    CHECK(bar->m_PosX == -20);
    CHECK(TwMouseMotion(bar->m_PosX + bar->m_Width, title_y) == 0);
    CHECK(TwMouseMotion(bar->m_PosX + bar->m_Width - 1, title_y));
    CHECK(TwMouseMotion(100, bar->m_PosY + bar->m_Height) == 0);
    CHECK(TwDefine("Baseline text=dark"));
    CHECK(TwMouseMotion(1000, 950) == 0);
    capture(output, "negative-x-dark-text");
    CHECK(TwDefine("Baseline size='700 160'"));
    capture(output, "clipped");

    CHECK(TwDefine("Baseline visible=false"));
    CHECK(TwDefine("TW_HELP visible=true iconified=false position='32 32' size='600 600'"));
    /* Bypass only the help refresh throttle, so elapsed wall time cannot select content. */
    g_TwMgr->m_HelpBarUpdateNow = true;
    g_TwMgr->m_HelpBarNotUpToDate = true;
    CHECK(TwDraw());
    CHECK(TwDefine("TW_HELP/RotoSlider opened=true"));
    capture(output, "help");

    CHECK(TwDefine("TW_HELP visible=false"));
    CHECK(TwDefine("Baseline visible=true position='40 40' size='700 880'"));
    CHECK(TwDraw());
    click(value_x, row_y(bar, "text", 1));
    CHECK(bar->m_EditInPlace.m_Active);
    CHECK(TwKeyPressed(TW_KEY_HOME, 0));
    CHECK(bar->m_EditInPlace.m_CaretPos == 0);
    CHECK(TwKeyPressed(TW_KEY_DOWN, 0));
    CHECK(bar->m_EditInPlace.m_CaretPos > 0);
    CHECK(TwKeyPressed(TW_KEY_UP, 0));
    CHECK(bar->m_EditInPlace.m_CaretPos == 0);
    CHECK(TwKeyPressed('X', 0));
    CHECK(bar->m_EditInPlace.m_String[0] == 'X');
    CHECK(TwKeyPressed(TW_KEY_ESCAPE, 0));
    CHECK(!bar->m_EditInPlace.m_Active);
    CHECK(text[0] == 'F');
    CHECK(TwTerminate());
    CHECK(TestGraph_LiveTextObjects() == 0);
    ++checks;
}

static int compare_files(const char *expected_path, const char *actual_path)
{
    /* Text mode tolerates native CRLF checkouts on Windows. */
    FILE *expected = fopen(expected_path, "r");
    FILE *actual = fopen(actual_path, "r");
    if (!expected || !actual) {
        fprintf(stderr, "cannot open drawing baseline or actual trace\n");
        if (expected) fclose(expected);
        if (actual) fclose(actual);
        return 0;
    }
    size_t line = 1;
    int a, b;
    do {
        a = fgetc(expected);
        b = fgetc(actual);
        if (a != b) {
            fprintf(stderr, "drawing baseline differs at line %zu; compare %s and %s\n", line, expected_path, actual_path);
            fclose(expected);
            fclose(actual);
            return 0;
        }
        if (a == '\n') ++line;
    } while (a != EOF);
    int ok = !ferror(expected) && !ferror(actual);
    fclose(expected);
    fclose(actual);
    return ok;
}

int main(int argc, char **argv)
{
    bool record = argc == 2 && !strcmp(argv[1], "--record");
    CHECK(argc == 1 || record);
    const char *actual_path = "build/tests/actual-layout.txt";
    const char *baseline_path = "tests/widget-layout.txt";
    FILE *output = fopen(actual_path, "wb");
    CHECK(output);
    TwHandleErrors(error_callback);
    test_parameters();
    for (int scale = 1; scale <= 2; ++scale)
        for (int font = 1; font <= 3; ++font)
            test_scene(output, font, scale);
    CHECK(fclose(output) == 0);
    if (record) {
        FILE *actual = fopen(actual_path, "rb");
        FILE *baseline = fopen(baseline_path, "wb");
        CHECK(actual && baseline);
        int byte;
        while ((byte = fgetc(actual)) != EOF) CHECK(fputc(byte, baseline) != EOF);
        CHECK(!ferror(actual));
        CHECK(fclose(actual) == 0);
        CHECK(fclose(baseline) == 0);
        printf("Recorded %s; review the fixture diff before accepting it.\n", baseline_path);
    } else {
        CHECK(compare_files(baseline_path, actual_path));
    }
    printf("PASS: %d parameter/scene checks and drawing baseline\n", checks);
    return EXIT_SUCCESS;
}
