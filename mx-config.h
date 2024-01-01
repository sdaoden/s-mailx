/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Some constants etc. for which adjustments may be desired.
 *@ This is included (as mx/config.h) after all the (system) headers.
 *@ TODO It is a wild name convention mess, to say the least.
 *
 * Copyright (c) 2012 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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

#define ACCOUNT_NULL "null" /* Name of "null" account */
#define n_ALIAS_MAXEXP 25 /* Maximum expansion of aliases */

#define mx_CONTENT_DESC_FORWARDED_MESSAGE "Forwarded message"
#define mx_CONTENT_DESC_QUOTE_ATTACHMENT "Original message content"
#define mx_CONTENT_DESC_SMIME_MESSAGE "S/MIME encrypted message"
#define mx_CONTENT_DESC_SMIME_SIG "S/MIME digital signature"

#define mx_DOTLOCK_TRIES 5 /* Number of open(2) calls for dotlock */

#define n_ERROR "ERROR" /* Is-error?  Also as n_error[] */
#define n_ESCAPE "~" /* Default escape for sending (POSIX standard) */

#define mx_FILE_LOCK_TRIES 10 /* Maximum tries before file_lock() fails */
#define mx_FILE_LOCK_MILLIS 200 /* Wait time in between tries */
#define n_FORWARD_INJECT_HEAD "-------- Original Message --------\n" /* DOC! */
#define n_FORWARD_INJECT_TAIL NIL /* DOC! */
#define mx_FS_FILETYPE_CAT_PROG "cat" /* cat(1) */
#define mx_FS_TMP_OPEN_TRIES 42 /* Maximum number of fs_tmp_open() tries */

#define n_IMAP_DELIM "/." /* Directory separator ([0] == replacer, too) */

#define n_LINE_EDITOR_CPL_WORD_BREAKS "\"'@=;|:"
/* Fallback in case the systems reports an empty hostname (?) */
#define n_LOCALHOST_DEFAULT_NAME "localhost.localdomain"

#define n_MAILDIR_SEPARATOR ':' /* Flag separator character */
#define n_MAXARGC 512 /* Maximum list of raw strings TODO dyn vector! */

#define n_QUOTE_INJECT_HEAD "%f wrote:\n\n" /* DOC! */
#define n_QUOTE_INJECT_TAIL NIL /* DOC! */

#define REFERENCES_MAX 20 /* Maximum entries in References: */

/* *bind-inter-byte-timeout* default -- update manual on change! */
#define mx_BIND_INTER_BYTE_TIMEOUT "200"

/* * */

/* Default log level */
#define n_LOG_LEVEL su_DBGOR(su_LOG_WARN, su_LOG_CRIT)

/* * */

/* Fallback MIME charsets, if *charset-7bit* and *charset-8bit* are not set.
 * Note: iconv_norm_name() MUST NOT cause modification! */
#define CHARSET_7BIT "us-ascii"
#ifdef mx_HAVE_ICONV
# define CHARSET_8BIT "utf-8"
# define CHARSET_8BIT_OKEY ok_v_charset_8bit
#else
# ifdef mx_HAVE_ALWAYS_UNICODE_LOCALE
#  define CHARSET_8BIT "utf-8"
# else
#  define CHARSET_8BIT "iso-8859-1"
# endif
# define CHARSET_8BIT_OKEY ok_v_ttycharset
#endif

#ifndef HOST_NAME_MAX
# ifdef _POSIX_HOST_NAME_MAX
#  define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
# else
#  define HOST_NAME_MAX 255
# endif
#endif

/* Supported [mx_HAVE_]IDNA implementations TODO should not be here!?! */
#define n_IDNA_IMPL_LIBIDN2 0
#define n_IDNA_IMPL_LIBIDN 1
#define n_IDNA_IMPL_IDNKIT 2 /* 1 + 2 */

/* Max line width (less X so that PAGE_SIZE*2 usually suffices for all) */
#define mx_LINESIZE ((su_PAGE_SIZE * 2) - (su_PAGE_SIZE / 2))
#define mx_LINEPOOL_QUEUE_MAX 2

/* Our I/O buffer size */
#define mx_BUFFER_SIZE (BUFSIZ >= (1u << 13) ? BUFSIZ : (1u << 14))

/* Default *mime-encoding* as enum mx_mime_enc; ONLY one of _B64, _QP, _8B */
#define mx_MIME_DEFAULT_ENCODING mx_MIME_ENC_QP

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

/* Maximum number of quote characters (not bytes!) that will be used on
 * follow lines when compressing leading quote characters */
#define n_QUOTE_MAX 42u

/* How much spaces a <tab> counts when *quote-fold*ing? (power-of-two!) */
#define n_QUOTE_TAB_SPACES 8

/* Smells fishy after, or asks for shell expansion, dependent on context */
#define n_SHEXP_MAGIC_PATH_CHARS "|&;<>{}()[]*?$`'\"\\"

/* Maximum size of a message that is passed through to the spam system */
#define SPAM_MAXSIZE 420000

/* Supported [mx_HAVE_]TLS implementations TODO should not be here!?!
 * In addition mx_HAVE_XTLS is defined if it is OpenSSL/derivate, and set to
 * "a version number", then (or 0 if not tested) */
#define mx_TLS_IMPL_OPENSSL 1
#define mx_TLS_IMPL_RESSL 2

/* ... */

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

#endif /* mx_CONFIG_H */
/* s-it-mode */
