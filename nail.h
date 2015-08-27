/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Header inclusion, macros, constants, types and the global var declarations.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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

#ifdef HAVE_OPENSSL_MD5
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
#define FILE_LOCK_TRIES 10       /* Maximum tries before file_lock() fails */
#define ERRORS_MAX      1000     /* Maximum error ring entries TODO configable*/
#define ESCAPE          '~'      /* Default escape for sending */
#define FIO_STACK_SIZE  20       /* Maximum recursion for sourcing */
#define HIST_SIZE       242      /* tty.c: history list default size */
#define HSHSIZE         23       /* Hash prime TODO make dynamic, obsolete */
#define MAXARGC         1024     /* Maximum list of raw strings */
#define MAXEXP          25       /* Maximum expansion of aliases */
#define PROMPT_BUFFER_SIZE 80    /* getprompt() bufsize (> 3!) */
#define REFERENCES_MAX  20       /* Maximum entries in References: */
#define FTMP_OPEN_TRIES 10       /* Maximum number of Ftmp() open(2) tries */

#define ACCOUNT_NULL    "null"   /* Name of "null" account */
#define MAILRC          "~/.mailrc"
#define NETRC           "~/.netrc"
#define TMPDIR_FALLBACK "/tmp"

/* Some environment variables for pipe hooks */
#define AGENT_USER      "NAIL_USER"
#define AGENT_USER_ENC  "NAIL_USER_ENC"
#define AGENT_HOST      "NAIL_HOST"
#define AGENT_HOST_PORT "NAIL_HOST_PORT"

#undef COLOUR
#ifdef HAVE_COLOUR
# define COLOUR(X)      X
#else
# define COLOUR(X)
#endif
#define COLOUR_MSGINFO  "fg=green"
#define COLOUR_PARTINFO "fg=brown"
#define COLOUR_FROM_    "fg=brown"
#define COLOUR_HEADER   "fg=red"
#define COLOUR_UHEADER  "ft=bold,fg=red"
#define COLOUR_TERMS    \
   "cons25,linux,rxvt,rxvt-unicode,screen,sun,vt100,vt220,wsvt25,xterm"
#define COLOUR_USER_HEADERS "from,subject"

#define FROM_DATEBUF    64    /* Size of RFC 4155 From_ line date */
#define DATE_DAYSYEAR   365L
#define DATE_SECSMIN    60L
#define DATE_MINSHOUR   60L
#define DATE_HOURSDAY   24L
#define DATE_SECSDAY    (DATE_SECSMIN * DATE_MINSHOUR * DATE_HOURSDAY)

/* *indentprefix* default as of POSIX */
#define INDENT_DEFAULT  "\t"

/* Default *encoding* as enum mime_enc below */
#define MIME_DEFAULT_ENCODING MIMEE_QP

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

/* Fallback MIME charsets, if *charset-7bit* and *charset-8bit* or not set */
#define CHARSET_7BIT    "US-ASCII"
#ifdef HAVE_ICONV
# define CHARSET_8BIT      "UTF-8"
# define CHARSET_8BIT_OKEY charset_8bit
#else
# define CHARSET_8BIT      "ISO-8859-1"
# define CHARSET_8BIT_OKEY ttycharset
#endif

/* Some environment variables for pipe hooks etc. */
#define NAILENV_TMPDIR              "NAIL_TMPDIR"
#define NAILENV_FILENAME            "NAIL_FILENAME"
#define NAILENV_FILENAME_GENERATED  "NAIL_FILENAME_GENERATED"
#define NAILENV_CONTENT             "NAIL_CONTENT"
#define NAILENV_CONTENT_EVIDENCE    "NAIL_CONTENT_EVIDENCE"

/* Is *W* a quoting (ASCII only) character? */
#define ISQUOTE(W)      \
   ((W) == L'>' || (W) == L'|' || (W) == L'}' || (W) == L':')

/* Maximum number of quote characters (not bytes!) that'll be used on
 * follow lines when compressing leading quote characters */
#define QUOTE_MAX       42

/* How much spaces should a <tab> count when *quote-fold*ing? (power-of-two!) */
#define QUOTE_TAB_SPACES 8

/* Maximum size of a message that is passed through to the spam system */
#define SPAM_MAXSIZE    420000

/* String dope: dynamic buffer size, and size of the single builtin one that's
 * used first; note that these value include the size of the structure */
#define SBUFFER_SIZE    ((0x10000u >> 1u) - 0x400)
#define SBUFFER_BUILTIN (0x10000u >> 1u)

/* These come from the configuration (named Xxy to not clash with sh(1)..) */
#ifndef XSHELL
# define XSHELL         "/bin/sh"
#endif
#ifndef XLISTER
# define XLISTER        "ls"
#endif
#ifndef XPAGER
# define XPAGER         "more"
#endif

/*
 * OS, CC support, generic macros etc.
 */

/* OS: we're not a library, only set what needs special treatment somewhere */
#define OS_DRAGONFLY    0
#define OS_SOLARIS      0
#define OS_SUNOS        0

#ifdef __DragonFly__
# undef OS_DRAGONFLY
# define OS_DRAGONFLY   1
#elif defined __solaris__ || defined __sun
# if defined __SVR4 || defined __svr4__
#  undef OS_SOLARIS
#  define OS_SOLARIS    1
# else
#  undef OS_SUNOS
#  define OS_SUNOS      1
# endif
#endif

/* CC */
#define CC_CLANG           0
#define PREREQ_CLANG(X,Y)  0
#define CC_GCC             0
#define PREREQ_GCC(X,Y)    0

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
#endif

#ifndef __EXTEN
# define __EXTEN
#endif

/* Suppress some technical warnings via #pragma's unless developing.
 * XXX Wild guesses: clang(1) 1.7 and (OpenBSD) gcc(1) 4.2.1 don't work */
#ifndef HAVE_DEVEL
# if PREREQ_CLANG(3, 4)
#  pragma clang diagnostic ignored "-Wunused-result"
#  pragma clang diagnostic ignored "-Wformat"
# elif PREREQ_GCC(4, 7)
#  pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#  pragma GCC diagnostic ignored "-Wunused-result"
#  pragma GCC diagnostic ignored "-Wformat"
# endif
#endif

/* For injection macros like DBG(), NATCH_CHAR() */
#define COMMA           ,

#define EMPTY_FILE()    typedef int CONCAT(avoid_empty_file__, n_FILE);

/* Pointer to size_t */
#define PTR2SIZE(X)     ((size_t)(uintptr_t)(X))

/* Pointer comparison (types from below) */
#define PTRCMP(A,C,B)   ((uintptr_t)(A) C (uintptr_t)(B))

/* Ditto, compare (maybe mixed-signed) integers cases to T bits, unsigned;
 * Note: doesn't sign-extend correctly, that's still up to the caller */
#define UICMP(T,A,C,B)  ((ui ## T ## _t)(A) C (ui ## T ## _t)(B))

/* Align something to a size/boundary that cannot cause just any problem */
#define n_ALIGN(X)      (((X) + 2*sizeof(void*)) & ~((2*sizeof(void*)) - 1))

/* Members in constant array */
#ifndef NELEM
# define NELEM(A)       (sizeof(A) / sizeof(A[0]))
#endif

/* sizeof() for member fields */
#define SIZEOF_FIELD(T,F) sizeof(((T *)NULL)->F)

/* Casts-away (*NOT* cast-away) */
#define UNUSED(X)       ((void)(X))
#define UNCONST(P)      ((void*)(uintptr_t)(void const*)(P))
#define UNVOLATILE(P)   ((void*)(uintptr_t)(void volatile*)(P))
#define UNXXX(T,C,P)    ((T)(uintptr_t)(C)(P))

/* __STDC_VERSION__ is ISO C99, so also use __STDC__, which should work */
#if defined __STDC__ || defined __STDC_VERSION__ /*|| defined __cplusplus*/
# define STRING(X)      #X
# define XSTRING(X)     STRING(X)
# define CONCAT(S1,S2)  _CONCAT(S1, S2)
# define _CONCAT(S1,S2) S1 ## S2
#else
# define STRING(X)      "X"
# define XSTRING        STRING
# define CONCAT(S1,S2)  S1/**/S2
#endif

#if defined __STDC_VERSION__ && __STDC_VERSION__ + 0 >= 199901L
  /* Variable size arrays and structure fields */
# define VFIELD_SIZE(X)
# define VFIELD_SIZEOF(T,F) (0)
  /* Inline functions */
# define HAVE_INLINE
# define INLINE         inline
# define SINLINE        static inline
#else
# define VFIELD_SIZE(X) \
  ((X) == 0 ? sizeof(size_t) \
   : ((ssize_t)(X) < 0 ? sizeof(size_t) - (X) : (size_t)(X)))
# define VFIELD_SIZEOF(T,F) SIZEOF_FIELD(T, F)
# if CC_CLANG || PREREQ_GCC(2, 9)
#   define INLINE       static __inline
#   define SINLINE      static __inline
# else
#   define INLINE
#   define SINLINE      static
# endif
#endif

#undef __FUN__
#if defined __STDC_VERSION__ && __STDC_VERSION__ + 0 >= 199901L
# define __FUN__        __func__
#elif CC_CLANG || PREREQ_GCC(3, 4)
# define __FUN__        __extension__ __FUNCTION__
#else
# define __FUN__        uagent   /* Something that is not a literal */
#endif

#if defined __predict_true && defined __predict_false
# define LIKELY(X)      __predict_true(X)
# define UNLIKELY(X)    __predict_false(X)
#elif CC_CLANG || PREREQ_GCC(2, 96)
# define LIKELY(X)      __builtin_expect(X, 1)
# define UNLIKELY(X)    __builtin_expect(X, 0)
#else
# define LIKELY(X)      (X)
# define UNLIKELY(X)    (X)
#endif

#undef HAVE_NATCH_CHAR
#undef NATCH_CHAR
#if defined HAVE_SETLOCALE && defined HAVE_C90AMEND1 && defined HAVE_WCWIDTH
# define HAVE_NATCH_CHAR
# define NATCH_CHAR(X)  X
#else
# define NATCH_CHAR(X)
#endif

/* Compile-Time-Assert
 * Problem is that some compilers warn on unused local typedefs, so add
 * a special local CTA to overcome this */
#define CTA(TEST)       _CTA_1(TEST, n_FILE, __LINE__)
#define LCTA(TEST)      _LCTA_1(TEST, n_FILE, __LINE__)

#define _CTA_1(T,F,L)   _CTA_2(T, F, L)
#define _CTA_2(T,F,L)  \
   typedef char ASSERTION_failed_in_file_## F ## _at_line_ ## L[(T) ? 1 : -1]
#define _LCTA_1(T,F,L)  _LCTA_2(T, F, L)
#define _LCTA_2(T,F,L) \
do {\
   typedef char ASSERTION_failed_in_file_## F ## _at_line_ ## L[(T) ? 1 : -1];\
   ASSERTION_failed_in_file_## F ## _at_line_ ## L __i_am_unused__;\
   UNUSED(__i_am_unused__);\
} while (0)

#define UNINIT(N,V)     N = V

#undef ISPOW2
#define ISPOW2(X)       ((((X) - 1) & (X)) == 0)
#undef MIN
#define MIN(A, B)       ((A) < (B) ? (A) : (B))
#undef MAX
#define MAX(A, B)       ((A) < (B) ? (B) : (A))
#undef ABS
#define ABS(A)          ((A) < 0 ? -(A) : (A))

#undef DBG
#undef NDBG
#ifndef HAVE_DEBUG
# undef assert
# define assert(X)      UNUSED(0)
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
 * Types
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
# define PRIxZ_FMT_CTA() CTA(1 == 1)
# define UIZ_MAX        SIZE_MAX
#elif defined SIZE_MAX
  /* UnixWare has size_t as unsigned as required but uses a signed limit
   * constant (which is thus false!) */
# if SIZE_MAX == UI64_MAX || SIZE_MAX == SI64_MAX
#  define PRIuZ         PRIu64
#  define PRIdZ         PRId64
#  define PRIxZ_FMT_CTA() CTA(sizeof(size_t) == sizeof(ui64_t))
# elif SIZE_MAX == UI32_MAX || SIZE_MAX == SI32_MAX
#  define PRIuZ         PRIu32
#  define PRIdZ         PRId32
#  define PRIxZ_FMT_CTA() CTA(sizeof(size_t) == sizeof(ui32_t))
# else
#  error SIZE_MAX is neither UI64_MAX nor UI32_MAX (please report this)
# endif
# define UIZ_MAX        SIZE_MAX
#endif
#ifndef PRIuZ
# define PRIuZ          "lu"
# define PRIdZ          "ld"
# define PRIxZ_FMT_CTA() CTA(sizeof(size_t) == sizeof(unsigned long))
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

enum {FAL0, TRU1, TRUM1 = -1};
typedef si8_t           bool_t;

/* Add shorter aliases for "normal" integers */
typedef unsigned long   ul_i;
typedef unsigned int    ui_i;
typedef unsigned short  us_i;
typedef unsigned char   uc_i;

typedef signed long     sl_i;
typedef signed int      si_i;
typedef signed short    ss_i;
typedef signed char     sc_i;

typedef void (          *sighandler_type)(int);

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

enum colourspec {
   COLOURSPEC_MSGINFO,
   COLOURSPEC_PARTINFO,
   COLOURSPEC_FROM_,
   COLOURSPEC_HEADER,
   COLOURSPEC_UHEADER,
   COLOURSPEC_RESET
};

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
   CPROTO_SMTP,
   CPROTO_POP3,
   CPROTO_IMAP
};

enum dotlock_state {
   DLS_NONE,
   DLS_CANT_CHDIR,            /* Failed to chdir(2) into desired path */
   DLS_NAMETOOLONG,           /* Lock file name would be too long */
   DLS_NOPERM,                /* No permission to creat lock file */
   DLS_NOEXEC,                /* Privilege separated dotlocker not found */
   DLS_PRIVFAILED,            /* Rising privileges failed in dotlocker */
   DLS_EXIST,                 /* Lock file already exists, stale lock? */
   DLS_FISHY,                 /* Something makes us think bad of situation */
   DLS_DUNNO,                 /* Catch-all error */
   DLS_PING,                  /* Not an error, but have to wait for lock */
   DLS_ABANDON    = 1<<7      /* ORd to any but _NONE: give up, don't retry */
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
   FEXP_LOCAL     = 1<<0,     /* Result must be local file/maildir */
   FEXP_SHELL     = 1<<1,     /* No folder %,#,&,+ stuff, yet sh(1) */
   FEXP_NSHORTCUT = 1<<2,     /* Don't expand shortcuts */
   FEXP_SILENT    = 1<<3,     /* Don't print but only return errors */
   FEXP_MULTIOK   = 1<<4,     /* Expansion to many entries is ok */
   FEXP_NSHELL    = 1<<5      /* Don't do shell word exp. (but ~/, $VAR) */
};

enum file_lock_type {
   FLT_READ,
   FLT_WRITE
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
    * For mustquote(), specifies wether special RFC 2047 header rules
    * should be used instead */
   MIMEEF_ISHEAD     = 1<<5,
   /* (encode) Ditto; for mustquote() this furtherly fine-tunes behaviour in
    * that characters which would not be reported as "must-quote" when
    * detecting wether quoting is necessary at all will be reported as
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

/* Special handler return values for mime_type_mimepart_handler() */
#define MIME_TYPE_HANDLER_TEXT   (char*)-1
#define MIME_TYPE_HANDLER_HTML   (char*)-2

enum mime_parse_flags {
   MIME_PARSE_NONE      = 0,
   MIME_PARSE_DECRYPT   = 1<<0,
   MIME_PARSE_PARTS     = 1<<1
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
   OF_REGISTER    = 1<<10     /* Register file in our file table */
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
   PROTO_IMAP,       /* is an imap server string */
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

enum user_options {
   OPT_NONE,
   OPT_DEBUG      = 1u<< 0,   /* -d / *debug* */
   OPT_VERB       = 1u<< 1,   /* -v / *verbose* */
   OPT_VERBVERB   = 1u<< 2,   /* .. even more verbosity */
   OPT_EXISTONLY  = 1u<< 3,   /* -e */
   OPT_HEADERSONLY = 1u<<4,   /* -H */
   OPT_HEADERLIST = 1u<< 5,   /* -L */
   OPT_QUICKRUN_MASK = OPT_EXISTONLY | OPT_HEADERSONLY | OPT_HEADERLIST,
   OPT_NOSRC      = 1u<< 6,   /* -n */
   OPT_E_FLAG     = 1u<< 7,   /* -E / *skipemptybody* */
   OPT_F_FLAG     = 1u<< 8,   /* -F */
   OPT_N_FLAG     = 1u<< 9,   /* -N / *header* */
   OPT_R_FLAG     = 1u<<10,   /* -R */
   OPT_r_FLAG     = 1u<<11,   /* -r (plus option_r_arg) */
   OPT_t_FLAG     = 1u<<12,   /* -t */
   OPT_u_FLAG     = 1u<<13,   /* -u / $USER and pw->pw_uid != getuid(2) */
   OPT_TILDE_FLAG = 1u<<14,   /* -~ */
   OPT_BATCH_FLAG = 1u<<15,   /* -# */

   /*  */
   OPT_MEMDEBUG   = 1<<16,    /* *memdebug* */

   /*  */
   OPT_SENDMODE   = 1u<<17,   /* Usage case forces send mode */
   OPT_INTERACTIVE = 1u<<18,  /* isatty(0) */
   OPT_TTYIN      = OPT_INTERACTIVE,
   OPT_TTYOUT     = 1u<<19,
   OPT_UNICODE    = 1u<<20,   /* We're in an UTF-8 environment */
   OPT_ENC_MBSTATE = 1u<<21,  /* Multibyte environment with shift states */

   /* Some easy-access shortcuts */
   OPT_D_V        = OPT_DEBUG | OPT_VERB,
   OPT_D_VV       = OPT_DEBUG | OPT_VERBVERB
};

#define IS_TTY_SESSION() \
   ((options & (OPT_TTYIN | OPT_TTYOUT)) == (OPT_TTYIN | OPT_TTYOUT))

#define OBSOLETE(X) \
do {\
   if (options & OPT_D_V)\
      n_err("%s: %s\n", _("Obsoletion warning"), X);\
} while (0)
#define OBSOLETE2(X,Y) \
do {\
   if (options & OPT_D_V)\
      n_err("%s: %s: %s\n", _("Obsoletion warning"), X, Y);\
} while (0)

enum program_state {
   PS_STARTED        = 1<< 0,       /* main.c startup code passed, functional */

   PS_LOADING        = 1<< 1,       /* Loading user resource files, startup */
   PS_SOURCING       = 1<< 2,       /* Sourcing a resource file */
   PS_IN_LOAD        = PS_LOADING | PS_SOURCING,

   PS_EVAL_ERROR     = 1<< 4,       /* Last evaluate() command failed */

   PS_HOOK_NEWMAIL   = 1<< 6,
   PS_HOOK           = 1<< 7,
   PS_HOOK_MASK      = PS_HOOK_NEWMAIL | PS_HOOK,

   PS_EDIT           = 1<< 8,       /* Current mailbox not a "system mailbox" */
   PS_SAW_COMMAND    = 1<< 9,       /* ..after mailbox switch */

   PS_DID_PRINT_DOT  = 1<<16,       /* Current message has been printed */

   PS_MSGLIST_SAW_NO = 1<<17,       /* Last *LIST saw numerics */
   PS_MSGLIST_DIRECT = 1<<18,       /* One msg was directly chosen by number */
   PS_MSGLIST_MASK   = PS_MSGLIST_SAW_NO | PS_MSGLIST_DIRECT,

   /* Various first-time-init switches */
   PS_ERRORS_NOTED   = 1<<24,       /* Ring of `errors' content, print msg */
   PS_t_FLAG         = 1<<25        /* OPT_t_FLAG made persistant */
};

/* A large enum with all the binary and value options a.k.a their keys.
 * Only the constant keys are in here, to be looked up via ok_[bv]look(),
 * ok_[bv]set() and ok_[bv]clear().
 * Note: see the comments in accmacvar.c before changing *anything* in here! */
enum okeys {
   /* Option keys for binary options */
   ok_b_add_file_recipients,
   ok_b_allnet,
   ok_b_append,
   ok_b_ask,
   ok_b_askatend,
   ok_b_askattach,
   ok_b_askbcc,
   ok_b_askcc,
   ok_b_asksign,
   ok_b_asksub,
   ok_b_attachment_ask_content_description,
   ok_b_attachment_ask_content_disposition,
   ok_b_attachment_ask_content_id,
   ok_b_attachment_ask_content_type,
   ok_b_autocollapse,
   ok_b_autoprint,
   ok_b_autothread,
   ok_b_bang,
   ok_b_batch_exit_on_error,
   ok_b_bsdannounce,
   ok_b_bsdcompat,
   ok_b_bsdflags,
   ok_b_bsdheadline,
   ok_b_bsdmsgs,
   ok_b_bsdorder,
   ok_b_bsdset,
   ok_b_colour_disable,
   ok_b_colour_pager,
   ok_b_debug,                         /* {special=1} */
   ok_b_disconnected,
   ok_b_disposition_notification_send,
   ok_b_dot,
   ok_b_dotlock_ignore_error,
   ok_b_editalong,
   ok_b_editheaders,
   ok_b_emptybox,
   ok_b_emptystart,
   ok_b_flipr,
   ok_b_followup_to,
   ok_b_forward_as_attachment,
   ok_b_fullnames,
   ok_b_header,                        /* {special=1} */
   ok_b_history_gabby,
   ok_b_history_gabby_persist,
   ok_b_hold,
   ok_b_idna_disable,
   ok_b_ignore,
   ok_b_ignoreeof,
   ok_b_imap_use_starttls,
   ok_b_keep,
   ok_b_keep_content_length,
   ok_b_keepsave,
   ok_b_line_editor_disable,
   ok_b_markanswered,
   ok_b_mbox_rfc4155,
   ok_b_message_id_disable,
   ok_b_metoo,
   ok_b_mime_allow_text_controls,
   ok_b_netrc_lookup,
   ok_b_outfolder,
   ok_b_page,
   ok_b_piperaw,
   ok_b_pop3_bulk_load,
   ok_b_pop3_no_apop,
   ok_b_pop3_use_starttls,
   ok_b_print_all_chars,
   ok_b_print_alternatives,
   ok_b_quiet,
   ok_b_quote_as_attachment,
   ok_b_recipients_in_cc,
   ok_b_record_resent,
   ok_b_reply_in_same_charset,
   ok_b_rfc822_body_from_,             /* {name=rfc822-body-from_} */
   ok_b_save,
   ok_b_searchheaders,
   ok_b_sendcharsets_else_ttycharset,
   ok_b_sendwait,
   ok_b_showlast,
   ok_b_showname,
   ok_b_showto,
   ok_b_skipemptybody,                 /* {special=1} */
   ok_b_smime_force_encryption,
   ok_b_smime_no_default_ca,
   ok_b_smime_sign,
   ok_b_smtp_use_starttls,
   ok_b_ssl_no_default_ca,
   ok_b_term_ca_mode,
   ok_b_v15_compat,
   ok_b_verbose,                       /* {special=1} */
   ok_b_writebackedited,
   ok_b_memdebug,                      /* {special=1} */

   /* Option keys for values options */
   ok_v_agent_shell_lookup,
   ok_v_attrlist,
   ok_v_autobcc,
   ok_v_autocc,
   ok_v_autosort,
   ok_v_charset_7bit,
   ok_v_charset_8bit,
   ok_v_charset_unknown_8bit,
   ok_v_cmd,
   ok_v_colour_from_,                  /* {name=colour-from_} */
   ok_v_colour_header,
   ok_v_colour_msginfo,
   ok_v_colour_partinfo,
   ok_v_colour_terms,
   ok_v_colour_uheader,
   ok_v_colour_user_headers,
   ok_v_crt,
   ok_v_datefield,
   ok_v_datefield_markout_older,
   ok_v_DEAD,
   ok_v_EDITOR,
   ok_v_encoding,
   ok_v_escape,
   ok_v_expandaddr,
   ok_v_expandargv,
   ok_v_features,                      /* {rdonly=1,virtual=_features} */
   ok_v_folder,                        /* {special=1} */
   ok_v_folder_hook,
   ok_v_followup_to_honour,
   ok_v_from,
   ok_v_fwdheading,
   ok_v_headline,
   ok_v_headline_bidi,
   ok_v_hostname,
   ok_v_imap_auth,
   ok_v_imap_cache,
   ok_v_imap_keepalive,
   ok_v_imap_list_depth,
   ok_v_indentprefix,
   ok_v_line_editor_cursor_right,      /* {special=1} */
   ok_v_LISTER,
   ok_v_MAIL,
   ok_v_MBOX,
   ok_v_mime_counter_evidence,
   /* TODO v15-compat: mimetypes-load-control -> mimetypes-load / mimetypes */
   ok_v_mimetypes_load_control,
   ok_v_NAIL_EXTRA_RC,                 /* {name=NAIL_EXTRA_RC} */
   /* TODO v15-compat: NAIL_HEAD -> message-head? */
   ok_v_NAIL_HEAD,                     /* {name=NAIL_HEAD} */
   /* TODO v15-compat: NAIL_HISTFILE -> history-file */
   ok_v_NAIL_HISTFILE,                 /* {name=NAIL_HISTFILE} */
   /* TODO v15-compat: NAIL_HISTSIZE -> history-size{,limit} */
   ok_v_NAIL_HISTSIZE,                 /* {name=NAIL_HISTSIZE} */
   /* TODO v15-compat: NAIL_TAIL -> message-tail? */
   ok_v_NAIL_TAIL,                     /* {name=NAIL_TAIL} */
   ok_v_newfolders,
   ok_v_newmail,
   ok_v_ORGANIZATION,
   ok_v_PAGER,
   ok_v_password,
   /* TODO pop3_auth is yet a dummy to enable easier impl. of ccred_lookup()! */
   ok_v_pop3_auth,
   ok_v_pop3_keepalive,
   ok_v_prompt,
   ok_v_quote,
   ok_v_quote_fold,
   ok_v_record,
   ok_v_reply_strings,
   ok_v_replyto,
   ok_v_reply_to_honour,
   ok_v_screen,
   ok_v_sendcharsets,
   ok_v_sender,
   ok_v_sendmail,
   ok_v_sendmail_arguments,
   /* FIXME this is not falsely sorted, but this entire enum including the
    * FIXME manual should be sorted alphabetically instead of binary/value */
   ok_b_sendmail_no_default_arguments,
   ok_v_sendmail_progname,
   ok_v_SHELL,
   ok_v_Sign,
   ok_v_sign,
   ok_v_signature,
   ok_v_smime_ca_dir,
   ok_v_smime_ca_file,
   ok_v_smime_cipher,
   ok_v_smime_crl_dir,
   ok_v_smime_crl_file,
   ok_v_smime_sign_cert,
   ok_v_smime_sign_include_certs,
   ok_v_smime_sign_message_digest,
   ok_v_smtp,
   /* TODO v15-compat: smtp-auth: drop */
   ok_v_smtp_auth,
   ok_v_smtp_auth_password,
   ok_v_smtp_auth_user,
   ok_v_smtp_hostname,
   ok_v_spam_interface,
   ok_v_spam_maxsize,
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
   ok_v_ssl_protocol,
   ok_v_ssl_rand_egd,
   ok_v_ssl_rand_file,
   ok_v_ssl_verify,
   ok_v_stealthmua,
   ok_v_toplines,
   ok_v_ttycharset,
   ok_v_user,
   ok_v_version,                       /* {rdonly=1,virtual=VERSION} */
   ok_v_version_major,                 /* {rdonly=1,virtual=VERSION_MAJOR} */
   ok_v_version_minor,                 /* {rdonly=1,virtual=VERSION_MINOR} */
   ok_v_version_update,                /* {rdonly=1,virtual=VERSION_UPDATE} */
   ok_v_VISUAL
};

/* Locale-independent character classes */
enum {
   C_CNTRL        = 0000,
   C_BLANK        = 0001,
   C_WHITE        = 0002,
   C_SPACE        = 0004,
   C_PUNCT        = 0010,
   C_OCTAL        = 0020,
   C_DIGIT        = 0040,
   C_UPPER        = 0100,
   C_LOWER        = 0200
};

struct str {
   char     *s;      /* the string's content */
   size_t   l;       /* the stings's length */
};

struct colour_table {
   /* Plus a copy of *colour-user-headers* */
   struct str  ct_csinfo[COLOURSPEC_RESET+1 + 1];
};

struct bidi_info {
   struct str  bi_start;      /* Start of (possibly) bidirectional text */
   struct str  bi_end;        /* End of ... */
   size_t      bi_pad;        /* No of visual columns to reserve for BIDI pad */
};

struct url {
   char const     *url_input;       /* Input as given (really) */
   enum cproto    url_cproto;       /* Communication protocol as given */
   bool_t         url_needs_tls;    /* Wether the protocol uses SSL/TLS */
   bool_t         url_had_user;     /* Wether .url_user was part of the URL */
   ui16_t         url_portno;       /* atoi .url_port or default, host endian */
   char const     *url_port;        /* Port (if given) or NULL */
   char           url_proto[14];    /* Communication protocol as 'xy\0//' */
   ui8_t          url_proto_len;    /* Length of .url_proto ('\0' index) */
   ui8_t          url_proto_xlen;   /* .. if '\0' is replaced with ':' */
   struct str     url_user;         /* User, exactly as given / looked up */
   struct str     url_user_enc;     /* User, urlxenc()oded */
   struct str     url_pass;         /* Pass (urlxdec()oded) or NULL */
   struct str     url_host;         /* Service hostname */
   struct str     url_path;         /* CPROTO_IMAP: path suffix or NULL */
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
struct dotlock_info {
   char const  *di_file_name;
   char const  *di_lock_name;
   char const  *di_hostname;
   char const  *di_randstr;
   size_t      di_pollmsecs;
   struct stat *di_stb;
};
#endif

struct time_current {
   time_t      tc_time;
   struct tm   tc_gm;
   struct tm   tc_local;
   char        tc_ctime[32];
};

struct quoteflt {
   FILE        *qf_os;        /* Output stream */
   char const  *qf_pfix;
   ui32_t      qf_pfix_len;   /* Length of prefix: 0: bypass */
   ui32_t      qf_qfold_min;  /* Simple way: wrote prefix? */
#ifdef HAVE_QUOTE_FOLD
   ui32_t      qf_qfold_max;  /* Otherwise: line lengths */
   ui8_t       qf_state;      /* *quote-fold* state machine */
   bool_t      qf_brk_isws;   /* Breakpoint is at WS */
   ui8_t       __dummy[2];
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

struct eval_ctx {
   struct str  ev_line;          /* The terminated data to evaluate */
   ui32_t      ev_line_size;     /* May be used to store line memory size */
   bool_t      ev_is_recursive;  /* Evaluation in evaluation? (collect ~:) */
   ui8_t       __dummy[3];
   bool_t      ev_add_history;   /* Enter (final) command in history? */
   char const  *ev_new_content;  /* History: reenter line, start with this */
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
      tcsetattr(0, TCSADRAIN, &termios_state.ts_tios);\
      termios_state.ts_needs_reset = FAL0;\
   }\
} while (0)

struct sock {                 /* data associated with a socket */
   int         s_fd;          /* file descriptor */
#ifdef HAVE_SSL
   int         s_use_ssl;     /* SSL is used */
# ifdef HAVE_OPENSSL
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
      MB_BYE      = 010       /* may accept a BYE state */
   }           mb_active;
   FILE        *mb_itf;       /* temp file with messages, read open */
   FILE        *mb_otf;       /* same, write open */
   char        *mb_sorted;    /* sort method */
   enum {
      MB_VOID,       /* no type (e. g. connection failed) */
      MB_FILE,       /* local file */
      MB_POP3,       /* POP3 mailbox */
      MB_IMAP,       /* IMAP mailbox */
      MB_MAILDIR,    /* maildir folder */
      MB_CACHE       /* cached mailbox */
   }           mb_type;       /* type of mailbox */
   enum {
      MB_DELE = 01,  /* may delete messages in mailbox */
      MB_EDIT = 02   /* may edit messages in mailbox */
   }           mb_perm;
   int mb_threaded;           /* mailbox has been threaded */
#ifdef HAVE_IMAP
   enum mbflags {
      MB_NOFLAGS  = 000,
      MB_UIDPLUS  = 001 /* supports IMAP UIDPLUS */
   }           mb_flags;
   unsigned long  mb_uidvalidity;   /* IMAP unique identifier validity */
   char        *mb_imap_account;    /* name of current IMAP account */
   char        *mb_imap_pass;       /* xxx v15-compat URL workaround */
   char        *mb_imap_mailbox;    /* name of current IMAP mailbox */
   char        *mb_cache_directory; /* name of cache directory */
#endif
   struct sock mb_sock;       /* socket structure */
};

enum needspec {
   NEED_UNSPEC,      /* unspecified need, don't fetch */
   NEED_HEADER,      /* need the header of a message */
   NEED_BODY         /* need header and body of a message */
};

enum havespec {
   HAVE_NOTHING,              /* nothing downloaded yet */
   HAVE_HEADER    = 01,       /* header is downloaded */
   HAVE_BODY      = 02        /* entire message is downloaded */
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
   MFULLYCACHED   = (1<<13),  /* message is completely cached */
   MBOXED         = (1<<14),  /* message has been sent to mbox */
   MUNLINKED      = (1<<15),  /* message was unlinked from cache */
   MNEWEST        = (1<<16),  /* message is very new (newmail) */
   MFLAG          = (1<<17),  /* message has been flagged recently */
   MUNFLAG        = (1<<18),  /* message has been unflagged */
   MFLAGGED       = (1<<19),  /* message is `flagged' */
   MANSWER        = (1<<20),  /* message has been answered recently */
   MUNANSWER      = (1<<21),  /* message has been unanswered */
   MANSWERED      = (1<<22),  /* message is `answered' */
   MDRAFT         = (1<<23),  /* message has been drafted recently */
   MUNDRAFT       = (1<<24),  /* message has been undrafted */
   MDRAFTED       = (1<<25),  /* message is marked as `draft' */
   MOLDMARK       = (1<<26),  /* messages was marked previously */
   MSPAM          = (1<<27),  /* message is classified as spam */
   MSPAMUNSURE    = (1<<28)   /* message may be spam, but it is unsure */
};
#define MMNORM          (MDELETED | MSAVED | MHIDDEN)
#define MMNDEL          (MDELETED | MHIDDEN)

#define visible(mp)     (((mp)->m_flag & MMNDEL) == 0)

struct mimepart {
   enum mflag  m_flag;        /* flags */
   enum havespec m_have;      /* downloaded parts of the part */
#ifdef HAVE_SPAM
   ui32_t      m_spamscore;   /* Spam score as int, 24:8 bits */
#endif
   int         m_block;       /* block number of this part */
   size_t      m_offset;      /* offset in block of part */
   size_t      m_size;        /* Bytes in the part */
   size_t      m_xsize;       /* Bytes in the full part */
   long        m_lines;       /* Lines in the message */
   long        m_xlines;      /* Lines in the full message */
   time_t      m_time;        /* time the message was sent */
   char const  *m_from;       /* message sender */
   struct mimepart *m_nextpart;     /* next part at same level */
   struct mimepart *m_multipart;    /* parts of multipart */
   struct mimepart *m_parent;       /* enclosing multipart part */
   char const  *m_ct_type;          /* content-type */
   char const  *m_ct_type_plain;    /* content-type without specs */
   char const  *m_ct_type_usr_ovwr; /* Forcefully overwritten one */
   enum mimecontent m_mimecontent;  /* same in enum */
   char const  *m_charset;    /* charset */
   char const  *m_ct_enc;     /* content-transfer-encoding */
   enum mime_enc m_mime_enc;     /* same in enum */
   char        *m_partstring; /* part level string */
   char        *m_filename;   /* attachment filename */
};

struct message {
   enum mflag  m_flag;        /* flags */
   enum havespec m_have;      /* downloaded parts of the message */
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
#ifdef HAVE_IMAP
   unsigned long m_uid;       /* IMAP unique identifier */
#endif
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
   ARG_RAWLIST    = 2,        /* Shell string list */
   ARG_NOLIST     = 3,        /* Just plain 0 */
   ARG_NDMLIST    = 4,        /* Message list, no defaults */
   ARG_ECHOLIST   = 5,        /* Like raw list, but keep quote chars */
   ARG_ARGMASK    = 7,        /* Mask of the above */

   ARG_A          = 1u<< 4,   /* Needs an active mailbox */
   ARG_F          = 1u<< 5,   /* Is a conditional command */
   ARG_H          = 1u<< 6,   /* Never place in history */
   ARG_I          = 1u<< 7,   /* Interactive command bit */
   ARG_M          = 1u<< 8,   /* Legal from send mode bit */
   ARG_P          = 1u<< 9,   /* Autoprint dot after command */
   ARG_R          = 1u<<10,   /* Cannot be called from collect / recursion */
   ARG_T          = 1u<<11,   /* Is a transparent command */
   ARG_V          = 1u<<12,   /* Places data in temporary_arg_v_store */
   ARG_W          = 1u<<13,   /* Invalid when read only bit */
   ARG_O          = 1u<<14    /* OBSOLETE()d command */
};

enum gfield {
   GTO            = 1<< 0,    /* Grab To: line */
   GSUBJECT       = 1<< 1,    /* Likewise, Subject: line */
   GCC            = 1<< 2,    /* And the Cc: line */
   GBCC           = 1<< 3,    /* And also the Bcc: line */

   GNL            = 1<< 4,    /* Print blank line after */
   GDEL           = 1<< 5,    /* Entity removed from list */
   GCOMMA         = 1<< 6,    /* detract puts in commas */
   GUA            = 1<< 7,    /* User-Agent field */
   GMIME          = 1<< 8,    /* MIME 1.0 fields */
   GMSGID         = 1<< 9,    /* a Message-ID */

   GIDENT         = 1<<11,    /* From:, Reply-To:, Organization:, MFT: */
   GREF           = 1<<12,    /* References:, In-Reply-To:, (Message-Id:) */
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
   char        *h_organization; /* overridden "Organization:" field */
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

struct addrguts {
   char const  *ag_input;     /* Input string as given */
   size_t      ag_ilen;       /* strlen() of input */
   size_t      ag_iaddr_start; /* Start of *addr-spec* in .ag_input */
   size_t      ag_iaddr_aend; /* ..and one past its end */
   char        *ag_skinned;   /* Output (alloced if !=.ag_input) */
   size_t      ag_slen;       /* strlen() of .ag_skinned */
   size_t      ag_sdom_start; /* Start of domain in .ag_skinned, */
   enum nameflags ag_n_flags; /* enum nameflags of .ag_skinned */
};

/* MIME attachments */
enum attach_conv {
   AC_DEFAULT,       /* _get_lc() -> _iter_*() */
   AC_FIX_OUTCS,     /* _get_lc() -> "charset=" .a_charset */
   AC_FIX_INCS,      /* "charset=".a_input_charset (nocnv) */
   AC_TMPFILE        /* attachment.a_tmpf is converted */
};

struct attachment {
   struct attachment *a_flink;   /* Forward link in list. */
   struct attachment *a_blink;   /* Backward list link */
   char const  *a_name;       /* file name */
   char const  *a_content_type;  /* content type */
   char const  *a_content_disposition; /* content disposition */
   char const  *a_content_id; /* content id */
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

/* Structure of the hash table of ignored header fields */
struct ignoretab {
   int         i_count;       /* Number of entries */
   struct ignored {
      struct ignored *i_link;    /* Next ignored field in bucket */
      char           *i_field;   /* This ignored field */
   }           *i_head[HSHSIZE];
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

VL char const  *homedir;            /* Path name of home directory */
VL char const  *myname;             /* My login name */
VL char const  *progname;           /* Our name */
VL char const  *tempdir;            /* The temporary directory */

VL ui32_t      group_id;            /* getgid() and getuid() */
VL ui32_t      user_id;

VL int         exit_status;         /* Exit status */
VL ui32_t      options;             /* Bits of enum user_options */
VL struct name *option_r_arg;       /* Argument to -r option */
VL char const  **smopts;            /* sendmail(1) opts from commline */
VL size_t      smopts_count;        /* Entries in smopts */

VL ui32_t      pstate;              /* Bits of enum program_state */
VL size_t      noreset;             /* String resets suspended (recursive) */

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
VL int            imap_created_mailbox; /* hack to get feedback from imap */

VL struct ignoretab  ignore[2];        /* ignored and retained fields
                                        * 0 is ignore, 1 is retain */
VL struct ignoretab  saveignore[2];    /* ignored and retained fields
                                        * on save to folder */
VL struct ignoretab  allignore[2];     /* special, ignore all headers */
VL struct ignoretab  fwdignore[2];     /* fields to ignore for forwarding */

VL struct time_current  time_current;  /* time(3); send: mail1() XXXcarrier */
VL struct termios_state termios_state; /* getpassword(); see commands().. */

#ifdef HAVE_COLOUR
VL struct colour_table  *colour_table;
#endif

#ifdef HAVE_SSL
VL enum ssl_verify_level   ssl_verify_level; /* SSL verification level */
#endif

#ifdef HAVE_ICONV
VL iconv_t     iconvd;
#endif

VL sigjmp_buf  srbuf;
VL int         interrupts;
VL sighandler_type dflpipe;
VL sighandler_type handlerstacktop;
#define handlerpush(f)  (savedtop = handlerstacktop, handlerstacktop = (f))
#define handlerpop()    (handlerstacktop = savedtop)

/* TODO Temporary hacks unless the codebase doesn't jump and uses pass-by-value
 * TODO carrier structs instead of locals */
VL char        *temporary_arg_v_store;
VL void        *temporary_localopts_store;
/* TODO temporary storage to overcome which_protocol() mess (for PROTO_FILE) */
VL char const  *temporary_protocol_ext;

/* The remaining variables need initialization */

#ifndef HAVE_AMALGAMATION
VL char const  month_names[12 + 1][4];
VL char const  weekday_names[7 + 1][4];

VL char const  uagent[];            /* User agent */

VL uc_i const  class_char[];
#endif

/*
 * Finally, let's include the function prototypes XXX embed
 */

#ifndef n_PRIVSEP_SOURCE
# include "nailfuns.h"
#endif

/* s-it-mode */
