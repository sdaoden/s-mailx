/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Filter objects.
 *
 * Copyright (c) 2013 - 2014 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/*
 * Quotation filter
 */

/*
 * TODO quotation filter: anticipate in future data: don't break if only WS
 * TODO or a LF escaping \ follows on the line (simply reuse the latter).
 */

#ifdef HAVE_QUOTE_FOLD
CTA(QUOTE_MAX > 3);

enum qf_state {
   _QF_CLEAN,
   _QF_PREFIX,
   _QF_DATA
};

struct qf_vc {
   struct quoteflt *self;
   char const *buf;
   size_t len;
};

/* Print out prefix and current quote */
static ssize_t _qf_dump_prefix(struct quoteflt *self);

/* Add one data character */
static ssize_t _qf_add_data(struct quoteflt *self, wchar_t wc);

/* State machine handlers */
static ssize_t _qf_state_prefix(struct qf_vc *vc);
static ssize_t _qf_state_data(struct qf_vc *vc);

static ssize_t
_qf_dump_prefix(struct quoteflt *self)
{
   ssize_t rv;
   size_t i;

   if ((i = self->qf_pfix_len) > 0 && i != fwrite(self->qf_pfix, 1, i,
         self->qf_os))
      goto jerr;
   rv = i;

   if ((i = self->qf_currq.l) > 0 && i != fwrite(self->qf_currq.s, 1, i,
         self->qf_os))
      goto jerr;
   rv += i;
jleave:
   return rv;
jerr:
   rv = -1;
   goto jleave;
}

static ssize_t
_qf_add_data(struct quoteflt *self, wchar_t wc)
{
   char *save_b;
   ui_it save_l, save_w;
   ssize_t rv = 0;
   int w, l;

   save_l = save_w = 0; /* silence cc */
   save_b = NULL;
   /* <newline> ends state */
   if (wc == L'\n')
      goto jflush;
   if (wc == L'\r') /* TODO CR should be stripped in lower level!! */
      goto jleave;

   /* Unroll <tab> to spaces */
   if (wc == L'\t') {
      save_l = self->qf_datw;
      save_w = (save_l + QUOTE_TAB_SPACES) & ~(QUOTE_TAB_SPACES - 1);
      save_w -= save_l;
      while (save_w-- > 0) {
         ssize_t j = _qf_add_data(self, L' ');
         if (j < 0) {
            rv = j;
            break;
         }
         rv += j;
      }
      goto jleave;
   }

   w = wcwidth(wc);
   if (w == -1) {
jbad:
      ++self->qf_datw;
      self->qf_dat.s[self->qf_dat.l++] = '?';
   } else {
      l = wctomb(self->qf_dat.s + self->qf_dat.l, wc);
      if (l < 0)
         goto jbad;
      self->qf_datw += (ui_it)w;
      self->qf_dat.l += (size_t)l;
   }

   /* TODO The last visual may excess *qfold-max* if it's a wide one;
    * TODO place it on the next line, break before */
   if (self->qf_datw >= self->qf_qfold_max) {
      /* If we have seen a nice breakpoint during traversal, shuffle data
       * around a bit so as to restore the trailing part after flushing */
      if (self->qf_brkl > 0) {
         save_w = self->qf_datw - self->qf_brkw;
         save_l = self->qf_dat.l - self->qf_brkl;
         save_b = self->qf_dat.s + self->qf_brkl + 2;
         memmove(save_b, save_b - 2, save_l);
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
         memmove(self->qf_dat.s, save_b, save_l);
      }

   } else if (self->qf_datw >= self->qf_qfold_min && ! self->qf_brk_isws) {
      bool_t isws = iswspace(wc);

      if ((isws && ! self->qf_brk_isws) || self->qf_brkl == 0) {
         self->qf_brkl = self->qf_dat.l;
         self->qf_brkw = self->qf_datw;
         self->qf_brk_isws = isws;
      }
   }

   /* If state changed to prefix, perform full reset (note this implies that
    * quoteflt_flush() performs too much work..) */
   if (wc == '\n') {
      self->qf_state = _QF_PREFIX;
      self->qf_wscnt = self->qf_datw = 0;
      self->qf_currq.l = 0;
   }
jleave:
   return rv;
}

static ssize_t
_qf_state_prefix(struct qf_vc *vc)
{
   struct quoteflt *self = vc->self;
   ssize_t rv = 0;
   char const *buf;
   size_t len, i;
   wchar_t wc;

   for (buf = vc->buf, len = vc->len; len > 0;) {
      /* TODO NULL BYTE! */
      i = mbrtowc(&wc, buf, len, self->qf_mbps);
      if (i == (size_t)-1) {
         /* On hard error, don't modify mbstate_t and step one byte */
         self->qf_mbps[0] = self->qf_mbps[1];
         ++buf;
         --len;
         self->qf_wscnt = 0;
         continue;
      }
      self->qf_mbps[1] = self->qf_mbps[0];
      if (i == (size_t)-2) {
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
      if (i == 1 && ISQUOTE(wc)) {
         self->qf_wscnt = 0;
         if (self->qf_currq.l >= QUOTE_MAX - 3) {
            self->qf_currq.s[QUOTE_MAX - 3] = '.';
            self->qf_currq.s[QUOTE_MAX - 2] = '.';
            self->qf_currq.s[QUOTE_MAX - 1] = '.';
            self->qf_currq.l = QUOTE_MAX;
         } else
            self->qf_currq.s[self->qf_currq.l++] = buf[-1];
         continue;
      }

      /* The quote is parsed and compressed; dump it */
jfin:
      self->qf_datw = self->qf_pfix_len + self->qf_currq.l;
      self->qf_state = _QF_DATA;
      /* Overtake WS (xxx but we de-facto "normalize" to ASCII SP here) */
      while (self->qf_wscnt-- > 0 && self->qf_currq.l < QUOTE_MAX)
         self->qf_currq.s[self->qf_currq.l++] = ' ';
      self->qf_wscnt = 0;
      rv = _qf_add_data(self, wc);
      break;
   }

   vc->buf = buf;
   vc->len = len;
   return rv;
}

static ssize_t
_qf_state_data(struct qf_vc *vc)
{
   struct quoteflt *self = vc->self;
   ssize_t rv = 0;
   char const *buf;
   size_t len, i;
   wchar_t wc;

   for (buf = vc->buf, len = vc->len; len > 0;) {
      /* TODO NULL BYTE! */
      i = mbrtowc(&wc, buf, len, self->qf_mbps);
      if (i == (size_t)-1) {
         /* On hard error, don't modify mbstate_t and step one byte */
         self->qf_mbps[0] = self->qf_mbps[1];
         ++buf;
         --len;
         continue;
      }
      self->qf_mbps[1] = self->qf_mbps[0];
      if (i == (size_t)-2) {
         /* Redundant shift sequence, out of buffer */
         len = 0;
         break;
      }
      buf += i;
      len -= i;

      {  ssize_t j = _qf_add_data(self, wc);
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
   return rv;
}
#endif /* HAVE_QUOTE_FOLD */

FL struct quoteflt *
quoteflt_dummy(void) /* TODO LEGACY */
{
   static struct quoteflt qf_i;

   return &qf_i;
}

FL void
quoteflt_init(struct quoteflt *self, char const *prefix)
{
#ifdef HAVE_QUOTE_FOLD
   char *xcp, *cp;
#endif

   memset(self, 0, sizeof *self);

   if ((self->qf_pfix = prefix) != NULL)
      self->qf_pfix_len = (ui_it)strlen(prefix);

   /* Check wether the user wants the more fancy quoting algorithm */
   /* TODO *quote-fold*: QUOTE_MAX may excess it! */
#ifdef HAVE_QUOTE_FOLD
   if (self->qf_pfix_len > 0 && (cp = ok_vlook(quote_fold)) != NULL) {
      ui_it qmin, qmax = (ui_it)strtol(cp, (char**)&xcp, 10);
      /* These magic values ensure we don't bail :) */
      if (qmax < self->qf_pfix_len + 6)
         qmax = self->qf_pfix_len + 6;
      --qmax; /* The newline escape */
      if (cp == xcp || *xcp == '\0')
         qmin = (qmax >> 1) + (qmax >> 2) + (qmax >> 5);
      else {
         qmin = (ui_it)strtol(xcp + 1, NULL, 10);
         if (qmin < qmax >> 1)
            qmin = qmax >> 1;
         else if (qmin > qmax - 2)
            qmin = qmax - 2;
      }
      self->qf_qfold_min = qmin;
      self->qf_qfold_max = qmax;

      /* Add pad for takeover copies, backslash and newline */
      self->qf_dat.s = salloc((qmax + 3) * mb_cur_max);
      self->qf_currq.s = salloc((QUOTE_MAX + 1) * mb_cur_max);
   }
#endif
}

FL void
quoteflt_destroy(struct quoteflt *self) /* xxx inline */
{
   (void)self;
}

FL void
quoteflt_reset(struct quoteflt *self, FILE *f) /* xxx inline */
{
   self->qf_os = f;
#ifdef HAVE_QUOTE_FOLD
   self->qf_state = _QF_CLEAN;
   self->qf_dat.l =
   self->qf_currq.l = 0;
   memset(self->qf_mbps, 0, sizeof self->qf_mbps);
#endif
}

FL ssize_t
quoteflt_push(struct quoteflt *self, char const *dat, size_t len)
{
   /* (xxx Ideally the actual push() [and flush()] would be functions on their
    * xxx own, via indirect vtbl call ..) */
   ssize_t rv = 0;

   if (len == 0)
      goto jleave;

   /* Bypass? XXX Finally, this filter simply should not be used, then */
   if (self->qf_pfix_len == 0) {
      if (len != fwrite(dat, 1, len, self->qf_os))
         goto jerr;
      rv = len;
   }
   /* Normal: place *indentprefix* at every BOL */
   else
#ifdef HAVE_QUOTE_FOLD
      if (self->qf_qfold_max == 0)
#endif
   {
      void *vp;
      size_t ll;
      bool_t pxok = (self->qf_qfold_min != 0);

      for (;;) {
         if (!pxok) {
            ll = self->qf_pfix_len;
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
         if ((vp = memchr(dat, '\n', len)) == NULL)
            ll = len;
         else {
            pxok = FAL0;
            ll = (size_t)((char*)vp - dat) + 1;
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
#ifdef HAVE_QUOTE_FOLD
   else {
      struct qf_vc vc;
      ssize_t i;

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
#endif /* HAVE_QUOTE_FOLD */

jleave:
   return rv;

jerr:
   rv = -1;
   goto jleave;
}

FL ssize_t
quoteflt_flush(struct quoteflt *self)
{
   ssize_t rv = 0;
   UNUSED(self);

#ifdef HAVE_QUOTE_FOLD
   if (self->qf_dat.l > 0) {
      rv = _qf_dump_prefix(self);
      if (rv >= 0) {
         size_t i = self->qf_dat.l;
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
   return rv;
}

/* vim:set fenc=utf-8:s-it-mode */
