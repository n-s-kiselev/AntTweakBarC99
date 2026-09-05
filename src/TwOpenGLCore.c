//  ---------------------------------------------------------------------------
//
//  @file       TwOpenGLCore.c
//  @author     Philippe Decaudin
//  @license    This file is part of the AntTweakBar library.
//              For conditions of distribution and use, see License.txt
//
//  ---------------------------------------------------------------------------

#include <glad/glad.h>

#include "TwOpenGLCore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(_WIN32)
#   include <windows.h>
#endif
#if defined(__APPLE__)
#   include <AvailabilityMacros.h>
#endif

// TwSetLastError: internal C-linkage bridge into the still-C++ CTwMgr (see
// its declaration in TwMgr.h). This file is plain C99 and cannot include
// TwMgr.h itself (it declares C++ classes), so it is forward-declared here
// instead, matching the exact extern "C" signature TwMgr.h already gives
// it. The #ifdef __cplusplus guard matters even though this file is plain
// C99: nob.c currently compiles every source, .c included, with the C++
// driver on macOS (until the rewrite reaches its final step), and a bare
// `extern` declaration compiled as C++ gets C++ name mangling applied,
// which would then mismatch TwMgr.h's real extern "C" symbol at link time
// (see src/TwFonts.c for the same precedent).
#ifdef __cplusplus
extern "C" {
#endif
int TwSetLastError(const char *_StaticErrorMessage);
#ifdef __cplusplus
}
#endif

//  ---------------------------------------------------------------------------
//  Small growable arrays, replacing std::vector<Vec2>/std::vector<color32>
//  (see CTextObj below). Minimal, purpose-built - no generic container
//  dependency added (AGENTS.md SS2/SS4).
//  ---------------------------------------------------------------------------

typedef struct { GLfloat x, y; } Vec2;

static Vec2 Vec2Make(GLfloat _X, GLfloat _Y)
{
    Vec2 v;
    v.x = _X;
    v.y = _Y;
    return v;
}

typedef struct { Vec2 *items; size_t count, capacity; } Vec2Array;
typedef struct { color32 *items; size_t count, capacity; } Color32Array;

static void Vec2Array_Push(Vec2Array *_Arr, Vec2 _V)
{
    if( _Arr->count>=_Arr->capacity )
    {
        size_t newCap = _Arr->capacity ? _Arr->capacity*2 : 64;
        Vec2 *newItems = (Vec2 *)realloc(_Arr->items, newCap*sizeof(Vec2));
        if( !newItems )
            return; // out of memory: drop the point rather than corrupt the buffer
        _Arr->items = newItems;
        _Arr->capacity = newCap;
    }
    _Arr->items[_Arr->count++] = _V;
}

static void Color32Array_Push(Color32Array *_Arr, color32 _C)
{
    if( _Arr->count>=_Arr->capacity )
    {
        size_t newCap = _Arr->capacity ? _Arr->capacity*2 : 64;
        color32 *newItems = (color32 *)realloc(_Arr->items, newCap*sizeof(color32));
        if( !newItems )
            return;
        _Arr->items = newItems;
        _Arr->capacity = newCap;
    }
    _Arr->items[_Arr->count++] = _C;
}

// CTextObj: was a nested `struct CTextObj { std::vector<Vec2> ...; }`.
typedef struct CTextObj
{
    Vec2Array       m_TextVerts;
    Vec2Array       m_TextUVs;
    Vec2Array       m_BgVerts;
    Color32Array    m_Colors;
    Color32Array    m_BgColors;
} CTextObj;

//  ---------------------------------------------------------------------------
//  TwGraphOpenGLCore: the combined struct (ITwGraph base + private fields),
//  see TwGraph.h's own comment for why `base` must be the first member.
//  ---------------------------------------------------------------------------

typedef struct TwGraphOpenGLCore
{
    ITwGraph            base;

    bool                m_Drawing;
    GLuint              m_FontTexID;
    const CTexFont *    m_FontTex;

    GLfloat             m_PrevLineWidth;
    GLint               m_PrevActiveTexture;
    GLint               m_PrevTexture;
    GLuint              m_PrevVArray;
    GLboolean           m_PrevLineSmooth;
    GLboolean           m_PrevCullFace;
    GLboolean           m_PrevDepthTest;
    GLboolean           m_PrevBlend;
    GLint               m_PrevSrcBlend;
    GLint               m_PrevDstBlend;
    GLboolean           m_PrevScissorTest;
    GLint               m_PrevScissorBox[4];
    GLint               m_PrevViewport[4];
    GLuint              m_PrevProgramObject;

    GLuint              m_LineRectVS;
    GLuint              m_LineRectFS;
    GLuint              m_LineRectProgram;
    GLuint              m_LineRectVArray;
    GLuint              m_LineRectVertices;
    GLuint              m_LineRectColors;
    GLuint              m_TriVS;
    GLuint              m_TriFS;
    GLuint              m_TriProgram;
    GLuint              m_TriUniVS;
    GLuint              m_TriUniFS;
    GLuint              m_TriUniProgram;
    GLuint              m_TriTexVS;
    GLuint              m_TriTexFS;
    GLuint              m_TriTexProgram;
    GLuint              m_TriTexUniVS;
    GLuint              m_TriTexUniFS;
    GLuint              m_TriTexUniProgram;
    GLuint              m_TriVArray;
    GLuint              m_TriVertices;
    GLuint              m_TriUVs;
    GLuint              m_TriColors;
    GLint               m_TriLocationOffset;
    GLint               m_TriLocationWndSize;
    GLint               m_TriUniLocationOffset;
    GLint               m_TriUniLocationWndSize;
    GLint               m_TriUniLocationColor;
    GLint               m_TriTexLocationOffset;
    GLint               m_TriTexLocationWndSize;
    GLint               m_TriTexLocationTexture;
    GLint               m_TriTexUniLocationOffset;
    GLint               m_TriTexUniLocationWndSize;
    GLint               m_TriTexUniLocationColor;
    GLint               m_TriTexUniLocationTexture;
    size_t              m_TriBufferSize;

    int                 m_WndWidth;
    int                 m_WndHeight;
    int                 m_OffsetX;
    int                 m_OffsetY;
} TwGraphOpenGLCore;

//  ---------------------------------------------------------------------------

#ifdef _DEBUG
    static void CheckGLCoreError(const char *file, int line, const char *func)
    {
        int err=0;
        char msg[256];
        while( (err=glGetError())!=0 )
        {
            snprintf(msg, sizeof(msg), "%s(%d) : [%s] GL_CORE_ERROR=0x%x\n", file, line, func, err);
            #if defined(_WIN32)
                OutputDebugString(msg);
            #endif
            fprintf(stderr, "%s", msg);
        }
    }
#   ifdef __FUNCTION__
#       define CHECK_GL_ERROR CheckGLCoreError(__FILE__, __LINE__, __FUNCTION__)
#   else
#       define CHECK_GL_ERROR CheckGLCoreError(__FILE__, __LINE__, "")
#   endif
#else
#   define CHECK_GL_ERROR ((void)(0))
#endif

//  ---------------------------------------------------------------------------

static GLuint BindFont(const CTexFont *_Font)
{
    GLuint TexID = 0;
    glGenTextures(1, &TexID);
    glBindTexture(GL_TEXTURE_2D, TexID);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, _Font->m_TexWidth, _Font->m_TexHeight, 0, GL_RED, GL_UNSIGNED_BYTE, _Font->m_TexBytes);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    return TexID;
}

static void UnbindFont(GLuint _FontTexID)
{
    if( _FontTexID>0 )
        glDeleteTextures(1, &_FontTexID);
}

//  ---------------------------------------------------------------------------

static GLuint CompileShader(GLuint shader)
{
    glCompileShader(shader); CHECK_GL_ERROR;

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status); CHECK_GL_ERROR;
    if (status == GL_FALSE)
    {
        GLint infoLogLength;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength); CHECK_GL_ERROR;
        (void)infoLogLength;

        GLchar strInfoLog[256];
        glGetShaderInfoLog(shader, sizeof(strInfoLog), NULL, strInfoLog); CHECK_GL_ERROR;
#if defined(_WIN32)
        OutputDebugString("Compile failure: ");
        OutputDebugString(strInfoLog);
        OutputDebugString("\n");
#endif
        fprintf(stderr, "Compile failure: %s\n", strInfoLog);
        shader = 0;
    }

    return shader;
}

static GLuint LinkProgram(GLuint program)
{
    glLinkProgram(program); CHECK_GL_ERROR;

    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status); CHECK_GL_ERROR;
    if (status == GL_FALSE)
    {
        GLint infoLogLength;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength); CHECK_GL_ERROR;
        (void)infoLogLength;

        GLchar strInfoLog[256];
        glGetProgramInfoLog(program, sizeof(strInfoLog), NULL, strInfoLog); CHECK_GL_ERROR;
#if defined(_WIN32)
        OutputDebugString("Linker failure: ");
        OutputDebugString(strInfoLog);
        OutputDebugString("\n");
#endif
        fprintf(stderr, "Linker failure: %s\n", strInfoLog);
        program = 0;
    }

    return program;
}

//  ---------------------------------------------------------------------------

static void ResizeTriBuffers(TwGraphOpenGLCore *self, size_t _NewSize)
{
    self->m_TriBufferSize = _NewSize;

    glBindVertexArray(self->m_TriVArray);

    glBindBuffer(GL_ARRAY_BUFFER, self->m_TriVertices);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(self->m_TriBufferSize*sizeof(Vec2)), 0, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, self->m_TriUVs);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(self->m_TriBufferSize*sizeof(Vec2)), 0, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, self->m_TriColors);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(self->m_TriBufferSize*sizeof(color32)), 0, GL_DYNAMIC_DRAW);

    CHECK_GL_ERROR;
}

//  ---------------------------------------------------------------------------

static int TwGraphOpenGLCore_Init(ITwGraph *_This)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)_This;

    self->m_Drawing = false;
    self->m_FontTexID = 0;
    self->m_FontTex = NULL;

    // Was: LoadOpenGLCore(), a custom dynamic-loading step. GLAD (already
    // linked into the library, loaded by the host application before
    // TwInit()) already resolves every Core Profile function this renderer
    // needs; LoadOGLCore.cpp/.h were redundant with it and have been
    // deleted. This is the cheap defensive check the C99 rewrite plan
    // called for in their place: glCreateShader is representative of any
    // Core Profile function - if GLAD hasn't been loaded yet, it (and
    // everything else here) is NULL.
    if( glCreateShader==NULL )
    {
        TwSetLastError("OpenGL Core Profile functions are not loaded - call gladLoadGLLoader (or equivalent) before TwInit");
        return 0;
    }

    // Create line/rect shaders
    const GLchar *lineRectVS[] = {
        "#version 150 core\n"
        "in vec3 vertex;"
        "in vec4 color;"
        "out vec4 fcolor;"
        "void main() { gl_Position = vec4(vertex, 1); fcolor = color; }"
    };
    self->m_LineRectVS = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(self->m_LineRectVS, 1, lineRectVS, NULL);
    self->m_LineRectVS = CompileShader(self->m_LineRectVS);

    const GLchar *lineRectFS[] = {
        "#version 150 core\n"
        "precision highp float;"
        "in vec4 fcolor;"
        "out vec4 outColor;"
        "void main() { outColor = fcolor; }"
    };
    self->m_LineRectFS = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(self->m_LineRectFS, 1, lineRectFS, NULL);
    self->m_LineRectFS = CompileShader(self->m_LineRectFS);

    self->m_LineRectProgram = glCreateProgram();
    glAttachShader(self->m_LineRectProgram, self->m_LineRectVS);
    glAttachShader(self->m_LineRectProgram, self->m_LineRectFS);
    glBindAttribLocation(self->m_LineRectProgram, 0, "vertex");
    glBindAttribLocation(self->m_LineRectProgram, 1, "color");
    self->m_LineRectProgram = LinkProgram(self->m_LineRectProgram);

    // Create line/rect vertex buffer
    const GLfloat lineRectInitVertices[] = { 0,0,0, 0,0,0, 0,0,0, 0,0,0 };
    const color32 lineRectInitColors[] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };
    glGenVertexArrays(1, &self->m_LineRectVArray);
    glBindVertexArray(self->m_LineRectVArray);
    glGenBuffers(1, &self->m_LineRectVertices);
    glBindBuffer(GL_ARRAY_BUFFER, self->m_LineRectVertices);
    glBufferData(GL_ARRAY_BUFFER, sizeof(lineRectInitVertices), lineRectInitVertices, GL_DYNAMIC_DRAW);
    glGenBuffers(1, &self->m_LineRectColors);
    glBindBuffer(GL_ARRAY_BUFFER, self->m_LineRectColors);
    glBufferData(GL_ARRAY_BUFFER, sizeof(lineRectInitColors), lineRectInitColors, GL_DYNAMIC_DRAW);

    // Create triangles shaders
    const GLchar *triVS[] = {
        "#version 150 core\n"
        "uniform vec2 offset;"
        "uniform vec2 wndSize;"
        "in vec2 vertex;"
        "in vec4 color;"
        "out vec4 fcolor;"
        "void main() { gl_Position = vec4(2.0*(vertex.x+offset.x-0.5)/wndSize.x - 1.0, 1.0 - 2.0*(vertex.y+offset.y-0.5)/wndSize.y, 0, 1); fcolor = color; }"
    };
    self->m_TriVS = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(self->m_TriVS, 1, triVS, NULL);
    self->m_TriVS = CompileShader(self->m_TriVS);

    const GLchar *triUniVS[] = {
        "#version 150 core\n"
        "uniform vec2 offset;"
        "uniform vec2 wndSize;"
        "uniform vec4 color;"
        "in vec2 vertex;"
        "out vec4 fcolor;"
        "void main() { gl_Position = vec4(2.0*(vertex.x+offset.x-0.5)/wndSize.x - 1.0, 1.0 - 2.0*(vertex.y+offset.y-0.5)/wndSize.y, 0, 1); fcolor = color; }"
    };
    self->m_TriUniVS = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(self->m_TriUniVS, 1, triUniVS, NULL);
    self->m_TriUniVS = CompileShader(self->m_TriUniVS);

    self->m_TriFS = self->m_TriUniFS = self->m_LineRectFS;

    self->m_TriProgram = glCreateProgram();
    glAttachShader(self->m_TriProgram, self->m_TriVS);
    glAttachShader(self->m_TriProgram, self->m_TriFS);
    glBindAttribLocation(self->m_TriProgram, 0, "vertex");
    glBindAttribLocation(self->m_TriProgram, 1, "color");
    self->m_TriProgram = LinkProgram(self->m_TriProgram);
    self->m_TriLocationOffset = glGetUniformLocation(self->m_TriProgram, "offset");
    self->m_TriLocationWndSize = glGetUniformLocation(self->m_TriProgram, "wndSize");

    self->m_TriUniProgram = glCreateProgram();
    glAttachShader(self->m_TriUniProgram, self->m_TriUniVS);
    glAttachShader(self->m_TriUniProgram, self->m_TriUniFS);
    glBindAttribLocation(self->m_TriUniProgram, 0, "vertex");
    glBindAttribLocation(self->m_TriUniProgram, 1, "color");
    self->m_TriUniProgram = LinkProgram(self->m_TriUniProgram);
    self->m_TriUniLocationOffset = glGetUniformLocation(self->m_TriUniProgram, "offset");
    self->m_TriUniLocationWndSize = glGetUniformLocation(self->m_TriUniProgram, "wndSize");
    self->m_TriUniLocationColor = glGetUniformLocation(self->m_TriUniProgram, "color");

    const GLchar *triTexFS[] = {
        "#version 150 core\n"
        "precision highp float;"
        "uniform sampler2D tex;"
        "in vec2 fuv;"
        "in vec4 fcolor;"
        "out vec4 outColor;"
// texture2D is deprecated and replaced by texture with GLSL 3.30 but it seems
// that on Mac Lion backward compatibility is not ensured.
#if defined(__APPLE__) && (MAC_OS_X_VERSION_MAX_ALLOWED >= 1070)
        "void main() { outColor.rgb = fcolor.bgr; outColor.a = fcolor.a * texture(tex, fuv).r; }"
#else
        "void main() { outColor.rgb = fcolor.bgr; outColor.a = fcolor.a * texture2D(tex, fuv).r; }"
#endif
    };
    self->m_TriTexFS = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(self->m_TriTexFS, 1, triTexFS, NULL);
    self->m_TriTexFS = CompileShader(self->m_TriTexFS);

    const GLchar *triTexVS[] = {
        "#version 150 core\n"
        "uniform vec2 offset;"
        "uniform vec2 wndSize;"
        "in vec2 vertex;"
        "in vec2 uv;"
        "in vec4 color;"
        "out vec2 fuv;"
        "out vec4 fcolor;"
        "void main() { gl_Position = vec4(2.0*(vertex.x+offset.x-0.5)/wndSize.x - 1.0, 1.0 - 2.0*(vertex.y+offset.y-0.5)/wndSize.y, 0, 1); fuv = uv; fcolor = color; }"
    };
    self->m_TriTexVS = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(self->m_TriTexVS, 1, triTexVS, NULL);
    self->m_TriTexVS = CompileShader(self->m_TriTexVS);

    const GLchar *triTexUniVS[] = {
        "#version 150 core\n"
        "uniform vec2 offset;"
        "uniform vec2 wndSize;"
        "uniform vec4 color;"
        "in vec2 vertex;"
        "in vec2 uv;"
        "out vec4 fcolor;"
        "out vec2 fuv;"
        "void main() { gl_Position = vec4(2.0*(vertex.x+offset.x-0.5)/wndSize.x - 1.0, 1.0 - 2.0*(vertex.y+offset.y-0.5)/wndSize.y, 0, 1); fuv = uv; fcolor = color; }"
    };
    self->m_TriTexUniVS = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(self->m_TriTexUniVS, 1, triTexUniVS, NULL);
    self->m_TriTexUniVS = CompileShader(self->m_TriTexUniVS);

    self->m_TriTexUniFS = self->m_TriTexFS;

    self->m_TriTexProgram = glCreateProgram();
    glAttachShader(self->m_TriTexProgram, self->m_TriTexVS);
    glAttachShader(self->m_TriTexProgram, self->m_TriTexFS);
    glBindAttribLocation(self->m_TriTexProgram, 0, "vertex");
    glBindAttribLocation(self->m_TriTexProgram, 1, "uv");
    glBindAttribLocation(self->m_TriTexProgram, 2, "color");
    self->m_TriTexProgram = LinkProgram(self->m_TriTexProgram);
    self->m_TriTexLocationOffset = glGetUniformLocation(self->m_TriTexProgram, "offset");
    self->m_TriTexLocationWndSize = glGetUniformLocation(self->m_TriTexProgram, "wndSize");
    self->m_TriTexLocationTexture = glGetUniformLocation(self->m_TriTexProgram, "tex");

    self->m_TriTexUniProgram = glCreateProgram();
    glAttachShader(self->m_TriTexUniProgram, self->m_TriTexUniVS);
    glAttachShader(self->m_TriTexUniProgram, self->m_TriTexUniFS);
    glBindAttribLocation(self->m_TriTexUniProgram, 0, "vertex");
    glBindAttribLocation(self->m_TriTexUniProgram, 1, "uv");
    glBindAttribLocation(self->m_TriTexUniProgram, 2, "color");
    self->m_TriTexUniProgram = LinkProgram(self->m_TriTexUniProgram);
    self->m_TriTexUniLocationOffset = glGetUniformLocation(self->m_TriTexUniProgram, "offset");
    self->m_TriTexUniLocationWndSize = glGetUniformLocation(self->m_TriTexUniProgram, "wndSize");
    self->m_TriTexUniLocationColor = glGetUniformLocation(self->m_TriTexUniProgram, "color");
    self->m_TriTexUniLocationTexture = glGetUniformLocation(self->m_TriTexUniProgram, "tex");

    // Create tri vertex buffer
    glGenVertexArrays(1, &self->m_TriVArray);
    glGenBuffers(1, &self->m_TriVertices);
    glGenBuffers(1, &self->m_TriUVs);
    glGenBuffers(1, &self->m_TriColors);
    ResizeTriBuffers(self, 16384); // set initial size

    // CompileShader/LinkProgram report failure by returning 0 for the
    // handle they were given; check every one of them now instead of
    // reporting Init() success while every subsequent draw call would
    // silently render nothing/garbage with a broken shader/program.
    if(    self->m_LineRectVS==0 || self->m_LineRectFS==0 || self->m_LineRectProgram==0
        || self->m_TriVS==0 || self->m_TriUniVS==0 || self->m_TriProgram==0 || self->m_TriUniProgram==0
        || self->m_TriTexFS==0 || self->m_TriTexVS==0 || self->m_TriTexUniVS==0
        || self->m_TriTexProgram==0 || self->m_TriTexUniProgram==0 )
    {
        TwSetLastError("Failed to compile or link one or more OpenGL Core Profile shaders (see stderr for the compiler/linker log)");
        return 0;
    }

    CHECK_GL_ERROR;
    return 1;
}

//  ---------------------------------------------------------------------------

static int TwGraphOpenGLCore_Shut(ITwGraph *_This)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)_This;
    assert(self->m_Drawing==false);

    UnbindFont(self->m_FontTexID);

    CHECK_GL_ERROR;

    glDeleteProgram(self->m_LineRectProgram); self->m_LineRectProgram = 0;
    glDeleteShader(self->m_LineRectVS); self->m_LineRectVS = 0;
    glDeleteShader(self->m_LineRectFS); self->m_LineRectFS = 0;

    glDeleteProgram(self->m_TriProgram); self->m_TriProgram = 0;
    glDeleteShader(self->m_TriVS); self->m_TriVS = 0;

    glDeleteProgram(self->m_TriUniProgram); self->m_TriUniProgram = 0;
    glDeleteShader(self->m_TriUniVS); self->m_TriUniVS = 0;

    glDeleteProgram(self->m_TriTexProgram); self->m_TriTexProgram = 0;
    glDeleteShader(self->m_TriTexVS); self->m_TriTexVS = 0;
    glDeleteShader(self->m_TriTexFS); self->m_TriTexFS = 0;

    glDeleteProgram(self->m_TriTexUniProgram); self->m_TriTexUniProgram = 0;
    glDeleteShader(self->m_TriTexUniVS); self->m_TriTexUniVS = 0;

    glDeleteBuffers(1, &self->m_LineRectVertices); self->m_LineRectVertices = 0;
    glDeleteBuffers(1, &self->m_LineRectColors); self->m_LineRectColors = 0;
    glDeleteVertexArrays(1, &self->m_LineRectVArray); self->m_LineRectVArray = 0;

    glDeleteBuffers(1, &self->m_TriVertices); self->m_TriVertices = 0;
    glDeleteBuffers(1, &self->m_TriColors); self->m_TriColors = 0;
    glDeleteBuffers(1, &self->m_TriUVs); self->m_TriUVs = 0;
    glDeleteVertexArrays(1, &self->m_TriVArray); self->m_TriVArray = 0;

    CHECK_GL_ERROR;

    // Was: UnloadOpenGLCore() and its failure check - nothing to unload
    // now that this renderer calls GLAD's functions directly rather than
    // owning its own dynamic loader (see TwGraphOpenGLCore_Init above).
    return 1;
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGLCore_BeginDraw(ITwGraph *_This, int _WndWidth, int _WndHeight)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)_This;
    CHECK_GL_ERROR;
    assert(self->m_Drawing==false && _WndWidth>0 && _WndHeight>0);
    self->m_Drawing = true;
    self->m_WndWidth = _WndWidth;
    self->m_WndHeight = _WndHeight;
    self->m_OffsetX = 0;
    self->m_OffsetY = 0;

    glGetIntegerv(GL_VIEWPORT, self->m_PrevViewport); CHECK_GL_ERROR;
    if( _WndWidth>0 && _WndHeight>0 )
    {
        GLint Vp[4];
        Vp[0] = 0;
        Vp[1] = 0;
        Vp[2] = _WndWidth-1;
        Vp[3] = _WndHeight-1;
        glViewport(Vp[0], Vp[1], Vp[2], Vp[3]);
    }

    self->m_PrevVArray = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, (GLint*)&self->m_PrevVArray); CHECK_GL_ERROR;
    glBindVertexArray(0); CHECK_GL_ERROR;

    self->m_PrevLineWidth = 1;
    glGetFloatv(GL_LINE_WIDTH, &self->m_PrevLineWidth); CHECK_GL_ERROR;
    glLineWidth(1); CHECK_GL_ERROR;

    self->m_PrevLineSmooth = glIsEnabled(GL_LINE_SMOOTH);
    glDisable(GL_LINE_SMOOTH); CHECK_GL_ERROR;

    self->m_PrevCullFace = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE); CHECK_GL_ERROR;

    self->m_PrevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST); CHECK_GL_ERROR;

    self->m_PrevBlend = glIsEnabled(GL_BLEND);
    glEnable(GL_BLEND); CHECK_GL_ERROR;

    self->m_PrevScissorTest = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST); CHECK_GL_ERROR;

    glGetIntegerv(GL_SCISSOR_BOX, self->m_PrevScissorBox); CHECK_GL_ERROR;

    glGetIntegerv(GL_BLEND_SRC, &self->m_PrevSrcBlend); CHECK_GL_ERROR;
    glGetIntegerv(GL_BLEND_DST, &self->m_PrevDstBlend); CHECK_GL_ERROR;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); CHECK_GL_ERROR;

    self->m_PrevTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &self->m_PrevTexture); CHECK_GL_ERROR;
    glBindTexture(GL_TEXTURE_2D, 0); CHECK_GL_ERROR;

    self->m_PrevProgramObject = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, (GLint*)&self->m_PrevProgramObject); CHECK_GL_ERROR;
    glBindVertexArray(0); CHECK_GL_ERROR;
    glUseProgram(0); CHECK_GL_ERROR;

    self->m_PrevActiveTexture = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, (GLint*)&self->m_PrevActiveTexture); CHECK_GL_ERROR;
    glActiveTexture(GL_TEXTURE0);

    CHECK_GL_ERROR;
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGLCore_EndDraw(ITwGraph *_This)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)_This;
    assert(self->m_Drawing==true);
    self->m_Drawing = false;

    glLineWidth(self->m_PrevLineWidth); CHECK_GL_ERROR;

    if( self->m_PrevLineSmooth )
    {
      glEnable(GL_LINE_SMOOTH); CHECK_GL_ERROR;
    }
    else
    {
      glDisable(GL_LINE_SMOOTH); CHECK_GL_ERROR;
    }

    if( self->m_PrevCullFace )
    {
      glEnable(GL_CULL_FACE); CHECK_GL_ERROR;
    }
    else
    {
      glDisable(GL_CULL_FACE); CHECK_GL_ERROR;
    }

    if( self->m_PrevDepthTest )
    {
      glEnable(GL_DEPTH_TEST); CHECK_GL_ERROR;
    }
    else
    {
      glDisable(GL_DEPTH_TEST); CHECK_GL_ERROR;
    }

    if( self->m_PrevBlend )
    {
      glEnable(GL_BLEND); CHECK_GL_ERROR;
    }
    else
    {
      glDisable(GL_BLEND); CHECK_GL_ERROR;
    }

    if( self->m_PrevScissorTest )
    {
      glEnable(GL_SCISSOR_TEST); CHECK_GL_ERROR;
    }
    else
    {
      glDisable(GL_SCISSOR_TEST); CHECK_GL_ERROR;
    }

    glScissor(self->m_PrevScissorBox[0], self->m_PrevScissorBox[1], self->m_PrevScissorBox[2], self->m_PrevScissorBox[3]); CHECK_GL_ERROR;

    glBlendFunc(self->m_PrevSrcBlend, self->m_PrevDstBlend); CHECK_GL_ERROR;

    glBindTexture(GL_TEXTURE_2D, self->m_PrevTexture); CHECK_GL_ERROR;

    glUseProgram(self->m_PrevProgramObject); CHECK_GL_ERROR;

    glBindVertexArray(self->m_PrevVArray); CHECK_GL_ERROR;

    glViewport(self->m_PrevViewport[0], self->m_PrevViewport[1], self->m_PrevViewport[2], self->m_PrevViewport[3]); CHECK_GL_ERROR;

    CHECK_GL_ERROR;
}

//  ---------------------------------------------------------------------------

static bool TwGraphOpenGLCore_IsDrawing(ITwGraph *_This)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)_This;
    return self->m_Drawing;
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGLCore_Restore(ITwGraph *_This)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)_This;
    UnbindFont(self->m_FontTexID);
    self->m_FontTexID = 0;
    self->m_FontTex = NULL;
}

//  ---------------------------------------------------------------------------

static float ToNormScreenX(float x, int wndWidth)
{
    return 2.0f*(x-0.5f)/(float)wndWidth - 1.0f;
}

static float ToNormScreenY(float y, int wndHeight)
{
    return 1.0f - 2.0f*(y-0.5f)/(float)wndHeight;
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGLCore_DrawLine(ITwGraph *_This, int _X0, int _Y0, int _X1, int _Y1, color32 _Color0, color32 _Color1, bool _AntiAliased)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)_This;
    CHECK_GL_ERROR;
    assert(self->m_Drawing==true);

    const GLfloat dx = 0;
    const GLfloat dy = -0.5f;
    if( _AntiAliased )
        glEnable(GL_LINE_SMOOTH);
    else
        glDisable(GL_LINE_SMOOTH);

    glBindVertexArray(self->m_LineRectVArray);

    GLfloat x0 = ToNormScreenX((GLfloat)_X0+dx + (GLfloat)self->m_OffsetX, self->m_WndWidth);
    GLfloat y0 = ToNormScreenY((GLfloat)_Y0+dy + (GLfloat)self->m_OffsetY, self->m_WndHeight);
    GLfloat x1 = ToNormScreenX((GLfloat)_X1+dx + (GLfloat)self->m_OffsetX, self->m_WndWidth);
    GLfloat y1 = ToNormScreenY((GLfloat)_Y1+dy + (GLfloat)self->m_OffsetY, self->m_WndHeight);
    GLfloat vertices[] = { x0,y0,0,  x1,y1,0 };
    glBindBuffer(GL_ARRAY_BUFFER, self->m_LineRectVertices);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_TRUE, 0, NULL);
    glEnableVertexAttribArray(0);

    color32 colors[] = { _Color0, _Color1 };
    glBindBuffer(GL_ARRAY_BUFFER, self->m_LineRectColors);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(colors), colors);
    glVertexAttribPointer(1, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 0, NULL);
    glEnableVertexAttribArray(1);

    glUseProgram(self->m_LineRectProgram);
    glDrawArrays(GL_LINES, 0, 2);

    if( _AntiAliased )
        glDisable(GL_LINE_SMOOTH);

    CHECK_GL_ERROR;
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGLCore_DrawRect(ITwGraph *_This, int _X0, int _Y0, int _X1, int _Y1, color32 _Color00, color32 _Color10, color32 _Color01, color32 _Color11)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)_This;
    CHECK_GL_ERROR;
    assert(self->m_Drawing==true);

    // border adjustment
    if(_X0<_X1)
        ++_X1;
    else if(_X0>_X1)
        ++_X0;
    if(_Y0<_Y1)
        --_Y0;
    else if(_Y0>_Y1)
        --_Y1;

    glBindVertexArray(self->m_LineRectVArray);

    GLfloat x0 = ToNormScreenX((float)_X0 + (float)self->m_OffsetX, self->m_WndWidth);
    GLfloat y0 = ToNormScreenY((float)_Y0 + (float)self->m_OffsetY, self->m_WndHeight);
    GLfloat x1 = ToNormScreenX((float)_X1 + (float)self->m_OffsetX, self->m_WndWidth);
    GLfloat y1 = ToNormScreenY((float)_Y1 + (float)self->m_OffsetY, self->m_WndHeight);
    GLfloat vertices[] = { x0,y0,0, x1,y0,0, x0,y1,0, x1,y1,0 };
    glBindBuffer(GL_ARRAY_BUFFER, self->m_LineRectVertices);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_TRUE, 0, NULL);
    glEnableVertexAttribArray(0);

    GLuint colors[] = { _Color00, _Color10, _Color01, _Color11 };
    glBindBuffer(GL_ARRAY_BUFFER, self->m_LineRectColors);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(colors), colors);
    glVertexAttribPointer(1, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 0, NULL);
    glEnableVertexAttribArray(1);

    glUseProgram(self->m_LineRectProgram);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    CHECK_GL_ERROR;
}

//  ---------------------------------------------------------------------------

static void *TwGraphOpenGLCore_NewTextObj(ITwGraph *_This)
{
    (void)_This;
    return calloc(1, sizeof(CTextObj));
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGLCore_DeleteTextObj(ITwGraph *_This, void *_TextObj)
{
    (void)_This;
    assert(_TextObj!=NULL);
    CTextObj *TextObj = (CTextObj *)_TextObj;
    free(TextObj->m_TextVerts.items);
    free(TextObj->m_TextUVs.items);
    free(TextObj->m_BgVerts.items);
    free(TextObj->m_Colors.items);
    free(TextObj->m_BgColors.items);
    free(TextObj);
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGLCore_BuildText(ITwGraph *_This, void *_TextObj, const char * const *_TextLines, color32 *_LineColors, color32 *_LineBgColors, int _NbLines, const CTexFont *_Font, int _Sep, int _BgWidth)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)_This;
    assert(self->m_Drawing==true);
    assert(_TextObj!=NULL);
    assert(_Font!=NULL);

    if( _Font != self->m_FontTex )
    {
        UnbindFont(self->m_FontTexID);
        self->m_FontTexID = BindFont(_Font);
        self->m_FontTex = _Font;
    }
    CTextObj *TextObj = (CTextObj *)_TextObj;
    TextObj->m_TextVerts.count = 0;
    TextObj->m_TextUVs.count = 0;
    TextObj->m_BgVerts.count = 0;
    TextObj->m_Colors.count = 0;
    TextObj->m_BgColors.count = 0;

    int x, x1, y, y1, i, Len, Line;
    unsigned char ch;
    const unsigned char *Text;
    color32 LineColor = COLOR32_RED;
    for( Line=0; Line<_NbLines; ++Line )
    {
        x = 0;
        y = Line * (_Font->m_CharHeight+_Sep);
        y1 = y+_Font->m_CharHeight;
        Len = (int)strlen(_TextLines[Line]);
        Text = (const unsigned char *)_TextLines[Line];
        if( _LineColors!=NULL )
            LineColor = (_LineColors[Line]&0xff00ff00) | (color32)(GLubyte)(_LineColors[Line]>>16) | ((color32)(GLubyte)(_LineColors[Line])<<16);

        for( i=0; i<Len; ++i )
        {
            ch = Text[i];
            x1 = x + _Font->m_CharWidth[ch];

            Vec2Array_Push(&TextObj->m_TextVerts, Vec2Make((GLfloat)x , (GLfloat)y ));
            Vec2Array_Push(&TextObj->m_TextVerts, Vec2Make((GLfloat)x1, (GLfloat)y ));
            Vec2Array_Push(&TextObj->m_TextVerts, Vec2Make((GLfloat)x , (GLfloat)y1));
            Vec2Array_Push(&TextObj->m_TextVerts, Vec2Make((GLfloat)x1, (GLfloat)y ));
            Vec2Array_Push(&TextObj->m_TextVerts, Vec2Make((GLfloat)x1, (GLfloat)y1));
            Vec2Array_Push(&TextObj->m_TextVerts, Vec2Make((GLfloat)x , (GLfloat)y1));

            Vec2Array_Push(&TextObj->m_TextUVs, Vec2Make(_Font->m_CharU0[ch], _Font->m_CharV0[ch]));
            Vec2Array_Push(&TextObj->m_TextUVs, Vec2Make(_Font->m_CharU1[ch], _Font->m_CharV0[ch]));
            Vec2Array_Push(&TextObj->m_TextUVs, Vec2Make(_Font->m_CharU0[ch], _Font->m_CharV1[ch]));
            Vec2Array_Push(&TextObj->m_TextUVs, Vec2Make(_Font->m_CharU1[ch], _Font->m_CharV0[ch]));
            Vec2Array_Push(&TextObj->m_TextUVs, Vec2Make(_Font->m_CharU1[ch], _Font->m_CharV1[ch]));
            Vec2Array_Push(&TextObj->m_TextUVs, Vec2Make(_Font->m_CharU0[ch], _Font->m_CharV1[ch]));

            if( _LineColors!=NULL )
            {
                Color32Array_Push(&TextObj->m_Colors, LineColor);
                Color32Array_Push(&TextObj->m_Colors, LineColor);
                Color32Array_Push(&TextObj->m_Colors, LineColor);
                Color32Array_Push(&TextObj->m_Colors, LineColor);
                Color32Array_Push(&TextObj->m_Colors, LineColor);
                Color32Array_Push(&TextObj->m_Colors, LineColor);
            }

            x = x1;
        }
        if( _BgWidth>0 )
        {
            Vec2Array_Push(&TextObj->m_BgVerts, Vec2Make(-1           , (GLfloat)y ));
            Vec2Array_Push(&TextObj->m_BgVerts, Vec2Make((GLfloat)_BgWidth+1, (GLfloat)y ));
            Vec2Array_Push(&TextObj->m_BgVerts, Vec2Make(-1           , (GLfloat)y1));
            Vec2Array_Push(&TextObj->m_BgVerts, Vec2Make((GLfloat)_BgWidth+1, (GLfloat)y ));
            Vec2Array_Push(&TextObj->m_BgVerts, Vec2Make((GLfloat)_BgWidth+1, (GLfloat)y1));
            Vec2Array_Push(&TextObj->m_BgVerts, Vec2Make(-1           , (GLfloat)y1));

            if( _LineBgColors!=NULL )
            {
                color32 LineBgColor = (_LineBgColors[Line]&0xff00ff00) | (color32)(GLubyte)(_LineBgColors[Line]>>16) | ((color32)(GLubyte)(_LineBgColors[Line])<<16);
                Color32Array_Push(&TextObj->m_BgColors, LineBgColor);
                Color32Array_Push(&TextObj->m_BgColors, LineBgColor);
                Color32Array_Push(&TextObj->m_BgColors, LineBgColor);
                Color32Array_Push(&TextObj->m_BgColors, LineBgColor);
                Color32Array_Push(&TextObj->m_BgColors, LineBgColor);
                Color32Array_Push(&TextObj->m_BgColors, LineBgColor);
            }
        }
    }
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGLCore_DrawText(ITwGraph *_This, void *_TextObj, int _X, int _Y, color32 _Color, color32 _BgColor)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)_This;
    CHECK_GL_ERROR;
    assert(self->m_Drawing==true);
    assert(_TextObj!=NULL);
    CTextObj *TextObj = (CTextObj *)_TextObj;

    if( TextObj->m_TextVerts.count<4 && TextObj->m_BgVerts.count<4 )
        return; // nothing to draw

    // draw character background triangles
    if( (_BgColor!=0 || TextObj->m_BgColors.count==TextObj->m_BgVerts.count) && TextObj->m_BgVerts.count>=4 )
    {
        size_t numBgVerts = TextObj->m_BgVerts.count;
        if( numBgVerts > self->m_TriBufferSize )
            ResizeTriBuffers(self, numBgVerts + 2048);

        glBindVertexArray(self->m_TriVArray);

        glBindBuffer(GL_ARRAY_BUFFER, self->m_TriVertices);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(numBgVerts*sizeof(Vec2)), &(TextObj->m_BgVerts.items[0]));
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_TRUE, 0, NULL);
        glEnableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);

        if( TextObj->m_BgColors.count==TextObj->m_BgVerts.count && _BgColor==0 )
        {
            glBindBuffer(GL_ARRAY_BUFFER, self->m_TriColors);
            glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(numBgVerts*sizeof(color32)), &(TextObj->m_BgColors.items[0]));
            glVertexAttribPointer(1, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 0, NULL);
            glEnableVertexAttribArray(1);

            glUseProgram(self->m_TriProgram);
            glUniform2f(self->m_TriLocationOffset, (float)_X, (float)_Y);
            glUniform2f(self->m_TriLocationWndSize, (float)self->m_WndWidth, (float)self->m_WndHeight);
        }
        else
        {
            glUseProgram(self->m_TriUniProgram);
            glUniform4f(self->m_TriUniLocationColor, (GLfloat)((_BgColor>>16)&0xff)/256.0f, (GLfloat)((_BgColor>>8)&0xff)/256.0f, (GLfloat)(_BgColor&0xff)/256.0f, (GLfloat)((_BgColor>>24)&0xff)/256.0f);
            glUniform2f(self->m_TriUniLocationOffset, (float)_X, (float)_Y);
            glUniform2f(self->m_TriUniLocationWndSize, (float)self->m_WndWidth, (float)self->m_WndHeight);
        }

        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)TextObj->m_BgVerts.count);
    }

    // draw character triangles
    if( TextObj->m_TextVerts.count>=4 )
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, self->m_FontTexID);
        size_t numTextVerts = TextObj->m_TextVerts.count;
        if( numTextVerts > self->m_TriBufferSize )
            ResizeTriBuffers(self, numTextVerts + 2048);

        glBindVertexArray(self->m_TriVArray);
        glDisableVertexAttribArray(2);

        glBindBuffer(GL_ARRAY_BUFFER, self->m_TriVertices);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(numTextVerts*sizeof(Vec2)), &(TextObj->m_TextVerts.items[0]));
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_TRUE, 0, NULL);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, self->m_TriUVs);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(numTextVerts*sizeof(Vec2)), &(TextObj->m_TextUVs.items[0]));
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, NULL);
        glEnableVertexAttribArray(1);

        if( TextObj->m_Colors.count==TextObj->m_TextVerts.count && _Color==0 )
        {
            glBindBuffer(GL_ARRAY_BUFFER, self->m_TriColors);
            glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(numTextVerts*sizeof(color32)), &(TextObj->m_Colors.items[0]));
            glVertexAttribPointer(2, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 0, NULL);
            glEnableVertexAttribArray(2);

            glUseProgram(self->m_TriTexProgram);
            glUniform2f(self->m_TriTexLocationOffset, (float)_X, (float)_Y);
            glUniform2f(self->m_TriTexLocationWndSize, (float)self->m_WndWidth, (float)self->m_WndHeight);
            glUniform1i(self->m_TriTexLocationTexture, 0);
        }
        else
        {
            glUseProgram(self->m_TriTexUniProgram);
            glUniform4f(self->m_TriTexUniLocationColor, (GLfloat)((_Color>>16)&0xff)/256.0f, (GLfloat)((_Color>>8)&0xff)/256.0f, (GLfloat)(_Color&0xff)/256.0f, (GLfloat)((_Color>>24)&0xff)/256.0f);
            glUniform2f(self->m_TriTexUniLocationOffset, (float)_X, (float)_Y);
            glUniform2f(self->m_TriTexUniLocationWndSize, (float)self->m_WndWidth, (float)self->m_WndHeight);
            glUniform1i(self->m_TriTexUniLocationTexture, 0);
        }

        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)TextObj->m_TextVerts.count);
    }

    CHECK_GL_ERROR;
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGLCore_SetScissor(ITwGraph *_This, int _X0, int _Y0, int _Width, int _Height);

static void TwGraphOpenGLCore_ChangeViewport(ITwGraph *_This, int _X0, int _Y0, int _Width, int _Height, int _OffsetX, int _OffsetY)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)_This;
    // glViewport impacts the NDC; use glScissor instead
    self->m_OffsetX = _X0 + _OffsetX;
    self->m_OffsetY = _Y0 + _OffsetY;
    TwGraphOpenGLCore_SetScissor(_This, _X0, _Y0, _Width, _Height);
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGLCore_RestoreViewport(ITwGraph *_This)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)_This;
    self->m_OffsetX = self->m_OffsetY = 0;
    TwGraphOpenGLCore_SetScissor(_This, 0, 0, 0, 0);
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGLCore_SetScissor(ITwGraph *_This, int _X0, int _Y0, int _Width, int _Height)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)_This;
    if( _Width>0 && _Height>0 )
    {
        glScissor(_X0-1, self->m_WndHeight-_Y0-_Height, _Width-1, _Height);
        glEnable(GL_SCISSOR_TEST);
    }
    else
        glDisable(GL_SCISSOR_TEST);
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGLCore_DrawTriangles(ITwGraph *_This, int _NumTriangles, int *_Vertices, color32 *_Colors, enum TwGraphCull _CullMode)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)_This;
    assert(self->m_Drawing==true);

    const GLfloat dx = +0.0f;
    const GLfloat dy = +0.0f;

    // Backup states
    GLint prevCullFaceMode, prevFrontFace;
    glGetIntegerv(GL_CULL_FACE_MODE, &prevCullFaceMode);
    glGetIntegerv(GL_FRONT_FACE, &prevFrontFace);
    GLboolean prevCullEnable = glIsEnabled(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_CULL_FACE);
    if( _CullMode==TW_GRAPH_CULL_CW )
        glFrontFace(GL_CCW);
    else if( _CullMode==TW_GRAPH_CULL_CCW )
        glFrontFace(GL_CW);
    else
        glDisable(GL_CULL_FACE);

    glUseProgram(self->m_TriProgram);
    glBindVertexArray(self->m_TriVArray);
    glUniform2f(self->m_TriLocationOffset, (float)self->m_OffsetX+dx, (float)self->m_OffsetY+dy);
    glUniform2f(self->m_TriLocationWndSize, (float)self->m_WndWidth, (float)self->m_WndHeight);
    glDisableVertexAttribArray(2);

    size_t numVerts = 3*(size_t)_NumTriangles;
    if( numVerts > self->m_TriBufferSize )
        ResizeTriBuffers(self, numVerts + 2048);

    glBindBuffer(GL_ARRAY_BUFFER, self->m_TriVertices);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(numVerts*2*sizeof(int)), _Vertices);
    glVertexAttribPointer(0, 2, GL_INT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, self->m_TriColors);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(numVerts*sizeof(color32)), _Colors);
    glVertexAttribPointer(1, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 0, NULL);
    glEnableVertexAttribArray(1);

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)numVerts);

    // Reset states
    glCullFace(prevCullFaceMode);
    glFrontFace(prevFrontFace);
    if( prevCullEnable )
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);

    CHECK_GL_ERROR;
}

//  ---------------------------------------------------------------------------

ITwGraph *TwGraphOpenGLCore_Create(void)
{
    TwGraphOpenGLCore *self = (TwGraphOpenGLCore *)calloc(1, sizeof(TwGraphOpenGLCore));
    if( !self )
        return NULL;

    self->base.Init             = TwGraphOpenGLCore_Init;
    self->base.Shut             = TwGraphOpenGLCore_Shut;
    self->base.BeginDraw        = TwGraphOpenGLCore_BeginDraw;
    self->base.EndDraw          = TwGraphOpenGLCore_EndDraw;
    self->base.IsDrawing        = TwGraphOpenGLCore_IsDrawing;
    self->base.Restore          = TwGraphOpenGLCore_Restore;
    self->base.DrawLine         = TwGraphOpenGLCore_DrawLine;
    self->base.DrawRect         = TwGraphOpenGLCore_DrawRect;
    self->base.DrawTriangles    = TwGraphOpenGLCore_DrawTriangles;
    self->base.NewTextObj       = TwGraphOpenGLCore_NewTextObj;
    self->base.DeleteTextObj    = TwGraphOpenGLCore_DeleteTextObj;
    self->base.BuildText        = TwGraphOpenGLCore_BuildText;
    self->base.DrawText         = TwGraphOpenGLCore_DrawText;
    self->base.ChangeViewport   = TwGraphOpenGLCore_ChangeViewport;
    self->base.RestoreViewport  = TwGraphOpenGLCore_RestoreViewport;
    self->base.SetScissor       = TwGraphOpenGLCore_SetScissor;

    return &self->base;
}
