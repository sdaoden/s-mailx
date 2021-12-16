/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ String support routines.
 *
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE strings
#define mx_SOURCE
#define mx_SOURCE_STRINGS

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>

#include "mx/compat.h"

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

static void a_cs_panic(void);

static void
a_cs_panic(void){
   n_panic(_("Memory allocation too large"));
}

FL char *
(savestr)(char const *str  su_DVL_LOC_ARGS_DECL)
{
   uz size;
   char *news;
   NYD_IN;

   size = su_cs_len(str);
   news = su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(size +1,  su_DVL_LOC_ARGS_ORUSE);
   if(size > 0)
      su_mem_copy(news, str, size);
   news[size] = '\0';
   NYD_OU;
   return news;
}

FL char *
(savestrbuf)(char const *sbuf, uz sbuf_len  su_DVL_LOC_ARGS_DECL)
{
   char *news;
   NYD_IN;

   news = su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(sbuf_len +1, su_DVL_LOC_ARGS_ORUSE);
   if(sbuf_len > 0)
      su_mem_copy(news, sbuf, sbuf_len);
   news[sbuf_len] = 0;
   NYD_OU;
   return news;
}

FL char *
(savecatsep)(char const *s1, char sep, char const *s2  su_DVL_LOC_ARGS_DECL)
{
   uz l1, l2;
   char *news;
   NYD_IN;

   l1 = (s1 != NULL) ? su_cs_len(s1) : 0;
   l2 = su_cs_len(s2);
   news = su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(l1 + (sep != '\0') + l2 +1,
         su_DVL_LOC_ARGS_ORUSE);
   if (l1 > 0) {
      su_mem_copy(news + 0, s1, l1);
      if (sep != '\0')
         news[l1++] = sep;
   }
   if(l2 > 0)
      su_mem_copy(news + l1, s2, l2);
   news[l1 + l2] = '\0';
   NYD_OU;
   return news;
}

/*
 * Support routines, auto-reclaimed storage
 */

FL struct str *
str_concat_csvl(struct str *self, ...) /* XXX onepass maybe better here */
{
   va_list vl;
   uz l;
   char const *cs;
   NYD_IN;

   va_start(vl, self);
   for (l = 0; (cs = va_arg(vl, char const*)) != NULL;)
      l += su_cs_len(cs);
   va_end(vl);

   self->l = l;
   self->s = n_autorec_alloc(l +1);

   va_start(vl, self);
   for (l = 0; (cs = va_arg(vl, char const*)) != NULL;) {
      uz i;

      i = su_cs_len(cs);
      if(i > 0){
         su_mem_copy(self->s + l, cs, i);
         l += i;
      }
   }
   self->s[l] = '\0';
   va_end(vl);
   NYD_OU;
   return self;
}

FL struct str *
(str_concat_cpa)(struct str *self, char const * const *cpa,
   char const *sep_o_null  su_DVL_LOC_ARGS_DECL)
{
   uz sonl, l;
   char const * const *xcpa;
   NYD_IN;

   sonl = (sep_o_null != NULL) ? su_cs_len(sep_o_null) : 0;

   for (l = 0, xcpa = cpa; *xcpa != NULL; ++xcpa)
      l += su_cs_len(*xcpa) + sonl;

   self->l = l;
   self->s = su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(l +1, su_DVL_LOC_ARGS_ORUSE);

   for (l = 0, xcpa = cpa; *xcpa != NULL; ++xcpa) {
      uz i;

      i = su_cs_len(*xcpa);
      if(i > 0){
         su_mem_copy(self->s + l, *xcpa, i);
         l += i;
      }
      if (sonl > 0) {
         su_mem_copy(self->s + l, sep_o_null, sonl);
         l += sonl;
      }
   }
   self->s[l] = '\0';
   NYD_OU;
   return self;
}

/*
 * Routines that are not related to auto-reclaimed storage follow.
 */

FL boole
n_re_could_be_one_buf(char const *buf, uz len){
   boole rv;
   NYD2_IN;

   rv = (su_cs_first_of_cbuf_cbuf(buf, len, "^[*+?|$", su_UZ_MAX
         ) != su_UZ_MAX);
   NYD2_OU;
   return rv;
}

FL struct str *
(n_str_assign_buf)(struct str *self, char const *buf, uz buflen
      su_DVL_LOC_ARGS_DECL){
   NYD_IN;
   if(buflen == UZ_MAX)
      buflen = (buf == NULL) ? 0 : su_cs_len(buf);

   ASSERT(buflen == 0 || buf != NULL);

   if(LIKELY(buflen > 0)){
      self->s = su_MEM_REALLOC_LOCOR(self->s, (self->l = buflen) +1,
            su_DVL_LOC_ARGS_ORUSE);
      su_mem_copy(self->s, buf, buflen);
      self->s[buflen] = '\0';
   }else
      self->l = 0;
   NYD_OU;
   return self;
}

FL struct str *
(n_str_add_buf)(struct str *self, char const *buf, uz buflen
      su_DVL_LOC_ARGS_DECL){
   NYD_IN;
   if(buflen == UZ_MAX)
      buflen = (buf == NULL) ? 0 : su_cs_len(buf);

   ASSERT(buflen == 0 || buf != NULL);

   if(buflen > 0) {
      uz osl = self->l, nsl = osl + buflen;

      self->s = su_MEM_REALLOC_LOCOR(self->s, (self->l = nsl) +1,
            su_DVL_LOC_ARGS_ORUSE);
      su_mem_copy(self->s + osl, buf, buflen);
      self->s[nsl] = '\0';
   }
   NYD_OU;
   return self;
}

FL struct str *
n_str_trim(struct str *self, enum n_str_trim_flags stf){
   uz l;
   char const *cp;
   NYD2_IN;

   cp = self->s;

   if((l = self->l) > 0 && (stf & n_STR_TRIM_FRONT)){
      while(su_cs_is_space(*cp)){
         ++cp;
         if(--l == 0)
            break;
      }
      self->s = n_UNCONST(cp);
   }

   if(l > 0 && (stf & n_STR_TRIM_END)){
      for(cp += l -1; su_cs_is_space(*cp); --cp)
         if(--l == 0)
            break;
   }
   self->l = l;

   NYD2_OU;
   return self;
}

FL struct str *
n_str_trim_ifs(struct str *self, boole dodefaults){
   char s, t, n, c;
   char const *ifs, *cp;
   uz l, i;
   NYD2_IN;

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
         while(su_cs_find_c(ifs, *cp) != NULL){
            ++cp;
            if(--l == 0)
               break;
         }
         self->s = n_UNCONST(cp);

         if(l > 0){
            for(cp += l -1; su_cs_find_c(ifs, *cp) != NULL;){
               if(--l == 0)
                  break;
               /* An uneven number of reverse solidus escapes last WS! */
               else if(*--cp == '\\'){
                  sz j;

                  for(j = 1; l - (uz)j > 0 && cp[-j] == '\\'; ++j)
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
            sz j;

            for(j = 1; l - (uz)j > 0 && cp[-j] == '\\'; ++j)
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
   NYD2_OU;
   return self;
}

/*
 * struct n_string TODO extend, optimize
 */

FL struct n_string *
n__string_clear(struct n_string *self){
   NYD_IN;
   ASSERT(self != NIL);
   ASSERT(self->s_dat != NIL);

   if(!self->s_auto)
      su_FREE(self->s_dat);
   self->s_dat = NIL;
   self->s_len = self->s_size = 0;

   NYD_OU;
   return self;
}

FL struct n_string *
(n_string_reserve)(struct n_string *self, uz noof  su_DVL_LOC_ARGS_DECL){
   u32 i, l, s;
   NYD_IN;
   ASSERT(self != NULL);

   s = self->s_size;
   l = self->s_len;
   if((uz)S32_MAX - Z_ALIGN(1) - l <= noof)
      a_cs_panic();

   if((i = s - l) <= ++noof){
      i += l + (u32)noof;
      i = Z_ALIGN(i);
      self->s_size = i -1;

      if(!self->s_auto)
         self->s_dat = su_MEM_REALLOC_LOCOR(self->s_dat, i,
               su_DVL_LOC_ARGS_ORUSE);
      else{
         char *ndat;

         ndat = su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(i, su_DVL_LOC_ARGS_ORUSE);
         if(l > 0)
            su_mem_copy(ndat, self->s_dat, l);
         self->s_dat = ndat;
      }
   }
   NYD_OU;
   return self;
}

FL struct n_string *
(n_string_resize)(struct n_string *self, uz nlen  su_DVL_LOC_ARGS_DECL){
   NYD_IN;
   ASSERT(self != NULL);

   if(UCMP(z, S32_MAX, <=, nlen))
      a_cs_panic();

   if(self->s_len < nlen)
      self = (n_string_reserve)(self, nlen  su_DVL_LOC_ARGS_USE);
   self->s_len = (u32)nlen;
   NYD_OU;
   return self;
}

FL struct n_string *
(n_string_push_buf)(struct n_string *self, char const *buf, uz buflen
      su_DVL_LOC_ARGS_DECL){
   NYD_IN;

   ASSERT(self != NULL);
   ASSERT(buflen == 0 || buf != NULL);

   if(buflen == UZ_MAX)
      buflen = (buf == NULL) ? 0 : su_cs_len(buf);

   if(buflen > 0){
      u32 i;

      self = (n_string_reserve)(self, buflen  su_DVL_LOC_ARGS_USE);
      su_mem_copy(&self->s_dat[i = self->s_len], buf, buflen);
      self->s_len = (i += (u32)buflen);
   }
   NYD_OU;
   return self;
}

FL struct n_string *
(n_string_push_c)(struct n_string *self, char c  su_DVL_LOC_ARGS_DECL){
   NYD_IN;

   ASSERT(self != NULL);

   if(self->s_len + 1 >= self->s_size)
      self = (n_string_reserve)(self, 1  su_DVL_LOC_ARGS_USE);
   self->s_dat[self->s_len++] = c;
   NYD_OU;
   return self;
}

FL struct n_string *
(n_string_unshift_buf)(struct n_string *self, char const *buf, uz buflen
      su_DVL_LOC_ARGS_DECL){
   NYD_IN;

   ASSERT(self != NULL);
   ASSERT(buflen == 0 || buf != NULL);

   if(buflen == UZ_MAX)
      buflen = (buf == NULL) ? 0 : su_cs_len(buf);

   if(buflen > 0){
      self = (n_string_reserve)(self, buflen  su_DVL_LOC_ARGS_USE);
      if(self->s_len > 0)
         su_mem_move(&self->s_dat[buflen], self->s_dat, self->s_len);
      su_mem_copy(self->s_dat, buf, buflen);
      self->s_len += (u32)buflen;
   }
   NYD_OU;
   return self;
}

FL struct n_string *
(n_string_unshift_c)(struct n_string *self, char c  su_DVL_LOC_ARGS_DECL){
   NYD_IN;

   ASSERT(self != NULL);

   if(self->s_len + 1 >= self->s_size)
      self = (n_string_reserve)(self, 1  su_DVL_LOC_ARGS_USE);
   if(self->s_len > 0)
      su_mem_move(&self->s_dat[1], self->s_dat, self->s_len);
   self->s_dat[0] = c;
   ++self->s_len;
   NYD_OU;
   return self;
}

FL struct n_string *
(n_string_insert_buf)(struct n_string *self, uz idx,
      char const *buf, uz buflen  su_DVL_LOC_ARGS_DECL){
   NYD_IN;

   ASSERT(self != NULL);
   ASSERT(buflen == 0 || buf != NULL);
   ASSERT(idx <= self->s_len);

   if(buflen == UZ_MAX)
      buflen = (buf == NULL) ? 0 : su_cs_len(buf);

   if(buflen > 0){
      self = (n_string_reserve)(self, buflen  su_DVL_LOC_ARGS_USE);
      if(self->s_len > 0)
         su_mem_move(&self->s_dat[idx + buflen], &self->s_dat[idx],
            self->s_len - idx);
      su_mem_copy(&self->s_dat[idx], buf, buflen);
      self->s_len += (u32)buflen;
   }
   NYD_OU;
   return self;
}

FL struct n_string *
(n_string_insert_c)(struct n_string *self, uz idx,
      char c  su_DVL_LOC_ARGS_DECL){
   NYD_IN;

   ASSERT(self != NULL);
   ASSERT(idx <= self->s_len);

   if(self->s_len + 1 >= self->s_size)
      self = (n_string_reserve)(self, 1  su_DVL_LOC_ARGS_USE);
   if(self->s_len > 0)
      su_mem_move(&self->s_dat[idx + 1], &self->s_dat[idx], self->s_len - idx);
   self->s_dat[idx] = c;
   ++self->s_len;
   NYD_OU;
   return self;
}

FL struct n_string *
n_string_cut(struct n_string *self, uz idx, uz len){
   NYD_IN;

   ASSERT(self != NULL);
   ASSERT(UZ_MAX - idx > len);
   ASSERT(S32_MAX >= idx + len);
   ASSERT(idx + len <= self->s_len);

   if(len > 0)
      su_mem_move(&self->s_dat[idx], &self->s_dat[idx + len],
         (self->s_len -= len) - idx);
   NYD_OU;
   return self;
}

FL char *
(n_string_cp)(struct n_string *self  su_DVL_LOC_ARGS_DECL){
   char *rv;
   NYD2_IN;

   ASSERT(self != NULL);

   if(self->s_size == 0)
      self = (n_string_reserve)(self, 1  su_DVL_LOC_ARGS_USE);

   (rv = self->s_dat)[self->s_len] = '\0';
   NYD2_OU;
   return rv;
}

FL char const *
n_string_cp_const(struct n_string const *self){
   char const *rv;
   NYD2_IN;

   ASSERT(self != NULL);

   if(self->s_size != 0){
      ((struct n_string*)n_UNCONST(self))->s_dat[self->s_len] = '\0';
      rv = self->s_dat;
   }else
      rv = n_empty;
   NYD2_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_STRINGS
/* s-it-mode */
