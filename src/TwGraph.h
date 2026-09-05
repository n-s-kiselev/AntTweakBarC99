//  ---------------------------------------------------------------------------
//
//  @file       TwGraph.h
//  @brief      ITwGraph: the renderer interface, as a plain C99
//              function-pointer vtable (was a C++ abstract base class -
//              see docs/plans/c99-rewrite.md Cluster 2 for the conversion
//              record).
//  @author     Philippe Decaudin
//  @license    This file is part of the AntTweakBar library.
//              For conditions of distribution and use, see License.txt
//
//  note:       Private header
//
//  ---------------------------------------------------------------------------


#if !defined ANT_TW_GRAPH_INCLUDED
#define ANT_TW_GRAPH_INCLUDED

#include "TwColors.h"
#include "TwFonts.h"
#include <stdbool.h>
#include <stdlib.h>


//  ---------------------------------------------------------------------------

#ifdef DrawText     // DirectX redefines 'DrawText' !!
#   undef DrawText
#endif  // DrawText

enum TwGraphCull { TW_GRAPH_CULL_NONE, TW_GRAPH_CULL_CW, TW_GRAPH_CULL_CCW };

// A concrete renderer (TwOpenGL.c/TwOpenGLCore.c) allocates one combined
// struct whose first member is an ITwGraph (the classic C struct-based
// polymorphism idiom: the embedded ITwGraph's address equals the combined
// struct's address, so each vtable function safely casts its ITwGraph*
// argument back to the concrete struct pointer to reach its private
// fields - the same memory layout the original C++ vtable/subclass
// relationship already had). ITwGraph itself holds only function
// pointers; there is no separate opaque `Self` field.
typedef struct ITwGraph ITwGraph;
struct ITwGraph
{
    int         (*Init)(ITwGraph *_This);
    int         (*Shut)(ITwGraph *_This);
    void        (*BeginDraw)(ITwGraph *_This, int _WndWidth, int _WndHeight);
    void        (*EndDraw)(ITwGraph *_This);
    bool        (*IsDrawing)(ITwGraph *_This);
    void        (*Restore)(ITwGraph *_This);

    // Only the general (2-endpoint-color) form is kept: every external
    // caller already always passed the same color for both endpoints
    // (confirmed by inspection of every call site during the C99 port -
    // the single-color C++ overload never actually needed a distinct
    // vtable entry), so callers now just pass _Color0==_Color1.
    void        (*DrawLine)(ITwGraph *_This, int _X0, int _Y0, int _X1, int _Y1, color32 _Color0, color32 _Color1, bool _AntiAliased);
    // Likewise, only the general (4-corner-color) form is kept; callers
    // wanting one flat color pass it four times.
    void        (*DrawRect)(ITwGraph *_This, int _X0, int _Y0, int _X1, int _Y1, color32 _Color00, color32 _Color10, color32 _Color01, color32 _Color11);
    void        (*DrawTriangles)(ITwGraph *_This, int _NumTriangles, int *_Vertices, color32 *_Colors, enum TwGraphCull _CullMode);

    void *      (*NewTextObj)(ITwGraph *_This);
    void        (*DeleteTextObj)(ITwGraph *_This, void *_TextObj);
    // _TextLines was `const std::string *` in the original C++ interface
    // (an array of _NbLines std::string objects) - replaced with an array
    // of plain C strings, the only shape expressible in C99. Every caller
    // (still C++, in TwBar.cpp) builds a temporary array of `.c_str()`
    // pointers before calling this.
    void        (*BuildText)(ITwGraph *_This, void *_TextObj, const char * const *_TextLines, color32 *_LineColors, color32 *_LineBgColors, int _NbLines, const CTexFont *_Font, int _Sep, int _BgWidth);
    void        (*DrawText)(ITwGraph *_This, void *_TextObj, int _X, int _Y, color32 _Color, color32 _BgColor);

    void        (*ChangeViewport)(ITwGraph *_This, int _X0, int _Y0, int _Width, int _Height, int _OffsetX, int _OffsetY);
    void        (*RestoreViewport)(ITwGraph *_This);
    void        (*SetScissor)(ITwGraph *_This, int _X0, int _Y0, int _Width, int _Height);
};

// Frees the whole combined struct behind _Graph (i.e. plain free()) -
// call _Graph->Shut(_Graph) first to release the renderer's own GL/D3D
// resources; this only releases the struct's own memory, mirroring what
// `delete` used to do for the C++ base-class pointer.
static inline void TwGraph_Destroy(ITwGraph *_Graph)
{
    free(_Graph);
}

//  ---------------------------------------------------------------------------

#endif  // ANT_TW_GRAPH_INCLUDED
