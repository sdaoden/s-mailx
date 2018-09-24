/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ String support routines.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause
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
#undef su_FILE
#define su_FILE strings
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <ctype.h>

FL char *
(savestr)(char const *str  su_DBG_LOC_ARGS_DECL)
{
   size_t size;
   char *news;
   n_NYD_IN;

   size = strlen(str);
   news = su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(size +1,  su_DBG_LOC_ARGS_ORUSE);
   if(size > 0)
      memcpy(news, str, size);
   news[size] = '\0';
   n_NYD_OU;
   return news;
}

FL char *
(savestrbuf)(char const *sbuf, size_t sbuf_len  su_DBG_LOC_ARGS_DECL)
{
   char *news;
   n_NYD_IN;

   news = su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(sbuf_len +1, su_DBG_LOC_ARGS_ORUSE);
   if(sbuf_len > 0)
      memcpy(news, sbuf, sbuf_len);
   news[sbuf_len] = 0;
   n_NYD_OU;
   return news;
}

FL char *
(savecatsep)(char const *s1, char sep, char const *s2  su_DBG_LOC_ARGS_DECL)
{
   size_t l1, l2;
   char *news;
   n_NYD_IN;

   l1 = (s1 != NULL) ? strlen(s1) : 0;
   l2 = strlen(s2);
   news = su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(l1 + (sep != '\0') + l2 +1,
         su_DBG_LOC_ARGS_ORUSE);
   if (l1 > 0) {
      memcpy(news + 0, s1, l1);
      if (sep != '\0')
         news[l1++] = sep;
   }
   if(l2 > 0)
      memcpy(news + l1, s2, l2);
   news[l1 + l2] = '\0';
   n_NYD_OU;
   return news;
}

/*
 * Support routines, auto-reclaimed storage
 */

FL struct str *
str_concat_csvl(struct str *self, ...) /* XXX onepass maybe better here */
{
   va_list vl;
   size_t l;
   char const *cs;
   n_NYD_IN;

   va_start(vl, self);
   for (l = 0; (cs = va_arg(vl, char const*)) != NULL;)
      l += strlen(cs);
   va_end(vl);

   self->l = l;
   self->s = n_autorec_alloc(l +1);

   va_start(vl, self);
   for (l = 0; (cs = va_arg(vl, char const*)) != NULL;) {
      size_t i;

      i = strlen(cs);
      if(i > 0){
         memcpy(self->s + l, cs, i);
         l += i;
      }
   }
   self->s[l] = '\0';
   va_end(vl);
   n_NYD_OU;
   return self;
}

FL struct str *
(str_concat_cpa)(struct str *self, char const * const *cpa,
   char const *sep_o_null  su_DBG_LOC_ARGS_DECL)
{
   size_t sonl, l;
   char const * const *xcpa;
   n_NYD_IN;

   sonl = (sep_o_null != NULL) ? strlen(sep_o_null) : 0;

   for (l = 0, xcpa = cpa; *xcpa != NULL; ++xcpa)
      l += strlen(*xcpa) + sonl;

   self->l = l;
   self->s = su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(l +1, su_DBG_LOC_ARGS_ORUSE);

   for (l = 0, xcpa = cpa; *xcpa != NULL; ++xcpa) {
      size_t i;

      i = strlen(*xcpa);
      if(i > 0){
         memcpy(self->s + l, *xcpa, i);
         l += i;
      }
      if (sonl > 0) {
         memcpy(self->s + l, sep_o_null, sonl);
         l += sonl;
      }
   }
   self->s[l] = '\0';
   n_NYD_OU;
   return self;
}

/*
 * Routines that are not related to auto-reclaimed storage follow.
 */

FL bool_t
n_anyof_buf(char const *template, char const *dat, size_t len){
   char c;
   n_NYD2_IN;

   if(len == UIZ_MAX){
      while((c = *template++) != '\0')
         if(strchr(dat, c) != NULL)
            break;
   }else if(len > 0){
      while((c = *template++) != '\0')
         if(memchr(dat, c, len) != NULL)
            break;
   }else
      c = '\0';
   n_NYD2_OU;
   return (c != '\0');
}

FL char *
n_strsep(char **iolist, char sep, bool_t ignore_empty){
   char *base, *cp;
   n_NYD2_IN;

   for(base = *iolist; base != NULL; base = *iolist){
      while(*base != '\0' && blankspacechar(*base))
         ++base;

      cp = strchr(base, sep);
      if(cp != NULL)
         *iolist = &cp[1];
      else{
         *iolist = NULL;
         cp = &base[strlen(base)];
      }
      while(cp > base && blankspacechar(cp[-1]))
         --cp;
      *cp = '\0';
      if(*base != '\0' || !ignore_empty)
         break;
   }
   n_NYD2_OU;
   return base;
}

FL char *
n_strsep_esc(char **iolist, char sep, bool_t ignore_empty){
   char *cp, c, *base;
   bool_t isesc, anyesc;
   n_NYD2_IN;

   for(base = *iolist; base != NULL; base = *iolist){
      while((c = *base) != '\0' && blankspacechar(c))
         ++base;

      for(isesc = anyesc = FAL0, cp = base;; ++cp){
         if(n_UNLIKELY((c = *cp) == '\0')){
            *iolist = NULL;
            break;
         }else if(!isesc){
            if(c == sep){
               *iolist = &cp[1];
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
      }

      if(*base != '\0' || !ignore_empty)
         break;
   }
   n_NYD2_OU;
   return base;
}

FL bool_t
is_prefix(char const *as1, char const *as2) /* TODO arg order */
{
   char c;
   n_NYD2_IN;

   for (; (c = *as1) == *as2 && c != '\0'; ++as1, ++as2)
      if (*as2 == '\0')
         break;
   n_NYD2_OU;
   return (c == '\0');
}

FL char *
string_quote(char const *v) /* TODO too simpleminded (getrawlist(), +++ ..) */
{
   char const *cp;
   size_t i;
   char c, *rv;
   n_NYD2_IN;

   for (i = 0, cp = v; (c = *cp) != '\0'; ++i, ++cp)
      if (c == '"' || c == '\\')
         ++i;
   rv = n_autorec_alloc(i +1);

   for (i = 0, cp = v; (c = *cp) != '\0'; rv[i++] = c, ++cp)
      if (c == '"' || c == '\\')
         rv[i++] = '\\';
   rv[i] = '\0';
   n_NYD2_OU;
   return rv;
}

FL void
makelow(char *cp) /* TODO isn't that crap? --> */
{
      n_NYD_IN;
#ifdef mx_HAVE_C90AMEND1
   if (n_mb_cur_max > 1) {
      char *tp = cp;
      wchar_t wc;
      int len;

      while (*cp != '\0') {
         len = mbtowc(&wc, cp, n_mb_cur_max);
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
   n_NYD_OU;
}

FL bool_t
substr(char const *str, char const *sub)
{
   char const *cp, *backup;
   n_NYD_IN;

   cp = sub;
   backup = str;
   while (*str != '\0' && *cp != '\0') {
#ifdef mx_HAVE_C90AMEND1
      if (n_mb_cur_max > 1) {
         wchar_t c, c2;
         int sz;

         if ((sz = mbtowc(&c, cp, n_mb_cur_max)) == -1)
            goto Jsinglebyte;
         cp += sz;
         if ((sz = mbtowc(&c2, str, n_mb_cur_max)) == -1)
            goto Jsinglebyte;
         str += sz;
         c = towupper(c);
         c2 = towupper(c2);
         if (c != c2) {
            if ((sz = mbtowc(&c, backup, n_mb_cur_max)) > 0) {
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
   n_NYD_OU;
   return (*cp == '\0');
}

FL char *
sstpcpy(char *dst, char const *src)
{
   n_NYD2_IN;
   while ((*dst = *src++) != '\0')
      ++dst;
   n_NYD2_OU;
   return dst;
}

FL char *
(sstrdup)(char const *cp  su_DBG_LOC_ARGS_DECL)
{
   char *dp;
   n_NYD2_IN;

   dp = (cp == NULL) ? NULL : (sbufdup)(cp, strlen(cp)  su_DBG_LOC_ARGS_USE);
   n_NYD2_OU;
   return dp;
}

FL char *
(sbufdup)(char const *cp, size_t len  su_DBG_LOC_ARGS_DECL)
{
   char *dp = NULL;
   n_NYD2_IN;

   dp = su_MEM_ALLOC_LOCOR(len +1, su_DBG_LOC_ARGS_ORUSE);
   if (cp != NULL)
      memcpy(dp, cp, len);
   dp[len] = '\0';
   n_NYD2_OU;
   return dp;
}

FL ssize_t
n_strscpy(char *dst, char const *src, size_t dstsize){
   ssize_t rv;
   n_NYD2_IN;

   if(n_LIKELY(dstsize > 0)){
      rv = 0;
      do{
         if((dst[rv] = src[rv]) == '\0')
            goto jleave;
         ++rv;
      }while(--dstsize > 0);
      dst[--rv] = '\0';
   }
#ifdef mx_HAVE_DEVEL
   else
      assert(dstsize > 0);
#endif
   rv = -1;
jleave:
   n_NYD2_OU;
   return rv;
}

FL int
asccasecmp(char const *s1, char const *s2)
{
   int cmp;
   n_NYD2_IN;

   for (;;) {
      char c1 = *s1++, c2 = *s2++;
      if ((cmp = lowerconv(c1) - lowerconv(c2)) != 0 || c1 == '\0')
         break;
   }
   n_NYD2_OU;
   return cmp;
}

FL int
ascncasecmp(char const *s1, char const *s2, size_t sz)
{
   int cmp = 0;
   n_NYD2_IN;

   while (sz-- > 0) {
      char c1 = *s1++, c2 = *s2++;
      cmp = (ui8_t)lowerconv(c1);
      cmp -= (ui8_t)lowerconv(c2);
      if (cmp != 0 || c1 == '\0')
         break;
   }
   n_NYD2_OU;
   return cmp;
}

FL char const *
asccasestr(char const *s1, char const *s2)
{
   char c2, c1;
   n_NYD2_IN;

   for (c2 = *s2++, c2 = lowerconv(c2);;) {
      if ((c1 = *s1++) == '\0') {
         s1 = NULL;
         break;
      }
      if (lowerconv(c1) == c2 && is_asccaseprefix(s2, s1)) {
         --s1;
         break;
      }
   }
   n_NYD2_OU;
   return s1;
}

FL bool_t
is_asccaseprefix(char const *as1, char const *as2) /* TODO arg order */
{
   char c1, c2;
   n_NYD2_IN;

   for(;; ++as1, ++as2){
      c1 = *as1;
      c1 = lowerconv(c1);
      c2 = *as2;
      c2 = lowerconv(c2);

      if(c1 != c2 || c1 == '\0')
         break;
      if(c2 == '\0')
         break;
   }
   n_NYD2_OU;
   return (c1 == '\0');
}

FL bool_t
is_ascncaseprefix(char const *as1, char const *as2, size_t sz)
{
   char c1, c2;
   bool_t rv;
   n_NYD2_IN;

   for(rv = TRU1; sz-- > 0; ++as1, ++as2){
      c1 = *as1;
      c1 = lowerconv(c1);
      c2 = *as2;
      c2 = lowerconv(c2);

      if(!(rv = (c1 == c2)) || c1 == '\0')
         break;
      if(c2 == '\0')
         break;
   }
   n_NYD2_OU;
   return rv;
}


FL struct str *
(n_str_assign_buf)(struct str *self, char const *buf, uiz_t buflen
      su_DBG_LOC_ARGS_DECL){
   n_NYD_IN;
   if(buflen == UIZ_MAX)
      buflen = (buf == NULL) ? 0 : strlen(buf);

   assert(buflen == 0 || buf != NULL);

   if(n_LIKELY(buflen > 0)){
      self->s = su_MEM_REALLOC_LOCOR(self->s, (self->l = buflen) +1,
            su_DBG_LOC_ARGS_ORUSE);
      memcpy(self->s, buf, buflen);
      self->s[buflen] = '\0';
   }else
      self->l = 0;
   n_NYD_OU;
   return self;
}

FL struct str *
(n_str_add_buf)(struct str *self, char const *buf, uiz_t buflen
      su_DBG_LOC_ARGS_DECL){
   n_NYD_IN;
   if(buflen == UIZ_MAX)
      buflen = (buf == NULL) ? 0 : strlen(buf);

   assert(buflen == 0 || buf != NULL);

   if(buflen > 0) {
      size_t osl = self->l, nsl = osl + buflen;

      self->s = su_MEM_REALLOC_LOCOR(self->s, (self->l = nsl) +1,
            su_DBG_LOC_ARGS_ORUSE);
      memcpy(self->s + osl, buf, buflen);
      self->s[nsl] = '\0';
   }
   n_NYD_OU;
   return self;
}

FL struct str *
n_str_trim(struct str *self, enum n_str_trim_flags stf){
   size_t l;
   char const *cp;
   n_NYD2_IN;

   cp = self->s;

   if((l = self->l) > 0 && (stf & n_STR_TRIM_FRONT)){
      while(spacechar(*cp)){
         ++cp;
         if(--l == 0)
            break;
      }
      self->s = n_UNCONST(cp);
   }

   if(l > 0 && (stf & n_STR_TRIM_END)){
      for(cp += l -1; spacechar(*cp); --cp)
         if(--l == 0)
            break;
   }
   self->l = l;

   n_NYD2_OU;
   return self;
}

FL struct str *
n_str_trim_ifs(struct str *self, bool_t dodefaults){
   char s, t, n, c;
   char const *ifs, *cp;
   size_t l, i;
   n_NYD2_IN;

   if((l = self->l) == 0)
      goto jleave;

   ifs = ok_vlook(ifs_ws);
   cp = self->s;
   s = t = n = '\0';

   /* Check whether we can go fast(er) path */
   for(i = 0; (c = ifs[i]) != '\0'; ++i){
      switch(c){
      case ' ': s = c; break;
      case '\t': t = c; break;
      case '\n': n = c; break;
      default:
         /* Need to go the slow path */
         while(strchr(ifs, *cp) != NULL){
            ++cp;
            if(--l == 0)
               break;
         }
         self->s = n_UNCONST(cp);

         if(l > 0){
            for(cp += l -1; strchr(ifs, *cp) != NULL;){
               if(--l == 0)
                  break;
               /* An uneven number of reverse solidus escapes last WS! */
               else if(*--cp == '\\'){
                  siz_t j;

                  for(j = 1; l - (uiz_t)j > 0 && cp[-j] == '\\'; ++j)
                     ;
                  if(j & 1){
                     ++l;
                     break;
                  }
               }
            }
         }
         self->l = l;

         if(!dodefaults)
            goto jleave;
         cp = self->s;
         ++i;
         break;
      }
   }

   /* No ifs-ws?  No more data?  No trimming */
   if(l == 0 || (i == 0 && !dodefaults))
      goto jleave;

   if(dodefaults){
      s = ' ';
      t = '\t';
      n = '\n';
   }

   if(l > 0){
      while((c = *cp) != '\0' && (c == s || c == t || c == n)){
         ++cp;
         if(--l == 0)
            break;
      }
      self->s = n_UNCONST(cp);
   }

   if(l > 0){
      for(cp += l -1; (c = *cp) != '\0' && (c == s || c == t || c == n);){
         if(--l == 0)
            break;
         /* An uneven number of reverse solidus escapes last WS! */
         else if(*--cp == '\\'){
            siz_t j;

            for(j = 1; l - (uiz_t)j > 0 && cp[-j] == '\\'; ++j)
               ;
            if(j & 1){
               ++l;
               break;
            }
         }
      }
   }
   self->l = l;
jleave:
   n_NYD2_OU;
   return self;
}

/*
 * struct n_string TODO extend, optimize
 */

FL struct n_string *
(n_string_clear)(struct n_string *self su_DBG_LOC_ARGS_DECL){
   n_NYD_IN;

   assert(self != NULL);

   if(self->s_size != 0){
      if(!self->s_auto)
         su_MEM_FREE_LOCOR(self->s_dat, su_DBG_LOC_ARGS_ORUSE);
      self->s_len = self->s_auto = self->s_size = 0;
      self->s_dat = NULL;
   }
   n_NYD_OU;
   return self;
}

FL struct n_string *
(n_string_reserve)(struct n_string *self, size_t noof  su_DBG_LOC_ARGS_DECL){
   ui32_t i, l, s;
   n_NYD_IN;
   assert(self != NULL);

   s = self->s_size;
   l = self->s_len;
   if((size_t)SI32_MAX - n_ALIGN(1) - l <= noof)
      n_panic(_("Memory allocation too large"));

   if((i = s - l) <= ++noof){
      i += l + (ui32_t)noof;
      i = n_ALIGN(i);
      self->s_size = i -1;

      if(!self->s_auto)
         self->s_dat = su_MEM_REALLOC_LOCOR(self->s_dat, i,
               su_DBG_LOC_ARGS_ORUSE);
      else{
         char *ndat;

         ndat = su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(i, su_DBG_LOC_ARGS_ORUSE);
         if(l > 0)
            memcpy(ndat, self->s_dat, l);
         self->s_dat = ndat;
      }
   }
   n_NYD_OU;
   return self;
}

FL struct n_string *
(n_string_resize)(struct n_string *self, size_t nlen  su_DBG_LOC_ARGS_DECL){
   n_NYD_IN;
   assert(self != NULL);

   if(UICMP(z, SI32_MAX, <=, nlen))
      n_panic(_("Memory allocation too large"));

   if(self->s_len < nlen)
      self = (n_string_reserve)(self, nlen  su_DBG_LOC_ARGS_USE);
   self->s_len = (ui32_t)nlen;
   n_NYD_OU;
   return self;
}

FL struct n_string *
(n_string_push_buf)(struct n_string *self, char const *buf, size_t buflen
      su_DBG_LOC_ARGS_DECL){
   n_NYD_IN;

   assert(self != NULL);
   assert(buflen == 0 || buf != NULL);

   if(buflen == UIZ_MAX)
      buflen = (buf == NULL) ? 0 : strlen(buf);

   if(buflen > 0){
      ui32_t i;

      self = (n_string_reserve)(self, buflen  su_DBG_LOC_ARGS_USE);
      memcpy(&self->s_dat[i = self->s_len], buf, buflen);
      self->s_len = (i += (ui32_t)buflen);
   }
   n_NYD_OU;
   return self;
}

FL struct n_string *
(n_string_push_c)(struct n_string *self, char c  su_DBG_LOC_ARGS_DECL){
   n_NYD_IN;

   assert(self != NULL);

   if(self->s_len + 1 >= self->s_size)
      self = (n_string_reserve)(self, 1  su_DBG_LOC_ARGS_USE);
   self->s_dat[self->s_len++] = c;
   n_NYD_OU;
   return self;
}

FL struct n_string *
(n_string_unshift_buf)(struct n_string *self, char const *buf, size_t buflen
      su_DBG_LOC_ARGS_DECL){
   n_NYD_IN;

   assert(self != NULL);
   assert(buflen == 0 || buf != NULL);

   if(buflen == UIZ_MAX)
      buflen = (buf == NULL) ? 0 : strlen(buf);

   if(buflen > 0){
      self = (n_string_reserve)(self, buflen  su_DBG_LOC_ARGS_USE);
      if(self->s_len > 0)
         memmove(&self->s_dat[buflen], self->s_dat, self->s_len);
      memcpy(self->s_dat, buf, buflen);
      self->s_len += (ui32_t)buflen;
   }
   n_NYD_OU;
   return self;
}

FL struct n_string *
(n_string_unshift_c)(struct n_string *self, char c  su_DBG_LOC_ARGS_DECL){
   n_NYD_IN;

   assert(self != NULL);

   if(self->s_len + 1 >= self->s_size)
      self = (n_string_reserve)(self, 1  su_DBG_LOC_ARGS_USE);
   if(self->s_len > 0)
      memmove(&self->s_dat[1], self->s_dat, self->s_len);
   self->s_dat[0] = c;
   ++self->s_len;
   n_NYD_OU;
   return self;
}

FL struct n_string *
(n_string_insert_buf)(struct n_string *self, size_t idx,
      char const *buf, size_t buflen  su_DBG_LOC_ARGS_DECL){
   n_NYD_IN;

   assert(self != NULL);
   assert(buflen == 0 || buf != NULL);
   assert(idx <= self->s_len);

   if(buflen == UIZ_MAX)
      buflen = (buf == NULL) ? 0 : strlen(buf);

   if(buflen > 0){
      self = (n_string_reserve)(self, buflen  su_DBG_LOC_ARGS_USE);
      if(self->s_len > 0)
         memmove(&self->s_dat[idx + buflen], &self->s_dat[idx],
            self->s_len - idx);
      memcpy(&self->s_dat[idx], buf, buflen);
      self->s_len += (ui32_t)buflen;
   }
   n_NYD_OU;
   return self;
}

FL struct n_string *
(n_string_insert_c)(struct n_string *self, size_t idx,
      char c  su_DBG_LOC_ARGS_DECL){
   n_NYD_IN;

   assert(self != NULL);
   assert(idx <= self->s_len);

   if(self->s_len + 1 >= self->s_size)
      self = (n_string_reserve)(self, 1  su_DBG_LOC_ARGS_USE);
   if(self->s_len > 0)
      memmove(&self->s_dat[idx + 1], &self->s_dat[idx], self->s_len - idx);
   self->s_dat[idx] = c;
   ++self->s_len;
   n_NYD_OU;
   return self;
}

FL struct n_string *
n_string_cut(struct n_string *self, size_t idx, size_t len){
   n_NYD_IN;

   assert(self != NULL);
   assert(UIZ_MAX - idx > len);
   assert(SI32_MAX >= idx + len);
   assert(idx + len <= self->s_len);

   if(len > 0)
      memmove(&self->s_dat[idx], &self->s_dat[idx + len],
         (self->s_len -= len) - idx);
   n_NYD_OU;
   return self;
}

FL char *
(n_string_cp)(struct n_string *self  su_DBG_LOC_ARGS_DECL){
   char *rv;
   n_NYD2_IN;

   assert(self != NULL);

   if(self->s_size == 0)
      self = (n_string_reserve)(self, 1  su_DBG_LOC_ARGS_USE);

   (rv = self->s_dat)[self->s_len] = '\0';
   n_NYD2_OU;
   return rv;
}

FL char const *
n_string_cp_const(struct n_string const *self){
   char const *rv;
   n_NYD2_IN;

   assert(self != NULL);

   if(self->s_size != 0){
      ((struct n_string*)n_UNCONST(self))->s_dat[self->s_len] = '\0';
      rv = self->s_dat;
   }else
      rv = n_empty;
   n_NYD2_OU;
   return rv;
}

/*
 * UTF-8
 */

FL ui32_t
n_utf8_to_utf32(char const **bdat, size_t *blen){
   ui32_t c, x, x1;
   char const *cp, *cpx;
   size_t l, lx;
   n_NYD2_IN;

   lx = l = *blen - 1;
   x = (ui8_t)*(cp = *bdat);
   cpx = ++cp;

   if(n_LIKELY(x <= 0x7Fu))
      c = x;
   /* 0xF8, but Unicode guarantees maximum of 0x10FFFFu -> F4 8F BF BF.
    * Unicode 9.0, 3.9, UTF-8, Table 3-7. Well-Formed UTF-8 Byte Sequences */
   else if(n_LIKELY(x > 0xC0u && x <= 0xF4u)){
      if(n_LIKELY(x < 0xE0u)){
         if(n_UNLIKELY(l < 1))
            goto jenobuf;
         --l;

         c = (x &= 0x1Fu);
      }else if(n_LIKELY(x < 0xF0u)){
         if(n_UNLIKELY(l < 2))
            goto jenobuf;
         l -= 2;

         x1 = x;
         c = (x &= 0x0Fu);

         /* Second byte constraints */
         x = (ui8_t)*cp++;
         switch(x1){
         case 0xE0u:
            if(n_UNLIKELY(x < 0xA0u || x > 0xBFu))
               goto jerr;
            break;
         case 0xEDu:
            if(n_UNLIKELY(x < 0x80u || x > 0x9Fu))
               goto jerr;
            break;
         default:
            if(n_UNLIKELY((x & 0xC0u) != 0x80u))
               goto jerr;
            break;
         }
         c <<= 6;
         c |= (x &= 0x3Fu);
      }else{
         if(n_UNLIKELY(l < 3))
            goto jenobuf;
         l -= 3;

         x1 = x;
         c = (x &= 0x07u);

         /* Second byte constraints */
         x = (ui8_t)*cp++;
         switch(x1){
         case 0xF0u:
            if(n_UNLIKELY(x < 0x90u || x > 0xBFu))
               goto jerr;
            break;
         case 0xF4u:
            if(n_UNLIKELY((x & 0xF0u) != 0x80u)) /* 80..8F */
               goto jerr;
            break;
         default:
            if(n_UNLIKELY((x & 0xC0u) != 0x80u))
               goto jerr;
            break;
         }
         c <<= 6;
         c |= (x &= 0x3Fu);

         x = (ui8_t)*cp++;
         if(n_UNLIKELY((x & 0xC0u) != 0x80u))
            goto jerr;
         c <<= 6;
         c |= (x &= 0x3Fu);
      }

      x = (ui8_t)*cp++;
      if(n_UNLIKELY((x & 0xC0u) != 0x80u))
         goto jerr;
      c <<= 6;
      c |= x & 0x3Fu;
   }else
      goto jerr;

   cpx = cp;
   lx = l;
jleave:
   *bdat = cpx;
   *blen = lx;
   n_NYD2_OU;
   return c;
jenobuf:
jerr:
   c = UI32_MAX;
   goto jleave;
}

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
   buf[3] = (char)0x80u | (char)(c & 0x3Fu); c >>= 6;
j3:
   buf[2] = (char)0x80u | (char)(c & 0x3Fu); c >>= 6;
j2:
   buf[1] = (char)0x80u | (char)(c & 0x3Fu); c >>= 6;
j1:
   buf[0] = (char)catp->enc_leader | (char)(c);
j0:
   buf[catp->enc_lval] = '\0';
   l = catp->enc_lval;
   n_NYD2_OU;
   return l;
}

/*
 * Our iconv(3) wrapper
 */

FL char *
n_iconv_normalize_name(char const *cset){
   char *cp, c, *tcp, tc;
   bool_t any;
   n_NYD2_IN;

   /* We need to strip //SUFFIXes off, we want to normalize to all lowercase,
    * and we perform some slight content testing, too */
   for(any = FAL0, cp = n_UNCONST(cset); (c = *cp) != '\0'; ++cp){
      if(!alnumchar(c) && !punctchar(c)){
         n_err(_("Invalid character set name %s\n"),
            n_shexp_quote_cp(cset, FAL0));
         cset = NULL;
         goto jleave;
      }else if(c == '/')
         break;
      else if(upperchar(c))
         any = TRU1;
   }

   if(any || c != '\0'){
      cp = savestrbuf(cset, PTR2SIZE(cp - cset));
      for(tcp = cp; (tc = *tcp) != '\0'; ++tcp)
         *tcp = lowerconv(tc);

      if(c != '\0' && (n_poption & n_PO_D_V))
         n_err(_("Stripped off character set suffix: %s -> %s\n"),
            n_shexp_quote_cp(cset, FAL0), n_shexp_quote_cp(cp, FAL0));

      cset = cp;
   }
jleave:
   n_NYD2_OU;
   return n_UNCONST(cset);
}

FL bool_t
n_iconv_name_is_ascii(char const *cset){ /* TODO ctext/su */
   /* In reversed MIME preference order */
   static char const * const names[] = {"csASCII", "cp367", "IBM367", "us",
         "ISO646-US", "ISO_646.irv:1991", "ANSI_X3.4-1986", "iso-ir-6",
         "ANSI_X3.4-1968", "ASCII", "US-ASCII"};
   bool_t rv;
   char const * const *npp;
   n_NYD2_IN;

   npp = &names[n_NELEM(names)];
   do if((rv = !asccasecmp(cset, *--npp)))
      break;
   while((rv = (npp != &names[0])));
   n_NYD2_OU;
   return rv;
}

#ifdef mx_HAVE_ICONV
FL iconv_t
n_iconv_open(char const *tocode, char const *fromcode){
   iconv_t id;
   n_NYD_IN;

   if((!asccasecmp(fromcode, "unknown-8bit") ||
            !asccasecmp(fromcode, "binary")) &&
         (fromcode = ok_vlook(charset_unknown_8bit)) == NULL)
      fromcode = ok_vlook(CHARSET_8BIT_OKEY);

   id = iconv_open(tocode, fromcode);

   /* If the encoding names are equal at this point, they are just not
    * understood by iconv(), and we cannot sensibly use it in any way.  We do
    * not perform this as an optimization above since iconv() can otherwise be
    * used to check the validity of the input even with identical encoding
    * names */
   if (id == (iconv_t)-1 && !asccasecmp(tocode, fromcode))
      n_err_no = n_ERR_NONE;
   n_NYD_OU;
   return id;
}

FL void
n_iconv_close(iconv_t cd){
   n_NYD_IN;
   iconv_close(cd);
   if(cd == iconvd)
      iconvd = (iconv_t)-1;
   n_NYD_OU;
}

FL void
n_iconv_reset(iconv_t cd){
   n_NYD_IN;
   iconv(cd, NULL, NULL, NULL, NULL);
   n_NYD_OU;
}

/* (2012-09-24: export and use it exclusively to isolate prototype problems
 * (*inb* is 'char const **' except in POSIX) in a single place.
 * GNU libiconv even allows for configuration time const/non-const..
 * In the end it's an ugly guess, but we can't do better since make(1) doesn't
 * support compiler invocations which bail on error, so no -Werror */
/* Citrus project? */
# if defined _ICONV_H_ && defined __ICONV_F_HIDE_INVALID
  /* DragonFly 3.2.1 is special TODO newer DragonFly too, but different */
#  if n_OS_DRAGONFLY
#   define __INBCAST(S) (char ** __restrict__)n_UNCONST(S)
#  else
#   define __INBCAST(S) (char const **)n_UNCONST(S)
#  endif
# elif n_OS_SUNOS || n_OS_SOLARIS
#  define __INBCAST(S) (char const ** __restrict__)n_UNCONST(S)
# endif
# ifndef __INBCAST
#  define __INBCAST(S)  (char **)n_UNCONST(S)
# endif

FL int
n_iconv_buf(iconv_t cd, enum n_iconv_flags icf,
   char const **inb, size_t *inbleft, char **outb, size_t *outbleft){
   int err;
   n_NYD2_IN;

   if((icf & n_ICONV_UNIREPL) && !(n_psonce & n_PSO_UNICODE))
      icf &= ~n_ICONV_UNIREPL;

   for(;;){
      size_t sz;

      if((sz = iconv(cd, __INBCAST(inb), inbleft, outb, outbleft)) == 0)
         break;
      if(sz != (size_t)-1){
         if(!(icf & n_ICONV_IGN_NOREVERSE)){
            err = n_ERR_NOENT;
            goto jleave;
         }
         break;
      }

      if((err = n_err_no) == n_ERR_2BIG)
         goto jleave;

      if(!(icf & n_ICONV_IGN_ILSEQ) || err != n_ERR_ILSEQ)
         goto jleave;
      if(*inbleft > 0){
         ++(*inb);
         --(*inbleft);
         if(icf & n_ICONV_UNIREPL){
            if(*outbleft >= sizeof(n_unirepl) -1){
               memcpy(*outb, n_unirepl, sizeof(n_unirepl) -1);
               *outb += sizeof(n_unirepl) -1;
               *outbleft -= sizeof(n_unirepl) -1;
               continue;
            }
         }else if(*outbleft > 0){
            *(*outb)++ = '?';
            --*outbleft;
            continue;
         }
         err = n_ERR_2BIG;
         goto jleave;
      }else if(*outbleft > 0){
         **outb = '\0';
         goto jleave;
      }
   }
   err = 0;
jleave:
   n_iconv_err_no = err;
   n_NYD2_OU;
   return err;
}
# undef __INBCAST

FL int
n_iconv_str(iconv_t cd, enum n_iconv_flags icf,
      struct str *out, struct str const *in, struct str *in_rest_or_null){
   struct n_string s, *sp = &s;
   char const *ib;
   int err;
   size_t il;
   n_NYD2_IN;

   il = in->l;
   if(!n_string_get_can_book(il) || !n_string_get_can_book(out->l)){
      err = n_ERR_INVAL;
      goto j_leave;
   }
   ib = in->s;

   sp = n_string_creat(sp);
   sp = n_string_take_ownership(sp, out->s, out->l, 0);

   for(;;){
      char *ob_base, *ob;
      size_t ol, nol;

      if((nol = ol = sp->s_len) < il)
         nol = il;
      assert(sizeof(sp->s_len) == sizeof(ui32_t));
      if(nol < 128)
         nol += 32;
      else{
         ui64_t xnol;

         xnol = (ui64_t)(nol << 1) - (nol >> 4);
         if(!n_string_can_book(sp, xnol)){
            xnol = ol + 64;
            if(!n_string_can_book(sp, xnol)){
               err = n_ERR_INVAL;
               goto jleave;
            }
         }
         nol = (size_t)xnol;
      }
      sp = n_string_resize(sp, nol);

      ob = ob_base = &sp->s_dat[ol];
      nol -= ol;
      err = n_iconv_buf(cd, icf, &ib, &il, &ob, &nol);

      sp = n_string_trunc(sp, ol + PTR2SIZE(ob - ob_base));
      if(err == 0 || err != n_ERR_2BIG)
         break;
   }

   if(in_rest_or_null != NULL){
      in_rest_or_null->s = n_UNCONST(ib);
      in_rest_or_null->l = il;
   }

jleave:
   out->s = n_string_cp(sp);
   out->l = sp->s_len;
   sp = n_string_drop_ownership(sp);
   /* n_string_gut(sp)*/
j_leave:
   n_NYD2_OU;
   return err;
}

FL char *
n_iconv_onetime_cp(enum n_iconv_flags icf,
      char const *tocode, char const *fromcode, char const *input){
   struct str out, in;
   iconv_t icd;
   char *rv;
   n_NYD2_IN;

   rv = NULL;
   if(tocode == NULL)
      tocode = ok_vlook(ttycharset);
   if(fromcode == NULL)
      fromcode = "utf-8";

   if((icd = iconv_open(tocode, fromcode)) == (iconv_t)-1)
      goto jleave;

   in.l = strlen(in.s = n_UNCONST(input)); /* logical */
   out.s = NULL, out.l = 0;
   if(!n_iconv_str(icd, icf, &out, &in, NULL))
      rv = savestrbuf(out.s, out.l);
   if(out.s != NULL)
      n_free(out.s);

   iconv_close(icd);
jleave:
   n_NYD2_OU;
   return rv;
}
#endif /* mx_HAVE_ICONV */

/* s-it-mode */
