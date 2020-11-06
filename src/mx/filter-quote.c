/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ struct quoteflt: quotation (sub) filter.
 *@ TODO quotation filter: anticipate in future data: don't break if only WS
 *@ TODO or a LF escaping \ follows on the line (simply reuse the latter).
 *
 * Copyright (c) 2012/3 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef su_FILE
#define su_FILE filter_quote
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#ifdef mx_HAVE_FILTER_QUOTE_FOLD
# ifdef mx_HAVE_C90AMEND1
#  include <wchar.h>
#  include <wctype.h>
# endif

# include <su/icodec.h>
#endif

#include "mx/filter-quote.h"
/* TODO fake */
#include "su/code-in.h"

#ifdef mx_HAVE_FILTER_QUOTE_FOLD
CTAV(n_QUOTE_MAX > 3);

enum qf_state {
   _QF_CLEAN,
   _QF_PREFIX,
   _QF_DATA
};

struct qf_vc {
   struct quoteflt   *self;
   char const        *buf;
   uz            len;
};

/* Print out prefix and current quote */
static sz _qf_dump_prefix(struct quoteflt *self);

/* Add one data character */
static sz _qf_add_data(struct quoteflt *self, wchar_t wc);

/* State machine handlers */
static sz _qf_state_prefix(struct qf_vc *vc);
static sz _qf_state_data(struct qf_vc *vc);

static sz
_qf_dump_prefix(struct quoteflt *self)
{
   sz rv;
   uz i;
   NYD_IN;

   if ((i = self->qf_pfix_len) > 0 && i != fwrite(self->qf_pfix, 1, i,
         self->qf_os))
      goto jerr;
   rv = i;

   if ((i = self->qf_currq.l) > 0 && i != fwrite(self->qf_currq.s, 1, i,
         self->qf_os))
      goto jerr;
   rv += i;
jleave:
   NYD_OU;
   return rv;
jerr:
   rv = -1;
   goto jleave;
}

static sz
_qf_add_data(struct quoteflt *self, wchar_t wc)
{
   int w, l;
   char *save_b;
   u32 save_l, save_w;
   sz rv;
   NYD_IN;

   rv = 0;
   save_l = save_w = 0; /* silence cc */
   save_b = NULL;

   /* <newline> ends state */
   if (wc == L'\n') {
      w = 0;
      goto jflush;
   }
   if (wc == L'\r') /* TODO CR should be stripped in lower level!! */
      goto jleave;

   /* Unroll <tab> to spaces */
   if (wc == L'\t') {
      save_l = self->qf_datw;
      save_w = (save_l + n_QUOTE_TAB_SPACES) & ~(n_QUOTE_TAB_SPACES - 1);
      save_w -= save_l;
      while (save_w-- > 0) {
         sz j = _qf_add_data(self, L' ');
         if (j < 0) {
            rv = j;
            break;
         }
         rv += j;
      }
      goto jleave;
   }

   /* To avoid that the last visual excesses *qfold-max*, which may happen for
    * multi-column characters, use w as an indicator for this and move that
    * thing to the next line */
   w = wcwidth(wc);
   if (w == -1) {
      w = 0;
jbad:
      ++self->qf_datw;
      self->qf_dat.s[self->qf_dat.l++] = '?';
   } else if (self->qf_datw > self->qf_qfold_max - w) {
      w = -1;
      goto jneednl;
   } else {
      l = wctomb(self->qf_dat.s + self->qf_dat.l, wc);
      if (l < 0)
         goto jbad;
      self->qf_datw += (u32)w;
      self->qf_dat.l += (uz)l;
   }

   if (self->qf_datw >= self->qf_qfold_max) {
      /* If we have seen a nice breakpoint during traversal, shuffle data
       * around a bit so as to restore the trailing part after flushing */
jneednl:
      if (self->qf_brkl > 0) {
         save_w = self->qf_datw - self->qf_brkw;
         save_l = self->qf_dat.l - self->qf_brkl;
         save_b = self->qf_dat.s + self->qf_brkl + 2;
         su_mem_move(save_b, save_b - 2, save_l);
         self->qf_dat.l = self->qf_brkl;
      }

      self->qf_dat.s[self->qf_dat.l++] = '\\';
jflush:
      self->qf_dat.s[self->qf_dat.l++] = '\n';
      rv = quoteflt_flush(self);

      /* Restore takeovers, if any */
      if (save_b != NULL) {
         self->qf_brk_isws = FAL0;
         self->qf_datw += save_w;
         self->qf_dat.l = save_l;
         su_mem_move(self->qf_dat.s, save_b, save_l);
      }
   } else if (self->qf_datw >= self->qf_qfold_min && !self->qf_brk_isws) {
      boole isws = (iswspace(wc) != 0);

      if (isws || !self->qf_brk_isws || self->qf_brkl == 0) {
         if((self->qf_brk_isws = isws) ||
               self->qf_brkl < self->qf_qfold_maxnws){
            self->qf_brkl = self->qf_dat.l;
            self->qf_brkw = self->qf_datw;
         }
      }
   }

   /* Did we hold this back to avoid qf_fold_max excess?  Then do it now */
   if(rv >= 0 && w == -1){
      sz j = _qf_add_data(self, wc);
      if(j < 0)
         rv = j;
      else
         rv += j;
   }
   /* If state changed to prefix, perform full reset (note this implies that
    * quoteflt_flush() performs too much work..) */
   else if (wc == '\n') {
      self->qf_state = _QF_PREFIX;
      self->qf_wscnt = self->qf_datw = 0;
      self->qf_currq.l = 0;
   }
jleave:
   NYD_OU;
   return rv;
}

static sz
_qf_state_prefix(struct qf_vc *vc)
{
   struct quoteflt *self;
   sz rv;
   char const *buf;
   uz len, i;
   wchar_t wc;
   NYD_IN;

   self = vc->self;
   rv = 0;

   for (buf = vc->buf, len = vc->len; len > 0;) {
      /* xxx NULL BYTE! */
      i = mbrtowc(&wc, buf, len, self->qf_mbps);
      if (i == (uz)-1) {
         /* On hard error, don't modify mbstate_t and step one byte */
         self->qf_mbps[0] = self->qf_mbps[1];
         ++buf;
         --len;
         self->qf_wscnt = 0;
         continue;
      }
      self->qf_mbps[1] = self->qf_mbps[0];
      if (i == (uz)-2) {
         /* Redundant shift sequence, out of buffer */
         len = 0;
         break;
      }
      buf += i;
      len -= i;

      if (wc == L'\n')
         goto jfin;
      if (iswspace(wc)) {
         ++self->qf_wscnt;
         continue;
      }
      if (i == 1 && su_cs_is_ascii(wc) &&
            su_cs_find_c(self->qf_quote_chars, (char)wc) != NULL){
         self->qf_wscnt = 0;
         if (self->qf_currq.l >= n_QUOTE_MAX - 3) {
            self->qf_currq.s[n_QUOTE_MAX - 3] = '.';
            self->qf_currq.s[n_QUOTE_MAX - 2] = '.';
            self->qf_currq.s[n_QUOTE_MAX - 1] = '.';
            self->qf_currq.l = n_QUOTE_MAX;
         } else
            self->qf_currq.s[self->qf_currq.l++] = buf[-1];
         continue;
      }

      /* The quote is parsed and compressed; dump it */
jfin:
      self->qf_state = _QF_DATA;
      /* Overtake WS to the current quote in order to preserve it for eventual
       * necessary follow lines, too */
      /* TODO we de-facto "normalize" to ASCII SP here which MESSES tabs!! */
      while (self->qf_wscnt-- > 0 && self->qf_currq.l < n_QUOTE_MAX)
         self->qf_currq.s[self->qf_currq.l++] = ' ';
      self->qf_datw = self->qf_pfix_len + self->qf_currq.l;
      self->qf_wscnt = 0;
      rv = _qf_add_data(self, wc);
      break;
   }

   vc->buf = buf;
   vc->len = len;
   NYD_OU;
   return rv;
}

static sz
_qf_state_data(struct qf_vc *vc)
{
   struct quoteflt *self;
   sz rv;
   char const *buf;
   uz len, i;
   wchar_t wc;
   NYD_IN;

   self = vc->self;
   rv = 0;

   for (buf = vc->buf, len = vc->len; len > 0;) {
      /* xxx NULL BYTE! */
      i = mbrtowc(&wc, buf, len, self->qf_mbps);
      if (i == (uz)-1) {
         /* On hard error, don't modify mbstate_t and step one byte */
         self->qf_mbps[0] = self->qf_mbps[1];
         ++buf;
         --len;
         continue;
      }
      self->qf_mbps[1] = self->qf_mbps[0];
      if (i == (uz)-2) {
         /* Redundant shift sequence, out of buffer */
         len = 0;
         break;
      }
      buf += i;
      len -= i;

      {  sz j = _qf_add_data(self, wc);
         if (j < 0) {
            rv = j;
            break;
         }
         rv += j;
      }

      if (self->qf_state != _QF_DATA)
         break;
   }

   vc->buf = buf;
   vc->len = len;
   NYD_OU;
   return rv;
}
#endif /* mx_HAVE_FILTER_QUOTE_FOLD */

struct quoteflt *
quoteflt_dummy(void) /* TODO LEGACY (until filters are plugged when needed) */
{
   static struct quoteflt qf_i;

   qf_i.qf_bypass = TRU1;
   return &qf_i;
}

void
quoteflt_init(struct quoteflt *self, char const *prefix, boole bypass)
{
#ifdef mx_HAVE_FILTER_QUOTE_FOLD
   char const *xcp, *cp;
#endif
   NYD_IN;

   su_mem_set(self, 0, sizeof *self);

   if ((self->qf_pfix = prefix) != NULL)
      self->qf_pfix_len = (u32)su_cs_len(prefix);
   self->qf_bypass = bypass;

   /* Check whether the user wants the more fancy quoting algorithm */
   /* TODO *quote-fold*: n_QUOTE_MAX may excess it! */
#ifdef mx_HAVE_FILTER_QUOTE_FOLD
   if (!bypass && (cp = ok_vlook(quote_fold)) != NULL) {
      u32 qmax, qmaxnws, qmin;

      /* These magic values ensure we don't bail */
      su_idec_u32_cp(&qmax, cp, 10, &xcp);
      if (qmax < self->qf_pfix_len + 6)
         qmax = self->qf_pfix_len + 6;
      qmaxnws = --qmax; /* The newline escape */
      if (cp == xcp || *xcp == '\0')
         qmin = (qmax >> 1) + (qmax >> 2) + (qmax >> 5);
      else {
         su_idec_u32_cp(&qmin, &xcp[1], 10, &xcp);
         if (qmin < qmax >> 1)
            qmin = qmax >> 1;
         else if (qmin > qmax - 2)
            qmin = qmax - 2;

         if (cp != xcp && *xcp != '\0') {
            su_idec_u32_cp(&qmaxnws, &xcp[1], 10, &xcp);
            if (qmaxnws > qmax || qmaxnws < qmin)
               qmaxnws = qmax;
         }
      }
      self->qf_qfold_min = qmin;
      self->qf_qfold_max = qmax;
      self->qf_qfold_maxnws = qmaxnws;
      self->qf_quote_chars = ok_vlook(quote_chars);

      /* Add pad for takeover copies, reverse solidus and newline */
      self->qf_dat.s = su_AUTO_ALLOC((qmax + 3) * n_mb_cur_max);
      self->qf_currq.s = su_AUTO_ALLOC((n_QUOTE_MAX + 1) * n_mb_cur_max);
   }
#endif
   NYD_OU;
}

void
quoteflt_destroy(struct quoteflt *self) /* xxx inline */
{
   NYD_IN;
   UNUSED(self);
   NYD_OU;
}

void
quoteflt_reset(struct quoteflt *self, FILE *f) /* xxx inline */
{
   NYD_IN;
   self->qf_os = f;
#ifdef mx_HAVE_FILTER_QUOTE_FOLD
   self->qf_state = _QF_CLEAN;
   self->qf_dat.l =
   self->qf_currq.l = 0;
   su_mem_set(self->qf_mbps, 0, sizeof self->qf_mbps);
#endif
   NYD_OU;
}

sz
quoteflt_push(struct quoteflt *self, char const *dat, uz len)
{
   /* (xxx Ideally the actual push() [and flush()] would be functions on their
    * xxx own, via indirect vtbl call ..) */
   sz rv = 0;
   NYD_IN;

   self->qf_nl_last = (len > 0 && dat[len - 1] == '\n'); /* TODO HACK */

   if (len == 0)
      goto jleave;

   /* Bypass? TODO Finally, this filter simply should not be used, then
    * (TODO It supersedes prefix_write() or something) */
   if (self->qf_bypass) {
      if (len != fwrite(dat, 1, len, self->qf_os))
         goto jerr;
      rv = len;
   }
   /* Normal: place *indentprefix* at every BOL */
   else
#ifdef mx_HAVE_FILTER_QUOTE_FOLD
      if (self->qf_qfold_max == 0)
#endif
   {
      void *vp;
      uz ll;
      boole pxok = (self->qf_qfold_min != 0);

      for (;;) {
         if (!pxok && (ll = self->qf_pfix_len) > 0) {
            if (ll != fwrite(self->qf_pfix, 1, ll, self->qf_os))
               goto jerr;
            rv += ll;
            pxok = TRU1;
         }

         /* xxx Strictly speaking this is invalid, because only `/' and `.' are
          * xxx mandated by POSIX.1-2008 as "invariant across all locales
          * xxx supported"; though there is no charset known which uses this
          * xxx control char as part of a multibyte character; note that S-nail
          * XXX (and the Mail codebase as such) do not support EBCDIC */
         if ((vp = su_mem_find(dat, '\n', len)) == NULL)
            ll = len;
         else {
            pxok = FAL0;
            ll = P2UZ((char*)vp - dat) + 1;
         }

         if (ll != fwrite(dat, sizeof *dat, ll, self->qf_os))
            goto jerr;
         rv += ll;
         if ((len -= ll) == 0)
            break;
         dat += ll;
      }

      self->qf_qfold_min = pxok;
   }
   /* Overly complicated, though still only line-per-line: *quote-fold*.
    * - If .qf_currq.l is 0, then we are in a clean state.  Reset .qf_mbps;
    *   TODO note this means we assume that lines start with reset escape seq,
    *   TODO but i don't think this is any worse than what we currently do;
    *   TODO in 15.0, with the value carrier, we should carry conversion states
    *   TODO all along, only resetting on error (or at words for header =???=);
    *   TODO this still is weird for error handling, but we need to act more
    *   TODO stream-alike (though in practice i don't think cross-line states
    *   TODO can be found, because of compatibility reasons; however, being
    *   TODO a problem rather than a solution is not a good thing (tm))
    * - Lookout for a newline */
#ifdef mx_HAVE_FILTER_QUOTE_FOLD
   else {
      struct qf_vc vc;
      sz i;

      vc.self = self;
      vc.buf = dat;
      vc.len = len;
      while (vc.len > 0) {
         switch (self->qf_state) {
         case _QF_CLEAN:
         case _QF_PREFIX:
            i = _qf_state_prefix(&vc);
            break;
         default: /* silence cc (`i' unused) */
         case _QF_DATA:
            i = _qf_state_data(&vc);
            break;
         }
         if (i < 0)
            goto jerr;
         rv += i;
      }
   }
#endif /* mx_HAVE_FILTER_QUOTE_FOLD */

jleave:
   NYD_OU;
   return rv;
jerr:
   rv = -1;
   goto jleave;
}

sz
quoteflt_flush(struct quoteflt *self)
{
   sz rv = 0;
   NYD_IN;
   UNUSED(self);

#ifdef mx_HAVE_FILTER_QUOTE_FOLD
   if (self->qf_dat.l > 0) {
      rv = _qf_dump_prefix(self);
      if (rv >= 0) {
         uz i = self->qf_dat.l;
         if (i == fwrite(self->qf_dat.s, 1, i, self->qf_os))
            rv += i;
         else
            rv = -1;
         self->qf_dat.l = 0;
         self->qf_brk_isws = FAL0;
         self->qf_wscnt = self->qf_brkl = self->qf_brkw = 0;
         self->qf_datw = self->qf_pfix_len + self->qf_currq.l;
      }
   }
#endif
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
/* s-it-mode */
