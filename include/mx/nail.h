/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Header inclusion, macros, constants, types and the global var declarations.
 *@ TODO Should be split in myriads of FEATURE-GROUP.h headers.  Sort.  def.h.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

/* TODO fake */
#include "su/code-in.h"

struct mx_dig_msg_ctx;
struct mx_go_data_ctx;
struct mx_mime_type_handler;

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

#undef mx_HAVE_NATCH_CHAR
#if defined mx_HAVE_SETLOCALE && defined mx_HAVE_C90AMEND1 && \
      defined mx_HAVE_WCWIDTH
# define mx_HAVE_NATCH_CHAR
#endif

#define n_UNCONST(X) su_UNCONST(void*,X) /* TODO */

/*
 * Types
 */

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
   EAF_RESTRICT = 1u<<0, /* "restrict" (do unless interactive / -[~#]) */
   EAF_FAIL = 1u<<1, /* "fail" */
   EAF_FAILINVADDR = 1u<<2, /* "failinvaddr" */
   EAF_DOMAINCHECK = 1u<<3, /* "domaincheck" <-> *expandaddr-domaincheck* */
   EAF_NAMETOADDR = 1u<<4, /* "nametoaddr": expand valid name to NAME@HOST */
   EAF_SHEXP_PARSE = 1u<<5, /* shexp_parse() the address first is allowed */
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
    * TODO addresses may be automatically removed, silently, and no one will
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
   EACM_STRICT = 1u<<1, /* Never allow any file or pipe addressee */
   EACM_MODE_MASK = 0x3u, /* _NORMAL and _STRICT are mutual! */

   EACM_NOLOG = 1u<<2, /* Do not log check errors */

   /* Some special overwrites of EAF_TARGETs.
    * May NOT clash with EAF_* bits which may be ORd to these here! */

   EACM_NONAME = 1u<<16,
   EACM_NONAME_OR_FAIL = 1u<<17,
   EACM_DOMAINCHECK = 1u<<18 /* Honour it! */
};

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
   CPROTO_NONE, /* Invalid.  But sometimes used to be able to parse an URL */
CPROTO_IMAP,
   CPROTO_POP3,
   CPROTO_SMTP,
   CPROTO_CCRED, /* Special dummy credential proto (S/MIME etc.) */
   CPROTO_CERTINFO, /* Special dummy proto for TLS certificate info xxx */
   CPROTO_SOCKS /* Special dummy SOCKS5 proxy proto */
   /* We need a _DEDUCE, as default, for normal URL object */
};

/* enum n_err_number from gen-config.h, which is in sync with
 * su_err_doc(), su_err_name() and su_err_from_name() */

enum n_exit_status{
   n_EXIT_OK = EXIT_SUCCESS,
   n_EXIT_ERR = EXIT_FAILURE,
   n_EXIT_USE = 64, /* sysexits.h:EX_USAGE */
   n_EXIT_NOUSER = 67, /* :EX_NOUSER */
   n_EXIT_IOERR = 74, /* :EX_IOERR */
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
   FEXP_MOST,
   FEXP_NOPROTO = 1u<<0, /* TODO no which_protocol() to decide sh expansion */
   FEXP_SILENT = 1u<<1, /* Do not print but only return errors */
   FEXP_MULTIOK = 1u<<2, /* Expansion to many entries is ok */
   FEXP_LOCAL = 1u<<3, /* Result must be local file/maildir */
   FEXP_LOCAL_FILE = 1u<<4, /* ..must be a local file: strips protocol://! */
   FEXP_SHORTCUT = 1u<<5, /* Do expand shortcuts */
   FEXP_NSPECIAL = 1u<<6, /* No %,#,& specials */
   FEXP_NFOLDER = 1u<<7, /* NSPECIAL and no + folder, too */
   FEXP_NSHELL = 1u<<8, /* Do not do shell word exp. (but ~/, $VAR) */
   FEXP_NVAR = 1u<<9, /* ..not even $VAR expansion */

   /* Actually does expand ~/ etc. */
   FEXP_NONE = FEXP_NOPROTO | FEXP_NSPECIAL | FEXP_NFOLDER | FEXP_NVAR,
   FEXP_FULL = FEXP_SHORTCUT /* Full expansion */
};

enum mx_header_subject_edit_flags{
   mx_HEADER_SUBJECT_EDIT_NONE = 0,
   /* Whether MIME decoding has to be performed first.
    * The data will be stored in auto-reclaimed memory */
   mx_HEADER_SUBJECT_EDIT_DECODE_MIME = 1u<<0,
   /* Without _DECODE_MIME and/or any _PREPEND bit this flag ensures that the
    * returned subject resides in detached auto-reclaimed storage */
   mx_HEADER_SUBJECT_EDIT_DUP = 1u<<1,

   /* What is to be trimmed */
   mx_HEADER_SUBJECT_EDIT_TRIM_RE = 1u<<8,
   mx_HEADER_SUBJECT_EDIT_TRIM_FWD = 1u<<9,
   mx_HEADER_SUBJECT_EDIT_TRIM_ALL = mx_HEADER_SUBJECT_EDIT_TRIM_RE |
         mx_HEADER_SUBJECT_EDIT_TRIM_FWD,

   /* Whether the according prefix should be prepended.
    * This will create and return an auto-reclaimed copy */
   mx_HEADER_SUBJECT_EDIT_PREPEND_RE = 1u<<16,
   mx_HEADER_SUBJECT_EDIT_PREPEND_FWD = 2u<<16,
   mx_HEADER_SUBJECT_EDIT__PREPEND_MASK = mx_HEADER_SUBJECT_EDIT_PREPEND_RE |
         mx_HEADER_SUBJECT_EDIT_PREPEND_FWD
};

enum n_header_extract_flags{
   n_HEADER_EXTRACT_NONE,
   n_HEADER_EXTRACT_EXTENDED = 1u<<0,
   n_HEADER_EXTRACT_FULL = 2u<<0,
   n_HEADER_EXTRACT__MODE_MASK = n_HEADER_EXTRACT_EXTENDED |
         n_HEADER_EXTRACT_FULL,

   n_HEADER_EXTRACT_COMPOSE_MODE = 1u<<8, /* Extracting during compose mode */
   /* Prefill the receivers with the already existing content of the given
    * struct header arguent */
   n_HEADER_EXTRACT_PREFILL_RECEIVERS = 1u<<9,
   /* Understand and ignore shell-style comments */
   n_HEADER_EXTRACT_IGNORE_SHELL_COMMENTS = 1u<<10,
   /* Ignore a MBOX From_ line _silently */
   n_HEADER_EXTRACT_IGNORE_FROM_ = 1u<<11
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
   n_PROTO_EML, /* Local electronic mail file (single message, rdonly) */
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
   /*n_SHEXP_PARSE_META_AMPERSAND = 1u<<22, * NEVER for this mailer! */
   /* Interpret ; as a sequencing operator, go_input_inject() remainder */
   n_SHEXP_PARSE_META_SEMICOLON = 1u<<23,
   /* LPAREN, RPAREN, LESSTHAN, GREATERTHAN */

   n_SHEXP_PARSE_META_MASK = n_SHEXP_PARSE_META_VERTBAR |
         /*n_SHEXP_PARSE_META_AMPERSAND |*/ n_SHEXP_PARSE_META_SEMICOLON,

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
   /*n_SHEXP_STATE_META_AMPERSAND = 1u<<8, NEVER! Metacharacter & follows/ed */
   n_SHEXP_STATE_META_SEMICOLON = 1u<<9, /* Metacharacter ; follows/ed */

   n_SHEXP_STATE_META_MASK = n_SHEXP_STATE_META_VERTBAR |
         /*n_SHEXP_STATE_META_AMPERSAND |*/ n_SHEXP_STATE_META_SEMICOLON,

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

enum n_program_option{
   n_PO_D = 1u<<0, /* -d / *debug* */
   n_PO_V = 1u<<1, /* -v / *verbose* */
   n_PO_VV = 1u<<2, /* .. more verbosity */
   n_PO_VVV = 1u<<3, /* .. most verbosity */
   n_PO_EXISTONLY = 1u<<4, /* -e */
   n_PO_HEADERSONLY = 1u<<5, /* -H */
   n_PO_HEADERLIST = 1u<<6, /* -L */
   n_PO_QUICKRUN_MASK = n_PO_EXISTONLY | n_PO_HEADERSONLY | n_PO_HEADERLIST,
   n_PO_E_FLAG = 1u<<7, /* -E / *skipemptybody* */
   n_PO_F_FLAG = 1u<<8, /* -F */
   n_PO_f_FLAG = 1u<<9, /* -f [and file on command line] */
   n_PO_Mm_FLAG = 1u<<10, /* -M or -m (plus n_poption_arg_Mm) */
   n_PO_R_FLAG = 1u<<11, /* -R */
   n_PO_r_FLAG = 1u<<12, /* -r (plus n_poption_arg_r) */
   n_PO_S_FLAG_TEMPORARY = 1u<<13, /* -S about to set a variable */
   n_PO_t_FLAG = 1u<<14, /* -t */
   n_PO_TILDE_FLAG = 1u<<15, /* -~ */
   n_PO_BATCH_FLAG = 1u<<16, /* -# */

   /* Some easy-access shortcut; the V bits must be contiguous! */
   n_PO_V_MASK = n_PO_V | n_PO_VV | n_PO_VVV,
   n_PO_D_V = n_PO_D | n_PO_V_MASK,
   n_PO_D_VV = n_PO_D | n_PO_VV | n_PO_VVV,
   n_PO_D_VVV = n_PO_D | n_PO_VVV
};
MCTA(n_PO_V << 1 == n_PO_VV, "PO_V* must be successive")
MCTA(n_PO_VV << 1 == n_PO_VVV, "PO_V* must be successive")

#define n_OBSOLETE(X) \
do if(!su_state_has(su_STATE_REPRODUCIBLE)){\
   static boole su_CONCAT(a__warned__, __LINE__);\
   if(!su_CONCAT(a__warned__, __LINE__)){\
      su_CONCAT(a__warned__, __LINE__) = TRU1;\
      n_err("%s: %s\n", _("Obsoletion warning"), X);\
   }\
}while(0)
#define n_OBSOLETE2(X,Y) \
do if(!su_state_has(su_STATE_REPRODUCIBLE)){\
   static boole su_CONCAT(a__warned__, __LINE__);\
   if(!su_CONCAT(a__warned__, __LINE__)){\
      su_CONCAT(a__warned__, __LINE__) = TRU1;\
      n_err("%s: %s: %s\n", _("Obsoletion warning"), X, Y);\
   }\
}while(0)

/* Program state bits which may regularly fluctuate */
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

   n_PS_ROBOT = 1u<<2, /* State machine in a macro/non-interactive chain */
   n_PS_COMPOSE_MODE = 1u<<3, /* State machine recursed */
   n_PS_COMPOSE_FORKHOOK = 1u<<4, /* A hook running in a subprocess */

   n_PS_HOOK_NEWMAIL = 1u<<7,
   n_PS_HOOK = 1u<<8,
   n_PS_HOOK_MASK = n_PS_HOOK_NEWMAIL | n_PS_HOOK,

   n_PS_EDIT = 1u<<9, /* Current mailbox no "system mailbox" */
   n_PS_SETFILE_OPENED = 1u<<10, /* (hack) setfile() opened a new box */
   /* After mailbox switch or `newmail': we have seen any command; if not set,
    * `next' will select message number 1 instead of "next good after dot" */
   n_PS_SAW_COMMAND = 1u<<11,
   n_PS_DID_PRINT_DOT = 1u<<12, /* Current message has been printed */

   n_PS_SIGWINCH_PEND = 1u<<13, /* Need update of $COLUMNS/$LINES */
   n_PS_PSTATE_PENDMASK = n_PS_SIGWINCH_PEND, /* pstate housekeeping needed */

   n_PS_ARGLIST_MASK = su_BITENUM_MASK(14, 18),
   n_PS_MSGLIST_MASK = su_BITENUM_MASK(17, 18),
   n_PS_ARGMOD_LOCAL = 1u<<14, /* "local" modifier TODO struct CmdCtx */
   n_PS_ARGMOD_VPUT = 1u<<15, /* "vput" modifier TODO struct CmdCtx */
   n_PS_ARGMOD_WYSH = 1u<<16, /* "wysh" modifier TODO struct CmdCtx */
   n_PS_MSGLIST_GABBY = 1u<<17, /* n_getmsglist() saw something gabby */
   n_PS_MSGLIST_DIRECT = 1u<<18, /* A msg was directly chosen by number */

   n_PS_EXPAND_MULTIRESULT = 1u<<19, /* Last fexpand() with MULTIOK had .. */
   /* In the interactive mainloop, we want any error to appear once for each
    * tick, even if it is the same as in the tick before and would normally be
    * suppressed */
   n_PS_ERRORS_NEED_PRINT_ONCE = 1u<<20,

   /* Bad hacks */
   n_PS_HEADER_NEEDED_MIME = 1u<<24, /* mime_write_tohdr() not ASCII clean */
   n_PS_READLINE_NL = 1u<<25, /* readline_input()+ saw a \n */
   n_PS_BASE64_STRIP_CR = 1u<<26, /* Go for text output, strip CR's */
   n_PS_SIGALARM = 1u<<27 /* Some network timer has alarm(2) installed */
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
   /* *posix* requires us to exit with error if sending any mail failed */
   n_PSO_SEND_ERROR = 1u<<5,

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
   n_PSO_GETFILENAME_QUOTE_NOTED = 1u<<16,
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
 * - Other i3val=s and/or defval=s are imposed by POSIX: we do not (or only
 *   additionally "trust" the system-wide RC file to establish the settings
 * - code assumes (in conjunction with make-okey-map.pl) case-insensitive sort!
 * (Keep in SYNC: nail.h:okeys, nail.rc, nail.1:"Initial settings") */
enum okeys{
   /* This is used for all macro(-local) variables etc., i.e.,
    * [*@#]|[1-9][0-9]*, in order to have something with correct properties.
    * It is also used for the ${^.+} multiplexer */
   ok_v___special_param, /* {nolopts=1,rdonly=1,nodel=1} */
   /*__qm/__em aka ?/! should be num=1 but that more expensive than what now */
   ok_v___qm, /* {name=?,nolopts=1,rdonly=1,nodel=1} */
   ok_v___em, /* {name=!,nolopts=1,rdonly=1,nodel=1} */

   ok_v_account, /* {nolopts=1,rdonly=1,nodel=1} */
   ok_b_add_file_recipients,
ok_v_agent_shell_lookup, /* {obsolete=1} */
   ok_b_allnet,
   ok_b_append,
   /* *ask* is auto-mapped to *asksub* as imposed by standard! */
   ok_b_ask, /* {vip=1} */
   ok_b_askatend,
   ok_b_askattach,
   ok_b_askbcc,
   ok_b_askcc,
   ok_b_asksign,
   ok_b_asksend,
   ok_b_asksub, /* {i3val=TRU1} */
   ok_v_attrlist,
   ok_v_autobcc,
   ok_v_autocc,
   ok_b_autocollapse,
   ok_b_autoprint,
ok_b_autothread, /* {obsolete=1} */
   ok_v_autosort,

   ok_b_bang,
ok_v_bind_timeout, /* {vip=1,obsolete=1,notempty=1,posnum=1} */
   ok_v_bind_inter_byte_timeout, /* {\ } */
      /* {notempty=1,posnum=1,defval=mx_BIND_INTER_BYTE_TIMEOUT} */
   ok_v_bind_inter_key_timeout, /* {notempty=1,posnum=1} */
ok_b_bsdannounce, /* {obsolete=1} */
   ok_b_bsdcompat,
   ok_b_bsdflags,
   ok_b_bsdheadline,
   ok_b_bsdmsgs,
   ok_b_bsdorder,
   ok_v_build_cc, /* {virt=VAL_BUILD_CC_ARRAY} */
   ok_v_build_ld, /* {virt=VAL_BUILD_LD_ARRAY} */
   ok_v_build_os, /* {virt=VAL_BUILD_OS} */
   ok_v_build_rest, /* {virt=VAL_BUILD_REST_ARRAY} */

   ok_v_COLUMNS, /* {notempty=1,posnum=1,env=1} */
   /* Charset lowercase conversion handled via vip= */
   ok_v_charset_7bit, /* {vip=1,notempty=1,defval=CHARSET_7BIT} */
   /* But unused without mx_HAVE_ICONV, we use ok_vlook(CHARSET_8BIT_OKEY)! */
   ok_v_charset_8bit, /* {vip=1,notempty=1,defval=CHARSET_8BIT} */
   ok_v_charset_unknown_8bit, /* {vip=1} */
   ok_v_cmd,
   ok_b_colour_disable,
   ok_b_colour_pager,
   ok_v_contact_mail, /* {virt=VAL_CONTACT_MAIL} */
   ok_v_contact_web, /* {virt=VAL_CONTACT_WEB} */
   ok_v_content_description_forwarded_message, /* {\ } */
      /* {defval=mx_CONTENT_DESC_FORWARDED_MESSAGE} */
   ok_v_content_description_quote_attachment, /* {\ } */
      /* {defval=mx_CONTENT_DESC_QUOTE_ATTACHMENT} */
   ok_v_content_description_smime_message, /* {\ } */
      /* {defval=mx_CONTENT_DESC_SMIME_MESSAGE} */
   ok_v_content_description_smime_signature, /* {\ } */
      /* {defval=mx_CONTENT_DESC_SMIME_SIG} */

   ok_v_crt, /* {posnum=1} */
   ok_v_customhdr, /* {vip=1} */

   ok_v_DEAD, /* {notempty=1,env=1,defval=VAL_DEAD} */
   ok_v_datefield, /* {i3val="%Y-%m-%d %H:%M"} */
   ok_v_datefield_markout_older, /* {i3val="%Y-%m-%d"} */
   ok_b_debug, /* {vip=1} */
   ok_b_disposition_notification_send,
   ok_b_dot,
   ok_b_dotlock_disable,
ok_b_dotlock_ignore_error, /* {obsolete=1} */

   ok_v_EDITOR, /* {env=1,notempty=1,defval=VAL_EDITOR} */
   ok_v_editalong,
   ok_b_editheaders,
   ok_b_emptystart,
ok_v_encoding, /* {obsolete=1} */
   ok_b_errexit,
   ok_v_errors_limit, /* {notempty=1,posnum=1,defval=VAL_ERRORS_LIMIT} */
   ok_v_escape, /* {defval=n_ESCAPE} */
   ok_v_expandaddr,
   ok_v_expandaddr_domaincheck, /* {notempty=1} */
   ok_v_expandargv,

   ok_v_features, /* {virt=VAL_FEATURES} */
   ok_b_flipr,
   ok_v_folder, /* {vip=1} */
   ok_v_folder_resolved, /* {rdonly=1,nodel=1} */
   ok_v_folder_hook,
   ok_b_followup_to,
   ok_b_followup_to_add_cc,
   ok_v_followup_to_honour,
   ok_b_forward_add_cc,
   ok_b_forward_as_attachment,
   ok_v_forward_inject_head,
   ok_v_forward_inject_tail,
   ok_v_from, /* {vip=1} */
   ok_b_fullnames,
ok_v_fwdheading, /* {obsolete=1} */

   ok_v_HOME, /* {vip=1,nodel=1,notempty=1,import=1} */
   ok_b_header, /* {i3val=TRU1} */
   ok_v_headline,
   ok_v_headline_bidi,
   ok_b_headline_plain,
   ok_v_history_file,
   ok_v_history_gabby,
   ok_b_history_gabby_persist,
   ok_v_history_size, /* {notempty=1,posnum=1} */
   ok_b_hold,
   ok_v_hostname, /* {vip=1} */

   ok_b_idna_disable,
   ok_v_ifs, /* {vip=1,defval=" \t\n"} */
   ok_v_ifs_ws, /* {vip=1,rdonly=1,nodel=1,i3val=" \t\n"} */
   ok_b_ignore,
   ok_b_ignoreeof,
   ok_v_inbox,
   ok_v_indentprefix, /* {defval="\t"} */

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

   ok_v_MAIL, /* {env=1} */
   ok_v_MAILCAPS, /* {import=1,defval=VAL_MAILCAPS} */
   ok_v_MAILRC, /* {import=1,notempty=1,defval=VAL_MAILRC} */
   ok_b_MAILX_NO_SYSTEM_RC, /* {name=MAILX_NO_SYSTEM_RC,import=1} */
   ok_v_MBOX, /* {env=1,notempty=1,defval=VAL_MBOX} */
   ok_v_mailbox_resolved, /* {nolopts=1,rdonly=1,nodel=1} */
   ok_v_mailbox_display, /* {nolopts=1,rdonly=1,nodel=1} */
   ok_b_mailcap_disable,
   ok_v_mailx_extra_rc, /* {notempty=1} */
   ok_b_markanswered,
   ok_b_mbox_fcc_and_pcc, /* {i3val=1} */
   ok_b_mbox_rfc4155,
   ok_b_memdebug, /* {vip=1} */
   ok_b_message_id_disable,
   ok_v_message_inject_head,
   ok_v_message_inject_tail,
   ok_b_metoo,
   ok_b_mime_allow_text_controls,
   ok_b_mime_alternative_favour_rich,
   ok_v_mime_counter_evidence, /* {posnum=1} */
   ok_v_mime_encoding,
   ok_b_mime_force_sendout,
   ok_v_mimetypes_load_control,
   ok_v_mta, /* {notempty=1,defval=VAL_MTA} */
   ok_v_mta_aliases, /* {notempty=1} */
   ok_v_mta_arguments,
   ok_b_mta_no_default_arguments,
   ok_b_mta_no_receiver_arguments,
   ok_v_mta_argv0, /* {notempty=1,defval=VAL_MTA_ARGV0} */
   ok_b_mta_bcc_ok,

ok_v_NAIL_EXTRA_RC, /* {name=NAIL_EXTRA_RC,env=1,notempty=1,obsolete=1} */
ok_b_NAIL_NO_SYSTEM_RC, /* {name=NAIL_NO_SYSTEM_RC,import=1,obsolete=1} */
ok_v_NAIL_HEAD, /* {name=NAIL_HEAD,obsolete=1} */
ok_v_NAIL_HISTFILE, /* {name=NAIL_HISTFILE,obsolete=1} */
ok_v_NAIL_HISTSIZE, /* {name=NAIL_HISTSIZE,notempty=1,num=1,obsolete=1} */
ok_v_NAIL_TAIL, /* {name=NAIL_TAIL,obsolete=1} */
   ok_v_NETRC, /* {env=1,notempty=1,defval=VAL_NETRC} */
   ok_b_netrc_lookup, /* {chain=1} */
   ok_v_netrc_pipe,
   ok_v_newfolders,
   ok_v_newmail,

   ok_v_on_account_cleanup, /* {notempty=1} */
   ok_v_on_compose_cleanup, /* {notempty=1} */
   ok_v_on_compose_enter, /* {notempty=1} */
   ok_v_on_compose_leave, /* {notempty=1} */
   ok_v_on_compose_splice, /* {notempty=1} */
   ok_v_on_compose_splice_shell, /* {notempty=1} */
   ok_v_on_history_addition, /* {notempty=1} */
   ok_v_on_main_loop_tick, /* {notempty=1} */
   ok_v_on_program_exit, /* {notempty=1} */
   ok_v_on_resend_cleanup, /* {notempty=1} */
   ok_v_on_resend_enter, /* {notempty=1} */
   ok_b_outfolder,

   ok_v_PAGER, /* {env=1,notempty=1,defval=VAL_PAGER} */
   ok_v_PATH, /* {nodel=1,import=1} */
   /* XXX POSIXLY_CORRECT->posix: needs initial call via main()! */
   ok_b_POSIXLY_CORRECT, /* {vip=1,import=1,name=POSIXLY_CORRECT} */
   ok_b_page,
   ok_v_password, /* {chain=1} */
   ok_b_piperaw,
   ok_v_pop3_auth, /* {chain=1} */
   ok_b_pop3_bulk_load,
   ok_v_pop3_keepalive, /* {notempty=1,posnum=1} */
   ok_b_pop3_no_apop, /* {chain=1} */
   ok_b_pop3_use_starttls, /* {chain=1} */
   ok_b_posix, /* {vip=1} */
   ok_b_print_alternatives,
   ok_v_prompt, /* {i3val="? "} */
   ok_v_prompt2, /* {i3val=".. "} */

   ok_b_quiet,
   ok_v_quote,
   ok_b_quote_add_cc,
   ok_b_quote_as_attachment,
   ok_v_quote_chars, /* {vip=1,notempty=1,defval=">|}:"} */
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
ok_v_replyto, /* {obsolete=1,notempty=1} */
   ok_v_reply_to, /* {notempty=1} */
   ok_v_reply_to_honour,
   ok_v_reply_to_swap_in,
   ok_b_rfc822_body_from_, /* {name=rfc822-body-from_} */

   ok_v_SHELL, /* {import=1,notempty=1,defval=VAL_SHELL} */
ok_b_SYSV3, /* {env=1,obsolete=1} */
   ok_b_save, /* {i3val=TRU1} */
   ok_v_screen, /* {notempty=1,posnum=1} */
   ok_b_searchheaders,
   /* Charset lowercase conversion handled via vip= */
   ok_v_sendcharsets, /* {vip=1} */
   ok_b_sendcharsets_else_ttycharset,
   ok_v_sender, /* {vip=1} */
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
   ok_b_skipemptybody, /* {vip=1} */
   ok_v_smime_ca_dir,
   ok_v_smime_ca_file,
   ok_v_smime_ca_flags,
   ok_b_smime_ca_no_defaults,
   ok_v_smime_cipher, /* {chain=1} */
   ok_v_smime_crl_dir,
   ok_v_smime_crl_file,
   ok_v_smime_encrypt, /* {chain=1} */
   ok_b_smime_force_encryption,
ok_b_smime_no_default_ca, /* {obsolete=1} */
   ok_b_smime_sign,
   ok_v_smime_sign_cert, /* {chain=1} */
   ok_v_smime_sign_digest, /* {chain=1} */
   ok_v_smime_sign_include_certs, /* {chain=1} */
ok_v_smime_sign_message_digest, /* {chain=1,obsolete=1} */
ok_v_smtp, /* {obsolete=1} */
   ok_v_smtp_auth, /* {chain=1} */
ok_v_smtp_auth_password, /* {obsolete=1} */
ok_v_smtp_auth_user, /* {obsolete=1} */
   ok_v_smtp_config, /* {chain=1} */
   ok_v_smtp_hostname, /* {vip=1,chain=1} */
ok_b_smtp_use_starttls, /* {chain=1,obsolete=1} */
   ok_v_SOCKS5_PROXY, /* {vip=1,import=1,notempty=1,name=SOCKS5_PROXY} */
   ok_v_SOURCE_DATE_EPOCH, /* {\ } */
      /* {name=SOURCE_DATE_EPOCH,rdonly=1,import=1,notempty=1,posnum=1} */
   ok_v_socket_connect_timeout, /* {posnum=1} */
   ok_v_socks_proxy, /* {vip=1,chain=1,notempty=1} */
   ok_v_spam_interface,
   ok_v_spam_maxsize, /* {notempty=1,posnum=1} */
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
   ok_v_system_mailrc, /* {virt=VAL_SYSCONFDIR "/" VAL_SYSCONFRC} */

   ok_v_TERM, /* {env=1} */
   ok_v_TMPDIR, /* {import=1,vip=1,notempty=1,defval=VAL_TMPDIR} */
   ok_v_termcap,
   ok_b_termcap_ca_mode,
   ok_b_termcap_disable,
   ok_v_tls_ca_dir, /* {chain=1} */
   ok_v_tls_ca_file, /* {chain=1} */
   ok_v_tls_ca_flags, /* {chain=1} */
   ok_b_tls_ca_no_defaults, /* {chain=1} */
   ok_v_tls_config_file,
   ok_v_tls_config_module, /* {chain=1} */
   ok_v_tls_config_pairs, /* {chain=1} */
   ok_v_tls_crl_dir,
   ok_v_tls_crl_file,
   ok_v_tls_features, /* {virt=VAL_TLS_FEATURES} */
   ok_v_tls_fingerprint, /* {chain=1} */
   ok_v_tls_fingerprint_digest, /* {chain=1} */
   ok_v_tls_rand_file,
   ok_v_tls_verify, /* {chain=1} */
   ok_v_toplines, /* {notempty=1,num=1,defval="5"} */
   ok_b_topsqueeze,
   /* Charset lowercase conversion handled via vip= */
   ok_v_ttycharset, /* {vip=1,notempty=1,defval=CHARSET_8BIT} */
   ok_b_typescript_mode, /* {vip=1} */

   ok_v_USER, /* {rdonly=1,import=1} */
   ok_v_umask, /* {vip=1,nodel=1,posnum=1,i3val="0077"} */
   ok_v_user, /* {notempty=1,chain=1} */

   ok_v_VISUAL, /* {env=1,notempty=1,defval=VAL_VISUAL} */
   ok_v_v15_compat, /* {i3val="y"} */
   ok_v_verbose, /* {vip=1,posnum=1} */
   ok_v_version, /* {virt=mx_VERSION} */
   ok_v_version_date, /* {virt=mx_VERSION_DATE} */
   ok_v_version_hexnum, /* {virt=mx_VERSION_HEXNUM,posnum=1} */
   ok_v_version_major, /* {virt=mx_VERSION_MAJOR,posnum=1} */
   ok_v_version_minor, /* {virt=mx_VERSION_MINOR,posnum=1} */
   ok_v_version_update, /* {virt=mx_VERSION_UPDATE,posnum=1} */

   ok_b_writebackedited

,  /* Obsolete IMAP related non-sorted */
ok_b_disconnected, /* {chain=1} */
ok_v_imap_auth, /* {chain=1} */
ok_v_imap_cache,
ok_v_imap_delim, /* {chain=1} */
ok_v_imap_keepalive, /* {chain=1} */
ok_v_imap_list_depth,
ok_b_imap_use_starttls, /* {chain=1} */

   ok_v__S_MAILX_TEST /* {name=_S_MAILX_TEST,env=1} */
}; /* }}} */
enum{
   n_OKEYS_FIRST = ok_v_account, /* Truly accessible first */
   n_OKEYS_MAX = ok_v__S_MAILX_TEST
};

/* Forwards */
struct mx_attachment;
struct mx_name;

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
   su_ALLOC(VSTRUCT_SIZEOF(struct n_strlist, sl_dat) + (SZ) +1)
#define n_STRLIST_AUTO_ALLOC(SZ) \
   su_AUTO_ALLOC(VSTRUCT_SIZEOF(struct n_strlist, sl_dat) + (SZ) +1)
#define n_STRLIST_LOFI_ALLOC(SZ) \
   su_LOFI_ALLOC(VSTRUCT_SIZEOF(struct n_strlist, sl_dat) + (SZ) +1)
#define n_STRLIST_PLAIN_SIZE() VSTRUCT_SIZEOF(struct n_strlist, sl_dat)

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
      MB_UIDPLUS = 001, /* supports IMAP UIDPLUS */
      MB_SASL_IR = 002  /* supports RFC 4959 SASL-IR */
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
   u32/*enum mx_mime_type*/ m_mime_type;
   u32/*enum mx_mime_enc*/ m_mime_enc;
   char *m_partstring; /* Part level string */
   char *m_filename; /* ..of attachment */
   char const *m_content_description;
   char const *m_external_body_url; /* message/external-body:access-type=URL */
   struct mx_mime_type_handler *m_handler; /* MIME handler if yet classified */
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
   /* HACK: support "|bla", i.e., anything enclosed in quotes; e.g., used for
    * MTA alias parsing */
   GQUOTE_ENCLOSED_OK = 1u<<23
};
#define GMASK (GTO | GSUBJECT | GCC | GBCC)

enum header_flags{
   HF_NONE = 0,

   HF_CMD_forward = 1u<<0,
   HF_CMD_mail = 2u<<0,
   HF_CMD_Lreply = 3u<<0,
   HF_CMD_Reply = 4u<<0,
   HF_CMD_reply = 5u<<0,
   HF_CMD_resend = 6u<<0,
   HF_CMD_MASK = 7u<<0,

   HF_LIST_REPLY = 1u<<8, /* `Lreply' (special address massage needed) */
   HF_MFT_SENDER = 1u<<9, /* Add ourselves to Mail-Followup-To: */
   HF_RECIPIENT_RECORD = 1u<<10, /* Save message in file named after rec. */
   HF_COMPOSE_MODE = 1u<<11, /* XXX not here Header in compose-mode */
   HF_USER_EDITED = 1u<<12, /* User has edited the template at least once */
   HF__NEXT_SHIFT = 16u
};
#define HF_CMD_TO_OFF(CMD) ((CMD) - 1)

/* Structure used to pass about the current state of a message (header) */
struct n_header_field{
   struct n_header_field *hf_next;
   u32 hf_nl; /* Field-name length */
   u32 hf_bl; /* Field-body length*/
   char hf_dat[VFIELD_SIZE(0)];
};

struct header{
   BITENUM_IS(u32,header_flags) h_flags;
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
   struct mx_attachment *h_attach; /* MIME attachments */
   struct mx_name *h_reply_to; /* overridden "Reply-To:" field */
   struct mx_name *h_message_id; /* overridden "Message-ID:" field */
   struct mx_name *h_in_reply_to;/* overridden "In-Reply-To:" field */
   struct mx_name *h_mft; /* Mail-Followup-To */
   char const *h_list_post; /* Address from List-Post:, for `Lreply' */
   struct n_header_field *h_user_headers;
   struct n_header_field *h_custom_headers; /* (Cached result) */
   /* Raw/original versions of the header(s). If any */
   struct mx_name *h_mailx_raw_to;
   struct mx_name *h_mailx_raw_cc;
   struct mx_name *h_mailx_raw_bcc;
   struct mx_name *h_mailx_orig_sender;
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

struct mx_send_ctx{
   struct header *sc_hp;
   struct mx_name *sc_to;
   FILE *sc_input;
   struct mx_url *sc_urlp; /* Or NIL for file-based MTA */
   struct mx_cred_ctx *sc_credp; /* cred-auth.h not included */
   struct str sc_signer; /* USER@HOST for signing+ */
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
VL u32 n_psonce; /* Bits of enum n_program_state_once */
VL u32 n_pstate; /* Bits of enum n_program_state */
/* TODO "cmd_tab.h ARG_EM set"-storage (n_[01..]) as long as we don't have a
 * TODO struct CmdCtx where each command has its own ARGC/ARGV, errno and exit
 * TODO status and may-place-in-history bit, need to manage global bypass.. */

#ifdef mx_HAVE_ERRORS
VL u32 n_pstate_err_cnt; /* What backs $^ERRQUEUE-xy */
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
/* getmsglist() 1st marked (for e.g. `Reply') HACK TODO (should be in a ctx) */
VL struct message *n_msgmark1;
VL int *n_msgvec; /* Folder setmsize(), list.c res. store */
#ifdef mx_HAVE_IMAP
VL int imap_created_mailbox; /* hack to get feedback from imap */
#endif

VL struct n_header_field *n_customhdr_list; /* *customhdr* list */

VL struct time_current time_current; /* time(3); send: mail1() XXXcarrier */

#ifdef mx_HAVE_TLS
VL enum n_tls_verify_level n_tls_verify_level; /* TODO local per-context! */
#endif

VL volatile int interrupts; /* TODO rid! */

/*
 * Finally, let's include the function prototypes XXX embed
 */

#ifndef mx_SOURCE_PS_DOTLOCK_MAIN
# include "mx/nailfuns.h"
#endif

#include "su/code-ou.h"
#endif /* n_NAIL_H */
/* s-it-mode */
