// Macros from MFC not yet expunged from source.
/*
#include "mfc_emulation.h"
*/

#if !defined( _MFC_EMULATION_INCLUDED_ ) && !defined( _MFC_VER )
#define _MFC_EMULATION_INCLUDED_

#if /*!defined( _DEBUG) ||*/ !defined( _MSC_VER )
// Reporting macros
#define _CRT_WARN 0
#ifdef _DEBUG

#define _RPT0(p0,p1) fprintf(stderr, p1);
#define _RPT1(p0,p1,p2) fprintf(stderr, p1, p2);
#define _RPT2(p0,p1,p2,p3) fprintf(stderr, p1, p2, p3);
#define _RPT3(p0,p1,p2,p3,p4) fprintf(stderr, p1, p2, p3, p4);
#define _RPT4(p0,p1,p2,p3,p4,p5) fprintf(stderr, p1, p2, p3, p4, p5);

// va args don't seem to work on mac
//#define _RPT_BASE(...) () (fprintf(stderr, __VA_ARGS__))
//#define _RPTN(rptno, ...) _RPT_BASE(rptno, __VA_ARGS__)
#define _RPTN(rptno, msg, ...)
#else

#define _RPT0(p0,p1)
#define _RPT1(p0,p1,p2)
#define _RPT2(p0,p1,p2,p3)
#define _RPT3(p0,p1,p2,p3,p4)
#define _RPT4(p0,p1,p2,p3,p4,p5)
#define _RPTN(rptno, msg, ...)

#endif

#define _RPTW0(p0,p1)
#define _RPTW1(p0,p1,p2)
#define _RPTW2(p0,p1,p2,p3)
#define _RPTW3(p0,p1,p2,p3,p4)
#define _RPTW4(p0,p1,p2,p3,p4,p5)
#else
#include <crtdbg.h>
#endif

#endif
