#include "record_graph.h"
#include "sds.h"
#include <stdarg.h>
#include <string.h>

typedef struct TestGraph {
    ITwGraph base;
    bool drawing;
} TestGraph;

typedef struct TestText {
    sds description;
} TestText;

static FILE *record_output;
static int live_text_objects;

void TestGraph_RecordTo(FILE *output) { record_output = output; }
int TestGraph_LiveTextObjects(void) { return live_text_objects; }

static void record(const char *format, ...)
{
    if (!record_output) return;
    va_list args;
    va_start(args, format);
    if (vfprintf(record_output, format, args) < 0) {
        perror("writing drawing trace");
        exit(EXIT_FAILURE);
    }
    va_end(args);
}

static int init(ITwGraph *graph) { (void)graph; return 1; }
static int shut(ITwGraph *graph) { (void)graph; return 1; }
static void begin(ITwGraph *graph, int width, int height)
{
    ((TestGraph *)graph)->drawing = true;
    record("begin %d %d\n", width, height);
}
static void end(ITwGraph *graph)
{
    ((TestGraph *)graph)->drawing = false;
    record("end\n");
}
static bool is_drawing(ITwGraph *graph) { return ((TestGraph *)graph)->drawing; }
static void restore(ITwGraph *graph) { (void)graph; record("restore\n"); }

static void line(ITwGraph *graph, int x0, int y0, int x1, int y1,
                 color32 c0, color32 c1, bool antialiased)
{
    (void)graph;
    record("line %d %d %d %d %08x %08x %d\n", x0, y0, x1, y1,
           (unsigned)c0, (unsigned)c1, antialiased);
}
static void rect(ITwGraph *graph, int x0, int y0, int x1, int y1,
                 color32 c00, color32 c10, color32 c01, color32 c11)
{
    (void)graph;
    record("rect %d %d %d %d %08x %08x %08x %08x\n", x0, y0, x1, y1,
           (unsigned)c00, (unsigned)c10, (unsigned)c01, (unsigned)c11);
}
static void triangles(ITwGraph *graph, int count, int *vertices, color32 *colors,
                      enum TwGraphCull cull)
{
    (void)graph;
    record("triangles %d %d\n", count, cull);
    for (int i = 0; i < count * 3; ++i)
        record("  %d %d %08x\n", vertices[2*i], vertices[2*i+1], (unsigned)colors[i]);
}

static void *new_text(ITwGraph *graph)
{
    (void)graph;
    TestText *text = calloc(1, sizeof(*text));
    if (!text || !(text->description = sdsempty())) exit(EXIT_FAILURE);
    ++live_text_objects;
    return text;
}
static void delete_text(ITwGraph *graph, void *object)
{
    (void)graph;
    if (!object) return;
    TestText *text = object;
    sdsfree(text->description);
    free(text);
    --live_text_objects;
}
static void build_text(ITwGraph *graph, void *object, const char * const *lines,
                       color32 *colors, color32 *backgrounds, int count,
                       const CTexFont *font, int separation, int background_width)
{
    (void)graph;
    TestText *text = object;
    sdsclear(text->description);
    text->description = sdscatprintf(text->description, "  font-height %d gap %d background-width %d lines %d\n",
                                    font->m_CharHeight, separation, background_width, count);
    for (int i = 0; i < count; ++i) {
        text->description = sdscatprintf(text->description, "  color %s%08x background %s%08x ",
                                        colors ? "" : "default:", colors ? (unsigned)colors[i] : 0,
                                        backgrounds ? "" : "default:", backgrounds ? (unsigned)backgrounds[i] : 0);
        text->description = sdscatrepr(text->description, lines[i], strlen(lines[i]));
        text->description = sdscat(text->description, "\n");
    }
    if (!text->description) exit(EXIT_FAILURE);
}
static void draw_text(ITwGraph *graph, void *object, int x, int y, color32 color, color32 background)
{
    (void)graph;
    TestText *text = object;
    record("text %d %d %08x %08x\n%s", x, y, (unsigned)color, (unsigned)background, text->description);
}
static void viewport(ITwGraph *graph, int x, int y, int width, int height, int offset_x, int offset_y)
{
    (void)graph;
    record("viewport %d %d %d %d %d %d\n", x, y, width, height, offset_x, offset_y);
}
static void restore_viewport(ITwGraph *graph) { (void)graph; record("restore-viewport\n"); }
static void scissor(ITwGraph *graph, int x, int y, int width, int height)
{
    (void)graph;
    record("scissor %d %d %d %d\n", x, y, width, height);
}

ITwGraph *TwGraphOpenGL_Create(void)
{
    TestGraph *graph = calloc(1, sizeof(*graph));
    if (!graph) return NULL;
    graph->base = (ITwGraph){
        .Init = init, .Shut = shut, .BeginDraw = begin, .EndDraw = end,
        .IsDrawing = is_drawing, .Restore = restore,
        .DrawLine = line, .DrawRect = rect, .DrawTriangles = triangles,
        .NewTextObj = new_text, .DeleteTextObj = delete_text,
        .BuildText = build_text, .DrawText = draw_text,
        .ChangeViewport = viewport, .RestoreViewport = restore_viewport, .SetScissor = scissor,
    };
    return &graph->base;
}

ITwGraph *TwGraphOpenGLCore_Create(void) { return TwGraphOpenGL_Create(); }
