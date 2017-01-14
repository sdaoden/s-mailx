/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Header inclusion, macros, constants, types and the global var declarations.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 */
/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Mail -- a mail program
 *
 * Author: Kurt Shoens (UCB) March 25, 1978
 */

#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_GETTIMEOFDAY
# include <sys/time.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#if defined __STDC_VERSION__ && __STDC_VERSION__ + 0 >= 199901L
# include <stdint.h>
#else
# include <inttypes.h>
#endif

#ifdef HAVE_C90AMEND1
# include <wchar.h>
# include <wctype.h>
#endif
#ifdef HAVE_DEBUG
# include <assert.h>
#endif
#ifdef HAVE_ICONV
# include <iconv.h>
#endif
#ifdef HAVE_REGEX
# include <regex.h>
#endif

#ifdef HAVE_XSSL_MD5
# include <openssl/md5.h>
#endif

/*
 * Constants, some nail-specific macros
 */

#if !defined NI_MAXHOST
# define NI_MAXHOST     1025
#endif

/* TODO PATH_MAX: fixed-size buffer is always wrong (think NFS) */
#ifndef PATH_MAX
# ifdef MAXPATHLEN
#  define PATH_MAX      MAXPATHLEN
# else
#  define PATH_MAX      1024        /* _XOPEN_PATH_MAX POSIX 2008/Cor 1-2013 */
# endif
#endif

#ifndef HOST_NAME_MAX
# ifdef _POSIX_HOST_NAME_MAX
#  define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
# else
#  define HOST_NAME_MAX 255
# endif
#endif

#ifndef NAME_MAX
# ifdef _POSIX_NAME_MAX
#  define NAME_MAX      _POSIX_NAME_MAX
# else
#  define NAME_MAX      14
# endif
#endif
#if NAME_MAX < 8
# error NAME_MAX too small
#endif

#ifndef STDIN_FILENO
# define STDIN_FILENO   0
#endif
#ifndef STDOUT_FILENO
# define STDOUT_FILENO  1
#endif
#ifndef STDERR_FILENO
# define STDERR_FILENO  2
#endif

#ifdef NSIG_MAX
# undef NSIG
# define NSIG           NSIG_MAX
#elif !defined NSIG
# define NSIG           ((sizeof(sigset_t) * 8) - 1)
#endif

#ifdef O_CLOEXEC
# define _O_CLOEXEC        O_CLOEXEC
# define _CLOEXEC_SET(FD)  do {;} while(0)
#else
# define _O_CLOEXEC        0
# define _CLOEXEC_SET(FD)  \
   do { (void)fcntl((FD), F_SETFD, FD_CLOEXEC); } while (0)
#endif

#ifdef O_NOFOLLOW
# define n_O_NOFOLLOW      O_NOFOLLOW
#else
# define n_O_NOFOLLOW      0
#endif

/*  */

#if BUFSIZ + 0 > 2560               /* TODO simply use BUFSIZ? */
# define LINESIZE       BUFSIZ      /* max readable line width */
#else
# define LINESIZE       2560
#endif
#define BUFFER_SIZE     (BUFSIZ >= (1u << 13) ? BUFSIZ : (1u << 14))

/* Network protocol newline */
#define NETNL           "\015\012"
#define NETLINE(X)      X NETNL

/* Number of Not-Yet-Dead calls that are remembered */
#if defined HAVE_DEBUG || defined HAVE_DEVEL || defined HAVE_NYD2
# ifdef HAVE_NYD2
#  define NYD_CALLS_MAX (25 * 84)
# elif defined HAVE_DEVEL
#  define NYD_CALLS_MAX (25 * 42)
# else
#  define NYD_CALLS_MAX (25 * 10)
# endif
#endif

#define APPEND                   /* New mail goes to end of mailbox */
#define CBAD            (-15555)
#define DOTLOCK_TRIES   5        /* Number of open(2) calls for dotlock */
#define FILE_LOCK_TRIES 10       /* Maximum tries before n_file_lock() fails */
#define FILE_LOCK_MILLIS 200     /* If UIZ_MAX, fall back to that */
#define n_ERROR "ERROR"          /* Is-error?  Also as n_error[] */
#define ERRORS_MAX      1000     /* Maximum error ring entries TODO configable*/
#define n_ESCAPE        '~'      /* Default escape for sending */
#define HIST_SIZE       242      /* tty.c: history list default size */
#define HSHSIZE         23       /* Hash prime TODO make dynamic, obsolete */
#define MAXARGC         1024     /* Maximum list of raw strings */
#define MAXEXP          25       /* Maximum expansion of aliases */
#define REFERENCES_MAX  20       /* Maximum entries in References: */
#define n_UNIREPL "\xEF\xBF\xBD" /* 0xFFFD in UTF-8 */
#define FTMP_OPEN_TRIES 10       /* Maximum number of Ftmp() open(2) tries */

#define ACCOUNT_NULL    "null"   /* Name of "null" account */

/* Colour stuff */
#ifdef HAVE_COLOUR
# define n_COLOUR(X)       X
#else
# define n_COLOUR(X)
#endif

/* Special FD requests for run_command() / start_command() */
#define COMMAND_FD_PASS -1
#define COMMAND_FD_NULL -2

/*  */
#define FROM_DATEBUF    64    /* Size of RFC 4155 From_ line date */
#define DATE_DAYSYEAR   365L
#define DATE_SECSMIN    60L
#define DATE_MINSHOUR   60L
#define DATE_HOURSDAY   24L
#define DATE_SECSHOUR   (DATE_SECSMIN * DATE_MINSHOUR)
#define DATE_SECSDAY    (DATE_SECSHOUR * DATE_HOURSDAY)

/* *indentprefix* default as of POSIX */
#define INDENT_DEFAULT  "\t"

/* Auto-reclaimed memory storage: size of the buffers.  Maximum auto-reclaimed
 * storage is that value /2, which is n_CTA()ed to be > 1024 */
#define n_MEMORY_AUTOREC_SIZE 0x2000u
/* Ugly, but avoid dynamic allocation for the management structure! */
#define n_MEMORY_AUTOREC_TYPE_SIZEOF (7 * sizeof(void*))

/* Default *encoding* as enum mime_enc below */
#define MIME_DEFAULT_ENCODING MIMEE_B64

/* Maximum allowed line length in a mail before QP folding is necessary), and
 * the real limit we go for */
#define MIME_LINELEN_MAX   998   /* Plus CRLF */
#define MIME_LINELEN_LIMIT (MIME_LINELEN_MAX - 48)

/* Ditto, SHOULD */
#define MIME_LINELEN    78    /* Plus CRLF */

/* And in headers which contain an encoded word according to RFC 2047 there is
 * yet another limit; also RFC 2045: 6.7, (5). */
#define MIME_LINELEN_RFC2047 76

/* Locations of mime.types(5) */
#define MIME_TYPES_USR  "~/.mime.types"
#define MIME_TYPES_SYS  "/etc/mime.types"

/* Fallback MIME charsets, if *charset-7bit* and *charset-8bit* or not set.
 * Note: must be lowercase!  Changes affect enum okeys!
 * (Keep in SYNC: ./nail.1:"Character sets", ./nail.h:CHARSET_*!) */
#define CHARSET_7BIT "us-ascii"
#ifdef HAVE_ICONV
# define CHARSET_8BIT "utf-8"
# define CHARSET_8BIT_OKEY charset_8bit
#else
# define CHARSET_8BIT "iso-8859-1"
# define CHARSET_8BIT_OKEY ttycharset
#endif

/* Some environment variables for pipe hooks etc. */
#define n_PIPEENV_FILENAME "MAILX_FILENAME"
#define n_PIPEENV_FILENAME_GENERATED "MAILX_FILENAME_GENERATED"
#define n_PIPEENV_FILENAME_TEMPORARY "MAILX_FILENAME_TEMPORARY"
#define n_PIPEENV_CONTENT "MAILX_CONTENT"
#define n_PIPEENV_CONTENT_EVIDENCE "MAILX_CONTENT_EVIDENCE"

/* Is *W* a quoting (ASCII only) character? */
#define ISQUOTE(W) \
   ((W) == n_WC_C('>') || (W) == n_WC_C('|') ||\
    (W) == n_WC_C('}') || (W) == n_WC_C(':'))

/* Maximum number of quote characters (not bytes!) that'll be used on
 * follow lines when compressing leading quote characters */
#define QUOTE_MAX       42

/* How much spaces should a <tab> count when *quote-fold*ing? (power-of-two!) */
#define QUOTE_TAB_SPACES 8

/* Smells fishy after, or asks for shell expansion, dependent on context */
#define n_SHEXP_MAGIC_PATH_CHARS "|&;<>{}()[]*?$`'\"\\"

/* Maximum size of a message that is passed through to the spam system */
#define SPAM_MAXSIZE    420000

/* Switch indicating necessity of terminal access interface (termcap.c) */
#if defined HAVE_TERMCAP || defined HAVE_COLOUR || defined HAVE_MLE
# define n_HAVE_TCAP
#endif

/*
 * OS, CC support, generic macros etc.
 */

#define n_ISPOW2(X)     ((((X) - 1) & (X)) == 0)
#define n_MIN(A, B)     ((A) < (B) ? (A) : (B))
#define n_MAX(A, B)     ((A) < (B) ? (B) : (A))
#define n_ABS(A)        ((A) < 0 ? -(A) : (A))

/* OS: we're not a library, only set what needs special treatment somewhere */
#define n_OS_DRAGONFLY 0
#define n_OS_OPENBSD 0
#define n_OS_SOLARIS 0
#define n_OS_SUNOS 0

#ifdef __DragonFly__
# undef n_OS_DRAGONFLY
# define n_OS_DRAGONFLY 1
#elif defined __OpenBSD__
# undef n_OS_OPENBSD
# define n_OS_OPENBSD 1
#elif defined __solaris__ || defined __sun
# if defined __SVR4 || defined __svr4__
#  undef n_OS_SOLARIS
#  define n_OS_SOLARIS 1
# else
#  undef n_OS_SUNOS
#  define n_OS_SUNOS 1
# endif
#endif

/* CC */
#define CC_CLANG           0
#define PREREQ_CLANG(X,Y)  0
#define CC_GCC             0
#define PREREQ_GCC(X,Y)    0
#define CC_TCC             0
#define PREREQ_TCC(X,Y)    0

#ifdef __clang__
# undef CC_CLANG
# undef PREREQ_CLANG
# define CC_CLANG          1
# define PREREQ_CLANG(X,Y) \
   (__clang_major__ + 0 > (X) || \
    (__clang_major__ + 0 == (X) && __clang_minor__ + 0 >= (Y)))
# define __EXTEN           __extension__

#elif defined __GNUC__
# undef CC_GCC
# undef PREREQ_GCC
# define CC_GCC            1
# define PREREQ_GCC(X,Y)   \
   (__GNUC__ + 0 > (X) || (__GNUC__ + 0 == (X) && __GNUC_MINOR__ + 0 >= (Y)))
# define __EXTEN           __extension__

#elif defined __TINYC__
# undef CC_TCC
# define CC_TCC            1
#endif

#ifndef __EXTEN
# define __EXTEN
#endif

/* Suppress some technical warnings via #pragma's unless developing.
 * XXX Wild guesses: clang(1) 1.7 and (OpenBSD) gcc(1) 4.2.1 don't work */
#ifndef HAVE_DEVEL
# if PREREQ_CLANG(3, 4)
#  pragma clang diagnostic ignored "-Wassign-enum"
#  pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#  pragma clang diagnostic ignored "-Wformat"
#  pragma clang diagnostic ignored "-Wunused-result"
# elif PREREQ_GCC(4, 7)
#  pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#  pragma GCC diagnostic ignored "-Wunused-result"
#  pragma GCC diagnostic ignored "-Wformat"
# endif
#endif

/* For injection macros like DBG(), n_NATCH_CHAR() */
#define COMMA           ,

#define EMPTY_FILE()    typedef int n_CONCAT(avoid_empty_file__, n_FILE);

/* Pointer to size_t */
#define PTR2SIZE(X)     ((size_t)(uintptr_t)(X))

/* Pointer comparison (types from below) */
#define PTRCMP(A,C,B)   ((uintptr_t)(A) C (uintptr_t)(B))

/* Ditto, compare (maybe mixed-signed) integers cases to T bits, unsigned;
 * Note: doesn't sign-extend correctly, that's still up to the caller */
#define UICMP(T,A,C,B)  ((ui ## T ## _t)(A) C (ui ## T ## _t)(B))

/* Align something to a size/boundary that cannot cause just any problem */
#define n_ALIGN(X) (((X) + 2*sizeof(void*)) & ~((2*sizeof(void*)) - 1))
#define n_ALIGN_SMALL(X) \
   (((X) + n_MAX(sizeof(size_t), sizeof(void*))) &\
    ~(n_MAX(sizeof(size_t), sizeof(void*)) - 1))

/* Members in constant array */
#define n_NELEM(A) (sizeof(A) / sizeof((A)[0]))

/* sizeof() for member fields */
#define n_SIZEOF_FIELD(T,F) sizeof(((T *)NULL)->F)

/* Casts-away (*NOT* cast-away) */
#define n_UNUSED(X) ((void)(X))
#define n_UNCONST(P) ((void*)(uintptr_t)(void const*)(P))
#define n_UNVOLATILE(P) ((void*)(uintptr_t)(void volatile*)(P))
/* To avoid warnings with modern compilers for "char*i; *(si32_t*)i=;" */
#define n_UNALIGN(T,P) ((T)(uintptr_t)(P))
#define n_UNXXX(T,C,P) ((T)(uintptr_t)(C)(P))

/* __STDC_VERSION__ is ISO C99, so also use __STDC__, which should work */
#if defined __STDC__ || defined __STDC_VERSION__ /*|| defined __cplusplus*/
# define n_STRING(X) #X
# define n_XSTRING(X) n_STRING(X)
# define n_CONCAT(S1,S2) n__CONCAT_1(S1, S2)
# define n__CONCAT_1(S1,S2) S1 ## S2
#else
# define n_STRING(X) "X"
# define n_XSTRING STRING
# define n_CONCAT(S1,S2) S1/* won't work out */S2
#endif

#if defined __STDC_VERSION__ && __STDC_VERSION__ + 0 >= 199901L
# define n_FIELD_INITN(N) CONCAT(., N) =
# define n_FIELD_INITI(I) [I] =
#else
# define n_FIELD_INITN(N)
# define n_FIELD_INITI(N)
#endif

#if defined __STDC_VERSION__ && __STDC_VERSION__ + 0 >= 199901L
# define n_VFIELD_SIZE(X)
# define n_VSTRUCT_SIZEOF(T,F) sizeof(T)
#else
# define n_VFIELD_SIZE(X) \
  ((X) == 0 ? sizeof(size_t) \
   : ((ssize_t)(X) < 0 ? sizeof(size_t) - n_ABS(X) : (size_t)(X)))
# define n_VSTRUCT_SIZEOF(T,F) (sizeof(T) - n_SIZEOF_FIELD(T, F))
#endif

#ifdef HAVE_INLINE
# define SINLINE        n_INLINE /* TODO obsolete */
#else
# define SINLINE        static
#endif

#undef __FUN__
#if defined __STDC_VERSION__ && __STDC_VERSION__ + 0 >= 199901L
# define __FUN__        __func__
#elif CC_CLANG || PREREQ_GCC(3, 4)
# define __FUN__        __extension__ __FUNCTION__
#else
# define __FUN__        uagent /* Something that is not a literal */
#endif

#if defined __predict_true && defined __predict_false
# define n_LIKELY(X) __predict_true(X)
# define n_UNLIKELY(X) __predict_false(X)
#elif CC_CLANG || PREREQ_GCC(2, 96)
# define n_LIKELY(X) __builtin_expect(X, 1)
# define n_UNLIKELY(X) __builtin_expect(X, 0)
#else
# define n_LIKELY(X) (X)
# define n_UNLIKELY(X) (X)
#endif

#undef HAVE_NATCH_CHAR
#if defined HAVE_SETLOCALE && defined HAVE_C90AMEND1 && defined HAVE_WCWIDTH
# define HAVE_NATCH_CHAR
# define n_NATCH_CHAR(X) X
#else
# define n_NATCH_CHAR(X)
#endif

/* Compile-Time-Assert
 * Problem is that some compilers warn on unused local typedefs, so add
 * a special local CTA to overcome this */
#if defined __STDC_VERSION__ && __STDC_VERSION__ + 0 >= 201112L
# define n_CTA(T,M) _Static_assert(T, M)
# define n_LCTA(T,M) _Static_assert(T, M)
#else
# define n_CTA(T,M)  n__CTA_1(T, n_FILE, __LINE__)
# define n_LCTA(T,M) n__LCTA_1(T, n_FILE, __LINE__)
#endif
#define n_CTAV(T) n_CTA(T, "Unexpected value of constant")
#define n_LCTAV(T) n_LCTA(T, "Unexpected value of constant")

#ifdef n_MAIN_SOURCE
# define n_MCTA(T,M) n_CTA(T, M);
#else
# define n_MCTA(T,M)
#endif

#define n__CTA_1(T,F,L)   n__CTA_2(T, F, L)
#define n__CTA_2(T,F,L) \
   typedef char ASSERTION_failed_in_file_## F ## _at_line_ ## L[(T) ? 1 : -1]
#define n__LCTA_1(T,F,L)  n__LCTA_2(T, F, L)
#define n__LCTA_2(T,F,L) \
do{\
   typedef char ASSERTION_failed_in_file_## F ## _at_line_ ## L[(T) ? 1 : -1];\
   ASSERTION_failed_in_file_## F ## _at_line_ ## L __i_am_unused__;\
   n_UNUSED(__i_am_unused__);\
}while(0)

#define n_UNINIT(N,V)     N = V

/* Create a bit mask for the bit range LO..HI -- HI can't use highest bit! */
#define n_BITENUM_MASK(LO,HI) (((1u << ((HI) + 1)) - 1) & ~((1u << (LO)) - 1))

#undef DBG
#undef NDBG
#ifndef HAVE_DEBUG
# undef assert
# define assert(X)      n_UNUSED(0)
# define DBG(X)
# define NDBG(X)        X
# define DBGOR(X,Y)     Y
#else
# define DBG(X)         X
# define NDBG(X)
# define DBGOR(X,Y)     X
#endif

/* Translation (init in main.c) */
#undef _
#undef N_
#undef V_
#define _(S)            S
#define N_(S)           S
#define V_(S)           S

/*
 * Types TODO v15: n_XX_t
 */

#ifdef UINT8_MAX
# define UI8_MAX        UINT8_MAX
# define SI8_MIN        INT8_MIN
# define SI8_MAX        INT8_MAX
typedef uint8_t         ui8_t;
typedef int8_t          si8_t;
#elif UCHAR_MAX != 255
# error UCHAR_MAX must be 255
#else
# define UI8_MAX        UCHAR_MAX
# define SI8_MIN        CHAR_MIN
# define SI8_MAX        CHAR_MAX
typedef unsigned char   ui8_t;
typedef signed char     si8_t;
#endif

#if !defined PRIu8 || !defined PRId8
# undef PRIu8
# undef PRId8
# define PRIu8          "hhu"
# define PRId8          "hhd"
#endif

#ifdef UINT16_MAX
# define UI16_MAX       UINT16_MAX
# define SI16_MIN       INT16_MIN
# define SI16_MAX       INT16_MAX
typedef uint16_t        ui16_t;
typedef int16_t         si16_t;
#elif USHRT_MAX != 0xFFFFu
# error USHRT_MAX must be 0xFFFF
#else
# define UI16_MAX       USHRT_MAX
# define SI16_MIN       SHRT_MIN
# define SI16_MAX       SHRT_MAX
typedef unsigned short  ui16_t;
typedef signed short    si16_t;
#endif

#if !defined PRIu16 || !defined PRId16
# undef PRIu16
# undef PRId16
# if UI16_MAX == UINT_MAX
#  define PRIu16        "u"
#  define PRId16        "d"
# else
#  define PRIu16        "hu"
#  define PRId16        "hd"
# endif
#endif

#ifdef UINT32_MAX
# define UI32_MAX       UINT32_MAX
# define SI32_MIN       INT32_MIN
# define SI32_MAX       INT32_MAX
typedef uint32_t        ui32_t;
typedef int32_t         si32_t;
#elif ULONG_MAX == 0xFFFFFFFFu
# define UI32_MAX       ULONG_MAX
# define SI32_MIN       LONG_MIN
# define SI32_MAX       LONG_MAX
typedef unsigned long int  ui32_t;
typedef signed long int    si32_t;
#elif UINT_MAX != 0xFFFFFFFFu
# error UINT_MAX must be 0xFFFFFFFF
#else
# define UI32_MAX       UINT_MAX
# define SI32_MIN       INT_MIN
# define SI32_MAX       INT_MAX
typedef unsigned int    ui32_t;
typedef signed int      si32_t;
#endif

#if !defined PRIu32 || !defined PRId32
# undef PRIu32
# undef PRId32
# if UI32_MAX == ULONG_MAX
#  define PRIu32        "lu"
#  define PRId32        "ld"
# else
#  define PRIu32        "u"
#  define PRId32        "d"
# endif
#endif

#ifdef UINT64_MAX
# define UI64_MAX       UINT64_MAX
# define SI64_MIN       INT64_MIN
# define SI64_MAX       INT64_MAX
typedef uint64_t        ui64_t;
typedef int64_t         si64_t;
#elif ULONG_MAX <= 0xFFFFFFFFu
# if !defined ULLONG_MAX || (ULLONG_MAX >> 31) < 0xFFFFFFFFu
#  error We need a 64 bit integer
# else
#  define UI64_MAX      ULLONG_MAX
#  define SI64_MIN      LLONG_MIN
#  define SI64_MAX      LLONG_MAX
__EXTEN typedef unsigned long long  ui64_t;
__EXTEN typedef signed long long    si64_t;
# endif
#else
# define UI64_MAX       ULONG_MAX
# define SI64_MIN       LONG_MIN
# define SI64_MAX       LONG_MAX
typedef unsigned long   ui64_t;
typedef signed long     si64_t;
#endif

#if !defined PRIu64 || !defined PRId64 || !defined PRIX64
# undef PRIu64
# undef PRId64
# undef PRIX64
# if defined ULLONG_MAX && UI64_MAX == ULLONG_MAX
#  define PRIu64        "llu"
#  define PRId64        "lld"
#  define PRIX64        "llX"
# else
#  define PRIu64        "lu"
#  define PRId64        "ld"
#  define PRIX64        "lX"
# endif
#endif

/* (So that we can use UICMP() for size_t comparison, too) */
typedef size_t          uiz_t;
typedef ssize_t         siz_t;

#undef PRIuZ
#undef PRIdZ
#if defined __STDC_VERSION__ && __STDC_VERSION__ + 0 >= 199901L
# define PRIuZ          "zu"
# define PRIdZ          "zd"
# define UIZ_MAX        SIZE_MAX
#elif defined SIZE_MAX
  /* UnixWare has size_t as unsigned as required but uses a signed limit
   * constant (which is thus false!) */
# if SIZE_MAX == UI64_MAX || SIZE_MAX == SI64_MAX
#  define PRIuZ         PRIu64
#  define PRIdZ         PRId64
n_MCTA(sizeof(size_t) == sizeof(ui64_t),
   "Format string mismatch, compile with ISO C99 compiler (-std=c99)!")
# elif SIZE_MAX == UI32_MAX || SIZE_MAX == SI32_MAX
#  define PRIuZ         PRIu32
#  define PRIdZ         PRId32
n_MCTA(sizeof(size_t) == sizeof(ui32_t),
   "Format string mismatch, compile with ISO C99 compiler (-std=c99)!")
# else
#  error SIZE_MAX is neither UI64_MAX nor UI32_MAX (please report this)
# endif
# define UIZ_MAX        SIZE_MAX
#endif
#ifndef PRIuZ
# define PRIuZ          "lu"
# define PRIdZ          "ld"
n_MCTA(sizeof(size_t) == sizeof(unsigned long),
   "Format string mismatch, compile with ISO C99 compiler (-std=c99)!")
# define UIZ_MAX        ULONG_MAX
#endif

#ifndef UINTPTR_MAX
# ifdef SIZE_MAX
#  define uintptr_t     size_t
#  define UINTPTR_MAX   SIZE_MAX
# else
#  define uintptr_t     unsigned long
#  define UINTPTR_MAX   ULONG_MAX
# endif
#endif

#if !defined PRIuPTR || !defined PRIXPTR
# undef PRIuPTR
# undef PRIXPTR
# if UINTPTR_MAX == ULONG_MAX
#  define PRIuPTR       "lu"
#  define PRIXPTR       "lX"
# else
#  define PRIuPTR       "u"
#  define PRIXPTR       "X"
# endif
#endif

#ifdef HAVE_C90AMEND1
typedef wchar_t         wc_t;
# define n_WC_C(X)      L ## X
#else
typedef char            wc_t; /* Yep: really 8-bit char */
# define n_WC_C(X)      X
#endif

enum {FAL0, TRU1, TRUM1 = -1};
typedef si8_t           bool_t;

/* Add shorter aliases for "normal" integers TODO v15 -> n_XX_t */
typedef unsigned long   ul_i;
typedef unsigned int    ui_i;
typedef unsigned short  us_i;
typedef unsigned char   uc_i;

typedef signed long     sl_i;
typedef signed int      si_i;
typedef signed short    ss_i;
typedef signed char     sc_i;

typedef void (          *sighandler_type)(int); /* TODO v15 obsolete */
typedef void (          *n_sighdl_t)(int);

enum authtype {
   AUTHTYPE_NONE     = 1<<0,
   AUTHTYPE_PLAIN    = 1<<1,  /* POP3: APOP is covered by this */
   AUTHTYPE_LOGIN    = 1<<2,
   AUTHTYPE_CRAM_MD5 = 1<<3,
   AUTHTYPE_GSSAPI   = 1<<4
};

enum expand_addr_flags {
   EAF_NONE       = 0,        /* -> EAF_NOFILE | EAF_NOPIPE */
   EAF_RESTRICT   = 1<<0,     /* "restrict" (do unless interaktive / -[~#]) */
   EAF_FAIL       = 1<<1,     /* "fail" */
   EAF_FAILINVADDR = 1<<2,    /* "failinvaddr" */
   /* Bits reused by enum expand_addr_check_mode! */
   EAF_FILE       = 1<<3,     /* +"file" targets */
   EAF_PIPE       = 1<<4,     /* +"pipe" command pipe targets */
   EAF_NAME       = 1<<5,     /* +"name"s (non-address) names / MTA aliases */
   EAF_ADDR       = 1<<6,     /* +"addr" network address (contain "@") */

   EAF_TARGET_MASK  = EAF_FILE | EAF_PIPE | EAF_NAME | EAF_ADDR,
   EAF_RESTRICT_TARGETS = EAF_NAME | EAF_ADDR /* (default set if not set) */
};

enum expand_addr_check_mode {
   EACM_NONE      = 0,        /* Don't care about *expandaddr* */
   EACM_NORMAL    = 1<<0,     /* Use our normal *expandaddr* checking */
   EACM_STRICT    = 1<<1,     /* Never allow any file or pipe addresse */
   EACM_MODE_MASK = 0x3,      /* _NORMAL and _STRICT are mutual! */

   EACM_NOLOG     = 1<<2,     /* Don't log check errors */

   /* Some special overwrites of EAF_TARGETs.
    * May NOT clash with EAF_* bits which may be ORd to these here! */

   EACM_NONAME    = 1<<16
};

enum n_cmd_arg_desc_flags{/* TODO incomplete, misses getmsglist() */
   /* - A type */
   n_CMD_ARG_DESC_STRING = 1<<0,    /* A !blankspacechar() string */
   n_CMD_ARG_DESC_WYSH = 1<<1,      /* sh(1)ell-style quoted */

   n__CMD_ARG_DESC_TYPE_MASK = n_CMD_ARG_DESC_STRING | n_CMD_ARG_DESC_WYSH,

   /* - Optional flags */
   /* It is not an error if an optional argument is missing; once an argument
    * has been declared optional only optional arguments may follow */
   n_CMD_ARG_DESC_OPTION = 1<<16,
   /* GREEDY: parse as many of that type as possible; must be last entry */
   n_CMD_ARG_DESC_GREEDY = 1<<17,
   /* Honour an overall "stop" request in one of the arguments (\c@ or #) */
   n_CMD_ARG_DESC_HONOUR_STOP = 1<<18,

   n__CMD_ARG_DESC_FLAG_MASK = n_CMD_ARG_DESC_OPTION | n_CMD_ARG_DESC_GREEDY |
         n_CMD_ARG_DESC_HONOUR_STOP
};

#ifdef HAVE_COLOUR
/* We do have several contexts of colour IDs; since only one of them can be
 * active at any given time let's share the value range */
enum n_colour_ctx{
   n_COLOUR_CTX_SUM,
   n_COLOUR_CTX_VIEW,
   n_COLOUR_CTX_MLE,
   n__COLOUR_CTX_MAX1
};

enum n_colour_id{
   /* Header summary */
   n_COLOUR_ID_SUM_DOTMARK = 0,
   n_COLOUR_ID_SUM_HEADER,
   n_COLOUR_ID_SUM_THREAD,

   /* Message display */
   n_COLOUR_ID_VIEW_FROM_ = 0,
   n_COLOUR_ID_VIEW_HEADER,
   n_COLOUR_ID_VIEW_MSGINFO,
   n_COLOUR_ID_VIEW_PARTINFO,

   /* Mailx-Line-Editor */
   n_COLOUR_ID_MLE_POSITION = 0,
   n_COLOUR_ID_MLE_PROMPT,

   n__COLOUR_IDS = n_COLOUR_ID_VIEW_PARTINFO + 1
};

/* Colour preconditions, let's call them tags, cannot be an enum because for
 * message display they are the actual header name of the current header.  Thus
 * let's use constants of pseudo pointers */
# define n_COLOUR_TAG_SUM_DOT ((char*)-2)
# define n_COLOUR_TAG_SUM_OLDER ((char*)-3)
#endif /* HAVE_COLOUR */

enum conversion {
   CONV_NONE,        /* no conversion */
   CONV_7BIT,        /* no conversion, is 7bit */
   CONV_FROMQP,      /* convert from quoted-printable */
   CONV_TOQP,        /* convert to quoted-printable */
   CONV_8BIT,        /* convert to 8bit (iconv) */
   CONV_FROMB64,     /* convert from base64 */
   CONV_FROMB64_T,   /* convert from base64/text */
   CONV_TOB64,       /* convert to base64 */
   CONV_FROMHDR,     /* convert from RFC1522 format */
   CONV_TOHDR,       /* convert to RFC1522 format */
   CONV_TOHDR_A      /* convert addresses for header */
};

enum cproto {
   CPROTO_CCRED,     /* Special dummy credential proto (S/MIME etc.) */
   CPROTO_SMTP,
   CPROTO_POP3
};

enum n_dotlock_state{
   n_DLS_NONE,
   n_DLS_CANT_CHDIR,    /* Failed to chdir(2) into desired path */
   n_DLS_NAMETOOLONG,   /* Lock file name would be too long */
   n_DLS_ROFS,          /* Read-only filesystem (no error, mailbox RO) */
   n_DLS_NOPERM,        /* No permission to creat lock file */
   n_DLS_NOEXEC,        /* Privilege separated dotlocker not found */
   n_DLS_PRIVFAILED,    /* Rising privileges failed in dotlocker */
   n_DLS_EXIST,         /* Lock file already exists, stale lock? */
   n_DLS_FISHY,         /* Something makes us think bad of situation */
   n_DLS_DUNNO,         /* Catch-all error */
   n_DLS_PING,          /* Not an error, but have to wait for lock */
   n_DLS_ABANDON = 1<<7 /* ORd to any but _NONE: give up, don't retry */
};

enum exit_status {
   EXIT_OK        = EXIT_SUCCESS,
   EXIT_ERR       = EXIT_FAILURE,
   EXIT_USE       = 64,       /* sysexits.h:EX_USAGE */
   EXIT_NOUSER    = 67,       /* :EX_NOUSER */
   EXIT_COLL_ABORT = 1<<1,    /* Message collection was aborted */
   EXIT_SEND_ERROR = 1<<2     /* Unspecified send error occurred */
};

enum fedit_mode {
   FEDIT_NONE     = 0,
   FEDIT_SYSBOX   = 1<<0,     /* %: prefix */
   FEDIT_RDONLY   = 1<<1,     /* Readonly (per-box, OPT_R_FLAG is global) */
   FEDIT_NEWMAIL  = 1<<2      /* `newmail' operation TODO OBSOLETE THIS! */
};

enum fexp_mode {
   FEXP_FULL,                 /* Full expansion */
   FEXP_NOPROTO = 1<<0,       /* TODO no which_protocol() to decide expansion */
   FEXP_SILENT = 1<<1,        /* Don't print but only return errors */
   FEXP_MULTIOK = 1<<2,       /* Expansion to many entries is ok */
   FEXP_LOCAL = 1<<3,         /* Result must be local file/maildir */
   FEXP_NSHORTCUT = 1<<4,     /* Don't expand shortcuts */
   FEXP_NSPECIAL = 1<<5,      /* No %,#,& specials */
   FEXP_NFOLDER = 1<<6,       /* NSPECIAL and no + folder, too */
   FEXP_NSHELL = 1<<7,        /* Don't do shell word exp. (but ~/, $VAR) */
   FEXP_NVAR = 1<<8           /* ..not even $VAR expansion */
};

enum n_file_lock_type{
   FLT_READ,
   FLT_WRITE
};

enum n_iconv_flags{
   n_ICONV_NONE = 0,
   n_ICONV_IGN_ILSEQ = 1<<0,     /* Ignore EILSEQ in input (replacement char) */
   n_ICONV_IGN_NOREVERSE = 1<<1, /* .. non-reversible conversions in output */
   n_ICONV_UNIREPL = 1<<2,       /* Use Unicode replacement 0xFFFD = EF BF BD */
   n_ICONV_DEFAULT = n_ICONV_IGN_ILSEQ | n_ICONV_IGN_NOREVERSE,
   n_ICONV_UNIDEFAULT = n_ICONV_DEFAULT | n_ICONV_UNIREPL
};

/* Special ignore (where _TYPE is covered by POSIX `ignore' / `retain').
 * _ALL is very special in that it doesn't have a backing object */
#define n_IGNORE_ALL ((struct n_ignore*)-2)
#define n_IGNORE_TYPE ((struct n_ignore*)-3)
#define n_IGNORE_SAVE ((struct n_ignore*)-4)
#define n_IGNORE_FWD ((struct n_ignore*)-5)
#define n_IGNORE_TOP ((struct n_ignore*)-6)
#define n__IGNORE_ADJUST 3
#define n__IGNORE_MAX (6 - n__IGNORE_ADJUST)

enum n_lexinput_flags{
   n_LEXINPUT_NONE,
   n_LEXINPUT_CTX_BASE = 0,            /* Generic shared base: don't use! */
   n_LEXINPUT_CTX_DEFAULT = 1,         /* Default input */
   n_LEXINPUT_CTX_COMPOSE = 2,         /* Compose mode input */
   n__LEXINPUT_CTX_MASK = 3,
   n__LEXINPUT_CTX_MAX1 = n_LEXINPUT_CTX_COMPOSE + 1,

   n_LEXINPUT_FORCE_STDIN = 1<<8,      /* Even in macro, use stdin (`read')! */
   n_LEXINPUT_NL_ESC = 1<<9,           /* Support "\\$" line continuation */
   n_LEXINPUT_NL_FOLLOW = 1<<10,       /* ..on such a follow line */
   n_LEXINPUT_PROMPT_NONE = 1<<11,     /* Don't print prompt */
   n_LEXINPUT_PROMPT_EVAL = 1<<12,     /* Instead, evaluate *prompt* */
#if 0
   n_LEXINPUT_DROP_TRAIL_SPC = 1<<14,  /* Drop any trailing space */
   n_LEXINPUT_DROP_LEAD_SPC = 1<<15,   /* ..leading ones */
   n_LEXINPUT_TRIM_SPACE = n_LEXINPUT_DROP_TRAIL_SPC | n_LEXINPUT_DROP_LEAD_SPC,
#endif

   n_LEXINPUT_HIST_ADD = 1<<16,        /* Add the result to history list */
   n_LEXINPUT_HIST_GABBY = 1<<17       /* Consider history entry as gabby */
};

enum mimecontent {
   MIME_UNKNOWN,     /* unknown content */
   MIME_SUBHDR,      /* inside a multipart subheader */
   MIME_822,         /* message/rfc822 content */
   MIME_MESSAGE,     /* other message/ content */
   MIME_TEXT_PLAIN,  /* text/plain content */
   MIME_TEXT_HTML,   /* text/html content */
   MIME_TEXT,        /* other text/ content */
   MIME_ALTERNATIVE, /* multipart/alternative content */
   MIME_RELATED,     /* mime/related (RFC 2387) */
   MIME_DIGEST,      /* multipart/digest content */
   MIME_SIGNED,      /* multipart/signed */
   MIME_ENCRYPTED,   /* multipart/encrypted */
   MIME_MULTI,       /* other multipart/ content */
   MIME_PKCS7,       /* PKCS7 content */
   MIME_DISCARD      /* content is discarded */
};

enum mime_counter_evidence {
   MIMECE_NONE,
   MIMECE_SET        = 1<<0,  /* *mime-counter-evidence* was set */
   MIMECE_BIN_OVWR   = 1<<1,  /* appli../octet-stream: check, ovw if possible */
   MIMECE_ALL_OVWR   = 1<<2,  /* all: check, ovw if possible */
   MIMECE_BIN_PARSE  = 1<<3   /* appli../octet-stream: classify contents last */
};

/* Content-Transfer-Encodings as defined in RFC 2045:
 * - Quoted-Printable, section 6.7
 * - Base64, section 6.8 */
#define QP_LINESIZE     (4 * 19)       /* Max. compliant QP linesize */

#define B64_LINESIZE    (4 * 19)       /* Max. compliant Base64 linesize */
#define B64_ENCODE_INPUT_PER_LINE 57   /* Max. input for Base64 encode/line */

enum mime_enc {
   MIMEE_NONE,       /* message is not in MIME format */
   MIMEE_BIN,        /* message is in binary encoding */
   MIMEE_8B,         /* message is in 8bit encoding */
   MIMEE_7B,         /* message is in 7bit encoding */
   MIMEE_QP,         /* message is quoted-printable */
   MIMEE_B64         /* message is in base64 encoding */
};

/* xxx QP came later, maybe rewrite all to use mime_enc_flags directly? */
enum mime_enc_flags {
   MIMEEF_NONE,
   MIMEEF_SALLOC     = 1<<0,  /* Use salloc(), not srealloc().. */
   /* ..result .s,.l point to user buffer of *_LINESIZE+[+[+]] bytes instead */
   MIMEEF_BUF        = 1<<1,
   MIMEEF_CRLF       = 1<<2,  /* (encode) Append "\r\n" to lines */
   MIMEEF_LF         = 1<<3,  /* (encode) Append "\n" to lines */
   /* (encode) If one of _CRLF/_LF is set, honour *_LINESIZE+[+[+]] and
    * inject the desired line-ending whenever a linewrap is desired */
   MIMEEF_MULTILINE  = 1<<4,
   /* (encode) Quote with header rules, do not generate soft NL breaks?
    * For mustquote(), specifies whether special RFC 2047 header rules
    * should be used instead */
   MIMEEF_ISHEAD     = 1<<5,
   /* (encode) Ditto; for mustquote() this furtherly fine-tunes behaviour in
    * that characters which would not be reported as "must-quote" when
    * detecting whether quoting is necessary at all will be reported as
    * "must-quote" if they have to be encoded in an encoded word */
   MIMEEF_ISENCWORD  = 1<<6,
   __MIMEEF_LAST     = 6
};

enum qpflags {
   QP_NONE        = MIMEEF_NONE,
   QP_SALLOC      = MIMEEF_SALLOC,
   QP_BUF         = MIMEEF_BUF,
   QP_ISHEAD      = MIMEEF_ISHEAD,
   QP_ISENCWORD   = MIMEEF_ISENCWORD
};

enum b64flags {
   B64_NONE       = MIMEEF_NONE,
   B64_SALLOC     = MIMEEF_SALLOC,
   B64_BUF        = MIMEEF_BUF,
   B64_CRLF       = MIMEEF_CRLF,
   B64_LF         = MIMEEF_LF,
   B64_MULTILINE  = MIMEEF_MULTILINE,
   /* Not used, but for clarity only */
   B64_ISHEAD     = MIMEEF_ISHEAD,
   B64_ISENCWORD  = MIMEEF_ISENCWORD,
   /* Special version of Base64, "Base64URL", according to RFC 4648.
    * Only supported for encoding! */
   B64_RFC4648URL = 1<<(__MIMEEF_LAST+1),
   /* Don't use any ("=") padding;
    * may NOT be used with any of _CRLF, _LF or _MULTILINE */
   B64_NOPAD      = 1<<(__MIMEEF_LAST+2)
};

enum mime_parse_flags {
   MIME_PARSE_NONE      = 0,
   MIME_PARSE_DECRYPT   = 1<<0,
   MIME_PARSE_PARTS     = 1<<1,
   MIME_PARSE_SHALLOW   = 1<<2
};

enum mime_handler_flags {
   MIME_HDL_NULL,                /* No pipe- mimetype handler, go away */
   MIME_HDL_CMD,                 /* Normal command */
   MIME_HDL_TEXT,                /* @ special cmd to force treatment as text */
   MIME_HDL_PTF,                 /* A special pointer-to-function handler */
   MIME_HDL_MSG,                 /* Display msg (returned as command string) */
   MIME_HDL_TYPE_MASK   = 7,
   MIME_HDL_ISQUOTE     = 1<<4,  /* Is quote action (we have info, keep it!) */
   MIME_HDL_NOQUOTE     = 1<<5,  /* No MIME for quoting */
   MIME_HDL_ALWAYS      = 1<<6,  /* Handler shall run for multi-msg actions */
   MIME_HDL_ASYNC       = 1<<7,  /* Should run asynchronously */
   MIME_HDL_NEEDSTERM   = 1<<8,  /* Takes over terminal */
   MIME_HDL_TMPF        = 1<<9,  /* Create temporary file (zero-sized) */
   MIME_HDL_TMPF_FILL   = 1<<10, /* Fill in the msg body content */
   MIME_HDL_TMPF_UNLINK = 1<<11  /* Delete it later again */
};

enum mlist_state {
   MLIST_OTHER       = 0,     /* Normal address */
   MLIST_KNOWN       = 1,     /* A known `mlist' */
   MLIST_SUBSCRIBED  = -1     /* A `mlsubscribe'd list */
};

enum oflags {
   OF_RDONLY      = 1<<0,
   OF_WRONLY      = 1<<1,
   OF_RDWR        = 1<<2,
   OF_APPEND      = 1<<3,
   OF_CREATE      = 1<<4,
   OF_TRUNC       = 1<<5,
   OF_EXCL        = 1<<6,
   OF_CLOEXEC     = 1<<7,     /* TODO not used, always implied!  CHANGE!! */
   OF_UNLINK      = 1<<8,     /* Only for Ftmp(): unlink(2) after creation */
   OF_HOLDSIGS    = 1<<9,     /* Mutual with OF_UNLINK - await Ftmp_free() */
   OF_REGISTER    = 1<<10,    /* Register file in our file table */
   OF_REGISTER_UNLINK = 1<<11, /* unlink(2) upon unreg.; _REGISTER asserted! */
   OF_SUFFIX      = 1<<12     /* Ftmp() name hint is mandatory! extension! */
};

enum okay {
   STOP = 0,
   OKAY = 1
};

enum okey_xlook_mode {
   OXM_PLAIN      = 1<<0,     /* Plain key always tested */
   OXM_H_P        = 1<<1,     /* Check PLAIN-.url_h_p */
   OXM_U_H_P      = 1<<2,     /* Check PLAIN-.url_u_h_p */
   OXM_ALL        = 0x7
};

/* <0 means "stop" unless *prompt* extensions are enabled. */
enum prompt_exp {
   PROMPT_STOP    = -1,       /* \c */
   /* *prompt* extensions: \$, \@ etc. */
   PROMPT_DOLLAR  = -2,
   PROMPT_AT      = -3
};

enum protocol {
   PROTO_FILE,       /* refers to a local file */
   PROTO_POP3,       /* is a pop3 server string */
   PROTO_MAILDIR,    /* refers to a maildir folder */
   PROTO_UNKNOWN     /* unknown protocol */
};

enum sendaction {
   SEND_MBOX,        /* no conversion to perform */
   SEND_RFC822,      /* no conversion, no From_ line */
   SEND_TODISP,      /* convert to displayable form */
   SEND_TODISP_ALL,  /* same, include all MIME parts */
   SEND_SHOW,        /* convert to 'show' command form */
   SEND_TOSRCH,      /* convert for IMAP SEARCH */
   SEND_TOFILE,      /* convert for saving body to a file */
   SEND_TOPIPE,      /* convert for pipe-content/subc. */
   SEND_QUOTE,       /* convert for quoting */
   SEND_QUOTE_ALL,   /* same, include all MIME parts */
   SEND_DECRYPT      /* decrypt */
};

enum n_shexp_parse_flags{
   n_SHEXP_PARSE_NONE,
   /* Don't perform any expansions or interpret backslash escape sequences etc.
    * Output may be NULL, otherwise the possibly trimmed non-expanded input is
    * used as output */
   n_SHEXP_PARSE_DRYRUN = 1<<0,
   n_SHEXP_PARSE_TRUNC = 1<<1,         /* Truncate result storage on entry */
   n_SHEXP_PARSE_TRIMSPACE = 1<<2,     /* Ignore space surrounding tokens */
   n_SHEXP_PARSE_LOG = 1<<3,           /* Log errors */
   n_SHEXP_PARSE_LOG_D_V = 1<<4,       /* Log errors if OPT_D_V */
   n_SHEXP_PARSE_IFS_ADD_COMMA = 1<<5, /* Add comma , to normal "IFS" */
   n_SHEXP_PARSE_IFS_IS_COMMA = 1<<6,  /* Let comma , be the sole "IFS" */
   n_SHEXP_PARSE_IGNORE_EMPTY = 1<<7,  /* Ignore empty tokens, start over */
   /* Implicitly open quotes, and ditto closing.  _AUTO_FIXED may only be used
    * if an auto-quote-mode is enabled, implies _AUTO_CLOSE and causes the
    * quote mode to be permanently active (cannot be closed) */
   n_SHEXP_PARSE_QUOTE_AUTO_FIXED = 1<<8,
   n_SHEXP_PARSE_QUOTE_AUTO_SQ = 1<<9,
   n_SHEXP_PARSE_QUOTE_AUTO_DQ = 1<<10,
   n_SHEXP_PARSE_QUOTE_AUTO_DSQ = 1<<11,
   n_SHEXP_PARSE_QUOTE_AUTO_CLOSE = 1<<12, /* Ignore an open quote at EOS */
   n__SHEXP_PARSE_QUOTE_AUTO_MASK = n_SHEXP_PARSE_QUOTE_AUTO_SQ |
         n_SHEXP_PARSE_QUOTE_AUTO_DQ | n_SHEXP_PARSE_QUOTE_AUTO_DSQ,

   n__SHEXP_PARSE_LAST = 12
};

enum n_shexp_state{
   n_SHEXP_STATE_NONE,
   /* We have produced some output (or would have, with _PARSE_DRYRUN).
    * Note that empty quotes like '' produce no output but set this bit */
   n_SHEXP_STATE_OUTPUT = 1<<2,
   /* Don't call the parser again (\c0 or # comment seen; out of input).
    * Not (necessarily) mutual with _OUTPUT) */
   n_SHEXP_STATE_STOP = 1<<1,
   n_SHEXP_STATE_UNICODE = 1<<3,       /* \[Uu] used */
   n_SHEXP_STATE_CONTROL = 1<<4,       /* Control characters seen */

   n_SHEXP_STATE_ERR_CONTROL = 1<<16,  /* \c notation with invalid argument */
   n_SHEXP_STATE_ERR_UNICODE = 1<<17,  /* Valid \[Uu] used and !OPT_UNICODE */
   n_SHEXP_STATE_ERR_NUMBER = 1<<18,   /* Bad number (\[UuXx]) */
   n_SHEXP_STATE_ERR_BRACE = 1<<19,    /* _QUOTEOPEN + no } brace for ${VAR */
   n_SHEXP_STATE_ERR_BADSUB = 1<<20,   /* Empty/bad ${} substitution */
   n_SHEXP_STATE_ERR_QUOTEOPEN = 1<<21, /* Quote remains open at EOS */

   n_SHEXP_STATE_ERR_MASK = n_BITENUM_MASK(16, 21)
};

enum n_sigman_flags{
   n_SIGMAN_NONE = 0,
   n_SIGMAN_HUP = 1<<0,
   n_SIGMAN_INT = 1<<1,
   n_SIGMAN_QUIT = 1<<2,
   n_SIGMAN_TERM = 1<<3,
   n_SIGMAN_PIPE = 1<<4,

   n_SIGMAN_IGN_HUP = 1<<5,
   n_SIGMAN_IGN_INT = 1<<6,
   n_SIGMAN_IGN_QUIT = 1<<7,
   n_SIGMAN_IGN_TERM = 1<<8,

   n_SIGMAN_ALL = 0xFF,
   /* Mostly for _leave() reraise flags */
   n_SIGMAN_VIPSIGS = n_SIGMAN_HUP | n_SIGMAN_INT | n_SIGMAN_QUIT |
         n_SIGMAN_TERM,
   n_SIGMAN_NTTYOUT_PIPE = 1<<16,
   n_SIGMAN_VIPSIGS_NTTYOUT = n_SIGMAN_HUP | n_SIGMAN_INT | n_SIGMAN_QUIT |
         n_SIGMAN_TERM | n_SIGMAN_NTTYOUT_PIPE,

   n__SIGMAN_PING = 1<<17
};

#ifdef HAVE_SSL
enum ssl_verify_level {
   SSL_VERIFY_IGNORE,
   SSL_VERIFY_WARN,
   SSL_VERIFY_ASK,
   SSL_VERIFY_STRICT
};
#endif

enum tdflags {
   TD_NONE,                   /* no display conversion */
   TD_ISPR        = 1<<0,     /* use isprint() checks */
   TD_ICONV       = 1<<1,     /* use iconv() */
   TD_DELCTRL     = 1<<2,     /* delete control characters */

   /*
    * NOTE: _TD_EOF and _TD_BUFCOPY may be ORd with enum conversion and
    * enum sendaction, and may thus NOT clash with their bit range!
    */
   _TD_EOF        = 1<<14,    /* EOF seen, last round! */
   _TD_BUFCOPY    = 1<<15     /* Buffer may be constant, copy it */
};

#ifdef n_HAVE_TCAP
enum n_termcap_captype{
   n_TERMCAP_CAPTYPE_NONE = 0,
   /* Internally we share the bitspace, so ensure no value ends up as 0 */
   n_TERMCAP_CAPTYPE_BOOL = 1,
   n_TERMCAP_CAPTYPE_NUMERIC,
   n_TERMCAP_CAPTYPE_STRING,
   n__TERMCAP_CAPTYPE_MAX1
};

/* Termcap commands; different to queries commands perform actions.
 * Commands are resolved upon init time, and are all termcap(5)-compatible,
 * therefore we use the short termcap(5) names.
 * Note this is parsed by mk-tcap-map.pl, which expects the syntax
 * "CONSTANT, COMMENT" where COMMENT is "Capname/TCap-Code, TYPE[, FLAGS]",
 * and one of Capname and TCap-Code may be the string "-" meaning ENOENT;
 * a | vertical bar or end-of-comment ends processing; see termcap.c.
 * We may use the free-form part after | for the "Variable String" and notes on
 * necessary termcap_cmd() arguments; if those are in [] brackets they are not
 * regular but are only used when the command, i.e., its effect, is somehow
 * simulated / faked by a builtin fallback implementation.
 * Availability of builtin fallback indicated by leading @ (at-sign) */
enum n_termcap_cmd{
# ifdef HAVE_TERMCAP
   n_TERMCAP_CMD_te, /* rmcup/te, STRING | exit_ca_mode: -,- */
   n_TERMCAP_CMD_ti, /* smcup/ti, STRING | enter_ca_mode: -,- */

   n_TERMCAP_CMD_ks, /* smkx/ks, STRING | keypad_xmit: -,- */
   n_TERMCAP_CMD_ke, /* rmkx/ke, STRING | keypad_local: -,- */

   n_TERMCAP_CMD_cd, /* ed/cd, STRING | clr_eos: -,- */
   n_TERMCAP_CMD_cl, /* clear/cl, STRING | clear_screen(+home): -,- */
   n_TERMCAP_CMD_ho, /* home/ho, STRING | cursor_home: -,- */
# endif

# ifdef HAVE_MLE
   n_TERMCAP_CMD_ce, /* el/ce, STRING | @ clr_eol: [start-column],- */
   n_TERMCAP_CMD_ch, /* hpa/ch, STRING, IDX1 | column_address: column,- */
   n_TERMCAP_CMD_cr, /* cr/cr, STRING | @ carriage_return: -,- */
   n_TERMCAP_CMD_le, /* cub1/le, STRING, CNT | @ cursor_left: count,- */
   n_TERMCAP_CMD_nd, /* cuf1/nd, STRING, CNT | @ cursor_right: count,- */
# endif

   n__TERMCAP_CMD_MAX1,
   n__TERMCAP_CMD_MASK = (1<<24) - 1,

   /* Only perform command if ca-mode is used */
   n_TERMCAP_CMD_FLAG_CA_MODE = 1<<29,
   /* I/O should be flushed after command completed */
   n_TERMCAP_CMD_FLAG_FLUSH = 1<<30
};

/* Termcap queries; a query is a command that returns a struct n_termcap_value.
 * Queries are resolved once they are used first, and may not be termcap(5)-
 * compatible, therefore we use terminfo(5) names.
 * Note this is parsed by mk-tcap-map.pl, which expects the syntax
 * "CONSTANT, COMMENT" where COMMENT is "Capname/TCap-Code, TYPE[, FLAGS]",
 * and one of Capname and TCap-Code may be the string "-" meaning ENOENT;
 * a | vertical bar or end-of-comment ends processing; see termcap.c.
 * We may use the free-form part after | for the "Variable String" and notes.
 * The "xkey | X:" keys are Dickey's xterm extensions, see (our) manual */
enum n_termcap_query{
# ifdef HAVE_COLOUR
   n_TERMCAP_QUERY_colors, /* colors/Co, NUMERIC | max_colors */
# endif

   /* --mk-tcap-map--: only KEY_BINDINGS follow.  DON'T CHANGE THIS LINE! */
   /* Update the `bind' manual on change! */
# ifdef HAVE_KEY_BINDINGS
   n_TERMCAP_QUERY_key_backspace, /* kbs/kb, STRING */
   n_TERMCAP_QUERY_key_dc,       /* kdch1/kD, STRING | delete-character */
      n_TERMCAP_QUERY_key_sdc,      /* kDC / *4, STRING | ..shifted */
   n_TERMCAP_QUERY_key_eol,      /* kel/kE, STRING | clear-to-end-of-line */
   n_TERMCAP_QUERY_key_exit,     /* kext/@9, STRING */
   n_TERMCAP_QUERY_key_ic,       /* kich1/kI, STRING | insert character */
      n_TERMCAP_QUERY_key_sic,      /* kIC/#3, STRING | ..shifted */
   n_TERMCAP_QUERY_key_home,     /* khome/kh, STRING */
      n_TERMCAP_QUERY_key_shome,    /* kHOM/#2, STRING | ..shifted */
   n_TERMCAP_QUERY_key_end,      /* kend/@7, STRING */
      n_TERMCAP_QUERY_key_send,     /* kEND / *7, STRING | ..shifted */
   n_TERMCAP_QUERY_key_npage,    /* knp/kN, STRING */
   n_TERMCAP_QUERY_key_ppage,    /* kpp/kP, STRING */
   n_TERMCAP_QUERY_key_left,     /* kcub1/kl, STRING */
      n_TERMCAP_QUERY_key_sleft,    /* kLFT/#4, STRING | ..shifted */
      n_TERMCAP_QUERY_xkey_aleft,   /* kLFT3/-, STRING | X: Alt+left */
      n_TERMCAP_QUERY_xkey_cleft,   /* kLFT5/-, STRING | X: Control+left */
   n_TERMCAP_QUERY_key_right,    /* kcuf1/kr, STRING */
      n_TERMCAP_QUERY_key_sright,   /* kRIT/%i, STRING | ..shifted */
      n_TERMCAP_QUERY_xkey_aright,  /* kRIT3/-, STRING | X: Alt+right */
      n_TERMCAP_QUERY_xkey_cright,  /* kRIT5/-, STRING | X: Control+right */
   n_TERMCAP_QUERY_key_down,     /* kcud1/kd, STRING */
      n_TERMCAP_QUERY_xkey_sdown,   /* kDN/-, STRING | ..shifted */
      n_TERMCAP_QUERY_xkey_adown,   /* kDN3/-, STRING | X: Alt+down */
      n_TERMCAP_QUERY_xkey_cdown,   /* kDN5/-, STRING | X: Control+down */
   n_TERMCAP_QUERY_key_up,       /* kcuu1/ku, STRING */
      n_TERMCAP_QUERY_xkey_sup,     /* kUP/-, STRING | ..shifted */
      n_TERMCAP_QUERY_xkey_aup,     /* kUP3/-, STRING | X: Alt+up */
      n_TERMCAP_QUERY_xkey_cup,     /* kUP5/-, STRING | X: Control+up */
   n_TERMCAP_QUERY_kf0,          /* kf0/k0, STRING */
   n_TERMCAP_QUERY_kf1,          /* kf1/k1, STRING */
   n_TERMCAP_QUERY_kf2,          /* kf2/k2, STRING */
   n_TERMCAP_QUERY_kf3,          /* kf3/k3, STRING */
   n_TERMCAP_QUERY_kf4,          /* kf4/k4, STRING */
   n_TERMCAP_QUERY_kf5,          /* kf5/k5, STRING */
   n_TERMCAP_QUERY_kf6,          /* kf6/k6, STRING */
   n_TERMCAP_QUERY_kf7,          /* kf7/k7, STRING */
   n_TERMCAP_QUERY_kf8,          /* kf8/k8, STRING */
   n_TERMCAP_QUERY_kf9,          /* kf9/k9, STRING */
   n_TERMCAP_QUERY_kf10,         /* kf10/k;, STRING */
   n_TERMCAP_QUERY_kf11,         /* kf11/F1, STRING */
   n_TERMCAP_QUERY_kf12,         /* kf12/F2, STRING */
   n_TERMCAP_QUERY_kf13,         /* kf13/F3, STRING */
   n_TERMCAP_QUERY_kf14,         /* kf14/F4, STRING */
   n_TERMCAP_QUERY_kf15,         /* kf15/F5, STRING */
   n_TERMCAP_QUERY_kf16,         /* kf16/F6, STRING */
   n_TERMCAP_QUERY_kf17,         /* kf17/F7, STRING */
   n_TERMCAP_QUERY_kf18,         /* kf18/F8, STRING */
   n_TERMCAP_QUERY_kf19,         /* kf19/F9, STRING */
# endif /* HAVE_KEY_BINDINGS */

   n__TERMCAP_QUERY_MAX1
};
#endif /* n_HAVE_TCAP */

enum n_visual_info_flags{
   n_VISUAL_INFO_NONE,
   n_VISUAL_INFO_ONE_CHAR = 1<<0,         /* Step only one char, then return */
   n_VISUAL_INFO_SKIP_ERRORS = 1<<1,      /* Treat via replacement, step byte */
   n_VISUAL_INFO_WIDTH_QUERY = 1<<2,      /* Detect visual character widths */

   /* Rest only with HAVE_C90AMEND1, mutual with _ONE_CHAR */
   n_VISUAL_INFO_WOUT_CREATE = 1<<8,      /* Use/create .vic_woudat */
   n_VISUAL_INFO_WOUT_SALLOC = 1<<9,      /* ..salloc() it first */
   /* Only visuals into .vic_woudat - implies _WIDTH_QUERY */
   n_VISUAL_INFO_WOUT_PRINTABLE = 1<<10,
   n__VISUAL_INFO_FLAGS = n_VISUAL_INFO_WOUT_CREATE |
         n_VISUAL_INFO_WOUT_SALLOC | n_VISUAL_INFO_WOUT_PRINTABLE
};

enum user_options {
   OPT_NONE,
   OPT_DEBUG      = 1u<< 0,   /* -d / *debug* */
   OPT_VERB       = 1u<< 1,   /* -v / *verbose* */
   OPT_VERBVERB   = 1u<< 2,   /* .. even more verbosity */
   OPT_EXISTONLY  = 1u<< 3,   /* -e */
   OPT_HEADERSONLY = 1u<<4,   /* -H */
   OPT_HEADERLIST = 1u<< 5,   /* -L */
   OPT_QUICKRUN_MASK = OPT_EXISTONLY | OPT_HEADERSONLY | OPT_HEADERLIST,
   OPT_E_FLAG     = 1u<< 7,   /* -E / *skipemptybody* */
   OPT_F_FLAG     = 1u<< 8,   /* -F */
   OPT_Mm_FLAG    = 1u<< 9,   /* -M or -m (plus option_Mm_arg) */
   OPT_N_FLAG     = 1u<<10,   /* -N / *header* */
   OPT_R_FLAG     = 1u<<11,   /* -R */
   OPT_r_FLAG     = 1u<<12,   /* -r (plus option_r_arg) */
   OPT_t_FLAG     = 1u<<13,   /* -t */
   OPT_TILDE_FLAG = 1u<<14,   /* -~ */
   OPT_BATCH_FLAG = 1u<<15,   /* -# */

   /*  */
   OPT_MEMDEBUG   = 1<<16,    /* *memdebug* */

   /*  */
   OPT_SENDMODE   = 1u<<17,   /* Usage case forces send mode */
   OPT_TTYIN      = 1u<<18,
   OPT_TTYOUT     = 1u<<19,
   OPT_INTERACTIVE = 1u<<20,
   OPT_UNICODE    = 1u<<21,   /* We're in an UTF-8 environment */

   OPT_ENC_MBSTATE = 1u<<22,  /* Multibyte environment with shift states */

   /* Some easy-access shortcuts */
   OPT_D_V        = OPT_DEBUG | OPT_VERB,
   OPT_D_VV       = OPT_DEBUG | OPT_VERBVERB,
   OPT_D_V_VV     = OPT_DEBUG | OPT_VERB | OPT_VERBVERB
};

#define OBSOLETE(X) \
do {\
   if (options & OPT_D_V_VV)\
      n_err("%s: %s\n", _("Obsoletion warning"), X);\
} while (0)
#define OBSOLETE2(X,Y) \
do {\
   if (options & OPT_D_V_VV)\
      n_err("%s: %s: %s\n", _("Obsoletion warning"), X, Y);\
} while (0)

enum program_state {
   PS_NONE           = 0,
   PS_STARTED        = 1u<< 0,      /* main.c startup code passed, functional */
   PS_ROOT           = 1u<<30,      /* Temporary "bypass any checks" bit */

   PS_EXIT           = 1u<< 1,      /* Exit request pending */
   PS_SOURCING       = 1u<< 2,      /* During load() or `source' */
   PS_COMPOSE_MODE   = 1u<< 3,      /* State machine recursed */
   PS_COMPOSE_FORKHOOK = 1u<<4,     /* *on-compose-done* running (fork(2)ed!) */
   PS_ROBOT          = 1u<< 5,      /* State machine in non-interactive state */

   PS_EVAL_ERROR     = 1u<< 6,      /* Last evaluate() command failed */

   PS_HOOK_NEWMAIL   = 1u<< 7,
   PS_HOOK           = 1u<< 8,
   PS_HOOK_MASK      = PS_HOOK_NEWMAIL | PS_HOOK,

   PS_EDIT           = 1u<< 9,      /* Current mailbox not a "system mailbox" */
   PS_SETFILE_OPENED = 1u<<10,      /* (hack) setfile() opened a new box */
   PS_SAW_COMMAND    = 1u<<11,      /* ..after mailbox switch */

   PS_DID_PRINT_DOT  = 1u<<12,      /* Current message has been printed */

   PS_SIGWINCH_PEND  = 1u<<14,      /* Need update of $COLUMNS/$LINES */
   PS_PSTATE_PENDMASK = PS_SIGWINCH_PEND, /* pstate housekeeping needed */

   PS_ARGLIST_MASK   = n_BITENUM_MASK(17, 18),
   PS_MSGLIST_GABBY  = 1u<<17,      /* getmsglist() saw what it thinks: gabby */
   PS_MSGLIST_DIRECT = 1u<<18,      /* One msg was directly chosen by number */
   /* TODO HACK: until v15 PS_MSGLIST_SAW_NO is an indication whether an entry
    * TODO may be placed in the history or not (grep this, see commands()),
    * TODO so avoid reusing this bit */
   PS_WYSHLIST_SAW_CONTROL = 1u<<18, /* ..saw C0+ control characters */

   PS_EXPAND_MULTIRESULT = 1u<<19,  /* Last fexpand() with MULTIOK had .. */

   PS_HEADER_NEEDED_MIME = 1u<<20,  /* mime_write_tohdr() needed x TODO HACK! */

   PS_READLINE_NL = 1u<<21,         /* readline_input()+ saw a \n TODO HACK! */

   PS_COLOUR_ACTIVE  = 1u<<22,      /* n_colour_env_create().._gut() cycle */

   PS_ERRORS_PROMPT  = 1u<<24,      /* New error is to be reported in prompt */
   /* Various first-time-init switches */
   PS_ERRORS_NOTED   = 1u<<25,      /* Ring of `errors' advisory */
   PS_t_FLAG         = 1u<<26,      /* OPT_t_FLAG made persistant */
   PS_TERMCAP_DISABLE = 1u<<27,     /* HAVE_TERMCAP: *termcap-disable* was set */
   PS_TERMCAP_CA_MODE = 1u<<28,     /* HAVE_TERMCAP: ca_mode available & used */
   PS_LINE_EDITOR_INIT = 1u<<29     /* MLE is initialized */
};

/* A large enum with all the boolean and value options a.k.a their keys.
 * Only the constant keys are in here, to be looked up via ok_[bv]look(),
 * ok_[bv]set() and ok_[bv]clear().
 * Notes:
 * - see the comments in accmacvar.c before changing *anything* in here!
 * - virt= implies rdonly,nodel
 * - import= implies env
 * - defval= implies notempty
 * - num and posnum are mutual exclusive
 * - most default VAL_ues come from in from build system via ./make.rc
 * (Keep in SYNC: ./nail.h:okeys, ./nail.rc, ./nail.1:"Initial settings") */
enum okeys {
   /* This is used for all macro arguments etc., i.e., [*@#]|[1-9][0-9]*... */
   ok_v___special_param,               /* {nolopts=1,rdonly=1,nodel=1} */
   /* xxx __qm a.k.a. ? should be num=1 but that more expensive than what now */
   ok_v___qm,                          /* {name=?,nolopts=1,rdonly=1,nodel=1} */

   ok_v__account,                      /* {nolopts=1,rdonly=1,nodel=1} */
   ok_b_add_file_recipients,
ok_v_agent_shell_lookup,
   ok_b_allnet,
   ok_b_append,
   ok_b_ask,
   ok_b_askatend,
   ok_b_askattach,
   ok_b_askbcc,
   ok_b_askcc,
   ok_b_asksign,
   ok_b_asksub,                        /* {i3val=TRU1} */
   ok_v_attrlist,
   ok_v_autobcc,
   ok_v_autocc,
   ok_b_autocollapse,
   ok_b_autoprint,
ok_b_autothread,
   ok_v_autosort,

   ok_b_bang,
   ok_v_batch_exit_on_error,           /* {posnum=1} */
   ok_v_bind_timeout,                  /* {notempty=1,posnum=1} */
   ok_b_bsdannounce,
   ok_b_bsdcompat,
   ok_b_bsdflags,
   ok_b_bsdheadline,
   ok_b_bsdmsgs,
   ok_b_bsdorder,

   ok_v_COLUMNS,                       /* {notempty=1,posnum=1,env=1} */
   ok_v_charset_7bit,                  /* {lower=1,defval=CHARSET_7BIT} */
   /* But unused without HAVE_ICONV, we use ok_vlook(CHARSET_8BIT_OKEY)! */
   ok_v_charset_8bit,                  /* {lower=1,defval=CHARSET_8BIT} */
   ok_v_charset_unknown_8bit,          /* {lower=1} */
   ok_v_cmd,
   ok_b_colour_disable,
   ok_b_colour_pager,
   ok_v_crt,                           /* {posnum=1} */
   ok_v_customhdr,                     /* {nocntrls=1} */

   /* TODO likely temporary hook data, v15 drop */
   ok_v_compose_from,                  /* {rdonly=1} */
   ok_v_compose_sender,                /* {rdonly=1} */
   ok_v_compose_to,                    /* {rdonly=1} */
   ok_v_compose_cc,                    /* {rdonly=1} */
   ok_v_compose_bcc,                   /* {rdonly=1} */
   ok_v_compose_subject,               /* {rdonly=1} */

   ok_v_DEAD,                          /* {notempty=1,env=1,defval=VAL_DEAD} */
   ok_v_datefield,
   ok_v_datefield_markout_older,
   ok_b_debug,                         /* {vip=1} */
   ok_b_disposition_notification_send,
   ok_b_dot,
   ok_b_dotlock_ignore_error,

   ok_v_EDITOR,                        /* {env=1,defval=VAL_EDITOR} */
   ok_b_editalong,
   ok_b_editheaders,
   ok_b_emptystart,
   ok_v_encoding,
   ok_v_escape,
   ok_v_expandaddr,
   ok_v_expandargv,

   ok_v_features,                      /* {virt=VAL_FEATURES} */
   ok_b_flipr,
   ok_v_folder,                        /* {vip=1} */
   ok_v__folder_resolved,              /* {rdonly=1,nodel=1} */
   ok_v_folder_hook,
   ok_b_followup_to,
   ok_v_followup_to_honour,
   ok_b_forward_as_attachment,
   ok_v_from,
   ok_b_fullnames,
   ok_v_fwdheading,

   ok_v_HOME,                          /* {vip=1,nodel=1,import=1} */
   ok_b_header,                        /* {vip=1,i3val=TRU1} */
   ok_v_headline,
   ok_v_headline_bidi,
   ok_v_history_file,
   ok_b_history_gabby,
   ok_b_history_gabby_persist,
   ok_v_history_size,                  /* {notempty=1,num=1} */
   ok_b_hold,
   ok_v_hostname,

   ok_b_idna_disable,
   ok_b_ignore,
   ok_b_ignoreeof,
   ok_v_inbox,
   ok_v_indentprefix,

   ok_b_keep,
   ok_b_keep_content_length,
   ok_b_keepsave,

   ok_v_LINES,                         /* {notempty=1,posnum=1,env=1} */
   ok_v_LISTER,                        /* {env=1,defval=VAL_LISTER} */
   ok_v_LOGNAME,                       /* {rdonly=1,import=1} */
   ok_b_line_editor_disable,
   ok_b_line_editor_no_defaults,

   ok_v_MAIL,                          /* {env=1} */
   ok_v_MAILRC,                        /* {import=1,defval=VAL_MAILRC} */
   ok_b_MAILX_NO_SYSTEM_RC,            /* {name=MAILX_NO_SYSTEM_RC,import=1} */
   ok_v_MBOX,                          /* {env=1,defval=VAL_MBOX} */
   ok_v__mailbox_resolved,             /* {nolopts=1,rdonly=1,nodel=1} */
   ok_v__mailbox_display,              /* {nolopts=1,rdonly=1,nodel=1} */
   ok_v_mailx_extra_rc,
   ok_b_markanswered,
   ok_b_mbox_rfc4155,
   ok_b_memdebug,                      /* {vip=1} */
   ok_b_message_id_disable,
   ok_v_message_inject_head,
   ok_v_message_inject_tail,
   ok_b_metoo,
   ok_b_mime_allow_text_controls,
   ok_b_mime_alternative_favour_rich,
   ok_v_mime_counter_evidence,         /* {posnum=1} */
   ok_v_mimetypes_load_control,
   /* TODO: v15 (not yet due to <-> sendmail): {defval=VAL_MTA} */
   ok_v_mta,
   ok_v_mta_arguments,
   ok_b_mta_no_default_arguments,
    /* TODO v15: (not yet due to <-> sendmail-progname {defval=VAL_MTA_ARGV0} */
   ok_v_mta_argv0,

ok_v_NAIL_EXTRA_RC,                 /* {name=NAIL_EXTRA_RC} */
ok_b_NAIL_NO_SYSTEM_RC,             /* {name=NAIL_NO_SYSTEM_RC,import=1} */
ok_v_NAIL_HEAD,                     /* {name=NAIL_HEAD} */
ok_v_NAIL_HISTFILE,                 /* {name=NAIL_HISTFILE} */
ok_v_NAIL_HISTSIZE,                 /* {name=NAIL_HISTSIZE,notempty=1,num=1} */
ok_v_NAIL_TAIL,                     /* {name=NAIL_TAIL} */
   ok_v_NETRC,                      /* {env=1,defval=VAL_NETRC} */
   ok_b_netrc_lookup,
   ok_v_netrc_pipe,
   ok_v_newfolders,
   ok_v_newmail,

   ok_v_on_compose_done,               /* {notempty=1} */
   ok_v_on_compose_done_shell,         /* {notempty=1} */
   ok_v_on_compose_enter,              /* {notempty=1} */
   ok_v_on_compose_leave,              /* {notempty=1} */
   ok_b_outfolder,

   ok_v_PAGER,                         /* {env=1,defval=VAL_PAGER} */
   ok_v_PATH,                          /* {nodel=1,import=1} */
   ok_b_POSIXLY_CORRECT,            /* {vip=1,import=1,name=POSIXLY_CORRECT} */
   ok_b_page,
   ok_v_password,
   ok_b_piperaw,
   ok_v_pop3_auth,
   ok_b_pop3_bulk_load,
   ok_v_pop3_keepalive,
   ok_b_pop3_no_apop,
   ok_b_pop3_use_starttls,
   ok_b_posix,                         /* {vip=1} */
   ok_b_print_alternatives,
   ok_v_prompt,                        /* {i3val="? "} */
   ok_v_prompt2,                       /* {i3val=".. "} */

   ok_b_quiet,
   ok_v_quote,
   ok_b_quote_as_attachment,
   ok_v_quote_fold,

   ok_b_r_option_implicit,
   ok_b_recipients_in_cc,
   ok_v_record,
   ok_b_record_resent,
   ok_b_reply_in_same_charset,
   ok_v_reply_strings,
   ok_v_replyto,
   ok_v_reply_to_honour,
   ok_b_rfc822_body_from_,             /* {name=rfc822-body-from_} */

   ok_v_SHELL,                         /* {import=1,defval=VAL_SHELL} */
ok_b_SYSV3,                         /* {env=1} */
   ok_b_save,                          /* {i3val=TRU1} */
   ok_v_screen,                        /* {notempty=1,posnum=1} */
   ok_b_searchheaders,
   ok_v_sendcharsets,                  /* {lower=1} */
   ok_b_sendcharsets_else_ttycharset,
   ok_v_sender,
ok_v_sendmail,
ok_v_sendmail_arguments,
ok_b_sendmail_no_default_arguments,
ok_v_sendmail_progname,
   ok_b_sendwait,
   ok_b_showlast,
   ok_b_showname,
   ok_b_showto,
   ok_v_Sign,
   ok_v_sign,
   ok_v_signature,
   ok_b_skipemptybody,                 /* {vip=1} */
   ok_v_smime_ca_dir,
   ok_v_smime_ca_file,
   ok_v_smime_cipher,
   ok_v_smime_crl_dir,
   ok_v_smime_crl_file,
   /* smime-encrypt-USER@HOST */
   ok_b_smime_force_encryption,
   ok_b_smime_no_default_ca,
   ok_b_smime_sign,
   ok_v_smime_sign_cert,
   ok_v_smime_sign_include_certs,
   ok_v_smime_sign_message_digest,
ok_v_smtp,
   ok_v_smtp_auth,
ok_v_smtp_auth_password,
ok_v_smtp_auth_user,
   ok_v_smtp_hostname,
   ok_b_smtp_use_starttls,
   ok_v_SOURCE_DATE_EPOCH,/*{name=SOURCE_DATE_EPOCH,env=1,notempty=1,posnum=1}*/
   ok_v_spam_interface,
   ok_v_spam_maxsize,                  /* {notempty=1,posnum=1} */
   ok_v_spamc_command,
   ok_v_spamc_arguments,
   ok_v_spamc_user,
ok_v_spamd_socket,
ok_v_spamd_user,
   ok_v_spamfilter_ham,
   ok_v_spamfilter_noham,
   ok_v_spamfilter_nospam,
   ok_v_spamfilter_rate,
   ok_v_spamfilter_rate_scanscore,
   ok_v_spamfilter_spam,
   ok_v_ssl_ca_dir,
   ok_v_ssl_ca_file,
   ok_v_ssl_cert,
   ok_v_ssl_cipher_list,
   ok_v_ssl_config_file,
   ok_v_ssl_crl_dir,
   ok_v_ssl_crl_file,
   ok_v_ssl_key,
   ok_v_ssl_method,
   ok_b_ssl_no_default_ca,
   ok_v_ssl_protocol,
   ok_v_ssl_rand_egd,
   ok_v_ssl_rand_file,
   ok_v_ssl_verify,
   ok_v_stealthmua,

   ok_v_TERM,                          /* {env=1} */
   ok_v_TMPDIR,                  /* {import=1,notempty=1,defval=VAL_TMPDIR} */
   ok_v_termcap,
   ok_b_termcap_disable,
   ok_v_toplines,                      /* {notempty=1,num=1,defval="5"} */
   ok_b_topsqueeze,
   ok_v_ttycharset,                    /* {lower=1,defval=CHARSET_8BIT} */
   ok_b_typescript_mode,               /* {vip=1} */

   ok_v_USER,                          /* {rdonly=1,import=1} */
   ok_v_umask,                      /* {vip=1,nodel=1,posnum=1,i3val="0077"} */
   ok_v_user,

   ok_v_VISUAL,                        /* {env=1,defval=VAL_VISUAL} */
   ok_b_v15_compat,
   ok_b_verbose,                       /* {vip=1} */
   ok_v_version,                       /* {virt=VERSION} */
   ok_v_version_major,                 /* {virt=VERSION_MAJOR} */
   ok_v_version_minor,                 /* {virt=VERSION_MINOR} */
   ok_v_version_update,                /* {virt=VERSION_UPDATE} */

   ok_b_writebackedited
};

/* Locale-independent character classes */
enum {
   C_CNTRL        = 1<<0,
   C_BLANK        = 1<<1,
   C_WHITE        = 1<<2,
   C_SPACE        = 1<<3,
   C_PUNCT        = 1<<4,
   C_OCTAL        = 1<<5,
   C_DIGIT        = 1<<6,
   C_UPPER        = 1<<7,
   C_LOWER        = 1<<8
};

struct str {
   char     *s;      /* the string's content */
   size_t   l;       /* the stings's length */
};

struct n_string{
   char *s_dat;         /*@ May contain NULs, not automatically terminated */
   ui32_t s_len;        /*@ gth of string */
#ifdef HAVE_BYTE_ORDER_LITTLE
   ui32_t s_auto : 1;   /* Stored in auto-reclaimed storage? */
#endif
   ui32_t s_size : 31;  /* of .s_dat, -1 */
#ifndef HAVE_BYTE_ORDER_LITTLE
   ui32_t s_auto : 1;
#endif
};

struct n_strlist{
   struct n_strlist *sl_next;
   size_t sl_len;
   char sl_dat[n_VFIELD_SIZE(0)];
};
#define n_STRLIST_MALLOC(SZ) /* XXX -> nailfuns.h (and pimp interface) */\
   smalloc(n_VSTRUCT_SIZEOF(struct n_strlist, sl_dat) + (SZ) +1)

struct bidi_info {
   struct str  bi_start;      /* Start of (possibly) bidirectional text */
   struct str  bi_end;        /* End of ... */
   size_t      bi_pad;        /* No of visual columns to reserve for BIDI pad */
};

struct n_cmd_arg_desc{
   char cad_name[12];   /* Name of command */
   ui32_t cad_no;       /* Number of entries in cad_ent_flags */
   /* [enum n_cmd_arg_desc_flags,arg-dep] */
   ui32_t cad_ent_flags[n_VFIELD_SIZE(0)][2];
};
/* ISO C(99) doesn't allow initialization of "flex array" */
#define n_CMD_ARG_DESC_SUBCLASS_DEF(CMD,NO,VAR) \
   struct n_cmd_arg_desc_ ## CMD {\
      char cad_name[12];\
      ui32_t cad_no;\
      ui32_t cad_ent_flags[NO][2];\
   } const VAR = { #CMD, NO,
#define n_CMD_ARG_DESC_SUBCLASS_DEF_END }
#define n_CMD_ARG_DESC_SUBCLASS_CAST(P) ((struct n_cmd_arg_desc const*)P)

struct n_cmd_arg_ctx{
   struct n_cmd_arg_desc const *cac_desc;
   char const *cac_indat;     /* Input that shall be parsed */
   size_t cac_inlen;          /* Input length (UIZ_MAX: do a strlen()) */
   size_t cac_no;             /* Output: number of parsed arguments */
   struct n_cmd_arg *cac_arg; /* Output: parsed arguments */
};

struct n_cmd_arg{/* TODO incomplete, misses getmsglist() */
   struct n_cmd_arg *ca_next;
   char const *ca_indat;   /* Pointer into n_cmd_arg_ctx.cac_indat */
   size_t ca_inlen;        /* of .ca_indat of this arg (not terminated) */
   ui32_t ca_ent_flags[2]; /* Copy of n_cmd_arg_desc.cad_ent_flags[X] */
   ui32_t ca_arg_flags;    /* [Output: _WYSH: copy of parse result flags] */
   ui8_t ca__dummy[4];
   union{
      struct str ca_str;      /* _STRING, _WYSH */
   } ca_arg;               /* Output: parsed result */
};

#ifdef HAVE_COLOUR
struct n_colour_pen;
#endif

struct url {
   char const     *url_input;       /* Input as given (really) */
   enum cproto    url_cproto;       /* Communication protocol as given */
   bool_t         url_needs_tls;    /* Whether the protocol uses SSL/TLS */
   bool_t         url_had_user;     /* Whether .url_user was part of the URL */
   ui16_t         url_portno;       /* atoi .url_port or default, host endian */
   char const     *url_port;        /* Port (if given) or NULL */
   char           url_proto[14];    /* Communication protocol as 'xy\0//' */
   ui8_t          url_proto_len;    /* Length of .url_proto ('\0' index) */
   ui8_t          url_proto_xlen;   /* .. if '\0' is replaced with ':' */
   struct str     url_user;         /* User, exactly as given / looked up */
   struct str     url_user_enc;     /* User, urlxenc()oded */
   struct str     url_pass;         /* Pass (urlxdec()oded) or NULL */
   struct str     url_host;         /* Service hostname */
   struct str     url_path;         /* Path suffix or NULL */
   /* TODO: url_get_component(url *, enum COMPONENT, str *store) */
   struct str     url_h_p;          /* .url_host[:.url_port] */
   /* .url_user@.url_host
    * Note: for CPROTO_SMTP this may resolve HOST via *smtp-hostname* (->
    * *hostname*)!  (And may later be overwritten according to *from*!) */
   struct str     url_u_h;
   struct str     url_u_h_p;        /* .url_user@.url_host[:.url_port] */
   struct str     url_eu_h_p;       /* .url_user_enc@.url_host[:.url_port] */
   char const     *url_p_u_h_p;     /* .url_proto://.url_u_h_p */
   char const     *url_p_eu_h_p;    /* .url_proto://.url_eu_h_p */
   char const     *url_p_eu_h_p_p;  /* .url_proto://.url_eu_h_p[/.url_path] */
};

struct ccred {
   enum cproto    cc_cproto;     /* Communication protocol */
   enum authtype  cc_authtype;   /* Desired authentication */
   char const     *cc_auth;      /* Authentication type as string */
   struct str     cc_user;       /* User (urlxdec()oded) or NULL */
   struct str     cc_pass;       /* Password (urlxdec()oded) or NULL */
};

#ifdef HAVE_DOTLOCK
struct n_dotlock_info{
   char const *di_file_name;  /* Mailbox to lock */
   char const *di_lock_name;  /* .di_file_name + .lock */
   char const *di_hostname;   /* ..filled in parent (due resolver delays) */
   char const *di_randstr;    /* ..ditto, random string */
   size_t di_pollmsecs;       /* Delay in between locking attempts */
   struct stat *di_stb;
};
#endif

/* Execution context bundles  */
struct n_exec_ctx{

   void *l_smem;                 /* salloc() memory TODO -> memraw? */
   /* TODO our new per-exec-ctx memory "allocators" are yet very dumb.
    * TODO for v15 those should not be wrapping nodes but real allocators */
   struct n_mem_raw *l_memraw;   /* n_mem_alloc() (/ n_mem_free()) */
   struct n_mem_wrap *l_memwrap; /* n_mem_wrap() (/ n_mem_unwrap()) */
};

struct n_mem_raw{
   struct n_mem_raw *mr_prev;
   struct n_mem_raw *mr_next;
   char mr_buf[n_VFIELD_SIZE(0)];
};

struct n_mem_wrap{
   struct n_mem_wrap *mw_prev;
   struct n_mem_wrap *mw_next;
   void *mw_obj;
   void (*mw_dtor)(void *obj);
};

struct mime_handler {
   enum mime_handler_flags mh_flags;
   struct str  mh_msg;           /* Message describing this command */
   /* XXX union{} the following? */
   char const  *mh_shell_cmd;    /* For MIME_HDL_CMD */
   int         (*mh_ptf)(void);  /* PTF main() for MIME_HDL_PTF */
};

struct quoteflt {
   FILE        *qf_os;        /* Output stream */
   char const  *qf_pfix;
   ui32_t      qf_pfix_len;   /* Length of prefix: 0: bypass */
   ui32_t      qf_qfold_min;  /* Simple way: wrote prefix? */
   /* TODO quoteflt.qf_nl_last is a hack that i have introduced so that we
    * TODO finally can gracefully place a newline last in the visual display.
    * TODO I.e., for cases where quoteflt shouldn't be used at all ;} */
   bool_t      qf_nl_last;    /* Last thing written/seen was NL */
#ifndef HAVE_QUOTE_FOLD
   ui8_t       __dummy[7];
#else
   ui8_t       __dummy[1];
   ui8_t       qf_state;      /* *quote-fold* state machine */
   bool_t      qf_brk_isws;   /* Breakpoint is at WS */
   ui32_t      qf_qfold_max;  /* Otherwise: line lengths */
   ui32_t      qf_wscnt;      /* Whitespace count */
   ui32_t      qf_brkl;       /* Breakpoint */
   ui32_t      qf_brkw;       /* Visual width, breakpoint */
   ui32_t      qf_datw;       /* Current visual output line width */
   struct str  qf_dat;        /* Current visual output line */
   struct str  qf_currq;      /* Current quote, compressed */
   mbstate_t   qf_mbps[2];
#endif
};

#ifdef HAVE_FILTER_HTML_TAGSOUP
struct htmlflt {
   FILE        *hf_os;        /* Output stream */
   ui32_t      hf_flags;
   ui32_t      hf_lmax;       /* Maximum byte +1 in .hf_line/4 */
   ui32_t      hf_len;        /* Current bytes in .hf_line */
   ui32_t      hf_last_ws;    /* Last whitespace on line (fold purposes) */
   ui32_t      hf_mboff;      /* Last offset for "mbtowc" */
   ui32_t      hf_mbwidth;    /* We count characters not bytes if possible */
   char        *hf_line;      /* Output line buffer - MUST be last field! */
   si32_t      hf_href_dist;  /* Count of lines since last HREF flush */
   ui32_t      hf_href_no;    /* HREF sequence number */
   struct htmlflt_href *hf_hrefs;
   struct htmlflt_tag const *hf_ign_tag; /* Tag that will end ignore mode */
   char        *hf_curr;      /* Current cursor into .hf_bdat */
   char        *hf_bmax;      /* Maximum byte in .hf_bdat +1 */
   char        *hf_bdat;      /* (Temporary) Tag content data storage */
};
#endif

struct search_expr {
   char const  *ss_where;     /* ..to search for the expr. (not always used) */
   char const  *ss_sexpr;     /* String search expr.; NULL: use .ss_regex */
#ifdef HAVE_REGEX
   regex_t     ss_regex;
#endif
};

/* This is somewhat temporary for pre v15 */
struct n_sigman{
   ui32_t sm_flags;           /* enum n_sigman_flags */
   int sm_signo;
   struct n_sigman *sm_outer;
   sighandler_type sm_ohup;
   sighandler_type sm_oint;
   sighandler_type sm_oquit;
   sighandler_type sm_oterm;
   sighandler_type sm_opipe;
   sigjmp_buf sm_jump;
};

struct termios_state {
   struct termios ts_tios;
   char        *ts_linebuf;
   size_t      ts_linesize;
   bool_t      ts_needs_reset;
};

#define termios_state_reset() \
do {\
   if (termios_state.ts_needs_reset) {\
      tcsetattr(STDIN_FILENO, TCSADRAIN, &termios_state.ts_tios);\
      termios_state.ts_needs_reset = FAL0;\
   }\
} while (0)

#ifdef n_HAVE_TCAP
struct n_termcap_value{
   enum n_termcap_captype tv_captype;
   ui8_t tv__dummy[4];
   union n_termcap_value_data{
      bool_t tvd_bool;
      ui32_t tvd_numeric;
      char const *tvd_string;
   } tv_data;
};
#endif

struct n_visual_info_ctx{
   char const *vic_indat;  /*I Input data */
   size_t vic_inlen;       /*I If UIZ_MAX, strlen(.vic_indat) */
   char const *vic_oudat;  /*O remains */
   size_t vic_oulen;
   size_t vic_chars_seen;  /*O number of characters processed */
   size_t vic_bytes_seen;  /*O number of bytes passed */
   size_t vic_vi_width;    /*[O] visual width of the entire range */
   wc_t *vic_woudat;       /*[O] if so requested */
   size_t vic_woulen;      /*[O] entries in .vic_woudat, if used */
   wc_t vic_waccu;         /*O The last wchar_t/char processed (if any) */
   enum n_visual_info_flags vic_flags; /*O Copy of parse flags */
   /* TODO bidi */
#ifdef HAVE_C90AMEND1
   mbstate_t *vic_mbstate; /*IO .vic_mbs_def used if NULL */
   mbstate_t vic_mbs_def;
#endif
};

struct time_current {
   time_t      tc_time;
   struct tm   tc_gm;
   struct tm   tc_local;
   char        tc_ctime[32];
};

struct sock {                 /* data associated with a socket */
   int         s_fd;          /* file descriptor */
#ifdef HAVE_SSL
   int         s_use_ssl;     /* SSL is used */
# ifdef HAVE_XSSL
   void        *s_ssl;        /* SSL object */
# endif
#endif
   char        *s_wbuf;       /* for buffered writes */
   int         s_wbufsize;    /* allocated size of s_buf */
   int         s_wbufpos;     /* position of first empty data byte */
   char        *s_rbufptr;    /* read pointer to s_rbuf */
   int         s_rsz;         /* size of last read in s_rbuf */
   char const  *s_desc;       /* description of error messages */
   void        (*s_onclose)(void);     /* execute on close */
   char        s_rbuf[LINESIZE + 1];   /* for buffered reads */
};

struct sockconn {
   struct url     sc_url;
   struct ccred   sc_cred;
   struct sock    sc_sock;
};

struct mailbox {
   enum {
      MB_NONE     = 000,      /* no reply expected */
      MB_COMD     = 001,      /* command reply expected */
      MB_MULT     = 002,      /* multiline reply expected */
      MB_PREAUTH  = 004,      /* not in authenticated state */
      MB_BYE      = 010,      /* may accept a BYE state */
      MB_FROM__WARNED = 1<<4  /* MBOX with invalid from seen & logged */
   }           mb_active;
   FILE        *mb_itf;       /* temp file with messages, read open */
   FILE        *mb_otf;       /* same, write open */
   char        *mb_sorted;    /* sort method */
   enum {
      MB_VOID,       /* no type (e. g. connection failed) */
      MB_FILE,       /* local file */
      MB_POP3,       /* POP3 mailbox */
      MB_MAILDIR     /* maildir folder */
   }           mb_type;       /* type of mailbox */
   enum {
      MB_DELE = 01,  /* may delete messages in mailbox */
      MB_EDIT = 02   /* may edit messages in mailbox */
   }           mb_perm;
   int mb_threaded;           /* mailbox has been threaded */
   struct sock mb_sock;       /* socket structure */
};

enum needspec {
   NEED_UNSPEC,      /* unspecified need, don't fetch */
   NEED_HEADER,      /* need the header of a message */
   NEED_BODY         /* need header and body of a message */
};

enum content_info {
   CI_NOTHING,                /* Nothing downloaded yet */
   CI_HAVE_HEADER = 1<<0,     /* Header is downloaded */
   CI_HAVE_BODY   = 1<<1,     /* Entire message is downloaded */
   CI_MIME_ERRORS = 1<<2,     /* Defective MIME structure */
   CI_EXPANDED    = 1<<3,     /* Container part (pk7m) exploded into X */
   CI_SIGNED      = 1<<4,     /* Has a signature.. */
   CI_SIGNED_OK   = 1<<5,     /* ..verified ok.. */
   CI_SIGNED_BAD  = 1<<6,     /* ..verified bad (missing key).. */
   CI_ENCRYPTED   = 1<<7,     /* Is encrypted.. */
   CI_ENCRYPTED_OK = 1<<8,    /* ..decryption possible/ok.. */
   CI_ENCRYPTED_BAD = 1<<9    /* ..not possible/ok */
};

/* flag bits. Attention: Flags that are used in cache.c may not change */
enum mflag {
   MUSED          = (1<< 0),  /* entry is used, but this bit isn't */
   MDELETED       = (1<< 1),  /* entry has been deleted */
   MSAVED         = (1<< 2),  /* entry has been saved */
   MTOUCH         = (1<< 3),  /* entry has been noticed */
   MPRESERVE      = (1<< 4),  /* keep entry in sys mailbox */
   MMARK          = (1<< 5),  /* message is marked! */
   MODIFY         = (1<< 6),  /* message has been modified */
   MNEW           = (1<< 7),  /* message has never been seen */
   MREAD          = (1<< 8),  /* message has been read sometime. */
   MSTATUS        = (1<< 9),  /* message status has changed */
   MBOX           = (1<<10),  /* Send this to mbox, regardless */
   MNOFROM        = (1<<11),  /* no From line */
   MHIDDEN        = (1<<12),  /* message is hidden to user */
   MBOXED         = (1<<13),  /* message has been sent to mbox */
   MNEWEST        = (1<<14),  /* message is very new (newmail) */
   MFLAG          = (1<<15),  /* message has been flagged recently */
   MUNFLAG        = (1<<16),  /* message has been unflagged */
   MFLAGGED       = (1<<17),  /* message is `flagged' */
   MANSWER        = (1<<18),  /* message has been answered recently */
   MUNANSWER      = (1<<19),  /* message has been unanswered */
   MANSWERED      = (1<<20),  /* message is `answered' */
   MDRAFT         = (1<<21),  /* message has been drafted recently */
   MUNDRAFT       = (1<<22),  /* message has been undrafted */
   MDRAFTED       = (1<<23),  /* message is marked as `draft' */
   MOLDMARK       = (1<<24),  /* messages was marked previously */
   MSPAM          = (1<<25),  /* message is classified as spam */
   MSPAMUNSURE    = (1<<26)   /* message may be spam, but it is unsure */
};
#define MMNORM          (MDELETED | MSAVED | MHIDDEN)
#define MMNDEL          (MDELETED | MHIDDEN)

#define visible(mp)     (((mp)->m_flag & MMNDEL) == 0)

struct mimepart {
   enum mflag  m_flag;
   enum content_info m_content_info;
#ifdef HAVE_SPAM
   ui32_t      m_spamscore;   /* Spam score as int, 24:8 bits */
#endif
   int         m_block;       /* block number of this part */
   size_t      m_offset;      /* offset in block of part */
   size_t      m_size;        /* Bytes in the part */
   size_t      m_xsize;       /* Bytes in the full part */
   long        m_lines;       /* Lines in the message: write format! */
   long        m_xlines;      /* Lines in the full message; ditto */
   time_t      m_time;        /* time the message was sent */
   char const  *m_from;       /* message sender */
   struct mimepart *m_nextpart;     /* next part at same level */
   struct mimepart *m_multipart;    /* parts of multipart */
   struct mimepart *m_parent;       /* enclosing multipart part */
   char const  *m_ct_type;          /* content-type */
   char const  *m_ct_type_plain;    /* content-type without specs */
   char const  *m_ct_type_usr_ovwr; /* Forcefully overwritten one */
   enum mimecontent m_mimecontent;  /* ..in enum */
   char const  *m_charset;
   char const  *m_ct_enc;           /* Content-Transfer-Encoding */
   enum mime_enc m_mime_enc;        /* ..in enum */
   char        *m_partstring;       /* Part level string */
   char        *m_filename;         /* ..of attachment */
   char const  *m_content_description;
   struct mime_handler *m_handler;  /* MIME handler if yet classified */
};

struct message {
   enum mflag  m_flag;        /* flags */
   enum content_info m_content_info;
#ifdef HAVE_SPAM
   ui32_t      m_spamscore;   /* Spam score as int, 24:8 bits */
#endif
   int         m_block;       /* block number of this message */
   size_t      m_offset;      /* offset in block of message */
   size_t      m_size;        /* Bytes in the message */
   size_t      m_xsize;       /* Bytes in the full message */
   long        m_lines;       /* Lines in the message */
   long        m_xlines;      /* Lines in the full message */
   time_t      m_time;        /* time the message was sent */
   time_t      m_date;        /* time in the 'Date' field */
   unsigned    m_idhash;      /* hash on Message-ID for threads */
   struct message *m_child;   /* first child of this message */
   struct message *m_younger; /* younger brother of this message */
   struct message *m_elder;   /* elder brother of this message */
   struct message *m_parent;  /* parent of this message */
   unsigned    m_level;       /* thread level of message */
   long        m_threadpos;   /* position in threaded display */
   char        *m_maildir_file;  /* original maildir file of msg */
   ui32_t      m_maildir_hash;   /* hash of file name in maildir sub */
   int         m_collapsed;      /* collapsed thread information */
};

/* Given a file address, determine the block number it represents */
#define mailx_blockof(off)                ((int) ((off) / 4096))
#define mailx_offsetof(off)               ((int) ((off) % 4096))
#define mailx_positionof(block, offset)   ((off_t)(block) * 4096 + (offset))

/* Argument types */
enum argtype {
   ARG_MSGLIST    = 0,        /* Message list type */
   ARG_STRLIST    = 1,        /* A pure string */
   ARG_RAWLIST    = 2,        /* getrawlist(), old style */
   ARG_NOLIST     = 3,        /* Just plain 0 */
   ARG_NDMLIST    = 4,        /* Message list, no defaults */
   ARG_WYSHLIST   = 5,        /* getrawlist(), sh(1) compatible */
     ARG_WYRALIST = 6,        /* _RAWLIST or _WYSHLIST (with `wysh') */
   ARG_ARGMASK    = 7,        /* Mask of the above */

   ARG_A          = 1u<< 4,   /* Needs an active mailbox */
   ARG_F          = 1u<< 5,   /* Is a conditional command */
   ARG_G          = 1u<< 6,   /* Is supposed to produce "gabby" history */
   ARG_H          = 1u<< 7,   /* Never place in `history' */
   ARG_I          = 1u<< 8,   /* Interactive command bit */
   ARG_M          = 1u<< 9,   /* Legal from send mode bit */
   ARG_O          = 1u<<10,   /* OBSOLETE()d command */
   ARG_P          = 1u<<11,   /* Autoprint dot after command */
   ARG_R          = 1u<<12,   /* Cannot be called in compose mode recursion */
   ARG_S          = 1u<<13,   /* Cannot be called unless PS_STARTED (POSIX) */
   ARG_T          = 1u<<14,   /* Is a transparent command */
   ARG_V          = 1u<<15,   /* Places data in temporary_arg_v_store */
   ARG_W          = 1u<<16,   /* Invalid when read only bit */
   ARG_X          = 1u<<17    /* Valid command in PS_COMPOSE_FORKHOOK mode */
};

enum gfield {
   GTO            = 1<< 0,    /* Grab To: line */
   GSUBJECT       = 1<< 1,    /* Likewise, Subject: line */
   GCC            = 1<< 2,    /* And the Cc: line */
   GBCC           = 1<< 3,    /* And also the Bcc: line */

   GNL            = 1<< 4,    /* Print blank line after */
   GDEL           = 1<< 5,    /* Entity removed from list */
   GCOMMA         = 1<< 6,    /* detract() puts in commas */
   GUA            = 1<< 7,    /* User-Agent field */
   GMIME          = 1<< 8,    /* MIME 1.0 fields */
   GMSGID         = 1<< 9,    /* a Message-ID */
   GNAMEONLY      = 1<<10,    /* detract() does NOT use fullnames */

   GIDENT         = 1<<11,    /* From:, Reply-To:, MFT: (user headers) */
   GREF           = 1<<12,    /* References:, In-Reply-To:, (Message-ID:) */
   GDATE          = 1<<13,    /* Date: field */
   GFULL          = 1<<14,    /* Include full names, comments etc. */
   GSKIN          = 1<<15,    /* Skin names */
   GEXTRA         = 1<<16,    /* Extra fields (mostly like GIDENT XXX) */
   GFILES         = 1<<17,    /* Include filename and pipe addresses */
   GFULLEXTRA     = 1<<18     /* Only with GFULL: GFULL less address */
};
#define GMASK           (GTO | GSUBJECT | GCC | GBCC)

enum header_flags {
   HF_NONE        = 0,
   HF_LIST_REPLY  = 1<< 0,
   HF_MFT_SENDER  = 1<< 1,    /* Add ourselves to Mail-Followup-To: */
   HF_RECIPIENT_RECORD = 1<<10, /* Save message in file named after rec. */
   HF__NEXT_SHIFT = 11
};

/* Structure used to pass about the current state of a message (header) */
struct n_header_field{
   struct n_header_field *hf_next;
   ui32_t hf_nl;              /* Field-name length */
   ui32_t hf_bl;              /* Field-body length*/
   char hf_dat[n_VFIELD_SIZE(0)];
};

struct header {
   ui32_t      h_flags;       /* enum header_flags bits */
   ui32_t      h_dummy;
   struct name *h_to;         /* Dynamic "To:" string */
   char        *h_subject;    /* Subject string */
   struct name *h_cc;         /* Carbon copies string */
   struct name *h_bcc;        /* Blind carbon copies */
   struct name *h_ref;        /* References (possibly overridden) */
   struct attachment *h_attach; /* MIME attachments */
   char        *h_charset;    /* preferred charset */
   struct name *h_from;       /* overridden "From:" field */
   struct name *h_sender;     /* overridden "Sender:" field */
   struct name *h_replyto;    /* overridden "Reply-To:" field */
   struct name *h_message_id; /* overridden "Message-ID:" field */
   struct name *h_in_reply_to;/* overridden "In-Reply-To:" field */
   struct name *h_mft;        /* Mail-Followup-To */
   char const  *h_list_post;  /* Address from List-Post:, for `Lreply' */
   struct n_header_field *h_user_headers;
   struct n_header_field *h_custom_headers; /* (Cached result) */
};

/* Handling of namelist nodes used in processing the recipients of mail and
 * aliases, inspection of mail-addresses and all that kind of stuff */
enum nameflags {
   NAME_NAME_SALLOC        = 1<< 0, /* .n_name is doped */
   NAME_FULLNAME_SALLOC    = 1<< 1, /* .n_fullname is doped */
   NAME_SKINNED            = 1<< 2, /* Is actually skin()ned */
   NAME_IDNA               = 1<< 3, /* IDNA was applied */

   NAME_ADDRSPEC_CHECKED   = 1<< 4, /* Address has been .. and */
   NAME_ADDRSPEC_ISFILE    = 1<< 5, /* ..is a file path */
   NAME_ADDRSPEC_ISPIPE    = 1<< 6, /* ..is a command for piping */
   NAME_ADDRSPEC_ISFILEORPIPE = NAME_ADDRSPEC_ISFILE | NAME_ADDRSPEC_ISPIPE,
   NAME_ADDRSPEC_ISNAME    = 1<< 7, /* ..is a valid mail network address */
   NAME_ADDRSPEC_ISADDR    = 1<< 8, /* ..is a valid mail network address */

   NAME_ADDRSPEC_ERR_EMPTY = 1<< 9, /* An empty string (or NULL) */
   NAME_ADDRSPEC_ERR_ATSEQ = 1<<10, /* Weird @ sequence */
   NAME_ADDRSPEC_ERR_CHAR  = 1<<11, /* Invalid character */
   NAME_ADDRSPEC_ERR_IDNA  = 1<<12, /* IDNA convertion failed */
   NAME_ADDRSPEC_INVALID   = NAME_ADDRSPEC_ERR_EMPTY |
         NAME_ADDRSPEC_ERR_ATSEQ | NAME_ADDRSPEC_ERR_CHAR |
         NAME_ADDRSPEC_ERR_IDNA,

   /* Error storage (we must fit in 31-bit) */
   _NAME_SHIFTWC  = 13,
   _NAME_MAXWC    = 0x3FFFF,
   _NAME_MASKWC   = _NAME_MAXWC << _NAME_SHIFTWC
};

/* In the !_ERR_EMPTY case, the failing character can be queried */
#define NAME_ADDRSPEC_ERR_GETWC(F)  \
   ((((unsigned int)(F) & _NAME_MASKWC) >> _NAME_SHIFTWC) & _NAME_MAXWC)
#define NAME_ADDRSPEC_ERR_SET(F, E, WC) \
do {\
   (F) = ((F) & ~(NAME_ADDRSPEC_INVALID | _NAME_MASKWC)) |\
         (E) | (((unsigned int)(WC) & _NAME_MAXWC) << _NAME_SHIFTWC);\
} while (0)

struct name {
   struct name *n_flink;      /* Forward link in list. */
   struct name *n_blink;      /* Backward list link */
   enum gfield n_type;        /* From which list it came */
   enum nameflags n_flags;    /* enum nameflags */
   char        *n_name;       /* This fella's address */
   char        *n_fullname;   /* Ditto, unless GFULL including comment */
   char        *n_fullextra;  /* GFULL, without address */
};

struct n_addrguts{
   /* Input string as given (maybe replaced with a fixed one!) */
   char const *ag_input;
   size_t ag_ilen;            /* strlen() of input */
   size_t ag_iaddr_start;     /* Start of *addr-spec* in .ag_input */
   size_t ag_iaddr_aend;      /* ..and one past its end */
   char *ag_skinned;          /* Output (alloced if !=.ag_input) */
   size_t ag_slen;            /* strlen() of .ag_skinned */
   size_t ag_sdom_start;      /* Start of domain in .ag_skinned, */
   enum nameflags ag_n_flags; /* enum nameflags of .ag_skinned */
};

/* MIME attachments */
enum attach_conv {
   AC_DEFAULT,       /* _get_lc() -> _iter_*() */
   AC_FIX_INCS,      /* "charset=".a_input_charset (nocnv) */
   AC_TMPFILE        /* attachment.a_tmpf is converted */
};

enum n_attach_error{
   n_ATTACH_ERR_NONE,
   n_ATTACH_ERR_FILE_OPEN,
   n_ATTACH_ERR_ICONV_FAILED,
   n_ATTACH_ERR_ICONV_NAVAIL,
   n_ATTACH_ERR_OTHER
};

struct attachment {
   struct attachment *a_flink; /* Forward link in list. */
   struct attachment *a_blink; /* Backward list link */
   char const  *a_path_user;  /* Path as given (maybe including iconv spec) */
   char const  *a_path;       /* Path as opened */
   char const  *a_path_bname; /* Basename of path as opened */
   char const  *a_name;       /* File name to be stored (EQ a_path_bname) */
   char const  *a_content_type;  /* content type */
   char const  *a_content_disposition; /* content disposition */
   struct name *a_content_id; /* content id */
   char const  *a_content_description; /* content description */
   char const  *a_input_charset; /* Interpretation depends on .a_conv */
   char const  *a_charset;    /* ... */
   FILE        *a_tmpf;       /* If AC_TMPFILE */
   enum attach_conv a_conv;   /* User chosen conversion */
   int         a_msgno;       /* message number */
};

struct sendbundle {
   struct header  *sb_hp;
   struct name    *sb_to;
   FILE           *sb_input;
   struct str     sb_signer;  /* USER@HOST for signing+ */
   struct url     sb_url;
   struct ccred   sb_ccred;
};

/* For saving the current directory and later returning */
struct cw {
#ifdef HAVE_FCHDIR
   int         cw_fd;
#else
   char        cw_wd[PATH_MAX];
#endif
};

/*
 * Global variable declarations
 *
 * These become instantiated in main.c.
 */

#undef VL
#ifdef n_MAIN_SOURCE
# ifndef HAVE_AMALGAMATION
#  define VL
# else
#  define VL            static
# endif
#else
# define VL             extern
#endif

VL int         mb_cur_max;          /* Value of MB_CUR_MAX */
VL int         realscreenheight;    /* The real screen height */
VL int         scrnwidth;           /* Screen width, or best guess */
VL int         scrnheight;          /* Screen height/guess (4 header) */
VL FILE        *n_tty_fp;           /* Our terminal output TODO input channel */

VL char const  *progname;           /* Our name */

VL gid_t       group_id;            /* getgid() and getuid() */
VL uid_t       user_id;

VL int         exit_status;         /* Exit status */
VL ui32_t      options;             /* Bits of enum user_options */
VL char const *option_Mm_arg;       /* Argument for -[Mm] aka OPT_[Mm]_FLAG */
VL struct name *option_r_arg;       /* Argument to -r option */
VL char const  **smopts;            /* sendmail(1) opts from commline */
VL size_t      smopts_cnt;          /* Entries in smopts */

VL ui32_t      pstate;              /* Bits of enum program_state */

/* XXX stylish sorting */
VL int            msgCount;            /* Count of messages read in */
VL struct mailbox mb;                  /* Current mailbox */
VL int            image;               /* File descriptor for msg image */
VL char           mailname[PATH_MAX];  /* Name of current file TODO URL/object*/
VL char           displayname[80 - 40]; /* Prettyfied for display TODO URL/obj*/
VL char           prevfile[PATH_MAX];  /* Name of previous file TODO URL/obj */
VL char const     *account_name;       /* Current account name or NULL */
VL off_t          mailsize;            /* Size of system mailbox */
VL struct message *dot;                /* Pointer to current message */
VL struct message *prevdot;            /* Previous current message */
VL struct message *message;            /* The actual message structure */
VL struct message *threadroot;         /* first threaded message */
VL int            *n_msgvec;           /* Folder setmsize(), list.c res. store*/

VL struct time_current  time_current;  /* time(3); send: mail1() XXXcarrier */
VL struct termios_state termios_state; /* getpassword(); see commands().. */

#ifdef HAVE_SSL
VL enum ssl_verify_level   ssl_verify_level; /* SSL verification level */
#endif

#ifdef HAVE_ICONV
VL iconv_t     iconvd;
#endif

VL volatile int interrupts; /* TODO rid! */
VL sighandler_type dflpipe;

/* TODO Temporary hacks unless the codebase doesn't jump and uses pass-by-value
 * TODO carrier structs instead of locals */
VL char        *temporary_arg_v_store;
/* TODO temporary storage to overcome which_protocol() mess (for PROTO_FILE) */
VL char const  *temporary_protocol_ext;

/* The remaining variables need initialization */

#ifndef HAVE_AMALGAMATION
VL char const  month_names[12 + 1][4];
VL char const  weekday_names[7 + 1][4];

VL char const  uagent[sizeof VAL_UAGENT];
VL char const  n_error[sizeof n_ERROR];
VL char const  n_unirepl[sizeof n_UNIREPL];
VL char const  n_empty[1];

VL ui16_t const class_char[1 + 0x7F];
#endif

/*
 * Finally, let's include the function prototypes XXX embed
 */

#ifndef n_PRIVSEP_SOURCE
# include "nailfuns.h"
#endif

/* s-it-mode */
