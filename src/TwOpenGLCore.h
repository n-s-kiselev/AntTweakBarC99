//  ---------------------------------------------------------------------------
//
//  @file       TwOpenGLCore.h
//  @brief      OpenGL Core graph functions
//  @author     Philippe Decaudin
//  @license    This file is part of the AntTweakBar library.
//              For conditions of distribution and use, see License.txt
//
//  note:       Private header
//
//  ---------------------------------------------------------------------------


#if !defined ANT_TW_OPENGL_CORE_INCLUDED
#define ANT_TW_OPENGL_CORE_INCLUDED

#include "TwGraph.h"

//  ---------------------------------------------------------------------------

// Allocates and returns an ITwGraph implemented by the OpenGL Core Profile
// renderer (was `class CTwGraphOpenGLCore : public ITwGraph` - see
// TwGraph.h's own comment for the C struct-based polymorphism pattern this
// now uses). All other members of the original class were `protected` and
// are genuinely private to TwOpenGLCore.c now - nothing else in the
// library touches them, so they are not declared here at all.
ITwGraph *TwGraphOpenGLCore_Create(void);

//  ---------------------------------------------------------------------------


#endif // !defined ANT_TW_OPENGL_CORE_INCLUDED
