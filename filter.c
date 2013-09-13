/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Filter objects.
 *
 * Copyright (c) 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
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

#include "rcv.h"

#include "extern.h"

/*
 * Quotation filter
 */

#undef HAVE_QUOTE_FOLD

#ifdef HAVE_QUOTE_FOLD
CTASSERT(QUOTE_MAX > 3);

enum qf_state {
   _QF_NONE,   /* Clean line, nothing read yet */
   _QF_PREFIX, /* Still collecting prefix */
   _QF_DATA    /* Prefix passed */
};

struct qf_vc {
   struct quoteflt * self;
   char const *      buf;
   size_t            len;
};

static ssize_t _qf_state_prefix(struct qf_vc *vc);
static ssize_t _qf_state_data(struct qf_vc *vc);

#if 0
static size_t
prefixwrite(char const *ptr, size_t size, FILE *f,
   char const *prefix, size_t prefixlen)
{
   p = ptr;
   maxp = p + size;

   } else {
      /* After writing a real newline followed by our prefix,
       * compress the quoted prefixes;
       * note that \n is only matched by spacechar(), not by
       * blankchar() or blankspacechar() */
      /*
       * TODO the problem we have is again the odd 4:3 relation of
       * TODO base64 -- if we quote a mail that is in base64 then
       * TODO prefixwrite() doesn't get invoked with partial multi-
       * TODO byte characters (S-nail uses the `rest' mechanism to
       * TODO avoid that), but it may of course be invoked with a
       * TODO partial line, and even with multiple thereof.
       * TODO this has to be addressed in 15.0 with the MIME and send
       * TODO layer rewrite.  The solution is that `prefixwrite' has
       * TODO to be an object with state -- if a part is to be quoted
       * TODO you create it, and simply feed in data; once the part
       * TODO is done, you'll release it;  the object itself gobbles
       * TODO data unless a *hard* newline is seen.
       * TODO in fact we can then even implement paragraph-wise
       * TODO quoting! FIXME in fact that is the way: objects
       * TODO then, evaluate quote-fold when sending starts, ONCE!
       * FIXME NOTE: base64 (yet TODO for qp) may have CRLF line
       * FIXME endings, these need to be removed in a LOWER LAYER!!
       */
fprintf(stderr, "ENTRY: lastlen=%zu, size=%zu <%.*s>\n",lastlen,size,(int)size,p);
      if ((lnlen = lastlen) != 0)
         goto jcontb64;
jhardnl:
      lastlen = prefixlen;
      for (zipl = i = 0; p + i < maxp; ++i) {
         c = p[i];
         if (blankspacechar(c))
            continue;
         if (! ISQUOTE(c))
            break;
         if (zipl == sizeof(zipb) - 1) {
            zipb[sizeof(zipb) - 2] = '.';
            zipb[sizeof(zipb) - 3] = '.';
            zipb[sizeof(zipb) - 4] = '.';
            continue;
         }
         zipb[zipl++] = c;
      }
      zipb[zipl] = '\0';
      p += i;
jsoftnl:
      if (zipl > 0) {
         wsz += fwrite(zipb, sizeof *zipb, zipl, f);
         lnlen += zipl;
         lastlen = lnlen;
      }
jcontb64:
      /* Search forward until either *quote-fold* or NL.
       * In the former case try to break at whitespace,
       * but only if's located in the 2nd half of the data */
      for (c = i = 0; p + i < maxp;) {
         c = p[i++];
         if (c == '\n')
            break;
         if (lnlen + i <= qfold_max)
            continue;

         /* We're excessing bounds -- but don't "continue"
          * trailing WS nor a continuation */
         if (c == '\\' || spacechar(c)) {
            char const *cp;

            for (cp = p + i; cp < maxp; ++cp)
               if (! spacechar(*cp))
                  break;
            if (cp == maxp || (*cp == '\\' &&
                  cp[1] == '\n')) {
               i = (size_t)(maxp - p);
               c = 0;
               break;
            }
         } else if (p + i < maxp && p[i] == '\n') {
            ++i;
            c = 0;
            break;
         }

         /* We have to fold this line */
         if (qfold_min < lnlen) {
            /* This is because of base64 and odd 4:3.
             * i.e., entered with some partial line yet
             * written..  This is weird, as we may have
             * written out `qfold_max' already.. */
            j = 0;
            size = --i;
fprintf(stderr, "-- size=%zu p<%.*s>\n",size,(int)size,p);
         } else {
            j = qfold_min - lnlen;
            i =
            size = j + ((qfold_max - qfold_min) >> 1);
            assert(p + i < maxp);
         }
         while (i > j && ! spacechar(p[i - 1]))
            --i;
         if (i == j)
            i = size;
         c = 0;
         break;
      }

      if (i > 0) {
         wsz += fwrite(p, sizeof *p, i, f);
         p += i;
         lastlen += i;
      }

      if (p < maxp) {
         if (c != '\n') {
            putc('\\', f);
            putc('\n', f);
            wsz += 2;
         }
         wsz += fwrite(prefix, sizeof *prefix, prefixlen, f);
         lnlen = prefixlen;
         if (c != '\n')
            goto jsoftnl;
         goto jhardnl;
      }
   }

   lastc = p[-1];
   return wsz;
}
#endif


static ssize_t
_qf_state_prefix(struct qf_vc *vc)
{
   struct quoteflt *self = vc->self;
   char const *buf = vc->buf;
   size_t len = vc->len, i;
   ssize_t rv = 0;
   wchar_t wc;



   for (;;) {
      if (len < mb_cur_max) {
         len = vc->len - len;
         memcpy(self->dat.s + self->dat.l, vc->buf, len);
         self->dat.l += len;
         break;
      }

      i = mbrtowc(&wc, buf, len, &self->mbps);
      if (i == (size_t)-1 || i == (size_t)-2)
         ;

      buf += i;
      len -= i;

      if (iswspace(wc))
         continue;

      if (i == 1 && ISQUOTE(wc)) {
         if (self->currq.l == QUOTE_MAX - 3) {
            self->currq.s[QUOTE_MAX - 3] = '.';
            self->currq.s[QUOTE_MAX - 2] = '.';
            self->currq.s[QUOTE_MAX - 1] = '.';
         } else
            self->currq.s[self->currq.l++] = buf[-1];
         continue;
      }

      self->qf_state = _QF_DATA;
      break;


   }


jleave:

   return rv;
}

static ssize_t
_qf_state_data(struct qf_vc *vc)
{




   return ;
}
#endif /* HAVE_QUOTE_FOLD */

struct quoteflt *
quoteflt_dummy(void) /* TODO LEGACY */
{
   static struct quoteflt qf_i;

   return &qf_i;
}

void
quoteflt_init(struct quoteflt *self, char const *prefix)
{
#ifdef HAVE_QUOTE_FOLD
   char *xcp, *cp;
#endif

   memset(self, 0, sizeof *self);

   if ((self->qf_pfix = prefix) != NULL)
      self->qf_pfix_len = (ui_it)strlen(prefix);

   /* Check wether the user wants the more fancy quoting algorithm */
#ifdef HAVE_QUOTE_FOLD
   if ((cp = voption("quote-fold")) != NULL) {
      ui_it qmin, qmax = (ui_it)strtol(cp, (char**)&xcp, 10);
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

      self->qf_dat.s = salloc((qmax + 2) * mb_cur_len);
      self->qf_currq.s = salloc((QUOTE_MAX + 1) * mb_cur_len);
   }
#endif
}

void
quoteflt_destroy(struct quoteflt *self)
{
#ifndef HAVE_QUOTE_FOLD
   (void)self;
#else
   void *p;

   if ((p = self->qf_dat.s) != NULL)
      free(p);
   if ((p = self->qf_currq.s) != NULL)
      free(p);
#endif
}

void
quoteflt_reset(struct quoteflt *self, FILE *f) /* XXX inline */
{
   self->qf_os = f;
#ifdef HAVE_QUOTE_FOLD
   self->qf_state = _QF_NONE;
   self->qf_dat.l =
   self->qf_currq.l = 0;
   memset(self->qf_mbps, 0, sizeof self->qf_mbps);
#endif
}

ssize_t
quoteflt_push(struct quoteflt *self, char const *dat, size_t len)
{
   /* (xxx Ideally the actual push() [and flush()] would be functions on their
    * xxx own, via indirect vtbl call ..) */
   ssize_t i, rv = 0;

   if (len == 0)
      goto jleave;

   /* Simple bypass? XXX Finally, this filter simply should not be used, then */
   if (self->qf_pfix_len == 0)
      rv = fwrite(dat, sizeof *dat, len, self->qf_os);
   /* The simple algorithm: place *indentprefix* at every BOL */
   else
#ifdef HAVE_QUOTE_FOLD
      if (self->qf_qfold_max == 0)
#endif
   {
      void *vp;
      size_t ll;
      bool_t pxok = (self->qf_qfold_min != 0);

      for (;;) {
         if (! pxok) {
            i = fwrite(self->qf_pfix, sizeof *self->qf_pfix, self->qf_pfix_len,
                  self->qf_os);
            if (i < 0)
               goto jerr;
            rv += i;
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

         i = fwrite(dat, sizeof *dat, ll, self->qf_os);
         if (i < 0)
            goto jerr;
         rv += i;
         if ((len -= ll) == 0)
            break;
         dat += ll;
      }

      self->qf_qfold_min = pxok;
   }
   /* More complicated, though still only line-per-line: *quote-fold*.
    * - If .qf_currq.l is 0, then we are in a clean state.  Reset .qf_mbps;
    *   note this means we assume that lines start with reset escape seq
    * - Lookout for a newline
    */
#ifdef HAVE_QUOTE_FOLD
   else {
      struct qf_vc vc;

      vc.self = self;
      vc.buf = dat;
      vc.len = len;

      while (vc.len > 0) {
         switch (self->qf_state) {
         case _QF_NONE:
         case _QF_PREFIX:
            i = _qf_state_prefix(&vc);
            break;
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

ssize_t
quoteflt_flush(struct quoteflt *self)
{
   ssize_t rv = 0;
   (void)self;

#ifdef HAVE_QUOTE_FOLD
   if (self->qf_dat.l > 0) {
      rv = fwrite(self->qf_pfix, sizeof *self->qf_pfix, self->qf_pfix_len,
            self->qf_os);
      if (rv > 0) {
         ssize_t j = fwrite(self->qf_dat.s, sizeof *self->qf_dat.s,
               self->qf_dat.l, self->qf_os);
         rv = (j < 0) ? j : rv + j;
      }
      self->qf_dat.l = 0;
   }
#endif
   return rv;
}

/* vim:set fenc=utf-8:s-it-mode */
