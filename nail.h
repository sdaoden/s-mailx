/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Header inclusion, macros, constants, types and the global var declarations.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 * The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#include <errno.h>
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

#if !defined NI_MAXHOST || NI_MAXHOST < 1025
# undef NI_MAXHOST
# define NI_MAXHOST     1025
#endif

#ifndef PATH_MAX
# ifdef MAXPATHLEN
#  define PATH_MAX      MAXPATHLEN
# else
#  define PATH_MAX      1024        /* _XOPEN_PATH_MAX POSIX 2008/Cor 1-2013 */
# endif
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

/*  */

#if BUFSIZ > 2560                   /* TODO simply use BUFSIZ? */
# define LINESIZE       BUFSIZ      /* max readable line width */
#else
# define LINESIZE       2560
#endif
#define BUFFER_SIZE     (BUFSIZ >= (1u << 13) ? BUFSIZ : (1u << 14))

#define CBAD            (-15555)
#define APPEND                   /* New mail goes to end of mailbox */
#define ESCAPE          '~'      /* Default escape for sending */
#define FIO_STACK_SIZE  20       /* Maximum recursion for sourcing */
#define HIST_SIZE       242      /* tty.c: history list default size */
#define HSHSIZE         23       /* Hash prime (aliases, vars, macros) */
#define MAXARGC         1024     /* Maximum list of raw strings */
#define MAXEXP          25       /* Maximum expansion of aliases */
#define NYD_CALLS_MAX   1000     /* Number of NYD calls that are remembered */
#define PROMPT_BUFFER_SIZE 80    /* getprompt() bufsize (> 3!) */

#define ACCOUNT_NULL    "null"   /* Name of "null" account */
#define MAILRC          "~/.mailrc"
#define TMPDIR_FALLBACK "/tmp"

#define COLOUR_MSGINFO  "fg=green"
#define COLOUR_PARTINFO "fg=brown"
#define COLOUR_FROM_    "fg=brown"
#define COLOUR_HEADER   "fg=red"
#define COLOUR_UHEADER  "ft=bold,fg=red"
#define COLOUR_PAGERS   "less"
#define COLOUR_TERMS    \
   "cons25,linux,rxvt,rxvt-unicode,screen,sun,"\
   "vt100,vt220,wsvt25,xterm,xterm-color"
#define COLOUR_USER_HEADERS "from,subject"

#define FROM_DATEBUF    64    /* Size of RFC 4155 From_ line date */
#define DATE_DAYSYEAR   365L
#define DATE_SECSMIN    60L
#define DATE_MINSHOUR   60L
#define DATE_HOURSDAY   24L
#define DATE_SECSDAY    (DATE_SECSMIN * DATE_MINSHOUR * DATE_HOURSDAY)

/* Default *encoding* as enum conversion below */
#define MIME_DEFAULT_ENCODING CONV_TOQP

/* Maximum allowed line length in a mail before QP folding is necessary), and
 * the real limit we go for */
#define MIME_LINELEN_MAX   1000
#define MIME_LINELEN_LIMIT (MIME_LINELEN_MAX - 50)

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
 * used first */
#define SBUFFER_SIZE    0x18000u
#define SBUFFER_BUILTIN 0x2000u

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
 * CC support, generic macros etc.
 */

#undef __PREREQ
#if defined __GNUC__ || defined __clang__
# define __EXTEN  __extension__
# ifdef __GNUC__
#  define __PREREQ(X,Y) \
   (__GNUC__ > (X) || (__GNUC__ == (X) && __GNUC_MINOR__ >= (Y)))
# else
#  define __PREREQ(X,Y) 1
# endif
#else
# define __EXTEN
# define __PREREQ(X,Y)  0
#endif

#define EMPTY_FILE(F)   typedef int CONCAT(avoid_empty_file__, F);

/* Pointer to size_t */
#define PTR2SIZE(X)     ((size_t)(uintptr_t)(X))

/* Pointer comparison (types from below) */
#define PTRCMP(A,C,B)   ((uintptr_t)(A) C (uintptr_t)(B))

/* Ditto, compare (maybe mixed-signed) integers cases to T bits, unsigned;
 * Note: doesn't sign-extend correctly, that's still up to the caller */
#define UICMP(T,A,C,B)  ((ui ## T ## _t)(A) C (ui ## T ## _t)(B))

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
# define VFIELD_SIZE(X) (X)
# define VFIELD_SIZEOF(T,F) SIZEOF_FIELD(T, F)
# if __PREREQ(2, 9)
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
#elif __PREREQ(3, 4)
# define __FUN__        __FUNCTION__
#else
# define __FUN__        uagent   /* Something that is not a literal */
#endif

#if defined __predict_true && defined __predict_false
# define LIKELY(X)      __predict_true(X)
# define UNLIKELY(X)    __predict_false(X)
#elif __PREREQ(2, 96)
# define LIKELY(X)      __builtin_expect(X, 1)
# define UNLIKELY(X)    __builtin_expect(X, 0)
#else
# define LIKELY(X)      (X)
# define UNLIKELY(X)    (X)
#endif

/* Compile-Time-Assert */
#define CTA(TEST)       _CTA_1(TEST, __LINE__)
#define _CTA_1(TEST,L)  _CTA_2(TEST, L)
#define _CTA_2(TEST,L)  \
   typedef char COMPILE_TIME_ASSERT_failed_at_line_ ## L[(TEST) ? 1 : -1]

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
#else
# define DBG(X)         X
# define NDBG(X)
#endif

/* Translation (init in main.c) */
#undef tr
#ifdef HAVE_CATGETS
# define CATSET         1
# define tr(c,d)        catgets(catd, CATSET, c, d)
#else
# define tr(c,d)        (d)
#endif

/*
 * Types
 */

/* TODO convert all integer types to the new [su]i(8|16|32|64)_t */
typedef unsigned long   ul_it;
typedef unsigned int    ui_it;
typedef unsigned short  us_it;
typedef unsigned char   uc_it;

typedef signed long     sl_it;
typedef signed int      si_it;
typedef signed short    ss_it;
typedef signed char     sc_it;

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

#ifdef UINT64_MAX
# define UI64_MAX       UINT64_MAX
# define SI64_MIN       INT64_MIN
# define SI64_MAX       INT64_MAX
typedef uint64_t        ui64_t;
#elif ULONG_MAX <= 0xFFFFFFFFu
# if !defined ULLONG_MAX || ULLONG_MAX != 0xFFFFFFFFFFFFFFFFu
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

/* (So that we can use UICMP() for size_t comparison, too) */
typedef size_t          uiz_t;
typedef ssize_t         siz_t;

#ifndef UINTPTR_MAX
# ifdef SIZE_MAX
#  define uintptr_t     size_t
#  define UINTPTR_MAX   SIZE_MAX
# else
#  define uintptr_t     unsigned long
#  define UINTPTR_MAX   ULONG_MAX
# endif
#endif

/* XXX Note we don't really deal with that the right way in that we pass size_t
 * XXX arguments without casting; should do, because above we assert UINT_MAX
 * XXX is indeed ui32_t */
#if defined __STDC_VERSION__ && __STDC_VERSION__ + 0 >= 199901L
# define ZFMT           "zu"
#elif defined SIZE_MAX && SIZE_MAX == 0xFFFFFFFFu && ULONG_MAX != UINT_MAX
# define ZFMT           "u"
#endif
#ifndef ZFMT
# define ZFMT           "lu"
#endif

enum {FAL0, TRU1};
typedef si8_t           bool_t;

typedef void (          *sighandler_type)(int);

enum user_options {
   OPT_NONE,
   OPT_DEBUG      = 1u<< 0,   /* -d / *debug* */
   OPT_VERBOSE    = 1u<< 1,   /* -v / *verbose* */
   OPT_EXISTONLY  = 1u<< 2,   /* -e */
   OPT_HEADERSONLY = 1u<< 3,  /* -H */
   OPT_HEADERLIST = 1u<< 4,   /* -L */
   OPT_NOSRC      = 1u<< 5,   /* -n */
   OPT_E_FLAG     = 1u<< 6,   /* -E / *skipemptybody* */
   OPT_F_FLAG     = 1u<< 7,   /* -F */
   OPT_N_FLAG     = 1u<< 8,   /* -N / *header* */
   OPT_R_FLAG     = 1u<< 9,   /* -R */
   OPT_r_FLAG     = 1u<<10,   /* -r (plus option_r_arg) */
   OPT_t_FLAG     = 1u<<11,   /* -t */
   OPT_u_FLAG     = 1u<<12,   /* -u given, or USER != getpwnam(3) */
   OPT_TILDE_FLAG = 1u<<13,   /* -~ */
   OPT_BATCH_FLAG = 1u<<14,   /* -# */

   OPT_SENDMODE   = 1u<<15,   /* Usage case forces send mode */
   OPT_INTERACTIVE = 1u<<16,  /* isatty(0) */
   OPT_TTYIN      = OPT_INTERACTIVE,
   OPT_TTYOUT     = 1u<<17
};
#define IS_TTY_SESSION() \
   ((options & (OPT_TTYIN | OPT_TTYOUT)) == (OPT_TTYIN | OPT_TTYOUT))

enum exit_status {
   EXIT_OK        = EXIT_SUCCESS,
   EXIT_ERR       = EXIT_FAILURE,
   EXIT_USE       = 2,
   EXIT_COLL_ABORT = 1<<1,    /* Message collection was aborted */
   EXIT_SEND_ERROR = 1<<2     /* Unspecified send error occurred */
};

enum fexp_mode {
   FEXP_FULL,                 /* Full expansion */
   FEXP_LOCAL     = 1<<0,     /* Result must be local file/maildir */
   FEXP_SHELL     = 1<<1,     /* No folder %,#,&,+ stuff, yet sh(1) */
   FEXP_NSHORTCUT = 1<<2,     /* Don't expand shortcuts */
   FEXP_SILENT    = 1<<3,     /* Don't print but only return errors */
   FEXP_MULTIOK   = 1<<4      /* Expansion to many entries is ok */
};

enum flock_type {
   FLOCK_READ,
   FLOCK_WRITE,
   FLOCK_UNLOCK
};

/* <0 means "stop" unless *prompt* extensions are enabled. */
enum prompt_exp {
   PROMPT_STOP    = -1,       /* \c */
   /* *prompt* extensions: \$, \@ etc. */
   PROMPT_DOLLAR  = -2,
   PROMPT_AT      = -3
};

enum colourspec {
   COLOURSPEC_MSGINFO,
   COLOURSPEC_PARTINFO,
   COLOURSPEC_FROM_,
   COLOURSPEC_HEADER,
   COLOURSPEC_UHEADER,
   COLOURSPEC_RESET
};

enum okay {
   STOP = 0,
   OKAY = 1
};

enum mimeenc {
   MIME_NONE,        /* message is not in MIME format */
   MIME_BIN,         /* message is in binary encoding */
   MIME_8B,          /* message is in 8bit encoding */
   MIME_7B,          /* message is in 7bit encoding */
   MIME_QP,          /* message is quoted-printable */
   MIME_B64          /* message is in base64 encoding */
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

enum sendaction {
   SEND_MBOX,        /* no conversion to perform */
   SEND_RFC822,      /* no conversion, no From_ line */
   SEND_TODISP,      /* convert to displayable form */
   SEND_TODISP_ALL,  /* same, include all MIME parts */
   SEND_SHOW,        /* convert to 'show' command form */
   SEND_TOSRCH,      /* convert for IMAP SEARCH */
   SEND_TOFLTR,      /* convert for spam mail filtering */
   SEND_TOFILE,      /* convert for saving body to a file */
   SEND_TOPIPE,      /* convert for pipe-content/subc. */
   SEND_QUOTE,       /* convert for quoting */
   SEND_QUOTE_ALL,   /* same, include all MIME parts */
   SEND_DECRYPT      /* decrypt */
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
   MIME_DIGEST,      /* multipart/digest content */
   MIME_MULTI,       /* other multipart/ content */
   MIME_PKCS7,       /* PKCS7 content */
   MIME_DISCARD      /* content is discarded */
};

enum oflags {
   OF_RDONLY      = 1<<0,
   OF_WRONLY      = 1<<1,
   OF_RDWR        = 1<<2,
   OF_APPEND      = 1<<3,
   OF_CREATE      = 1<<4,
   OF_TRUNC       = 1<<5,
   OF_EXCL        = 1<<6,
   OF_CLOEXEC     = 1<<7,
   OF_UNLINK      = 1<<8,     /* Only for Ftmp(): unlink(2) after creation */
   OF_HOLDSIGS    = 1<<9,     /* Mutual with OF_UNLINK - await Ftmp_free() */
   OF_REGISTER    = 1<<10     /* Register file in our file table */
};

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

enum protocol {
   PROTO_FILE,       /* refers to a local file */
   PROTO_POP3,       /* is a pop3 server string */
   PROTO_IMAP,       /* is an imap server string */
   PROTO_MAILDIR,    /* refers to a maildir folder */
   PROTO_UNKNOWN     /* unknown protocol */
};

#ifdef HAVE_SSL
enum ssl_verify_level {
   SSL_VERIFY_IGNORE,
   SSL_VERIFY_WARN,
   SSL_VERIFY_ASK,
   SSL_VERIFY_STRICT
};
#endif

/* A large enum with all the binary and value options a.k.a their keys.
 * Only the constant keys are in here, to be looked up via ok_[bv]look(),
 * ok_[bv]set() and ok_[bv]clear().
 * Note: see the comments in acmava.c before changing *anything* in here! */
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
   ok_b_debug,                         /* {special=1} */
   ok_b_disconnected,
   ok_b_dot,
   ok_b_editalong,
   ok_b_editheaders,
   ok_b_emptybox,
   ok_b_emptystart,
   ok_b_flipr,
   ok_b_forward_as_attachment,
   ok_b_fullnames,
   ok_b_header,                        /* {special=1} */
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
   ok_b_message_id_disable,
   ok_b_metoo,
   ok_b_mime_allow_text_controls,
   ok_b_mime_counter_evidence,
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
   ok_b_Replyall,
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
   ok_b_ssl_v2_allow,
   ok_b_writebackedited,
   ok_b_verbose,                       /* {special=1} */

   /* Option keys for values options */
   ok_v_attrlist,
   ok_v_autobcc,
   ok_v_autocc,
   ok_v_autosort,
   ok_v_charset_7bit,
   ok_v_charset_8bit,
   ok_v_cmd,
   ok_v_colour_pagers,
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
   ok_v_folder,                        /* {special=1} */
   ok_v_folder_hook,
   ok_v_from,
   ok_v_fwdheading,
   ok_v_headline,
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
   ok_v_mimetypes_load_control,
   ok_v_NAIL_EXTRA_RC,                 /* {name=NAIL_EXTRA_RC} */
   ok_v_NAIL_HEAD,                     /* {name=NAIL_HEAD} */
   ok_v_NAIL_HISTFILE,                 /* {name=NAIL_HISTFILE} */
   ok_v_NAIL_HISTSIZE,                 /* {name=NAIL_HISTSIZE} */
   ok_v_NAIL_TAIL,                     /* {name=NAIL_TAIL} */
   ok_v_newfolders,
   ok_v_newmail,
   ok_v_ORGANIZATION,
   ok_v_PAGER,
   ok_v_pop3_keepalive,
   ok_v_prompt,
   ok_v_quote,
   ok_v_quote_fold,
   ok_v_record,
   ok_v_replyto,
   ok_v_screen,
   ok_v_sendcharsets,
   ok_v_sender,
   ok_v_sendmail,
   ok_v_sendmail_progname,
   ok_v_SHELL,
   ok_v_Sign,
   ok_v_sign,
   ok_v_signature,
   ok_v_smime_ca_dir,
   ok_v_smime_ca_file,
   ok_v_smime_crl_dir,
   ok_v_smime_crl_file,
   ok_v_smime_sign_cert,
   ok_v_smime_sign_include_certs,
   ok_v_smtp,
   ok_v_smtp_auth,
   ok_v_smtp_auth_password,
   ok_v_smtp_auth_user,
   ok_v_spam_command,
   ok_v_spam_host,
   ok_v_spam_maxsize,
   ok_v_spam_port,
   ok_v_spam_socket,
   ok_v_spam_user,
   ok_v_ssl_ca_dir,
   ok_v_ssl_ca_file,
   ok_v_ssl_cert,
   ok_v_ssl_cipher_list,
   ok_v_ssl_crl_dir,
   ok_v_ssl_crl_file,
   ok_v_ssl_key,
   ok_v_ssl_method,
   ok_v_ssl_rand_egd,
   ok_v_ssl_rand_file,
   ok_v_ssl_verify,
   ok_v_stealthmua,
   ok_v_toplines,
   ok_v_ttycharset,
   ok_v_VISUAL
};

struct str {
   char     *s;      /* the string's content */
   size_t   l;       /* the stings's length */
};

struct colour_table {
   /* Plus a copy of *colour-user-headers* */
   struct str  ct_csinfo[COLOURSPEC_RESET+1 + 1];
};

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

struct search_expr {
   char const  *ss_where;  /* to search for the expression (not always used) */
   char const  *ss_sexpr;  /* String search expression; NULL: use .ss_reexpr */
#ifdef HAVE_REGEX
   regex_t     ss_reexpr;
#endif
};

struct eval_ctx {
   struct str  ev_line;
   bool_t      ev_is_recursive;  /* Evaluation in evaluation? (collect ~:) */
   ui8_t       __dummy[6];
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
   void        *s_ctx;        /* SSL context object */
# endif
#endif
   char        *s_wbuf;       /* for buffered writes */
   int         s_wbufsize;    /* allocated size of s_buf */
   int         s_wbufpos;     /* position of first empty data byte */
   char        *s_rbufptr;    /* read pointer to s_rbuf */
   int         s_rsz;         /* size of last read in s_rbuf */
   char const  *s_desc;       /* description of error messages */
   void        (*s_onclose)(void); /* execute on close */
   char        s_rbuf[LINESIZE + 1]; /* for buffered reads */
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
   int mb_compressed;         /* is a compressed mbox file */
   int mb_threaded;           /* mailbox has been threaded */
#ifdef HAVE_IMAP
   enum mbflags {
      MB_NOFLAGS  = 000,
      MB_UIDPLUS  = 001 /* supports IMAP UIDPLUS */
   }           mb_flags;
   unsigned long  mb_uidvalidity;   /* IMAP unique identifier validity */
   char        *mb_imap_account;    /* name of current IMAP account */
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
   MSPAM          = (1<<27)   /* message is classified as spam */
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
   char        *m_ct_type;          /* content-type */
   char        *m_ct_type_plain;    /* content-type without specs */
   enum mimecontent m_mimecontent;  /* same in enum */
   char const  *m_charset;    /* charset */
   char        *m_ct_transfer_enc;  /* content-transfer-encoding */
   enum mimeenc m_mimeenc;     /* same in enum */
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
   ARG_W          = 1u<<13    /* Illegal when read only bit */
};

enum gfield {
   GTO            = 1,        /* Grab To: line */
   GSUBJECT       = 2,        /* Likewise, Subject: line */
   GCC            = 4,        /* And the Cc: line */
   GBCC           = 8,        /* And also the Bcc: line */

   GNL            = 16,       /* Print blank line after */
   GDEL           = 32,       /* Entity removed from list */
   GCOMMA         = 64,       /* detract puts in commas */
   GUA            = 128,      /* User-Agent field */
   GMIME          = 256,      /* MIME 1.0 fields */
   GMSGID         = 512,      /* a Message-ID */
   /* 1024 */
   GIDENT         = 2048,     /* From:, Reply-To: and Organization: field */
   GREF           = 4096,     /* References: field */
   GDATE          = 8192,     /* Date: field */
   GFULL          = 16384,    /* include full names */
   GSKIN          = 32768,    /* skin names */
   GEXTRA         = 65536,    /* extra fields */
   GFILES         = 131072    /* include filename addresses */
};
#define GMASK           (GTO | GSUBJECT | GCC | GBCC)

/* Structure used to pass about the current state of a message (header) */
struct header {
   struct name *h_to;         /* Dynamic "To:" string */
   char        *h_subject;    /* Subject string */
   struct name *h_cc;         /* Carbon copies string */
   struct name *h_bcc;        /* Blind carbon copies */
   struct name *h_ref;        /* References */
   struct attachment *h_attach; /* MIME attachments */
   char        *h_charset;    /* preferred charset */
   struct name *h_from;       /* overridden "From:" field */
   struct name *h_replyto;    /* overridden "Reply-To:" field */
   struct name *h_sender;     /* overridden "Sender:" field */
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
   NAME_ADDRSPEC_ERR_EMPTY = 1<< 7, /* An empty string (or NULL) */
   NAME_ADDRSPEC_ERR_ATSEQ = 1<< 8, /* Weird @ sequence */
   NAME_ADDRSPEC_ERR_CHAR  = 1<< 9, /* Invalid character */
   NAME_ADDRSPEC_ERR_IDNA  = 1<<10, /* IDNA convertion failed */
   NAME_ADDRSPEC_INVALID   = NAME_ADDRSPEC_ERR_EMPTY |
         NAME_ADDRSPEC_ERR_ATSEQ | NAME_ADDRSPEC_ERR_CHAR |
         NAME_ADDRSPEC_ERR_IDNA,

   _NAME_SHIFTWC  = 11,
   _NAME_MAXWC    = 0xFFFFF,
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
   char        *n_name;       /* This fella's name */
   char        *n_fullname;   /* Sometimes, name including comment */
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

struct group {
   struct group *ge_link;     /* Next person in this group */
   char        *ge_name;      /* This person's user name */
};

struct grouphead {
   struct grouphead *g_link;  /* Next grouphead in list */
   char        *g_name;       /* Name of this group */
   struct group *g_list;      /* Users in group. */
};

/* Structure of the hash table of ignored header fields */
struct ignoretab {
   int         i_count;       /* Number of entries */
   struct ignore {
      struct ignore  *i_link;    /* Next ignored field in bucket */
      char           *i_field;   /* This ignored field */
   }           *i_head[HSHSIZE];
};

/* Token values returned by the scanner used for argument lists.
 * Also, sizes of scanner-related things */
enum ltoken {
   TEOL           = 0,        /* End of the command line */
   TNUMBER        = 1,        /* A message number */
   TDASH          = 2,        /* A simple dash */
   TSTRING        = 3,        /* A string (possibly containing -) */
   TDOT           = 4,        /* A "." */
   TUP            = 5,        /* An "^" */
   TDOLLAR        = 6,        /* A "$" */
   TSTAR          = 7,        /* A "*" */
   TOPEN          = 8,        /* An '(' */
   TCLOSE         = 9,        /* A ')' */
   TPLUS          = 10,       /* A '+' */
   TERROR         = 11,       /* A lexical error */
   TCOMMA         = 12,       /* A ',' */
   TSEMI          = 13,       /* A ';' */
   TBACK          = 14        /* A '`' */
};

#define REGDEP          2     /* Maximum regret depth. */

/* For the 'shortcut' and 'unshortcut' functionality */
struct shortcut {
   struct shortcut *sh_next;  /* next shortcut in list */
   char        *sh_short;     /* shortcut string */
   char        *sh_long;      /* expanded form */
};

/* Kludges to handle the change from setexit / reset to setjmp / longjmp */
#define setexit()       (void)sigsetjmp(srbuf, 1)
#define reset(x)        siglongjmp(srbuf, x)

/* Content-Transfer-Encodings as defined in RFC 2045:
 * - Quoted-Printable, section 6.7
 * - Base64, section 6.8 */

#define QP_LINESIZE     (4 * 19)       /* Max. compliant QP linesize */

#define B64_LINESIZE    (4 * 19)       /* Max. compliant Base64 linesize */
#define B64_ENCODE_INPUT_PER_LINE 57   /* Max. input for Base64 encode/line */

/* xxx QP came later, maybe rewrite all to use mimecte_flags directly? */
enum __mimecte_flags {
   MIMECTE_NONE,
   MIMECTE_SALLOC = 1<<0,     /* Use salloc(), not srealloc().. */
   /* ..result .s,.l point to user buffer of *_LINESIZE+[+[+]] bytes instead */
   MIMECTE_BUF    = 1<<1,
   MIMECTE_CRLF   = 1<<2,     /* (encode) Append "\r\n" to lines */
   MIMECTE_LF     = 1<<3,     /* (encode) Append "\n" to lines */
   /* (encode) If one of _CRLF/_LF is set, honour *_LINESIZE+[+[+]] and
    * inject the desired line-ending whenever a linewrap is desired */
   MIMECTE_MULTILINE = 1<<4,
   /* (encode) Quote with header rules, do not generate soft NL breaks? */
   MIMECTE_ISHEAD = 1<<5
};

enum qpflags {
   QP_NONE        = MIMECTE_NONE,
   QP_SALLOC      = MIMECTE_SALLOC,
   QP_BUF         = MIMECTE_BUF,
   QP_ISHEAD      = MIMECTE_ISHEAD
};

enum b64flags {
   B64_NONE       = MIMECTE_NONE,
   B64_SALLOC     = MIMECTE_SALLOC,
   B64_BUF        = MIMECTE_BUF,
   B64_CRLF       = MIMECTE_CRLF,
   B64_LF         = MIMECTE_LF,
   B64_MULTILINE  = MIMECTE_MULTILINE
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

#define __ischarof(C, FLAGS)  \
   (asciichar(C) && (class_char[(uc_it)(C)] & (FLAGS)) != 0)

#define asciichar(c)    ((uc_it)(c) <= 0177)
#define alnumchar(c)    __ischarof(c, C_DIGIT | C_OCTAL | C_UPPER | C_LOWER)
#define alphachar(c)    __ischarof(c, C_UPPER | C_LOWER)
#define blankchar(c)    __ischarof(c, C_BLANK)
#define blankspacechar(c) __ischarof(c, C_BLANK | C_SPACE)
#define cntrlchar(c)    __ischarof(c, C_CNTRL)
#define digitchar(c)    __ischarof(c, C_DIGIT | C_OCTAL)
#define lowerchar(c)    __ischarof(c, C_LOWER)
#define punctchar(c)    __ischarof(c, C_PUNCT)
#define spacechar(c)    __ischarof(c, C_BLANK | C_SPACE | C_WHITE)
#define upperchar(c)    __ischarof(c, C_UPPER)
#define whitechar(c)    __ischarof(c, C_BLANK | C_WHITE)
#define octalchar(c)    __ischarof(c, C_OCTAL)

#define upperconv(c)    (lowerchar(c) ? (char)((uc_it)(c) - 'a' + 'A') : (c))
#define lowerconv(c)    (upperchar(c) ? (char)((uc_it)(c) - 'A' + 'a') : (c))
/* RFC 822, 3.2. */
#define fieldnamechar(c) \
   (asciichar(c) && (c) > 040 && (c) != 0177 && (c) != ':')

/* Try to use alloca() for some function-local buffers and data, fall back to
 * smalloc()/free() if not available */
#ifdef HAVE_ALLOCA
# define ac_alloc(n)    HAVE_ALLOCA(n)
# define ac_free(n)     do {UNUSED(n);} while (0)
#else
# define ac_alloc(n)    smalloc(n)
# define ac_free(n)     free(n)
#endif

/* Single-threaded, use unlocked I/O */
#ifdef HAVE_PUTC_UNLOCKED
# undef getc
# define getc(c)        getc_unlocked(c)
# undef putc
# define putc(c, f)     putc_unlocked(c, f)
# undef putchar
# define putchar(c)     putc_unlocked((c), stdout)
#endif

/* Truncate a file to the last character written.  This is useful just before
 * closing an old file that was opened for read/write */
#define ftrunc(stream) \
do {\
   off_t off;\
   fflush(stream);\
   off = ftell(stream);\
   if (off >= 0)\
      ftruncate(fileno(stream), off);\
} while (0)

/* fflush() and rewind() */
#define fflush_rewind(stream) \
do {\
   fflush(stream);\
   rewind(stream);\
} while (0)

/* There are problems with dup()ing of file-descriptors for child processes.
 * As long as those are not fixed in equal spirit to (outof(): FIX and
 * recode.., 2012-10-04), and to avoid reviving of bugs like (If *record* is
 * set, avoid writing dead content twice.., 2012-09-14), we have to somehow
 * accomplish that the FILE* fp makes itself comfortable with the *real* offset
 * of the underlaying file descriptor.  Unfortunately Standard I/O and POSIX
 * don't describe a way for that -- fflush();rewind(); won't do it.  This
 * fseek(END),rewind() pair works around the problem on *BSD and Linux.
 * Update as of 2014-03-03: with Issue 7 POSIX has overloaded fflush(3): if
 * used on a readable stream, then
 *
 *    if the file is not already at EOF, and the file is one capable of
 *    seeking, the file offset of the underlying open file description shall
 *    be set to the file position of the stream.
 *
 * We need our own, simplified and reliable I/O */
#if defined _POSIX_VERSION && _POSIX_VERSION + 0 >= 200809L
# define really_rewind(stream) \
do {\
   rewind(stream);\
   fflush(stream);\
} while (0)
#else
# define really_rewind(stream) \
do {\
   fseek(stream, 0, SEEK_END);\
   rewind(stream);\
} while (0)
#endif

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
#ifdef _MAIN_SOURCE
# ifndef HAVE_AMALGAMATION
#  define VL
# else
#  define VL            static
# endif
#else
# define VL             extern
#endif

VL gid_t       effectivegid;        /* Saved from when we started up */
VL gid_t       realgid;             /* Saved from when we started up */

VL int         mb_cur_max;          /* Value of MB_CUR_MAX */
VL int         realscreenheight;    /* The real screen height */
VL int         scrnwidth;           /* Screen width, or best guess */
VL int         scrnheight;          /* Screen height/guess (4 header) */
VL int         utf8;                /* Locale uses UTF-8 encoding */
VL int         enc_has_state;       /* Encoding has shift states */

VL char        **altnames;          /* List of alternate names of user */
VL char const  *homedir;            /* Path name of home directory */
VL char const  *myname;             /* My login name */
VL char const  *progname;           /* Our name */
VL char const  *tempdir;            /* The temporary directory */

VL int         exit_status;         /* Exit status */
VL int         options;             /* Bits of enum user_options */
VL char        *option_r_arg;       /* Argument to -r option */
VL char const  **smopts;            /* sendmail(1) opts from commline */
VL size_t      smopts_count;        /* Entries in smopts */

/* TODO Join as many of these state machine bits into a single carrier! */
VL int         inhook;              /* Currently executing a hook */
VL bool_t      exec_last_comm_error; /* Last execute() command failed */
VL bool_t      edit;                /* Indicates editing a file */
VL bool_t      did_print_dot;       /* Current message has been printed */
VL bool_t      list_saw_numbers;    /* Last *LIST saw numerics */
VL bool_t      msglist_is_single;   /* Last NDMLIST/MSGLIST chose 1 msg */
VL bool_t      loading;             /* Loading user definitions */
VL bool_t      sourcing;            /* Currently reading variant file */
VL bool_t      sawcom;              /* Set after first command */
VL bool_t      starting;            /* Still in startup code */
VL bool_t      var_clear_allow_undefined; /* v?ok_[bv]clear(): no complain */
VL int         noreset;             /* String resets suspended */

/* XXX stylish sorting */
VL int            msgCount;            /* Count of messages read in */
VL struct mailbox mb;                  /* Current mailbox */
VL int            image;               /* File descriptor for msg image */
VL char           mailname[PATH_MAX];  /* Name of current file */
VL char           displayname[80 - 40]; /* Prettyfied for display */
VL char           prevfile[PATH_MAX];  /* Name of previous file */
VL char const     *account_name;       /* Current account name or NULL */
VL off_t          mailsize;            /* Size of system mailbox */
VL struct message *dot;                /* Pointer to current message */
VL struct message *prevdot;            /* Previous current message */
VL struct message *message;            /* The actual message structure */
VL struct message *threadroot;         /* first threaded message */
VL int            imap_created_mailbox; /* hack to get feedback from imap */

VL struct grouphead  *groups[HSHSIZE]; /* Pointer to active groups */
VL struct ignoretab  ignore[2];        /* ignored and retained fields
                                        * 0 is ignore, 1 is retain */
VL struct ignoretab  saveignore[2];    /* ignored and retained fields
                                        * on save to folder */
VL struct ignoretab  allignore[2];     /* special, ignore all headers */
VL struct ignoretab  fwdignore[2];     /* fields to ignore for forwarding */
VL struct shortcut   *shortcuts;       /* list of shortcuts */

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

#ifdef HAVE_CATGETS
VL nl_catd     catd;
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

/* The remaining variables need initialization */

#ifndef HAVE_AMALGAMATION
VL char const  month_names[12 + 1][4];
VL char const  weekday_names[7 + 1][4];

VL char const  uagent[];            /* User agent */
VL char const  version[];           /* The version string */
VL char const  features[];          /* The "feature string" */

VL uc_it const class_char[];
#endif

/*
 * Finally, let's include the function prototypes XXX embed
 */

#include "extern.h"

/* vim:set fenc=utf-8:s-it-mode */
