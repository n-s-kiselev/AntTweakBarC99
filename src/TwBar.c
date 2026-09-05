//  ---------------------------------------------------------------------------
//
//  @file       TwBar.cpp
//  @author     Philippe Decaudin
//  @license    This file is part of the AntTweakBar library.
//              For conditions of distribution and use, see License.txt
//
//  ---------------------------------------------------------------------------


#include "TwPrecomp.h"
#include <AntTweakBar.h>
#include "TwMgr.h"
#include "TwBar.h"
#include "TwColors.h"
#include <GLFW/glfw3.h> // EditInPlaceGetClipboard/SetClipboard delegate to GLFW3's clipboard

extern const char *g_ErrNotFound;
const char *g_ErrUnknownAttrib  = "Unknown parameter"; // shared with TwMgr.c (see its own `extern const char *g_ErrUnknownAttrib;`)
static const char *g_ErrInvalidAttrib  = "Invalid parameter";
static const char *g_ErrNotGroup       = "Value is not a group";
const char *g_ErrNoValue        = "Value required"; // shared with TwMgr.c (see its own `extern const char *g_ErrNoValue;`)
const char *g_ErrBadValue       = "Bad value"; // shared with TwMgr.c (see its own `extern const char *g_ErrBadValue;`)
static const char *g_ErrUnknownType    = "Unknown type";
static const char *g_ErrNotEnum        = "Must be of type Enum";

#undef PERF         // comment to print benchs
#define PERF(cmd)

// Was PerfTimer g_BarTimer - a free-running clock never Reset() after
// construction, so "elapsed since construction" was really just "elapsed
// since program start," identical to what glfwGetTime() already provides.
// All 4 call sites now call glfwGetTime() directly.

// Maps each named native cursor to the toolkit-agnostic ETwCursor value (and,
// for the custom ones, the res/TwXCursors.h bitmap index) reported to a
// callback installed via TwSetCursorCallback (see AntTweakBar.h and
// CTwMgr_SetCursor). Center/Point/the 12 roto cursors have no
// standard-shape equivalent across platforms/toolkits, so they all collapse
// to TW_CURSOR_CUSTOM plus a bitmap index CTwMgr_SetCursor converts to an
// RGBA bitmap on demand.
#define ANT_CURSOR_SEMANTIC_Arrow           TW_CURSOR_ARROW
#define ANT_CURSOR_SEMANTIC_Move            TW_CURSOR_MOVE
#define ANT_CURSOR_SEMANTIC_WE              TW_CURSOR_RESIZE_WE
#define ANT_CURSOR_SEMANTIC_NS              TW_CURSOR_RESIZE_NS
#define ANT_CURSOR_SEMANTIC_TopRight        TW_CURSOR_RESIZE_NESW
#define ANT_CURSOR_SEMANTIC_BottomLeft      TW_CURSOR_RESIZE_NESW
#define ANT_CURSOR_SEMANTIC_TopLeft         TW_CURSOR_RESIZE_NWSE
#define ANT_CURSOR_SEMANTIC_BottomRight     TW_CURSOR_RESIZE_NWSE
#define ANT_CURSOR_SEMANTIC_Help            TW_CURSOR_HELP
#define ANT_CURSOR_SEMANTIC_Hand            TW_CURSOR_HAND
#define ANT_CURSOR_SEMANTIC_Cross           TW_CURSOR_CROSS
#define ANT_CURSOR_SEMANTIC_UpArrow         TW_CURSOR_UPARROW
#define ANT_CURSOR_SEMANTIC_No              TW_CURSOR_NO
#define ANT_CURSOR_SEMANTIC_IBeam           TW_CURSOR_IBEAM
#define ANT_CURSOR_SEMANTIC_Center          TW_CURSOR_CUSTOM
#define ANT_CURSOR_SEMANTIC_Point           TW_CURSOR_CUSTOM

#define ANT_CURSOR_BITMAP_Arrow             -1
#define ANT_CURSOR_BITMAP_Move              -1
#define ANT_CURSOR_BITMAP_WE                -1
#define ANT_CURSOR_BITMAP_NS                -1
#define ANT_CURSOR_BITMAP_TopRight          -1
#define ANT_CURSOR_BITMAP_BottomLeft        -1
#define ANT_CURSOR_BITMAP_TopLeft           -1
#define ANT_CURSOR_BITMAP_BottomRight       -1
#define ANT_CURSOR_BITMAP_Help              -1
#define ANT_CURSOR_BITMAP_Hand              -1
#define ANT_CURSOR_BITMAP_Cross             -1
#define ANT_CURSOR_BITMAP_UpArrow           -1
#define ANT_CURSOR_BITMAP_No                -1
#define ANT_CURSOR_BITMAP_IBeam             -1
#define ANT_CURSOR_BITMAP_Center            0
#define ANT_CURSOR_BITMAP_Point             1

#define ANT_SET_CURSOR(_Name)       CTwMgr_SetCursor(ANT_CURSOR_SEMANTIC_##_Name, ANT_CURSOR_BITMAP_##_Name)
#define ANT_SET_ROTO_CURSOR(_Num)   CTwMgr_SetCursor(TW_CURSOR_CUSTOM, 2+(_Num))

#if !defined(ANT_WINDOWS)
#   define _stricmp strcasecmp
#   define _strdup  strdup
#endif  // defined(ANT_WINDOWS)

#if !defined(M_PI)
#   define M_PI 3.1415926535897932384626433832795
#endif  // !defined(M_PI)

static const float  FLOAT_MAX  = 3.0e+38f;
static const double DOUBLE_MAX = 1.0e+308;
static const double DOUBLE_EPS = 1.0e-307;

bool IsCustomType(int _Type)
{
    return (g_TwMgr && _Type>=(int)TW_TYPE_CUSTOM_BASE && _Type<(int)TW_TYPE_CUSTOM_BASE+g_TwMgr->m_NbCustoms);
}

bool IsCSStringType(int _Type)
{
    return (_Type>(int)TW_TYPE_CSSTRING_BASE && _Type<=(int)TW_TYPE_CSSTRING_MAX);
}

bool IsEnumType(int _Type)
{
    return (g_TwMgr && _Type>=(int)TW_TYPE_ENUM_BASE && _Type<(int)TW_TYPE_ENUM_BASE+(int)g_TwMgr->m_Enums.count);
}

//  ---------------------------------------------------------------------------

void CTwVar_InitBase(CTwVar *_Var, ETwVarKind _Kind)
{
    _Var->m_Kind = _Kind;
    _Var->m_Name = sdsempty();
    _Var->m_Label = sdsempty();
    _Var->m_Help = sdsempty();
    _Var->m_IsRoot = false;
    _Var->m_DontClip = false;
    _Var->m_Visible = true;
    _Var->m_LeftMargin = 0;
    _Var->m_TopMargin = 0;
    _Var->m_ColorPtr = &COLOR32_WHITE;
    _Var->m_BgColorPtr = &COLOR32_ZERO;   // default
}

void CTwVar_FreeBase(CTwVar *_Var)
{
    sdsfree(_Var->m_Name);
    sdsfree(_Var->m_Label);
    sdsfree(_Var->m_Help);
}

// Dispatchers: forward to the concrete kind's own (ordinary, non-virtual)
// method, replacing the C++ virtual calls this project used to rely on.
bool CTwVar_IsCustom(const CTwVar *_Var)
{
    return _Var->m_Kind==TW_VARKIND_ATOM && IsCustomType(((const CTwVarAtom *)_Var)->m_Type);
}

const CTwVar *CTwVar_Find(const CTwVar *_Var, const char *_Name, CTwVarGroup **_Parent, int *_Index)
{
    if( _Var->m_Kind==TW_VARKIND_ATOM )
        return CTwVarAtom_Find((const CTwVarAtom *)_Var, _Name, _Parent, _Index);
    else
        return CTwVarGroup_Find((const CTwVarGroup *)_Var, _Name, _Parent, _Index);
}

int CTwVar_HasAttrib(const CTwVar *_Var, const char *_Attrib, bool *_HasValue)
{
    if( _Var->m_Kind==TW_VARKIND_ATOM )
        return CTwVarAtom_HasAttrib((const CTwVarAtom *)_Var, _Attrib, _HasValue);
    else
        return CTwVarGroup_HasAttrib((const CTwVarGroup *)_Var, _Attrib, _HasValue);
}

int CTwVar_SetAttrib(CTwVar *_Var, int _AttribID, const char *_Value, TwBar *_Bar, CTwVarGroup *_VarParent, int _VarIndex)
{
    if( _Var->m_Kind==TW_VARKIND_ATOM )
        return CTwVarAtom_SetAttrib((CTwVarAtom *)_Var, _AttribID, _Value, _Bar, _VarParent, _VarIndex);
    else
        return CTwVarGroup_SetAttrib((CTwVarGroup *)_Var, _AttribID, _Value, _Bar, _VarParent, _VarIndex);
}

ERetType CTwVar_GetAttrib(const CTwVar *_Var, int _AttribID, TwBar *_Bar, CTwVarGroup *_VarParent, int _VarIndex, CDoubleArray *outDouble, sds *outString)
{
    if( _Var->m_Kind==TW_VARKIND_ATOM )
        return CTwVarAtom_GetAttrib((const CTwVarAtom *)_Var, _AttribID, _Bar, _VarParent, _VarIndex, outDouble, outString);
    else
        return CTwVarGroup_GetAttrib((const CTwVarGroup *)_Var, _AttribID, _Bar, _VarParent, _VarIndex, outDouble, outString);
}

void CTwVar_SetReadOnly(CTwVar *_Var, bool _ReadOnly)
{
    if( _Var->m_Kind==TW_VARKIND_ATOM )
        CTwVarAtom_SetReadOnly((CTwVarAtom *)_Var, _ReadOnly);
    else
        CTwVarGroup_SetReadOnly((CTwVarGroup *)_Var, _ReadOnly);
}

bool CTwVar_IsReadOnly(const CTwVar *_Var)
{
    if( _Var->m_Kind==TW_VARKIND_ATOM )
        return CTwVarAtom_IsReadOnly((const CTwVarAtom *)_Var);
    else
        return CTwVarGroup_IsReadOnly((const CTwVarGroup *)_Var);
}

void CTwVar_Delete(CTwVar *_Var)
{
    if( _Var==NULL )
        return;
    if( _Var->m_Kind==TW_VARKIND_ATOM )
        CTwVarAtom_Free((CTwVarAtom *)_Var);
    else
        CTwVarGroup_Free((CTwVarGroup *)_Var);
    free(_Var);
}

void CTwVarAtom_Init(CTwVarAtom *_Atom)
{
    CTwVar_InitBase(&_Atom->m_Base, TW_VARKIND_ATOM);
    _Atom->m_Type = TW_TYPE_UNDEF;
    _Atom->m_Ptr = NULL;
    _Atom->m_SetCallback = NULL;
    _Atom->m_GetCallback = NULL;
    _Atom->m_ClientData = NULL;
    _Atom->m_ReadOnly = false;
    _Atom->m_NoSlider = false;
    _Atom->m_KeyIncr[0] = 0;
    _Atom->m_KeyIncr[1] = 0;
    _Atom->m_KeyDecr[0] = 0;
    _Atom->m_KeyDecr[1] = 0;
    memset(&_Atom->m_Val, 0, sizeof(union CTwVal));
}

void CTwVarAtom_Free(CTwVarAtom *_Atom)
{
    if( _Atom->m_Type==TW_TYPE_BOOL8 || _Atom->m_Type==TW_TYPE_BOOL16 || _Atom->m_Type==TW_TYPE_BOOL32 || _Atom->m_Type==TW_TYPE_BOOLCPP )
    {
        if( _Atom->m_Val.m_Bool.m_FreeTrueString && _Atom->m_Val.m_Bool.m_TrueString!=NULL )
        {
            free(_Atom->m_Val.m_Bool.m_TrueString);
            _Atom->m_Val.m_Bool.m_TrueString = NULL;
        }
        if( _Atom->m_Val.m_Bool.m_FreeFalseString && _Atom->m_Val.m_Bool.m_FalseString!=NULL )
        {
            free(_Atom->m_Val.m_Bool.m_FalseString);
            _Atom->m_Val.m_Bool.m_FalseString = NULL;
        }
    }
    // TW_TYPE_CDSTDSTRING (the internal type TW_TYPE_STDSTRING variables
    // were converted to) cleanup removed along with TW_TYPE_STDSTRING.
    /*
    else if( m_Type==TW_TYPE_ENUM8 || m_Type==TW_TYPE_ENUM16 || m_Type==TW_TYPE_ENUM32 )
    {
        if( m_Val.m_Enum.m_Entries!=NULL )
        {
            delete m_Val.m_Enum.m_Entries;
            m_Val.m_Enum.m_Entries = NULL;
        }
    }
    */
    CTwVar_FreeBase(&_Atom->m_Base);
}

CTwVarAtom *CTwVarAtom_New(void)
{
    CTwVarAtom *Atom = (CTwVarAtom *)malloc(sizeof(CTwVarAtom));
    CTwVarAtom_Init(Atom);
    return Atom;
}

//  ---------------------------------------------------------------------------

void CTwVarAtom_ValueToString(const CTwVarAtom *_Atom, sds *_Str)
{
    assert(_Str!=NULL);
    static const char *ErrStr = "unreachable";
    char Tmp[1024];
    if( _Atom->m_Type==TW_TYPE_UNDEF || _Atom->m_Type==TW_TYPE_HELP_ATOM || _Atom->m_Type==TW_TYPE_HELP_GRP || _Atom->m_Type==TW_TYPE_BUTTON )  // has no value
    {
        *_Str = sdscpy(*_Str, "");
        return;
    }
    else if( _Atom->m_Type==TW_TYPE_HELP_HEADER )
    {
        *_Str = sdscpy(*_Str, "SHORTCUTS");
        return;
    }
    else if( _Atom->m_Type==TW_TYPE_SHORTCUT ) // special case for help bar: display shortcut
    {
        *_Str = sdscpy(*_Str, "");
        if( _Atom->m_ReadOnly && _Atom->m_Val.m_Shortcut.m_Incr[0]==0 && _Atom->m_Val.m_Shortcut.m_Decr[0]==0 )
            *_Str = sdscpy(*_Str, "(read only)");
        else
        {
            if( _Atom->m_Val.m_Shortcut.m_Incr[0]>0 )
                TwGetKeyString(_Str, _Atom->m_Val.m_Shortcut.m_Incr[0], _Atom->m_Val.m_Shortcut.m_Incr[1]);
            else
                *_Str = sdscat(*_Str, "(none)");
            if( _Atom->m_Val.m_Shortcut.m_Decr[0]>0 )
            {
                *_Str = sdscat(*_Str, "  ");
                TwGetKeyString(_Str, _Atom->m_Val.m_Shortcut.m_Decr[0], _Atom->m_Val.m_Shortcut.m_Decr[1]);
            }
        }
        return;
    }
    else if( _Atom->m_Type==TW_TYPE_HELP_STRUCT )
    {
        int idx = _Atom->m_Val.m_HelpStruct.m_StructType - TW_TYPE_STRUCT_BASE;
        if( idx>=0 && idx<(int)g_TwMgr->m_Structs.count )
        {
            if( sdslen(g_TwMgr->m_Structs.items[idx].m_Name)>0 )
            {
                *_Str = sdscpy(*_Str, "{");
                *_Str = sdscat(*_Str, g_TwMgr->m_Structs.items[idx].m_Name);
                *_Str = sdscat(*_Str, "}");
            }
            else
                *_Str = sdscpy(*_Str, "{struct}");
        }
        return;
    }

    if( _Atom->m_Ptr==NULL && _Atom->m_GetCallback==NULL )
    {
        *_Str = sdscpy(*_Str, ErrStr);
        return;
    }
    bool UseGet = (_Atom->m_GetCallback!=NULL);
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch" // TW_TYPE_BOOLCPP (see TwMgr.h) is a numeric macro here, not a real ETwType enumerator, in a real-C99 build
    switch( _Atom->m_Type )
    {
    case TW_TYPE_BOOLCPP:
        {
            bool Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(bool *)_Atom->m_Ptr;
            if( Val )
                *_Str = sdscpy(*_Str, (_Atom->m_Val.m_Bool.m_TrueString!=NULL) ? _Atom->m_Val.m_Bool.m_TrueString : "1");
            else
                *_Str = sdscpy(*_Str, (_Atom->m_Val.m_Bool.m_FalseString!=NULL) ? _Atom->m_Val.m_Bool.m_FalseString : "0");
        }
        break;
    case TW_TYPE_BOOL8:
        {
            char Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(char *)_Atom->m_Ptr;
            if( Val )
                *_Str = sdscpy(*_Str, (_Atom->m_Val.m_Bool.m_TrueString!=NULL) ? _Atom->m_Val.m_Bool.m_TrueString : "1");
            else
                *_Str = sdscpy(*_Str, (_Atom->m_Val.m_Bool.m_FalseString!=NULL) ? _Atom->m_Val.m_Bool.m_FalseString : "0");
        }
        break;
    case TW_TYPE_BOOL16:
        {
            short Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(short *)_Atom->m_Ptr;
            if( Val )
                *_Str = sdscpy(*_Str, (_Atom->m_Val.m_Bool.m_TrueString!=NULL) ? _Atom->m_Val.m_Bool.m_TrueString : "1");
            else
                *_Str = sdscpy(*_Str, (_Atom->m_Val.m_Bool.m_FalseString!=NULL) ? _Atom->m_Val.m_Bool.m_FalseString : "0");
        }
        break;
    case TW_TYPE_BOOL32:
        {
            int Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(int *)_Atom->m_Ptr;
            if( Val )
                *_Str = sdscpy(*_Str, (_Atom->m_Val.m_Bool.m_TrueString!=NULL) ? _Atom->m_Val.m_Bool.m_TrueString : "1");
            else
                *_Str = sdscpy(*_Str, (_Atom->m_Val.m_Bool.m_FalseString!=NULL) ? _Atom->m_Val.m_Bool.m_FalseString : "0");
        }
        break;
    case TW_TYPE_CHAR:
        {
            unsigned char Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(unsigned char *)_Atom->m_Ptr;
            if( Val!=0 )
            {
                int d = Val;
                if( _Atom->m_Val.m_Char.m_Hexa )
                    sprintf(Tmp, "%c (0x%.2X)", Val, d);
                else
                    sprintf(Tmp, "%c (%d)", Val, d);
                *_Str = sdscpy(*_Str, Tmp);
            }
            else
            {
                *_Str = sdscpy(*_Str, "  (0)");
                (*_Str)[0] = '\0';
            }
        }
        break;
    case TW_TYPE_INT8:
        {
            signed char Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(signed char *)_Atom->m_Ptr;
            int d = Val;
            if( _Atom->m_Val.m_Int8.m_Hexa )
                sprintf(Tmp, "0x%.2X", d&0xff);
            else
                sprintf(Tmp, "%d", d);
            *_Str = sdscpy(*_Str, Tmp);
        }
        break;
    case TW_TYPE_UINT8:
        {
            unsigned char Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(unsigned char *)_Atom->m_Ptr;
            unsigned int d = Val;
            if( _Atom->m_Val.m_UInt8.m_Hexa )
                sprintf(Tmp, "0x%.2X", d);
            else        
                sprintf(Tmp, "%u", d);
            *_Str = sdscpy(*_Str, Tmp);
        }
        break;
    case TW_TYPE_INT16:
        {
            short Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(short *)_Atom->m_Ptr;
            int d = Val;
            if( _Atom->m_Val.m_Int16.m_Hexa )
                sprintf(Tmp, "0x%.4X", d&0xffff);
            else
                sprintf(Tmp, "%d", d);
            *_Str = sdscpy(*_Str, Tmp);
        }
        break;
    case TW_TYPE_UINT16:
        {
            unsigned short Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(unsigned short *)_Atom->m_Ptr;
            unsigned int d = Val;
            if( _Atom->m_Val.m_UInt16.m_Hexa )
                sprintf(Tmp, "0x%.4X", d);
            else
                sprintf(Tmp, "%u", d);
            *_Str = sdscpy(*_Str, Tmp);
        }
        break;
    case TW_TYPE_INT32:
        {
            int Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(int *)_Atom->m_Ptr;
            if( _Atom->m_Val.m_Int32.m_Hexa )
                sprintf(Tmp, "0x%.8X", Val);
            else
                sprintf(Tmp, "%d", Val);
            *_Str = sdscpy(*_Str, Tmp);
        }
        break;
    case TW_TYPE_UINT32:
        {
            unsigned int Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(unsigned int *)_Atom->m_Ptr;
            if( _Atom->m_Val.m_UInt32.m_Hexa )
                sprintf(Tmp, "0x%.8X", Val);
            else
                sprintf(Tmp, "%u", Val);
            *_Str = sdscpy(*_Str, Tmp);
        }
        break;
    case TW_TYPE_FLOAT:
        {
            float Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(float *)_Atom->m_Ptr;
            if( _Atom->m_Val.m_Float32.m_Precision<0 )
                sprintf(Tmp, "%g", Val);
            else
            {
                char Fmt[64];
                sprintf(Fmt, "%%.%df", (int)_Atom->m_Val.m_Float32.m_Precision);
                sprintf(Tmp, Fmt, Val);
            }
            *_Str = sdscpy(*_Str, Tmp);
        }
        break;  
    case TW_TYPE_DOUBLE:
        {
            double Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(double *)_Atom->m_Ptr;
            if( _Atom->m_Val.m_Float64.m_Precision<0 )
                sprintf(Tmp, "%g", Val);
            else
            {
                char Fmt[128];
                sprintf(Fmt, "%%.%dlf", (int)_Atom->m_Val.m_Float64.m_Precision);
                sprintf(Tmp, Fmt, Val);
            }
            *_Str = sdscpy(*_Str, Tmp);
        }
        break;
    // TW_TYPE_STDSTRING case removed along with the type itself.
    /*
    case TW_TYPE_ENUM8:
    case TW_TYPE_ENUM16:
    case TW_TYPE_ENUM32:
        {
            unsigned int d = 0;
            if( _Atom->m_Type==TW_TYPE_ENUM8 )
            {
                unsigned char Val = 0;
                if( UseGet )
                    _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
                else
                    Val = *(unsigned char *)_Atom->m_Ptr;
                d = Val;
            }
            else if( _Atom->m_Type==TW_TYPE_ENUM16 )
            {
                unsigned short Val = 0;
                if( UseGet )
                    _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
                else
                    Val = *(unsigned short *)_Atom->m_Ptr;
                d = Val;
            }
            else
            {
                assert(_Atom->m_Type==TW_TYPE_ENUM32);
                unsigned int Val = 0;
                if( UseGet )
                    _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
                else
                    Val = *(unsigned int *)_Atom->m_Ptr;
                d = Val;
            }
            bool Found = false;
            if( _Atom->m_Val.m_Enum.m_Entries!=NULL )
            {
                UVal::CEnumVal::CEntries::iterator It = _Atom->m_Val.m_Enum.m_Entries->find(d);
                if( It!=_Atom->m_Val.m_Enum.m_Entries->end() )
                {
                    *_Str = sdscpy(*_Str, It->second);
                    Found = true;
                }
            }
            if( !Found )
            {
                sprintf(Tmp, "%u", d);
                *_Str = sdscpy(*_Str, Tmp);
            }
        }
        break;
    */
    default:
        if( IsEnumType(_Atom->m_Type) )
        {
            unsigned int Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(unsigned int *)_Atom->m_Ptr;

            CEnum *e = &g_TwMgr->m_Enums.items[_Atom->m_Type-TW_TYPE_ENUM_BASE];
            sds Label = CEnum_Find(e, Val);
            if( Label!=NULL )
                *_Str = sdscpy(*_Str, Label);
            else
            {
                sprintf(Tmp, "%u", Val);
                *_Str = sdscpy(*_Str, Tmp);
            }
        }
        else if( IsCSStringType(_Atom->m_Type) )
        {
            char *Val = NULL;
            if( UseGet )
            {
                int n = TW_CSSTRING_SIZE(_Atom->m_Type);
                if( n+32>(int)g_TwMgr->m_CSStringBuffer.count )
                    tw_da_resize(&g_TwMgr->m_CSStringBuffer, (size_t)(n+32));
                Val = g_TwMgr->m_CSStringBuffer.items;
                _Atom->m_GetCallback(Val , _Atom->m_ClientData);
                Val[n] = '\0';
            }
            else
                Val = (char *)_Atom->m_Ptr;
            if( Val!=NULL )
                *_Str = sdscpy(*_Str, Val);
            else
                *_Str = sdscpy(*_Str, "");
        }
        else if( _Atom->m_Type==TW_TYPE_CDSTRING )
        {
            char *Val = NULL;
            if( UseGet )
                _Atom->m_GetCallback(&Val , _Atom->m_ClientData);
            else
                Val = *(char **)_Atom->m_Ptr;
            if( Val!=NULL )
                *_Str = sdscpy(*_Str, Val);
            else
                *_Str = sdscpy(*_Str, "");
        }
        else if( IsCustomType(_Atom->m_Type) ) // _Atom->m_Type>=TW_TYPE_CUSTOM_BASE && _Atom->m_Type<TW_TYPE_CUSTOM_BASE+(int)g_TwMgr->m_Customs.size() )
        {
            *_Str = sdscpy(*_Str, "");
        }
        else
        {
            *_Str = sdscpy(*_Str, "unknown type");
            ((CTwVarAtom *)(_Atom))->m_ReadOnly = true;
        }
    }
    #pragma GCC diagnostic pop
}

//  ---------------------------------------------------------------------------

double CTwVarAtom_ValueToDouble(const CTwVarAtom *_Atom)
{
    if( _Atom->m_Ptr==NULL && _Atom->m_GetCallback==NULL )
        return 0;   // unreachable
    bool UseGet = (_Atom->m_GetCallback!=NULL);
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch" // TW_TYPE_BOOLCPP (see TwMgr.h) is a numeric macro here, not a real ETwType enumerator, in a real-C99 build
    switch( _Atom->m_Type )
    {
    case TW_TYPE_BOOLCPP:
        {
            bool Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(bool *)_Atom->m_Ptr;
            if( Val )
                return 1;
            else
                return 0;
        }
        break;
    case TW_TYPE_BOOL8:
        {
            char Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(char *)_Atom->m_Ptr;
            if( Val )
                return 1;
            else
                return 0;
        }
        break;
    case TW_TYPE_BOOL16:
        {
            short Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(short *)_Atom->m_Ptr;
            if( Val )
                return 1;
            else
                return 0;
        }
        break;
    case TW_TYPE_BOOL32:
        {
            int Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(int *)_Atom->m_Ptr;
            if( Val )
                return 1;
            else
                return 0;
        }
        break;
    case TW_TYPE_CHAR:
        {
            unsigned char Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(unsigned char *)_Atom->m_Ptr;
            return Val;
        }
        break;
    case TW_TYPE_INT8:
        {
            signed char Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(signed char *)_Atom->m_Ptr;
            int d = Val;
            return d;
        }
        break;
    case TW_TYPE_UINT8:
        {
            unsigned char Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(unsigned char *)_Atom->m_Ptr;
            unsigned int d = Val;
            return d;
        }
        break;
    case TW_TYPE_INT16:
        {
            short Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(short *)_Atom->m_Ptr;
            int d = Val;
            return d;
        }
        break;
    case TW_TYPE_UINT16:
        {
            unsigned short Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(unsigned short *)_Atom->m_Ptr;
            unsigned int d = Val;
            return d;
        }
        break;
    case TW_TYPE_INT32:
        {
            int Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(int *)_Atom->m_Ptr;
            return Val;
        }
        break;
    case TW_TYPE_UINT32:
        {
            unsigned int Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(unsigned int *)_Atom->m_Ptr;
            return Val;
        }
        break;
    case TW_TYPE_FLOAT:
        {
            float Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(float *)_Atom->m_Ptr;
            return Val;
        }
        break;  
    case TW_TYPE_DOUBLE:
        {
            double Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(double *)_Atom->m_Ptr;
            return Val;
        }
        break;
    /*
    case TW_TYPE_ENUM8:
    case TW_TYPE_ENUM16:
    case TW_TYPE_ENUM32:
        {
            unsigned int d = 0;
            if( _Atom->m_Type==TW_TYPE_ENUM8 )
            {
                unsigned char Val = 0;
                if( UseGet )
                    _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
                else
                    Val = *(unsigned char *)_Atom->m_Ptr;
                d = Val;
            }
            else if( _Atom->m_Type==TW_TYPE_ENUM16 )
            {
                unsigned short Val = 0;
                if( UseGet )
                    _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
                else
                    Val = *(unsigned short *)_Atom->m_Ptr;
                d = Val;
            }
            else
            {
                assert(_Atom->m_Type==TW_TYPE_ENUM32);
                unsigned int Val = 0;
                if( UseGet )
                    _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
                else
                    Val = *(unsigned int *)_Atom->m_Ptr;
                d = Val;
            }
            return d;
        }
        break;
    */
    default:
        if( IsEnumType(_Atom->m_Type) )
        {
            unsigned int Val = 0;
            if( UseGet )
                _Atom->m_GetCallback(&Val, _Atom->m_ClientData);
            else
                Val = *(unsigned int *)_Atom->m_Ptr;
            return Val;
        }
        else
            return 0; // unknown type
    }
    #pragma GCC diagnostic pop
}

//  ---------------------------------------------------------------------------

void CTwVarAtom_ValueFromDouble(CTwVarAtom *_Atom, double _Val)
{
    if( _Atom->m_Ptr==NULL && _Atom->m_SetCallback==NULL )
        return; // unreachable
    bool UseSet = (_Atom->m_SetCallback!=NULL);
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch" // TW_TYPE_BOOLCPP (see TwMgr.h) is a numeric macro here, not a real ETwType enumerator, in a real-C99 build
    switch( _Atom->m_Type )
    {
    case TW_TYPE_BOOLCPP:
        {
            bool Val = (_Val!=0);
            if( UseSet )
                _Atom->m_SetCallback(&Val, _Atom->m_ClientData);
            else
                *(bool*)_Atom->m_Ptr = Val;
        }
        break;
    case TW_TYPE_BOOL8:
        {
            char Val = (_Val!=0) ? 1 : 0;
            if( UseSet )
                _Atom->m_SetCallback(&Val, _Atom->m_ClientData);
            else
                *(char*)_Atom->m_Ptr = Val;
        }
        break;
    case TW_TYPE_BOOL16:
        {
            short Val = (_Val!=0) ? 1 : 0;
            if( UseSet )
                _Atom->m_SetCallback(&Val, _Atom->m_ClientData);
            else
                *(short*)_Atom->m_Ptr = Val;
        }
        break;
    case TW_TYPE_BOOL32:
        {
            int Val = (_Val!=0) ? 1 : 0;
            if( UseSet )
                _Atom->m_SetCallback(&Val, _Atom->m_ClientData);
            else
                *(int*)_Atom->m_Ptr = Val;
        }
        break;
    case TW_TYPE_CHAR:
        {
            unsigned char Val = (unsigned char)_Val;
            if( UseSet )
                _Atom->m_SetCallback(&Val, _Atom->m_ClientData);
            else
                *(unsigned char*)_Atom->m_Ptr = Val;
        }
        break;
    case TW_TYPE_INT8:
        {
            signed char Val = (signed char)_Val;
            if( UseSet )
                _Atom->m_SetCallback(&Val, _Atom->m_ClientData);
            else
                *(signed char*)_Atom->m_Ptr = Val;
        }
        break;
    case TW_TYPE_UINT8:
    //case TW_TYPE_ENUM8:
        {
            unsigned char Val = (unsigned char)_Val;
            if( UseSet )
                _Atom->m_SetCallback(&Val, _Atom->m_ClientData);
            else
                *(unsigned char*)_Atom->m_Ptr = Val;
        }
        break;
    case TW_TYPE_INT16:
        {
            short Val = (short)_Val;
            if( UseSet )
                _Atom->m_SetCallback(&Val, _Atom->m_ClientData);
            else
                *(short*)_Atom->m_Ptr = Val;
        }
        break;
    case TW_TYPE_UINT16:
    //case TW_TYPE_ENUM16:
        {
            unsigned short Val = (unsigned short)_Val;
            if( UseSet )
                _Atom->m_SetCallback(&Val, _Atom->m_ClientData);
            else
                *(unsigned short*)_Atom->m_Ptr = Val;
        }
        break;
    case TW_TYPE_INT32:
        {
            int Val = (int)_Val;
            if( UseSet )
                _Atom->m_SetCallback(&Val, _Atom->m_ClientData);
            else
                *(int*)_Atom->m_Ptr = Val;
        }
        break;
    case TW_TYPE_UINT32:
    //case TW_TYPE_ENUM32:
        {
            unsigned int Val = (unsigned int)_Val;
            if( UseSet )
                _Atom->m_SetCallback(&Val, _Atom->m_ClientData);
            else
                *(unsigned int*)_Atom->m_Ptr = Val;
        }
        break;
    case TW_TYPE_FLOAT:
        {
            float Val = (float)_Val;
            if( UseSet )
                _Atom->m_SetCallback(&Val, _Atom->m_ClientData);
            else
                *(float*)_Atom->m_Ptr = Val;
        }
        break;
    case TW_TYPE_DOUBLE:
        {
            double Val = (double)_Val;
            if( UseSet )
                _Atom->m_SetCallback(&Val, _Atom->m_ClientData);
            else
                *(double*)_Atom->m_Ptr = Val;
        }
        break;
    default:
        if( IsEnumType(_Atom->m_Type) )
        {
            unsigned int Val = (unsigned int)_Val;
            if( UseSet )
                _Atom->m_SetCallback(&Val, _Atom->m_ClientData);
            else
                *(unsigned int*)_Atom->m_Ptr = Val;
        }
    }
    #pragma GCC diagnostic pop
}

//  ---------------------------------------------------------------------------

void CTwVarAtom_MinMaxStepToDouble(const CTwVarAtom *_Atom, double *_Min, double *_Max, double *_Step)
{
    double max = DOUBLE_MAX;
    double min = -DOUBLE_MAX;
    double step = 1;

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch" // TW_TYPE_BOOLCPP (see TwMgr.h) is a numeric macro here, not a real ETwType enumerator, in a real-C99 build
    switch( _Atom->m_Type )
    {
    case TW_TYPE_BOOLCPP:
    case TW_TYPE_BOOL8:
    case TW_TYPE_BOOL16:
    case TW_TYPE_BOOL32:
        min = 0;
        max = 1;
        step = 1;
        break;
    case TW_TYPE_CHAR:
        min = (double)_Atom->m_Val.m_Char.m_Min;
        max = (double)_Atom->m_Val.m_Char.m_Max;
        step = (double)_Atom->m_Val.m_Char.m_Step;
        break;
    case TW_TYPE_INT8:
        min = (double)_Atom->m_Val.m_Int8.m_Min;
        max = (double)_Atom->m_Val.m_Int8.m_Max;
        step = (double)_Atom->m_Val.m_Int8.m_Step;
        break;
    case TW_TYPE_UINT8:
        min = (double)_Atom->m_Val.m_UInt8.m_Min;
        max = (double)_Atom->m_Val.m_UInt8.m_Max;
        step = (double)_Atom->m_Val.m_UInt8.m_Step;
        break;
    case TW_TYPE_INT16:
        min = (double)_Atom->m_Val.m_Int16.m_Min;
        max = (double)_Atom->m_Val.m_Int16.m_Max;
        step = (double)_Atom->m_Val.m_Int16.m_Step;
        break;
    case TW_TYPE_UINT16:
        min = (double)_Atom->m_Val.m_UInt16.m_Min;
        max = (double)_Atom->m_Val.m_UInt16.m_Max;
        step = (double)_Atom->m_Val.m_UInt16.m_Step;
        break;
    case TW_TYPE_INT32:
        min = (double)_Atom->m_Val.m_Int32.m_Min;
        max = (double)_Atom->m_Val.m_Int32.m_Max;
        step = (double)_Atom->m_Val.m_Int32.m_Step;
        break;
    case TW_TYPE_UINT32:
        min = (double)_Atom->m_Val.m_UInt32.m_Min;
        max = (double)_Atom->m_Val.m_UInt32.m_Max;
        step = (double)_Atom->m_Val.m_UInt32.m_Step;
        break;
    case TW_TYPE_FLOAT:
        min = (double)_Atom->m_Val.m_Float32.m_Min;
        max = (double)_Atom->m_Val.m_Float32.m_Max;
        step = (double)_Atom->m_Val.m_Float32.m_Step;
        break;
    case TW_TYPE_DOUBLE:
        min = _Atom->m_Val.m_Float64.m_Min;
        max = _Atom->m_Val.m_Float64.m_Max;
        step = _Atom->m_Val.m_Float64.m_Step;
        break;
    default:
        {}  // nothing
    }
    #pragma GCC diagnostic pop

    if( _Min!=NULL )
        *_Min = min;
    if( _Max!=NULL )
        *_Max = max;
    if( _Step!=NULL )
        *_Step = step;
}

//  ---------------------------------------------------------------------------

const CTwVar *CTwVarAtom_Find(const CTwVarAtom *_Atom, const char *_Name, CTwVarGroup **_Parent, int *_Index)
{
    if( strcmp(_Name, _Atom->m_Base.m_Name)==0 )
    {
        if( _Parent!=NULL )
            *_Parent = NULL;
        if( _Index!=NULL )
            *_Index = -1;
        return &_Atom->m_Base;
    }
    else
        return NULL;
}

//  ---------------------------------------------------------------------------

enum EVarAttribs
{
    V_LABEL = 1,
    V_HELP,
    V_GROUP,
    V_SHOW,
    V_HIDE,
    V_READONLY,
    V_READWRITE,
    V_ORDER,
    V_VISIBLE,
    V_ENDTAG
};

int CTwVar_HasAttribBase(const char *_Attrib, bool *_HasValue)
{
    *_HasValue = true;
    if( _stricmp(_Attrib, "label")==0 )
        return V_LABEL;
    else if( _stricmp(_Attrib, "help")==0 )
        return V_HELP;
    else if( _stricmp(_Attrib, "group")==0 )
        return V_GROUP;
    else if( _stricmp(_Attrib, "order")==0 )
        return V_ORDER;
    else if( _stricmp(_Attrib, "visible")==0 )
        return V_VISIBLE;
    else if( _stricmp(_Attrib, "readonly")==0 )
        return V_READONLY;

    // for backward compatibility
    *_HasValue = false;
    if( _stricmp(_Attrib, "show")==0 )
        return V_SHOW;
    else if( _stricmp(_Attrib, "hide")==0 )
        return V_HIDE;
    if( _stricmp(_Attrib, "readonly")==0 )
        return V_READONLY;
    else if( _stricmp(_Attrib, "readwrite")==0 )
        return V_READWRITE;

    return 0; // not found
}

int CTwVar_SetAttribBase(CTwVar *_Var, int _AttribID, const char *_Value, TwBar *_Bar, struct CTwVarGroup *_VarParent, int _VarIndex)
{
    switch( _AttribID )
    {
    case V_LABEL:
    case V_HELP:
        if( _Value && strlen(_Value)>0 )
        {
            {
                CTwVarGroup *Parent = NULL;
                CTwVar *ThisVar = (CTwVar *)CTwBar_Find(_Bar, _Var->m_Name, &Parent, NULL);
                if( _Var==ThisVar && Parent!=NULL && Parent->m_StructValuePtr!=NULL )
                {
                    int Idx = Parent->m_StructType-TW_TYPE_STRUCT_BASE;
                    if( Idx>=0 && Idx<(int)g_TwMgr->m_Structs.count )
                    {
                        size_t nl = sdslen(_Var->m_Name);
                        for( size_t im=0; im<g_TwMgr->m_Structs.items[Idx].m_Members.count; ++im )
                        {
                            size_t ml = sdslen(g_TwMgr->m_Structs.items[Idx].m_Members.items[im].m_Name);
                            if( nl>=ml && strcmp(g_TwMgr->m_Structs.items[Idx].m_Members.items[im].m_Name, _Var->m_Name+(nl-ml))==0 )
                            {
                                // TODO: would have to be applied to other vars already created
                                if( _AttribID==V_LABEL )
                                {
                                    CStructMember *m = &g_TwMgr->m_Structs.items[Idx].m_Members.items[im];
                                    m->m_Label = sdscpy(m->m_Label, _Value);
//                                    _Var->m_Label = _Value;
                                }
                                else // V_HELP
                                {
                                    CStructMember *m = &g_TwMgr->m_Structs.items[Idx].m_Members.items[im];
                                    m->m_Help = sdscpy(m->m_Help, _Value);
                                }
                                break;
                            }
                        }
                    }
                }
                else
                {
                    if( _AttribID==V_LABEL )
                        _Var->m_Label = sdscpy(_Var->m_Label, _Value);
                    else // V_HELP
                        _Var->m_Help = sdscpy(_Var->m_Help, _Value);
                }
            }
            CTwBar_NotUpToDate(_Bar);
            return 1;
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNoValue);
            return 0;
        }
    case V_GROUP:
        {
            CTwVarGroup *Grp = NULL;
            if( _Value==NULL || strlen(_Value)<=0 )
                Grp = &(_Bar->m_VarRoot);
            else
            {
                CTwVar *v = (CTwVar *)CTwBar_Find(_Bar, _Value, NULL, NULL);
                if( v && !CTwVar_IsGroup(v) )
                {
                    CTwMgr_SetLastError(g_TwMgr, g_ErrNotGroup);
                    return 0;
                }
                Grp = (CTwVarGroup *)v;
                if( Grp==NULL )
                {
                    Grp = CTwVarGroup_New();
                    Grp->m_Base.m_Name = sdscpy(Grp->m_Base.m_Name, _Value);
                    Grp->m_Open = true;
                    Grp->m_SummaryCallback = NULL;
                    Grp->m_SummaryClientData = NULL;
                    Grp->m_StructValuePtr = NULL;
                    Grp->m_Base.m_ColorPtr = &(_Bar->m_ColGrpText);
                    tw_da_append(&_Bar->m_VarRoot.m_Vars, &Grp->m_Base);
                }
            }
            tw_da_append(&Grp->m_Vars, _Var);
            if( _VarParent!=NULL && _VarIndex>=0 )
            {
                tw_da_remove_ordered(&_VarParent->m_Vars, (size_t)_VarIndex);
                if( _VarParent!=&(_Bar->m_VarRoot) && _VarParent->m_Vars.count<=0 )
                    TwRemoveVar(_Bar, _VarParent->m_Base.m_Name);
            }
            CTwBar_NotUpToDate(_Bar);
            return 1;
        }
    case V_SHOW: // for backward compatibility
        if( !_Var->m_Visible )
        {
            _Var->m_Visible = true;
            CTwBar_NotUpToDate(_Bar);
        }
        return 1;
    case V_HIDE: // for backward compatibility
        if( _Var->m_Visible )
        {
            _Var->m_Visible = false;
            CTwBar_NotUpToDate(_Bar);
        }
        return 1;
    /*
    case V_READONLY:
        CTwVar_SetReadOnly(_Var, true);
        CTwBar_NotUpToDate(_Bar);
        return 1;
    */
    case V_READWRITE: // for backward compatibility
        CTwVar_SetReadOnly(_Var, false);
        CTwBar_NotUpToDate(_Bar);
        return 1;
    case V_ORDER:
        // a special case for compatibility with deprecated command 'option=ogl/dx'
        if( CTwVar_IsGroup(_Var) && _Value!=NULL && ((CTwVarGroup *)_Var)->m_SummaryCallback==CColorExt_SummaryCB && ((CTwVarGroup *)_Var)->m_StructValuePtr!=NULL ) // is tw_type_color?
        {
            if( _stricmp(_Value, "ogl")==0 )
            {
                ((CColorExt *)(((CTwVarGroup *)_Var)->m_StructValuePtr))->m_OGL = true;
                return 1;
            }
            else if( _stricmp(_Value, "dx")==0 )
            {
                ((CColorExt *)(((CTwVarGroup *)_Var)->m_StructValuePtr))->m_OGL = false;
                return 1;
            }
        }
        // todo: general 'order' command (no else)
        return 0;
    case V_VISIBLE:
        if( _Value!=NULL && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "true")==0 || _stricmp(_Value, "1")==0 )
            {
                if( !_Var->m_Visible )
                {
                    _Var->m_Visible = true;
                    CTwBar_NotUpToDate(_Bar);
                }
                return 1;
            }
            else if( _stricmp(_Value, "false")==0 || _stricmp(_Value, "0")==0 )
            {
                if( _Var->m_Visible )
                {
                    _Var->m_Visible = false;
                    CTwBar_NotUpToDate(_Bar);
                }
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
    case V_READONLY:
        if( _Value==NULL || strlen(_Value)==0 // no value is acceptable (for backward compatibility)
            || _stricmp(_Value, "true")==0 || _stricmp(_Value, "1")==0 )
        {
            if( !CTwVar_IsReadOnly(_Var) )
            {
                CTwVar_SetReadOnly(_Var, true);
                CTwBar_NotUpToDate(_Bar);
            }
            return 1;
        }
        else if( _stricmp(_Value, "false")==0 || _stricmp(_Value, "0")==0 )
        {
            if( CTwVar_IsReadOnly(_Var) )
            {
                CTwVar_SetReadOnly(_Var, false);
                CTwBar_NotUpToDate(_Bar);
            }
            return 1;
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrBadValue);
            return 0;
        }
    default:
        CTwMgr_SetLastError(g_TwMgr, g_ErrUnknownAttrib);
        return 0;
    }
}


ERetType CTwVar_GetAttribBase(const CTwVar *_Var, int _AttribID, TwBar *_Bar, CTwVarGroup *_VarParent, int _VarIndex, CDoubleArray *outDoubles, sds *outString)
{
    (void)_Bar, (void)_VarIndex;
    outDoubles->count = 0;
    sdsclear(*outString);

    switch( _AttribID )
    {
    case V_LABEL:
        *outString = sdscat(*outString, _Var->m_Label);
        return RET_STRING;
    case V_HELP:
        *outString = sdscat(*outString, _Var->m_Help);
        return RET_STRING;
    case V_GROUP:
        if( _VarParent!=NULL )
            *outString = sdscat(*outString, _VarParent->m_Base.m_Name);
        return RET_STRING;
    case V_VISIBLE:
        tw_da_append(outDoubles, _Var->m_Visible ? 1 : 0);
        return RET_DOUBLE;
    case V_READONLY:
        tw_da_append(outDoubles, CTwVar_IsReadOnly(_Var) ? 1 : 0);
        return RET_DOUBLE;
    default:
        CTwMgr_SetLastError(g_TwMgr, g_ErrUnknownAttrib);
        return RET_ERROR;
    }
}


//  ---------------------------------------------------------------------------

enum EVarAtomAttribs
{
    VA_KEY_INCR = V_ENDTAG+1,
    VA_KEY_DECR,
    VA_MIN,
    VA_MAX,
    VA_STEP,
    VA_PRECISION,
    VA_HEXA,
    VA_DECIMAL, // for backward compatibility
    VA_TRUE,
    VA_FALSE,
    VA_ENUM,
    VA_VALUE
};

int CTwVarAtom_HasAttrib(const CTwVarAtom *_Atom, const char *_Attrib, bool *_HasValue)
{
    (void)_Atom;
    *_HasValue = true;
    if( _stricmp(_Attrib, "keyincr")==0 || _stricmp(_Attrib, "key")==0 )
        return VA_KEY_INCR;
    else if( _stricmp(_Attrib, "keydecr")==0 )
        return VA_KEY_DECR;
    else if( _stricmp(_Attrib, "min")==0 )
        return VA_MIN;
    else if( _stricmp(_Attrib, "max")==0 )
        return VA_MAX;
    else if( _stricmp(_Attrib, "step")==0 )
        return VA_STEP;
    else if( _stricmp(_Attrib, "precision")==0 )
        return VA_PRECISION;
    else if( _stricmp(_Attrib, "hexa")==0 )
        return VA_HEXA;
    else if( _stricmp(_Attrib, "decimal")==0 ) // for backward compatibility
    {
        *_HasValue = false;
        return VA_DECIMAL;
    }
    else if( _stricmp(_Attrib, "true")==0 )
        return VA_TRUE;
    else if( _stricmp(_Attrib, "false")==0 )
        return VA_FALSE;
    else if( _stricmp(_Attrib, "enum")==0 
             || _stricmp(_Attrib, "val")==0 ) // for backward compatibility
        return VA_ENUM;
    else if( _stricmp(_Attrib, "value")==0 )
        return VA_VALUE;

    return CTwVar_HasAttribBase(_Attrib, _HasValue);
}

int CTwVarAtom_SetAttrib(CTwVarAtom *_Atom, int _AttribID, const char *_Value, TwBar *_Bar, struct CTwVarGroup *_VarParent, int _VarIndex)
{
    switch( _AttribID )
    {
    case VA_KEY_INCR:
        {
            int Key = 0;
            int Mod = 0;
            if( TwGetKeyCode(&Key, &Mod, _Value) )
            {
                _Atom->m_KeyIncr[0] = Key;
                _Atom->m_KeyIncr[1] = Mod;
                return 1;
            }
            else
                return 0;
        }
    case VA_KEY_DECR:
        {
            int Key = 0;
            int Mod = 0;
            if( TwGetKeyCode(&Key, &Mod, _Value) )
            {
                _Atom->m_KeyDecr[0] = Key;
                _Atom->m_KeyDecr[1] = Mod;
                return 1;
            }
            else
                return 0;
        }
    case VA_TRUE:
        if( (_Atom->m_Type==TW_TYPE_BOOL8 || _Atom->m_Type==TW_TYPE_BOOL16 || _Atom->m_Type==TW_TYPE_BOOL32 || _Atom->m_Type==TW_TYPE_BOOLCPP) && _Value!=NULL )
        {
            if( _Atom->m_Val.m_Bool.m_FreeTrueString && _Atom->m_Val.m_Bool.m_TrueString!=NULL )
                free(_Atom->m_Val.m_Bool.m_TrueString);
            _Atom->m_Val.m_Bool.m_TrueString = _strdup(_Value);
            _Atom->m_Val.m_Bool.m_FreeTrueString = true;
            return 1;
        }
        else
            return 0;
    case VA_FALSE:
        if( (_Atom->m_Type==TW_TYPE_BOOL8 || _Atom->m_Type==TW_TYPE_BOOL16 || _Atom->m_Type==TW_TYPE_BOOL32 || _Atom->m_Type==TW_TYPE_BOOLCPP) && _Value!=NULL )
        {
            if( _Atom->m_Val.m_Bool.m_FreeFalseString && _Atom->m_Val.m_Bool.m_FalseString!=NULL )
                free(_Atom->m_Val.m_Bool.m_FalseString);
            _Atom->m_Val.m_Bool.m_FalseString = _strdup(_Value);
            _Atom->m_Val.m_Bool.m_FreeFalseString = true;
            return 1;
        }
        else
            return 0;
    case VA_MIN:
    case VA_MAX:
    case VA_STEP:
        if( _Value && strlen(_Value)>0 )
        {
            void *Ptr = NULL;
            const char *Fmt = NULL;
            int d = 0;
            unsigned int u = 0;
            int Num = (_AttribID==VA_STEP) ? 2 : ((_AttribID==VA_MAX) ? 1 : 0);
            switch( _Atom->m_Type )
            {
            case TW_TYPE_CHAR:
                //Ptr = (&_Atom->m_Val.m_Char.m_Min) + Num;
                //Fmt = "%c";
                Ptr = &u;
                Fmt = "%u";
                break;
            case TW_TYPE_INT16:
                Ptr = (&_Atom->m_Val.m_Int16.m_Min) + Num;
                Fmt = "%hd";
                break;
            case TW_TYPE_INT32:
                Ptr = (&_Atom->m_Val.m_Int32.m_Min) + Num;
                Fmt = "%d";
                break;
            case TW_TYPE_UINT16:
                Ptr = (&_Atom->m_Val.m_UInt16.m_Min) + Num;
                Fmt = "%hu";
                break;
            case TW_TYPE_UINT32:
                Ptr = (&_Atom->m_Val.m_UInt32.m_Min) + Num;
                Fmt = "%u";
                break;
            case TW_TYPE_FLOAT:
                Ptr = (&_Atom->m_Val.m_Float32.m_Min) + Num;
                Fmt = "%f";
                break;
            case TW_TYPE_DOUBLE:
                Ptr = (&_Atom->m_Val.m_Float64.m_Min) + Num;
                Fmt = "%lf";
                break;
            case TW_TYPE_INT8:
                Ptr = &d;
                Fmt = "%d";
                break;
            case TW_TYPE_UINT8:
                Ptr = &u;
                Fmt = "%u";
                break;
            default:
                CTwMgr_SetLastError(g_TwMgr, g_ErrUnknownType);
                return 0;
            }

            if( Fmt!=NULL && Ptr!=NULL && sscanf(_Value, Fmt, Ptr)==1 )
            {
                if( _Atom->m_Type==TW_TYPE_CHAR )
                    *((&_Atom->m_Val.m_Char.m_Min)+Num) = (unsigned char)(u);
                else if( _Atom->m_Type==TW_TYPE_INT8 )
                    *((&_Atom->m_Val.m_Int8.m_Min)+Num) = (signed char)(d);
                else if( _Atom->m_Type==TW_TYPE_UINT8 )
                    *((&_Atom->m_Val.m_UInt8.m_Min)+Num) = (unsigned char)(u);

                // set precision
                if( _AttribID==VA_STEP && ((_Atom->m_Type==TW_TYPE_FLOAT && _Atom->m_Val.m_Float32.m_Precision<0) || (_Atom->m_Type==TW_TYPE_DOUBLE && _Atom->m_Val.m_Float64.m_Precision<0)) )
                {
                    double Step = fabs( (_Atom->m_Type==TW_TYPE_FLOAT) ? _Atom->m_Val.m_Float32.m_Step : _Atom->m_Val.m_Float64.m_Step );
                    signed char *Precision = (_Atom->m_Type==TW_TYPE_FLOAT) ? &_Atom->m_Val.m_Float32.m_Precision : &_Atom->m_Val.m_Float64.m_Precision;
                    const double K_EPS = 1.0 - 1.0e-6;
                    if( Step>=1 )
                        *Precision = 0;
                    else if( Step>=0.1*K_EPS )
                        *Precision = 1;
                    else if( Step>=0.01*K_EPS )
                        *Precision = 2;
                    else if( Step>=0.001*K_EPS )
                        *Precision = 3;
                    else if( Step>=0.0001*K_EPS )
                        *Precision = 4;
                    else if( Step>=0.00001*K_EPS )
                        *Precision = 5;
                    else if( Step>=0.000001*K_EPS )
                        *Precision = 6;
                    else if( Step>=0.0000001*K_EPS )
                        *Precision = 7;
                    else if( Step>=0.00000001*K_EPS )
                        *Precision = 8;
                    else if( Step>=0.000000001*K_EPS )
                        *Precision = 9;
                    else if( Step>=0.0000000001*K_EPS )
                        *Precision = 10;
                    else if( Step>=0.00000000001*K_EPS )
                        *Precision = 11;
                    else if( Step>=0.000000000001*K_EPS )
                        *Precision = 12;
                    else
                        *Precision = -1;
                }

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
    case VA_PRECISION:
        if( _Value && strlen(_Value)>0 )
        {
            int Precision = 0;
            if( sscanf(_Value, "%d", &Precision)==1 && Precision>=-1 && Precision<=12 )
            {
                if( _Atom->m_Type==TW_TYPE_FLOAT )
                    _Atom->m_Val.m_Float32.m_Precision = (signed char)Precision;
                else if ( _Atom->m_Type==TW_TYPE_DOUBLE )
                    _Atom->m_Val.m_Float64.m_Precision = (signed char)Precision;
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
    case VA_HEXA:
    case VA_DECIMAL:
        {
            bool hexa = false;
            if (_AttribID==VA_HEXA) 
            {
                if( _Value==NULL || strlen(_Value)==0 // no value is acceptable (for backward compatibility)
                    || _stricmp(_Value, "true")==0 || _stricmp(_Value, "1")==0 )
                    hexa = true;
            }

            switch( _Atom->m_Type )
            {
            case TW_TYPE_CHAR:
                _Atom->m_Val.m_Char.m_Hexa = hexa;
                return 1;
            case TW_TYPE_INT8:
                _Atom->m_Val.m_Int8.m_Hexa = hexa;
                return 1;
            case TW_TYPE_INT16:
                _Atom->m_Val.m_Int16.m_Hexa = hexa;
                return 1;
            case TW_TYPE_INT32:
                _Atom->m_Val.m_Int32.m_Hexa = hexa;
                return 1;
            case TW_TYPE_UINT8:
                _Atom->m_Val.m_UInt8.m_Hexa = hexa;
                return 1;
            case TW_TYPE_UINT16:
                _Atom->m_Val.m_UInt16.m_Hexa = hexa;
                return 1;
            case TW_TYPE_UINT32:
                _Atom->m_Val.m_UInt32.m_Hexa = hexa;
                return 1;
            default:
                return 0;
            }
        }
    case VA_ENUM:
        if( _Value && strlen(_Value)>0 && IsEnumType(_Atom->m_Type) )
        {
            const char *s = _Value;
            int n = 0, i = 0;
            unsigned int u;
            bool Cont;
            CEnum_Clear(&g_TwMgr->m_Enums.items[_Atom->m_Type-TW_TYPE_ENUM_BASE]); // anyway reset entries
            do
            {
                Cont = false;
                i = 0;
                char Sep;
                n = sscanf(s, "%u %c%n", &u, &Sep, &i);
                if( n==2 && i>0 && ( Sep=='<' || Sep=='{' || Sep=='[' || Sep=='(' ) )
                {
                    if( Sep=='<' )  // Change to closing separator
                        Sep = '>';
                    else if( Sep=='{' )
                        Sep = '}';
                    else if( Sep=='[' )
                        Sep = ']';
                    else if( Sep=='(' )
                        Sep = ')';
                    s += i;
                    i = 0;
                    while( s[i]!=Sep && s[i]!=0 )
                        ++i;
                    if( s[i]==Sep )
                    {
                        //if( _Atom->m_Val.m_Enum.m_Entries==NULL )
                        //  _Atom->m_Val.m_Enum.m_Entries = new UVal::CEnumVal::CEntries;
                        //UVal::CEnumVal::CEntries::value_type v(u, "");
                        // CEnum_InsertOrReplaceLen already overwrites the
                        // label if u is already present (was: insert, then
                        // erase+reinsert on collision).
                        if( i>0 )
                            CEnum_InsertOrReplaceLen(&g_TwMgr->m_Enums.items[_Atom->m_Type-TW_TYPE_ENUM_BASE], u, s, (size_t)i);
                        else
                            CEnum_InsertOrReplace(&g_TwMgr->m_Enums.items[_Atom->m_Type-TW_TYPE_ENUM_BASE], u, "");

                        s += i+1;
                        i = 0;
                        n = sscanf(s, " ,%n", &i);
                        if( n==0 && i>=1 )
                        {
                            s += i;
                            Cont = true;
                        }
                    }
                    else
                    {
                        CTwMgr_SetLastError(g_TwMgr, g_ErrBadValue);
                        return 0;
                    }
                }
                else
                {
                    CTwMgr_SetLastError(g_TwMgr, g_ErrBadValue);
                    return 0;
                }
            } while( Cont );
            return 1;
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNoValue);
            return 0;
        }
        break;
    case VA_VALUE:
        if( _Value!=NULL && strlen(_Value)>0 ) // do not check ReadOnly here.
        {
            if( !( _Atom->m_Type==TW_TYPE_BUTTON || IsCustomType(_Atom->m_Type) ) ) // || (_Atom->m_Type>=TW_TYPE_CUSTOM_BASE && _Atom->m_Type<TW_TYPE_CUSTOM_BASE+(int)g_TwMgr->m_Customs.size()) ) )
            {
                if( _Atom->m_Type==TW_TYPE_CDSTRING )
                {
                    if( _Atom->m_SetCallback!=NULL )
                    {
                        _Atom->m_SetCallback(&_Value, _Atom->m_ClientData);
                        if( g_TwMgr!=NULL ) // Mgr might have been destroyed by the client inside a callback call
                            CTwBar_NotUpToDate(_Bar);
                        return 1;
                    }
                    else
                    {
                        char **StringPtr = (char **)_Atom->m_Ptr;
                        if( StringPtr!=NULL && g_TwMgr->m_CopyCDStringToClient!=NULL )
                        {
                            g_TwMgr->m_CopyCDStringToClient(StringPtr, _Value);
                            CTwBar_NotUpToDate(_Bar);
                            return 1;
                        }
                    }
                }
                else if( IsCSStringType(_Atom->m_Type) )
                {
                    int n = TW_CSSTRING_SIZE(_Atom->m_Type);
                    if( n>0 )
                    {
                        sds str = sdsnew(_Value);
                        if( (int)sdslen(str)>n-1 )
                            sdsrange(str, 0, n-2);
                        if( _Atom->m_SetCallback!=NULL )
                        {
                            _Atom->m_SetCallback(str, _Atom->m_ClientData);
                            sdsfree(str);
                            if( g_TwMgr!=NULL ) // Mgr might have been destroyed by the client inside a callback call
                                CTwBar_NotUpToDate(_Bar);
                            return 1;
                        }
                        else if( _Atom->m_Ptr!=NULL )
                        {
                            if( n>1 )
                                strncpy((char *)_Atom->m_Ptr, str, n-1);
                            ((char *)_Atom->m_Ptr)[n-1] = '\0';
                            sdsfree(str);
                            CTwBar_NotUpToDate(_Bar);
                            return 1;
                        }
                        else
                            sdsfree(str);
                    }
                }
                else
                {
                    double dbl;
                    if( sscanf(_Value, "%lf", &dbl)==1 )
                    {
                        CTwVarAtom_ValueFromDouble(_Atom, dbl);
                        if( g_TwMgr!=NULL ) // Mgr might have been destroyed by the client inside a callback call
                            CTwBar_NotUpToDate(_Bar);
                        return 1;
                    }
                }
            }
        }
        return 0;
    default:
        return CTwVar_SetAttribBase(&_Atom->m_Base, _AttribID, _Value, _Bar, _VarParent, _VarIndex);
    }
}

ERetType CTwVarAtom_GetAttrib(const CTwVarAtom *_Atom, int _AttribID, TwBar *_Bar, CTwVarGroup *_VarParent, int _VarIndex, CDoubleArray *outDoubles, sds *outString)
{
    outDoubles->count = 0;
    sdsclear(*outString);
    int num = 0;

    switch( _AttribID )
    {
    case VA_KEY_INCR:
        {
            sds str = sdsempty();
            if( TwGetKeyString(&str, _Atom->m_KeyIncr[0], _Atom->m_KeyIncr[1]) )
                *outString = sdscat(*outString, str);
            sdsfree(str);
        }
        return RET_STRING;
    case VA_KEY_DECR:
        {
            sds str = sdsempty();
            if( TwGetKeyString(&str, _Atom->m_KeyDecr[0], _Atom->m_KeyDecr[1]) )
                *outString = sdscat(*outString, str);
            sdsfree(str);
        }
        return RET_STRING;
    case VA_TRUE:
        if( _Atom->m_Type==TW_TYPE_BOOL8 || _Atom->m_Type==TW_TYPE_BOOL16 || _Atom->m_Type==TW_TYPE_BOOL32 || _Atom->m_Type==TW_TYPE_BOOLCPP )
        {
            *outString = sdscat(*outString, _Atom->m_Val.m_Bool.m_TrueString);
            return RET_STRING;
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrInvalidAttrib);
            return RET_ERROR;
        }
    case VA_FALSE:
        if( _Atom->m_Type==TW_TYPE_BOOL8 || _Atom->m_Type==TW_TYPE_BOOL16 || _Atom->m_Type==TW_TYPE_BOOL32 || _Atom->m_Type==TW_TYPE_BOOLCPP )
        {
            *outString = sdscat(*outString, _Atom->m_Val.m_Bool.m_FalseString);
            return RET_STRING;
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrInvalidAttrib);
            return RET_ERROR;
        }
    case VA_MIN:
    case VA_MAX:
    case VA_STEP:
        num = (_AttribID==VA_STEP) ? 2 : ((_AttribID==VA_MAX) ? 1 : 0);
        switch( _Atom->m_Type )
        {
        case TW_TYPE_CHAR:
            tw_da_append(outDoubles,  *((&_Atom->m_Val.m_Char.m_Min) + num) );
            return RET_DOUBLE;
        case TW_TYPE_INT8:
            tw_da_append(outDoubles,  *((&_Atom->m_Val.m_Int8.m_Min) + num) );
            return RET_DOUBLE;
        case TW_TYPE_UINT8:
            tw_da_append(outDoubles,  *((&_Atom->m_Val.m_UInt8.m_Min) + num) );
            return RET_DOUBLE;
        case TW_TYPE_INT16:
            tw_da_append(outDoubles,  *((&_Atom->m_Val.m_Int16.m_Min) + num) );
            return RET_DOUBLE;
        case TW_TYPE_INT32:
            tw_da_append(outDoubles,  *((&_Atom->m_Val.m_Int32.m_Min) + num) );
            return RET_DOUBLE;
        case TW_TYPE_UINT16:
            tw_da_append(outDoubles,  *((&_Atom->m_Val.m_UInt16.m_Min) + num) );
            return RET_DOUBLE;
        case TW_TYPE_UINT32:
            tw_da_append(outDoubles,  *((&_Atom->m_Val.m_UInt32.m_Min) + num) );
            return RET_DOUBLE;
        case TW_TYPE_FLOAT:
            tw_da_append(outDoubles,  *((&_Atom->m_Val.m_Float32.m_Min) + num) );
            return RET_DOUBLE;
        case TW_TYPE_DOUBLE:
            tw_da_append(outDoubles,  *((&_Atom->m_Val.m_Float64.m_Min) + num) );
            return RET_DOUBLE;
        default:
            CTwMgr_SetLastError(g_TwMgr, g_ErrInvalidAttrib);
            return RET_ERROR;
        }
    case VA_PRECISION:
        if( _Atom->m_Type==TW_TYPE_FLOAT )
        {
            tw_da_append(outDoubles,  _Atom->m_Val.m_Float32.m_Precision );
            return RET_DOUBLE;
        }
        else if ( _Atom->m_Type==TW_TYPE_DOUBLE )
        {
            tw_da_append(outDoubles,  _Atom->m_Val.m_Float64.m_Precision );
            return RET_DOUBLE;
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrInvalidAttrib);
            return RET_ERROR;
        }
    case VA_HEXA:
        switch( _Atom->m_Type )
        {
        case TW_TYPE_CHAR:
            tw_da_append(outDoubles,  _Atom->m_Val.m_Char.m_Hexa );
            return RET_DOUBLE;
        case TW_TYPE_INT8:
            tw_da_append(outDoubles,  _Atom->m_Val.m_Int8.m_Hexa );
            return RET_DOUBLE;
        case TW_TYPE_INT16:
            tw_da_append(outDoubles,  _Atom->m_Val.m_Int16.m_Hexa );
            return RET_DOUBLE;
        case TW_TYPE_INT32:
            tw_da_append(outDoubles,  _Atom->m_Val.m_Int32.m_Hexa );
            return RET_DOUBLE;
        case TW_TYPE_UINT8:
            tw_da_append(outDoubles,  _Atom->m_Val.m_UInt8.m_Hexa );
            return RET_DOUBLE;
        case TW_TYPE_UINT16:
            tw_da_append(outDoubles,  _Atom->m_Val.m_UInt16.m_Hexa );
            return RET_DOUBLE;
        case TW_TYPE_UINT32:
            tw_da_append(outDoubles,  _Atom->m_Val.m_UInt32.m_Hexa );
            return RET_DOUBLE;
        default:
            CTwMgr_SetLastError(g_TwMgr, g_ErrInvalidAttrib);
            return RET_ERROR;
        }
    case VA_ENUM:
        if( IsEnumType(_Atom->m_Type) )
        {
            CEnum *e = &g_TwMgr->m_Enums.items[_Atom->m_Type-TW_TYPE_ENUM_BASE];
            for( size_t k=0; k<e->m_Entries.count; ++k )
            {
                unsigned int Value = e->m_Entries.items[k].Value;
                sds Label = e->m_Entries.items[k].Label;
                if( k>0 )
                    *outString = sdscat(*outString, ",");
                *outString = sdscatprintf(*outString, "%u ", Value);
                if( strpbrk(Label, "{}")==NULL )
                    *outString = sdscatprintf(*outString, "{%s}", Label);
                else if ( strpbrk(Label, "<>")==NULL )
                    *outString = sdscatprintf(*outString, "<%s>", Label);
                else if ( strpbrk(Label, "()")==NULL )
                    *outString = sdscatprintf(*outString, "(%s)", Label);
                else if ( strpbrk(Label, "[]")==NULL )
                    *outString = sdscatprintf(*outString, "[%s]", Label);
                else
                    *outString = sdscatprintf(*outString, "{%s}", Label); // should not occured (use braces)
            }
            return RET_STRING;
        }
        CTwMgr_SetLastError(g_TwMgr, g_ErrInvalidAttrib);
        return RET_ERROR;
    case VA_VALUE:
        if( !( _Atom->m_Type==TW_TYPE_BUTTON || IsCustomType(_Atom->m_Type) ) ) // || (_Atom->m_Type>=TW_TYPE_CUSTOM_BASE && _Atom->m_Type<TW_TYPE_CUSTOM_BASE+(int)g_TwMgr->m_Customs.size()) ) )
        {
            if( _Atom->m_Type==TW_TYPE_CDSTRING || IsCSStringType(_Atom->m_Type) )
            {
                sds str = sdsempty();
                CTwVarAtom_ValueToString(_Atom, &str);
                *outString = sdscat(*outString, str);
                sdsfree(str);
                return RET_STRING;
            }
            else
            {
                tw_da_append(outDoubles,  CTwVarAtom_ValueToDouble(_Atom) );
                return RET_DOUBLE;
            }
        }
        CTwMgr_SetLastError(g_TwMgr, g_ErrInvalidAttrib);
        return RET_ERROR;
    default:
        return CTwVar_GetAttribBase(&_Atom->m_Base, _AttribID, _Bar, _VarParent, _VarIndex, outDoubles, outString);
    }
}

//  ---------------------------------------------------------------------------

void CTwVarAtom_Increment(CTwVarAtom *_Atom, int _Step)
{
    if( _Step==0 )
        return;
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch" // TW_TYPE_BOOLCPP (see TwMgr.h) is a numeric macro here, not a real ETwType enumerator, in a real-C99 build
    switch( _Atom->m_Type )
    {
    case TW_TYPE_BOOL8:
        {
            char v = false;
            if( _Atom->m_Ptr!=NULL )
                v = *((char *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            if( v )
                v = false;
            else
                v = true;
            if( _Atom->m_Ptr!=NULL )
                *((char *)_Atom->m_Ptr) = v;
            else if( _Atom->m_SetCallback!=NULL )
                _Atom->m_SetCallback(&v, _Atom->m_ClientData);
        }
        break;
    case TW_TYPE_BOOL16:
        {
            short v = false;
            if( _Atom->m_Ptr!=NULL )
                v = *((short *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            if( v )
                v = false;
            else
                v = true;
            if( _Atom->m_Ptr!=NULL )
                *((short *)_Atom->m_Ptr) = v;
            else if( _Atom->m_SetCallback!=NULL )
                _Atom->m_SetCallback(&v, _Atom->m_ClientData);
        }
        break;
    case TW_TYPE_BOOL32:
        {
            int v = false;
            if( _Atom->m_Ptr!=NULL )
                v = *((int *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            if( v )
                v = false;
            else
                v = true;
            if( _Atom->m_Ptr!=NULL )
                *((int *)_Atom->m_Ptr) = v;
            else if( _Atom->m_SetCallback!=NULL )
                _Atom->m_SetCallback(&v, _Atom->m_ClientData);
        }
        break;
    case TW_TYPE_BOOLCPP:
        {
            bool v = false;
            if( _Atom->m_Ptr!=NULL )
                v = *((bool *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            if( v )
                v = false;
            else
                v = true;
            if( _Atom->m_Ptr!=NULL )
                *((bool *)_Atom->m_Ptr) = v;
            else if( _Atom->m_SetCallback!=NULL )
                _Atom->m_SetCallback(&v, _Atom->m_ClientData);
        }
        break;
    case TW_TYPE_CHAR:
        {
            unsigned char v = 0;
            if( _Atom->m_Ptr!=NULL )
                v = *((unsigned char *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            int iv = _Step*(int)_Atom->m_Val.m_Char.m_Step + (int)v;
            if( iv<_Atom->m_Val.m_Char.m_Min )
                iv = _Atom->m_Val.m_Char.m_Min;
            if( iv>_Atom->m_Val.m_Char.m_Max )
                iv = _Atom->m_Val.m_Char.m_Max;
            if( iv<0 )
                iv = 0;
            else if( iv>0xff )
                iv = 0xff;
            v = (unsigned char)iv;
            if( _Atom->m_Ptr!=NULL )
                *((unsigned char *)_Atom->m_Ptr) = v;
            else if( _Atom->m_SetCallback!=NULL )
                _Atom->m_SetCallback(&v, _Atom->m_ClientData);
        }
        break;
    case TW_TYPE_INT8:
        {
            signed char v = 0;
            if( _Atom->m_Ptr!=NULL )
                v = *((signed char *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            int iv = _Step*(int)_Atom->m_Val.m_Int8.m_Step + (int)v;
            if( iv<_Atom->m_Val.m_Int8.m_Min )
                iv = _Atom->m_Val.m_Int8.m_Min;
            if( iv>_Atom->m_Val.m_Int8.m_Max )
                iv = _Atom->m_Val.m_Int8.m_Max;
            v = (signed char)iv;
            if( _Atom->m_Ptr!=NULL )
                *((signed char *)_Atom->m_Ptr) = v;
            else if( _Atom->m_SetCallback!=NULL )
                _Atom->m_SetCallback(&v, _Atom->m_ClientData);
        }
        break;
    case TW_TYPE_UINT8:
        {
            unsigned char v = 0;
            if( _Atom->m_Ptr!=NULL )
                v = *((unsigned char *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            int iv = _Step*(int)_Atom->m_Val.m_UInt8.m_Step + (int)v;
            if( iv<_Atom->m_Val.m_UInt8.m_Min )
                iv = _Atom->m_Val.m_UInt8.m_Min;
            if( iv>_Atom->m_Val.m_UInt8.m_Max )
                iv = _Atom->m_Val.m_UInt8.m_Max;
            if( iv<0 )
                iv = 0;
            else if( iv>0xff )
                iv = 0xff;
            v = (unsigned char)iv;
            if( _Atom->m_Ptr!=NULL )
                *((unsigned char *)_Atom->m_Ptr) = v;
            else if( _Atom->m_SetCallback!=NULL )
                _Atom->m_SetCallback(&v, _Atom->m_ClientData);
        }
        break;
    case TW_TYPE_INT16:
        {
            short v = 0;
            if( _Atom->m_Ptr!=NULL )
                v = *((short *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            int iv = _Step*(int)_Atom->m_Val.m_Int16.m_Step + (int)v;
            if( iv<_Atom->m_Val.m_Int16.m_Min )
                iv = _Atom->m_Val.m_Int16.m_Min;
            if( iv>_Atom->m_Val.m_Int16.m_Max )
                iv = _Atom->m_Val.m_Int16.m_Max;
            v = (short)iv;
            if( _Atom->m_Ptr!=NULL )
                *((short *)_Atom->m_Ptr) = v;
            else if( _Atom->m_SetCallback!=NULL )
                _Atom->m_SetCallback(&v, _Atom->m_ClientData);
        }
        break;
    case TW_TYPE_UINT16:
        {
            unsigned short v = 0;
            if( _Atom->m_Ptr!=NULL )
                v = *((unsigned short *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            int iv = _Step*(int)_Atom->m_Val.m_UInt16.m_Step + (int)v;
            if( iv<_Atom->m_Val.m_UInt16.m_Min )
                iv = _Atom->m_Val.m_UInt16.m_Min;
            if( iv>_Atom->m_Val.m_UInt16.m_Max )
                iv = _Atom->m_Val.m_UInt16.m_Max;
            if( iv<0 )
                iv = 0;
            else if( iv>0xffff )
                iv = 0xffff;
            v = (unsigned short)iv;
            if( _Atom->m_Ptr!=NULL )
                *((unsigned short *)_Atom->m_Ptr) = v;
            else if( _Atom->m_SetCallback!=NULL )
                _Atom->m_SetCallback(&v, _Atom->m_ClientData);
        }
        break;
    case TW_TYPE_INT32:
        {
            int v = 0;
            if( _Atom->m_Ptr!=NULL )
                v = *((int *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            double dv = (double)_Step*(double)_Atom->m_Val.m_Int32.m_Step + (double)v;
            if( dv>(double)0x7fffffff )
                v = 0x7fffffff;
            else if( dv<(double)(-0x7fffffff-1) )
                v = -0x7fffffff-1;
            else
                v = _Step*_Atom->m_Val.m_Int32.m_Step + v;
            if( v<_Atom->m_Val.m_Int32.m_Min )
                v = _Atom->m_Val.m_Int32.m_Min;
            if( v>_Atom->m_Val.m_Int32.m_Max )
                v = _Atom->m_Val.m_Int32.m_Max;
            if( _Atom->m_Ptr!=NULL )
                *((int *)_Atom->m_Ptr) = v;
            else if( _Atom->m_SetCallback!=NULL )
                _Atom->m_SetCallback(&v, _Atom->m_ClientData);
        }
        break;
    case TW_TYPE_UINT32:
        {
            unsigned int v = 0;
            if( _Atom->m_Ptr!=NULL )
                v = *((unsigned int *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            double dv = (double)_Step*(double)_Atom->m_Val.m_UInt32.m_Step + (double)v;
            if( dv>(double)0xffffffff )
                v = 0xffffffff;
            else if( dv<0 )
                v = 0;
            else
                v = _Step*_Atom->m_Val.m_UInt32.m_Step + v;
            if( v<_Atom->m_Val.m_UInt32.m_Min )
                v = _Atom->m_Val.m_UInt32.m_Min;
            if( v>_Atom->m_Val.m_UInt32.m_Max )
                v = _Atom->m_Val.m_UInt32.m_Max;
            if( _Atom->m_Ptr!=NULL )
                *((unsigned int *)_Atom->m_Ptr) = v;
            else if( _Atom->m_SetCallback!=NULL )
                _Atom->m_SetCallback(&v, _Atom->m_ClientData);
        }
        break;
    case TW_TYPE_FLOAT:
        {
            float v = 0;
            if( _Atom->m_Ptr!=NULL )
                v = *((float *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            v += _Step*_Atom->m_Val.m_Float32.m_Step;
            if( v<_Atom->m_Val.m_Float32.m_Min )
                v = _Atom->m_Val.m_Float32.m_Min;
            if( v>_Atom->m_Val.m_Float32.m_Max )
                v = _Atom->m_Val.m_Float32.m_Max;
            if( _Atom->m_Ptr!=NULL )
                *((float *)_Atom->m_Ptr) = v;
            else if( _Atom->m_SetCallback!=NULL )
                _Atom->m_SetCallback(&v, _Atom->m_ClientData);
        }
        break;
    case TW_TYPE_DOUBLE:
        {
            double v = 0;
            if( _Atom->m_Ptr!=NULL )
                v = *((double *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            v += _Step*_Atom->m_Val.m_Float64.m_Step;
            if( v<_Atom->m_Val.m_Float64.m_Min )
                v = _Atom->m_Val.m_Float64.m_Min;
            if( v>_Atom->m_Val.m_Float64.m_Max )
                v = _Atom->m_Val.m_Float64.m_Max;
            if( _Atom->m_Ptr!=NULL )
                *((double *)_Atom->m_Ptr) = v;
            else if( _Atom->m_SetCallback!=NULL )
                _Atom->m_SetCallback(&v, _Atom->m_ClientData);
        }
        break;
    /*
    case TW_TYPE_ENUM8:
        {
            assert(_Step==1 || _Step==-1);
            unsigned char v = 0;
            if( _Atom->m_Ptr!=NULL )
                v = *((unsigned char *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            if( _Atom->m_Val.m_Enum.m_Entries!=NULL )
            {
                UVal::CEnumVal::CEntries::iterator It = _Atom->m_Val.m_Enum.m_Entries->find(v);
                if( It==_Atom->m_Val.m_Enum.m_Entries->end() )
                    It = _Atom->m_Val.m_Enum.m_Entries->begin();
                else if( _Step==1 )
                {
                    ++It;
                    if( It==_Atom->m_Val.m_Enum.m_Entries->end() )
                        It = _Atom->m_Val.m_Enum.m_Entries->begin();
                }
                else if( _Step==-1 )
                {
                    if( It==_Atom->m_Val.m_Enum.m_Entries->begin() )
                        It = _Atom->m_Val.m_Enum.m_Entries->end();
                    if( It!=_Atom->m_Val.m_Enum.m_Entries->begin() )
                        --It;
                }
                if( It != _Atom->m_Val.m_Enum.m_Entries->end() )
                {
                    v = (unsigned char)(It->first);
                    if( _Atom->m_Ptr!=NULL )
                        *((unsigned char *)_Atom->m_Ptr) = v;
                    else if( _Atom->m_SetCallback!=NULL )
                        _Atom->m_SetCallback(&v, _Atom->m_ClientData);
                }
            }
        }
        break;
    case TW_TYPE_ENUM16:
        {
            assert(_Step==1 || _Step==-1);
            unsigned short v = 0;
            if( _Atom->m_Ptr!=NULL )
                v = *((unsigned short *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            if( _Atom->m_Val.m_Enum.m_Entries!=NULL )
            {
                UVal::CEnumVal::CEntries::iterator It = _Atom->m_Val.m_Enum.m_Entries->find(v);
                if( It==_Atom->m_Val.m_Enum.m_Entries->end() )
                    It = _Atom->m_Val.m_Enum.m_Entries->begin();
                else if( _Step==1 )
                {
                    ++It;
                    if( It==_Atom->m_Val.m_Enum.m_Entries->end() )
                        It = _Atom->m_Val.m_Enum.m_Entries->begin();
                }
                else if( _Step==-1 )
                {
                    if( It==_Atom->m_Val.m_Enum.m_Entries->begin() )
                        It = _Atom->m_Val.m_Enum.m_Entries->end();
                    if( It!=_Atom->m_Val.m_Enum.m_Entries->begin() )
                        --It;
                }
                if( It != _Atom->m_Val.m_Enum.m_Entries->end() )
                {
                    v = (unsigned short)(It->first);
                    if( _Atom->m_Ptr!=NULL )
                        *((unsigned short *)_Atom->m_Ptr) = v;
                    else if( _Atom->m_SetCallback!=NULL )
                        _Atom->m_SetCallback(&v, _Atom->m_ClientData);
                }
            }
        }
        break;
    case TW_TYPE_ENUM32:
        {
            assert(_Step==1 || _Step==-1);
            unsigned int v = 0;
            if( _Atom->m_Ptr!=NULL )
                v = *((unsigned int *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            if( _Atom->m_Val.m_Enum.m_Entries!=NULL )
            {
                UVal::CEnumVal::CEntries::iterator It = _Atom->m_Val.m_Enum.m_Entries->find(v);
                if( It==_Atom->m_Val.m_Enum.m_Entries->end() )
                    It = _Atom->m_Val.m_Enum.m_Entries->begin();
                else if( _Step==1 )
                {
                    ++It;
                    if( It==_Atom->m_Val.m_Enum.m_Entries->end() )
                        It = _Atom->m_Val.m_Enum.m_Entries->begin();
                }
                else if( _Step==-1 )
                {
                    if( It==_Atom->m_Val.m_Enum.m_Entries->begin() )
                        It = _Atom->m_Val.m_Enum.m_Entries->end();
                    if( It!=_Atom->m_Val.m_Enum.m_Entries->begin() )
                        --It;
                }
                if( It!=_Atom->m_Val.m_Enum.m_Entries->end() )
                {
                    v = (unsigned int)(It->first);
                    if( _Atom->m_Ptr!=NULL )
                        *((unsigned int *)_Atom->m_Ptr) = v;
                    else if( _Atom->m_SetCallback!=NULL )
                        _Atom->m_SetCallback(&v, _Atom->m_ClientData);
                }
            }
        }
        break;
    */
    default:
        if( _Atom->m_Type==TW_TYPE_BUTTON )
        {
            if( _Atom->m_Val.m_Button.m_Callback!=NULL )
            {
                _Atom->m_Val.m_Button.m_Callback(_Atom->m_ClientData);
                if( g_TwMgr==NULL ) // Mgr might have been destroyed by the client inside a callback call
                    return;
            }
        }
        else if( IsEnumType(_Atom->m_Type) )
        {
            assert(_Step==1 || _Step==-1);
            unsigned int v = 0;
            if( _Atom->m_Ptr!=NULL )
                v = *((unsigned int *)_Atom->m_Ptr);
            else if( _Atom->m_GetCallback!=NULL )
                _Atom->m_GetCallback(&v, _Atom->m_ClientData);
            CEnum *e = &g_TwMgr->m_Enums.items[_Atom->m_Type-TW_TYPE_ENUM_BASE];
            size_t n = e->m_Entries.count;
            size_t idx = n; // sentinel meaning "not found" (was It==end())
            for( size_t k=0; k<n; ++k )
                if( e->m_Entries.items[k].Value==v )
                {
                    idx = k;
                    break;
                }
            if( idx==n )
            {
                if( n>0 )
                    idx = 0; // was It=begin(); stays ==n (==end()) when n==0, since begin()==end() then
            }
            else if( _Step==1 )
            {
                ++idx;
                if( idx==n )
                    idx = 0;
            }
            else if( _Step==-1 )
                idx = (idx==0) ? n-1 : idx-1;
            if( idx<n ) // was It!=end()
            {
                v = e->m_Entries.items[idx].Value;
                if( _Atom->m_Ptr!=NULL )
                    *((unsigned int *)_Atom->m_Ptr) = v;
                else if( _Atom->m_SetCallback!=NULL )
                    _Atom->m_SetCallback(&v, _Atom->m_ClientData);
            }
        }
        else
            fprintf(stderr, "CTwVarAtom_Increment : unknown or unimplemented type\n");
    }
    #pragma GCC diagnostic pop
}

//  ---------------------------------------------------------------------------

void CTwVarAtom_SetDefaults(CTwVarAtom *_Atom)
{
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch" // TW_TYPE_BOOLCPP (see TwMgr.h) is a numeric macro here, not a real ETwType enumerator, in a real-C99 build
    switch( _Atom->m_Type )
    {
    case TW_TYPE_BOOL8:
    case TW_TYPE_BOOL16:
    case TW_TYPE_BOOL32:
    case TW_TYPE_BOOLCPP:
        _Atom->m_NoSlider = true;
        break;
    case TW_TYPE_CHAR:
        _Atom->m_Val.m_Char.m_Max = 0xff;
        _Atom->m_Val.m_Char.m_Min = 0;
        _Atom->m_Val.m_Char.m_Step = 1;
        _Atom->m_Val.m_Char.m_Precision = -1;
        _Atom->m_Val.m_Char.m_Hexa = false;
        break;
    case TW_TYPE_INT8:
        _Atom->m_Val.m_Int8.m_Max = 0x7f;
        _Atom->m_Val.m_Int8.m_Min = -_Atom->m_Val.m_Int8.m_Max-1;
        _Atom->m_Val.m_Int8.m_Step = 1;
        _Atom->m_Val.m_Int8.m_Precision = -1;
        _Atom->m_Val.m_Int8.m_Hexa = false;
        break;
    case TW_TYPE_UINT8:
        _Atom->m_Val.m_UInt8.m_Max = 0xff;
        _Atom->m_Val.m_UInt8.m_Min = 0;
        _Atom->m_Val.m_UInt8.m_Step = 1;
        _Atom->m_Val.m_UInt8.m_Precision = -1;
        _Atom->m_Val.m_UInt8.m_Hexa = false;
        break;
    case TW_TYPE_INT16:
        _Atom->m_Val.m_Int16.m_Max = 0x7fff;
        _Atom->m_Val.m_Int16.m_Min = -_Atom->m_Val.m_Int16.m_Max-1;
        _Atom->m_Val.m_Int16.m_Step = 1;
        _Atom->m_Val.m_Int16.m_Precision = -1;
        _Atom->m_Val.m_Int16.m_Hexa = false;
        break;
    case TW_TYPE_UINT16:
        _Atom->m_Val.m_UInt16.m_Max = 0xffff;
        _Atom->m_Val.m_UInt16.m_Min = 0;
        _Atom->m_Val.m_UInt16.m_Step = 1;
        _Atom->m_Val.m_UInt16.m_Precision = -1;
        _Atom->m_Val.m_UInt16.m_Hexa = false;
        break;
    case TW_TYPE_INT32:
        _Atom->m_Val.m_Int32.m_Max = 0x7fffffff;
        _Atom->m_Val.m_Int32.m_Min = -_Atom->m_Val.m_Int32.m_Max-1;
        _Atom->m_Val.m_Int32.m_Step = 1;
        _Atom->m_Val.m_Int32.m_Precision = -1;
        _Atom->m_Val.m_Int32.m_Hexa = false;
        break;
    case TW_TYPE_UINT32:
        _Atom->m_Val.m_UInt32.m_Max = 0xffffffff;
        _Atom->m_Val.m_UInt32.m_Min = 0;
        _Atom->m_Val.m_UInt32.m_Step = 1;
        _Atom->m_Val.m_UInt32.m_Precision = -1;
        _Atom->m_Val.m_UInt32.m_Hexa = false;
        break;
    case TW_TYPE_FLOAT:
        _Atom->m_Val.m_Float32.m_Max = FLOAT_MAX;
        _Atom->m_Val.m_Float32.m_Min = -FLOAT_MAX;
        _Atom->m_Val.m_Float32.m_Step = 1;
        _Atom->m_Val.m_Float32.m_Precision = -1;
        _Atom->m_Val.m_Float32.m_Hexa = false;
        break;
    case TW_TYPE_DOUBLE:
        _Atom->m_Val.m_Float64.m_Max = DOUBLE_MAX;
        _Atom->m_Val.m_Float64.m_Min = -DOUBLE_MAX;
        _Atom->m_Val.m_Float64.m_Step = 1;
        _Atom->m_Val.m_Float64.m_Precision = -1;
        _Atom->m_Val.m_Float64.m_Hexa = false;
        break;
    case TW_TYPE_CDSTRING:
        _Atom->m_NoSlider = true;
        break;
    /*
    case TW_TYPE_ENUM8:
    case TW_TYPE_ENUM16:
    case TW_TYPE_ENUM32:
        _Atom->m_NoSlider = true;
        break;
    */
    default:
        {} // nothing
    }
    #pragma GCC diagnostic pop

    // special types
    if(    _Atom->m_Type==TW_TYPE_BUTTON 
        || IsEnumType(_Atom->m_Type) // (_Atom->m_Type>=TW_TYPE_ENUM_BASE && _Atom->m_Type<TW_TYPE_ENUM_BASE+(int)g_TwMgr->m_Enums.size()) 
        || IsCSStringType(_Atom->m_Type) // (_Atom->m_Type>=TW_TYPE_CSSTRING_BASE && _Atom->m_Type<=TW_TYPE_CSSTRING_MAX)
        || IsCustomType(_Atom->m_Type) ) // (_Atom->m_Type>=TW_TYPE_CUSTOM_BASE && _Atom->m_Type<TW_TYPE_CUSTOM_BASE+(int)g_TwMgr->m_Customs.size()) )
        _Atom->m_NoSlider = true;
}

//  ---------------------------------------------------------------------------

/*
int CTwVarAtom::DefineEnum(const TwEnumVal *_EnumValues, unsigned int _NbValues)
{
    assert(_EnumValues!=NULL);
    if( m_Type!=TW_TYPE_ENUM8 && m_Type!=TW_TYPE_ENUM16 && m_Type!=TW_TYPE_ENUM32 )
    {
        CTwMgr_SetLastError(g_TwMgr, g_ErrNotEnum);
        return 0;
    }
    if( m_Val.m_Enum.m_Entries==NULL )
        m_Val.m_Enum.m_Entries = new UVal::CEnumVal::CEntries;
    for(unsigned int i=0; i<_NbValues; ++i)
    {
        UVal::CEnumVal::CEntries::value_type Entry(_EnumValues[i].Value, (_EnumValues[i].Label!=NULL)?_EnumValues[i].Label:"");
        pair<UVal::CEnumVal::CEntries::iterator, bool> Result = m_Val.m_Enum.m_Entries->insert(Entry);
        if( !Result.second )
            (Result.first)->second = Entry.second;
    }
    return 1;
}
*/

//  ---------------------------------------------------------------------------

enum EVarGroupAttribs
{
    VG_OPEN = V_ENDTAG+1, // for backward compatibility
    VG_CLOSE,       // for backward compatibility
    VG_OPENED,
    VG_TYPEID,      // used internally for structs
    VG_VALPTR,      // used internally for structs
    VG_ALPHA,       // for backward compatibility
    VG_NOALPHA,     // for backward compatibility
    VG_COLORALPHA,  // tw_type_color* only
    VG_HLS,         // for backward compatibility
    VG_RGB,         // for backward compatibility
    VG_COLORMODE,   // tw_type_color* only
    VG_COLORORDER,  // tw_type_color* only
    VG_ARROW,       // tw_type_quat* only
    VG_ARROWCOLOR,  // tw_type_quat* only
    VG_AXISX,       // tw_type_quat* only
    VG_AXISY,       // tw_type_quat* only
    VG_AXISZ,       // tw_type_quat* only
    VG_SHOWVAL      // tw_type_quat* only
};

int CTwVarGroup_HasAttrib(const CTwVarGroup *_Grp, const char *_Attrib, bool *_HasValue)
{
    (void)_Grp;
    *_HasValue = false;
    if( _stricmp(_Attrib, "open")==0 ) // for backward compatibility
        return VG_OPEN;
    else if( _stricmp(_Attrib, "close")==0 ) // for backward compatibility
        return VG_CLOSE;
    else if( _stricmp(_Attrib, "opened")==0 )
    {
        *_HasValue = true;
        return VG_OPENED;
    }
    else if( _stricmp(_Attrib, "typeid")==0 )
    {
        *_HasValue = true;
        return VG_TYPEID;
    }
    else if( _stricmp(_Attrib, "valptr")==0 )
    {
        *_HasValue = true;
        return VG_VALPTR;
    }
    else if( _stricmp(_Attrib, "alpha")==0 ) // for backward compatibility
        return VG_ALPHA;
    else if( _stricmp(_Attrib, "noalpha")==0 ) // for backward compatibility
        return VG_NOALPHA;
    else if( _stricmp(_Attrib, "coloralpha")==0 )
    {
        *_HasValue = true;
        return VG_COLORALPHA;
    }
    else if( _stricmp(_Attrib, "hls")==0 ) // for backward compatibility
        return VG_HLS;
    else if( _stricmp(_Attrib, "rgb")==0 ) // for backward compatibility
        return VG_RGB;
    else if( _stricmp(_Attrib, "colormode")==0 )
    {
        *_HasValue = true;
        return VG_COLORMODE;
    }
    else if( _stricmp(_Attrib, "colororder")==0 )
    {
        *_HasValue = true;
        return VG_COLORORDER;
    }
    else if( _stricmp(_Attrib, "arrow")==0 )
    {
        *_HasValue = true;
        return VG_ARROW;
    }
    else if( _stricmp(_Attrib, "arrowcolor")==0 )
    {
        *_HasValue = true;
        return VG_ARROWCOLOR;
    }
    else if( _stricmp(_Attrib, "axisx")==0 )
    {
        *_HasValue = true;
        return VG_AXISX;
    }
    else if( _stricmp(_Attrib, "axisy")==0 )
    {
        *_HasValue = true;
        return VG_AXISY;
    }
    else if( _stricmp(_Attrib, "axisz")==0 )
    {
        *_HasValue = true;
        return VG_AXISZ;
    }
    else if( _stricmp(_Attrib, "showval")==0 )
    {
        *_HasValue = true;
        return VG_SHOWVAL;
    }

    return CTwVar_HasAttribBase(_Attrib, _HasValue);
}

int CTwVarGroup_SetAttrib(CTwVarGroup *_Grp, int _AttribID, const char *_Value, TwBar *_Bar, struct CTwVarGroup *_VarParent, int _VarIndex)
{
    switch( _AttribID )
    {
    case VG_OPEN: // for backward compatibility
        if( !_Grp->m_Open )
        {
            _Grp->m_Open = true;
            CTwBar_NotUpToDate(_Bar);
        }
        return 1;
    case VG_CLOSE: // for backward compatibility
        if( _Grp->m_Open )
        {
            _Grp->m_Open = false;
            CTwBar_NotUpToDate(_Bar);
        }
        return 1;
    case VG_OPENED:
        if( _Value!=NULL && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "true")==0 || _stricmp(_Value, "1")==0 )
            {
                if( !_Grp->m_Open )
                {
                    _Grp->m_Open = true;
                    CTwBar_NotUpToDate(_Bar);
                }
                return 1;
            }
            else if( _stricmp(_Value, "false")==0 || _stricmp(_Value, "0")==0 )
            {
                if( _Grp->m_Open )
                {
                    _Grp->m_Open = false;
                    CTwBar_NotUpToDate(_Bar);
                }
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
    case VG_TYPEID:
        {
            int type = TW_TYPE_UNDEF;
            if( _Value!=NULL && sscanf(_Value, "%d", &type)==1 )
            {
                int idx = type - TW_TYPE_STRUCT_BASE;
                if( idx>=0 && idx<(int)g_TwMgr->m_Structs.count )
                {
                    _Grp->m_SummaryCallback   = g_TwMgr->m_Structs.items[idx].m_SummaryCallback;
                    _Grp->m_SummaryClientData = g_TwMgr->m_Structs.items[idx].m_SummaryClientData;
                    _Grp->m_StructType = (TwType)type;
                    return 1;
                }
            }
            return 0;
        }
    case VG_VALPTR:
        {
            void *structValuePtr = NULL;
            if( _Value!=NULL && sscanf(_Value, "%p", &structValuePtr)==1 )
            {
                _Grp->m_StructValuePtr = structValuePtr;
                _Grp->m_Base.m_ColorPtr = &(_Bar->m_ColStructText);
                return 1;
            }
            return 0;
        }
    case VG_ALPHA: // for backward compatibility
        if( _Grp->m_SummaryCallback==CColorExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_color?
            if( ((CColorExt *)(_Grp->m_StructValuePtr))->m_CanHaveAlpha )
            {
                ((CColorExt *)(_Grp->m_StructValuePtr))->m_HasAlpha = true;
                CTwBar_NotUpToDate(_Bar);
                return 1;
            }
        return 0;
    case VG_NOALPHA: // for backward compatibility
        if( _Grp->m_SummaryCallback==CColorExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_color?
        {
            ((CColorExt *)(_Grp->m_StructValuePtr))->m_HasAlpha = false;
            CTwBar_NotUpToDate(_Bar);
            return 1;
        }
        else
            return 0;
    case VG_COLORALPHA:
        if( _Value!=NULL && strlen(_Value)>0 )
        {
            if( _Grp->m_SummaryCallback==CColorExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_color?
            {
                if( _stricmp(_Value, "true")==0 || _stricmp(_Value, "1")==0 )
                {
                    if( ((CColorExt *)(_Grp->m_StructValuePtr))->m_CanHaveAlpha )
                    {
                        if( !((CColorExt *)(_Grp->m_StructValuePtr))->m_HasAlpha )
                        {
                            ((CColorExt *)(_Grp->m_StructValuePtr))->m_HasAlpha = true;
                            CTwBar_NotUpToDate(_Bar);
                        }
                        return 1;
                    }
                }
                else if( _stricmp(_Value, "false")==0 || _stricmp(_Value, "0")==0 )
                {
                    if( ((CColorExt *)(_Grp->m_StructValuePtr))->m_HasAlpha )
                    {
                        ((CColorExt *)(_Grp->m_StructValuePtr))->m_HasAlpha = false;
                        CTwBar_NotUpToDate(_Bar);
                    }
                    return 1;
                }
            }
        }
        return 0;
    case VG_HLS: // for backward compatibility
        if( _Grp->m_SummaryCallback==CColorExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_color?
        {
            ((CColorExt *)(_Grp->m_StructValuePtr))->m_HLS = true;
            CTwBar_NotUpToDate(_Bar);
            return 1;
        }
        else
            return 0;
    case VG_RGB: // for backward compatibility
        if( _Grp->m_SummaryCallback==CColorExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_color?
        {
            ((CColorExt *)(_Grp->m_StructValuePtr))->m_HLS = false;
            CTwBar_NotUpToDate(_Bar);
            return 1;
        }
        else
            return 0;
    case VG_COLORMODE:
        if( _Value!=NULL && strlen(_Value)>0 )
        {
            if( _Grp->m_SummaryCallback==CColorExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_color?
            {
                if( _stricmp(_Value, "hls")==0 )
                {
                    if( !((CColorExt *)(_Grp->m_StructValuePtr))->m_HLS )
                    {
                        ((CColorExt *)(_Grp->m_StructValuePtr))->m_HLS = true;
                        CTwBar_NotUpToDate(_Bar);
                    }
                    return 1;
                }
                else if( _stricmp(_Value, "rgb")==0 )
                {
                    if( ((CColorExt *)(_Grp->m_StructValuePtr))->m_HLS )
                    {
                        ((CColorExt *)(_Grp->m_StructValuePtr))->m_HLS = false;
                        CTwBar_NotUpToDate(_Bar);
                    }
                    return 1;
                }
            }
        }
        return 0;
    case VG_COLORORDER:
        if( _Grp->m_SummaryCallback==CColorExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_color?
        {
            if( _Value!=NULL )
            {
                if( _stricmp(_Value, "rgba")==0 )
                    ((CColorExt *)(_Grp->m_StructValuePtr))->m_OGL = true;
                else if( _stricmp(_Value, "argb")==0 )
                    ((CColorExt *)(_Grp->m_StructValuePtr))->m_OGL = false;
                else
                    return 0;
                return 1;
            }
            return 0;
        }
        else
            return 0;
    case VG_ARROW:
        if( _Grp->m_SummaryCallback==CQuaternionExt_SummaryCB && _Grp->m_StructValuePtr!=NULL )    // is tw_type_quat?
        {
            if( _Value!=NULL )
            {
                double *dir = ((CQuaternionExt *)(_Grp->m_StructValuePtr))->m_Dir;
                double x, y, z;
                if( sscanf(_Value, "%lf %lf %lf", &x, &y, &z)==3 )
                {
                    dir[0] = x; 
                    dir[1] = y;
                    dir[2] = z;
                }
                else if( _stricmp(_Value, "off")==0 || _stricmp(_Value, "0")==0 )
                    dir[0] = dir[1] = dir[2] = 0;
                else
                    return 0;
                return 1;
            }
            return 0;
        }
        else
            return 0;
    case VG_ARROWCOLOR:
        if( _Grp->m_SummaryCallback==CQuaternionExt_SummaryCB && _Grp->m_StructValuePtr!=NULL )    // is tw_type_quat?
        {
            if( _Value!=NULL )
            {
                int r, g, b;
                if( sscanf(_Value, "%d %d %d", &r, &g, &b)==3 )
                    ((CQuaternionExt *)(_Grp->m_StructValuePtr))->m_DirColor = Color32FromARGBi(255, r, g, b);
                else
                    return 0;
                return 1;
            }
            return 0;
        }
        else
            return 0;
    case VG_AXISX:
    case VG_AXISY:
    case VG_AXISZ:
        if( _Grp->m_SummaryCallback==CQuaternionExt_SummaryCB && _Grp->m_StructValuePtr!=NULL )    // is tw_type_quat?
        {
            if( _Value!=NULL )
            {
                float x = 0, y = 0, z = 0;
                if( _stricmp(_Value, "x")==0 || _stricmp(_Value, "+x")==0 )
                    x = 1;
                else if( _stricmp(_Value, "-x")==0 )
                    x = -1;
                else if( _stricmp(_Value, "y")==0 || _stricmp(_Value, "+y")==0 )
                    y = 1;
                else if( _stricmp(_Value, "-y")==0 )
                    y = -1;
                else if( _stricmp(_Value, "z")==0 || _stricmp(_Value, "+z")==0 )
                    z = 1;
                else if( _stricmp(_Value, "-z")==0 )
                    z = -1;
                else
                    return 0;
                int i = (_AttribID==VG_AXISX) ? 0 : ((_AttribID==VG_AXISY) ? 1 : 2);
                ((CQuaternionExt *)(_Grp->m_StructValuePtr))->m_Permute[i][0] = x; 
                ((CQuaternionExt *)(_Grp->m_StructValuePtr))->m_Permute[i][1] = y; 
                ((CQuaternionExt *)(_Grp->m_StructValuePtr))->m_Permute[i][2] = z;
                return 1;
            }
            return 0;
        }
        else
            return 0;
    case VG_SHOWVAL:
        if( _Grp->m_SummaryCallback==CQuaternionExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_quat?
        {
            if( _Value!=NULL )
            {
                if( _stricmp(_Value, "true")==0 || _stricmp(_Value, "on")==0 || _stricmp(_Value, "1")==0 )
                {
                    ((CQuaternionExt *)(_Grp->m_StructValuePtr))->m_ShowVal = true;
                    CTwBar_NotUpToDate(_Bar);
                    return 1;
                }
                else if( _stricmp(_Value, "false")==0 || _stricmp(_Value, "off")==0 || _stricmp(_Value, "0")==0 )
                {
                    ((CQuaternionExt *)(_Grp->m_StructValuePtr))->m_ShowVal = false;
                    CTwBar_NotUpToDate(_Bar);
                    return 1;
                }
            }
            return 0;
        }
        else
            return 0;
    default:
        return CTwVar_SetAttribBase(&_Grp->m_Base, _AttribID, _Value, _Bar, _VarParent, _VarIndex);
    }
}

ERetType CTwVarGroup_GetAttrib(const CTwVarGroup *_Grp, int _AttribID, TwBar *_Bar, struct CTwVarGroup *_VarParent, int _VarIndex, CDoubleArray *outDoubles, sds *outString)
{
    outDoubles->count = 0;
    sdsclear(*outString);

    switch( _AttribID )
    {
    case VG_OPENED:
        tw_da_append(outDoubles,  _Grp->m_Open );
        return RET_DOUBLE;
    case VG_COLORALPHA:
        if( _Grp->m_SummaryCallback==CColorExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_color?
        {
            tw_da_append(outDoubles,  ((CColorExt *)(_Grp->m_StructValuePtr))->m_HasAlpha );
            return RET_DOUBLE;
        }
        CTwMgr_SetLastError(g_TwMgr, g_ErrInvalidAttrib);
        return RET_ERROR;
    case VG_COLORMODE:
        if( _Grp->m_SummaryCallback==CColorExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_color?
        {
            if( ((CColorExt *)(_Grp->m_StructValuePtr))->m_HLS )
                *outString = sdscat(*outString, "hls");
            else
                *outString = sdscat(*outString, "rgb");
            return RET_STRING;
        }
        CTwMgr_SetLastError(g_TwMgr, g_ErrInvalidAttrib);
        return RET_ERROR;
    case VG_COLORORDER:
        if( _Grp->m_SummaryCallback==CColorExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_color?
        {
            if( ((CColorExt *)(_Grp->m_StructValuePtr))->m_OGL )
                *outString = sdscat(*outString, "rgba");
            else
                *outString = sdscat(*outString, "argb");
            return RET_STRING;
        }
        CTwMgr_SetLastError(g_TwMgr, g_ErrInvalidAttrib);
        return RET_ERROR;
    case VG_ARROW:
        if( _Grp->m_SummaryCallback==CQuaternionExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_quat?
        {
            double *dir = ((CQuaternionExt *)(_Grp->m_StructValuePtr))->m_Dir;
            tw_da_append(outDoubles, dir[0]);
            tw_da_append(outDoubles, dir[1]);
            tw_da_append(outDoubles, dir[2]);
            return RET_DOUBLE;
        }
        CTwMgr_SetLastError(g_TwMgr, g_ErrInvalidAttrib);
        return RET_ERROR;
    case VG_ARROWCOLOR:
        if( _Grp->m_SummaryCallback==CQuaternionExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_quat?
        {
            int a, r, g, b;
            a = r = g = b = 0;
            Color32ToARGBi(((CQuaternionExt *)(_Grp->m_StructValuePtr))->m_DirColor, &a, &r, &g, &b);
            tw_da_append(outDoubles, r);
            tw_da_append(outDoubles, g);
            tw_da_append(outDoubles, b);
            return RET_DOUBLE;
        }
        CTwMgr_SetLastError(g_TwMgr, g_ErrInvalidAttrib);
        return RET_ERROR;
    case VG_AXISX:
    case VG_AXISY:
    case VG_AXISZ:
        if( _Grp->m_SummaryCallback==CQuaternionExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_quat?
        {
            int i = (_AttribID==VG_AXISX) ? 0 : ((_AttribID==VG_AXISY) ? 1 : 2);
            float x = ((CQuaternionExt *)(_Grp->m_StructValuePtr))->m_Permute[i][0]; 
            float y = ((CQuaternionExt *)(_Grp->m_StructValuePtr))->m_Permute[i][1]; 
            float z = ((CQuaternionExt *)(_Grp->m_StructValuePtr))->m_Permute[i][2]; 
            if( x>0 )
                *outString = sdscat(*outString, "+x");
            else if( x<0 )
                *outString = sdscat(*outString, "-x");
            else if( y>0 )
                *outString = sdscat(*outString, "+y");
            else if( y<0 )
                *outString = sdscat(*outString, "-y");
            else if( z>0 )
                *outString = sdscat(*outString, "+z");
            else if( z<0 )
                *outString = sdscat(*outString, "-z");
            else
                *outString = sdscat(*outString, "0"); // should not happened
            return RET_DOUBLE;
        }
        CTwMgr_SetLastError(g_TwMgr, g_ErrInvalidAttrib);
        return RET_ERROR;
    case VG_SHOWVAL:
        if( _Grp->m_SummaryCallback==CQuaternionExt_SummaryCB && _Grp->m_StructValuePtr!=NULL ) // is tw_type_quat?
        {
            tw_da_append(outDoubles,  ((CQuaternionExt *)(_Grp->m_StructValuePtr))->m_ShowVal );
            return RET_DOUBLE;
        }
        CTwMgr_SetLastError(g_TwMgr, g_ErrInvalidAttrib);
        return RET_ERROR;
    default:
        return CTwVar_GetAttribBase(&_Grp->m_Base, _AttribID, _Bar, _VarParent, _VarIndex, outDoubles, outString);
    }
}

//  ---------------------------------------------------------------------------

const CTwVar *CTwVarGroup_Find(const CTwVarGroup *_Grp, const char *_Name, CTwVarGroup **_Parent, int *_Index)
{
    if( strcmp(_Name, _Grp->m_Base.m_Name)==0 )
    {
        if( _Parent!=NULL )
            *_Parent = NULL;
        if( _Index!=NULL )
            *_Index = -1;
        return &_Grp->m_Base;
    }
    else
    {
        const CTwVar *v;
        for( size_t i=0; i<_Grp->m_Vars.count; ++ i )
            if( _Grp->m_Vars.items[i]!=NULL )
            {
                v = CTwVar_Find(_Grp->m_Vars.items[i], _Name, _Parent, _Index);
                if( v!=NULL )
                {
                    if( _Parent!=NULL && *_Parent==NULL )
                    {
                        *_Parent = (CTwVarGroup *)(_Grp);
                        if( _Index!=NULL )
                            *_Index  = (int)i;
                    }
                    return v;
                }
            }
        return NULL;
    }
}

//  ---------------------------------------------------------------------------

size_t CTwVar_GetDataSize(TwType _Type)
{
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch" // TW_TYPE_BOOLCPP (see TwMgr.h) is a numeric macro here, not a real ETwType enumerator, in a real-C99 build
    switch( _Type )
    {
    case TW_TYPE_BOOLCPP:
        return sizeof(bool);
    case TW_TYPE_BOOL8:
    case TW_TYPE_CHAR:
    case TW_TYPE_INT8:
    case TW_TYPE_UINT8:
    //case TW_TYPE_ENUM8:
        return 1;
    case TW_TYPE_BOOL16:
    case TW_TYPE_INT16:
    case TW_TYPE_UINT16:
    //case TW_TYPE_ENUM16:
        return 2;
    case TW_TYPE_BOOL32:
    case TW_TYPE_INT32:
    case TW_TYPE_UINT32:
    case TW_TYPE_FLOAT:
    //case TW_TYPE_ENUM32:
        return 4;
    case TW_TYPE_DOUBLE:
        return 8;
    case TW_TYPE_CDSTRING:
        return sizeof(char *);
    default:
        if( g_TwMgr && _Type>=TW_TYPE_STRUCT_BASE && _Type<TW_TYPE_STRUCT_BASE+(int)g_TwMgr->m_Structs.count )
        {
            const CStruct *s = &g_TwMgr->m_Structs.items[_Type-TW_TYPE_STRUCT_BASE];
            return s->m_Size;
            /*
            size_t size = 0;
            for( size_t i=0; i<s.m_Members.size(); ++i )
                size += s.m_Members[i].m_Size;
            return size;
            */
        }
        else if( g_TwMgr && IsEnumType(_Type) )
            return 4;
        else if( IsCSStringType(_Type) )
            return TW_CSSTRING_SIZE(_Type);
        else    // includes TW_TYPE_BUTTON
            return 0;
    }
    #pragma GCC diagnostic pop
}

//  ---------------------------------------------------------------------------

CTwBar *CTwBar_Create(const char *_Name)
{
    assert(g_TwMgr!=NULL && g_TwMgr->m_Graph!=NULL);

    CTwBar *Bar = (CTwBar *)malloc(sizeof(CTwBar));

    // m_VarRoot no longer has its own constructor (converted to a free
    // function as part of the C99 rewrite; it used to run automatically as
    // a member subobject before this body executed), so it must be
    // explicitly initialized first, before anything below touches it.
    CTwVarGroup_Init(&Bar->m_VarRoot);

    Bar->m_Name = sdsnew(_Name);
    Bar->m_Label = sdsempty();
    Bar->m_Help = sdsempty();
    Bar->m_Visible = true;
    Bar->m_VarRoot.m_Base.m_IsRoot = true;
    Bar->m_VarRoot.m_Open = true;
    Bar->m_VarRoot.m_SummaryCallback = NULL;
    Bar->m_VarRoot.m_SummaryClientData = NULL;
    Bar->m_VarRoot.m_StructValuePtr = NULL;

    Bar->m_UpToDate = false;
    int n = (int)g_TwMgr->m_Bars.count;
    Bar->m_PosX = 24*n-8;
    Bar->m_PosY = 24*n-8;
    Bar->m_Width = 200;
    Bar->m_Height = 320;
    int cr, cg, cb;
    if( g_TwMgr->m_UseOldColorScheme )
    {
        ColorHLSToRGBi(g_TwMgr->m_BarInitColorHue%256, 180, 200, &cr, &cg, &cb);
        Bar->m_Color = Color32FromARGBi(0xf0, cr, cg, cb);
        Bar->m_DarkText = true;
    }
    else
    {
        ColorHLSToRGBi(g_TwMgr->m_BarInitColorHue%256, 80, 200, &cr, &cg, &cb);
        Bar->m_Color = Color32FromARGBi(64, cr, cg, cb);
        Bar->m_DarkText = false;
    }
    g_TwMgr->m_BarInitColorHue -= 16;
    if( g_TwMgr->m_BarInitColorHue<0 )
        g_TwMgr->m_BarInitColorHue += 256;
    Bar->m_Font = g_TwMgr->m_CurrentFont;
    //m_Font = g_DefaultNormalFont;
    //m_Font = g_DefaultSmallFont;
    //m_Font = g_DefaultLargeFont;
    Bar->m_TitleWidth = 0;
    Bar->m_Sep = 1;
//#pragma warning "lineSep WIP"
    Bar->m_LineSep = 1;
    Bar->m_ValuesWidth = 10*(Bar->m_Font->m_CharHeight/2); // about 10 characters
    Bar->m_NbHierLines = 0;
    Bar->m_NbDisplayedLines = 0;
    Bar->m_FirstLine = 0;
    Bar->m_LastUpdateTime = 0;
    Bar->m_UpdatePeriod = 2;
    Bar->m_ScrollYW = 0;
    Bar->m_ScrollYH = 0;
    Bar->m_ScrollY0 = 0;
    Bar->m_ScrollY1 = 0;

    Bar->m_DrawHandles = false;
    Bar->m_DrawIncrDecrBtn = false;
    Bar->m_DrawRotoBtn = false;
    Bar->m_DrawClickBtn = false;
    Bar->m_DrawListBtn = false;
    Bar->m_DrawBoolBtn = false;
    Bar->m_MouseDrag = false;
    Bar->m_MouseDragVar = false;
    Bar->m_MouseDragTitle = false;
    Bar->m_MouseDragScroll = false;
    Bar->m_MouseDragResizeUR = false;
    Bar->m_MouseDragResizeUL = false;
    Bar->m_MouseDragResizeLR = false;
    Bar->m_MouseDragResizeLL = false;
    Bar->m_MouseDragValWidth = false;
    Bar->m_MouseOriginX = 0;
    Bar->m_MouseOriginY = 0;
    Bar->m_ValuesWidthRatio = 0;
    Bar->m_VarHasBeenIncr = true;
    Bar->m_FirstLine0 = 0;
    Bar->m_HighlightedLine = -1;
    Bar->m_HighlightedLinePrev = -1;
    Bar->m_HighlightedLineLastValid = -1;
    Bar->m_HighlightIncrBtn = false;
    Bar->m_HighlightDecrBtn = false;
    Bar->m_HighlightRotoBtn = false;
    Bar->m_HighlightClickBtn = false;
    Bar->m_HighlightClickBtnAuto = 0;
    Bar->m_HighlightListBtn = false;
    Bar->m_HighlightBoolBtn = false;
    Bar->m_HighlightTitle = false;
    Bar->m_HighlightScroll = false;
    Bar->m_HighlightUpScroll = false;
    Bar->m_HighlightDnScroll = false;
    Bar->m_HighlightMinimize = false;
    Bar->m_HighlightFont = false;
    Bar->m_HighlightValWidth = false;
    Bar->m_HighlightLabelsHeader = false;
    Bar->m_HighlightValuesHeader = false;
    Bar->m_ButtonAlign = g_TwMgr->m_ButtonAlign;

    Bar->m_IsMinimized = false;
    Bar->m_MinNumber = 0;
    Bar->m_MinPosX = 0;
    Bar->m_MinPosY = 0;
    Bar->m_HighlightMaximize = false;
    Bar->m_IsHelpBar = false;
    Bar->m_IsPopupList = false;
    Bar->m_VarEnumLinkedToPopupList = NULL;
    Bar->m_BarLinkedToPopupList = NULL;

    Bar->m_Resizable = true;
    Bar->m_Movable = true;
    Bar->m_Iconifiable = true;
    Bar->m_Contained = g_TwMgr->m_Contained;

    Bar->m_TitleTextObj = g_TwMgr->m_Graph->NewTextObj(g_TwMgr->m_Graph);
    Bar->m_LabelsTextObj = g_TwMgr->m_Graph->NewTextObj(g_TwMgr->m_Graph);
    Bar->m_ValuesTextObj = g_TwMgr->m_Graph->NewTextObj(g_TwMgr->m_Graph);
    Bar->m_ShortcutTextObj = g_TwMgr->m_Graph->NewTextObj(g_TwMgr->m_Graph);
    Bar->m_HeadersTextObj = g_TwMgr->m_Graph->NewTextObj(g_TwMgr->m_Graph);
    Bar->m_ShortcutLine = -1;

    Bar->m_RotoMinRadius = 24;
    Bar->m_RotoNbSubdiv = 256;   // number of steps for one turn

    Bar->m_HierTags.items = NULL;
    Bar->m_HierTags.count = 0;
    Bar->m_HierTags.capacity = 0;

    Bar->m_CustomRecords.items = NULL;
    Bar->m_CustomRecords.count = 0;
    Bar->m_CustomRecords.capacity = 0;
    Bar->m_CustomActiveStructProxy = NULL;

    // m_Roto/m_EditInPlace no longer have their own constructors (converted
    // to free functions as part of the C99 rewrite), so they must be
    // explicitly initialized here instead of relying on automatic
    // member-subobject construction.
    CRotoSlider_Init(&Bar->m_Roto);
    CEditInPlace_Init(&Bar->m_EditInPlace);

    CTwBar_UpdateColors(Bar);
    CTwBar_NotUpToDate(Bar);

    return Bar;
}

//  ---------------------------------------------------------------------------

void CTwBar_Destroy(CTwBar *_Bar)
{
    if( _Bar->m_IsMinimized )
        CTwMgr_Maximize(g_TwMgr, _Bar);
    if( _Bar->m_TitleTextObj )
        g_TwMgr->m_Graph->DeleteTextObj(g_TwMgr->m_Graph, _Bar->m_TitleTextObj);
    if( _Bar->m_LabelsTextObj )
        g_TwMgr->m_Graph->DeleteTextObj(g_TwMgr->m_Graph, _Bar->m_LabelsTextObj);
    if( _Bar->m_ValuesTextObj )
        g_TwMgr->m_Graph->DeleteTextObj(g_TwMgr->m_Graph, _Bar->m_ValuesTextObj);
    if( _Bar->m_ShortcutTextObj )
        g_TwMgr->m_Graph->DeleteTextObj(g_TwMgr->m_Graph, _Bar->m_ShortcutTextObj);
    if( _Bar->m_HeadersTextObj )
        g_TwMgr->m_Graph->DeleteTextObj(g_TwMgr->m_Graph, _Bar->m_HeadersTextObj);
    tw_da_free(&_Bar->m_HierTags);
    tw_da_free(&_Bar->m_CustomRecords);
    CEditInPlace_Free(&_Bar->m_EditInPlace); // no longer runs automatically - see CTwBar_Create's matching CEditInPlace_Init
    sdsfree(_Bar->m_Name);
    sdsfree(_Bar->m_Label);
    sdsfree(_Bar->m_Help);

    // m_VarRoot no longer has its own destructor (converted to a free
    // function above); in the original C++ code its destructor ran
    // automatically AFTER this body finished (member destructors run in
    // reverse declaration order after ~CTwBar()'s body), so it is freed
    // last here too, right before releasing CTwBar's own memory.
    CTwVarGroup_Free(&_Bar->m_VarRoot);
    free(_Bar);
}

//  ---------------------------------------------------------------------------

const CTwVar *CTwBar_Find(const CTwBar *_Bar, const char *_Name, CTwVarGroup **_Parent, int *_Index)
{
    return CTwVarGroup_Find(&_Bar->m_VarRoot, _Name, _Parent, _Index);
}

//  ---------------------------------------------------------------------------

enum EBarAttribs
{
    BAR_LABEL = 1,
    BAR_HELP,
    BAR_COLOR,
    BAR_ALPHA,
    BAR_TEXT,
    BAR_SHOW,    // deprecated, used BAR_VISIBLE instead
    BAR_HIDE,    // deprecated, used BAR_VISIBLE instead
    BAR_ICONIFY, // deprecated, used BAR_ICONIFIED instead
    BAR_VISIBLE,
    BAR_ICONIFIED,
    BAR_SIZE,
    BAR_POSITION,
    BAR_REFRESH,
    BAR_FONT_SIZE,
    BAR_FONT_STYLE,
    BAR_VALUES_WIDTH,
    BAR_ICON_POS,
    BAR_ICON_ALIGN,
    BAR_ICON_MARGIN,
    BAR_RESIZABLE,
    BAR_MOVABLE,
    BAR_ICONIFIABLE,
    BAR_FONT_RESIZABLE,
    BAR_ALWAYS_TOP,
    BAR_ALWAYS_BOTTOM,
    BAR_COLOR_SCHEME,
    BAR_CONTAINED,
    BAR_BUTTON_ALIGN
};

int CTwBar_HasAttrib(const CTwBar *_Bar, const char *_Attrib, bool *_HasValue)
{
    (void)_Bar;
    *_HasValue = true;
    if( _stricmp(_Attrib, "label")==0 )
        return BAR_LABEL;
    else if( _stricmp(_Attrib, "help")==0 )
        return BAR_HELP;
    else if( _stricmp(_Attrib, "color")==0 )
        return BAR_COLOR;
    else if( _stricmp(_Attrib, "alpha")==0 )
        return BAR_ALPHA;
    else if( _stricmp(_Attrib, "text")==0 )
        return BAR_TEXT;
    else if( _stricmp(_Attrib, "size")==0 )
        return BAR_SIZE;
    else if( _stricmp(_Attrib, "position")==0 )
        return BAR_POSITION;
    else if( _stricmp(_Attrib, "refresh")==0 )
        return BAR_REFRESH;
    else if( _stricmp(_Attrib, "fontsize")==0 )
        return BAR_FONT_SIZE;
    else if( _stricmp(_Attrib, "fontstyle")==0 )
        return BAR_FONT_STYLE;
    else if( _stricmp(_Attrib, "valueswidth")==0 )
        return BAR_VALUES_WIDTH;
    else if( _stricmp(_Attrib, "iconpos")==0 )
        return BAR_ICON_POS;
    else if( _stricmp(_Attrib, "iconalign")==0 )
        return BAR_ICON_ALIGN;
    else if( _stricmp(_Attrib, "iconmargin")==0 )
        return BAR_ICON_MARGIN;
    else if( _stricmp(_Attrib, "resizable")==0 )
        return BAR_RESIZABLE;
    else if( _stricmp(_Attrib, "movable")==0 )
        return BAR_MOVABLE;
    else if( _stricmp(_Attrib, "iconifiable")==0 )
        return BAR_ICONIFIABLE;
    else if( _stricmp(_Attrib, "fontresizable")==0 )
        return BAR_FONT_RESIZABLE;
    else if( _stricmp(_Attrib, "alwaystop")==0 )
        return BAR_ALWAYS_TOP;
    else if( _stricmp(_Attrib, "alwaysbottom")==0 )
        return BAR_ALWAYS_BOTTOM;
    else if( _stricmp(_Attrib, "visible")==0 )
        return BAR_VISIBLE;
    else if( _stricmp(_Attrib, "iconified")==0 )
        return BAR_ICONIFIED;
    else if( _stricmp(_Attrib, "colorscheme")==0 )
        return BAR_COLOR_SCHEME;
    else if( _stricmp(_Attrib, "contained")==0 )
        return BAR_CONTAINED;
    else if( _stricmp(_Attrib, "buttonalign")==0 )
        return BAR_BUTTON_ALIGN;

    *_HasValue = false;
    if( _stricmp(_Attrib, "show")==0 ) // for backward compatibility
        return BAR_SHOW;
    else if( _stricmp(_Attrib, "hide")==0 ) // for backward compatibility
        return BAR_HIDE;
    else if( _stricmp(_Attrib, "iconify")==0 ) // for backward compatibility
        return BAR_ICONIFY;

    return 0; // not found
}

int CTwBar_SetAttrib(CTwBar *_Bar, int _AttribID, const char *_Value)
{
    switch( _AttribID )
    {
    case BAR_LABEL:
        if( _Value && strlen(_Value)>0 )
        {
            _Bar->m_Label = sdscpy(_Bar->m_Label, _Value);
            CTwBar_NotUpToDate(_Bar);
            return 1;
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNoValue);
            return 0;
        }
    case BAR_HELP:
        if( _Value && strlen(_Value)>0 )
        {
            _Bar->m_Help = sdscpy(_Bar->m_Help, _Value);
            CTwBar_NotUpToDate(_Bar);
            return 1;
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNoValue);
            return 0;
        }
    case BAR_COLOR:
        if( _Value && strlen(_Value)>0 )
        {
            int v0, v1, v2, v3;
            int n = sscanf(_Value, "%d%d%d%d", &v0, &v1, &v2, &v3);
            color32 c;
            int alpha = (_Bar->m_Color>>24) & 0xff;
            if( n==3 && v0>=0 && v0<=255 && v1>=0 && v1<=255 && v2>=0 && v2<=255 )
                c = Color32FromARGBi(alpha, v0, v1, v2);
            else if( n==4 && v0>=0 && v0<=255 && v1>=0 && v1<=255 && v2>=0 && v2<=255 && v3>=0 && v3<=255 )
                c = Color32FromARGBi(v0, v1, v2, v3);
            else
            {
                CTwMgr_SetLastError(g_TwMgr, g_ErrBadValue);
                return 0;
            }
            _Bar->m_Color = c;
            CTwBar_NotUpToDate(_Bar);
            return 1;
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNoValue);
            return 0;
        }
    case BAR_ALPHA:
        if( _Value && strlen(_Value)>0 )
        {
            int alpha = 255;
            int n = sscanf(_Value, "%d", &alpha);
            if( n==1 && alpha>=0 && alpha<=255 )
                _Bar->m_Color = (alpha<<24) | (_Bar->m_Color & 0xffffff);
            else
            {
                CTwMgr_SetLastError(g_TwMgr, g_ErrBadValue);
                return 0;
            }
            CTwBar_NotUpToDate(_Bar);
            return 1;
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNoValue);
            return 0;
        }
    case BAR_TEXT:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "dark")==0 )
                _Bar->m_DarkText = true;
            else if( _stricmp(_Value, "light")==0 )
                _Bar->m_DarkText = false;
            else
            {
                CTwMgr_SetLastError(g_TwMgr, g_ErrBadValue);
                return 0;
            }
            CTwBar_NotUpToDate(_Bar);
            return 1;
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNoValue);
            return 0;
        }
    case BAR_SIZE:
        if( _Value && strlen(_Value)>0 )
        {
            int sx, sy;
            int n = sscanf(_Value, "%d%d", &sx, &sy);
            if( n==2 && sx>0 && sy>0 )
            {
                _Bar->m_Width = sx;
                _Bar->m_Height = sy;
                CTwBar_NotUpToDate(_Bar);
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
    case BAR_POSITION:
        if( _Value && strlen(_Value)>0 )
        {
            int x, y;
            int n = sscanf(_Value, "%d%d", &x, &y);
            if( n==2 && x>=0 && y>=0 )
            {
                _Bar->m_PosX = x;
                _Bar->m_PosY = y;
                CTwBar_NotUpToDate(_Bar);
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
    case BAR_REFRESH:
        if( _Value && strlen(_Value)>0 )
        {
            float r;
            int n = sscanf(_Value, "%f", &r);
            if( n==1 && r>=0 )
            {
                _Bar->m_UpdatePeriod = r;
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
    case BAR_VALUES_WIDTH:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "fit")==0 )
            {
                _Bar->m_ValuesWidth = VALUES_WIDTH_FIT;
                CTwBar_NotUpToDate(_Bar);
                return 1;
            } 
            else
            {
                int w;
                int n = sscanf(_Value, "%d", &w);
                if( n==1 && w>0 )
                {
                    _Bar->m_ValuesWidth = w;
                    CTwBar_NotUpToDate(_Bar);
                    return 1;
                }
                else
                {
                    CTwMgr_SetLastError(g_TwMgr, g_ErrBadValue);
                    return 0;
                }
            }
        }
        else
        {
            CTwMgr_SetLastError(g_TwMgr, g_ErrNoValue);
            return 0;
        }
    case BAR_FONT_SIZE:
        return CTwMgr_SetAttrib(g_TwMgr, MGR_FONT_SIZE, _Value);
    case BAR_FONT_STYLE:
        return CTwMgr_SetAttrib(g_TwMgr, MGR_FONT_STYLE, _Value);
    case BAR_ICON_POS:
        return CTwMgr_SetAttrib(g_TwMgr, MGR_ICON_POS, _Value);
    case BAR_ICON_ALIGN:
        return CTwMgr_SetAttrib(g_TwMgr, MGR_ICON_ALIGN, _Value);
    case BAR_ICON_MARGIN:
        return CTwMgr_SetAttrib(g_TwMgr, MGR_ICON_MARGIN, _Value);
    case BAR_SHOW:    // deprecated
        TwSetBarState(_Bar, TW_STATE_SHOWN);
        return 1;
    case BAR_HIDE:    // deprecated
        TwSetBarState(_Bar, TW_STATE_HIDDEN);
        return 1;
    case BAR_ICONIFY: // deprecated
        TwSetBarState(_Bar, TW_STATE_ICONIFIED);
        return 1;
    case BAR_RESIZABLE:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "1")==0 || _stricmp(_Value, "true")==0 )
            {
                _Bar->m_Resizable = true;
                return 1;
            }
            else if( _stricmp(_Value, "0")==0 || _stricmp(_Value, "false")==0 )
            {
                _Bar->m_Resizable = false;
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
    case BAR_MOVABLE:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "1")==0 || _stricmp(_Value, "true")==0 )
            {
                _Bar->m_Movable = true;
                return 1;
            }
            else if( _stricmp(_Value, "0")==0 || _stricmp(_Value, "false")==0 )
            {
                _Bar->m_Movable = false;
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
    case BAR_ICONIFIABLE:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "1")==0 || _stricmp(_Value, "true")==0 )
            {
                _Bar->m_Iconifiable = true;
                return 1;
            }
            else if( _stricmp(_Value, "0")==0 || _stricmp(_Value, "false")==0 )
            {
                _Bar->m_Iconifiable = false;
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
    case BAR_FONT_RESIZABLE:
        return CTwMgr_SetAttrib(g_TwMgr, MGR_FONT_RESIZABLE, _Value);
    case BAR_ALWAYS_TOP:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "1")==0 || _stricmp(_Value, "true")==0 )
            {
                g_TwMgr->m_BarAlwaysOnTop = sdscpy(g_TwMgr->m_BarAlwaysOnTop, _Bar->m_Name);
                if( sdslen(g_TwMgr->m_BarAlwaysOnBottom)>0 && strcmp(g_TwMgr->m_BarAlwaysOnBottom, _Bar->m_Name)==0 )
                    sdsclear(g_TwMgr->m_BarAlwaysOnBottom);
                TwSetTopBar(_Bar);
                return 1;
            }
            else if( _stricmp(_Value, "0")==0 || _stricmp(_Value, "false")==0 )
            {
                if( sdslen(g_TwMgr->m_BarAlwaysOnTop)>0 && strcmp(g_TwMgr->m_BarAlwaysOnTop, _Bar->m_Name)==0 )
                    sdsclear(g_TwMgr->m_BarAlwaysOnTop);
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
    case BAR_ALWAYS_BOTTOM:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "1")==0 || _stricmp(_Value, "true")==0 )
            {
                g_TwMgr->m_BarAlwaysOnBottom = sdscpy(g_TwMgr->m_BarAlwaysOnBottom, _Bar->m_Name);
                if( sdslen(g_TwMgr->m_BarAlwaysOnTop)>0 && strcmp(g_TwMgr->m_BarAlwaysOnTop, _Bar->m_Name)==0 )
                    sdsclear(g_TwMgr->m_BarAlwaysOnTop);
                TwSetBottomBar(_Bar);
                return 1;
            }
            else if( _stricmp(_Value, "0")==0 || _stricmp(_Value, "false")==0 )
            {
                if( sdslen(g_TwMgr->m_BarAlwaysOnBottom)>0 && strcmp(g_TwMgr->m_BarAlwaysOnBottom, _Bar->m_Name)==0 )
                    sdsclear(g_TwMgr->m_BarAlwaysOnBottom);
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
    case BAR_VISIBLE:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "1")==0 || _stricmp(_Value, "true")==0 )
            {
                TwSetBarState(_Bar, TW_STATE_SHOWN);
                return 1;
            }
            else if( _stricmp(_Value, "0")==0 || _stricmp(_Value, "false")==0 )
            {
                TwSetBarState(_Bar, TW_STATE_HIDDEN);
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
    case BAR_ICONIFIED:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "1")==0 || _stricmp(_Value, "true")==0 )
            {
                TwSetBarState(_Bar, TW_STATE_ICONIFIED);
                return 1;
            }
            else if( _stricmp(_Value, "0")==0 || _stricmp(_Value, "false")==0 )
            {
                TwSetBarState(_Bar, TW_STATE_UNICONIFIED);
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
    case BAR_COLOR_SCHEME:
        return CTwMgr_SetAttrib(g_TwMgr, MGR_COLOR_SCHEME, _Value);
    case BAR_CONTAINED:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "1")==0 || _stricmp(_Value, "true")==0 )
            {
                _Bar->m_Contained = true;
                return 1;
            }
            else if( _stricmp(_Value, "0")==0 || _stricmp(_Value, "false")==0 )
            {
                _Bar->m_Contained = false;
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
    case BAR_BUTTON_ALIGN:
        if( _Value && strlen(_Value)>0 )
        {
            if( _stricmp(_Value, "left")==0 )
            {
                _Bar->m_ButtonAlign = BUTTON_ALIGN_LEFT;
                return 1;
            }
            else if( _stricmp(_Value, "center")==0 )
            {
                _Bar->m_ButtonAlign = BUTTON_ALIGN_CENTER;
                return 1;
            }
            if( _stricmp(_Value, "right")==0 )
            {
                _Bar->m_ButtonAlign = BUTTON_ALIGN_RIGHT;
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

ERetType CTwBar_GetAttrib(const CTwBar *_Bar, int _AttribID, CDoubleArray *outDoubles, sds *outString)
{
    outDoubles->count = 0;
    sdsclear(*outString);

    switch( _AttribID )
    {
    case BAR_LABEL:
        *outString = sdscat(*outString, _Bar->m_Label);
        return RET_STRING;
    case BAR_HELP:
        *outString = sdscat(*outString, _Bar->m_Help);
        return RET_STRING;
    case BAR_COLOR:
        {
            int a, r, g, b;
            a = r = g = b = 0;
            Color32ToARGBi(_Bar->m_Color, &a, &r, &g, &b);
            tw_da_append(outDoubles, r);
            tw_da_append(outDoubles, g);
            tw_da_append(outDoubles, b);
            return RET_DOUBLE;
        }
    case BAR_ALPHA:
        {
            int a, r, g, b;
            a = r = g = b = 0;
            Color32ToARGBi(_Bar->m_Color, &a, &r, &g, &b);
            tw_da_append(outDoubles, a);
            return RET_DOUBLE;
        }
    case BAR_TEXT:
        if( _Bar->m_DarkText )
            *outString = sdscat(*outString, "dark");
        else
            *outString = sdscat(*outString, "light");
        return RET_STRING;
    case BAR_SIZE:
        tw_da_append(outDoubles, _Bar->m_Width);
        tw_da_append(outDoubles, _Bar->m_Height);
        return RET_DOUBLE;
    case BAR_POSITION:
        tw_da_append(outDoubles, _Bar->m_PosX);
        tw_da_append(outDoubles, _Bar->m_PosY);
        return RET_DOUBLE;
    case BAR_REFRESH:
        tw_da_append(outDoubles, _Bar->m_UpdatePeriod);
        return RET_DOUBLE;
    case BAR_VALUES_WIDTH:
        tw_da_append(outDoubles, _Bar->m_ValuesWidth);
        return RET_DOUBLE;
    case BAR_FONT_SIZE:
        return CTwMgr_GetAttrib(g_TwMgr, MGR_FONT_SIZE, outDoubles, outString);
    case BAR_FONT_STYLE:
        return CTwMgr_GetAttrib(g_TwMgr, MGR_FONT_STYLE, outDoubles, outString);
    case BAR_ICON_POS:
        return CTwMgr_GetAttrib(g_TwMgr, MGR_ICON_POS, outDoubles, outString);
    case BAR_ICON_ALIGN:
        return CTwMgr_GetAttrib(g_TwMgr, MGR_ICON_ALIGN, outDoubles, outString);
    case BAR_ICON_MARGIN:
        return CTwMgr_GetAttrib(g_TwMgr, MGR_ICON_MARGIN, outDoubles, outString);
    case BAR_RESIZABLE:
        tw_da_append(outDoubles, _Bar->m_Resizable);
        return RET_DOUBLE;
    case BAR_MOVABLE:
        tw_da_append(outDoubles, _Bar->m_Movable);
        return RET_DOUBLE;
    case BAR_ICONIFIABLE:
        tw_da_append(outDoubles, _Bar->m_Iconifiable);
        return RET_DOUBLE;
    case BAR_FONT_RESIZABLE:
        return CTwMgr_GetAttrib(g_TwMgr, MGR_FONT_RESIZABLE, outDoubles, outString);
    case BAR_ALWAYS_TOP:
        tw_da_append(outDoubles,  strcmp(g_TwMgr->m_BarAlwaysOnTop, _Bar->m_Name)==0 );
        return RET_DOUBLE;
    case BAR_ALWAYS_BOTTOM:
        tw_da_append(outDoubles,  strcmp(g_TwMgr->m_BarAlwaysOnBottom, _Bar->m_Name)==0 );
        return RET_DOUBLE;
    case BAR_VISIBLE:
        tw_da_append(outDoubles, _Bar->m_Visible);
        return RET_DOUBLE;
    case BAR_ICONIFIED:
        tw_da_append(outDoubles, _Bar->m_IsMinimized);
        return RET_DOUBLE;
    case BAR_COLOR_SCHEME:
        return CTwMgr_GetAttrib(g_TwMgr, MGR_COLOR_SCHEME, outDoubles, outString);
    case BAR_CONTAINED:
        tw_da_append(outDoubles, _Bar->m_Contained);
        return RET_DOUBLE;
    case BAR_BUTTON_ALIGN:
        if( _Bar->m_ButtonAlign==BUTTON_ALIGN_LEFT )
            *outString = sdscat(*outString, "left");
        else if( _Bar->m_ButtonAlign==BUTTON_ALIGN_CENTER )
            *outString = sdscat(*outString, "center");
        else
            *outString = sdscat(*outString, "right");
        return RET_STRING;
    default:
        CTwMgr_SetLastError(g_TwMgr, g_ErrUnknownAttrib);
        return RET_ERROR;
    }
}

//  ---------------------------------------------------------------------------

void CTwBar_NotUpToDate(CTwBar *_Bar)
{
    _Bar->m_UpToDate = false;
}

//  ---------------------------------------------------------------------------

void CTwBar_UpdateColors(CTwBar *_Bar)
{
    float a, r, g, b, h, l, s;
    Color32ToARGBf(_Bar->m_Color, &a, &r, &g, &b);
    ColorRGBToHLSf(r, g, b, &h, &l, &s);
    bool lightText = !_Bar->m_DarkText;

    // Colors independant of _Bar->m_Color

    // Highlighted line background ramp
    _Bar->m_ColHighBg0 = lightText ? Color32FromARGBf(0.4f, 0.9f, 0.9f, 0.9f) : Color32FromARGBf(0.4f, 1.0f, 1.0f, 1.0f);
    _Bar->m_ColHighBg1 = lightText ? Color32FromARGBf(0.4f, 0.2f, 0.2f, 0.2f) : Color32FromARGBf(0.1f, 0.7f, 0.7f, 0.7f);

    // Text colors & background
    _Bar->m_ColLabelText = lightText ? COLOR32_WHITE : COLOR32_BLACK;
    _Bar->m_ColStructText = lightText ? 0xffefef00 : 0xff303000;

    _Bar->m_ColValText = lightText ? 0xffc7d7ff : 0xff000080;
    _Bar->m_ColValTextRO = lightText ? 0xffb7b7b7 : 0xff505050;
    _Bar->m_ColValMin = lightText ? 0xff9797ff : 0xff0000f0;
    _Bar->m_ColValMax = _Bar->m_ColValMin;
    _Bar->m_ColValTextNE = lightText ? 0xff97f797 : 0xff004000;

    _Bar->m_ColValBg = lightText ? Color32FromARGBf(0.2f+0.3f*a, 0.1f, 0.1f, 0.1f) : Color32FromARGBf(0.2f+0.3f*a, 1, 1, 1);
    _Bar->m_ColStructBg = lightText ? Color32FromARGBf(0.4f*a, 0, 0, 0) : Color32FromARGBf(0.4f*a, 1, 1, 1);

    _Bar->m_ColLine = lightText ? Color32FromARGBf(0.6f, 1, 1, 1) : Color32FromARGBf(0.6f, 0.3f, 0.3f, 0.3f);
    _Bar->m_ColLineShadow = lightText ? Color32FromARGBf(0.6f, 0, 0, 0) : Color32FromARGBf(0.6f, 0, 0, 0);
    _Bar->m_ColUnderline = lightText ? 0xffd0d0d0 : 0xff202000;

    _Bar->m_ColGrpBg = lightText ? Color32FromARGBf(0.1f+0.25f*a, 1, 1, 1) : Color32FromARGBf(0.1f+0.05f*a, 0, 0, 0);
    _Bar->m_ColGrpText = lightText ? 0xffffff80 : 0xff000000;

    _Bar->m_ColShortcutText = lightText ? 0xffffb060 : 0xff802000;
    _Bar->m_ColShortcutBg = lightText ? Color32FromARGBf(0.4f*a, 0.2f, 0.2f, 0.2f) : Color32FromARGBf(0.4f*a, 0.8f, 0.8f, 0.8f);
    _Bar->m_ColInfoText = Color32FromARGBf(1.0f, 0.5f, 0.5f, 0.5f);

    _Bar->m_ColRoto = lightText ? Color32FromARGBf(0.8f, 0.85f, 0.85f, 0.85f) : Color32FromARGBf(0.8f, 0.1f, 0.1f, 0.1f);
    _Bar->m_ColRotoVal = Color32FromARGBf(1, 1.0f, 0.2f, 0.2f);
    _Bar->m_ColRotoBound = lightText ? Color32FromARGBf(0.8f, 0.6f, 0.6f, 0.6f) : Color32FromARGBf(0.8f, 0.3f, 0.3f, 0.3f);

    _Bar->m_ColEditText = lightText ? COLOR32_WHITE : COLOR32_BLACK;
    _Bar->m_ColEditBg = lightText ? 0xff575757 : 0xffc7c7c7; // must be opaque
    _Bar->m_ColEditSelText = lightText ? COLOR32_BLACK : COLOR32_WHITE;
    _Bar->m_ColEditSelBg = lightText ? 0xffc7c7c7 : 0xff575757;

    // Colors dependant of m_Colors
    
    // Bar background
    ColorHLSToRGBf(h, l, s, &r, &g, &b);
    _Bar->m_ColBg = Color32FromARGBf(a, r, g, b);
    ColorHLSToRGBf(h, l-0.05f, s, &r, &g, &b);
    _Bar->m_ColBg1 = Color32FromARGBf(a, r, g, b);
    ColorHLSToRGBf(h, l-0.1f, s, &r, &g, &b);
    _Bar->m_ColBg2 = Color32FromARGBf(a, r, g, b);
    
    ColorHLSToRGBf(h, l-0.15f, s, &r, &g, &b);
    _Bar->m_ColTitleBg = Color32FromARGBf(a+0.9f, r, g, b);
    _Bar->m_ColTitleText = lightText ? COLOR32_WHITE : COLOR32_BLACK;
    _Bar->m_ColTitleShadow = lightText ? 0x40000000 : 0x00000000;
    ColorHLSToRGBf(h, l-0.25f, s, &r, &g, &b);
    _Bar->m_ColTitleHighBg = Color32FromARGBf(a+0.8f, r, g, b);
    ColorHLSToRGBf(h, l-0.3f, s, &r, &g, &b);
    _Bar->m_ColTitleUnactiveBg = Color32FromARGBf(a+0.2f, r, g, b);

    ColorHLSToRGBf(h, l-0.2f, s, &r, &g, &b);
    _Bar->m_ColHierBg = Color32FromARGBf(a, r, g, b);

    ColorHLSToRGBf(h, l+0.1f, s, &r, &g, &b);
    _Bar->m_ColBtn = Color32FromARGBf(0.2f+0.4f*a, r, g, b);
    ColorHLSToRGBf(h, l-0.35f, s, &r, &g, &b);
    _Bar->m_ColHighBtn = Color32FromARGBf(0.4f+0.4f*a, r, g, b);
    ColorHLSToRGBf(h, l-0.25f, s, &r, &g, &b);
    _Bar->m_ColFold = Color32FromARGBf(0.1f+0.4f*a, r, g, b);
    ColorHLSToRGBf(h, l-0.35f, s, &r, &g, &b);
    _Bar->m_ColHighFold = Color32FromARGBf(0.3f+0.4f*a, r, g, b);

    ColorHLSToRGBf(h, 0.75f, s, &r, &g, &b);
    _Bar->m_ColHelpBg = Color32FromARGBf(0.2f, 1, 1, 1);
    _Bar->m_ColHelpText = lightText ? Color32FromARGBf(1, 0.2f, 1.0f, 0.2f) : Color32FromARGBf(1, 0, 0.4f, 0);
    _Bar->m_ColSeparator = _Bar->m_ColValTextRO;
    _Bar->m_ColStaticText = _Bar->m_ColHelpText;
}

/*
void CTwBar::UpdateColors()
{
    float a, r, g, b, h, l, s;
    Color32ToARGBf(m_Color, &a, &r, &g, &b);
    ColorRGBToHLSf(r, g, b, &h, &l, &s);
    bool lightText = !m_DarkText; // (l<=0.45f);
    l = 0.2f + 0.6f*l;
    
    ColorHLSToRGBf(h, l, s, &r, &g, &b);
    m_ColBg = Color32FromARGBf(a, r, g, b);
    ColorHLSToRGBf(h, l-0.1f, s, &r, &g, &b);
    m_ColBg1 = Color32FromARGBf(a, r, g, b);
    ColorHLSToRGBf(h, l-0.2f, s, &r, &g, &b);
    m_ColBg2 = Color32FromARGBf(a, r, g, b);

    ColorHLSToRGBf(h, l+0.1f, s, &r, &g, &b);
    m_ColHighBg = Color32FromARGBf(0.4f, r, g, b);
    //m_ColHighBg = Color32FromARGBf(a, 0.95f, 0.95f, 0.2f);
    
    m_ColLabelText = lightText ? COLOR32_WHITE : COLOR32_BLACK;
    m_ColStructText = lightText ? 0xffefef00 : 0xff505000;

    m_ColValText = lightText ? 0xffb7b7ff : 0xff000080;
    m_ColValTextRO = lightText ? 0xffb7b7b7 : 0xff505050;
    m_ColValMin = lightText ? 0xff9797ff : 0xff0000f0;
    m_ColValMax = m_ColValMin;
    m_ColValTextNE = lightText ? 0xff97f797 : 0xff006000;

    ColorHLSToRGBf(h, lightText ? (min(l+0.2f, 0.3f)) : (max(l-0.2f, 0.6f)), s, &r, &g, &b);
    m_ColValBg = Color32FromARGBf(0.4f*a, 0, 0, 0);
    m_ColStructBg = Color32FromARGBf(0.4f*a, 0, 0, 0);

    ColorHLSToRGBf(h, 0.4f, s, &r, &g, &b);
    m_ColTitleBg = Color32FromARGBf(a+0.4f, r, g, b);
    m_ColTitleText = lightText ? COLOR32_WHITE : COLOR32_BLACK;
    m_ColTitleShadow = lightText ? 0x80000000 : 0x80ffffff;
    ColorHLSToRGBf(h, 0.3f, s, &r, &g, &b);
    m_ColTitleHighBg = Color32FromARGBf(a+0.4f, r, g, b);
    ColorHLSToRGBf(h, 0.4f, s, &r, &g, &b);
    m_ColTitleUnactiveBg = Color32FromARGBf(a+0.2f, r, g, b);

    ColorHLSToRGBf(h, 0.8f, s, &r, &g, &b);
    m_ColLine = Color32FromARGBf(0.6f, r, g, b); // 0xfff0f0f0;
    m_ColLineShadow = Color32FromARGBf(0.6f, 0, 0, 0); //COLOR32_BLACK;
    m_ColUnderline = lightText ? 0xffd0d0d0 : 0xff202000;
    ColorHLSToRGBf(h, 0.7f, s, &r, &g, &b);
    m_ColBtn = Color32FromARGBf(0.6f, r, g, b);
    ColorHLSToRGBf(h, 0.4f, s, &r, &g, &b);
    m_ColHighBtn = Color32FromARGBf(0.6f, r, g, b);
    ColorHLSToRGBf(h, 0.6f, s, &r, &g, &b);
    m_ColFold = Color32FromARGBf(0.3f*a, r, g, b);
    ColorHLSToRGBf(h, 0.4f, s, &r, &g, &b);
    m_ColHighFold = Color32FromARGBf(0.3f, r, g, b);

    ColorHLSToRGBf(h, lightText ? l+0.2f : l-0.2f, s, &r, &g, &b);
    m_ColGrpBg = Color32FromARGBf(0.5f*a, r, g, b);
    m_ColGrpText = lightText ? 0xffffff80 : 0xff404000;

    ColorHLSToRGBf(h, 0.75f, s, &r, &g, &b);
    m_ColHelpBg = Color32FromARGBf(a, r, g, b);
    m_ColHelpText = Color32FromARGBf(1, 0, 0.4f, 0);

    ColorHLSToRGBf(h, 0.45f, s, &r, &g, &b);
    m_ColHierBg = Color32FromARGBf(0.75f*a, r, g, b);

    m_ColShortcutText = lightText ? 0xffff8040 : 0xff802000;  //0xfff0f0f0;
    m_ColShortcutBg = Color32FromARGBf(0.4f*a, 0.2f, 0.2f, 0.2f);
    m_ColInfoText = Color32FromARGBf(1.0f, 0.7f, 0.7f, 0.7f);

    m_ColRoto = Color32FromARGBf(1, 0.75f, 0.75f, 0.75f);
    m_ColRotoVal = Color32FromARGBf(1, 1.0f, 0.2f, 0.2f);
    m_ColRotoBound = Color32FromARGBf(1, 0.4f, 0.4f, 0.4f);

    m_ColEditText = lightText ? COLOR32_WHITE : COLOR32_BLACK;
    m_ColEditBg = lightText ? 0xb7575757 : 0xb7c7c7c7;
    m_ColEditSelText = lightText ? COLOR32_BLACK : COLOR32_WHITE;
    m_ColEditSelBg = lightText ? 0xffc7c7c7 : 0xff575757;

    m_ColSeparator = m_ColValTextRO;
    m_ColStaticText = m_ColHelpText;
}
*/

//  ---------------------------------------------------------------------------

void CTwVarGroup_Init(CTwVarGroup *_Grp)
{
    CTwVar_InitBase(&_Grp->m_Base, TW_VARKIND_GROUP);
    _Grp->m_Vars.items = NULL;
    _Grp->m_Vars.count = 0;
    _Grp->m_Vars.capacity = 0;
    _Grp->m_Open = false;
    _Grp->m_StructType = TW_TYPE_UNDEF;
    _Grp->m_SummaryCallback = NULL;
    _Grp->m_SummaryClientData = NULL;
    _Grp->m_StructValuePtr = NULL;
}

void CTwVarGroup_Free(CTwVarGroup *_Grp)
{
    for( size_t i=0; i<_Grp->m_Vars.count; ++i )
        if( _Grp->m_Vars.items[i]!=NULL )
        {
            CTwVar_Delete(_Grp->m_Vars.items[i]);
            _Grp->m_Vars.items[i] = NULL;
        }
    tw_da_free(&_Grp->m_Vars);
    CTwVar_FreeBase(&_Grp->m_Base);
}

CTwVarGroup *CTwVarGroup_New(void)
{
    CTwVarGroup *Grp = (CTwVarGroup *)malloc(sizeof(CTwVarGroup));
    CTwVarGroup_Init(Grp);
    return Grp;
}

//  ---------------------------------------------------------------------------

static inline int IncrBtnWidth(int _CharHeight) 
{ 
    return ((2*_CharHeight)/3+2)&0xfffe; // force even value 
}

//  ---------------------------------------------------------------------------

void CTwBar_BrowseHierarchy(CTwBar *_Bar, int *_CurrLine, int _CurrLevel, const CTwVar *_Var, int _First, int _Last)
{
    assert(_Var!=NULL);
    if( !_Var->m_IsRoot )
    {
        if( (*_CurrLine)>=_First && (*_CurrLine)<=_Last )
        {
            CHierTag Tag;
            Tag.m_Level = _CurrLevel;
            Tag.m_Var = (CTwVar *)(_Var);
            Tag.m_Closing = false;
            tw_da_append(&_Bar->m_HierTags, Tag);
        }
        *_CurrLine += 1;
    }
    else
    {
        *_CurrLine = 0;
        _CurrLevel = -1;
        tw_da_resize(&_Bar->m_HierTags, 0);
    }

    if( CTwVar_IsGroup(_Var) )
    {
        const CTwVarGroup *Grp = (const CTwVarGroup *)_Var;
        if( Grp->m_Open )
            for( size_t i=0; i<Grp->m_Vars.count; ++i )
                if( Grp->m_Vars.items[i]->m_Visible )
                    CTwBar_BrowseHierarchy(_Bar, _CurrLine, _CurrLevel+1, Grp->m_Vars.items[i], _First, _Last);
        if( _Bar->m_HierTags.count>0 )
            _Bar->m_HierTags.items[_Bar->m_HierTags.count-1].m_Closing = true;
    }
}

//  ---------------------------------------------------------------------------

void CTwBar_ListLabels(CTwBar *_Bar, CSdsArray *_Labels, CColor32Array *_Colors, CColor32Array *_BgColors, bool *_HasBgColors, const CTexFont *_Font, int _AtomWidthMax, int _GroupWidthMax)
{
    const int NbEtc = 2;
    int Len, i, x, Etc, s;
    const unsigned char *Text;
    unsigned char ch;
    int WidthMax;
    
    int Space = _Font->m_CharWidth[(int)' '];
    int LevelSpace = max(_Font->m_CharHeight-6, 4); // space used by DrawHierHandles

    int nh = (int)_Bar->m_HierTags.count;
    for( int h=0; h<nh; ++h )
    {
        Len = (int)sdslen(_Bar->m_HierTags.items[h].m_Var->m_Label);
        if( Len>0 )
            Text = (const unsigned char *)(_Bar->m_HierTags.items[h].m_Var->m_Label);
        else
        {
            Text = (const unsigned char *)(_Bar->m_HierTags.items[h].m_Var->m_Name);
            Len = (int)sdslen(_Bar->m_HierTags.items[h].m_Var->m_Name);
        }
        x = 0;
        Etc = 0;
        tw_da_append(_Labels, sdsempty());  // add a new text line
        if( !CTwVar_IsGroup(_Bar->m_HierTags.items[h].m_Var) && ((const CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var)->m_Type==TW_TYPE_BUTTON && ((const CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var)->m_ReadOnly && ((const CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var)->m_Val.m_Button.m_Callback!=NULL )
            tw_da_append(_Colors, _Bar->m_ColValTextRO); // special case for read-only buttons
        else
            tw_da_append(_Colors, _Bar->m_HierTags.items[h].m_Var->m_ColorPtr!=NULL ? *(_Bar->m_HierTags.items[h].m_Var->m_ColorPtr) : COLOR32_WHITE);
        color32 bg = _Bar->m_HierTags.items[h].m_Var->m_BgColorPtr!=NULL ? *(_Bar->m_HierTags.items[h].m_Var->m_BgColorPtr) : 0;
        tw_da_append(_BgColors, bg);
        if( _HasBgColors!=NULL && bg!=0 )
            *_HasBgColors = true;
        bool IsCustom = CTwVar_IsCustom(_Bar->m_HierTags.items[h].m_Var);
        if( !IsCustom )
        {
            sds *CurrentLabel = &_Labels->items[_Labels->count-1];
            if( CTwVar_IsGroup(_Bar->m_HierTags.items[h].m_Var) && ((const CTwVarGroup *)_Bar->m_HierTags.items[h].m_Var)->m_SummaryCallback==NULL )
                WidthMax = _GroupWidthMax;
            else if( !CTwVar_IsGroup(_Bar->m_HierTags.items[h].m_Var) && ((const CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var)->m_Type==TW_TYPE_BUTTON )
            {
                if( ((const CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var)->m_Val.m_Button.m_Callback==NULL )
                    WidthMax = _GroupWidthMax; // separator/info line: label may use the full row width
                else
                    // Interactive button: label is clipped to the normal atom label
                    // column, like every other variable type, since the button itself
                    // now fills the value column instead of squeezing next to the label.
                    WidthMax = _AtomWidthMax;
            }
            //else if( _Bar->m_HighlightedLine==h && _Bar->m_DrawRotoBtn )
            //  WidthMax = _AtomWidthMax - IncrBtnWidth(_Bar->m_Font->m_CharHeight);
            else
                WidthMax = _AtomWidthMax;
            if( Space>0 )
                for( s=0; s<_Bar->m_HierTags.items[h].m_Level*LevelSpace; s+=Space )
                {
                    char sp = ' ';
                    *CurrentLabel = sdscatlen(*CurrentLabel, &sp, 1);
                    x += Space;
                }
            if( x+(NbEtc+2)*_Font->m_CharWidth[(int)'.']<WidthMax || _Bar->m_HierTags.items[h].m_Var->m_DontClip)
                for( i=0; i<Len; ++i )
                {
                    ch = (Etc==0) ? Text[i] : '.';
                    *CurrentLabel = sdscatlen(*CurrentLabel, &ch, 1);
                    x += _Font->m_CharWidth[(int)ch];
                    if( Etc>0 )
                    {
                        ++Etc;
                        if( Etc>NbEtc )
                            break;
                    }
                    else if( i<Len-2 && x+(NbEtc+2)*_Font->m_CharWidth[(int)'.']>=WidthMax && !(_Bar->m_HierTags.items[h].m_Var->m_DontClip))
                        Etc = 1;
                }       
        }
    }
}

//  ---------------------------------------------------------------------------

void CTwBar_ListValues(CTwBar *_Bar, CSdsArray *_Values, CColor32Array *_Colors, CColor32Array *_BgColors, const CTexFont *_Font, int _WidthMax)
{
    unsigned int fpuState = TwFPU_Save(); // force fpu precision

    const int NbEtc = 2;
    const CTwVarAtom *Atom = NULL;
    sds ValStr = sdsempty();
    int Len, i, x, Etc;
    const unsigned char *Text;
    unsigned char ch;
    bool ReadOnly;
    bool IsMax;
    bool IsMin;
    bool IsROText;
    bool HasBgColor;
    bool AcceptEdit;
    size_t SummaryMaxLength = max(_WidthMax/_Font->m_CharWidth[(int)'I'], 4);
    static CCharArray Summary = {0};
    tw_da_resize(&Summary, SummaryMaxLength+32);

    int nh = (int)_Bar->m_HierTags.count;
    for( int h=0; h<nh; ++h )
        if( !CTwVar_IsGroup(_Bar->m_HierTags.items[h].m_Var) || _Bar->m_IsHelpBar
            || (CTwVar_IsGroup(_Bar->m_HierTags.items[h].m_Var) && ((const CTwVarGroup *)_Bar->m_HierTags.items[h].m_Var)->m_SummaryCallback!=NULL) )
        {
            ReadOnly = true;
            IsMax = false;
            IsMin = false;
            IsROText = false;
            HasBgColor = true;
            AcceptEdit = false;
            if( !CTwVar_IsGroup(_Bar->m_HierTags.items[h].m_Var) )
            {
                Atom = (const CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var;
                CTwVarAtom_ValueToString(Atom, &ValStr);
                if( !_Bar->m_IsHelpBar || (Atom->m_Type==TW_TYPE_SHORTCUT && (Atom->m_Val.m_Shortcut.m_Incr[0]>0 || Atom->m_Val.m_Shortcut.m_Decr[0]>0)) )
                    ReadOnly = Atom->m_ReadOnly;
                if( !Atom->m_NoSlider )
                {
                    double v, vmin, vmax;
                    v = CTwVarAtom_ValueToDouble(Atom);
                    CTwVarAtom_MinMaxStepToDouble(Atom, &vmin, &vmax, NULL);
                    IsMax = (v>=vmax);
                    IsMin = (v<=vmin);
                }
                if( Atom->m_Type==TW_TYPE_BOOLCPP || Atom->m_Type==TW_TYPE_BOOL8 || Atom->m_Type==TW_TYPE_BOOL16 || Atom->m_Type==TW_TYPE_BOOL32 )
                {
                    if (strcmp(ValStr, "1")==0)
                        ValStr = sdscpy(ValStr, "\x7f"); // check sign
                    else if (strcmp(ValStr, "0")==0)
                        ValStr = sdscpy(ValStr, " -"); //"\x97"; // uncheck sign
                }
                if( Atom->m_Type==TW_TYPE_CDSTRING && Atom->m_SetCallback==NULL && g_TwMgr->m_CopyCDStringToClient==NULL )
                    IsROText = true;
                if( Atom->m_Type==TW_TYPE_HELP_ATOM || Atom->m_Type==TW_TYPE_HELP_GRP || Atom->m_Type==TW_TYPE_BUTTON || IsCustomType(Atom->m_Type) ) // (Atom->m_Type>=TW_TYPE_CUSTOM_BASE && Atom->m_Type<TW_TYPE_CUSTOM_BASE+(int)g_TwMgr->m_Customs.size()) )
                    HasBgColor = false;
                AcceptEdit = CTwBar_EditInPlaceAcceptVar(_Bar, Atom) || (Atom->m_Type==TW_TYPE_SHORTCUT);
            }
            else if(CTwVar_IsGroup(_Bar->m_HierTags.items[h].m_Var) && ((const CTwVarGroup *)_Bar->m_HierTags.items[h].m_Var)->m_SummaryCallback!=NULL)
            {
                const CTwVarGroup *Grp = (const CTwVarGroup *)_Bar->m_HierTags.items[h].m_Var;
                // force internal value update
                for( size_t v=0; v<Grp->m_Vars.count; v++ )
                    if( Grp->m_Vars.items[v]!=NULL && !CTwVar_IsGroup(Grp->m_Vars.items[v]) && Grp->m_Vars.items[v]->m_Visible )
                        CTwVarAtom_ValueToDouble((CTwVarAtom *)Grp->m_Vars.items[v]);

                Summary.items[0] = '\0';
                if( Grp->m_SummaryCallback==CStruct_DefaultSummary )
                    Grp->m_SummaryCallback(&Summary.items[0], SummaryMaxLength, Grp, Grp->m_SummaryClientData);
                else
                    Grp->m_SummaryCallback(&Summary.items[0], SummaryMaxLength, Grp->m_StructValuePtr, Grp->m_SummaryClientData);
                ValStr = sdscpy(ValStr, (const char *)(&Summary.items[0]));
            }
            else
            {
                sdsclear(ValStr);    // is a group in the help bar
                HasBgColor = false;
            }
            Len = (int)sdslen(ValStr);
            Text = (const unsigned char *)(ValStr);
            x = 0;
            Etc = 0;
            tw_da_append(_Values, sdsempty());  // add a new text line
            if( ReadOnly || (IsMin && IsMax) || IsROText )
                tw_da_append(_Colors, _Bar->m_ColValTextRO);
            else if( IsMin )
                tw_da_append(_Colors, _Bar->m_ColValMin);
            else if( IsMax )
                tw_da_append(_Colors, _Bar->m_ColValMax);
            else if( !AcceptEdit )
                tw_da_append(_Colors, _Bar->m_ColValTextNE);
            else
                tw_da_append(_Colors, _Bar->m_ColValText);
            if( !HasBgColor )
                tw_da_append(_BgColors, (color32)0x00000000);
            else if( CTwVar_IsGroup(_Bar->m_HierTags.items[h].m_Var) )
            {
                const CTwVarGroup *Grp = (const CTwVarGroup *)_Bar->m_HierTags.items[h].m_Var;
                // if typecolor set bgcolor
                if( Grp->m_SummaryCallback==CColorExt_SummaryCB )
                    tw_da_append(_BgColors, (color32)0xff000000);
                else
                    tw_da_append(_BgColors, _Bar->m_ColStructBg);
            }
            else
                tw_da_append(_BgColors, _Bar->m_ColValBg);

            sds *CurrentValue = &_Values->items[_Values->count-1];
            int wmax = _WidthMax;
            if( _Bar->m_HighlightedLine==h && _Bar->m_DrawRotoBtn )
                wmax -= 3*IncrBtnWidth(_Bar->m_Font->m_CharHeight);
            else if( _Bar->m_HighlightedLine==h && _Bar->m_DrawIncrDecrBtn )
                wmax -= 2*IncrBtnWidth(_Bar->m_Font->m_CharHeight);
            else if( _Bar->m_HighlightedLine==h && _Bar->m_DrawListBtn )
                wmax -= 1*IncrBtnWidth(_Bar->m_Font->m_CharHeight);
            else if( _Bar->m_HighlightedLine==h && _Bar->m_DrawBoolBtn )
                wmax -= 1*IncrBtnWidth(_Bar->m_Font->m_CharHeight);
            for( i=0; i<Len; ++i )
            {
                ch = (Etc==0) ? Text[i] : '.';
                *CurrentValue = sdscatlen(*CurrentValue, &ch, 1);
                x += _Font->m_CharWidth[(int)ch];
                if( Etc>0 )
                {
                    ++Etc;
                    if( Etc>NbEtc )
                        break;
                }
                else if( i<Len-2 && x+(NbEtc+2)*(_Font->m_CharWidth[(int)'.'])>=wmax )
                    Etc = 1;
            }
        }
        else
        {
            tw_da_append(_Values, sdsempty());  // add a new empty line
            tw_da_append(_Colors, (color32)COLOR32_BLACK);
            tw_da_append(_BgColors, (color32)0x00000000);
        }
    sdsfree(ValStr);
    TwFPU_Restore(fpuState);
}

//  ---------------------------------------------------------------------------

int CTwBar_ComputeLabelsWidth(CTwBar *_Bar, const CTexFont *_Font)
{
    int Len, i, x, s;
    const unsigned char *Text;
    int LabelsWidth = 0;    
    int Space = _Font->m_CharWidth[(int)' '];
    int LevelSpace = max(_Font->m_CharHeight-6, 4); // space used by DrawHierHandles

    int nh = (int)_Bar->m_HierTags.count;
    for( int h=0; h<nh; ++h )
    {
        Len = (int)sdslen(_Bar->m_HierTags.items[h].m_Var->m_Label);
        if( Len>0 )
            Text = (const unsigned char *)(_Bar->m_HierTags.items[h].m_Var->m_Label);
        else
        {
            Text = (const unsigned char *)(_Bar->m_HierTags.items[h].m_Var->m_Name);
            Len = (int)sdslen(_Bar->m_HierTags.items[h].m_Var->m_Name);
        }
        x = 0;
        bool IsCustom = CTwVar_IsCustom(_Bar->m_HierTags.items[h].m_Var);
        if( !IsCustom )
        {
            if( Space>0 )
                for( s=0; s<_Bar->m_HierTags.items[h].m_Level*LevelSpace; s+=Space )
                    x += Space;
            for( i=0; i<Len; ++i )
                x += _Font->m_CharWidth[(int)Text[i]];
            x += 3*Space; // add little margin
        }
        if (x > LabelsWidth)
            LabelsWidth = x;
    }

    return LabelsWidth;
}

int CTwBar_ComputeValuesWidth(CTwBar *_Bar, const CTexFont *_Font)
{
    unsigned int fpuState = TwFPU_Save(); // force fpu precision

    const CTwVarAtom *Atom = NULL;
    sds ValStr = sdsempty();
    int Len, i, x;
    int Space = _Font->m_CharWidth[(int)' '];
    const unsigned char *Text;
    int ValuesWidth = 0;

    int nh = (int)_Bar->m_HierTags.count;
    for( int h=0; h<nh; ++h )
        if( !CTwVar_IsGroup(_Bar->m_HierTags.items[h].m_Var) )
        {
            Atom = (const CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var;
            CTwVarAtom_ValueToString(Atom, &ValStr);

            Len = (int)sdslen(ValStr);
            Text = (const unsigned char *)(ValStr);
            x = 0;
            for( i=0; i<Len; ++i )
                x += _Font->m_CharWidth[(int)Text[i]];
            x += 2*Space; // add little margin
            if (x > ValuesWidth)
                ValuesWidth = x;
        }

    sdsfree(ValStr);
    TwFPU_Restore(fpuState);
    return ValuesWidth;
}

//  ---------------------------------------------------------------------------

static int ClampText(sds *_Text, const CTexFont *_Font, int _WidthMax)
{
    int Len = (int)sdslen(*_Text);
    unsigned char ch;
    int Width = 0;
    int i;
    for( i=0; i<Len; ++i )
    {
        ch = (*_Text)[i];
        if( i<Len-1 && Width+_Font->m_CharWidth[(int)'.']>=_WidthMax )
            break;
        Width += _Font->m_CharWidth[ch];
    }
    if( i<Len ) // clamp
    {
        size_t curLen = sdslen(*_Text);
        size_t newLen = (size_t)(i+2);
        if( newLen>curLen )
            *_Text = sdsMakeRoomFor(*_Text, newLen-curLen);
        (*_Text)[i+0] = '.';
        (*_Text)[i+1] = '.';
        (*_Text)[newLen] = '\0';
        sdssetlen(*_Text, newLen);
        Width += 2*_Font->m_CharWidth[(int)'.'];
    }
    return Width;
}

//  ---------------------------------------------------------------------------

CCustomRecord *CTwBar_CustomMap_Find(CTwBar *_Bar, CStructProxy *_Key)
{
    for( size_t i=0; i<_Bar->m_CustomRecords.count; ++i )
        if( _Bar->m_CustomRecords.items[i].m_Key==_Key )
            return &_Bar->m_CustomRecords.items[i].m_Value;
    return NULL;
}

//  ---------------------------------------------------------------------------

void CTwBar_Update(CTwBar *_Bar)
{
    assert(_Bar->m_UpToDate==false);
    assert(_Bar->m_Font);
    ITwGraph *Gr = g_TwMgr->m_Graph;

    if( g_TwMgr->m_WndWidth<=0 || g_TwMgr->m_WndHeight<=0 )
        return; // graphic window is not ready

    bool DoEndDraw = false;
    if( !Gr->IsDrawing(Gr) )
    {
        Gr->BeginDraw(Gr, g_TwMgr->m_WndWidth, g_TwMgr->m_WndHeight);
        DoEndDraw = true;
    }

    bool ValuesWidthFit = false;
    if( _Bar->m_ValuesWidth==VALUES_WIDTH_FIT )
    {
        ValuesWidthFit = true;
        _Bar->m_ValuesWidth = 0;
    }
    int PrevPosY = _Bar->m_PosY;
    int vpx, vpy, vpw, vph;
    vpx = 0;
    vpy = 0;
    vpw = g_TwMgr->m_WndWidth;
    vph = g_TwMgr->m_WndHeight;
    if( !_Bar->m_IsMinimized && vpw>0 && vph>0 )
    {
        bool Modif = false;
        if( _Bar->m_Resizable )
        {
            if( _Bar->m_Width>vpw && _Bar->m_Contained )
            {
                _Bar->m_Width = vpw;
                Modif = true;
            }
            if( _Bar->m_Width<8*_Bar->m_Font->m_CharHeight )
            {
                _Bar->m_Width = 8*_Bar->m_Font->m_CharHeight;
                Modif = true;
            }
            if( _Bar->m_Height>vph && _Bar->m_Contained )
            {
                _Bar->m_Height = vph;
                Modif = true;
            }
            if( _Bar->m_Height<5*_Bar->m_Font->m_CharHeight )
            {
                _Bar->m_Height = 5*_Bar->m_Font->m_CharHeight;
                Modif = true;
            }
        }
        if( _Bar->m_Movable && _Bar->m_Contained )
        {
            if( _Bar->m_PosX+_Bar->m_Width>vpx+vpw )
                _Bar->m_PosX = vpx+vpw-_Bar->m_Width;
            if( _Bar->m_PosX<vpx )
                _Bar->m_PosX = vpx;
            if( _Bar->m_PosY+_Bar->m_Height>vpy+vph )
                _Bar->m_PosY = vpy+vph-_Bar->m_Height;
            if( _Bar->m_PosY<vpy )
                _Bar->m_PosY = vpy;
        }
        _Bar->m_ScrollY0 += _Bar->m_PosY-PrevPosY;
        _Bar->m_ScrollY1 += _Bar->m_PosY-PrevPosY;
        if( _Bar->m_ValuesWidth<2*_Bar->m_Font->m_CharHeight )
        {
            _Bar->m_ValuesWidth = 2*_Bar->m_Font->m_CharHeight;
            Modif = true;
        }
        if( _Bar->m_ValuesWidth>_Bar->m_Width-4*_Bar->m_Font->m_CharHeight )
        {
            _Bar->m_ValuesWidth = _Bar->m_Width-4*_Bar->m_Font->m_CharHeight;
            Modif = true;
        }
        if (ValuesWidthFit)
            Modif = true;
        if( Modif && _Bar->m_IsHelpBar )
        {
            g_TwMgr->m_HelpBarNotUpToDate = true;
            g_TwMgr->m_KeyPressedBuildText = true;
            g_TwMgr->m_InfoBuildText = true;
        }
    }

    CTwBar_UpdateColors(_Bar);

    // update geometry relatively to (_Bar->m_PosX, _Bar->m_PosY)
    if( !_Bar->m_IsPopupList )
    {
        //_Bar->m_VarX0 = 2*_Bar->m_Font->m_CharHeight+_Bar->m_Sep;
        _Bar->m_VarX0 = _Bar->m_Font->m_CharHeight+_Bar->m_Sep;
        //_Bar->m_VarX2 = _Bar->m_Width - 4;
        _Bar->m_VarX2 = _Bar->m_Width - _Bar->m_Font->m_CharHeight - _Bar->m_Sep-2;
        _Bar->m_VarX1 = _Bar->m_VarX2 - _Bar->m_ValuesWidth;
    }
    else
    {
        //_Bar->m_VarX0 = _Bar->m_Font->m_CharHeight+6+_Bar->m_Sep;
        _Bar->m_VarX0 = 2;
        //_Bar->m_VarX2 = _Bar->m_Width - 4;
        _Bar->m_VarX2 = _Bar->m_Width - _Bar->m_Font->m_CharHeight - _Bar->m_Sep-2;
        _Bar->m_VarX1 = _Bar->m_VarX2;
    }
    if( _Bar->m_VarX1<_Bar->m_VarX0+32 )
        _Bar->m_VarX1 = _Bar->m_VarX0+32;
    if( _Bar->m_VarX1>_Bar->m_VarX2 )
        _Bar->m_VarX1 = _Bar->m_VarX2;
    if( !_Bar->m_IsPopupList )
    {
        _Bar->m_VarY0 = _Bar->m_Font->m_CharHeight+2+_Bar->m_Sep+6;
        _Bar->m_VarY1 = _Bar->m_Height-_Bar->m_Font->m_CharHeight-2-_Bar->m_Sep;
        _Bar->m_VarY2 = _Bar->m_Height-1;
    }
    else
    {
        _Bar->m_VarY0 = 4;
        _Bar->m_VarY1 = _Bar->m_Height-2-_Bar->m_Sep;
        _Bar->m_VarY2 = _Bar->m_Height-1;
    }

    int NbLines = (_Bar->m_VarY1-_Bar->m_VarY0+1)/(_Bar->m_Font->m_CharHeight+_Bar->m_LineSep);
    if( NbLines<= 0 )
        NbLines = 1;
    if( !_Bar->m_IsMinimized )
    {
        int LineNum = 0;
        CTwBar_BrowseHierarchy(_Bar, &LineNum, 0, &_Bar->m_VarRoot.m_Base, _Bar->m_FirstLine, _Bar->m_FirstLine+NbLines); // add a dummy tag at the end to avoid wrong 'tag-closing' problems
        if( (int)_Bar->m_HierTags.count>NbLines )
            tw_da_resize(&_Bar->m_HierTags, (size_t)NbLines); // remove the last dummy tag
        _Bar->m_NbHierLines = LineNum;
        _Bar->m_NbDisplayedLines = (int)_Bar->m_HierTags.count;

        if( ValuesWidthFit )
        {
            _Bar->m_ValuesWidth = CTwBar_ComputeValuesWidth(_Bar, _Bar->m_Font);
            if( _Bar->m_ValuesWidth<2*_Bar->m_Font->m_CharHeight )
                _Bar->m_ValuesWidth = 2*_Bar->m_Font->m_CharHeight; // enough to draw buttons
            if( _Bar->m_ValuesWidth>_Bar->m_VarX2 - _Bar->m_VarX0 )
                _Bar->m_ValuesWidth = max(_Bar->m_VarX2 - _Bar->m_VarX0 - _Bar->m_Font->m_CharHeight, 0);
            _Bar->m_VarX1 = _Bar->m_VarX2 - _Bar->m_ValuesWidth;
            if( _Bar->m_VarX1<_Bar->m_VarX0+32 )
                _Bar->m_VarX1 = _Bar->m_VarX0+32;
            if( _Bar->m_VarX1>_Bar->m_VarX2 )
                _Bar->m_VarX1 = _Bar->m_VarX2;
            _Bar->m_ValuesWidth = _Bar->m_VarX2 - _Bar->m_VarX1;
        }
    }

    // scroll bar
    int y0 = _Bar->m_PosY+_Bar->m_VarY0;
    int y1 = _Bar->m_PosY+_Bar->m_VarY1;
    int x0 = _Bar->m_PosX+2;
    int x1 = _Bar->m_PosX+_Bar->m_Font->m_CharHeight-2;
    if( ((x0+x1)&1)==1 )
        x1 += 1;
    int w  = x1-x0+1;
    int h  = y1-y0-2*w;
    int hscr = (_Bar->m_NbHierLines>0) ? ((h*_Bar->m_NbDisplayedLines)/_Bar->m_NbHierLines) : h;
    if( hscr<=4 )
        hscr = 4;
    if( hscr>h )
        hscr = h;
    int yscr = (_Bar->m_NbHierLines>0) ? ((h*_Bar->m_FirstLine)/_Bar->m_NbHierLines) : 0;
    if( yscr<=0 )
        yscr = 0;
    if( yscr>h-4 )
        yscr = h-4;
    if( yscr+hscr>h )
        hscr = h-yscr;
    if( hscr>h )
        hscr = h;
    if( hscr<=4 )
        hscr = 4;
    _Bar->m_ScrollYW = w;
    _Bar->m_ScrollYH = h;
    _Bar->m_ScrollY0 = y0+w+yscr;
    _Bar->m_ScrollY1 = y0+w+yscr+hscr;

    // Build title
    sds Title = sdsempty();
    if( sdslen(_Bar->m_Label)>0 )
        Title = sdscpy(Title, _Bar->m_Label);
    else
        Title = sdscpy(Title, _Bar->m_Name);
    _Bar->m_TitleWidth = ClampText(&Title, _Bar->m_Font, (!_Bar->m_IsMinimized)?(_Bar->m_Width-5*_Bar->m_Font->m_CharHeight):(16*_Bar->m_Font->m_CharHeight));
    const char *TitleC = Title;
    Gr->BuildText(Gr, _Bar->m_TitleTextObj, &TitleC, NULL, NULL, 1, _Bar->m_Font, 0, 0);
    sdsfree(Title);

    if( !_Bar->m_IsMinimized )
    {
        // Build labels
        CSdsArray    Labels = {0};
        CColor32Array Colors = {0};
        CColor32Array BgColors = {0};
        bool HasBgColors = false;
        CTwBar_ListLabels(_Bar, &Labels, &Colors, &BgColors, &HasBgColors, _Bar->m_Font, _Bar->m_VarX1-_Bar->m_VarX0, _Bar->m_VarX2-_Bar->m_VarX0);
        assert( Labels.count==Colors.count && Labels.count==BgColors.count );
        if( Labels.count>0 )
            Gr->BuildText(Gr, _Bar->m_LabelsTextObj, (const char * const *)Labels.items, Colors.items, BgColors.items, (int)Labels.count, _Bar->m_Font, _Bar->m_LineSep, HasBgColors ? _Bar->m_VarX1-_Bar->m_VarX0-_Bar->m_Font->m_CharHeight+2 : 0);
        else
            Gr->BuildText(Gr, _Bar->m_LabelsTextObj, NULL, NULL, NULL, 0, _Bar->m_Font, _Bar->m_LineSep, 0);

        // Should draw click button?
        _Bar->m_DrawClickBtn    = ( _Bar->m_VarX2-_Bar->m_VarX1>4*IncrBtnWidth(_Bar->m_Font->m_CharHeight)
                              && _Bar->m_HighlightedLine>=0 && _Bar->m_HighlightedLine<(int)_Bar->m_HierTags.count
                              && _Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var!=NULL 
                              && !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)
                              && !((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_ReadOnly
                              && (    ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_Type==TW_TYPE_BUTTON ));
                            //     || ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_Type==TW_TYPE_BOOLCPP
                            //     || ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_Type==TW_TYPE_BOOL8
                            //     || ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_Type==TW_TYPE_BOOL16
                            //     || ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_Type==TW_TYPE_BOOL32 ));

        // Should draw [-/+] button?
        _Bar->m_DrawIncrDecrBtn = ( _Bar->m_VarX2-_Bar->m_VarX1>5*IncrBtnWidth(_Bar->m_Font->m_CharHeight)
                              && _Bar->m_HighlightedLine>=0 && _Bar->m_HighlightedLine<(int)_Bar->m_HierTags.count
                              && _Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var!=NULL 
                              && !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)
                              && ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_Type!=TW_TYPE_BUTTON
                              && !((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_ReadOnly
                              && !((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_NoSlider 
                              && !(_Bar->m_EditInPlace.m_Active && _Bar->m_EditInPlace.m_Var==(CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) );

        // Should draw [v] button (list)?
        _Bar->m_DrawListBtn     = ( _Bar->m_VarX2-_Bar->m_VarX1>2*IncrBtnWidth(_Bar->m_Font->m_CharHeight)
                              && _Bar->m_HighlightedLine>=0 && _Bar->m_HighlightedLine<(int)_Bar->m_HierTags.count
                              && _Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var!=NULL 
                              && !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)
                              && IsEnumType(((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_Type)
                              && !((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_ReadOnly );

        // Should draw [<>] button (bool)?
        _Bar->m_DrawBoolBtn     = ( _Bar->m_VarX2-_Bar->m_VarX1>4*IncrBtnWidth(_Bar->m_Font->m_CharHeight)
                              && _Bar->m_HighlightedLine>=0 && _Bar->m_HighlightedLine<(int)_Bar->m_HierTags.count
                              && _Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var!=NULL 
                              && !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)
                              && !((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_ReadOnly
                              && (    ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_Type==TW_TYPE_BOOLCPP
                                   || ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_Type==TW_TYPE_BOOL8
                                   || ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_Type==TW_TYPE_BOOL16
                                   || ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_Type==TW_TYPE_BOOL32 ));

        // Should draw [o] button?
        _Bar->m_DrawRotoBtn     = _Bar->m_DrawIncrDecrBtn;
        /*
        _Bar->m_DrawRotoBtn     = ( _Bar->m_HighlightedLine>=0 && _Bar->m_HighlightedLine<(int)_Bar->m_HierTags.count
                              && _Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var!=NULL 
                              && !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)
                              && ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_Type!=TW_TYPE_BUTTON
                              && !((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_ReadOnly
                              && !((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_NoSlider );
        */

        // Build values (reuse Labels' storage - CSdsArray& Values = Labels;
        // was a C++ reference alias, dropped in favor of using Labels
        // directly, since they name the exact same object)
        for( size_t li=0; li<Labels.count; ++li ) // free the label strings before overwriting them with value strings
            sdsfree(Labels.items[li]);
        Labels.count = 0;
        Colors.count = 0;
        BgColors.count = 0;
        CTwBar_ListValues(_Bar, &Labels, &Colors, &BgColors, _Bar->m_Font, _Bar->m_VarX2-_Bar->m_VarX1);
        assert( BgColors.count==Labels.count && Colors.count==Labels.count );
        if( Labels.count>0 )
            Gr->BuildText(Gr, _Bar->m_ValuesTextObj, (const char * const *)Labels.items, Colors.items, BgColors.items, (int)Labels.count, _Bar->m_Font, _Bar->m_LineSep, _Bar->m_VarX2-_Bar->m_VarX1);
        else
            Gr->BuildText(Gr, _Bar->m_ValuesTextObj, NULL, NULL, NULL, 0, _Bar->m_Font, _Bar->m_LineSep, _Bar->m_VarX2-_Bar->m_VarX1);
        for( size_t vi=0; vi<Labels.count; ++vi )
            sdsfree(Labels.items[vi]);
        tw_da_free(&Labels);
        tw_da_free(&Colors);
        tw_da_free(&BgColors);

        // Build key shortcut text
        sds Shortcut = sdsempty();
        _Bar->m_ShortcutLine = -1;
        if( _Bar->m_HighlightedLine>=0 && _Bar->m_HighlightedLine<(int)_Bar->m_HierTags.count && _Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var!=NULL && !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) )
        {
            const CTwVarAtom *Atom = ((const CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var);
            if( Atom->m_KeyIncr[0]>0 || Atom->m_KeyDecr[0]>0 )
            {
                if( Atom->m_KeyIncr[0]>0 && Atom->m_KeyDecr[0]>0 )
                    Shortcut = sdscpy(Shortcut, "Keys: ");
                else
                    Shortcut = sdscpy(Shortcut, "Key: ");
                if( Atom->m_KeyIncr[0]>0 )
                    TwGetKeyString(&Shortcut, Atom->m_KeyIncr[0], Atom->m_KeyIncr[1]);
                else
                    Shortcut = sdscat(Shortcut, "(none)");
                if( Atom->m_KeyDecr[0]>0 )
                {
                    Shortcut = sdscat(Shortcut, "  ");
                    TwGetKeyString(&Shortcut, Atom->m_KeyDecr[0], Atom->m_KeyDecr[1]);
                }
                _Bar->m_ShortcutLine = _Bar->m_HighlightedLine;
            }
        }
        ClampText(&Shortcut, _Bar->m_Font, _Bar->m_Width-3*_Bar->m_Font->m_CharHeight);
        const char *ShortcutC = Shortcut;
        Gr->BuildText(Gr, _Bar->m_ShortcutTextObj, &ShortcutC, NULL, NULL, 1, _Bar->m_Font, 0, 0);
        sdsfree(Shortcut);

        // build headers text
        if (_Bar->m_HighlightLabelsHeader || _Bar->m_HighlightValuesHeader) {
            sds HeadersText = sdsnew("Fit column content");
            ClampText(&HeadersText, _Bar->m_Font, _Bar->m_Width-3*_Bar->m_Font->m_CharHeight);
            const char *HeadersTextC = HeadersText;
            Gr->BuildText(Gr, _Bar->m_HeadersTextObj, &HeadersTextC, NULL, NULL, 1, _Bar->m_Font, 0, 0);
            sdsfree(HeadersText);
        }
    }

    if( DoEndDraw )
        Gr->EndDraw(Gr);

    _Bar->m_UpToDate = true;
    _Bar->m_LastUpdateTime = (float)(glfwGetTime());
}

//  ---------------------------------------------------------------------------

void CTwBar_DrawHierHandle(CTwBar *_Bar)
{
    assert(_Bar->m_Font);
    ITwGraph *Gr = g_TwMgr->m_Graph;

    //int x0 = _Bar->m_PosX+_Bar->m_Font->m_CharHeight+1;
    int x0 = _Bar->m_PosX+3;
    //int x2 = _Bar->m_PosX+_Bar->m_VarX0-5;
    //int x2 = _Bar->m_PosX+3*_Bar->m_Font->m_CharWidth[(int)' ']-2;
    int x2 = _Bar->m_PosX+_Bar->m_Font->m_CharHeight-3;
    if( x2-x0<4 )
        x2 = x0+4;
    if( (x2-x0)&1 )
        --x2;
    int x1 = (x0+x2)/2;
    int w = x2-x0+1;
    int y0 = _Bar->m_PosY+_Bar->m_VarY0 +1;
    int y1;
    int dh0 = (_Bar->m_Font->m_CharHeight+_Bar->m_Sep-1-w)/2;
    if( dh0<0 )
        dh0 = 0;
    int dh1 = dh0+w-1;
    int i, h=0;

    if( !_Bar->m_IsPopupList )
    {
        CTwVarGroup *Grp;
        int nh = (int)_Bar->m_HierTags.count;
        for( h=0; h<nh; ++h )
        {
            y1 = y0 + _Bar->m_Font->m_CharHeight+_Bar->m_Sep-1;
            if( CTwVar_IsGroup(_Bar->m_HierTags.items[h].m_Var) )
                Grp = ((CTwVarGroup *)_Bar->m_HierTags.items[h].m_Var);
            else
                Grp = NULL;

            int dx = _Bar->m_HierTags.items[h].m_Level * (x2-x0);

            if( Grp )
            {
                if( _Bar->m_ColGrpBg!=0 && Grp->m_StructValuePtr==NULL )
                {
                    color32 cb = (Grp->m_StructType==TW_TYPE_HELP_STRUCT) ? _Bar->m_ColStructBg : _Bar->m_ColGrpBg;
                    //Gr->DrawRect(Gr, x0+dx-1, y0, _Bar->m_PosX+_Bar->m_VarX2, y0+_Bar->m_Font->m_CharHeight-1, cb, cb, cb, cb);
                    Gr->DrawRect(Gr, x2+dx+3, y0, _Bar->m_PosX+_Bar->m_VarX2, y0+_Bar->m_Font->m_CharHeight-1+_Bar->m_LineSep-1, cb, cb, cb, cb);
                }

                if( _Bar->m_DrawHandles )
                {
                    Gr->DrawLine(Gr, dx+x2+1, y0+dh0+1, dx+x2+1, y0+dh1+1, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
                    Gr->DrawLine(Gr, dx+x0+1, y0+dh1+1, dx+x2+2, y0+dh1+1, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
                }

                //Gr->DrawRect(Gr, x0+1, y0+dh0+1, x2-1, y0+dh1-1, (h==_Bar->m_HighlightedLine) ? _Bar->m_ColHighBtn : _Bar->m_ColBtn, (h==_Bar->m_HighlightedLine) ? _Bar->m_ColHighBtn : _Bar->m_ColBtn, (h==_Bar->m_HighlightedLine) ? _Bar->m_ColHighBtn : _Bar->m_ColBtn, (h==_Bar->m_HighlightedLine) ? _Bar->m_ColHighBtn : _Bar->m_ColBtn);
                Gr->DrawRect(Gr, dx+x0, y0+dh0, dx+x2, y0+dh1, (h==_Bar->m_HighlightedLine) ? _Bar->m_ColHighFold : _Bar->m_ColFold, (h==_Bar->m_HighlightedLine) ? _Bar->m_ColHighFold : _Bar->m_ColFold, (h==_Bar->m_HighlightedLine) ? _Bar->m_ColHighFold : _Bar->m_ColFold, (h==_Bar->m_HighlightedLine) ? _Bar->m_ColHighFold : _Bar->m_ColFold);
                if( _Bar->m_DrawHandles )
                {
                    Gr->DrawLine(Gr, dx+x0, y0+dh0, dx+x2, y0+dh0, _Bar->m_ColLine, _Bar->m_ColLine, false);
                    Gr->DrawLine(Gr, dx+x2, y0+dh0, dx+x2, y0+dh1+1, _Bar->m_ColLine, _Bar->m_ColLine, false);
                    Gr->DrawLine(Gr, dx+x2, y0+dh1, dx+x0, y0+dh1, _Bar->m_ColLine, _Bar->m_ColLine, false);
                    Gr->DrawLine(Gr, dx+x0, y0+dh1, dx+x0, y0+dh0, _Bar->m_ColLine, _Bar->m_ColLine, false);
                }
                
                Gr->DrawLine(Gr, dx+x0+2, y0+dh0+w/2, dx+x2-1, y0+dh0+w/2, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
                if( !Grp->m_Open )
                    Gr->DrawLine(Gr, dx+x1, y0+dh0+2, dx+x1, y0+dh1-1, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);

                /*
                if( _Bar->m_ColGrpBg!=0 && Grp->m_StructValuePtr==NULL )
                {
                    color32 cb = (Grp->m_StructType==TW_TYPE_HELP_STRUCT) ? _Bar->m_ColStructBg : _Bar->m_ColGrpBg;
                    //int decal = _Bar->m_Font->m_CharHeight/2-2+2*_Bar->m_HierTags.items[h].m_Level;
                    //if( decal>_Bar->m_Font->m_CharHeight-3 )
                    //  decal = _Bar->m_Font->m_CharHeight-3;
                    int margin = dx; //_Bar->m_Font->m_CharWidth[(int)' ']*_Bar->m_HierTags.items[h].m_Level;
                    //Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX0+margin, y0+decal, _Bar->m_PosX+_Bar->m_VarX2, y0+_Bar->m_Font->m_CharHeight-1, cb, cb, cb, cb);
                    Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX0+margin-1, y0+1, _Bar->m_PosX+_Bar->m_VarX2, y0+_Bar->m_Font->m_CharHeight, cb, cb, cb, cb);// _Bar->m_ColHierBg);
                    //Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX0-4, y0+_Bar->m_Font->m_CharHeight/2-1, _Bar->m_PosX+_Bar->m_VarX0+margin-2, y0+_Bar->m_Font->m_CharHeight/2, _Bar->m_ColHierBg, _Bar->m_ColHierBg, _Bar->m_ColHierBg, _Bar->m_ColHierBg);
                }
                */
            }
            else if( ((CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var)->m_Type==TW_TYPE_HELP_GRP && _Bar->m_ColHelpBg!=0 )
                Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX0+_Bar->m_HierTags.items[h].m_Var->m_LeftMargin, y0+_Bar->m_HierTags.items[h].m_Var->m_TopMargin, _Bar->m_PosX+_Bar->m_VarX2, y0+_Bar->m_Font->m_CharHeight-1, _Bar->m_ColHelpBg, _Bar->m_ColHelpBg, _Bar->m_ColHelpBg, _Bar->m_ColHelpBg);
            //else if( ((CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var)->m_Type==TW_TYPE_HELP_HEADER && _Bar->m_ColHelpBg!=0 )
            //  Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX0+_Bar->m_HierTags.items[h].m_Var->m_LeftMargin, y0+_Bar->m_HierTags.items[h].m_Var->m_TopMargin, _Bar->m_PosX+_Bar->m_VarX2, y0+_Bar->m_Font->m_CharHeight-1, _Bar->m_ColHelpBg, _Bar->m_ColHelpBg, _Bar->m_ColHelpBg, _Bar->m_ColHelpBg);
            /*
            else if( ((CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var)->m_Type==TW_TYPE_BUTTON && _Bar->m_ColBtn!=0 )
            {
                // draw button
                int cbx0 = _Bar->m_PosX+_Bar->m_VarX2-2*bw+bw/2, cby0 = y0+2, cbx1 = _Bar->m_PosX+_Bar->m_VarX2-2-bw/2, cby1 = y0+_Bar->m_Font->m_CharHeight-4;
                if( _Bar->m_HighlightClickBtn )
                {
                    Gr->DrawRect(Gr, cbx0+2, cby0+2, cbx1+2, cby1+2, _Bar->m_ColBtn, _Bar->m_ColBtn, _Bar->m_ColBtn, _Bar->m_ColBtn);
                    Gr->DrawLine(Gr, cbx0+3, cby1+3, cbx1+4, cby1+3, 0x7F000000, 0x7F000000, false);
                    Gr->DrawLine(Gr, cbx1+3, cby0+3, cbx1+3, cby1+3, 0x7F000000, 0x7F000000, false);                       
                }
                else
                {
                    Gr->DrawRect(Gr, cbx0+3, cby1+1, cbx1+3, cby1+3, 0x7F000000, 0x7F000000, 0x7F000000, 0x7F000000);
                    Gr->DrawRect(Gr, cbx1+1, cby0+3, cbx1+3, cby1, 0x7F000000, 0x7F000000, 0x7F000000, 0x7F000000);
                    Gr->DrawRect(Gr, cbx0, cby0, cbx1, cby1, _Bar->m_ColBtn, _Bar->m_ColBtn, _Bar->m_ColBtn, _Bar->m_ColBtn);
                }
            }
            */

            y0 = y1+_Bar->m_LineSep;
        }
    }

    if( _Bar->m_NbDisplayedLines<_Bar->m_NbHierLines )
    {
        // Draw scroll bar
        y0 = _Bar->m_PosY+_Bar->m_VarY0;
        y1 = _Bar->m_PosY+_Bar->m_VarY1;
        //x0 = _Bar->m_PosX+2;
        //x1 = _Bar->m_PosX+_Bar->m_Font->m_CharHeight-2;
        x0 = _Bar->m_PosX + _Bar->m_VarX2+4;
        x1 = x0 + _Bar->m_Font->m_CharHeight-4;
        if( ((x0+x1)&1)==1 )
            x1 += 1;
        w  = _Bar->m_ScrollYW;
        h  = _Bar->m_ScrollYH;

        Gr->DrawRect(Gr, x0+2, y0+w, x1-2, y1-1-w, (_Bar->m_ColBg&0xffffff)|0x11000000, (_Bar->m_ColBg&0xffffff)|0x11000000, (_Bar->m_ColBg&0xffffff)|0x11000000, (_Bar->m_ColBg&0xffffff)|0x11000000);
        if( _Bar->m_DrawHandles || _Bar->m_IsPopupList )
        {
            // scroll handle shadow lines
            Gr->DrawLine(Gr, x1-1, _Bar->m_ScrollY0+1, x1-1, _Bar->m_ScrollY1+1, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            Gr->DrawLine(Gr, x0+2, _Bar->m_ScrollY1+1, x1, _Bar->m_ScrollY1+1, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            
            // up & down arrow
            for( i=0; i<(x1-x0-2)/2; ++i )
            {
                Gr->DrawLine(Gr, x0+2+i, y0+w-2*i, x1-i, y0+w-2*i, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
                Gr->DrawLine(Gr, x0+1+i, y0+w-1-2*i, x1-1-i, y0+w-1-2*i, _Bar->m_HighlightUpScroll?((_Bar->m_ColLine&0xffffff)|0x4f000000):_Bar->m_ColLine, _Bar->m_HighlightUpScroll?((_Bar->m_ColLine&0xffffff)|0x4f000000):_Bar->m_ColLine, false);

                Gr->DrawLine(Gr, x0+2+i, y1-w+2+2*i, x1-i, y1-w+2+2*i, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
                Gr->DrawLine(Gr, x0+1+i, y1-w+1+2*i, x1-1-i, y1-w+1+2*i, _Bar->m_HighlightDnScroll?((_Bar->m_ColLine&0xffffff)|0x4f000000):_Bar->m_ColLine, _Bar->m_HighlightDnScroll?((_Bar->m_ColLine&0xffffff)|0x4f000000):_Bar->m_ColLine, false);
            }

            // middle lines
            Gr->DrawLine(Gr, (x0+x1)/2-1, y0+w, (x0+x1)/2-1, _Bar->m_ScrollY0, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, (x0+x1)/2, y0+w, (x0+x1)/2, _Bar->m_ScrollY0, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, (x0+x1)/2+1, y0+w, (x0+x1)/2+1, _Bar->m_ScrollY0, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            Gr->DrawLine(Gr, (x0+x1)/2-1, _Bar->m_ScrollY1, (x0+x1)/2-1, y1-w+1, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, (x0+x1)/2, _Bar->m_ScrollY1, (x0+x1)/2, y1-w+1, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, (x0+x1)/2+1, _Bar->m_ScrollY1, (x0+x1)/2+1, y1-w+1, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            // scroll handle lines
            Gr->DrawRect(Gr, x0+2, _Bar->m_ScrollY0+1, x1-3, _Bar->m_ScrollY1-1, _Bar->m_HighlightScroll?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightScroll?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightScroll?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightScroll?_Bar->m_ColHighBtn:_Bar->m_ColBtn);
            Gr->DrawLine(Gr, x1-2, _Bar->m_ScrollY0, x1-2, _Bar->m_ScrollY1, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, x0+1, _Bar->m_ScrollY0, x0+1, _Bar->m_ScrollY1, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, x0+1, _Bar->m_ScrollY1, x1-1, _Bar->m_ScrollY1, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, x0+1, _Bar->m_ScrollY0, x1-2, _Bar->m_ScrollY0, _Bar->m_ColLine, _Bar->m_ColLine, false);
        }
        else
            Gr->DrawRect(Gr, x0+3, _Bar->m_ScrollY0+1, x1-3, _Bar->m_ScrollY1-1, _Bar->m_ColBtn, _Bar->m_ColBtn, _Bar->m_ColBtn, _Bar->m_ColBtn);
    }

    if( _Bar->m_DrawHandles && !_Bar->m_IsPopupList )
    {
        if( _Bar->m_Resizable ) // Draw resize handles
        {
            //   lower-left
            Gr->DrawLine(Gr, _Bar->m_PosX+3, _Bar->m_PosY+_Bar->m_Height-_Bar->m_Font->m_CharHeight+3, _Bar->m_PosX+3, _Bar->m_PosY+_Bar->m_Height-4, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, _Bar->m_PosX+4, _Bar->m_PosY+_Bar->m_Height-_Bar->m_Font->m_CharHeight+4, _Bar->m_PosX+4, _Bar->m_PosY+_Bar->m_Height-3, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            Gr->DrawLine(Gr, _Bar->m_PosX+3, _Bar->m_PosY+_Bar->m_Height-4, _Bar->m_PosX+_Bar->m_Font->m_CharHeight-4, _Bar->m_PosY+_Bar->m_Height-4, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, _Bar->m_PosX+4, _Bar->m_PosY+_Bar->m_Height-3, _Bar->m_PosX+_Bar->m_Font->m_CharHeight-3, _Bar->m_PosY+_Bar->m_Height-3, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            //   lower-right
            Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_Width-4, _Bar->m_PosY+_Bar->m_Height-_Bar->m_Font->m_CharHeight+3, _Bar->m_PosX+_Bar->m_Width-4, _Bar->m_PosY+_Bar->m_Height-4, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_Width-3, _Bar->m_PosY+_Bar->m_Height-_Bar->m_Font->m_CharHeight+4, _Bar->m_PosX+_Bar->m_Width-3, _Bar->m_PosY+_Bar->m_Height-3, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_Width-4, _Bar->m_PosY+_Bar->m_Height-4, _Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight+3, _Bar->m_PosY+_Bar->m_Height-4, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_Width-3, _Bar->m_PosY+_Bar->m_Height-3, _Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight+4, _Bar->m_PosY+_Bar->m_Height-3, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            //   upper-left
            Gr->DrawLine(Gr, _Bar->m_PosX+3, _Bar->m_PosY+_Bar->m_Font->m_CharHeight-4, _Bar->m_PosX+3, _Bar->m_PosY+3, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, _Bar->m_PosX+4, _Bar->m_PosY+_Bar->m_Font->m_CharHeight-3, _Bar->m_PosX+4, _Bar->m_PosY+4, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            Gr->DrawLine(Gr, _Bar->m_PosX+3, _Bar->m_PosY+3, _Bar->m_PosX+_Bar->m_Font->m_CharHeight-4, _Bar->m_PosY+3, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, _Bar->m_PosX+4, _Bar->m_PosY+4, _Bar->m_PosX+_Bar->m_Font->m_CharHeight-3, _Bar->m_PosY+4, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            //   upper-right
            Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_Width-4, _Bar->m_PosY+3, _Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight+3, _Bar->m_PosY+3, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_Width-3, _Bar->m_PosY+4, _Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight+4, _Bar->m_PosY+4, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_Width-4, _Bar->m_PosY+_Bar->m_Font->m_CharHeight-4, _Bar->m_PosX+_Bar->m_Width-4, _Bar->m_PosY+3, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_Width-3, _Bar->m_PosY+_Bar->m_Font->m_CharHeight-3, _Bar->m_PosX+_Bar->m_Width-3, _Bar->m_PosY+4, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
        }

        int xm = _Bar->m_PosX+_Bar->m_Width-2*_Bar->m_Font->m_CharHeight, wm=_Bar->m_Font->m_CharHeight-6;
        wm = (wm<6) ? 6 : wm;
        if( _Bar->m_Iconifiable ) // Draw minimize button
        {
            Gr->DrawRect(Gr, xm+1, _Bar->m_PosY+4, xm+wm-1, _Bar->m_PosY+3+wm, _Bar->m_HighlightMinimize?_Bar->m_ColHighBtn:((_Bar->m_ColBtn&0xffffff)|0x4f000000), _Bar->m_HighlightMinimize?_Bar->m_ColHighBtn:((_Bar->m_ColBtn&0xffffff)|0x4f000000), _Bar->m_HighlightMinimize?_Bar->m_ColHighBtn:((_Bar->m_ColBtn&0xffffff)|0x4f000000), _Bar->m_HighlightMinimize?_Bar->m_ColHighBtn:((_Bar->m_ColBtn&0xffffff)|0x4f000000));
            Gr->DrawLine(Gr, xm, _Bar->m_PosY+3, xm+wm, _Bar->m_PosY+3, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, xm+wm, _Bar->m_PosY+3, xm+wm, _Bar->m_PosY+3+wm, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, xm+wm, _Bar->m_PosY+3+wm, xm, _Bar->m_PosY+3+wm, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, xm, _Bar->m_PosY+3+wm, xm, _Bar->m_PosY+3, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, xm+wm+1, _Bar->m_PosY+4, xm+wm+1, _Bar->m_PosY+4+wm, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            Gr->DrawLine(Gr, xm+wm+1, _Bar->m_PosY+4+wm, xm, _Bar->m_PosY+4+wm, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            Gr->DrawLine(Gr, xm+wm/3+((wm<9)?1:0)-1, _Bar->m_PosY+4+wm/3-((wm<9)?0:1), xm+wm/2, _Bar->m_PosY+2+wm-1, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
            Gr->DrawLine(Gr, xm+wm-wm/3+((wm<9)?0:1), _Bar->m_PosY+4+wm/3-((wm<9)?0:1), xm+wm/2, _Bar->m_PosY+2+wm-1, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
        }

        if( g_TwMgr->m_FontResizable ) // Draw font button
        {
            xm = _Bar->m_PosX+_Bar->m_Font->m_CharHeight+2;
            Gr->DrawRect(Gr, xm+1, _Bar->m_PosY+4, xm+wm-1, _Bar->m_PosY+3+wm, _Bar->m_HighlightFont?_Bar->m_ColHighBtn:((_Bar->m_ColBtn&0xffffff)|0x4f000000), _Bar->m_HighlightFont?_Bar->m_ColHighBtn:((_Bar->m_ColBtn&0xffffff)|0x4f000000), _Bar->m_HighlightFont?_Bar->m_ColHighBtn:((_Bar->m_ColBtn&0xffffff)|0x4f000000), _Bar->m_HighlightFont?_Bar->m_ColHighBtn:((_Bar->m_ColBtn&0xffffff)|0x4f000000));
            Gr->DrawLine(Gr, xm, _Bar->m_PosY+3, xm+wm, _Bar->m_PosY+3, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, xm+wm, _Bar->m_PosY+3, xm+wm, _Bar->m_PosY+3+wm, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, xm+wm, _Bar->m_PosY+3+wm, xm, _Bar->m_PosY+3+wm, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, xm, _Bar->m_PosY+3+wm, xm, _Bar->m_PosY+3, _Bar->m_ColLine, _Bar->m_ColLine, false);
            Gr->DrawLine(Gr, xm+wm+1, _Bar->m_PosY+4, xm+wm+1, _Bar->m_PosY+4+wm, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            Gr->DrawLine(Gr, xm+wm+1, _Bar->m_PosY+4+wm, xm, _Bar->m_PosY+4+wm, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
            Gr->DrawLine(Gr, xm+wm/2-wm/6, _Bar->m_PosY+3+wm/3, xm+wm/2+wm/6+1, _Bar->m_PosY+3+wm/3, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
            Gr->DrawLine(Gr, xm+wm/2-wm/6, _Bar->m_PosY+3+wm/3, xm+wm/2-wm/6, _Bar->m_PosY+4+wm-wm/3+(wm>11?1:0), _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
            Gr->DrawLine(Gr, xm+wm/2-wm/6, _Bar->m_PosY+3+wm/2+(wm>11?1:0), xm+wm/2+wm/6, _Bar->m_PosY+3+wm/2+(wm>11?1:0), _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
        }
    }
}

//  ---------------------------------------------------------------------------

void CTwBar_Draw(CTwBar *_Bar, int _DrawPart)
{
    PERF( PerfTimer Timer; double DT; )

    assert(_Bar->m_Font);
    ITwGraph *Gr = g_TwMgr->m_Graph;

    _Bar->m_CustomRecords.count = 0;

    if( (float)(glfwGetTime())>_Bar->m_LastUpdateTime+_Bar->m_UpdatePeriod )
        CTwBar_NotUpToDate(_Bar);

    if( _Bar->m_HighlightedLine!=_Bar->m_HighlightedLinePrev )
    {
        _Bar->m_HighlightedLinePrev = _Bar->m_HighlightedLine;
        CTwBar_NotUpToDate(_Bar);
    }

    if( _Bar->m_IsHelpBar && g_TwMgr->m_HelpBarNotUpToDate )
        CTwMgr_UpdateHelpBar(g_TwMgr);

    if( !_Bar->m_UpToDate )
        CTwBar_Update(_Bar);

    if( !_Bar->m_IsMinimized )
    {
        int y = _Bar->m_PosY+1;
        int LevelSpace = max(_Bar->m_Font->m_CharHeight-6, 4); // space used by DrawHierHandles

        color32 colBg = _Bar->m_ColBg, colBg1 = _Bar->m_ColBg1, colBg2 = _Bar->m_ColBg2;
        if( _Bar->m_DrawHandles || _Bar->m_IsPopupList )
        {
            unsigned int alphaMin = 0x70;
            if( _Bar->m_IsPopupList )
                alphaMin = 0xa0;
            if( (colBg>>24)<alphaMin )
                colBg = (colBg&0xffffff)|(alphaMin<<24);
            if( (colBg1>>24)<alphaMin )
                colBg1 = (colBg1&0xffffff)|(alphaMin<<24);
            if( (colBg2>>24)<alphaMin )
                colBg2 = (colBg2&0xffffff)|(alphaMin<<24);
        }

        // Draw title
        if( !_Bar->m_IsPopupList )
        {
            PERF( Timer.Reset(); )
            if( _DrawPart&DRAW_BG )
            {
                //Gr->DrawRect(Gr, _Bar->m_PosX, _Bar->m_PosY, _Bar->m_PosX+_Bar->m_Width-1, _Bar->m_PosY+_Bar->m_Font->m_CharHeight+1, (_Bar->m_HighlightTitle||_Bar->m_MouseDragTitle) ? _Bar->m_ColTitleHighBg : (_Bar->m_DrawHandles ? _Bar->m_ColTitleBg : _Bar->m_ColTitleUnactiveBg), (_Bar->m_HighlightTitle||_Bar->m_MouseDragTitle) ? _Bar->m_ColTitleHighBg : (_Bar->m_DrawHandles ? _Bar->m_ColTitleBg : _Bar->m_ColTitleUnactiveBg), (_Bar->m_HighlightTitle||_Bar->m_MouseDragTitle) ? _Bar->m_ColTitleHighBg : (_Bar->m_DrawHandles ? _Bar->m_ColTitleBg : _Bar->m_ColTitleUnactiveBg), (_Bar->m_HighlightTitle||_Bar->m_MouseDragTitle) ? _Bar->m_ColTitleHighBg : (_Bar->m_DrawHandles ? _Bar->m_ColTitleBg : _Bar->m_ColTitleUnactiveBg));
                if( _Bar->m_HighlightTitle || _Bar->m_MouseDragTitle )
                    Gr->DrawRect(Gr, _Bar->m_PosX, _Bar->m_PosY, _Bar->m_PosX+_Bar->m_Width-1, _Bar->m_PosY+_Bar->m_Font->m_CharHeight+1, _Bar->m_ColTitleHighBg, _Bar->m_ColTitleHighBg, _Bar->m_ColTitleHighBg, _Bar->m_ColTitleHighBg);
                else if (_Bar->m_DrawHandles)
                    Gr->DrawRect(Gr, _Bar->m_PosX, _Bar->m_PosY, _Bar->m_PosX+_Bar->m_Width-1, _Bar->m_PosY+_Bar->m_Font->m_CharHeight+1, _Bar->m_ColTitleBg, _Bar->m_ColTitleBg, colBg2, colBg1);
                else
                    Gr->DrawRect(Gr, _Bar->m_PosX, _Bar->m_PosY, _Bar->m_PosX+_Bar->m_Width-1, _Bar->m_PosY+_Bar->m_Font->m_CharHeight+1, _Bar->m_ColTitleBg, _Bar->m_ColTitleBg, colBg2, colBg1);
            }
            if( _DrawPart&DRAW_CONTENT )
            {
                const color32 COL0 = 0x50ffffff;
                const color32 COL1 = 0x501f1f1f;
                Gr->DrawRect(Gr, _Bar->m_PosX, _Bar->m_PosY, _Bar->m_PosX+_Bar->m_Width-1, y, COL0, COL0, COL1, COL1);
                if( _Bar->m_ColTitleShadow!=0 )
                    Gr->DrawText(Gr, _Bar->m_TitleTextObj, _Bar->m_PosX+(_Bar->m_Width-_Bar->m_TitleWidth)/2+1, _Bar->m_PosY+1, _Bar->m_ColTitleShadow, 0);
                Gr->DrawText(Gr, _Bar->m_TitleTextObj, _Bar->m_PosX+(_Bar->m_Width-_Bar->m_TitleWidth)/2, _Bar->m_PosY, _Bar->m_ColTitleText, 0);
            }
            y = _Bar->m_PosY+_Bar->m_Font->m_CharHeight+1;
            if( _DrawPart&DRAW_CONTENT && _Bar->m_DrawHandles )
                Gr->DrawLine(Gr, _Bar->m_PosX, y, _Bar->m_PosX+_Bar->m_Width-1, y, 0x30ffffff, 0x30ffffff, false); // 0x80afafaf);
            y++;
            PERF( DT = Timer.GetTime(); printf("Title=%.4fms ", 1000.0*DT); )
        }

        // Draw background
        PERF( Timer.Reset(); )
        if( _DrawPart&DRAW_BG )
        {
            Gr->DrawRect(Gr, _Bar->m_PosX, y, _Bar->m_PosX+_Bar->m_Width-1, _Bar->m_PosY+_Bar->m_Height-1, colBg2, colBg1, colBg1, colBg);
            //Gr->DrawRect(Gr, _Bar->m_PosX, y, _Bar->m_PosX+_Bar->m_VarX0-5, _Bar->m_PosY+_Bar->m_Height-1, _Bar->m_ColHierBg, _Bar->m_ColHierBg, _Bar->m_ColHierBg, _Bar->m_ColHierBg);
            Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX2+3, y, _Bar->m_PosX+_Bar->m_Width-1, _Bar->m_PosY+_Bar->m_Height-1, _Bar->m_ColHierBg, _Bar->m_ColHierBg, _Bar->m_ColHierBg, _Bar->m_ColHierBg);
        }

        if( _DrawPart&DRAW_CONTENT )
        {
            // Draw highlighted line
            if( _Bar->m_HighlightedLine>=0 && _Bar->m_HighlightedLine<(int)_Bar->m_HierTags.count && _Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var!=NULL
                && (CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) 
                    || (!((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_ReadOnly && !_Bar->m_IsHelpBar
                        && !CTwVar_IsCustom(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) ) ) )
            {
                int y0 = _Bar->m_PosY + _Bar->m_VarY0 + _Bar->m_HighlightedLine*(_Bar->m_Font->m_CharHeight+_Bar->m_LineSep);
                Gr->DrawRect(Gr, _Bar->m_PosX+LevelSpace+6+LevelSpace*_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Level, y0+1, _Bar->m_PosX+_Bar->m_VarX2, y0+_Bar->m_Font->m_CharHeight-1+_Bar->m_LineSep-1, _Bar->m_ColHighBg0, _Bar->m_ColHighBg0, _Bar->m_ColHighBg1, _Bar->m_ColHighBg1);
                int eps = (g_TwMgr->m_GraphAPI==TW_OPENGL || g_TwMgr->m_GraphAPI==TW_OPENGL_CORE) ? 1 : 0;
                if( !_Bar->m_EditInPlace.m_Active )
                    Gr->DrawLine(Gr, _Bar->m_PosX+LevelSpace+6+LevelSpace*_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Level, y0+_Bar->m_Font->m_CharHeight+_Bar->m_LineSep-1+eps, _Bar->m_PosX+_Bar->m_VarX2, y0+_Bar->m_Font->m_CharHeight+_Bar->m_LineSep-1+eps, _Bar->m_ColUnderline, _Bar->m_ColUnderline, false);
            }
            else if( _Bar->m_HighlightedLine>=0 && _Bar->m_HighlightedLine<(int)_Bar->m_HierTags.count && !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) )
            {
                int y0 = _Bar->m_PosY + _Bar->m_VarY0 + _Bar->m_HighlightedLine*(_Bar->m_Font->m_CharHeight+_Bar->m_LineSep);
                color32 col = ColorBlend(_Bar->m_ColHighBg0, _Bar->m_ColHighBg1, 0.5f);
                CTwVarAtom *Atom = ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var);
                if( !IsCustomType(Atom->m_Type)
                    && !(Atom->m_Type==TW_TYPE_BUTTON && Atom->m_Val.m_Button.m_Callback==NULL) )
                    Gr->DrawRect(Gr, _Bar->m_PosX+LevelSpace+6+LevelSpace*_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Level, y0+1, _Bar->m_PosX+_Bar->m_VarX2, y0+_Bar->m_Font->m_CharHeight-1+_Bar->m_LineSep-1, col, col, col, col);
                else
                    Gr->DrawRect(Gr, _Bar->m_PosX+LevelSpace+6+LevelSpace*_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Level, y0+1, _Bar->m_PosX+LevelSpace+6+LevelSpace*_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Level+4, y0+_Bar->m_Font->m_CharHeight-1+_Bar->m_LineSep-1, col, col, col, col);
            }
            color32 clight = 0x5FFFFFFF; // bar contour
            Gr->DrawLine(Gr, _Bar->m_PosX, _Bar->m_PosY, _Bar->m_PosX, _Bar->m_PosY+_Bar->m_Height, clight, clight, false);
            Gr->DrawLine(Gr, _Bar->m_PosX, _Bar->m_PosY, _Bar->m_PosX+_Bar->m_Width, _Bar->m_PosY, clight, clight, false);
            Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_Width, _Bar->m_PosY, _Bar->m_PosX+_Bar->m_Width, _Bar->m_PosY+_Bar->m_Height, clight, clight, false);
            Gr->DrawLine(Gr, _Bar->m_PosX, _Bar->m_PosY+_Bar->m_Height, _Bar->m_PosX+_Bar->m_Width, _Bar->m_PosY+_Bar->m_Height, clight, clight, false);
            int dshad = 3;  // bar shadows
            color32 cshad = (((_Bar->m_Color>>24)/2)<<24) & 0xFF000000;
            Gr->DrawRect(Gr, _Bar->m_PosX, _Bar->m_PosY+_Bar->m_Height, _Bar->m_PosX+dshad, _Bar->m_PosY+_Bar->m_Height+dshad, 0, cshad, 0, 0);
            Gr->DrawRect(Gr, _Bar->m_PosX+dshad+1, _Bar->m_PosY+_Bar->m_Height, _Bar->m_PosX+_Bar->m_Width-1, _Bar->m_PosY+_Bar->m_Height+dshad, cshad, cshad, 0, 0);
            Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_Width, _Bar->m_PosY+_Bar->m_Height, _Bar->m_PosX+_Bar->m_Width+dshad, _Bar->m_PosY+_Bar->m_Height+dshad, cshad, 0, 0, 0);
            Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_Width, _Bar->m_PosY, _Bar->m_PosX+_Bar->m_Width+dshad, _Bar->m_PosY+dshad, 0, 0, cshad, 0);
            Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_Width, _Bar->m_PosY+dshad+1, _Bar->m_PosX+_Bar->m_Width+dshad, _Bar->m_PosY+_Bar->m_Height-1, cshad, 0, cshad, 0);
            PERF( DT = Timer.GetTime(); printf("Bg=%.4fms ", 1000.0*DT); )

            // Draw hierarchy handle
            PERF( Timer.Reset(); )
            CTwBar_DrawHierHandle(_Bar);
            PERF( DT = Timer.GetTime(); printf("Handles=%.4fms ", 1000.0*DT); )

            // Draw labels
            PERF( Timer.Reset(); )
            Gr->DrawText(Gr, _Bar->m_LabelsTextObj, _Bar->m_PosX+LevelSpace+6, _Bar->m_PosY+_Bar->m_VarY0, 0 /*_Bar->m_ColLabelText*/, 0);
            PERF( DT = Timer.GetTime(); printf("Labels=%.4fms ", 1000.0*DT); )

            // Draw values
            if( !_Bar->m_IsPopupList )
            {
                PERF( Timer.Reset(); )
                Gr->DrawText(Gr, _Bar->m_ValuesTextObj, _Bar->m_PosX+_Bar->m_VarX1, _Bar->m_PosY+_Bar->m_VarY0, 0 /*_Bar->m_ColValText*/, 0 /*_Bar->m_ColValBg*/);
                PERF( DT = Timer.GetTime(); printf("Values=%.4fms ", 1000.0*DT); )
            }

            // Draw preview for color values and draw buttons and custom types
            int h, nh = (int)_Bar->m_HierTags.count;
            int yh = _Bar->m_PosY+_Bar->m_VarY0;
            int bw = IncrBtnWidth(_Bar->m_Font->m_CharHeight);
            for( h=0; h<nh; ++h )
            {
                if( CTwVar_IsGroup(_Bar->m_HierTags.items[h].m_Var) )
                {
                    const CTwVarGroup * Grp = ((const CTwVarGroup *)_Bar->m_HierTags.items[h].m_Var);
                    if( Grp->m_SummaryCallback==CColorExt_SummaryCB && Grp->m_StructValuePtr!=NULL )
                    {
                        // draw color value
                        if( Grp->m_Vars.count>0 && Grp->m_Vars.items[0]!=NULL && !CTwVar_IsGroup(Grp->m_Vars.items[0]) )
                            CTwVarAtom_ValueToDouble((CTwVarAtom *)Grp->m_Vars.items[0]); // force ext update
                        int ydecal = (g_TwMgr->m_GraphAPI==TW_OPENGL || g_TwMgr->m_GraphAPI==TW_OPENGL_CORE) ? 1 : 0;
                        const int checker = 8;
                        for( int c=0; c<checker; ++c )
                            Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX1+(c*(_Bar->m_VarX2-_Bar->m_VarX1))/checker, yh+1+ydecal+((c%2)*(_Bar->m_Font->m_CharHeight-2))/2, _Bar->m_PosX+_Bar->m_VarX1-1+((c+1)*(_Bar->m_VarX2-_Bar->m_VarX1))/checker, yh+ydecal+(((c%2)+1)*(_Bar->m_Font->m_CharHeight-2))/2, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff);
                        Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX1, yh+1+ydecal, _Bar->m_PosX+_Bar->m_VarX2-1, yh+ydecal+_Bar->m_Font->m_CharHeight-2, 0xbfffffff, 0xbfffffff, 0xbfffffff, 0xbfffffff);
                        const CColorExt *colExt = (const CColorExt *)(Grp->m_StructValuePtr);
                        color32 col = Color32FromARGBi((colExt->m_HasAlpha ? colExt->A : 255), colExt->R, colExt->G, colExt->B);
                        if( col!=0 )
                            Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX1, yh+1+ydecal, _Bar->m_PosX+_Bar->m_VarX2-1, yh+ydecal+_Bar->m_Font->m_CharHeight-2, col, col, col, col);
                        /*
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX1-1, yh, _Bar->m_PosX+_Bar->m_VarX2+1, yh, 0xff000000, 0xff000000, false);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX1-1, yh+_Bar->m_Font->m_CharHeight, _Bar->m_PosX+_Bar->m_VarX2+1, yh+_Bar->m_Font->m_CharHeight, 0xff000000, 0xff000000, false);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX1-1, yh, _Bar->m_PosX+_Bar->m_VarX1-1, yh+_Bar->m_Font->m_CharHeight, 0xff000000, 0xff000000, false);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2, yh, _Bar->m_PosX+_Bar->m_VarX2, yh+_Bar->m_Font->m_CharHeight, 0xff000000, 0xff000000, false);
                        */
                    }
                    //else if( Grp->m_SummaryCallback==CustomTypeSummaryCB && Grp->m_StructValuePtr!=NULL )
                    //{
                    //}
                }
                else if( ((CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var)->m_Type==TW_TYPE_BUTTON && !_Bar->m_IsPopupList )
                {
                    // draw button
                    int cbx0, cbx1;
                    if( _Bar->m_ButtonAlign == BUTTON_ALIGN_LEFT )
                    {
                        cbx0 = _Bar->m_PosX+_Bar->m_VarX1+2;
                        cbx1 = _Bar->m_PosX+_Bar->m_VarX1+bw;
                    }
                    else if( _Bar->m_ButtonAlign == BUTTON_ALIGN_CENTER )
                    {
                        cbx0 = _Bar->m_PosX+(_Bar->m_VarX1+_Bar->m_VarX2)/2-bw/2+1;
                        cbx1 = _Bar->m_PosX+(_Bar->m_VarX1+_Bar->m_VarX2)/2+bw/2-1;
                    }
                    else
                    {
                        // BUTTON_ALIGN_RIGHT (default): span the whole value
                        // column, matching the width/position of every other
                        // widget type (color swatches, text-edit boxes, ...)
                        // instead of a narrow button squeezed at the right edge.
                        cbx0 = _Bar->m_PosX+_Bar->m_VarX1+1;
                        cbx1 = _Bar->m_PosX+_Bar->m_VarX2-2;
                    }
                    int cby0 = yh+3;
                    int cby1 = yh+_Bar->m_Font->m_CharHeight-3;
                    if( !((CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var)->m_ReadOnly )
                    {
                        double BtnAutoDelta = glfwGetTime() - _Bar->m_HighlightClickBtnAuto;
                        if( (_Bar->m_HighlightClickBtn || (BtnAutoDelta>=0 && BtnAutoDelta<0.1)) && h==_Bar->m_HighlightedLine )
                        {
                            cbx0--; cby0--; cbx1--; cby1--;
                            Gr->DrawRect(Gr, cbx0+2, cby0+2, cbx1+2, cby1+2, _Bar->m_ColHighBtn, _Bar->m_ColHighBtn, _Bar->m_ColHighBtn, _Bar->m_ColHighBtn);
                            Gr->DrawLine(Gr, cbx0+3, cby1+3, cbx1+4, cby1+3, 0xAF000000, 0xAF000000, false);
                            Gr->DrawLine(Gr, cbx1+3, cby0+3, cbx1+3, cby1+3, 0xAF000000, 0xAF000000, false);                       
                            Gr->DrawLine(Gr, cbx0+2, cby0+2, cbx0+2, cby1+2, _Bar->m_ColLine, _Bar->m_ColLine, false);
                            Gr->DrawLine(Gr, cbx0+2, cby1+2, cbx1+2, cby1+2, _Bar->m_ColLine, _Bar->m_ColLine, false);
                            Gr->DrawLine(Gr, cbx1+2, cby1+2, cbx1+2, cby0+2, _Bar->m_ColLine, _Bar->m_ColLine, false);
                            Gr->DrawLine(Gr, cbx1+2, cby0+2, cbx0+2, cby0+2, _Bar->m_ColLine, _Bar->m_ColLine, false);
                        }
                        else
                        {
                            Gr->DrawRect(Gr, cbx0+2, cby1+1, cbx1+2, cby1+2, (h==_Bar->m_HighlightedLine)?0xAF000000:0x7F000000, (h==_Bar->m_HighlightedLine)?0xAF000000:0x7F000000, (h==_Bar->m_HighlightedLine)?0xAF000000:0x7F000000, (h==_Bar->m_HighlightedLine)?0xAF000000:0x7F000000);
                            Gr->DrawRect(Gr, cbx1+1, cby0+2, cbx1+2, cby1, (h==_Bar->m_HighlightedLine)?0xAF000000:0x7F000000, (h==_Bar->m_HighlightedLine)?0xAF000000:0x7F000000, (h==_Bar->m_HighlightedLine)?0xAF000000:0x7F000000, (h==_Bar->m_HighlightedLine)?0xAF000000:0x7F000000);
                            Gr->DrawRect(Gr, cbx0, cby0, cbx1, cby1, (h==_Bar->m_HighlightedLine)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (h==_Bar->m_HighlightedLine)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (h==_Bar->m_HighlightedLine)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (h==_Bar->m_HighlightedLine)?_Bar->m_ColHighBtn:_Bar->m_ColBtn);
                            Gr->DrawLine(Gr, cbx0, cby0, cbx0, cby1, _Bar->m_ColLine, _Bar->m_ColLine, false);
                            Gr->DrawLine(Gr, cbx0, cby1, cbx1, cby1, _Bar->m_ColLine, _Bar->m_ColLine, false);
                            Gr->DrawLine(Gr, cbx1, cby1, cbx1, cby0, _Bar->m_ColLine, _Bar->m_ColLine, false);
                            Gr->DrawLine(Gr, cbx1, cby0, cbx0, cby0, _Bar->m_ColLine, _Bar->m_ColLine, false);
                        }
                    }
                    else if( ((CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var)->m_Val.m_Button.m_Callback!=NULL )
                    {
                        Gr->DrawRect(Gr, cbx0+1, cby0+1, cbx1+1, cby1+1, _Bar->m_ColBtn, _Bar->m_ColBtn, _Bar->m_ColBtn, _Bar->m_ColBtn);
                    }
                    else if( ((CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var)->m_Val.m_Button.m_Separator==1 )
                    {
                        int LevelSpace = max(_Bar->m_Font->m_CharHeight-6, 4); // space used by DrawHierHandles
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX0+_Bar->m_HierTags.items[h].m_Level*LevelSpace, yh+_Bar->m_Font->m_CharHeight/2, _Bar->m_PosX+_Bar->m_VarX2, yh+_Bar->m_Font->m_CharHeight/2, _Bar->m_ColSeparator, _Bar->m_ColSeparator, false);
                    }
                }
                else if( CTwVar_IsCustom(_Bar->m_HierTags.items[h].m_Var) )
                {   // record custom types
                    CMemberProxy *mProxy = ((CTwVarAtom *)_Bar->m_HierTags.items[h].m_Var)->m_Val.m_Custom.m_MemberProxy;
                    if( mProxy!=NULL && mProxy->m_StructProxy!=NULL )
                    {
                        CCustomRecord *rec = CTwBar_CustomMap_Find(_Bar, mProxy->m_StructProxy);
                        int xMin = _Bar->m_PosX + _Bar->m_VarX0 + _Bar->m_HierTags.items[h].m_Level*LevelSpace;
                        int xMax = _Bar->m_PosX + _Bar->m_VarX2 - 2;
                        int yMin = yh + 1;
                        int yMax = yh + _Bar->m_Font->m_CharHeight;
                        if( rec==NULL )
                        {
                            CCustomEntry entry;
                            entry.m_Key = mProxy->m_StructProxy;
                            entry.m_Value.m_IndexMin = entry.m_Value.m_IndexMax = mProxy->m_MemberIndex;
                            entry.m_Value.m_XMin = xMin;
                            entry.m_Value.m_XMax = xMax;
                            entry.m_Value.m_YMin = yMin;
                            entry.m_Value.m_YMax = yMax;
                            entry.m_Value.m_Y0 = 0; // will be filled by the draw loop below
                            entry.m_Value.m_Y1 = 0; // will be filled by the draw loop below
                            entry.m_Value.m_Var = mProxy->m_VarParent;
                            tw_da_append(&_Bar->m_CustomRecords, entry);
                        }
                        else
                        {
                            rec->m_IndexMin = min(rec->m_IndexMin, mProxy->m_MemberIndex);
                            rec->m_IndexMax = min(rec->m_IndexMax, mProxy->m_MemberIndex);
                            rec->m_XMin = min(rec->m_XMin, xMin);
                            rec->m_XMax = max(rec->m_XMax, xMax);
                            rec->m_YMin = min(rec->m_YMin, yMin);
                            rec->m_YMax = max(rec->m_YMax, yMax);
                            rec->m_Y0 = 0;
                            rec->m_Y1 = 0;
                            assert( rec->m_Var==mProxy->m_VarParent );
                        }
                    }
                }

                yh += _Bar->m_Font->m_CharHeight+_Bar->m_LineSep;
            }

            // Draw custom types
            for( size_t ci=0; ci<_Bar->m_CustomRecords.count; ++ci )
            {
                CStructProxy *sProxy = _Bar->m_CustomRecords.items[ci].m_Key;
                assert( sProxy!=NULL );
                CCustomRecord *r = &_Bar->m_CustomRecords.items[ci].m_Value;
                if( sProxy->m_CustomDrawCallback!=NULL )
                {
                    int y0 = r->m_YMin - max(r->m_IndexMin - sProxy->m_CustomIndexFirst, 0)*(_Bar->m_Font->m_CharHeight + _Bar->m_LineSep);
                    int y1 = y0 + max(sProxy->m_CustomIndexLast - sProxy->m_CustomIndexFirst + 1, 0)*(_Bar->m_Font->m_CharHeight + _Bar->m_LineSep) - 2;
                    if( y0<y1 )
                    {
                        r->m_Y0 = y0;
                        r->m_Y1 = y1;
                        Gr->ChangeViewport(Gr, r->m_XMin, r->m_YMin, r->m_XMax-r->m_XMin+1, r->m_YMax-r->m_YMin+1, 0, y0-r->m_YMin+1);
                        sProxy->m_CustomDrawCallback(r->m_XMax-r->m_XMin, y1-y0, sProxy->m_StructExtData, sProxy->m_StructClientData, _Bar, r->m_Var);
                        Gr->RestoreViewport(Gr);
                    }
                }
            }

            if( _Bar->m_DrawHandles && !_Bar->m_IsPopupList )
            {
                // Draw -/+/o/click/v buttons
                if( (_Bar->m_DrawIncrDecrBtn || _Bar->m_DrawClickBtn || _Bar->m_DrawListBtn || _Bar->m_DrawBoolBtn || _Bar->m_DrawRotoBtn) && _Bar->m_HighlightedLine>=0 && _Bar->m_HighlightedLine<(int)_Bar->m_HierTags.count )
                {
                    int y0 = _Bar->m_PosY + _Bar->m_VarY0 + _Bar->m_HighlightedLine*(_Bar->m_Font->m_CharHeight+_Bar->m_LineSep);
                    if( _Bar->m_DrawIncrDecrBtn )
                    {
                        bool IsMin = false;
                        bool IsMax = false;
                        if( !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) )
                        {
                            const CTwVarAtom *Atom = ((const CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var);
                            double v, vmin, vmax;
                            v = CTwVarAtom_ValueToDouble(Atom);
                            CTwVarAtom_MinMaxStepToDouble(Atom, &vmin, &vmax, NULL);
                            IsMax = (v>=vmax);
                            IsMin = (v<=vmin);
                        }

                        /*
                        Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX2-2*bw+1, y0+1, _Bar->m_PosX+_Bar->m_VarX2-bw-1, y0+_Bar->m_Font->m_CharHeight-2, (_Bar->m_HighlightDecrBtn && !IsMin)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (_Bar->m_HighlightDecrBtn && !IsMin)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (_Bar->m_HighlightDecrBtn && !IsMin)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (_Bar->m_HighlightDecrBtn && !IsMin)?_Bar->m_ColHighBtn:_Bar->m_ColBtn);
                        Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw+1, y0+1, _Bar->m_PosX+_Bar->m_VarX2-1, y0+_Bar->m_Font->m_CharHeight-2, (_Bar->m_HighlightIncrBtn && !IsMax)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (_Bar->m_HighlightIncrBtn && !IsMax)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (_Bar->m_HighlightIncrBtn && !IsMax)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (_Bar->m_HighlightIncrBtn && !IsMax)?_Bar->m_ColHighBtn:_Bar->m_ColBtn);
                        // [-]
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-2*bw+3+(bw>8?1:0), y0+_Bar->m_Font->m_CharHeight/2, _Bar->m_PosX+_Bar->m_VarX2-bw-2-(bw>8?1:0), y0+_Bar->m_Font->m_CharHeight/2, IsMin?_Bar->m_ColValTextRO:_Bar->m_ColTitleText, IsMin?_Bar->m_ColValTextRO:_Bar->m_ColTitleText, false);
                        // [+]
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw+3, y0+_Bar->m_Font->m_CharHeight/2, _Bar->m_PosX+_Bar->m_VarX2-2, y0+_Bar->m_Font->m_CharHeight/2, IsMax?_Bar->m_ColValTextRO:_Bar->m_ColTitleText, IsMax?_Bar->m_ColValTextRO:_Bar->m_ColTitleText, false);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw/2, y0+_Bar->m_Font->m_CharHeight/2-bw/2+2, _Bar->m_PosX+_Bar->m_VarX2-bw/2, y0+_Bar->m_Font->m_CharHeight/2+bw/2-1, IsMax?_Bar->m_ColValTextRO:_Bar->m_ColTitleText, IsMax?_Bar->m_ColValTextRO:_Bar->m_ColTitleText, false);
                        */
                        Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX2-3*bw+1, y0+1, _Bar->m_PosX+_Bar->m_VarX2-2*bw-1, y0+_Bar->m_Font->m_CharHeight-2, (_Bar->m_HighlightDecrBtn && !IsMin)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (_Bar->m_HighlightDecrBtn && !IsMin)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (_Bar->m_HighlightDecrBtn && !IsMin)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (_Bar->m_HighlightDecrBtn && !IsMin)?_Bar->m_ColHighBtn:_Bar->m_ColBtn);
                        Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX2-2*bw+1, y0+1, _Bar->m_PosX+_Bar->m_VarX2-bw-1, y0+_Bar->m_Font->m_CharHeight-2, (_Bar->m_HighlightIncrBtn && !IsMax)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (_Bar->m_HighlightIncrBtn && !IsMax)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (_Bar->m_HighlightIncrBtn && !IsMax)?_Bar->m_ColHighBtn:_Bar->m_ColBtn, (_Bar->m_HighlightIncrBtn && !IsMax)?_Bar->m_ColHighBtn:_Bar->m_ColBtn);
                        // [-]
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-3*bw+3+(bw>8?1:0), y0+_Bar->m_Font->m_CharHeight/2, _Bar->m_PosX+_Bar->m_VarX2-2*bw-2-(bw>8?1:0), y0+_Bar->m_Font->m_CharHeight/2, IsMin?_Bar->m_ColValTextRO:_Bar->m_ColTitleText, IsMin?_Bar->m_ColValTextRO:_Bar->m_ColTitleText, false);
                        // [+]
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-2*bw+3, y0+_Bar->m_Font->m_CharHeight/2, _Bar->m_PosX+_Bar->m_VarX2-bw-2, y0+_Bar->m_Font->m_CharHeight/2, IsMax?_Bar->m_ColValTextRO:_Bar->m_ColTitleText, IsMax?_Bar->m_ColValTextRO:_Bar->m_ColTitleText, false);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw-bw/2, y0+_Bar->m_Font->m_CharHeight/2-bw/2+2, _Bar->m_PosX+_Bar->m_VarX2-bw-bw/2, y0+_Bar->m_Font->m_CharHeight/2+bw/2-1, IsMax?_Bar->m_ColValTextRO:_Bar->m_ColTitleText, IsMax?_Bar->m_ColValTextRO:_Bar->m_ColTitleText, false);
                    }
                    else if( _Bar->m_DrawListBtn )
                    {
                        // [v]
                        int eps = 1;
                        int dx = -1;
                        Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw+1, y0+1, _Bar->m_PosX+_Bar->m_VarX2-1, y0+_Bar->m_Font->m_CharHeight-2, _Bar->m_HighlightListBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightListBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightListBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightListBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw+4+dx, y0+_Bar->m_Font->m_CharHeight/2-eps, _Bar->m_PosX+_Bar->m_VarX2-bw/2+1+dx, y0+_Bar->m_Font->m_CharHeight-4, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw/2+1+dx, y0+_Bar->m_Font->m_CharHeight-4, _Bar->m_PosX+_Bar->m_VarX2-2+dx, y0+_Bar->m_Font->m_CharHeight/2-1, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                    }
                    else if( _Bar->m_DrawBoolBtn )
                    {
                        Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw+1, y0+1, _Bar->m_PosX+_Bar->m_VarX2-1, y0+_Bar->m_Font->m_CharHeight-2, _Bar->m_HighlightBoolBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightBoolBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightBoolBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightBoolBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn);
                        // [x]
                        //Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw/2-bw/6, y0+_Bar->m_Font->m_CharHeight/2-bw/6, _Bar->m_PosX+_Bar->m_VarX2-bw/2+bw/6, y0+_Bar->m_Font->m_CharHeight/2+bw/6, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                        //Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw/2-bw/6, y0+_Bar->m_Font->m_CharHeight/2+bw/6, _Bar->m_PosX+_Bar->m_VarX2-bw/2+bw/6, y0+_Bar->m_Font->m_CharHeight/2-bw/6, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                        // [<>]
                        int s = bw/4;
                        int eps = 1;
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw/2-1, y0+_Bar->m_Font->m_CharHeight/2-s, _Bar->m_PosX+_Bar->m_VarX2-bw/2-s-1, y0+_Bar->m_Font->m_CharHeight/2, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw/2-s-1, y0+_Bar->m_Font->m_CharHeight/2, _Bar->m_PosX+_Bar->m_VarX2-bw/2-eps, y0+_Bar->m_Font->m_CharHeight/2+s+1-eps, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                        //Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw/2+1, y0+_Bar->m_Font->m_CharHeight/2+s, _Bar->m_PosX+_Bar->m_VarX2-bw/2+s+1, y0+_Bar->m_Font->m_CharHeight/2, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                        //Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw/2+s+1, y0+_Bar->m_Font->m_CharHeight/2, _Bar->m_PosX+_Bar->m_VarX2-bw/2+1, y0+_Bar->m_Font->m_CharHeight/2-s, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw/2+2, y0+_Bar->m_Font->m_CharHeight/2-s, _Bar->m_PosX+_Bar->m_VarX2-bw/2+s+2, y0+_Bar->m_Font->m_CharHeight/2, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw/2+s+2, y0+_Bar->m_Font->m_CharHeight/2, _Bar->m_PosX+_Bar->m_VarX2-bw/2+1+eps, y0+_Bar->m_Font->m_CharHeight/2+s+1-eps, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                    }

                    if( _Bar->m_DrawRotoBtn )
                    {
                        // [o] rotoslider button
                        /*
                        Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX1-bw-1, y0+1, _Bar->m_PosX+_Bar->m_VarX1-3, y0+_Bar->m_Font->m_CharHeight-2, _Bar->m_HighlightRotoBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightRotoBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightRotoBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightRotoBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX1-bw+bw/2-2, y0+_Bar->m_Font->m_CharHeight/2-1, _Bar->m_PosX+_Bar->m_VarX1-bw+bw/2-1, y0+_Bar->m_Font->m_CharHeight/2-1, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX1-bw+bw/2-3, y0+_Bar->m_Font->m_CharHeight/2+0, _Bar->m_PosX+_Bar->m_VarX1-bw+bw/2+0, y0+_Bar->m_Font->m_CharHeight/2+0, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX1-bw+bw/2-3, y0+_Bar->m_Font->m_CharHeight/2+1, _Bar->m_PosX+_Bar->m_VarX1-bw+bw/2+0, y0+_Bar->m_Font->m_CharHeight/2+1, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX1-bw+bw/2-2, y0+_Bar->m_Font->m_CharHeight/2+2, _Bar->m_PosX+_Bar->m_VarX1-bw+bw/2-1, y0+_Bar->m_Font->m_CharHeight/2+2, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
                        */
                        /*
                        Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX2-3*bw+1, y0+1, _Bar->m_PosX+_Bar->m_VarX2-2*bw-1, y0+_Bar->m_Font->m_CharHeight-2, _Bar->m_HighlightRotoBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightRotoBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightRotoBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightRotoBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-3*bw+bw/2+0, y0+_Bar->m_Font->m_CharHeight/2-1, _Bar->m_PosX+_Bar->m_VarX2-3*bw+bw/2+1, y0+_Bar->m_Font->m_CharHeight/2-1, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-3*bw+bw/2-1, y0+_Bar->m_Font->m_CharHeight/2+0, _Bar->m_PosX+_Bar->m_VarX2-3*bw+bw/2+2, y0+_Bar->m_Font->m_CharHeight/2+0, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-3*bw+bw/2-1, y0+_Bar->m_Font->m_CharHeight/2+1, _Bar->m_PosX+_Bar->m_VarX2-3*bw+bw/2+2, y0+_Bar->m_Font->m_CharHeight/2+1, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-3*bw+bw/2+0, y0+_Bar->m_Font->m_CharHeight/2+2, _Bar->m_PosX+_Bar->m_VarX2-3*bw+bw/2+1, y0+_Bar->m_Font->m_CharHeight/2+2, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
                        */
                        int dy = 0;
                        Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw+1, y0+1, _Bar->m_PosX+_Bar->m_VarX2-1, y0+_Bar->m_Font->m_CharHeight-2, _Bar->m_HighlightRotoBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightRotoBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightRotoBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightRotoBtn?_Bar->m_ColHighBtn:_Bar->m_ColBtn);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw+bw/2+0, y0+_Bar->m_Font->m_CharHeight/2-1+dy, _Bar->m_PosX+_Bar->m_VarX2-bw+bw/2+1, y0+_Bar->m_Font->m_CharHeight/2-1+dy, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw+bw/2-1, y0+_Bar->m_Font->m_CharHeight/2+0+dy, _Bar->m_PosX+_Bar->m_VarX2-bw+bw/2+2, y0+_Bar->m_Font->m_CharHeight/2+0+dy, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw+bw/2-1, y0+_Bar->m_Font->m_CharHeight/2+1+dy, _Bar->m_PosX+_Bar->m_VarX2-bw+bw/2+2, y0+_Bar->m_Font->m_CharHeight/2+1+dy, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                        Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX2-bw+bw/2+0, y0+_Bar->m_Font->m_CharHeight/2+2+dy, _Bar->m_PosX+_Bar->m_VarX2-bw+bw/2+1, y0+_Bar->m_Font->m_CharHeight/2+2+dy, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                    }
                }
                

                // Draw value width slider
                if( !_Bar->m_HighlightValWidth )
                {
                    color32 col = _Bar->m_DarkText ? COLOR32_WHITE : _Bar->m_ColTitleText;
                    Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX1-2, _Bar->m_PosY+_Bar->m_VarY0-8, _Bar->m_PosX+_Bar->m_VarX1-1, _Bar->m_PosY+_Bar->m_VarY0-4, col, col, col, col);
                    Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX1-1, _Bar->m_PosY+_Bar->m_VarY0-3, _Bar->m_PosX+_Bar->m_VarX1, _Bar->m_PosY+_Bar->m_VarY0-3, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
                    Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX1, _Bar->m_PosY+_Bar->m_VarY0-3, _Bar->m_PosX+_Bar->m_VarX1, _Bar->m_PosY+_Bar->m_VarY0-8, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
                }
                else
                {
                    color32 col = _Bar->m_DarkText ? COLOR32_WHITE : _Bar->m_ColTitleText;
                    Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX1-2, _Bar->m_PosY+_Bar->m_VarY0-8, _Bar->m_PosX+_Bar->m_VarX1-1, _Bar->m_PosY+_Bar->m_VarY1, col, col, col, col);
                    Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX1-1, _Bar->m_PosY+_Bar->m_VarY1+1, _Bar->m_PosX+_Bar->m_VarX1, _Bar->m_PosY+_Bar->m_VarY1+1, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
                    Gr->DrawLine(Gr, _Bar->m_PosX+_Bar->m_VarX1, _Bar->m_PosY+_Bar->m_VarY1+1, _Bar->m_PosX+_Bar->m_VarX1, _Bar->m_PosY+_Bar->m_VarY0-8, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
                }

                // Draw labels & values headers
                if (_Bar->m_HighlightLabelsHeader) 
                {
                    Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX0, _Bar->m_PosY+_Bar->m_Font->m_CharHeight+2, _Bar->m_PosX+_Bar->m_VarX1-4, _Bar->m_PosY+_Bar->m_VarY0-1, _Bar->m_ColHighBg0, _Bar->m_ColHighBg0, _Bar->m_ColHighBg1, _Bar->m_ColHighBg1);
                }
                if (_Bar->m_HighlightValuesHeader) 
                {
                    Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_VarX1+2, _Bar->m_PosY+_Bar->m_Font->m_CharHeight+2, _Bar->m_PosX+_Bar->m_VarX2, _Bar->m_PosY+_Bar->m_VarY0-1, _Bar->m_ColHighBg0, _Bar->m_ColHighBg0, _Bar->m_ColHighBg1, _Bar->m_ColHighBg1);
                }
            }

            // Draw key shortcut text
            if( _Bar->m_HighlightedLine>=0 && _Bar->m_HighlightedLine==_Bar->m_ShortcutLine && !_Bar->m_IsPopupList && !_Bar->m_EditInPlace.m_Active )
            {
                PERF( Timer.Reset(); )  
                Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_Font->m_CharHeight-2, _Bar->m_PosY+_Bar->m_VarY1+1, _Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight-2, _Bar->m_PosY+_Bar->m_VarY1+1+_Bar->m_Font->m_CharHeight, _Bar->m_ColShortcutBg, _Bar->m_ColShortcutBg, _Bar->m_ColShortcutBg, _Bar->m_ColShortcutBg);
                Gr->DrawText(Gr, _Bar->m_ShortcutTextObj, _Bar->m_PosX+_Bar->m_Font->m_CharHeight, _Bar->m_PosY+_Bar->m_VarY1+1, _Bar->m_ColShortcutText, 0);
                PERF( DT = Timer.GetTime(); printf("Shortcut=%.4fms ", 1000.0*DT); )
            }
            else if( (_Bar->m_HighlightLabelsHeader || _Bar->m_HighlightValuesHeader) && !_Bar->m_IsPopupList && !_Bar->m_EditInPlace.m_Active )
            {
                Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_Font->m_CharHeight-2, _Bar->m_PosY+_Bar->m_VarY1+1, _Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight-2, _Bar->m_PosY+_Bar->m_VarY1+1+_Bar->m_Font->m_CharHeight, _Bar->m_ColShortcutBg, _Bar->m_ColShortcutBg, _Bar->m_ColShortcutBg, _Bar->m_ColShortcutBg);
                Gr->DrawText(Gr, _Bar->m_HeadersTextObj, _Bar->m_PosX+_Bar->m_Font->m_CharHeight, _Bar->m_PosY+_Bar->m_VarY1+1, _Bar->m_ColShortcutText, 0);
            }
            else if( _Bar->m_IsHelpBar )
            {
                if( g_TwMgr->m_KeyPressedTextObj && sdslen(g_TwMgr->m_KeyPressedStr)>0 ) // Draw key pressed
                {
                    if( g_TwMgr->m_KeyPressedBuildText )
                    {
                        sds Str = sdsdup(g_TwMgr->m_KeyPressedStr);
                        ClampText(&Str, _Bar->m_Font, _Bar->m_Width-2*_Bar->m_Font->m_CharHeight);
                        const char *StrC = Str;
                        g_TwMgr->m_Graph->BuildText(g_TwMgr->m_Graph, g_TwMgr->m_KeyPressedTextObj, &StrC, NULL, NULL, 1, g_TwMgr->m_HelpBar->m_Font, 0, 0);
                        sdsfree(Str);
                        g_TwMgr->m_KeyPressedBuildText = false;
                        g_TwMgr->m_KeyPressedTime = (float)glfwGetTime();
                    }
                    if( (float)glfwGetTime()>g_TwMgr->m_KeyPressedTime+1.0f ) // draw key pressed at least 1 second
                        sdsclear(g_TwMgr->m_KeyPressedStr);
                    PERF( Timer.Reset(); )  
                    Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_Font->m_CharHeight-2, _Bar->m_PosY+_Bar->m_VarY1+1, _Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight-2, _Bar->m_PosY+_Bar->m_VarY1+1+_Bar->m_Font->m_CharHeight, _Bar->m_ColShortcutBg, _Bar->m_ColShortcutBg, _Bar->m_ColShortcutBg, _Bar->m_ColShortcutBg);
                    Gr->DrawText(Gr, g_TwMgr->m_KeyPressedTextObj, _Bar->m_PosX+_Bar->m_Font->m_CharHeight, _Bar->m_PosY+_Bar->m_VarY1+1, _Bar->m_ColShortcutText, 0);
                    PERF( DT = Timer.GetTime(); printf("KeyPressed=%.4fms ", 1000.0*DT); )  
                }
                else
                {
                    if( g_TwMgr->m_InfoBuildText )
                    {
                        sds Info = sdsnew("atb ");
                        char Ver[64];
                        sprintf(Ver, " %d.%02d", TW_VERSION/100, TW_VERSION%100);
                        Info = sdscat(Info, Ver);
                        ClampText(&Info, _Bar->m_Font, _Bar->m_Width-2*_Bar->m_Font->m_CharHeight);
                        const char *InfoC = Info;
                        g_TwMgr->m_Graph->BuildText(g_TwMgr->m_Graph, g_TwMgr->m_InfoTextObj, &InfoC, NULL, NULL, 1, g_TwMgr->m_HelpBar->m_Font, 0, 0);
                        sdsfree(Info);
                        g_TwMgr->m_InfoBuildText = false;
                    }
                    PERF( Timer.Reset(); )  
                    Gr->DrawRect(Gr, _Bar->m_PosX+_Bar->m_Font->m_CharHeight-2, _Bar->m_PosY+_Bar->m_VarY1+1, _Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight-2, _Bar->m_PosY+_Bar->m_VarY1+1+_Bar->m_Font->m_CharHeight, _Bar->m_ColShortcutBg, _Bar->m_ColShortcutBg, _Bar->m_ColShortcutBg, _Bar->m_ColShortcutBg);
                    Gr->DrawText(Gr, g_TwMgr->m_InfoTextObj, _Bar->m_PosX+_Bar->m_Font->m_CharHeight, _Bar->m_PosY+_Bar->m_VarY1+1, _Bar->m_ColInfoText, 0);
                    PERF( DT = Timer.GetTime(); printf("Info=%.4fms ", 1000.0*DT); )
                }
            }

            if( !_Bar->m_IsPopupList )
            {
                // Draw RotoSlider
                CTwBar_RotoDraw(_Bar);

                // Draw EditInPlace
                CTwBar_EditInPlaceDraw(_Bar);
            }

            if( g_TwMgr->m_PopupBar!=NULL && _Bar!=g_TwMgr->m_PopupBar )
            {
                // darken bar if a popup bar is displayed
                Gr->DrawRect(Gr, _Bar->m_PosX, _Bar->m_PosY, _Bar->m_PosX+_Bar->m_Width-1, _Bar->m_PosY+_Bar->m_Height-1, 0x1F000000, 0x1F000000, 0x1F000000, 0x1F000000);
            }
        }
    }
    else // minimized
    {
        int vpx, vpy, vpw, vph;
        vpx = 0;
        vpy = 0;
        vpw = g_TwMgr->m_WndWidth;
        vph = g_TwMgr->m_WndHeight;
        if( g_TwMgr->m_IconMarginX>0 )
        {
            vpx = min(g_TwMgr->m_IconMarginX, vpw/3);
            vpw -= 2 * vpx;
        }
        if( g_TwMgr->m_IconMarginY>0 )
        {
            vpy = min(g_TwMgr->m_IconMarginY, vph/3);
            vph -= 2 * vpy;
        }

        int MinXOffset = 0, MinYOffset = 0;
        if( g_TwMgr->m_IconPos==3 )         // top-right
        {
            if( g_TwMgr->m_IconAlign==1 )   // horizontal
            {
                int n = max(1, vpw/_Bar->m_Font->m_CharHeight-1);
                _Bar->m_MinPosX = vpx + vpw-((_Bar->m_MinNumber%n)+1)*_Bar->m_Font->m_CharHeight;
                _Bar->m_MinPosY = vpy + (_Bar->m_MinNumber/n)*_Bar->m_Font->m_CharHeight;
                MinYOffset = _Bar->m_Font->m_CharHeight;
                MinXOffset = -_Bar->m_TitleWidth;
            }
            else // vertical
            {
                int n = max(1, vph/_Bar->m_Font->m_CharHeight-1);
                _Bar->m_MinPosY = vpy + (_Bar->m_MinNumber%n)*_Bar->m_Font->m_CharHeight;
                _Bar->m_MinPosX = vpx + vpw-((_Bar->m_MinNumber/n)+1)*_Bar->m_Font->m_CharHeight;
                MinXOffset = -_Bar->m_TitleWidth-_Bar->m_Font->m_CharHeight;
            }
        }
        else if( g_TwMgr->m_IconPos==2 )    // top-left
        {
            if( g_TwMgr->m_IconAlign==1 )   // horizontal
            {
                int n = max(1, vpw/_Bar->m_Font->m_CharHeight-1);
                _Bar->m_MinPosX = vpx + (_Bar->m_MinNumber%n)*_Bar->m_Font->m_CharHeight;
                _Bar->m_MinPosY = vpy + (_Bar->m_MinNumber/n)*_Bar->m_Font->m_CharHeight;
                MinYOffset = _Bar->m_Font->m_CharHeight;
            }
            else // vertical
            {
                int n = max(1, vph/_Bar->m_Font->m_CharHeight-1);
                _Bar->m_MinPosY = vpy + (_Bar->m_MinNumber%n)*_Bar->m_Font->m_CharHeight;
                _Bar->m_MinPosX = vpx + (_Bar->m_MinNumber/n)*_Bar->m_Font->m_CharHeight;
                MinXOffset = _Bar->m_Font->m_CharHeight;
            }
        }
        else if( g_TwMgr->m_IconPos==1 )    // bottom-right
        {
            if( g_TwMgr->m_IconAlign==1 )   // horizontal
            {
                int n = max(1, vpw/_Bar->m_Font->m_CharHeight-1);
                _Bar->m_MinPosX = vpx + vpw-((_Bar->m_MinNumber%n)+1)*_Bar->m_Font->m_CharHeight;
                _Bar->m_MinPosY = vpy + vph-((_Bar->m_MinNumber/n)+1)*_Bar->m_Font->m_CharHeight;
                MinYOffset = -_Bar->m_Font->m_CharHeight;
                MinXOffset = -_Bar->m_TitleWidth;
            }
            else // vertical
            {
                int n = max(1, vph/_Bar->m_Font->m_CharHeight-1);
                _Bar->m_MinPosY = vpy + vph-((_Bar->m_MinNumber%n)+1)*_Bar->m_Font->m_CharHeight;
                _Bar->m_MinPosX = vpx + vpw-((_Bar->m_MinNumber/n)+1)*_Bar->m_Font->m_CharHeight;
                MinXOffset = -_Bar->m_TitleWidth-_Bar->m_Font->m_CharHeight;
            }
        }
        else // bottom-left
        {
            if( g_TwMgr->m_IconAlign==1 )   // horizontal
            {
                int n = max(1, vpw/_Bar->m_Font->m_CharHeight-1);
                _Bar->m_MinPosX = vpx + (_Bar->m_MinNumber%n)*_Bar->m_Font->m_CharHeight;
                _Bar->m_MinPosY = vpy + vph-((_Bar->m_MinNumber/n)+1)*_Bar->m_Font->m_CharHeight;
                MinYOffset = -_Bar->m_Font->m_CharHeight;
            }
            else // vertical
            {
                int n = max(1, vph/_Bar->m_Font->m_CharHeight-1);
                _Bar->m_MinPosY = vpy + vph-((_Bar->m_MinNumber%n)+1)*_Bar->m_Font->m_CharHeight;
                _Bar->m_MinPosX = vpx + (_Bar->m_MinNumber/n)*_Bar->m_Font->m_CharHeight;
                MinXOffset = _Bar->m_Font->m_CharHeight;
            }
        }

        if( _Bar->m_HighlightMaximize )
        {
            // Draw title
            if( _DrawPart&DRAW_BG )
            {
                Gr->DrawRect(Gr, _Bar->m_MinPosX, _Bar->m_MinPosY, _Bar->m_MinPosX+_Bar->m_Font->m_CharHeight, _Bar->m_MinPosY+_Bar->m_Font->m_CharHeight, _Bar->m_ColTitleUnactiveBg, _Bar->m_ColTitleUnactiveBg, _Bar->m_ColTitleUnactiveBg, _Bar->m_ColTitleUnactiveBg);
                Gr->DrawRect(Gr, _Bar->m_MinPosX+MinXOffset, _Bar->m_MinPosY+MinYOffset, _Bar->m_MinPosX+MinXOffset+_Bar->m_TitleWidth+_Bar->m_Font->m_CharHeight, _Bar->m_MinPosY+MinYOffset+_Bar->m_Font->m_CharHeight, _Bar->m_ColTitleUnactiveBg, _Bar->m_ColTitleUnactiveBg, _Bar->m_ColTitleUnactiveBg, _Bar->m_ColTitleUnactiveBg);
            }
            if( _DrawPart&DRAW_CONTENT )
            {
                if( _Bar->m_ColTitleShadow!=0 )
                    Gr->DrawText(Gr, _Bar->m_TitleTextObj, _Bar->m_MinPosX+MinXOffset+_Bar->m_Font->m_CharHeight/2, _Bar->m_MinPosY+1+MinYOffset, _Bar->m_ColTitleShadow, 0);
                Gr->DrawText(Gr, _Bar->m_TitleTextObj, _Bar->m_MinPosX+MinXOffset+_Bar->m_Font->m_CharHeight/2, _Bar->m_MinPosY+MinYOffset, _Bar->m_ColTitleText, 0);
            }
        }

        if( !_Bar->m_IsHelpBar )
        {
            // Draw maximize button
            int xm = _Bar->m_MinPosX+2, wm=_Bar->m_Font->m_CharHeight-6;
            wm = (wm<6) ? 6 : wm;
            if( _DrawPart&DRAW_BG )
                Gr->DrawRect(Gr, xm+1, _Bar->m_MinPosY+4, xm+wm-1, _Bar->m_MinPosY+3+wm, _Bar->m_HighlightMaximize?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightMaximize?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightMaximize?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightMaximize?_Bar->m_ColHighBtn:_Bar->m_ColBtn);
            if( _DrawPart&DRAW_CONTENT )
            {
                Gr->DrawLine(Gr, xm, _Bar->m_MinPosY+3, xm+wm, _Bar->m_MinPosY+3, _Bar->m_ColLine, _Bar->m_ColLine, false);
                Gr->DrawLine(Gr, xm+wm, _Bar->m_MinPosY+3, xm+wm, _Bar->m_MinPosY+3+wm, _Bar->m_ColLine, _Bar->m_ColLine, false);
                Gr->DrawLine(Gr, xm+wm, _Bar->m_MinPosY+3+wm, xm, _Bar->m_MinPosY+3+wm, _Bar->m_ColLine, _Bar->m_ColLine, false);
                Gr->DrawLine(Gr, xm, _Bar->m_MinPosY+3+wm, xm, _Bar->m_MinPosY+3, _Bar->m_ColLine, _Bar->m_ColLine, false);
                Gr->DrawLine(Gr, xm+wm+1, _Bar->m_MinPosY+4, xm+wm+1, _Bar->m_MinPosY+4+wm, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
                Gr->DrawLine(Gr, xm+wm+1, _Bar->m_MinPosY+4+wm, xm, _Bar->m_MinPosY+4+wm, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
                Gr->DrawLine(Gr, xm+wm/3-1, _Bar->m_MinPosY+3+wm-wm/3, xm+wm/2, _Bar->m_MinPosY+6, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
                Gr->DrawLine(Gr, xm+wm-wm/3+1, _Bar->m_MinPosY+3+wm-wm/3, xm+wm/2, _Bar->m_MinPosY+6, _Bar->m_ColTitleText, _Bar->m_ColTitleText, true);
            }
        }
        else
        {
            // Draw help button
            int xm = _Bar->m_MinPosX+2, wm=_Bar->m_Font->m_CharHeight-6;
            wm = (wm<6) ? 6 : wm;
            if( _DrawPart&DRAW_BG )
                Gr->DrawRect(Gr, xm+1, _Bar->m_MinPosY+4, xm+wm-1, _Bar->m_MinPosY+3+wm, _Bar->m_HighlightMaximize?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightMaximize?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightMaximize?_Bar->m_ColHighBtn:_Bar->m_ColBtn, _Bar->m_HighlightMaximize?_Bar->m_ColHighBtn:_Bar->m_ColBtn);
            if( _DrawPart&DRAW_CONTENT )
            {
                Gr->DrawLine(Gr, xm, _Bar->m_MinPosY+3, xm+wm, _Bar->m_MinPosY+3, _Bar->m_ColLine, _Bar->m_ColLine, false);
                Gr->DrawLine(Gr, xm+wm, _Bar->m_MinPosY+3, xm+wm, _Bar->m_MinPosY+3+wm, _Bar->m_ColLine, _Bar->m_ColLine, false);
                Gr->DrawLine(Gr, xm+wm, _Bar->m_MinPosY+3+wm, xm, _Bar->m_MinPosY+3+wm, _Bar->m_ColLine, _Bar->m_ColLine, false);
                Gr->DrawLine(Gr, xm, _Bar->m_MinPosY+3+wm, xm, _Bar->m_MinPosY+3, _Bar->m_ColLine, _Bar->m_ColLine, false);
                Gr->DrawLine(Gr, xm+wm+1, _Bar->m_MinPosY+4, xm+wm+1, _Bar->m_MinPosY+4+wm, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
                Gr->DrawLine(Gr, xm+wm+1, _Bar->m_MinPosY+4+wm, xm, _Bar->m_MinPosY+4+wm, _Bar->m_ColLineShadow, _Bar->m_ColLineShadow, false);
                Gr->DrawLine(Gr, xm+wm/2-wm/6, _Bar->m_MinPosY+3+wm/4, xm+wm-wm/3, _Bar->m_MinPosY+3+wm/4, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
                Gr->DrawLine(Gr, xm+wm-wm/3, _Bar->m_MinPosY+3+wm/4, xm+wm-wm/3, _Bar->m_MinPosY+3+wm/2, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
                Gr->DrawLine(Gr, xm+wm-wm/3, _Bar->m_MinPosY+3+wm/2, xm+wm/2, _Bar->m_MinPosY+3+wm/2, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
                Gr->DrawLine(Gr, xm+wm/2, _Bar->m_MinPosY+3+wm/2, xm+wm/2, _Bar->m_MinPosY+3+wm-wm/4, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
                Gr->DrawLine(Gr, xm+wm/2, _Bar->m_MinPosY+3+wm-wm/4+1, xm+wm/2, _Bar->m_MinPosY+3+wm-wm/4+2, _Bar->m_ColTitleText, _Bar->m_ColTitleText, false);
            }
        }
    }
}

//  ---------------------------------------------------------------------------

bool CTwBar_MouseMotion(CTwBar *_Bar, int _X, int _Y)
{
    assert(g_TwMgr->m_Graph && g_TwMgr->m_WndHeight>0 && g_TwMgr->m_WndWidth>0);
    if( !_Bar->m_UpToDate )
        CTwBar_Update(_Bar);
    
    bool Handled = false;
    bool CustomArea = false;
    if( !_Bar->m_IsMinimized )
    {
        bool InBar = (_X>=_Bar->m_PosX && _X<_Bar->m_PosX+_Bar->m_Width && _Y>=_Bar->m_PosY && _Y<_Bar->m_PosY+_Bar->m_Height);
        for( size_t ib=0; ib<g_TwMgr->m_Bars.count; ++ib )
            if( g_TwMgr->m_Bars.items[ib]!=NULL )
            {
                g_TwMgr->m_Bars.items[ib]->m_DrawHandles = false;
                g_TwMgr->m_Bars.items[ib]->m_HighlightTitle = false;
            }
        _Bar->m_DrawHandles = InBar;
        const int ContainedMargin = 32;

        if( !_Bar->m_MouseDrag )
        {   
            Handled = InBar;
            _Bar->m_HighlightedLine = -1;
            _Bar->m_HighlightIncrBtn = false;
            _Bar->m_HighlightDecrBtn = false;
            _Bar->m_HighlightRotoBtn = false;
            if( abs(_Bar->m_MouseOriginX-_X)>6 || abs(_Bar->m_MouseOriginY-_Y)>6 )
                _Bar->m_HighlightClickBtn = false;
            _Bar->m_HighlightListBtn = false;
            _Bar->m_HighlightTitle = false;
            _Bar->m_HighlightScroll = false;
            _Bar->m_HighlightUpScroll = false;
            _Bar->m_HighlightDnScroll = false;
            _Bar->m_HighlightMinimize = false;
            _Bar->m_HighlightFont = false;
            _Bar->m_HighlightValWidth = false;
            _Bar->m_HighlightLabelsHeader = false;
            _Bar->m_HighlightValuesHeader = false;
            //if( InBar && _X>_Bar->m_PosX+_Bar->m_Font->m_CharHeight+1 && _X<_Bar->m_PosX+_Bar->m_VarX2 && _Y>=_Bar->m_PosY+_Bar->m_VarY0 && _Y<_Bar->m_PosY+_Bar->m_VarY1 )
            if( InBar && _X>_Bar->m_PosX+2 && _X<_Bar->m_PosX+_Bar->m_VarX2 && _Y>=_Bar->m_PosY+_Bar->m_VarY0 && _Y<_Bar->m_PosY+_Bar->m_VarY1 )
            {   // mouse over var line
                _Bar->m_HighlightedLine = (_Y-_Bar->m_PosY-_Bar->m_VarY0)/(_Bar->m_Font->m_CharHeight+_Bar->m_LineSep);
                if( _Bar->m_HighlightedLine>=(int)_Bar->m_HierTags.count )
                    _Bar->m_HighlightedLine = -1;
                else if(_Bar->m_HighlightedLine>=0)
                    _Bar->m_HighlightedLineLastValid = _Bar->m_HighlightedLine;
                if( _Bar->m_HighlightedLine<0 || _Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var==NULL || CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) )
                    ANT_SET_CURSOR(Arrow);
                else
                {
                    if( !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) && ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_NoSlider )
                    {
                        if( ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_ReadOnly && !_Bar->m_IsHelpBar 
                            && !(((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_Type==TW_TYPE_BUTTON && ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_Val.m_Button.m_Callback==NULL) )
                            ANT_SET_CURSOR(No); //(Arrow);
                        else
                        {
                            ANT_SET_CURSOR(Arrow);
                            CustomArea = true;
                        }

                        if( _Bar->m_DrawListBtn )
                        {
                            _Bar->m_HighlightListBtn = true;
                            CustomArea = false;
                        }
                        if( _Bar->m_DrawBoolBtn )
                        {
                            _Bar->m_HighlightBoolBtn = true;
                            CustomArea = false;
                        }
                    }
                    else if( _Bar->m_DrawRotoBtn && ( _X>=_Bar->m_PosX+_Bar->m_VarX2-IncrBtnWidth(_Bar->m_Font->m_CharHeight) || _X<_Bar->m_PosX+_Bar->m_VarX1 ) )  // [o] button
                    //else if( _Bar->m_DrawRotoBtn && _X<_Bar->m_PosX+_Bar->m_VarX1 ) // [o] button
                    {
                        _Bar->m_HighlightRotoBtn = true;
                        ANT_SET_CURSOR(Point);
                    }
                    else if( _Bar->m_DrawIncrDecrBtn && _X>=_Bar->m_PosX+_Bar->m_VarX2-2*IncrBtnWidth(_Bar->m_Font->m_CharHeight) ) // [+] button
                    {
                        _Bar->m_HighlightIncrBtn = true;
                        ANT_SET_CURSOR(Arrow);
                    }
                    else if( _Bar->m_DrawIncrDecrBtn && _X>=_Bar->m_PosX+_Bar->m_VarX2-3*IncrBtnWidth(_Bar->m_Font->m_CharHeight) ) // [-] button
                    {
                        _Bar->m_HighlightDecrBtn = true;
                        ANT_SET_CURSOR(Arrow);
                    }
                    else if( !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) && ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_ReadOnly )
                    {
                        if( !_Bar->m_IsHelpBar )
                            ANT_SET_CURSOR(No);
                        else
                            ANT_SET_CURSOR(Arrow);
                    }
                    else
                        //ANT_SET_CURSOR(Point);
                        ANT_SET_CURSOR(IBeam);
                }
            }
            else if( InBar && _Bar->m_Movable && !_Bar->m_IsPopupList && _X>=_Bar->m_PosX+2*_Bar->m_Font->m_CharHeight && _X<_Bar->m_PosX+_Bar->m_Width-2*_Bar->m_Font->m_CharHeight && _Y<_Bar->m_PosY+_Bar->m_Font->m_CharHeight )
            {   // mouse over title
                _Bar->m_HighlightTitle = true;
                ANT_SET_CURSOR(Move);
            }
            else if ( InBar && !_Bar->m_IsPopupList && _X>=_Bar->m_PosX+_Bar->m_VarX1-5 && _X<_Bar->m_PosX+_Bar->m_VarX1+5 && _Y>_Bar->m_PosY+_Bar->m_Font->m_CharHeight && _Y<_Bar->m_PosY+_Bar->m_VarY0 )
            {   // mouse over ValuesWidth handle
                _Bar->m_HighlightValWidth = true;
                ANT_SET_CURSOR(WE);
            }
            else if ( InBar && !_Bar->m_IsPopupList && !_Bar->m_IsHelpBar && _X>=_Bar->m_PosX+_Bar->m_VarX0 && _X<_Bar->m_PosX+_Bar->m_VarX1-5 && _Y>_Bar->m_PosY+_Bar->m_Font->m_CharHeight && _Y<_Bar->m_PosY+_Bar->m_VarY0 )
            {   // mouse over left column header
                _Bar->m_HighlightLabelsHeader = true;
                ANT_SET_CURSOR(Arrow);
            }
            else if ( InBar && !_Bar->m_IsPopupList && _X>=_Bar->m_PosX+_Bar->m_VarX1+5 && _X<_Bar->m_PosX+_Bar->m_VarX2 && _Y>_Bar->m_PosY+_Bar->m_Font->m_CharHeight && _Y<_Bar->m_PosY+_Bar->m_VarY0 )
            {   // mouse over right column header
                _Bar->m_HighlightValuesHeader = true;
                ANT_SET_CURSOR(Arrow);
            }
            //else if( InBar && _Bar->m_NbDisplayedLines<_Bar->m_NbHierLines && _X>=_Bar->m_PosX && _X<_Bar->m_PosX+_Bar->m_Font->m_CharHeight && _Y>=_Bar->m_ScrollY0 && _Y<_Bar->m_ScrollY1 )
            else if( InBar && _Bar->m_NbDisplayedLines<_Bar->m_NbHierLines && _X>=_Bar->m_PosX+_Bar->m_VarX2+2 && _X<_Bar->m_PosX+_Bar->m_Width-2 && _Y>=_Bar->m_ScrollY0 && _Y<_Bar->m_ScrollY1 )
            {
                _Bar->m_HighlightScroll = true;
              #ifdef ANT_WINDOWS
                ANT_SET_CURSOR(NS);
              #else
                ANT_SET_CURSOR(Arrow);
              #endif
            }
            else if( InBar && _X>=_Bar->m_PosX+_Bar->m_VarX2+2 && _X<_Bar->m_PosX+_Bar->m_Width-2 && _Y>=_Bar->m_PosY+_Bar->m_VarY0 && _Y<_Bar->m_ScrollY0 )
            {
                _Bar->m_HighlightUpScroll = true;
                ANT_SET_CURSOR(Arrow);
            }
            else if( InBar && _X>=_Bar->m_PosX+_Bar->m_VarX2+2 && _X<_Bar->m_PosX+_Bar->m_Width-2 && _Y>=_Bar->m_ScrollY1 && _Y<_Bar->m_PosY+_Bar->m_VarY1 )
            {
                _Bar->m_HighlightDnScroll = true;
                ANT_SET_CURSOR(Arrow);
            }
            else if( InBar && _Bar->m_Resizable && !_Bar->m_IsPopupList && _X>=_Bar->m_PosX && _X<_Bar->m_PosX+_Bar->m_Font->m_CharHeight && _Y>=_Bar->m_PosY && _Y<_Bar->m_PosY+_Bar->m_Font->m_CharHeight )
                ANT_SET_CURSOR(TopLeft);
            else if( InBar && !_Bar->m_IsPopupList && _X>=_Bar->m_PosX && _X<_Bar->m_PosX+_Bar->m_Font->m_CharHeight && _Y>=_Bar->m_PosY+_Bar->m_Height-_Bar->m_Font->m_CharHeight && _Y<_Bar->m_PosY+_Bar->m_Height )
                ANT_SET_CURSOR(BottomLeft);
            else if( InBar && _Bar->m_Resizable && !_Bar->m_IsPopupList && _X>=_Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight && _X<_Bar->m_PosX+_Bar->m_Width && _Y>=_Bar->m_PosY && _Y<_Bar->m_PosY+_Bar->m_Font->m_CharHeight )
                ANT_SET_CURSOR(TopRight);
            else if( InBar && _Bar->m_Resizable && !_Bar->m_IsPopupList && _X>=_Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight && _X<_Bar->m_PosX+_Bar->m_Width && _Y>=_Bar->m_PosY+_Bar->m_Height-_Bar->m_Font->m_CharHeight && _Y<_Bar->m_PosY+_Bar->m_Height )
                ANT_SET_CURSOR(BottomRight);
            else if( InBar && g_TwMgr->m_FontResizable && !_Bar->m_IsPopupList && _X>=_Bar->m_PosX+_Bar->m_Font->m_CharHeight && _X<_Bar->m_PosX+2*_Bar->m_Font->m_CharHeight && _Y<_Bar->m_PosY+_Bar->m_Font->m_CharHeight )
            {
                _Bar->m_HighlightFont = true;
                ANT_SET_CURSOR(Arrow);
            }
            else if( InBar && _Bar->m_Iconifiable && !_Bar->m_IsPopupList && _X>=_Bar->m_PosX+_Bar->m_Width-2*_Bar->m_Font->m_CharHeight && _X<_Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight && _Y<_Bar->m_PosY+_Bar->m_Font->m_CharHeight )
            {
                _Bar->m_HighlightMinimize = true;
                ANT_SET_CURSOR(Arrow);
            }
            else if( _Bar->m_IsHelpBar && InBar && _X>=_Bar->m_PosX+_Bar->m_VarX0 && _X<_Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight && _Y>_Bar->m_PosY+_Bar->m_Height-_Bar->m_Font->m_CharHeight && _Y<_Bar->m_PosY+_Bar->m_Height )
                ANT_SET_CURSOR(Arrow); //(Hand);   // web link
            else // if( InBar )
                ANT_SET_CURSOR(Arrow);
        }
        else
        {
            if( _Bar->m_MouseDragVar && _Bar->m_HighlightedLine>=0 && _Bar->m_HighlightedLine<(int)_Bar->m_HierTags.count && _Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var && !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) )
            {
                /*
                CTwVarAtom *Var = ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var);
                int Delta = _X-_Bar->m_MouseOriginX;
                if( Delta!=0 )
                {
                    if( !Var->m_NoSlider && !Var->m_ReadOnly )
                    {
                        Var->Increment(Delta);
                        CTwBar_NotUpToDate(_Bar);
                    }
                    _Bar->m_VarHasBeenIncr = true;
                }
                _Bar->m_MouseOriginX = _X;
                _Bar->m_MouseOriginY = _Y;
                if( !Var->m_NoSlider && !Var->m_ReadOnly )
                    ANT_SET_CURSOR(Center);
                    //ANT_SET_CURSOR(WE);
                else
                    ANT_SET_CURSOR(Arrow);
                Handled = true;
                */

                // move rotoslider
                if( !((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_NoSlider )
                    CTwBar_RotoOnMouseMove(_Bar, _X, _Y);

                if( ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_ReadOnly )
                    ANT_SET_CURSOR(No);
                else if( ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_NoSlider )
                {
                    ANT_SET_CURSOR(Arrow);
                    CustomArea = true;
                }
                _Bar->m_VarHasBeenIncr = true;
                Handled = true;
                _Bar->m_DrawHandles = true;
            }
            else if( _Bar->m_MouseDragTitle )
            {
                int y = _Bar->m_PosY;
                _Bar->m_PosX += _X-_Bar->m_MouseOriginX;
                _Bar->m_PosY += _Y-_Bar->m_MouseOriginY;
                _Bar->m_MouseOriginX = _X;
                _Bar->m_MouseOriginY = _Y;
                int vpx, vpy, vpw, vph;
                vpx = 0;
                vpy = 0;
                vpw = g_TwMgr->m_WndWidth;
                vph = g_TwMgr->m_WndHeight;
                if( _Bar->m_Contained )
                {
                    if( _Bar->m_PosX+_Bar->m_Width>vpx+vpw )
                        _Bar->m_PosX = vpx+vpw-_Bar->m_Width;
                    if( _Bar->m_PosX<vpx )
                        _Bar->m_PosX = vpx;
                    if( _Bar->m_PosY+_Bar->m_Height>vpy+vph )
                        _Bar->m_PosY = vpy+vph-_Bar->m_Height;
                    if( _Bar->m_PosY<vpy )
                        _Bar->m_PosY = vpy;
                } 
                else 
                {
                    if( _Bar->m_PosX+ContainedMargin>vpx+vpw )
                        _Bar->m_PosX = vpx+vpw-ContainedMargin;
                    if( _Bar->m_PosX+_Bar->m_Width<vpx+ContainedMargin )
                        _Bar->m_PosX = vpx+ContainedMargin-_Bar->m_Width;
                    if( _Bar->m_PosY+ContainedMargin>vpy+vph )
                        _Bar->m_PosY = vpy+vph-ContainedMargin;
                    if( _Bar->m_PosY+_Bar->m_Height<vpy+ContainedMargin )
                        _Bar->m_PosY = vpy+ContainedMargin-_Bar->m_Height;
                }
                _Bar->m_ScrollY0 += _Bar->m_PosY-y;
                _Bar->m_ScrollY1 += _Bar->m_PosY-y;
                ANT_SET_CURSOR(Move);
                Handled = true;
            }
            else if( _Bar->m_MouseDragValWidth )
            {
                _Bar->m_ValuesWidth += _Bar->m_MouseOriginX-_X;
                _Bar->m_MouseOriginX = _X;
                CTwBar_NotUpToDate(_Bar);
                if( _Bar->m_IsHelpBar )
                    g_TwMgr->m_HelpBarNotUpToDate = true;
                ANT_SET_CURSOR(WE);
                Handled = true;
                _Bar->m_DrawHandles = true;
            }
            else if( _Bar->m_MouseDragScroll )
            {
                if( _Bar->m_ScrollYH>0 )
                {
                    int dl = ((_Y-_Bar->m_MouseOriginY)*_Bar->m_NbHierLines)/_Bar->m_ScrollYH;
                    if( _Bar->m_FirstLine0+dl<0 )
                        _Bar->m_FirstLine = 0;
                    else if( _Bar->m_FirstLine0+dl+_Bar->m_NbDisplayedLines>_Bar->m_NbHierLines )
                        _Bar->m_FirstLine = _Bar->m_NbHierLines-_Bar->m_NbDisplayedLines;
                    else
                        _Bar->m_FirstLine = _Bar->m_FirstLine0+dl;
                    CTwBar_NotUpToDate(_Bar);
                }
              #ifdef ANT_WINDOWS
                ANT_SET_CURSOR(NS);
              #else
                ANT_SET_CURSOR(Arrow);
              #endif
                Handled = true;
                _Bar->m_DrawHandles = true;
            }
            else if( _Bar->m_MouseDragResizeUL )
            {
                int w = _Bar->m_Width;
                int h = _Bar->m_Height;
                _Bar->m_PosX += _X-_Bar->m_MouseOriginX;
                _Bar->m_PosY += _Y-_Bar->m_MouseOriginY;
                _Bar->m_Width -= _X-_Bar->m_MouseOriginX;
                _Bar->m_Height -= _Y-_Bar->m_MouseOriginY;
                _Bar->m_MouseOriginX = _X;
                _Bar->m_MouseOriginY = _Y;
                int vpx = 0, vpy = 0, vpw = g_TwMgr->m_WndWidth, vph = g_TwMgr->m_WndHeight;
                if( !_Bar->m_Contained )
                {
                    if( _Bar->m_PosX+ContainedMargin>vpx+vpw )
                        _Bar->m_PosX = vpx+vpw-ContainedMargin;
                    if( _Bar->m_PosX+_Bar->m_Width<vpx+ContainedMargin )
                        _Bar->m_PosX = vpx+ContainedMargin-_Bar->m_Width;
                    if( _Bar->m_PosY+ContainedMargin>vpy+vph )
                        _Bar->m_PosY = vpy+vph-ContainedMargin;
                    if( _Bar->m_PosY+_Bar->m_Height<vpy+ContainedMargin )
                        _Bar->m_PosY = vpy+ContainedMargin-_Bar->m_Height;
                }
                else 
                {
                    if( _Bar->m_PosX<vpx ) 
                    {
                        _Bar->m_PosX = vpx;
                        _Bar->m_Width = w;
                    }
                    if( _Bar->m_PosY<vpy ) 
                    {
                        _Bar->m_PosY = vpy;
                        _Bar->m_Height = h;
                    }
                }
                if (_Bar->m_ValuesWidthRatio > 0) 
                    _Bar->m_ValuesWidth = (int)(_Bar->m_ValuesWidthRatio * _Bar->m_Width + 0.5);
                ANT_SET_CURSOR(TopLeft);
                CTwBar_NotUpToDate(_Bar);
                if( _Bar->m_IsHelpBar )
                {
                    g_TwMgr->m_HelpBarNotUpToDate = true;
                    g_TwMgr->m_HelpBarUpdateNow = true;
                }
                g_TwMgr->m_KeyPressedBuildText = true;
                g_TwMgr->m_InfoBuildText = true;
                Handled = true;
                _Bar->m_DrawHandles = true;
            }
            else if( _Bar->m_MouseDragResizeUR )
            {
                int h = _Bar->m_Height;
                _Bar->m_PosY += _Y-_Bar->m_MouseOriginY;
                _Bar->m_Width += _X-_Bar->m_MouseOriginX;
                _Bar->m_Height -= _Y-_Bar->m_MouseOriginY;
                _Bar->m_MouseOriginX = _X;
                _Bar->m_MouseOriginY = _Y;
                int vpx = 0, vpy = 0, vpw = g_TwMgr->m_WndWidth, vph = g_TwMgr->m_WndHeight;
                if( !_Bar->m_Contained )
                {
                    if( _Bar->m_PosX+ContainedMargin>vpx+vpw )
                        _Bar->m_PosX = vpx+vpw-ContainedMargin;
                    if( _Bar->m_PosX+_Bar->m_Width<vpx+ContainedMargin )
                        _Bar->m_PosX = vpx+ContainedMargin-_Bar->m_Width;
                    if( _Bar->m_PosY+ContainedMargin>vpy+vph )
                        _Bar->m_PosY = vpy+vph-ContainedMargin;
                    if( _Bar->m_PosY+_Bar->m_Height<vpy+ContainedMargin )
                        _Bar->m_PosY = vpy+ContainedMargin-_Bar->m_Height;
                }
                else
                {
                    if( _Bar->m_PosX+_Bar->m_Width>vpx+vpw )
                        _Bar->m_Width = vpx+vpw-_Bar->m_PosX;
                    if( _Bar->m_PosY<vpy ) 
                    {
                        _Bar->m_PosY = vpy;
                        _Bar->m_Height = h;
                    }
                }
                if (_Bar->m_ValuesWidthRatio > 0) 
                    _Bar->m_ValuesWidth = (int)(_Bar->m_ValuesWidthRatio * _Bar->m_Width + 0.5);
                ANT_SET_CURSOR(TopRight);
                CTwBar_NotUpToDate(_Bar);
                if( _Bar->m_IsHelpBar )
                {
                    g_TwMgr->m_HelpBarNotUpToDate = true;
                    g_TwMgr->m_HelpBarUpdateNow = true;
                }
                g_TwMgr->m_KeyPressedBuildText = true;
                g_TwMgr->m_InfoBuildText = true;
                Handled = true;
                _Bar->m_DrawHandles = true;
            }
            else if( _Bar->m_MouseDragResizeLL )
            {
                int w = _Bar->m_Width;
                _Bar->m_PosX += _X-_Bar->m_MouseOriginX;
                _Bar->m_Width -= _X-_Bar->m_MouseOriginX;
                _Bar->m_Height += _Y-_Bar->m_MouseOriginY;
                _Bar->m_MouseOriginX = _X;
                _Bar->m_MouseOriginY = _Y;
                int vpx = 0, vpy = 0, vpw = g_TwMgr->m_WndWidth, vph = g_TwMgr->m_WndHeight;
                if( !_Bar->m_Contained )
                {
                    if( _Bar->m_PosX+ContainedMargin>vpx+vpw )
                        _Bar->m_PosX = vpx+vpw-ContainedMargin;
                    if( _Bar->m_PosX+_Bar->m_Width<vpx+ContainedMargin )
                        _Bar->m_PosX = vpx+ContainedMargin-_Bar->m_Width;
                    if( _Bar->m_PosY+ContainedMargin>vpy+vph )
                        _Bar->m_PosY = vpy+vph-ContainedMargin;
                    if( _Bar->m_PosY+_Bar->m_Height<vpy+ContainedMargin )
                        _Bar->m_PosY = vpy+ContainedMargin-_Bar->m_Height;
                }
                else
                {
                    if( _Bar->m_PosY+_Bar->m_Height>vpy+vph )
                        _Bar->m_Height = vpy+vph-_Bar->m_PosY;
                    if( _Bar->m_PosX<vpx ) 
                    {
                        _Bar->m_PosX = vpx;
                        _Bar->m_Width = w;
                    }
                }
                if (_Bar->m_ValuesWidthRatio > 0) 
                    _Bar->m_ValuesWidth = (int)(_Bar->m_ValuesWidthRatio * _Bar->m_Width + 0.5);
                ANT_SET_CURSOR(BottomLeft);
                CTwBar_NotUpToDate(_Bar);
                if( _Bar->m_IsHelpBar )
                {
                    g_TwMgr->m_HelpBarNotUpToDate = true;
                    g_TwMgr->m_HelpBarUpdateNow = true;
                }
                g_TwMgr->m_KeyPressedBuildText = true;
                g_TwMgr->m_InfoBuildText = true;
                Handled = true;
                _Bar->m_DrawHandles = true;
            }
            else if( _Bar->m_MouseDragResizeLR )
            {
                _Bar->m_Width += _X-_Bar->m_MouseOriginX;
                _Bar->m_Height += _Y-_Bar->m_MouseOriginY;
                _Bar->m_MouseOriginX = _X;
                _Bar->m_MouseOriginY = _Y;
                int vpx = 0, vpy = 0, vpw = g_TwMgr->m_WndWidth, vph = g_TwMgr->m_WndHeight;
                if( !_Bar->m_Contained )
                {
                    if( _Bar->m_PosX+ContainedMargin>vpx+vpw )
                        _Bar->m_PosX = vpx+vpw-ContainedMargin;
                    if( _Bar->m_PosX+_Bar->m_Width<vpx+ContainedMargin )
                        _Bar->m_PosX = vpx+ContainedMargin-_Bar->m_Width;
                    if( _Bar->m_PosY+ContainedMargin>vpy+vph )
                        _Bar->m_PosY = vpy+vph-ContainedMargin;
                    if( _Bar->m_PosY+_Bar->m_Height<vpy+ContainedMargin )
                        _Bar->m_PosY = vpy+ContainedMargin-_Bar->m_Height;
                } 
                else
                {
                    if( _Bar->m_PosX+_Bar->m_Width>vpx+vpw )
                        _Bar->m_Width = vpx+vpw-_Bar->m_PosX;
                    if( _Bar->m_PosY+_Bar->m_Height>vpy+vph )
                        _Bar->m_Height = vpy+vph-_Bar->m_PosY;
                }
                if (_Bar->m_ValuesWidthRatio > 0) 
                    _Bar->m_ValuesWidth = (int)(_Bar->m_ValuesWidthRatio * _Bar->m_Width + 0.5);
                ANT_SET_CURSOR(BottomRight);
                CTwBar_NotUpToDate(_Bar);
                if( _Bar->m_IsHelpBar )
                {
                    g_TwMgr->m_HelpBarNotUpToDate = true;
                    g_TwMgr->m_HelpBarUpdateNow = true;
                }
                g_TwMgr->m_KeyPressedBuildText = true;
                g_TwMgr->m_InfoBuildText = true;
                Handled = true;
                _Bar->m_DrawHandles = true;
            }
            else if( _Bar->m_EditInPlace.m_Active )
            {
                CTwBar_EditInPlaceMouseMove(_Bar, _X, _Y, true);
                ANT_SET_CURSOR(IBeam);
                Handled = true;
            }
            //else if( InBar )
            //  ANT_SET_CURSOR(Arrow);
        }
    }
    else // minimized
    {
        if( _Bar->m_Iconifiable && _X>=_Bar->m_MinPosX+2 && _X<_Bar->m_MinPosX+_Bar->m_Font->m_CharHeight && _Y>_Bar->m_MinPosY && _Y<_Bar->m_MinPosY+_Bar->m_Font->m_CharHeight-2 )
        {
            _Bar->m_HighlightMaximize = true;
            if( !_Bar->m_IsHelpBar )
                ANT_SET_CURSOR(Arrow);
            else
              #ifdef ANT_WINDOWS
                ANT_SET_CURSOR(Help);
              #else
                ANT_SET_CURSOR(Arrow);
              #endif
            Handled = true;
        }
        else
            _Bar->m_HighlightMaximize = false;
    }

    // Handled by a custom widget?
    CStructProxy *currentCustomActiveStructProxy = NULL;
    if( g_TwMgr!=NULL && (!Handled || CustomArea) && !_Bar->m_IsMinimized && _Bar->m_CustomRecords.count>0 )
    {
        bool CustomHandled = false;
        for( int s=0; s<2; ++s )    // 2 iterations: first for custom widget having focus, second for others if no focused widget.
            for( size_t ci=0; ci<_Bar->m_CustomRecords.count; ++ci )
            {
                CStructProxy *sProxy = _Bar->m_CustomRecords.items[ci].m_Key;
                const CCustomRecord *r = &_Bar->m_CustomRecords.items[ci].m_Value;
                if( (s==1 || sProxy->m_CustomCaptureFocus) && !CustomHandled && sProxy!=NULL && sProxy->m_CustomMouseMotionCallback!=NULL && r->m_XMin<r->m_XMax && r->m_Y0<r->m_Y1 && r->m_YMin<=r->m_YMax && r->m_YMin>=r->m_Y0 && r->m_YMax<=r->m_Y1 )
                {
                    if( sProxy->m_CustomCaptureFocus || (_X>=r->m_XMin && _X<r->m_XMax && _Y>=r->m_YMin && _Y<r->m_YMax) )
                    {
                        CustomHandled = sProxy->m_CustomMouseMotionCallback(_X-r->m_XMin, _Y-r->m_Y0, r->m_XMax-r->m_XMin, r->m_Y1-r->m_Y0, sProxy->m_StructExtData, sProxy->m_StructClientData, _Bar, r->m_Var);
                        currentCustomActiveStructProxy = sProxy;
                        s = 2; // force s-loop exit
                    }
                }
                else if( sProxy!=NULL )
                {
                    sProxy->m_CustomCaptureFocus = false;   // force free focus, just in case.
                    ANT_SET_CURSOR(Arrow);
                }
            }
        if( CustomHandled )
            Handled = true;
    }
    // If needed, send a 'MouseLeave' message to previously active custom struct
    if( g_TwMgr!=NULL && _Bar->m_CustomActiveStructProxy!=NULL && _Bar->m_CustomActiveStructProxy!=currentCustomActiveStructProxy )
    {
        bool found = false;
        for( CStructProxyNode *node=g_TwMgr->m_StructProxies; node!=NULL && !found; node=node->Next )
            found = (&node->Proxy==_Bar->m_CustomActiveStructProxy);
        if( found && _Bar->m_CustomActiveStructProxy->m_CustomMouseLeaveCallback!=NULL )
            _Bar->m_CustomActiveStructProxy->m_CustomMouseLeaveCallback(_Bar->m_CustomActiveStructProxy->m_StructExtData, _Bar->m_CustomActiveStructProxy->m_StructClientData, _Bar);
    }
    _Bar->m_CustomActiveStructProxy = currentCustomActiveStructProxy;

    return Handled;
}

//  ---------------------------------------------------------------------------

#ifdef ANT_WINDOWS
#   pragma optimize("", off)
//  disable optimizations because the conversion of Enum from unsigned int to double is not always exact if optimized and GraphAPI=DirectX !
#endif
static void ANT_CALL PopupCallback(void *_ClientData)
{
    unsigned int fpuState = TwFPU_Save(); // force fpu precision

    if( g_TwMgr!=NULL && g_TwMgr->m_PopupBar!=NULL )
    {
        unsigned int Enum = *(unsigned int *)&_ClientData;
        CTwVarAtom *Var = g_TwMgr->m_PopupBar->m_VarEnumLinkedToPopupList;
        CTwBar *Bar = g_TwMgr->m_PopupBar->m_BarLinkedToPopupList;
        if( Bar!=NULL && Var!=NULL && !Var->m_ReadOnly && IsEnumType(Var->m_Type) )
        {
            CTwVarAtom_ValueFromDouble(Var, Enum);
            //Bar->UnHighlightLine();
            CTwBar_HaveFocus(Bar, true);
            CTwBar_NotUpToDate(Bar);
        }
        if( g_TwMgr->m_PopupBar!=NULL ) // check again because it might have been destroyed by an enum callback
            TwDeleteBar(g_TwMgr->m_PopupBar);
        g_TwMgr->m_PopupBar = NULL;
    }
    TwFPU_Restore(fpuState);
}
#ifdef ANT_WINDOWS
#   pragma optimize("", on)
#endif

//  ---------------------------------------------------------------------------

bool CTwBar_MouseButton(CTwBar *_Bar, ETwMouseButtonID _Button, bool _Pressed, int _X, int _Y)
{
    assert(g_TwMgr->m_Graph && g_TwMgr->m_WndHeight>0 && g_TwMgr->m_WndWidth>0);
    bool Handled = false;
    if( !_Bar->m_UpToDate )
        CTwBar_Update(_Bar);
    bool EditInPlaceActive = false;
    bool CustomArea = false;

    if( !_Bar->m_IsMinimized )
    {
        Handled = (_X>=_Bar->m_PosX && _X<_Bar->m_PosX+_Bar->m_Width && _Y>=_Bar->m_PosY && _Y<_Bar->m_PosY+_Bar->m_Height);
        if( _Button==TW_MOUSE_LEFT && _Bar->m_HighlightedLine>=0 && _Bar->m_HighlightedLine<(int)_Bar->m_HierTags.count && _Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var )
        {
            bool OnFocus = (_Bar->m_HighlightedLine==(_Y-_Bar->m_PosY-_Bar->m_VarY0)/(_Bar->m_Font->m_CharHeight+_Bar->m_LineSep) && Handled);
            if( CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) )
            {
                if( _Pressed && !g_TwMgr->m_IsRepeatingMousePressed && OnFocus )
                {
                    CTwVarGroup *Grp = ((CTwVarGroup *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var);
                    Grp->m_Open = !Grp->m_Open;
                    CTwBar_NotUpToDate(_Bar);
                    ANT_SET_CURSOR(Arrow);
                }
            }
            else if( _Pressed && _Bar->m_HighlightIncrBtn )
            {
                CTwVarAtom_Increment((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var, 1);
                if( g_TwMgr==NULL ) // Mgr might have been destroyed by the client inside a callback call
                    return true;
                CTwBar_NotUpToDate(_Bar);
            }
            else if( _Pressed && _Bar->m_HighlightDecrBtn )
            {
                CTwVarAtom_Increment((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var, -1);
                if( g_TwMgr==NULL ) // Mgr might have been destroyed by the client inside a callback call
                    return true;
                CTwBar_NotUpToDate(_Bar);
            }
            else if( _Pressed && !_Bar->m_MouseDrag )
            {
                _Bar->m_MouseDrag = true;
                _Bar->m_MouseDragVar = true;
                _Bar->m_MouseOriginX = _X;
                _Bar->m_MouseOriginY = _Y;
                _Bar->m_VarHasBeenIncr = false;
                CTwVarAtom * Var = ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var);
                if( !Var->m_NoSlider && !Var->m_ReadOnly && _Bar->m_HighlightRotoBtn )
                {
                    // begin rotoslider
                    if( _X>_Bar->m_PosX+_Bar->m_VarX1 && OnFocus )
                        CTwBar_RotoOnLButtonDown(_Bar, _Bar->m_PosX+_Bar->m_VarX2-(1*IncrBtnWidth(_Bar->m_Font->m_CharHeight))/2, _Y);
                    else
                        CTwBar_RotoOnLButtonDown(_Bar, _X, _Y);
                    _Bar->m_MouseDrag = true;
                    _Bar->m_MouseDragVar = true;
                }
                else if( (Var->m_Type==TW_TYPE_BOOL8 || Var->m_Type==TW_TYPE_BOOL16 || Var->m_Type==TW_TYPE_BOOL32 || Var->m_Type==TW_TYPE_BOOLCPP) && !Var->m_ReadOnly && OnFocus )
                {
                    CTwVarAtom_Increment(Var, 1);
                    //_Bar->m_HighlightClickBtn = true;
                    _Bar->m_VarHasBeenIncr = true;
                    _Bar->m_MouseDragVar = false;
                    _Bar->m_MouseDrag = false;
                    CTwBar_NotUpToDate(_Bar);
                }
                else if( Var->m_Type==TW_TYPE_BUTTON && !Var->m_ReadOnly )
                {
                    _Bar->m_HighlightClickBtn = true;
                    _Bar->m_MouseDragVar = false;
                    _Bar->m_MouseDrag = false;
                }
                //else if( (Var->m_Type==TW_TYPE_ENUM8 || Var->m_Type==TW_TYPE_ENUM16 || Var->m_Type==TW_TYPE_ENUM32) && !Var->m_ReadOnly )
                else if( IsEnumType(Var->m_Type) && !Var->m_ReadOnly && !g_TwMgr->m_IsRepeatingMousePressed && OnFocus )
                {
                    _Bar->m_MouseDragVar = false;
                    _Bar->m_MouseDrag = false;
                    if( g_TwMgr->m_PopupBar!=NULL )
                    {
                        TwDeleteBar(g_TwMgr->m_PopupBar);
                        g_TwMgr->m_PopupBar = NULL;
                    }
                    // popup list
                    CEnum *e = &g_TwMgr->m_Enums.items[Var->m_Type-TW_TYPE_ENUM_BASE];
                    g_TwMgr->m_PopupBar = TwNewBar("~ Enum Popup ~");
                    g_TwMgr->m_PopupBar->m_IsPopupList = true;
                    g_TwMgr->m_PopupBar->m_Color = _Bar->m_Color;
                    g_TwMgr->m_PopupBar->m_DarkText = _Bar->m_DarkText;
                    g_TwMgr->m_PopupBar->m_PosX = _Bar->m_PosX + _Bar->m_VarX1 - 2;
                    g_TwMgr->m_PopupBar->m_PosY = _Bar->m_PosY + _Bar->m_VarY0 + (_Bar->m_HighlightedLine+1)*(_Bar->m_Font->m_CharHeight+_Bar->m_LineSep);
                    g_TwMgr->m_PopupBar->m_Width = _Bar->m_Width - 2*_Bar->m_Font->m_CharHeight;
                    g_TwMgr->m_PopupBar->m_LineSep = g_TwMgr->m_PopupBar->m_Sep;
                    int popHeight0 = (int)e->m_Entries.count*(_Bar->m_Font->m_CharHeight+_Bar->m_Sep) + _Bar->m_Font->m_CharHeight/2+2;
                    int popHeight = popHeight0;
                    if( g_TwMgr->m_PopupBar->m_PosY+popHeight+2 > g_TwMgr->m_WndHeight )
                        popHeight = g_TwMgr->m_WndHeight-g_TwMgr->m_PopupBar->m_PosY-2;
                    if( popHeight<popHeight0/2 && popHeight<g_TwMgr->m_WndHeight/2 )
                        popHeight = min(popHeight0, g_TwMgr->m_WndHeight/2);
                    if( popHeight<3*(_Bar->m_Font->m_CharHeight+_Bar->m_Sep) )
                        popHeight = 3*(_Bar->m_Font->m_CharHeight+_Bar->m_Sep);
                    g_TwMgr->m_PopupBar->m_Height = popHeight;
                    g_TwMgr->m_PopupBar->m_VarEnumLinkedToPopupList = Var;
                    g_TwMgr->m_PopupBar->m_BarLinkedToPopupList = _Bar;
                    unsigned int CurrentEnumValue = (unsigned int)((int)CTwVarAtom_ValueToDouble(Var));
                    for( size_t k=0; k<e->m_Entries.count; ++k )
                    {
                        char ID[64];
                        sprintf(ID, "%u", e->m_Entries.items[k].Value);
                        //ultoa(e->m_Entries.items[k].Value, ID, 10);
                        TwAddButton(g_TwMgr->m_PopupBar, ID, PopupCallback, *(void**)&(e->m_Entries.items[k].Value), NULL);
                        CTwVar *Btn = (CTwVar *)CTwBar_Find(g_TwMgr->m_PopupBar, ID, NULL, NULL);
                        if( Btn!=NULL )
                        {
                            Btn->m_Label = sdscpy(Btn->m_Label, e->m_Entries.items[k].Label);
                            if( e->m_Entries.items[k].Value==CurrentEnumValue )
                            {
                                Btn->m_ColorPtr = &_Bar->m_ColValTextNE;
                                Btn->m_BgColorPtr = &_Bar->m_ColGrpBg;
                            }
                        }
                    }
                    g_TwMgr->m_HelpBarNotUpToDate = false;
                }
                else if( (Var->m_ReadOnly && (Var->m_Type==TW_TYPE_CDSTRING || IsCSStringType(Var->m_Type)) && CTwBar_EditInPlaceAcceptVar(_Bar, Var))
                         || (!Var->m_ReadOnly && CTwBar_EditInPlaceAcceptVar(_Bar, Var)) )
                    {
                        int dw = 0;
                        //if( _Bar->m_DrawIncrDecrBtn )
                        //  dw = 2*IncrBtnWidth(_Bar->m_Font->m_CharHeight);
                        if( !_Bar->m_EditInPlace.m_Active || _Bar->m_EditInPlace.m_Var!=Var )
                        {
                            CTwBar_EditInPlaceStart(_Bar, Var, _Bar->m_VarX1, _Bar->m_VarY0+(_Bar->m_HighlightedLine)*(_Bar->m_Font->m_CharHeight+_Bar->m_LineSep), _Bar->m_VarX2-_Bar->m_VarX1-dw-1);
                            if( CTwBar_EditInPlaceIsReadOnly(_Bar) )
                                CTwBar_EditInPlaceMouseMove(_Bar, _X, _Y, false);
                            _Bar->m_MouseDrag = false;
                            _Bar->m_MouseDragVar = false;
                        }
                        else
                        {
                            CTwBar_EditInPlaceMouseMove(_Bar, _X, _Y, false);
                            _Bar->m_MouseDrag = true;
                            _Bar->m_MouseDragVar = false;
                        }
                        EditInPlaceActive = _Bar->m_EditInPlace.m_Active;
                        if( Var->m_ReadOnly )
                            ANT_SET_CURSOR(No);
                        else
                            ANT_SET_CURSOR(IBeam);
                    }
                else if( Var->m_ReadOnly )
                    ANT_SET_CURSOR(No);
                else
                {
                    ANT_SET_CURSOR(Arrow);
                    CustomArea = true;
                }
            }
            else if ( !_Pressed && _Bar->m_MouseDragVar )
            {
                _Bar->m_MouseDrag = false;
                _Bar->m_MouseDragVar = false;
                if( !Handled )
                    _Bar->m_DrawHandles = false;
                Handled = true;
                // end rotoslider
                CTwBar_RotoOnLButtonUp(_Bar, _X, _Y);

                /* Incr/decr on right or left click
                if( !_Bar->m_VarHasBeenIncr && !((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_ReadOnly )
                {
                    if( _Button==TW_MOUSE_LEFT )
                        CTwVarAtom_Increment((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var, -1);
                    else if( _Button==TW_MOUSE_RIGHT )
                        CTwVarAtom_Increment((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var, 1);
                    CTwBar_NotUpToDate(_Bar);
                }
                */

                if( ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var)->m_ReadOnly )
                    ANT_SET_CURSOR(No);
                else
                {
                    ANT_SET_CURSOR(Arrow);
                    CustomArea = true;
                }
            }
            else if( !_Pressed && _Bar->m_HighlightClickBtn ) // a button variable is activated
            {
                _Bar->m_HighlightClickBtn = false;
                _Bar->m_MouseDragVar = false;
                _Bar->m_MouseDrag = false;
                Handled = true;
                CTwBar_NotUpToDate(_Bar);
                if( !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) )
                {
                    CTwVarAtom * Var = ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var);
                    if( !Var->m_ReadOnly && Var->m_Type==TW_TYPE_BUTTON && Var->m_Val.m_Button.m_Callback!=NULL )
                    {
                        Var->m_Val.m_Button.m_Callback(Var->m_ClientData);
                        if( g_TwMgr==NULL ) // Mgr might have been destroyed by the client inside a callback call
                            return true;
                    }
                }
            }
            else if( !_Pressed )
            {
                _Bar->m_MouseDragVar = false;
                _Bar->m_MouseDrag = false;
                CustomArea = true;
            }
        }
        else if( _Pressed && !_Bar->m_MouseDrag && _Bar->m_Movable && !_Bar->m_IsPopupList 
                 && ( (_Button==TW_MOUSE_LEFT && _X>=_Bar->m_PosX+2*_Bar->m_Font->m_CharHeight && _X<_Bar->m_PosX+_Bar->m_Width-2*_Bar->m_Font->m_CharHeight && _Y>=_Bar->m_PosY && _Y<_Bar->m_PosY+_Bar->m_Font->m_CharHeight)
                      || (_Button==TW_MOUSE_MIDDLE && _X>=_Bar->m_PosX && _X<_Bar->m_PosX+_Bar->m_Width && _Y>=_Bar->m_PosY && _Y<_Bar->m_PosY+_Bar->m_Height) ) )
        {
            _Bar->m_MouseDrag = true;
            _Bar->m_MouseDragTitle = true;
            _Bar->m_MouseOriginX = _X;
            _Bar->m_MouseOriginY = _Y;
            _Bar->m_HighlightTitle = true;
            ANT_SET_CURSOR(Move);
        }
        else if( !_Pressed && _Bar->m_MouseDragTitle )
        {
            _Bar->m_MouseDrag = false;
            _Bar->m_MouseDragTitle = false;
            ANT_SET_CURSOR(Arrow);
        }
        else if( _Pressed && !_Bar->m_MouseDrag && !_Bar->m_IsPopupList && _Button==TW_MOUSE_LEFT && _X>=_Bar->m_PosX+_Bar->m_VarX1-3 && _X<_Bar->m_PosX+_Bar->m_VarX1+3 && _Y>_Bar->m_PosY+_Bar->m_Font->m_CharHeight && _Y<_Bar->m_PosY+_Bar->m_VarY0 )
        {
            _Bar->m_MouseDrag = true;
            _Bar->m_MouseDragValWidth = true;
            _Bar->m_MouseOriginX = _X;
            _Bar->m_MouseOriginY = _Y;
            ANT_SET_CURSOR(WE);
        }
        else if( !_Pressed && _Bar->m_MouseDragValWidth )
        {
            _Bar->m_MouseDrag = false;
            _Bar->m_MouseDragValWidth = false;
            ANT_SET_CURSOR(Arrow);
        }
        else if( _Pressed && !_Bar->m_MouseDrag && _Bar->m_NbDisplayedLines<_Bar->m_NbHierLines && _Button==TW_MOUSE_LEFT && _X>=_Bar->m_PosX+_Bar->m_VarX2+2 && _X<_Bar->m_PosX+_Bar->m_Width-2 && _Y>=_Bar->m_ScrollY0 && _Y<_Bar->m_ScrollY1 )
        {
            _Bar->m_MouseDrag = true;
            _Bar->m_MouseDragScroll = true;
            _Bar->m_MouseOriginX = _X;
            _Bar->m_MouseOriginY = _Y;
            _Bar->m_FirstLine0 = _Bar->m_FirstLine;
          #ifdef ANT_WINDOWS
            ANT_SET_CURSOR(NS);
          #else
            ANT_SET_CURSOR(Arrow);
          #endif
        }
        else if( !_Pressed && _Bar->m_MouseDragScroll )
        {
            _Bar->m_MouseDrag = false;
            _Bar->m_MouseDragScroll = false;
            ANT_SET_CURSOR(Arrow);
        }
        else if( _Pressed && _Button==TW_MOUSE_LEFT && _X>=_Bar->m_PosX+_Bar->m_VarX2+2 && _X<_Bar->m_PosX+_Bar->m_Width-2 && _Y>=_Bar->m_PosY+_Bar->m_VarY0 && _Y<_Bar->m_ScrollY0 )
        {
            if( _Bar->m_FirstLine>0 )
            {
                --_Bar->m_FirstLine;
                CTwBar_NotUpToDate(_Bar);
            }
        }
        else if( _Pressed && _Button==TW_MOUSE_LEFT && _X>=_Bar->m_PosX+_Bar->m_VarX2+2 && _X<_Bar->m_PosX+_Bar->m_Width-2 && _Y>=_Bar->m_ScrollY1 && _Y<_Bar->m_PosY+_Bar->m_VarY1 )
        {
            if( _Bar->m_FirstLine<_Bar->m_NbHierLines-_Bar->m_NbDisplayedLines )
            {
                ++_Bar->m_FirstLine;
                CTwBar_NotUpToDate(_Bar);
            }
        }
        else if( _Pressed && !_Bar->m_MouseDrag && _Bar->m_Resizable && !_Bar->m_IsPopupList && _Button==TW_MOUSE_LEFT && _X>=_Bar->m_PosX && _X<_Bar->m_PosX+_Bar->m_Font->m_CharHeight && _Y>=_Bar->m_PosY && _Y<_Bar->m_PosY+_Bar->m_Font->m_CharHeight )
        {
            _Bar->m_MouseDrag = true;
            _Bar->m_MouseDragResizeUL = true;
            _Bar->m_MouseOriginX = _X;
            _Bar->m_MouseOriginY = _Y;
            _Bar->m_ValuesWidthRatio = (_Bar->m_Width>0) ? (double)_Bar->m_ValuesWidth/_Bar->m_Width : 0;
            ANT_SET_CURSOR(TopLeft);
        }
        else if( !_Pressed && _Bar->m_MouseDragResizeUL )
        {
            _Bar->m_MouseDrag = false;
            _Bar->m_MouseDragResizeUL = false;
            ANT_SET_CURSOR(Arrow);
        }
        else if( _Pressed && !_Bar->m_MouseDrag && _Bar->m_Resizable && !_Bar->m_IsPopupList && _Button==TW_MOUSE_LEFT && _X>=_Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight && _X<_Bar->m_PosX+_Bar->m_Width && _Y>=_Bar->m_PosY && _Y<_Bar->m_PosY+_Bar->m_Font->m_CharHeight )
        {
            _Bar->m_MouseDrag = true;
            _Bar->m_MouseDragResizeUR = true;
            _Bar->m_MouseOriginX = _X;
            _Bar->m_MouseOriginY = _Y;
            _Bar->m_ValuesWidthRatio = (_Bar->m_Width>0) ? (double)_Bar->m_ValuesWidth/_Bar->m_Width : 0;
            ANT_SET_CURSOR(TopRight);
        }
        else if( !_Pressed && _Bar->m_MouseDragResizeUR )
        {
            _Bar->m_MouseDrag = false;
            _Bar->m_MouseDragResizeUR = false;
            ANT_SET_CURSOR(Arrow);
        }
        else if( _Pressed && !_Bar->m_MouseDrag && _Bar->m_Resizable && !_Bar->m_IsPopupList && _Button==TW_MOUSE_LEFT && _X>=_Bar->m_PosX && _X<_Bar->m_PosX+_Bar->m_Font->m_CharHeight && _Y>=_Bar->m_PosY+_Bar->m_Height-_Bar->m_Font->m_CharHeight && _Y<_Bar->m_PosY+_Bar->m_Height )
        {
            _Bar->m_MouseDrag = true;
            _Bar->m_MouseDragResizeLL = true;
            _Bar->m_MouseOriginX = _X;
            _Bar->m_MouseOriginY = _Y;
            _Bar->m_ValuesWidthRatio = (_Bar->m_Width>0) ? (double)_Bar->m_ValuesWidth/_Bar->m_Width : 0;
            ANT_SET_CURSOR(BottomLeft);
        }
        else if( !_Pressed && _Bar->m_MouseDragResizeLL )
        {
            _Bar->m_MouseDrag = false;
            _Bar->m_MouseDragResizeLL = false;
            ANT_SET_CURSOR(Arrow);
        }
        else if( _Pressed && !_Bar->m_MouseDrag && _Bar->m_Resizable && !_Bar->m_IsPopupList && _Button==TW_MOUSE_LEFT && _X>=_Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight && _X<_Bar->m_PosX+_Bar->m_Width && _Y>=_Bar->m_PosY+_Bar->m_Height-_Bar->m_Font->m_CharHeight && _Y<_Bar->m_PosY+_Bar->m_Height )
        {
            _Bar->m_MouseDrag = true;
            _Bar->m_MouseDragResizeLR = true;
            _Bar->m_MouseOriginX = _X;
            _Bar->m_MouseOriginY = _Y;
            _Bar->m_ValuesWidthRatio = (_Bar->m_Width>0) ? (double)_Bar->m_ValuesWidth/_Bar->m_Width : 0;
            ANT_SET_CURSOR(BottomRight);
        }
        else if( !_Pressed && _Bar->m_MouseDragResizeLR )
        {
            _Bar->m_MouseDrag = false;
            _Bar->m_MouseDragResizeLR = false;
            ANT_SET_CURSOR(Arrow);
        }
        else if( _Pressed && !_Bar->m_IsPopupList && _Button==TW_MOUSE_LEFT && _Bar->m_HighlightLabelsHeader )
        {
            int w = CTwBar_ComputeLabelsWidth(_Bar, _Bar->m_Font);
            if( w<_Bar->m_Font->m_CharHeight )
                w = _Bar->m_Font->m_CharHeight;
            _Bar->m_ValuesWidth = _Bar->m_VarX2 - _Bar->m_VarX0 - w;
            if( _Bar->m_ValuesWidth<_Bar->m_Font->m_CharHeight )
                _Bar->m_ValuesWidth = _Bar->m_Font->m_CharHeight;
            if( _Bar->m_ValuesWidth>_Bar->m_VarX2 - _Bar->m_VarX0 )
                _Bar->m_ValuesWidth = max(_Bar->m_VarX2 - _Bar->m_VarX0 - _Bar->m_Font->m_CharHeight, 0);
            CTwBar_NotUpToDate(_Bar);
            ANT_SET_CURSOR(Arrow);
        }
        else if( _Pressed && !_Bar->m_IsPopupList && _Button==TW_MOUSE_LEFT && _Bar->m_HighlightValuesHeader )
        {
            int w = CTwBar_ComputeValuesWidth(_Bar, _Bar->m_Font);
            if( w<2*_Bar->m_Font->m_CharHeight )
                w = 2*_Bar->m_Font->m_CharHeight; // enough to draw a button
            _Bar->m_ValuesWidth = w;
            if( _Bar->m_ValuesWidth>_Bar->m_VarX2 - _Bar->m_VarX0 )
                _Bar->m_ValuesWidth = max(_Bar->m_VarX2 - _Bar->m_VarX0 - _Bar->m_Font->m_CharHeight, 0);
            CTwBar_NotUpToDate(_Bar);
            ANT_SET_CURSOR(Arrow);
        }
        else if( _Pressed && g_TwMgr->m_FontResizable && !_Bar->m_IsPopupList && _X>=_Bar->m_PosX+_Bar->m_Font->m_CharHeight && _X<_Bar->m_PosX+2*_Bar->m_Font->m_CharHeight && _Y>_Bar->m_PosY && _Y<_Bar->m_PosY+_Bar->m_Font->m_CharHeight )
        {
            // change font
            if( _Button==TW_MOUSE_LEFT )
            {
                if( _Bar->m_Font==g_DefaultSmallFont )
                    CTwMgr_SetFont(g_TwMgr, g_DefaultNormalFont, true);
                else if( _Bar->m_Font==g_DefaultNormalFont )
                    CTwMgr_SetFont(g_TwMgr, g_DefaultLargeFont, true);
                else if( _Bar->m_Font==g_DefaultLargeFont )
                    CTwMgr_SetFont(g_TwMgr, g_DefaultSmallFont, true);
                else
                    CTwMgr_SetFont(g_TwMgr, g_DefaultNormalFont, true);
            }
            else if( _Button==TW_MOUSE_RIGHT )
            {
                if( _Bar->m_Font==g_DefaultSmallFont )
                    CTwMgr_SetFont(g_TwMgr, g_DefaultLargeFont, true);
                else if( _Bar->m_Font==g_DefaultNormalFont )
                    CTwMgr_SetFont(g_TwMgr, g_DefaultSmallFont, true);
                else if( _Bar->m_Font==g_DefaultLargeFont )
                    CTwMgr_SetFont(g_TwMgr, g_DefaultNormalFont, true);
                else
                    CTwMgr_SetFont(g_TwMgr, g_DefaultNormalFont, true);
            }

            ANT_SET_CURSOR(Arrow);
        }
        else if( _Pressed && _Bar->m_Iconifiable && !_Bar->m_IsPopupList && _Button==TW_MOUSE_LEFT && _X>=_Bar->m_PosX+_Bar->m_Width-2*_Bar->m_Font->m_CharHeight && _X<_Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight && _Y>_Bar->m_PosY && _Y<_Bar->m_PosY+_Bar->m_Font->m_CharHeight )
        {
            // minimize
            CTwMgr_Minimize(g_TwMgr, _Bar);
            ANT_SET_CURSOR(Arrow);
        }
        else if( _Bar->m_IsHelpBar && _Pressed && !g_TwMgr->m_IsRepeatingMousePressed && _X>=_Bar->m_PosX+_Bar->m_VarX0 && _X<_Bar->m_PosX+_Bar->m_Width-_Bar->m_Font->m_CharHeight && _Y>_Bar->m_PosY+_Bar->m_Height-_Bar->m_Font->m_CharHeight && _Y<_Bar->m_PosY+_Bar->m_Height )
        {
            /*
            const char *WebPage = "http://";
            #if defined ANT_WINDOWS
                ShellExecute(NULL, "open", WebPage, NULL, NULL, SW_SHOWNORMAL);
            #elif defined ANT_UNIX
                // brute force: try all the possible browsers (I don't know how to find the default one; someone?)
                char DefaultBrowsers[] = "firefox,chrome,opera,mozilla,konqueror,galeon,dillo,netscape";
                char *browser = strtok(DefaultBrowsers, ",");
                char cmd[256];
                while(browser)
                {
                    snprintf(cmd, sizeof(cmd), "%s \"%s\" 1>& null &", browser, WebPage);
                    if( system(cmd) ) {} // avoiding warn_unused_result
                    browser = strtok(NULL, ","); // grab the next browser
                }
            #elif defined ANT_OSX
                char cmd[256];
                snprintf(cmd, sizeof(cmd), "open \"%s\" 1>& null &", WebPage);
                if( system(cmd) ) {} // avoiding warn_unused_result
            #endif
            ANT_SET_CURSOR(Hand);
            */
        }
        else
        {
            CustomArea = true;
        }
    }
    else // minimized
    {
        if( _Pressed && _Bar->m_HighlightMaximize )
        {
            _Bar->m_HighlightMaximize = false;
            CTwMgr_Maximize(g_TwMgr, _Bar);
            ANT_SET_CURSOR(Arrow);
            Handled = true;
        }
    }

    if( g_TwMgr!=NULL ) // Mgr might have been destroyed by the client inside a callback call
        if( _Pressed && !EditInPlaceActive && _Bar->m_EditInPlace.m_Active )
            CTwBar_EditInPlaceEnd(_Bar, true);
        
    // Handled by a custom widget?
    if( g_TwMgr!=NULL && (!Handled || CustomArea) && !_Bar->m_IsMinimized && _Bar->m_CustomRecords.count>0 )
    {
        bool CustomHandled = false;
        for( int s=0; s<2; ++s )    // 2 iterations: first for custom widget having focus, second for others if no focused widget.
            for( size_t ci=0; ci<_Bar->m_CustomRecords.count; ++ci )
            {
                CStructProxy *sProxy = _Bar->m_CustomRecords.items[ci].m_Key;
                const CCustomRecord *r = &_Bar->m_CustomRecords.items[ci].m_Value;
                if( (s==1 || sProxy->m_CustomCaptureFocus) && !CustomHandled && sProxy!=NULL && sProxy->m_CustomMouseButtonCallback!=NULL && r->m_XMin<r->m_XMax && r->m_Y0<r->m_Y1 && r->m_YMin<=r->m_YMax && r->m_YMin>=r->m_Y0 && r->m_YMax<=r->m_Y1 )
                {
                    if( sProxy->m_CustomCaptureFocus || (_X>=r->m_XMin && _X<r->m_XMax && _Y>=r->m_YMin && _Y<r->m_YMax) )
                    {
                        sProxy->m_CustomCaptureFocus = _Pressed;
                        CustomHandled = sProxy->m_CustomMouseButtonCallback(_Button, _Pressed, _X-r->m_XMin, _Y-r->m_Y0, r->m_XMax-r->m_XMin, r->m_Y1-r->m_Y0, sProxy->m_StructExtData, sProxy->m_StructClientData, _Bar, r->m_Var);
                        s = 2; // force s-loop exit
                    }
                }
                else if( sProxy!=NULL )
                {
                    sProxy->m_CustomCaptureFocus = false;   // force free focus, just in case.
                    ANT_SET_CURSOR(Arrow);
                }
            }
        if( CustomHandled )
            Handled = true;
    }

    return Handled;
}


//  ---------------------------------------------------------------------------

bool CTwBar_MouseWheel(CTwBar *_Bar, int _Pos, int _PrevPos, int _MouseX, int _MouseY)
{
    assert(g_TwMgr->m_Graph && g_TwMgr->m_WndHeight>0 && g_TwMgr->m_WndWidth>0);
    if( !_Bar->m_UpToDate )
        CTwBar_Update(_Bar);
    
    bool Handled = false;
    if( !_Bar->m_IsMinimized && _MouseX>=_Bar->m_PosX && _MouseX<_Bar->m_PosX+_Bar->m_Width && _MouseY>=_Bar->m_PosY && _MouseY<_Bar->m_PosY+_Bar->m_Height )
    {
        if( _Pos>_PrevPos && _Bar->m_FirstLine>0 )
        {
            --_Bar->m_FirstLine;
            CTwBar_NotUpToDate(_Bar);
        }
        else if( _Pos<_PrevPos && _Bar->m_FirstLine<_Bar->m_NbHierLines-_Bar->m_NbDisplayedLines )
        {
            ++_Bar->m_FirstLine;
            CTwBar_NotUpToDate(_Bar);
        }

        if( _Pos!=_PrevPos )
        {
            Handled = true;
            if( _Bar->m_EditInPlace.m_Active )
                CTwBar_EditInPlaceEnd(_Bar, true);
        }
    }

    return Handled;
}

//  ---------------------------------------------------------------------------

CTwVarAtom *CTwVarGroup_FindShortcut(CTwVarGroup *_Grp, int _Key, int _Modifiers, bool *_DoIncr)
{
    CTwVarAtom *Atom;
    int Mask = 0xffffffff;
    if( _Key>' ' && _Key<256 ) // don't test SHIFT if _Key is a common key
        Mask &= ~TW_KMOD_SHIFT;

    // don't test KMOD_NUM and KMOD_CAPS modifiers coming from SDL
    Mask &= ~(0x1000);  // 0x1000 is the KMOD_NUM value defined in SDL_keysym.h
    Mask &= ~(0x2000);  // 0x2000 is the KMOD_CAPS value defined in SDL_keysym.h

    // complete partial modifiers comming from SDL
    if( _Modifiers & TW_KMOD_SHIFT )
        _Modifiers |= TW_KMOD_SHIFT;
    if( _Modifiers & TW_KMOD_CTRL )
        _Modifiers |= TW_KMOD_CTRL;
    if( _Modifiers & TW_KMOD_ALT )
        _Modifiers |= TW_KMOD_ALT;
    if( _Modifiers & TW_KMOD_META )
        _Modifiers |= TW_KMOD_META;

    for(size_t i=0; i<_Grp->m_Vars.count; ++i)
        if( _Grp->m_Vars.items[i]!=NULL )
        {
            if( CTwVar_IsGroup(_Grp->m_Vars.items[i]) )
            {
                Atom = CTwVarGroup_FindShortcut((CTwVarGroup *)_Grp->m_Vars.items[i], _Key, _Modifiers, _DoIncr);
                if( Atom!=NULL )
                    return Atom;
            }
            else
            {
                Atom = (CTwVarAtom *)_Grp->m_Vars.items[i];
                if( Atom->m_KeyIncr[0]==_Key && (Atom->m_KeyIncr[1]&Mask)==(_Modifiers&Mask) )
                {
                    if( _DoIncr!=NULL )
                        *_DoIncr = true;
                    return Atom;
                }
                else if( Atom->m_KeyDecr[0]==_Key && (Atom->m_KeyDecr[1]&Mask)==(_Modifiers&Mask) )
                {
                    if( _DoIncr!=NULL )
                        *_DoIncr = false;
                    return Atom;
                }
            }
        }
    return NULL;
}

bool CTwBar_KeyPressed(CTwBar *_Bar, int _Key, int _Modifiers)
{
    assert(g_TwMgr->m_Graph && g_TwMgr->m_WndHeight>0 && g_TwMgr->m_WndWidth>0);
    bool Handled = false;
    if( !_Bar->m_UpToDate )
        CTwBar_Update(_Bar);

    if( _Key>0 && _Key<TW_KEY_LAST )
    {
        /* cf TranslateKey in TwMgr.cpp
        // CTRL special cases
        if( (_Modifiers&TW_KMOD_CTRL) && !(_Modifiers&TW_KMOD_ALT || _Modifiers&TW_KMOD_META) && _Key>0 && _Key<32 )
            _Key += 'a'-1;

        // PAD translation (for SDL keysym)
        if( _Key>=256 && _Key<=272 ) // 256=SDLK_KP0 ... 272=SDLK_KP_EQUALS
        {
            bool Num = ((_Modifiers&TW_KMOD_SHIFT) && !(_Modifiers&0x1000)) || (!(_Modifiers&TW_KMOD_SHIFT) && (_Modifiers&0x1000)); // 0x1000 is SDL's KMOD_NUM
            _Modifiers &= ~TW_KMOD_SHIFT;   // remove shift modifier
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
        */

        /*
        string Str;
        TwGetKeyString(&Str, _Key, _Modifiers);
        printf("key: %d 0x%04xd %s\n", _Key, _Modifiers, Str.c_str());
        */

        if( _Bar->m_EditInPlace.m_Active )
        {
            Handled = CTwBar_EditInPlaceKeyPressed(_Bar, _Key, _Modifiers);
        }
        else
        {
            bool BarActive = (_Bar->m_DrawHandles || _Bar->m_IsPopupList) && !_Bar->m_IsMinimized;
            bool DoIncr = true;
            CTwVarAtom *Atom = CTwVarGroup_FindShortcut(&_Bar->m_VarRoot, _Key, _Modifiers, &DoIncr);
            if( Atom!=NULL && Atom->m_Base.m_Visible )
            {
                if( !Atom->m_ReadOnly )
                {
                    CTwVarAtom_Increment(Atom, DoIncr ? +1 : -1 );
                    if( g_TwMgr==NULL ) // Mgr might have been destroyed by the client inside a callback call
                        return true;
                    _Bar->m_HighlightClickBtnAuto = glfwGetTime();
                }
                CTwBar_NotUpToDate(_Bar);
                CTwBar_Show(_Bar, &Atom->m_Base);
                Handled = true;
            }
            else if( BarActive && _Bar->m_HighlightedLine>=0 && _Bar->m_HighlightedLine<(int)_Bar->m_HierTags.count && _Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var )
            {
                if( _Key==TW_KEY_RIGHT )
                {
                    if( !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) ) 
                    {
                        CTwVarAtom *Atom = ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var);
                        bool Accept = !Atom->m_NoSlider || Atom->m_Type==TW_TYPE_BUTTON 
                                      || Atom->m_Type==TW_TYPE_BOOL8 || Atom->m_Type==TW_TYPE_BOOL16 || Atom->m_Type==TW_TYPE_BOOL32 || Atom->m_Type==TW_TYPE_BOOLCPP
                                      || IsEnumType(Atom->m_Type);
                        if( !CTwVarAtom_IsReadOnly(Atom) && !_Bar->m_IsPopupList && Accept )
                        {
                            CTwVarAtom_Increment(Atom, +1);
                            if( g_TwMgr==NULL ) // Mgr might have been destroyed by the client inside a callback call
                                return true;
                            _Bar->m_HighlightClickBtnAuto = glfwGetTime();
                            CTwBar_NotUpToDate(_Bar);
                        }
                    } 
                    else 
                    {
                        CTwVarGroup *Grp = ((CTwVarGroup *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var);
                        if( !Grp->m_Open )
                        {
                            Grp->m_Open = true;
                            CTwBar_NotUpToDate(_Bar);
                        }
                    }
                    Handled = true;
                }
                else if( _Key==TW_KEY_LEFT )
                {
                    if( !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) ) 
                    {
                        CTwVarAtom *Atom = ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var);
                        bool Accept = !Atom->m_NoSlider || Atom->m_Type==TW_TYPE_BUTTON 
                                      || Atom->m_Type==TW_TYPE_BOOL8 || Atom->m_Type==TW_TYPE_BOOL16 || Atom->m_Type==TW_TYPE_BOOL32 || Atom->m_Type==TW_TYPE_BOOLCPP
                                      || IsEnumType(Atom->m_Type);
                        if( !CTwVarAtom_IsReadOnly(Atom) && Accept && !_Bar->m_IsPopupList )
                        {
                            CTwVarAtom_Increment(Atom, -1);
                            if( g_TwMgr==NULL ) // Mgr might have been destroyed by the client inside a callback call
                                return true;
                            _Bar->m_HighlightClickBtnAuto = glfwGetTime();
                            CTwBar_NotUpToDate(_Bar);
                        }
                    } 
                    else 
                    {
                        CTwVarGroup *Grp = ((CTwVarGroup *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var);
                        if( Grp->m_Open )
                        {
                            Grp->m_Open = false;
                            CTwBar_NotUpToDate(_Bar);
                        }
                    }
                    Handled = true;
                }
                else if( _Key==TW_KEY_RETURN )
                {
                    if( !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) ) 
                    {
                        CTwVarAtom *Atom = ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var);
                        if( !CTwVarAtom_IsReadOnly(Atom) )
                        {
                            if( Atom->m_Type==TW_TYPE_BUTTON || Atom->m_Type==TW_TYPE_BOOLCPP 
                                || Atom->m_Type==TW_TYPE_BOOL8 || Atom->m_Type==TW_TYPE_BOOL16 || Atom->m_Type==TW_TYPE_BOOL32 )
                            {
                                bool isPopup =  _Bar->m_IsPopupList;
                                CTwVarAtom_Increment(Atom, +1);
                                if( g_TwMgr==NULL // Mgr might have been destroyed by the client inside a callback call
                                    || isPopup )  // A popup destroys itself
                                    return true;
                                _Bar->m_HighlightClickBtnAuto = glfwGetTime();
                                CTwBar_NotUpToDate(_Bar);
                            } 
                            else // if( IsEnumType(Atom->m_Type) )
                            {
                                // simulate a mouse click
                                int y = _Bar->m_PosY + _Bar->m_VarY0 + _Bar->m_HighlightedLine*(_Bar->m_Font->m_CharHeight+_Bar->m_LineSep) + _Bar->m_Font->m_CharHeight/2;
                                int x = _Bar->m_PosX + _Bar->m_VarX1 + 2;
                                if( x>_Bar->m_PosX+_Bar->m_VarX2-2 ) 
                                    x = _Bar->m_PosX + _Bar->m_VarX2 - 2;
                                CTwBar_MouseMotion(_Bar, x, y);
                                CTwBar_MouseButton(_Bar, TW_MOUSE_LEFT, true, x, y);
                            }
                        }
                    } 
                    else 
                    {
                        CTwVarGroup *Grp = ((CTwVarGroup *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var);
                        Grp->m_Open = !Grp->m_Open;
                        CTwBar_NotUpToDate(_Bar);
                    }
                    Handled = true;
                }
                else if( _Key==TW_KEY_UP )
                {
                    --_Bar->m_HighlightedLine;
                    if( _Bar->m_HighlightedLine<0 )
                    {
                        _Bar->m_HighlightedLine = 0;
                        if( _Bar->m_FirstLine>0 )
                        {
                            --_Bar->m_FirstLine;
                            CTwBar_NotUpToDate(_Bar);
                        }
                    }
                    _Bar->m_HighlightedLineLastValid = _Bar->m_HighlightedLine;
                    Handled = true;
                }
                else if( _Key==TW_KEY_DOWN )
                {
                    ++_Bar->m_HighlightedLine;
                    if( _Bar->m_HighlightedLine>=(int)_Bar->m_HierTags.count )
                    {
                        _Bar->m_HighlightedLine = (int)_Bar->m_HierTags.count - 1;
                        if( _Bar->m_FirstLine<_Bar->m_NbHierLines-_Bar->m_NbDisplayedLines )
                        {
                            ++_Bar->m_FirstLine;
                            CTwBar_NotUpToDate(_Bar);
                        }                    
                    }
                    _Bar->m_HighlightedLineLastValid = _Bar->m_HighlightedLine;
                    Handled = true;
                }
                else if( _Key==TW_KEY_ESCAPE && _Bar->m_IsPopupList )
                {
                    Handled = true;
                    CTwBar *LinkedBar = _Bar->m_BarLinkedToPopupList;
                    TwDeleteBar(_Bar);
                    g_TwMgr->m_PopupBar = NULL;                
                    if( LinkedBar!=NULL )
                        LinkedBar->m_DrawHandles = true;
                    return true; // _Bar bar has been destroyed
                }
            }
            else if( BarActive )
            {
                if( _Key==TW_KEY_UP || _Key==TW_KEY_DOWN || _Key==TW_KEY_LEFT || _Key==TW_KEY_RIGHT || _Key==TW_KEY_RETURN )
                {
                    if( _Bar->m_HighlightedLineLastValid>=0 && _Bar->m_HighlightedLineLastValid<(int)_Bar->m_HierTags.count )
                        _Bar->m_HighlightedLine = _Bar->m_HighlightedLineLastValid;
                    else if( _Bar->m_HierTags.count>0 )
                    {
                        if( _Key==TW_KEY_UP )
                            _Bar->m_HighlightedLine = (int)_Bar->m_HierTags.count-1;
                        else
                            _Bar->m_HighlightedLine = 0;
                    }
                    Handled = true;
                }
                else if( _Key==TW_KEY_ESCAPE && _Bar->m_IsPopupList )
                {
                    Handled = true;
                    CTwBar *LinkedBar = _Bar->m_BarLinkedToPopupList;
                    TwDeleteBar(_Bar);
                    g_TwMgr->m_PopupBar = NULL;                
                    if( LinkedBar!=NULL )
                        LinkedBar->m_DrawHandles = true;
                    return true; // _Bar bar has been destroyed
                }
            }
        }
    }
    return Handled;
}

//  ---------------------------------------------------------------------------

bool CTwBar_KeyTest(CTwBar *_Bar, int _Key, int _Modifiers)
{
    assert(g_TwMgr->m_Graph && g_TwMgr->m_WndHeight>0 && g_TwMgr->m_WndWidth>0);
    bool Handled = false;
    if( !_Bar->m_UpToDate )
        CTwBar_Update(_Bar);

    if( _Key>0 && _Key<TW_KEY_LAST )
    {
        if( _Bar->m_EditInPlace.m_Active )
            Handled = true;
        else
        {
            bool BarActive = (_Bar->m_DrawHandles || _Bar->m_IsPopupList) && !_Bar->m_IsMinimized;
            bool DoIncr;
            CTwVarAtom *Atom = CTwVarGroup_FindShortcut(&_Bar->m_VarRoot, _Key, _Modifiers, &DoIncr);
            if( Atom!=NULL && Atom->m_Base.m_Visible )
                Handled = true;
            else if( BarActive && ( _Key==TW_KEY_RIGHT || _Key==TW_KEY_LEFT || _Key==TW_KEY_UP || _Key==TW_KEY_DOWN
                                    || _Key==TW_KEY_RETURN || (_Key==TW_KEY_ESCAPE && _Bar->m_IsPopupList) ) )
                Handled = true;
        }
    }
    return Handled;
}

//  ---------------------------------------------------------------------------

bool CTwBar_Show(CTwBar *_Bar, CTwVar *_Var)
{
    if( _Var==NULL || !_Var->m_Visible )
        return false;
    if( !_Bar->m_UpToDate )
        CTwBar_Update(_Bar);

    if( CTwBar_OpenHier(_Bar, &_Bar->m_VarRoot, _Var) )
    {
        if( !_Bar->m_UpToDate )
            CTwBar_Update(_Bar);
        int l = CTwBar_LineInHier(_Bar, &_Bar->m_VarRoot, _Var);
        if( l>=0 )
        {
            int NbLines = (_Bar->m_VarY1-_Bar->m_VarY0+1)/(_Bar->m_Font->m_CharHeight+_Bar->m_LineSep);
            if( NbLines<= 0 )
                NbLines = 1;
            if( l<_Bar->m_FirstLine || l>=_Bar->m_FirstLine+NbLines )
            {
                _Bar->m_FirstLine = l-NbLines/2;
                if( _Bar->m_FirstLine<0 )
                    _Bar->m_FirstLine = 0;
                CTwBar_NotUpToDate(_Bar);
                CTwBar_Update(_Bar);
                if( _Bar->m_NbDisplayedLines<NbLines )
                {
                    _Bar->m_FirstLine -= NbLines-_Bar->m_NbDisplayedLines;
                    if( _Bar->m_FirstLine<0 )
                        _Bar->m_FirstLine = 0;                    
                    CTwBar_NotUpToDate(_Bar);
                }
            }
            _Bar->m_HighlightedLine = l-_Bar->m_FirstLine;
            return true;
        }
    }

    return false;
}

//  ---------------------------------------------------------------------------

bool CTwBar_OpenHier(CTwBar *_Bar, CTwVarGroup *_Root, CTwVar *_Var)
{
    assert( _Root!=NULL );
    for(size_t i=0; i<_Root->m_Vars.count; ++i)
        if( _Root->m_Vars.items[i]!=NULL )
        {
            if( _Var==_Root->m_Vars.items[i]
                || (CTwVar_IsGroup(_Root->m_Vars.items[i]) && CTwBar_OpenHier(_Bar, (CTwVarGroup *)_Root->m_Vars.items[i], _Var)) )
            {
                _Root->m_Open = true;
                CTwBar_NotUpToDate(_Bar);
                return true;
            }
        }
    return false;
}

//  ---------------------------------------------------------------------------

int CTwBar_LineInHier(CTwBar *_Bar, CTwVarGroup *_Root, CTwVar *_Var)
{
    assert( _Root!=NULL );
    int l = 0;
    for(size_t i=0; i<_Root->m_Vars.count; ++i)
        if( _Root->m_Vars.items[i]!=NULL && _Root->m_Vars.items[i]->m_Visible )
        {
            if( _Var==_Root->m_Vars.items[i] )
                return l;
            else if( CTwVar_IsGroup(_Root->m_Vars.items[i]) && ((CTwVarGroup *)_Root->m_Vars.items[i])->m_Open )
            {
                ++l;
                int ll = CTwBar_LineInHier(_Bar, (CTwVarGroup *)_Root->m_Vars.items[i], _Var);
                if( ll>=0 )
                    return l+ll;
                else
                    l += -ll-2;
            }
            ++l;
        }
    return -l-1;
}

//  ---------------------------------------------------------------------------

void DrawArc(int _X, int _Y, int _Radius, float _StartAngleDeg, float _EndAngleDeg, color32 _Color) // angles in degree
{
    ITwGraph *Gr = g_TwMgr->m_Graph;
    if( Gr==NULL || !Gr->IsDrawing(Gr) || _Radius==0 || _StartAngleDeg==_EndAngleDeg )
        return;

    float startAngle = (float)M_PI*_StartAngleDeg/180;
    float endAngle = (float)M_PI*_EndAngleDeg/180;
    //float stepAngle = 8/(float)_Radius;   // segment length = 8 pixels
    float stepAngle = 4/(float)_Radius; // segment length = 4 pixels
    if( stepAngle>(float)M_PI/4 )
        stepAngle = (float)M_PI/4;
    bool fullCircle = fabsf(endAngle-startAngle)>=2.0f*(float)M_PI+fabsf(stepAngle);
    int numSteps;
    if( fullCircle )
    {
        numSteps = (int)((2.0f*(float)M_PI)/stepAngle);
        startAngle = 0;
        endAngle = 2.0f*(float)M_PI;
    }
    else
        numSteps = (int)(fabsf(endAngle-startAngle)/stepAngle);
    if( startAngle>endAngle )
        stepAngle = -stepAngle;

    int x0 = (int)(_X + _Radius * cosf(startAngle) + 0.5f);
    int y0 = (int)(_Y - _Radius * sinf(startAngle) + 0.5f);
    int x1, y1;
    float angle = startAngle+stepAngle;

    for( int i=0; i<numSteps; ++i, angle+=stepAngle )
    {
        x1 = (int)(_X + _Radius * cosf(angle) + 0.5f);
        y1 = (int)(_Y - _Radius * sinf(angle) + 0.5f);
        Gr->DrawLine(Gr, x0, y0, x1, y1, _Color, _Color, true);
        x0 = x1;
        y0 = y1;
    }

    if( fullCircle )
    {
        x1 = (int)(_X + _Radius * cosf(startAngle) + 0.5f);
        y1 = (int)(_Y - _Radius * sinf(startAngle) + 0.5f);
    }
    else
    {
        x1 = (int)(_X + _Radius * cosf(endAngle) + 0.5f);
        y1 = (int)(_Y - _Radius * sinf(endAngle) + 0.5f);
    }
    Gr->DrawLine(Gr, x0, y0, x1, y1, _Color, _Color, true);
}

//  ---------------------------------------------------------------------------

void CRotoSlider_Init(CRotoSlider *_Roto)
{
    _Roto->m_Var = NULL;
    _Roto->m_Active = false;
    _Roto->m_ActiveMiddle = false;
    _Roto->m_Subdiv = 256; // will be recalculated in RotoOnLButtonDown
}

void CTwBar_RotoDraw(CTwBar *_Bar)
{
    ITwGraph *Gr = g_TwMgr->m_Graph;
    if( Gr==NULL || !Gr->IsDrawing(Gr) )
        return;

    if( _Bar->m_Roto.m_Active )
    {
        DrawArc(_Bar->m_Roto.m_Origin.x, _Bar->m_Roto.m_Origin.y, 32, 0, 360, _Bar->m_ColRoto);
        DrawArc(_Bar->m_Roto.m_Origin.x+1, _Bar->m_Roto.m_Origin.y, 32, 0, 360, _Bar->m_ColRoto);
        DrawArc(_Bar->m_Roto.m_Origin.x, _Bar->m_Roto.m_Origin.y+1, 32, 0, 360, _Bar->m_ColRoto);

        if( _Bar->m_Roto.m_HasPrevious )
        {
            double varMax = CTwBar_RotoGetMax(_Bar);
            double varMin = CTwBar_RotoGetMin(_Bar);
            double varStep = CTwBar_RotoGetStep(_Bar);
            if( varMax<DOUBLE_MAX && varMin>-DOUBLE_MAX && fabs(varStep)>DOUBLE_EPS && _Bar->m_Roto.m_Subdiv>0 )
            {
                double dtMax = 360.0*(varMax-_Bar->m_Roto.m_ValueAngle0)/((double)_Bar->m_Roto.m_Subdiv*varStep);//+2;
                double dtMin = 360.0*(varMin-_Bar->m_Roto.m_ValueAngle0)/((double)_Bar->m_Roto.m_Subdiv*varStep);//-2;

                if( dtMax>=0 && dtMax<360 && dtMin<=0 && dtMin>-360 && fabs(dtMax-dtMin)<=360 )
                {
                    int x1, y1, x2, y2;
                    double da = 2.0*M_PI/_Bar->m_Roto.m_Subdiv;

                    x1 = _Bar->m_Roto.m_Origin.x + (int)(40*cos(-M_PI*(_Bar->m_Roto.m_Angle0+dtMax)/180-da));
                    y1 = _Bar->m_Roto.m_Origin.y + (int)(40*sin(-M_PI*(_Bar->m_Roto.m_Angle0+dtMax)/180-da)+0.5);
                    x2 = _Bar->m_Roto.m_Origin.x + (int)(40*cos(-M_PI*(_Bar->m_Roto.m_Angle0+dtMax-10)/180-da));
                    y2 = _Bar->m_Roto.m_Origin.y + (int)(40*sin(-M_PI*(_Bar->m_Roto.m_Angle0+dtMax-10)/180-da)+0.5);
                    Gr->DrawLine(Gr, _Bar->m_Roto.m_Origin.x, _Bar->m_Roto.m_Origin.y, x1, y1, _Bar->m_ColRotoBound, _Bar->m_ColRotoBound, true);
                    Gr->DrawLine(Gr, _Bar->m_Roto.m_Origin.x+1, _Bar->m_Roto.m_Origin.y, x1+1, y1, _Bar->m_ColRotoBound, _Bar->m_ColRotoBound, true);
                    Gr->DrawLine(Gr, _Bar->m_Roto.m_Origin.x, _Bar->m_Roto.m_Origin.y+1, x1, y1+1, _Bar->m_ColRotoBound, _Bar->m_ColRotoBound, true);
                    Gr->DrawLine(Gr, x1, y1, x2, y2, _Bar->m_ColRotoBound, _Bar->m_ColRotoBound, true);
                    Gr->DrawLine(Gr, x1+1, y1, x2+1, y2, _Bar->m_ColRotoBound, _Bar->m_ColRotoBound, true);
                    Gr->DrawLine(Gr, x1, y1+1, x2, y2+1, _Bar->m_ColRotoBound, _Bar->m_ColRotoBound, true);

                    x1 = _Bar->m_Roto.m_Origin.x + (int)(40*cos(-M_PI*(_Bar->m_Roto.m_Angle0+dtMin)/180+da));
                    y1 = _Bar->m_Roto.m_Origin.y + (int)(40*sin(-M_PI*(_Bar->m_Roto.m_Angle0+dtMin)/180+da)+0.5);
                    x2 = _Bar->m_Roto.m_Origin.x + (int)(40*cos(-M_PI*(_Bar->m_Roto.m_Angle0+dtMin+10)/180+da));
                    y2 = _Bar->m_Roto.m_Origin.y + (int)(40*sin(-M_PI*(_Bar->m_Roto.m_Angle0+dtMin+10)/180+da)+0.5);
                    Gr->DrawLine(Gr, _Bar->m_Roto.m_Origin.x, _Bar->m_Roto.m_Origin.y, x1, y1, _Bar->m_ColRotoBound, _Bar->m_ColRotoBound, true);
                    Gr->DrawLine(Gr, _Bar->m_Roto.m_Origin.x+1, _Bar->m_Roto.m_Origin.y, x1+1, y1, _Bar->m_ColRotoBound, _Bar->m_ColRotoBound, true);
                    Gr->DrawLine(Gr, _Bar->m_Roto.m_Origin.x, _Bar->m_Roto.m_Origin.y+1, x1, y1+1, _Bar->m_ColRotoBound, _Bar->m_ColRotoBound, true);
                    Gr->DrawLine(Gr, x1, y1, x2, y2, _Bar->m_ColRotoBound, _Bar->m_ColRotoBound, true);
                    Gr->DrawLine(Gr, x1+1, y1, x2+1, y2, _Bar->m_ColRotoBound, _Bar->m_ColRotoBound, true);
                    Gr->DrawLine(Gr, x1, y1+1, x2, y2+1, _Bar->m_ColRotoBound, _Bar->m_ColRotoBound, true);
                }
            }
        }

        Gr->DrawLine(Gr, _Bar->m_Roto.m_Origin.x+1, _Bar->m_Roto.m_Origin.y, _Bar->m_Roto.m_Current.x+1, _Bar->m_Roto.m_Current.y, _Bar->m_ColRotoVal, _Bar->m_ColRotoVal, true);
        Gr->DrawLine(Gr, _Bar->m_Roto.m_Origin.x, _Bar->m_Roto.m_Origin.y+1, _Bar->m_Roto.m_Current.x, _Bar->m_Roto.m_Current.y+1, _Bar->m_ColRotoVal, _Bar->m_ColRotoVal, true);
        Gr->DrawLine(Gr, _Bar->m_Roto.m_Origin.x, _Bar->m_Roto.m_Origin.y, _Bar->m_Roto.m_Current.x, _Bar->m_Roto.m_Current.y, _Bar->m_ColRotoVal, _Bar->m_ColRotoVal, true);

        if( fabs(_Bar->m_Roto.m_AngleDT)>=1 )
        {
            DrawArc(_Bar->m_Roto.m_Origin.x, _Bar->m_Roto.m_Origin.y, 32, (float)(_Bar->m_Roto.m_Angle0), (float)(_Bar->m_Roto.m_Angle0+_Bar->m_Roto.m_AngleDT-1), _Bar->m_ColRotoVal);
            DrawArc(_Bar->m_Roto.m_Origin.x+1, _Bar->m_Roto.m_Origin.y, 32, (float)(_Bar->m_Roto.m_Angle0), (float)(_Bar->m_Roto.m_Angle0+_Bar->m_Roto.m_AngleDT-1), _Bar->m_ColRotoVal);
            DrawArc(_Bar->m_Roto.m_Origin.x, _Bar->m_Roto.m_Origin.y+1, 32, (float)(_Bar->m_Roto.m_Angle0), (float)(_Bar->m_Roto.m_Angle0+_Bar->m_Roto.m_AngleDT-1), _Bar->m_ColRotoVal);
        }
    }
}

double CTwBar_RotoGetValue(const CTwBar *_Bar)
{
    assert(_Bar->m_Roto.m_Var!=NULL);
    return CTwVarAtom_ValueToDouble(_Bar->m_Roto.m_Var);
}

void CTwBar_RotoSetValue(CTwBar *_Bar, double _Val)
{
    assert(_Bar->m_Roto.m_Var!=NULL);
    if( _Val!=_Bar->m_Roto.m_CurrentValue )
    {
        _Bar->m_Roto.m_CurrentValue = _Val;
        CTwVarAtom_ValueFromDouble(_Bar->m_Roto.m_Var, _Val);
        CTwBar_NotUpToDate(_Bar);
    }
}

double CTwBar_RotoGetMin(const CTwBar *_Bar)
{
    assert(_Bar->m_Roto.m_Var!=NULL);
    double min = -DOUBLE_MAX;
    CTwVarAtom_MinMaxStepToDouble(_Bar->m_Roto.m_Var, &min, NULL, NULL);
    return min;
}

double CTwBar_RotoGetMax(const CTwBar *_Bar)
{
    assert(_Bar->m_Roto.m_Var!=NULL);
    double max = DOUBLE_MAX;
    CTwVarAtom_MinMaxStepToDouble(_Bar->m_Roto.m_Var, NULL, &max, NULL);
    return max;
}

double CTwBar_RotoGetStep(const CTwBar *_Bar)
{
    assert(_Bar->m_Roto.m_Var!=NULL);
    double step = 1;
    CTwVarAtom_MinMaxStepToDouble(_Bar->m_Roto.m_Var, NULL, NULL, &step);
    return step;
}

double CTwBar_RotoGetSteppedValue(const CTwBar *_Bar)
{
    double d = _Bar->m_Roto.m_PreciseValue-_Bar->m_Roto.m_Value0;
    double n = (int)(d/CTwBar_RotoGetStep(_Bar));
    return _Bar->m_Roto.m_Value0 + CTwBar_RotoGetStep(_Bar)*n;
}

void CTwBar_RotoOnMouseMove(CTwBar *_Bar, int _X, int _Y)
{
    CPoint p = CPoint_Make(_X, _Y);
    if( _Bar->m_Roto.m_Active )
    {
        _Bar->m_Roto.m_Current = p;
        CTwBar_RotoSetValue(_Bar, CTwBar_RotoGetSteppedValue(_Bar));
        //DrawManip();

        int ti = -1;
        double t = 0;
        float r = sqrtf((float)(  (_Bar->m_Roto.m_Current.x-_Bar->m_Roto.m_Origin.x)*(_Bar->m_Roto.m_Current.x-_Bar->m_Roto.m_Origin.x) 
                              + (_Bar->m_Roto.m_Current.y-_Bar->m_Roto.m_Origin.y)*(_Bar->m_Roto.m_Current.y-_Bar->m_Roto.m_Origin.y)));
        if( r>_Bar->m_RotoMinRadius )
        {
            t = - atan2((double)(_Bar->m_Roto.m_Current.y-_Bar->m_Roto.m_Origin.y), (double)(_Bar->m_Roto.m_Current.x-_Bar->m_Roto.m_Origin.x));
            ti = ((int)((t/(2.0*M_PI)+1.0)*NB_ROTO_CURSORS+0.5)) % NB_ROTO_CURSORS;
            if( _Bar->m_Roto.m_HasPrevious )
            {
                CPoint v0 = CPoint_Sub(_Bar->m_Roto.m_Previous, _Bar->m_Roto.m_Origin);
                CPoint v1 = CPoint_Sub(_Bar->m_Roto.m_Current, _Bar->m_Roto.m_Origin);
                double l0 = sqrt((double)(v0.x*v0.x+v0.y*v0.y));
                double l1 = sqrt((double)(v1.x*v1.x+v1.y*v1.y));
                double dt = acos(max(-1+1.0e-30,min(1-1.0e-30,(double)(v0.x*v1.x+v0.y*v1.y)/(l0*l1))));
                if( v0.x*v1.y-v0.y*v1.x>0 )
                    dt = - dt;
                double preciseInc = (double)(_Bar->m_Roto.m_Subdiv) * dt/(2.0*M_PI) * CTwBar_RotoGetStep(_Bar);
                if( preciseInc>CTwBar_RotoGetStep(_Bar) || preciseInc<-CTwBar_RotoGetStep(_Bar) )
                {
                    _Bar->m_Roto.m_PreciseValue += preciseInc;
                    if( _Bar->m_Roto.m_PreciseValue>CTwBar_RotoGetMax(_Bar) )
                    {
                        _Bar->m_Roto.m_PreciseValue = CTwBar_RotoGetMax(_Bar);
                        _Bar->m_Roto.m_Value0 = CTwBar_RotoGetMax(_Bar);

                        double da = 360*(CTwBar_RotoGetMax(_Bar)-_Bar->m_Roto.m_ValueAngle0)/((double)(_Bar->m_Roto.m_Subdiv)*CTwBar_RotoGetStep(_Bar));
                        _Bar->m_Roto.m_Angle0 = (((int)((t/(2.0*M_PI)+1.0)*360.0+0.5)) % 360) - da;
                        _Bar->m_Roto.m_AngleDT = da;
                    }
                    else if( _Bar->m_Roto.m_PreciseValue<CTwBar_RotoGetMin(_Bar) )
                    {
                        _Bar->m_Roto.m_PreciseValue = CTwBar_RotoGetMin(_Bar);
                        _Bar->m_Roto.m_Value0 = CTwBar_RotoGetMin(_Bar);

                        double da = 360*(CTwBar_RotoGetMin(_Bar)-_Bar->m_Roto.m_ValueAngle0)/((double)(_Bar->m_Roto.m_Subdiv)*CTwBar_RotoGetStep(_Bar));
                        _Bar->m_Roto.m_Angle0 = (((int)((t/(2.0*M_PI)+1.0)*360.0+0.5)) % 360) - da;
                        _Bar->m_Roto.m_AngleDT = da;
                    }
                    _Bar->m_Roto.m_Previous = _Bar->m_Roto.m_Current;
                    _Bar->m_Roto.m_AngleDT += 180.0*dt/M_PI;
                }
            }
            else
            {
                _Bar->m_Roto.m_Previous = _Bar->m_Roto.m_Current;
                _Bar->m_Roto.m_Value0 = CTwBar_RotoGetValue(_Bar);
                _Bar->m_Roto.m_PreciseValue = _Bar->m_Roto.m_Value0;
                _Bar->m_Roto.m_HasPrevious = true;
                _Bar->m_Roto.m_Angle0 = ((int)((t/(2.0*M_PI)+1.0)*360.0+0.5)) % 360;
                _Bar->m_Roto.m_ValueAngle0 = _Bar->m_Roto.m_Value0;
                _Bar->m_Roto.m_AngleDT = 0;
            }
        }
        else
        {
            if( _Bar->m_Roto.m_HasPrevious )
            {
                CTwBar_RotoSetValue(_Bar, CTwBar_RotoGetSteppedValue(_Bar));
                _Bar->m_Roto.m_Value0 = CTwBar_RotoGetValue(_Bar);
                _Bar->m_Roto.m_ValueAngle0 = _Bar->m_Roto.m_Value0;
                _Bar->m_Roto.m_PreciseValue = _Bar->m_Roto.m_Value0;
                _Bar->m_Roto.m_Angle0 = 0;    
            }
            _Bar->m_Roto.m_HasPrevious = false;
            _Bar->m_Roto.m_AngleDT = 0;
        }
        if( ti>=0 && ti<NB_ROTO_CURSORS )
            ANT_SET_ROTO_CURSOR(ti);
        else
            ANT_SET_CURSOR(Center);
    }
    else
    {
        if( _Bar->m_HighlightRotoBtn )
            ANT_SET_CURSOR(Point);
        else
            ANT_SET_CURSOR(Arrow);
    }
}

void CTwBar_RotoOnLButtonDown(CTwBar *_Bar, int _X, int _Y)
{
    CPoint p = CPoint_Make(_X, _Y);
    if( !_Bar->m_Roto.m_Active && _Bar->m_HighlightedLine>=0 && _Bar->m_HighlightedLine<(int)_Bar->m_HierTags.count && _Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var && !CTwVar_IsGroup(_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var) )
    {
        _Bar->m_Roto.m_Var = ((CTwVarAtom *)_Bar->m_HierTags.items[_Bar->m_HighlightedLine].m_Var);
        int y = _Bar->m_PosY + _Bar->m_VarY0 + _Bar->m_HighlightedLine*(_Bar->m_Font->m_CharHeight+_Bar->m_LineSep) + _Bar->m_Font->m_CharHeight/2;
        _Bar->m_Roto.m_Origin = CPoint_Make(p.x, y); //r.CenterPoint().y);
        _Bar->m_Roto.m_Current = p;
        _Bar->m_Roto.m_Active = true;
        _Bar->m_Roto.m_HasPrevious = false;
        _Bar->m_Roto.m_Angle0 = 0;
        _Bar->m_Roto.m_AngleDT = 0;
        //SetCapture();

        _Bar->m_Roto.m_Value0 = CTwBar_RotoGetValue(_Bar);
        _Bar->m_Roto.m_CurrentValue = _Bar->m_Roto.m_Value0;
        _Bar->m_Roto.m_ValueAngle0 = _Bar->m_Roto.m_Value0;
        _Bar->m_Roto.m_PreciseValue = _Bar->m_Roto.m_Value0;
        //CTwBar_RotoSetValue(_Bar, CTwBar_RotoGetSteppedValue(_Bar));  Not here
        //DrawManip();

        _Bar->m_Roto.m_Subdiv = _Bar->m_RotoNbSubdiv;
        // re-adjust m_Subdiv if needed:
        double min=-DOUBLE_MAX, max=DOUBLE_MAX, step=1;
        CTwVarAtom_MinMaxStepToDouble(_Bar->m_Roto.m_Var, &min, &max, &step);
        if( fabs(step)>0 && min>-DOUBLE_MAX && max<DOUBLE_MAX )
        {
            double dsubdiv = fabs(max-min)/fabs(step)+0.5;
            if( dsubdiv<_Bar->m_RotoNbSubdiv/3 )
                _Bar->m_Roto.m_Subdiv = 3*(int)dsubdiv;
        }

        ANT_SET_CURSOR(Center);
    }
}

void CTwBar_RotoOnLButtonUp(CTwBar *_Bar, int _X, int _Y)
{
    (void)_X, (void)_Y;
    if( !_Bar->m_Roto.m_ActiveMiddle )
    {
        //if( _Bar->m_Roto.m_Var )
        //  CTwBar_RotoSetValue(_Bar, CTwBar_RotoGetSteppedValue(_Bar));

        _Bar->m_Roto.m_Var = NULL;
        _Bar->m_Roto.m_Active = false;
    }
}

void CTwBar_RotoOnMButtonDown(CTwBar *_Bar, int _X, int _Y)
{
    if( !_Bar->m_Roto.m_Active )
    {
        _Bar->m_Roto.m_ActiveMiddle = true;
        CTwBar_RotoOnLButtonDown(_Bar, _X, _Y);
    }
}

void CTwBar_RotoOnMButtonUp(CTwBar *_Bar, int _X, int _Y)
{
    if( _Bar->m_Roto.m_ActiveMiddle )
    {
        _Bar->m_Roto.m_ActiveMiddle = false;
        CTwBar_RotoOnLButtonUp(_Bar, _X, _Y);
    }
}


//  ---------------------------------------------------------------------------

void CEditInPlace_Init(CEditInPlace *_Edit)
{
    assert( g_TwMgr!=NULL && g_TwMgr->m_Graph!=NULL );

    _Edit->m_Var = NULL;
    _Edit->m_Active = false;
    _Edit->m_String = sdsempty();
    _Edit->m_Clipboard = sdsempty();
    _Edit->m_EditTextObj = g_TwMgr->m_Graph->NewTextObj(g_TwMgr->m_Graph);
    _Edit->m_EditSelTextObj = g_TwMgr->m_Graph->NewTextObj(g_TwMgr->m_Graph);

    _Edit->m_X = _Edit->m_Y = _Edit->m_Width = 0;
}

void CEditInPlace_Free(CEditInPlace *_Edit)
{
    assert( g_TwMgr!=NULL && g_TwMgr->m_Graph!=NULL );

    sdsfree(_Edit->m_String);
    sdsfree(_Edit->m_Clipboard);
    if( _Edit->m_EditTextObj )
        g_TwMgr->m_Graph->DeleteTextObj(g_TwMgr->m_Graph, _Edit->m_EditTextObj);
    if( _Edit->m_EditSelTextObj )
        g_TwMgr->m_Graph->DeleteTextObj(g_TwMgr->m_Graph, _Edit->m_EditSelTextObj);
}

bool CTwBar_EditInPlaceIsReadOnly(CTwBar *_Bar)
{
    if( _Bar->m_EditInPlace.m_Var==NULL )
        return true;
    else if( _Bar->m_EditInPlace.m_Var->m_ReadOnly )
        return true;
    else if( _Bar->m_EditInPlace.m_Var->m_Type==TW_TYPE_CDSTRING && ((_Bar->m_EditInPlace.m_Var->m_Ptr==NULL && _Bar->m_EditInPlace.m_Var->m_SetCallback==NULL) || (_Bar->m_EditInPlace.m_Var->m_Ptr!=NULL && g_TwMgr->m_CopyCDStringToClient==NULL)) )
        return true;
    else
        return false;
}

void CTwBar_EditInPlaceDraw(CTwBar *_Bar)
{
    if( !_Bar->m_EditInPlace.m_Active || _Bar->m_EditInPlace.m_Var==NULL || _Bar->m_EditInPlace.m_Width<=0 )
        return;

    // adjust m_FirstChar to see the caret, and extract the visible sub-string
    int i, StringLen = (int)sdslen(_Bar->m_EditInPlace.m_String);
    if( _Bar->m_EditInPlace.m_FirstChar>_Bar->m_EditInPlace.m_CaretPos )
        _Bar->m_EditInPlace.m_FirstChar = _Bar->m_EditInPlace.m_CaretPos;
    int SubstrWidth = 0;
    for( i=min(_Bar->m_EditInPlace.m_CaretPos, StringLen-1); i>=0 && SubstrWidth<_Bar->m_EditInPlace.m_Width; --i )
    {
        unsigned char u = _Bar->m_EditInPlace.m_String[i];
        SubstrWidth += _Bar->m_Font->m_CharWidth[u];
    }
    int FirstChar = max(0, i);
    if( SubstrWidth>=_Bar->m_EditInPlace.m_Width )
        FirstChar += 2;
    if( _Bar->m_EditInPlace.m_FirstChar<FirstChar && FirstChar<StringLen )
        _Bar->m_EditInPlace.m_FirstChar = FirstChar;
    if( _Bar->m_EditInPlace.m_CaretPos==_Bar->m_EditInPlace.m_FirstChar && _Bar->m_EditInPlace.m_FirstChar>0 )
        --_Bar->m_EditInPlace.m_FirstChar;
    SubstrWidth = 0;
    for( i=_Bar->m_EditInPlace.m_FirstChar; i<StringLen && SubstrWidth<_Bar->m_EditInPlace.m_Width; ++i )
    {
        unsigned char u = _Bar->m_EditInPlace.m_String[i];
        SubstrWidth += _Bar->m_Font->m_CharWidth[u];
    }
    int LastChar = i;
    if( SubstrWidth>=_Bar->m_EditInPlace.m_Width )
        --LastChar;
    sds Substr = sdsnewlen( _Bar->m_EditInPlace.m_String+_Bar->m_EditInPlace.m_FirstChar, LastChar-_Bar->m_EditInPlace.m_FirstChar );

    // compute caret x pos
    int CaretX = _Bar->m_PosX + _Bar->m_EditInPlace.m_X;
    for( i=_Bar->m_EditInPlace.m_FirstChar; i<_Bar->m_EditInPlace.m_CaretPos && i<StringLen; ++i )
    {
        unsigned char u = _Bar->m_EditInPlace.m_String[i];
        CaretX += _Bar->m_Font->m_CharWidth[u];
    }

    // draw edit text
    color32 ColText = CTwBar_EditInPlaceIsReadOnly(_Bar) ? _Bar->m_ColValTextRO : _Bar->m_ColEditText;
    color32 ColBg = CTwBar_EditInPlaceIsReadOnly(_Bar) ? _Bar->m_ColValBg : _Bar->m_ColEditBg;
    const char *SubstrC = Substr;
    g_TwMgr->m_Graph->BuildText(g_TwMgr->m_Graph, _Bar->m_EditInPlace.m_EditTextObj, &SubstrC, NULL, NULL, 1, _Bar->m_Font, 0, _Bar->m_EditInPlace.m_Width);
    g_TwMgr->m_Graph->DrawText(g_TwMgr->m_Graph, _Bar->m_EditInPlace.m_EditTextObj, _Bar->m_PosX+_Bar->m_EditInPlace.m_X, _Bar->m_PosY+_Bar->m_EditInPlace.m_Y, ColText, ColBg);
    sdsfree(Substr);

    // draw selected text
    sds StrSelected;
    if( _Bar->m_EditInPlace.m_CaretPos>_Bar->m_EditInPlace.m_SelectionStart )
    {
        int FirstSel = max(_Bar->m_EditInPlace.m_SelectionStart, _Bar->m_EditInPlace.m_FirstChar);
        int LastSel = min(_Bar->m_EditInPlace.m_CaretPos, LastChar);
        StrSelected = sdsnewlen( _Bar->m_EditInPlace.m_String+FirstSel, LastSel-FirstSel );
    }
    else
    {
        int FirstSel = max(_Bar->m_EditInPlace.m_CaretPos, _Bar->m_EditInPlace.m_FirstChar);
        int LastSel = min(_Bar->m_EditInPlace.m_SelectionStart, LastChar);
        StrSelected = sdsnewlen( _Bar->m_EditInPlace.m_String+FirstSel, LastSel-FirstSel );
    }
    int SelWidth = 0;
    for( i=0; i<(int)sdslen(StrSelected); ++i )
    {
        unsigned char u = StrSelected[i];
        SelWidth += _Bar->m_Font->m_CharWidth[u];
    }
    if( SelWidth>0 && sdslen(StrSelected)>0 )
    {
        color32 ColSelBg = CTwBar_EditInPlaceIsReadOnly(_Bar) ? _Bar->m_ColValTextRO : _Bar->m_ColEditSelBg;
        const char *StrSelectedC = StrSelected;
        g_TwMgr->m_Graph->BuildText(g_TwMgr->m_Graph, _Bar->m_EditInPlace.m_EditSelTextObj, &StrSelectedC, NULL, NULL, 1, _Bar->m_Font, 0, SelWidth);
        if ( _Bar->m_EditInPlace.m_CaretPos>_Bar->m_EditInPlace.m_SelectionStart )
            g_TwMgr->m_Graph->DrawText(g_TwMgr->m_Graph, _Bar->m_EditInPlace.m_EditSelTextObj, CaretX-SelWidth, _Bar->m_PosY+_Bar->m_EditInPlace.m_Y, _Bar->m_ColEditSelText, ColSelBg);
        else
            g_TwMgr->m_Graph->DrawText(g_TwMgr->m_Graph, _Bar->m_EditInPlace.m_EditSelTextObj, CaretX, _Bar->m_PosY+_Bar->m_EditInPlace.m_Y, _Bar->m_ColEditSelText, ColSelBg);
    }
    sdsfree(StrSelected);

    // draw caret
    if( CaretX<=_Bar->m_PosX+_Bar->m_EditInPlace.m_X+_Bar->m_EditInPlace.m_Width )
        g_TwMgr->m_Graph->DrawLine(g_TwMgr->m_Graph, CaretX, _Bar->m_PosY+_Bar->m_EditInPlace.m_Y+1, CaretX, _Bar->m_PosY+_Bar->m_EditInPlace.m_Y+_Bar->m_Font->m_CharHeight, _Bar->m_ColEditText, _Bar->m_ColEditText, false);
}

bool CTwBar_EditInPlaceAcceptVar(CTwBar *_Bar, const CTwVarAtom* _Var)
{
    (void)_Bar;
    if( _Var==NULL )
        return false;
    if( _Var->m_Type>=TW_TYPE_CHAR && _Var->m_Type<=TW_TYPE_DOUBLE )
        return true;
    if( _Var->m_Type==TW_TYPE_CDSTRING )
        return true;
    if( IsCSStringType(_Var->m_Type) )
        return true;

    return false;
}

void CTwBar_EditInPlaceStart(CTwBar *_Bar, CTwVarAtom* _Var, int _X, int _Y, int _Width)
{
    if( _Bar->m_EditInPlace.m_Active )
        CTwBar_EditInPlaceEnd(_Bar, true);

    _Bar->m_EditInPlace.m_Active = true;
    _Bar->m_EditInPlace.m_Var = _Var;
    _Bar->m_EditInPlace.m_X = _X;
    _Bar->m_EditInPlace.m_Y = _Y;
    _Bar->m_EditInPlace.m_Width = _Width;
    CTwVarAtom_ValueToString(_Bar->m_EditInPlace.m_Var, &_Bar->m_EditInPlace.m_String);
    if( _Bar->m_EditInPlace.m_Var->m_Type==TW_TYPE_CHAR )
        sdsrange(_Bar->m_EditInPlace.m_String, 0, 0);
    _Bar->m_EditInPlace.m_CaretPos = (int)sdslen(_Bar->m_EditInPlace.m_String);
    if( CTwBar_EditInPlaceIsReadOnly(_Bar) )
        _Bar->m_EditInPlace.m_SelectionStart = _Bar->m_EditInPlace.m_CaretPos;
    else
        _Bar->m_EditInPlace.m_SelectionStart = 0;
    _Bar->m_EditInPlace.m_FirstChar = 0;
}

void CTwBar_EditInPlaceEnd(CTwBar *_Bar, bool _Commit)
{
    if( _Commit && _Bar->m_EditInPlace.m_Active && _Bar->m_EditInPlace.m_Var!=NULL )
    {
        if( _Bar->m_EditInPlace.m_Var->m_Type==TW_TYPE_CDSTRING )
        {
            if( _Bar->m_EditInPlace.m_Var->m_SetCallback!=NULL )
            {
                const char *String = _Bar->m_EditInPlace.m_String;
                _Bar->m_EditInPlace.m_Var->m_SetCallback(&String, _Bar->m_EditInPlace.m_Var->m_ClientData);
            }
            else
            {
                char **StringPtr = (char **)_Bar->m_EditInPlace.m_Var->m_Ptr;
                if( StringPtr!=NULL && g_TwMgr->m_CopyCDStringToClient!=NULL )
                    g_TwMgr->m_CopyCDStringToClient(StringPtr, _Bar->m_EditInPlace.m_String);
            }
        }
        else if( IsCSStringType(_Bar->m_EditInPlace.m_Var->m_Type) )
        {
            int n = TW_CSSTRING_SIZE(_Bar->m_EditInPlace.m_Var->m_Type);
            if( n>0 )
            {
                if( (int)sdslen(_Bar->m_EditInPlace.m_String)>n-1 )
                    sdsrange(_Bar->m_EditInPlace.m_String, 0, n-2);
                if( _Bar->m_EditInPlace.m_Var->m_SetCallback!=NULL )
                    _Bar->m_EditInPlace.m_Var->m_SetCallback(_Bar->m_EditInPlace.m_String, _Bar->m_EditInPlace.m_Var->m_ClientData);
                else if( _Bar->m_EditInPlace.m_Var->m_Ptr!=NULL )
                {
                    if( n>1 )
                        strncpy((char *)_Bar->m_EditInPlace.m_Var->m_Ptr, _Bar->m_EditInPlace.m_String, n-1);
                    ((char *)_Bar->m_EditInPlace.m_Var->m_Ptr)[n-1] = '\0';
                }
            }
        }
        else
        {
            double Val = 0, Min = 0, Max = 0, Step = 0;
            int n = 0;
            if( _Bar->m_EditInPlace.m_Var->m_Type==TW_TYPE_CHAR )
            {
                unsigned char Char = 0;
                n = sscanf(_Bar->m_EditInPlace.m_String, "%c", &Char);
                Val = Char;
            }
            else
                n = sscanf(_Bar->m_EditInPlace.m_String, "%lf", &Val);
            if( n==1 )
            {
                CTwVarAtom_MinMaxStepToDouble(_Bar->m_EditInPlace.m_Var, &Min, &Max, &Step);
                if( Val<Min )
                    Val = Min;
                else if( Val>Max )
                    Val = Max;
                CTwVarAtom_ValueFromDouble(_Bar->m_EditInPlace.m_Var, Val);
            }
        }
        if( g_TwMgr!=NULL ) // Mgr might have been destroyed by the client inside a callback call
            CTwBar_NotUpToDate(_Bar);
    }
    _Bar->m_EditInPlace.m_Active = false;
    _Bar->m_EditInPlace.m_Var = NULL;
}

// Insert _Len bytes from _Text at byte offset _Pos into _S, growing it as needed.
static sds SdsInsertAt(sds _S, int _Pos, const char *_Text, size_t _Len)
{
    size_t oldLen = sdslen(_S);
    assert( _Pos>=0 && (size_t)_Pos<=oldLen );
    _S = sdsMakeRoomFor(_S, _Len);
    memmove(_S+_Pos+_Len, _S+_Pos, oldLen-_Pos);
    memcpy(_S+_Pos, _Text, _Len);
    sdsIncrLen(_S, (ssize_t)_Len);
    return _S;
}

// Remove _Len bytes at byte offset _Pos from _S, in place (never reallocates).
static void SdsEraseAt(sds _S, int _Pos, int _Len)
{
    size_t oldLen = sdslen(_S);
    assert( _Pos>=0 && _Len>=0 && (size_t)(_Pos+_Len)<=oldLen );
    memmove(_S+_Pos, _S+_Pos+_Len, oldLen-_Pos-_Len);
    sdssetlen(_S, oldLen-_Len);
    _S[oldLen-_Len] = '\0';
}

bool CTwBar_EditInPlaceKeyPressed(CTwBar *_Bar, int _Key, int _Modifiers)
{
    if( !_Bar->m_EditInPlace.m_Active )
        return false;
    bool Handled = true; // if EditInPlace is active, it catches all key events
    bool DoCopy = false, DoPaste = false;

    switch( _Key )
    {
    case TW_KEY_ESCAPE:
        CTwBar_EditInPlaceEnd(_Bar, false);
        break;
    case TW_KEY_RETURN:
        CTwBar_EditInPlaceEnd(_Bar, true);
        break;
    case TW_KEY_LEFT:
        if( _Modifiers==TW_KMOD_SHIFT )
            _Bar->m_EditInPlace.m_CaretPos = max(0, _Bar->m_EditInPlace.m_CaretPos-1);
        else
        {
            if( _Bar->m_EditInPlace.m_SelectionStart!=_Bar->m_EditInPlace.m_CaretPos )
                _Bar->m_EditInPlace.m_CaretPos = min(_Bar->m_EditInPlace.m_SelectionStart, _Bar->m_EditInPlace.m_CaretPos);
            else
                _Bar->m_EditInPlace.m_CaretPos = max(0, _Bar->m_EditInPlace.m_CaretPos-1);
            _Bar->m_EditInPlace.m_SelectionStart = _Bar->m_EditInPlace.m_CaretPos;
        }
        break;
    case TW_KEY_RIGHT:
        if( _Modifiers==TW_KMOD_SHIFT )
            _Bar->m_EditInPlace.m_CaretPos = min((int)sdslen(_Bar->m_EditInPlace.m_String), _Bar->m_EditInPlace.m_CaretPos+1);
        else
        {
            if( _Bar->m_EditInPlace.m_SelectionStart!=_Bar->m_EditInPlace.m_CaretPos )
                _Bar->m_EditInPlace.m_CaretPos = max(_Bar->m_EditInPlace.m_SelectionStart, _Bar->m_EditInPlace.m_CaretPos);
            else
                _Bar->m_EditInPlace.m_CaretPos = min((int)sdslen(_Bar->m_EditInPlace.m_String), _Bar->m_EditInPlace.m_CaretPos+1);
            _Bar->m_EditInPlace.m_SelectionStart = _Bar->m_EditInPlace.m_CaretPos;
        }
        break;
    case TW_KEY_BACKSPACE:
        if( !CTwBar_EditInPlaceIsReadOnly(_Bar) )
        {
            if( _Bar->m_EditInPlace.m_SelectionStart==_Bar->m_EditInPlace.m_CaretPos )
                _Bar->m_EditInPlace.m_SelectionStart = max(0, _Bar->m_EditInPlace.m_CaretPos-1);
            CTwBar_EditInPlaceEraseSelect(_Bar);
        }
        break;
    case TW_KEY_DELETE:
        if( !CTwBar_EditInPlaceIsReadOnly(_Bar) )
        {
            if( _Bar->m_EditInPlace.m_SelectionStart==_Bar->m_EditInPlace.m_CaretPos )
                _Bar->m_EditInPlace.m_SelectionStart = min(_Bar->m_EditInPlace.m_CaretPos+1, (int)sdslen(_Bar->m_EditInPlace.m_String));
            CTwBar_EditInPlaceEraseSelect(_Bar);
        }
        break;
    case TW_KEY_HOME:
        _Bar->m_EditInPlace.m_CaretPos = 0;
        if( _Modifiers!=TW_KMOD_SHIFT )
            _Bar->m_EditInPlace.m_SelectionStart = _Bar->m_EditInPlace.m_CaretPos;
        break;
    case TW_KEY_END:
        _Bar->m_EditInPlace.m_CaretPos = (int)sdslen(_Bar->m_EditInPlace.m_String);
        if( _Modifiers!=TW_KMOD_SHIFT )
            _Bar->m_EditInPlace.m_SelectionStart = _Bar->m_EditInPlace.m_CaretPos;
        break;
    case TW_KEY_INSERT:
        if( _Modifiers==TW_KMOD_CTRL )
            DoCopy = true;
        else if( _Modifiers==TW_KMOD_SHIFT )
            DoPaste = true;
        break;
    default:
#if defined ANT_OSX
        // macOS convention is Command+C/Command+V, not Control+C/Control+V;
        // accept either so both muscle memories work.
        if( _Modifiers==TW_KMOD_CTRL || _Modifiers==TW_KMOD_META )
#else
        if( _Modifiers==TW_KMOD_CTRL )
#endif
        {
            if( _Key=='c' || _Key=='C' )
                DoCopy = true;
            else if( _Key=='v' || _Key=='V' )
                DoPaste = true;
        }
        else if( _Key>=32 && _Key<=255 )
        {
            if( !CTwBar_EditInPlaceIsReadOnly(_Bar) && _Bar->m_EditInPlace.m_CaretPos>=0 && _Bar->m_EditInPlace.m_CaretPos<=(int)sdslen(_Bar->m_EditInPlace.m_String) )
            {
                if( _Bar->m_EditInPlace.m_SelectionStart!=_Bar->m_EditInPlace.m_CaretPos )
                    CTwBar_EditInPlaceEraseSelect(_Bar);
                char Ch = (char)_Key;
                _Bar->m_EditInPlace.m_String = SdsInsertAt(_Bar->m_EditInPlace.m_String, _Bar->m_EditInPlace.m_CaretPos, &Ch, 1);
                ++_Bar->m_EditInPlace.m_CaretPos;
                _Bar->m_EditInPlace.m_SelectionStart = _Bar->m_EditInPlace.m_CaretPos;
            }
        }
    }

    if( DoPaste && !CTwBar_EditInPlaceIsReadOnly(_Bar) )
    {
        if( _Bar->m_EditInPlace.m_SelectionStart!=_Bar->m_EditInPlace.m_CaretPos )
            CTwBar_EditInPlaceEraseSelect(_Bar);
        sds Str = sdsempty();
        if( CTwBar_EditInPlaceGetClipboard(_Bar, &Str) && sdslen(Str)>0 )
        {
            _Bar->m_EditInPlace.m_String = SdsInsertAt(_Bar->m_EditInPlace.m_String, _Bar->m_EditInPlace.m_CaretPos, Str, sdslen(Str));
            _Bar->m_EditInPlace.m_CaretPos += (int)sdslen(Str);
            _Bar->m_EditInPlace.m_SelectionStart = _Bar->m_EditInPlace.m_CaretPos;
        }
        sdsfree(Str);
    }
    if( DoCopy )
    {
        sds Str;
        if( _Bar->m_EditInPlace.m_CaretPos>_Bar->m_EditInPlace.m_SelectionStart )
            Str = sdsnewlen(_Bar->m_EditInPlace.m_String+_Bar->m_EditInPlace.m_SelectionStart, _Bar->m_EditInPlace.m_CaretPos-_Bar->m_EditInPlace.m_SelectionStart);
        else if( _Bar->m_EditInPlace.m_CaretPos<_Bar->m_EditInPlace.m_SelectionStart )
            Str = sdsnewlen(_Bar->m_EditInPlace.m_String+_Bar->m_EditInPlace.m_CaretPos, _Bar->m_EditInPlace.m_SelectionStart-_Bar->m_EditInPlace.m_CaretPos);
        else
            Str = sdsempty();
        CTwBar_EditInPlaceSetClipboard(_Bar, Str);
        sdsfree(Str);
    }

    return Handled;
}


bool CTwBar_EditInPlaceEraseSelect(CTwBar *_Bar)
{
    assert(_Bar->m_EditInPlace.m_Active);
    if( !CTwBar_EditInPlaceIsReadOnly(_Bar) && _Bar->m_EditInPlace.m_SelectionStart!=_Bar->m_EditInPlace.m_CaretPos )
    {
        int PosMin = min( _Bar->m_EditInPlace.m_CaretPos, _Bar->m_EditInPlace.m_SelectionStart );
        SdsEraseAt( _Bar->m_EditInPlace.m_String, PosMin, abs(_Bar->m_EditInPlace.m_CaretPos - _Bar->m_EditInPlace.m_SelectionStart) );
        _Bar->m_EditInPlace.m_SelectionStart = _Bar->m_EditInPlace.m_CaretPos = PosMin;
        if( _Bar->m_EditInPlace.m_FirstChar>PosMin )
            _Bar->m_EditInPlace.m_FirstChar = PosMin;
        return true;
    }
    else
        return false;
}


bool CTwBar_EditInPlaceMouseMove(CTwBar *_Bar, int _X, int _Y, bool _Select)
{
    if ( !_Bar->m_EditInPlace.m_Active || _Y<_Bar->m_PosY+_Bar->m_EditInPlace.m_Y || _Y>_Bar->m_PosY+_Bar->m_EditInPlace.m_Y+_Bar->m_Font->m_CharHeight )
        return false;

    int i, CaretX = _Bar->m_PosX+_Bar->m_EditInPlace.m_X;
    for( i=_Bar->m_EditInPlace.m_FirstChar; i<(int)sdslen(_Bar->m_EditInPlace.m_String) && CaretX<_Bar->m_PosX+_Bar->m_EditInPlace.m_X+_Bar->m_EditInPlace.m_Width; ++i )
    {
        unsigned char u = _Bar->m_EditInPlace.m_String[i];
        int CharWidth = _Bar->m_Font->m_CharWidth[u];
        if( _X < CaretX + CharWidth / 2 )
            break;
        CaretX += CharWidth;
    }
    if( CaretX>=_Bar->m_PosX+_Bar->m_EditInPlace.m_X+_Bar->m_EditInPlace.m_Width )
        i = max(0, i-1);

    _Bar->m_EditInPlace.m_CaretPos = i;
    if( !_Select )
        _Bar->m_EditInPlace.m_SelectionStart = _Bar->m_EditInPlace.m_CaretPos;
    return true;
}


bool CTwBar_EditInPlaceGetClipboard(CTwBar *_Bar, sds *_OutString)
{
    assert( _OutString!=NULL );
    *_OutString = sdscpy(*_OutString, _Bar->m_EditInPlace.m_Clipboard); // default implementation, used if
                                              // the system clipboard is empty
                                              // or glfwGetClipboardString fails

    // Delegate to GLFW3 rather than AntTweakBar's own hand-rolled
    // Win32/NSPasteboard/X11-ICCCM clipboard code - GLFW3 already solves _Bar
    // portably (window param is deprecated/nullable since GLFW 3.0, see
    // glfw3.h), so there's no need to duplicate or maintain it here.
    const char *ClipboardText = glfwGetClipboardString(NULL);
    if( ClipboardText!=NULL )
        *_OutString = sdscpy(*_OutString, ClipboardText);

    return true;
}


bool CTwBar_EditInPlaceSetClipboard(CTwBar *_Bar, const char *_String)
{
    if( _String==NULL || strlen(_String)<=0 )
        return false;   // keep last clipboard
    _Bar->m_EditInPlace.m_Clipboard = sdscpy(_Bar->m_EditInPlace.m_Clipboard, _String); // default implementation

    glfwSetClipboardString(NULL, _String);

    return true;
}


#if defined ANT_UNIX
int TW_CALL TwHandleX11SelectionRequest(void *_XEvent)
{
    // AntTweakBar no longer claims the X11 CLIPBOARD/PRIMARY selection
    // itself (see EditInPlaceSetClipboard, above - GLFW3's own X11 backend
    // does that internally now), so there is nothing left to answer here.
    // Kept as a no-op (rather than removed) since it's still a documented
    // part of the public API (AntTweakBar.h) for callers pumping X11
    // events through their own event loop.
    (void)_XEvent;
    return 0;
}
#endif


//  ---------------------------------------------------------------------------


