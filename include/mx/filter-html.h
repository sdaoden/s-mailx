/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ HTML tagsoup filter.
 *
 * Copyright (c) 2015 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

/* TODO Because we don't support filter chains yet this filter will be run
 * TODO in a dedicated subprocess, driven via a special Popen() mode */
FL int         htmlflt_process_main(void);

FL void        htmlflt_init(struct htmlflt *self);
FL void        htmlflt_destroy(struct htmlflt *self);
FL void        htmlflt_reset(struct htmlflt *self, FILE *f);
FL ssize_t     htmlflt_push(struct htmlflt *self, char const *dat, size_t len);
FL ssize_t     htmlflt_flush(struct htmlflt *self);

#endif /* mx_HAVE_FILTER_HTML_TAGSOUP */
#endif /* mx_FILTER_HTML_H */
/* s-it-mode */
