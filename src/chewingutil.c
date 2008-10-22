/**
 * chewingutil.c
 *
 * Copyright (c) 1999, 2000, 2001
 *	Lu-chuan Kung and Kang-pen Chen.
 *	All rights reserved.
 *
 * Copyright (c) 2004, 2005, 2006
 *	libchewing Core Team. See ChangeLog for details.
 *
 * See the file "COPYING" for information on usage and redistribution
 * of this file.
 */

/* This file is encoded in UTF-8 */

#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "chewing-utf8-util.h"
#include "global.h"
#include "global-private.h"
#include "chewingutil.h"
#include "zuin.h"
#include "zuin-private.h"
#include "choice-private.h"
#include "tree-private.h"
#include "userphrase-private.h"
#include "private.h"

extern const char *zhuin_tab[]; 
static void MakePreferInterval( ChewingData *pgdata );
static void ShiftInterval( ChewingOutput *pgo, ChewingData *pgdata );
static int ChewingKillSelectIntervalAcross( int cursor, ChewingData *pgdata );

static int FindSymbolKey( const char *symbol );
static SymbolEntry** symbol_table = NULL;
static unsigned int n_symbol_entry = 0;

static char g_easy_symbol_key[] = {
	'0','1','2','3','4','5','6','7','8','9',
	'A','B','C','D','E','F','G','H','I','J',
	'K','L','M','N','O','P','Q','R','S','T',
	'U','V','W','X','Y','Z'
};

#define EASY_SYMBOL_KEY_TAB_LEN \
	sizeof( g_easy_symbol_key )
static char *g_easy_symbol_value[ EASY_SYMBOL_KEY_TAB_LEN ] = { NULL };
static int g_easy_symbol_num[ EASY_SYMBOL_KEY_TAB_LEN ] = { 0 };

static int FindEasySymbolIndex( char ch )
{
	int lo, hi, mid;

	lo = 0;
	hi = EASY_SYMBOL_KEY_TAB_LEN - 1;
	while ( lo <= hi ) {
		mid = (hi - lo) / 2 + lo;
		if ( ch > g_easy_symbol_key[ mid ] ) {
			lo = mid + 1;
			continue;
		}
		else if ( ch < g_easy_symbol_key[ mid ] ) {
			hi = mid - 1;
			continue;
		}
		return mid;
	}
	return -1;
}

void SetUpdatePhraseMsg( ChewingData *pgdata, char *addWordSeq, int len, int state )
{
	char *insert = "\xE5\x8A\xA0\xE5\x85\xA5\xEF\xBC\x9A";
	char *modify = "\xE5\xB7\xB2\xE6\x9C\x89\xEF\xBC\x9A";
	int begin = 3, i;

	pgdata->showMsgLen = begin + len;
	if ( state == USER_UPDATE_INSERT ) {
		ueStrNCpy( (char *) pgdata->showMsg[ 0 ].s, insert, 1, 1);
		ueStrNCpy( (char *) pgdata->showMsg[ 1 ].s,
		           ueStrSeek( insert, 1 ), 
		           1, 1 );
		ueStrNCpy( (char *) pgdata->showMsg[ 2 ].s,
		           ueStrSeek( insert, 2 ), 
		           1, 1 );
	}
	else {
		ueStrNCpy( (char *) pgdata->showMsg[ 0 ].s, modify, 1, 1);
		ueStrNCpy( (char *) pgdata->showMsg[ 1 ].s,
		           ueStrSeek( modify, 1 ),
			   1, 1 );
		ueStrNCpy( (char *) pgdata->showMsg[ 2 ].s,
		           ueStrSeek( modify, 2 ),
			   1, 1 );
	}
	for ( i = 0; i < len; i++ ) {
		ueStrNCpy( (char *) pgdata->showMsg[ begin + i ].s,
		           ueStrSeek(addWordSeq, i),
			   1, 1);
	}
}

int NoSymbolBetween( ChewingData *pgdata, int begin, int end )
{
	int i, nChi, k;

	/* find the beginning index in the chiSymbolBuf */
	for ( nChi = i = 0; i < pgdata->chiSymbolBufLen && nChi < begin; i++ )
		/* it is Chinese word */
		if ( pgdata->chiSymbolBuf[ i ].wch == (wchar_t) 0 )
			nChi++;

	for ( k = i + 1; k < pgdata->chiSymbolBufLen && k <= end; k++ )
		/*  not a Chinese word */
		if ( pgdata->chiSymbolBuf[ i ].wch != (wchar_t) 0 )
			return 0;

	return 1;
}

int ChewingIsEntering( ChewingData *pgdata )
{
	if ( pgdata->choiceInfo.isSymbol )
		return 1;
	return (
		pgdata->chiSymbolBufLen != 0 || 
		ZuinIsEntering( &( pgdata->zuinData ) ) );
}

#define CEIL_DIV(a,b) ((a+b-1)/b)

int HaninSymbolInput(
		ChoiceInfo *pci, 
		AvailInfo *pai, 
		const uint16 phoneSeq[] UNUSED, 
		int candPerPage)
{
	unsigned int i;

	/* No available symbol table */
	if ( ! symbol_table )
		return ZUIN_ABSORB;

	pci->nTotalChoice = 0;
	for ( i = 0; i < n_symbol_entry; i++ ) {
		strcpy( pci->totalChoiceStr[ pci->nTotalChoice ], 
			symbol_table[ i ]->category );
		pci->nTotalChoice++; 
	}
	pai->avail[ 0 ].len = 1;
	pai->avail[ 0 ].id = -1;  
	pai->nAvail = 1;
	pai->currentAvail = 0;
	pci->nChoicePerPage = candPerPage;
	if ( pci->nChoicePerPage > MAX_SELKEY ) {
		pci->nChoicePerPage = MAX_SELKEY;
	}
	pci->nPage = CEIL_DIV( pci->nTotalChoice, pci->nChoicePerPage );
	pci->pageNo = 0;
	pci->isSymbol = 1;
	return ZUIN_ABSORB;
}

static int _Inner_InternalSpecialSymbol(
		int key, ChewingData *pgdata, 
		char symkey, char *chibuf )
{
	int rtn = ZUIN_IGNORE; /* very strange , and very difficult to understand */
	int kbtype;

	if ( key == symkey && NULL != chibuf ) {
		rtn = ZUIN_ABSORB;
		memmove( 
			&( pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor + 1 ] ),
			&( pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor ] ),
			sizeof( wch_t ) * ( pgdata->chiSymbolBufLen - pgdata->chiSymbolCursor ) );

		pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor ].wch = (wchar_t) 0;
		ueStrNCpy( (char *) pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor ].s,
				chibuf, 1, 1);
		/* Save Symbol Key */
		memmove( 
			&( pgdata->symbolKeyBuf[ pgdata->chiSymbolCursor + 1 ] ),
			&( pgdata->symbolKeyBuf[ pgdata->chiSymbolCursor ] ),
			sizeof( pgdata->symbolKeyBuf[0] ) * 
			( pgdata->chiSymbolBufLen - pgdata->chiSymbolCursor ) );
		pgdata->symbolKeyBuf[ pgdata->chiSymbolCursor ] = key;
		pgdata->chiSymbolCursor++;
		pgdata->chiSymbolBufLen++;
		pgdata->bUserArrCnnct[ pgdata->cursor ] = 0;
		/* reset Zuin data */
		/* Don't forget the kbtype */
		kbtype = pgdata->zuinData.kbtype;
		memset( &( pgdata->zuinData ), 0, sizeof( ZuinData ) );
		pgdata->zuinData.kbtype = kbtype;
		return 1;
	}
	return 0;
}

static int InternalSpecialSymbol(
		int key, ChewingData *pgdata,
		int nSpecial, char keybuf[], char *chibuf[] )
{
	int i, rtn = ZUIN_IGNORE; /* very strange , and very difficult to understand */

	for ( i = 0; i < nSpecial; i++ ) {
		if ( 1 == _Inner_InternalSpecialSymbol( key, pgdata, keybuf[ i ], chibuf[ i ]) ) {
			rtn = ZUIN_ABSORB;
			break;
		}
	}
	return rtn;
}

int SpecialSymbolInput( int key, ChewingData *pgdata )
{
	static char keybuf[] = {
		'[', ']', '{', '}', '\'','<', ':', '\"', '>',
		'~', '!', '@', '#', '$', '%', '^', '&', '*',
		'(', ')', '_', '+', '=','\\', '|', '?',
		',', '.', ';'
	};

	static char *chibuf[] = {
		"\xE3\x80\x8C", "\xE3\x80\x8D", "\xE3\x80\x8E", "\xE3\x80\x8F",
		"\xE3\x80\x81", "\xEF\xBC\x8C", "\xEF\xBC\x9A", "\xEF\xBC\x9B",
		"\xE3\x80\x82", "\xEF\xBD\x9E", "\xEF\xBC\x81", "\xEF\xBC\xA0",
		"\xEF\xBC\x83", "\xEF\xBC\x84", "\xEF\xBC\x85", "\xEF\xB8\xBF",
		"\xEF\xBC\x86", "\xEF\xBC\x8A", "\xEF\xBC\x88", "\xEF\xBC\x89",
		"\xEF\xB9\x8D", "\xEF\xBC\x8B", "\xEF\xBC\x9D", "\xEF\xBC\xBC",
		"\xEF\xBD\x9C", "\xEF\xBC\x9F", "\xEF\xBC\x8C", "\xE3\x80\x82",
		"\xEF\xBC\x9B"
	};
	static int nSpecial = 29;
	return InternalSpecialSymbol( key, pgdata, nSpecial, keybuf, chibuf );
}

int FullShapeSymbolInput( int key, ChewingData *pgdata )
{
	int rtn;
	static char keybuf[] = {
		'0','1','2','3','4','5','6','7','8','9',
		'a','b','c','d','e','f','g','h','i','j',
		'k','l','m','n','o','p','q','r','s','t',
		'u','v','w','x','y','z','A','B','C','D',
		'E','F','G','H','I','J','K','L','M','N',
		'O','P','Q','R','S','T','U','V','W','X',
		'Y','Z',' ','\"','\'','/','<','>','`','[',
		']','{','}','+','-'
	};
	static char* chibuf[] = {
		"\xEF\xBC\x90","\xEF\xBC\x91","\xEF\xBC\x92","\xEF\xBC\x93","\xEF\xBC\x94",
		"\xEF\xBC\x95","\xEF\xBC\x96","\xEF\xBC\x97","\xEF\xBC\x98","\xEF\xBC\x99",
		"\xEF\xBD\x81","\xEF\xBD\x82","\xEF\xBD\x83","\xEF\xBD\x84","\xEF\xBD\x85",
		"\xEF\xBD\x86","\xEF\xBD\x87","\xEF\xBD\x88","\xEF\xBD\x89","\xEF\xBD\x8A",
		"\xEF\xBD\x8B","\xEF\xBD\x8C","\xEF\xBD\x8D","\xEF\xBD\x8E","\xEF\xBD\x8F",
		"\xEF\xBD\x90","\xEF\xBD\x91","\xEF\xBD\x92","\xEF\xBD\x93","\xEF\xBD\x94",
		"\xEF\xBD\x95","\xEF\xBD\x96","\xEF\xBD\x97","\xEF\xBD\x98","\xEF\xBD\x99",
		"\xEF\xBD\x9A","\xEF\xBC\xA1","\xEF\xBC\xA2","\xEF\xBC\xA3","\xEF\xBC\xA4",
		"\xEF\xBC\xA5","\xEF\xBC\xA6","\xEF\xBC\xA7","\xEF\xBC\xA8","\xEF\xBC\xA9",
		"\xEF\xBC\xAA","\xEF\xBC\xAB","\xEF\xBC\xAC","\xEF\xBC\xAD","\xEF\xBC\xAE",
		"\xEF\xBC\xAF","\xEF\xBC\xB0","\xEF\xBC\xB1","\xEF\xBC\xB2","\xEF\xBC\xB3",
		"\xEF\xBC\xB4","\xEF\xBC\xB5","\xEF\xBC\xB6","\xEF\xBC\xB7","\xEF\xBC\xB8",
		"\xEF\xBC\xB9","\xEF\xBC\xBA","\xE3\x80\x80","\xE2\x80\x9D","\xE2\x80\x99",
		"\xEF\xBC\x8F","\xEF\xBC\x9C","\xEF\xBC\x9E","\xE2\x80\xB5","\xE3\x80\x94",
		"\xE3\x80\x95","\xEF\xBD\x9B","\xEF\xBD\x9D","\xEF\xBC\x8B","\xEF\xBC\x8D"
	};
	static int nSpecial = sizeof(keybuf) / sizeof(char);
	rtn = InternalSpecialSymbol( key, pgdata, nSpecial, keybuf, chibuf );
	if ( rtn == ZUIN_IGNORE )
		rtn = SpecialSymbolInput( key, pgdata );
	return (rtn == ZUIN_IGNORE ? SYMBOL_KEY_ERROR : SYMBOL_KEY_OK);
}

int EasySymbolInput( int key, ChewingData *pgdata, ChewingOutput *pgo UNUSED )
{
	int rtn, loop, index;
	char wordbuf[ 8 ];

	int nSpecial = EASY_SYMBOL_KEY_TAB_LEN / sizeof( char );

	index = FindEasySymbolIndex( key );
	if ( -1 != index ) {
		for ( loop = 0; loop < g_easy_symbol_num[ index ]; ++loop ) {
			ueStrNCpy(wordbuf, 
					ueStrSeek( g_easy_symbol_value[ index ], loop), 1, 1 );
			rtn = _Inner_InternalSpecialSymbol( key, pgdata, key, wordbuf );
		}
		return SYMBOL_KEY_OK;
	}

	rtn = InternalSpecialSymbol( 
			key, pgdata, nSpecial, 
			g_easy_symbol_key, g_easy_symbol_value);
	if ( rtn == ZUIN_IGNORE )
		rtn = SpecialSymbolInput( key, pgdata );
	return (rtn == ZUIN_IGNORE ? SYMBOL_KEY_ERROR : SYMBOL_KEY_OK);
}

#if 0
int SpecialEtenSymbolInput( int key, ChewingData *pgdata )
{
	static char keybuf[] = {
		17, 23, 5, 18, 20, 25, 21, 9, 15, 16,
		1, 19, 4, 6, 7, 8, 10, 11, 12, 59, 39,
		26, 24, 3, 22, 2, 14, 13, 44, 46, 47
	};

	static char *chibuf[] = {
		"\xE2\x94\x8C","\xE2\x94\xAC","\xE2\x94\x90","\xE2\x96\xA1","\xE3\x80\x88",
		"\xE3\x80\x89","\xE2\x80\xA6","\xE3\x80\x81","\xE3\x80\x82","\xE2\x80\xBB",
		"\xE2\x94\x9C","\xE2\x94\xBC","\xE2\x94\xA4","\xE3\x80\x90","\xE3\x80\x91",
		"\xE2\x97\x87","\xE2\x97\x8B","\xE2\x80\x94","\xE2\x94\x82","\xEF\xBC\x9B",
		"\xEF\xBC\x9A","\xE2\x94\x94","\xE2\x94\xB4","\xE2\x94\x98", "\xCB\x87",
		"\xE3\x80\x8A", "\xE3\x80\x8B" ,"\xE2\x94\x80", "\xEF\xBC\x8C","\xEF\xBC\x8E",
		"\xEF\xBC\x9F"
	};
	static int nSpecial = 31;
	return InternalSpecialSymbol( key, pgdata, nSpecial, keybuf, chibuf );
}
#endif

int SymbolChoice( ChewingData *pgdata, int sel_i )
{
	int kbtype;
	int i;
	int symbol_type;
	int key;

	if ( ! symbol_table && pgdata->choiceInfo.isSymbol != 3 )
		return ZUIN_ABSORB;

	if ( pgdata->choiceInfo.isSymbol == 1 && 
			0 == symbol_table[sel_i]->nSymbols )
		symbol_type = 2;
	else
		symbol_type = pgdata->choiceInfo.isSymbol;

	/* level one, symbol category */
	if ( symbol_type == 1 ) {
		ChoiceInfo* pci = &pgdata->choiceInfo;
		AvailInfo* pai = &pgdata->availInfo;

		/* Display all symbols in this category */
		pci->nTotalChoice = 0;
		for ( i = 0; i < symbol_table[ sel_i ]->nSymbols; i++ ) {
			ueStrNCpy( pci->totalChoiceStr[ pci->nTotalChoice ],
					symbol_table[ sel_i ]->symbols[ i ], 1, 1 );
			pci->nTotalChoice++;
		}
		pai->avail[ 0 ].len = 1;
		pai->avail[ 0 ].id = -1;  
		pai->nAvail = 1;
		pai->currentAvail = 0;
		pci->nChoicePerPage = pgdata->config.candPerPage;
		if ( pci->nChoicePerPage > MAX_SELKEY )
			pci->nChoicePerPage = MAX_SELKEY;
		pci->nPage = CEIL_DIV( pci->nTotalChoice, pci->nChoicePerPage );
		pci->pageNo = 0;
		pci->isSymbol = 2;
	}
	else { /* level 2 symbol or OpenSymbolChoice */
		/* TODO: FIXME, this part is buggy! */
		if ( symbol_type == 2 ) {
			memmove(
				&( pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor + 1 ] ),
				&( pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor ] ),
				sizeof( wch_t ) * ( pgdata->chiSymbolBufLen - pgdata->chiSymbolCursor ) );
		}
		pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor ].wch = (wchar_t) 0 ;
		ueStrNCpy( (char *) pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor ].s,
				pgdata->choiceInfo.totalChoiceStr[ sel_i ], 1, 1);

		/* This is very strange */
		key = FindSymbolKey( pgdata->choiceInfo.totalChoiceStr[ sel_i ] );
		pgdata->symbolKeyBuf[ pgdata->chiSymbolCursor ] = key ? key : '1';

		pgdata->bUserArrCnnct[ pgdata->cursor ] = 0;
		ChoiceEndChoice(pgdata);
		/* Don't forget the kbtype */
		kbtype = pgdata->zuinData.kbtype;
		memset( &( pgdata->zuinData ), 0, sizeof( ZuinData ) );
		pgdata->zuinData.kbtype = kbtype;

		if ( symbol_type == 2 ) {
			pgdata->chiSymbolBufLen++;
			pgdata->chiSymbolCursor ++ ; 
			if ( ! pgdata->bAutoShiftCur ) {
				/* No action */
			}
		}
		else if ( symbol_type == 3 ) { /* OpenSymbolChoice */
			/* No action */
		}
		pgdata->choiceInfo.isSymbol = 0;
	}
	return ZUIN_ABSORB;
}

int SymbolInput( int key, ChewingData *pgdata )
{
	if ( isprint( (char) key ) && /* other character was ignored */
	     (pgdata->chiSymbolBufLen < MAX_PHONE_SEQ_LEN) ) { /* protect the buffer */
		memmove(
			&( pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor + 1 ] ),
			&( pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor ] ),
			sizeof( wch_t ) * ( pgdata->chiSymbolBufLen - pgdata->chiSymbolCursor ) );

		pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor ].wch = (wchar_t) 0;
		pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor ].s[ 0 ] = (char) key;

		/* Save Symbol Key */
		memmove( &( pgdata->symbolKeyBuf[ pgdata->chiSymbolCursor + 1 ] ),
			&( pgdata->symbolKeyBuf[ pgdata->chiSymbolCursor ] ),
			sizeof( pgdata->symbolKeyBuf[0] ) * 
			( pgdata->chiSymbolBufLen - pgdata->chiSymbolCursor ) );
			pgdata->symbolKeyBuf[ pgdata->chiSymbolCursor ] = toupper( key );

		pgdata->chiSymbolCursor++;
		pgdata->chiSymbolBufLen++;
		pgdata->bUserArrCnnct[ pgdata->cursor ] = 0;
		return SYMBOL_KEY_OK;
	}
	return SYMBOL_KEY_ERROR;
}

static int CompInterval( const IntervalType *a, const IntervalType *b )
{
	int cmp = a->from - b->from;
	if ( cmp )
		return cmp;
	return ( a->to - b->to );
}

static int FindIntervalFrom( int from, IntervalType inte[], int nInte )
{
	int i;

	for ( i = 0; i < nInte; i++ )
		if ( inte[ i ].from == from )
			return i;
	return -1;
}

int WriteChiSymbolToBuf( wch_t csBuf[], int csBufLen, ChewingData *pgdata )
{
	int i, phoneseq_i = 0;

	for ( i = 0 ; i < csBufLen; i++ ) {
		if ( ChewingIsChiAt( i, pgdata ) ) {
			/*
			 * Workaround to avoid different initialization behavior 
			 * among Win32 and Unix-like OSs.
			 */
			memset( &( csBuf[ i ].s ), 0, MAX_UTF8_SIZE + 1 );
			ueStrNCpy( (char *) csBuf[ i ].s,
			           &( pgdata->phrOut.chiBuf[ phoneseq_i ] ), 
				   1, 1);
			phoneseq_i += ueBytesFromChar( pgdata->phrOut.chiBuf[ phoneseq_i ] );
		}
		else 
			csBuf[ i ] = pgdata->chiSymbolBuf[ i ];
	}
	return 0;
}

static int CountReleaseNum( ChewingData *pgdata )
{
	int remain, i;

	/* reserve ZUIN_SIZE positions for Zuin */
	remain = pgdata->chiSymbolBufLen + ZUIN_SIZE - pgdata->config.maxChiSymbolLen;
	if ( remain <= 0 )
		return 0;

	qsort(
		pgdata->preferInterval, 
		pgdata->nPrefer, 
		sizeof( IntervalType ),
		(CompFuncType) CompInterval ); 

	if ( ! ChewingIsChiAt( 0, pgdata ) ) {
		for ( i = 0; i < pgdata->chiSymbolCursor; ++i ) {
			if ( ChewingIsChiAt( i, pgdata ) ) {
				break;
			}
		}
		return i;
	}
	
	i = FindIntervalFrom( 0, pgdata->preferInterval, pgdata->nPrefer );
	if ( i >= 0 ) {
		return ( pgdata->preferInterval[ i ].to - pgdata->preferInterval[ i ].from ); 
	}

	return 1;
}

static void KillFromLeft( ChewingData *pgdata, int nKill )
{
	int i;

	for ( i = 0; i < nKill; i++ )
		ChewingKillChar( pgdata, 0, 0, DECREASE_CURSOR );
}

void CleanAllBuf( ChewingData *pgdata )
{
	/* 1 */
	pgdata->nPhoneSeq = 0 ;
	memset( pgdata->phoneSeq, 0, sizeof( pgdata->phoneSeq ) );
	/* 2 */
	pgdata->chiSymbolBufLen = 0;
	memset( pgdata->chiSymbolBuf, 0, sizeof( pgdata->chiSymbolBuf ) );
	/* 3 */
	memset( pgdata->bUserArrBrkpt, 0, sizeof( pgdata->bUserArrBrkpt ) );
	/* 4 */
	pgdata->nSelect = 0;
	/* 5 */
	pgdata->cursor = 0;
	/* 6 */
	pgdata->chiSymbolCursor = 0;
	/* 7 */
	memset( pgdata->bUserArrCnnct, 0, sizeof( pgdata->bUserArrCnnct ) );

	pgdata->phrOut.nNumCut = 0;

	memset( pgdata->symbolKeyBuf, 0, sizeof( pgdata->symbolKeyBuf ) );

	pgdata->nPrefer = 0;
}

int ReleaseChiSymbolBuf( ChewingData *pgdata, ChewingOutput *pgo )
{
	int throwEnd;
	uint16 bufPhoneSeq[ MAX_PHONE_SEQ_LEN + 1 ];
	char bufWordSeq[ MAX_PHONE_SEQ_LEN * 3 + 1 ];

	throwEnd = CountReleaseNum( pgdata );

	pgo->nCommitStr = throwEnd;
	if ( throwEnd ) {
		/*
		 * count how many chinese words in "chiSymbolBuf[ 0 .. (throwEnd - 1)]"
		 * And release from "chiSymbolBuf" && "phoneSeq"
		 */
		WriteChiSymbolToBuf( pgo->commitStr, throwEnd, pgdata );

		/* Add to userphrase */
		memcpy( bufPhoneSeq, pgdata->phoneSeq, sizeof( uint16 ) * throwEnd );
		bufPhoneSeq[ throwEnd ] = (uint16) 0;
		ueStrNCpy( bufWordSeq, pgdata->phrOut.chiBuf, throwEnd, 1 );
		UserUpdatePhrase( bufPhoneSeq, bufWordSeq );

		KillFromLeft( pgdata, throwEnd );
	}
	return throwEnd;
}

void AutoLearnPhrase( ChewingData *pgdata )
{
	uint16 bufPhoneSeq[ MAX_PHONE_SEQ_LEN + 1 ];
	char bufWordSeq[ MAX_PHONE_SEQ_LEN * 3 + 1 ];
	int i, from, len;

	for ( i = 0; i < pgdata->nPrefer; i++ ) {
		from = pgdata->preferInterval[ i ].from;
		len = pgdata->preferInterval[i].to - from;
		memcpy( bufPhoneSeq, &pgdata->phoneSeq[ from ], sizeof( uint16 ) * len );
		bufPhoneSeq[ len ] = (uint16) 0;
		ueStrNCpy( bufWordSeq,
		           ueStrSeek( (char *) &pgdata->phrOut.chiBuf, from ),
		           len, 1);
		UserUpdatePhrase( bufPhoneSeq, bufWordSeq );
	}
}

int AddChi( uint16 phone, ChewingData *pgdata )
{
	int i;

	/* shift the selectInterval */
	for ( i = 0; i < pgdata->nSelect; i++ ) {
		if ( pgdata->selectInterval[ i ].from >= pgdata->cursor ) {
			pgdata->selectInterval[ i ].from++;
			pgdata->selectInterval[ i ].to++;
		}
	}

	/* shift the Brkpt */
	memmove( 
		&( pgdata->bUserArrBrkpt[ pgdata->cursor + 2 ] ),
		&( pgdata->bUserArrBrkpt[ pgdata->cursor + 1 ] ),
		sizeof( int ) * ( pgdata->nPhoneSeq - pgdata->cursor ) );
	memmove(
		&( pgdata->bUserArrCnnct[ pgdata->cursor + 2 ] ),
		&( pgdata->bUserArrCnnct[pgdata->cursor + 1 ] ),
		sizeof( int ) * ( pgdata->nPhoneSeq - pgdata->cursor ) );

	/* add to phoneSeq */
	memmove(
		&( pgdata->phoneSeq[ pgdata->cursor + 1 ] ),
		&( pgdata->phoneSeq[ pgdata->cursor ] ) ,
		sizeof( uint16 ) * ( pgdata->nPhoneSeq - pgdata->cursor ) );
	pgdata->phoneSeq[ pgdata->cursor ] = phone;
	pgdata->nPhoneSeq ++;
	pgdata->cursor ++;

	/* add to chiSymbolBuf */
	memmove(
		&( pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor + 1 ] ),
		&( pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor ] ) ,
		sizeof( wch_t ) * ( pgdata->chiSymbolBufLen - pgdata->chiSymbolCursor ) );
	/* "0" means Chinese word */
	pgdata->chiSymbolBuf[ pgdata->chiSymbolCursor ].wch = (wchar_t) 0;
	pgdata->chiSymbolBufLen++;
	pgdata->chiSymbolCursor++;

	return 0;
}

#ifdef ENABLE_DEBUG
static void ShowChewingData( ChewingData *pgdata )
{
	int i ;

	DEBUG_OUT(
		"nPhoneSeq : %d\n"
		"phoneSeq  : ", 
		pgdata->nPhoneSeq );
	for ( i = 0; i < pgdata->nPhoneSeq; i++ )
		DEBUG_OUT( "%hu ", pgdata->phoneSeq[ i ] );
	DEBUG_OUT(
		"[cursor : %d]\n"
		"nSelect : %d\n"
		"selectStr       selectInterval\n", 
		pgdata->cursor, 
		pgdata->nSelect );
	for ( i = 0; i < pgdata->nSelect; i++ ) {
		DEBUG_OUT(
			"  %14s%4d%4d\n",
			pgdata->selectStr[ i ], 
			pgdata->selectInterval[ i ].from,
			pgdata->selectInterval[ i ].to );
	}
	
	DEBUG_OUT( "bUserArrCnnct : " );
	for ( i = 0; i <= pgdata->nPhoneSeq; i++ )
		DEBUG_OUT( "%d ", pgdata->bUserArrCnnct[ i ] );
	DEBUG_OUT( "\n" );

	DEBUG_OUT( "bUserArrBrkpt : " );
	for ( i = 0; i <= pgdata->nPhoneSeq; i++ )
		DEBUG_OUT( "%d ", pgdata->bUserArrBrkpt[ i ] );
	DEBUG_OUT( "\n" );

	DEBUG_OUT( "bArrBrkpt     : " );
	for ( i = 0; i <= pgdata->nPhoneSeq; i++ )
		DEBUG_OUT( "%d ", pgdata->bArrBrkpt[ i ] );
	DEBUG_OUT( "\n" );

	DEBUG_OUT( 
		"bChiSym : %d , bSelect : %d , bCaseChange : %d\n",
		pgdata->bChiSym, 
		pgdata->bSelect, 
		pgdata->bCaseChange );
}
#endif

int CallPhrasing( ChewingData *pgdata )
{
	/* set "bSymbolArrBrkpt" && "bArrBrkpt" */
	int i, ch_count = 0;

	memcpy(
		pgdata->bArrBrkpt, 
		pgdata->bUserArrBrkpt, 
		(MAX_PHONE_SEQ_LEN + 1) * sizeof( int ) );
	memset(
		pgdata->bSymbolArrBrkpt, 0, 
		(MAX_PHONE_SEQ_LEN + 1) * sizeof( int ) );

	for ( i = 0; i < pgdata->chiSymbolBufLen; i++ ) {
		if ( ChewingIsChiAt( i, pgdata ) )
			ch_count++;
		else {
			pgdata->bArrBrkpt[ ch_count ] = 1;
			pgdata->bSymbolArrBrkpt[ ch_count ] = 1;
		}
	}

	/* kill select interval */
	for ( i = 0; i < pgdata->nPhoneSeq; i++ ) {
		if ( pgdata->bArrBrkpt[ i ] ) {
			ChewingKillSelectIntervalAcross( i, pgdata );
		}
	}

#ifdef ENABLE_DEBUG
	ShowChewingData(pgdata);
#endif

	/* then phrasing */
	Phrasing( 
		&( pgdata->phrOut ), pgdata->phoneSeq, pgdata->nPhoneSeq,
		pgdata->selectStr, pgdata->selectInterval, pgdata->nSelect, 
		pgdata->bArrBrkpt, pgdata->bUserArrCnnct );

	/* and then make prefer interval */
	MakePreferInterval( pgdata );

	return 0;
}


static void Union( int set1,int set2, int parent[] )
{
	if ( set1 != set2 )
		parent[ max( set1, set2 ) ] = min( set1, set2 );
}

static int SameSet( int set1,int set2, int parent[] )
{
	while ( parent[ set1 ] != 0 ) {
		set1 = parent[ set1 ];
	}
	while ( parent[ set2 ] != 0 ) {
		set2 = parent[ set2 ];
	}
	return ( set1 == set2 );
}

/* make prefer interval from phrOut->dispInterval */
static void MakePreferInterval( ChewingData *pgdata )
{
	int i, j, set_no;
	int belong_set[ MAX_PHONE_SEQ_LEN + 1 ];
	int parent[ MAX_PHONE_SEQ_LEN + 1 ];

	memset( belong_set, 0 , sizeof( int ) * ( MAX_PHONE_SEQ_LEN + 1 ) );
	memset( parent, 0, sizeof( int ) * ( MAX_PHONE_SEQ_LEN + 1 ) );

	/* for each interval */
	for ( i = 0; i < pgdata->phrOut.nDispInterval; i++ ) {
		for (
			j = pgdata->phrOut.dispInterval[ i ].from;
			j < pgdata->phrOut.dispInterval[ i ].to;
			j++ ) {
			belong_set[ j ] = i + 1;
		}
	}
	set_no = i;
	for ( i = 0; i < pgdata->nPhoneSeq; i++ )
		if ( belong_set[i] == 0 ) 
			belong_set[ i ] = set_no++;

	/* for each connect point */
	for ( i = 1; i < pgdata->nPhoneSeq; i++ ) {
		if ( pgdata->bUserArrCnnct[ i ] ) {
			Union( belong_set[ i - 1 ], belong_set[ i ], parent );
		}
	}

	/* generate new intervals */
	pgdata->nPrefer = 0;
	i = 0;
	while ( i < pgdata->nPhoneSeq ) {
		for ( j = i + 1; j < pgdata->nPhoneSeq; j++ )
			if ( ! SameSet( belong_set[ i ], belong_set[ j ], parent ) )
				break;

		pgdata->preferInterval[ pgdata->nPrefer ].from = i;
		pgdata->preferInterval[ pgdata->nPrefer ].to = j;
		pgdata->nPrefer++;
		i = j;
	}
}

/* for MakeOutput */
static void ShiftInterval( ChewingOutput *pgo, ChewingData *pgdata )
{
	int i, arrPos[ MAX_PHONE_SEQ_LEN ], k = 0, from, len;

	for ( i = 0; i < pgdata->chiSymbolBufLen; i++ ) {
		if ( ChewingIsChiAt( i, pgdata ) ) {
			arrPos[ k++ ] = i;
		}
	}
	arrPos[ k ] = i;

	pgo->nDispInterval = pgdata->nPrefer;
	for ( i = 0; i < pgdata->nPrefer; i++ ) {
		from = pgdata->preferInterval[ i ].from;
		len = pgdata->preferInterval[ i ].to - from;
		pgo->dispInterval[ i ].from = arrPos[ from ];
		pgo->dispInterval[ i ].to = arrPos[ from ] + len;
	}
}

static int MakeOutput( ChewingOutput *pgo, ChewingData *pgdata )
{
	int chi_i, chiSymbol_i, i ;

	/* fill zero to chiSymbolBuf first */
	memset( pgo->chiSymbolBuf, 0, sizeof( wch_t ) * MAX_PHONE_SEQ_LEN );

	/* fill chiSymbolBuf */
	for ( 
		chi_i = chiSymbol_i = 0; 
		chiSymbol_i < pgdata->chiSymbolBufLen; 
		chiSymbol_i ++ ) {
		if ( pgdata->chiSymbolBuf[ chiSymbol_i ].wch == (wchar_t) 0 ) { 
			/* is Chinese, then copy from the PhrasingOutput "phrOut" */
			pgo->chiSymbolBuf[ chiSymbol_i ].wch = (wchar_t) 0;
			ueStrNCpy( (char *) pgo->chiSymbolBuf[ chiSymbol_i ].s,
			           &( pgdata->phrOut.chiBuf[ chi_i ] ),
			           1, 1 );
			chi_i += ueBytesFromChar( pgo->chiSymbolBuf[ chiSymbol_i ].s[0] );
		}
		else {
			/* is Symbol */
			pgo->chiSymbolBuf[ chiSymbol_i ] = pgdata->chiSymbolBuf[ chiSymbol_i ];
		}
	}

	/* fill point */
	pgo->PointStart = pgdata->PointStart;
	pgo->PointEnd = pgdata->PointEnd;

	/* fill other fields */
	pgo->chiSymbolBufLen = pgdata->chiSymbolBufLen;
	pgo->chiSymbolCursor = pgdata->chiSymbolCursor;
	
	/* fill zuinBuf */
        if ( pgdata->zuinData.kbtype >= KB_HANYU_PINYIN ) {
		char *p = pgdata->zuinData.pinYinData.keySeq;
		/* 
		 * Copy from old content in zuinBuf
		 * NOTE: No Unicode transformation here.
		 */
		for ( i = 0; i< ZUIN_SIZE; i++) {
			int j;
			for ( j = 0; j < 2; j++ ) {
				if ( p[ 0 ] ) {
					pgo->zuinBuf[ i ].s[ j ] = p[ 0 ];
					p++;
				} 
				else {
					pgo->zuinBuf[ i ].s[ j ] = '\0';
				}
			}
			pgo->zuinBuf[ i ].s[ 2 ] = '\0';
		}
	} else {
		for ( i = 0; i < ZUIN_SIZE; i++ ) { 
			if ( pgdata->zuinData.pho_inx[ i ] != 0 ) {
				/* Here we should use (zhuin_tab[i] + 2) to
				 * skip the 2 space characters at 
				 * zhuin_tab[0] and zhuin_tab[1]. */
				ueStrNCpy( (char *) pgo->zuinBuf[ i ].s,
				           ueStrSeek( (char *) (zhuin_tab[ i ] + 2),
						      pgdata->zuinData.pho_inx[ i ] - 1 ),
				           1, 1);
			}
			else
				pgo->zuinBuf[ i ].wch = (wchar_t) 0;
		}
        }

	ShiftInterval( pgo, pgdata );
	memcpy( 
		pgo->dispBrkpt, pgdata->bUserArrBrkpt, 
		sizeof( pgo->dispBrkpt[ 0 ] ) * ( MAX_PHONE_SEQ_LEN + 1 ) );
	pgo->pci = &( pgdata->choiceInfo );
	pgo->bChiSym = pgdata->bChiSym;
	memcpy( pgo->selKey, pgdata->config.selKey, sizeof( pgdata->config.selKey ) );
	pgo->bShowMsg = 0;
	return 0;
}

int MakeOutputWithRtn( ChewingOutput *pgo, ChewingData *pgdata, int keystrokeRtn )
{
	pgo->keystrokeRtn = keystrokeRtn;
	return MakeOutput( pgo, pgdata );
}

void MakeOutputAddMsgAndCleanInterval( ChewingOutput *pgo, ChewingData *pgdata )
{
	pgo->bShowMsg = 1;
	memcpy( pgo->showMsg, pgdata->showMsg, sizeof( wch_t ) * ( pgdata->showMsgLen ) );
	pgo->showMsgLen = pgdata->showMsgLen;
	pgo->nDispInterval = 0;
}

int AddSelect( ChewingData *pgdata, int sel_i )
{
	int length, nSelect;

	/* save the typing time */
	length = pgdata->availInfo.avail[ pgdata->availInfo.currentAvail ].len;
	nSelect = pgdata->nSelect;

	/* change "selectStr" , "selectInterval" , and "nSelect" of ChewingData */
	ueStrNCpy( pgdata->selectStr[ nSelect ],
			pgdata->choiceInfo.totalChoiceStr[ sel_i ],
			length, 1 );
	pgdata->selectInterval[ nSelect ].from = pgdata->cursor;
	pgdata->selectInterval[ nSelect ].to = pgdata->cursor + length;
	pgdata->nSelect++;
	return 0;
}

int CountSelKeyNum( int key, ChewingData *pgdata )
	/* return value starts from 0.  If less than zero : error key */
{
	int i;

	for ( i = 0; i < MAX_SELKEY; i++ )
		if ( pgdata->config.selKey[ i ] == key )
			return i;
	return -1;
}

int ChewingIsChiAt( int chiSymbolCursor, ChewingData *pgdata )
{
	/* wch == 0 means Chinese */
	return (
		( chiSymbolCursor < pgdata->chiSymbolBufLen ) &&
		( 0 <= chiSymbolCursor ) &&
		(pgdata->chiSymbolBuf[ chiSymbolCursor ].wch == (wchar_t) 0 ) );
}

void RemoveSelectElement( int i, ChewingData *pgdata )
{
	if ( --pgdata->nSelect == i )
		return;
	pgdata->selectInterval[ i ] = pgdata->selectInterval[ pgdata->nSelect ];
	strcpy( pgdata->selectStr[ i ], pgdata->selectStr[ pgdata->nSelect ] );
}

static int ChewingKillSelectIntervalAcross( int cursor, ChewingData *pgdata )
{
	int i;
	for ( i = 0; i < pgdata->nSelect; i++ ) {
		if ( pgdata->selectInterval[ i ].from < cursor && 
			pgdata->selectInterval[ i ].to > cursor ) {
			RemoveSelectElement( i, pgdata );
			i--;
		}
	}
	return 0;
}

static int KillCharInSelectIntervalAndBrkpt( ChewingData *pgdata, int cursorToKill )
{
	int i; 

	for ( i = 0; i < pgdata->nSelect; i++ ) { 
		if ( pgdata->selectInterval[ i ].from <= cursorToKill && 
			pgdata->selectInterval[ i ].to > cursorToKill ) { 
			RemoveSelectElement( i, pgdata ); 
			i--;      /* the last one was swap to i, we need to recheck i */ 
		} 
		else if( pgdata->selectInterval[ i ].from > cursorToKill ) { 
			pgdata->selectInterval[ i ].from--; 
			pgdata->selectInterval[ i ].to--; 
		} 
	} 
	memmove( 
		&( pgdata->bUserArrBrkpt[ cursorToKill ] ),
		&( pgdata->bUserArrBrkpt[ cursorToKill + 1 ] ),
		sizeof( int ) * ( pgdata->nPhoneSeq - cursorToKill ) );
	memmove( 
		&( pgdata->bUserArrCnnct[ cursorToKill ] ),
		&( pgdata->bUserArrCnnct[ cursorToKill + 1 ] ),
		sizeof( int ) * ( pgdata->nPhoneSeq - cursorToKill ) );

	return 0;
}

int ChewingKillChar(
		ChewingData *pgdata, 
		int cursorToKill, 
		int chiSymbolCursorToKill, 
		int minus )
{
	if ( ChewingIsChiAt( chiSymbolCursorToKill, pgdata ) ) {
		KillCharInSelectIntervalAndBrkpt(pgdata, cursorToKill);
		memmove(
			&( pgdata->phoneSeq[ cursorToKill ] ), 
			&(pgdata->phoneSeq[ cursorToKill + 1 ] ),
			(pgdata->nPhoneSeq - cursorToKill - 1) * sizeof( uint16 ) );
		pgdata->nPhoneSeq--;
		pgdata->cursor -= minus;
		if (pgdata->cursor < 0)
			pgdata->cursor = 0;
	}
	pgdata->symbolKeyBuf[ chiSymbolCursorToKill ] = 0;
	memmove( 
		& pgdata->chiSymbolBuf[ chiSymbolCursorToKill ],
		& pgdata->chiSymbolBuf[ chiSymbolCursorToKill + 1 ], 
		(pgdata->chiSymbolBufLen - chiSymbolCursorToKill) * sizeof( wch_t ) );
	pgdata->chiSymbolBufLen--;
	pgdata->chiSymbolCursor -= minus;
	if (pgdata->chiSymbolCursor < 0)
		pgdata->chiSymbolCursor = 0;
	return 0;
}

int IsPreferIntervalConnted( int cursor, ChewingData *pgdata )
{
	int i;

	for ( i = 0; i < pgdata->nPrefer; i++ ) {
		if ( 
			pgdata->preferInterval[ i ].from < cursor &&
			pgdata->preferInterval[ i ].to > cursor ) 
			return 1;
	}
	return 0;
}

static char *symbol_buf[][ 50 ] = {
	{ "\x30", "\xC3\xB8", 0 },
	{ "\x5B", "\xE3\x80\x8C", "\xE3\x80\x8E", "\xE3\x80\x8A", "\xE3\x80\x88",
		  "\xE3\x80\x90", "\xE3\x80\x94", 0 },
	{ "\x5D", "\xE3\x80\x8D", "\xE3\x80\x8F", "\xE3\x80\x8B", "\xE3\x80\x89",
		  "\xE3\x80\x91", "\xE3\x80\x95", 0 },
	{ "\x7B", "\xEF\xBD\x9B", 0 },
	{ "\x7D", "\xEF\xBD\x9D", 0 },
	{ "\x3C", "\xEF\xBC\x8C", "\xE2\x86\x90", 0 },
	{ "\x3E", "\xE3\x80\x82", "\xE2\x86\x92", "\xEF\xBC\x8E", 0 },
	{ "\x3F", "\xEF\xBC\x9F", "\xC2\xBF", 0 },
	{ "\x21", "\xEF\xBC\x81", "\xE2\x85\xA0","\xC2\xA1", 0 },
	{ "\x40", "\xEF\xBC\xA0", "\xE2\x85\xA1", "\xE2\x8A\x95", "\xE2\x8A\x99",
		  "\xE3\x8A\xA3", "\xEF\xB9\xAB", 0 },
	{ "\x23", "\xEF\xBC\x83", "\xE2\x85\xA2", "\xEF\xB9\x9F", 0 },
	{ "\x24", "\xEF\xBC\x84", "\xE2\x85\xA3", "\xE2\x82\xAC", "\xEF\xB9\xA9",
		  "\xEF\xBF\xA0", "\xE2\x88\xAE","\xEF\xBF\xA1", "\xEF\xBF\xA5", 0 },
	{ "\x25", "\xEF\xBC\x85", "\xE2\x85\xA4", 0 },
	{ "\x5E", "\xEF\xB8\xBF", "\xE2\x85\xA5", "\xEF\xB9\x80", "\xEF\xB8\xBD",
		  "\xEF\xB8\xBE", 0 },
	{ "\x26", "\xEF\xBC\x86", "\xE2\x85\xA6", "\xEF\xB9\xA0", 0 },
	{ "\x2A", "\xEF\xBC\x8A", "\xE2\x85\xA7", "\xC3\x97", "\xE2\x80\xBB",
		  "\xE2\x95\xB3", "\xEF\xB9\xA1", "\xE2\x98\xAF", "\xE2\x98\x86",
		  "\xE2\x98\x85", 0 },
	{ "\x28", "\xEF\xBC\x88", "\xE2\x85\xA8", 0 },
	{ "\x29", "\xEF\xBC\x89", "\xE2\x85\xA9", 0 },
	{ "\x5F", "\xEF\xBC\xBF", "\xE2\x80\xA6", "\xE2\x80\xA5", "\xE2\x86\x90",
		  "\xE2\x86\x92", "\xEF\xB9\x8D", "\xEF\xB9\x89", "\xCB\x8D",
		  "\xEF\xBF\xA3", "\xE2\x80\x93", "\xE2\x80\x94", "\xC2\xAF",
		  "\xEF\xB9\x8A", "\xEF\xB9\x8E", "\xEF\xB9\x8F", "\xEF\xB9\xA3",
		  "\xEF\xBC\x8D", 0 },
	{ "\x2B", "\xEF\xBC\x8B", "\xC2\xB1", "\xEF\xB9\xA2", 0 },
	{ "\x3D", "\xEF\xBC\x9D", "\xE2\x89\x92", "\xE2\x89\xA0", "\xE2\x89\xA1",
		  "\xE2\x89\xA6", "\xE2\x89\xA7", "\xEF\xB9\xA6", 0 },
	{ "\x60", "\xE3\x80\x8F", "\xE3\x80\x8E", "\xE2\x80\xB2", "\xE2\x80\xB5", 0 },
	{ "\x7E", "\xEF\xBD\x9E", 0 },
	{ "\x3A", "\xEF\xBC\x9A", "\xEF\xBC\x9B", "\xEF\xB8\xB0", "\xEF\xB9\x95", 0 },
	{ "\"", "\xEF\xBC\x9B", 0 },
	{ "\'", "\xE3\x80\x81", "\xE2\x80\xA6", "\xE2\x80\xA5", 0 },
	{ "\\", "\xEF\xBC\xBC", "\xE2\x86\x96", "\xE2\x86\x98", "\xEF\xB9\xA8", 0 },
	{ "\x2D", "\xEF\xBC\x8D", "\xEF\xBC\xBF", "\xEF\xBF\xA3", "\xC2\xAF",
		  "\xCB\x8D", "\xE2\x80\x93", "\xE2\x80\x94", "\xE2\x80\xA5",
		  "\xE2\x80\xA6", "\xE2\x86\x90", "\xE2\x86\x92", "\xE2\x95\xB4",
		  "\xEF\xB9\x89", "\xEF\xB9\x8A", "\xEF\xB9\x8D", "\xEF\xB9\x8E",
		  "\xEF\xB9\x8F", "\xEF\xB9\xA3", 0 },
	{ "\x2F", "\xEF\xBC\x8F", "\xC3\xB7", "\xE2\x86\x97", "\xE2\x86\x99",
		  "\xE2\x88\x95", 0 },
	{ "\x7C", "\xE2\x86\x91", "\xE2\x86\x93", "\xE2\x88\xA3", "\xE2\x88\xA5",
		  "\xEF\xB8\xB1", "\xEF\xB8\xB3", "\xEF\xB8\xB4" ,0 },
	{ "\x41", "\xC3\x85","\xCE\x91", "\xCE\xB1", "\xE2\x94\x9C", "\xE2\x95\xA0",
		  "\xE2\x95\x9F", "\xE2\x95\x9E", 0 },
	{ "\x42", "\xCE\x92", "\xCE\xB2","\xE2\x88\xB5", 0 },
	{ "\x43", "\xCE\xA7", "\xCF\x87", "\xE2\x94\x98", "\xE2\x95\xAF",
		  "\xE2\x95\x9D", "\xE2\x95\x9C", "\xE2\x95\x9B", "\xE3\x8F\x84",
		  "\xE2\x84\x83", "\xE3\x8E\x9D", "\xE2\x99\xA3", "\xC2\xA9", 0 },
	{ "\x44", "\xCE\x94", "\xCE\xB4", "\xE2\x97\x87", "\xE2\x97\x86",
		  "\xE2\x94\xA4", "\xE2\x95\xA3", "\xE2\x95\xA2", "\xE2\x95\xA1",
		  "\xE2\x99\xA6", 0 },
	{ "\x45", "\xCE\x95", "\xCE\xB5", "\xE2\x94\x90", "\xE2\x95\xAE",
		  "\xE2\x95\x97", "\xE2\x95\x93", "\xE2\x95\x95", 0 },
	{ "\x46", "\xCE\xA6", "\xCF\x88", "\xE2\x94\x82", "\xE2\x95\x91",
		  "\xE2\x99\x80", 0 },
	{ "\x47", "\xCE\x93", "\xCE\xB3", 0 },
	{ "\x48", "\xCE\x97", "\xCE\xB7","\xE2\x99\xA5", 0 },
	{ "\x49", "\xCE\x99", "\xCE\xB9", 0 },
	{ "\x4A", "\xCF\x86", 0 },
	{ "\x4B", "\xCE\x9A", "\xCE\xBA","\xE3\x8E\x9E", "\xE3\x8F\x8E", 0 },
	{ "\x4C", "\xCE\x9B", "\xCE\xBB","\xE3\x8F\x92", "\xE3\x8F\x91", 0 },
	{ "\x4D", "\xCE\x9C", "\xCE\xBC", "\xE2\x99\x82", "\xE2\x84\x93",
		  "\xE3\x8E\x8E", "\xE3\x8F\x95", "\xE3\x8E\x9C","\xE3\x8E\xA1", 0 },
	{ "\x4E", "\xCE\x9D", "\xCE\xBD","\xE2\x84\x96", 0 },
	{ "\x4F", "\xCE\x9F", "\xCE\xBF", 0 },
	{ "\x50", "\xCE\xA0", "\xCF\x80", 0 },
	{ "\x51", "\xCE\x98", "\xCE\xB8","\xD0\x94","\xE2\x94\x8C", "\xE2\x95\xAD",
		  "\xE2\x95\x94", "\xE2\x95\x93", "\xE2\x95\x92", 0 },
	{ "\x52", "\xCE\xA1", "\xCF\x81", "\xE2\x94\x80", "\xE2\x95\x90" ,"\xC2\xAE" , 0 },
	{ "\x53", "\xCE\xA3", "\xCF\x83", "\xE2\x88\xB4", "\xE2\x96\xA1",
		  "\xE2\x96\xA0", "\xE2\x94\xBC", "\xE2\x95\xAC", "\xE2\x95\xAA",
		  "\xE2\x95\xAB", "\xE2\x88\xAB", "\xC2\xA7", "\xE2\x99\xA0", 0 },
	{ "\x54", "\xCE\xA4", "\xCF\x84", "\xCE\xB8", "\xE2\x96\xB3", "\xE2\x96\xB2",
		  "\xE2\x96\xBD", "\xE2\x96\xBC", "\xE2\x84\xA2", "\xE2\x8A\xBF",
		  "\xE2\x84\xA2", 0 },
	{ "\x55", "\xCE\xA5", "\xCF\x85","\xCE\xBC","\xE2\x88\xAA", "\xE2\x88\xA9", 0 },
	{ "\x56",  0 },
	{ "\x57", "\xE2\x84\xA6", "\xCF\x89", "\xE2\x94\xAC", "\xE2\x95\xA6",
		  "\xE2\x95\xA4", "\xE2\x95\xA5", 0 },
	{ "\x58", "\xCE\x9E", "\xCE\xBE", "\xE2\x94\xB4", "\xE2\x95\xA9",
		  "\xE2\x95\xA7", "\xE2\x95\xA8", 0 },
	{ "\x59", "\xCE\xA8", 0 },
	{ "\x5A", "\xCE\x96", "\xCE\xB6", "\xE2\x94\x94", "\xE2\x95\xB0",
		  "\xE2\x95\x9A", "\xE2\x95\x99", "\xE2\x95\x98", 0 },
};

static int FindSymbolKey( const char *symbol )
{
	unsigned int i;
	char **buf;
	for ( i = 0; i < sizeof( symbol_buf ) / sizeof( symbol_buf[ 0 ] ); ++i ) {
		for ( buf = symbol_buf[ i ]; *buf; ++buf )	{
			if (  0 == strcmp( *buf, symbol ) )
				return *symbol_buf[ i ][ 0 ];
		}
	}
	return 0;
}

int OpenSymbolChoice( ChewingData *pgdata )
{
	int i, symbol_buf_len = sizeof( symbol_buf ) / sizeof( symbol_buf[ 0 ] );
	char **pBuf;
	ChoiceInfo *pci = &( pgdata->choiceInfo );
	pci->oldCursor = pgdata->cursor;
	pci->oldChiSymbolCursor = pgdata->chiSymbolCursor;

	/* see if there is some word in the cursor position */
	if ( pgdata->chiSymbolCursor == pgdata->chiSymbolBufLen )
		pgdata->chiSymbolCursor--;
	if ( pgdata->symbolKeyBuf[ pgdata->chiSymbolCursor ] == '1' ) {
		pgdata->bSelect = 1;
		HaninSymbolInput( pci, &( pgdata->availInfo ),
				pgdata->phoneSeq, pgdata->config.candPerPage );
		return 0;
	}
	for ( i = 0; i < symbol_buf_len; i++ ) {
		if ( symbol_buf[ i ][ 0 ][ 0 ] == 
				pgdata->symbolKeyBuf[ pgdata->chiSymbolCursor ] ) {
			pBuf = symbol_buf[i];
			break;
		}
	}
	if ( i == symbol_buf_len ) {
		ChoiceEndChoice( pgdata );
		return 0;
	}
	pci->nTotalChoice = 0;
	for ( i = 1; pBuf[ i ]; i++ ) {
		ueStrNCpy( pci->totalChoiceStr[ pci->nTotalChoice ], 
				pBuf[ i ], ueStrLen( pBuf[i] ), 1 );
		pci->nTotalChoice++; 
	}

	pci->nChoicePerPage = pgdata->config.candPerPage;
	if ( pci->nChoicePerPage > MAX_SELKEY )
		pci->nChoicePerPage = MAX_SELKEY;
	pci->nPage = CEIL_DIV( pci->nTotalChoice, pci->nChoicePerPage );
	pci->pageNo = 0;
	pci->isSymbol = 3;

	pgdata->bSelect = 1;
	pgdata->availInfo.nAvail = 1;
	pgdata->availInfo.currentAvail = 0;
	pgdata->availInfo.avail[ 0 ].id = -1;
	pgdata->availInfo.avail[ 0 ].len = 1;     
	return 0;
}

static void TerminateSymbolTable();
static void TerminateEasySymbolTable();

int InitSymbolTable( const char *prefix )
{
	const char DIRPATH_SEP_FILENAME[] = "%s" PLAT_SEPARATOR "%s";
	FILE *file;
	char filename[ PATH_MAX ];
	char line[512];
	char *category;
	char *symbols, *symbol;
	SymbolEntry* tmp_tab[ 100 ];
	int len = 0, i;

	n_symbol_entry = 0;
	symbol_table = NULL;

	sprintf( filename, DIRPATH_SEP_FILENAME, prefix, SYMBOL_TABLE_FILE );
	file = fopen( filename, "r" );

	if ( ! file )
		return 0;

	while( fgets( line, ( sizeof( line ) / sizeof( char ) ), file ) ) {
		if ( n_symbol_entry >= (sizeof(tmp_tab) / sizeof(SymbolEntry *)) )
			break;
		category = strtok( line, "=\r\n" );
		if ( category ) {
			symbols = strtok( NULL, "\r\n" );
			if ( symbols ) {
				len = ueStrLen( symbols );
				tmp_tab[ n_symbol_entry ] = ALC(
						SymbolEntry,
						sizeof( SymbolEntry ) +
						(len - 1) * (MAX_UTF8_SIZE + 1) );
				tmp_tab[ n_symbol_entry ]->nSymbols = len;
				symbol = symbols;
				for( i = 0; i < len; ++i ) {
					ueStrNCpy(
						tmp_tab[ n_symbol_entry ]->symbols[ i ], 
						symbol, 1, 1 );
					symbol += ueBytesFromChar( symbol[ 0 ] );
				}
			}
			else {
				tmp_tab[ n_symbol_entry ] = 
					(SymbolEntry *) calloc( 1, 
						sizeof( SymbolEntry ) - ( MAX_UTF8_SIZE + 1 ) );
				tmp_tab[ n_symbol_entry ]->nSymbols = 0;
			}
			ueStrNCpy(
				tmp_tab[ n_symbol_entry ]->category, 
				category, 
				MAX_PHRASE_LEN, 1 );
			++n_symbol_entry;
		}
	}
	symbol_table = (SymbolEntry **) calloc( n_symbol_entry, sizeof( SymbolEntry * ) );
	memcpy( symbol_table, tmp_tab, n_symbol_entry * sizeof( SymbolEntry *) );
	fclose( file );
	addTerminateService( TerminateSymbolTable );
	addTerminateService( TerminateEasySymbolTable );
	return 1;
}

static void TerminateSymbolTable()
{
	unsigned int i;
	if ( symbol_table ) {
		for ( i = 0; i < n_symbol_entry; ++i )
			free( symbol_table[ i ] );
		free( symbol_table );
		n_symbol_entry = 0;
		symbol_table = NULL;
	}
}

int InitEasySymbolInput( const char *prefix )
{
	const char DIRPATH_SEP_FILENAME[] = "%s" PLAT_SEPARATOR "%s";
	FILE *file;
	char filename[ PATH_MAX ];
	char line[ 512 ];
	char *symbol;
	int len = 0, index;

	sprintf( filename, DIRPATH_SEP_FILENAME, prefix, SOFTKBD_TABLE_FILE );
	file = fopen( filename, "r" );

	if ( ! file )
		return 0;

	line[ 0 ] = '\0';
	while ( fgets( line, sizeof( line ) / sizeof( char ), file ) ) {
		if ( '\0' == line[ 0 ] ) {
			break;
		}

		line[ sizeof( line ) / sizeof( char ) - 1] = '\0';
		if ( ' ' != line[ 1 ] ) {
			continue;
		}

		len = strcspn( line, "\r\n\0" );
		line[ len ] = '\0';

		line[ 0 ] = toupper( line[ 0 ] );
		index = FindEasySymbolIndex( line[ 0 ] );
		if ( -1 == index ) {
			continue;
		}

		len = ueStrLen( &line[ 2 ] );
		if ( 0 == len || 10 <= len ) {
			continue;
		}

		symbol = ALC( char, 6 * 10 );
		if ( NULL == symbol ) {
			break;
		}
		ueStrNCpy( symbol, &line[ 2 ], 9, 1 );

		if ( NULL != g_easy_symbol_value[ index] ) {
			free( g_easy_symbol_value[ index ] );
		}
		g_easy_symbol_value[ index ] = symbol;
		g_easy_symbol_num[ index ] = len;
	}
	fclose( file );
	return 1;
}

static void TerminateEasySymbolTable()
{
	unsigned int i;
	for ( i = 0; i < EASY_SYMBOL_KEY_TAB_LEN / sizeof( char ); ++i ) {
		if ( NULL != g_easy_symbol_value[ i ] ) {
			free( g_easy_symbol_value[ i ] );
			g_easy_symbol_value[ i ] = NULL;
		}
		g_easy_symbol_num[ i ] = 0;
	}
}

