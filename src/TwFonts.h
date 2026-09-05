//  ---------------------------------------------------------------------------
//
//  @file       TwFonts.h
//  @brief      Bitmaps fonts
//  @author     Philippe Decaudin
//  @license    This file is part of the AntTweakBar library.
//              For conditions of distribution and use, see License.txt
//
//  note:       Private header
//
//  ---------------------------------------------------------------------------


#if !defined ANT_TW_FONTS_INCLUDED
#define ANT_TW_FONTS_INCLUDED

//#include <AntTweakBar.h>

/*
A source bitmap includes 224 characters starting from ascii char 32 (i.e. space)
to ascii char 255 (extended ASCII Latin1/CP1252):

 !"#$%&'()*+,-./0123456789:;<=>?
@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
`abcdefghijklmnopqrstuvwxyz{|}~
��������������������������������
��������������������������������
��������������������������������
��������������������������������

First pixel column of a source bitmap is a delimiter with color=zero at the end of each line of characters.
Last pixel row of a line of characters is a delimiter with color=zero at the last pixel of each character.

*/


struct CTexFont
{
    unsigned char * m_TexBytes;
    int             m_TexWidth;     // power of 2
    int             m_TexHeight;    // power of 2
    float           m_CharU0[256];
    float           m_CharV0[256];
    float           m_CharU1[256];
    float           m_CharV1[256];
    int             m_CharWidth[256];
    int             m_CharHeight;
    int             m_NbCharRead;
};
typedef struct CTexFont CTexFont;

// CTexFont no longer has C++ constructor/destructor - plain C99 init/free
// functions instead. Both are safe to call with _Font==NULL (matching C++
// new/delete's NULL-safety), and only used internally by TwFonts.c itself -
// no other file constructs/destroys a CTexFont (only ever holds/reads
// `const CTexFont *` pointers returned by TwGenerateFont(), confirmed by
// inspection during the C99 port).
void TwTexFont_Init(CTexFont *_Font);
void TwTexFont_Free(CTexFont *_Font);


CTexFont *TwGenerateFont(const unsigned char *_Bitmap, int _BmWidth, int _BmHeight, float _Scaling);


extern CTexFont *g_DefaultSmallFont;
extern CTexFont *g_DefaultNormalFont;
extern CTexFont *g_DefaultLargeFont;
extern CTexFont *g_DefaultFixed1Font;

void TwGenerateDefaultFonts(float _Scaling);
void TwDeleteDefaultFonts(void);


#endif  // !defined ANT_TW_FONTS_INCLUDED
