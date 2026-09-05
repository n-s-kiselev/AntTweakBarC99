//  ---------------------------------------------------------------------------
//
//  @file       TwOpenGL.c
//  @author     Philippe Decaudin
//  @license    This file is part of the AntTweakBar library.
//              For conditions of distribution and use, see License.txt
//
//  ---------------------------------------------------------------------------


#include <glad/glad.h>
#include "TwOpenGL.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

// TwSetLastError: internal C-linkage bridge into the still-C++ CTwMgr (see
// its declaration in TwMgr.h). This file is plain C99 and cannot include
// TwMgr.h itself (it declares C++ classes), so it's forward-declared here
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

static const char *g_ErrGLADNotLoaded = "OpenGL not loaded: call gladLoadGLLoader() before TwInit()";

// These 3 are plain enum tokens from old ARB extensions (predating the
// features they name being promoted to core, if ever) - GLAD's generated
// header does not declare them (confirmed by inspection: GLAD omits
// extension-only tokens its generation config didn't request), but
// glIsEnabled/glEnable/glDisable accept any GLenum value, extension-defined
// or not, so they're still safe/meaningful to pass through unconditionally.
#ifndef GL_VERTEX_PROGRAM_ARB
#   define GL_VERTEX_PROGRAM_ARB 0x8620
#endif
#ifndef GL_FRAGMENT_PROGRAM_ARB
#   define GL_FRAGMENT_PROGRAM_ARB 0x8804
#endif
#ifndef GL_TEXTURE_RECTANGLE_ARB
#   define GL_TEXTURE_RECTANGLE_ARB 0x84F5
#endif

GLuint g_SmallFontTexID = 0;
GLuint g_NormalFontTexID = 0;
GLuint g_LargeFontTexID = 0;

//  ---------------------------------------------------------------------------
//  Vec2 / dynamic arrays (was std::vector<Vec2>/std::vector<color32> members
//  of the original C++ CTextObj)

typedef struct Vec2 { GLfloat x, y; } Vec2;

static inline Vec2 Vec2Make(GLfloat _X, GLfloat _Y)
{
    Vec2 v;
    v.x = _X;
    v.y = _Y;
    return v;
}

typedef struct Vec2Array { Vec2 *items; size_t count, capacity; } Vec2Array;
typedef struct Color32Array { color32 *items; size_t count, capacity; } Color32Array;

static void Vec2Array_Push(Vec2Array *_Arr, Vec2 _V)
{
    if( _Arr->count>=_Arr->capacity )
    {
        _Arr->capacity = _Arr->capacity ? _Arr->capacity*2 : 16;
        _Arr->items = (Vec2 *)realloc(_Arr->items, _Arr->capacity*sizeof(Vec2));
    }
    _Arr->items[_Arr->count++] = _V;
}

static void Vec2Array_Clear(Vec2Array *_Arr) { _Arr->count = 0; }
static void Vec2Array_Free(Vec2Array *_Arr) { free(_Arr->items); _Arr->items = NULL; _Arr->count = _Arr->capacity = 0; }

static void Color32Array_Push(Color32Array *_Arr, color32 _C)
{
    if( _Arr->count>=_Arr->capacity )
    {
        _Arr->capacity = _Arr->capacity ? _Arr->capacity*2 : 16;
        _Arr->items = (color32 *)realloc(_Arr->items, _Arr->capacity*sizeof(color32));
    }
    _Arr->items[_Arr->count++] = _C;
}

static void Color32Array_Clear(Color32Array *_Arr) { _Arr->count = 0; }
static void Color32Array_Free(Color32Array *_Arr) { free(_Arr->items); _Arr->items = NULL; _Arr->count = _Arr->capacity = 0; }

typedef struct CTextObj
{
    Vec2Array       m_TextVerts;
    Vec2Array       m_TextUVs;
    Vec2Array       m_BgVerts;
    Color32Array    m_Colors;
    Color32Array    m_BgColors;
} CTextObj;

//  ---------------------------------------------------------------------------

#ifdef _DEBUG
    static void CheckGLError(const char *file, int line, const char *func)
    {
        int err=0;
        char msg[256];
        while( (err=glGetError())!=0 )
        {
            snprintf(msg, sizeof(msg), "%s(%d) : [%s] GL_ERROR=0x%x\n", file, line, func, err);
            #ifdef ANT_WINDOWS
                OutputDebugString(msg);
            #endif
            fprintf(stderr, "%s", msg);
        }
    }
#   ifdef __FUNCTION__
#       define CHECK_GL_ERROR CheckGLError(__FILE__, __LINE__, __FUNCTION__)
#   else
#       define CHECK_GL_ERROR CheckGLError(__FILE__, __LINE__, "")
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
    glPixelTransferf(GL_ALPHA_SCALE, 1);
    glPixelTransferf(GL_ALPHA_BIAS, 0);
    glPixelTransferf(GL_RED_BIAS, 1);
    glPixelTransferf(GL_GREEN_BIAS, 1);
    glPixelTransferf(GL_BLUE_BIAS, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, 4, _Font->m_TexWidth, _Font->m_TexHeight, 0, GL_ALPHA, GL_UNSIGNED_BYTE, _Font->m_TexBytes);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPixelTransferf(GL_ALPHA_BIAS, 0);
    glPixelTransferf(GL_RED_BIAS, 0);
    glPixelTransferf(GL_GREEN_BIAS, 0);
    glPixelTransferf(GL_BLUE_BIAS, 0);

    return TexID;
}

static void UnbindFont(GLuint _FontTexID)
{
    if( _FontTexID>0 )
        glDeleteTextures(1, &_FontTexID);
}

//  ---------------------------------------------------------------------------

static int TwGraphOpenGL_Init(ITwGraph *_This)
{
    TwGraphOpenGL *self = (TwGraphOpenGL *)_This;
    self->m_Drawing = false;
    self->m_FontTexID = 0;
    self->m_FontTex = NULL;
    self->m_MaxClipPlanes = -1;

    // GLAD is expected to already be loaded (gladLoadGLLoader()) by the
    // application before TwInit() - true of every example in examples/.
    // glBegin is the most fundamental function this compatibility-profile
    // renderer depends on (used by every Draw* method below); if it's
    // NULL, GLAD was never loaded.
    if( glBegin==NULL )
    {
        TwSetLastError(g_ErrGLADNotLoaded);
        return 0;
    }

    self->m_SupportTexRect = false; // updated in BeginDraw

    return 1;
}

//  ---------------------------------------------------------------------------

static int TwGraphOpenGL_Shut(ITwGraph *_This)
{
    TwGraphOpenGL *self = (TwGraphOpenGL *)_This;
    assert(self->m_Drawing==false);

    UnbindFont(self->m_FontTexID);

    return 1;
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGL_BeginDraw(ITwGraph *_This, int _WndWidth, int _WndHeight)
{
    TwGraphOpenGL *self = (TwGraphOpenGL *)_This;
    assert(self->m_Drawing==false && _WndWidth>0 && _WndHeight>0);
    self->m_Drawing = true;
    self->m_WndWidth = _WndWidth;
    self->m_WndHeight = _WndHeight;

    CHECK_GL_ERROR;

    {
        static bool s_SupportTexRectChecked = false;
        if (!s_SupportTexRectChecked)
        {
            const char *ext = (const char *)glGetString(GL_EXTENSIONS);
            if( ext!=0 && strlen(ext)>0 )
                self->m_SupportTexRect = (strstr(ext, "GL_ARB_texture_rectangle")!=NULL);
            s_SupportTexRectChecked = true;
        }
    }

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);

    {
        GLint maxTexUnits = 1;
        GLint i;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &self->m_PrevActiveTexture);
        glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &self->m_PrevClientActiveTexture);
        glGetIntegerv(GL_MAX_TEXTURE_COORDS, &maxTexUnits);
        if( maxTexUnits<1 )
            maxTexUnits = 1;
        else if( maxTexUnits > TW_GRAPH_OPENGL_MAX_TEXTURES )
            maxTexUnits = TW_GRAPH_OPENGL_MAX_TEXTURES;
        for( i=0; i<maxTexUnits; ++i )
        {
            glActiveTexture(GL_TEXTURE0+i);
            self->m_PrevActiveTexture1D[i] = glIsEnabled(GL_TEXTURE_1D);
            self->m_PrevActiveTexture2D[i] = glIsEnabled(GL_TEXTURE_2D);
            self->m_PrevActiveTexture3D[i] = glIsEnabled(GL_TEXTURE_3D);
            glDisable(GL_TEXTURE_1D);
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_TEXTURE_3D);
        }
        glActiveTexture(GL_TEXTURE0);

        for( i=0; i<maxTexUnits; i++ )
        {
            glClientActiveTexture(GL_TEXTURE0+i);
            self->m_PrevClientTexCoordArray[i] = glIsEnabled(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        glClientActiveTexture(GL_TEXTURE0);
    }

    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    {
    GLint Vp[4];
    glGetIntegerv(GL_VIEWPORT, Vp);
    if( _WndWidth>0 && _WndHeight>0 )
    {
        Vp[0] = 0;
        Vp[1] = 0;
        Vp[2] = _WndWidth-1;
        Vp[3] = _WndHeight-1;
        glViewport(Vp[0], Vp[1], Vp[2], Vp[3]);
    }
    glLoadIdentity();
    glOrtho(Vp[0], Vp[0]+Vp[2], Vp[1]+Vp[3], Vp[1], -1, 1);
    glGetIntegerv(GL_VIEWPORT, self->m_ViewportInit);
    glGetFloatv(GL_PROJECTION_MATRIX, self->m_ProjMatrixInit);
    }

    glGetFloatv(GL_LINE_WIDTH, &self->m_PrevLineWidth);
    glDisable(GL_POLYGON_STIPPLE);
    glLineWidth(1);
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_LINE_STIPPLE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &self->m_PrevTexEnv);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glGetIntegerv(GL_POLYGON_MODE, self->m_PrevPolygonMode);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glDisable(GL_LOGIC_OP);
    glDisable(GL_SCISSOR_TEST);
    if( self->m_MaxClipPlanes<0 )
    {
        glGetIntegerv(GL_MAX_CLIP_PLANES, &self->m_MaxClipPlanes);
        if( self->m_MaxClipPlanes<0 || self->m_MaxClipPlanes>255 )
            self->m_MaxClipPlanes = 6;
    }
    {
        GLint i;
        for( i=0; i<self->m_MaxClipPlanes; ++i )
            glDisable(GL_CLIP_PLANE0+i);
    }
    self->m_PrevTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &self->m_PrevTexture);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_INDEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_EDGE_FLAG_ARRAY);

    self->m_PrevVertexArray = 0;
    // glBindVertexArray (core since GL 3.0) is not guaranteed present on a
    // legacy/compatibility 2.1-ish context (confirmed: macOS's Legacy GL
    // profile leaves it NULL) - matches the original C++ file's own
    // if(_glBindVertexArray!=NULL) guard around this exact call.
    if( glBindVertexArray )
    {
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, (GLint*)&self->m_PrevVertexArray);
        glBindVertexArray(0);
    }

    self->m_PrevArrayBuffer = self->m_PrevElementArrayBuffer = 0;
    // glBindBuffer (core since GL 1.5) is not guaranteed present on a very
    // old/software legacy context - matches the original C++ file's own
    // if(_glBindBufferARB!=NULL) guard around this exact call.
    if( glBindBuffer )
    {
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &self->m_PrevArrayBuffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &self->m_PrevElementArrayBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    // GL_VERTEX_PROGRAM_ARB/GL_FRAGMENT_PROGRAM_ARB (the old assembly-shader
    // extension, long superseded by core GLSL) are plain enum tokens -
    // glIsEnabled/glEnable/glDisable accept them unconditionally regardless
    // of whether that extension's own function entry points are loaded, so
    // no availability guard is needed here (the original guarded on
    // _glBindProgramARB being resolved, which only ever probed for the
    // extension's presence and was never itself called).
    self->m_PrevVertexProgramARB = glIsEnabled(GL_VERTEX_PROGRAM_ARB);
    self->m_PrevFragmentProgramARB = glIsEnabled(GL_FRAGMENT_PROGRAM_ARB);
    glDisable(GL_VERTEX_PROGRAM_ARB);
    glDisable(GL_FRAGMENT_PROGRAM_ARB);

    self->m_PrevProgramObject = 0;
    // glUseProgram (core since GL 2.0) is not guaranteed present on a
    // pre-GLSL legacy context - matches the original C++ file's own
    // if(_glGetHandleARB!=NULL && _glUseProgramObjectARB!=NULL) guard
    // around its ARB-extension precursor.
    if( glUseProgram )
    {
        glGetIntegerv(GL_CURRENT_PROGRAM, &self->m_PrevProgramObject);
        glUseProgram(0);
    }

    glDisable(GL_TEXTURE_1D);
    glDisable(GL_TEXTURE_2D);
    self->m_PrevTexture3D = 0;
    // glTexImage3D (core since GL 1.2) is not guaranteed present on a very
    // old/software legacy context - matches the original C++ file's own
    // if(_glTexImage3D!=NULL) guard around this exact block.
    if( glTexImage3D )
    {
        self->m_PrevTexture3D = glIsEnabled(GL_TEXTURE_3D);
        glDisable(GL_TEXTURE_3D);
    }

    if( self->m_SupportTexRect )
    {
        self->m_PrevTexRectARB = glIsEnabled(GL_TEXTURE_RECTANGLE_ARB);
        glDisable(GL_TEXTURE_RECTANGLE_ARB);
    }

    // glBlendEquationSeparate/glBlendFuncSeparate/glBlendEquation (core
    // since GL 2.0/1.4/1.4 respectively) and glDisableVertexAttribArray
    // (core since GL 2.0) are not guaranteed present on a very old/software
    // legacy context - each guard below matches the original C++ file's
    // own if(_glXxx!=NULL) guard around that exact call.
    if( glBlendEquationSeparate )
    {
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &self->m_PrevBlendEquationRGB);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &self->m_PrevBlendEquationAlpha);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    }

    if( glBlendFuncSeparate )
    {
        glGetIntegerv(GL_BLEND_SRC_RGB, &self->m_PrevBlendSrcRGB);
        glGetIntegerv(GL_BLEND_DST_RGB, &self->m_PrevBlendDstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &self->m_PrevBlendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &self->m_PrevBlendDstAlpha);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    if( glBlendEquation )
    {
        glGetIntegerv(GL_BLEND_EQUATION, &self->m_PrevBlendEquation);
        glBlendEquation(GL_FUNC_ADD);
    }

    if( glDisableVertexAttribArray )
    {
        GLint maxVertexAttribs;
        int i;
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxVertexAttribs);
        if(maxVertexAttribs>TW_GRAPH_OPENGL_MAX_VERTEX_ATTRIBS)
            maxVertexAttribs=TW_GRAPH_OPENGL_MAX_VERTEX_ATTRIBS;

        for(i=0; i<maxVertexAttribs; i++)
        {
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &self->m_PrevEnabledVertexAttrib[i]);
            glDisableVertexAttribArray(i);
        }
    }

    CHECK_GL_ERROR;
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGL_EndDraw(ITwGraph *_This)
{
    TwGraphOpenGL *self = (TwGraphOpenGL *)_This;
    assert(self->m_Drawing==true);
    self->m_Drawing = false;

    glBindTexture(GL_TEXTURE_2D, self->m_PrevTexture);
    if( glBindVertexArray )
        glBindVertexArray(self->m_PrevVertexArray);
    if( glBindBuffer )
    {
        glBindBuffer(GL_ARRAY_BUFFER, self->m_PrevArrayBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self->m_PrevElementArrayBuffer);
    }
    if( self->m_PrevVertexProgramARB )
        glEnable(GL_VERTEX_PROGRAM_ARB);
    if( self->m_PrevFragmentProgramARB )
        glEnable(GL_FRAGMENT_PROGRAM_ARB);
    if( glUseProgram )
        glUseProgram((GLuint)self->m_PrevProgramObject);
    if( glTexImage3D && self->m_PrevTexture3D )
        glEnable(GL_TEXTURE_3D);
    if( self->m_SupportTexRect && self->m_PrevTexRectARB )
        glEnable(GL_TEXTURE_RECTANGLE_ARB);
    if( glBlendEquation )
        glBlendEquation(self->m_PrevBlendEquation);
    if( glBlendEquationSeparate )
        glBlendEquationSeparate(self->m_PrevBlendEquationRGB, self->m_PrevBlendEquationAlpha);
    if( glBlendFuncSeparate )
        glBlendFuncSeparate(self->m_PrevBlendSrcRGB, self->m_PrevBlendDstRGB, self->m_PrevBlendSrcAlpha, self->m_PrevBlendDstAlpha);

    glPolygonMode(GL_FRONT, (GLenum)self->m_PrevPolygonMode[0]);
    glPolygonMode(GL_BACK, (GLenum)self->m_PrevPolygonMode[1]);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, self->m_PrevTexEnv);
    glLineWidth(self->m_PrevLineWidth);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();

    {
        GLint maxTexUnits = 1;
        GLint i;
        glGetIntegerv(GL_MAX_TEXTURE_COORDS, &maxTexUnits);
        if( maxTexUnits<1 )
            maxTexUnits = 1;
        else if( maxTexUnits > TW_GRAPH_OPENGL_MAX_TEXTURES )
            maxTexUnits = TW_GRAPH_OPENGL_MAX_TEXTURES;
        for( i=0; i<maxTexUnits; ++i )
        {
            glActiveTexture(GL_TEXTURE0+i);
            if( self->m_PrevActiveTexture1D[i] )
                glEnable(GL_TEXTURE_1D);
            if( self->m_PrevActiveTexture2D[i] )
                glEnable(GL_TEXTURE_2D);
            if( self->m_PrevActiveTexture3D[i] )
                glEnable(GL_TEXTURE_3D);
        }
        glActiveTexture((GLenum)self->m_PrevActiveTexture);

        for( i=0; i<maxTexUnits; ++i )
        {
            glClientActiveTexture(GL_TEXTURE0+i);
            if( self->m_PrevClientTexCoordArray[i] )
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        glClientActiveTexture((GLenum)self->m_PrevClientActiveTexture);
    }
    if( glEnableVertexAttribArray )
    {
        GLint maxVertexAttribs;
        int i;
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxVertexAttribs);
        if(maxVertexAttribs>TW_GRAPH_OPENGL_MAX_VERTEX_ATTRIBS)
            maxVertexAttribs=TW_GRAPH_OPENGL_MAX_VERTEX_ATTRIBS;

        for(i=0; i<maxVertexAttribs; i++)
        {
            if(self->m_PrevEnabledVertexAttrib[i]!=0)
                glEnableVertexAttribArray(i);
        }
    }

    CHECK_GL_ERROR;
}

//  ---------------------------------------------------------------------------

static bool TwGraphOpenGL_IsDrawing(ITwGraph *_This)
{
    TwGraphOpenGL *self = (TwGraphOpenGL *)_This;
    return self->m_Drawing;
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGL_Restore(ITwGraph *_This)
{
    TwGraphOpenGL *self = (TwGraphOpenGL *)_This;
    UnbindFont(self->m_FontTexID);
    self->m_FontTexID = 0;
    self->m_FontTex = NULL;
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGL_DrawLine(ITwGraph *_This, int _X0, int _Y0, int _X1, int _Y1, color32 _Color0, color32 _Color1, bool _AntiAliased)
{
    TwGraphOpenGL *self = (TwGraphOpenGL *)_This;
    const GLfloat dx = +0.5f;
    const GLfloat dy = -0.5f;
    assert(self->m_Drawing==true);
    if( _AntiAliased )
        glEnable(GL_LINE_SMOOTH);
    else
        glDisable(GL_LINE_SMOOTH);
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glBegin(GL_LINES);
        glColor4ub((GLubyte)(_Color0>>16), (GLubyte)(_Color0>>8), (GLubyte)_Color0, (GLubyte)(_Color0>>24));
        glVertex2f((GLfloat)_X0+dx, (GLfloat)_Y0+dy);
        glColor4ub((GLubyte)(_Color1>>16), (GLubyte)(_Color1>>8), (GLubyte)_Color1, (GLubyte)(_Color1>>24));
        glVertex2f((GLfloat)_X1+dx, (GLfloat)_Y1+dy);
    glEnd();
    glDisable(GL_LINE_SMOOTH);
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGL_DrawRect(ITwGraph *_This, int _X0, int _Y0, int _X1, int _Y1, color32 _Color00, color32 _Color10, color32 _Color01, color32 _Color11)
{
    TwGraphOpenGL *self = (TwGraphOpenGL *)_This;
    const GLfloat dx = +0.0f;
    const GLfloat dy = +0.0f;
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

    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glBegin(GL_QUADS);
        glColor4ub((GLubyte)(_Color00>>16), (GLubyte)(_Color00>>8), (GLubyte)_Color00, (GLubyte)(_Color00>>24));
        glVertex2f((GLfloat)_X0+dx, (GLfloat)_Y0+dy);
        glColor4ub((GLubyte)(_Color10>>16), (GLubyte)(_Color10>>8), (GLubyte)_Color10, (GLubyte)(_Color10>>24));
        glVertex2f((GLfloat)_X1+dx, (GLfloat)_Y0+dy);
        glColor4ub((GLubyte)(_Color11>>16), (GLubyte)(_Color11>>8), (GLubyte)_Color11, (GLubyte)(_Color11>>24));
        glVertex2f((GLfloat)_X1+dx, (GLfloat)_Y1+dy);
        glColor4ub((GLubyte)(_Color01>>16), (GLubyte)(_Color01>>8), (GLubyte)_Color01, (GLubyte)(_Color01>>24));
        glVertex2f((GLfloat)_X0+dx, (GLfloat)_Y1+dy);
    glEnd();
}

//  ---------------------------------------------------------------------------

static void *TwGraphOpenGL_NewTextObj(ITwGraph *_This)
{
    (void)_This;
    return (CTextObj *)calloc(1, sizeof(CTextObj));
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGL_DeleteTextObj(ITwGraph *_This, void *_TextObj)
{
    CTextObj *TextObj = (CTextObj *)_TextObj;
    (void)_This;
    assert(_TextObj!=NULL);
    Vec2Array_Free(&TextObj->m_TextVerts);
    Vec2Array_Free(&TextObj->m_TextUVs);
    Vec2Array_Free(&TextObj->m_BgVerts);
    Color32Array_Free(&TextObj->m_Colors);
    Color32Array_Free(&TextObj->m_BgColors);
    free(TextObj);
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGL_BuildText(ITwGraph *_This, void *_TextObj, const char * const *_TextLines, color32 *_LineColors, color32 *_LineBgColors, int _NbLines, const CTexFont *_Font, int _Sep, int _BgWidth)
{
    TwGraphOpenGL *self = (TwGraphOpenGL *)_This;
    CTextObj *TextObj = (CTextObj *)_TextObj;
    int x, x1, y, y1, i, Len, Line;
    unsigned char ch;
    const unsigned char *Text;
    color32 LineColor = COLOR32_RED;

    assert(self->m_Drawing==true);
    assert(_TextObj!=NULL);
    assert(_Font!=NULL);

    if( _Font != self->m_FontTex )
    {
        UnbindFont(self->m_FontTexID);
        self->m_FontTexID = BindFont(_Font);
        self->m_FontTex = _Font;
    }
    Vec2Array_Clear(&TextObj->m_TextVerts);
    Vec2Array_Clear(&TextObj->m_TextUVs);
    Vec2Array_Clear(&TextObj->m_BgVerts);
    Color32Array_Clear(&TextObj->m_Colors);
    Color32Array_Clear(&TextObj->m_BgColors);

    for( Line=0; Line<_NbLines; ++Line )
    {
        x = 0;
        y = Line * (_Font->m_CharHeight+_Sep);
        y1 = y+_Font->m_CharHeight;
        Len = (int)strlen(_TextLines[Line]);
        Text = (const unsigned char *)(_TextLines[Line]);
        if( _LineColors!=NULL )
            LineColor = (_LineColors[Line]&0xff00ff00) | (GLubyte)(_LineColors[Line]>>16) | ((GLubyte)(_LineColors[Line])<<16);

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
            Vec2Array_Push(&TextObj->m_BgVerts, Vec2Make(-1          , (GLfloat)y ));
            Vec2Array_Push(&TextObj->m_BgVerts, Vec2Make((GLfloat)(_BgWidth+1), (GLfloat)y ));
            Vec2Array_Push(&TextObj->m_BgVerts, Vec2Make(-1          , (GLfloat)y1));
            Vec2Array_Push(&TextObj->m_BgVerts, Vec2Make((GLfloat)(_BgWidth+1), (GLfloat)y ));
            Vec2Array_Push(&TextObj->m_BgVerts, Vec2Make((GLfloat)(_BgWidth+1), (GLfloat)y1));
            Vec2Array_Push(&TextObj->m_BgVerts, Vec2Make(-1          , (GLfloat)y1));

            if( _LineBgColors!=NULL )
            {
                color32 LineBgColor = (_LineBgColors[Line]&0xff00ff00) | (GLubyte)(_LineBgColors[Line]>>16) | ((GLubyte)(_LineBgColors[Line])<<16);
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

static void TwGraphOpenGL_DrawText(ITwGraph *_This, void *_TextObj, int _X, int _Y, color32 _Color, color32 _BgColor)
{
    TwGraphOpenGL *self = (TwGraphOpenGL *)_This;
    CTextObj *TextObj = (CTextObj *)_TextObj;
    assert(self->m_Drawing==true);
    assert(_TextObj!=NULL);

    if( TextObj->m_TextVerts.count<4 && TextObj->m_BgVerts.count<4 )
        return; // nothing to draw

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef((GLfloat)_X, (GLfloat)_Y, 0);
    glEnableClientState(GL_VERTEX_ARRAY);
    if( (_BgColor!=0 || TextObj->m_BgColors.count==TextObj->m_BgVerts.count) && TextObj->m_BgVerts.count>=4 )
    {
        glDisable(GL_TEXTURE_2D);
        glVertexPointer(2, GL_FLOAT, 0, TextObj->m_BgVerts.items);
        if( TextObj->m_BgColors.count==TextObj->m_BgVerts.count && _BgColor==0 )
        {
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, TextObj->m_BgColors.items);
        }
        else
        {
            glDisableClientState(GL_COLOR_ARRAY);
            glColor4ub((GLubyte)(_BgColor>>16), (GLubyte)(_BgColor>>8), (GLubyte)_BgColor, (GLubyte)(_BgColor>>24));
        }
        glDrawArrays(GL_TRIANGLES, 0, (int)TextObj->m_BgVerts.count);
    }
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, self->m_FontTexID);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    if( TextObj->m_TextVerts.count>=4 )
    {
        glVertexPointer(2, GL_FLOAT, 0, TextObj->m_TextVerts.items);
        glTexCoordPointer(2, GL_FLOAT, 0, TextObj->m_TextUVs.items);
        if( TextObj->m_Colors.count==TextObj->m_TextVerts.count && _Color==0 )
        {
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, TextObj->m_Colors.items);
        }
        else
        {
            glDisableClientState(GL_COLOR_ARRAY);
            glColor4ub((GLubyte)(_Color>>16), (GLubyte)(_Color>>8), (GLubyte)_Color, (GLubyte)(_Color>>24));
        }

        glDrawArrays(GL_TRIANGLES, 0, (int)TextObj->m_TextVerts.count);
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGL_ChangeViewport(ITwGraph *_This, int _X0, int _Y0, int _Width, int _Height, int _OffsetX, int _OffsetY)
{
    TwGraphOpenGL *self = (TwGraphOpenGL *)_This;
    if( _Width>0 && _Height>0 )
    {
        GLint vp[4];
        GLint matrixMode = 0;
        vp[0] = _X0;
        vp[1] = _Y0;
        vp[2] = _Width-1;
        vp[3] = _Height-1;
        glViewport(vp[0], self->m_WndHeight-vp[1]-vp[3], vp[2], vp[3]);

        glGetIntegerv(GL_MATRIX_MODE, &matrixMode);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(_OffsetX, _OffsetX+vp[2], vp[3]-_OffsetY, -_OffsetY, -1, 1);
        glMatrixMode((GLenum)matrixMode);
    }
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGL_RestoreViewport(ITwGraph *_This)
{
    TwGraphOpenGL *self = (TwGraphOpenGL *)_This;
    GLint matrixMode = 0;
    glViewport(self->m_ViewportInit[0], self->m_ViewportInit[1], self->m_ViewportInit[2], self->m_ViewportInit[3]);

    glGetIntegerv(GL_MATRIX_MODE, &matrixMode);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(self->m_ProjMatrixInit);
    glMatrixMode((GLenum)matrixMode);
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGL_SetScissor(ITwGraph *_This, int _X0, int _Y0, int _Width, int _Height)
{
    TwGraphOpenGL *self = (TwGraphOpenGL *)_This;
    if( _Width>0 && _Height>0 )
    {
        glScissor(_X0-1, self->m_WndHeight-_Y0-_Height, _Width-1, _Height);
        glEnable(GL_SCISSOR_TEST);
    }
    else
        glDisable(GL_SCISSOR_TEST);
}

//  ---------------------------------------------------------------------------

static void TwGraphOpenGL_DrawTriangles(ITwGraph *_This, int _NumTriangles, int *_Vertices, color32 *_Colors, enum TwGraphCull _CullMode)
{
    const GLfloat dx = +0.0f;
    const GLfloat dy = +0.0f;
    TwGraphOpenGL *self = (TwGraphOpenGL *)_This;
    GLint prevCullFaceMode, prevFrontFace;
    GLboolean prevCullEnable;
    int i;
    assert(self->m_Drawing==true);

    glGetIntegerv(GL_CULL_FACE_MODE, &prevCullFaceMode);
    glGetIntegerv(GL_FRONT_FACE, &prevFrontFace);
    prevCullEnable = glIsEnabled(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_CULL_FACE);
    if( _CullMode==TW_GRAPH_CULL_CW )
        glFrontFace(GL_CCW);
    else if( _CullMode==TW_GRAPH_CULL_CCW )
        glFrontFace(GL_CW);
    else
        glDisable(GL_CULL_FACE);

    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glBegin(GL_TRIANGLES);
    for(i=0; i<3*_NumTriangles; ++i)
    {
        color32 col = _Colors[i];
        glColor4ub((GLubyte)(col>>16), (GLubyte)(col>>8), (GLubyte)col, (GLubyte)(col>>24));
        glVertex2f((GLfloat)_Vertices[2*i+0]+dx, (GLfloat)_Vertices[2*i+1]+dy);
    }
    glEnd();

    glCullFace((GLenum)prevCullFaceMode);
    glFrontFace((GLenum)prevFrontFace);
    if( prevCullEnable )
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
}

//  ---------------------------------------------------------------------------

ITwGraph *TwGraphOpenGL_Create(void)
{
    TwGraphOpenGL *self = (TwGraphOpenGL *)calloc(1, sizeof(TwGraphOpenGL));
    if( !self )
        return NULL;

    self->base.Init            = TwGraphOpenGL_Init;
    self->base.Shut            = TwGraphOpenGL_Shut;
    self->base.BeginDraw       = TwGraphOpenGL_BeginDraw;
    self->base.EndDraw         = TwGraphOpenGL_EndDraw;
    self->base.IsDrawing       = TwGraphOpenGL_IsDrawing;
    self->base.Restore         = TwGraphOpenGL_Restore;
    self->base.DrawLine        = TwGraphOpenGL_DrawLine;
    self->base.DrawRect        = TwGraphOpenGL_DrawRect;
    self->base.DrawTriangles   = TwGraphOpenGL_DrawTriangles;
    self->base.NewTextObj      = TwGraphOpenGL_NewTextObj;
    self->base.DeleteTextObj   = TwGraphOpenGL_DeleteTextObj;
    self->base.BuildText       = TwGraphOpenGL_BuildText;
    self->base.DrawText        = TwGraphOpenGL_DrawText;
    self->base.ChangeViewport  = TwGraphOpenGL_ChangeViewport;
    self->base.RestoreViewport = TwGraphOpenGL_RestoreViewport;
    self->base.SetScissor      = TwGraphOpenGL_SetScissor;

    return &self->base;
}
