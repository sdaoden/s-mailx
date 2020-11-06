/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ MIME types: handlers, `mimetype', etc.
 *
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_MIME_TYPE_H
#define mx_MIME_TYPE_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_mime_type_handler;

enum mx_mime_type{
   mx_MIME_TYPE_UNKNOWN, /* unknown */
   mx_MIME_TYPE_SUBHDR, /* inside a multipart subheader */
   mx_MIME_TYPE_822, /* message/rfc822 */
   mx_MIME_TYPE_MESSAGE, /* other message/ */
   mx_MIME_TYPE_TEXT_PLAIN, /* text/plain */
   mx_MIME_TYPE_TEXT_HTML, /* text/html */
   mx_MIME_TYPE_TEXT, /* other text/ */
   mx_MIME_TYPE_ALTERNATIVE, /* multipart/alternative */
   mx_MIME_TYPE_RELATED, /* mime/related (RFC 2387) */
   mx_MIME_TYPE_DIGEST, /* multipart/digest */
   mx_MIME_TYPE_SIGNED, /* multipart/signed */
   mx_MIME_TYPE_ENCRYPTED, /* multipart/encrypted */
   mx_MIME_TYPE_MULTI, /* other multipart/ */
   mx_MIME_TYPE_PKCS7, /* PKCS7 */
   mx_MIME_TYPE_DISCARD /* is discarded */
};

enum mx_mime_type_handler_flags{
   mx_MIME_TYPE_HDL_NIL, /* No pipe- mimetype handler, go away */
   mx_MIME_TYPE_HDL_CMD, /* Normal command */
   mx_MIME_TYPE_HDL_MSG, /* Display msg (returned as command string) */
   mx_MIME_TYPE_HDL_PTF, /* A special pointer-to-function handler */
   mx_MIME_TYPE_HDL_TEXT, /* ? special cmd to force treatment as text */
   mx_MIME_TYPE_HDL_TEXT, /* @ special cmd to force treatment as text */
   mx_MIME_TYPE_HDL_TYPE_MASK = 7u,

   /* compose, composetyped, edit, print */

   mx_MIME_TYPE_HDL_ASYNC = 1u<<8, /* Should run asynchronously */
   mx_MIME_TYPE_HDL_COPIOUSOUTPUT = 1u<<9, /* _CMD output directly usable */
   mx_MIME_TYPE_HDL_NEEDSTERM = 1u<<10, /* Takes over terminal */
   mx_MIME_TYPE_HDL_NOQUOTE = 1u<<11, /* No MIME for quoting */
   mx_MIME_TYPE_HDL_TMPF = 1u<<12, /* Create temporary file (zero-sized) */
   mx_MIME_TYPE_HDL_TMPF_FILL = 1u<<13, /* Fill in the msg body content */
   mx_MIME_TYPE_HDL_TMPF_UNLINK = 1u<<14, /* Delete it later again */
   /* Handler with _HDL_TMPF has a .mth_tmpf_nametmpl.. */
   mx_MIME_TYPE_HDL_TMPF_NAMETMPL = 1u<<15,
   mx_MIME_TYPE_HDL_TMPF_NAMETMPL_SUFFIX = 1u<<16 /* ..to be used as suffix */
   /* xxx textualnewlines */
};
enum {mx_MIME_TYPE_HDL_MAX = mx_MIME_TYPE_HDL_TMPF_NAMETMPL_SUFFIX};

struct mx_mime_type_handler{
   BITENUM_IS(u32,mx_mime_type_handler_flags) mth_flags;
   su_64( u8 mth__dummy[4]; )
   char const *mth_tmpf_nametmpl; /* Only with HDL_TMPF_NAMETMPL */
   /* XXX union{} the following? */
   char const *mth_shell_cmd; /* For HDL_CMD */
   int (*mth_ptf)(void); /* PTF main() for HDL_PTF */
   struct str mth_msg; /* Message describing this command */
};

/* Check whether name is correct according to RFC 4288, 4.2.
 * With t_a_subt, check for valid TYPE/SUBTYPE.
 * With subt_wildcard_ok, allow * as a SUBTYPE. */
EXPORT boole mx_mime_type_is_valid(char const *name, boole t_a_subt,
      boole subt_wildcard_ok);

/* Check whether the Content-Type name is internally known */
EXPORT boole mx_mime_type_is_known(char const *name);

/* Return a Content-Type matching the name, or NIL if none could be found */
EXPORT char *mx_mime_type_classify_filename(char const *name);

/* Classify content of *fp* as necessary and fill in arguments; **charset* is
 * set to *charset-7bit* or mime_charset_iter_or_fallback() if NIL.
 * no_mboxo states whether 7BIT/8BIT is acceptable if only existence of
 * a ^From_ would otherwise enforce QP/BASE64 */
/* TODO this should take a carrier and only fill that in with what has been
 * TODO detected/classified, and suggest hints; rest up to caller!
 * TODO This is not only more correct (no_mboxo crux++), it simplifies a lot */
EXPORT enum conversion mx_mime_type_classify_file(FILE *fp,
      char const **content_type, char const **charset, boole *do_iconv,
      boole no_mboxo);

/* Dependent on *mime-counter-evidence* mpp->m_ct_type_usr_ovwr will be set,
 * but otherwise mpp is const.  for_user_context rather maps 1:1 to
 * MIME_PARSE_FOR_USER_CONTEXT */
EXPORT enum mx_mime_type mx_mime_type_classify_part(struct mimepart *mpp,
      boole for_user_context);

/* Query handler for a part, return the plain type (& HDL_TYPE_MASK).
 * mthp is anyway initialized (.mth_flags, .mth_msg) */
EXPORT BITENUM_IS(u32,mx_mime_type_handler_flags) mx_mime_type_handler(
      struct mx_mime_type_handler *mthp, struct mimepart const *mpp,
      enum sendaction action);

/* `(un)?mimetype' commands */
EXPORT int c_mimetype(void *vp);
EXPORT int c_unmimetype(void *vp);

#include <su/code-ou.h>
#endif /* mx_MIME_TYPE_H */
/* s-it-mode */
