/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Header inclusion, macros, constants, types and the global var declarations.
 *@ TODO Should be split in myriads of FEATURE-GROUP.h headers.  Sort.  def.h.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause TODO ISC
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
#ifndef n_NAIL_H
# define n_NAIL_H

#include <mx/gen-config.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifdef mx_HAVE_GETTIMEOFDAY
# include <sys/time.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef mx_HAVE_REGEX
# include <regex.h>
#endif

/* Many things possibly of interest for adjustments have been outsourced */
#include <mx/config.h>

#include <su/code.h>
#include <su/mem-bag.h> /* TODO should not be needed */

/* TODO fake */
#include "su/code-in.h"

struct mx_dig_msg_ctx;

/*  */
#define n_FROM_DATEBUF 64 /* Size of RFC 4155 From_ line date */
#define n_DATE_DAYSYEAR 365u
#define n_DATE_NANOSSEC (n_DATE_MICROSSEC * 1000)
#define n_DATE_MICROSSEC (n_DATE_MILLISSEC * 1000)
#define n_DATE_MILLISSEC 1000u
#define n_DATE_SECSMIN 60u
#define n_DATE_MINSHOUR 60u
#define n_DATE_HOURSDAY 24u
#define n_DATE_SECSHOUR (n_DATE_SECSMIN * n_DATE_MINSHOUR)
#define n_DATE_SECSDAY (n_DATE_SECSHOUR * n_DATE_HOURSDAY)

/* Network protocol newline */
#define NETNL "\015\012"
#define NETLINE(X) X NETNL

/*
 * OS, CC support, generic macros etc. TODO remove -> SU!
 */

/* CC */

/* Suppress some technical warnings via #pragma's unless developing.
 * XXX Wild guesses: clang(1) 1.7 and (OpenBSD) gcc(1) 4.2.1 don't work */
#ifndef mx_HAVE_DEVEL
# if su_CC_VCHECK_CLANG(3, 4)
#  pragma clang diagnostic ignored "-Wassign-enum"
#  pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#  pragma clang diagnostic ignored "-Wformat"
# elif su_CC_VCHECK_GCC(4, 7)
#  pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#  pragma GCC diagnostic ignored "-Wformat"
# endif
#endif

#undef mx_HAVE_NATCH_CHAR
#if defined mx_HAVE_SETLOCALE && defined mx_HAVE_C90AMEND1 && \
      defined mx_HAVE_WCWIDTH
# define mx_HAVE_NATCH_CHAR
# define n_NATCH_CHAR(X) X
#else
# define n_NATCH_CHAR(X)
#endif

#define n_UNCONST(X) su_UNCONST(void*,X) /* TODO */

/*
 * Types
 */

typedef void (*n_sighdl_t)(int);

enum n_announce_flags{
   n_ANNOUNCE_NONE = 0, /* Only housekeeping */
   n_ANNOUNCE_MAIN_CALL = 1u<<0, /* POSIX covered startup call */
   n_ANNOUNCE_STATUS = 1u<<1, /* Only print status */
   n_ANNOUNCE_CHANGE = 1u<<2, /* Folder changed */

   n__ANNOUNCE_HEADER = 1u<<6,
   n__ANNOUNCE_ANY = 1u<<7
};

enum expand_addr_flags{
   EAF_NONE = 0, /* -> EAF_NOFILE | EAF_NOPIPE */
   EAF_RESTRICT = 1u<<0, /* "restrict" (do unless interaktive / -[~#]) */
   EAF_FAIL = 1u<<1, /* "fail" */
   EAF_FAILINVADDR = 1u<<2, /* "failinvaddr" */
   EAF_DOMAINCHECK = 1u<<3, /* "domaincheck" <-> *expandaddr-domaincheck* */
   EAF_SHEXP_PARSE = 1u<<4, /* shexp_parse() the address first is allowed */
   /* Bits reused by enum expand_addr_check_mode! */
   EAF_FCC = 1u<<8, /* +"fcc" umbrella */
   EAF_FILE = 1u<<9, /* +"file" targets */
   EAF_PIPE = 1u<<10, /* +"pipe" command pipe targets */
   EAF_NAME = 1u<<11, /* +"name"s (non-address) names / MTA aliases */
   EAF_ADDR = 1u<<12, /* +"addr" network address (contain "@") */

   EAF_TARGET_MASK = EAF_FCC | EAF_FILE | EAF_PIPE | EAF_NAME | EAF_ADDR,
   EAF_RESTRICT_TARGETS = EAF_NAME | EAF_ADDR /* (default set if not set) */
   /* TODO HACK!  In pre-v15 we have a control flow problem (it is a general
    * TODO design problem): if n_collect() calls makeheader(), e.g., for -t or
    * TODO because of ~e diting, then that will checkaddr() and that will
    * TODO remove invalid headers.  However, this code path does not know
    * TODO about keeping track of senderrors unless a pointer has been passed,
    * TODO but which it doesn't for ~e, and shall not, too.  Thus, invalid
    * TODO addresses may be automatically removed, silently, and noone will
    * TODO ever know, in particular not regarding "failinvaddr".
    * TODO The hacky solution is this bit -- which can ONLY be used for fields
    * TODO which will be subject to namelist_vaporise_head() later on!! --,
    * TODO if it is set (by n_header_extract()) then checkaddr() will NOT strip
    * TODO invalid headers off IF it deals with a NULL senderror pointer */
   ,EAF_MAYKEEP = 1u<<15
};

enum expand_addr_check_mode{
   EACM_NONE = 0u, /* Don't care about *expandaddr* */
   EACM_NORMAL = 1u<<0, /* Use our normal *expandaddr* checking */
   EACM_STRICT = 1u<<1, /* Never allow any file or pipe addresse */
   EACM_MODE_MASK = 0x3u, /* _NORMAL and _STRICT are mutual! */

   EACM_NOLOG = 1u<<2, /* Do not log check errors */

   /* Some special overwrites of EAF_TARGETs.
    * May NOT clash with EAF_* bits which may be ORd to these here! */

   EACM_NONAME = 1u<<16,
   EACM_NONAME_OR_FAIL = 1u<<17,
   EACM_DOMAINCHECK = 1u<<18 /* Honour it! */
};

enum n_cmd_arg_flags{ /* TODO Most of these need to change, in fact in v15
   * TODO i rather see the mechanism that is used in c_bind() extended and used
   * TODO anywhere, i.e. n_cmd_arg_parse().
   * TODO Note we may NOT support arguments with su_cs_len()>=U32_MAX (?) */
   n_CMD_ARG_TYPE_MSGLIST = 0, /* Message list type */
   n_CMD_ARG_TYPE_NDMLIST = 1, /* Message list, no defaults */
   n_CMD_ARG_TYPE_RAWDAT = 2, /* The plain string in an argv[] */
     n_CMD_ARG_TYPE_STRING = 3, /* A pure string TODO obsolete */
   n_CMD_ARG_TYPE_WYSH = 4, /* getrawlist(), sh(1) compatible */
      n_CMD_ARG_TYPE_RAWLIST = 5, /* getrawlist(), old style TODO obsolete */
     n_CMD_ARG_TYPE_WYRA = 6, /* _RAWLIST or _WYSH (with `wysh') TODO obs. */
   n_CMD_ARG_TYPE_ARG = 7, /* n_cmd_arg_desc/n_cmd_arg() new-style */
   n_CMD_ARG_TYPE_MASK = 7, /* Mask of the above */

   n_CMD_ARG_A = 1u<<4, /* Needs an active mailbox */
   n_CMD_ARG_F = 1u<<5, /* Is a conditional command */
   n_CMD_ARG_G = 1u<<6, /* Is supposed to produce "gabby" history */
   n_CMD_ARG_H = 1u<<7, /* Never place in `history' */
   n_CMD_ARG_I = 1u<<8, /* Interactive command bit */
   n_CMD_ARG_L = 1u<<9, /* Supports `local' prefix (only WYSH/WYRA) */
   n_CMD_ARG_M = 1u<<10, /* Legal from send mode bit */
   n_CMD_ARG_O = 1u<<11, /* n_OBSOLETE()d command */
   n_CMD_ARG_P = 1u<<12, /* Autoprint dot after command */
   n_CMD_ARG_R = 1u<<13, /* Forbidden in compose mode recursion */
   n_CMD_ARG_SC = 1u<<14, /* Forbidden pre-n_PSO_STARTED_CONFIG */
   n_CMD_ARG_S = 1u<<15, /* Forbidden pre-n_PSO_STARTED (POSIX) */
   n_CMD_ARG_T = 1u<<16, /* Is a transparent command */
   n_CMD_ARG_V = 1u<<17, /* Supports `vput' prefix (only WYSH/WYRA) */
   n_CMD_ARG_W = 1u<<18, /* Invalid when read only bit */
   n_CMD_ARG_X = 1u<<19, /* Valid command in n_PS_COMPOSE_FORKHOOK mode */
   /* XXX Note that CMD_ARG_EM implies a _real_ return value for $! */
   n_CMD_ARG_EM = 1u<<30 /* If error: n_pstate_err_no (4 $! aka. ok_v___em) */
};

enum n_cmd_arg_desc_flags{
   /* - A type */
   n_CMD_ARG_DESC_SHEXP = 1u<<0, /* sh(1)ell-style token */
   /* TODO All MSGLIST arguments can only be used last and are always greedy
    * TODO (but MUST be _GREEDY, and MUST NOT be _OPTION too!).
    * MSGLIST_AND_TARGET may create a NULL target */
   n_CMD_ARG_DESC_MSGLIST = 1u<<1,  /* Message specification(s) */
   n_CMD_ARG_DESC_NDMSGLIST = 1u<<2,
   n_CMD_ARG_DESC_MSGLIST_AND_TARGET = 1u<<3,

   n__CMD_ARG_DESC_TYPE_MASK = n_CMD_ARG_DESC_SHEXP |
         n_CMD_ARG_DESC_MSGLIST | n_CMD_ARG_DESC_NDMSGLIST |
         n_CMD_ARG_DESC_MSGLIST_AND_TARGET,

   /* - Optional flags */
   /* It is not an error if an optional argument is missing; once an argument
    * has been declared optional only optional arguments may follow */
   n_CMD_ARG_DESC_OPTION = 1u<<16,
   /* GREEDY: parse as many of that type as possible; must be last entry */
   n_CMD_ARG_DESC_GREEDY = 1u<<17,
   /* If greedy, join all given arguments separated by ASCII SP right away */
   n_CMD_ARG_DESC_GREEDY_JOIN = 1u<<18,
   /* Honour an overall "stop" request in one of the arguments (\c@ or #) */
   n_CMD_ARG_DESC_HONOUR_STOP = 1u<<19,
   /* With any MSGLIST, only one message may be give or ERR_NOTSUP (default) */
   n_CMD_ARG_DESC_MSGLIST_NEEDS_SINGLE = 1u<<20,

   n__CMD_ARG_DESC_FLAG_MASK = n_CMD_ARG_DESC_OPTION | n_CMD_ARG_DESC_GREEDY |
         n_CMD_ARG_DESC_GREEDY_JOIN | n_CMD_ARG_DESC_HONOUR_STOP |
         n_CMD_ARG_DESC_MSGLIST_NEEDS_SINGLE,

   /* We may include something for n_pstate_err_no */
   n_CMD_ARG_DESC_ERRNO_SHIFT = 21u,
   n_CMD_ARG_DESC_ERRNO_MASK = (1u<<10) - 1
};
#define n_CMD_ARG_DESC_ERRNO_TO_ORBITS(ENO) \
   (((u32)(ENO)) << n_CMD_ARG_DESC_ERRNO)
#define n_CMD_ARG_DESC_TO_ERRNO(FLAGCARRIER) \
   (((u32)(FLAGCARRIER) >> n_CMD_ARG_DESC_ERRNO_SHIFT) &\
      n_CMD_ARG_DESC_ERRNO_MASK)

enum conversion{
   CONV_NONE, /* no conversion */
   CONV_7BIT, /* no conversion, is 7bit */
   CONV_FROMQP, /* convert from quoted-printable */
   CONV_TOQP, /* convert to quoted-printable */
   CONV_8BIT, /* convert to 8bit (iconv) */
   CONV_FROMB64, /* convert from base64 */
   CONV_FROMB64_T, /* convert from base64/text */
   CONV_TOB64, /* convert to base64 */
   CONV_FROMHDR, /* convert from RFC1522 format */
   CONV_TOHDR, /* convert to RFC1522 format */
   CONV_TOHDR_A /* convert addresses for header */
};

enum cproto{
   CPROTO_CERTINFO, /* Special dummy proto for TLS certificate info xxx */
   CPROTO_CCRED, /* Special dummy credential proto (S/MIME etc.) */
   CPROTO_SOCKS, /* Special dummy SOCKS5 proxy proto */
   CPROTO_SMTP,
   CPROTO_POP3
,CPROTO_IMAP
};

/* enum n_err_number from gen-config.h, which is in sync with
 * su_err_doc(), su_err_name() and su_err_from_name() */

enum n_exit_status{
   n_EXIT_OK = EXIT_SUCCESS,
   n_EXIT_ERR = EXIT_FAILURE,
   n_EXIT_USE = 64, /* sysexits.h:EX_USAGE */
   n_EXIT_NOUSER = 67, /* :EX_NOUSER */
   n_EXIT_COLL_ABORT = 1<<1, /* Message collection was aborted */
   n_EXIT_SEND_ERROR = 1<<2 /* Unspecified send error occurred */
};

enum fedit_mode{
   FEDIT_NONE = 0,
   FEDIT_SYSBOX = 1u<<0, /* %: prefix */
   FEDIT_RDONLY = 1u<<1, /* Readonly (per-box, n_OPT_R_FLAG is global) */
   FEDIT_NEWMAIL = 1u<<2, /* `newmail' operation TODO OBSOLETE THIS! */
   FEDIT_ACCOUNT = 1u<<3 /* setfile() called by `account' */
};

enum fexp_mode{
   FEXP_FULL, /* Full expansion */
   FEXP_NOPROTO = 1u<<0, /* TODO no which_protocol() to decide expansion */
   FEXP_SILENT = 1u<<1, /* Don't print but only return errors */
   FEXP_MULTIOK = 1u<<2, /* Expansion to many entries is ok */
   FEXP_LOCAL = 1u<<3, /* Result must be local file/maildir */
   FEXP_LOCAL_FILE = 1u<<4, /* ..must be a local file: strips protocol://! */
   FEXP_NSHORTCUT = 1u<<5, /* Don't expand shortcuts */
   FEXP_NSPECIAL = 1u<<6, /* No %,#,& specials */
   FEXP_NFOLDER = 1u<<7, /* NSPECIAL and no + folder, too */
   FEXP_NSHELL = 1u<<8, /* Don't do shell word exp. (but ~/, $VAR) */
   FEXP_NVAR = 1u<<9 /* ..not even $VAR expansion */
};

enum n_go_input_flags{
   n_GO_INPUT_NONE,
   n_GO_INPUT_CTX_BASE = 0, /* Generic shared base: don't use! */
   n_GO_INPUT_CTX_DEFAULT = 1, /* Default input */
   n_GO_INPUT_CTX_COMPOSE = 2, /* Compose mode input */
   n__GO_INPUT_CTX_MASK = 3,
   /* _MASK is not a valid index here, but the lower bits are not misused,
    * therefore -- to save space! -- indexing is performed via "& _MASK".
    * This is CTA()d!  For actual spacing of arrays we use _MAX1 instead */
   n__GO_INPUT_CTX_MAX1 = n_GO_INPUT_CTX_COMPOSE + 1,

   n_GO_INPUT_HOLDALLSIGS = 1u<<8, /* sigs_all_hold() active TODO */
   /* `xcall' is `call' (at the level where this is set): to be set when
    * teardown of top level has undesired effects, e.g., for `account's and
    * folder hooks etc., where we do not to loose our `localopts' unroll list */
   n_GO_INPUT_NO_XCALL = 1u<<9,

   n_GO_INPUT_FORCE_STDIN = 1u<<10, /* Even in macro, use stdin (`read')! */
   n_GO_INPUT_DELAY_INJECTIONS = 1u<<11, /* Skip go_input_inject()ions */
   n_GO_INPUT_NL_ESC = 1u<<12, /* Support "\\$" line continuation */
   n_GO_INPUT_NL_FOLLOW = 1u<<13, /* ..on such a follow line */
   n_GO_INPUT_PROMPT_NONE = 1u<<14, /* Don't print prompt */
   n_GO_INPUT_PROMPT_EVAL = 1u<<15, /* Instead, evaluate *prompt* */

   /* XXX The remains are mostly hacks */

   n_GO_INPUT_HIST_ADD = 1u<<16, /* Add the result to history list */
   n_GO_INPUT_HIST_GABBY = 1u<<17, /* Consider history entry as gabby */

   n_GO_INPUT_IGNERR = 1u<<18, /* Imply `ignerr' command modifier */

   n__GO_FREEBIT = 24
};

enum n_go_input_inject_flags{
   n_GO_INPUT_INJECT_NONE = 0,
   n_GO_INPUT_INJECT_COMMIT = 1u<<0, /* Auto-commit input */
   n_GO_INPUT_INJECT_HISTORY = 1u<<1 /* Allow history addition */
};

enum n_header_extract_flags{
   n_HEADER_EXTRACT_NONE,
   n_HEADER_EXTRACT_EXTENDED = 1u<<0,
   n_HEADER_EXTRACT_FULL = 2u<<0,
   n_HEADER_EXTRACT__MODE_MASK = n_HEADER_EXTRACT_EXTENDED |
         n_HEADER_EXTRACT_FULL,

   /* Prefill the receivers with the already existing content of the given
    * struct header arguent */
   n_HEADER_EXTRACT_PREFILL_RECEIVERS = 1u<<8,
   /* Understand and ignore shell-style comments */
   n_HEADER_EXTRACT_IGNORE_SHELL_COMMENTS = 1u<<9,
   /* Ignore a MBOX From_ line _silently */
   n_HEADER_EXTRACT_IGNORE_FROM_ = 1u<<10
};

/* Special ignore (where _TYPE is covered by POSIX `ignore' / `retain').
 * _ALL is very special in that it doesn't have a backing object.
 * Go over enum to avoid cascads of (different) CC warnings for used CTA()s */
#define n_IGNORE_ALL ((struct n_ignore*)n__IGNORE_ALL)
#define n_IGNORE_TYPE ((struct n_ignore*)n__IGNORE_TYPE)
#define n_IGNORE_SAVE ((struct n_ignore*)n__IGNORE_SAVE)
#define n_IGNORE_FWD ((struct n_ignore*)n__IGNORE_FWD)
#define n_IGNORE_TOP ((struct n_ignore*)n__IGNORE_TOP)

enum{
   n__IGNORE_ALL = -2,
   n__IGNORE_TYPE = -3,
   n__IGNORE_SAVE = -4,
   n__IGNORE_FWD = -5,
   n__IGNORE_TOP = -6,
   n__IGNORE_ADJUST = 3,
   n__IGNORE_MAX = 6 - n__IGNORE_ADJUST
};

enum n_mailsend_flags{
   n_MAILSEND_NONE,
   n_MAILSEND_IS_FWD = 1u<<0,
   n_MAILSEND_HEADERS_PRINT = 1u<<2,
   n_MAILSEND_RECORD_RECIPIENT = 1u<<3,
   n_MAILSEND_ALTERNATES_NOSTRIP = 1u<<4,

   n_MAILSEND_ALL = n_MAILSEND_IS_FWD | n_MAILSEND_HEADERS_PRINT |
         n_MAILSEND_RECORD_RECIPIENT | n_MAILSEND_ALTERNATES_NOSTRIP
};

enum mimecontent{
   MIME_UNKNOWN, /* unknown content */
   MIME_SUBHDR, /* inside a multipart subheader */
   MIME_822, /* message/rfc822 content */
   MIME_MESSAGE, /* other message/ content */
   MIME_TEXT_PLAIN, /* text/plain content */
   MIME_TEXT_HTML, /* text/html content */
   MIME_TEXT, /* other text/ content */
   MIME_ALTERNATIVE, /* multipart/alternative content */
   MIME_RELATED, /* mime/related (RFC 2387) */
   MIME_DIGEST, /* multipart/digest content */
   MIME_SIGNED, /* multipart/signed */
   MIME_ENCRYPTED, /* multipart/encrypted */
   MIME_MULTI, /* other multipart/ content */
   MIME_PKCS7, /* PKCS7 content */
   MIME_DISCARD /* content is discarded */
};

enum mime_counter_evidence{
   MIMECE_NONE,
   MIMECE_SET = 1u<<0, /* *mime-counter-evidence* was set */
   MIMECE_BIN_OVWR = 1u<<1, /* appli../octet-stream: check, ovw if possible */
   MIMECE_ALL_OVWR = 1u<<2, /* all: check, ovw if possible */
   MIMECE_BIN_PARSE = 1u<<3 /* appli../octet-stream: classify contents last */
};

/* Content-Transfer-Encodings as defined in RFC 2045:
 * - Quoted-Printable, section 6.7
 * - Base64, section 6.8 */
#define QP_LINESIZE (4 * 19) /* Max. compliant QP linesize */

#define B64_LINESIZE (4 * 19) /* Max. compliant Base64 linesize */
#define B64_ENCODE_INPUT_PER_LINE ((B64_LINESIZE / 4) * 3)

enum mime_enc{
   MIMEE_NONE, /* message is not in MIME format */
   MIMEE_BIN, /* message is in binary encoding */
   MIMEE_8B, /* message is in 8bit encoding */
   MIMEE_7B, /* message is in 7bit encoding */
   MIMEE_QP, /* message is quoted-printable */
   MIMEE_B64 /* message is in base64 encoding */
};

/* xxx QP came later, maybe rewrite all to use mime_enc_flags directly? */
enum mime_enc_flags{
   MIMEEF_NONE,
   MIMEEF_SALLOC = 1u<<0, /* Use n_autorec_alloc(), not n_realloc().. */
   /* ..result .s,.l point to user buffer of *_LINESIZE+[+[+]] bytes instead */
   MIMEEF_BUF = 1u<<1,
   MIMEEF_CRLF = 1u<<2, /* (encode) Append "\r\n" to lines */
   MIMEEF_LF = 1u<<3, /* (encode) Append "\n" to lines */
   /* (encode) If one of _CRLF/_LF is set, honour *_LINESIZE+[+[+]] and
    * inject the desired line-ending whenever a linewrap is desired */
   MIMEEF_MULTILINE = 1u<<4,
   /* (encode) Quote with header rules, do not generate soft NL breaks?
    * For mustquote(), specifies whether special RFC 2047 header rules
    * should be used instead */
   MIMEEF_ISHEAD = 1u<<5,
   /* (encode) Ditto; for mustquote() this furtherly fine-tunes behaviour in
    * that characters which would not be reported as "must-quote" when
    * detecting whether quoting is necessary at all will be reported as
    * "must-quote" if they have to be encoded in an encoded word */
   MIMEEF_ISENCWORD = 1u<<6,
   __MIMEEF_LAST = 6u
};

enum qpflags{
   QP_NONE = MIMEEF_NONE,
   QP_SALLOC = MIMEEF_SALLOC,
   QP_BUF = MIMEEF_BUF,
   QP_ISHEAD = MIMEEF_ISHEAD,
   QP_ISENCWORD = MIMEEF_ISENCWORD
};

enum b64flags{
   B64_NONE = MIMEEF_NONE,
   B64_SALLOC = MIMEEF_SALLOC,
   B64_BUF = MIMEEF_BUF,
   B64_CRLF = MIMEEF_CRLF,
   B64_LF = MIMEEF_LF,
   B64_MULTILINE = MIMEEF_MULTILINE,
   /* Not used, but for clarity only */
   B64_ISHEAD = MIMEEF_ISHEAD,
   B64_ISENCWORD = MIMEEF_ISENCWORD,
   /* Special version of Base64, "Base64URL", according to RFC 4648.
    * Only supported for encoding! */
   B64_RFC4648URL = 1u<<(__MIMEEF_LAST+1),
   /* Don't use any ("=") padding;
    * may NOT be used with any of _CRLF, _LF or _MULTILINE */
   B64_NOPAD = 1u<<(__MIMEEF_LAST+2)
};

enum mime_parse_flags{
   MIME_PARSE_NONE,
   MIME_PARSE_DECRYPT = 1u<<0,
   MIME_PARSE_PARTS = 1u<<1,
   MIME_PARSE_SHALLOW = 1u<<2,
   /* In effect we parse this message for user display or quoting purposes, so
    * relaxed rules regarding content inspection may be applicable */
   MIME_PARSE_FOR_USER_CONTEXT = 1u<<3
};

enum mime_handler_flags{
   MIME_HDL_NULL, /* No pipe- mimetype handler, go away */
   MIME_HDL_CMD, /* Normal command */
   MIME_HDL_TEXT, /* @ special cmd to force treatment as text */
   MIME_HDL_PTF, /* A special pointer-to-function handler */
   MIME_HDL_MSG, /* Display msg (returned as command string) */
   MIME_HDL_TYPE_MASK = 7u,
   MIME_HDL_COPIOUSOUTPUT = 1u<<4, /* _CMD produces reintegratable text */
   MIME_HDL_ISQUOTE = 1u<<5, /* Is quote action (we have info, keep it!) */
   MIME_HDL_NOQUOTE = 1u<<6, /* No MIME for quoting */
   MIME_HDL_ASYNC = 1u<<7, /* Should run asynchronously */
   MIME_HDL_NEEDSTERM = 1u<<8, /* Takes over terminal */
   MIME_HDL_TMPF = 1u<<9, /* Create temporary file (zero-sized) */
   MIME_HDL_TMPF_FILL = 1u<<10, /* Fill in the msg body content */
   MIME_HDL_TMPF_UNLINK = 1u<<11 /* Delete it later again */
};

enum okay{
   STOP = 0,
   OKAY = 1
};

enum okey_xlook_mode{
   OXM_PLAIN = 1u<<0, /* Plain key always tested */
   OXM_H_P = 1u<<1, /* Check PLAIN-.url_h_p */
   OXM_U_H_P = 1u<<2, /* Check PLAIN-.url_u_h_p */
   OXM_ALL = 0x7u
};

/* <0 means "stop" unless *prompt* extensions are enabled. */
enum prompt_exp{
   PROMPT_STOP = -1, /* \c */
   /* *prompt* extensions: \$, \@ etc. */
   PROMPT_DOLLAR = -2,
   PROMPT_AT = -3
};

enum protocol{
   n_PROTO_NONE,
   n_PROTO_FILE, /* refers to a local file */
PROTO_FILE = n_PROTO_FILE,
   n_PROTO_POP3, /* is a pop3 server string */
PROTO_POP3 = n_PROTO_POP3,
n_PROTO_IMAP,
PROTO_IMAP = n_PROTO_IMAP,
   n_PROTO_MAILDIR, /* refers to a maildir folder */
PROTO_MAILDIR = n_PROTO_MAILDIR,
   n_PROTO_UNKNOWN, /* unknown protocol */
PROTO_UNKNOWN = n_PROTO_UNKNOWN,

   n_PROTO_MASK = (1u << 5) - 1
};

enum sendaction{
   SEND_MBOX, /* no conversion to perform */
   SEND_RFC822, /* no conversion, no From_ line */
   SEND_TODISP, /* convert to displayable form */
   SEND_TODISP_ALL, /* same, include all MIME parts */
   SEND_TODISP_PARTS, /* same, but only interactive, user-selected parts */
   SEND_SHOW, /* convert to 'show' command form */
   SEND_TOSRCH, /* convert for IMAP SEARCH */
   SEND_TOFILE, /* convert for saving body to a file */
   SEND_TOPIPE, /* convert for pipe-content/subc. */
   SEND_QUOTE, /* convert for quoting */
   SEND_QUOTE_ALL, /* same, include all MIME parts */
   SEND_DECRYPT /* decrypt */
};

enum n_shexp_parse_flags{
   n_SHEXP_PARSE_NONE,
   /* Don't perform expansions or interpret reverse solidus escape sequences.
    * Output may be NULL, otherwise the possibly trimmed non-expanded input is
    * used as output (implies _PARSE_META_KEEP) */
   n_SHEXP_PARSE_DRYRUN = 1u<<0,
   n_SHEXP_PARSE_TRUNC = 1u<<1, /* Truncate result storage on entry */
   n_SHEXP_PARSE_TRIM_SPACE = 1u<<2, /* ..surrounding tokens */
   n_SHEXP_PARSE_TRIM_IFSSPACE = 1u<<3, /* " */
   n_SHEXP_PARSE_LOG = 1u<<4, /* Log errors */
   n_SHEXP_PARSE_LOG_D_V = 1u<<5, /* Log errors if n_PO_D_V */
   n_SHEXP_PARSE_IFS_VAR = 1u<<6, /* IFS is *ifs*, not su_cs_is_blank() */
   n_SHEXP_PARSE_IFS_ADD_COMMA = 1u<<7, /* Add comma , to normal "IFS" */
   n_SHEXP_PARSE_IFS_IS_COMMA = 1u<<8, /* Let comma , be the sole "IFS" */
   n_SHEXP_PARSE_IGNORE_EMPTY = 1u<<9, /* Ignore empty tokens, start over */

   /* Implicitly open quotes, and ditto closing.  _AUTO_FIXED may only be used
    * if an auto-quote-mode is enabled, implies _AUTO_CLOSE and causes the
    * quote mode to be permanently active (cannot be closed) */
   n_SHEXP_PARSE_QUOTE_AUTO_FIXED = 1u<<16,
   n_SHEXP_PARSE_QUOTE_AUTO_SQ = 1u<<17,
   n_SHEXP_PARSE_QUOTE_AUTO_DQ = 1u<<18,
   n_SHEXP_PARSE_QUOTE_AUTO_DSQ = 1u<<19,
   n_SHEXP_PARSE_QUOTE_AUTO_CLOSE = 1u<<20, /* Ignore an open quote at EOS */
   n__SHEXP_PARSE_QUOTE_AUTO_MASK = n_SHEXP_PARSE_QUOTE_AUTO_SQ |
         n_SHEXP_PARSE_QUOTE_AUTO_DQ | n_SHEXP_PARSE_QUOTE_AUTO_DSQ,

   /* Recognize metacharacters to separate tokens */
   n_SHEXP_PARSE_META_VERTBAR = 1u<<21,
   n_SHEXP_PARSE_META_AMPERSAND = 1u<<22,
   /* Interpret ; as a sequencing operator, go_input_inject() remainder */
   n_SHEXP_PARSE_META_SEMICOLON = 1u<<23,
   /* LPAREN, RPAREN, LESSTHAN, GREATERTHAN */

   n_SHEXP_PARSE_META_MASK = n_SHEXP_PARSE_META_VERTBAR |
         n_SHEXP_PARSE_META_AMPERSAND | n_SHEXP_PARSE_META_SEMICOLON,

   /* Keep the metacharacter (or IFS character), do not skip over it */
   n_SHEXP_PARSE_META_KEEP = 1u<<24,

   n__SHEXP_PARSE_LAST = 24
};

enum n_shexp_state{
   n_SHEXP_STATE_NONE,
   /* We have produced some output (or would have, with _PARSE_DRYRUN).
    * Note that empty quotes like '' produce no output but set this bit */
   n_SHEXP_STATE_OUTPUT = 1u<<0,
   /* Don't call the parser again (# comment seen; out of input).
    * Not (necessarily) mutual with _OUTPUT) */
   n_SHEXP_STATE_STOP = 1u<<1,
   n_SHEXP_STATE_UNICODE = 1u<<2, /* \[Uu] used */
   n_SHEXP_STATE_CONTROL = 1u<<3, /* Control characters seen */
   n_SHEXP_STATE_QUOTE = 1u<<4, /* Any quotes seen */
   n_SHEXP_STATE_WS_LEAD = 1u<<5, /* _TRIM_{IFS,}SPACE: seen.. */
   n_SHEXP_STATE_WS_TRAIL = 1u<<6, /* .. leading / trailing WS */
   n_SHEXP_STATE_META_VERTBAR = 1u<<7, /* Metacharacter | follows/ed */
   n_SHEXP_STATE_META_AMPERSAND = 1u<<8, /* Metacharacter & follows/ed */
   n_SHEXP_STATE_META_SEMICOLON = 1u<<9, /* Metacharacter ; follows/ed */

   n_SHEXP_STATE_META_MASK = n_SHEXP_STATE_META_VERTBAR |
         n_SHEXP_STATE_META_AMPERSAND | n_SHEXP_STATE_META_SEMICOLON,

   n_SHEXP_STATE_ERR_CONTROL = 1u<<16, /* \c notation with invalid arg. */
   n_SHEXP_STATE_ERR_UNICODE = 1u<<17, /* Valid \[Uu] and !n_PSO_UNICODE */
   n_SHEXP_STATE_ERR_NUMBER = 1u<<18, /* Bad number (\[UuXx]) */
   n_SHEXP_STATE_ERR_IDENTIFIER = 1u<<19, /* Invalid identifier */
   n_SHEXP_STATE_ERR_BADSUB = 1u<<20, /* Empty/bad ${}/[] substitution */
   n_SHEXP_STATE_ERR_GROUPOPEN = 1u<<21, /* _QUOTEOPEN + no }/]/)/ 4 ${/[/( */
   n_SHEXP_STATE_ERR_QUOTEOPEN = 1u<<22, /* Quote remains open at EOS */

   n_SHEXP_STATE_ERR_MASK = su_BITENUM_MASK(16, 22)
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

enum n_str_trim_flags{
   n_STR_TRIM_FRONT = 1u<<0,
   n_STR_TRIM_END = 1u<<1,
   n_STR_TRIM_BOTH = n_STR_TRIM_FRONT | n_STR_TRIM_END
};

#ifdef mx_HAVE_TLS
enum n_tls_verify_level{
   n_TLS_VERIFY_IGNORE,
   n_TLS_VERIFY_WARN,
   n_TLS_VERIFY_ASK,
   n_TLS_VERIFY_STRICT
};
#endif

enum tdflags{
   TD_NONE, /* no display conversion */
   TD_ISPR = 1<<0, /* use isprint() checks */
   TD_ICONV = 1<<1, /* use iconv() */
   TD_DELCTRL = 1<<2, /* delete control characters */

   /* NOTE: _TD_EOF and _TD_BUFCOPY may be ORd with enum conversion and
    * enum sendaction, and may thus NOT clash with their bit range! */
   _TD_EOF = 1<<14, /* EOF seen, last round! */
   _TD_BUFCOPY = 1<<15 /* Buffer may be constant, copy it */
};

enum n_url_flags{
   n_URL_TLS_REQUIRED = 1u<<0, /* Whether protocol always uses SSL/TLS.. */
   n_URL_TLS_OPTIONAL = 1u<<1, /* ..may later upgrade to SSL/TLS */
   n_URL_TLS_MASK = n_URL_TLS_REQUIRED | n_URL_TLS_OPTIONAL,
   n_URL_HAD_USER = 1u<<2, /* Whether .url_user was part of the URL */
   n_URL_HOST_IS_NAME = 1u<<3 /* .url_host not numeric address */
};

enum n_visual_info_flags{
   n_VISUAL_INFO_NONE,
   n_VISUAL_INFO_ONE_CHAR = 1u<<0, /* Step only one char, then return */
   n_VISUAL_INFO_SKIP_ERRORS = 1u<<1, /* Treat via replacement, step byte */
   n_VISUAL_INFO_WIDTH_QUERY = 1u<<2, /* Detect visual character widths */

   /* Rest only with mx_HAVE_C90AMEND1, mutual with _ONE_CHAR */
   n_VISUAL_INFO_WOUT_CREATE = 1u<<8, /* Use/create .vic_woudat */
   n_VISUAL_INFO_WOUT_SALLOC = 1u<<9, /* ..autorec_alloc() it first */
   /* Only visuals into .vic_woudat - implies _WIDTH_QUERY */
   n_VISUAL_INFO_WOUT_PRINTABLE = 1u<<10,
   n__VISUAL_INFO_FLAGS = n_VISUAL_INFO_WOUT_CREATE |
         n_VISUAL_INFO_WOUT_SALLOC | n_VISUAL_INFO_WOUT_PRINTABLE
};

enum n_program_option{
   n_PO_DEBUG = 1u<<0, /* -d / *debug* */
   n_PO_VERB = 1u<<1, /* -v / *verbose* */
   n_PO_VERBVERB = 1u<<2, /* .. even more verbosity */
   n_PO_EXISTONLY = 1u<<3, /* -e */
   n_PO_HEADERSONLY = 1u<<4, /* -H */
   n_PO_HEADERLIST = 1u<<5, /* -L */
   n_PO_QUICKRUN_MASK = n_PO_EXISTONLY | n_PO_HEADERSONLY | n_PO_HEADERLIST,
   n_PO_E_FLAG = 1u<<6, /* -E / *skipemptybody* */
   n_PO_F_FLAG = 1u<<7, /* -F */
   n_PO_Mm_FLAG = 1u<<8, /* -M or -m (plus n_poption_arg_Mm) */
   n_PO_R_FLAG = 1u<<9, /* -R */
   n_PO_r_FLAG = 1u<<10, /* -r (plus n_poption_arg_r) */
   n_PO_S_FLAG_TEMPORARY = 1u<<11, /* -S about to set a variable */
   n_PO_t_FLAG = 1u<<12, /* -t */
   n_PO_TILDE_FLAG = 1u<<13, /* -~ */
   n_PO_BATCH_FLAG = 1u<<14, /* -# */

   /* Some easy-access shortcuts TODO n_PO_VERB+ should be mask(s) already! */
   n_PO_D_V = n_PO_DEBUG | n_PO_VERB | n_PO_VERBVERB,
   n_PO_D_VV = n_PO_DEBUG | n_PO_VERBVERB
};

#define n_OBSOLETE(X) \
do{\
   if(n_poption & n_PO_D_V)\
      n_err("%s: %s\n", _("Obsoletion warning"), X);\
}while(0)
#define n_OBSOLETE2(X,Y) \
do{\
   if(n_poption & n_PO_D_V)\
      n_err("%s: %s: %s\n", _("Obsoletion warning"), X, Y);\
}while(0)

/* Program state bits which may regulary fluctuate */
enum n_program_state{
   n_PS_ROOT = 1u<<30, /* Temporary "bypass any checks" bit */
#define n_PS_ROOT_BLOCK(ACT) \
do{\
   boole a___reset___ = !(n_pstate & n_PS_ROOT);\
   n_pstate |= n_PS_ROOT;\
   ACT;\
   if(a___reset___)\
      n_pstate &= ~n_PS_ROOT;\
}while(0)

   /* XXX These are internal to the state machine and do not belong here,
    * XXX yet this was the easiest (accessible) approach */
   n_PS_ERR_XIT = 1u<<0, /* Unless `ignerr' seen -> n_PSO_XIT */
   n_PS_ERR_QUIT = 1u<<1, /* ..ditto: -> n_PSO_QUIT */
   n_PS_ERR_EXIT_MASK = n_PS_ERR_XIT | n_PS_ERR_QUIT,

   n_PS_SOURCING = 1u<<2, /* During load() or `source' */
   n_PS_ROBOT = 1u<<3, /* .. even more robotic */
   n_PS_COMPOSE_MODE = 1u<<4, /* State machine recursed */
   n_PS_COMPOSE_FORKHOOK = 1u<<5, /* A hook running in a subprocess */

   n_PS_HOOK_NEWMAIL = 1u<<7,
   n_PS_HOOK = 1u<<8,
   n_PS_HOOK_MASK = n_PS_HOOK_NEWMAIL | n_PS_HOOK,

   n_PS_EDIT = 1u<<9, /* Current mailbox no "system mailbox" */
   n_PS_SETFILE_OPENED = 1u<<10, /* (hack) setfile() opened a new box */
   n_PS_SAW_COMMAND = 1u<<11, /* ..after mailbox switch */
   n_PS_DID_PRINT_DOT = 1u<<12, /* Current message has been printed */

   n_PS_SIGWINCH_PEND = 1u<<13, /* Need update of $COLUMNS/$LINES */
   n_PS_PSTATE_PENDMASK = n_PS_SIGWINCH_PEND, /* pstate housekeeping needed */

   n_PS_ARGLIST_MASK = su_BITENUM_MASK(14, 16),
   n_PS_ARGMOD_LOCAL = 1u<<14, /* "local" modifier TODO struct CmdCtx */
   n_PS_ARGMOD_VPUT = 1u<<15, /* "vput" modifier TODO struct CmdCtx */
   n_PS_ARGMOD_WYSH = 1u<<16, /* "wysh" modifier TODO struct CmdCtx */
   n_PS_MSGLIST_GABBY = 1u<<14, /* n_getmsglist() saw something gabby */
   n_PS_MSGLIST_DIRECT = 1u<<15, /* A msg was directly chosen by number */

   n_PS_EXPAND_MULTIRESULT = 1u<<17, /* Last fexpand() with MULTIOK had .. */
   n_PS_ERRORS_PROMPT = 1u<<18, /* New error to be reported in prompt */

   /* Bad hacks */
   n_PS_HEADER_NEEDED_MIME = 1u<<24, /* mime_write_tohdr() not ASCII clean */
   n_PS_READLINE_NL = 1u<<25, /* readline_input()+ saw a \n */
   n_PS_BASE64_STRIP_CR = 1u<<26 /* Go for text output, strip CR's */
};

/* Various states set once, and first time messages or initializers */
enum n_program_state_once{
   /* We have four program states: (0) pre getopt() done, (_GETOPT) pre rcfile
    * loaded etc., (_CONFIG) only -X evaluation missing still, followed by
    * _STARTED when we are fully setup */
   n_PSO_STARTED_GETOPT = 1u<<0,
   n_PSO_STARTED_CONFIG = 1u<<1,
   n_PSO_STARTED = 1u<<2,

   /* Exit request pending (quick) */
   n_PSO_XIT = 1u<<3,
   n_PSO_QUIT = 1u<<4,
   n_PSO_EXIT_MASK = n_PSO_XIT | n_PSO_QUIT,

   /* Pre _STARTED */
   /* 1u<<5, */
   n_PSO_UNICODE = 1u<<6,
   n_PSO_ENC_MBSTATE = 1u<<7,

   n_PSO_SENDMODE = 1u<<9,
   n_PSO_INTERACTIVE = 1u<<10,
   n_PSO_TTYIN = 1u<<11,
   n_PSO_TTYOUT = 1u<<12, /* TODO should be TTYERR! */
   n_PSO_TTYANY = n_PSO_TTYIN | n_PSO_TTYOUT, /* mx_tty_fp = TTY */
   n_PSO_TTYERR = 1u<<13,

   /* "Later" */
   n_PSO_t_FLAG_DONE = 1u<<15,
   n_PSO_ATTACH_QUOTE_NOTED = 1u<<16,
   n_PSO_ERRORS_NOTED = 1u<<17,
   n_PSO_LINE_EDITOR_INIT = 1u<<18,
   n_PSO_RANDOM_INIT = 1u<<19,
   n_PSO_TERMCAP_DISABLE = 1u<<20,
   n_PSO_TERMCAP_CA_MODE = 1u<<21,
   n_PSO_TERMCAP_FULLWIDTH = 1u<<22, /* !am or am+xn (right margin wrap) */
   n_PSO_PS_DOTLOCK_NOTED = 1u<<23
};

/* {{{ A large enum with all the boolean and value options a.k.a their keys.
 * Only the constant keys are in here, to be looked up via ok_[bv]look(),
 * ok_[bv]set() and ok_[bv]clear().
 * Variable properties are placed in {PROP=VALUE[:,PROP=VALUE:]} comments,
 * a {\} comment causes the next line to be read for (overlong) properties.
 * Notes:
 * - see the introductional source comments before changing *anything* in here!
 * - virt= implies rdonly,nodel
 * - import= implies env
 * - num and posnum are mutual exclusive
 * - most default VAL_ues come from in from build system via ./make.rc
 * (Keep in SYNC: nail.h:okeys, nail.rc, nail.1:"Initial settings") */
enum okeys {
   /* This is used for all macro(-local) variables etc., i.e.,
    * [*@#]|[1-9][0-9]*, in order to have something with correct properties.
    * It is also used for the ${^.+} multiplexer */
   ok_v___special_param,   /* {nolopts=1,rdonly=1,nodel=1} */
   /*__qm/__em aka ?/! should be num=1 but that more expensive than what now */
   ok_v___qm,              /* {name=?,nolopts=1,rdonly=1,nodel=1} */
   ok_v___em,              /* {name=!,nolopts=1,rdonly=1,nodel=1} */

   ok_v_account,                       /* {nolopts=1,rdonly=1,nodel=1} */
   ok_b_add_file_recipients,
ok_v_agent_shell_lookup, /* {obsolete=1} */
   ok_b_allnet,
   ok_b_append,
   /* *ask* is auto-mapped to *asksub* as imposed by standard! */
   ok_b_ask,                           /* {vip=1} */
   ok_b_askatend,
   ok_b_askattach,
   ok_b_askbcc,
   ok_b_askcc,
   ok_b_asksign,
   ok_b_asksend,                       /* {i3val=TRU1} */
   ok_b_asksub,                        /* {i3val=TRU1} */
   ok_v_attrlist,
   ok_v_autobcc,
   ok_v_autocc,
   ok_b_autocollapse,
   ok_b_autoprint,
ok_b_autothread, /* {obsolete=1} */
   ok_v_autosort,

   ok_b_bang,
ok_b_batch_exit_on_error, /* {obsolete=1} */
   ok_v_bind_timeout,                  /* {notempty=1,posnum=1} */
ok_b_bsdannounce, /* {obsolete=1} */
   ok_b_bsdcompat,
   ok_b_bsdflags,
   ok_b_bsdheadline,
   ok_b_bsdmsgs,
   ok_b_bsdorder,
   ok_v_build_cc,                      /* {virt=VAL_BUILD_CC_ARRAY} */
   ok_v_build_ld,                      /* {virt=VAL_BUILD_LD_ARRAY} */
   ok_v_build_os,                      /* {virt=VAL_BUILD_OS} */
   ok_v_build_rest,                    /* {virt=VAL_BUILD_REST_ARRAY} */

   ok_v_COLUMNS,                       /* {notempty=1,posnum=1,env=1} */
   /* Charset lowercase conversion handled via vip= */
   ok_v_charset_7bit,            /* {vip=1,notempty=1,defval=CHARSET_7BIT} */
   /* But unused without mx_HAVE_ICONV, we use ok_vlook(CHARSET_8BIT_OKEY)! */
   ok_v_charset_8bit,            /* {vip=1,notempty=1,defval=CHARSET_8BIT} */
   ok_v_charset_unknown_8bit,          /* {vip=1} */
   ok_v_cmd,
   ok_b_colour_disable,
   ok_b_colour_pager,
   ok_v_contact_mail,                  /* {virt=VAL_CONTACT_MAIL} */
   ok_v_contact_web,                   /* {virt=VAL_CONTACT_WEB} */
   ok_v_crt,                           /* {posnum=1} */
   ok_v_customhdr,                     /* {vip=1} */

   ok_v_DEAD,                          /* {notempty=1,env=1,defval=VAL_DEAD} */
   ok_v_datefield,                     /* {i3val="%Y-%m-%d %H:%M"} */
   ok_v_datefield_markout_older,       /* {i3val="%Y-%m-%d"} */
   ok_b_debug,                         /* {vip=1} */
   ok_b_disposition_notification_send,
   ok_b_dot,
   ok_b_dotlock_disable,
ok_b_dotlock_ignore_error, /* {obsolete=1} */

   ok_v_EDITOR,                     /* {env=1,notempty=1,defval=VAL_EDITOR} */
   ok_v_editalong,
   ok_b_editheaders,
   ok_b_emptystart,
ok_v_encoding, /* {obsolete=1} */
   ok_b_errexit,
   ok_v_escape,                        /* {defval=n_ESCAPE} */
   ok_v_expandaddr,
   ok_v_expandaddr_domaincheck,        /* {notempty=1} */
   ok_v_expandargv,

   ok_v_features,                      /* {virt=VAL_FEATURES} */
   ok_b_flipr,
   ok_v_folder,                        /* {vip=1} */
   ok_v_folder_resolved,               /* {rdonly=1,nodel=1} */
   ok_v_folder_hook,
   ok_b_followup_to,
   ok_v_followup_to_honour,
   ok_b_forward_as_attachment,
   ok_v_forward_inject_head,
   ok_v_forward_inject_tail,
   ok_v_from,                          /* {vip=1} */
   ok_b_fullnames,
ok_v_fwdheading, /* {obsolete=1} */

   ok_v_HOME,                          /* {vip=1,nodel=1,notempty=1,import=1} */
   ok_b_header,                        /* {i3val=TRU1} */
   ok_v_headline,
   ok_v_headline_bidi,
   ok_b_headline_plain,
   ok_v_history_file,
   ok_b_history_gabby,
   ok_b_history_gabby_persist,
   ok_v_history_size,                  /* {notempty=1,posnum=1} */
   ok_b_hold,
   ok_v_hostname,                      /* {vip=1} */

   ok_b_idna_disable,
   ok_v_ifs,                           /* {vip=1,defval=" \t\n"} */
   ok_v_ifs_ws,                     /* {vip=1,rdonly=1,nodel=1,i3val=" \t\n"} */
   ok_b_ignore,
   ok_b_ignoreeof,
   ok_v_inbox,
   ok_v_indentprefix,                  /* {defval="\t"} */

   ok_b_keep,
   ok_b_keep_content_length,
   ok_b_keepsave,

   ok_v_LANG, /* {vip=1,env=1,notempty=1} */
   ok_v_LC_ALL, /* {name=LC_ALL,vip=1,env=1,notempty=1} */
   ok_v_LC_CTYPE, /* {name=LC_CTYPE,vip=1,env=1,notempty=1} */
   ok_v_LINES, /* {notempty=1,posnum=1,env=1} */
   ok_v_LISTER, /* {env=1,notempty=1,defval=VAL_LISTER} */
   ok_v_LOGNAME, /* {rdonly=1,import=1} */
   ok_v_line_editor_cpl_word_breaks, /* {\ } */
      /* {defval=n_LINE_EDITOR_CPL_WORD_BREAKS} */
   ok_b_line_editor_disable,
   ok_b_line_editor_no_defaults,
   ok_v_log_prefix, /* {nodel=1,i3val=VAL_UAGENT ": "} */

   ok_v_MAIL,                          /* {env=1} */
   ok_v_MAILRC,                  /* {import=1,notempty=1,defval=VAL_MAILRC} */
   ok_b_MAILX_NO_SYSTEM_RC,            /* {name=MAILX_NO_SYSTEM_RC,import=1} */
   ok_v_MBOX,                          /* {env=1,notempty=1,defval=VAL_MBOX} */
   ok_v_mailbox_resolved,              /* {nolopts=1,rdonly=1,nodel=1} */
   ok_v_mailbox_display,               /* {nolopts=1,rdonly=1,nodel=1} */
   ok_v_mailx_extra_rc,
   ok_b_markanswered,
   ok_b_mbox_fcc_and_pcc,              /* {i3val=1} */
   ok_b_mbox_rfc4155,
   ok_b_memdebug,                      /* {vip=1} */
   ok_b_message_id_disable,
   ok_v_message_inject_head,
   ok_v_message_inject_tail,
   ok_b_metoo,
   ok_b_mime_allow_text_controls,
   ok_b_mime_alternative_favour_rich,
   ok_v_mime_counter_evidence,         /* {posnum=1} */
   ok_v_mime_encoding,
   ok_b_mime_force_sendout,
   ok_v_mimetypes_load_control,
   ok_v_mta, /* {notempty=1,defval=VAL_MTA} */
   ok_v_mta_aliases, /* {notempty=1} */
   ok_v_mta_arguments,
   ok_b_mta_no_default_arguments,
   ok_b_mta_no_receiver_arguments,
   ok_v_mta_argv0,                     /* {notempty=1,defval=VAL_MTA_ARGV0} */

   /* TODO drop all those _v_mailx which are now accessible via `digmsg'!
    * TODO Documentation yet removed, n_temporary_compose_hook_varset() not */
ok_v_mailx_command,                 /* {rdonly=1,nodel=1} */
ok_v_mailx_subject,                 /* {rdonly=1,nodel=1} */
ok_v_mailx_from,                    /* {rdonly=1,nodel=1} */
ok_v_mailx_sender,                  /* {rdonly=1,nodel=1} */
ok_v_mailx_to,                      /* {rdonly=1,nodel=1} */
ok_v_mailx_cc,                      /* {rdonly=1,nodel=1} */
ok_v_mailx_bcc,                     /* {rdonly=1,nodel=1} */
ok_v_mailx_raw_to,                  /* {rdonly=1,nodel=1} */
ok_v_mailx_raw_cc,                  /* {rdonly=1,nodel=1} */
ok_v_mailx_raw_bcc,                 /* {rdonly=1,nodel=1} */
ok_v_mailx_orig_from,               /* {rdonly=1,nodel=1} */
ok_v_mailx_orig_to,                 /* {rdonly=1,nodel=1} */
ok_v_mailx_orig_cc,                 /* {rdonly=1,nodel=1} */
ok_v_mailx_orig_bcc,                /* {rdonly=1,nodel=1} */

ok_v_NAIL_EXTRA_RC, /* {name=NAIL_EXTRA_RC,obsolete=1} */
ok_b_NAIL_NO_SYSTEM_RC, /* {name=NAIL_NO_SYSTEM_RC,import=1,obsolete=1} */
ok_v_NAIL_HEAD, /* {name=NAIL_HEAD,obsolete=1} */
ok_v_NAIL_HISTFILE, /* {name=NAIL_HISTFILE,obsolete=1} */
ok_v_NAIL_HISTSIZE, /* {name=NAIL_HISTSIZE,notempty=1,num=1,obsolete=1} */
ok_v_NAIL_TAIL, /* {name=NAIL_TAIL,obsolete=1} */
   ok_v_NETRC,                         /* {env=1,notempty=1,defval=VAL_NETRC} */
   ok_b_netrc_lookup,                  /* {chain=1} */
   ok_v_netrc_pipe,
   ok_v_newfolders,
   ok_v_newmail,

   ok_v_on_account_cleanup,            /* {notempty=1} */
   ok_v_on_compose_cleanup,            /* {notempty=1} */
   ok_v_on_compose_enter,              /* {notempty=1} */
   ok_v_on_compose_leave,              /* {notempty=1} */
   ok_v_on_compose_splice,             /* {notempty=1} */
   ok_v_on_compose_splice_shell,       /* {notempty=1} */
   ok_v_on_history_addition,           /* {notempty=1} */
   ok_v_on_resend_cleanup,             /* {notempty=1} */
   ok_v_on_resend_enter,               /* {notempty=1} */
   ok_b_outfolder,

   ok_v_PAGER,                         /* {env=1,notempty=1,defval=VAL_PAGER} */
   ok_v_PATH,                          /* {nodel=1,import=1} */
   ok_b_POSIXLY_CORRECT,            /* {vip=1,import=1,name=POSIXLY_CORRECT} */
   ok_b_page,
   ok_v_password,                      /* {chain=1} */
   ok_b_piperaw,
   ok_v_pop3_auth,                     /* {chain=1} */
   ok_b_pop3_bulk_load,
   ok_v_pop3_keepalive,                /* {notempty=1,posnum=1} */
   ok_b_pop3_no_apop,                  /* {chain=1} */
   ok_b_pop3_use_starttls,             /* {chain=1} */
   ok_b_posix,                         /* {vip=1} */
   ok_b_print_alternatives,
   ok_v_prompt,                        /* {i3val="? "} */
   ok_v_prompt2,                       /* {i3val=".. "} */

   ok_b_quiet,
   ok_v_quote,
   ok_b_quote_as_attachment,
   ok_v_quote_chars,                   /* {vip=1,notempty=1,defval=">|}:"} */
   ok_v_quote_fold,
   ok_v_quote_inject_head,
   ok_v_quote_inject_tail,

   ok_b_r_option_implicit,
   ok_b_recipients_in_cc,
   ok_v_record,
   ok_b_record_files,
   ok_b_record_resent,
   ok_b_reply_in_same_charset,
   ok_v_reply_strings,
ok_v_replyto, /* {obsolete=1} */
   ok_v_reply_to,                      /* {notempty=1} */
   ok_v_reply_to_honour,
   ok_b_rfc822_body_from_,             /* {name=rfc822-body-from_} */

   ok_v_SHELL,                      /* {import=1,notempty=1,defval=VAL_SHELL} */
ok_b_SYSV3, /* {env=1,obsolete=1} */
   ok_b_save,                          /* {i3val=TRU1} */
   ok_v_screen,                        /* {notempty=1,posnum=1} */
   ok_b_searchheaders,
   /* Charset lowercase conversion handled via vip= */
   ok_v_sendcharsets,                  /* {vip=1} */
   ok_b_sendcharsets_else_ttycharset,
   ok_v_sender,                        /* {vip=1} */
ok_v_sendmail, /* {obsolete=1} */
ok_v_sendmail_arguments, /* {obsolete=1} */
ok_b_sendmail_no_default_arguments, /* {obsolete=1} */
ok_v_sendmail_progname, /* {obsolete=1} */
   ok_v_sendwait, /* {i3val=""} */
   ok_b_showlast,
   ok_b_showname,
   ok_b_showto,
   ok_v_Sign,
   ok_v_sign,
ok_v_signature, /* {obsolete=1} */
   ok_b_skipemptybody,                 /* {vip=1} */
   ok_v_smime_ca_dir,
   ok_v_smime_ca_file,
   ok_v_smime_ca_flags,
   ok_b_smime_ca_no_defaults,
   ok_v_smime_cipher,                  /* {chain=1} */
   ok_v_smime_crl_dir,
   ok_v_smime_crl_file,
   ok_v_smime_encrypt,                 /* {chain=1} */
   ok_b_smime_force_encryption,
ok_b_smime_no_default_ca, /* {obsolete=1} */
   ok_b_smime_sign,
   ok_v_smime_sign_cert,               /* {chain=1} */
   ok_v_smime_sign_digest,             /* {chain=1} */
   ok_v_smime_sign_include_certs,      /* {chain=1} */
ok_v_smime_sign_message_digest,     /* {chain=1,obsolete=1} */
ok_v_smtp, /* {obsolete=1} */
   ok_v_smtp_auth,                     /* {chain=1} */
ok_v_smtp_auth_password, /* {obsolete=1} */
ok_v_smtp_auth_user, /* {obsolete=1} */
   ok_v_smtp_hostname,                 /* {vip=1} */
   ok_b_smtp_use_starttls,             /* {chain=1} */
   ok_v_SOURCE_DATE_EPOCH,             /* {\ } */
      /* {name=SOURCE_DATE_EPOCH,rdonly=1,import=1,notempty=1,posnum=1} */

   ok_v_socket_connect_timeout,        /* {posnum=1} */
   ok_v_socks_proxy,                   /* {chain=1,notempty=1} */
   ok_v_spam_interface,
   ok_v_spam_maxsize,                  /* {notempty=1,posnum=1} */
   ok_v_spamc_command,
   ok_v_spamc_arguments,
   ok_v_spamc_user,
   ok_v_spamfilter_ham,
   ok_v_spamfilter_noham,
   ok_v_spamfilter_nospam,
   ok_v_spamfilter_rate,
   ok_v_spamfilter_rate_scanscore,
   ok_v_spamfilter_spam,
ok_v_ssl_ca_dir, /* {chain=1,obsolete=1} */
ok_v_ssl_ca_file, /* {chain=1,obsolete=1} */
ok_v_ssl_ca_flags, /* {chain=1,obsolete=1} */
ok_b_ssl_ca_no_defaults, /* {chain=1,obsolete=1} */
ok_v_ssl_cert, /* {chain=1,obsolete=1} */
ok_v_ssl_cipher_list, /* {chain=1,obsolete=1} */
ok_v_ssl_config_file, /* {obsolete=1} */
ok_v_ssl_config_module, /* {chain=1,obsolete=1} */
ok_v_ssl_config_pairs, /* {chain=1,obsolete=1} */
ok_v_ssl_curves, /* {chain=1,obsolete=1} */
ok_v_ssl_crl_dir, /* {obsolete=1} */
ok_v_ssl_crl_file, /* {obsolete=1} */
ok_v_ssl_features, /* {virt=VAL_TLS_FEATURES,obsolete=1} */
ok_v_ssl_key, /* {chain=1,obsolete=1} */
ok_v_ssl_method, /* {chain=1,obsolete=1} */
ok_b_ssl_no_default_ca, /* {obsolete=1} */
ok_v_ssl_protocol, /* {chain=1,obsolete=1} */
ok_v_ssl_rand_egd, /* {obsolete=1} */
ok_v_ssl_rand_file, /* {obsolete=1}*/
ok_v_ssl_verify, /* {chain=1,obsolete=1} */
   ok_v_stealthmua,
   ok_v_system_mailrc,           /* {virt=VAL_SYSCONFDIR "/" VAL_SYSCONFRC} */

   ok_v_TERM,                          /* {env=1} */
   ok_v_TMPDIR,            /* {import=1,vip=1,notempty=1,defval=VAL_TMPDIR} */
   ok_v_termcap,
   ok_b_termcap_ca_mode,
   ok_b_termcap_disable,
   ok_v_tls_ca_dir,                    /* {chain=1} */
   ok_v_tls_ca_file,                   /* {chain=1} */
   ok_v_tls_ca_flags,                  /* {chain=1} */
   ok_b_tls_ca_no_defaults,            /* {chain=1} */
   ok_v_tls_config_file,
   ok_v_tls_config_module,             /* {chain=1} */
   ok_v_tls_config_pairs,              /* {chain=1} */
   ok_v_tls_crl_dir,
   ok_v_tls_crl_file,
   ok_v_tls_features,                  /* {virt=VAL_TLS_FEATURES} */
   ok_v_tls_fingerprint,               /* {chain=1} */
   ok_v_tls_fingerprint_digest,        /* {chain=1} */
   ok_v_tls_rand_file,
   ok_v_tls_verify,                    /* {chain=1} */
   ok_v_toplines,                      /* {notempty=1,num=1,defval="5"} */
   ok_b_topsqueeze,
   /* Charset lowercase conversion handled via vip= */
   ok_v_ttycharset,              /* {vip=1,notempty=1,defval=CHARSET_8BIT} */
   ok_b_typescript_mode,               /* {vip=1} */

   ok_v_USER,                          /* {rdonly=1,import=1} */
   ok_v_umask,                      /* {vip=1,nodel=1,posnum=1,i3val="0077"} */
   ok_v_user,                       /* {notempty=1,chain=1} */

   ok_v_VISUAL,                     /* {env=1,notempty=1,defval=VAL_VISUAL} */
   ok_v_v15_compat,
   ok_b_verbose,                       /* {vip=1} */
   ok_v_version,                       /* {virt=n_VERSION} */
   ok_v_version_date,                  /* {virt=n_VERSION_DATE} */
   ok_v_version_hexnum,                /* {virt=n_VERSION_HEXNUM,posnum=1} */
   ok_v_version_major,                 /* {virt=n_VERSION_MAJOR,posnum=1} */
   ok_v_version_minor,                 /* {virt=n_VERSION_MINOR,posnum=1} */
   ok_v_version_update,                /* {virt=n_VERSION_UPDATE,posnum=1} */

   ok_b_writebackedited

,  /* Obsolete IMAP related non-sorted */
ok_b_disconnected,               /* {chain=1} */
ok_v_imap_auth,                  /* {chain=1} */
ok_v_imap_cache,
ok_v_imap_delim,                 /* {chain=1} */
ok_v_imap_keepalive,             /* {chain=1} */
ok_v_imap_list_depth,
ok_b_imap_use_starttls           /* {chain=1} */
}; /* }}} */
enum {n_OKEYS_MAX = ok_b_imap_use_starttls};

/* Forwards */
struct mx_name; /* -> names.h */

struct str{
   char *s; /* the string's content */
   uz l; /* the stings's length */
};

struct n_string{
   char *s_dat; /*@ May contain NULs, not automatically terminated */
   u32 s_len; /*@ gth of string */
   u32 s_auto : 1; /* Stored in auto-reclaimed storage? */
   u32 s_size : 31; /* of .s_dat, -1 */
};

struct n_strlist{
   struct n_strlist *sl_next;
   uz sl_len;
   char sl_dat[VFIELD_SIZE(0)];
};
#define n_STRLIST_ALLOC(SZ) /* XXX -> nailfuns.h (and pimp interface) */\
   n_alloc(VSTRUCT_SIZEOF(struct n_strlist, sl_dat) + (SZ) +1)
#define n_STRLIST_AUTO_ALLOC(SZ) \
   n_autorec_alloc(VSTRUCT_SIZEOF(struct n_strlist, sl_dat) + (SZ) +1)
#define n_STRLIST_LOFI_ALLOC(SZ) \
   n_lofi_alloc(VSTRUCT_SIZEOF(struct n_strlist, sl_dat) + (SZ) +1)

struct n_cmd_arg_desc{
   char cad_name[12]; /* Name of command */
   u32 cad_no; /* Number of entries in cad_ent_flags */
   /* [enum n_cmd_arg_desc_flags,arg-dep] */
   u32 cad_ent_flags[VFIELD_SIZE(0)][2];
};
/* ISO C(99) doesn't allow initialization of "flex array" */
#define n_CMD_ARG_DESC_SUBCLASS_DEF(CMD,NO,VAR) \
   static struct n_cmd_arg_desc_ ## CMD {\
      char cad_name[12];\
      u32 cad_no;\
      u32 cad_ent_flags[NO][2];\
   } const VAR = { #CMD "\0", NO,
#define n_CMD_ARG_DESC_SUBCLASS_DEF_END }
#define n_CMD_ARG_DESC_SUBCLASS_CAST(P) ((struct n_cmd_arg_desc const*)P)

struct n_cmd_arg_ctx{
   struct n_cmd_arg_desc const *cac_desc; /* Input: description of command */
   char const *cac_indat; /* Input that shall be parsed */
   uz cac_inlen; /* Input length (UZ_MAX: do a su_cs_len()) */
   u32 cac_msgflag; /* Input (option): required flags of messages */
   u32 cac_msgmask; /* Input (option): relevant flags of messages */
   uz cac_no; /* Output: number of parsed arguments */
   struct n_cmd_arg *cac_arg; /* Output: parsed arguments */
   char const *cac_vput; /* "Output": vput prefix used: varname */
};

struct n_cmd_arg{
   struct n_cmd_arg *ca_next;
   char const *ca_indat; /*[PRIV] Pointer into n_cmd_arg_ctx.cac_indat */
   uz ca_inlen; /*[PRIV] of .ca_indat of this arg (no NUL) */
   u32 ca_ent_flags[2]; /* Copy of n_cmd_arg_desc.cad_ent_flags[X] */
   u32 ca_arg_flags; /* [Output: _WYSH: copy of parse result flags] */
   u8 ca__dummy[4];
   union{
      struct str ca_str; /* _CMD_ARG_DESC_SHEXP */
      int *ca_msglist; /* _CMD_ARG_DESC_MSGLIST+ */
   } ca_arg; /* Output: parsed result */
};

struct n_cmd_desc{
   char const *cd_name; /* Name of command */
   int (*cd_func)(void*); /* Implementor of command */
   enum n_cmd_arg_flags cd_caflags;
   u32 cd_msgflag; /* Required flags of msgs */
   u32 cd_msgmask; /* Relevant flags of msgs */
   /* XXX requires cmd-tab.h initializer changes u8 cd__pad[4];*/
   struct n_cmd_arg_desc const *cd_cadp;
#ifdef mx_HAVE_DOCSTRINGS
   char const *cd_doc; /* One line doc for command */
#endif
};
/* Yechh, can't initialize unions */
#define cd_minargs cd_msgflag /* Minimum argcount for WYSH/WYRA/RAWLIST */
#define cd_maxargs cd_msgmask /* Max argcount for WYSH/WYRA/RAWLIST */

struct url{
   char const *url_input; /* Input as given (really) */
   u32 url_flags;
   u16 url_portno; /* atoi .url_port or default, host endian */
   u8 url_cproto; /* enum cproto as given */
   u8 url_proto_len; /* Length of .url_proto (to first '\0') */
   char url_proto[16]; /* Communication protocol as 'xy\0://\0' */
   char const *url_port; /* Port (if given) or NULL */
   struct str url_user; /* User, exactly as given / looked up */
   struct str url_user_enc; /* User, urlxenc()oded */
   struct str url_pass; /* Pass (urlxdec()oded) or NULL */
   /* TODO we don't know whether .url_host is a name or an address.  Us
    * TODO Net::IPAddress::fromString() to check that, then set
    * TODO n_URL_HOST_IS_NAME solely based on THAT!  Until then,
    * TODO n_URL_HOST_IS_NAME ONLY set if n_URL_TLS_MASK+mx_HAVE_GETADDRINFO */
   struct str url_host; /* Service hostname TODO we don't know */
   struct str url_path; /* Path suffix or NULL */
   /* TODO: url_get_component(url *, enum COMPONENT, str *store) */
   struct str url_h_p; /* .url_host[:.url_port] */
   /* .url_user@.url_host
    * Note: for CPROTO_SMTP this may resolve HOST via *smtp-hostname* (->
    * *hostname*)!  (And may later be overwritten according to *from*!) */
   struct str url_u_h;
   struct str url_u_h_p; /* .url_user@.url_host[:.url_port] */
   struct str url_eu_h_p; /* .url_user_enc@.url_host[:.url_port] */
   char const *url_p_u_h_p; /* .url_proto://.url_u_h_p */
   char const *url_p_eu_h_p; /* .url_proto://.url_eu_h_p */
   char const *url_p_eu_h_p_p; /* .url_proto://.url_eu_h_p[/.url_path] */
};

struct n_go_data_ctx{
   struct su_mem_bag *gdc_membag;
   void *gdc_ifcond; /* Saved state of conditional stack */
#ifdef mx_HAVE_COLOUR
   struct mx_colour_env *gdc_colour;
   boole gdc_colour_active;
   u8 gdc__colour_pad[7];
# define mx_COLOUR_IS_ACTIVE() \
   (/*n_go_data->gc_data.gdc_colour != su_NIL &&*/\
    /*n_go_data->gc_data.gdc_colour->ce_enabled*/ n_go_data->gdc_colour_active)
#endif
   struct su_mem_bag gdc__membag_buf[1];
};

struct mime_handler{
   enum mime_handler_flags mh_flags;
   struct str mh_msg; /* Message describing this command */
   /* XXX union{} the following? */
   char const *mh_shell_cmd; /* For MIME_HDL_CMD */
   int (*mh_ptf)(void); /* PTF main() for MIME_HDL_PTF */
};

struct search_expr{
   /* XXX Type of search should not be evaluated but be enum */
   boole ss_field_exists; /* Only check whether field spec. exists */
   boole ss_skin; /* Shall work on (skin()ned) addresses */
   u8 ss__pad[6];
   char const *ss_field; /* Field spec. where to search (not always used) */
   char const *ss_body; /* Field body search expression */
#ifdef mx_HAVE_REGEX
   regex_t *ss_fieldre; /* Could be instead of .ss_field */
   regex_t *ss_bodyre; /* Ditto, .ss_body */
   regex_t ss__fieldre_buf;
   regex_t ss__bodyre_buf;
#endif
};

/* This is somewhat temporary for pre v15 */
struct n_sigman{
   u32 sm_flags; /* enum n_sigman_flags */
   int sm_signo;
   struct n_sigman *sm_outer;
   n_sighdl_t sm_ohup;
   n_sighdl_t sm_oint;
   n_sighdl_t sm_oquit;
   n_sighdl_t sm_oterm;
   n_sighdl_t sm_opipe;
   sigjmp_buf sm_jump;
};

struct n_timespec{
   s64 ts_sec;
   sz ts_nsec;
};

struct time_current{ /* TODO s64, etc. */
   time_t tc_time;
   struct tm tc_gm;
   struct tm tc_local;
   char tc_ctime[32];
};

struct mailbox{
   enum{
      MB_NONE = 000, /* no reply expected */
      MB_COMD = 001, /* command reply expected */
      MB_MULT = 002, /* multiline reply expected */
      MB_PREAUTH = 004, /* not in authenticated state */
      MB_BYE = 010, /* may accept a BYE state */
      MB_BAD_FROM_ = 1<<4 /* MBOX with invalid From_ seen & logged */
   } mb_active;
   FILE *mb_itf; /* temp file with messages, read open */
   FILE *mb_otf; /* same, write open */
   char *mb_sorted; /* sort method */
   enum{
      MB_VOID, /* no type (e. g. connection failed) */
      MB_FILE, /* local file */
      MB_POP3, /* POP3 mailbox */
MB_IMAP, /* IMAP mailbox */
MB_CACHE, /* IMAP cache */
      MB_MAILDIR /* maildir folder */
   } mb_type; /* type of mailbox */
   enum{
      MB_DELE = 01, /* may delete messages in mailbox */
      MB_EDIT = 02 /* may edit messages in mailbox */
   } mb_perm;
   int mb_threaded; /* mailbox has been threaded */
#ifdef mx_HAVE_IMAP
   enum mbflags{
      MB_NOFLAGS = 000,
      MB_UIDPLUS = 001 /* supports IMAP UIDPLUS */
   } mb_flags;
   u64 mb_uidvalidity; /* IMAP unique identifier validity */
   char *mb_imap_account; /* name of current IMAP account */
   char *mb_imap_pass; /* xxx v15-compat URL workaround */
   char *mb_imap_mailbox; /* name of current IMAP mailbox */
   char *mb_cache_directory; /* name of cache directory */
   char mb_imap_delim[8]; /* Directory separator(s), [0] += replacer */
#endif
   /* XXX mailbox.mb_accmsg is a hack in so far as the mailbox object should
    * XXX have an on_close event to which that machinery should connect */
   struct mx_dig_msg_ctx *mb_digmsg; /* Open `digmsg' connections */
   struct mx_socket *mb_sock; /* socket structure */
};

enum needspec{
   NEED_UNSPEC, /* unspecified need, don't fetch */
   NEED_HEADER, /* need the header of a message */
   NEED_BODY /* need header and body of a message */
};

enum content_info{
   CI_NOTHING, /* Nothing downloaded yet */
   CI_HAVE_HEADER = 1u<<0, /* Header is downloaded */
   CI_HAVE_BODY = 1u<<1, /* Entire message is downloaded */
   CI_HAVE_MASK = CI_HAVE_HEADER | CI_HAVE_BODY,
   CI_MIME_ERRORS = 1u<<2, /* Defective MIME structure */
   CI_EXPANDED = 1u<<3, /* Container part (pk7m) exploded into X */
   CI_SIGNED = 1u<<4, /* Has a signature.. */
   CI_SIGNED_OK = 1u<<5, /* ..verified ok.. */
   CI_SIGNED_BAD = 1u<<6, /* ..verified bad (missing key).. */
   CI_ENCRYPTED = 1u<<7, /* Is encrypted.. */
   CI_ENCRYPTED_OK = 1u<<8, /* ..decryption possible/ok.. */
   CI_ENCRYPTED_BAD = 1u<<9 /* ..not possible/ok */
};

/* Note: flags that are used in obs-imap-cache.c may not change */
enum mflag{
   MUSED = 1u<<0, /* entry is used, but this bit isn't */
   MDELETED = 1u<<1, /* entry has been deleted */
   MSAVED = 1u<<2, /* entry has been saved */
   MTOUCH = 1u<<3, /* entry has been noticed */
   MPRESERVE = 1u<<4, /* keep entry in sys mailbox */
   MMARK = 1u<<5, /* message is marked! */
   MODIFY = 1u<<6, /* message has been modified */
   MNEW = 1u<<7, /* message has never been seen */
   MREAD = 1u<<8, /* message has been read sometime. */
   MSTATUS = 1u<<9, /* message status has changed */
   MBOX = 1u<<10, /* Send this to mbox, regardless */
   MNOFROM = 1u<<11, /* no From line */
   MHIDDEN = 1u<<12, /* message is hidden to user */
MFULLYCACHED = 1u<<13, /* IMAP cached */
   MBOXED = 1u<<14, /* message has been sent to mbox */
MUNLINKED = 1u<<15, /* Unlinked from IMAP cache */
   MNEWEST = 1u<<16, /* message is very new (newmail) */
   MFLAG = 1u<<17, /* message has been flagged recently */
   MUNFLAG = 1u<<18, /* message has been unflagged */
   MFLAGGED = 1u<<19, /* message is `flagged' */
   MANSWER = 1u<<20, /* message has been answered recently */
   MUNANSWER = 1u<<21, /* message has been unanswered */
   MANSWERED = 1u<<22, /* message is `answered' */
   MDRAFT = 1u<<23, /* message has been drafted recently */
   MUNDRAFT = 1u<<24, /* message has been undrafted */
   MDRAFTED = 1u<<25, /* message is marked as `draft' */
   MOLDMARK = 1u<<26, /* messages was marked previously */
   MSPAM = 1u<<27, /* message is classified as spam */
   MSPAMUNSURE = 1u<<28, /* message may be spam, but it is unsure */

   /* The following are hacks in so far as they let imagine what the future
    * will bring, without doing this already today */
   MBADFROM_ = 1u<<29, /* From_ line must be replaced */
   MDISPLAY = 1u<<30 /* Display content of this part */
};
#define MMNORM (MDELETED | MSAVED | MHIDDEN)
#define MMNDEL (MDELETED | MHIDDEN)

#define visible(mp) (((mp)->m_flag & MMNDEL) == 0)

struct mimepart{
   enum mflag m_flag;
   enum content_info m_content_info;
#ifdef mx_HAVE_SPAM
   u32 m_spamscore; /* Spam score as int, 24:8 bits */
#else
   u8 m__pad1[4];
#endif
   int m_block; /* Block number of this part */
   uz m_offset; /* Offset in block of part */
   uz m_size; /* Bytes in the part */
   uz m_xsize; /* Bytes in the full part */
   long m_lines; /* Lines in the message; wire format! */
   long m_xlines; /* Lines in the full message; ditto */
   time_t m_time; /* Time the message was sent */
   char const *m_from; /* Message sender */
   struct mimepart *m_nextpart; /* Next part at same level */
   struct mimepart *m_multipart; /* Parts of multipart */
   struct mimepart *m_parent; /* Enclosing multipart part */
   char const *m_ct_type; /* Content-type */
   char const *m_ct_type_plain; /* Content-type without specs */
   char const *m_ct_type_usr_ovwr; /* Forcefully overwritten one */
   char const *m_charset;
   char const *m_ct_enc; /* Content-Transfer-Encoding */
   enum mimecontent m_mimecontent; /* ..in enum */
   enum mime_enc m_mime_enc; /* ..in enum */
   char *m_partstring; /* Part level string */
   char *m_filename; /* ..of attachment */
   char const *m_content_description;
   char const *m_external_body_url; /* message/external-body:access-type=URL */
   struct mime_handler *m_handler; /* MIME handler if yet classified */
};

struct message{
   enum mflag m_flag; /* flags */
   enum content_info m_content_info;
#ifdef mx_HAVE_SPAM
   u32 m_spamscore; /* Spam score as int, 24:8 bits */
#else
   u8 m__pad1[4];
#endif
   int m_block; /* block number of this message */
   uz m_offset; /* offset in block of message */
   uz m_size; /* Bytes in the message */
   uz m_xsize; /* Bytes in the full message */
   long m_lines; /* Lines in the message */
   long m_xlines; /* Lines in the full message */
   time_t m_time; /* time the message was sent */
   time_t m_date; /* time in the 'Date' field */
#ifdef mx_HAVE_IMAP
   u64 m_uid; /* IMAP unique identifier */
#endif
#ifdef mx_HAVE_MAILDIR
   char const *m_maildir_file; /* original maildir file of msg */
   u32 m_maildir_hash; /* hash of file name in maildir sub */
#endif
   int m_collapsed; /* collapsed thread information */
   unsigned m_idhash; /* hash on Message-ID for threads */
   unsigned m_level; /* thread level of message */
   long m_threadpos; /* position in threaded display */
   struct message *m_child; /* first child of this message */
   struct message *m_younger; /* younger brother of this message */
   struct message *m_elder; /* elder brother of this message */
   struct message *m_parent; /* parent of this message */
};

/* Given a file address, determine the block number it represents */
#define mailx_blockof(off) S(int,(off) / 4096)
#define mailx_offsetof(off) S(int,(off) % 4096)
#define mailx_positionof(block, offset) (S(off_t,block) * 4096 + (offset))

enum gfield{ /* TODO -> enum m_grab_head, m_GH_xy */
   GNONE,
   GTO = 1u<<0, /* Grab To: line */
   GSUBJECT = 1u<<1, /* Likewise, Subject: line */
   GCC = 1u<<2, /* And the Cc: line */
   GBCC = 1u<<3, /* And also the Bcc: line */

   GNL = 1u<<4, /* Print blank line after */
   GDEL = 1u<<5, /* Entity removed from list */
   GCOMMA = 1u<<6, /* detract() puts in commas */
   GUA = 1u<<7, /* User-Agent field */
   GMIME = 1u<<8, /* MIME 1.0 fields */
   GMSGID = 1u<<9, /* a Message-ID */
   GNAMEONLY = 1u<<10, /* detract() does NOT use fullnames */

   GIDENT = 1u<<11, /* From:, Reply-To:, MFT: (user headers) */
   GREF = 1u<<12, /* References:, In-Reply-To:, (Message-ID:) */
   GREF_IRT = 1u<<30, /* XXX Hack; only In-Reply-To: -> n_run_editor() */
   GDATE = 1u<<13, /* Date: field */
   GFULL = 1u<<14, /* Include full names, comments etc. */
   GSKIN = 1u<<15, /* Skin names */
   GEXTRA = 1u<<16, /* Extra fields (mostly like GIDENT XXX) */
   GFILES = 1u<<17, /* Include filename and pipe addresses */
   GFULLEXTRA = 1u<<18, /* Only with GFULL: GFULL less address */
   GBCC_IS_FCC = 1u<<19, /* This GBCC is (or was) indeed a Fcc: */
   GSHEXP_PARSE_HACK = 1u<<20, /* lextract()+: *expandaddr*=shquote */
   /* All given input (nalloc() etc.) to be interpreted as a single address */
   GNOT_A_LIST = 1u<<21,
   GNULL_OK = 1u<<22, /* NULL return OK for nalloc()+ */
   GMAILTO_URI = 1u<<23, /* RFC 6068-style */
   /* HACK: support "|bla", i.e., anything enclosed in quotes; e.g., used for
    * MTA alias parsing */
   GQUOTE_ENCLOSED_OK = 1u<<24
};
#define GMASK (GTO | GSUBJECT | GCC | GBCC)

enum header_flags{
   HF_NONE = 0,
   HF_LIST_REPLY = 1u<<0,
   HF_MFT_SENDER = 1u<<1, /* Add ourselves to Mail-Followup-To: */
   HF_RECIPIENT_RECORD = 1u<<10, /* Save message in file named after rec. */
   HF__NEXT_SHIFT = 11u
};

/* Structure used to pass about the current state of a message (header) */
struct n_header_field{
   struct n_header_field *hf_next;
   u32 hf_nl; /* Field-name length */
   u32 hf_bl; /* Field-body length*/
   char hf_dat[VFIELD_SIZE(0)];
};

struct header{
   u32 h_flags; /* enum header_flags bits */
   u32 h_dummy;
   char *h_subject; /* Subject string */
   char const *h_charset; /* preferred charset */
   struct mx_name *h_from; /* overridden "From:" field */
   struct mx_name *h_sender; /* overridden "Sender:" field */
   struct mx_name *h_to; /* Dynamic "To:" string */
   struct mx_name *h_cc; /* Carbon copies string */
   struct mx_name *h_bcc; /* Blind carbon copies */
   struct mx_name *h_fcc; /* Fcc: file carbon copies to */
   struct mx_name *h_ref; /* References (possibly overridden) */
   struct attachment *h_attach; /* MIME attachments */
   struct mx_name *h_reply_to; /* overridden "Reply-To:" field */
   struct mx_name *h_message_id; /* overridden "Message-ID:" field */
   struct mx_name *h_in_reply_to;/* overridden "In-Reply-To:" field */
   struct mx_name *h_mft; /* Mail-Followup-To */
   char const *h_list_post; /* Address from List-Post:, for `Lreply' */
   struct n_header_field *h_user_headers;
   struct n_header_field *h_custom_headers; /* (Cached result) */
   /* Raw/original versions of the header(s). If any */
   char const *h_mailx_command;
   struct mx_name *h_mailx_raw_to;
   struct mx_name *h_mailx_raw_cc;
   struct mx_name *h_mailx_raw_bcc;
   struct mx_name *h_mailx_orig_from;
   struct mx_name *h_mailx_orig_to;
   struct mx_name *h_mailx_orig_cc;
   struct mx_name *h_mailx_orig_bcc;
};

struct n_addrguts{
   /* Input string as given (maybe replaced with a fixed one!) */
   char const *ag_input;
   uz ag_ilen; /* su_cs_len() of input */
   uz ag_iaddr_start; /* Start of *addr-spec* in .ag_input */
   uz ag_iaddr_aend; /* ..and one past its end */
   char *ag_skinned; /* Output (alloced if !=.ag_input) */
   uz ag_slen; /* su_cs_len() of .ag_skinned */
   uz ag_sdom_start; /* Start of domain in .ag_skinned, */
   u32 ag_n_flags; /* enum mx_name_flags of .ag_skinned */
};

/* MIME attachments */
enum attach_conv{
   AC_DEFAULT, /* _get_lc() -> _iter_*() */
   AC_FIX_INCS, /* "charset=".a_input_charset (nocnv) */
   AC_TMPFILE /* attachment.a_tmpf is converted */
};

enum n_attach_error{
   n_ATTACH_ERR_NONE,
   n_ATTACH_ERR_FILE_OPEN,
   n_ATTACH_ERR_ICONV_FAILED,
   n_ATTACH_ERR_ICONV_NAVAIL,
   n_ATTACH_ERR_OTHER
};

struct attachment{
   struct attachment *a_flink; /* Forward link in list. */
   struct attachment *a_blink; /* Backward list link */
   char const *a_path_user; /* Path as given (maybe including iconv spec) */
   char const *a_path; /* Path as opened */
   char const *a_path_bname; /* Basename of path as opened */
   char const *a_name; /* File name to be stored (EQ a_path_bname) */
   char const *a_content_type; /* content type */
   char const *a_content_disposition; /* content disposition */
   struct mx_name *a_content_id; /* content id */
   char const *a_content_description; /* content description */
   char const *a_input_charset; /* Interpretation depends on .a_conv */
   char const *a_charset; /* ... */
   FILE *a_tmpf; /* If AC_TMPFILE */
   enum attach_conv a_conv; /* User chosen conversion */
   int a_msgno; /* message number */
};

struct sendbundle{
   struct header *sb_hp;
   struct mx_name *sb_to;
   FILE *sb_input;
   struct url *sb_urlp; /* Or NIL for file-based MTA */
   struct mx_cred_ctx *sb_credp; /* cred-auth.h not included */
   struct str sb_signer; /* USER@HOST for signing+ */
};

/* For saving the current directory and later returning */
struct cw{
#ifdef mx_HAVE_FCHDIR
   int cw_fd;
#else
   char cw_wd[PATH_MAX];
#endif
};

/*
 * Global variable declarations
 *
 * These become instantiated in main.c.
 */

#undef VL
#ifdef mx_SOURCE_MASTER
# ifndef mx_HAVE_AMALGAMATION
#  define VL
# else
#  define VL static
# endif
#else
# define VL extern
#endif

#define n_empty su_empty
#ifndef mx_HAVE_AMALGAMATION
VL char const n_month_names[12 + 1][4];
VL char const n_weekday_names[7 + 1][4];

VL char const n_uagent[sizeof VAL_UAGENT];
# ifdef mx_HAVE_UISTRINGS
VL char const n_error[sizeof n_ERROR];
# endif
VL char const n_path_devnull[sizeof n_PATH_DEVNULL];
VL char const n_0[2];
VL char const n_1[2];
VL char const n_m1[3]; /* -1 */
VL char const n_em[2]; /* Exclamation-mark ! */
VL char const n_ns[2]; /* Number sign # */
VL char const n_star[2]; /* Asterisk * */
VL char const n_hy[2]; /* Hyphen-Minus - */
VL char const n_qm[2]; /* Question-mark ? */
VL char const n_at[2]; /* Commercial at @ */
#endif /* mx_HAVE_AMALGAMATION */

VL FILE *n_stdin;
VL FILE *n_stdout;
VL FILE *n_stderr;
/* XXX Plus mx_tty_fp in tty.h */
/* XXX *_read_overlay and dig_msg_compose_ctx are hacks caused by missing
 * XXX event driven nature of individual program parts */
VL void *n_readctl_read_overlay; /* `readctl' XXX HACK */

VL u32 n_mb_cur_max; /* Value of MB_CUR_MAX */

VL gid_t n_group_id; /* getgid() and getuid() */
VL uid_t n_user_id;
VL pid_t n_pid; /* getpid() (lazy initialized) */

VL int n_exit_status; /* Program exit status TODO long term: ex_no */
VL u32 n_poption; /* Bits of enum n_program_option */
VL struct n_header_field *n_poption_arg_C; /* -C custom header list */
VL char const *n_poption_arg_Mm; /* Argument for -[Mm] aka n_PO_[Mm]_FLAG */
VL struct mx_name *n_poption_arg_r; /* Argument to -r option */
VL char const **n_smopts; /* MTA options from command line */
VL uz n_smopts_cnt; /* Entries in n_smopts */

/* The current execution data context */
VL struct n_go_data_ctx *n_go_data;
VL u32 n_psonce; /* Bits of enum n_program_state_once */
VL u32 n_pstate; /* Bits of enum n_program_state */
/* TODO "cmd_tab.h ARG_EM set"-storage (n_[01..]) as long as we don't have a
 * TODO struct CmdCtx where each command has its own ARGC/ARGV, errno and exit
 * TODO status and may-place-in-history bit, need to manage a global bypass.. */
#ifdef mx_HAVE_ERRORS
VL s32 n_pstate_err_cnt; /* What backs $^ERRQUEUE-xy */
#endif
/* TODO n_pstate_err_no: this should contain the error number in the lower
 * TODO bits, and a suberror in the high bits: offer accessor/setter macros.
 * TODO Like this we could use $^ERR-SUBNO or so to access these from outer
 * TODO space, and could perform much better testing; e.g., too many failures
 * TODO simply result in _INVAL, but what has it been exactly?
 * TODO This will furthermore allow better testing, in that even without
 * TODO uistrings we can test error conditions _exactly_!
 * TODO And change the tests accordingly, even support a mode where our
 * TODO error output is entirely suppressed, so that we _really_ can test
 * TODO and only based upon the subnumber!! */
VL s32 n_pstate_err_no; /* What backs $! su_ERR_* TODO ..HACK */
VL s32 n_pstate_ex_no; /* What backs $? n_EX_* TODO ..HACK ->64-bit */

/* XXX stylish sorting */
VL int msgCount; /* Count of messages read in */
VL struct mailbox mb; /* Current mailbox */
VL char mailname[PATH_MAX]; /* Name of current file TODO URL/object*/
VL char displayname[80 - 16]; /* Prettyfied for display TODO URL/obj*/
VL char prevfile[PATH_MAX]; /* Name of previous file TODO URL/obj */
VL off_t mailsize; /* Size of system mailbox */
VL struct message *dot; /* Pointer to current message */
VL struct message *prevdot; /* Previous current message */
VL struct message *message; /* The actual message structure */
VL struct message *threadroot; /* first threaded message */
VL int *n_msgvec; /* Folder setmsize(), list.c res. store*/
#ifdef mx_HAVE_IMAP
VL int imap_created_mailbox; /* hack to get feedback from imap */
#endif

VL struct n_header_field *n_customhdr_list; /* *customhdr* list */

VL struct time_current time_current; /* time(3); send: mail1() XXXcarrier */

#ifdef mx_HAVE_TLS
VL enum n_tls_verify_level n_tls_verify_level; /* TODO local per-context! */
#endif

VL volatile int interrupts; /* TODO rid! */
VL n_sighdl_t dflpipe;

/*
 * Finally, let's include the function prototypes XXX embed
 */

#ifndef mx_SOURCE_PS_DOTLOCK_MAIN
# include "mx/nailfuns.h"
#endif

#include "su/code-ou.h"
#endif /* n_NAIL_H */
/* s-it-mode */
