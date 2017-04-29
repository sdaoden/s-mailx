/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Some constants etc. for which adjustments may be desired.
 *@ This is included after all the (system) headers.
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
#ifndef n_CONFIG_H
# define n_CONFIG_H

#define APPEND                   /* New mail goes to end of mailbox */
#define CBAD            (-15555)
#define DOTLOCK_TRIES   5        /* Number of open(2) calls for dotlock */
#define FILE_LOCK_TRIES 10       /* Maximum tries before n_file_lock() fails */
#define FILE_LOCK_MILLIS 200     /* If UIZ_MAX, fall back to that */
#define n_ERROR "ERROR"          /* Is-error?  Also as n_error[] */
#define ERRORS_MAX      1000     /* Maximum error ring entries TODO configable*/
#define n_ESCAPE        '~'      /* Default escape for sending */
#define HSHSIZE         23       /* Hash prime TODO make dynamic, obsolete */
#define n_MAXARGC       512      /* Maximum list of raw strings */
#define MAXEXP          25       /* Maximum expansion of aliases */
#define REFERENCES_MAX  20       /* Maximum entries in References: */
#define n_UNIREPL "\xEF\xBF\xBD" /* 0xFFFD in UTF-8 */
#define FTMP_OPEN_TRIES 10       /* Maximum number of Ftmp() open(2) tries */
#define n_VEXPR_REGEX_MAX 10     /* Maximum address. `vexpr' regex(7) matches */

/* Max readable line width TODO simply use BUFSIZ? */
#if BUFSIZ + 0 > 2560
# define LINESIZE BUFSIZ
#else
# define LINESIZE 2560
#endif
#define BUFFER_SIZE (BUFSIZ >= (1u << 13) ? BUFSIZ : (1u << 14))

/* *indentprefix* default as of POSIX */
#define INDENT_DEFAULT  "\t"

/* Auto-reclaimed memory storage: size of the buffers.  Maximum auto-reclaimed
 * storage is that value /2, which is n_CTA()ed to be > 1024 */
#define n_MEMORY_AUTOREC_SIZE 0x2000u
/* Ugly, but avoid dynamic allocation for the management structure! */
#define n_MEMORY_POOL_TYPE_SIZEOF (7 * sizeof(void*))

/* Default *encoding* as enum mime_enc */
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
#define n_QUOTE_IS_A(W) \
   ((W) == n_WC_C('>') || (W) == n_WC_C('|') ||\
    (W) == n_WC_C('}') || (W) == n_WC_C(':'))

/* Maximum number of quote characters (not bytes!) that'll be used on
 * follow lines when compressing leading quote characters */
#define n_QUOTE_MAX 42u

/* How much spaces should a <tab> count when *quote-fold*ing? (power-of-two!) */
#define n_QUOTE_TAB_SPACES 8

/* For long iterative output, like `list', tabulator-completion, etc.,
 * determine the screen width that should be used */
#define n_SCRNWIDTH_FOR_LISTS ((size_t)n_scrnwidth - ((size_t)n_scrnwidth >> 3))

/* Smells fishy after, or asks for shell expansion, dependent on context */
#define n_SHEXP_MAGIC_PATH_CHARS "|&;<>{}()[]*?$`'\"\\"

/* Maximum size of a message that is passed through to the spam system */
#define SPAM_MAXSIZE    420000

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

#endif /* n_CONFIG_H */
/* s-it-mode */
