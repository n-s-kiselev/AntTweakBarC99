//  ---------------------------------------------------------------------------
//
//  @file       TwOpenGL.h
//  @brief      OpenGL graph functions
//  @author     Philippe Decaudin
//  @license    This file is part of the AntTweakBar library.
//              For conditions of distribution and use, see License.txt
//
//  note:       Private header
//
//  ---------------------------------------------------------------------------


#if !defined ANT_TW_OPENGL_INCLUDED
#define ANT_TW_OPENGL_INCLUDED

#include "TwGraph.h"

//  ---------------------------------------------------------------------------

// Was CTwGraphOpenGL : public ITwGraph (C++ virtual class) - now a plain C
// struct whose first member is the ITwGraph vtable itself (C struct-based
// polymorphism, see TwGraph.h's own comment on this pattern). Every field
// below was a `protected:` member of the original class.
typedef struct TwGraphOpenGL
{
    ITwGraph            base;

    bool                m_Drawing;
    GLuint              m_FontTexID;
    const CTexFont *    m_FontTex;
    GLfloat             m_PrevLineWidth;
    GLint               m_PrevTexEnv;
    GLint               m_PrevPolygonMode[2];
    GLint               m_MaxClipPlanes;
    GLint               m_PrevTexture;
    GLint               m_PrevArrayBuffer;
    GLint               m_PrevElementArrayBuffer;
    GLboolean           m_PrevVertexProgramARB;
    GLboolean           m_PrevFragmentProgramARB;
    GLint               m_PrevProgramObject;
    GLboolean           m_PrevTexture3D;
    GLboolean           m_PrevActiveTexture1D[128];
    GLboolean           m_PrevActiveTexture2D[128];
    GLboolean           m_PrevActiveTexture3D[128];
    GLboolean           m_PrevClientTexCoordArray[128];
    GLint               m_PrevActiveTexture;
    GLint               m_PrevClientActiveTexture;
    bool                m_SupportTexRect;
    GLboolean           m_PrevTexRectARB;
    GLint               m_PrevBlendEquation;
    GLint               m_PrevBlendEquationRGB;
    GLint               m_PrevBlendEquationAlpha;
    GLint               m_PrevBlendSrcRGB;
    GLint               m_PrevBlendDstRGB;
    GLint               m_PrevBlendSrcAlpha;
    GLint               m_PrevBlendDstAlpha;
    GLuint              m_PrevVertexArray;
    GLint               m_ViewportInit[4];
    GLfloat             m_ProjMatrixInit[16];
    GLint               m_PrevEnabledVertexAttrib[128];
    int                 m_WndWidth;
    int                 m_WndHeight;
} TwGraphOpenGL;

#define TW_GRAPH_OPENGL_MAX_TEXTURES        128
#define TW_GRAPH_OPENGL_MAX_VERTEX_ATTRIBS  128

// Allocates and fills in a TwGraphOpenGL, returning it as an ITwGraph* (the
// address is the same, since `base` is TwGraphOpenGL's first member) -
// replaces the original `new CTwGraphOpenGL`. Call TwGraph_Destroy() (see
// TwGraph.h) to free it, after calling its Shut() first.
ITwGraph *TwGraphOpenGL_Create(void);

//  ---------------------------------------------------------------------------


#endif // !defined ANT_TW_OPENGL_INCLUDED
