/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ String support routines.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
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
#define n_FILE strings

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <ctype.h>

FL char *
(savestr)(char const *str SALLOC_DEBUG_ARGS)
{
   size_t size;
   char *news;
   NYD_ENTER;

   size = strlen(str) +1;
   news = (salloc)(size SALLOC_DEBUG_ARGSCALL);
   memcpy(news, str, size);
   NYD_LEAVE;
   return news;
}

FL char *
(savestrbuf)(char const *sbuf, size_t sbuf_len SALLOC_DEBUG_ARGS)
{
   char *news;
   NYD_ENTER;

   news = (salloc)(sbuf_len +1 SALLOC_DEBUG_ARGSCALL);
   memcpy(news, sbuf, sbuf_len);
   news[sbuf_len] = 0;
   NYD_LEAVE;
   return news;
}

FL char *
(savecatsep)(char const *s1, char sep, char const *s2 SALLOC_DEBUG_ARGS)
{
   size_t l1, l2;
   char *news;
   NYD_ENTER;

   l1 = (s1 != NULL) ? strlen(s1) : 0;
   l2 = strlen(s2);
   news = (salloc)(l1 + (sep != '\0') + l2 +1 SALLOC_DEBUG_ARGSCALL);
   if (l1 > 0) {
      memcpy(news + 0, s1, l1);
      if (sep != '\0')
         news[l1++] = sep;
   }
   memcpy(news + l1, s2, l2);
   news[l1 + l2] = '\0';
   NYD_LEAVE;
   return news;
}

/*
 * Support routines, auto-reclaimed storage
 */

FL char *
(i_strdup)(char const *src SALLOC_DEBUG_ARGS)
{
   size_t sz;
   char *dest;
   NYD_ENTER;

   sz = strlen(src) +1;
   dest = (salloc)(sz SALLOC_DEBUG_ARGSCALL);
   i_strcpy(dest, src, sz);
   NYD_LEAVE;
   return dest;
}

FL struct str *
str_concat_csvl(struct str *self, ...) /* XXX onepass maybe better here */
{
   va_list vl;
   size_t l;
   char const *cs;
   NYD_ENTER;

   va_start(vl, self);
   for (l = 0; (cs = va_arg(vl, char const*)) != NULL;)
      l += strlen(cs);
   va_end(vl);

   self->l = l;
   self->s = salloc(l +1);

   va_start(vl, self);
   for (l = 0; (cs = va_arg(vl, char const*)) != NULL;) {
      size_t i = strlen(cs);
      memcpy(self->s + l, cs, i);
      l += i;
   }
   self->s[l] = '\0';
   va_end(vl);
   NYD_LEAVE;
   return self;
}

FL struct str *
(str_concat_cpa)(struct str *self, char const * const *cpa,
   char const *sep_o_null SALLOC_DEBUG_ARGS)
{
   size_t sonl, l;
   char const * const *xcpa;
   NYD_ENTER;

   sonl = (sep_o_null != NULL) ? strlen(sep_o_null) : 0;

   for (l = 0, xcpa = cpa; *xcpa != NULL; ++xcpa)
      l += strlen(*xcpa) + sonl;

   self->l = l;
   self->s = (salloc)(l +1 SALLOC_DEBUG_ARGSCALL);

   for (l = 0, xcpa = cpa; *xcpa != NULL; ++xcpa) {
      size_t i = strlen(*xcpa);
      memcpy(self->s + l, *xcpa, i);
      l += i;
      if (sonl > 0) {
         memcpy(self->s + l, sep_o_null, sonl);
         l += sonl;
      }
   }
   self->s[l] = '\0';
   NYD_LEAVE;
   return self;
}

/*
 * Routines that are not related to auto-reclaimed storage follow.
 */

FL int
anyof(char const *s1, char const *s2)
{
   NYD2_ENTER;
   for (; *s1 != '\0'; ++s1)
      if (strchr(s2, *s1) != NULL)
         break;
   NYD2_LEAVE;
   return (*s1 != '\0');
}

FL char *
n_strsep(char **iolist, char sep, bool_t ignore_empty)
{
   char *base, *cp;
   NYD2_ENTER;

   for (base = *iolist; base != NULL; base = *iolist) {
      while (*base != '\0' && blankspacechar(*base))
         ++base;
      cp = strchr(base, sep);
      if (cp != NULL)
         *iolist = cp + 1;
      else {
         *iolist = NULL;
         cp = base + strlen(base);
      }
      while (cp > base && blankspacechar(cp[-1]))
         --cp;
      *cp = '\0';
      if (*base != '\0' || !ignore_empty)
         break;
   }
   NYD2_LEAVE;
   return base;
}

FL char *
n_strescsep(char **iolist, char sep, bool_t ignore_empty){
   char *cp, c, *base;
   bool_t isesc, anyesc;
   NYD2_ENTER;

   assert(sep != '\0');

   for(base = *iolist; base != NULL; base = *iolist){
      while((c = *base) != '\0' && blankspacechar(c))
         ++base;

      for(isesc = anyesc = FAL0, cp = base;; ++cp){
         if(UNLIKELY((c = *cp) == '\0')){
            *iolist = NULL;
            break;
         }else if(!isesc){
            if(c == sep){
               *iolist = cp + 1;
               break;
            }
            isesc = (c == '\\');
         }else{
            isesc = FAL0;
            anyesc |= (c == sep);
         }
      }

      while(cp > base && blankspacechar(cp[-1]))
         --cp;
      *cp = '\0';

      if(*base != '\0'){
         if(anyesc){
            char *ins;

            for(ins = cp = base;; ++ins)
               if((c = *cp) == '\\' && cp[1] == sep){
                  *ins = sep;
                  cp += 2;
               }else if((*ins = (++cp, c)) == '\0')
                  break;
         }
         break;
      }
      if(!ignore_empty)
         break;
   }
   NYD2_LEAVE;
   return base;
}

FL void
i_strcpy(char *dest, char const *src, size_t size)
{
   NYD2_ENTER;
   if (size > 0) {
      for (;; ++dest, ++src)
         if ((*dest = lowerconv(*src)) == '\0') {
            break;
         } else if (--size == 0) {
            *dest = '\0';
            break;
         }
   }
   NYD2_LEAVE;
}

FL int
is_prefix(char const *as1, char const *as2)
{
   char c;
   NYD2_ENTER;

   for (; (c = *as1) == *as2 && c != '\0'; ++as1, ++as2)
      if (*as2 == '\0')
         break;
   NYD2_LEAVE;
   return (c == '\0');
}

FL char *
string_quote(char const *v) /* TODO too simpleminded (getrawlist(), +++ ..) */
{
   char const *cp;
   size_t i;
   char c, *rv;
   NYD2_ENTER;

   for (i = 0, cp = v; (c = *cp) != '\0'; ++i, ++cp)
      if (c == '"' || c == '\\')
         ++i;
   rv = salloc(i +1);

   for (i = 0, cp = v; (c = *cp) != '\0'; rv[i++] = c, ++cp)
      if (c == '"' || c == '\\')
         rv[i++] = '\\';
   rv[i] = '\0';
   NYD2_LEAVE;
   return rv;
}

FL char *
laststring(char *linebuf, bool_t *needs_list, bool_t strip)
{
   char *cp, *p, quoted;
   NYD_ENTER;

   /* Anything to do at all? */
   if (*(cp = linebuf) == '\0')
      goto jnull;
   cp += strlen(linebuf) -1;

   /* Strip away trailing blanks */
   while (spacechar(*cp) && cp > linebuf)
      --cp;
   cp[1] = '\0';
   if (cp == linebuf)
      goto jleave;

   /* Now search for the BOS of the "last string" */
   quoted = *cp;
   if (quoted == '\'' || quoted == '"') {
      if (strip)
         *cp = '\0';
   } else
      quoted = ' ';

   while (cp > linebuf) {
      --cp;
      if (quoted != ' ') {
         if (*cp != quoted)
            continue;
      } else if (!spacechar(*cp))
         continue;
      if (cp == linebuf || cp[-1] != '\\') {
         /* When in whitespace mode, WS prefix doesn't belong */
         if (quoted == ' ')
            ++cp;
         break;
      }
      /* Expand the escaped quote character */
      for (p = --cp; (p[0] = p[1]) != '\0'; ++p)
         ;
   }
   if (strip && quoted != ' ' && *cp == quoted)
      for (p = cp; (p[0] = p[1]) != '\0'; ++p)
         ;

   /* The "last string" has been skipped over, but still, try to step backwards
    * until we are at BOS or see whitespace, so as to make possible things like
    * "? copy +'x y.mbox'" or even "? copy +x\ y.mbox" */
   while (cp > linebuf) {
      --cp;
      if (spacechar(*cp)) {
         p = cp;
         *cp++ = '\0';
         /* We can furtherly release our callees if we now decide wether the
          * remaining non-"last string" line content contains non-WS */
         while (--p >= linebuf)
            if (!spacechar(*p))
               goto jleave;
         linebuf = cp;
         break;
      }
   }

jleave:
   if (cp != NULL && *cp == '\0')
      goto jnull;
   *needs_list = (cp != linebuf && *linebuf != '\0');
j_leave:
   NYD_LEAVE;
   return cp;
jnull:
   *needs_list = FAL0;
   cp = NULL;
   goto j_leave;
}

FL void
makelow(char *cp) /* TODO isn't that crap? --> */
{
      NYD_ENTER;
#ifdef HAVE_C90AMEND1
   if (mb_cur_max > 1) {
      char *tp = cp;
      wchar_t wc;
      int len;

      while (*cp != '\0') {
         len = mbtowc(&wc, cp, mb_cur_max);
         if (len < 0)
            *tp++ = *cp++;
         else {
            wc = towlower(wc);
            if (wctomb(tp, wc) == len)
               tp += len, cp += len;
            else
               *tp++ = *cp++; /* <-- at least here */
         }
      }
   } else
#endif
   {
      do
         *cp = tolower((uc_i)*cp);
      while (*cp++ != '\0');
   }
   NYD_LEAVE;
}

FL bool_t
substr(char const *str, char const *sub)
{
   char const *cp, *backup;
   NYD_ENTER;

   cp = sub;
   backup = str;
   while (*str != '\0' && *cp != '\0') {
#ifdef HAVE_C90AMEND1
      if (mb_cur_max > 1) {
         wchar_t c, c2;
         int sz;

         if ((sz = mbtowc(&c, cp, mb_cur_max)) == -1)
            goto Jsinglebyte;
         cp += sz;
         if ((sz = mbtowc(&c2, str, mb_cur_max)) == -1)
            goto Jsinglebyte;
         str += sz;
         c = towupper(c);
         c2 = towupper(c2);
         if (c != c2) {
            if ((sz = mbtowc(&c, backup, mb_cur_max)) > 0) {
               backup += sz;
               str = backup;
            } else
               str = ++backup;
            cp = sub;
         }
      } else
Jsinglebyte:
#endif
      {
         int c, c2;

         c = *cp++ & 0377;
         if (islower(c))
            c = toupper(c);
         c2 = *str++ & 0377;
         if (islower(c2))
            c2 = toupper(c2);
         if (c != c2) {
            str = ++backup;
            cp = sub;
         }
      }
   }
   NYD_LEAVE;
   return (*cp == '\0');
}

FL char *
sstpcpy(char *dst, char const *src)
{
   NYD2_ENTER;
   while ((*dst = *src++) != '\0')
      ++dst;
   NYD2_LEAVE;
   return dst;
}

FL char *
(sstrdup)(char const *cp SMALLOC_DEBUG_ARGS)
{
   char *dp;
   NYD2_ENTER;

   dp = (cp == NULL) ? NULL : (sbufdup)(cp, strlen(cp) SMALLOC_DEBUG_ARGSCALL);
   NYD2_LEAVE;
   return dp;
}

FL char *
(sbufdup)(char const *cp, size_t len SMALLOC_DEBUG_ARGS)
{
   char *dp = NULL;
   NYD2_ENTER;

   dp = (smalloc)(len +1 SMALLOC_DEBUG_ARGSCALL);
   if (cp != NULL)
      memcpy(dp, cp, len);
   dp[len] = '\0';
   NYD2_LEAVE;
   return dp;
}

FL ssize_t
n_strscpy(char *dst, char const *src, size_t dstsize){
   ssize_t rv;
   NYD2_ENTER;

   if(LIKELY(dstsize > 0)){
      rv = 0;
      do{
         if((dst[rv] = src[rv]) == '\0')
            goto jleave;
         ++rv;
      }while(--dstsize > 0);
      dst[--rv] = '\0';
   }
#ifdef HAVE_DEVEL
   else
      assert(dstsize > 0);
#endif
   rv = -1;
jleave:
   NYD2_LEAVE;
   return rv;
}

FL int
asccasecmp(char const *s1, char const *s2)
{
   int cmp;
   NYD2_ENTER;

   for (;;) {
      char c1 = *s1++, c2 = *s2++;
      if ((cmp = lowerconv(c1) - lowerconv(c2)) != 0 || c1 == '\0')
         break;
   }
   NYD2_LEAVE;
   return cmp;
}

FL int
ascncasecmp(char const *s1, char const *s2, size_t sz)
{
   int cmp = 0;
   NYD2_ENTER;

   while (sz-- > 0) {
      char c1 = *s1++, c2 = *s2++;
      cmp = (ui8_t)lowerconv(c1);
      cmp -= (ui8_t)lowerconv(c2);
      if (cmp != 0 || c1 == '\0')
         break;
   }
   NYD2_LEAVE;
   return cmp;
}

FL char const *
asccasestr(char const *s1, char const *s2)
{
   char c2, c1;
   NYD2_ENTER;

   for (c2 = *s2++, c2 = lowerconv(c2);;) {
      if ((c1 = *s1++) == '\0') {
         s1 = NULL;
         break;
      }
      if (lowerconv(c1) == c2 && is_asccaseprefix(s1, s2)) {
         --s1;
         break;
      }
   }
   NYD2_LEAVE;
   return s1;
}

FL bool_t
is_asccaseprefix(char const *as1, char const *as2)
{
   bool_t rv = FAL0;
   NYD2_ENTER;

   for (;; ++as1, ++as2) {
      char c1 = lowerconv(*as1), c2 = lowerconv(*as2);

      if ((rv = (c2 == '\0')))
         break;
      if (c1 != c2)
         break;
   }
   NYD2_LEAVE;
   return rv;
}

FL struct str *
(n_str_dup)(struct str *self, struct str const *t SMALLOC_DEBUG_ARGS)
{
   NYD_ENTER;
   if (t != NULL && t->l > 0) {
      self->l = t->l;
      self->s = (srealloc)(self->s, t->l +1 SMALLOC_DEBUG_ARGSCALL);
      memcpy(self->s, t->s, t->l);
      self->s[t->l] = '\0';
   } else
      self->l = 0;
   NYD_LEAVE;
   return self;
}

FL struct str *
(n_str_add_buf)(struct str *self, char const *buf, size_t buflen
   SMALLOC_DEBUG_ARGS)
{
   NYD_ENTER;
   if (buflen != 0) {
      size_t sl = self->l;
      self->l = sl + buflen;
      self->s = (srealloc)(self->s, self->l +1 SMALLOC_DEBUG_ARGSCALL);
      memcpy(self->s + sl, buf, buflen);
      self->s[self->l] = '\0';
   }
   NYD_LEAVE;
   return self;
}

/*
 * struct n_string TODO extend, optimize
 */

FL struct n_string *
(n_string_clear)(struct n_string *self SMALLOC_DEBUG_ARGS){
   NYD_ENTER;

   assert(self != NULL);

   if(self->s_size != 0){
      if(!self->s_auto){
#ifdef HAVE_DEBUG
         sfree(self->s_dat SMALLOC_DEBUG_ARGSCALL);
#else
         free(self->s_dat);
#endif
      }
      self->s_len = self->s_auto = self->s_size = 0;
      self->s_dat = NULL;
   }
   NYD_LEAVE;
   return self;
}

FL struct n_string *
(n_string_reserve)(struct n_string *self, size_t noof SMALLOC_DEBUG_ARGS){
   ui32_t i, l, s;
   NYD_ENTER;

   assert(self != NULL);

   s = self->s_size;
   l = self->s_len;
#if 0 /* FIXME memory alloc too large */
   if(SI32_MAX - n_ALIGN(1) - l <= noof)
      n_panic(_("Memory allocation too large"));
#endif

   if((i = s - l) <= noof){
      i += 1 + l + (ui32_t)noof;
      i = n_ALIGN(i);
      self->s_size = i -1;

      if(!self->s_auto)
         self->s_dat = (srealloc)(self->s_dat, i SMALLOC_DEBUG_ARGSCALL);
      else{
         char *ndat = (salloc)(i SALLOC_DEBUG_ARGSCALL);

         if(l > 0)
            memcpy(ndat, self->s_dat, l);
         self->s_dat = ndat;
      }
   }
   NYD_LEAVE;
   return self;
}

FL struct n_string *
(n_string_push_buf)(struct n_string *self, char const *buf, size_t buflen
      SMALLOC_DEBUG_ARGS){
   NYD_ENTER;

   assert(self != NULL);
   assert(buflen == 0 || buf != NULL);

   if(buflen == UIZ_MAX)
      buflen = (buf == NULL) ? 0 : strlen(buf);

   if(buflen > 0){
      ui32_t i;

      self = (n_string_reserve)(self, buflen SMALLOC_DEBUG_ARGSCALL);
      memcpy(self->s_dat + (i = self->s_len), buf, buflen);
      self->s_len = (i += (ui32_t)buflen);
   }
   NYD_LEAVE;
   return self;
}

FL struct n_string *
(n_string_push_c)(struct n_string *self, char c SMALLOC_DEBUG_ARGS){
   NYD_ENTER;

   assert(self != NULL);

   if(self->s_len + 1 >= self->s_size)
      self = (n_string_reserve)(self, 1 SMALLOC_DEBUG_ARGSCALL);
   self->s_dat[self->s_len++] = c;
   NYD_LEAVE;
   return self;
}

FL struct n_string *
(n_string_unshift_buf)(struct n_string *self, char const *buf, size_t buflen
      SMALLOC_DEBUG_ARGS){
   NYD_ENTER;

   assert(self != NULL);
   assert(buflen == 0 || buf != NULL);

   if(buflen == UIZ_MAX)
      buflen = (buf == NULL) ? 0 : strlen(buf);

   if(buflen > 0){
      self = (n_string_reserve)(self, buflen SMALLOC_DEBUG_ARGSCALL);
      if(self->s_len > 0)
         memmove(self->s_dat + buflen, self->s_dat, self->s_len);
      memcpy(self->s_dat, buf, buflen);
      self->s_len += (ui32_t)buflen;
   }
   NYD_LEAVE;
   return self;
}

FL struct n_string *
(n_string_unshift_c)(struct n_string *self, char c SMALLOC_DEBUG_ARGS){
   NYD_ENTER;

   assert(self != NULL);

   if(self->s_len + 1 >= self->s_size)
      self = (n_string_reserve)(self, 1 SMALLOC_DEBUG_ARGSCALL);
   if(self->s_len > 0)
      memmove(self->s_dat + 1, self->s_dat, self->s_len);
   self->s_dat[0] = c;
   ++self->s_len;
   NYD_LEAVE;
   return self;
}

FL char *
(n_string_cp)(struct n_string *self SMALLOC_DEBUG_ARGS){
   char *rv;
   NYD2_ENTER;

   assert(self != NULL);

   if(self->s_size == 0)
      self = (n_string_reserve)(self, 1 SMALLOC_DEBUG_ARGSCALL);

   (rv = self->s_dat)[self->s_len] = '\0';
   NYD2_LEAVE;
   return rv;
}

FL char const *
n_string_cp_const(struct n_string const *self){
   char const *rv;
   NYD2_ENTER;

   assert(self != NULL);

   if(self->s_size != 0){
      ((struct n_string*)UNCONST(self))->s_dat[self->s_len] = '\0';
      rv = self->s_dat;
   }else
      rv = "";
   NYD2_LEAVE;
   return rv;
}

/*
 * UTF-8
 */

#ifdef HAVE_NATCH_CHAR
FL ui32_t
n_utf8_to_utf32(char const **bdat, size_t *blen) /* TODO check false UTF8 */
{
   char const *cp;
   size_t l;
   ui32_t c, x;
   NYD2_ENTER;

   cp = *bdat;
   l = *blen - 1;
   x = (ui8_t)*cp++;

   if (x <= 0x7F)
      c = x;
   else {
      if ((x & 0xE0) == 0xC0) {
         if (l < 1)
            goto jerr;
         l -= 1;
         c = x & ~0xC0;
      } else if ((x & 0xF0) == 0xE0) {
         if (l < 2)
            goto jerr;
         l -= 2;
         c = x & ~0xE0;
         c <<= 6;
         x = (ui8_t)*cp++;
         c |= x & 0x7F;
      } else {
         if (l < 3)
            goto jerr;
         l -= 3;
         c = x & ~0xF0;
         c <<= 6;
         x = (ui8_t)*cp++;
         c |= x & 0x7F;
         c <<= 6;
         x = (ui8_t)*cp++;
         c |= x & 0x7F;
      }
      c <<= 6;
      x = (ui8_t)*cp++;
      c |= x & 0x7F;
   }

jleave:
   *bdat = cp;
   *blen = l;
   NYD2_LEAVE;
   return c;
jerr:
   c = UI32_MAX;
   goto jleave;
}
#endif /* HAVE_NATCH_CHAR */

#ifdef HAVE_FILTER_HTML_TAGSOUP
FL size_t
n_utf32_to_utf8(ui32_t c, char *buf)
{
   struct {
      ui32_t   lower_bound;
      ui32_t   upper_bound;
      ui8_t    enc_leader;
      ui8_t    enc_lval;
      ui8_t    dec_leader_mask;
      ui8_t    dec_leader_val_mask;
      ui8_t    dec_bytes_togo;
      ui8_t    cat_index;
      ui8_t    __dummy[2];
   } const _cat[] = {
      {0x00000000, 0x00000000, 0x00, 0, 0x00,      0x00,   0, 0, {0,}},
      {0x00000000, 0x0000007F, 0x00, 1, 0x80,      0x7F, 1-1, 1, {0,}},
      {0x00000080, 0x000007FF, 0xC0, 2, 0xE0, 0xFF-0xE0, 2-1, 2, {0,}},
      /* We assume surrogates are U+D800 - U+DFFF, _cat index 3 */
      /* xxx _from_utf32() simply assumes magic code points for surrogates!
       * xxx (However, should we ever get yet another surrogate range we
       * xxx need to deal with that all over the place anyway? */
      {0x00000800, 0x0000FFFF, 0xE0, 3, 0xF0, 0xFF-0xF0, 3-1, 3, {0,}},
      {0x00010000, 0x0010FFFF, 0xF0, 4, 0xF8, 0xFF-0xF8, 4-1, 4, {0,}},
   }, *catp = _cat;
   size_t l;

   if (c <= _cat[0].upper_bound) { catp += 0; goto j0; }
   if (c <= _cat[1].upper_bound) { catp += 1; goto j1; }
   if (c <= _cat[2].upper_bound) { catp += 2; goto j2; }
   if (c <= _cat[3].upper_bound) {
      /* Surrogates may not be converted (Compatibility rule C10) */
      if (c >= 0xD800u && c <= 0xDFFFu)
         goto jerr;
      catp += 3;
      goto j3;
   }
   if (c <= _cat[4].upper_bound) { catp += 4; goto j4; }
jerr:
   c = 0xFFFDu; /* Unicode replacement character */
   catp += 3;
   goto j3;
j4:
   buf[3] = (char)0x80 | (char)(c & 0x3F); c >>= 6;
j3:
   buf[2] = (char)0x80 | (char)(c & 0x3F); c >>= 6;
j2:
   buf[1] = (char)0x80 | (char)(c & 0x3F); c >>= 6;
j1:
   buf[0] = (char)catp->enc_leader | (char)(c);
j0:
   buf[catp->enc_lval] = '\0';
   l = catp->enc_lval;
   NYD2_LEAVE;
   return l;
}
#endif /* HAVE_FILTER_HTML_TAGSOUP */

/*
 * Our iconv(3) wrapper
 */
#ifdef HAVE_ICONV

static void _ic_toupper(char *dest, char const *src);
static void _ic_stripdash(char *p);

static void
_ic_toupper(char *dest, char const *src)
{
   NYD2_ENTER;
   do
      *dest++ = upperconv(*src);
   while (*src++ != '\0');
   NYD2_LEAVE;
}

static void
_ic_stripdash(char *p)
{
   char *q = p;
   NYD2_ENTER;

   do
      if (*(q = p) != '-')
         ++q;
   while (*p++ != '\0');
   NYD2_LEAVE;
}

FL iconv_t
n_iconv_open(char const *tocode, char const *fromcode)
{
   iconv_t id;
   char *t, *f;
   NYD_ENTER;

   if ((!asccasecmp(fromcode, "unknown-8bit") ||
            !asccasecmp(fromcode, "binary")) &&
         (fromcode = ok_vlook(charset_unknown_8bit)) == NULL)
      fromcode = charset_get_8bit();

   if ((id = iconv_open(tocode, fromcode)) != (iconv_t)-1)
      goto jleave;

   /* Remove the "iso-" prefixes for Solaris */
   if (!ascncasecmp(tocode, "iso-", 4))
      tocode += 4;
   else if (!ascncasecmp(tocode, "iso", 3))
      tocode += 3;
   if (!ascncasecmp(fromcode, "iso-", 4))
      fromcode += 4;
   else if (!ascncasecmp(fromcode, "iso", 3))
      fromcode += 3;
   if (*tocode == '\0' || *fromcode == '\0') {
      id = (iconv_t)-1;
      goto jleave;
   }
   if ((id = iconv_open(tocode, fromcode)) != (iconv_t)-1)
      goto jleave;

   /* Solaris prefers upper-case charset names. Don't ask... */
   t = salloc(strlen(tocode) +1);
   _ic_toupper(t, tocode);
   f = salloc(strlen(fromcode) +1);
   _ic_toupper(f, fromcode);
   if ((id = iconv_open(t, f)) != (iconv_t)-1)
      goto jleave;

   /* Strip dashes for UnixWare */
   _ic_stripdash(t);
   _ic_stripdash(f);
   if ((id = iconv_open(t, f)) != (iconv_t)-1)
      goto jleave;

   /* Add your vendor's sillynesses here */

   /* If the encoding names are equal at this point, they are just not
    * understood by iconv(), and we cannot sensibly use it in any way.  We do
    * not perform this as an optimization above since iconv() can otherwise be
    * used to check the validity of the input even with identical encoding
    * names */
   if (!strcmp(t, f))
      errno = 0;
jleave:
   NYD_LEAVE;
   return id;
}

FL void
n_iconv_close(iconv_t cd)
{
   NYD_ENTER;
   iconv_close(cd);
   if (cd == iconvd)
      iconvd = (iconv_t)-1;
   NYD_LEAVE;
}

FL void
n_iconv_reset(iconv_t cd)
{
   NYD_ENTER;
   iconv(cd, NULL, NULL, NULL, NULL);
   NYD_LEAVE;
}

/* (2012-09-24: export and use it exclusively to isolate prototype problems
 * (*inb* is 'char const **' except in POSIX) in a single place.
 * GNU libiconv even allows for configuration time const/non-const..
 * In the end it's an ugly guess, but we can't do better since make(1) doesn't
 * support compiler invocations which bail on error, so no -Werror */
/* Citrus project? */
# if defined _ICONV_H_ && defined __ICONV_F_HIDE_INVALID
  /* DragonFly 3.2.1 is special TODO newer DragonFly too, but different */
#  if OS_DRAGONFLY
#   define __INBCAST(S) (char ** __restrict__)UNCONST(S)
#  else
#   define __INBCAST(S) (char const **)UNCONST(S)
#  endif
# elif OS_SUNOS || OS_SOLARIS
#  define __INBCAST(S) (char const ** __restrict__)UNCONST(S)
# endif
# ifndef __INBCAST
#  define __INBCAST(S)  (char **)UNCONST(S)
# endif

FL int
n_iconv_buf(iconv_t cd, char const **inb, size_t *inbleft,/*XXX redo iconv use*/
   char **outb, size_t *outbleft, bool_t skipilseq)
{
   int err = 0;
   NYD2_ENTER;

   for (;;) {
      size_t sz = iconv(cd, __INBCAST(inb), inbleft, outb, outbleft);
      if (sz != (size_t)-1)
         break;
      err = errno;
      if (!skipilseq || err != EILSEQ)
         break;
      if (*inbleft > 0) {
         ++(*inb);
         --(*inbleft);
      } else if (*outbleft > 0) {
         **outb = '\0';
         break;
      }
      if (*outbleft > 0/* TODO 0xFFFD 2*/) {
         /* TODO 0xFFFD (*outb)[0] = '[';
          * TODO (*outb)[1] = '?';
          * TODO 0xFFFD (*outb)[2] = ']';
          * TODO (*outb) += 3;
          * TODO (*outbleft) -= 3; */
          *(*outb)++ = '?';
          --*outbleft;
      } else {
         err = E2BIG;
         break;
      }
      err = 0;
   }
   NYD2_LEAVE;
   return err;
}
# undef __INBCAST

FL int
n_iconv_str(iconv_t cd, struct str *out, struct str const *in,
   struct str *in_rest_or_null, bool_t skipilseq)
{
   int err;
   char *obb, *ob;
   char const *ib;
   size_t olb, ol, il;
   NYD2_ENTER;

   err = 0;
   obb = out->s;
   olb = out->l;
   ol = in->l;

   ol = (ol << 1) - (ol >> 4);
   if (olb <= ol) {
      olb = ol;
      goto jrealloc;
   }

   for (;;) {
      ib = in->s;
      il = in->l;
      ob = obb;
      ol = olb;
      err = n_iconv_buf(cd, &ib, &il, &ob, &ol, skipilseq);
      if (err == 0 || err != E2BIG)
         break;
      err = 0;
      olb += in->l;
jrealloc:
      obb = srealloc(obb, olb +1);
   }

   if (in_rest_or_null != NULL) {
      in_rest_or_null->s = UNCONST(ib);
      in_rest_or_null->l = il;
   }
   out->s = obb;
   out->s[out->l = olb - ol] = '\0';
   NYD2_LEAVE;
   return err;
}
#endif /* HAVE_ICONV */

/* s-it-mode */
