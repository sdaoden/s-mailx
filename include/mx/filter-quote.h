/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ struct quoteflt: quotation (sub) filter.
 *
 * Copyright (c) 2012/3 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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

#if defined mx_HAVE_FILTER_QUOTE_FOLD && defined mx_HAVE_C90AMEND1
# include <wchar.h>
#endif

#define mx_HEADER
#include <su/code-in.h>

struct quoteflt;

struct quoteflt{
   FILE *qf_os; /* Output stream */
   char const *qf_pfix;
   u32 qf_pfix_len; /* Length of prefix: 0: bypass */
   u32 qf_qfold_min; /* Simple way: wrote prefix? */
   boole qf_bypass; /* Simply write to .qf_os TODO BYPASS, then! */
   /* TODO quoteflt.qf_nl_last is a hack that i have introduced so that we
    * TODO finally can gracefully place a newline last in the visual display.
    * TODO I.e., for cases where quoteflt shouldn't be used at all  */
   boole qf_nl_last; /* Last thing written/seen was NL */
#ifndef mx_HAVE_FILTER_QUOTE_FOLD
   u8 qf__dummy[6];
#else
   u8 qf_state; /* *quote-fold* state machine */
   boole qf_brk_isws; /* Breakpoint is at WS */
   u32 qf_qfold_max; /* Otherwise: line lengths */
   u32 qf_qfold_maxnws;
   u32 qf_wscnt; /* Whitespace count */
   char const *qf_quote_chars; /* *quote-chars* */
   u32 qf_brkl; /* Breakpoint */
   u32 qf_brkw; /* Visual width, breakpoint */
   u32 qf_datw; /* Current visual output line width */
   u8 qf__dummy2[4];
   struct str qf_dat; /* Current visual output line */
   struct str qf_currq; /* Current quote, compressed */
   mbstate_t qf_mbps[2];
#endif
};

EXPORT struct quoteflt *quoteflt_dummy(void); /* TODO LEGACY */
EXPORT void quoteflt_init(struct quoteflt *self, char const *prefix,
      boole bypass);
EXPORT void quoteflt_destroy(struct quoteflt *self);
EXPORT void quoteflt_reset(struct quoteflt *self, FILE *f);
EXPORT sz quoteflt_push(struct quoteflt *self, char const *dat, uz len);
EXPORT sz quoteflt_flush(struct quoteflt *self);

#include <su/code-ou.h>
#endif /* mx_FILTER_QUOTE_H */
/* s-it-mode */
