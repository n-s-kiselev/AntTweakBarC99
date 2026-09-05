//  ---------------------------------------------------------------------------
//
//  @file       TwBar.h
//  @brief      Tweak bar and var classes.
//  @author     Philippe Decaudin
//  @license    This file is part of the AntTweakBar library.
//              For conditions of distribution and use, see License.txt
//
//  note:       Private header
//
//  ---------------------------------------------------------------------------


#if !defined ANT_TW_BAR_INCLUDED
#define ANT_TW_BAR_INCLUDED

#include <AntTweakBar.h>
#include "TwColors.h"
  
#define ANT_TWEAK_BAR_DLL "AntTweakBar"


//  ---------------------------------------------------------------------------
//  Growable-array macros - the same {items; count; capacity;} idiom
//  vendor/nob/nob.h's own nob_da_* macros use elsewhere in this project,
//  redefined locally (`tw_da_*`) rather than #including the build-tool
//  header into runtime library code (nob.h is written for nob.c's own
//  build-orchestration needs - process/file/command handling unrelated to
//  anything a tweak-bar variable list needs - so pulling the whole header
//  in here would be an odd, one-sided dependency). These replace every
//  `std::vector<T>` this file used as part of the C99 rewrite.
#ifdef __cplusplus
#define TW_DA_CAST(T) (decltype(T))
#else
#define TW_DA_CAST(T)
#endif

#define TW_DA_INIT_CAP 8

#define tw_da_reserve(da, expected_capacity)                                          \
    do {                                                                              \
        if ((expected_capacity) > (da)->capacity) {                                   \
            if ((da)->capacity==0)                                                    \
                (da)->capacity = TW_DA_INIT_CAP;                                      \
            while ((expected_capacity) > (da)->capacity)                              \
                (da)->capacity *= 2;                                                  \
            (da)->items = TW_DA_CAST((da)->items)realloc((da)->items, (da)->capacity*sizeof(*(da)->items)); \
            assert((da)->items!=NULL);                                               \
        }                                                                             \
    } while (0)

#define tw_da_append(da, item)                \
    do {                                       \
        tw_da_reserve((da), (da)->count + 1);  \
        (da)->items[(da)->count++] = (item);   \
    } while (0)

#define tw_da_resize(da, new_count)     \
    do {                                \
        tw_da_reserve((da), new_count); \
        (da)->count = (new_count);      \
    } while (0)

#define tw_da_free(da) free((da)->items)

// Concrete growable-array types (one per element type actually used).
struct CDoubleArray { double *items; size_t count; size_t capacity; }; // typedef'd in TwMgr.h (forward-declared there since TwMgr.h is included first and needs it as a pointer type)
typedef struct { sds    *items; size_t count; size_t capacity; } CSdsArray;
typedef struct { color32 *items; size_t count; size_t capacity; } CColor32Array;
typedef struct { char   *items; size_t count; size_t capacity; } CCharArray;

// Ordered removal (shifts every following element down by one) - unlike
// nob_da_remove_unordered, this preserves display/hierarchy order, which
// every removal site in this file relies on.
#define tw_da_remove_ordered(da, i)                                                          \
    do {                                                                                     \
        size_t tw_da_i_ = (i);                                                               \
        assert(tw_da_i_ < (da)->count);                                                      \
        memmove((da)->items+tw_da_i_, (da)->items+tw_da_i_+1, ((da)->count-tw_da_i_-1)*sizeof(*(da)->items)); \
        --(da)->count;                                                                       \
    } while (0)

//  ---------------------------------------------------------------------------

bool IsCustomType(int _Type);

// CTwVar used to be an abstract C++ base class (CTwVarAtom/CTwVarGroup its
// only two subclasses) with virtual dispatch. As part of the C99 rewrite
// this is now plain composition: CTwVarAtom/CTwVarGroup each embed a
// CTwVar `m_Base` as their FIRST member (so &x->m_Base == (void*)x, the
// same struct-based-polymorphism idiom already used for ITwGraph in
// TwGraph.h), tagged by m_Kind. Operations that used to be virtual calls
// through a `CTwVar *` of unknown concrete kind are now the free
// "CTwVar_*" dispatcher functions below, which check m_Kind and forward
// to the concrete type's own (ordinary, non-virtual) method. Call sites
// that already know the concrete type (e.g. a `CTwVarAtom *`) keep
// calling its methods directly (`Atom->HasAttrib(...)`) - unchanged.
typedef enum ETwVarKind { TW_VARKIND_ATOM, TW_VARKIND_GROUP } ETwVarKind;

typedef struct CTwVar
{
    ETwVarKind              m_Kind;
    sds                     m_Name;
    sds                     m_Label;
    sds                     m_Help;
    bool                    m_IsRoot;
    bool                    m_DontClip;
    bool                    m_Visible;
    signed short            m_LeftMargin;
    signed short            m_TopMargin;
    const color32 *         m_ColorPtr;
    const color32 *         m_BgColorPtr;
} CTwVar;

// Replaces std::vector<CTwVar *> (CTwVarGroup::m_Vars).
typedef struct { CTwVar **items; size_t count; size_t capacity; } CTwVarPtrArray;

// Base field init/cleanup (formerly CTwVar's constructor/destructor) -
// called explicitly by CTwVarAtom's/CTwVarGroup's own constructor/
// destructor, since plain composition has no automatic base-subobject
// construction the way C++ inheritance did.
void                        CTwVar_InitBase(CTwVar *_Var, ETwVarKind _Kind);
void                        CTwVar_FreeBase(CTwVar *_Var);

// Dispatchers (concrete kind unknown to the caller).
static inline bool          CTwVar_IsGroup(const CTwVar *_Var) { return _Var->m_Kind==TW_VARKIND_GROUP; }
bool                        CTwVar_IsCustom(const CTwVar *_Var);
const CTwVar *              CTwVar_Find(const CTwVar *_Var, const char *_Name, struct CTwVarGroup **_Parent, int *_Index);
int                         CTwVar_HasAttrib(const CTwVar *_Var, const char *_Attrib, bool *_HasValue);
int                         CTwVar_SetAttrib(CTwVar *_Var, int _AttribID, const char *_Value, TwBar *_Bar, struct CTwVarGroup *_VarParent, int _VarIndex);
ERetType                    CTwVar_GetAttrib(const CTwVar *_Var, int _AttribID, TwBar *_Bar, struct CTwVarGroup *_VarParent, int _VarIndex, CDoubleArray *outDouble, sds *outString);
void                        CTwVar_SetReadOnly(CTwVar *_Var, bool _ReadOnly);
bool                        CTwVar_IsReadOnly(const CTwVar *_Var);
void                        CTwVar_Delete(CTwVar *_Var); // was: delete on a CTwVar* of unknown concrete kind

size_t                      CTwVar_GetDataSize(TwType _Type); // was: static CTwVar::GetDataSize

// Shared "base" attribute handling (formerly CTwVar::HasAttrib/SetAttrib/
// GetAttrib's bodies) - called explicitly by CTwVarAtom's/CTwVarGroup's
// own HasAttrib/SetAttrib/GetAttrib as a fallback for attributes common
// to every var (label, help, group, visible, readonly, ...).
int                         CTwVar_HasAttribBase(const char *_Attrib, bool *_HasValue);
int                         CTwVar_SetAttribBase(CTwVar *_Var, int _AttribID, const char *_Value, TwBar *_Bar, struct CTwVarGroup *_VarParent, int _VarIndex);
ERetType                    CTwVar_GetAttribBase(const CTwVar *_Var, int _AttribID, TwBar *_Bar, struct CTwVarGroup *_VarParent, int _VarIndex, CDoubleArray *outDouble, sds *outString);


// TVal<T> was a C++ template; C99 has none, so each numeric type gets its
// own named struct instead (same layout as every former instantiation).
struct CTwValUChar   { unsigned char  m_Min, m_Max, m_Step; signed char m_Precision; bool m_Hexa; }; // m_Char and m_UInt8 (both were TVal<unsigned char>)
struct CTwValSChar   { signed char    m_Min, m_Max, m_Step; signed char m_Precision; bool m_Hexa; }; // m_Int8
struct CTwValInt16   { signed short   m_Min, m_Max, m_Step; signed char m_Precision; bool m_Hexa; };
struct CTwValUInt16  { unsigned short m_Min, m_Max, m_Step; signed char m_Precision; bool m_Hexa; };
struct CTwValInt32   { signed int     m_Min, m_Max, m_Step; signed char m_Precision; bool m_Hexa; };
struct CTwValUInt32  { unsigned int   m_Min, m_Max, m_Step; signed char m_Precision; bool m_Hexa; };
struct CTwValFloat32 { float          m_Min, m_Max, m_Step; signed char m_Precision; bool m_Hexa; };
struct CTwValFloat64 { double         m_Min, m_Max, m_Step; signed char m_Precision; bool m_Hexa; };

struct CTwBoolVal
{
    char *          m_TrueString;
    char *          m_FalseString;
    bool            m_FreeTrueString;
    bool            m_FreeFalseString;
};
struct CTwVarEnumVal // enum entries are deduced from m_Type; kept as a
{                     // named (non-empty - C99 forbids empty structs) tag.
    char            m_Unused; // Not to be confused with the public API's
};                            // own, unrelated CTwEnumVal (AntTweakBar.h).
struct CTwShortcutVal
{
    int             m_Incr[2];
    int             m_Decr[2];
};
struct CTwHelpStructVal
{
    int             m_StructType;
};
struct CTwButtonVal
{
    TwButtonCallback m_Callback;
    int             m_Separator;
};
struct CTwCustomVal
{
    CMemberProxy *m_MemberProxy;
};

typedef union CTwVal
{
    struct CTwValUChar      m_Char;
    struct CTwValSChar      m_Int8;
    struct CTwValUChar      m_UInt8;
    struct CTwValInt16      m_Int16;
    struct CTwValUInt16     m_UInt16;
    struct CTwValInt32      m_Int32;
    struct CTwValUInt32     m_UInt32;
    struct CTwValFloat32    m_Float32;
    struct CTwValFloat64    m_Float64;
    struct CTwBoolVal       m_Bool;
    struct CTwVarEnumVal    m_Enum;
    struct CTwShortcutVal   m_Shortcut;
    struct CTwHelpStructVal m_HelpStruct;
    struct CTwButtonVal     m_Button;
    struct CTwCustomVal     m_Custom;
} CTwVal;


typedef struct CTwVarAtom
{
    CTwVar                  m_Base; // must be the first member
    ETwType                 m_Type;
    void *                  m_Ptr;
    TwSetVarCallback        m_SetCallback;
    TwGetVarCallback        m_GetCallback;
    void *                  m_ClientData;
    bool                    m_ReadOnly;
    bool                    m_NoSlider;
    int                     m_KeyIncr[2];   // [0]=key_code [1]=modifiers
    int                     m_KeyDecr[2];   // [0]=key_code [1]=modifiers

    union CTwVal            m_Val;
} CTwVarAtom;

void                        CTwVarAtom_ValueToString(const CTwVarAtom *_Atom, sds *_Str);
double                      CTwVarAtom_ValueToDouble(const CTwVarAtom *_Atom);
void                        CTwVarAtom_ValueFromDouble(CTwVarAtom *_Atom, double _Val);
void                        CTwVarAtom_MinMaxStepToDouble(const CTwVarAtom *_Atom, double *_Min, double *_Max, double *_Step);
const CTwVar *              CTwVarAtom_Find(const CTwVarAtom *_Atom, const char *_Name, struct CTwVarGroup **_Parent, int *_Index);
int                         CTwVarAtom_HasAttrib(const CTwVarAtom *_Atom, const char *_Attrib, bool *_HasValue);
int                         CTwVarAtom_SetAttrib(CTwVarAtom *_Atom, int _AttribID, const char *_Value, TwBar *_Bar, struct CTwVarGroup *_VarParent, int _VarIndex);
ERetType                    CTwVarAtom_GetAttrib(const CTwVarAtom *_Atom, int _AttribID, TwBar *_Bar, struct CTwVarGroup *_VarParent, int _VarIndex, CDoubleArray *outDouble, sds *outString);
void                        CTwVarAtom_Increment(CTwVarAtom *_Atom, int _Step);
void                        CTwVarAtom_SetDefaults(CTwVarAtom *_Atom);
static inline void          CTwVarAtom_SetReadOnly(CTwVarAtom *_Atom, bool _ReadOnly) { _Atom->m_ReadOnly=_ReadOnly; if( _Atom->m_Type!=TW_TYPE_BUTTON && _Atom->m_SetCallback==NULL && _Atom->m_Ptr==NULL ) _Atom->m_ReadOnly=true; }
static inline bool          CTwVarAtom_IsReadOnly(const CTwVarAtom *_Atom) { if( _Atom->m_Type!=TW_TYPE_BUTTON && _Atom->m_SetCallback==NULL && _Atom->m_Ptr==NULL ) return true; else return _Atom->m_ReadOnly; }

// Was CTwVarAtom's constructor/destructor; CTwVarAtom_New replaces `new CTwVarAtom`.
void                        CTwVarAtom_Init(CTwVarAtom *_Atom);
void                        CTwVarAtom_Free(CTwVarAtom *_Atom);
CTwVarAtom *                CTwVarAtom_New(void);


struct CTwVarGroup // typedef'd in TwMgr.h (forward-declared there, needed as a pointer type before this full definition loads)
{
    CTwVar                  m_Base; // must be the first member
    CTwVarPtrArray          m_Vars;
    bool                    m_Open;
    TwSummaryCallback       m_SummaryCallback;
    void *                  m_SummaryClientData;
    void *                  m_StructValuePtr;
    TwType                  m_StructType;
};

const CTwVar *              CTwVarGroup_Find(const CTwVarGroup *_Grp, const char *_Name, CTwVarGroup **_Parent, int *_Index);
int                         CTwVarGroup_HasAttrib(const CTwVarGroup *_Grp, const char *_Attrib, bool *_HasValue);
int                         CTwVarGroup_SetAttrib(CTwVarGroup *_Grp, int _AttribID, const char *_Value, TwBar *_Bar, struct CTwVarGroup *_VarParent, int _VarIndex);
ERetType                    CTwVarGroup_GetAttrib(const CTwVarGroup *_Grp, int _AttribID, TwBar *_Bar, struct CTwVarGroup *_VarParent, int _VarIndex, CDoubleArray *outDouble, sds *outString);
CTwVarAtom *                CTwVarGroup_FindShortcut(CTwVarGroup *_Grp, int _Key, int _Modifiers, bool *_DoIncr);
static inline void          CTwVarGroup_SetReadOnly(CTwVarGroup *_Grp, bool _ReadOnly) { for(size_t i=0; i<_Grp->m_Vars.count; ++i) if(_Grp->m_Vars.items[i]) CTwVar_SetReadOnly(_Grp->m_Vars.items[i], _ReadOnly); }
static inline bool          CTwVarGroup_IsReadOnly(const CTwVarGroup *_Grp) { for(size_t i=0; i<_Grp->m_Vars.count; ++i) if(_Grp->m_Vars.items[i] && !CTwVar_IsReadOnly(_Grp->m_Vars.items[i])) return false; return true; }

// Was CTwVarGroup's constructor/destructor; CTwVarGroup_New replaces `new CTwVarGroup`.
void                        CTwVarGroup_Init(CTwVarGroup *_Grp);
void                        CTwVarGroup_Free(CTwVarGroup *_Grp);
CTwVarGroup *               CTwVarGroup_New(void);

//  ---------------------------------------------------------------------------

// Was nested in CTwBar (as "CPoint"/static member functions); moved to file
// scope because CTwBar's own methods are converted to free functions below
// and would otherwise need an explicit "CTwBar::" qualifier to reach them.
typedef struct CPoint
{
    int                     x, y;
} CPoint;
static inline CPoint        CPoint_Make(int _X, int _Y) { CPoint p; p.x=_X; p.y=_Y; return p; }
static inline CPoint        CPoint_Sub(CPoint _A, CPoint _B) { return CPoint_Make(_A.x-_B.x, _A.y-_B.y); }

// The following were nested inside `struct CTwBar` (C++'s "member type"
// idiom - not valid C, which has no nested type declarations inside a
// struct's member list at all) - hoisted to file scope, in dependency
// order, immediately before `struct CTwBar` itself. Every `CTwBar::
// TypeName` qualification elsewhere was updated to the bare, now-file-
// scope name to match.
typedef struct CHierTag
{
    CTwVar *            m_Var;
    int                 m_Level;
    bool                m_Closing;
} CHierTag;
// Replaces std::vector<CHierTag>.
typedef struct { CHierTag *items; size_t count; size_t capacity; } CHierTagArray;

// RotoSlider
typedef struct CRotoSlider
{
    CTwVarAtom *        m_Var;
    double              m_PreciseValue;
    double              m_CurrentValue;
    double              m_Value0;
    double              m_ValueAngle0;
    bool                m_Active;
    bool                m_ActiveMiddle;
    CPoint              m_Origin;
    CPoint              m_Current;
    bool                m_HasPrevious;
    CPoint              m_Previous;
    double              m_Angle0;
    double              m_AngleDT;
    int                 m_Subdiv;
} CRotoSlider;

// Edit-in-place
typedef struct CEditInPlace
{
    CTwVarAtom *        m_Var;
    bool                m_Active;
    sds                 m_String;
    void *              m_EditTextObj;
    void *              m_EditSelTextObj;
    int                 m_CaretPos;
    int                 m_SelectionStart;
    int                 m_X, m_Y;
    int                 m_Width;
    int                 m_FirstChar;
    sds                 m_Clipboard;
} CEditInPlace;

typedef struct CCustomRecord
{
    int                 m_IndexMin;
    int                 m_IndexMax;
    int                 m_XMin, m_XMax;
    int                 m_YMin, m_YMax; // Y visible range
    int                 m_Y0, m_Y1;     // Y widget range
    CTwVarGroup *       m_Var;
} CCustomRecord;
// Replaces std::map<CStructProxy*, CCustomRecord>: cleared every
// frame and rebuilt with a handful of entries (one per distinct custom
// struct proxy seen that frame), so a linear-scan array (find via
// CustomMap_Find, defined in TwBar.cpp) needs no ordering/hashing.
typedef struct CCustomEntry { CStructProxy *m_Key; CCustomRecord m_Value; } CCustomEntry;
typedef struct { CCustomEntry *items; size_t count; size_t capacity; } CustomMap;

enum EValuesWidthFit    { VALUES_WIDTH_FIT = -5555 };
enum EDrawPart          { DRAW_BG=(1<<0), DRAW_CONTENT=(1<<1), DRAW_ALL=DRAW_BG|DRAW_CONTENT };

struct CTwBar // typedef'd in TwMgr.h (forward-declared there, needed as a pointer type before this full definition loads)
{
    sds                     m_Name;
    sds                     m_Label;
    sds                     m_Help;
    bool                    m_Visible;
    int                     m_PosX;
    int                     m_PosY;
    int                     m_Width;
    int                     m_Height;
    color32                 m_Color;
    bool                    m_DarkText;
    const CTexFont *        m_Font;
    int                     m_ValuesWidth;
    int                     m_Sep;
    int                     m_LineSep;
    int                     m_FirstLine;
    float                   m_UpdatePeriod;
    bool                    m_IsHelpBar;
    int                     m_MinNumber;    // accessed by TwDeleteBar
    bool                    m_IsPopupList;
    CTwVarAtom *            m_VarEnumLinkedToPopupList;
    CTwBar *                m_BarLinkedToPopupList;
    bool                    m_Resizable;
    bool                    m_Movable;
    bool                    m_Iconifiable;
    bool                    m_Contained;

    CTwVarGroup             m_VarRoot;

    color32                 m_ColBg, m_ColBg1, m_ColBg2;
    color32                 m_ColHighBg0;
    color32                 m_ColHighBg1;
    color32                 m_ColLabelText;
    color32                 m_ColStructText;
    color32                 m_ColValBg;
    color32                 m_ColValText;
    color32                 m_ColValTextRO;
    color32                 m_ColValTextNE;
    color32                 m_ColValMin;
    color32                 m_ColValMax;
    color32                 m_ColStructBg;
    color32                 m_ColTitleBg;
    color32                 m_ColTitleHighBg;
    color32                 m_ColTitleUnactiveBg;
    color32                 m_ColTitleText;
    color32                 m_ColTitleShadow;
    color32                 m_ColLine;
    color32                 m_ColLineShadow;
    color32                 m_ColUnderline;
    color32                 m_ColBtn;
    color32                 m_ColHighBtn;
    color32                 m_ColFold;
    color32                 m_ColHighFold;
    color32                 m_ColGrpBg;
    color32                 m_ColGrpText;
    color32                 m_ColHierBg;
    color32                 m_ColShortcutText;
    color32                 m_ColShortcutBg;
    color32                 m_ColInfoText;
    color32                 m_ColHelpBg;
    color32                 m_ColHelpText;
    color32                 m_ColRoto;
    color32                 m_ColRotoVal;
    color32                 m_ColRotoBound;
    color32                 m_ColEditBg;
    color32                 m_ColEditText;
    color32                 m_ColEditSelBg;
    color32                 m_ColEditSelText;
    color32                 m_ColSeparator;
    color32                 m_ColStaticText;

    int                     m_TitleWidth;
    int                     m_VarX0;
    int                     m_VarX1;
    int                     m_VarX2;
    int                     m_VarY0;
    int                     m_VarY1;
    int                     m_VarY2;
    int                     m_ScrollYW;
    int                     m_ScrollYH;
    int                     m_ScrollY0;
    int                     m_ScrollY1;
    int                     m_NbHierLines;
    int                     m_NbDisplayedLines;
    bool                    m_UpToDate;
    float                   m_LastUpdateTime;

    bool                    m_MouseDrag;
    bool                    m_MouseDragVar;
    bool                    m_MouseDragTitle;
    bool                    m_MouseDragScroll;
    bool                    m_MouseDragResizeUR;
    bool                    m_MouseDragResizeUL;
    bool                    m_MouseDragResizeLR;
    bool                    m_MouseDragResizeLL;
    bool                    m_MouseDragValWidth;
    int                     m_MouseOriginX;
    int                     m_MouseOriginY;
    double                  m_ValuesWidthRatio;
    bool                    m_VarHasBeenIncr;
    int                     m_FirstLine0;
    int                     m_HighlightedLine;
    int                     m_HighlightedLinePrev;
    int                     m_HighlightedLineLastValid;
    bool                    m_HighlightIncrBtn;
    bool                    m_HighlightDecrBtn;
    bool                    m_HighlightRotoBtn;
    bool                    m_HighlightListBtn;
    bool                    m_HighlightBoolBtn;
    bool                    m_HighlightClickBtn;
    double                  m_HighlightClickBtnAuto;
    bool                    m_HighlightTitle;
    bool                    m_HighlightScroll;
    bool                    m_HighlightUpScroll;
    bool                    m_HighlightDnScroll;
    bool                    m_HighlightMinimize;
    bool                    m_HighlightFont;
    bool                    m_HighlightValWidth;
    bool                    m_HighlightLabelsHeader;
    bool                    m_HighlightValuesHeader;
    bool                    m_DrawHandles;

    bool                    m_IsMinimized;
    int                     m_MinPosX;
    int                     m_MinPosY;
    bool                    m_HighlightMaximize;
    bool                    m_DrawIncrDecrBtn;
    bool                    m_DrawRotoBtn;
    bool                    m_DrawClickBtn;
    bool                    m_DrawListBtn;
    bool                    m_DrawBoolBtn;
    EButtonAlign            m_ButtonAlign;

    CHierTagArray           m_HierTags;
    void *                  m_TitleTextObj;
    void *                  m_LabelsTextObj;
    void *                  m_ValuesTextObj;
    void *                  m_ShortcutTextObj;
    int                     m_ShortcutLine;
    void *                  m_HeadersTextObj;

    CRotoSlider             m_Roto;
    int                     m_RotoMinRadius;
    int                     m_RotoNbSubdiv; // number of steps for one turn

    CEditInPlace            m_EditInPlace;

    CustomMap               m_CustomRecords;
    CStructProxy *  m_CustomActiveStructProxy;
};

// CTwBar's own methods, converted to free functions taking an explicit
// CTwBar *_Bar (or const CTwBar *_Bar) first parameter - same idiom as
// CTwVar's dispatchers above, including the constructor/destructor
// (CTwBar_Create replaces `new CTwBar`, CTwBar_Destroy replaces `delete`).
void                        CRotoSlider_Init(CRotoSlider *_Roto);
void                        CEditInPlace_Init(CEditInPlace *_Edit);
void                        CEditInPlace_Free(CEditInPlace *_Edit);

CTwBar *                    CTwBar_Create(const char *_Name);
void                        CTwBar_Destroy(CTwBar *_Bar);
void                        CTwBar_Draw(CTwBar *_Bar, int _DrawPart);
void                        CTwBar_NotUpToDate(CTwBar *_Bar);
// Only one Find (const-in/const-out), matching the CTwVar_Find precedent -
// callers needing a mutable result cast the result themselves.
const CTwVar *              CTwBar_Find(const CTwBar *_Bar, const char *_Name, CTwVarGroup **_Parent, int *_Index);
int                         CTwBar_HasAttrib(const CTwBar *_Bar, const char *_Attrib, bool *_HasValue);
int                         CTwBar_SetAttrib(CTwBar *_Bar, int _AttribID, const char *_Value);
ERetType                    CTwBar_GetAttrib(const CTwBar *_Bar, int _AttribID, CDoubleArray *outDouble, sds *outString);
bool                        CTwBar_MouseMotion(CTwBar *_Bar, int _X, int _Y);
bool                        CTwBar_MouseButton(CTwBar *_Bar, ETwMouseButtonID _Button, bool _Pressed, int _X, int _Y);
bool                        CTwBar_MouseWheel(CTwBar *_Bar, int _Pos, int _PrevPos, int _MouseX, int _MouseY);
bool                        CTwBar_KeyPressed(CTwBar *_Bar, int _Key, int _Modifiers);
bool                        CTwBar_KeyTest(CTwBar *_Bar, int _Key, int _Modifiers);
bool                        CTwBar_Show(CTwBar *_Bar, CTwVar *_Var); // display the line associated to _Var
bool                        CTwBar_OpenHier(CTwBar *_Bar, CTwVarGroup *_Root, CTwVar *_Var); // open a hierarchy if it contains _Var
int                         CTwBar_LineInHier(CTwBar *_Bar, CTwVarGroup *_Root, CTwVar *_Var); // returns the number of the line associated to _Var
void                        CTwBar_UpdateColors(CTwBar *_Bar);
void                        CTwBar_Update(CTwBar *_Bar);
void                        CTwBar_BrowseHierarchy(CTwBar *_Bar, int *_LineNum, int _CurrLevel, const CTwVar *_Var, int _First, int _Last);
void                        CTwBar_ListLabels(CTwBar *_Bar, CSdsArray *_Labels, CColor32Array *_Colors, CColor32Array *_BgColors, bool *_HasBgColors, const CTexFont *_Font, int _AtomWidthMax, int _GroupWidthMax);
void                        CTwBar_ListValues(CTwBar *_Bar, CSdsArray *_Values, CColor32Array *_Colors, CColor32Array *_BgColors, const CTexFont *_Font, int _WidthMax);
int                         CTwBar_ComputeLabelsWidth(CTwBar *_Bar, const CTexFont *_Font);
int                         CTwBar_ComputeValuesWidth(CTwBar *_Bar, const CTexFont *_Font);
void                        CTwBar_DrawHierHandle(CTwBar *_Bar);
void                        CTwBar_RotoDraw(CTwBar *_Bar);
void                        CTwBar_RotoOnMouseMove(CTwBar *_Bar, int _X, int _Y);
void                        CTwBar_RotoOnLButtonDown(CTwBar *_Bar, int _X, int _Y);
void                        CTwBar_RotoOnLButtonUp(CTwBar *_Bar, int _X, int _Y);
void                        CTwBar_RotoOnMButtonDown(CTwBar *_Bar, int _X, int _Y);
void                        CTwBar_RotoOnMButtonUp(CTwBar *_Bar, int _X, int _Y);
double                      CTwBar_RotoGetValue(const CTwBar *_Bar);
void                        CTwBar_RotoSetValue(CTwBar *_Bar, double _Val);
double                      CTwBar_RotoGetMin(const CTwBar *_Bar);
double                      CTwBar_RotoGetMax(const CTwBar *_Bar);
double                      CTwBar_RotoGetStep(const CTwBar *_Bar);
double                      CTwBar_RotoGetSteppedValue(const CTwBar *_Bar);
void                        CTwBar_EditInPlaceDraw(CTwBar *_Bar);
bool                        CTwBar_EditInPlaceAcceptVar(CTwBar *_Bar, const CTwVarAtom* _Var);
bool                        CTwBar_EditInPlaceIsReadOnly(CTwBar *_Bar);
void                        CTwBar_EditInPlaceStart(CTwBar *_Bar, CTwVarAtom* _Var, int _X, int _Y, int _Width);
void                        CTwBar_EditInPlaceEnd(CTwBar *_Bar, bool _Commit);
bool                        CTwBar_EditInPlaceKeyPressed(CTwBar *_Bar, int _Key, int _Modifiers);
bool                        CTwBar_EditInPlaceEraseSelect(CTwBar *_Bar);
bool                        CTwBar_EditInPlaceMouseMove(CTwBar *_Bar, int _X, int _Y, bool _Select);
bool                        CTwBar_EditInPlaceSetClipboard(CTwBar *_Bar, const char *_String);
bool                        CTwBar_EditInPlaceGetClipboard(CTwBar *_Bar, sds *_OutString);
CCustomRecord *     CTwBar_CustomMap_Find(CTwBar *_Bar, CStructProxy *_Key); // NULL if absent

static inline bool          CTwBar_IsMinimized(const CTwBar *_Bar) { return _Bar->m_IsMinimized; }
static inline bool          CTwBar_IsDragging(const CTwBar *_Bar)  { return _Bar->m_MouseDrag; }
static inline void          CTwBar_UnHighlightLine(CTwBar *_Bar) { _Bar->m_HighlightedLine = -1; CTwBar_NotUpToDate(_Bar); } // used by PopupCallback
static inline void          CTwBar_HaveFocus(CTwBar *_Bar, bool _Focus) { _Bar->m_DrawHandles = _Focus; }                  // used by PopupCallback
static inline void          CTwBar_StopEditInPlace(CTwBar *_Bar) { if( _Bar->m_EditInPlace.m_Active ) CTwBar_EditInPlaceEnd(_Bar, false); }

void DrawArc(int _X, int _Y, int _Radius, float _StartAngleDeg, float _EndAngleDeg, color32 _Color);

//  ---------------------------------------------------------------------------


#endif // !defined ANT_TW_BAR_INCLUDED
