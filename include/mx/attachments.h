/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Handling of attachments. TODO Interface sick
 *
 * Copyright (c) 2012 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_ATTACHMENTS_H
#define mx_ATTACHMENTS_H

#include <mx/nail.h>

#include <mx/go.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_attachment;

/* Forwards */
struct mx_name;

/* MIME attachments */
enum mx_attachments_conv{
   mx_ATTACHMENTS_CONV_DEFAULT, /* _get_lc() -> _iter_*() */
   mx_ATTACHMENTS_CONV_FIX_INCS, /* "charset=".a_input_charset (nocnv) */
   mx_ATTACHMENTS_CONV_TMPFILE /* attachment.a_tmpf is converted */
};

enum mx_attachments_error{
   mx_ATTACHMENTS_ERR_NONE,
   mx_ATTACHMENTS_ERR_FILE_OPEN,
   mx_ATTACHMENTS_ERR_ICONV_FAILED,
   mx_ATTACHMENTS_ERR_ICONV_NAVAIL,
   mx_ATTACHMENTS_ERR_OTHER
};

struct mx_attachment{
   struct mx_attachment *a_flink; /* Forward link in list. */
   struct mx_attachment *a_blink; /* Backward list link */
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
   enum mx_attachments_conv a_conv; /* User chosen conversion */
   int a_msgno; /* message number */
};

/* Try to add an attachment for file, fexpand(_LOCAL|_NOPROTO)ed.
 * Return the new aplist aphead.
 * The newly created attachment may be stored in *newap, or NIL on error */
EXPORT struct mx_attachment *mx_attachments_append(
      struct mx_attachment *aplist, char const *file,
      BITENUM_IS(u32,mx_attach_error) *aerr_or_nil,
      struct mx_attachment **newap_or_nil);

/* Shell-token parse names, and append resulting file names to aplist, return
 * (new) aplist head */
EXPORT struct mx_attachment *mx_attachments_append_list(
      struct mx_attachment *aplist, char const *names);

/* Remove ap from aplist, and return the new aplist head */
EXPORT struct mx_attachment *mx_attachments_remove(
      struct mx_attachment *aplist, struct mx_attachment *ap);

/* Find by file-name.  If any path component exists in name then an exact match
 * of the creation-path is used directly; if instead the basename of that path
 * matches all attachments are traversed to find an exact match first, the
 * first of all basename matches is returned as a last resort;
 * If no path component exists the filename= parameter is searched (and also
 * returned) in preference over the basename, otherwise likewise.
 * If name is in fact a message number the first match is taken.
 * If stat_or_null is given: FAL0 on NIL return, TRU1 for exact/single match,
 * TRUM1 for ambiguous matches */
EXPORT struct mx_attachment *mx_attachments_find(struct mx_attachment *aplist,
      char const *name, boole *stat_or_nil);

/* Interactively edit the attachment list, return updated list */
EXPORT struct mx_attachment *mx_attachments_list_edit(
      struct mx_attachment *aplist, BITENUM_IS(u32,mx_go_input_flags) gif);

/* Print all attachments, return number of lines written, -1 on error */
EXPORT sz mx_attachments_list_print(struct mx_attachment const *aplist,
      FILE *fp);

#include <su/code-ou.h>
#endif /* mx_ATTACHMENTS_H */
/* s-it-mode */
