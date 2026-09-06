//  ---------------------------------------------------------------------------
//
//  @file       TwMgr.h
//  @brief      Tweak bar manager.
//  @author     Philippe Decaudin
//  @license    This file is part of the AntTweakBar library.
//              For conditions of distribution and use, see License.txt
//
//  note:       Private header
//
//  ---------------------------------------------------------------------------


#if !defined ANT_TW_MGR_INCLUDED
#define ANT_TW_MGR_INCLUDED

#include <AntTweakBar.h>
#define ANT_CALL TW_CALL

// AntTweakBar.h's public enums are declared `typedef enum EXxx {...}
// TwXxx;` (the typedef name drops the leading "E") - this project's own
// internal code refers to several of them by their bare *tag* name
// (ETwGraphAPI, ETwType, ETwMouseAction, ETwMouseButtonID), which C++
// treats as an implicit type name but C99 requires the `enum` keyword
// for, unless a typedef exists. Self-referencing typedefs (tag -> same
// name) make the existing internal spelling work unchanged in strict
// C99, without touching the public header or any of the many call sites.
typedef enum ETwGraphAPI ETwGraphAPI;
typedef enum ETwType ETwType;
typedef enum ETwMouseAction ETwMouseAction;
typedef enum ETwMouseButtonID ETwMouseButtonID;

#ifndef __cplusplus
// TW_TYPE_BOOLCPP is only defined (as an ETwType enum value, = 1) under
// `#ifdef __cplusplus` in AntTweakBar.h - a still-C++ client application's
// native `bool` ABI-compatibility type, distinct from the explicit-width
// TW_TYPE_BOOL8/16/32 a C client uses. This library is now real C99, so the
// identifier itself does not exist in this translation unit at all - but
// the library must still recognize the value when it appears in a
// TwAddVar's type argument coming from such a client, so redefine it here
// as its known numeric literal rather than removing any of the (many)
// existing `... == TW_TYPE_BOOLCPP` checks throughout TwBar.c/TwMgr.c.
#define TW_TYPE_BOOLCPP 1
#endif

#include "TwColors.h"
#include "TwFonts.h"
#include "TwGraph.h"

// CDoubleArray (replaces std::vector<double> for GetAttrib's outDouble
// parameter) is defined in TwBar.h, which this header can't #include
// (TwBar.h itself depends on TwMgr.h's nested types) - a forward
// declaration is enough for a pointer parameter; TwMgr.cpp, which
// actually dereferences it, already includes TwBar.h.
typedef struct CDoubleArray CDoubleArray;

// Replace CTwMgr's std::vector<TwBar*>/std::vector<int>/std::vector<bool>
// (m_Bars/m_Order/m_MinOccupied). tw_da_* macros to build/free these are
// defined in TwBar.h, already #included by TwMgr.cpp before this header.
// std::vector<bool> is a bitset specialization in C++; nothing here relies
// on that packed layout, so a plain bool* array is the direct replacement.
typedef struct { TwBar **items; size_t count; size_t capacity; } CTwBarPtrArray;
typedef struct { int    *items; size_t count; size_t capacity; } CIntArray;
typedef struct { bool   *items; size_t count; size_t capacity; } CBoolArray;
// Same shape as TwBar.h's CCharArray, but that type isn't visible yet here
// (TwMgr.h is #included before TwBar.h in TwMgr.cpp, and TwBar.h itself
// depends on TwMgr.h's nested types, so the include order can't just be
// swapped) - a distinctly-named twin avoids a same-name/different-type
// redefinition once both headers are in scope. m_CSStringBuffer (replacing
// std::vector<char>) is the only field using it.
typedef struct { char   *items; size_t count; size_t capacity; } CByteArray;


//#define BENCH // uncomment to activate benchmarks

#ifdef BENCH
#   define PERF(cmd)    cmd
#else   // BENCH
#   define PERF(cmd)
#endif  // BENCH

//  ---------------------------------------------------------------------------
//  API unexposed by AntTweakBar.h
//  ---------------------------------------------------------------------------

// bar states -> use TwDefine instead
typedef enum ETwState
{
    TW_STATE_SHOWN       = 1,
    TW_STATE_ICONIFIED   = 2,
    TW_STATE_HIDDEN      = 3,
    TW_STATE_UNICONIFIED = 4,
    TW_STATE_ERROR       = 0
} TwState;
/*ANT_TWEAK_BAR_API*/ int       ANT_CALL TwSetBarState(TwBar *bar, TwState state);
// var states -> use TwDefine instead: visible/iconified implemented only as string commands
//ANT_TWEAK_BAR_API int     ANT_CALL TwSetVarState(TwBar *bar, const char *name, TwState state);
//ANT_TWEAK_BAR_API TwState ANT_CALL TwGetVarState(const TwBar *bar, const char *name);

typedef struct CTwVarGroup CTwVarGroup;
typedef struct CTwBar CTwBar; // full definition in TwBar.h; forward-declared
                              // here since it's used as a pointer type (e.g.
                              // CMemberProxy::m_Bar below) before TwBar.h loads
typedef void (ANT_CALL *TwStructExtInitCallback)(void *structExtValue, void *clientData);
typedef void (ANT_CALL *TwCopyVarFromExtCallback)(void *structValue, const void *structExtValue, unsigned int structExtMemberIndex, void *clientData);
typedef void (ANT_CALL *TwCopyVarToExtCallback)(const void *structValue, void *structExtValue, unsigned int structExtMemberIndex, void *clientData);
/*ANT_TWEAK_BAR_API*/ TwType    ANT_CALL TwDefineStructExt(const char *name, const TwStructMember *structExtMembers, unsigned int nbExtMembers, size_t structSize, size_t structExtSize, TwStructExtInitCallback structExtInitCallback, TwCopyVarFromExtCallback copyVarFromExtCallback, TwCopyVarToExtCallback copyVarToExtCallback, TwSummaryCallback summaryCallback, void *clientData, const char *help);
typedef void (ANT_CALL *TwCustomDrawCallback)(int w, int h, void *structExtValue, void *clientData, TwBar *bar, CTwVarGroup *varGrp);
typedef bool (ANT_CALL *TwCustomMouseMotionCallback)(int mouseX, int mouseY, int w, int h, void *structExtValue, void *clientData, TwBar *bar, CTwVarGroup *varGrp);
typedef bool (ANT_CALL *TwCustomMouseButtonCallback)(TwMouseButtonID button, bool pressed, int mouseX, int mouseY, int w, int h, void *structExtValue, void *clientData, TwBar *bar, CTwVarGroup *varGrp);
typedef void (ANT_CALL *TwCustomMouseLeaveCallback)(void *structExtValue, void *clientData, TwBar *bar);

typedef enum ERetType
{
    RET_ERROR = 0,
    RET_DOUBLE,
    RET_STRING
} ERetType;

typedef enum EButtonAlign
{
    BUTTON_ALIGN_LEFT,
    BUTTON_ALIGN_CENTER,
    BUTTON_ALIGN_RIGHT
} EButtonAlign;

//  ---------------------------------------------------------------------------
//  AntTweakBar Manager
//  ---------------------------------------------------------------------------

// The following were nested inside `struct CTwMgr` (C++'s "member type"
// idiom - a struct scopes its own nested type declarations, reachable
// afterward as bare names from anywhere that can see the outer struct, or
// qualified as CTwMgr::TypeName from outside it). C has no such nesting at
// all - a type declaration cannot even appear inside a struct's member
// list syntactically - so these are hoisted to file scope, in the same
// dependency order they were declared in, immediately before `struct
// CTwMgr` itself. Every `CTwMgr::TypeName` qualification elsewhere in this
// project (TwMgr.h/.c, TwBar.h/.c) was updated to the bare, now-file-scope
// name to match.
typedef struct CStructMember
{
    sds             m_Name;
    sds             m_Label;
    TwType          m_Type;
    size_t          m_Offset;
    sds             m_DefString;
    size_t          m_Size;
    sds             m_Help;
} CStructMember;
// Replaces std::vector<CStructMember>.
typedef struct { CStructMember *items; size_t count; size_t capacity; } CStructMemberArray;
typedef struct CStruct
{
    sds                          m_Name;
    CStructMemberArray           m_Members;
    size_t                      m_Size;
    TwSummaryCallback           m_SummaryCallback;
    void *                      m_SummaryClientData;
    sds                          m_Help;
    bool                        m_IsExt;
    size_t                      m_ClientStructSize;
    TwStructExtInitCallback     m_StructExtInitCallback;
    TwCopyVarFromExtCallback    m_CopyVarFromExtCallback;
    TwCopyVarToExtCallback      m_CopyVarToExtCallback;
    void *                      m_ExtClientData;
    // Was CStruct's constructor (member-initializer defaults); the one
    // place a CStruct is built (TwDefineStructExt) now zero-inits/
    // sdsempty()s these fields explicitly instead.
} CStruct;
// Replaces std::vector<CStruct>.
typedef struct { CStruct *items; size_t count; size_t capacity; } CStructArray;

// followings are used for TwAddVarCB( ... StructType ... )
typedef struct CStructProxy
{
    TwType           m_Type;
    void *           m_StructData;
    bool             m_DeleteStructData;
    void *           m_StructExtData;
    TwSetVarCallback m_StructSetCallback;
    TwGetVarCallback m_StructGetCallback;
    void *           m_StructClientData;
    TwCustomDrawCallback        m_CustomDrawCallback;
    TwCustomMouseMotionCallback m_CustomMouseMotionCallback;
    TwCustomMouseButtonCallback m_CustomMouseButtonCallback;
    TwCustomMouseLeaveCallback  m_CustomMouseLeaveCallback;
    bool             m_CustomCaptureFocus;
    int              m_CustomIndexFirst;
    int              m_CustomIndexLast;
} CStructProxy;
typedef struct CMemberProxy
{
    CStructProxy *  m_StructProxy;
    int             m_MemberIndex;
    struct CTwVar * m_Var;
    struct CTwVarGroup * m_VarParent;
    CTwBar *        m_Bar;
} CMemberProxy;
// Replaces std::list<CStructProxy>/std::list<CMemberProxy>: elements
// must never move once created (their addresses are captured long-term
// as callback client-data and inside CColorExt/CQuaternionExt::
// m_StructProxy, itself embedded in the persistent variable tree), and
// nothing ever erases an individual element - only ever appended, and
// freed all at once at manager teardown - so a singly-linked list of
// individually-malloc'd nodes (never realloc'd, never moved) is the
// simplest replacement providing the same address-stability guarantee.
typedef struct CStructProxyNode { CStructProxy Proxy; struct CStructProxyNode *Next; } CStructProxyNode;
typedef struct CMemberProxyNode { CMemberProxy Proxy; struct CMemberProxyNode *Next; } CMemberProxyNode;

// Replaces std::map<unsigned int, std::string> CEnum::m_Entries: a
// linear array kept SORTED ASCENDING BY Value at all times (mirroring
// std::map's automatic key ordering) - TwBar.cpp's enum popup-list UI
// iterates this array in order to build its buttons, so losing the
// sort would silently reorder what the user sees, not crash. Use
// CEnum_Clear/CEnum_InsertOrReplace/CEnum_Find (TwMgr.cpp) to keep the
// invariant instead of touching .items directly.
typedef struct CEnumEntry { unsigned int Value; sds Label; } CEnumEntry;
typedef struct { CEnumEntry *items; size_t count; size_t capacity; } CEnumEntryArray;
typedef struct CEnum
{
    sds             m_Name;
    CEnumEntryArray m_Entries;
} CEnum;
typedef struct { CEnum *items; size_t count; size_t capacity; } CEnumArray;

// m_CDStdStringCopyBuffers: despite its name (a holdover from when
// this map was shared with the now-removed TW_TYPE_STDSTRING copy
// path), this is used only by the surviving TwCopyCDStringToLibrary
// (TW_TYPE_CDSTRING) - a per-client-pointer scratch buffer cache.
// Replaces std::map<void *, std::vector<char> >: one call site, so a
// linear-scan find-or-insert array (same idiom as CustomMap_Find/
// CTwWndArray_Find) needs no ordering/hashing.
typedef struct CCDStringCopyEntry { void *Key; CByteArray Value; } CCDStringCopyEntry;
typedef struct { CCDStringCopyEntry *items; size_t count; size_t capacity; } CCDStringCopyMap;

typedef struct CTwMgr
{
    ETwGraphAPI         m_GraphAPI;
    void *              m_Device;
    int                 m_WndID;
    struct ITwGraph *   m_Graph;
    int                 m_WndWidth;
    int                 m_WndHeight;
    const CTexFont *    m_CurrentFont;

    CTwBarPtrArray      m_Bars;
    CIntArray           m_Order;

    CBoolArray          m_MinOccupied;
    int                 m_LastMouseX;
    int                 m_LastMouseY;
    int                 m_LastMouseWheelPos;
    int                 m_IconPos;      // 0: bottom-left, 1:bottom-right, 2:top-left, 3:top-right
    int                 m_IconAlign;    // 0: vertical, 1: horizontal
    int                 m_IconMarginX, m_IconMarginY;
    bool                m_FontResizable;
    sds                 m_BarAlwaysOnTop;
    sds                 m_BarAlwaysOnBottom;
    bool                m_UseOldColorScheme;
    bool                m_Contained;
    EButtonAlign        m_ButtonAlign;
    bool                m_OverlapContent;
    bool                m_Terminating;

    sds                 m_Help;
    TwBar *             m_HelpBar;
    float               m_LastHelpUpdateTime;
    bool                m_HelpBarNotUpToDate;
    bool                m_HelpBarUpdateNow;
    void *              m_KeyPressedTextObj;
    bool                m_KeyPressedBuildText;
    sds                 m_KeyPressedStr;
    float               m_KeyPressedTime;
    void *              m_InfoTextObj;
    bool                m_InfoBuildText;
    int                 m_BarInitColorHue;
    TwBar *             m_PopupBar;
    //bool              IsProcessing() const            { return m_Processing);
    //void              SetProcessing(bool processing)  { m_Processing = processing; }

    CStructArray m_Structs;

    //void              InitVarData(TwType _Type, void *_Data, size_t _Size);
    //void              UninitVarData(TwType _Type, void *_Data, size_t _Size);
    CStructProxyNode *m_StructProxies; // head of the list; NULL when empty
    CMemberProxyNode *m_MemberProxies;

    CEnumArray           m_Enums;

    TwType              m_TypeColor32;
    TwType              m_TypeColor3F;
    TwType              m_TypeColor4F;
    TwType              m_TypeQuat4F;
    TwType              m_TypeQuat4D;
    TwType              m_TypeDir3F;
    TwType              m_TypeDir3D;

    CByteArray          m_CSStringBuffer;
    // m_CDStdStringCopyBuffers: despite its name (a holdover from when
    // this map was shared with the now-removed TW_TYPE_STDSTRING copy
    // path), this is used only by the surviving TwCopyCDStringToLibrary
    // (TW_TYPE_CDSTRING) - a per-client-pointer scratch buffer cache.
    // Replaces std::map<void *, std::vector<char> >: one call site, so a
    // linear-scan find-or-insert array (same idiom as CustomMap_Find/
    // CTwWndArray_Find) needs no ordering/hashing.
    CCDStringCopyMap    m_CDStdStringCopyBuffers;

    // Was std::vector<CCustom*> where CCustom was a polymorphic marker base
    // with zero derived classes anywhere in the codebase; every use only
    // ever read .size() (as a running custom-type-ID counter) or
    // push_back(NULL) (to increment it) - never dereferenced a stored
    // element, so a plain counter is the correct C99 shape, not an array.
    int                 m_NbCustoms;

    double              m_LastMousePressedTime;
    TwMouseButtonID     m_LastMousePressedButtonID;
    int                 m_LastMousePressedPosition[2];
    double              m_RepeatMousePressedDelay;
    double              m_RepeatMousePressedPeriod;
    bool                m_CanRepeatMousePressed;
    bool                m_IsRepeatingMousePressed;
    double              m_LastDrawTime;

    TwCopyCDStringToClient  m_CopyCDStringToClient;

    // Was `protected:` - C99 structs have no access control, and free
    // functions outside the struct (see CTwMgr_* below) need to reach
    // these fields directly, same rationale as CTwBar's own public-ification
    // in Cluster 3.
    int                 m_NbMinimizedBars;
    const char *        m_LastError;
    const char *        m_CurrentDbgFile;
    int                 m_CurrentDbgLine;
    //bool              m_Processing;
} CTwMgr;

// Was CTwMgr's own ordinary (non-virtual) member functions - converted to
// free functions taking an explicit CTwMgr* first parameter, same idiom as
// CTwBar's/CTwVarAtom's/CTwVarGroup's methods in Cluster 3. The constructor/
// destructor stay C++ for now too (final Cluster 4 pass).
void        CTwMgr_Minimize(CTwMgr *_Mgr, TwBar *_Bar);
void        CTwMgr_Maximize(CTwMgr *_Mgr, TwBar *_Bar);
void        CTwMgr_Hide(CTwMgr *_Mgr, TwBar *_Bar);
void        CTwMgr_Unhide(CTwMgr *_Mgr, TwBar *_Bar);
void        CTwMgr_SetFont(CTwMgr *_Mgr, const CTexFont *_Font, bool _ResizeBars);
void        CTwMgr_UpdateHelpBar(CTwMgr *_Mgr);
int         CTwMgr_FindBar(const CTwMgr *_Mgr, const char *_Name);
int         CTwMgr_HasAttrib(const CTwMgr *_Mgr, const char *_Attrib, bool *_HasValue);
int         CTwMgr_SetAttrib(CTwMgr *_Mgr, int _AttribID, const char *_Value);
ERetType    CTwMgr_GetAttrib(const CTwMgr *_Mgr, int _AttribID, CDoubleArray *outDouble, sds *outString);
void        CTwMgr_SetLastError(CTwMgr *_Mgr, const char *_StaticErrorMesssage); // _StaticErrorMesssage must be a static string
const char *CTwMgr_GetLastError(CTwMgr *_Mgr);                                   // returns a static string describing the error, and set LastError to NULL
const char *CTwMgr_CheckLastError(const CTwMgr *_Mgr);                           // returns the LastError, but does not set it to NULL
void        CTwMgr_SetCurrentDbgParams(CTwMgr *_Mgr, const char *file, int line);
// Was CTwMgr::CreateCursors/PixmapCursor/FreeCursors/SetCursor (3 native,
// per-platform implementations each) - deleted entirely, on every platform,
// not ported: TwSetCursorCallback() (see AntTweakBar.h) is now the only
// cursor-shape mechanism.
void        CTwMgr_SetCursor(ETwCursor _Semantic);

// CEnum helpers: keep m_Entries sorted ascending by Value (see the
// comment above CEnum) - use these instead of touching .items
// directly so the sort invariant can't be broken by a call site.
void CEnum_Clear(CEnum *_Enum); // frees every entry's label, keeps capacity
void CEnum_InsertOrReplace(CEnum *_Enum, unsigned int _Value, const char *_Label);
void CEnum_InsertOrReplaceLen(CEnum *_Enum, unsigned int _Value, const char *_Label, size_t _LabelLen); // _Label need not be NUL-terminated
sds  CEnum_Find(const CEnum *_Enum, unsigned int _Value); // returns the entry's Label, or NULL if absent

// Was CStruct::DefaultSummary (already static void ANT_CALL - a
// plain C-callable function pointer, no `this`); dropped the class
// qualification since CStruct is now a plain struct.
void ANT_CALL CStruct_DefaultSummary(char *_SummaryString, size_t _SummaryMaxLength, const void *_Value, void *_ClientData);

// Was CStructProxy/CMemberProxy's constructor/destructor. _New
// mallocs a node, prepends it to _Mgr's list (order doesn't matter - the
// only consumer is an unordered existence check), inits the embedded
// Proxy, and returns &node->Proxy - a pointer stable for the node's whole
// lifetime, same guarantee std::list gave. _Free releases a Proxy's own
// owned resources (called while walking the list at manager teardown,
// right before the node itself is freed - it does not free the node).
void CStructProxy_Init(CStructProxy *_Proxy);
void CStructProxy_Free(CStructProxy *_Proxy);
CStructProxy *CStructProxy_New(CTwMgr *_Mgr);
void CMemberProxy_Init(CMemberProxy *_Proxy);
void CMemberProxy_Free(CMemberProxy *_Proxy);
CMemberProxy *CMemberProxy_New(CTwMgr *_Mgr);
// Was CMemberProxy::SetCB/GetCB - already static (no `this`), just
// de-scoped since C has no nested-struct-scoped functions.
void ANT_CALL CMemberProxy_SetCB(const void *_Value, void *_ClientData);
void ANT_CALL CMemberProxy_GetCB(void *_Value, void *_ClientData);

// Was CTwMgr's constructor/destructor - the last two ordinary C++ member
// functions in this file, both already fully C99-shaped internally
// (every field a plain assignment, every container/proxy-list release
// already using tw_da_free/sdsfree/an explicit node walk from earlier
// passes) - only the ctor/dtor wrapper itself needed removing.
CTwMgr *CTwMgr_Create(ETwGraphAPI _GraphAPI, void *_Device, int _WndID);
void    CTwMgr_Destroy(CTwMgr *_Mgr);

extern CTwMgr *g_TwMgr;


//  ---------------------------------------------------------------------------
//  Extra functions and TwTypes
//  ---------------------------------------------------------------------------


bool TwGetKeyCode(int *_Code, int *_Modif, const char *_String);
bool TwGetKeyString(sds *_String, int _Code, int _Modif); 

// static: these are file-scope `const` data definitions in a header
// #included by multiple translation units - `const` gives them internal
// linkage in C++ (safe to leave in every TU) but *external* linkage in
// C99, which would collide as duplicate symbols at link time without an
// explicit `static` (see AGENTS.md's "Known constraints" - the same
// gotcha TwColors.h's COLOR32_* constants were already written to avoid).
// TwType(x) (a C++ functional-style cast) also becomes the C-style
// (TwType)(x) - C has no functional-cast syntax at all.
static const TwType TW_TYPE_SHORTCUT       = (TwType)(0xfff1);
static const TwType TW_TYPE_HELP_GRP       = (TwType)(0xfff2);
static const TwType TW_TYPE_HELP_ATOM      = (TwType)(0xfff3);
static const TwType TW_TYPE_HELP_HEADER    = (TwType)(0xfff4);
static const TwType TW_TYPE_HELP_STRUCT    = (TwType)(0xfff5);
static const TwType TW_TYPE_BUTTON         = (TwType)(0xfff6);
// TW_TYPE_CDSTDSTRING (0xfff7, an internal type TwAddVar used to convert
// TW_TYPE_STDSTRING into) removed along with TW_TYPE_STDSTRING.
static const TwType TW_TYPE_STRUCT_BASE    = (TwType)(0x10000000);
static const TwType TW_TYPE_ENUM_BASE      = (TwType)(0x20000000);
static const TwType TW_TYPE_CSSTRING_BASE  = TW_TYPE_CSSTRING(0);          // defined as 0x30000000 (see AntTweakBar.h)
static const TwType TW_TYPE_CSSTRING_MAX   = TW_TYPE_CSSTRING(0xfffffff);
#define TW_CSSTRING_SIZE(type)      ((int)((type)&0xfffffff))
static const TwType TW_TYPE_CUSTOM_BASE    = (TwType)(0x40000000);
// TW_TYPE_STDSTRING_VS2008/VS2010 (VC++ Debug/Release std::string ABI
// disambiguation) removed along with TW_TYPE_STDSTRING.

// `extern "C"` used to be needed here to prevent C++ name-mangling, since
// other already-converted C99 files (TwFonts.c, TwOpenGL.c, TwOpenGLCore.c)
// each declare a plain-C `int TwSetLastError(const char *)` themselves
// (not including this header at all) that must link against the same
// symbol this file defines. Now that this header/TwMgr.c are themselves
// real C99, there is no name-mangling to prevent - `extern "C"` is invalid
// C syntax and simply unneeded.
int ANT_CALL TwSetLastError(const char *_StaticErrorMessage);

// Same bridge pattern as TwSetLastError above: lets a file already
// converted to plain C99 (e.g. TwFonts.c) read g_TwMgr's current graph API.
// Returns a TwGraphAPI value (see AntTweakBar.h), or -1 if g_TwMgr is NULL.
int ANT_CALL TwMgrGetGraphAPI(void);

//const TwGraphAPI TW_OPENGL_CORE = (TwGraphAPI)5; // WIP (note: OpenGL Core Profil requires OpenGL 3.2 or later)

// Clipping helper
typedef struct CRect
{
    int X, Y, W, H;
} CRect;
// Replaces std::vector<CRect>; tw_da_* macros to build/free one are defined
// in TwBar.h, already #included by TwMgr.cpp (which is where every
// CRectArray is actually built/read) before this header.
typedef struct { CRect *items; size_t count; size_t capacity; } CRectArray;
static inline CRect CRect_Make(int _X, int _Y, int _W, int _H) { CRect r; r.X=_X; r.Y=_Y; r.W=_W; r.H=_H; return r; }
static inline bool  CRect_Empty(const CRect *_Rect, int _Margin) { return (_Rect->W<=_Margin || _Rect->H<=_Margin); }
static inline bool  CRect_Equal(const CRect *_A, const CRect *_B) { return (CRect_Empty(_A,0) && CRect_Empty(_B,0)) || (_A->X==_B->X && _A->Y==_B->Y && _A->W==_B->W && _A->H==_B->H); }
bool CRect_Subtract(const CRect *_Rect, const CRect *_Other, CRectArray *_OutRects);
bool CRect_SubtractMany(const CRect *_Rect, const CRectArray *_Others, CRectArray *_OutRects);


//  ---------------------------------------------------------------------------
//  Global bar attribs
//  ---------------------------------------------------------------------------


enum EMgrAttribs
{
    MGR_HELP = 1,
    MGR_FONT_SIZE,
    MGR_FONT_STYLE,
    MGR_ICON_POS,
    MGR_ICON_ALIGN,
    MGR_ICON_MARGIN,
    MGR_FONT_RESIZABLE,
    MGR_COLOR_SCHEME,
    MGR_CONTAINED,
    MGR_BUTTON_ALIGN,
    MGR_OVERLAP
};


//  ---------------------------------------------------------------------------
//  Color struct ext
//  ---------------------------------------------------------------------------


typedef struct CColorExt
{
    int                  R, G, B;
    int                  H, L, S;
    int                  A;
    bool                 m_HLS, m_HasAlpha, m_OGL;
    bool                 m_CanHaveAlpha;
    bool                 m_IsColorF;
    unsigned int         m_PrevConvertedColor;
    CStructProxy*m_StructProxy;
} CColorExt;
// CColorExt's own methods, converted from C++ instance methods to free
// functions (RGB2HLS/HLS2RGB used `this`) and de-scoped static callbacks
// (no `this` to begin with - these were always plain C-callable function
// pointers, just qualified by the class for namespacing).
void CColorExt_RGB2HLS(CColorExt *_Ext);
void CColorExt_HLS2RGB(CColorExt *_Ext);
void ANT_CALL CColorExt_InitColor32CB(void *_ExtValue, void *_ClientData);
void ANT_CALL CColorExt_InitColor3FCB(void *_ExtValue, void *_ClientData);
void ANT_CALL CColorExt_InitColor4FCB(void *_ExtValue, void *_ClientData);
void ANT_CALL CColorExt_CopyVarFromExtCB(void *_VarValue, const void *_ExtValue, unsigned int _ExtMemberIndex, void *_ClientData);
void ANT_CALL CColorExt_CopyVarToExtCB(const void *_VarValue, void *_ExtValue, unsigned int _ExtMemberIndex, void *_ClientData);
void ANT_CALL CColorExt_SummaryCB(char *_SummaryString, size_t _SummaryMaxLength, const void *_ExtValue, void *_ClientData);
void CColorExt_CreateTypes(void);


//  ---------------------------------------------------------------------------
//  Quaternion struct ext
//  ---------------------------------------------------------------------------


typedef struct CQuaternionExt
{
    double               Qx, Qy, Qz, Qs;    // Quat value
    double               Vx, Vy, Vz, Angle; // Not used
    double               Dx, Dy, Dz;        // Dir value set when used as a direction
    bool                 m_AAMode;          // Axis & angle mode -> disabled
    bool                 m_ShowVal;         // Display values
    bool                 m_IsFloat;         // Quat/Dir uses floats
    bool                 m_IsDir;           // Mapped to a dir vector instead of a quat
    double               m_Dir[3];          // If not zero, display one direction vector
    color32              m_DirColor;        // Direction vector color
    float                m_Permute[3][3];   // Permute frame axis
    CStructProxy*m_StructProxy;
    bool                 m_Highlighted;
    bool                 m_Rotating;
    double               m_OrigQuat[4];
    float                m_OrigX, m_OrigY;
    double               m_PrevX, m_PrevY;
} CQuaternionExt;
// CQuaternionExt's own methods, converted the same way as CColorExt's above:
// real this-using instance methods (ConvertToAxisAngle/ConvertFromAxisAngle/
// CopyToVar/Permute/PermuteInv) became free functions; already-static
// methods and callbacks were just de-scoped (dropped the CQuaternionExt::
// qualifier - C has no nested-scope functions).
void ANT_CALL CQuaternionExt_InitQuat4FCB(void *_ExtValue, void *_ClientData);
void ANT_CALL CQuaternionExt_InitQuat4DCB(void *_ExtValue, void *_ClientData);
void ANT_CALL CQuaternionExt_InitDir3FCB(void *_ExtValue, void *_ClientData);
void ANT_CALL CQuaternionExt_InitDir3DCB(void *_ExtValue, void *_ClientData);
void ANT_CALL CQuaternionExt_CopyVarFromExtCB(void *_VarValue, const void *_ExtValue, unsigned int _ExtMemberIndex, void *_ClientData);
void ANT_CALL CQuaternionExt_CopyVarToExtCB(const void *_VarValue, void *_ExtValue, unsigned int _ExtMemberIndex, void *_ClientData);
void ANT_CALL CQuaternionExt_SummaryCB(char *_SummaryString, size_t _SummaryMaxLength, const void *_ExtValue, void *_ClientData);
void ANT_CALL CQuaternionExt_DrawCB(int _W, int _H, void *_ExtValue, void *_ClientData, TwBar *_Bar, CTwVarGroup *varGrp);
bool ANT_CALL CQuaternionExt_MouseMotionCB(int _MouseX, int _MouseY, int _W, int _H, void *_StructExtValue, void *_ClientData, TwBar *_Bar, CTwVarGroup *varGrp);
bool ANT_CALL CQuaternionExt_MouseButtonCB(TwMouseButtonID _Button, bool _Pressed, int _MouseX, int _MouseY, int _W, int _H, void *_StructExtValue, void *_ClientData, TwBar *_Bar, CTwVarGroup *varGrp);
void ANT_CALL CQuaternionExt_MouseLeaveCB(void *_StructExtValue, void *_ClientData, TwBar *_Bar);
void CQuaternionExt_CreateTypes(void);
void CQuaternionExt_ConvertToAxisAngle(CQuaternionExt *_Ext);
void CQuaternionExt_ConvertFromAxisAngle(CQuaternionExt *_Ext);
void CQuaternionExt_CopyToVar(CQuaternionExt *_Ext);
enum EArrowParts     { ARROW_CONE, ARROW_CONE_CAP, ARROW_CYL, ARROW_CYL_CAP };
void CQuaternionExt_CreateSphere(void);
void CQuaternionExt_CreateArrow(void);
void CQuaternionExt_ApplyQuat(float *outX, float *outY, float *outZ, float x, float y, float z, float qx, float qy, float qz, float qs);
void CQuaternionExt_QuatFromDir(double *outQx, double *outQy, double *outQz, double *outQs, double dx, double dy, double dz);
void CQuaternionExt_PermuteF(const CQuaternionExt *_Ext, float *outX, float *outY, float *outZ, float x, float y, float z);
void CQuaternionExt_PermuteInvF(const CQuaternionExt *_Ext, float *outX, float *outY, float *outZ, float x, float y, float z);
void CQuaternionExt_PermuteD(const CQuaternionExt *_Ext, double *outX, double *outY, double *outZ, double x, double y, double z);
void CQuaternionExt_PermuteInvD(const CQuaternionExt *_Ext, double *outX, double *outY, double *outZ, double x, double y, double z);


//  ---------------------------------------------------------------------------
//  TwFPU_Save/TwFPU_Restore set and restore the fpu precision if needed.
//  (could be useful because DirectX changes it and AntTweakBar requires default double precision)
//  Was CTwFPU, an RAII guard (constructor=Save, destructor=Restore); converted
//  to an explicit save/restore pair since C99 has no destructors - every
//  former `CTwFPU fpu;` local's enclosing function must now call
//  TwFPU_Restore() on every exit path (see the call sites in TwMgr.cpp/
//  TwBar.cpp). Nesting is safe: TwFPU_Save() captures whatever the FPU's
//  control word actually is at call time (not a global on/off flag), so a
//  nested Save/Restore pair just captures-and-restores its own narrower
//  scope's value, exactly like the original nested CTwFPU locals did.
//  ---------------------------------------------------------------------------


unsigned int TwFPU_Save(void);
void TwFPU_Restore(unsigned int state0);

//  ---------------------------------------------------------------------------


#endif // !defined ANT_TW_MGR_INCLUDED
