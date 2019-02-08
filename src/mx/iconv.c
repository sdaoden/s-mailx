/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ iconv(3) interface.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE iconv
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/utf.h>

#include "mx/iconv.h"

/* TODO fake */
#include "su/code-in.h"

FL char *
n_iconv_normalize_name(char const *cset){
   char *cp, c, *tcp, tc;
   bool_t any;
   n_NYD2_IN;

   /* We need to strip //SUFFIXes off, we want to normalize to all lowercase,
    * and we perform some slight content testing, too */
   for(any = FAL0, cp = n_UNCONST(cset); (c = *cp) != '\0'; ++cp){
      if(!su_cs_is_alnum(c) && !su_cs_is_punct(c)){
         n_err(_("Invalid character set name %s\n"),
            n_shexp_quote_cp(cset, FAL0));
         cset = NULL;
         goto jleave;
      }else if(c == '/')
         break;
      else if(su_cs_is_upper(c))
         any = TRU1;
   }

   if(any || c != '\0'){
      cp = savestrbuf(cset, PTR2SIZE(cp - cset));
      for(tcp = cp; (tc = *tcp) != '\0'; ++tcp)
         *tcp = su_cs_to_lower(tc);

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
   do if((rv = !su_cs_cmp_case(cset, *--npp)))
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

   if((!su_cs_cmp_case(fromcode, "unknown-8bit") ||
            !su_cs_cmp_case(fromcode, "binary")) &&
         (fromcode = ok_vlook(charset_unknown_8bit)) == NULL)
      fromcode = ok_vlook(CHARSET_8BIT_OKEY);

   id = iconv_open(tocode, fromcode);

   /* If the encoding names are equal at this point, they are just not
    * understood by iconv(), and we cannot sensibly use it in any way.  We do
    * not perform this as an optimization above since iconv() can otherwise be
    * used to check the validity of the input even with identical encoding
    * names */
   if (id == (iconv_t)-1 && !su_cs_cmp_case(tocode, fromcode))
      su_err_set_no(su_ERR_NONE);
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
      size_t i;

      if((i = iconv(cd, __INBCAST(inb), inbleft, outb, outbleft)) == 0)
         break;
      if(i != (size_t)-1){
         if(!(icf & n_ICONV_IGN_NOREVERSE)){
            err = su_ERR_NOENT;
            goto jleave;
         }
         break;
      }

      if((err = su_err_no()) == su_ERR_2BIG)
         goto jleave;

      if(!(icf & n_ICONV_IGN_ILSEQ) || err != su_ERR_ILSEQ)
         goto jleave;
      if(*inbleft > 0){
         ++(*inb);
         --(*inbleft);
         if(icf & n_ICONV_UNIREPL){
            if(*outbleft >= sizeof(su_utf8_replacer) -1){
               su_mem_copy(*outb, su_utf8_replacer,
                  sizeof(su_utf8_replacer) -1);
               *outb += sizeof(su_utf8_replacer) -1;
               *outbleft -= sizeof(su_utf8_replacer) -1;
               continue;
            }
         }else if(*outbleft > 0){
            *(*outb)++ = '?';
            --*outbleft;
            continue;
         }
         err = su_ERR_2BIG;
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
   struct n_string s_b, *s;
   char const *ib;
   int err;
   size_t il;
   n_NYD2_IN;

   il = in->l;
   if(!n_string_get_can_book(il) || !n_string_get_can_book(out->l)){
      err = su_ERR_INVAL;
      goto j_leave;
   }
   ib = in->s;

   s = n_string_creat(&s_b);
   s = n_string_take_ownership(s, out->s, out->l, 0);

   for(;;){
      char *ob_base, *ob;
      size_t ol, nol;

      if((nol = ol = s->s_len) < il)
         nol = il;
      assert(sizeof(s->s_len) == sizeof(ui32_t));
      if(nol < 128)
         nol += 32;
      else{
         ui64_t xnol;

         xnol = (ui64_t)(nol << 1) - (nol >> 4);
         if(!n_string_can_book(s, xnol)){
            xnol = ol + 64;
            if(!n_string_can_book(s, xnol)){
               err = su_ERR_INVAL;
               goto jleave;
            }
         }
         nol = (size_t)xnol;
      }
      s = n_string_resize(s, nol);

      ob = ob_base = &s->s_dat[ol];
      nol -= ol;
      err = n_iconv_buf(cd, icf, &ib, &il, &ob, &nol);

      s = n_string_trunc(s, ol + PTR2SIZE(ob - ob_base));
      if(err == 0 || err != su_ERR_2BIG)
         break;
   }

   if(in_rest_or_null != NULL){
      in_rest_or_null->s = n_UNCONST(ib);
      in_rest_or_null->l = il;
   }

jleave:
   out->s = n_string_cp(s);
   out->l = s->s_len;
   s = n_string_drop_ownership(s);
   /* n_string_gut(s)*/
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

   in.l = su_cs_len(in.s = n_UNCONST(input)); /* logical */
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

#include "su/code-ou.h"
/* s-it-mode */
