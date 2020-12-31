/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ HTML tagsoup filter.
 *
 * Copyright (c) 2015 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_FILTER_HTML_H
#define mx_FILTER_HTML_H

#include <mx/nail.h>
#ifdef mx_HAVE_FILTER_HTML_TAGSOUP

#define mx_HEADER
#include <su/code-in.h>

struct mx_flthtml;

struct mx_flthtml{
   FILE *fh_os; /* Output stream */
   u32 fh_flags;
   u32 fh_lmax; /* Maximum byte +1 in .fh_line/4 */
   u32 fh_len; /* Current bytes in .fh_line */
   u32 fh_last_ws; /* Last whitespace on line (fold purposes) */
   u32 fh_mboff; /* Last offset for "mbtowc" */
   u32 fh_mbwidth; /* We count characters not bytes if possible */
   char *fh_line; /* Output line buffer - MUST be last field! */
   s32 fh_href_dist; /* Count of lines since last HREF flush */
   u32 fh_href_no; /* HREF sequence number */
   struct mx_flthtml_href *fh_hrefs;
   struct mx_flthtml_tag const *fh_ign_tag; /* Tag that ends ignore mode */
   char *fh_curr; /* Current cursor into .fh_bdat */
   char *fh_bmax; /* Maximum byte in .fh_bdat +1 */
   char *fh_bdat; /* (Temporary) Tag content data storage */
};

/* TODO Because we don't support filter chains yet this filter will be run
 * TODO in a dedicated subprocess, driven via a special fs_popen() mode */
EXPORT int mx_flthtml_process_main(void);

EXPORT void mx_flthtml_init(struct mx_flthtml *self);
EXPORT void mx_flthtml_destroy(struct mx_flthtml *self);
EXPORT void mx_flthtml_reset(struct mx_flthtml *self, FILE *f);
EXPORT sz mx_flthtml_push(struct mx_flthtml *self, char const *dat,uz len);
EXPORT sz mx_flthtml_flush(struct mx_flthtml *self);

#include <su/code-ou.h>
#endif /* mx_HAVE_FILTER_HTML_TAGSOUP */
#endif /* mx_FILTER_HTML_H */
/* s-it-mode */
