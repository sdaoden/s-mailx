/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Visual strings, string classification/preparation for the user interface.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef su_FILE
#define su_FILE ui_str
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#ifdef mx_HAVE_NL_LANGINFO
# include <langinfo.h>
#endif
#ifdef mx_HAVE_SETLOCALE
# include <locale.h>
#endif
#ifdef mx_HAVE_C90AMEND1
# include <wctype.h>
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/utf.h>

#include "mx/ui-str.h"
#include "su/code-in.h"

#ifdef mx_HAVE_NATCH_CHAR
struct a_uis_bidi_info{
   struct str bi_start; /* Start of (possibly) bidirectional text */
   struct str bi_end; /* End of ... */
   uz bi_pad; /* No of visual columns to reserve for BIDI pad */
};
#endif

#ifdef mx_HAVE_NATCH_CHAR
/* Check whether bidirectional info maybe needed for blen bytes of bdat */
static boole a_uis_bidi_info_needed(char const *bdat, uz blen);

/* Create bidirectional text encapsulation info; without HAVE_NATCH_CHAR
 * the strings are always empty */
static void a_uis_bidi_info_create(struct a_uis_bidi_info *bip);
#endif

#ifdef mx_HAVE_NATCH_CHAR
static boole
a_uis_bidi_info_needed(char const *bdat, uz blen){
   boole rv;
   NYD2_IN;

   rv = FAL0;

   if(n_psonce & n_PSO_UNICODE){
      while(blen > 0){
         /* TODO Checking for BIDI character: use S-CText fromutf8
          * TODO plus isrighttoleft (or whatever there will be)! */
         u32 c = su_utf8_to_32(&bdat, &blen);
         if(c == U32_MAX)
            break;

         if(c <= 0x05BE)
            continue;

         /* (Very very fuzzy, awaiting S-CText for good) */
         if((c >= 0x05BE && c <= 0x08E3) ||
               (c >= 0xFB1D && c <= 0xFE00) /* No: variation selectors */ ||
               (c >= 0xFE70 && c <= 0xFEFC) ||
               (c >= 0x10800 && c <= 0x10C48) ||
               (c >= 0x1EE00 && c <= 0x1EEF1)){
            rv = TRU1;
            break;
         }
      }
   }

   NYD2_OU;
   return rv;
}

static void
a_uis_bidi_info_create(struct a_uis_bidi_info *bip){
   /* Unicode: how to isolate RIGHT-TO-LEFT scripts via *headline-bidi*
    * 1.1 (Jun 1993): U+200E (E2 80 8E) LEFT-TO-RIGHT MARK
    * 6.3 (Sep 2013): U+2068 (E2 81 A8) FIRST STRONG ISOLATE,
    *                 U+2069 (E2 81 A9) POP DIRECTIONAL ISOLATE
    * Worse results seen for: U+202D "\xE2\x80\xAD" U+202C "\xE2\x80\xAC" */
   char const *hb;
   NYD2_IN;

   su_mem_set(bip, 0, sizeof *bip);
   bip->bi_start.s = bip->bi_end.s = UNCONST(char*,su_empty);

   if((n_psonce & n_PSO_UNICODE) && (hb = ok_vlook(headline_bidi)) != NIL){
      switch(*hb){
      case '3':
         bip->bi_pad = 2;
         /* FALLTHRU */
      case '2':
         bip->bi_start.s = bip->bi_end.s = UNCONST(char*,"\xE2\x80\x8E");
         break;
      case '1':
         bip->bi_pad = 2;
         /* FALLTHRU */
      default:
         bip->bi_start.s = UNCONST(char*,"\xE2\x81\xA8");
         bip->bi_end.s = UNCONST(char*,"\xE2\x81\xA9");
         break;
      }
      bip->bi_start.l = bip->bi_end.l = 3;
   }

   NYD2_OU;
}
#endif /* mx_HAVE_NATCH_CHAR */

void
mx_locale_init(void){
   NYD2_IN;

   n_psonce &= ~(n_PSO_UNICODE | n_PSO_ENC_MBSTATE);

#ifndef mx_HAVE_SETLOCALE
   n_mb_cur_max = 1;
#else

   if(setlocale(LC_ALL, su_empty) == NIL && (n_psonce & n_PSO_INTERACTIVE))
      n_err(_("Cannot set locale to $LC_ALL=%s\n"), n_var_oklook(ok_v_LC_ALL));

   n_mb_cur_max = MB_CUR_MAX;
# ifdef mx_HAVE_NL_LANGINFO
   /* C99 */{
      char const *cp;

      if((cp = nl_langinfo(CODESET)) != NIL)
         /* (Will log during startup if user set that via -S) */
         ok_vset(ttycharset, cp);
   }
# endif /* mx_HAVE_SETLOCALE */

# ifdef mx_HAVE_C90AMEND1
   if(n_mb_cur_max > 1){
#  ifdef mx_HAVE_ALWAYS_UNICODE_LOCALE
      n_psonce |= n_PSO_UNICODE;
#  else
      wchar_t wc;

      if(mbtowc(&wc, "\303\266", 2) == 2 && wc == 0xF6 &&
            mbtowc(&wc, "\342\202\254", 3) == 3 && wc == 0x20AC)
         n_psonce |= n_PSO_UNICODE;
      /* Reset possibly messed up state; luckily this also gives us an
       * indication whether the encoding has locking shift state sequences */
      if(mbtowc(&wc, NULL, n_mb_cur_max))
         n_psonce |= n_PSO_ENC_MBSTATE;
#  endif
   }
# endif
#endif /* mx_HAVE_C90AMEND1 */

   NYD2_OU;
}

boole
mx_visual_info(struct mx_visual_info_ctx *vicp,
      BITENUM_IS(u32,mx_visual_info_flags) vif){
#ifdef mx_HAVE_C90AMEND1
   mbstate_t *mbp;
#endif
   uz il;
   char const *ib;
   boole rv;
   NYD2_IN;

   ASSERT(vicp != NIL);
   ASSERT(vicp->vic_inlen == 0 || vicp->vic_indat != NIL);
   ASSERT(!(vif & n__VISUAL_INFO_FLAGS) || !(vif & mx_VISUAL_INFO_ONE_CHAR));

   rv = TRU1;
   ib = vicp->vic_indat;
   if((il = vicp->vic_inlen) == UZ_MAX)
      il = vicp->vic_inlen = su_cs_len(ib);

   if((vif & (mx_VISUAL_INFO_WIDTH_QUERY | mx_VISUAL_INFO_WOUT_PRINTABLE)
         ) == mx_VISUAL_INFO_WOUT_PRINTABLE)
      vif |= mx_VISUAL_INFO_WIDTH_QUERY;

   vicp->vic_chars_seen = vicp->vic_bytes_seen = vicp->vic_vi_width = 0;
   if(vif & mx_VISUAL_INFO_WOUT_CREATE){
      if(vif & mx_VISUAL_INFO_WOUT_AUTO_ALLOC)
         vicp->vic_woudat = su_AUTO_ALLOC(sizeof(*vicp->vic_woudat) * (il +1));
      vicp->vic_woulen = 0;
   }

#ifdef mx_HAVE_C90AMEND1
   if((mbp = vicp->vic_mbstate) == NIL)
      mbp = &vicp->vic_mbs_def;
#endif

   if(il > 0){
      do/* while(!(vif & mx_VISUAL_INFO_ONE_CHAR) && il > 0) */{
#ifdef mx_HAVE_C90AMEND1
         uz i = mbrtowc(&vicp->vic_waccu, ib, il, mbp);

         if(i == S(uz,-2)){
            rv = FAL0;
            break;
         }else if(i == S(uz,-1)){
            if(!(vif & mx_VISUAL_INFO_SKIP_ERRORS)){
               rv = FAL0;
               break;
            }
            su_mem_set(mbp, 0, sizeof *mbp);
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

         if(vif & mx_VISUAL_INFO_WIDTH_QUERY){
            int w;
            wchar_t wc;

            wc = vicp->vic_waccu;

# ifdef mx_HAVE_WCWIDTH
            w = (wc == '\t' ? 1 : wcwidth(wc));
# else
            if(wc == '\t' || iswprint(wc))
               w = 1 + (wc >= 0x1100u); /* S-CText isfullwidth() */
            else
               w = -1;
# endif
            if(w > 0)
               vicp->vic_vi_width += w;
            else if(vif & mx_VISUAL_INFO_WOUT_PRINTABLE)
               continue;
         }
#else /* mx_HAVE_C90AMEND1 */
         char c;

         if((c = *ib) == '\0'){
            il = 0;
            break;
         }

         ++vicp->vic_chars_seen;
         ++vicp->vic_bytes_seen;
         vicp->vic_waccu = c;
         if(vif & mx_VISUAL_INFO_WIDTH_QUERY)
            vicp->vic_vi_width += (c == '\t' || su_cs_is_print(c)); /* XXX */

         ++ib;
         --il;
#endif

         if(vif & mx_VISUAL_INFO_WOUT_CREATE)
            vicp->vic_woudat[vicp->vic_woulen++] = vicp->vic_waccu;
      }while(!(vif & mx_VISUAL_INFO_ONE_CHAR) && il > 0);
   }

   if(vif & mx_VISUAL_INFO_WOUT_CREATE)
      vicp->vic_woudat[vicp->vic_woulen] = '\0';
   vicp->vic_oudat = ib;
   vicp->vic_oulen = il;
   vicp->vic_flags = vif;

   NYD2_OU;
   return rv;
}

uz
mx_field_detect_clip(uz maxlen, char const *buf, uz blen){/*TODO mbrtowc()*/
   uz rv;
   NYD_IN;

#ifdef mx_HAVE_NATCH_CHAR
   maxlen = MIN(maxlen, blen);
   for(rv = 0; maxlen > 0;){
      int ml = mblen(buf, maxlen);
      if(ml <= 0){
         mblen(NIL, 0);
         break;
      }
      buf += ml;
      rv += ml;
      maxlen -= ml;
   }
#else
   rv = MIN(blen, maxlen);
#endif

   NYD_OU;
   return rv;
}

char *
mx_colalign(char const *cp, int col, int fill, int *cols_decr_used_or_nil){
#ifdef mx_HAVE_NATCH_CHAR
   struct a_uis_bidi_info bi;
#endif
   int col_orig, n, size;
   boole isbidi, isuni, istab, isrepl;
   char *nb, *np;
   NYD_IN;

   /* Bidi only on request and when there is 8-bit data */
   col_orig = col;
   isbidi = isuni = FAL0;
#ifdef mx_HAVE_NATCH_CHAR
   isuni = ((n_psonce & n_PSO_UNICODE) != 0);

   a_uis_bidi_info_create(&bi);
   if(bi.bi_start.l == 0)
      goto jnobidi;

   if(!(isbidi = a_uis_bidi_info_needed(cp, su_cs_len(cp))))
      goto jnobidi;

   if(S(uz,col) >= bi.bi_pad)
      col -= bi.bi_pad;
   else
      col = 0;
jnobidi:
#endif

   /* C99 */{
      uz i;

      i = fill ? col : 0;
      if(isbidi){
#ifdef mx_HAVE_NATCH_CHAR
         i += bi.bi_start.l + bi.bi_end.l;
#endif
      }

      np = nb = su_AUTO_ALLOC(n_mb_cur_max * su_cs_len(cp)  + i +1);
   }

#ifdef mx_HAVE_NATCH_CHAR
   if(isbidi){
      su_mem_copy(np, bi.bi_start.s, bi.bi_start.l);
      np += bi.bi_start.l;
   }
#endif

   while(*cp != '\0'){
      istab = FAL0;
#ifdef mx_HAVE_C90AMEND1
      if(n_mb_cur_max > 1){
         wchar_t  wc;

         n = 1;
         isrepl = TRU1;
         if((size = mbtowc(&wc, cp, n_mb_cur_max)) == -1)
            size = 1;
         else if(wc == L'\t'){
            cp += size - 1; /* Silly, charset unknown (.. until S-Ctext) */
            isrepl = FAL0;
            istab = TRU1;
         }else if(iswprint(wc)){
# ifndef mx_HAVE_WCWIDTH
            n = 1 + (wc >= 0x1100u); /* TODO use S-CText isfullwidth() */
# else
            if((n = wcwidth(wc)) == -1)
               n = 1;
            else
# endif
               isrepl = FAL0;
         }
      }else
#endif
      {
         n = size = 1;
         istab = (*cp == '\t');
         isrepl = !(istab || su_cs_is_print(S(uc,*cp)));
      }

      if(n > col)
         break;
      col -= n;

      if(isrepl){
         if(isuni){
            /* Contained in n_mb_cur_max, then */
            su_mem_copy(np, su_utf8_replacer, sizeof(su_utf8_replacer) -1);
            np += sizeof(su_utf8_replacer) -1;
         }else
            *np++ = '?';
         cp += size;
      }else if(istab || (size == 1 && su_cs_is_space(*cp))){
         *np++ = ' ';
         ++cp;
      }else
         while(size--)
            *np++ = *cp++;
   }

   if(fill && col != 0){
      if(fill > 0){
         su_mem_move(nb + col, nb, P2UZ(np - nb));
         su_mem_set(nb, ' ', col);
      }else
         su_mem_set(np, ' ', col);
      np += col;
      col = 0;
   }

#ifdef mx_HAVE_NATCH_CHAR
   if(isbidi){
      su_mem_copy(np, bi.bi_end.s, bi.bi_end.l);
      np += bi.bi_end.l;
   }
#endif

   *np = '\0';

   if(cols_decr_used_or_nil != NIL)
      *cols_decr_used_or_nil -= col_orig - col;

   NYD_OU;
   return nb;
}

void
mx_makeprint(struct str const *in, struct str *out){ /* TODO <-> TTYCHARSET! */
   /* TODO: makeprint() should honour *ttycharset*.  This of course does not
    * TODO work with ISO C / POSIX since mbrtowc() do know about locales, not
    * TODO charsets, and ditto iswprint() etc. do work with the locale too.
    * TODO I hope S-CText can do something about that, and/or otherwise add
    * TODO some special treatment for UTF-8 (take it from S-CText too then) */
   char const *inp, *maxp;
   char *outp;
   ASSERT_INJ( uz msz; )
   NYD_IN;

   out->s =
   outp = su_ALLOC(ASSERT_INJ( msz = )
         in->l*n_mb_cur_max + 2u*n_mb_cur_max +1);
   inp = in->s;
   maxp = inp + in->l;

#ifdef mx_HAVE_NATCH_CHAR
   if(n_mb_cur_max > 1){
      char mbb[MB_LEN_MAX + 1];
      wchar_t wc;
      int i, n;
      boole isuni;

      isuni = ((n_psonce & n_PSO_UNICODE) != 0);

      out->l = 0;
      while(inp < maxp){
         if(*inp & 0200)
            n = mbtowc(&wc, inp, P2UZ(maxp - inp));
         else{
            wc = *inp;
            n = 1;
         }

         if(n == -1){
            /* FIXME Why mbtowc() resetting here?
             * FIXME what about ISO 2022-JP plus -- those
             * FIXME will loose shifts, then!
             * FIXME THUS - we'd need special "known points"
             * FIXME to do so - say, after a newline!!
             * FIXME WE NEED TO CHANGE ALL USES +MBLEN! */
            mbtowc(&wc, NIL, n_mb_cur_max);
            wc = isuni ? 0xFFFD : '?';
            n = 1;
         }else if(n == 0)
            n = 1;
         inp += n;

         if(!iswprint(wc) && wc != '\n' /*&& wc != '\r' && wc != '\b'*/ &&
               wc != '\t'){
            if ((wc & ~S(wchar_t,037)) == 0)
               wc = isuni ? 0x2400 | wc : '?';
            else if(wc == 0177)
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

         if((n = wctomb(mbb, wc)) <= 0)
            continue;
         out->l += n;
         ASSERT(out->l < msz);
         for(i = 0; i < n; ++i)
            *outp++ = mbb[i];
      }
   }else
#endif /* mx_HAVE_NATCH_CHAR */
   {
      int c;
      while(inp < maxp){
         c = *inp++ & 0377;
         if(!su_cs_is_print(c) &&
               c != '\n' && c != '\r' && c != '\b' && c != '\t')
            c = '?';
         *outp++ = c;
      }
      out->l = in->l;
   }

   out->s[out->l] = '\0';

   NYD_OU;
}

char *
mx_makeprint_cp(char const *cp){
   struct str in, out;
   char *rv;
   NYD_IN;

   in.s = UNCONST(char*,cp);
   in.l = su_cs_len(cp);

   mx_makeprint(&in, &out);
   rv = savestrbuf(out.s, out.l);
   su_FREE(out.s);

   NYD_OU;
   return rv;
}

int
mx_makeprint_write_fp(char const *s, uz size, FILE *fp){
   struct str in, out;
   int n;
   NYD_IN;

   in.s = UNCONST(char*,s);
   in.l = size;

   mx_makeprint(&in, &out);
   n = fwrite(out.s, 1, out.l, fp);
   su_FREE(out.s);

   NYD_OU;
   return n;
}

void
mx_makelow(char *cp){ /* TODO isn't that crap? --> */
      NYD_IN;

#ifdef mx_HAVE_C90AMEND1
   if(n_mb_cur_max > 1){
      char *tp;
      wchar_t wc;
      int len;

      tp = cp;

      while(*cp != '\0'){
         len = mbtowc(&wc, cp, n_mb_cur_max);
         if(len < 0)
            *tp++ = *cp++;
         else{
            wc = towlower(wc);
            if(wctomb(tp, wc) == len)
               tp += len, cp += len;
            else
               *tp++ = *cp++; /* <-- at least here */
         }
      }
   }else
#endif
   for(;; ++cp){
      char c;

      if((c = *cp) == '\0')
         break;
      *cp = su_cs_to_lower(c);
   }

   NYD_OU;
}

char *
mx_substr(char const *str, char const *sub){
   char const *cp, *backup;
   NYD_IN;

   cp = sub;
   backup = str;

   while(*str != '\0' && *cp != '\0'){
#ifdef mx_HAVE_C90AMEND1
      if(n_mb_cur_max > 1){
         wchar_t c1, c2;
         int i;

         if((i = mbtowc(&c1, cp, n_mb_cur_max)) == -1)
            goto Jsinglebyte;
         cp += i;
         if((i = mbtowc(&c2, str, n_mb_cur_max)) == -1)
            goto Jsinglebyte;
         str += i;
         c1 = towupper(c1);
         c2 = towupper(c2);
         if(c1 != c2){
            if((i = mbtowc(&c1, backup, n_mb_cur_max)) > 0)
               backup += i;
            else
               ++backup;
            str = backup;
            cp = sub;
         }
      }else
Jsinglebyte:
#endif
      {
         char c1, c2;

         c1 = su_cs_to_upper(*cp++);
         c2 = su_cs_to_upper(*str++);
         if(c1 != c2){
            str = ++backup;
            cp = sub;
         }
      }
   }

   NYD_OU;
   return (*cp == '\0' ? UNCONST(char*,backup) : NIL);
}

uz
mx_del_cntrl(char *cp, uz len){
   uz x, y;
   NYD2_IN;

   for(x = y = 0; x < len; ++x)
      if(!su_cs_is_cntrl(cp[x]))
         cp[y++] = cp[x];
   cp[y] = '\0';

   NYD2_OU;
   return y;
}

#include "su/code-ou.h"
/* s-it-mode */
