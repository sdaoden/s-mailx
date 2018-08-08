/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ UserInterface: string related operations.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause TODO ISC
 */
/*
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
#undef n_FILE
#define n_FILE ui_str

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <ctype.h>

FL bool_t
n_visual_info(struct n_visual_info_ctx *vicp, enum n_visual_info_flags vif){
#ifdef HAVE_C90AMEND1
   mbstate_t *mbp;
#endif
   size_t il;
   char const *ib;
   bool_t rv;
   NYD2_ENTER;

   assert(vicp != NULL);
   assert(vicp->vic_inlen == 0 || vicp->vic_indat != NULL);
   assert(!(vif & n__VISUAL_INFO_FLAGS) || !(vif & n_VISUAL_INFO_ONE_CHAR));

   rv = TRU1;
   ib = vicp->vic_indat;
   if((il = vicp->vic_inlen) == UIZ_MAX)
      il = vicp->vic_inlen = strlen(ib);

   if((vif & (n_VISUAL_INFO_WIDTH_QUERY | n_VISUAL_INFO_WOUT_PRINTABLE)) ==
         n_VISUAL_INFO_WOUT_PRINTABLE)
      vif |= n_VISUAL_INFO_WIDTH_QUERY;

   vicp->vic_chars_seen = vicp->vic_bytes_seen = vicp->vic_vi_width = 0;
   if(vif & n_VISUAL_INFO_WOUT_CREATE){
      if(vif & n_VISUAL_INFO_WOUT_SALLOC)
         vicp->vic_woudat =
               n_autorec_alloc(sizeof(*vicp->vic_woudat) * (il +1));
      vicp->vic_woulen = 0;
   }
#ifdef HAVE_C90AMEND1
   if((mbp = vicp->vic_mbstate) == NULL)
      mbp = &vicp->vic_mbs_def;
#endif

   if(il > 0){
      do/* while(!(vif & n_VISUAL_INFO_ONE_CHAR) && il > 0) */{
#ifdef HAVE_C90AMEND1
         size_t i = mbrtowc(&vicp->vic_waccu, ib, il, mbp);

         if(i == (size_t)-2){
            rv = FAL0;
            break;
         }else if(i == (size_t)-1){
            if(!(vif & n_VISUAL_INFO_SKIP_ERRORS)){
               rv = FAL0;
               break;
            }
            memset(mbp, 0, sizeof *mbp);
            vicp->vic_waccu = (n_psonce & n_PSO_UNICODE) ? 0xFFFD : '?';
            i = 1;
         }else if(i == 0){
            il = 0;
            break;
         }

         ++vicp->vic_chars_seen;
         vicp->vic_bytes_seen += i;
         ib += i;
         il -= i;

         if(vif & n_VISUAL_INFO_WIDTH_QUERY){
            int w;
            wchar_t wc = vicp->vic_waccu;

# ifdef HAVE_WCWIDTH
            w = (wc == '\t' ? 1 : wcwidth(wc));
# else
            if(wc == '\t' || iswprint(wc))
               w = 1 + (wc >= 0x1100u); /* S-CText isfullwidth() */
            else
               w = -1;
# endif
            if(w > 0)
               vicp->vic_vi_width += w;
            else if(vif & n_VISUAL_INFO_WOUT_PRINTABLE)
               continue;
         }
#else /* HAVE_C90AMEND1 */
         char c = *ib;

         if(c == '\0'){
            il = 0;
            break;
         }

         ++vicp->vic_chars_seen;
         ++vicp->vic_bytes_seen;
         vicp->vic_waccu = c;
         if(vif & n_VISUAL_INFO_WIDTH_QUERY)
            vicp->vic_vi_width += (c == '\t' || isprint(c)); /* XXX */

         ++ib;
         --il;
#endif

         if(vif & n_VISUAL_INFO_WOUT_CREATE)
            vicp->vic_woudat[vicp->vic_woulen++] = vicp->vic_waccu;
      }while(!(vif & n_VISUAL_INFO_ONE_CHAR) && il > 0);
   }

   if(vif & n_VISUAL_INFO_WOUT_CREATE)
      vicp->vic_woudat[vicp->vic_woulen] = L'\0';
   vicp->vic_oudat = ib;
   vicp->vic_oulen = il;
   vicp->vic_flags = vif;
   NYD2_LEAVE;
   return rv;
}

FL size_t
field_detect_clip(size_t maxlen, char const *buf, size_t blen)/*TODO mbrtowc()*/
{
   size_t rv;
   NYD_ENTER;

#ifdef HAVE_NATCH_CHAR
   maxlen = n_MIN(maxlen, blen);
   for (rv = 0; maxlen > 0;) {
      int ml = mblen(buf, maxlen);
      if (ml <= 0) {
         mblen(NULL, 0);
         break;
      }
      buf += ml;
      rv += ml;
      maxlen -= ml;
   }
#else
   rv = n_MIN(blen, maxlen);
#endif
   NYD_LEAVE;
   return rv;
}

FL char *
colalign(char const *cp, int col, int fill, int *cols_decr_used_or_null)
{
   n_NATCH_CHAR( struct bidi_info bi; )
   int col_orig = col, n, sz;
   bool_t isbidi, isuni, istab, isrepl;
   char *nb, *np;
   NYD_ENTER;

   /* Bidi only on request and when there is 8-bit data */
   isbidi = isuni = FAL0;
#ifdef HAVE_NATCH_CHAR
   isuni = ((n_psonce & n_PSO_UNICODE) != 0);
   bidi_info_create(&bi);
   if (bi.bi_start.l == 0)
      goto jnobidi;
   if (!(isbidi = bidi_info_needed(cp, strlen(cp))))
      goto jnobidi;

   if ((size_t)col >= bi.bi_pad)
      col -= bi.bi_pad;
   else
      col = 0;
jnobidi:
#endif

   np = nb = n_autorec_alloc(n_mb_cur_max * strlen(cp) +
         ((fill ? col : 0)
         n_NATCH_CHAR( + (isbidi ? bi.bi_start.l + bi.bi_end.l : 0) )
         +1));

#ifdef HAVE_NATCH_CHAR
   if (isbidi) {
      memcpy(np, bi.bi_start.s, bi.bi_start.l);
      np += bi.bi_start.l;
   }
#endif

   while (*cp != '\0') {
      istab = FAL0;
#ifdef HAVE_C90AMEND1
      if (n_mb_cur_max > 1) {
         wchar_t  wc;

         n = 1;
         isrepl = TRU1;
         if ((sz = mbtowc(&wc, cp, n_mb_cur_max)) == -1)
            sz = 1;
         else if (wc == L'\t') {
            cp += sz - 1; /* Silly, no such charset known (.. until S-Ctext) */
            isrepl = FAL0;
            istab = TRU1;
         } else if (iswprint(wc)) {
# ifndef HAVE_WCWIDTH
            n = 1 + (wc >= 0x1100u); /* TODO use S-CText isfullwidth() */
# else
            if ((n = wcwidth(wc)) == -1)
               n = 1;
            else
# endif
               isrepl = FAL0;
         }
      } else
#endif
      {
         n = sz = 1;
         istab = (*cp == '\t');
         isrepl = !(istab || isprint((uc_i)*cp));
      }

      if (n > col)
         break;
      col -= n;

      if (isrepl) {
         if (isuni) {
            /* Contained in n_mb_cur_max, then */
            memcpy(np, n_unirepl, sizeof(n_unirepl) -1);
            np += sizeof(n_unirepl) -1;
         } else
            *np++ = '?';
         cp += sz;
      } else if (istab || (sz == 1 && spacechar(*cp))) {
         *np++ = ' ';
         ++cp;
      } else
         while (sz--)
            *np++ = *cp++;
   }

   if (fill && col != 0) {
      if (fill > 0) {
         memmove(nb + col, nb, PTR2SIZE(np - nb));
         memset(nb, ' ', col);
      } else
         memset(np, ' ', col);
      np += col;
      col = 0;
   }

#ifdef HAVE_NATCH_CHAR
   if (isbidi) {
      memcpy(np, bi.bi_end.s, bi.bi_end.l);
      np += bi.bi_end.l;
   }
#endif

   *np = '\0';
   if (cols_decr_used_or_null != NULL)
      *cols_decr_used_or_null -= col_orig - col;
   NYD_LEAVE;
   return nb;
}

FL void
makeprint(struct str const *in, struct str *out) /* TODO <-> TTYCHARSET!! */
{
   /* TODO: makeprint() should honour *ttycharset*.  This of course does not
    * TODO work with ISO C / POSIX since mbrtowc() do know about locales, not
    * TODO charsets, and ditto iswprint() etc. do work with the locale too.
    * TODO I hope S-CText can do something about that, and/or otherwise add
    * TODO some special treatment for UTF-8 (take it from S-CText too then) */
   char const *inp, *maxp;
   char *outp;
   DBG( size_t msz; )
   NYD_ENTER;

   out->s =
   outp = n_alloc(DBG( msz = ) in->l*n_mb_cur_max + 2u*n_mb_cur_max +1);
   inp = in->s;
   maxp = inp + in->l;

#ifdef HAVE_NATCH_CHAR
   if (n_mb_cur_max > 1) {
      char mbb[MB_LEN_MAX + 1];
      wchar_t wc;
      int i, n;
      bool_t isuni = ((n_psonce & n_PSO_UNICODE) != 0);

      out->l = 0;
      while (inp < maxp) {
         if (*inp & 0200)
            n = mbtowc(&wc, inp, PTR2SIZE(maxp - inp));
         else {
            wc = *inp;
            n = 1;
         }
         if (n == -1) {
            /* FIXME Why mbtowc() resetting here?
             * FIXME what about ISO 2022-JP plus -- those
             * FIXME will loose shifts, then!
             * FIXME THUS - we'd need special "known points"
             * FIXME to do so - say, after a newline!!
             * FIXME WE NEED TO CHANGE ALL USES +MBLEN! */
            mbtowc(&wc, NULL, n_mb_cur_max);
            wc = isuni ? 0xFFFD : '?';
            n = 1;
         } else if (n == 0)
            n = 1;
         inp += n;
         if (!iswprint(wc) && wc != '\n' /*&& wc != '\r' && wc != '\b'*/ &&
               wc != '\t') {
            if ((wc & ~(wchar_t)037) == 0)
               wc = isuni ? 0x2400 | wc : '?';
            else if (wc == 0177)
               wc = isuni ? 0x2421 : '?';
            else
               wc = isuni ? 0x2426 : '?';
         }else if(isuni){ /* TODO ctext */
            /* Need to filter out L-TO-R and R-TO-R marks TODO ctext */
            if(wc == 0x200E || wc == 0x200F || (wc >= 0x202A && wc <= 0x202E))
               continue;
            /* And some zero-width messes */
            if(wc == 0x00AD || (wc >= 0x200B && wc <= 0x200D))
               continue;
            /* Oh about the ISO C wide character interfaces, baby! */
            if(wc == 0xFEFF)
               continue;
         }
         if ((n = wctomb(mbb, wc)) <= 0)
            continue;
         out->l += n;
         assert(out->l < msz);
         for (i = 0; i < n; ++i)
            *outp++ = mbb[i];
      }
   } else
#endif /* NATCH_CHAR */
   {
      int c;
      while (inp < maxp) {
         c = *inp++ & 0377;
         if (!isprint(c) && c != '\n' && c != '\r' && c != '\b' && c != '\t')
            c = '?';
         *outp++ = c;
      }
      out->l = in->l;
   }
   out->s[out->l] = '\0';
   NYD_LEAVE;
}

FL size_t
delctrl(char *cp, size_t len)
{
   size_t x, y;
   NYD_ENTER;

   for (x = y = 0; x < len; ++x)
      if (!cntrlchar(cp[x]))
         cp[y++] = cp[x];
   cp[y] = '\0';
   NYD_LEAVE;
   return y;
}

FL char *
prstr(char const *s)
{
   struct str in, out;
   char *rp;
   NYD_ENTER;

   in.s = n_UNCONST(s);
   in.l = strlen(s);
   makeprint(&in, &out);
   rp = savestrbuf(out.s, out.l);
   n_free(out.s);
   NYD_LEAVE;
   return rp;
}

FL int
prout(char const *s, size_t sz, FILE *fp)
{
   struct str in, out;
   int n;
   NYD_ENTER;

   in.s = n_UNCONST(s);
   in.l = sz;
   makeprint(&in, &out);
   n = fwrite(out.s, 1, out.l, fp);
   n_free(out.s);
   NYD_LEAVE;
   return n;
}

FL bool_t
bidi_info_needed(char const *bdat, size_t blen)
{
   bool_t rv = FAL0;
   NYD_ENTER;

#ifdef HAVE_NATCH_CHAR
   if (n_psonce & n_PSO_UNICODE)
      while (blen > 0) {
         /* TODO Checking for BIDI character: use S-CText fromutf8
          * TODO plus isrighttoleft (or whatever there will be)! */
         ui32_t c = n_utf8_to_utf32(&bdat, &blen);
         if (c == UI32_MAX)
            break;

         if (c <= 0x05BE)
            continue;

         /* (Very very fuzzy, awaiting S-CText for good) */
         if ((c >= 0x05BE && c <= 0x08E3) ||
               (c >= 0xFB1D && c <= 0xFE00) /* No: variation selectors */ ||
               (c >= 0xFE70 && c <= 0xFEFC) ||
               (c >= 0x10800 && c <= 0x10C48) ||
               (c >= 0x1EE00 && c <= 0x1EEF1)) {
            rv = TRU1;
            break;
         }
      }
#endif /* HAVE_NATCH_CHAR */
   NYD_LEAVE;
   return rv;
}

FL void
bidi_info_create(struct bidi_info *bip)
{
   /* Unicode: how to isolate RIGHT-TO-LEFT scripts via *headline-bidi*
    * 1.1 (Jun 1993): U+200E (E2 80 8E) LEFT-TO-RIGHT MARK
    * 6.3 (Sep 2013): U+2068 (E2 81 A8) FIRST STRONG ISOLATE,
    *                 U+2069 (E2 81 A9) POP DIRECTIONAL ISOLATE
    * Worse results seen for: U+202D "\xE2\x80\xAD" U+202C "\xE2\x80\xAC" */
   n_NATCH_CHAR( char const *hb; )
   NYD_ENTER;

   memset(bip, 0, sizeof *bip);
   bip->bi_start.s = bip->bi_end.s = n_UNCONST(n_empty);

#ifdef HAVE_NATCH_CHAR
   if ((n_psonce & n_PSO_UNICODE) && (hb = ok_vlook(headline_bidi)) != NULL) {
      switch (*hb) {
      case '3':
         bip->bi_pad = 2;
         /* FALLTHRU */
      case '2':
         bip->bi_start.s = bip->bi_end.s = n_UNCONST("\xE2\x80\x8E");
         break;
      case '1':
         bip->bi_pad = 2;
         /* FALLTHRU */
      default:
         bip->bi_start.s = n_UNCONST("\xE2\x81\xA8");
         bip->bi_end.s = n_UNCONST("\xE2\x81\xA9");
         break;
      }
      bip->bi_start.l = bip->bi_end.l = 3;
   }
#endif
   NYD_LEAVE;
}

/* s-it-mode */
