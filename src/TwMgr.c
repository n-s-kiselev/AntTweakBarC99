//  ---------------------------------------------------------------------------
//
//  @file       TwMgr.cpp
//  @author     Philippe Decaudin
//  @license    This file is part of the AntTweakBar library.
//              For conditions of distribution and use, see License.txt
//
//  ---------------------------------------------------------------------------


#include "TwPrecomp.h"
#include <AntTweakBar.h>
#include "TwMgr.h"
#include "TwBar.h"
#include "TwFonts.h"
#include "TwOpenGL.h"
#include "TwOpenGLCore.h"
// glfwGetTime() replaces the deleted PerfTimer/AntPerfTimer.h m_Timer field
// (see TwMgr.h) - this project already hard-depends on GLFW3 elsewhere
// (TwBar.cpp includes it directly for clipboard access).
#include <GLFW/glfw3.h>
#ifdef ANT_WINDOWS
#   include "TwDirect3D9.h"
#   include "TwDirect3D10.h"
#   include "TwDirect3D11.h"
#   include "resource.h"
#   ifdef _DEBUG
#       include <crtdbg.h>
#   endif // _DEBUG
#endif // ANT_WINDOWS

#if !defined(ANT_WINDOWS)
#   define _snprintf snprintf
#endif  // defined(ANT_WINDOWS)

// g_CurPict/g_CurMask/g_CurHot (cursor pixmap data): needed to build the
// RGBA bitmaps passed to an installed TwCursorCB for TW_CURSOR_CUSTOM - see
// BuildCustomCursorRGBA() near CTwMgr_SetCursor().
#include "res/TwXCursors.h"

CTwMgr *g_TwMgr = NULL; // current TwMgr
bool g_BreakOnError = false;
TwErrorHandler g_ErrorHandler = NULL;

// Was CStruct::s_PassProxyAsClientData (a static class member used
// purely as an identity-comparison sentinel tag, never dereferenced);
// moved up here (from its original spot much later in this file) since
// CQuaternionExt_CreateTypes/CColorExt-related code near the top of the
// file already references it via TwDefineStructExt.
static int s_PassProxy = 0;
void *g_PassProxyAsClientData = &s_PassProxy;  // special tag
int g_TabLength = 4;
CTwBar * const TW_GLOBAL_BAR = (CTwBar *)(-1);
int g_InitWndWidth = -1;
int g_InitWndHeight = -1;
TwCopyCDStringToClient  g_InitCopyCDStringToClient = NULL;
float g_FontScaling = 1.0f;
TwCursorCB g_CursorCallback = NULL;
void *     g_CursorCallbackClientData = NULL;

// multi-windows
static const int TW_MASTER_WINDOW_ID = 0;
// Replaces std::map<int, CTwMgr*>: a handful of windows at most, so a
// linear-scan array (same idiom as CustomMap/CustomMap_Find in
// TwBar.cpp) needs no ordering/hashing.
typedef struct { int WndID; CTwMgr *Mgr; } CWndEntry;
typedef struct { CWndEntry *items; size_t count; size_t capacity; } CTwWndArray;
static CTwWndArray g_Wnds = {0};
static CTwMgr *CTwWndArray_Find(int _WndID) // NULL if absent
{
    for( size_t i=0; i<g_Wnds.count; ++i )
        if( g_Wnds.items[i].WndID==_WndID )
            return g_Wnds.items[i].Mgr;
    return NULL;
}
static void CTwWndArray_Set(int _WndID, CTwMgr *_Mgr) // insert or overwrite
{
    for( size_t i=0; i<g_Wnds.count; ++i )
        if( g_Wnds.items[i].WndID==_WndID )
        {
            g_Wnds.items[i].Mgr = _Mgr;
            return;
        }
    CWndEntry Entry; Entry.WndID = _WndID; Entry.Mgr = _Mgr;
    tw_da_append(&g_Wnds, Entry);
}
CTwMgr *g_TwMasterMgr = NULL;

// CEnum helpers - m_Entries is kept sorted ascending by Value at
// all times (see the comment above CEnum in TwMgr.h): TwBar.cpp's
// enum popup-list UI iterates this array in order to build its buttons, so
// losing the sort would silently reorder what the user sees.
void CEnum_Clear(CEnum *_Enum)
{
    for( size_t i=0; i<_Enum->m_Entries.count; ++i )
        sdsfree(_Enum->m_Entries.items[i].Label);
    _Enum->m_Entries.count = 0;
}
sds CEnum_Find(const CEnum *_Enum, unsigned int _Value)
{
    for( size_t i=0; i<_Enum->m_Entries.count; ++i )
        if( _Enum->m_Entries.items[i].Value==_Value )
            return _Enum->m_Entries.items[i].Label;
    return NULL;
}
void CEnum_InsertOrReplaceLen(CEnum *_Enum, unsigned int _Value, const char *_Label, size_t _LabelLen)
{
    size_t i;
    for( i=0; i<_Enum->m_Entries.count; ++i )
        if( _Enum->m_Entries.items[i].Value==_Value )
        {
            _Enum->m_Entries.items[i].Label = sdscpylen(_Enum->m_Entries.items[i].Label, _Label, _LabelLen);
            return;
        }
        else if( _Enum->m_Entries.items[i].Value>_Value )
            break;
    // not found: insert a new entry at position i, keeping ascending order
    tw_da_reserve(&_Enum->m_Entries, _Enum->m_Entries.count+1);
    memmove(_Enum->m_Entries.items+i+1, _Enum->m_Entries.items+i, (_Enum->m_Entries.count-i)*sizeof(*_Enum->m_Entries.items));
    _Enum->m_Entries.items[i].Value = _Value;
    _Enum->m_Entries.items[i].Label = sdsnewlen(_Label, _LabelLen);
    ++_Enum->m_Entries.count;
}
void CEnum_InsertOrReplace(CEnum *_Enum, unsigned int _Value, const char *_Label)
{
    CEnum_InsertOrReplaceLen(_Enum, _Value, _Label, strlen(_Label));
}

// Was CTwFPU's constructor/destructor (see TwMgr.h) - temporarily forces the
// FPU to at-least-53-bit precision on Windows (a no-op elsewhere), restored
// by the matching TwFPU_Restore call.
unsigned int TwFPU_Save(void)
{
    unsigned int state0;
#ifdef ANT_WINDOWS
    state0 = _controlfp(0, 0);
    if( (state0&MCW_PC)==_PC_24 )   // we need at least _PC_53
        _controlfp(_PC_53, MCW_PC);
#else
    state0 = 0;
#endif
    return state0;
}

void TwFPU_Restore(unsigned int state0)
{
#ifdef ANT_WINDOWS
    if( (state0&MCW_PC)==_PC_24 )
        _controlfp(_PC_24, MCW_PC);
#else
    (void)state0;
#endif
}

// error messages
extern const char *g_ErrUnknownAttrib;
extern const char *g_ErrNoValue;
extern const char *g_ErrBadValue;
static const char *g_ErrInit       = "Already initialized";
static const char *g_ErrShut       = "Already shutdown";
static const char *g_ErrNotInit    = "Not initialized";
static const char *g_ErrUnknownAPI = "Unsupported graph API";
static const char *g_ErrBadDevice  = "Invalid graph device";
static const char *g_ErrBadParam   = "Invalid parameter";
static const char *g_ErrExist      = "Exists already";
const char *g_ErrNotFound   = "Not found"; // shared with TwBar.c (see its own `extern const char *g_ErrNotFound;`)
static const char *g_ErrNthToDo    = "Nothing to do";
static const char *g_ErrBadSize    = "Bad size";
static const char *g_ErrIsDrawing  = "Asynchronous drawing detected";
static const char *g_ErrIsProcessing="Asynchronous processing detected";
static const char *g_ErrOffset     = "Offset larger than StructSize";
static const char *g_ErrDelStruct  = "Cannot delete a struct member";
static const char *g_ErrNoBackQuote= "Name cannot include back-quote";
static const char *g_ErrCStrParam  = "Value count for TW_PARAM_CSTRING must be 1";
static const char *g_ErrOutOfRange = "Index out of range";
static const char *g_ErrHasNoValue = "Has no value";
static const char *g_ErrBadType    = "Incompatible type";
static const char *g_ErrDelHelp    = "Cannot delete help bar";
char g_ErrParse[512];

void ANT_CALL TwGlobalError(const char *_ErrorMessage);

#if defined(ANT_UNIX) || defined(ANT_OSX)
#define _stricmp strcasecmp
#define _strdup strdup
#endif

//  ---------------------------------------------------------------------------

static const float  FLOAT_EPS     = 1.0e-7f;
static const float  FLOAT_EPS_SQ  = 1.0e-14f;
static const float  FLOAT_PI      = 3.14159265358979323846f;
static const double DOUBLE_EPS    = 1.0e-14;
static const double DOUBLE_EPS_SQ = 1.0e-28;
static const double DOUBLE_PI     = 3.14159265358979323846;

static inline double DegToRad(double degree) { return degree * (DOUBLE_PI/180.0); }
static inline double RadToDeg(double radian) { return radian * (180.0/DOUBLE_PI); }

//  ---------------------------------------------------------------------------

//  a static global object to verify that Tweakbar module has been properly terminated (in debug mode only)
#ifdef _DEBUG
static struct CTwVerif
{
    ~CTwVerif() 
    { 
        if( g_TwMgr!=NULL )
            CTwMgr_SetLastError(g_TwMgr, "Tweak bar module has not been terminated properly: call TwTerminate()\n");
    }
} s_Verif;
#endif // _DEBUG

//  ---------------------------------------------------------------------------
//  Color ext type
//  ---------------------------------------------------------------------------

void CColorExt_RGB2HLS(CColorExt *_Ext)
{
    float fH = 0, fL = 0, fS = 0;
    ColorRGBToHLSf((float)_Ext->R/255.0f, (float)_Ext->G/255.0f, (float)_Ext->B/255.0f, &fH, &fL, &fS);
    _Ext->H = (int)fH;
    if( _Ext->H>=360 )
        _Ext->H -= 360;
    else if( _Ext->H<0 )
        _Ext->H += 360;
    _Ext->L = (int)(255.0f*fL + 0.5f);
    if( _Ext->L<0 )
        _Ext->L = 0;
    else if( _Ext->L>255 )
        _Ext->L = 255;
    _Ext->S = (int)(255.0f*fS + 0.5f);
    if( _Ext->S<0 )
        _Ext->S = 0;
    else if( _Ext->S>255 )
        _Ext->S = 255;
}

void CColorExt_HLS2RGB(CColorExt *_Ext)
{
    float fR = 0, fG = 0, fB = 0;
    ColorHLSToRGBf((float)_Ext->H, (float)_Ext->L/255.0f, (float)_Ext->S/255.0f, &fR, &fG, &fB);
    _Ext->R = (int)(255.0f*fR + 0.5f);
    if( _Ext->R<0 )
        _Ext->R = 0;
    else if( _Ext->R>255 )
        _Ext->R = 255;
    _Ext->G = (int)(255.0f*fG + 0.5f);
    if( _Ext->G<0 )
        _Ext->G = 0;
    else if( _Ext->G>255 )
        _Ext->G = 255;
    _Ext->B = (int)(255.0f*fB + 0.5f);
    if( _Ext->B<0 )
        _Ext->B = 0;
    else if( _Ext->B>255 )
        _Ext->B = 255;
}

void ANT_CALL CColorExt_InitColor32CB(void *_ExtValue, void *_ClientData)
{
    CColorExt *ext = (CColorExt *)(_ExtValue);
    if( ext )
    {
        ext->m_IsColorF = false;
        ext->R = 0;
        ext->G = 0;
        ext->B = 0;
        ext->H = 0;
        ext->L = 0;
        ext->S = 0;
        ext->A = 255;
        ext->m_HLS = false;
        ext->m_HasAlpha = false;
        ext->m_CanHaveAlpha = true;
        if( g_TwMgr && g_TwMgr->m_GraphAPI==TW_DIRECT3D9 ) // D3D10 now use OGL rgba order!
            ext->m_OGL = false;
        else
            ext->m_OGL = true;
        ext->m_PrevConvertedColor = Color32FromARGBi(ext->A, ext->R, ext->G, ext->B);
        ext->m_StructProxy = (CStructProxy *)_ClientData;
    }
}

void ANT_CALL CColorExt_InitColor3FCB(void *_ExtValue, void *_ClientData)
{
    CColorExt_InitColor32CB(_ExtValue, _ClientData);
    CColorExt *ext = (CColorExt *)(_ExtValue);
    if( ext )
    {
        ext->m_IsColorF = true;
        ext->m_HasAlpha = false;
        ext->m_CanHaveAlpha = false;
    }
}

void ANT_CALL CColorExt_InitColor4FCB(void *_ExtValue, void *_ClientData)
{
    CColorExt_InitColor32CB(_ExtValue, _ClientData);
    CColorExt *ext = (CColorExt *)(_ExtValue);
    if( ext )
    {
        ext->m_IsColorF = true;
        ext->m_HasAlpha = true;
        ext->m_CanHaveAlpha = true;
    }
}

void ANT_CALL CColorExt_CopyVarFromExtCB(void *_VarValue, const void *_ExtValue, unsigned int _ExtMemberIndex, void *_ClientData)
{
    unsigned int *var32 = (unsigned int *)(_VarValue);
    float *varF = (float *)(_VarValue);
    CColorExt *ext = (CColorExt *)(_ExtValue);
    CMemberProxy *mProxy = (CMemberProxy *)(_ClientData);
    if( _VarValue && ext )
    {
        if( ext->m_HasAlpha && mProxy && mProxy->m_StructProxy && mProxy->m_StructProxy->m_Type==g_TwMgr->m_TypeColor3F )
            ext->m_HasAlpha = false;

        // Synchronize HLS and RGB
        if( _ExtMemberIndex>=0 && _ExtMemberIndex<=2 )
            CColorExt_RGB2HLS(ext);
        else if( _ExtMemberIndex>=3 && _ExtMemberIndex<=5 )
            CColorExt_HLS2RGB(ext);
        else if( mProxy && _ExtMemberIndex==7 && mProxy->m_VarParent )
        {
            assert( mProxy->m_VarParent->m_Vars.count==8 );
            if(    mProxy->m_VarParent->m_Vars.items[0]->m_Visible != !ext->m_HLS
                || mProxy->m_VarParent->m_Vars.items[1]->m_Visible != !ext->m_HLS
                || mProxy->m_VarParent->m_Vars.items[2]->m_Visible != !ext->m_HLS
                || mProxy->m_VarParent->m_Vars.items[3]->m_Visible != ext->m_HLS
                || mProxy->m_VarParent->m_Vars.items[4]->m_Visible != ext->m_HLS
                || mProxy->m_VarParent->m_Vars.items[5]->m_Visible != ext->m_HLS )
            {
                mProxy->m_VarParent->m_Vars.items[0]->m_Visible = !ext->m_HLS;
                mProxy->m_VarParent->m_Vars.items[1]->m_Visible = !ext->m_HLS;
                mProxy->m_VarParent->m_Vars.items[2]->m_Visible = !ext->m_HLS;
                mProxy->m_VarParent->m_Vars.items[3]->m_Visible = ext->m_HLS;
                mProxy->m_VarParent->m_Vars.items[4]->m_Visible = ext->m_HLS;
                mProxy->m_VarParent->m_Vars.items[5]->m_Visible = ext->m_HLS;
                CTwBar_NotUpToDate(mProxy->m_Bar);
            }
            if( mProxy->m_VarParent->m_Vars.items[6]->m_Visible != ext->m_HasAlpha )
            {
                mProxy->m_VarParent->m_Vars.items[6]->m_Visible = ext->m_HasAlpha;
                CTwBar_NotUpToDate(mProxy->m_Bar);
            }
            if( ((CTwVarAtom *)(mProxy->m_VarParent->m_Vars.items[7]))->m_ReadOnly )
            {
                ((CTwVarAtom *)(mProxy->m_VarParent->m_Vars.items[7]))->m_ReadOnly = false;
                CTwBar_NotUpToDate(mProxy->m_Bar);
            }
        }
        // Convert to color32
        color32 col = Color32FromARGBi((ext->m_HasAlpha ? ext->A : 255), ext->R, ext->G, ext->B);
        if( ext->m_OGL && !ext->m_IsColorF )
            col = (col&0xff00ff00) | (unsigned char)(col>>16) | (((unsigned char)(col))<<16);
        if( ext->m_IsColorF )
            Color32ToARGBf(col, (ext->m_HasAlpha ? varF+3 : NULL), varF+0, varF+1, varF+2);
        else
        {
            if( ext->m_HasAlpha )
                *var32 = col;
            else
                *var32 = ((*var32)&0xff000000) | (col&0x00ffffff);
        }
        ext->m_PrevConvertedColor = col;
    }
}

void ANT_CALL CColorExt_CopyVarToExtCB(const void *_VarValue, void *_ExtValue, unsigned int _ExtMemberIndex, void *_ClientData)
{
    const unsigned int *var32 = (const unsigned int *)(_VarValue);
    const float *varF = (const float *)(_VarValue);
    CColorExt *ext = (CColorExt *)(_ExtValue);
    CMemberProxy *mProxy = (CMemberProxy *)(_ClientData);
    if( _VarValue && ext )
    {
        if( ext->m_HasAlpha && mProxy && mProxy->m_StructProxy && mProxy->m_StructProxy->m_Type==g_TwMgr->m_TypeColor3F )
            ext->m_HasAlpha = false;

        if( mProxy && _ExtMemberIndex==7 && mProxy->m_VarParent )
        {
            assert( mProxy->m_VarParent->m_Vars.count==8 );
            if(    mProxy->m_VarParent->m_Vars.items[0]->m_Visible != !ext->m_HLS
                || mProxy->m_VarParent->m_Vars.items[1]->m_Visible != !ext->m_HLS
                || mProxy->m_VarParent->m_Vars.items[2]->m_Visible != !ext->m_HLS
                || mProxy->m_VarParent->m_Vars.items[3]->m_Visible != ext->m_HLS
                || mProxy->m_VarParent->m_Vars.items[4]->m_Visible != ext->m_HLS
                || mProxy->m_VarParent->m_Vars.items[5]->m_Visible != ext->m_HLS )
            {
                mProxy->m_VarParent->m_Vars.items[0]->m_Visible = !ext->m_HLS;
                mProxy->m_VarParent->m_Vars.items[1]->m_Visible = !ext->m_HLS;
                mProxy->m_VarParent->m_Vars.items[2]->m_Visible = !ext->m_HLS;
                mProxy->m_VarParent->m_Vars.items[3]->m_Visible = ext->m_HLS;
                mProxy->m_VarParent->m_Vars.items[4]->m_Visible = ext->m_HLS;
                mProxy->m_VarParent->m_Vars.items[5]->m_Visible = ext->m_HLS;
                CTwBar_NotUpToDate(mProxy->m_Bar);
            }
            if( mProxy->m_VarParent->m_Vars.items[6]->m_Visible != ext->m_HasAlpha )
            {
                mProxy->m_VarParent->m_Vars.items[6]->m_Visible = ext->m_HasAlpha;
                CTwBar_NotUpToDate(mProxy->m_Bar);
            }
            if( ((CTwVarAtom *)(mProxy->m_VarParent->m_Vars.items[7]))->m_ReadOnly )
            {
                ((CTwVarAtom *)(mProxy->m_VarParent->m_Vars.items[7]))->m_ReadOnly = false;
                CTwBar_NotUpToDate(mProxy->m_Bar);
            }
        }
        color32 col;
        if( ext->m_IsColorF )
            col = Color32FromARGBf((ext->m_HasAlpha ? varF[3] : 1), varF[0], varF[1], varF[2]);
        else
            col = *var32;
        if( ext->m_OGL && !ext->m_IsColorF )
            col = (col&0xff00ff00) | (unsigned char)(col>>16) | (((unsigned char)(col))<<16);
        Color32ToARGBi(col, (ext->m_HasAlpha ? &ext->A : NULL), &ext->R, &ext->G, &ext->B);
        if( (col & 0x00ffffff)!=(ext->m_PrevConvertedColor & 0x00ffffff) )
            CColorExt_RGB2HLS(ext);
        ext->m_PrevConvertedColor = col;
    }
}

void ANT_CALL CColorExt_SummaryCB(char *_SummaryString, size_t _SummaryMaxLength, const void *_ExtValue, void *_ClientData)
{
    (void)_SummaryMaxLength, (void)_ClientData;
    // copy var
    CColorExt *ext = (CColorExt *)(_ExtValue);
    if( ext && ext->m_StructProxy && ext->m_StructProxy->m_StructData )
    {
        if( ext->m_StructProxy->m_StructGetCallback )
            ext->m_StructProxy->m_StructGetCallback(ext->m_StructProxy->m_StructData, ext->m_StructProxy->m_StructClientData);
        //if( *(unsigned int *)(ext->m_StructProxy->m_StructData)!=ext->m_PrevConvertedColor )
        CColorExt_CopyVarToExtCB(ext->m_StructProxy->m_StructData, ext, 99, NULL);
    }

    //unsigned int col = 0;
    //CopyVar32FromExtCB(&col, _ExtValue, 99, _ClientData);
    //_snprintf(_SummaryString, _SummaryMaxLength, "0x%.8X", col);
    //(void) _SummaryMaxLength, _ExtValue, _ClientData;
    _SummaryString[0] = ' ';    // required to force background color for this value
    _SummaryString[1] = '\0';
}

void CColorExt_CreateTypes(void)
{
    if( g_TwMgr==NULL )
        return;
    TwStructMember ColorExtMembers[] = { { "Red", TW_TYPE_INT32, offsetof(CColorExt, R), "min=0 max=255" },
                                         { "Green", TW_TYPE_INT32, offsetof(CColorExt, G), "min=0 max=255" },
                                         { "Blue", TW_TYPE_INT32, offsetof(CColorExt, B), "min=0 max=255" },
                                         { "Hue", TW_TYPE_INT32, offsetof(CColorExt, H), "hide min=0 max=359" },
                                         { "Lightness", TW_TYPE_INT32, offsetof(CColorExt, L), "hide min=0 max=255" },
                                         { "Saturation", TW_TYPE_INT32, offsetof(CColorExt, S), "hide min=0 max=255" },
                                         { "Alpha", TW_TYPE_INT32, offsetof(CColorExt, A), "hide min=0 max=255" },
                                         { "Mode", TW_TYPE_BOOLCPP, offsetof(CColorExt, m_HLS), "true='HLS' false='RGB' readwrite" } };
    g_TwMgr->m_TypeColor32 = TwDefineStructExt("COLOR32", ColorExtMembers, 8, sizeof(unsigned int), sizeof(CColorExt), CColorExt_InitColor32CB, CColorExt_CopyVarFromExtCB, CColorExt_CopyVarToExtCB, CColorExt_SummaryCB, g_PassProxyAsClientData, "A 32-bit-encoded color.");
    g_TwMgr->m_TypeColor3F = TwDefineStructExt("COLOR3F", ColorExtMembers, 8, 3*sizeof(float), sizeof(CColorExt), CColorExt_InitColor3FCB, CColorExt_CopyVarFromExtCB, CColorExt_CopyVarToExtCB, CColorExt_SummaryCB, g_PassProxyAsClientData, "A 3-floats-encoded RGB color.");
    g_TwMgr->m_TypeColor4F = TwDefineStructExt("COLOR4F", ColorExtMembers, 8, 4*sizeof(float), sizeof(CColorExt), CColorExt_InitColor4FCB, CColorExt_CopyVarFromExtCB, CColorExt_CopyVarToExtCB, CColorExt_SummaryCB, g_PassProxyAsClientData, "A 4-floats-encoded RGBA color.");
    // Do not name them "TW_COLOR*" because the name is displayed in the help bar.
}

//  ---------------------------------------------------------------------------
//  Quaternion ext type
//  ---------------------------------------------------------------------------

void ANT_CALL CQuaternionExt_InitQuat4FCB(void *_ExtValue, void *_ClientData)
{
    CQuaternionExt *ext = (CQuaternionExt *)(_ExtValue);
    if( ext )
    {
        ext->Qx = ext->Qy = ext->Qz = 0;
        ext->Qs = 1;
        ext->Vx = 1;
        ext->Vy = ext->Vz = 0;
        ext->Angle = 0;
        ext->Dx = ext->Dy = ext->Dz = 0;
        ext->m_AAMode = false; // Axis & angle mode hidden
        ext->m_ShowVal = false;
        ext->m_IsFloat = true;
        ext->m_IsDir = false;
        ext->m_Dir[0] = ext->m_Dir[1] = ext->m_Dir[2] = 0;
        ext->m_DirColor = 0xffffff00;
        int i, j;
        for(i=0; i<3; ++i)
            for(j=0; j<3; ++j)
                ext->m_Permute[i][j] = (i==j) ? 1.0f : 0.0f;
        ext->m_StructProxy = (CStructProxy *)_ClientData;
        CQuaternionExt_ConvertToAxisAngle(ext);
        ext->m_Highlighted = false;
        ext->m_Rotating = false;
        if( ext->m_StructProxy!=NULL )
        {
            ext->m_StructProxy->m_CustomDrawCallback = CQuaternionExt_DrawCB;
            ext->m_StructProxy->m_CustomMouseButtonCallback = CQuaternionExt_MouseButtonCB;
            ext->m_StructProxy->m_CustomMouseMotionCallback = CQuaternionExt_MouseMotionCB;
            ext->m_StructProxy->m_CustomMouseLeaveCallback = CQuaternionExt_MouseLeaveCB;
        }
    }
}

void ANT_CALL CQuaternionExt_InitQuat4DCB(void *_ExtValue, void *_ClientData)
{
    CQuaternionExt *ext = (CQuaternionExt *)(_ExtValue);
    if( ext )
    {
        ext->Qx = ext->Qy = ext->Qz = 0;
        ext->Qs = 1;
        ext->Vx = 1;
        ext->Vy = ext->Vz = 0;
        ext->Angle = 0;
        ext->Dx = ext->Dy = ext->Dz = 0;
        ext->m_AAMode = false; // Axis & angle mode hidden
        ext->m_ShowVal = false;
        ext->m_IsFloat = false;
        ext->m_IsDir = false;
        ext->m_Dir[0] = ext->m_Dir[1] = ext->m_Dir[2] = 0;
        ext->m_DirColor = 0xffffff00;
        int i, j;
        for(i=0; i<3; ++i)
            for(j=0; j<3; ++j)
                ext->m_Permute[i][j] = (i==j) ? 1.0f : 0.0f;
        ext->m_StructProxy = (CStructProxy *)_ClientData;
        CQuaternionExt_ConvertToAxisAngle(ext);
        ext->m_Highlighted = false;
        ext->m_Rotating = false;
        if( ext->m_StructProxy!=NULL )
        {
            ext->m_StructProxy->m_CustomDrawCallback = CQuaternionExt_DrawCB;
            ext->m_StructProxy->m_CustomMouseButtonCallback = CQuaternionExt_MouseButtonCB;
            ext->m_StructProxy->m_CustomMouseMotionCallback = CQuaternionExt_MouseMotionCB;
            ext->m_StructProxy->m_CustomMouseLeaveCallback = CQuaternionExt_MouseLeaveCB;
        }
    }
}

void ANT_CALL CQuaternionExt_InitDir3FCB(void *_ExtValue, void *_ClientData)
{
    CQuaternionExt *ext = (CQuaternionExt *)(_ExtValue);
    if( ext )
    {
        ext->Qx = ext->Qy = ext->Qz = 0;
        ext->Qs = 1;
        ext->Vx = 1;
        ext->Vy = ext->Vz = 0;
        ext->Angle = 0;
        ext->Dx = 1;
        ext->Dy = ext->Dz = 0;
        ext->m_AAMode = false; // Axis & angle mode hidden
        ext->m_ShowVal = true;
        ext->m_IsFloat = true;
        ext->m_IsDir = true;
        ext->m_Dir[0] = ext->m_Dir[1] = ext->m_Dir[2] = 0;
        ext->m_DirColor = 0xffffff00;
        int i, j;
        for(i=0; i<3; ++i)
            for(j=0; j<3; ++j)
                ext->m_Permute[i][j] = (i==j) ? 1.0f : 0.0f;
        ext->m_StructProxy = (CStructProxy *)_ClientData;
        CQuaternionExt_ConvertToAxisAngle(ext);
        ext->m_Highlighted = false;
        ext->m_Rotating = false;
        if( ext->m_StructProxy!=NULL )
        {
            ext->m_StructProxy->m_CustomDrawCallback = CQuaternionExt_DrawCB;
            ext->m_StructProxy->m_CustomMouseButtonCallback = CQuaternionExt_MouseButtonCB;
            ext->m_StructProxy->m_CustomMouseMotionCallback = CQuaternionExt_MouseMotionCB;
            ext->m_StructProxy->m_CustomMouseLeaveCallback = CQuaternionExt_MouseLeaveCB;
        }
    }
}

void ANT_CALL CQuaternionExt_InitDir3DCB(void *_ExtValue, void *_ClientData)
{
    CQuaternionExt *ext = (CQuaternionExt *)(_ExtValue);
    if( ext )
    {
        ext->Qx = ext->Qy = ext->Qz = 0;
        ext->Qs = 1;
        ext->Vx = 1;
        ext->Vy = ext->Vz = 0;
        ext->Angle = 0;
        ext->Dx = 1;
        ext->Dy = ext->Dz = 0;
        ext->m_AAMode = false; // Axis & angle mode hidden
        ext->m_ShowVal = true;
        ext->m_IsFloat = false;
        ext->m_IsDir = true;
        ext->m_Dir[0] = ext->m_Dir[1] = ext->m_Dir[2] = 0;
        ext->m_DirColor = 0xffffff00;
        int i, j;
        for(i=0; i<3; ++i)
            for(j=0; j<3; ++j)
                ext->m_Permute[i][j] = (i==j) ? 1.0f : 0.0f;
        ext->m_StructProxy = (CStructProxy *)_ClientData;
        CQuaternionExt_ConvertToAxisAngle(ext);
        ext->m_Highlighted = false;
        ext->m_Rotating = false;
        if( ext->m_StructProxy!=NULL )
        {
            ext->m_StructProxy->m_CustomDrawCallback = CQuaternionExt_DrawCB;
            ext->m_StructProxy->m_CustomMouseButtonCallback = CQuaternionExt_MouseButtonCB;
            ext->m_StructProxy->m_CustomMouseMotionCallback = CQuaternionExt_MouseMotionCB;
            ext->m_StructProxy->m_CustomMouseLeaveCallback = CQuaternionExt_MouseLeaveCB;
        }
    }
}

void ANT_CALL CQuaternionExt_CopyVarFromExtCB(void *_VarValue, const void *_ExtValue, unsigned int _ExtMemberIndex, void *_ClientData)
{
    CQuaternionExt *ext = (CQuaternionExt *)(_ExtValue);
    CMemberProxy *mProxy = (CMemberProxy *)(_ClientData);
    if( _VarValue && ext )
    {
        // Synchronize Quat and AxisAngle
        if( _ExtMemberIndex>=4 && _ExtMemberIndex<=7 )
        {
            CQuaternionExt_ConvertToAxisAngle(ext);
            // show/hide quat values
            if( _ExtMemberIndex==4 && mProxy && mProxy->m_VarParent )
            {
                assert( mProxy->m_VarParent->m_Vars.count==16 );
                bool visible = ext->m_ShowVal;
                if( ext->m_IsDir )
                {
                    if(    mProxy->m_VarParent->m_Vars.items[13]->m_Visible != visible
                        || mProxy->m_VarParent->m_Vars.items[14]->m_Visible != visible
                        || mProxy->m_VarParent->m_Vars.items[15]->m_Visible != visible )
                    {
                        mProxy->m_VarParent->m_Vars.items[13]->m_Visible = visible;
                        mProxy->m_VarParent->m_Vars.items[14]->m_Visible = visible;
                        mProxy->m_VarParent->m_Vars.items[15]->m_Visible = visible;
                        CTwBar_NotUpToDate(mProxy->m_Bar);
                    }
                }
                else
                {
                    if(    mProxy->m_VarParent->m_Vars.items[4]->m_Visible != visible
                        || mProxy->m_VarParent->m_Vars.items[5]->m_Visible != visible
                        || mProxy->m_VarParent->m_Vars.items[6]->m_Visible != visible
                        || mProxy->m_VarParent->m_Vars.items[7]->m_Visible != visible )
                    {
                        mProxy->m_VarParent->m_Vars.items[4]->m_Visible = visible;
                        mProxy->m_VarParent->m_Vars.items[5]->m_Visible = visible;
                        mProxy->m_VarParent->m_Vars.items[6]->m_Visible = visible;
                        mProxy->m_VarParent->m_Vars.items[7]->m_Visible = visible;
                        CTwBar_NotUpToDate(mProxy->m_Bar);
                    }
                }
            }
        }
        else if( _ExtMemberIndex>=8 && _ExtMemberIndex<=11 )
            CQuaternionExt_ConvertFromAxisAngle(ext);
        else if( mProxy && _ExtMemberIndex==12 && mProxy->m_VarParent && !ext->m_IsDir )
        {
            assert( mProxy->m_VarParent->m_Vars.count==16 );
            bool aa = ext->m_AAMode;
            if(    mProxy->m_VarParent->m_Vars.items[4]->m_Visible != !aa
                || mProxy->m_VarParent->m_Vars.items[5]->m_Visible != !aa
                || mProxy->m_VarParent->m_Vars.items[6]->m_Visible != !aa
                || mProxy->m_VarParent->m_Vars.items[7]->m_Visible != !aa
                || mProxy->m_VarParent->m_Vars.items[8 ]->m_Visible != aa
                || mProxy->m_VarParent->m_Vars.items[9 ]->m_Visible != aa
                || mProxy->m_VarParent->m_Vars.items[10]->m_Visible != aa
                || mProxy->m_VarParent->m_Vars.items[11]->m_Visible != aa )
            {
                mProxy->m_VarParent->m_Vars.items[4]->m_Visible = !aa;
                mProxy->m_VarParent->m_Vars.items[5]->m_Visible = !aa;
                mProxy->m_VarParent->m_Vars.items[6]->m_Visible = !aa;
                mProxy->m_VarParent->m_Vars.items[7]->m_Visible = !aa;
                mProxy->m_VarParent->m_Vars.items[8 ]->m_Visible = aa;
                mProxy->m_VarParent->m_Vars.items[9 ]->m_Visible = aa;
                mProxy->m_VarParent->m_Vars.items[10]->m_Visible = aa;
                mProxy->m_VarParent->m_Vars.items[11]->m_Visible = aa;
                CTwBar_NotUpToDate(mProxy->m_Bar);
            }
            if( ((CTwVarAtom *)(mProxy->m_VarParent->m_Vars.items[12]))->m_ReadOnly )
            {
                ((CTwVarAtom *)(mProxy->m_VarParent->m_Vars.items[12]))->m_ReadOnly = false;
                CTwBar_NotUpToDate(mProxy->m_Bar);
            }
        }

        if( ext->m_IsFloat )
        {
            float *var = (float *)(_VarValue);
            if( ext->m_IsDir )
            {
                var[0] = (float)ext->Dx;
                var[1] = (float)ext->Dy;
                var[2] = (float)ext->Dz;
            }
            else // quat
            {
                var[0] = (float)ext->Qx;
                var[1] = (float)ext->Qy;
                var[2] = (float)ext->Qz;
                var[3] = (float)ext->Qs;
            }
        }
        else
        {
            double *var = (double *)(_VarValue);
            if( ext->m_IsDir )
            {
                var[0] = ext->Dx;
                var[1] = ext->Dy;
                var[2] = ext->Dz;
            }
            else // quat
            {
                var[0] = ext->Qx;
                var[1] = ext->Qy;
                var[2] = ext->Qz;
                var[3] = ext->Qs;
            }
        }
    }
}

void ANT_CALL CQuaternionExt_CopyVarToExtCB(const void *_VarValue, void *_ExtValue, unsigned int _ExtMemberIndex, void *_ClientData)
{
    CQuaternionExt *ext = (CQuaternionExt *)(_ExtValue);
    CMemberProxy *mProxy = (CMemberProxy *)(_ClientData);
    (void)mProxy;
    if( _VarValue && ext )
    {
        if( mProxy && _ExtMemberIndex==12 && mProxy->m_VarParent && !ext->m_IsDir )
        {
            assert( mProxy->m_VarParent->m_Vars.count==16 );
            bool aa = ext->m_AAMode;
            if(    mProxy->m_VarParent->m_Vars.items[4]->m_Visible != !aa
                || mProxy->m_VarParent->m_Vars.items[5]->m_Visible != !aa
                || mProxy->m_VarParent->m_Vars.items[6]->m_Visible != !aa
                || mProxy->m_VarParent->m_Vars.items[7]->m_Visible != !aa
                || mProxy->m_VarParent->m_Vars.items[8 ]->m_Visible != aa
                || mProxy->m_VarParent->m_Vars.items[9 ]->m_Visible != aa
                || mProxy->m_VarParent->m_Vars.items[10]->m_Visible != aa
                || mProxy->m_VarParent->m_Vars.items[11]->m_Visible != aa )
            {
                mProxy->m_VarParent->m_Vars.items[4]->m_Visible = !aa;
                mProxy->m_VarParent->m_Vars.items[5]->m_Visible = !aa;
                mProxy->m_VarParent->m_Vars.items[6]->m_Visible = !aa;
                mProxy->m_VarParent->m_Vars.items[7]->m_Visible = !aa;
                mProxy->m_VarParent->m_Vars.items[8 ]->m_Visible = aa;
                mProxy->m_VarParent->m_Vars.items[9 ]->m_Visible = aa;
                mProxy->m_VarParent->m_Vars.items[10]->m_Visible = aa;
                mProxy->m_VarParent->m_Vars.items[11]->m_Visible = aa;
                CTwBar_NotUpToDate(mProxy->m_Bar);
            }
            if( ((CTwVarAtom *)(mProxy->m_VarParent->m_Vars.items[12]))->m_ReadOnly )
            {
                ((CTwVarAtom *)(mProxy->m_VarParent->m_Vars.items[12]))->m_ReadOnly = false;
                CTwBar_NotUpToDate(mProxy->m_Bar);
            }
        }
        else if( mProxy && _ExtMemberIndex==4 && mProxy->m_VarParent )
        {
            assert( mProxy->m_VarParent->m_Vars.count==16 );
            bool visible = ext->m_ShowVal;
            if( ext->m_IsDir )
            {
                if(    mProxy->m_VarParent->m_Vars.items[13]->m_Visible != visible
                    || mProxy->m_VarParent->m_Vars.items[14]->m_Visible != visible
                    || mProxy->m_VarParent->m_Vars.items[15]->m_Visible != visible )
                {
                    mProxy->m_VarParent->m_Vars.items[13]->m_Visible = visible;
                    mProxy->m_VarParent->m_Vars.items[14]->m_Visible = visible;
                    mProxy->m_VarParent->m_Vars.items[15]->m_Visible = visible;
                    CTwBar_NotUpToDate(mProxy->m_Bar);
                }
            }
            else
            {
                if(    mProxy->m_VarParent->m_Vars.items[4]->m_Visible != visible
                    || mProxy->m_VarParent->m_Vars.items[5]->m_Visible != visible
                    || mProxy->m_VarParent->m_Vars.items[6]->m_Visible != visible
                    || mProxy->m_VarParent->m_Vars.items[7]->m_Visible != visible )
                {
                    mProxy->m_VarParent->m_Vars.items[4]->m_Visible = visible;
                    mProxy->m_VarParent->m_Vars.items[5]->m_Visible = visible;
                    mProxy->m_VarParent->m_Vars.items[6]->m_Visible = visible;
                    mProxy->m_VarParent->m_Vars.items[7]->m_Visible = visible;
                    CTwBar_NotUpToDate(mProxy->m_Bar);
                }
            }
        }

        if( ext->m_IsFloat )
        {
            const float *var = (const float *)(_VarValue);
            if( ext->m_IsDir )
            {
                ext->Dx = var[0];
                ext->Dy = var[1];
                ext->Dz = var[2];
                CQuaternionExt_QuatFromDir(&ext->Qx, &ext->Qy, &ext->Qz, &ext->Qs, var[0], var[1], var[2]);
            }
            else
            {
                ext->Qx = var[0];
                ext->Qy = var[1];
                ext->Qz = var[2];
                ext->Qs = var[3];
            }

        }
        else
        {
            const double *var = (const double *)(_VarValue);
            if( ext->m_IsDir )
            {
                ext->Dx = var[0];
                ext->Dy = var[1];
                ext->Dz = var[2];
                CQuaternionExt_QuatFromDir(&ext->Qx, &ext->Qy, &ext->Qz, &ext->Qs, var[0], var[1], var[2]);
            }
            else
            {
                ext->Qx = var[0];
                ext->Qy = var[1];
                ext->Qz = var[2];
                ext->Qs = var[3];
            }
        }
        CQuaternionExt_ConvertToAxisAngle(ext);
    }
}

void ANT_CALL CQuaternionExt_SummaryCB(char *_SummaryString, size_t _SummaryMaxLength, const void *_ExtValue, void *_ClientData)
{
    (void)_ClientData;
    const CQuaternionExt *ext = (const CQuaternionExt *)(_ExtValue);
    if( ext )
    {
        if( ext->m_AAMode )
            _snprintf(_SummaryString, _SummaryMaxLength, "V={%.2f,%.2f,%.2f} A=%.0f%c", ext->Vx, ext->Vy, ext->Vz, ext->Angle, 176);
        else if( ext->m_IsDir )
        {
            //float d[] = {1, 0, 0};
            //CQuaternionExt_ApplyQuat(d+0, d+1, d+2, 1, 0, 0, (float)ext->Qx, (float)ext->Qy, (float)ext->Qz, (float)ext->Qs);
            _snprintf(_SummaryString, _SummaryMaxLength, "V={%.2f,%.2f,%.2f}", ext->Dx, ext->Dy, ext->Dz);
        }
        else
            _snprintf(_SummaryString, _SummaryMaxLength, "Q={x:%.2f,y:%.2f,z:%.2f,s:%.2f}", ext->Qx, ext->Qy, ext->Qz, ext->Qs);
    }
    else
    {
        _SummaryString[0] = ' ';    // required to force background color for this value
        _SummaryString[1] = '\0';
    }
}

// Was a CQuaternionExt class-static member; there being only one
// CQuaternionExt-related custom type, a plain file-scope static works.
static TwType s_CustomType = TW_TYPE_UNDEF;
// Were CQuaternionExt class-static std::vector members (a geometry cache
// for the quaternion/dir widget's sphere+arrow mesh, built once by
// CQuaternionExt_CreateSphere/CreateArrow, read every draw call by
// CQuaternionExt_DrawCB) - plain file-scope C99 arrays now that
// CQuaternionExt provides no namespacing scope. CColor32Array (TwBar.h)
// and CIntArray (TwMgr.h, Cluster 4 pass 2) already exist and are reused
// here; CFloatArray is new.
typedef struct { float *items; size_t count; size_t capacity; } CFloatArray;
static CFloatArray   s_SphTri = {0};
static CColor32Array s_SphCol = {0};
static CIntArray     s_SphTriProj = {0};
static CColor32Array s_SphColLight = {0};
static CFloatArray   s_ArrowTri[4] = {{0}};
static CFloatArray   s_ArrowNorm[4] = {{0}};
static CIntArray     s_ArrowTriProj[4] = {{0}};
static CColor32Array s_ArrowColLight[4] = {{0}};

void CQuaternionExt_CreateTypes(void)
{
    if( g_TwMgr==NULL )
        return;
    s_CustomType = (TwType)(TW_TYPE_CUSTOM_BASE + g_TwMgr->m_NbCustoms);
    ++g_TwMgr->m_NbCustoms; // increment custom type number

    for(int pass=0; pass<2; pass++) // pass 0: create quat types; pass 1: create dir types
    {
        const char *quatDefPass0 = "step=0.01 hide";
        const char *quatDefPass1 = "step=0.01 hide";
        const char *quatSDefPass0 = "step=0.01 min=-1 max=1 hide";
        const char *quatSDefPass1 = "step=0.01 min=-1 max=1 hide";
        const char *dirDefPass0 = "step=0.01 hide";
        const char *dirDefPass1 = "step=0.01";
        const char *quatDef = (pass==0) ? quatDefPass0 : quatDefPass1;
        const char *quatSDef = (pass==0) ? quatSDefPass0 : quatSDefPass1;
        const char *dirDef = (pass==0) ? dirDefPass0 : dirDefPass1;

        TwStructMember QuatExtMembers[] = { { "0", s_CustomType, 0, "" },
                                            { "1", s_CustomType, 0, "" },
                                            { "2", s_CustomType, 0, "" }, 
                                            { "3", s_CustomType, 0, "" }, 
                                            { "Quat X", TW_TYPE_DOUBLE, offsetof(CQuaternionExt, Qx), quatDef }, // copy of the source quaternion
                                            { "Quat Y", TW_TYPE_DOUBLE, offsetof(CQuaternionExt, Qy), quatDef },
                                            { "Quat Z", TW_TYPE_DOUBLE, offsetof(CQuaternionExt, Qz), quatDef },
                                            { "Quat S", TW_TYPE_DOUBLE, offsetof(CQuaternionExt, Qs), quatSDef },
                                            { "Axis X", TW_TYPE_DOUBLE, offsetof(CQuaternionExt, Vx), "step=0.01 hide" }, // axis and angle conversion -> Mode hidden because it is not equivalent to a quat (would have required vector renormalization)
                                            { "Axis Y", TW_TYPE_DOUBLE, offsetof(CQuaternionExt, Vy), "step=0.01 hide" },
                                            { "Axis Z", TW_TYPE_DOUBLE, offsetof(CQuaternionExt, Vz), "step=0.01 hide" },
                                            { "Angle (degree)",  TW_TYPE_DOUBLE, offsetof(CQuaternionExt, Angle), "step=1 min=-360 max=360 hide" },
                                            { "Mode", TW_TYPE_BOOLCPP, offsetof(CQuaternionExt, m_AAMode), "true='Axis Angle' false='Quaternion' readwrite hide" },
                                            { "Dir X", TW_TYPE_DOUBLE, offsetof(CQuaternionExt, Dx), dirDef },      // copy of the source direction
                                            { "Dir Y", TW_TYPE_DOUBLE, offsetof(CQuaternionExt, Dy), dirDef },
                                            { "Dir Z", TW_TYPE_DOUBLE, offsetof(CQuaternionExt, Dz), dirDef } };
        if( pass==0 ) 
        {
            g_TwMgr->m_TypeQuat4F = TwDefineStructExt("QUAT4F", QuatExtMembers, sizeof(QuatExtMembers)/sizeof(QuatExtMembers[0]), 4*sizeof(float), sizeof(CQuaternionExt), CQuaternionExt_InitQuat4FCB, CQuaternionExt_CopyVarFromExtCB, CQuaternionExt_CopyVarToExtCB, CQuaternionExt_SummaryCB, g_PassProxyAsClientData, "A 4-floats-encoded quaternion");
            g_TwMgr->m_TypeQuat4D = TwDefineStructExt("QUAT4D", QuatExtMembers, sizeof(QuatExtMembers)/sizeof(QuatExtMembers[0]), 4*sizeof(double), sizeof(CQuaternionExt), CQuaternionExt_InitQuat4DCB, CQuaternionExt_CopyVarFromExtCB, CQuaternionExt_CopyVarToExtCB, CQuaternionExt_SummaryCB, g_PassProxyAsClientData, "A 4-doubles-encoded quaternion");
        }
        else if( pass==1 )
        {
            g_TwMgr->m_TypeDir3F = TwDefineStructExt("DIR4F", QuatExtMembers, sizeof(QuatExtMembers)/sizeof(QuatExtMembers[0]), 3*sizeof(float), sizeof(CQuaternionExt), CQuaternionExt_InitDir3FCB, CQuaternionExt_CopyVarFromExtCB, CQuaternionExt_CopyVarToExtCB, CQuaternionExt_SummaryCB, g_PassProxyAsClientData, "A 3-floats-encoded direction");
            g_TwMgr->m_TypeDir3D = TwDefineStructExt("DIR4D", QuatExtMembers, sizeof(QuatExtMembers)/sizeof(QuatExtMembers[0]), 3*sizeof(double), sizeof(CQuaternionExt), CQuaternionExt_InitDir3DCB, CQuaternionExt_CopyVarFromExtCB, CQuaternionExt_CopyVarToExtCB, CQuaternionExt_SummaryCB, g_PassProxyAsClientData, "A 3-doubles-encoded direction");
        }
    }

    CQuaternionExt_CreateSphere();
    CQuaternionExt_CreateArrow();
}

void CQuaternionExt_ConvertToAxisAngle(CQuaternionExt *_Ext)
{
    if( fabs(_Ext->Qs)>(1.0 + FLOAT_EPS) )
    {
        //_Ext->Vx = _Ext->Vy = _Ext->Vz = 0; // no, keep the previous value
        _Ext->Angle = 0;
    }
    else
    {
        double a;
        if( _Ext->Qs>=1.0f )
            a = 0; // and keep V
        else if( _Ext->Qs<=-1.0f )
            a = DOUBLE_PI; // and keep V
        else if( fabs(_Ext->Qx*_Ext->Qx+_Ext->Qy*_Ext->Qy+_Ext->Qz*_Ext->Qz+_Ext->Qs*_Ext->Qs)<FLOAT_EPS_SQ )
            a = 0;
        else
        {
            a = acos(_Ext->Qs);
            if( a*_Ext->Angle<0 ) // Preserve the sign of Angle
                a = -a;
            double f = 1.0f / sin(a);
            _Ext->Vx = _Ext->Qx * f;
            _Ext->Vy = _Ext->Qy * f;
            _Ext->Vz = _Ext->Qz * f;
        }
        _Ext->Angle = 2.0*a;
    }

    //  if( _Ext->Angle>FLOAT_PI )
    //      _Ext->Angle -= 2.0f*FLOAT_PI;
    //  else if( _Ext->Angle<-FLOAT_PI )
    //      _Ext->Angle += 2.0f*FLOAT_PI;
    _Ext->Angle = RadToDeg(_Ext->Angle);

    if( fabs(_Ext->Angle)<FLOAT_EPS && fabs(_Ext->Vx*_Ext->Vx+_Ext->Vy*_Ext->Vy+_Ext->Vz*_Ext->Vz)<FLOAT_EPS_SQ )
        _Ext->Vx = 1.0e-7;    // all components cannot be null
}

void CQuaternionExt_ConvertFromAxisAngle(CQuaternionExt *_Ext)
{
    double n = _Ext->Vx*_Ext->Vx + _Ext->Vy*_Ext->Vy + _Ext->Vz*_Ext->Vz;
    if( fabs(n)>FLOAT_EPS_SQ )
    {
        double f = 0.5*DegToRad(_Ext->Angle);
        _Ext->Qs = cos(f);
        //do not normalize
        //if( fabs(n - 1.0)>FLOAT_EPS_SQ )
        //  f = sin(f) * (1.0/sqrt(n)) ;
        //else
        //  f = sin(f);
        f = sin(f);

        _Ext->Qx = _Ext->Vx * f;
        _Ext->Qy = _Ext->Vy * f;
        _Ext->Qz = _Ext->Vz * f;
    }
    else
    {
        _Ext->Qs = 1.0;
        _Ext->Qx = _Ext->Qy = _Ext->Qz = 0.0;
    }
}

void CQuaternionExt_CopyToVar(CQuaternionExt *_Ext)
{
    if( _Ext->m_StructProxy!=NULL )
    {
        if( _Ext->m_StructProxy->m_StructSetCallback!=NULL )
        {
            if( _Ext->m_IsFloat )
            {
                if( _Ext->m_IsDir )
                {
                    float d[] = {1, 0, 0};
                    CQuaternionExt_ApplyQuat(d+0, d+1, d+2, 1, 0, 0, (float)_Ext->Qx, (float)_Ext->Qy, (float)_Ext->Qz, (float)_Ext->Qs);
                    float l = (float)sqrt(_Ext->Dx*_Ext->Dx + _Ext->Dy*_Ext->Dy + _Ext->Dz*_Ext->Dz);
                    d[0] *= l; d[1] *= l; d[2] *= l;
                    _Ext->Dx = d[0]; _Ext->Dy = d[1]; _Ext->Dz = d[2]; // update also Dx,Dy,Dz
                    _Ext->m_StructProxy->m_StructSetCallback(d, _Ext->m_StructProxy->m_StructClientData);
                }
                else
                {
                    float q[] = { (float)_Ext->Qx, (float)_Ext->Qy, (float)_Ext->Qz, (float)_Ext->Qs };
                    _Ext->m_StructProxy->m_StructSetCallback(q, _Ext->m_StructProxy->m_StructClientData);
                }
            }
            else
            {
                if( _Ext->m_IsDir )
                {
                    float d[] = {1, 0, 0};
                    CQuaternionExt_ApplyQuat(d+0, d+1, d+2, 1, 0, 0, (float)_Ext->Qx, (float)_Ext->Qy, (float)_Ext->Qz, (float)_Ext->Qs);
                    double l = sqrt(_Ext->Dx*_Ext->Dx + _Ext->Dy*_Ext->Dy + _Ext->Dz*_Ext->Dz);
                    double dd[] = {l*d[0], l*d[1], l*d[2]};
                    _Ext->Dx = dd[0]; _Ext->Dy = dd[1]; _Ext->Dz = dd[2]; // update also Dx,Dy,Dz
                    _Ext->m_StructProxy->m_StructSetCallback(dd, _Ext->m_StructProxy->m_StructClientData);
                }
                else
                {
                    double q[] = { _Ext->Qx, _Ext->Qy, _Ext->Qz, _Ext->Qs };
                    _Ext->m_StructProxy->m_StructSetCallback(q, _Ext->m_StructProxy->m_StructClientData);
                }
            }
        }
        else if( _Ext->m_StructProxy->m_StructData!=NULL )
        {
            if( _Ext->m_IsFloat )
            {
                if( _Ext->m_IsDir )
                {
                    float *d = (float *)(_Ext->m_StructProxy->m_StructData);
                    CQuaternionExt_ApplyQuat(d+0, d+1, d+2, 1, 0, 0, (float)_Ext->Qx, (float)_Ext->Qy, (float)_Ext->Qz, (float)_Ext->Qs);
                    float l = (float)sqrt(_Ext->Dx*_Ext->Dx + _Ext->Dy*_Ext->Dy + _Ext->Dz*_Ext->Dz);
                    d[0] *= l; d[1] *= l; d[2] *= l;
                    _Ext->Dx = d[0]; _Ext->Dy = d[1]; _Ext->Dz = d[2]; // update also Dx,Dy,Dz
                }
                else
                {
                    float *q = (float *)(_Ext->m_StructProxy->m_StructData);
                    q[0] = (float)_Ext->Qx; q[1] = (float)_Ext->Qy; q[2] = (float)_Ext->Qz; q[3] = (float)_Ext->Qs;
                }
            }
            else
            {
                if( _Ext->m_IsDir )
                {
                    double *dd = (double *)(_Ext->m_StructProxy->m_StructData);
                    float d[] = {1, 0, 0};
                    CQuaternionExt_ApplyQuat(d+0, d+1, d+2, 1, 0, 0, (float)_Ext->Qx, (float)_Ext->Qy, (float)_Ext->Qz, (float)_Ext->Qs);
                    double l = sqrt(_Ext->Dx*_Ext->Dx + _Ext->Dy*_Ext->Dy + _Ext->Dz*_Ext->Dz);
                    dd[0] = l*d[0]; dd[1] = l*d[1]; dd[2] = l*d[2];
                    _Ext->Dx = dd[0]; _Ext->Dy = dd[1]; _Ext->Dz = dd[2]; // update also Dx,Dy,Dz
                }
                else
                {
                    double *q = (double *)(_Ext->m_StructProxy->m_StructData);
                    q[0] = _Ext->Qx; q[1] = _Ext->Qy; q[2] = _Ext->Qz; q[3] = _Ext->Qs;
                }
            }
        }
    }
}

void CQuaternionExt_CreateSphere(void)
{
    const int SUBDIV = 7;
    s_SphTri.count = 0;
    s_SphCol.count = 0;

    const float A[8*3] = { 1,0,0, 0,0,-1, -1,0,0, 0,0,1,   0,0,1,  1,0,0,  0,0,-1, -1,0,0 };
    const float B[8*3] = { 0,1,0, 0,1,0,  0,1,0,  0,1,0,   0,-1,0, 0,-1,0, 0,-1,0, 0,-1,0 };
    const float C[8*3] = { 0,0,1, 1,0,0,  0,0,-1, -1,0,0,  1,0,0,  0,0,-1, -1,0,0, 0,0,1  };
    //const color32 COL_A[8] = { 0xffff8080, 0xff000080, 0xff800000, 0xff8080ff,  0xff8080ff, 0xffff8080, 0xff000080, 0xff800000 };
    //const color32 COL_B[8] = { 0xff80ff80, 0xff80ff80, 0xff80ff80, 0xff80ff80,  0xff008000, 0xff008000, 0xff008000, 0xff008000 };
    //const color32 COL_C[8] = { 0xff8080ff, 0xffff8080, 0xff000080, 0xff800000,  0xffff8080, 0xff000080, 0xff800000, 0xff8080ff };
    const color32 COL_A[8] = { 0xffffffff, 0xffffff40, 0xff40ff40, 0xff40ffff,  0xffff40ff, 0xffff4040, 0xff404040, 0xff4040ff };
    const color32 COL_B[8] = { 0xffffffff, 0xffffff40, 0xff40ff40, 0xff40ffff,  0xffff40ff, 0xffff4040, 0xff404040, 0xff4040ff };
    const color32 COL_C[8] = { 0xffffffff, 0xffffff40, 0xff40ff40, 0xff40ffff,  0xffff40ff, 0xffff4040, 0xff404040, 0xff4040ff };

    int i, j, k, l;
    float xa, ya, za, xb, yb, zb, xc, yc, zc, x, y, z, norm, u[3], v[3];
    color32 col;
    for( i=0; i<8; ++i )
    {
        xa = A[3*i+0]; ya = A[3*i+1]; za = A[3*i+2];
        xb = B[3*i+0]; yb = B[3*i+1]; zb = B[3*i+2];
        xc = C[3*i+0]; yc = C[3*i+1]; zc = C[3*i+2];
        for( j=0; j<=SUBDIV; ++j )
            for( k=0; k<=2*(SUBDIV-j); ++k )
            {
                if( k%2==0 )
                {
                    u[0] = ((float)j)/(SUBDIV+1);
                    v[0] = ((float)(k/2))/(SUBDIV+1);
                    u[1] = ((float)(j+1))/(SUBDIV+1);
                    v[1] = ((float)(k/2))/(SUBDIV+1);
                    u[2] = ((float)j)/(SUBDIV+1);
                    v[2] = ((float)(k/2+1))/(SUBDIV+1);
                }
                else
                {
                    u[0] = ((float)j)/(SUBDIV+1);
                    v[0] = ((float)(k/2+1))/(SUBDIV+1);
                    u[1] = ((float)(j+1))/(SUBDIV+1);
                    v[1] = ((float)(k/2))/(SUBDIV+1);
                    u[2] = ((float)(j+1))/(SUBDIV+1);
                    v[2] = ((float)(k/2+1))/(SUBDIV+1);
                }

                for( l=0; l<3; ++l )
                {
                    x = (1.0f-u[l]-v[l])*xa + u[l]*xb + v[l]*xc;
                    y = (1.0f-u[l]-v[l])*ya + u[l]*yb + v[l]*yc;
                    z = (1.0f-u[l]-v[l])*za + u[l]*zb + v[l]*zc;
                    norm = sqrtf(x*x+y*y+z*z);
                    x /= norm; y /= norm; z /= norm;
                    tw_da_append(&s_SphTri, x); tw_da_append(&s_SphTri, y); tw_da_append(&s_SphTri, z);
                    if( u[l]+v[l]>FLOAT_EPS )
                        col = ColorBlend(COL_A[i], ColorBlend(COL_B[i], COL_C[i], v[l]/(u[l]+v[l])), u[l]+v[l]);
                    else
                        col = COL_A[i];
                    //if( (j==0 && k==0) || (j==0 && k==2*SUBDIV) || (j==SUBDIV && k==0) )
                    //  col = 0xffff0000;
                    tw_da_append(&s_SphCol, col);
                }
            }
    }
    // .resize(N, 0) zero-fills new elements; tw_da_resize does not, so
    // memset explicitly after (both arrays are freshly cleared above, so
    // every element here is "new").
    tw_da_resize(&s_SphTriProj, 2*s_SphCol.count);
    memset(s_SphTriProj.items, 0, s_SphTriProj.count*sizeof(int));
    tw_da_resize(&s_SphColLight, s_SphCol.count);
    memset(s_SphColLight.items, 0, s_SphColLight.count*sizeof(color32));
}

void CQuaternionExt_CreateArrow(void)
{
    const int   SUBDIV  = 15;
    const float CYL_RADIUS  = 0.08f;
    const float CONE_RADIUS = 0.16f;
    const float CONE_LENGTH = 0.25f;
    const float ARROW_BGN = -1.1f;
    const float ARROW_END = 1.15f;
    int i;
    for(i=0; i<4; ++i)
    {
        s_ArrowTri[i].count = 0;
        s_ArrowNorm[i].count = 0;
    }
    
    float x0, x1, y0, y1, z0, z1, a0, a1, nx, nn;
    for(i=0; i<SUBDIV; ++i)
    {
        a0 = 2.0f*FLOAT_PI*((float)(i))/SUBDIV;
        a1 = 2.0f*FLOAT_PI*((float)(i+1))/SUBDIV;
        x0 = ARROW_BGN;
        x1 = ARROW_END-CONE_LENGTH;
        y0 = cosf(a0);
        z0 = sinf(a0);
        y1 = cosf(a1);
        z1 = sinf(a1);
        tw_da_append(&s_ArrowTri[ARROW_CYL], x1); tw_da_append(&s_ArrowTri[ARROW_CYL], CYL_RADIUS*y0); tw_da_append(&s_ArrowTri[ARROW_CYL], CYL_RADIUS*z0);
        tw_da_append(&s_ArrowTri[ARROW_CYL], x0); tw_da_append(&s_ArrowTri[ARROW_CYL], CYL_RADIUS*y0); tw_da_append(&s_ArrowTri[ARROW_CYL], CYL_RADIUS*z0);
        tw_da_append(&s_ArrowTri[ARROW_CYL], x0); tw_da_append(&s_ArrowTri[ARROW_CYL], CYL_RADIUS*y1); tw_da_append(&s_ArrowTri[ARROW_CYL], CYL_RADIUS*z1);
        tw_da_append(&s_ArrowTri[ARROW_CYL], x1); tw_da_append(&s_ArrowTri[ARROW_CYL], CYL_RADIUS*y0); tw_da_append(&s_ArrowTri[ARROW_CYL], CYL_RADIUS*z0);
        tw_da_append(&s_ArrowTri[ARROW_CYL], x0); tw_da_append(&s_ArrowTri[ARROW_CYL], CYL_RADIUS*y1); tw_da_append(&s_ArrowTri[ARROW_CYL], CYL_RADIUS*z1);
        tw_da_append(&s_ArrowTri[ARROW_CYL], x1); tw_da_append(&s_ArrowTri[ARROW_CYL], CYL_RADIUS*y1); tw_da_append(&s_ArrowTri[ARROW_CYL], CYL_RADIUS*z1);
        tw_da_append(&s_ArrowNorm[ARROW_CYL], 0); tw_da_append(&s_ArrowNorm[ARROW_CYL], y0); tw_da_append(&s_ArrowNorm[ARROW_CYL], z0);
        tw_da_append(&s_ArrowNorm[ARROW_CYL], 0); tw_da_append(&s_ArrowNorm[ARROW_CYL], y0); tw_da_append(&s_ArrowNorm[ARROW_CYL], z0);
        tw_da_append(&s_ArrowNorm[ARROW_CYL], 0); tw_da_append(&s_ArrowNorm[ARROW_CYL], y1); tw_da_append(&s_ArrowNorm[ARROW_CYL], z1);
        tw_da_append(&s_ArrowNorm[ARROW_CYL], 0); tw_da_append(&s_ArrowNorm[ARROW_CYL], y0); tw_da_append(&s_ArrowNorm[ARROW_CYL], z0);
        tw_da_append(&s_ArrowNorm[ARROW_CYL], 0); tw_da_append(&s_ArrowNorm[ARROW_CYL], y1); tw_da_append(&s_ArrowNorm[ARROW_CYL], z1);
        tw_da_append(&s_ArrowNorm[ARROW_CYL], 0); tw_da_append(&s_ArrowNorm[ARROW_CYL], y1); tw_da_append(&s_ArrowNorm[ARROW_CYL], z1);
        tw_da_append(&s_ArrowTri[ARROW_CYL_CAP], x0); tw_da_append(&s_ArrowTri[ARROW_CYL_CAP], 0); tw_da_append(&s_ArrowTri[ARROW_CYL_CAP], 0);
        tw_da_append(&s_ArrowTri[ARROW_CYL_CAP], x0); tw_da_append(&s_ArrowTri[ARROW_CYL_CAP], CYL_RADIUS*y1); tw_da_append(&s_ArrowTri[ARROW_CYL_CAP], CYL_RADIUS*z1);
        tw_da_append(&s_ArrowTri[ARROW_CYL_CAP], x0); tw_da_append(&s_ArrowTri[ARROW_CYL_CAP], CYL_RADIUS*y0); tw_da_append(&s_ArrowTri[ARROW_CYL_CAP], CYL_RADIUS*z0);
        tw_da_append(&s_ArrowNorm[ARROW_CYL_CAP], -1); tw_da_append(&s_ArrowNorm[ARROW_CYL_CAP], 0); tw_da_append(&s_ArrowNorm[ARROW_CYL_CAP], 0);
        tw_da_append(&s_ArrowNorm[ARROW_CYL_CAP], -1); tw_da_append(&s_ArrowNorm[ARROW_CYL_CAP], 0); tw_da_append(&s_ArrowNorm[ARROW_CYL_CAP], 0);
        tw_da_append(&s_ArrowNorm[ARROW_CYL_CAP], -1); tw_da_append(&s_ArrowNorm[ARROW_CYL_CAP], 0); tw_da_append(&s_ArrowNorm[ARROW_CYL_CAP], 0);
        x0 = ARROW_END-CONE_LENGTH;
        x1 = ARROW_END;
        nx = CONE_RADIUS/(x1-x0);
        nn = 1.0f/sqrtf(nx*nx+1);
        tw_da_append(&s_ArrowTri[ARROW_CONE], x1); tw_da_append(&s_ArrowTri[ARROW_CONE], 0); tw_da_append(&s_ArrowTri[ARROW_CONE], 0);
        tw_da_append(&s_ArrowTri[ARROW_CONE], x0); tw_da_append(&s_ArrowTri[ARROW_CONE], CONE_RADIUS*y0); tw_da_append(&s_ArrowTri[ARROW_CONE], CONE_RADIUS*z0);
        tw_da_append(&s_ArrowTri[ARROW_CONE], x0); tw_da_append(&s_ArrowTri[ARROW_CONE], CONE_RADIUS*y1); tw_da_append(&s_ArrowTri[ARROW_CONE], CONE_RADIUS*z1);
        tw_da_append(&s_ArrowTri[ARROW_CONE], x1); tw_da_append(&s_ArrowTri[ARROW_CONE], 0); tw_da_append(&s_ArrowTri[ARROW_CONE], 0);
        tw_da_append(&s_ArrowTri[ARROW_CONE], x0); tw_da_append(&s_ArrowTri[ARROW_CONE], CONE_RADIUS*y1); tw_da_append(&s_ArrowTri[ARROW_CONE], CONE_RADIUS*z1);
        tw_da_append(&s_ArrowTri[ARROW_CONE], x1); tw_da_append(&s_ArrowTri[ARROW_CONE], 0); tw_da_append(&s_ArrowTri[ARROW_CONE], 0);
        tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*nx); tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*y0); tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*z0);
        tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*nx); tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*y0); tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*z0);
        tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*nx); tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*y1); tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*z1);
        tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*nx); tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*y0); tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*z0);
        tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*nx); tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*y1); tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*z1);
        tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*nx); tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*y1); tw_da_append(&s_ArrowNorm[ARROW_CONE], nn*z1);
        tw_da_append(&s_ArrowTri[ARROW_CONE_CAP], x0); tw_da_append(&s_ArrowTri[ARROW_CONE_CAP], 0); tw_da_append(&s_ArrowTri[ARROW_CONE_CAP], 0);
        tw_da_append(&s_ArrowTri[ARROW_CONE_CAP], x0); tw_da_append(&s_ArrowTri[ARROW_CONE_CAP], CONE_RADIUS*y1); tw_da_append(&s_ArrowTri[ARROW_CONE_CAP], CONE_RADIUS*z1);
        tw_da_append(&s_ArrowTri[ARROW_CONE_CAP], x0); tw_da_append(&s_ArrowTri[ARROW_CONE_CAP], CONE_RADIUS*y0); tw_da_append(&s_ArrowTri[ARROW_CONE_CAP], CONE_RADIUS*z0);
        tw_da_append(&s_ArrowNorm[ARROW_CONE_CAP], -1); tw_da_append(&s_ArrowNorm[ARROW_CONE_CAP], 0); tw_da_append(&s_ArrowNorm[ARROW_CONE_CAP], 0);
        tw_da_append(&s_ArrowNorm[ARROW_CONE_CAP], -1); tw_da_append(&s_ArrowNorm[ARROW_CONE_CAP], 0); tw_da_append(&s_ArrowNorm[ARROW_CONE_CAP], 0);
        tw_da_append(&s_ArrowNorm[ARROW_CONE_CAP], -1); tw_da_append(&s_ArrowNorm[ARROW_CONE_CAP], 0); tw_da_append(&s_ArrowNorm[ARROW_CONE_CAP], 0);
    }

    for(i=0; i<4; ++i)
    {
        tw_da_resize(&s_ArrowTriProj[i], 2*(s_ArrowTri[i].count/3));
        memset(s_ArrowTriProj[i].items, 0, s_ArrowTriProj[i].count*sizeof(int));
        tw_da_resize(&s_ArrowColLight[i], s_ArrowTri[i].count/3);
        memset(s_ArrowColLight[i].items, 0, s_ArrowColLight[i].count*sizeof(color32));
    }
}

static inline void QuatMult(double *out, const double *q1, const double *q2)
{
    out[0] = q1[3]*q2[0] + q1[0]*q2[3] + q1[1]*q2[2] - q1[2]*q2[1];
    out[1] = q1[3]*q2[1] + q1[1]*q2[3] + q1[2]*q2[0] - q1[0]*q2[2];
    out[2] = q1[3]*q2[2] + q1[2]*q2[3] + q1[0]*q2[1] - q1[1]*q2[0];
    out[3] = q1[3]*q2[3] - (q1[0]*q2[0] + q1[1]*q2[1] + q1[2]*q2[2]);
}

static inline void QuatFromAxisAngle(double *out, const double *axis, double angle)
{
    double n = axis[0]*axis[0] + axis[1]*axis[1] + axis[2]*axis[2];
    if( fabs(n)>DOUBLE_EPS )
    {
        double f = 0.5*angle;
        out[3] = cos(f);
        f = sin(f)/sqrt(n);
        out[0] = axis[0]*f;
        out[1] = axis[1]*f;
        out[2] = axis[2]*f;
    }
    else
    {
        out[3] = 1.0;
        out[0] = out[1] = out[2] = 0.0;
    }
}

static inline void Vec3Cross(double *out, const double *a, const double *b)
{
    out[0] = a[1]*b[2]-a[2]*b[1];
    out[1] = a[2]*b[0]-a[0]*b[2];
    out[2] = a[0]*b[1]-a[1]*b[0];
}

static inline double Vec3Dot(const double *a, const double *b)
{
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

static inline void Vec3RotY(float *x, float *y, float *z)
{
    (void)y;
    float tmp = *x;
    *x = - *z;
    *z = tmp;
}

static inline void Vec3RotZ(float *x, float *y, float *z)
{
    (void)z;
    float tmp = *x;
    *x = - *y;
    *y = tmp;
}

void CQuaternionExt_ApplyQuat(float *outX, float *outY, float *outZ, float x, float y, float z, float qx, float qy, float qz, float qs)
{
    float ps = - qx * x - qy * y - qz * z;
    float px =   qs * x + qy * z - qz * y;
    float py =   qs * y + qz * x - qx * z;
    float pz =   qs * z + qx * y - qy * x;
    *outX = - ps * qx + px * qs - py * qz + pz * qy;
    *outY = - ps * qy + py * qs - pz * qx + px * qz;
    *outZ = - ps * qz + pz * qs - px * qy + py * qx;
}

void CQuaternionExt_QuatFromDir(double *outQx, double *outQy, double *outQz, double *outQs, double dx, double dy, double dz)
{
    // compute a quaternion that rotates (1,0,0) to (dx,dy,dz)

    double dn = sqrt(dx*dx + dy*dy + dz*dz);
    if( dn<DOUBLE_EPS_SQ )
    {
        *outQx = *outQy = *outQz = 0;
        *outQs = 1;
    }
    else
    {
        double rotAxis[3] = { 0, -dz, dy };
        if( rotAxis[0]*rotAxis[0] + rotAxis[1]*rotAxis[1] + rotAxis[2]*rotAxis[2]<DOUBLE_EPS_SQ )
        {
            rotAxis[0] = rotAxis[1] = 0;
            rotAxis[2] = 1;
        }
        double rotAngle = acos(dx/dn);
        double rotQuat[4];
        QuatFromAxisAngle(rotQuat, rotAxis, rotAngle);
        *outQx = rotQuat[0];
        *outQy = rotQuat[1];
        *outQz = rotQuat[2];
        *outQs = rotQuat[3];
    }
}

void CQuaternionExt_PermuteF(const CQuaternionExt *_Ext, float *outX, float *outY, float *outZ, float x, float y, float z)
{
    float px = x, py = y, pz = z;
    *outX = _Ext->m_Permute[0][0]*px + _Ext->m_Permute[1][0]*py + _Ext->m_Permute[2][0]*pz;
    *outY = _Ext->m_Permute[0][1]*px + _Ext->m_Permute[1][1]*py + _Ext->m_Permute[2][1]*pz;
    *outZ = _Ext->m_Permute[0][2]*px + _Ext->m_Permute[1][2]*py + _Ext->m_Permute[2][2]*pz;
}

void CQuaternionExt_PermuteInvF(const CQuaternionExt *_Ext, float *outX, float *outY, float *outZ, float x, float y, float z)
{
    float px = x, py = y, pz = z;
    *outX = _Ext->m_Permute[0][0]*px + _Ext->m_Permute[0][1]*py + _Ext->m_Permute[0][2]*pz;
    *outY = _Ext->m_Permute[1][0]*px + _Ext->m_Permute[1][1]*py + _Ext->m_Permute[1][2]*pz;
    *outZ = _Ext->m_Permute[2][0]*px + _Ext->m_Permute[2][1]*py + _Ext->m_Permute[2][2]*pz;
}

void CQuaternionExt_PermuteD(const CQuaternionExt *_Ext, double *outX, double *outY, double *outZ, double x, double y, double z)
{
    double px = x, py = y, pz = z;
    *outX = _Ext->m_Permute[0][0]*px + _Ext->m_Permute[1][0]*py + _Ext->m_Permute[2][0]*pz;
    *outY = _Ext->m_Permute[0][1]*px + _Ext->m_Permute[1][1]*py + _Ext->m_Permute[2][1]*pz;
    *outZ = _Ext->m_Permute[0][2]*px + _Ext->m_Permute[1][2]*py + _Ext->m_Permute[2][2]*pz;
}

void CQuaternionExt_PermuteInvD(const CQuaternionExt *_Ext, double *outX, double *outY, double *outZ, double x, double y, double z)
{
    double px = x, py = y, pz = z;
    *outX = _Ext->m_Permute[0][0]*px + _Ext->m_Permute[0][1]*py + _Ext->m_Permute[0][2]*pz;
    *outY = _Ext->m_Permute[1][0]*px + _Ext->m_Permute[1][1]*py + _Ext->m_Permute[1][2]*pz;
    *outZ = _Ext->m_Permute[2][0]*px + _Ext->m_Permute[2][1]*py + _Ext->m_Permute[2][2]*pz;
}

static inline float QuatD(int w, int h)
{
    return (float)min(abs(w), abs(h)) - 4;
}

static inline int QuatPX(float x, int w, int h)
{
    return (int)(x*0.5f*QuatD(w, h) + (float)w*0.5f + 0.5f);
}

static inline int QuatPY(float y, int w, int h)
{
    return (int)(-y*0.5f*QuatD(w, h) + (float)h*0.5f - 0.5f);
}

static inline float QuatIX(int x, int w, int h)
{
    return (2.0f*(float)x - (float)w - 1.0f)/QuatD(w, h);
}

static inline float QuatIY(int y, int w, int h)
{
    return (-2.0f*(float)y + (float)h - 1.0f)/QuatD(w, h);
}

void CQuaternionExt_DrawCB(int w, int h, void *_ExtValue, void *_ClientData, TwBar *_Bar, CTwVarGroup *varGrp)
{
    if( g_TwMgr==NULL || g_TwMgr->m_Graph==NULL )
        return;
    assert( g_TwMgr->m_Graph->IsDrawing(g_TwMgr->m_Graph) );
    CQuaternionExt *ext = (CQuaternionExt *)(_ExtValue);
    assert( ext!=NULL );
    (void)_ClientData; (void)_Bar;

    // show/hide quat values
    assert( varGrp->m_Vars.count==16 );
    bool visible = ext->m_ShowVal;
    if( ext->m_IsDir )
    {
        if(    varGrp->m_Vars.items[13]->m_Visible != visible
            || varGrp->m_Vars.items[14]->m_Visible != visible
            || varGrp->m_Vars.items[15]->m_Visible != visible )
        {
            varGrp->m_Vars.items[13]->m_Visible = visible;
            varGrp->m_Vars.items[14]->m_Visible = visible;
            varGrp->m_Vars.items[15]->m_Visible = visible;
            CTwBar_NotUpToDate(_Bar);
        }
    }
    else
    {
        if(    varGrp->m_Vars.items[4]->m_Visible != visible
            || varGrp->m_Vars.items[5]->m_Visible != visible
            || varGrp->m_Vars.items[6]->m_Visible != visible
            || varGrp->m_Vars.items[7]->m_Visible != visible )
        {
            varGrp->m_Vars.items[4]->m_Visible = visible;
            varGrp->m_Vars.items[5]->m_Visible = visible;
            varGrp->m_Vars.items[6]->m_Visible = visible;
            varGrp->m_Vars.items[7]->m_Visible = visible;
            CTwBar_NotUpToDate(_Bar);
        }
    }

    // force ext update
    CTwVarAtom_ValueToDouble((CTwVarAtom *)(varGrp->m_Vars.items[4]));

    assert( s_SphTri.count>0 );
    assert( s_SphTri.count==3*s_SphCol.count );
    assert( s_SphTriProj.count==2*s_SphCol.count );
    assert( s_SphColLight.count==s_SphCol.count );

    if( QuatD(w, h)<=2 )
        return;
    float x, y, z, nx, ny, nz, kx, ky, kz, qx, qy, qz, qs;
    int i, j, k, l, m;

    // normalize quaternion
    float qn = (float)sqrt(ext->Qs*ext->Qs+ext->Qx*ext->Qx+ext->Qy*ext->Qy+ext->Qz*ext->Qz);
    if( qn>FLOAT_EPS )
    {
        qx = (float)ext->Qx/qn;
        qy = (float)ext->Qy/qn;
        qz = (float)ext->Qz/qn;
        qs = (float)ext->Qs/qn;
    }
    else
    {
        qx = qy = qz = 0;
        qs = 1;
    }

    double normDir = sqrt(ext->m_Dir[0]*ext->m_Dir[0] + ext->m_Dir[1]*ext->m_Dir[1] + ext->m_Dir[2]*ext->m_Dir[2]);
    bool drawDir = ext->m_IsDir || (normDir>DOUBLE_EPS);
    color32 alpha = ext->m_Highlighted ? 0xffffffff : 0xb0ffffff;
    
    // check if frame is right-handed
    CQuaternionExt_PermuteF(ext, &kx, &ky, &kz, 1, 0, 0);
    double px[3] = { (double)kx, (double)ky, (double)kz };
    CQuaternionExt_PermuteF(ext, &kx, &ky, &kz, 0, 1, 0);
    double py[3] = { (double)kx, (double)ky, (double)kz };
    CQuaternionExt_PermuteF(ext, &kx, &ky, &kz, 0, 0, 1);
    double pz[3] = { (double)kx, (double)ky, (double)kz };
    double ez[3];
    Vec3Cross(ez, px, py);
    bool frameRightHanded = (ez[0]*pz[0]+ez[1]*pz[1]+ez[2]*pz[2] >= 0);
    enum TwGraphCull cull = frameRightHanded ? TW_GRAPH_CULL_CW : TW_GRAPH_CULL_CCW;

    if( drawDir )
    {
        float dir[] = {(float)ext->m_Dir[0], (float)ext->m_Dir[1], (float)ext->m_Dir[2]};
        if( normDir<DOUBLE_EPS )
        {
            normDir = 1;
            dir[0] = 1;
        }
        kx = dir[0]; ky = dir[1]; kz = dir[2];
        double rotDirAxis[3] = { 0, -kz, ky };
        if( rotDirAxis[0]*rotDirAxis[0] + rotDirAxis[1]*rotDirAxis[1] + rotDirAxis[2]*rotDirAxis[2]<DOUBLE_EPS_SQ )
        {
            rotDirAxis[0] = rotDirAxis[1] = 0;
            rotDirAxis[2] = 1;
        }
        double rotDirAngle = acos(kx/normDir);
        double rotDirQuat[4];
        QuatFromAxisAngle(rotDirQuat, rotDirAxis, rotDirAngle);

        kx = 1; ky = 0; kz = 0;
        CQuaternionExt_ApplyQuat(&kx, &ky, &kz, kx, ky, kz, (float)rotDirQuat[0], (float)rotDirQuat[1], (float)rotDirQuat[2], (float)rotDirQuat[3]);
        CQuaternionExt_ApplyQuat(&kx, &ky, &kz, kx, ky, kz, qx, qy, qz, qs);
        for(k=0; k<4; ++k) // 4 parts of the arrow
        {
            // draw order
            CQuaternionExt_PermuteF(ext, &x, &y, &z, kx, ky, kz);
            j = (z>0) ? 3-k : k;

            assert( s_ArrowTriProj[j].count==2*(s_ArrowTri[j].count/3) && s_ArrowColLight[j].count==s_ArrowTri[j].count/3 && s_ArrowNorm[j].count==s_ArrowTri[j].count ); 
            const int ntri = (int)s_ArrowTri[j].count/3;
            const float *tri = s_ArrowTri[j].items;
            const float *norm = s_ArrowNorm[j].items;
            int *triProj = s_ArrowTriProj[j].items;
            color32 *colLight = s_ArrowColLight[j].items;
            for(i=0; i<ntri; ++i)
            {
                x = tri[3*i+0]; y = tri[3*i+1]; z = tri[3*i+2];
                nx = norm[3*i+0]; ny = norm[3*i+1]; nz = norm[3*i+2];
                if( x>0 )
                    x = 2.5f*x - 2.0f;
                else
                    x += 0.2f;
                y *= 1.5f;
                z *= 1.5f;
                CQuaternionExt_ApplyQuat(&x, &y, &z, x, y, z, (float)rotDirQuat[0], (float)rotDirQuat[1], (float)rotDirQuat[2], (float)rotDirQuat[3]);
                CQuaternionExt_ApplyQuat(&x, &y, &z, x, y, z, qx, qy, qz, qs);
                CQuaternionExt_PermuteF(ext, &x, &y, &z, x, y, z);
                CQuaternionExt_ApplyQuat(&nx, &ny, &nz, nx, ny, nz, (float)rotDirQuat[0], (float)rotDirQuat[1], (float)rotDirQuat[2], (float)rotDirQuat[3]);
                CQuaternionExt_ApplyQuat(&nx, &ny, &nz, nx, ny, nz, qx, qy, qz, qs);
                CQuaternionExt_PermuteF(ext, &nx, &ny, &nz, nx, ny, nz);
                triProj[2*i+0] = QuatPX(x, w, h);
                triProj[2*i+1] = QuatPY(y, w, h);
                color32 col = (ext->m_DirColor|0xff000000) & alpha;
                colLight[i] = ColorBlend(0xff000000, col, fabsf(TClampFloat(nz, -1.0f, 1.0f)));
            }
            if( s_ArrowTri[j].count>=9 ) // 1 tri = 9 floats
                g_TwMgr->m_Graph->DrawTriangles(g_TwMgr->m_Graph, (int)s_ArrowTri[j].count/9, triProj, colLight, cull);
        }
    }
    else
    {
        /*
        int px0 = QuatPX(0, w, h)-1, py0 = QuatPY(0, w, h), r0 = (int)(0.5f*QuatD(w, h)-0.5f);
        color32 col0 = 0x80000000;
        DrawArc(px0-1, py0, r0, 0, 360, col0);
        DrawArc(px0+1, py0, r0, 0, 360, col0);
        DrawArc(px0, py0-1, r0, 0, 360, col0);
        DrawArc(px0, py0+1, r0, 0, 360, col0);
        */
        // draw arrows & sphere
        const float SPH_RADIUS = 0.75f;
        for(m=0; m<2; ++m)  // m=0: back, m=1: front
        {
            for(l=0; l<3; ++l)  // draw 3 arrows
            {
                kx = 1; ky = 0; kz = 0;
                if( l==1 )
                    Vec3RotZ(&kx, &ky, &kz); 
                else if( l==2 )
                    Vec3RotY(&kx, &ky, &kz);
                CQuaternionExt_ApplyQuat(&kx, &ky, &kz, kx, ky, kz, qx, qy, qz, qs);
                for(k=0; k<4; ++k) // 4 parts of the arrow
                {
                    // draw order
                    CQuaternionExt_PermuteF(ext, &x, &y, &z, kx, ky, kz);
                    j = (z>0) ? 3-k : k;

                    bool cone = true;
                    if( (m==0 && z>0) || (m==1 && z<=0) )
                    {
                        if( j==ARROW_CONE || j==ARROW_CONE_CAP ) // do not draw cone
                            continue;
                        else
                            cone = false;
                    }
                    assert( s_ArrowTriProj[j].count==2*(s_ArrowTri[j].count/3) && s_ArrowColLight[j].count==s_ArrowTri[j].count/3 && s_ArrowNorm[j].count==s_ArrowTri[j].count ); 
                    const int ntri = (int)s_ArrowTri[j].count/3;
                    const float *tri = s_ArrowTri[j].items;
                    const float *norm = s_ArrowNorm[j].items;
                    int *triProj = s_ArrowTriProj[j].items;
                    color32 *colLight = s_ArrowColLight[j].items;
                    for(i=0; i<ntri; ++i)
                    {
                        x = tri[3*i+0]; y = tri[3*i+1]; z = tri[3*i+2];
                        if( cone && x<=0 )
                            x = SPH_RADIUS;
                        else if( !cone && x>0 )
                            x = -SPH_RADIUS;
                        nx = norm[3*i+0]; ny = norm[3*i+1]; nz = norm[3*i+2];
                        if( l==1 )
                        {
                            Vec3RotZ(&x, &y, &z); 
                            Vec3RotZ(&nx, &ny, &nz); 
                        }
                        else if( l==2 )
                        {
                            Vec3RotY(&x, &y, &z);
                            Vec3RotY(&nx, &ny, &nz);
                        }
                        CQuaternionExt_ApplyQuat(&x, &y, &z, x, y, z, qx, qy, qz, qs);
                        CQuaternionExt_PermuteF(ext, &x, &y, &z, x, y, z);
                        CQuaternionExt_ApplyQuat(&nx, &ny, &nz, nx, ny, nz, qx, qy, qz, qs);
                        CQuaternionExt_PermuteF(ext, &nx, &ny, &nz, nx, ny, nz);
                        triProj[2*i+0] = QuatPX(x, w, h);
                        triProj[2*i+1] = QuatPY(y, w, h);
                        float fade = ( m==0 && z<0 ) ? TClampFloat(2.0f*z*z, 0.0f, 1.0f) : 0;
                        float alphaFade = 1.0f;
                        Color32ToARGBf(alpha, &alphaFade, NULL, NULL, NULL);
                        alphaFade *= (1.0f-fade);
                        color32 alphaFadeCol = Color32FromARGBf(alphaFade, 1, 1, 1);
                        color32 col = (l==0) ? 0xffff0000 : ( (l==1) ? 0xff00ff00 : 0xff0000ff );
                        colLight[i] = ColorBlend(0xff000000, col, fabsf(TClampFloat(nz, -1.0f, 1.0f))) & alphaFadeCol;
                    }
                    if( s_ArrowTri[j].count>=9 ) // 1 tri = 9 floats
                        g_TwMgr->m_Graph->DrawTriangles(g_TwMgr->m_Graph, (int)s_ArrowTri[j].count/9, triProj, colLight, cull);
                }
            }

            if( m==0 )
            {
                const float *tri = s_SphTri.items;
                int *triProj = s_SphTriProj.items;
                const color32 *col = s_SphCol.items;
                color32 *colLight = s_SphColLight.items;
                const int ntri = (int)s_SphTri.count/3;
                for(i=0; i<ntri; ++i)   // draw sphere
                {
                    x = SPH_RADIUS*tri[3*i+0]; y = SPH_RADIUS*tri[3*i+1]; z = SPH_RADIUS*tri[3*i+2];
                    CQuaternionExt_ApplyQuat(&x, &y, &z, x, y, z, qx, qy, qz, qs);
                    CQuaternionExt_PermuteF(ext, &x, &y, &z, x, y, z);
                    triProj[2*i+0] = QuatPX(x, w, h);
                    triProj[2*i+1] = QuatPY(y, w, h);
                    colLight[i] = ColorBlend(0xff000000, col[i], fabsf(TClampFloat(z/SPH_RADIUS, -1.0f, 1.0f))) & alpha;
                }
                g_TwMgr->m_Graph->DrawTriangles(g_TwMgr->m_Graph, (int)s_SphTri.count/9, triProj, colLight, cull);
            }
        }

        // draw x
        g_TwMgr->m_Graph->DrawLine(g_TwMgr->m_Graph, w-12, h-36, w-12+5, h-36+5, 0xffc00000, 0xffc00000, true);
        g_TwMgr->m_Graph->DrawLine(g_TwMgr->m_Graph, w-12+5, h-36, w-12, h-36+5, 0xffc00000, 0xffc00000, true);
        // draw y
        g_TwMgr->m_Graph->DrawLine(g_TwMgr->m_Graph, w-12, h-25, w-12+3, h-25+4, 0xff00c000, 0xff00c000, true);
        g_TwMgr->m_Graph->DrawLine(g_TwMgr->m_Graph, w-12+5, h-25, w-12, h-25+7, 0xff00c000, 0xff00c000, true);
        // draw z
        g_TwMgr->m_Graph->DrawLine(g_TwMgr->m_Graph, w-12, h-12, w-12+5, h-12, 0xff0000c0, 0xff0000c0, true);
        g_TwMgr->m_Graph->DrawLine(g_TwMgr->m_Graph, w-12, h-12+5, w-12+5, h-12+5, 0xff0000c0, 0xff0000c0, true);
        g_TwMgr->m_Graph->DrawLine(g_TwMgr->m_Graph, w-12, h-12+5, w-12+5, h-12, 0xff0000c0, 0xff0000c0, true);
    }

    // draw borders
    g_TwMgr->m_Graph->DrawLine(g_TwMgr->m_Graph, 1, 0, w-1, 0, 0x40000000, 0x40000000, false);
    g_TwMgr->m_Graph->DrawLine(g_TwMgr->m_Graph, w-1, 0, w-1, h-1, 0x40000000, 0x40000000, false);
    g_TwMgr->m_Graph->DrawLine(g_TwMgr->m_Graph, w-1, h-1, 1, h-1, 0x40000000, 0x40000000, false);
    g_TwMgr->m_Graph->DrawLine(g_TwMgr->m_Graph, 1, h-1, 1, 0, 0x40000000, 0x40000000, false);
}

bool CQuaternionExt_MouseMotionCB(int mouseX, int mouseY, int w, int h, void *structExtValue, void *clientData, TwBar *bar, CTwVarGroup *varGrp)
{
    CQuaternionExt *ext = (CQuaternionExt *)(structExtValue);
    if( ext==NULL )
        return false;
    (void)clientData, (void)varGrp;

    if( mouseX>0 && mouseX<w && mouseY>0 && mouseY<h )
        ext->m_Highlighted = true;

    if( ext->m_Rotating )
    {
        double x = QuatIX(mouseX, w, h);
        double y = QuatIY(mouseY, w, h);
        double z = 1;
        double px, py, pz, ox, oy, oz;
        CQuaternionExt_PermuteInvD(ext, &px, &py, &pz, x, y, z);
        CQuaternionExt_PermuteInvD(ext, &ox, &oy, &oz, ext->m_OrigX, ext->m_OrigY, 1);
        double n0 = sqrt(ox*ox + oy*oy + oz*oz);
        double n1 = sqrt(px*px + py*py + pz*pz);
        if( n0>DOUBLE_EPS && n1>DOUBLE_EPS )
        {
            double v0[] = { ox/n0, oy/n0, oz/n0 };
            double v1[] = { px/n1, py/n1, pz/n1 };
            double axis[3];
            Vec3Cross(axis, v0, v1);
            double sa = sqrt(Vec3Dot(axis, axis));
            double ca = Vec3Dot(v0, v1);
            double angle = atan2(sa, ca);
            if( x*x+y*y>1.0 )
                angle *= 1.0 + 0.2f*(sqrt(x*x+y*y)-1.0);
            double qrot[4], qres[4], qorig[4];
            QuatFromAxisAngle(qrot, axis, angle);
            double nqorig = sqrt(ext->m_OrigQuat[0]*ext->m_OrigQuat[0]+ext->m_OrigQuat[1]*ext->m_OrigQuat[1]+ext->m_OrigQuat[2]*ext->m_OrigQuat[2]+ext->m_OrigQuat[3]*ext->m_OrigQuat[3]);
            if( fabs(nqorig)>DOUBLE_EPS_SQ )
            {
                qorig[0] = ext->m_OrigQuat[0]/nqorig;
                qorig[1] = ext->m_OrigQuat[1]/nqorig;
                qorig[2] = ext->m_OrigQuat[2]/nqorig;
                qorig[3] = ext->m_OrigQuat[3]/nqorig;
                QuatMult(qres, qrot, qorig);
                ext->Qx = qres[0];
                ext->Qy = qres[1];
                ext->Qz = qres[2];
                ext->Qs = qres[3];
            }
            else
            {
                ext->Qx = qrot[0];
                ext->Qy = qrot[1];
                ext->Qz = qrot[2];
                ext->Qs = qrot[3];
            }
            CQuaternionExt_CopyToVar(ext);
            if( bar!=NULL )
                CTwBar_NotUpToDate(bar);

            ext->m_PrevX = x;
            ext->m_PrevY = y;
        }
    }

    return true;
}

bool CQuaternionExt_MouseButtonCB(TwMouseButtonID button, bool pressed, int mouseX, int mouseY, int w, int h, void *structExtValue, void *clientData, TwBar *bar, CTwVarGroup *varGrp)
{
    CQuaternionExt *ext = (CQuaternionExt *)(structExtValue);
    if( ext==NULL )
        return false;
    (void)clientData; (void)bar, (void)varGrp;

    if( button==TW_MOUSE_LEFT )
    {
        if( pressed )
        {
            ext->m_OrigQuat[0] = ext->Qx;
            ext->m_OrigQuat[1] = ext->Qy;
            ext->m_OrigQuat[2] = ext->Qz;
            ext->m_OrigQuat[3] = ext->Qs;
            ext->m_OrigX = QuatIX(mouseX, w, h);
            ext->m_OrigY = QuatIY(mouseY, w, h);
            ext->m_PrevX = ext->m_OrigX;
            ext->m_PrevY = ext->m_OrigY;
            ext->m_Rotating = true;
        }
        else
            ext->m_Rotating = false;
    }

    //printf("Click %x\n", structExtValue);
    return true;
}

void CQuaternionExt_MouseLeaveCB(void *structExtValue, void *clientData, TwBar *bar)
{
    CQuaternionExt *ext = (CQuaternionExt *)(structExtValue);
    if( ext==NULL )
        return;
    (void)clientData; (void)bar;

    //printf("Leave %x\n", structExtValue);
    ext->m_Highlighted = false;
    ext->m_Rotating = false;
}


//  ---------------------------------------------------------------------------
//  Management functions
//  ---------------------------------------------------------------------------


static int TwCreateGraph(ETwGraphAPI _GraphAPI)
{
    assert( g_TwMgr!=NULL && g_TwMgr->m_Graph==NULL );

    switch( _GraphAPI )
    {
    case TW_OPENGL:
        g_TwMgr->m_Graph = TwGraphOpenGL_Create();
        break;
    case TW_OPENGL_CORE:
        g_TwMgr->m_Graph = TwGraphOpenGLCore_Create();
        break;
    case TW_DIRECT3D9:
        // TW_NO_DIRECT3D: this build (see nob.c) targets the OpenGL backend
        // only (README.md) and doesn't compile/link TwDirect3D9/10/11.cpp -
        // without this guard, ANT_WINDOWS alone is enough for this
        // unconditional "new CTwGraphDirect3D9" to require those classes'
        // vtables at link time even though no example ever requests
        // TW_DIRECT3D9. TwInit() falls through to the "unknown API" error
        // below instead, same as any other unsupported ETwGraphAPI value.
        #if defined(ANT_WINDOWS) && !defined(TW_NO_DIRECT3D)
            if( g_TwMgr->m_Device!=NULL )
                g_TwMgr->m_Graph = new CTwGraphDirect3D9;
            else
            {
                CTwMgr_SetLastError(g_TwMgr, g_ErrBadDevice);
                return 0;
            }
        #endif // ANT_WINDOWS && !TW_NO_DIRECT3D
        break;
    case TW_DIRECT3D10:
        #if defined(ANT_WINDOWS) && !defined(TW_NO_DIRECT3D)
            if( g_TwMgr->m_Device!=NULL )
                g_TwMgr->m_Graph = new CTwGraphDirect3D10;
            else
            {
                CTwMgr_SetLastError(g_TwMgr, g_ErrBadDevice);
                return 0;
            }
        #endif // ANT_WINDOWS && !TW_NO_DIRECT3D
        break;
    case TW_DIRECT3D11:
        #if defined(ANT_WINDOWS) && !defined(TW_NO_DIRECT3D)
            if( g_TwMgr->m_Device!=NULL )
                g_TwMgr->m_Graph = new CTwGraphDirect3D11;
            else
            {
                CTwMgr_SetLastError(g_TwMgr, g_ErrBadDevice);
                return 0;
            }
        #endif // ANT_WINDOWS && !TW_NO_DIRECT3D
        break;
    }

    if( g_TwMgr->m_Graph==NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrUnknownAPI);
        return 0;
    }
    else
        return g_TwMgr->m_Graph->Init(g_TwMgr->m_Graph);
}

//  ---------------------------------------------------------------------------

static inline int TwFreeAsyncDrawing(void)
{
    if( g_TwMgr && g_TwMgr->m_Graph && g_TwMgr->m_Graph->IsDrawing(g_TwMgr->m_Graph) )
    {
        const double SLEEP_MAX = 0.25; // wait at most 1/4 second
        double startTime = glfwGetTime();
        while( g_TwMgr->m_Graph->IsDrawing(g_TwMgr->m_Graph) && glfwGetTime()-startTime<SLEEP_MAX )
        {
            #if defined(ANT_WINDOWS)
                Sleep(1); // milliseconds
            #elif defined(ANT_UNIX) || defined(ANT_OSX)
                usleep(1000); // microseconds
            #endif
        }
        if( g_TwMgr->m_Graph->IsDrawing(g_TwMgr->m_Graph) )
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrIsDrawing);
            return 0;
        }
    }
    return 1;
}

//  ---------------------------------------------------------------------------

/*
static inline int TwFreeAsyncProcessing()
{
    if( g_TwMgr && g_TwMgr->IsProcessing() )
    {
        const double SLEEP_MAX = 0.25; // wait at most 1/4 second
        PerfTimer timer;
        while( g_TwMgr->IsProcessing() && timer.GetTime()<SLEEP_MAX )
        {
            #if defined(ANT_WINDOWS)
                Sleep(1); // milliseconds
            #elif defined(ANT_UNIX) 
                usleep(1000); // microseconds
            #endif
        }
        if( g_TwMgr->IsProcessing() )
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrIsProcessing);
            return 0;
        }
    }
    return 1;
}

static inline int TwBeginProcessing()
{
    if( !TwFreeAsyncProcessing() )
        return 0;
    if( g_TwMgr )
        g_TwMgr->SetProcessing(true);
}

static inline int TwEndProcessing()
{
    if( g_TwMgr )
        g_TwMgr->SetProcessing(false);
}
*/

//  ---------------------------------------------------------------------------

static int TwInitMgr(void)
{
    assert( g_TwMasterMgr!=NULL );
    assert( g_TwMgr!=NULL );

    g_TwMgr->m_CurrentFont = g_DefaultNormalFont;
    g_TwMgr->m_Graph = g_TwMasterMgr->m_Graph;

    g_TwMgr->m_KeyPressedTextObj = g_TwMgr->m_Graph->NewTextObj(g_TwMgr->m_Graph);
    g_TwMgr->m_InfoTextObj = g_TwMgr->m_Graph->NewTextObj(g_TwMgr->m_Graph);

    g_TwMgr->m_HelpBar = TwNewBar("TW_HELP");
    if( g_TwMgr->m_HelpBar )
    {
        g_TwMgr->m_HelpBar->m_Label = sdscpy(g_TwMgr->m_HelpBar->m_Label, "~ Help & Shortcuts ~");
        g_TwMgr->m_HelpBar->m_PosX = 32;
        g_TwMgr->m_HelpBar->m_PosY = 32;
        g_TwMgr->m_HelpBar->m_Width = 400;
        g_TwMgr->m_HelpBar->m_Height = 200;
        g_TwMgr->m_HelpBar->m_ValuesWidth = 12*(g_TwMgr->m_HelpBar->m_Font->m_CharHeight/2);
        g_TwMgr->m_HelpBar->m_Color = 0xa05f5f5f; //0xd75f5f5f;
        g_TwMgr->m_HelpBar->m_DarkText = false;
        g_TwMgr->m_HelpBar->m_IsHelpBar = true;
        CTwMgr_Minimize(g_TwMgr, g_TwMgr->m_HelpBar);
    }
    else
        return 0;

    CColorExt_CreateTypes();
    CQuaternionExt_CreateTypes();

    return 1;
}


int ANT_CALL TwInit(ETwGraphAPI _GraphAPI, void *_Device)
{
#if defined(_DEBUG) && defined(ANT_WINDOWS)
    _CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF|_CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF));
#endif

    if( g_TwMasterMgr!=NULL )
    {
        CTwMgr_SetLastError(g_TwMasterMgr, g_ErrInit);
        return 0;
    }
    assert( g_TwMgr==0 );
    assert( g_Wnds.count==0 );

    g_TwMasterMgr = CTwMgr_Create(_GraphAPI, _Device, TW_MASTER_WINDOW_ID);
    CTwWndArray_Set(TW_MASTER_WINDOW_ID, g_TwMasterMgr);
    g_TwMgr = g_TwMasterMgr;

    TwGenerateDefaultFonts(g_FontScaling);
    g_TwMgr->m_CurrentFont = g_DefaultNormalFont;

    int Res = TwCreateGraph(_GraphAPI);
    if( Res )
        Res = TwInitMgr();
    
    if( !Res )
        TwTerminate();

    return Res;
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwSetLastError(const char *_StaticErrorMessage)
{
    if( g_TwMasterMgr!=0 )
    {
        CTwMgr_SetLastError(g_TwMasterMgr, _StaticErrorMessage);
        return 1;
    }
    else
        return 0;
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwMgrGetGraphAPI(void)
{
    return g_TwMgr!=NULL ? (int)g_TwMgr->m_GraphAPI : -1;
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwTerminate(void)
{
    if( g_TwMgr==NULL )
    {
        //TwGlobalError(g_ErrShut); -> not an error
        return 0;  // already shutdown
    }

    // For multi-thread safety
    if( !TwFreeAsyncDrawing() )
        return 0;

    for( size_t wi=0; wi<g_Wnds.count; ++wi )
    {
        g_TwMgr = g_Wnds.items[wi].Mgr;

        g_TwMgr->m_Terminating = true;
        TwDeleteAllBars();

        if( g_TwMgr->m_Graph )
        {
            if( g_TwMgr->m_KeyPressedTextObj )
            {
                g_TwMgr->m_Graph->DeleteTextObj(g_TwMgr->m_Graph, g_TwMgr->m_KeyPressedTextObj);
                g_TwMgr->m_KeyPressedTextObj = NULL;
            }
            if( g_TwMgr->m_InfoTextObj )
            {
                g_TwMgr->m_Graph->DeleteTextObj(g_TwMgr->m_Graph, g_TwMgr->m_InfoTextObj);
                g_TwMgr->m_InfoTextObj = NULL;
            }
            if (g_TwMgr != g_TwMasterMgr)
                g_TwMgr->m_Graph = NULL;
        }

        if (g_TwMgr != g_TwMasterMgr) 
        {
            CTwMgr_Destroy(g_TwMgr);
            g_TwMgr = NULL;
        }
    }

    // delete g_TwMasterMgr
    int Res = 1;
    g_TwMgr = g_TwMasterMgr;
    if( g_TwMasterMgr->m_Graph )
    {
        Res = g_TwMasterMgr->m_Graph->Shut(g_TwMasterMgr->m_Graph);
        TwGraph_Destroy(g_TwMasterMgr->m_Graph);
        g_TwMasterMgr->m_Graph = NULL;
    }
    TwDeleteDefaultFonts();
    CTwMgr_Destroy(g_TwMasterMgr);
    g_TwMasterMgr = NULL;
    g_TwMgr = NULL;
    g_Wnds.count = 0;

    return Res;
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwGetCurrentWindow(void)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return 0; // not initialized
    }

    return g_TwMgr->m_WndID;
}

int ANT_CALL TwSetCurrentWindow(int wndID)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return 0; // not initialized
    }

    if (wndID != g_TwMgr->m_WndID)
    {
        CTwMgr *foundWnd = CTwWndArray_Find(wndID);
        if (foundWnd == NULL)
        {
            // create a new CTwMgr
            g_TwMgr = CTwMgr_Create(g_TwMasterMgr->m_GraphAPI, g_TwMasterMgr->m_Device, wndID);
            CTwWndArray_Set(wndID, g_TwMgr);
            return TwInitMgr();
        }
        else
        {
            g_TwMgr = foundWnd;
            return 1;
        }
    }
    else
        return 1;
}

int ANT_CALL TwWindowExists(int wndID)
{
    CTwMgr *foundWnd = CTwWndArray_Find(wndID);
    if (foundWnd == NULL)
        return 0;
    else
        return 1;
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwDraw(void)
{
    //CTwFPU fpu;   // fpu precision only forced in update (do not modif dx draw calls)

    if( g_TwMgr==NULL || g_TwMgr->m_Graph==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return 0; // not initialized
    }

    assert(g_TwMgr->m_Bars.count==g_TwMgr->m_Order.count);

    // For multi-thread savety
    if( !TwFreeAsyncDrawing() )
        return 0;

    // Autorepeat TW_MOUSE_PRESSED
    double CurrTime = glfwGetTime();
    double RepeatDT = CurrTime - g_TwMgr->m_LastMousePressedTime;
    double DrawDT = CurrTime - g_TwMgr->m_LastDrawTime;
    if(    RepeatDT>2.0*g_TwMgr->m_RepeatMousePressedDelay 
        || DrawDT>2.0*g_TwMgr->m_RepeatMousePressedDelay 
        || abs(g_TwMgr->m_LastMousePressedPosition[0]-g_TwMgr->m_LastMouseX)>4
        || abs(g_TwMgr->m_LastMousePressedPosition[1]-g_TwMgr->m_LastMouseY)>4 )
    {
        g_TwMgr->m_CanRepeatMousePressed = false;
        g_TwMgr->m_IsRepeatingMousePressed = false;
    }
    if( g_TwMgr->m_CanRepeatMousePressed )
    {
        if(    (!g_TwMgr->m_IsRepeatingMousePressed && RepeatDT>g_TwMgr->m_RepeatMousePressedDelay)
            || (g_TwMgr->m_IsRepeatingMousePressed && RepeatDT>g_TwMgr->m_RepeatMousePressedPeriod) )
        {
            g_TwMgr->m_IsRepeatingMousePressed = true;
            g_TwMgr->m_LastMousePressedTime = glfwGetTime();
            TwMouseMotion(g_TwMgr->m_LastMouseX,g_TwMgr->m_LastMouseY);
            TwMouseButton(TW_MOUSE_PRESSED, g_TwMgr->m_LastMousePressedButtonID);
        }
    }
    g_TwMgr->m_LastDrawTime = CurrTime;

    if( g_TwMgr->m_WndWidth<0 || g_TwMgr->m_WndHeight<0 )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadSize);
        return 0;
    }
    else if( g_TwMgr->m_WndWidth==0 || g_TwMgr->m_WndHeight==0 )    // probably iconified
        return 1;   // nothing to do

    // count number of bars to draw
    size_t i, j;
    int Nb = 0;
    for( i=0; i<g_TwMgr->m_Bars.count; ++i )
        if( g_TwMgr->m_Bars.items[i]!=NULL && g_TwMgr->m_Bars.items[i]->m_Visible )
            ++Nb;

    if( Nb>0 )
    {
        g_TwMgr->m_Graph->BeginDraw(g_TwMgr->m_Graph, g_TwMgr->m_WndWidth, g_TwMgr->m_WndHeight);

        CRectArray TopBarsRects = {0}, ClippedBarRects = {0};
        for( i=0; i<g_TwMgr->m_Bars.count; ++i )
        {
            CTwBar *Bar = g_TwMgr->m_Bars.items[ g_TwMgr->m_Order.items[i] ];
            if( Bar->m_Visible )
            {
                if( g_TwMgr->m_OverlapContent || CTwBar_IsMinimized(Bar) )
                    CTwBar_Draw(Bar, DRAW_ALL);
                else
                {
                    // Clip overlapped transparent bars to make them more readable
                    const int Margin = 4;
                    CRect BarRect = CRect_Make(Bar->m_PosX - Margin, Bar->m_PosY - Margin, Bar->m_Width + 2*Margin, Bar->m_Height + 2*Margin);
                    TopBarsRects.count = 0;
                    for( j=i+1; j<g_TwMgr->m_Bars.count; ++j )
                    {
                        CTwBar *TopBar = g_TwMgr->m_Bars.items[g_TwMgr->m_Order.items[j]];
                        if( TopBar->m_Visible && !CTwBar_IsMinimized(TopBar) )
                            tw_da_append(&TopBarsRects, CRect_Make(TopBar->m_PosX, TopBar->m_PosY, TopBar->m_Width, TopBar->m_Height));
                    }
                    ClippedBarRects.count = 0;
                    CRect_SubtractMany(&BarRect, &TopBarsRects, &ClippedBarRects);

                    if( ClippedBarRects.count==1 && CRect_Equal(&ClippedBarRects.items[0], &BarRect) )
                        //g_TwMgr->m_Graph->DrawRect(g_TwMgr->m_Graph, Bar->m_PosX, Bar->m_PosY, Bar->m_PosX+Bar->m_Width-1, Bar->m_PosY+Bar->m_Height-1, 0x70ffffff, 0x70ffffff, 0x70ffffff, 0x70ffffff); // Clipping test
                        CTwBar_Draw(Bar, DRAW_ALL); // unclipped
                    else
                    {
                        CTwBar_Draw(Bar, DRAW_BG); // draw background only

                        // draw content for each clipped rectangle
                        for( j=0; j<ClippedBarRects.count; j++ )
                            if (ClippedBarRects.items[j].W>1 && ClippedBarRects.items[j].H>1)
                            {
                                g_TwMgr->m_Graph->SetScissor(g_TwMgr->m_Graph, ClippedBarRects.items[j].X+1, ClippedBarRects.items[j].Y, ClippedBarRects.items[j].W, ClippedBarRects.items[j].H-1);
                                //g_TwMgr->m_Graph->DrawRect(g_TwMgr->m_Graph, 0, 0, 1000, 1000, 0x70ffffff, 0x70ffffff, 0x70ffffff, 0x70ffffff); // Clipping test
                                CTwBar_Draw(Bar, DRAW_CONTENT);
                            }
                        g_TwMgr->m_Graph->SetScissor(g_TwMgr->m_Graph, 0, 0, 0, 0);
                    }
                }
            }
        }
        tw_da_free(&TopBarsRects);
        tw_da_free(&ClippedBarRects);

        g_TwMgr->m_Graph->EndDraw(g_TwMgr->m_Graph);
    }

    return 1;
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwWindowSize(int _Width, int _Height)
{
    g_InitWndWidth = _Width;
    g_InitWndHeight = _Height;

    if( g_TwMgr==NULL || g_TwMgr->m_Graph==NULL )
    {
        //TwGlobalError(g_ErrNotInit);  -> not an error here
        return 0;  // not initialized
    }

    if( _Width<0 || _Height<0 )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadSize);
        return 0;
    }

    // For multi-thread savety
    if( !TwFreeAsyncDrawing() )
        return 0;

    // Delete the extra text objects
    if( g_TwMgr->m_KeyPressedTextObj )
    {
        g_TwMgr->m_Graph->DeleteTextObj(g_TwMgr->m_Graph, g_TwMgr->m_KeyPressedTextObj);
        g_TwMgr->m_KeyPressedTextObj = NULL;
    }
    if( g_TwMgr->m_InfoTextObj )
    {
        g_TwMgr->m_Graph->DeleteTextObj(g_TwMgr->m_Graph, g_TwMgr->m_InfoTextObj);
        g_TwMgr->m_InfoTextObj = NULL;
    }

    g_TwMgr->m_WndWidth = _Width;
    g_TwMgr->m_WndHeight = _Height;
    g_TwMgr->m_Graph->Restore(g_TwMgr->m_Graph);

    // Recreate extra text objects
    if( g_TwMgr->m_WndWidth!=0 && g_TwMgr->m_WndHeight!=0 )
    {
        if( g_TwMgr->m_KeyPressedTextObj==NULL )
        {
            g_TwMgr->m_KeyPressedTextObj = g_TwMgr->m_Graph->NewTextObj(g_TwMgr->m_Graph);
            g_TwMgr->m_KeyPressedBuildText = true;
        }
        if( g_TwMgr->m_InfoTextObj==NULL )
        {
            g_TwMgr->m_InfoTextObj = g_TwMgr->m_Graph->NewTextObj(g_TwMgr->m_Graph);
            g_TwMgr->m_InfoBuildText = true;
        }
    }

    for( size_t bi=0; bi<g_TwMgr->m_Bars.count; ++bi )
        CTwBar_NotUpToDate(g_TwMgr->m_Bars.items[bi]);
    
    return 1;
}

//  ---------------------------------------------------------------------------

CTwMgr *CTwMgr_Create(ETwGraphAPI _GraphAPI, void *_Device, int _WndID)
{
    CTwMgr *Mgr = (CTwMgr *)malloc(sizeof(CTwMgr));

    Mgr->m_GraphAPI = _GraphAPI;
    Mgr->m_Device = _Device;
    Mgr->m_WndID = _WndID;
    Mgr->m_LastError = NULL;
    Mgr->m_CurrentDbgFile = "";
    Mgr->m_CurrentDbgLine = 0;
    //Mgr->m_Processing = false;
    Mgr->m_Graph = NULL;
    Mgr->m_WndWidth = g_InitWndWidth;
    Mgr->m_WndHeight = g_InitWndHeight;
    Mgr->m_CurrentFont = NULL;   // set after by TwIntialize
    Mgr->m_NbMinimizedBars = 0;
    Mgr->m_HelpBar = NULL;
    Mgr->m_HelpBarNotUpToDate = true;
    Mgr->m_HelpBarUpdateNow = false;
    Mgr->m_LastHelpUpdateTime = 0;
    Mgr->m_LastMouseX = -1;
    Mgr->m_LastMouseY = -1;
    Mgr->m_LastMouseWheelPos = 0;
    Mgr->m_IconPos = 0;
    Mgr->m_IconAlign = 0;
    Mgr->m_IconMarginX = Mgr->m_IconMarginY = 8;
    Mgr->m_FontResizable = true;
    // m_BarAlwaysOnTop/m_BarAlwaysOnBottom/m_Help/m_KeyPressedStr no longer
    // have a std::string default constructor to rely on - explicit
    // sdsempty(), freed in CTwMgr_Destroy.
    Mgr->m_BarAlwaysOnTop = sdsempty();
    Mgr->m_BarAlwaysOnBottom = sdsempty();
    Mgr->m_Help = sdsempty();
    Mgr->m_KeyPressedStr = sdsempty();
    Mgr->m_KeyPressedTextObj = NULL;
    Mgr->m_KeyPressedBuildText = false;
    Mgr->m_KeyPressedTime = 0;
    Mgr->m_InfoTextObj = NULL;
    Mgr->m_InfoBuildText = true;
    Mgr->m_BarInitColorHue = 155;
    Mgr->m_PopupBar = NULL;
    Mgr->m_TypeColor32 = TW_TYPE_UNDEF;
    Mgr->m_TypeColor3F = TW_TYPE_UNDEF;
    Mgr->m_TypeColor4F = TW_TYPE_UNDEF;
    Mgr->m_LastMousePressedTime = 0;
    Mgr->m_LastMousePressedButtonID = TW_MOUSE_MIDDLE;
    Mgr->m_LastMousePressedPosition[0] = -1000;
    Mgr->m_LastMousePressedPosition[1] = -1000;
    Mgr->m_RepeatMousePressedDelay = 0.5;
    Mgr->m_RepeatMousePressedPeriod = 0.1;
    Mgr->m_CanRepeatMousePressed = false;
    Mgr->m_IsRepeatingMousePressed = false;
    Mgr->m_LastDrawTime = 0;
    Mgr->m_UseOldColorScheme = false;
    Mgr->m_Contained = false;
    Mgr->m_ButtonAlign = BUTTON_ALIGN_RIGHT;
    Mgr->m_OverlapContent = false;
    Mgr->m_Terminating = false;
    Mgr->m_NbCustoms = 0;

    // Mgr->m_Bars/Mgr->m_Order/Mgr->m_MinOccupied no longer have a std::vector default
    // constructor to rely on - explicit zero-init, freed in CTwMgr_Destroy.
    Mgr->m_Bars.items = NULL;
    Mgr->m_Bars.count = 0;
    Mgr->m_Bars.capacity = 0;
    Mgr->m_Order.items = NULL;
    Mgr->m_Order.count = 0;
    Mgr->m_Order.capacity = 0;
    Mgr->m_MinOccupied.items = NULL;
    Mgr->m_MinOccupied.count = 0;
    Mgr->m_MinOccupied.capacity = 0;

    // Mgr->m_CSStringBuffer likewise no longer has a std::vector<char> default
    // constructor to rely on.
    Mgr->m_CSStringBuffer.items = NULL;
    Mgr->m_CSStringBuffer.count = 0;
    Mgr->m_CSStringBuffer.capacity = 0;

    // m_CDStdStringCopyBuffers likewise no longer has a std::map default
    // constructor to rely on.
    Mgr->m_CDStdStringCopyBuffers.items = NULL;
    Mgr->m_CDStdStringCopyBuffers.count = 0;
    Mgr->m_CDStdStringCopyBuffers.capacity = 0;

    // m_Enums likewise no longer has a std::vector<CEnum> default
    // constructor to rely on.
    Mgr->m_Enums.items = NULL;
    Mgr->m_Enums.count = 0;
    Mgr->m_Enums.capacity = 0;

    // m_StructProxies/m_MemberProxies likewise no longer have a std::list
    // default constructor to rely on - both start as empty lists (NULL
    // head), freed node-by-node in CTwMgr_Destroy.
    Mgr->m_StructProxies = NULL;
    Mgr->m_MemberProxies = NULL;

    Mgr->m_CopyCDStringToClient = g_InitCopyCDStringToClient;

    return Mgr;
}

//  ---------------------------------------------------------------------------

void CTwMgr_Destroy(CTwMgr *_Mgr)
{
    tw_da_free(&_Mgr->m_Bars);
    tw_da_free(&_Mgr->m_Order);
    tw_da_free(&_Mgr->m_MinOccupied);
    tw_da_free(&_Mgr->m_CSStringBuffer);
    for( size_t i=0; i<_Mgr->m_CDStdStringCopyBuffers.count; ++i )
        tw_da_free(&_Mgr->m_CDStdStringCopyBuffers.items[i].Value);
    tw_da_free(&_Mgr->m_CDStdStringCopyBuffers);
    for( size_t i=0; i<_Mgr->m_Enums.count; ++i )
    {
        CEnum_Clear(&_Mgr->m_Enums.items[i]);
        tw_da_free(&_Mgr->m_Enums.items[i].m_Entries);
        sdsfree(_Mgr->m_Enums.items[i].m_Name);
    }
    tw_da_free(&_Mgr->m_Enums);
    for( size_t i=0; i<_Mgr->m_Structs.count; ++i )
    {
        CStruct *s = &_Mgr->m_Structs.items[i];
        for( size_t j=0; j<s->m_Members.count; ++j )
        {
            sdsfree(s->m_Members.items[j].m_Name);
            sdsfree(s->m_Members.items[j].m_Label);
            sdsfree(s->m_Members.items[j].m_DefString);
            sdsfree(s->m_Members.items[j].m_Help);
        }
        tw_da_free(&s->m_Members);
        sdsfree(s->m_Name);
        sdsfree(s->m_Help);
    }
    tw_da_free(&_Mgr->m_Structs);
    // m_StructProxies/m_MemberProxies no longer have a std::list to rely on
    // for automatic per-node destruction - walk each singly-linked list,
    // freeing every node's owned resources then the node itself, reading
    // Next before freeing (not after).
    {
        CStructProxyNode *node = _Mgr->m_StructProxies;
        while( node!=NULL )
        {
            CStructProxyNode *next = node->Next;
            CStructProxy_Free(&node->Proxy);
            free(node);
            node = next;
        }
        _Mgr->m_StructProxies = NULL;
    }
    {
        CMemberProxyNode *node = _Mgr->m_MemberProxies;
        while( node!=NULL )
        {
            CMemberProxyNode *next = node->Next;
            CMemberProxy_Free(&node->Proxy);
            free(node);
            node = next;
        }
        _Mgr->m_MemberProxies = NULL;
    }
    sdsfree(_Mgr->m_BarAlwaysOnTop);
    sdsfree(_Mgr->m_BarAlwaysOnBottom);
    sdsfree(_Mgr->m_Help);
    sdsfree(_Mgr->m_KeyPressedStr);

    free(_Mgr);
}

//  ---------------------------------------------------------------------------

int CTwMgr_FindBar(const CTwMgr *_Mgr, const char *_Name)
{
    if( _Name==NULL || strlen(_Name)<=0 )
        return -1;
    int i;
    for( i=0; i<(int)_Mgr->m_Bars.count; ++i )
        if( _Mgr->m_Bars.items[i]!=NULL && strcmp(_Name, _Mgr->m_Bars.items[i]->m_Name)==0 )
            return i;
    return -1;
}


//  ---------------------------------------------------------------------------

int CTwMgr_HasAttrib(const CTwMgr *_Mgr, const char *_Attrib, bool *_HasValue)
{
    (void)_Mgr;
    *_HasValue = true;
    if( _stricmp(_Attrib, "help")==0 )
        return MGR_HELP;
    else if( _stricmp(_Attrib, "fontsize")==0 )
        return MGR_FONT_SIZE;
    else if( _stricmp(_Attrib, "fontstyle")==0 )
        return MGR_FONT_STYLE;
    else if( _stricmp(_Attrib, "iconpos")==0 )
        return MGR_ICON_POS;
    else if( _stricmp(_Attrib, "iconalign")==0 )
        return MGR_ICON_ALIGN;
    else if( _stricmp(_Attrib, "iconmargin")==0 )
        return MGR_ICON_MARGIN;
    else if( _stricmp(_Attrib, "fontresizable")==0 )
        return MGR_FONT_RESIZABLE;
    else if( _stricmp(_Attrib, "colorscheme")==0 )
        return MGR_COLOR_SCHEME;
    else if( _stricmp(_Attrib, "contained")==0 )
        return MGR_CONTAINED;
    else if( _stricmp(_Attrib, "buttonalign")==0 )
        return MGR_BUTTON_ALIGN;
    else if( _stricmp(_Attrib, "overlap")==0 )
        return MGR_OVERLAP;

    *_HasValue = false;
    return 0; // not found
}

int CTwMgr_SetAttrib(CTwMgr *_Mgr, int _AttribID, const char *_Value)
{
    switch( _AttribID )
    {
    case MGR_HELP:
        if( _Value && strlen(_Value)>0 )
        {
            _Mgr->m_Help = sdscpy(_Mgr->m_Help, _Value);
            _Mgr->m_HelpBarNotUpToDate = true;
            return 1;
        }
        else
        {
            CTwMgr_SetLastError(_Mgr, g_ErrNoValue);
            return 0;
        }
    case MGR_FONT_SIZE:
        if( _Value && strlen(_Value)>0 )
        {
            int s;
            int n = sscanf(_Value, "%d", &s);
            if( n==1 && s>=1 && s<=3 )
            {
                if( s==1 )
                    CTwMgr_SetFont(_Mgr, g_DefaultSmallFont, true);
                else if( s==2 )
                    CTwMgr_SetFont(_Mgr, g_DefaultNormalFont, true);
                else if( s==3 )
                    CTwMgr_SetFont(_Mgr, g_DefaultLargeFont, true);
                return 1;
            }
            else
            {
                CTwMgr_SetLastError(_Mgr, g_ErrBadValue);
                return 0;
            }
        }
        else
        {
            CTwMgr_SetLastError(_Mgr, g_ErrNoValue);
            return 0;
        }
    case MGR_FONT_STYLE:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "fixed")==0 )
            {
                if( _Mgr->m_CurrentFont!=g_DefaultFixed1Font )
                {
                    CTwMgr_SetFont(_Mgr, g_DefaultFixed1Font, true);
                    _Mgr->m_FontResizable = false; // for now fixed font is not resizable
                }
                return 1;
            } 
            else if( _stricmp(_Value, "default")==0 )
            {
                if( _Mgr->m_CurrentFont!=g_DefaultSmallFont && _Mgr->m_CurrentFont!=g_DefaultNormalFont && _Mgr->m_CurrentFont!=g_DefaultLargeFont )
                {
                    if( _Mgr->m_CurrentFont == g_DefaultFixed1Font )
                        _Mgr->m_FontResizable = true;
                    CTwMgr_SetFont(_Mgr, g_DefaultNormalFont, true);
                }
                return 1;
            }
            else
            {
                CTwMgr_SetLastError(_Mgr, g_ErrBadValue);
                return 0;
            }
        }
        else
        {
            CTwMgr_SetLastError(_Mgr, g_ErrNoValue);
            return 0;
        }
    case MGR_ICON_POS:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "bl")==0 || _stricmp(_Value, "lb")==0 || _stricmp(_Value, "bottomleft")==0 || _stricmp(_Value, "leftbottom")==0 )
            {
                _Mgr->m_IconPos = 0;
                return 1;
            }
            else if( _stricmp(_Value, "br")==0 || _stricmp(_Value, "rb")==0 || _stricmp(_Value, "bottomright")==0 || _stricmp(_Value, "rightbottom")==0 )
            {
                _Mgr->m_IconPos = 1;
                return 1;
            }
            else if( _stricmp(_Value, "tl")==0 || _stricmp(_Value, "lt")==0 || _stricmp(_Value, "topleft")==0 || _stricmp(_Value, "lefttop")==0 )
            {
                _Mgr->m_IconPos = 2;
                return 1;
            }
            else if( _stricmp(_Value, "tr")==0 || _stricmp(_Value, "rt")==0 || _stricmp(_Value, "topright")==0 || _stricmp(_Value, "righttop")==0 )
            {
                _Mgr->m_IconPos = 3;
                return 1;
            }
            else
            {
                CTwMgr_SetLastError(_Mgr, g_ErrBadValue);
                return 0;
            }
        }
        else
        {
            CTwMgr_SetLastError(_Mgr, g_ErrNoValue);
            return 0;
        }
    case MGR_ICON_ALIGN:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "vert")==0 || _stricmp(_Value, "vertical")==0  )
            {
                _Mgr->m_IconAlign = 0;
                return 1;
            }
            else if( _stricmp(_Value, "horiz")==0 || _stricmp(_Value, "horizontal")==0  )
            {
                _Mgr->m_IconAlign = 1;
                return 1;
            }
            else
            {
                CTwMgr_SetLastError(_Mgr, g_ErrBadValue);
                return 0;
            }
        }
        else
        {
            CTwMgr_SetLastError(_Mgr, g_ErrNoValue);
            return 0;
        }
    case MGR_ICON_MARGIN:
        if( _Value && strlen(_Value)>0 )
        {
            int x, y;
            int n = sscanf(_Value, "%d%d", &x, &y);
            if( n==2 && x>=0 && y>=0 )
            {
                _Mgr->m_IconMarginX = x;
                _Mgr->m_IconMarginY = y;
                return 1;
            }
            else
            {
                CTwMgr_SetLastError(_Mgr, g_ErrBadValue);
                return 0;
            }
        }
        else
        {
            CTwMgr_SetLastError(_Mgr, g_ErrNoValue);
            return 0;
        }
    case MGR_FONT_RESIZABLE:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "1")==0 || _stricmp(_Value, "true")==0 )
            {
                _Mgr->m_FontResizable = true;
                return 1;
            }
            else if( _stricmp(_Value, "0")==0 || _stricmp(_Value, "false")==0 )
            {
                _Mgr->m_FontResizable = false;
                return 1;
            }
            else
            {
                CTwMgr_SetLastError(g_TwMgr, g_ErrBadValue);
                return 0;
            }
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNoValue);
            return 0;
        }
    case MGR_COLOR_SCHEME:
        if( _Value && strlen(_Value)>0 )
        {
            int s;
            int n = sscanf(_Value, "%d", &s);
            if( n==1 && s>=0 && s<=1 )
            {
                if( s==0 )
                    _Mgr->m_UseOldColorScheme = true;
                else
                    _Mgr->m_UseOldColorScheme = false;
                return 1;
            }
            else
            {
                CTwMgr_SetLastError(_Mgr, g_ErrBadValue);
                return 0;
            }
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNoValue);
            return 0;
        }
    case MGR_CONTAINED:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "1")==0 || _stricmp(_Value, "true")==0 )
                _Mgr->m_Contained = true;
            else if( _stricmp(_Value, "0")==0 || _stricmp(_Value, "false")==0 )
                _Mgr->m_Contained = false;
            else
            {
                CTwMgr_SetLastError(g_TwMgr, g_ErrBadValue);
                return 0;
            }
            for( size_t barIdx=0; barIdx<g_TwMgr->m_Bars.count; ++barIdx )
                if( g_TwMgr->m_Bars.items[barIdx]!=NULL )
                    g_TwMgr->m_Bars.items[barIdx]->m_Contained = _Mgr->m_Contained;
            return 1;
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNoValue);
            return 0;
        }
    case MGR_BUTTON_ALIGN:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "left")==0 )
                _Mgr->m_ButtonAlign = BUTTON_ALIGN_LEFT;
            else if( _stricmp(_Value, "center")==0 )
                _Mgr->m_ButtonAlign = BUTTON_ALIGN_CENTER;
            else if( _stricmp(_Value, "right")==0 )
                _Mgr->m_ButtonAlign = BUTTON_ALIGN_RIGHT;
            else
            {
                CTwMgr_SetLastError(g_TwMgr, g_ErrBadValue);
                return 0;
            }
            for( size_t barIdx=0; barIdx<g_TwMgr->m_Bars.count; ++barIdx )
                if( g_TwMgr->m_Bars.items[barIdx]!=NULL )
                    g_TwMgr->m_Bars.items[barIdx]->m_ButtonAlign = _Mgr->m_ButtonAlign;
            return 1;
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNoValue);
            return 0;
        }
    case MGR_OVERLAP:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "1")==0 || _stricmp(_Value, "true")==0 )
            {
                _Mgr->m_OverlapContent = true;
                return 1;
            }
            else if( _stricmp(_Value, "0")==0 || _stricmp(_Value, "false")==0 )
            {
                _Mgr->m_OverlapContent = false;
                return 1;
            }
            else
            {
                CTwMgr_SetLastError(g_TwMgr, g_ErrBadValue);
                return 0;
            }
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNoValue);
            return 0;
        }
    default:
        CTwMgr_SetLastError(g_TwMgr, g_ErrUnknownAttrib);
        return 0;
    }
}

ERetType CTwMgr_GetAttrib(const CTwMgr *_Mgr, int _AttribID, CDoubleArray *outDoubles, sds *outString)
{
    outDoubles->count = 0;
    sdsclear(*outString);

    switch( _AttribID )
    {
    case MGR_HELP:
        *outString = sdscat(*outString, _Mgr->m_Help);
        return RET_STRING;
    case MGR_FONT_SIZE:
        if( _Mgr->m_CurrentFont==g_DefaultSmallFont )
            tw_da_append(outDoubles, 1);
        else if( _Mgr->m_CurrentFont==g_DefaultNormalFont )
            tw_da_append(outDoubles, 2);
        else if( _Mgr->m_CurrentFont==g_DefaultLargeFont )
            tw_da_append(outDoubles, 3);
        else
            tw_da_append(outDoubles, 0); // should not happened
        return RET_DOUBLE;
    case MGR_FONT_STYLE:
        if( _Mgr->m_CurrentFont==g_DefaultFixed1Font )
            *outString = sdscat(*outString, "fixed");
        else
            *outString = sdscat(*outString, "default");
        return RET_STRING;
    case MGR_ICON_POS:
        if( _Mgr->m_IconPos==0 )
            *outString = sdscat(*outString, "bottomleft");
        else if( _Mgr->m_IconPos==1 )
            *outString = sdscat(*outString, "bottomright");
        else if( _Mgr->m_IconPos==2 )
            *outString = sdscat(*outString, "topleft");
        else if( _Mgr->m_IconPos==3 )
            *outString = sdscat(*outString, "topright");
        else
            *outString = sdscat(*outString, "undefined"); // should not happened
        return RET_STRING;
    case MGR_ICON_ALIGN:
        if( _Mgr->m_IconAlign==0 )
            *outString = sdscat(*outString, "vertical");
        else if( _Mgr->m_IconAlign==1 )
            *outString = sdscat(*outString, "horizontal");
        else
            *outString = sdscat(*outString, "undefined"); // should not happened
        return RET_STRING;
    case MGR_ICON_MARGIN:
        tw_da_append(outDoubles, _Mgr->m_IconMarginX);
        tw_da_append(outDoubles, _Mgr->m_IconMarginY);
        return RET_DOUBLE;
    case MGR_FONT_RESIZABLE:
        tw_da_append(outDoubles, _Mgr->m_FontResizable);
        return RET_DOUBLE;
    case MGR_COLOR_SCHEME:
        tw_da_append(outDoubles, _Mgr->m_UseOldColorScheme ? 0 : 1);
        return RET_DOUBLE;
    case MGR_CONTAINED:
        {
            bool contained = _Mgr->m_Contained;
            /*
            if( contained ) 
            {
                vector<TwBar*>::iterator barIt;
                for( barIt=g_TwMgr->m_Bars.begin(); barIt!=g_TwMgr->m_Bars.end(); ++barIt )
                    if( (*barIt)!=NULL && !(*barIt)->m_Contained )
                    {
                        contained = false;
                        break;
                    }
            }
            */
            tw_da_append(outDoubles, contained);
            return RET_DOUBLE;
        }
    case MGR_BUTTON_ALIGN:
        if( _Mgr->m_ButtonAlign==BUTTON_ALIGN_LEFT )
            *outString = sdscat(*outString, "left");
        else if( _Mgr->m_ButtonAlign==BUTTON_ALIGN_CENTER )
            *outString = sdscat(*outString, "center");
        else
            *outString = sdscat(*outString, "right");
        return RET_STRING;
    case MGR_OVERLAP:
        tw_da_append(outDoubles, _Mgr->m_OverlapContent);
        return RET_DOUBLE;
    default:
        CTwMgr_SetLastError(g_TwMgr, g_ErrUnknownAttrib);
        return RET_ERROR;
    }
}

//  ---------------------------------------------------------------------------

void CTwMgr_Minimize(CTwMgr *_Mgr, TwBar *_Bar)
{
    assert(_Mgr->m_Graph!=NULL && _Bar!=NULL);
    assert(_Mgr->m_Bars.count==_Mgr->m_MinOccupied.count);
    if( _Bar->m_IsMinimized )
        return;
    if( _Bar->m_Visible )
    {
        size_t i = _Mgr->m_NbMinimizedBars;
        _Mgr->m_NbMinimizedBars++;
        for( i=0; i<_Mgr->m_MinOccupied.count; ++i )
            if( !_Mgr->m_MinOccupied.items[i] )
                break;
        if( i<_Mgr->m_MinOccupied.count )
            _Mgr->m_MinOccupied.items[i] = true;
        _Bar->m_MinNumber = (int)i;
    }
    else
        _Bar->m_MinNumber = -1;
    _Bar->m_IsMinimized = true;
    CTwBar_NotUpToDate(_Bar);
}

//  ---------------------------------------------------------------------------

void CTwMgr_Maximize(CTwMgr *_Mgr, TwBar *_Bar)
{
    assert(_Mgr->m_Graph!=NULL && _Bar!=NULL);
    assert(_Mgr->m_Bars.count==_Mgr->m_MinOccupied.count);
    if( !_Bar->m_IsMinimized )
        return;
    if( _Bar->m_Visible )
    {
        --_Mgr->m_NbMinimizedBars;
        if( _Mgr->m_NbMinimizedBars<0 )
            _Mgr->m_NbMinimizedBars = 0;
        if( _Bar->m_MinNumber>=0 && _Bar->m_MinNumber<(int)_Mgr->m_MinOccupied.count )
            _Mgr->m_MinOccupied.items[_Bar->m_MinNumber] = false;
    }
    _Bar->m_IsMinimized = false;
    CTwBar_NotUpToDate(_Bar);
    if( _Bar->m_IsHelpBar )
        _Mgr->m_HelpBarNotUpToDate = true;
}

//  ---------------------------------------------------------------------------

void CTwMgr_Hide(CTwMgr *_Mgr, TwBar *_Bar)
{
    assert(_Mgr->m_Graph!=NULL && _Bar!=NULL);
    if( !_Bar->m_Visible )
        return;
    if( CTwBar_IsMinimized(_Bar) )
    {
        CTwMgr_Maximize(_Mgr, _Bar);
        _Bar->m_Visible = false;
        CTwMgr_Minimize(_Mgr, _Bar);
    }
    else
        _Bar->m_Visible = false;
    if( !_Bar->m_IsHelpBar )
        _Mgr->m_HelpBarNotUpToDate = true;
}

//  ---------------------------------------------------------------------------

void CTwMgr_Unhide(CTwMgr *_Mgr, TwBar *_Bar)
{
    assert(_Mgr->m_Graph!=NULL && _Bar!=NULL);
    if( _Bar->m_Visible )
        return;
    if( CTwBar_IsMinimized(_Bar) )
    {
        CTwMgr_Maximize(_Mgr, _Bar);
        _Bar->m_Visible = true;
        CTwMgr_Minimize(_Mgr, _Bar);
    }
    else
        _Bar->m_Visible = true;
    CTwBar_NotUpToDate(_Bar);
    if( !_Bar->m_IsHelpBar )
        _Mgr->m_HelpBarNotUpToDate = true;
}

//  ---------------------------------------------------------------------------

void CTwMgr_SetFont(CTwMgr *_Mgr, const CTexFont *_Font, bool _ResizeBars)
{
    assert(_Mgr->m_Graph!=NULL);
    assert(_Font!=NULL);

    _Mgr->m_CurrentFont = _Font;

    for( int i=0; i<(int)_Mgr->m_Bars.count; ++i )
        if( _Mgr->m_Bars.items[i]!=NULL )
        {
            int fh = _Mgr->m_Bars.items[i]->m_Font->m_CharHeight;
            _Mgr->m_Bars.items[i]->m_Font = _Font;
            if( _ResizeBars )
            {
                if( _Mgr->m_Bars.items[i]->m_Movable )
                {
                    _Mgr->m_Bars.items[i]->m_PosX += (3*(fh-_Font->m_CharHeight))/2;
                    _Mgr->m_Bars.items[i]->m_PosY += (fh-_Font->m_CharHeight)/2;
                }
                if( _Mgr->m_Bars.items[i]->m_Resizable )
                {
                    _Mgr->m_Bars.items[i]->m_Width = (_Mgr->m_Bars.items[i]->m_Width*_Font->m_CharHeight)/fh;
                    _Mgr->m_Bars.items[i]->m_Height = (_Mgr->m_Bars.items[i]->m_Height*_Font->m_CharHeight)/fh;
                    _Mgr->m_Bars.items[i]->m_ValuesWidth = (_Mgr->m_Bars.items[i]->m_ValuesWidth*_Font->m_CharHeight)/fh;
                }
            }
            CTwBar_NotUpToDate(_Mgr->m_Bars.items[i]);
        }

    if( _Mgr->m_HelpBar!=NULL )
        CTwBar_Update(_Mgr->m_HelpBar);
    _Mgr->m_InfoBuildText = true;
    _Mgr->m_KeyPressedBuildText = true;
    _Mgr->m_HelpBarNotUpToDate = true;
}

//  ---------------------------------------------------------------------------

void ANT_CALL TwGlobalError(const char *_ErrorMessage)  // to be called when g_TwMasterMgr is not created
{
    if( g_ErrorHandler==NULL )
    {
        fprintf(stderr, "ERROR(AntTweakBar) >> %s\n", _ErrorMessage);
    #ifdef ANT_WINDOWS
        OutputDebugString("ERROR(AntTweakBar) >> ");
        OutputDebugString(_ErrorMessage);
        OutputDebugString("\n");
    #endif // ANT_WINDOWS
    }
    else
        g_ErrorHandler(_ErrorMessage);

    if( g_BreakOnError )
        abort();
}

//  ---------------------------------------------------------------------------

void CTwMgr_SetLastError(CTwMgr *_Mgr, const char *_ErrorMessage)    // _ErrorMessage must be a static string
{
    if (_Mgr != g_TwMasterMgr)
    {
        // route to master
        CTwMgr_SetLastError(g_TwMasterMgr, _ErrorMessage);
        return;
    }

    _Mgr->m_LastError = _ErrorMessage;

    if( g_ErrorHandler==NULL )
    {
        if( _Mgr->m_CurrentDbgFile!=NULL && strlen(_Mgr->m_CurrentDbgFile)>0 && _Mgr->m_CurrentDbgLine>0 )
            fprintf(stderr, "%s(%d): ", _Mgr->m_CurrentDbgFile, _Mgr->m_CurrentDbgLine);
        fprintf(stderr, "ERROR(AntTweakBar) >> %s\n", _Mgr->m_LastError);
    #ifdef ANT_WINDOWS
        if( _Mgr->m_CurrentDbgFile!=NULL && strlen(_Mgr->m_CurrentDbgFile)>0 && _Mgr->m_CurrentDbgLine>0 )
        {
            OutputDebugString(_Mgr->m_CurrentDbgFile);
            char sl[32];
            sprintf(sl, "(%d): ", _Mgr->m_CurrentDbgLine);
            OutputDebugString(sl);
        }
        OutputDebugString("ERROR(AntTweakBar) >> ");
        OutputDebugString(_Mgr->m_LastError);
        OutputDebugString("\n");
    #endif // ANT_WINDOWS
    }
    else
        g_ErrorHandler(_ErrorMessage);

    if( g_BreakOnError )
        abort();
}

//  ---------------------------------------------------------------------------

const char *CTwMgr_GetLastError(CTwMgr *_Mgr)
{
    if (_Mgr != g_TwMasterMgr)
    {
        // route to master
        return CTwMgr_GetLastError(g_TwMasterMgr);
    }

    const char *Err = _Mgr->m_LastError;
    _Mgr->m_LastError = NULL;
    return Err;
}

//  ---------------------------------------------------------------------------

const char *CTwMgr_CheckLastError(const CTwMgr *_Mgr)
{
    return _Mgr->m_LastError;
}

//  ---------------------------------------------------------------------------

void CTwMgr_SetCurrentDbgParams(CTwMgr *_Mgr, const char *dbgFile, int dbgLine)
{
    _Mgr->m_CurrentDbgFile = dbgFile;
    _Mgr->m_CurrentDbgLine = dbgLine;
}

//  ---------------------------------------------------------------------------

int ANT_CALL __TwDbg(const char *dbgFile, int dbgLine)
{
    if( g_TwMgr!=NULL )
        CTwMgr_SetCurrentDbgParams(g_TwMgr, dbgFile, dbgLine);
    return 0;   // always returns zero
}

//  ---------------------------------------------------------------------------

// Was two C++-overloaded TwHandleErrors(TwErrorHandler, int) /
// TwHandleErrors(TwErrorHandler) functions - C has no overloading, and the
// 2-argument version was only ever called internally by the 1-argument one
// (always with _BreakOnError=false, confirmed by grep - no other call site
// anywhere), so merged into a single function matching the public header's
// only declared signature, with that same always-false behavior inlined.
void ANT_CALL TwHandleErrors(TwErrorHandler _ErrorHandler)
{
    g_ErrorHandler = _ErrorHandler;
    g_BreakOnError = false;
}

//  ---------------------------------------------------------------------------

const char *ANT_CALL TwGetLastError(void)
{
    if( g_TwMasterMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return g_ErrNotInit;
    }
    else
        return CTwMgr_GetLastError(g_TwMasterMgr);
}

//  ---------------------------------------------------------------------------

TwBar *ANT_CALL TwNewBar(const char *_Name)
{
    if( g_TwMgr==NULL || g_TwMgr->m_Graph==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return NULL; // not initialized
    }

    TwFreeAsyncDrawing(); // For multi-thread savety

    if( _Name==NULL || strlen(_Name)<=0 )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        return NULL;
    }
    if( CTwMgr_FindBar(g_TwMgr, _Name)>=0 )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrExist);
        return NULL;
    }

    if( strstr(_Name, "`")!=NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrNoBackQuote);
        return NULL;
    }

    if( g_TwMgr->m_PopupBar!=NULL ) // delete popup bar if it exists
    {
        TwDeleteBar(g_TwMgr->m_PopupBar);
        g_TwMgr->m_PopupBar = NULL;
    }

    TwBar *Bar = CTwBar_Create(_Name);
    tw_da_append(&g_TwMgr->m_Bars, Bar);
    tw_da_append(&g_TwMgr->m_Order, (int)g_TwMgr->m_Bars.count-1);
    tw_da_append(&g_TwMgr->m_MinOccupied, false);
    g_TwMgr->m_HelpBarNotUpToDate = true;

    return Bar;
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwDeleteBar(TwBar *_Bar)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return 0; // not initialized
    }
    if( _Bar==NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        return 0;
    }
    if( _Bar==g_TwMgr->m_HelpBar )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrDelHelp);
        return 0;
    }

    TwFreeAsyncDrawing(); // For multi-thread savety

    int i = 0;
    for( ; i<(int)g_TwMgr->m_Bars.count; ++i )
        if( g_TwMgr->m_Bars.items[i]==_Bar )
            break;
    if( i==(int)g_TwMgr->m_Bars.count )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrNotFound);
        return 0;
    }

    if( g_TwMgr->m_PopupBar!=NULL && _Bar!=g_TwMgr->m_PopupBar )    // delete popup bar first if it exists
    {
        TwDeleteBar(g_TwMgr->m_PopupBar);
        g_TwMgr->m_PopupBar = NULL;
    }

    // force bar to un-minimize
    CTwMgr_Maximize(g_TwMgr, _Bar);
    // find an empty MinOccupied
    int j = 0;
    for( ; j<(int)g_TwMgr->m_MinOccupied.count; ++j )
        if( g_TwMgr->m_MinOccupied.items[j]==false )
            break;
    assert( j!=(int)g_TwMgr->m_MinOccupied.count );
    // shift MinNumbers and erase the empty MinOccupied
    for( size_t k=0; k<g_TwMgr->m_Bars.count; ++k )
        if( g_TwMgr->m_Bars.items[k]!=NULL && g_TwMgr->m_Bars.items[k]->m_MinNumber>j )
            g_TwMgr->m_Bars.items[k]->m_MinNumber -= 1;
    tw_da_remove_ordered(&g_TwMgr->m_MinOccupied, (size_t)j);
    // erase _Bar order
    int orderIdxToRemove = -1;
    for( size_t k=0; k<g_TwMgr->m_Order.count; ++k )
        if( g_TwMgr->m_Order.items[k]==i )
            orderIdxToRemove = (int)k;
        else if( g_TwMgr->m_Order.items[k]>i )
            g_TwMgr->m_Order.items[k] -= 1;
    assert( orderIdxToRemove>=0 );
    tw_da_remove_ordered(&g_TwMgr->m_Order, (size_t)orderIdxToRemove);

    // erase & delete _Bar
    tw_da_remove_ordered(&g_TwMgr->m_Bars, (size_t)i);
    CTwBar_Destroy(_Bar);

    g_TwMgr->m_HelpBarNotUpToDate = true;
    return 1;
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwDeleteAllBars(void)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return 0; // not initialized
    }

    TwFreeAsyncDrawing(); // For multi-thread savety

    int n = 0;
    if( g_TwMgr->m_Terminating || g_TwMgr->m_HelpBar==NULL ) 
    {
        for( size_t i=0; i<g_TwMgr->m_Bars.count; ++i )
            if( g_TwMgr->m_Bars.items[i]!=NULL )
            {
                ++n;
                CTwBar_Destroy(g_TwMgr->m_Bars.items[i]);
                g_TwMgr->m_Bars.items[i] = NULL;
            }
        g_TwMgr->m_Bars.count = 0;
        g_TwMgr->m_Order.count = 0;
        g_TwMgr->m_MinOccupied.count = 0;
        g_TwMgr->m_HelpBarNotUpToDate = true;
    }
    else
    {
        // Snapshot g_TwMgr->m_Bars before iterating: TwDeleteBar mutates
        // m_Bars/m_Order/m_MinOccupied in place, so a plain struct copy
        // (sharing the same items buffer) would be unsafe to iterate while
        // mutating - deep-copy the pointer array instead, same as the
        // original std::vector value-copy did.
        CTwBarPtrArray bars = {0};
        tw_da_resize(&bars, g_TwMgr->m_Bars.count);
        memcpy(bars.items, g_TwMgr->m_Bars.items, bars.count*sizeof(TwBar *));
        for( size_t i = 0; i < bars.count; ++i )
            if( bars.items[i]!=0 && bars.items[i]!=g_TwMgr->m_HelpBar)
            {
                ++n;
                TwDeleteBar(bars.items[i]);
            }
        tw_da_free(&bars);
        g_TwMgr->m_HelpBarNotUpToDate = true;
    }

    if( n==0 )
    {
        //CTwMgr_SetLastError(g_TwMgr, g_ErrNthToDo);
        return 0;
    }
    else
        return 1;
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwSetTopBar(const TwBar *_Bar)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return 0; // not initialized
    }
    if( _Bar==NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        return 0;
    }

    TwFreeAsyncDrawing(); // For multi-thread savety

    if( _Bar!=g_TwMgr->m_PopupBar && sdslen(g_TwMgr->m_BarAlwaysOnBottom)>0 )
    {
        if( strcmp(_Bar->m_Name, g_TwMgr->m_BarAlwaysOnBottom)==0 )
            return TwSetBottomBar(_Bar);
    }

    int i = -1, iOrder;
    for( iOrder=0; iOrder<(int)g_TwMgr->m_Bars.count; ++iOrder )
    {
        i = g_TwMgr->m_Order.items[iOrder];
        assert( i>=0 && i<(int)g_TwMgr->m_Bars.count );
        if( g_TwMgr->m_Bars.items[i]==_Bar )
            break;
    }
    if( i<0 || iOrder>=(int)g_TwMgr->m_Bars.count )    // bar not found
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrNotFound);
        return 0;
    }

    for( int j=iOrder; j<(int)g_TwMgr->m_Bars.count-1; ++j )
        g_TwMgr->m_Order.items[j] = g_TwMgr->m_Order.items[j+1];
    g_TwMgr->m_Order.items[(int)g_TwMgr->m_Bars.count-1] = i;

    if( _Bar!=g_TwMgr->m_PopupBar && sdslen(g_TwMgr->m_BarAlwaysOnTop)>0 )
    {
        int topIdx = CTwMgr_FindBar(g_TwMgr, g_TwMgr->m_BarAlwaysOnTop);
        TwBar *top = (topIdx>=0 && topIdx<(int)g_TwMgr->m_Bars.count) ? g_TwMgr->m_Bars.items[topIdx] : NULL;
        if( top!=NULL && top!=_Bar )
            TwSetTopBar(top);
    }

    if( g_TwMgr->m_PopupBar!=NULL && _Bar!=g_TwMgr->m_PopupBar )
        TwSetTopBar(g_TwMgr->m_PopupBar);

    return 1;
}

//  ---------------------------------------------------------------------------

TwBar * ANT_CALL TwGetTopBar(void)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return NULL; // not initialized
    }

    if( g_TwMgr->m_Bars.count>0 && g_TwMgr->m_PopupBar==NULL )
        return g_TwMgr->m_Bars.items[g_TwMgr->m_Order.items[ g_TwMgr->m_Bars.count-1 ]];
    else if( g_TwMgr->m_Bars.count>1 && g_TwMgr->m_PopupBar!=NULL )
        return g_TwMgr->m_Bars.items[g_TwMgr->m_Order.items[ g_TwMgr->m_Bars.count-2 ]];
    else
        return NULL;
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwSetBottomBar(const TwBar *_Bar)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return 0; // not initialized
    }
    if( _Bar==NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        return 0;
    }

    TwFreeAsyncDrawing(); // For multi-thread savety

    if( _Bar!=g_TwMgr->m_PopupBar && sdslen(g_TwMgr->m_BarAlwaysOnTop)>0 )
    {
        if( strcmp(_Bar->m_Name, g_TwMgr->m_BarAlwaysOnTop)==0 )
            return TwSetTopBar(_Bar);
    }

    int i = -1, iOrder;
    for( iOrder=0; iOrder<(int)g_TwMgr->m_Bars.count; ++iOrder )
    {
        i = g_TwMgr->m_Order.items[iOrder];
        assert( i>=0 && i<(int)g_TwMgr->m_Bars.count );
        if( g_TwMgr->m_Bars.items[i]==_Bar )
            break;
    }
    if( i<0 || iOrder>=(int)g_TwMgr->m_Bars.count )    // bar not found
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrNotFound);
        return 0;
    }

    if( iOrder>0 )
        for( int j=iOrder-1; j>=0; --j )
            g_TwMgr->m_Order.items[j+1] = g_TwMgr->m_Order.items[j];
    g_TwMgr->m_Order.items[0] = i;

    if( _Bar!=g_TwMgr->m_PopupBar && sdslen(g_TwMgr->m_BarAlwaysOnBottom)>0 )
    {
        int btmIdx = CTwMgr_FindBar(g_TwMgr, g_TwMgr->m_BarAlwaysOnBottom);
        TwBar *btm = (btmIdx>=0 && btmIdx<(int)g_TwMgr->m_Bars.count) ? g_TwMgr->m_Bars.items[btmIdx] : NULL;
        if( btm!=NULL && btm!=_Bar )
            TwSetBottomBar(btm);
    }

    return 1;
}

//  ---------------------------------------------------------------------------

TwBar* ANT_CALL TwGetBottomBar(void)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return NULL; // not initialized
    }

    if( g_TwMgr->m_Bars.count>0 )
        return g_TwMgr->m_Bars.items[g_TwMgr->m_Order.items[0]];
    else
        return NULL;
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwSetBarState(TwBar *_Bar, TwState _State)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return 0;  // not initialized
    }
    if( _Bar==NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        return 0;
    }

    TwFreeAsyncDrawing(); // For multi-thread savety

    switch( _State )
    {
    case TW_STATE_SHOWN:
        CTwMgr_Unhide(g_TwMgr, _Bar);
        return 1;
    case TW_STATE_ICONIFIED:
        //CTwMgr_Unhide(g_TwMgr, _Bar);
        CTwMgr_Minimize(g_TwMgr, _Bar);
        return 1;
    case TW_STATE_HIDDEN:
        //CTwMgr_Maximize(g_TwMgr, _Bar);
        CTwMgr_Hide(g_TwMgr, _Bar);
        return 1;
    case TW_STATE_UNICONIFIED:
        //CTwMgr_Unhide(g_TwMgr, _Bar);
        CTwMgr_Maximize(g_TwMgr, _Bar);
        return 1;
    default:
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        return 0;
    }
}

//  ---------------------------------------------------------------------------

/*
TwState ANT_CALL TwGetBarState(const TwBar *_Bar)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return TW_STATE_ERROR;  // not initialized
    }
    if( _Bar==NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        return TW_STATE_ERROR;
    }

    if( !_Bar->m_Visible )
        return TW_STATE_HIDDEN;
    else if( CTwBar_IsMinimized(_Bar) )
        return TW_STATE_ICONIFIED;
    else
        return TW_STATE_SHOWN;
}
*/

//  ---------------------------------------------------------------------------

const char * ANT_CALL TwGetBarName(const TwBar *_Bar)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return NULL;  // not initialized
    }
    if( _Bar==NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        return NULL;
    }
    int i = 0;
    for( ; i<(int)g_TwMgr->m_Bars.count; ++i )
        if( g_TwMgr->m_Bars.items[i]==_Bar )
            break;
    if( i==(int)g_TwMgr->m_Bars.count )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrNotFound);
        return NULL;
    }

    return _Bar->m_Name;
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwGetBarCount(void)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return 0;  // not initialized
    }

    return (int)g_TwMgr->m_Bars.count;
}


//  ---------------------------------------------------------------------------

TwBar * ANT_CALL TwGetBarByIndex(int index)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return NULL;  // not initialized
    }

    if( index>=0 && index<(int)g_TwMgr->m_Bars.count ) 
        return g_TwMgr->m_Bars.items[index];
    else 
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrOutOfRange);
        return NULL;
    }
}

//  ---------------------------------------------------------------------------

TwBar * ANT_CALL TwGetBarByName(const char *name)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return NULL; // not initialized
    }

    int idx = CTwMgr_FindBar(g_TwMgr, name);
    if ( idx>=0 && idx<(int)g_TwMgr->m_Bars.count )
        return g_TwMgr->m_Bars.items[idx];
    else
        return NULL;
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwRefreshBar(TwBar *bar)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return 0;  // not initialized
    }
    if( bar==NULL )
    {
        for( size_t bi=0; bi<g_TwMgr->m_Bars.count; ++bi )
            if( g_TwMgr->m_Bars.items[bi]!=NULL )
                CTwBar_NotUpToDate(g_TwMgr->m_Bars.items[bi]);
    }
    else
    {
        int i = 0;
        for( ; i<(int)g_TwMgr->m_Bars.count; ++i )
            if( g_TwMgr->m_Bars.items[i]==bar )
                break;
        if( i==(int)g_TwMgr->m_Bars.count )
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNotFound);
            return 0;
        }

        CTwBar_NotUpToDate(bar);
    }
    return 1;
}

//  ---------------------------------------------------------------------------

int BarVarHasAttrib(CTwBar *_Bar, CTwVar *_Var, const char *_Attrib, bool *_HasValue);
int BarVarSetAttrib(CTwBar *_Bar, CTwVar *_Var, CTwVarGroup *_VarParent, int _VarIndex, int _AttribID, const char *_Value);
ERetType BarVarGetAttrib(CTwBar *_Bar, CTwVar *_Var, CTwVarGroup *_VarParent, int _VarIndex, int _AttribID, CDoubleArray *outDouble, sds *outString);


int ANT_CALL TwGetParam(TwBar *bar, const char *varName, const char *paramName, TwParamValueType paramValueType, unsigned int outValueMaxCount, void *outValues)
{
    unsigned int fpuState = TwFPU_Save(); // force fpu precision

    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        TwFPU_Restore(fpuState);
        return 0; // not initialized
    }
    if( paramName==NULL || strlen(paramName)<=0 )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        TwFPU_Restore(fpuState);
        return 0;
    }
    if( outValueMaxCount<=0 || outValues==NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        TwFPU_Restore(fpuState);
        return 0;
    }

    if( bar==NULL )
        bar = TW_GLOBAL_BAR;
    else
    {
        int i = 0;
        for( ; i<(int)g_TwMgr->m_Bars.count; ++i )
            if( g_TwMgr->m_Bars.items[i]==bar )
                break;
        if( i==(int)g_TwMgr->m_Bars.count )
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNotFound);
            TwFPU_Restore(fpuState);
            return 0;
        }
    }
    CTwVarGroup *varParent = NULL;
    int varIndex = -1;
    CTwVar *var = NULL;
    if( varName!=NULL && strlen(varName)>0 )
    {
        var = (CTwVar *)CTwBar_Find(bar, varName, &varParent, &varIndex);
        if( var==NULL )
        {
            _snprintf(g_ErrParse, sizeof(g_ErrParse), "Unknown var '%s/%s'",
                      (bar==TW_GLOBAL_BAR) ? "GLOBAL" : bar->m_Name, varName);
            g_ErrParse[sizeof(g_ErrParse)-1] = '\0';
            CTwMgr_SetLastError(g_TwMgr, g_ErrParse);
            TwFPU_Restore(fpuState);
            return 0;
        }
    }

    bool hasValue = false;
    int paramID = BarVarHasAttrib(bar, var, paramName, &hasValue);
    if( paramID>0 )
    {
        sds valStr = sdsempty();
        CDoubleArray valDbl = {0};
        const char *PrevLastErrorPtr = CTwMgr_CheckLastError(g_TwMgr);

        ERetType retType = BarVarGetAttrib(bar, var, varParent, varIndex, paramID, &valDbl, &valStr);
        unsigned int i, valDblCount = (unsigned int)valDbl.count;
        if( valDblCount > outValueMaxCount )
            valDblCount = outValueMaxCount;
        if( retType==RET_DOUBLE && valDblCount==0 )
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrHasNoValue);
            retType = RET_ERROR;
        }

        if( retType==RET_DOUBLE )
        {
            switch( paramValueType )
            {
            case TW_PARAM_INT32:
                for( i=0; i<valDblCount; i++ )
                    ((int *)(outValues))[i] = (int)valDbl.items[i];
                sdsfree(valStr);
                tw_da_free(&valDbl);
                TwFPU_Restore(fpuState);
                return valDblCount;
            case TW_PARAM_FLOAT:
                for( i=0; i<valDblCount; i++ )
                    ((float *)(outValues))[i] = (float)valDbl.items[i];
                sdsfree(valStr);
                tw_da_free(&valDbl);
                TwFPU_Restore(fpuState);
                return valDblCount;
            case TW_PARAM_DOUBLE:
                for( i=0; i<valDblCount; i++ )
                    ((double *)(outValues))[i] = valDbl.items[i];
                sdsfree(valStr);
                tw_da_free(&valDbl);
                TwFPU_Restore(fpuState);
                return valDblCount;
            case TW_PARAM_CSTRING:
                sdsclear(valStr);
                for( i=0; i<(unsigned int)valDbl.count; i++ ) // not valDblCount here
                    valStr = sdscatprintf(valStr, "%s%g", (i>0) ? " " : "", valDbl.items[i]);
                strncpy((char *)(outValues), valStr, outValueMaxCount);
                i = (unsigned int)sdslen(valStr);
                if( i>outValueMaxCount-1 )
                    i = outValueMaxCount-1;
                ((char *)(outValues))[i] = '\0';
                sdsfree(valStr);
                tw_da_free(&valDbl);
                TwFPU_Restore(fpuState);
                return 1; // always returns 1 for CSTRING
            default:
                CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam); // Unknown param value type
                retType = RET_ERROR;
            }
        }
        else if( retType==RET_STRING )
        {
            if( paramValueType == TW_PARAM_CSTRING )
            {
                strncpy((char *)(outValues), valStr, outValueMaxCount);
                i = (unsigned int)sdslen(valStr);
                if( i>outValueMaxCount-1 )
                    i = outValueMaxCount-1;
                ((char *)(outValues))[i] = '\0';
                sdsfree(valStr);
                tw_da_free(&valDbl);
                TwFPU_Restore(fpuState);
                return 1; // always returns 1 for CSTRING
            }
            else
            {
                CTwMgr_SetLastError(g_TwMgr, g_ErrBadType); // string cannot be converted to int or double
                retType = RET_ERROR;
            }
        }

        if( retType==RET_ERROR )
        {
            bool errMsg = (CTwMgr_CheckLastError(g_TwMgr)!=NULL && strlen(CTwMgr_CheckLastError(g_TwMgr))>0 && PrevLastErrorPtr!=CTwMgr_CheckLastError(g_TwMgr));
            _snprintf(g_ErrParse, sizeof(g_ErrParse), "Unable to get param '%s%s%s %s' %s%s",
                      (bar==TW_GLOBAL_BAR) ? "GLOBAL" : bar->m_Name, (var!=NULL) ? "/" : "",
                      (var!=NULL) ? varName : "", paramName, errMsg ? " : " : "",
                      errMsg ? CTwMgr_CheckLastError(g_TwMgr) : "");
            g_ErrParse[sizeof(g_ErrParse)-1] = '\0';
            CTwMgr_SetLastError(g_TwMgr, g_ErrParse);
        }
        sdsfree(valStr);
        tw_da_free(&valDbl);
        TwFPU_Restore(fpuState);
        return retType;
    }
    else
    {
        _snprintf(g_ErrParse, sizeof(g_ErrParse), "Unknown param '%s%s%s %s'",
                  (bar==TW_GLOBAL_BAR) ? "GLOBAL" : bar->m_Name,
                  (var!=NULL) ? "/" : "", (var!=NULL) ? varName : "", paramName);
        g_ErrParse[sizeof(g_ErrParse)-1] = '\0';
        CTwMgr_SetLastError(g_TwMgr, g_ErrParse);
        TwFPU_Restore(fpuState);
        return 0;
    }
}


int ANT_CALL TwSetParam(TwBar *bar, const char *varName, const char *paramName, TwParamValueType paramValueType, unsigned int inValueCount, const void *inValues)
{
    unsigned int fpuState = TwFPU_Save(); // force fpu precision

    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        TwFPU_Restore(fpuState);
        return 0; // not initialized
    }
    if( paramName==NULL || strlen(paramName)<=0 )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        TwFPU_Restore(fpuState);
        return 0;
    }
    if( inValueCount>0 && inValues==NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        TwFPU_Restore(fpuState);
        return 0;
    }

    TwFreeAsyncDrawing(); // For multi-thread savety

    if( bar==NULL )
        bar = TW_GLOBAL_BAR;
    else
    {
        int i = 0;
        for( ; i<(int)g_TwMgr->m_Bars.count; ++i )
            if( g_TwMgr->m_Bars.items[i]==bar )
                break;
        if( i==(int)g_TwMgr->m_Bars.count )
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNotFound);
            TwFPU_Restore(fpuState);
            return 0;
        }
    }
    CTwVarGroup *varParent = NULL;
    int varIndex = -1;
    CTwVar *var = NULL;
    if( varName!=NULL && strlen(varName)>0 )
    {
        var = (CTwVar *)CTwBar_Find(bar, varName, &varParent, &varIndex);
        if( var==NULL )
        {
            _snprintf(g_ErrParse, sizeof(g_ErrParse), "Unknown var '%s/%s'",
                      (bar==TW_GLOBAL_BAR) ? "GLOBAL" : bar->m_Name, varName);
            g_ErrParse[sizeof(g_ErrParse)-1] = '\0';
            CTwMgr_SetLastError(g_TwMgr, g_ErrParse);
            TwFPU_Restore(fpuState);
            return 0;
        }
    }

    bool hasValue = false;
    int paramID = BarVarHasAttrib(bar, var, paramName, &hasValue);
    if( paramID>0 )
    {
        int ret = 0;
        const char *PrevLastErrorPtr = CTwMgr_CheckLastError(g_TwMgr);
        if( hasValue )
        {
            sds valuesStr = sdsempty();
            unsigned int i;
            switch( paramValueType )
            {
            case TW_PARAM_INT32:
                for( i=0; i<inValueCount; i++ )
                    valuesStr = sdscatprintf(valuesStr, "%d%s", ((const int *)(inValues))[i], ((i<inValueCount-1) ? " " : ""));
                break;
            case TW_PARAM_FLOAT:
                for( i=0; i<inValueCount; i++ )
                    valuesStr = sdscatprintf(valuesStr, "%g%s", ((const float *)(inValues))[i], ((i<inValueCount-1) ? " " : ""));
                break;
            case TW_PARAM_DOUBLE:
                for( i=0; i<inValueCount; i++ )
                    valuesStr = sdscatprintf(valuesStr, "%g%s", ((const double *)(inValues))[i], ((i<inValueCount-1) ? " " : ""));
                break;
            case TW_PARAM_CSTRING:
                /*
                for( i=0; i<inValueCount; i++ )
                {
                    valuesStr << '`';
                    const char *str = ((char * const *)(inValues))[i];
                    for( const char *ch = str; *ch!=0; ch++ )
                        if( *ch=='`' )
                            valuesStr << "`'`'`";
                        else
                            valuesStr << *ch;
                    valuesStr << "` ";
                }
                */
                if( inValueCount!=1 )
                {
                    CTwMgr_SetLastError(g_TwMgr, g_ErrCStrParam); // count for CString param must be 1
                    sdsfree(valuesStr);
                    TwFPU_Restore(fpuState);
                    return 0;
                }
                else
                    valuesStr = sdscat(valuesStr, (const char *)(inValues));
                break;
            default:
                CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam); // Unknown param value type
                sdsfree(valuesStr);
                TwFPU_Restore(fpuState);
                return 0;
            }
            ret = BarVarSetAttrib(bar, var, varParent, varIndex, paramID, valuesStr);
            sdsfree(valuesStr);
        }
        else
            ret = BarVarSetAttrib(bar, var, varParent, varIndex, paramID, NULL);
        if( ret==0 )
        {
            bool errMsg = (CTwMgr_CheckLastError(g_TwMgr)!=NULL && strlen(CTwMgr_CheckLastError(g_TwMgr))>0 && PrevLastErrorPtr!=CTwMgr_CheckLastError(g_TwMgr));
            _snprintf(g_ErrParse, sizeof(g_ErrParse), "Unable to set param '%s%s%s %s' %s%s",
                      (bar==TW_GLOBAL_BAR) ? "GLOBAL" : bar->m_Name, (var!=NULL) ? "/" : "",
                      (var!=NULL) ? varName : "", paramName, errMsg ? " : " : "",
                      errMsg ? CTwMgr_CheckLastError(g_TwMgr) : "");
            g_ErrParse[sizeof(g_ErrParse)-1] = '\0';
            CTwMgr_SetLastError(g_TwMgr, g_ErrParse);
        }
        TwFPU_Restore(fpuState);
        return ret;
    }
    else
    {
        _snprintf(g_ErrParse, sizeof(g_ErrParse), "Unknown param '%s%s%s %s'",
                  (bar==TW_GLOBAL_BAR) ? "GLOBAL" : bar->m_Name,
                  (var!=NULL) ? "/" : "", (var!=NULL) ? varName : "", paramName);
        g_ErrParse[sizeof(g_ErrParse)-1] = '\0';
        CTwMgr_SetLastError(g_TwMgr, g_ErrParse);
        TwFPU_Restore(fpuState);
        return 0;
    }
}

//  ---------------------------------------------------------------------------

void CStructProxy_Init(CStructProxy *_Proxy)
{
    memset(_Proxy, 0, sizeof(*_Proxy));
}

void CStructProxy_Free(CStructProxy *_Proxy)
{
    if( _Proxy->m_StructData!=NULL && _Proxy->m_DeleteStructData )
    {
        //if( _Proxy->m_StructExtData==NULL && g_TwMgr!=NULL && _Proxy->m_Type>=TW_TYPE_STRUCT_BASE && _Proxy->m_Type<TW_TYPE_STRUCT_BASE+(int)g_TwMgr->m_Structs.size() )
        //  g_TwMgr->UninitVarData(_Proxy->m_Type, _Proxy->m_StructData, g_TwMgr->m_Structs[_Proxy->m_Type-TW_TYPE_STRUCT_BASE].m_Size);
        free(_Proxy->m_StructData);
    }
    if( _Proxy->m_StructExtData!=NULL )
    {
        //if( g_TwMgr!=NULL && _Proxy->m_Type>=TW_TYPE_STRUCT_BASE && _Proxy->m_Type<TW_TYPE_STRUCT_BASE+(int)g_TwMgr->m_Structs.size() )
        //  g_TwMgr->UninitVarData(_Proxy->m_Type, _Proxy->m_StructExtData, g_TwMgr->m_Structs[_Proxy->m_Type-TW_TYPE_STRUCT_BASE].m_Size);
        free(_Proxy->m_StructExtData);
    }
    memset(_Proxy, 0, sizeof(*_Proxy));
}

CStructProxy *CStructProxy_New(CTwMgr *_Mgr)
{
    CStructProxyNode *Node = (CStructProxyNode *)malloc(sizeof(CStructProxyNode));
    CStructProxy_Init(&Node->Proxy);
    Node->Next = _Mgr->m_StructProxies;
    _Mgr->m_StructProxies = Node;
    return &Node->Proxy;
}

void CMemberProxy_Init(CMemberProxy *_Proxy)
{
    memset(_Proxy, 0, sizeof(*_Proxy));
}

void CMemberProxy_Free(CMemberProxy *_Proxy)
{
    memset(_Proxy, 0, sizeof(*_Proxy));
}

CMemberProxy *CMemberProxy_New(CTwMgr *_Mgr)
{
    CMemberProxyNode *Node = (CMemberProxyNode *)malloc(sizeof(CMemberProxyNode));
    CMemberProxy_Init(&Node->Proxy);
    Node->Next = _Mgr->m_MemberProxies;
    _Mgr->m_MemberProxies = Node;
    return &Node->Proxy;
}

void ANT_CALL CMemberProxy_SetCB(const void *_Value, void *_ClientData)
{
    if( _ClientData && _Value )
    {
        const CMemberProxy *mProxy = (const CMemberProxy *)(_ClientData);
        if( g_TwMgr && mProxy )
        {
            const CStructProxy *sProxy = mProxy->m_StructProxy;
            if( sProxy && sProxy->m_StructData && sProxy->m_Type>=TW_TYPE_STRUCT_BASE && sProxy->m_Type<TW_TYPE_STRUCT_BASE+(int)g_TwMgr->m_Structs.count )
            {
                CStruct *s = &g_TwMgr->m_Structs.items[sProxy->m_Type-TW_TYPE_STRUCT_BASE];
                if( mProxy->m_MemberIndex>=0 && mProxy->m_MemberIndex<(int)s->m_Members.count )
                {
                    CStructMember *m = &s->m_Members.items[mProxy->m_MemberIndex];
                    if( m->m_Size>0 && m->m_Type!=TW_TYPE_BUTTON )
                    {
                        if( s->m_IsExt )
                        {
                            memcpy((char *)sProxy->m_StructExtData + m->m_Offset, _Value, m->m_Size);
                            if( s->m_CopyVarFromExtCallback && sProxy->m_StructExtData )
                                s->m_CopyVarFromExtCallback(sProxy->m_StructData, sProxy->m_StructExtData, mProxy->m_MemberIndex, (s->m_ExtClientData==g_PassProxyAsClientData) ? _ClientData : s->m_ExtClientData);
                        }
                        else
                            memcpy((char *)sProxy->m_StructData + m->m_Offset, _Value, m->m_Size);
                        if( sProxy->m_StructSetCallback )
                            sProxy->m_StructSetCallback(sProxy->m_StructData, sProxy->m_StructClientData);
                    }
                }
            }
        }
    }
}

void ANT_CALL CMemberProxy_GetCB(void *_Value, void *_ClientData)
{
    if( _ClientData && _Value )
    {
        const CMemberProxy *mProxy = (const CMemberProxy *)(_ClientData);
        if( g_TwMgr && mProxy )
        {
            const CStructProxy *sProxy = mProxy->m_StructProxy;
            if( sProxy && sProxy->m_StructData && sProxy->m_Type>=TW_TYPE_STRUCT_BASE && sProxy->m_Type<TW_TYPE_STRUCT_BASE+(int)g_TwMgr->m_Structs.count )
            {
                CStruct *s = &g_TwMgr->m_Structs.items[sProxy->m_Type-TW_TYPE_STRUCT_BASE];
                if( mProxy->m_MemberIndex>=0 && mProxy->m_MemberIndex<(int)s->m_Members.count )
                {
                    CStructMember *m = &s->m_Members.items[mProxy->m_MemberIndex];
                    if( m->m_Size>0 && m->m_Type!=TW_TYPE_BUTTON )
                    {
                        if( sProxy->m_StructGetCallback )
                            sProxy->m_StructGetCallback(sProxy->m_StructData, sProxy->m_StructClientData);
                        if( s->m_IsExt )
                        {
                            if( s->m_CopyVarToExtCallback && sProxy->m_StructExtData )
                                s->m_CopyVarToExtCallback(sProxy->m_StructData, sProxy->m_StructExtData, mProxy->m_MemberIndex,  (s->m_ExtClientData==g_PassProxyAsClientData) ? _ClientData : s->m_ExtClientData);
                            memcpy(_Value, (char *)sProxy->m_StructExtData + m->m_Offset, m->m_Size);
                        }
                        else
                            memcpy(_Value, (char *)sProxy->m_StructData + m->m_Offset, m->m_Size);
                    }
                }
            }
        }
    }
}

//  ---------------------------------------------------------------------------

static int s_SeparatorTag = 0;

//  ---------------------------------------------------------------------------

static int AddVar(TwBar *_Bar, const char *_Name, ETwType _Type, void *_VarPtr, bool _ReadOnly, TwSetVarCallback _SetCallback, TwGetVarCallback _GetCallback, TwButtonCallback _ButtonCallback, void *_ClientData, const char *_Def)
{
    unsigned int fpuState = TwFPU_Save(); // force fpu precision

    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        TwFPU_Restore(fpuState);
        return 0; // not initialized
    }

    char unnamedVarName[64];
    if( _Name==NULL || strlen(_Name)==0 ) // create a name automatically
    {
        static unsigned int s_UnnamedVarCount = 0;
        _snprintf(unnamedVarName, sizeof(unnamedVarName), "TW_UNNAMED_%04X", s_UnnamedVarCount);
        _Name = unnamedVarName;
        ++s_UnnamedVarCount;
    }

    if( _Bar==NULL || _Name==NULL || strlen(_Name)==0 || (_VarPtr==NULL && _GetCallback==NULL && _Type!=TW_TYPE_BUTTON) )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        TwFPU_Restore(fpuState);
        return 0;
    }
    if( CTwBar_Find(_Bar, _Name, NULL, NULL)!=NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrExist);
        TwFPU_Restore(fpuState);
        return 0;
    }

    if( strstr(_Name, "`")!=NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrNoBackQuote);
        TwFPU_Restore(fpuState);
        return 0;
    }

    if( _VarPtr==NULL && _Type!=TW_TYPE_BUTTON && _GetCallback!=NULL && _SetCallback==NULL )
        _ReadOnly = true;   // force readonly in this case

    // Convert color types
    if( _Type==TW_TYPE_COLOR32 )
        _Type = g_TwMgr->m_TypeColor32;
    else if( _Type==TW_TYPE_COLOR3F )
        _Type = g_TwMgr->m_TypeColor3F;
    else if( _Type==TW_TYPE_COLOR4F )
        _Type = g_TwMgr->m_TypeColor4F;

    // Convert rotation types
    if( _Type==TW_TYPE_QUAT4F )
        _Type = g_TwMgr->m_TypeQuat4F;
    else if( _Type==TW_TYPE_QUAT4D )
        _Type = g_TwMgr->m_TypeQuat4D;
    else if( _Type==TW_TYPE_DIR3F )
        _Type = g_TwMgr->m_TypeDir3F;
    else if( _Type==TW_TYPE_DIR3D )
        _Type = g_TwMgr->m_TypeDir3D;

    if(    (_Type>TW_TYPE_UNDEF && _Type<TW_TYPE_STRUCT_BASE)
        || (_Type>=TW_TYPE_ENUM_BASE && _Type<TW_TYPE_ENUM_BASE+(int)g_TwMgr->m_Enums.count)
        || (_Type>TW_TYPE_CSSTRING_BASE && _Type<=TW_TYPE_CSSTRING_MAX)
        || IsCustomType(_Type) ) // (_Type>=TW_TYPE_CUSTOM_BASE && _Type<TW_TYPE_CUSTOM_BASE+(int)g_TwMgr->m_Customs.size()) )
    {
        CTwVarAtom *Var = CTwVarAtom_New();
        Var->m_Base.m_Name = sdscpy(Var->m_Base.m_Name, _Name);
        Var->m_Ptr = _VarPtr;
        Var->m_Type = _Type;
        Var->m_Base.m_ColorPtr = &(_Bar->m_ColLabelText);
        if( _VarPtr!=NULL )
        {
            assert( _GetCallback==NULL && _SetCallback==NULL && _ButtonCallback==NULL );

            Var->m_ReadOnly = _ReadOnly;
            Var->m_GetCallback = NULL;
            Var->m_SetCallback = NULL;
            Var->m_ClientData = NULL;
        }
        else
        {
            assert( _GetCallback!=NULL || _Type==TW_TYPE_BUTTON );

            Var->m_GetCallback = _GetCallback;
            Var->m_SetCallback = _SetCallback;
            Var->m_ClientData = _ClientData;
            if( _Type==TW_TYPE_BUTTON )
            {
                Var->m_Val.m_Button.m_Callback = _ButtonCallback;
                if( _ButtonCallback==NULL && _ClientData==&s_SeparatorTag )
                {
                    Var->m_Val.m_Button.m_Separator = 1;
                    Var->m_Base.m_Label = sdscpy(Var->m_Base.m_Label, " ");
                }
                else if( _ButtonCallback==NULL )
                    Var->m_Base.m_ColorPtr = &(_Bar->m_ColStaticText);
            }
            if( _Type!=TW_TYPE_BUTTON )
                Var->m_ReadOnly = (_SetCallback==NULL || _ReadOnly);
            else
                Var->m_ReadOnly = (_ButtonCallback==NULL);
        }
        CTwVarAtom_SetDefaults(Var);

        if( IsCustomType(_Type) ) // _Type>=TW_TYPE_CUSTOM_BASE && _Type<TW_TYPE_CUSTOM_BASE+(int)g_TwMgr->m_Customs.size() )
        {
            if( Var->m_GetCallback==CMemberProxy_GetCB && Var->m_SetCallback==CMemberProxy_SetCB )
                Var->m_Val.m_Custom.m_MemberProxy = (CMemberProxy *)(Var->m_ClientData);
            else
                Var->m_Val.m_Custom.m_MemberProxy = NULL;
        }

        tw_da_append(&_Bar->m_VarRoot.m_Vars, &Var->m_Base);
        CTwBar_NotUpToDate(_Bar);
        g_TwMgr->m_HelpBarNotUpToDate = true;

        if( _Def!=NULL && strlen(_Def)>0 )
        {
            sds d = sdscatprintf(sdsempty(), "`%s`/`%s` %s", _Bar->m_Name, _Name, _Def);
            int ret = TwDefine(d);
            sdsfree(d);
            TwFPU_Restore(fpuState);
            return ret;
        }
        else
        {
            TwFPU_Restore(fpuState);
            return 1;
        }
    }
    else if(_Type>=TW_TYPE_STRUCT_BASE && _Type<TW_TYPE_STRUCT_BASE+(TwType)g_TwMgr->m_Structs.count)
    {
        CStruct *s = &g_TwMgr->m_Structs.items[_Type-TW_TYPE_STRUCT_BASE];
        CStructProxy *sProxy = NULL;
        void *vPtr;
        if( !s->m_IsExt )
        {
            if( _VarPtr!=NULL )
                vPtr = _VarPtr;
            else
            {
                assert( _GetCallback!=NULL || _SetCallback!=NULL );
                assert( s->m_Size>0 );
                vPtr = malloc(s->m_Size);
                memset(vPtr, 0, s->m_Size);
                // create a new StructProxy
                sProxy = CStructProxy_New(g_TwMgr);
                sProxy->m_Type = _Type;
                sProxy->m_StructData = vPtr;
                sProxy->m_DeleteStructData = true;
                sProxy->m_StructSetCallback = _SetCallback;
                sProxy->m_StructGetCallback = _GetCallback;
                sProxy->m_StructClientData = _ClientData;
                sProxy->m_CustomDrawCallback = NULL;
                sProxy->m_CustomMouseButtonCallback = NULL;
                sProxy->m_CustomMouseMotionCallback = NULL;
                sProxy->m_CustomMouseLeaveCallback = NULL;
                sProxy->m_CustomCaptureFocus = false;
                sProxy->m_CustomIndexFirst = -1;
                sProxy->m_CustomIndexLast = -1;
                //g_TwMgr->InitVarData(sProxy->m_Type, sProxy->m_StructData, s->m_Size);
            }
        }
        else // s->m_IsExt
        {
            assert( s->m_Size>0 && s->m_ClientStructSize>0 );
            vPtr = malloc(s->m_Size);  // will be m_StructExtData
            memset(vPtr, 0, s->m_Size);
            // create a new StructProxy
            sProxy = CStructProxy_New(g_TwMgr);
            sProxy->m_Type = _Type;
            sProxy->m_StructExtData = vPtr;
            sProxy->m_StructSetCallback = _SetCallback;
            sProxy->m_StructGetCallback = _GetCallback;
            sProxy->m_StructClientData = _ClientData;
            sProxy->m_CustomDrawCallback = NULL;
            sProxy->m_CustomMouseButtonCallback = NULL;
            sProxy->m_CustomMouseMotionCallback = NULL;
            sProxy->m_CustomMouseLeaveCallback = NULL;
            sProxy->m_CustomCaptureFocus = false;
            sProxy->m_CustomIndexFirst = -1;
            sProxy->m_CustomIndexLast = -1;
            //g_TwMgr->InitVarData(sProxy->m_Type, sProxy->m_StructExtData, s->m_Size);
            if( _VarPtr!=NULL )
            {
                sProxy->m_StructData = _VarPtr;
                sProxy->m_DeleteStructData = false;
            }
            else
            {
                sProxy->m_StructData = malloc(s->m_ClientStructSize);
                memset(sProxy->m_StructData, 0, s->m_ClientStructSize);
                sProxy->m_DeleteStructData = true;
                //g_TwMgr->InitVarData(ClientStructType, sProxy->m_StructData, s->m_ClientStructSize); //ClientStructType is unknown
            }
            _VarPtr = NULL; // force use of TwAddVarCB for members

            // init m_StructExtdata
            if( s->m_ExtClientData==g_PassProxyAsClientData )
                s->m_StructExtInitCallback(sProxy->m_StructExtData, sProxy);
            else
                s->m_StructExtInitCallback(sProxy->m_StructExtData, s->m_ExtClientData);
        }

        for( int i=0; i<(int)s->m_Members.count; ++i )
        {
            CStructMember *m = &s->m_Members.items[i];
            sds name = sdscatprintf(sdsempty(), "%s.%s", _Name, m->m_Name);
            const char *access = "";
            if( _ReadOnly )
                access = "readonly ";
            sds def = sdscatprintf(sdsempty(), "label=`%s` group=`%s` %s", m->m_Name, _Name, access); // + m->m_DefString;  // member def must be done after group def
            if( _VarPtr!=NULL )
            {
                if( TwAddVarRW(_Bar, name, m->m_Type, (char*)vPtr+m->m_Offset, def)==0 )
                {
                    sdsfree(name);
                    sdsfree(def);
                    TwFPU_Restore(fpuState);
                    return 0;
                }
            }
            else
            {
                assert( sProxy!=NULL );
                // create a new MemberProxy
                CMemberProxy *mProxy = CMemberProxy_New(g_TwMgr);
                mProxy->m_StructProxy = sProxy;
                mProxy->m_MemberIndex = i;
                if( TwAddVarCB(_Bar, name, m->m_Type, CMemberProxy_SetCB, CMemberProxy_GetCB, mProxy, def)==0 )
                {
                    sdsfree(name);
                    sdsfree(def);
                    TwFPU_Restore(fpuState);
                    return 0;
                }
                mProxy->m_Var = (CTwVar *)CTwBar_Find(_Bar, name, &mProxy->m_VarParent, NULL);
                mProxy->m_Bar = _Bar;
            }
            sdsfree(name);
            sdsfree(def);

            if( sProxy!=NULL && IsCustomType(m->m_Type) ) // m->m_Type>=TW_TYPE_CUSTOM_BASE && m->m_Type<TW_TYPE_CUSTOM_BASE+(int)g_TwMgr->m_Customs.size() )
            {
                if( sProxy->m_CustomIndexFirst<0 )
                    sProxy->m_CustomIndexFirst = sProxy->m_CustomIndexLast = i;
                else
                    sProxy->m_CustomIndexLast = i;
            }
        }
        char structInfo[64];
        sprintf(structInfo, "typeid=%d valptr=%p close ", _Type, vPtr);
        sds grpDef = sdscatprintf(sdsempty(), "`%s`/`%s` %s", _Bar->m_Name, _Name, structInfo);
        if( _Def!=NULL && strlen(_Def)>0 )
            grpDef = sdscat(grpDef, _Def);
        int ret = TwDefine(grpDef);
        sdsfree(grpDef);
        for( int i=0; i<(int)s->m_Members.count; ++i ) // members must be defined even if grpDef has error
        {
            CStructMember *m = &s->m_Members.items[i];
            if( sdslen(m->m_DefString)>0 )
            {
                sds memberDef = sdscatprintf(sdsempty(), "`%s`/`%s.%s` %s", _Bar->m_Name, _Name, m->m_Name, m->m_DefString);
                if( !TwDefine(memberDef) ) // all members must be defined even if memberDef has error
                    ret = 0;
                sdsfree(memberDef);
            }
        }
        TwFPU_Restore(fpuState);
        return ret;
    }
    else
    {
        if( _Type==TW_TYPE_CSSTRING_BASE )
            CTwMgr_SetLastError(g_TwMgr, g_ErrBadSize); // static string of size null
        else
            CTwMgr_SetLastError(g_TwMgr, g_ErrNotFound);
        TwFPU_Restore(fpuState);
        return 0;
    }
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwAddVarRW(TwBar *_Bar, const char *_Name, ETwType _Type, void *_Var, const char *_Def)
{
    return AddVar(_Bar, _Name, _Type, _Var, false, NULL, NULL, NULL, NULL, _Def);
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwAddVarRO(TwBar *_Bar, const char *_Name, ETwType _Type, const void *_Var, const char *_Def)
{
    return AddVar(_Bar, _Name, _Type, (void *)(_Var), true, NULL, NULL, NULL, NULL, _Def);
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwAddVarCB(TwBar *_Bar, const char *_Name, ETwType _Type, TwSetVarCallback _SetCallback, TwGetVarCallback _GetCallback, void *_ClientData, const char *_Def)
{
    return AddVar(_Bar, _Name, _Type, NULL, false, _SetCallback, _GetCallback, NULL, _ClientData, _Def);
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwAddButton(TwBar *_Bar, const char *_Name, TwButtonCallback _Callback, void *_ClientData, const char *_Def)
{
    return AddVar(_Bar, _Name, TW_TYPE_BUTTON, NULL, false, NULL, NULL, _Callback, _ClientData, _Def);
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwAddSeparator(TwBar *_Bar, const char *_Name, const char *_Def)
{
    return AddVar(_Bar, _Name, TW_TYPE_BUTTON, NULL, true, NULL, NULL, NULL, &s_SeparatorTag, _Def);
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwRemoveVar(TwBar *_Bar, const char *_Name)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return 0; // not initialized
    }
    if( _Bar==NULL || _Name==NULL || strlen(_Name)==0 )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        return 0;
    }

    if( g_TwMgr->m_PopupBar!=NULL && _Bar!=g_TwMgr->m_PopupBar )    // delete popup bar first if it exists
    {
        TwDeleteBar(g_TwMgr->m_PopupBar);
        g_TwMgr->m_PopupBar = NULL;
    }

    CTwBar_StopEditInPlace(_Bar);    // desactivate EditInPlace

    CTwVarGroup *Parent = NULL;
    int Index = -1;
    CTwVar *Var = (CTwVar *)CTwBar_Find(_Bar, _Name, &Parent, &Index);
    if( Var!=NULL && Parent!=NULL && Index>=0 )
    {
        if( Parent->m_StructValuePtr!=NULL )
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrDelStruct);
            return 0;
        }

        CTwVar_Delete(Var);
        tw_da_remove_ordered(&Parent->m_Vars, (size_t)Index);
        if( Parent!=&(_Bar->m_VarRoot) && Parent->m_Vars.count<=0 )
            TwRemoveVar(_Bar, Parent->m_Base.m_Name);
        CTwBar_NotUpToDate(_Bar);
        if( _Bar!=g_TwMgr->m_HelpBar )
            g_TwMgr->m_HelpBarNotUpToDate = true;
        return 1;
    }

    CTwMgr_SetLastError(g_TwMgr, g_ErrNotFound);
    return 0;
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwRemoveAllVars(TwBar *_Bar)
{
    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        return 0; // not initialized
    }
    if( _Bar==NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        return 0;
    }

    if( g_TwMgr->m_PopupBar!=NULL && _Bar!=g_TwMgr->m_PopupBar && _Bar!=g_TwMgr->m_HelpBar )    // delete popup bar first if it exists
    {
        TwDeleteBar(g_TwMgr->m_PopupBar);
        g_TwMgr->m_PopupBar = NULL;
    }

    CTwBar_StopEditInPlace(_Bar);    // desactivate EditInPlace

    for( size_t vi=0; vi<_Bar->m_VarRoot.m_Vars.count; ++vi )
        if( _Bar->m_VarRoot.m_Vars.items[vi] != NULL )
        {
            CTwVar_Delete(_Bar->m_VarRoot.m_Vars.items[vi]);
            _Bar->m_VarRoot.m_Vars.items[vi] = NULL;
        }
    _Bar->m_VarRoot.m_Vars.count = 0;
    CTwBar_NotUpToDate(_Bar);
    g_TwMgr->m_HelpBarNotUpToDate = true;
    return 1;
}

//  ---------------------------------------------------------------------------

int ParseToken(sds *_Token, const char *_Def, int *_Line, int *_Column, bool _KeepQuotes, bool _EndCR, char _Sep1, char _Sep2)
{
    const char *Cur = _Def;
    sdsfree(*_Token); // discard whatever the caller's sds held before this call
    *_Token = sdsempty();
    // skip spaces
    while( *Cur==' ' || *Cur=='\t' || *Cur=='\r' || *Cur=='\n' )
    {
        if( *Cur=='\n' && _EndCR )
            return (int)(Cur-_Def); // a CR has been found
        ++Cur;
        if( *Cur=='\n' )
        {
            ++(*_Line);
            *_Column = 1;
        }
        else if( *Cur=='\t' )
            *_Column += g_TabLength;
        else if( *Cur!='\r' )
            ++(*_Column);
    }
    // read token
    int QuoteLine=0, QuoteColumn=0;
    char Quote = 0;
    bool AddChar;
    bool LineJustIncremented = false;
    while(    (Quote==0 && (*Cur!='\0' && *Cur!=' ' && *Cur!='\t' && *Cur!='\r' && *Cur!='\n' && *Cur!=_Sep1 && *Cur!=_Sep2))
           || (Quote!=0 && (*Cur!='\0' /* && *Cur!='\r' && *Cur!='\n' */)) ) // allow multi-line strings
    {
        LineJustIncremented = false;
        AddChar = true;
        if( Quote==0 && (*Cur=='\'' || *Cur=='\"' || *Cur=='`') )
        {
            Quote = *Cur;
            QuoteLine = *_Line;
            QuoteColumn = *_Column;
            AddChar = _KeepQuotes;
        }
        else if ( Quote!=0 && *Cur==Quote )
        {
            Quote = 0;
            AddChar = _KeepQuotes;
        }

        if( AddChar )
            *_Token = sdscatlen(*_Token, Cur, 1);
        ++Cur;
        if( *Cur=='\t' )
            *_Column += g_TabLength;
        else if( *Cur=='\n' )
        {
            ++(*_Line);
            LineJustIncremented = true;
            *_Column = 1;
        }
        else
            ++(*_Column);
    }

    if( Quote!=0 )
    {
        *_Line = QuoteLine;
        *_Column = QuoteColumn;
        return -(int)(Cur-_Def);    // unclosed quote
    }
    else
    {
        if( *Cur=='\n' )
        {
            if( !LineJustIncremented )
                ++(*_Line);
            *_Column = 1;
        }
        else if( *Cur=='\t' )
            *_Column += g_TabLength;
        else if( *Cur!='\r' && *Cur!='\0' )
            ++(*_Column);
        return (int)(Cur-_Def);
    }
}

//  ---------------------------------------------------------------------------

int GetBarVarFromString(CTwBar **_Bar, CTwVar **_Var, CTwVarGroup **_VarParent, int *_VarIndex, const char *_Str)
{
    *_Bar = NULL;
    *_Var = NULL;
    *_VarParent = NULL;
    *_VarIndex = -1;
    // Was vector<string> Names - the loop condition (NamesCount<=3, checked
    // BEFORE each push) allows NamesCount to reach 4, not 3, so this fixed
    // array must hold 4 slots even though only Names[0]/Names[1] are ever
    // read afterward.
    sds Names[4] = { NULL, NULL, NULL, NULL };
    int NamesCount = 0;
    sds Token = sdsempty();
    const char *Cur =_Str;
    int l=1, c=1, p=1;
    while( *Cur!='\0' && p>0 && NamesCount<=3 )
    {
        p = ParseToken(&Token, Cur, &l, &c, false, true, '/', '\\');
        if( p>0 && sdslen(Token)>0 )
        {
            Names[NamesCount] = sdsdup(Token);
            ++NamesCount;
            Cur += p + ((Cur[p]!='\0')?1:0);
        }
    }
    sdsfree(Token);
    if( p<=0 || (NamesCount!=1 && NamesCount!=2) )
    {
        for( int i=0; i<NamesCount; ++i )
            sdsfree(Names[i]);
        return 0;   // parse error
    }
    int BarIdx = CTwMgr_FindBar(g_TwMgr, Names[0]);
    if( BarIdx<0 )
    {
        int ret;
        if( NamesCount==1 && strcmp(Names[0], "GLOBAL")==0 )
        {
            *_Bar = TW_GLOBAL_BAR;
            ret = +3;  // 'GLOBAL' found
        }
        else
            ret = -1;  // bar not found
        for( int i=0; i<NamesCount; ++i )
            sdsfree(Names[i]);
        return ret;
    }
    *_Bar = g_TwMgr->m_Bars.items[BarIdx];
    if( NamesCount==1 )
    {
        sdsfree(Names[0]);
        return 1;   // bar found, no var name parsed
    }
    *_Var = (CTwVar *)CTwBar_Find(*_Bar, Names[1], _VarParent, _VarIndex);
    int ret = (*_Var==NULL) ? -2 : 2;  // var not found / bar and var found
    for( int i=0; i<NamesCount; ++i )
        sdsfree(Names[i]);
    return ret;
}


int BarVarHasAttrib(CTwBar *_Bar, CTwVar *_Var, const char *_Attrib, bool *_HasValue)
{
    assert(_Bar!=NULL && _HasValue!=NULL && _Attrib!=NULL && strlen(_Attrib)>0);
    *_HasValue = false;
    if( _Bar==TW_GLOBAL_BAR )
    {
        assert( _Var==NULL );
        return CTwMgr_HasAttrib(g_TwMgr, _Attrib, _HasValue);
    }
    else if( _Var==NULL )
        return CTwBar_HasAttrib(_Bar, _Attrib, _HasValue);
    else
        return CTwVar_HasAttrib(_Var, _Attrib, _HasValue);
}


int BarVarSetAttrib(CTwBar *_Bar, CTwVar *_Var, CTwVarGroup *_VarParent, int _VarIndex, int _AttribID, const char *_Value)
{
    assert(_Bar!=NULL && _AttribID>0);

    /* don't delete popupbar here: if any attrib is changed every frame by the app, popup will not work anymore.
    if( g_TwMgr->m_PopupBar!=NULL && _Bar!=g_TwMgr->m_PopupBar && g_TwMgr->m_PopupBar->m_BarLinkedToPopupList==_Bar )   // delete popup bar first if it exists
    {
        TwDeleteBar(g_TwMgr->m_PopupBar);
        g_TwMgr->m_PopupBar = NULL;
    }
    */

    if( _Bar==TW_GLOBAL_BAR )
    {
        assert( _Var==NULL );
        return CTwMgr_SetAttrib(g_TwMgr, _AttribID, _Value);
    }
    else if( _Var==NULL )
        return CTwBar_SetAttrib(_Bar, _AttribID, _Value);
    else
        return CTwVar_SetAttrib(_Var, _AttribID, _Value, _Bar, _VarParent, _VarIndex);
    // don't make _Bar not-up-to-date here, should be done in SetAttrib if needed to avoid too frequent refreshs
}
 

ERetType BarVarGetAttrib(CTwBar *_Bar, CTwVar *_Var, CTwVarGroup *_VarParent, int _VarIndex, int _AttribID, CDoubleArray *outDoubles, sds *outString)
{
    assert(_Bar!=NULL && _AttribID>0);

    if( _Bar==TW_GLOBAL_BAR )
    {
        assert( _Var==NULL );
        return CTwMgr_GetAttrib(g_TwMgr, _AttribID, outDoubles, outString);
    }
    else if( _Var==NULL )
        return CTwBar_GetAttrib(_Bar, _AttribID, outDoubles, outString);
    else
        return CTwVar_GetAttrib(_Var, _AttribID, _Bar, _VarParent, _VarIndex, outDoubles, outString);
}

//  ---------------------------------------------------------------------------

static inline void ErrorPosition(char *_Out, size_t _OutSize, bool _MultiLine, int _Line, int _Column)
{
    if( !_MultiLine )
        _Out[0] = '\0';
    else
    {
        //_snprintf(_Out, _OutSize-1, " line %d column %d", _Line, _Column);
        _snprintf(_Out, _OutSize-1, " line %d", _Line); (void)_Column;
        _Out[_OutSize-1] = '\0';
    }
}

//  ---------------------------------------------------------------------------

int ANT_CALL TwDefine(const char *_Def)
{
    unsigned int fpuState = TwFPU_Save(); // force fpu precision

    // hack to scale fonts artificially (for retina display for instance)
    if( g_TwMgr==NULL && _Def!=NULL )
    {
        size_t l = strlen(_Def);
        const char *eq = strchr(_Def, '=');
        if( eq!=NULL && eq!=_Def && l>0 && l<512 )
        {
            char *a = (char *)malloc(l+1);
            char *b = (char *)malloc(l+1);
            if( sscanf(_Def, "%s%s", a, b)==2 && strcmp(a, "GLOBAL")==0 )
            {
                if( strchr(b, '=') != NULL )
                    *strchr(b, '=') = '\0';
                double scal = 1.0;
                if( _stricmp(b, "fontscaling")==0 && sscanf(eq+1, "%lf", &scal)==1 && scal>0 )
                {
                    g_FontScaling = (float)scal;
                    free(a);
                    free(b);
                    TwFPU_Restore(fpuState);
                    return 1;
                }
            }
            free(a);
            free(b);
        }
    }

    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        TwFPU_Restore(fpuState);
        return 0; // not initialized
    }
    if( _Def==NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        TwFPU_Restore(fpuState);
        return 0;
    }

    bool MultiLine = false;
    const char *Cur = _Def;
    while( *Cur!='\0' )
    {
        if( *Cur=='\n' )
        {
            MultiLine = true;
            break;
        }
        ++Cur;
    }

    int Line = 1;
    int Column = 1;
    typedef enum EState { PARSE_NAME, PARSE_ATTRIB } EState;
    EState State = PARSE_NAME;
    sds Token = sdsempty();
    sds Value = sdsempty();
    char errPos[32];
    CTwBar *Bar = NULL;
    CTwVar *Var = NULL;
    CTwVarGroup *VarParent = NULL;
    int VarIndex = -1;
    int p;

    Cur = _Def;
    while( *Cur!='\0' )
    {
        const char *PrevCur = Cur;
        p = ParseToken(&Token, Cur, &Line, &Column, (State==PARSE_NAME), (State==PARSE_ATTRIB), (State==PARSE_ATTRIB)?'=':'\0', '\0');
        if( p<=0 || sdslen(Token)<=0 )
        {
            if( p>0 && Cur[p]=='\0' )
            {
                Cur += p;
                continue;
            }
            ErrorPosition(errPos, sizeof(errPos), MultiLine, Line, Column);
            _snprintf(g_ErrParse, sizeof(g_ErrParse), "Parsing error in def string%s [%-16s...]", errPos, (p<0)?(Cur-p):PrevCur);
            g_ErrParse[sizeof(g_ErrParse)-1] = '\0';
            CTwMgr_SetLastError(g_TwMgr, g_ErrParse);
            sdsfree(Token);
            sdsfree(Value);
            TwFPU_Restore(fpuState);
            return 0;
        }
        char CurSep = Cur[p];
        Cur += p + ((CurSep!='\0')?1:0);

        if( State==PARSE_NAME )
        {
            int Err = GetBarVarFromString(&Bar, &Var, &VarParent, &VarIndex, Token);
            if( Err<=0 )
            {
                ErrorPosition(errPos, sizeof(errPos), MultiLine, Line, Column);
                if( Err==-1 )
                    _snprintf(g_ErrParse, sizeof(g_ErrParse), "Parsing error in def string: Bar not found%s [%-16s...]", errPos, Token);
                else if( Err==-2 )
                    _snprintf(g_ErrParse, sizeof(g_ErrParse), "Parsing error in def string: Variable not found%s [%-16s...]", errPos, Token);
                else
                    _snprintf(g_ErrParse, sizeof(g_ErrParse), "Parsing error in def string%s [%-16s...]", errPos, Token);
                g_ErrParse[sizeof(g_ErrParse)-1] = '\0';
                CTwMgr_SetLastError(g_TwMgr, g_ErrParse);
                sdsfree(Token);
                sdsfree(Value);
                TwFPU_Restore(fpuState);
                return 0;
            }
            State = PARSE_ATTRIB;
        }
        else // State==PARSE_ATTRIB
        {
            assert(State==PARSE_ATTRIB);
            assert(Bar!=NULL);

            bool HasValue = false;
            Value = sdscpy(Value, "");
            int AttribID = BarVarHasAttrib(Bar, Var, Token, &HasValue);
            if( AttribID<=0 )
            {
                ErrorPosition(errPos, sizeof(errPos), MultiLine, Line, Column);
                _snprintf(g_ErrParse, sizeof(g_ErrParse), "Parsing error in def string: Unknown attribute%s [%-16s...]", errPos, Token);
                g_ErrParse[sizeof(g_ErrParse)-1] = '\0';
                CTwMgr_SetLastError(g_TwMgr, g_ErrParse);
                sdsfree(Token);
                sdsfree(Value);
                TwFPU_Restore(fpuState);
                return 0;
            }

            // special case for backward compatibility
            if( HasValue && ( _stricmp(Token, "readonly")==0 || _stricmp(Token, "hexa")==0 ) )
            {
                if( CurSep==' ' || CurSep=='\t' )
                {
                    const char *ch = Cur;
                    while( *ch==' ' || *ch=='\t' ) // find next non-space character
                        ++ch;
                    if( *ch!='=' ) // if this is not '=' the param has no value
                        HasValue = false;
                }
            }

            if( HasValue )
            {
                if( CurSep!='=' )
                {
                    sds EqualStr = sdsempty();
                    p = ParseToken(&EqualStr, Cur, &Line, &Column, true, true, '=', '\0');
                    CurSep = Cur[p];
                    if( p<0 || sdslen(EqualStr)>0 || CurSep!='=' )
                    {
                        ErrorPosition(errPos, sizeof(errPos), MultiLine, Line, Column);
                        _snprintf(g_ErrParse, sizeof(g_ErrParse), "Parsing error in def string: '=' not found while reading attribute value%s [%-16s...]", errPos, Token);
                        g_ErrParse[sizeof(g_ErrParse)-1] = '\0';
                        CTwMgr_SetLastError(g_TwMgr, g_ErrParse);
                        sdsfree(EqualStr);
                        sdsfree(Token);
                        sdsfree(Value);
                        TwFPU_Restore(fpuState);
                        return 0;
                    }
                    Cur += p + 1;
                    sdsfree(EqualStr);
                }
                p = ParseToken(&Value, Cur, &Line, &Column, false, true, '\0', '\0');
                if( p<=0 )
                {
                    ErrorPosition(errPos, sizeof(errPos), MultiLine, Line, Column);
                    _snprintf(g_ErrParse, sizeof(g_ErrParse), "Parsing error in def string: can't read attribute value%s [%-16s...]", errPos, Token);
                    g_ErrParse[sizeof(g_ErrParse)-1] = '\0';
                    CTwMgr_SetLastError(g_TwMgr, g_ErrParse);
                    sdsfree(Token);
                    sdsfree(Value);
                    TwFPU_Restore(fpuState);
                    return 0;
                }
                CurSep = Cur[p];
                Cur += p + ((CurSep!='\0')?1:0);
            }
            const char *PrevLastErrorPtr = CTwMgr_CheckLastError(g_TwMgr);
            if( BarVarSetAttrib(Bar, Var, VarParent, VarIndex, AttribID, HasValue?Value:NULL)==0 )
            {
                ErrorPosition(errPos, sizeof(errPos), MultiLine, Line, Column);
                if( CTwMgr_CheckLastError(g_TwMgr)==NULL || strlen(CTwMgr_CheckLastError(g_TwMgr))<=0 || CTwMgr_CheckLastError(g_TwMgr)==PrevLastErrorPtr )
                    _snprintf(g_ErrParse, sizeof(g_ErrParse), "Parsing error in def string: wrong attribute value%s [%-16s...]", errPos, Token);
                else
                    _snprintf(g_ErrParse, sizeof(g_ErrParse), "%s%s [%-16s...]", CTwMgr_CheckLastError(g_TwMgr), errPos, Token);
                g_ErrParse[sizeof(g_ErrParse)-1] = '\0';
                CTwMgr_SetLastError(g_TwMgr, g_ErrParse);
                sdsfree(Token);
                sdsfree(Value);
                TwFPU_Restore(fpuState);
                return 0;
            }
            // sweep spaces to detect next attrib
            while( *Cur==' ' || *Cur=='\t' || *Cur=='\r' )
            {
                ++Cur;
                if( *Cur=='\t' )
                    Column += g_TabLength;
                else if( *Cur!='\r' )
                    ++Column;
            }
            if( *Cur=='\n' )    // new line detected
            {
                ++Line;
                Column = 1;
                State = PARSE_NAME;
            }
        }
    }

    g_TwMgr->m_HelpBarNotUpToDate = true;
    sdsfree(Token);
    sdsfree(Value);
    TwFPU_Restore(fpuState);
    return 1;
}

//  ---------------------------------------------------------------------------

TwType ANT_CALL TwDefineEnum(const char *_Name, const TwEnumVal *_EnumValues, unsigned int _NbValues)
{
    unsigned int fpuState = TwFPU_Save(); // force fpu precision

    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        TwFPU_Restore(fpuState);
        return TW_TYPE_UNDEF; // not initialized
    }
    if( _EnumValues==NULL && _NbValues!=0 )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        TwFPU_Restore(fpuState);
        return TW_TYPE_UNDEF;
    }

    if( g_TwMgr->m_PopupBar!=NULL ) // delete popup bar first if it exists
    {
        TwDeleteBar(g_TwMgr->m_PopupBar);
        g_TwMgr->m_PopupBar = NULL;
    }

    size_t enumIndex = g_TwMgr->m_Enums.count;
    if( _Name!=NULL && strlen(_Name)>0 )
        for( size_t j=0; j<g_TwMgr->m_Enums.count; ++j )
            if( strcmp(_Name, g_TwMgr->m_Enums.items[j].m_Name)==0 )
            {
                enumIndex = j;
                break;
            }
    if( enumIndex==g_TwMgr->m_Enums.count )
    {
        CEnum NewEnum;
        NewEnum.m_Name = sdsempty();
        NewEnum.m_Entries.items = NULL;
        NewEnum.m_Entries.count = 0;
        NewEnum.m_Entries.capacity = 0;
        tw_da_append(&g_TwMgr->m_Enums, NewEnum);
    }
    assert( enumIndex>=0 && enumIndex<g_TwMgr->m_Enums.count );
    CEnum *e = &g_TwMgr->m_Enums.items[enumIndex];
    if( _Name!=NULL && strlen(_Name)>0 )
        e->m_Name = sdscpy(e->m_Name, _Name);
    else
        sdsclear(e->m_Name);
    CEnum_Clear(e);
    for(unsigned int i=0; i<_NbValues; ++i)
        CEnum_InsertOrReplace(e, _EnumValues[i].Value, (_EnumValues[i].Label!=NULL)?_EnumValues[i].Label:"");

    TwFPU_Restore(fpuState);
    return (TwType)( TW_TYPE_ENUM_BASE + enumIndex );
}

//  ---------------------------------------------------------------------------

TwType TW_CALL TwDefineEnumFromString(const char *_Name, const char *_EnumString)
{
    if (_EnumString == NULL)
        return TwDefineEnum(_Name, NULL, 0);

    // split enumString on ',', trimming each segment - was stringstream +
    // getline(...,',') + find_first_not_of/find_last_not_of(" \n\r\t"); this
    // manual scan replicates getline's exact splitting behavior, including
    // producing no trailing empty element after a final trailing comma
    // (matching that getline fails, rather than returning one more empty
    // read, once the stream position is already at end-of-input).
    CSdsArray Labels = {0};
    {
        const char *p = _EnumString;
        for(;;)
        {
            if( *p=='\0' )
                break;
            const char *comma = strchr(p, ',');
            size_t segLen = comma ? (size_t)(comma-p) : strlen(p);
            const char *segStart = p;
            const char *segEnd = p+segLen; // one past the last char
            while( segStart<segEnd && (*segStart==' ' || *segStart=='\n' || *segStart=='\r' || *segStart=='\t') )
                ++segStart;
            while( segEnd>segStart && (*(segEnd-1)==' ' || *(segEnd-1)=='\n' || *(segEnd-1)=='\r' || *(segEnd-1)=='\t') )
                --segEnd;
            tw_da_append(&Labels, sdsnewlen(segStart, (size_t)(segEnd-segStart)));
            if( !comma )
                break;
            p = comma+1;
        }
    }
    // create TwEnumVal array - Vals[i].Label points directly into Labels'
    // owned sds strings, valid until Labels is freed below (TwDefineEnum
    // itself copies every label it needs before returning, same as the
    // original vector<string>-backed version relied on).
    typedef struct { TwEnumVal *items; size_t count; size_t capacity; } CTwEnumValArray;
    CTwEnumValArray Vals = {0};
    tw_da_resize(&Vals, Labels.count);
    for( size_t i=0; i<Labels.count; ++i )
    {
        Vals.items[i].Value = (int)i;
        Vals.items[i].Label = Labels.items[i];
    }

    TwType ret = TwDefineEnum(_Name, (Vals.count==0) ? NULL : Vals.items, (unsigned int)Vals.count);

    tw_da_free(&Vals);
    for( size_t i=0; i<Labels.count; ++i )
        sdsfree(Labels.items[i]);
    tw_da_free(&Labels);
    return ret;
}

//  ---------------------------------------------------------------------------

void ANT_CALL CStruct_DefaultSummary(char *_SummaryString, size_t _SummaryMaxLength, const void *_Value, void *_ClientData)
{
    const CTwVarGroup *varGroup = ((const CTwVarGroup *)(_Value)); // special case
    if( _SummaryString && _SummaryMaxLength>0 )
        _SummaryString[0] = '\0';
    size_t structIndex = (size_t)(_ClientData);
    if(    g_TwMgr && _SummaryString && _SummaryMaxLength>2
        && varGroup && CTwVar_IsGroup(&varGroup->m_Base)
        && structIndex>=0 && structIndex<=g_TwMgr->m_Structs.count )
    {
        // return g_TwMgr->m_Structs.items[structIndex].m_Name;
        CStruct *s = &g_TwMgr->m_Structs.items[structIndex];
        _SummaryString[0] = '{';
        _SummaryString[1] = '\0';
        bool separator = false;
        for( size_t i=0; i<s->m_Members.count; ++i )
        {
            sds varName = sdscatprintf(sdsempty(), "%s.%s", varGroup->m_Base.m_Name, s->m_Members.items[i].m_Name);
            const CTwVar *var = CTwVarGroup_Find(varGroup, varName, NULL, NULL);
            sdsfree(varName);
            if( var )
            {
                if( CTwVar_IsGroup(var) )
                {
                    const CTwVarGroup *grp = ((const CTwVarGroup *)(var));
                    if( grp->m_SummaryCallback!=NULL )
                    {
                        size_t l = strlen(_SummaryString);
                        if( separator )
                        {
                            _SummaryString[l++] = ',';
                            _SummaryString[l++] = '\0';
                        }
                        if( grp->m_SummaryCallback==CStruct_DefaultSummary )
                            grp->m_SummaryCallback(_SummaryString+l, _SummaryMaxLength-l, grp, grp->m_SummaryClientData);
                        else
                            grp->m_SummaryCallback(_SummaryString+l, _SummaryMaxLength-l, grp->m_StructValuePtr, grp->m_SummaryClientData);
                        separator = true;
                    }
                }
                else
                {
                    size_t l = strlen(_SummaryString);
                    if( separator )
                    {
                        _SummaryString[l++] = ',';
                        _SummaryString[l++] = '\0';
                    }
                    sds valString = sdsempty();
                    const CTwVarAtom *atom = ((const CTwVarAtom *)(var));
                    CTwVarAtom_ValueToString(atom, &valString);
                    if( atom->m_Type==TW_TYPE_BOOLCPP || atom->m_Type==TW_TYPE_BOOL8 || atom->m_Type==TW_TYPE_BOOL16 || atom->m_Type==TW_TYPE_BOOL32 )
                    {
                        if (strcmp(valString, "0")==0)
                            valString = sdscpy(valString, "-");
                        else if (strcmp(valString, "1")==0)
                            valString = sdscpy(valString, "\x7f"); // check sign
                    }
                    strncat(_SummaryString, valString, _SummaryMaxLength-l);
                    sdsfree(valString);
                    separator = true;
                }
                if( strlen(_SummaryString)>_SummaryMaxLength-2 )
                    break;
            }
        }
        size_t l = strlen(_SummaryString);
        if( l>_SummaryMaxLength-2 )
        {
            _SummaryString[_SummaryMaxLength-2] = '.';
            _SummaryString[_SummaryMaxLength-1] = '.';
            _SummaryString[_SummaryMaxLength+0] = '\0';
        }
        else
        {
            _SummaryString[l+0] = '}';
            _SummaryString[l+1] = '\0';
        }
    }
}

//  ---------------------------------------------------------------------------

TwType ANT_CALL TwDefineStruct(const char *_StructName, const TwStructMember *_StructMembers, unsigned int _NbMembers, size_t _StructSize, TwSummaryCallback _SummaryCallback, void *_SummaryClientData)
{
    unsigned int fpuState = TwFPU_Save(); // force fpu precision

    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        TwFPU_Restore(fpuState);
        return TW_TYPE_UNDEF; // not initialized
    }
    if( _StructMembers==NULL || _NbMembers==0 || _StructSize==0 )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        TwFPU_Restore(fpuState);
        return TW_TYPE_UNDEF;
    }

    if( _StructName!=NULL && strlen(_StructName)>0 )
        for( size_t j=0; j<g_TwMgr->m_Structs.count; ++j )
            if( strcmp(_StructName, g_TwMgr->m_Structs.items[j].m_Name)==0 )
            {
                CTwMgr_SetLastError(g_TwMgr, g_ErrExist);
                TwFPU_Restore(fpuState);
                return TW_TYPE_UNDEF;
            }

    size_t structIndex = g_TwMgr->m_Structs.count;
    // Built locally then tw_da_append()ed below - a shallow struct-value
    // copy, since CStruct is now a plain struct (no more std::string/
    // std::vector deep-copying on push_back). The append transfers
    // ownership of s's sds/array fields to the array element it creates;
    // s is intentionally never freed here (freeing it would immediately
    // invalidate the copy the array now owns).
    CStruct s;
    s.m_Size = _StructSize;
    if( _StructName!=NULL && strlen(_StructName)>0 )
        s.m_Name = sdsnew(_StructName);
    else
        s.m_Name = sdsempty();
    s.m_Members.items = NULL;
    s.m_Members.count = 0;
    s.m_Members.capacity = 0;
    tw_da_resize(&s.m_Members, _NbMembers);
    if( _SummaryCallback!=NULL )
    {
        s.m_SummaryCallback = _SummaryCallback;
        s.m_SummaryClientData = _SummaryClientData;
    }
    else
    {
        s.m_SummaryCallback = CStruct_DefaultSummary;
        s.m_SummaryClientData = (void *)(structIndex);
    }
    s.m_Help = sdsempty();
    s.m_IsExt = false;
    s.m_ClientStructSize = 0;
    s.m_StructExtInitCallback = NULL;
    s.m_CopyVarFromExtCallback = NULL;
    s.m_CopyVarToExtCallback = NULL;
    s.m_ExtClientData = NULL;
    for( unsigned int i=0; i<_NbMembers; ++i )
    {
        // Every sds field is set to at least a safe sdsempty() before the
        // Offset validation below, which can return early - so at every
        // point from here on, s (and every member built so far) is fully
        // populated with valid, freeable sds pointers, never garbage from
        // tw_da_resize's uninitialized new elements.
        CStructMember *m = &s.m_Members.items[i];
        if( _StructMembers[i].Name!=NULL )
            m->m_Name = sdsnew(_StructMembers[i].Name);
        else
        {
            char name[16];
            sprintf(name, "%u", i);
            m->m_Name = sdsnew(name);
        }
        m->m_Label = sdsempty();
        m->m_Type = _StructMembers[i].Type;
        m->m_Size = 0;   // to avoid endless recursivity in GetDataSize
        m->m_Size = CTwVar_GetDataSize(m->m_Type);
        m->m_DefString = sdsempty();
        m->m_Help = sdsempty();
        if( _StructMembers[i].Offset<_StructSize )
            m->m_Offset = _StructMembers[i].Offset;
        else
        {
            // Bail out: free everything built so far (s.m_Members now
            // holds i+1 fully-initialized members, since this member's
            // sds fields were all set above before this check) - s was
            // never tw_da_append()ed, so nothing but this local owns it.
            CTwMgr_SetLastError(g_TwMgr, g_ErrOffset);
            for( unsigned int k=0; k<=i; ++k )
            {
                sdsfree(s.m_Members.items[k].m_Name);
                sdsfree(s.m_Members.items[k].m_Label);
                sdsfree(s.m_Members.items[k].m_DefString);
                sdsfree(s.m_Members.items[k].m_Help);
            }
            tw_da_free(&s.m_Members);
            sdsfree(s.m_Name);
            sdsfree(s.m_Help);
            TwFPU_Restore(fpuState);
            return TW_TYPE_UNDEF;
        }
        if( _StructMembers[i].DefString!=NULL && strlen(_StructMembers[i].DefString)>0 )
        {
            sdsfree(m->m_DefString);
            m->m_DefString = sdsnew(_StructMembers[i].DefString);
        }
    }

    tw_da_append(&g_TwMgr->m_Structs, s);
    assert( g_TwMgr->m_Structs.count==structIndex+1 );
    TwFPU_Restore(fpuState);
    return (TwType)( TW_TYPE_STRUCT_BASE + structIndex );
}

//  ---------------------------------------------------------------------------

TwType ANT_CALL TwDefineStructExt(const char *_StructName, const TwStructMember *_StructExtMembers, unsigned int _NbExtMembers, size_t _StructSize, size_t _StructExtSize, TwStructExtInitCallback _StructExtInitCallback, TwCopyVarFromExtCallback _CopyVarFromExtCallback, TwCopyVarToExtCallback _CopyVarToExtCallback, TwSummaryCallback _SummaryCallback, void *_ClientData, const char *_Help)
{
    unsigned int fpuState = TwFPU_Save(); // force fpu precision

    if( g_TwMgr==NULL )
    {
        TwGlobalError(g_ErrNotInit);
        TwFPU_Restore(fpuState);
        return TW_TYPE_UNDEF; // not initialized
    }
    if( _StructSize==0 || _StructExtInitCallback==NULL || _CopyVarFromExtCallback==NULL || _CopyVarToExtCallback==NULL )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrBadParam);
        TwFPU_Restore(fpuState);
        return TW_TYPE_UNDEF;
    }
    TwType type = TwDefineStruct(_StructName, _StructExtMembers, _NbExtMembers, _StructExtSize, _SummaryCallback, _ClientData);
    if( type>=TW_TYPE_STRUCT_BASE && type<TW_TYPE_STRUCT_BASE+(int)g_TwMgr->m_Structs.count )
    {
        CStruct *s = &g_TwMgr->m_Structs.items[type-TW_TYPE_STRUCT_BASE];
        s->m_IsExt = true;
        s->m_ClientStructSize = _StructSize;
        s->m_StructExtInitCallback = _StructExtInitCallback;
        s->m_CopyVarFromExtCallback = _CopyVarFromExtCallback;
        s->m_CopyVarToExtCallback = _CopyVarToExtCallback;
        s->m_ExtClientData = _ClientData;
        if( _Help!=NULL )
            s->m_Help = sdscpy(s->m_Help, _Help);
    }
    TwFPU_Restore(fpuState);
    return type;
}


//  ---------------------------------------------------------------------------

bool TwGetKeyCode(int *_Code, int *_Modif, const char *_String)
{
    assert(_Code!=NULL && _Modif!=NULL);
    bool Ok = true;
    *_Modif = TW_KMOD_NONE;
    *_Code = 0;
    size_t Start = strlen(_String)-1;
    if( Start<0 )
        return false;
    while( Start>0 && _String[Start-1]!='+' )
        --Start;
    while( _String[Start]==' ' || _String[Start]=='\t' )
        ++Start;
    char *CodeStr = _strdup(_String+Start);
    for( size_t i=strlen(CodeStr)-1; i>=0; ++i )
        if( CodeStr[i]==' ' || CodeStr[i]=='\t' )
            CodeStr[i] = '\0';
        else
            break;

    /*
    if( strstr(_String, "SHIFT")!=NULL || strstr(_String, "shift")!=NULL )
        *_Modif |= TW_KMOD_SHIFT;
    if( strstr(_String, "CTRL")!=NULL || strstr(_String, "ctrl")!=NULL )
        *_Modif |= TW_KMOD_CTRL;
    if( strstr(_String, "META")!=NULL || strstr(_String, "meta")!=NULL )
        *_Modif |= TW_KMOD_META;

    if( strstr(_String, "ALTGR")!=NULL || strstr(_String, "altgr")!=NULL )
        ((void)(0));    // *_Modif |= TW_KMOD_ALTGR;
    else // ALT and ALTGR are exclusive
        if( strstr(_String, "ALT")!=NULL || strstr(_String, "alt")!=NULL )  
            *_Modif |= TW_KMOD_ALT;
    */
    char *up = _strdup(_String);
    // _strupr(up);
    for( char *upch=up; *upch!='\0'; ++upch )
        *upch = (char)toupper(*upch);
    if( strstr(up, "SHIFT")!=NULL )
        *_Modif |= TW_KMOD_SHIFT;
    if( strstr(up, "CTRL")!=NULL )
        *_Modif |= TW_KMOD_CTRL;
    if( strstr(up, "META")!=NULL )
        *_Modif |= TW_KMOD_META;

    if( strstr(up, "ALTGR")!=NULL )
        ((void)(0));    // *_Modif |= TW_KMOD_ALTGR;
    else // ALT and ALTGR are exclusive
        if( strstr(up, "ALT")!=NULL )   
            *_Modif |= TW_KMOD_ALT;
    free(up);

    if( strlen(CodeStr)==1 )
        *_Code = (unsigned char)(CodeStr[0]);
    else if( _stricmp(CodeStr, "backspace")==0 || _stricmp(CodeStr, "bs")==0 )
        *_Code = TW_KEY_BACKSPACE;
    else if( _stricmp(CodeStr, "tab")==0 )
        *_Code = TW_KEY_TAB;
    else if( _stricmp(CodeStr, "clear")==0 || _stricmp(CodeStr, "clr")==0 )
        *_Code = TW_KEY_CLEAR;
    else if( _stricmp(CodeStr, "return")==0 || _stricmp(CodeStr, "ret")==0 )
        *_Code = TW_KEY_RETURN;
    else if( _stricmp(CodeStr, "pause")==0 )
        *_Code = TW_KEY_PAUSE;
    else if( _stricmp(CodeStr, "escape")==0 || _stricmp(CodeStr, "esc")==0 )
        *_Code = TW_KEY_ESCAPE;
    else if( _stricmp(CodeStr, "space")==0 )
        *_Code = TW_KEY_SPACE;
    else if( _stricmp(CodeStr, "delete")==0 || _stricmp(CodeStr, "del")==0 )
        *_Code = TW_KEY_DELETE;
    /*
    else if( strlen(CodeStr)==4 && CodeStr[3]>='0' && CodeStr[3]<='9' && (strstr(CodeStr, "pad")==CodeStr || strstr(CodeStr, "PAD")==CodeStr) )
        *_Code = TW_KEY_PAD_0 + CodeStr[3]-'0';
    else if( _stricmp(CodeStr, "pad.")==0 )
        *_Code = TW_KEY_PAD_PERIOD;
    else if( _stricmp(CodeStr, "pad/")==0 )
        *_Code = TW_KEY_PAD_DIVIDE;
    else if( _stricmp(CodeStr, "pad*")==0 )
        *_Code = TW_KEY_PAD_MULTIPLY;
    else if( _stricmp(CodeStr, "pad+")==0 )
        *_Code = TW_KEY_PAD_PLUS;
    else if( _stricmp(CodeStr, "pad-")==0 )
        *_Code = TW_KEY_PAD_MINUS;
    else if( _stricmp(CodeStr, "padenter")==0 )
        *_Code = TW_KEY_PAD_ENTER;
    else if( _stricmp(CodeStr, "pad=")==0 )
        *_Code = TW_KEY_PAD_EQUALS;
    */
    else if( _stricmp(CodeStr, "up")==0 )
        *_Code = TW_KEY_UP;
    else if( _stricmp(CodeStr, "down")==0 )
        *_Code = TW_KEY_DOWN;
    else if( _stricmp(CodeStr, "right")==0 )
        *_Code = TW_KEY_RIGHT;
    else if( _stricmp(CodeStr, "left")==0 )
        *_Code = TW_KEY_LEFT;
    else if( _stricmp(CodeStr, "insert")==0 || _stricmp(CodeStr, "ins")==0 )
        *_Code = TW_KEY_INSERT;
    else if( _stricmp(CodeStr, "home")==0 )
        *_Code = TW_KEY_HOME;
    else if( _stricmp(CodeStr, "end")==0 )
        *_Code = TW_KEY_END;
    else if( _stricmp(CodeStr, "pgup")==0 )
        *_Code = TW_KEY_PAGE_UP;
    else if( _stricmp(CodeStr, "pgdown")==0 )
        *_Code = TW_KEY_PAGE_DOWN;
    else if( (strlen(CodeStr)==2 || strlen(CodeStr)==3) && (CodeStr[0]=='f' || CodeStr[0]=='F') )
    {
        int n = 0;
        if( sscanf(CodeStr+1, "%d", &n)==1 && n>0 && n<16 )
            *_Code = TW_KEY_F1 + n-1;
        else
            Ok = false;
    }

    free(CodeStr);
    return Ok;
}

bool TwGetKeyString(sds *_String, int _Code, int _Modif)
{
    assert(_String!=NULL);
    bool Ok = true;
    if( _Modif & TW_KMOD_SHIFT )
        *_String = sdscat(*_String, "SHIFT+");
    if( _Modif & TW_KMOD_CTRL )
        *_String = sdscat(*_String, "CTRL+");
    if ( _Modif & TW_KMOD_ALT )
        *_String = sdscat(*_String, "ALT+");
    if ( _Modif & TW_KMOD_META )
        *_String = sdscat(*_String, "META+");
    // if ( _Modif & TW_KMOD_ALTGR )
    //  *_String = sdscat(*_String, "ALTGR+");
    switch( _Code )
    {
    case TW_KEY_BACKSPACE:
        *_String = sdscat(*_String, "BackSpace");
        break;
    case TW_KEY_TAB:
        *_String = sdscat(*_String, "Tab");
        break;
    case TW_KEY_CLEAR:
        *_String = sdscat(*_String, "Clear");
        break;
    case TW_KEY_RETURN:
        *_String = sdscat(*_String, "Return");
        break;
    case TW_KEY_PAUSE:
        *_String = sdscat(*_String, "Pause");
        break;
    case TW_KEY_ESCAPE:
        *_String = sdscat(*_String, "Esc");
        break;
    case TW_KEY_SPACE:
        *_String = sdscat(*_String, "Space");
        break;
    case TW_KEY_DELETE:
        *_String = sdscat(*_String, "Delete");
        break;
    /*
    case TW_KEY_PAD_0:
        *_String = sdscat(*_String, "PAD0");
        break;
    case TW_KEY_PAD_1:
        *_String = sdscat(*_String, "PAD1");
        break;
    case TW_KEY_PAD_2:
        *_String = sdscat(*_String, "PAD2");
        break;
    case TW_KEY_PAD_3:
        *_String = sdscat(*_String, "PAD3");
        break;
    case TW_KEY_PAD_4:
        *_String = sdscat(*_String, "PAD4");
        break;
    case TW_KEY_PAD_5:
        *_String = sdscat(*_String, "PAD5");
        break;
    case TW_KEY_PAD_6:
        *_String = sdscat(*_String, "PAD6");
        break;
    case TW_KEY_PAD_7:
        *_String = sdscat(*_String, "PAD7");
        break;
    case TW_KEY_PAD_8:
        *_String = sdscat(*_String, "PAD8");
        break;
    case TW_KEY_PAD_9:
        *_String = sdscat(*_String, "PAD9");
        break;
    case TW_KEY_PAD_PERIOD:
        *_String = sdscat(*_String, "PAD.");
        break;
    case TW_KEY_PAD_DIVIDE:
        *_String = sdscat(*_String, "PAD/");
        break;
    case TW_KEY_PAD_MULTIPLY:
        *_String = sdscat(*_String, "PAD*");
        break;
    case TW_KEY_PAD_MINUS:
        *_String = sdscat(*_String, "PAD-");
        break;
    case TW_KEY_PAD_PLUS:
        *_String = sdscat(*_String, "PAD+");
        break;
    case TW_KEY_PAD_ENTER:
        *_String = sdscat(*_String, "PADEnter");
        break;
    case TW_KEY_PAD_EQUALS:
        *_String = sdscat(*_String, "PAD=");
        break;
    */
    case TW_KEY_UP:
        *_String = sdscat(*_String, "Up");
        break;
    case TW_KEY_DOWN:
        *_String = sdscat(*_String, "Down");
        break;
    case TW_KEY_RIGHT:
        *_String = sdscat(*_String, "Right");
        break;
    case TW_KEY_LEFT:
        *_String = sdscat(*_String, "Left");
        break;
    case TW_KEY_INSERT:
        *_String = sdscat(*_String, "Insert");
        break;
    case TW_KEY_HOME:
        *_String = sdscat(*_String, "Home");
        break;
    case TW_KEY_END:
        *_String = sdscat(*_String, "End");
        break;
    case TW_KEY_PAGE_UP:
        *_String = sdscat(*_String, "PgUp");
        break;
    case TW_KEY_PAGE_DOWN:
        *_String = sdscat(*_String, "PgDown");
        break;
    case TW_KEY_F1:
        *_String = sdscat(*_String, "F1");
        break;
    case TW_KEY_F2:
        *_String = sdscat(*_String, "F2");
        break;
    case TW_KEY_F3:
        *_String = sdscat(*_String, "F3");
        break;
    case TW_KEY_F4:
        *_String = sdscat(*_String, "F4");
        break;
    case TW_KEY_F5:
        *_String = sdscat(*_String, "F5");
        break;
    case TW_KEY_F6:
        *_String = sdscat(*_String, "F6");
        break;
    case TW_KEY_F7:
        *_String = sdscat(*_String, "F7");
        break;
    case TW_KEY_F8:
        *_String = sdscat(*_String, "F8");
        break;
    case TW_KEY_F9:
        *_String = sdscat(*_String, "F9");
        break;
    case TW_KEY_F10:
        *_String = sdscat(*_String, "F10");
        break;
    case TW_KEY_F11:
        *_String = sdscat(*_String, "F11");
        break;
    case TW_KEY_F12:
        *_String = sdscat(*_String, "F12");
        break;
    case TW_KEY_F13:
        *_String = sdscat(*_String, "F13");
        break;
    case TW_KEY_F14:
        *_String = sdscat(*_String, "F14");
        break;
    case TW_KEY_F15:
        *_String = sdscat(*_String, "F15");
        break;
    default:
        if( _Code>0 && _Code<256 )
        {
            char c = (char)_Code;
            *_String = sdscatlen(*_String, &c, 1);
        }
        else
        {
            *_String = sdscat(*_String, "Unknown");
            Ok = false;
        }
    }
    return Ok;
}

//  ---------------------------------------------------------------------------
 
static const int        TW_MOUSE_NOMOTION = -1;
ETwMouseAction   TW_MOUSE_MOTION = (ETwMouseAction)(-2);
ETwMouseAction   TW_MOUSE_WHEEL = (ETwMouseAction)(-3);
ETwMouseButtonID TW_MOUSE_NA = (ETwMouseButtonID)(-1);

static int TwMouseEvent(ETwMouseAction _EventType, TwMouseButtonID _Button, int _MouseX, int _MouseY, int _WheelPos)
{
    unsigned int fpuState = TwFPU_Save(); // force fpu precision

    if( g_TwMgr==NULL || g_TwMgr->m_Graph==NULL )
    {
        // TwGlobalError(g_ErrNotInit); -> not an error here
        TwFPU_Restore(fpuState);
        return 0; // not initialized
    }
    if( g_TwMgr->m_WndHeight<=0 || g_TwMgr->m_WndWidth<=0 )
    {
        //CTwMgr_SetLastError(g_TwMgr, g_ErrBadWndSize);   // not an error, windows not yet ready.
        TwFPU_Restore(fpuState);
        return 0;
    }

    // For multi-thread safety
    if( !TwFreeAsyncDrawing() )
    {
        TwFPU_Restore(fpuState);
        return 0;
    }

    if( _MouseX==TW_MOUSE_NOMOTION )
        _MouseX = g_TwMgr->m_LastMouseX;
    else
        g_TwMgr->m_LastMouseX = _MouseX;
    if( _MouseY==TW_MOUSE_NOMOTION )
        _MouseY = g_TwMgr->m_LastMouseY;
    else
        g_TwMgr->m_LastMouseY = _MouseY;

    // for autorepeat
    if( (!g_TwMgr->m_IsRepeatingMousePressed || !g_TwMgr->m_CanRepeatMousePressed) && _EventType==TW_MOUSE_PRESSED )
    {
        g_TwMgr->m_LastMousePressedTime = glfwGetTime();
        g_TwMgr->m_LastMousePressedButtonID = _Button;
        g_TwMgr->m_LastMousePressedPosition[0] = _MouseX;
        g_TwMgr->m_LastMousePressedPosition[1] = _MouseY;
        g_TwMgr->m_CanRepeatMousePressed = true;
        g_TwMgr->m_IsRepeatingMousePressed = false;
    }
    else if( _EventType==TW_MOUSE_RELEASED || _EventType==TW_MOUSE_WHEEL )
    {
        g_TwMgr->m_CanRepeatMousePressed = false;
        g_TwMgr->m_IsRepeatingMousePressed = false;
    }

    bool Handled = false;
    bool wasPopup = (g_TwMgr->m_PopupBar!=NULL);
    CTwBar *Bar = NULL;
    int i;

    // search for a bar with mousedrag enabled
    CTwBar *BarDragging = NULL;
    for( i=((int)g_TwMgr->m_Bars.count)-1; i>=0; --i )
    {
        Bar = g_TwMgr->m_Bars.items[g_TwMgr->m_Order.items[i]];
        if( Bar!=NULL && Bar->m_Visible && CTwBar_IsDragging(Bar) )
        {
            BarDragging = Bar;
            break;
        }
    }

    for( i=(int)g_TwMgr->m_Bars.count; i>=0; --i )
    {
        if( i==(int)g_TwMgr->m_Bars.count )    // first try the bar with mousedrag enabled (this bar has the focus)
            Bar = BarDragging;
        else
        {
            Bar = g_TwMgr->m_Bars.items[g_TwMgr->m_Order.items[i]];
            if( Bar==BarDragging )
                continue;
        }
        if( Bar!=NULL && Bar->m_Visible )
        {
            if( _EventType==TW_MOUSE_MOTION )
                Handled = CTwBar_MouseMotion(Bar, _MouseX, _MouseY);
            else if( _EventType==TW_MOUSE_PRESSED || _EventType==TW_MOUSE_RELEASED )
                Handled = CTwBar_MouseButton(Bar, _Button, (_EventType==TW_MOUSE_PRESSED), _MouseX, _MouseY);
            else if( _EventType==TW_MOUSE_WHEEL )
            {
                if( abs(_WheelPos-g_TwMgr->m_LastMouseWheelPos)<4 ) // avoid crazy wheel positions
                    Handled = CTwBar_MouseWheel(Bar, _WheelPos, g_TwMgr->m_LastMouseWheelPos, _MouseX, _MouseY);
            }
            if( Handled )
                break;
        }
    }

    if( g_TwMgr==NULL ) // Mgr might have been destroyed by the client inside a callback call
    {
        TwFPU_Restore(fpuState);
        return 1;
    }

    /*
    if( i>=0 && Bar!=NULL && Handled && (_EventType==TW_MOUSE_PRESSED || CTwBar_IsMinimized(Bar)) && i!=((int)g_TwMgr->m_Bars.count)-1 )
    {
        int iOrder = g_TwMgr->m_Order.items[i];
        for( int j=i; j<(int)g_TwMgr->m_Bars.count-1; ++j )
            g_TwMgr->m_Order.items[j] = g_TwMgr->m_Order.items[j+1];
        g_TwMgr->m_Order.items[(int)g_TwMgr->m_Bars.count-1] = iOrder;
    }
    */
    if( _EventType==TW_MOUSE_PRESSED || (Bar!=NULL && CTwBar_IsMinimized(Bar) && Handled) )
    {
        if( wasPopup && Bar!=g_TwMgr->m_PopupBar && g_TwMgr->m_PopupBar!=NULL ) // delete popup
        {
            TwDeleteBar(g_TwMgr->m_PopupBar);
            g_TwMgr->m_PopupBar = NULL;
        }

        if( i>=0 && Bar!=NULL && Handled && !wasPopup )
            TwSetTopBar(Bar);
    }

    if( _EventType==TW_MOUSE_WHEEL )
        g_TwMgr->m_LastMouseWheelPos = _WheelPos;

    TwFPU_Restore(fpuState);
    return Handled ? 1 : 0;
}

int ANT_CALL TwMouseButton(ETwMouseAction _EventType, TwMouseButtonID _Button)
{
    return TwMouseEvent(_EventType, _Button, TW_MOUSE_NOMOTION, TW_MOUSE_NOMOTION, 0);
}

int ANT_CALL TwMouseMotion(int _MouseX, int _MouseY)
{
    return TwMouseEvent(TW_MOUSE_MOTION, TW_MOUSE_NA, _MouseX, _MouseY, 0);
}

int ANT_CALL TwMouseWheel(int _Pos)
{
    return TwMouseEvent(TW_MOUSE_WHEEL, TW_MOUSE_NA, TW_MOUSE_NOMOTION, TW_MOUSE_NOMOTION, _Pos);
}

//  ---------------------------------------------------------------------------

static int TranslateKey(int _Key, int _Modifiers)
{
    // CTRL special cases
    //if( (_Modifiers&TW_KMOD_CTRL) && !(_Modifiers&TW_KMOD_ALT || _Modifiers&TW_KMOD_META) && _Key>0 && _Key<32 )
    //  _Key += 'a'-1;
    if( (_Modifiers&TW_KMOD_CTRL) )
    {
        if( _Key>='a' && _Key<='z' && ( ((_Modifiers&0x2000) && !(_Modifiers&TW_KMOD_SHIFT)) || (!(_Modifiers&0x2000) && (_Modifiers&TW_KMOD_SHIFT)) )) // 0x2000 is SDL's KMOD_CAPS
            _Key += 'A'-'a';
        else if ( _Key>='A' && _Key<='Z' && ( ((_Modifiers&0x2000) && (_Modifiers&TW_KMOD_SHIFT)) || (!(_Modifiers&0x2000) && !(_Modifiers&TW_KMOD_SHIFT)) )) // 0x2000 is SDL's KMOD_CAPS
            _Key += 'a'-'A';
    }

    // PAD translation (for SDL keysym)
    if( _Key>=256 && _Key<=272 ) // 256=SDLK_KP0 ... 272=SDLK_KP_EQUALS
    {
        //bool Num = ((_Modifiers&TW_KMOD_SHIFT) && !(_Modifiers&0x1000)) || (!(_Modifiers&TW_KMOD_SHIFT) && (_Modifiers&0x1000)); // 0x1000 is SDL's KMOD_NUM
        //_Modifiers &= ~TW_KMOD_SHIFT; // remove shift modifier
        bool Num = (!(_Modifiers&TW_KMOD_SHIFT) && (_Modifiers&0x1000)); // 0x1000 is SDL's KMOD_NUM
        if( _Key==266 )          // SDLK_KP_PERIOD
            _Key = Num ? '.' : TW_KEY_DELETE;
        else if( _Key==267 )     // SDLK_KP_DIVIDE
            _Key = '/';
        else if( _Key==268 )     // SDLK_KP_MULTIPLY
            _Key = '*';
        else if( _Key==269 )     // SDLK_KP_MINUS
            _Key = '-';
        else if( _Key==270 )     // SDLK_KP_PLUS
            _Key = '+';
        else if( _Key==271 )     // SDLK_KP_ENTER
            _Key = TW_KEY_RETURN;
        else if( _Key==272 )     // SDLK_KP_EQUALS
            _Key = '=';
        else if( Num )           // num SDLK_KP0..9
            _Key += '0' - 256;
        else if( _Key==256 )     // non-num SDLK_KP01
            _Key = TW_KEY_INSERT;
        else if( _Key==257 )     // non-num SDLK_KP1
            _Key = TW_KEY_END;
        else if( _Key==258 )     // non-num SDLK_KP2
            _Key = TW_KEY_DOWN;
        else if( _Key==259 )     // non-num SDLK_KP3
            _Key = TW_KEY_PAGE_DOWN;
        else if( _Key==260 )     // non-num SDLK_KP4
            _Key = TW_KEY_LEFT;
        else if( _Key==262 )     // non-num SDLK_KP6
            _Key = TW_KEY_RIGHT;
        else if( _Key==263 )     // non-num SDLK_KP7
            _Key = TW_KEY_HOME;
        else if( _Key==264 )     // non-num SDLK_KP8
            _Key = TW_KEY_UP;
        else if( _Key==265 )     // non-num SDLK_KP9
            _Key = TW_KEY_PAGE_UP;
    }
    return _Key;
}

//  ---------------------------------------------------------------------------

static int KeyPressed(int _Key, int _Modifiers, bool _TestOnly)
{
    unsigned int fpuState = TwFPU_Save(); // force fpu precision

    if( g_TwMgr==NULL || g_TwMgr->m_Graph==NULL )
    {
        // TwGlobalError(g_ErrNotInit); -> not an error here
        TwFPU_Restore(fpuState);
        return 0; // not initialized
    }
    if( g_TwMgr->m_WndHeight<=0 || g_TwMgr->m_WndWidth<=0 )
    {
        //CTwMgr_SetLastError(g_TwMgr, g_ErrBadWndSize);   // not an error, windows not yet ready.
        TwFPU_Restore(fpuState);
        return 0;
    }

    // For multi-thread savety
    if( !TwFreeAsyncDrawing() )
    {
        TwFPU_Restore(fpuState);
        return 0;
    }

    /*
    // Test for TwDeleteBar
    if( _Key>='0' && _Key<='9' )
    {
        int n = _Key-'0';
        if( (int)g_TwMgr->m_Bars.count>n && g_TwMgr->m_Bars.items[n]!=NULL )
        {
            printf("Delete %s\n", g_TwMgr->m_Bars.items[n]->m_Name);
            TwDeleteBar(g_TwMgr->m_Bars.items[n]);
        }
        else
            printf("can't delete %d\n", n);
        return 1;
    }
    */

    //char s[256];
    //sprintf(s, "twkeypressed k=%d m=%x\n", _Key, _Modifiers);
    //OutputDebugString(s);

    _Key = TranslateKey(_Key, _Modifiers);
    if( _Key>' ' && _Key<256 ) // don't test SHIFT if _Key is a common key
        _Modifiers &= ~TW_KMOD_SHIFT;
    // complete partial modifiers comming from SDL
    if( _Modifiers & TW_KMOD_SHIFT )
        _Modifiers |= TW_KMOD_SHIFT;
    if( _Modifiers & TW_KMOD_CTRL )
        _Modifiers |= TW_KMOD_CTRL;
    if( _Modifiers & TW_KMOD_ALT )
        _Modifiers |= TW_KMOD_ALT;
    if( _Modifiers & TW_KMOD_META )
        _Modifiers |= TW_KMOD_META;

    bool Handled = false;
    CTwBar *Bar = NULL;
    CTwBar *PopupBar = g_TwMgr->m_PopupBar;
    //int Order = 0;
    int i;
    if( _Key>0 && _Key<TW_KEY_LAST )
    {
        // First send it to bar which includes the mouse pointer
        int MouseX = g_TwMgr->m_LastMouseX;
        int MouseY = g_TwMgr->m_LastMouseY;
        for( i=((int)g_TwMgr->m_Bars.count)-1; i>=0 && !Handled; --i )
        {
            Bar = g_TwMgr->m_Bars.items[g_TwMgr->m_Order.items[i]];
            if( Bar!=NULL && Bar->m_Visible && !CTwBar_IsMinimized(Bar) 
                && ( (MouseX>=Bar->m_PosX && MouseX<Bar->m_PosX+Bar->m_Width && MouseY>=Bar->m_PosY && MouseY<Bar->m_PosY+Bar->m_Height)
                     || Bar==PopupBar) )
            {
                if (_TestOnly)
                    Handled = CTwBar_KeyTest(Bar, _Key, _Modifiers);
                else
                    Handled = CTwBar_KeyPressed(Bar, _Key, _Modifiers);
            }
        }

        // If not handled, send it to non-iconified bars in the right order
        for( i=((int)g_TwMgr->m_Bars.count)-1; i>=0 && !Handled; --i )
        {
            Bar = g_TwMgr->m_Bars.items[g_TwMgr->m_Order.items[i]];
            /*
            for( size_t j=0; j<g_TwMgr->m_Bars.count; ++j )
                if( g_TwMgr->m_Order.items[j]==i )
                {
                    Bar = g_TwMgr->m_Bars.items[j];
                    break;
                }
            Order = i;
            */

            if( Bar!=NULL && Bar->m_Visible && !CTwBar_IsMinimized(Bar) )
            {
                if( _TestOnly )
                    Handled = CTwBar_KeyTest(Bar, _Key, _Modifiers);
                else
                    Handled = CTwBar_KeyPressed(Bar, _Key, _Modifiers);
                if( g_TwMgr==NULL ) // Mgr might have been destroyed by the client inside a callback call
                {
                    TwFPU_Restore(fpuState);
                    return 1;
                }
            }
        }

        // If not handled, send it to iconified bars in the right order
        for( i=((int)g_TwMgr->m_Bars.count)-1; i>=0 && !Handled; --i )
        {
            Bar = g_TwMgr->m_Bars.items[g_TwMgr->m_Order.items[i]];
            if( Bar!=NULL && Bar->m_Visible && CTwBar_IsMinimized(Bar) )
            {
                if( _TestOnly )
                    Handled = CTwBar_KeyTest(Bar, _Key, _Modifiers);
                else
                    Handled = CTwBar_KeyPressed(Bar, _Key, _Modifiers);
            }
        }
        
        if( g_TwMgr->m_HelpBar!=NULL && g_TwMgr->m_Graph && !_TestOnly )
        {
            sds Str = sdsempty();
            TwGetKeyString(&Str, _Key, _Modifiers);
            char Msg[256];
            sprintf(Msg, "Key pressed: %s", Str);
            g_TwMgr->m_KeyPressedStr = sdscpy(g_TwMgr->m_KeyPressedStr, Msg);
            g_TwMgr->m_KeyPressedBuildText = true;
            // OutputDebugString(Msg);
            sdsfree(Str);
        }
    }

    if( Handled && Bar!=g_TwMgr->m_PopupBar && g_TwMgr->m_PopupBar!=NULL && g_TwMgr->m_PopupBar==PopupBar )  // delete popup
    {
        TwDeleteBar(g_TwMgr->m_PopupBar);
        g_TwMgr->m_PopupBar = NULL;
    }

    if( Handled && Bar!=NULL && Bar!=g_TwMgr->m_PopupBar && Bar!=PopupBar ) // popup bar may have been destroyed
        TwSetTopBar(Bar);

    TwFPU_Restore(fpuState);
    return Handled ? 1 : 0;
}

int ANT_CALL TwKeyPressed(int _Key, int _Modifiers)
{
    return KeyPressed(_Key, _Modifiers, false);
}

int ANT_CALL TwKeyTest(int _Key, int _Modifiers)
{
    return KeyPressed(_Key, _Modifiers, true);
}

//  ---------------------------------------------------------------------------

// Replaces std::set<TwType, StructCompare>: built fresh, fully populated,
// then immediately consumed once (in UpdateHelpBar) - a plain dedup-insert
// array plus an explicit sort right before iterating is simpler than
// keeping it sorted throughout (unlike CEnum's m_Entries, which is read
// from multiple places while being built incrementally and so must stay
// sorted at all times).
typedef struct { TwType *items; size_t count; size_t capacity; } StructSet;

static void StructSet_InsertUnique(StructSet *_Set, TwType _Type)
{
    for( size_t i=0; i<_Set->count; ++i )
        if( _Set->items[i]==_Type )
            return;
    tw_da_append(_Set, _Type);
}

// Sorts a's/b's struct names alphabetically, replicating StructCompare's
// exact logic (including its out-of-range fallback).
static int StructSet_CompareByName(const void *_A, const void *_B)
{
    TwType a = *(const TwType *)_A;
    TwType b = *(const TwType *)_B;
    assert( g_TwMgr!=NULL );
    int i0 = a-TW_TYPE_STRUCT_BASE;
    int i1 = b-TW_TYPE_STRUCT_BASE;
    if( i0>=0 && i0<(int)g_TwMgr->m_Structs.count && i1>=0 && i1<(int)g_TwMgr->m_Structs.count )
        return strcmp(g_TwMgr->m_Structs.items[i0].m_Name, g_TwMgr->m_Structs.items[i1].m_Name);
    else
        return 0; // was StructCompare's "return false" fallback - neither considered less than the other
}

static void InsertUsedStructs(StructSet *_Set, const CTwVarGroup *_Grp)
{
    assert( g_TwMgr!=NULL && _Grp!=NULL );

    for( size_t i=0; i<_Grp->m_Vars.count; ++i )
        if( _Grp->m_Vars.items[i]!=NULL && _Grp->m_Vars.items[i]->m_Visible && CTwVar_IsGroup(_Grp->m_Vars.items[i]) )
        {
            const CTwVarGroup *SubGrp = ((const CTwVarGroup *)(_Grp->m_Vars.items[i]));
            if( SubGrp->m_StructValuePtr!=NULL && SubGrp->m_StructType>=TW_TYPE_STRUCT_BASE && SubGrp->m_StructType<TW_TYPE_STRUCT_BASE+(int)g_TwMgr->m_Structs.count && sdslen(g_TwMgr->m_Structs.items[SubGrp->m_StructType-TW_TYPE_STRUCT_BASE].m_Name)>0 )
            {
                if( sdslen(SubGrp->m_Base.m_Help)>0 )
                    StructSet_InsertUnique(_Set, SubGrp->m_StructType);
                else
                {
                    int idx = SubGrp->m_StructType - TW_TYPE_STRUCT_BASE;
                    if( idx>=0 && idx<(int)g_TwMgr->m_Structs.count && sdslen(g_TwMgr->m_Structs.items[idx].m_Name)>0 )
                    {
                        for( size_t j=0; j<g_TwMgr->m_Structs.items[idx].m_Members.count; ++j )
                            if( sdslen(g_TwMgr->m_Structs.items[idx].m_Members.items[j].m_Help)>0 )
                            {
                                StructSet_InsertUnique(_Set, SubGrp->m_StructType);
                                break;
                            }
                    }
                }
            }
            InsertUsedStructs(_Set, SubGrp);
        }
}

static void SplitString(CSdsArray *_OutSplits, const char *_String, int _Width, const CTexFont *_Font)
{
    assert( _Font!=NULL && _String!=NULL );
    _OutSplits->count = 0;
    int l = (int)strlen(_String);
    if( l==0 )
    {
        _String = " ";
        l = 1;
    }

    if( _String!=NULL && l>0 && _Width>0 )
    {
        int w = 0;
        int i = 0;
        int First = 0;
        int Last = 0;
        bool PrevNotBlank = true;
        unsigned char c;
        bool Tab = false, CR = false;
        sds Split = sdsempty();

        while( i<l )
        {
            c = _String[i];
            if( c=='\t' )
            {
                w += g_TabLength * _Font->m_CharWidth[(int)' '];
                Tab = true;
            }
            else if( c=='\n' )
            {
                w += _Width+1; // force split
                Last = i;
                CR = true;
            }
            else
                w += _Font->m_CharWidth[(int)c];
            if( w>_Width || i==l-1 )
            {
                if( Last<=First || i==l-1 )
                    Last = i;
                if( Tab )
                {
                    sdsclear(Split);
                    for(int k=0; k<Last-First+(CR?0:1); ++k)
                        if( _String[First+k]=='\t' )
                        {
                            for(int t=0; t<g_TabLength; ++t)
                                Split = sdscatlen(Split, " ", 1);
                        }
                        else
                            Split = sdscatlen(Split, &_String[First+k], 1);
                    Tab = false;
                }
                else
                    Split = sdscpylen(Split, _String+First, (size_t)(Last-First+(CR?0:1)));
                tw_da_append(_OutSplits, sdsdup(Split));
                First = Last+1;
                if( !CR )
                    while( First<l && (_String[First]==' ' || _String[First]=='\t') )   // skip blanks
                        ++First;
                Last = First;
                w = 0;
                PrevNotBlank = true;
                i = First;
                CR = false;
            }
            else if( c==' ' || c=='\t' )
            {
                if( PrevNotBlank )
                    Last = i-1;
                PrevNotBlank = false;
                ++i;
            }
            else
            {
                PrevNotBlank = true;
                ++i;
            }
        }
        sdsfree(Split);
    }
}

static int AppendHelpString(CTwVarGroup *_Grp, const char *_String, int _Level, int _Width, ETwType _Type)
{
    assert( _Grp!=NULL && g_TwMgr!=NULL && g_TwMgr->m_HelpBar!=NULL);
    assert( _String!=NULL );
    int n = 0;
    const CTexFont *Font = g_TwMgr->m_HelpBar->m_Font;
    assert(Font!=NULL);
    sds Decal = sdsempty();
    for( int s=0; s<_Level; ++s )
        Decal = sdscatlen(Decal, " ", 1);
    int DecalWidth = (_Level+2)*Font->m_CharWidth[(int)' '];

    if( _Width>DecalWidth )
    {
        CSdsArray Split = {0};
        SplitString(&Split, _String, _Width-DecalWidth, Font);
        for( size_t i=0; i<Split.count; ++i )
        {
            CTwVarAtom *Var = CTwVarAtom_New();
            sds combined = sdsdup(Decal);
            combined = sdscatsds(combined, Split.items[i]);
            Var->m_Base.m_Name = sdscpy(Var->m_Base.m_Name, combined);
            sdsfree(combined);
            Var->m_Ptr = NULL;
            if( _Type==TW_TYPE_HELP_HEADER )
                Var->m_ReadOnly = false;
            else
                Var->m_ReadOnly = true;
            Var->m_NoSlider = true;
            Var->m_Base.m_DontClip = true;
            Var->m_Type = _Type;
            Var->m_Base.m_LeftMargin = (signed short)((_Level+1)*Font->m_CharWidth[(int)' ']);
            Var->m_Base.m_TopMargin  = (signed short)(-g_TwMgr->m_HelpBar->m_Sep);
            //Var->m_TopMargin  = 1;
            Var->m_Base.m_ColorPtr = &(g_TwMgr->m_HelpBar->m_ColHelpText);
            CTwVarAtom_SetDefaults(Var);
            tw_da_append(&_Grp->m_Vars, &Var->m_Base);
            ++n;
        }
        for( size_t i=0; i<Split.count; ++i )
            sdsfree(Split.items[i]);
        tw_da_free(&Split);
    }
    sdsfree(Decal);
    return n;
}

static int AppendHelp(CTwVarGroup *_Grp, const CTwVarGroup *_ToAppend, int _Level, int _Width)
{
    assert( _Grp!=NULL );
    assert( _ToAppend!=NULL );
    int n = 0;
    sds Decal = sdsempty();
    for( int s=0; s<_Level; ++s )
        Decal = sdscatlen(Decal, " ", 1);

    if( sdslen(_ToAppend->m_Base.m_Help)>0 )
        n += AppendHelpString(_Grp, _ToAppend->m_Base.m_Help, _Level, _Width, TW_TYPE_HELP_GRP);

    for( size_t i=0; i<_ToAppend->m_Vars.count; ++i )
        if( _ToAppend->m_Vars.items[i]!=NULL && _ToAppend->m_Vars.items[i]->m_Visible )
        {
            bool append = true;
            if( !CTwVar_IsGroup(_ToAppend->m_Vars.items[i]) )
            {
                const CTwVarAtom *a = ((const CTwVarAtom *)(_ToAppend->m_Vars.items[i]));
                if( a->m_Type==TW_TYPE_BUTTON && a->m_Val.m_Button.m_Callback==NULL )
                    append = false;
                else if( a->m_KeyIncr[0]==0 && a->m_KeyIncr[1]==0 && a->m_KeyDecr[0]==0 && a->m_KeyDecr[1]==0 && sdslen(a->m_Base.m_Help)<=0 )
                    append = false;
            }
            else if( CTwVar_IsGroup(_ToAppend->m_Vars.items[i]) && ((const CTwVarGroup *)(_ToAppend->m_Vars.items[i]))->m_StructValuePtr!=NULL // that's a struct var
                     && sdslen(_ToAppend->m_Vars.items[i]->m_Help)<=0 )
                 append = false;

            if( append )
            {
                CTwVarAtom *Var = CTwVarAtom_New();
                Var->m_Base.m_Name = sdscpy(Var->m_Base.m_Name, Decal);
                if( sdslen(_ToAppend->m_Vars.items[i]->m_Label)>0 )
                    Var->m_Base.m_Name = sdscat(Var->m_Base.m_Name, _ToAppend->m_Vars.items[i]->m_Label);
                else
                    Var->m_Base.m_Name = sdscat(Var->m_Base.m_Name, _ToAppend->m_Vars.items[i]->m_Name);
                Var->m_Ptr = NULL;
                if( CTwVar_IsGroup(_ToAppend->m_Vars.items[i]) && ((const CTwVarGroup *)(_ToAppend->m_Vars.items[i]))->m_StructValuePtr!=NULL )
                {   // That's a struct var
                    Var->m_Type = TW_TYPE_HELP_STRUCT;
                    Var->m_Val.m_HelpStruct.m_StructType = ((const CTwVarGroup *)(_ToAppend->m_Vars.items[i]))->m_StructType;
                    Var->m_ReadOnly = true;
                    Var->m_NoSlider = true;
                }
                else if( !CTwVar_IsGroup(_ToAppend->m_Vars.items[i]) )
                {
                    Var->m_Type = TW_TYPE_SHORTCUT;
                    Var->m_Val.m_Shortcut.m_Incr[0] = ((const CTwVarAtom *)(_ToAppend->m_Vars.items[i]))->m_KeyIncr[0];
                    Var->m_Val.m_Shortcut.m_Incr[1] = ((const CTwVarAtom *)(_ToAppend->m_Vars.items[i]))->m_KeyIncr[1];
                    Var->m_Val.m_Shortcut.m_Decr[0] = ((const CTwVarAtom *)(_ToAppend->m_Vars.items[i]))->m_KeyDecr[0];
                    Var->m_Val.m_Shortcut.m_Decr[1] = ((const CTwVarAtom *)(_ToAppend->m_Vars.items[i]))->m_KeyDecr[1];
                    Var->m_ReadOnly = ((const CTwVarAtom *)(_ToAppend->m_Vars.items[i]))->m_ReadOnly;
                    Var->m_NoSlider = true;
                }
                else
                {
                    Var->m_Type = TW_TYPE_HELP_GRP;
                    Var->m_Base.m_DontClip = true;
                    Var->m_Base.m_LeftMargin = (signed short)((_Level+2)*g_TwMgr->m_HelpBar->m_Font->m_CharWidth[(int)' ']);
                    //Var->m_TopMargin  = (signed short)(g_TwMgr->m_HelpBar->m_Font->m_CharHeight/2-2+2*(_Level-1));
                    Var->m_Base.m_TopMargin  = 2;
                    if( Var->m_Base.m_TopMargin>g_TwMgr->m_HelpBar->m_Font->m_CharHeight-3 )
                        Var->m_Base.m_TopMargin = (signed short)(g_TwMgr->m_HelpBar->m_Font->m_CharHeight-3);
                    Var->m_ReadOnly = true;
                }
                CTwVarAtom_SetDefaults(Var);
                tw_da_append(&_Grp->m_Vars, &Var->m_Base);
                size_t VarIndex = _Grp->m_Vars.count-1;
                ++n;
                if( CTwVar_IsGroup(_ToAppend->m_Vars.items[i]) && ((const CTwVarGroup *)(_ToAppend->m_Vars.items[i]))->m_StructValuePtr==NULL )
                {
                    int nAppended = AppendHelp(_Grp, ((const CTwVarGroup *)(_ToAppend->m_Vars.items[i])), _Level+1, _Width);
                    if( _Grp->m_Vars.count==VarIndex+1 )
                    {
                        CTwVar_Delete(_Grp->m_Vars.items[VarIndex]);
                        tw_da_resize(&_Grp->m_Vars, VarIndex);
                    }
                    else
                        n += nAppended;
                }
                else if( sdslen(_ToAppend->m_Vars.items[i]->m_Help)>0 )
                    n += AppendHelpString(_Grp, _ToAppend->m_Vars.items[i]->m_Help, _Level+1, _Width, TW_TYPE_HELP_ATOM);
            }
        }
    sdsfree(Decal);
    return n;
}


static void CopyHierarchy(CTwVarGroup *dst, const CTwVarGroup *src)
{
    if( dst==NULL || src==NULL )
        return;

    dst->m_Base.m_Name = sdscpy(dst->m_Base.m_Name, src->m_Base.m_Name);
    dst->m_Open = src->m_Open;
    dst->m_Base.m_Visible = src->m_Base.m_Visible;
    dst->m_Base.m_ColorPtr = src->m_Base.m_ColorPtr;
    dst->m_Base.m_DontClip = src->m_Base.m_DontClip;
    dst->m_Base.m_IsRoot = src->m_Base.m_IsRoot;
    dst->m_Base.m_LeftMargin = src->m_Base.m_LeftMargin;
    dst->m_Base.m_TopMargin = src->m_Base.m_TopMargin;

    tw_da_resize(&dst->m_Vars, src->m_Vars.count);
    for(size_t i=0; i<src->m_Vars.count; ++i)
        if( src->m_Vars.items[i]!=NULL && CTwVar_IsGroup(src->m_Vars.items[i]) )
        {
            CTwVarGroup *grp = CTwVarGroup_New();
            CopyHierarchy(grp, ((const CTwVarGroup *)(src->m_Vars.items[i])));
            dst->m_Vars.items[i] = &grp->m_Base;
        }
        else
            dst->m_Vars.items[i] = NULL;
}

// copy the 'open' flag from original hierarchy to current hierarchy
static void SynchroHierarchy(CTwVarGroup *cur, const CTwVarGroup *orig)
{
    if( cur==NULL || orig==NULL )
        return;

    if( strcmp(cur->m_Base.m_Name, orig->m_Base.m_Name)==0 )
        cur->m_Open = orig->m_Open;

    size_t j = 0;
    while( j<orig->m_Vars.count && (orig->m_Vars.items[j]==NULL || !CTwVar_IsGroup(orig->m_Vars.items[j])) )
        ++j;

    for(size_t i=0; i<cur->m_Vars.count; ++i)
        if( cur->m_Vars.items[i]!=NULL && CTwVar_IsGroup(cur->m_Vars.items[i]) && j<orig->m_Vars.count && orig->m_Vars.items[j]!=NULL && CTwVar_IsGroup(orig->m_Vars.items[j]) )
        {
            CTwVarGroup *curGrp = ((CTwVarGroup *)(cur->m_Vars.items[i]));
            const CTwVarGroup *origGrp = ((const CTwVarGroup *)(orig->m_Vars.items[j]));
            if( strcmp(curGrp->m_Base.m_Name, origGrp->m_Base.m_Name)==0 )
            {
                curGrp->m_Open = origGrp->m_Open;

                SynchroHierarchy(curGrp, origGrp);

                ++j;
                while( j<orig->m_Vars.count && (orig->m_Vars.items[j]==NULL || !CTwVar_IsGroup(orig->m_Vars.items[j])) )
                    ++j;
            }
        }
}


void CTwMgr_UpdateHelpBar(CTwMgr *_Mgr)
{
    if( _Mgr->m_HelpBar==NULL || CTwBar_IsMinimized(_Mgr->m_HelpBar) )
        return;
    if( !_Mgr->m_HelpBarUpdateNow && (float)glfwGetTime()<_Mgr->m_LastHelpUpdateTime+2 )    // update at most every 2 seconds
        return;
    _Mgr->m_HelpBarUpdateNow = false;
    _Mgr->m_LastHelpUpdateTime = (float)glfwGetTime();
    #ifdef _DEBUG
        //printf("UPDATE HELPBAR\n");
    #endif // _DEBUG

    CTwVarGroup prevHierarchy;
    CopyHierarchy(&prevHierarchy, &_Mgr->m_HelpBar->m_VarRoot);

    TwRemoveAllVars(_Mgr->m_HelpBar);

    if( _Mgr->m_HelpBar->m_UpToDate )
        CTwBar_Update(_Mgr->m_HelpBar);

    if( sdslen(_Mgr->m_Help)>0 )
        AppendHelpString(&(_Mgr->m_HelpBar->m_VarRoot), _Mgr->m_Help, 0, _Mgr->m_HelpBar->m_VarX2-_Mgr->m_HelpBar->m_VarX0, TW_TYPE_HELP_ATOM);
    if( sdslen(_Mgr->m_HelpBar->m_Help)>0 )
        AppendHelpString(&(_Mgr->m_HelpBar->m_VarRoot), _Mgr->m_HelpBar->m_Help, 0, _Mgr->m_HelpBar->m_VarX2-_Mgr->m_HelpBar->m_VarX0, TW_TYPE_HELP_ATOM);
    AppendHelpString(&(_Mgr->m_HelpBar->m_VarRoot), "", 0, _Mgr->m_HelpBar->m_VarX2-_Mgr->m_HelpBar->m_VarX0, TW_TYPE_HELP_HEADER);

    for( size_t ib=0; ib<_Mgr->m_Bars.count; ++ib )
        if( _Mgr->m_Bars.items[ib]!=NULL && !(_Mgr->m_Bars.items[ib]->m_IsHelpBar) && _Mgr->m_Bars.items[ib]!=_Mgr->m_PopupBar && _Mgr->m_Bars.items[ib]->m_Visible )
        {
            // Create a group
            CTwVarGroup *Grp = CTwVarGroup_New();
            Grp->m_SummaryCallback = NULL;
            Grp->m_SummaryClientData = NULL;
            Grp->m_StructValuePtr = NULL;
            if( sdslen(_Mgr->m_Bars.items[ib]->m_Label)<=0 )
                Grp->m_Base.m_Name = sdscpy(Grp->m_Base.m_Name, _Mgr->m_Bars.items[ib]->m_Name);
            else
                Grp->m_Base.m_Name = sdscpy(Grp->m_Base.m_Name, _Mgr->m_Bars.items[ib]->m_Label);
            Grp->m_Open = true;
            Grp->m_Base.m_ColorPtr = &(_Mgr->m_HelpBar->m_ColGrpText);
            tw_da_append(&_Mgr->m_HelpBar->m_VarRoot.m_Vars, &Grp->m_Base);
            if( sdslen(_Mgr->m_Bars.items[ib]->m_Help)>0 )
                AppendHelpString(Grp, _Mgr->m_Bars.items[ib]->m_Help, 0, _Mgr->m_HelpBar->m_VarX2-_Mgr->m_HelpBar->m_VarX0, TW_TYPE_HELP_GRP);

            // Append variables (recursive)
            AppendHelp(Grp, &(_Mgr->m_Bars.items[ib]->m_VarRoot), 1, _Mgr->m_HelpBar->m_VarX2-_Mgr->m_HelpBar->m_VarX0);

            // Append structures
            StructSet UsedStructs = {0};
            InsertUsedStructs(&UsedStructs, &(_Mgr->m_Bars.items[ib]->m_VarRoot));
            qsort(UsedStructs.items, UsedStructs.count, sizeof(TwType), StructSet_CompareByName); // alphabetical by struct name, matching the old std::set<TwType, StructCompare>'s iteration order
            CTwVarGroup *StructGrp = NULL;
            int MemberCount = 0;
            for( size_t usi=0; usi<UsedStructs.count; ++usi )
            {
                int idx = UsedStructs.items[usi] - TW_TYPE_STRUCT_BASE;
                if( idx>=0 && idx<(int)g_TwMgr->m_Structs.count && sdslen(g_TwMgr->m_Structs.items[idx].m_Name)>0 )
                {
                    if( StructGrp==NULL )
                    {
                        StructGrp = CTwVarGroup_New();
                        StructGrp->m_StructType = TW_TYPE_HELP_STRUCT;  // a special line background color will be used
                        StructGrp->m_Base.m_Name = sdscpy(StructGrp->m_Base.m_Name, "Structures");
                        StructGrp->m_Open = false;
                        StructGrp->m_Base.m_ColorPtr = &(_Mgr->m_HelpBar->m_ColStructText);
                        //tw_da_append(&Grp->m_Vars, StructGrp);
                        MemberCount = 0;
                    }
                    CTwVarAtom *Var = CTwVarAtom_New();
                    Var->m_Ptr = NULL;
                    Var->m_Type = TW_TYPE_HELP_GRP;
                    Var->m_Base.m_DontClip = true;
                    Var->m_Base.m_LeftMargin = (signed short)(3*g_TwMgr->m_HelpBar->m_Font->m_CharWidth[(int)' ']);
                    Var->m_Base.m_TopMargin  = 2;
                    Var->m_ReadOnly = true;
                    Var->m_NoSlider = true;
                    Var->m_Base.m_Name = sdscpy(Var->m_Base.m_Name, "{");
                    Var->m_Base.m_Name = sdscat(Var->m_Base.m_Name, g_TwMgr->m_Structs.items[idx].m_Name);
                    Var->m_Base.m_Name = sdscat(Var->m_Base.m_Name, "}");
                    tw_da_append(&StructGrp->m_Vars, &Var->m_Base);
                    size_t structIndex = StructGrp->m_Vars.count-1;
                    if( sdslen(g_TwMgr->m_Structs.items[idx].m_Help)>0 )
                        AppendHelpString(StructGrp, g_TwMgr->m_Structs.items[idx].m_Help, 2, _Mgr->m_HelpBar->m_VarX2-_Mgr->m_HelpBar->m_VarX0-2*Var->m_Base.m_LeftMargin, TW_TYPE_HELP_ATOM);

                    // Append struct members
                    for( size_t im=0; im<g_TwMgr->m_Structs.items[idx].m_Members.count; ++im )
                    {
                        if( sdslen(g_TwMgr->m_Structs.items[idx].m_Members.items[im].m_Help)>0 )
                        {
                            CTwVarAtom *Var = CTwVarAtom_New();
                            Var->m_Ptr = NULL;
                            Var->m_Type = TW_TYPE_SHORTCUT;
                            Var->m_Val.m_Shortcut.m_Incr[0] = 0;
                            Var->m_Val.m_Shortcut.m_Incr[1] = 0;
                            Var->m_Val.m_Shortcut.m_Decr[0] = 0;
                            Var->m_Val.m_Shortcut.m_Decr[1] = 0;
                            Var->m_ReadOnly = false;
                            Var->m_NoSlider = true;
                            if( sdslen(g_TwMgr->m_Structs.items[idx].m_Members.items[im].m_Label)>0 )
                            {
                                Var->m_Base.m_Name = sdscpy(Var->m_Base.m_Name, "  ");
                                Var->m_Base.m_Name = sdscat(Var->m_Base.m_Name, g_TwMgr->m_Structs.items[idx].m_Members.items[im].m_Label);
                            }
                            else
                            {
                                Var->m_Base.m_Name = sdscpy(Var->m_Base.m_Name, "  ");
                                Var->m_Base.m_Name = sdscat(Var->m_Base.m_Name, g_TwMgr->m_Structs.items[idx].m_Members.items[im].m_Name);
                            }
                            tw_da_append(&StructGrp->m_Vars, &Var->m_Base);
                            //if( sdslen(g_TwMgr->m_Structs.items[idx].m_Members.items[im].m_Help)>0 )
                            AppendHelpString(StructGrp, g_TwMgr->m_Structs.items[idx].m_Members.items[im].m_Help, 3, _Mgr->m_HelpBar->m_VarX2-_Mgr->m_HelpBar->m_VarX0-4*Var->m_Base.m_LeftMargin, TW_TYPE_HELP_ATOM);
                        }
                    }

                    if( StructGrp->m_Vars.count==structIndex+1 ) // remove struct from help
                    {
                        CTwVar_Delete(StructGrp->m_Vars.items[structIndex]);
                        tw_da_resize(&StructGrp->m_Vars, structIndex);
                    }
                    else
                        ++MemberCount;
                }
            }
            if( StructGrp!=NULL )
            {
                if( MemberCount==1 )
                    StructGrp->m_Base.m_Name = sdscpy(StructGrp->m_Base.m_Name, "Structure");
                if( StructGrp->m_Vars.count>0 )
                    tw_da_append(&Grp->m_Vars, &StructGrp->m_Base);
                else
                {
                    CTwVarGroup_Free(StructGrp);
                    free(StructGrp);
                    StructGrp = NULL;
                }
            }
            tw_da_free(&UsedStructs);
        }

    // Append RotoSlider
    CTwVarGroup *RotoGrp = CTwVarGroup_New();
    RotoGrp->m_SummaryCallback = NULL;
    RotoGrp->m_SummaryClientData = NULL;
    RotoGrp->m_StructValuePtr = NULL;
    RotoGrp->m_Base.m_Name = sdscpy(RotoGrp->m_Base.m_Name, "RotoSlider");
    RotoGrp->m_Open = false;
    RotoGrp->m_Base.m_ColorPtr = &(_Mgr->m_HelpBar->m_ColGrpText);
    tw_da_append(&_Mgr->m_HelpBar->m_VarRoot.m_Vars, &RotoGrp->m_Base);
    AppendHelpString(RotoGrp, "The RotoSlider allows rapid editing of numerical values.", 0, _Mgr->m_HelpBar->m_VarX2-_Mgr->m_HelpBar->m_VarX0, TW_TYPE_HELP_ATOM);
    AppendHelpString(RotoGrp, "To modify a numerical value, click on its label or on its roto [.] button, then move the mouse outside of the grey circle while keeping the mouse button pressed, and turn around the circle to increase or decrease the numerical value.", 0, _Mgr->m_HelpBar->m_VarX2-_Mgr->m_HelpBar->m_VarX0, TW_TYPE_HELP_ATOM);
    AppendHelpString(RotoGrp, "The two grey lines depict the min and max bounds.", 0, _Mgr->m_HelpBar->m_VarX2-_Mgr->m_HelpBar->m_VarX0, TW_TYPE_HELP_ATOM);
    AppendHelpString(RotoGrp, "Moving the mouse far form the circle allows precise increase or decrease, while moving near the circle allows fast increase or decrease.", 0, _Mgr->m_HelpBar->m_VarX2-_Mgr->m_HelpBar->m_VarX0, TW_TYPE_HELP_ATOM);

    SynchroHierarchy(&_Mgr->m_HelpBar->m_VarRoot, &prevHierarchy);

    _Mgr->m_HelpBarNotUpToDate = false;
}

//  ---------------------------------------------------------------------------

// Builds a 32x32, 8-bit-per-channel RGBA (non-premultiplied alpha) bitmap
// from g_CurPict/g_CurMask/g_CurHot (res/TwXCursors.h) for cursor _CurIdx,
// for TwCursorCB's TW_CURSOR_CUSTOM case - the same source data
// PixmapCursor() converts to each platform's own native cursor format.
static void BuildCustomCursorRGBA(int _CurIdx, unsigned char *_OutRGBA32x32, int *_OutHotX, int *_OutHotY)
{
    for( int y=0; y<32; ++y )
    {
        for( int x=0; x<32; ++x )
        {
            const int src = x + y*32;
            const int dst = 4*src;
            const unsigned char shade = g_CurPict[_CurIdx][src] ? 255 : 0;
            _OutRGBA32x32[dst+0] = shade;
            _OutRGBA32x32[dst+1] = shade;
            _OutRGBA32x32[dst+2] = shade;
            _OutRGBA32x32[dst+3] = g_CurMask[_CurIdx][src] ? 255 : 0;
        }
    }
    *_OutHotX = g_CurHot[_CurIdx][0];
    *_OutHotY = g_CurHot[_CurIdx][1];
}

// Entry point used by CTwMgr_SetCursor(): dispatches to the callback
// installed via TwSetCursorCallback (see AntTweakBar.h) and reports whether
// it handled the request. TwSetCursorCallback() is the only cursor-shape
// mechanism now - the native per-platform fallback this used to gate has
// been deleted entirely (every platform), not ported.
static bool DispatchCursorCallback(ETwCursor _Semantic, int _BitmapIdx)
{
    if( g_CursorCallback==NULL )
        return false;
    if( _Semantic==TW_CURSOR_CUSTOM && _BitmapIdx>=0 )
    {
        unsigned char rgba[32*32*4];
        int hotX, hotY;
        BuildCustomCursorRGBA(_BitmapIdx, rgba, &hotX, &hotY);
        g_CursorCallback(_Semantic, rgba, hotX, hotY, g_CursorCallbackClientData);
    }
    else
        g_CursorCallback(_Semantic, NULL, 0, 0, g_CursorCallbackClientData);
    return true;
}

void CTwMgr_SetCursor(ETwCursor _Semantic, int _BitmapIdx)
{
    DispatchCursorCallback(_Semantic, _BitmapIdx);
}

//  ---------------------------------------------------------------------------

void TW_CALL TwSetCursorCallback(TwCursorCB _Callback, void *_ClientData)
{
    g_CursorCallback = _Callback;
    g_CursorCallbackClientData = _ClientData;
}

//  ---------------------------------------------------------------------------

void ANT_CALL TwCopyCDStringToClientFunc(TwCopyCDStringToClient copyCDStringToClientFunc)
{
    g_InitCopyCDStringToClient = copyCDStringToClientFunc;
    if( g_TwMgr!=NULL )
        g_TwMgr->m_CopyCDStringToClient = copyCDStringToClientFunc;
}

void ANT_CALL TwCopyCDStringToLibrary(char **destinationLibraryStringPtr, const char *sourceClientString)
{
    if( g_TwMgr==NULL )
    {
        if( destinationLibraryStringPtr!=NULL )
            *destinationLibraryStringPtr = (char *)(sourceClientString);
        return;
    }

    // static buffer to store sourceClientString copy associated to sourceClientString pointer
    void *Key = (void *)sourceClientString;
    CByteArray *Buf = NULL;
    for( size_t i=0; i<g_TwMgr->m_CDStdStringCopyBuffers.count; ++i )
        if( g_TwMgr->m_CDStdStringCopyBuffers.items[i].Key==Key )
        {
            Buf = &g_TwMgr->m_CDStdStringCopyBuffers.items[i].Value;
            break;
        }
    if( Buf==NULL )
    {
        CCDStringCopyEntry NewEntry;
        NewEntry.Key = Key;
        NewEntry.Value.items = NULL;
        NewEntry.Value.count = 0;
        NewEntry.Value.capacity = 0;
        tw_da_append(&g_TwMgr->m_CDStdStringCopyBuffers, NewEntry);
        Buf = &g_TwMgr->m_CDStdStringCopyBuffers.items[g_TwMgr->m_CDStdStringCopyBuffers.count-1].Value;
    }

    size_t len = (sourceClientString!=NULL) ? strlen(sourceClientString) : 0;
    if( Buf->count<len+1 )
        tw_da_resize(Buf, len+128); // len + some margin
    char *SrcStrCopy = Buf->items;
    SrcStrCopy[0] = '\0';
    if( sourceClientString!=NULL )
        memcpy(SrcStrCopy, sourceClientString, len+1);
    SrcStrCopy[len] = '\0';
    if( destinationLibraryStringPtr!=NULL )
        *destinationLibraryStringPtr = SrcStrCopy;
}

//  ---------------------------------------------------------------------------

bool CRect_Subtract(const CRect *_This, const CRect *_Rect, CRectArray *_OutRects)
{
    if( CRect_Empty(_This, 0) )
        return false;
    if( CRect_Empty(_Rect, 0) || _Rect->Y>=_This->Y+_This->H || _Rect->Y+_Rect->H<=_This->Y || _Rect->X>=_This->X+_This->W || _Rect->X+_Rect->W<=_This->X )
    {
        tw_da_append(_OutRects, *_This);
        return true;
    }

    bool Ret = false;
    int Y0 = _This->Y;
    int Y1 = _This->Y+_This->H-1;
    if( _Rect->Y>_This->Y )
    {
        Y0 = _Rect->Y;
        tw_da_append(_OutRects, CRect_Make(_This->X, _This->Y, _This->W, Y0-_This->Y+1));
        Ret = true;
    }
    if( _Rect->Y+_Rect->H<_This->Y+_This->H )
    {
        Y1 = _Rect->Y+_Rect->H;
        tw_da_append(_OutRects, CRect_Make(_This->X, Y1, _This->W, _This->Y+_This->H-Y1));
        Ret = true;
    }
    int X0 = _This->X;
    int X1 = _This->X+_This->W-1;
    if( _Rect->X>_This->X )
    {
        X0 = _Rect->X; //-2;
        tw_da_append(_OutRects, CRect_Make(_This->X, Y0, X0-_This->X+1, Y1-Y0+1));
        Ret = true;
    }
    if( _Rect->X+_Rect->W<_This->X+_This->W )
    {
        X1 = _Rect->X+_Rect->W; //-1;
        tw_da_append(_OutRects, CRect_Make(X1, Y0, _This->X+_This->W-X1, Y1-Y0+1));
        Ret = true;
    }
    return Ret;
}

bool CRect_SubtractMany(const CRect *_This, const CRectArray *_Rects, CRectArray *_OutRects)
{
    _OutRects->count = 0;
    size_t i, j, NbRects = _Rects->count;
    if( NbRects==0 )
    {
        tw_da_append(_OutRects, *_This);
        return true;
    }
    else
    {
        CRectArray TmpRects = {0};
        CRect_Subtract(_This, &_Rects->items[0], _OutRects);

        for( i=1; i<NbRects; i++)
        {
            for( j=0; j<_OutRects->count; j++ )
                CRect_Subtract(&_OutRects->items[j], &_Rects->items[i], &TmpRects);
            { CRectArray Swap = *_OutRects; *_OutRects = TmpRects; TmpRects = Swap; }
            TmpRects.count = 0;
        }
        bool Empty = (_OutRects->count==0);
        tw_da_free(&TmpRects);
        return Empty;
    }
}

//  ---------------------------------------------------------------------------

