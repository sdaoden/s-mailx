/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Some constants etc. for which adjustments may be desired.
 *@ This is included (as mx/config.h) after all the (system) headers.
 *
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef mx_CONFIG_H
# define mx_CONFIG_H

#define ACCOUNT_NULL "null"   /* Name of "null" account */
/* Protocol version for *on-compose-splice** -- update manual on change! */
#define n_DIG_MSG_PLUMBING_VERSION "0 0 1"
#define DOTLOCK_TRIES 5       /* Number of open(2) calls for dotlock */
#define FILE_LOCK_TRIES 10    /* Maximum tries before n_file_lock() fails */
#define FILE_LOCK_MILLIS 200  /* If UIZ_MAX, fall back to that */
#define n_ERROR "ERROR"       /* Is-error?  Also as n_error[] */
#define ERRORS_MAX 1000       /* Maximum error ring entries TODO configable*/
#define n_ESCAPE "~"          /* Default escape for sending (POSIX) */
#define FTMP_OPEN_TRIES 10    /* Maximum number of Ftmp() open(2) tries */
#define n_FORWARD_INJECT_HEAD "-------- Original Message --------\n" /* DOC! */
#define n_FORWARD_INJECT_TAIL NULL /* DOC! */
#define HSHSIZE 23            /* Hash prime TODO make dynamic, obsolete */
#define n_IMAP_DELIM "/."     /* Directory separator ([0] == replacer, too) */
#define n_MAILDIR_SEPARATOR ':' /* Flag separator character */
#define n_MAXARGC 512         /* Maximum list of raw strings TODO dyn vector! */
#define n_ALIAS_MAXEXP 25     /* Maximum expansion of aliases */
#define n_PATH_DEVNULL "/dev/null"  /* Note: manual uses /dev/null as such */
#define n_QUOTE_INJECT_HEAD "%f wrote:\n\n" /* DOC! */
#define n_QUOTE_INJECT_TAIL NULL /* DOC! */
#define REFERENCES_MAX 20     /* Maximum entries in References: */
#define n_SIGSUSPEND_NOT_WAITPID 1 /* Not waitpid(2), but sigsuspend(2) */
#define n_UNIREPL "\xEF\xBF\xBD" /* Unicode replacement 0xFFFD in UTF-8 */
#define n_VEXPR_REGEX_MAX 16  /* Maximum address. `vexpr' regex(7) matches */

/* * */

/* Default log level */
#define n_LOG_LEVEL su_DBGOR(su_LOG_WARN, su_LOG_CRIT)

/* * */

/* Fallback MIME charsets, if *charset-7bit* and *charset-8bit* are not set.
 * Note: must be lowercase!
 * (Keep in SYNC: ./nail.1:"Character sets", mx/config.h:CHARSET_*!) */
#define CHARSET_7BIT "us-ascii"
#ifdef mx_HAVE_ICONV
# define CHARSET_8BIT "utf-8"
# define CHARSET_8BIT_OKEY charset_8bit
#else
# ifdef mx_HAVE_ALWAYS_UNICODE_LOCALE
#  define CHARSET_8BIT "utf-8"
# else
#  define CHARSET_8BIT "iso-8859-1"
# endif
# define CHARSET_8BIT_OKEY ttycharset
#endif

#ifndef HOST_NAME_MAX
# ifdef _POSIX_HOST_NAME_MAX
#  define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
# else
#  define HOST_NAME_MAX 255
# endif
#endif

/* Supported IDNA implementations */
#define n_IDNA_IMPL_LIBIDN2 0
#define n_IDNA_IMPL_LIBIDN 1
#define n_IDNA_IMPL_IDNKIT 2 /* 1 + 2 */

/* Max readable line width TODO simply use BUFSIZ? */
#if BUFSIZ + 0 > 2560
# define LINESIZE BUFSIZ
#else
# define LINESIZE 2560
#endif
#define BUFFER_SIZE (BUFSIZ >= (1u << 13) ? BUFSIZ : (1u << 14))

/* Auto-reclaimed memory storage: size of the buffers.  Maximum auto-reclaimed
 * storage is that value /2, which is n_CTA()ed to be > 1024 */
#define n_MEMORY_AUTOREC_SIZE 0x2000u
/* Ugly, but avoid dynamic allocation for the management structure! */
#define n_MEMORY_POOL_TYPE_SIZEOF (8 * sizeof(void*))

/* Default *mime-encoding* as enum mime_enc */
#define MIME_DEFAULT_ENCODING MIMEE_QP

/* Maximum allowed line length in a mail before QP folding is necessary), and
 * the real limit we go for */
#define MIME_LINELEN_MAX 998 /* Plus CRLF */
#define MIME_LINELEN_LIMIT (MIME_LINELEN_MAX - 48)

/* Ditto, SHOULD */
#define MIME_LINELEN 78 /* Plus CRLF */

/* And in headers which contain an encoded word according to RFC 2047 there is
 * yet another limit; also RFC 2045: 6.7, (5). */
#define MIME_LINELEN_RFC2047 76

/* TODO PATH_MAX: fixed-size buffer is always wrong (think NFS) */
#ifndef PATH_MAX
# ifdef MAXPATHLEN
#  define PATH_MAX MAXPATHLEN
# else
#  define PATH_MAX 1024 /* _XOPEN_PATH_MAX POSIX 2008/Cor 1-2013 */
# endif
#endif

/* Some environment variables for pipe hooks etc. */
#define n_PIPEENV_FILENAME "MAILX_FILENAME"
#define n_PIPEENV_FILENAME_GENERATED "MAILX_FILENAME_GENERATED"
#define n_PIPEENV_FILENAME_TEMPORARY "MAILX_FILENAME_TEMPORARY"
#define n_PIPEENV_CONTENT "MAILX_CONTENT"
#define n_PIPEENV_CONTENT_EVIDENCE "MAILX_CONTENT_EVIDENCE"
#define n_PIPEENV_EXTERNAL_BODY_URL "MAILX_EXTERNAL_BODY_URL"

/* Maximum number of quote characters (not bytes!) that'll be used on
 * follow lines when compressing leading quote characters */
#define n_QUOTE_MAX 42u

/* How much spaces a <tab> counts when *quote-fold*ing? (power-of-two!) */
#define n_QUOTE_TAB_SPACES 8

/* Supported (external) PRG implementations */
#define n_RANDOM_IMPL_BUILTIN 0
#define n_RANDOM_IMPL_ARC4 1
#define n_RANDOM_IMPL_TLS 2
#define n_RANDOM_IMPL_GETRANDOM 3 /* (both, syscall + library) */
#define n_RANDOM_IMPL_URANDOM 4

/* For long iterative output, like `list', tabulator-completion, etc.,
 * determine the screen width that should be used */
#define n_SCRNWIDTH_FOR_LISTS \
   ((size_t)n_scrnwidth - ((size_t)n_scrnwidth >> 3))

/* Smells fishy after, or asks for shell expansion, dependent on context */
#define n_SHEXP_MAGIC_PATH_CHARS "|&;<>{}()[]*?$`'\"\\"

/* Port to native MS-Windows and to ancient UNIX */
#if !defined S_ISDIR && defined S_IFDIR && defined S_IFMT
# define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif

/* Maximum size of a message that is passed through to the spam system */
#define SPAM_MAXSIZE 420000

#ifndef NAME_MAX
# ifdef _POSIX_NAME_MAX
#  define NAME_MAX _POSIX_NAME_MAX
# else
#  define NAME_MAX 14
# endif
#endif
#if NAME_MAX + 0 < 8
# error NAME_MAX is too small
#endif

#ifndef STDIN_FILENO
# define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
# define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
# define STDERR_FILENO 2
#endif

#ifdef O_CLOEXEC
# define _O_CLOEXEC O_CLOEXEC
# define _CLOEXEC_SET(FD) do {;} while(0)
#else
# define _O_CLOEXEC 0
# define _CLOEXEC_SET(FD) n_fd_cloexec_set(FD)
#endif

#ifdef O_NOCTTY
# define n_O_NOCTTY O_NOCTTY
#else
# define n_O_NOCTTY 0
#endif
#ifdef O_NOFOLLOW
# define n_O_NOFOLLOW O_NOFOLLOW
#else
# define n_O_NOFOLLOW 0
#endif
#define n_O_NOXY_BITS (n_O_NOCTTY | n_O_NOFOLLOW)

#ifdef NSIG_MAX
# undef NSIG
# define NSIG NSIG_MAX
#elif !defined NSIG
# define NSIG ((sizeof(sigset_t) * 8) - 1)
#endif

/* * */

/* Switch indicating necessity of terminal access interface (termcap.c) */
#if defined mx_HAVE_TERMCAP || defined mx_HAVE_COLOUR || defined mx_HAVE_MLE
# define n_HAVE_TCAP
#endif

/* Whether we shall do our memory debug thing */
#if (defined mx_HAVE_DEBUG || defined mx_HAVE_DEVEL) && \
      !defined mx_HAVE_NOMEMDBG
# define mx_HAVE_MEMORY_DEBUG
#endif

/* Number of Not-Yet-Dead calls that are remembered */
#if defined mx_HAVE_DEBUG || defined mx_HAVE_DEVEL || defined mx_HAVE_NYD2
# ifdef mx_HAVE_NYD2
#  define NYD_CALLS_MAX (25 * 84)
# elif defined mx_HAVE_DEVEL
#  define NYD_CALLS_MAX (25 * 42)
# else
#  define NYD_CALLS_MAX (25 * 10)
# endif
#endif

#endif /* mx_CONFIG_H */
/* s-it-mode */
