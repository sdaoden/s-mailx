/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ struct quoteflt: quotation (sub) filter.
 *
 * Copyright (c) 2012/3 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_FILTER_QUOTE_H
#define mx_FILTER_QUOTE_H

#include <mx/nail.h>

#if defined mx_HAVE_QUOTE_FOLD && defined mx_HAVE_C90AMEND1
# include <wchar.h>
#endif

struct quoteflt {
   FILE        *qf_os;        /* Output stream */
   char const  *qf_pfix;
   ui32_t      qf_pfix_len;   /* Length of prefix: 0: bypass */
   ui32_t      qf_qfold_min;  /* Simple way: wrote prefix? */
   bool_t      qf_bypass;     /* Simply write to .qf_os TODO BYPASS, then! */
   /* TODO quoteflt.qf_nl_last is a hack that i have introduced so that we
    * TODO finally can gracefully place a newline last in the visual display.
    * TODO I.e., for cases where quoteflt shouldn't be used at all  */
   bool_t      qf_nl_last;    /* Last thing written/seen was NL */
#ifndef mx_HAVE_QUOTE_FOLD
   ui8_t       __dummy[6];
#else
   ui8_t       qf_state;      /* *quote-fold* state machine */
   bool_t      qf_brk_isws;   /* Breakpoint is at WS */
   ui32_t      qf_qfold_max;  /* Otherwise: line lengths */
   ui32_t      qf_qfold_maxnws;
   ui32_t      qf_wscnt;      /* Whitespace count */
   char const *qf_quote_chars; /* *quote-chars* */
   ui32_t      qf_brkl;       /* Breakpoint */
   ui32_t      qf_brkw;       /* Visual width, breakpoint */
   ui32_t      qf_datw;       /* Current visual output line width */
   ui8_t       __dummy2[4];
   struct str  qf_dat;        /* Current visual output line */
   struct str  qf_currq;      /* Current quote, compressed */
   mbstate_t   qf_mbps[2];
#endif
};

FL struct quoteflt * quoteflt_dummy(void); /* TODO LEGACY */
FL void        quoteflt_init(struct quoteflt *self, char const *prefix,
                  bool_t bypass);
FL void        quoteflt_destroy(struct quoteflt *self);
FL void        quoteflt_reset(struct quoteflt *self, FILE *f);
FL ssize_t     quoteflt_push(struct quoteflt *self, char const *dat,
                  size_t len);
FL ssize_t     quoteflt_flush(struct quoteflt *self);

#endif /* mx_FILTER_QUOTE_H */
/* s-it-mode */
