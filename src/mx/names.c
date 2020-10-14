/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of names.h.
 *@ XXX Use a su_cs_set for alternates stuff?
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause XXX ISC once yank stuff+ changed
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
#define su_FILE names
#define mx_SOURCE
#define mx_SOURCE_NAMES /* XXX a lie - it is rather n_ yet */

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/mem.h>
#include <su/sort.h>

#include "mx/compat.h"
#include "mx/go.h"
#include "mx/iconv.h"
#include "mx/mime.h"
#include "mx/mta-aliases.h"

#include "mx/names.h"
#include "su/code-in.h"

/* ..of a_nm_alias_dp.
 * We rely on resorting, and use has_key()...lookup() (a_nm_alias_expand()).
 * The value is a n_strlist*, which we manage directly (no toolbox).
 * name::n_name, after .sl_dat[.sl_len] one boole that indicates
 * recursion-allowed, thereafter name::n_fullname (empty if EQ n_name) */
#define a_NM_ALIAS_FLAGS (su_CS_DICT_POW2_SPACED |\
      su_CS_DICT_HEAD_RESORT | su_CS_DICT_AUTO_SHRINK | su_CS_DICT_ERR_PASS)
#define a_NM_ALIAS_TRESHOLD_SHIFT 2

/* ..of a_nm_a8s_dp */
#define a_NM_A8S_FLAGS (su_CS_DICT_CASE |\
      su_CS_DICT_AUTO_SHRINK | su_CS_DICT_ERR_PASS)
#define a_NM_A8S_TRESHOLD_SHIFT 2

static struct su_cs_dict *a_nm_alias_dp, a_nm_alias__d; /* XXX atexit gut()..*/
static struct su_cs_dict *a_nm_a8s_dp, a_nm_a8s__d; /* XXX .. (DVL()) */

/* Same name, while taking care for *allnet*? */
static boole a_nm_is_same_name(char const *n1, char const *n2,
      boole *isall_or_nil);

/* Mark all (!file, !pipe) nodes with the given name */
static struct mx_name *a_nm_namelist_mark_name(struct mx_name *np,
      char const *name);

/* Grab a single name (liberal name) */
static char const *a_nm_yankname(char const *ap, char *wbuf,
      char const *separators, int keepcomms);

/* Extraction multiplexer that splits an input line to names */
static struct mx_name *a_nm_extract1(char const *line, enum gfield ntype,
      char const *separators, boole keepcomms);

/* elide() helper */
static su_sz a_nm_elide_sort(void const *s1, void const *s2);

/* Recursively expand an alias name, adjust nlist for result and return it;
 * limit expansion to some fixed level.
 * metoo=*metoo*, logname=$LOGNAME == optimization */
static struct mx_name *a_nm_alias_expand(uz level, struct mx_name *nlist,
      char const *name, int ntype, boole metoo, char const *logname);

/* */
static struct n_strlist *a_nm_alias_dump(char const *cmdname, char const *key,
      void const *dat);

/* */
static struct n_strlist *a_nm_a8s_dump(char const *cmdname, char const *key,
      void const *dat);

static boole
a_nm_is_same_name(char const *n1, char const *n2, boole *isall_or_nil){
   char c1, c2, c1r, c2r;
   boole rv;
   NYD2_IN;

   rv = ok_blook(allnet);

   if(isall_or_nil != NIL)
      *isall_or_nil = rv;

   if(!rv)
      rv = !su_cs_cmp_case(n1, n2);
   else{
      for(;; ++n1, ++n2){
         c1 = *n1;
         c1 = su_cs_to_lower(c1);
         c1r = (c1 == '\0' || c1 == '@');
         c2 = *n2;
         c2 = su_cs_to_lower(c2);
         c2r = (c2 == '\0' || c2 == '@');

         if(c1r || c2r){
            rv = (c1r == c2r);
            break;
         }else if(c1 != c2){
            rv = FAL0;
            break;
         }
      }
   }

   NYD2_OU;
   return rv;
}

static struct mx_name *
a_nm_namelist_mark_name(struct mx_name *np, char const *name){
   struct mx_name *p;
   NYD2_IN;

   for(p = np; p != NULL; p = p->n_flink)
      if(!(p->n_type & GDEL) &&
            !(p->n_flags & (S(u32,S32_MIN) | mx_NAME_ADDRSPEC_ISFILE |
               mx_NAME_ADDRSPEC_ISPIPE)) &&
            a_nm_is_same_name(p->n_name, name, NIL))
         p->n_flags |= S(u32,S32_MIN);
   NYD2_OU;
   return np;
}

static char const *
a_nm_yankname(char const *ap, char *wbuf, char const *separators,
   int keepcomms)
{
   char const *cp;
   char *wp, c, inquote, lc, lastsp;
   NYD_IN;

   *(wp = wbuf) = '\0';

   /* Skip over intermediate list trash, as in ".org>  ,  <xy@zz.org>" */
   for (c = *ap; su_cs_is_blank(c) || c == ','; c = *++ap)
      ;
   if (c == '\0') {
      cp = NULL;
      goto jleave;
   }

   /* Parse a full name: TODO RFC 5322
    * - Keep everything in quotes, liberal handle *quoted-pair*s therein
    * - Skip entire (nested) comments
    * - In non-quote, non-comment, join adjacent space to a single SP
    * - Understand separators only in non-quote, non-comment context,
    *   and only if not part of a *quoted-pair* (XXX too liberal) */
   cp = ap;
   for (inquote = lc = lastsp = 0;; lc = c, ++cp) {
      c = *cp;
      if (c == '\0')
         break;
      if (c == '\\')
         goto jwpwc;
      if (c == '"') {
         if (lc != '\\')
            inquote = !inquote;
#if 0 /* TODO when doing real RFC 5322 parsers - why have i done this? */
         else
            --wp;
#endif
         goto jwpwc;
      }
      if (inquote || lc == '\\') {
jwpwc:
         *wp++ = c;
         lastsp = 0;
         continue;
      }
      if (c == '(') {
         ap = cp;
         cp = skip_comment(cp + 1);
         if (keepcomms)
            while (ap < cp)
               *wp++ = *ap++;
         --cp;
         lastsp = 0;
         continue;
      }
      if (su_cs_find_c(separators, c) != NULL)
         break;

      lc = lastsp;
      lastsp = su_cs_is_blank(c);
      if (!lastsp || !lc)
         *wp++ = c;
   }
   if (su_cs_is_blank(lc))
      --wp;

   *wp = '\0';
jleave:
   NYD_OU;
   return cp;
}

static struct mx_name *
a_nm_extract1(char const *line, enum gfield ntype, char const *seps,
      boole keepcomms){
   struct mx_name *headp, *tailp, *np;
   NYD_IN;

   headp = NULL;

   if(line == NULL || *line == '\0')
      ;
   else if(ntype & GNOT_A_LIST)
      /* Not GNULL_OK! (yet: see below) */
      headp = nalloc(line, ntype | GSKIN | GNOT_A_LIST);
   else{
      char const *cp;
      char *nbuf;

      nbuf = n_lofi_alloc(su_cs_len(cp = line) +1);

      for(tailp = headp;
            ((cp = a_nm_yankname(cp, nbuf, seps, keepcomms)) != NULL);){
         /* TODO Cannot set GNULL_OK because otherwise this software blows up.
          * TODO We will need a completely new way of reporting the errors of
          * TODO is_addr_invalid() ... */
         if((np = nalloc(nbuf, ntype /*| GNULL_OK*/)) != NULL){
            if((np->n_blink = tailp) != NULL)
               tailp->n_flink = np;
            else
               headp = np;
            tailp = np;
         }
      }

      n_lofi_free(nbuf);
   }

   NYD_OU;
   return headp;
}

static su_sz
a_nm_elide_sort(void const *s1, void const *s2){
   struct mx_name const *np1, *np2;
   su_sz rv;
   NYD2_IN;

   np1 = s1;
   np2 = s2;
   if(!(rv = su_cs_cmp_case(np1->n_name, np2->n_name))){
      LCTAV(GTO < GCC && GCC < GBCC);
      rv = (np1->n_type & (GTO | GCC | GBCC)) -
            (np2->n_type & (GTO | GCC | GBCC));
   }
   NYD2_OU;
   return rv;
}

static struct mx_name *
a_nm_alias_expand(uz level, struct mx_name *nlist, char const *name, int ntype,
      boole metoo, char const *logname){
   struct mx_name *np, *nlist_tail;
   char const *ccp;
   struct n_strlist const *slp, *slp_base, *slp_next;
   NYD2_IN;
   ASSERT_NYD(a_nm_alias_dp != NIL);
   ASSERT(mx_alias_is_valid_name(name));

   UNINIT(slp_base, NIL);

   if(UCMP(z, level++, ==, n_ALIAS_MAXEXP)){ /* TODO not a real error!! */
      n_err(_("alias: stopping recursion at depth %d\n"), n_ALIAS_MAXEXP);
      slp_next = NIL;
      ccp = name;
      goto jlinkin;
   }

   slp_next = slp_base =
   slp = S(struct n_strlist const*,su_cs_dict_lookup(a_nm_alias_dp, name));

   if(slp == NIL){
      ccp = name;
      goto jlinkin;
   }
   do{ /* while(slp != NIL); */
      slp_next = slp->sl_next;

      if(slp->sl_len == 0)
         continue;

      /* Cannot shadow itself.  Recursion allowed for target? */
      if(su_cs_cmp(name, slp->sl_dat) && slp->sl_dat[slp->sl_len + 1] != FAL0){
         /* For S-nail(1), the "alias" may *be* the sender in that a name
          * to a full address specification */
         nlist = a_nm_alias_expand(level, nlist, slp->sl_dat, ntype, metoo,
               logname);
         continue;
      }

      /* Here we should allow to expand to itself if only person in alias */
      if(metoo || slp_base->sl_next == NIL ||
            !a_nm_is_same_name(slp->sl_dat, logname, NIL)){
         /* Use .n_name if .n_fullname is not set */
         if(*(ccp = &slp->sl_dat[slp->sl_len + 2]) == '\0')
            ccp = slp->sl_dat;

jlinkin:
         if((np = n_extract_single(ccp, ntype | GFULL)) != NIL){
            if((nlist_tail = nlist) != NIL){ /* XXX su_list_push()! */
               while(nlist_tail->n_flink != NIL)
                  nlist_tail = nlist_tail->n_flink;
               nlist_tail->n_flink = np;
               np->n_blink = nlist_tail;
            }else
               nlist = np;
         }
      }
   }while((slp = slp_next) != NIL);

   NYD2_OU;
   return nlist;
}

static struct n_strlist *
a_nm_alias_dump(char const *cmdname, char const *key, void const *dat){
   struct n_string s_b, *s;
   struct n_strlist *slp;
   NYD2_IN;

   s = n_string_creat_auto(&s_b);
   s = n_string_resize(s, 511);
   s = n_string_trunc(s, VSTRUCT_SIZEOF(struct n_strlist,sl_dat)); /* gross */

   s = n_string_push_cp(s, cmdname);
   s = n_string_push_c(s, ' ');
   s = n_string_push_cp(s, key); /*n_shexp_quote_cp(key, TRU1); valid alias */

   for(slp = UNCONST(struct n_strlist*,dat); slp != NIL; slp = slp->sl_next){
      s = n_string_push_c(s, ' ');
      /* Use .n_fullname if available, fall back to .n_name */
      key = &slp->sl_dat[slp->sl_len + 2];
      if(*key == '\0')
         key = slp->sl_dat;
      s = n_string_push_cp(s, n_shexp_quote_cp(key, TRU1));
   }

   slp = C(struct n_strlist*,S(void const*,n_string_cp(s)));
   slp->sl_next = NIL;
   slp->sl_len = s->s_len - VSTRUCT_SIZEOF(struct n_strlist,sl_dat);

   NYD2_OU;
   return slp;
}

static struct n_strlist *
a_nm_a8s_dump(char const *cmdname, char const *key, void const *dat){
   /* XXX real strlist + str_to_fmt() */
   struct n_strlist *slp;
   uz kl;
   NYD2_IN;
   UNUSED(cmdname);
   UNUSED(dat);

   /*key = n_shexp_quote_cp(key, TRU1); plain address: not needed */
   kl = su_cs_len(key);

   slp = n_STRLIST_AUTO_ALLOC(kl +1);
   slp->sl_next = NIL;
   slp->sl_len = kl;
   su_mem_copy(slp->sl_dat, key, kl +1);
   NYD2_OU;
   return slp;
}

struct mx_name *
nalloc(char const *str, enum gfield ntype)
{
   struct n_addrguts ag;
   struct str in, out;
   struct mx_name *np;
   NYD_IN;
   ASSERT(!(ntype & GFULLEXTRA) || (ntype & GFULL) != 0);

   str = n_addrspec_with_guts(&ag, str, ntype);
   if(str == NULL){
      /* TODO this may not return NULL but for new-style callers */
      if(ntype & GNULL_OK){
         np = NULL;
         goto jleave;
      }
   }
   ntype &= ~(GNOT_A_LIST | GNULL_OK); /* (all this a hack is) */
   str = ag.ag_input; /* Take the possibly reordered thing */

   if (!(ag.ag_n_flags & mx_NAME_NAME_SALLOC)) {
      ag.ag_n_flags |= mx_NAME_NAME_SALLOC;
      np = n_autorec_alloc(sizeof(*np) + ag.ag_slen +1);
      su_mem_copy(np + 1, ag.ag_skinned, ag.ag_slen +1);
      ag.ag_skinned = (char*)(np + 1);
   } else
      np = n_autorec_alloc(sizeof *np);

   np->n_flink = NULL;
   np->n_blink = NULL;
   np->n_type = ntype;
   np->n_fullname = np->n_name = ag.ag_skinned;
   np->n_fullextra = NULL;
   np->n_flags = ag.ag_n_flags;

   if (ntype & GFULL) {
      if (ag.ag_ilen == ag.ag_slen
#ifdef mx_HAVE_IDNA
            && !(ag.ag_n_flags & mx_NAME_IDNA)
#endif
      )
         goto jleave;
      if (ag.ag_n_flags & mx_NAME_ADDRSPEC_ISFILEORPIPE)
         goto jleave;

      /* n_fullextra is only the complete name part without address.
       * Beware of "-r '<abc@def>'", don't treat that as FULLEXTRA */
      if ((ntype & GFULLEXTRA) && ag.ag_ilen > ag.ag_slen + 2) {
         uz s = ag.ag_iaddr_start, e = ag.ag_iaddr_aend, i;
         char const *cp;

         if (s == 0 || str[--s] != '<' || str[e++] != '>')
            goto jskipfullextra;

         i = ag.ag_ilen - e;
         in.s = su_LOFI_ALLOC(s + 1 + i +1);

         while(s > 0 && su_cs_is_blank(str[s - 1]))
            --s;
         su_mem_copy(in.s, str, s);
         if (i > 0) {
            in.s[s++] = ' ';
            while (su_cs_is_blank(str[e])) {
               ++e;
               if (--i == 0)
                  break;
            }
            if (i > 0)
               su_mem_copy(&in.s[s], &str[e], i);
         }
         s += i;
         in.s[in.l = s] = '\0';

         mx_mime_display_from_header(&in, &out,
            mx_MIME_DISPLAY_ICONV /* TODO | mx_MIME_DISPLAY_ISPRINT */);

         for (cp = out.s, i = out.l; i > 0 && su_cs_is_space(*cp); --i, ++cp)
            ;
         while (i > 0 && su_cs_is_space(cp[i - 1]))
            --i;
         np->n_fullextra = savestrbuf(cp, i);

         su_FREE(out.s);

         su_LOFI_FREE(in.s);
      }
jskipfullextra:

      /* n_fullname depends on IDNA conversion */
#ifdef mx_HAVE_IDNA
      if (!(ag.ag_n_flags & mx_NAME_IDNA)) {
#endif
         in.s = UNCONST(char*,str);
         in.l = ag.ag_ilen;
#ifdef mx_HAVE_IDNA
      } else {
         /* The domain name was IDNA and has been converted.  We also have to
          * ensure that the domain name in .n_fullname is replaced with the
          * converted version, since MIME doesn't perform encoding of addrs */
         /* TODO This definitily doesn't belong here! */
         uz l, lsuff;

         l = ag.ag_iaddr_start;
         lsuff = ag.ag_ilen - ag.ag_iaddr_aend;

         in.s = su_LOFI_ALLOC(l + ag.ag_slen + lsuff +1);

         su_mem_copy(in.s, str, l);
         su_mem_copy(in.s + l, ag.ag_skinned, ag.ag_slen);
         l += ag.ag_slen;
         su_mem_copy(in.s + l, str + ag.ag_iaddr_aend, lsuff);
         l += lsuff;
         in.s[l] = '\0';
         in.l = l;
      }
#endif

      mx_mime_display_from_header(&in, &out,
         mx_MIME_DISPLAY_ICONV /* TODO | mx_MIME_DISPLAY_ISPRINT */);
      np->n_fullname = savestr(out.s);
      su_FREE(out.s);

#ifdef mx_HAVE_IDNA
      if(ag.ag_n_flags & mx_NAME_IDNA)
         n_lofi_free(in.s);
#endif
   }

jleave:
   NYD_OU;
   return np;
}

struct mx_name *
nalloc_fcc(char const *file){
   struct mx_name *nnp;
   NYD_IN;

   nnp = n_autorec_alloc(sizeof *nnp);
   nnp->n_flink = nnp->n_blink = NULL;
   nnp->n_type = GBCC | GBCC_IS_FCC; /* xxx Bcc: <- namelist_vaporise_head */
   nnp->n_flags = mx_NAME_NAME_SALLOC | mx_NAME_SKINNED |
         mx_NAME_ADDRSPEC_ISFILE;
   nnp->n_fullname = nnp->n_name = savestr(file);
   nnp->n_fullextra = NULL;
   NYD_OU;
   return nnp;
}

struct mx_name *
ndup(struct mx_name *np, enum gfield ntype)
{
   struct mx_name *nnp;
   NYD_IN;

   if ((ntype & (GFULL | GSKIN)) && !(np->n_flags & mx_NAME_SKINNED)) {
      nnp = nalloc(np->n_name, ntype);
      goto jleave;
   }

   nnp = n_autorec_alloc(sizeof *np);
   nnp->n_flink = nnp->n_blink = NULL;
   nnp->n_type = ntype & ~(GFULL | GFULLEXTRA);
   nnp->n_flags = np->n_flags | mx_NAME_NAME_SALLOC;
   nnp->n_name = savestr(np->n_name);

   if(np->n_name == np->n_fullname || !(ntype & GFULL)){
      ASSERT((ntype & GFULL) || !(ntype & GFULLEXTRA));
      nnp->n_fullname = nnp->n_name;
      nnp->n_fullextra = NULL;
   }else{
      nnp->n_fullname = savestr(np->n_fullname);
      nnp->n_fullextra = (!(ntype & GFULLEXTRA) || np->n_fullextra == NIL)
            ? NIL : savestr(np->n_fullextra);
      nnp->n_type = ntype;
   }

jleave:
   NYD_OU;
   return nnp;
}

struct mx_name *
cat(struct mx_name *n1, struct mx_name *n2){
   struct mx_name *tail;
   NYD_IN;

   tail = n2;
   if(n1 == NULL)
      goto jleave;

   tail = n1;
   if(n2 == NULL || (n2->n_type & GDEL))
      goto jleave;

   while(tail->n_flink != NULL)
      tail = tail->n_flink;
   tail->n_flink = n2;
   n2->n_blink = tail;
   tail = n1;

jleave:
   NYD_OU;
   return tail;
}

struct mx_name *
n_namelist_dup(struct mx_name const *np, enum gfield ntype){
   struct mx_name *nlist, *xnp;
   NYD_IN;

   for(nlist = xnp = NULL; np != NULL; np = np->n_flink){
      struct mx_name *x;

      if(!(np->n_type & GDEL)){
         x = ndup(UNCONST(struct mx_name*,np), (np->n_type & ~GMASK) | ntype);
         if((x->n_blink = xnp) == NULL)
            nlist = x;
         else
            xnp->n_flink = x;
         xnp = x;
      }
   }
   NYD_OU;
   return nlist;
}

u32
count(struct mx_name const *np){
   u32 c;
   NYD_IN;

   for(c = 0; np != NIL; np = np->n_flink)
      if(!(np->n_type & GDEL))
         ++c;
   NYD_OU;
   return c;
}

u32
count_nonlocal(struct mx_name const *np){
   u32 c;
   NYD_IN;

   for(c = 0; np != NIL; np = np->n_flink)
      if(!(np->n_type & GDEL) &&
            !(np->n_flags & mx_NAME_ADDRSPEC_ISFILEORPIPE))
         ++c;
   NYD_OU;
   return c;
}

struct mx_name *
extract(char const *line, enum gfield ntype)
{
   struct mx_name *rv;
   NYD_IN;

   rv = a_nm_extract1(line, ntype, " \t,", 0);
   NYD_OU;
   return rv;
}

struct mx_name *
lextract(char const *line, enum gfield ntype)
{
   char *cp;
   struct mx_name *rv;
   NYD_IN;

   if(!(ntype & GSHEXP_PARSE_HACK) || !(expandaddr_to_eaf() & EAF_SHEXP_PARSE))
      cp = NULL;
   else{
      struct str sin;
      struct n_string s_b, *s;
      BITENUM_IS(u32,n_shexp_state) shs;

      n_autorec_relax_create();
      s = n_string_creat_auto(&s_b);
      sin.s = UNCONST(char*,line); /* logical */
      sin.l = UZ_MAX;
      shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG |
            n_SHEXP_PARSE_IGNORE_EMPTY | n_SHEXP_PARSE_QUOTE_AUTO_FIXED |
            n_SHEXP_PARSE_QUOTE_AUTO_DSQ), s, &sin, NULL);
      if(!(shs & n_SHEXP_STATE_ERR_MASK) && (shs & n_SHEXP_STATE_STOP)){
         line = cp = n_lofi_alloc(s->s_len +1);
         su_mem_copy(cp, n_string_cp(s), s->s_len +1);
      }else
         line = cp = su_NIL;
      n_autorec_relax_gut();
   }

   if(line == su_NIL)
      rv = su_NIL;
   else if((ntype & GNOT_A_LIST) || strpbrk(line, ",\"\\(<|"))
      rv = a_nm_extract1(line, ntype, ",", 1);
   else
      rv = extract(line, ntype);

   if(cp != su_NIL)
      n_lofi_free(cp);
   NYD_OU;
   return rv;
}

struct mx_name *
n_extract_single(char const *line, enum gfield ntype){
   struct mx_name *rv;
   NYD_IN;

   rv = nalloc(line, ntype | GSKIN | GNOT_A_LIST | GNULL_OK);
   NYD_OU;
   return rv;
}

char *
detract(struct mx_name *np, enum gfield ntype)
{
   char *topp, *cp;
   struct mx_name *p;
   int flags, s;
   NYD_IN;

   topp = NULL;
   if (np == NULL)
      goto jleave;

   flags = ntype & (GCOMMA | GNAMEONLY);
   ntype &= ~(GCOMMA | GNAMEONLY);
   s = 0;

   for (p = np; p != NULL; p = p->n_flink) {
      if (ntype && (p->n_type & GMASK) != ntype)
         continue;
      s += su_cs_len(flags & GNAMEONLY ? p->n_name : p->n_fullname) +1;
      if (flags & GCOMMA)
         ++s;
   }
   if (s == 0)
      goto jleave;

   s += 2;
   topp = n_autorec_alloc(s);
   cp = topp;
   for (p = np; p != NULL; p = p->n_flink) {
      if (ntype && (p->n_type & GMASK) != ntype)
         continue;
      cp = su_cs_pcopy(cp, (flags & GNAMEONLY ? p->n_name : p->n_fullname));
      if ((flags & GCOMMA) && p->n_flink != NULL)
         *cp++ = ',';
      *cp++ = ' ';
   }
   *--cp = 0;
   if ((flags & GCOMMA) && *--cp == ',')
      *cp = 0;
jleave:
   NYD_OU;
   return topp;
}

struct mx_name *
grab_names(u32/*mx_go_input_flags*/ gif, char const *field,
      struct mx_name *np, int comma, enum gfield gflags)
{
   struct mx_name *nq;
   NYD_IN;

jloop:
   np = lextract(mx_go_input_cp(gif, field, detract(np, comma)), gflags);
   for (nq = np; nq != NULL; nq = nq->n_flink)
      if (is_addr_invalid(nq, EACM_NONE))
         goto jloop;
   NYD_OU;
   return np;
}

boole
mx_name_is_same_address(struct mx_name const *n1, struct mx_name const *n2){
   boole isall, rv;
   NYD2_IN;

   rv = (a_nm_is_same_name(n1->n_name, n2->n_name, &isall) &&
         (isall || mx_name_is_same_domain(n1, n2)));

   NYD2_OU;
   return rv;
}

boole
mx_name_is_same_domain(struct mx_name const *n1, struct mx_name const *n2){
   boole rv;
   NYD2_IN;

   if((rv = (n1 != NIL && n2 != NIL))){
      char const *d1, *d2;

      d1 = su_cs_rfind_c(n1->n_name, '@');
      d2 = su_cs_rfind_c(n2->n_name, '@');

      rv = (d1 != NIL && d2 != NIL) ? !su_cs_cmp_case(++d1, ++d2) : FAL0;
   }

   NYD2_OU;
   return rv;
}

struct mx_name *
checkaddrs(struct mx_name *np, enum expand_addr_check_mode eacm,
   s8 *set_on_error)
{
   struct mx_name *n;
   NYD_IN;

   for (n = np; n != NULL; n = n->n_flink) {
      s8 rv;

      if ((rv = is_addr_invalid(n, eacm)) != 0) {
         if (set_on_error != NULL)
            *set_on_error |= rv; /* don't loose -1! */
         if (eacm & EAF_MAYKEEP) /* TODO HACK!  See definition! */
            continue;
         if (n->n_blink)
            n->n_blink->n_flink = n->n_flink;
         if (n->n_flink)
            n->n_flink->n_blink = n->n_blink;
         if (n == np)
            np = n->n_flink;
      }
   }
   NYD_OU;
   return np;
}

struct mx_name *
n_namelist_vaporise_head(struct header *hp, boole metoo,
   boole strip_alternates, enum expand_addr_check_mode eacm, s8 *set_on_error)
{
   /* TODO namelist_vaporise_head() is incredibly expensive and redundant */
   struct mx_name *tolist, *np, **npp;
   NYD_IN;

   tolist = cat(hp->h_to, cat(hp->h_cc, cat(hp->h_bcc, hp->h_fcc)));
   hp->h_to = hp->h_cc = hp->h_bcc = hp->h_fcc = NULL;

   tolist = usermap(tolist, metoo);

   /* MTA aliases are resolved last */
#ifdef mx_HAVE_MTA_ALIASES
   switch(mx_mta_aliases_expand(&tolist)){
   case su_ERR_DESTADDRREQ:
   case su_ERR_NONE:
   case su_ERR_NOENT:
      break;
   default:
      *set_on_error |= TRU1;
      break;
   }
#endif

   if(strip_alternates)
      tolist = mx_alternates_remove(tolist, TRU1);

   tolist = elide(tolist);

   tolist = checkaddrs(tolist, eacm, set_on_error);

   for (np = tolist; np != NULL; np = np->n_flink) {
      switch (np->n_type & (GDEL | GMASK)) {
      case GTO:   npp = &hp->h_to; break;
      case GCC:   npp = &hp->h_cc; break;
      case GBCC:  npp = &hp->h_bcc; break;
      default:    continue;
      }
      *npp = cat(*npp, ndup(np, np->n_type | GFULL));
   }

   NYD_OU;
   return tolist;
}

struct mx_name *
usermap(struct mx_name *names, boole force_metoo){
   struct su_cs_dict_view dv;
   struct mx_name *nlist, **nlist_tail, *np, *nxtnp;
   int metoo;
   char const *logname;
   NYD_IN;

   logname = ok_vlook(LOGNAME);
   metoo = (force_metoo || ok_blook(metoo));
   nlist = NIL;
   nlist_tail = &nlist;
   np = names;

   if(a_nm_alias_dp != NIL)
      su_cs_dict_view_setup(&dv, a_nm_alias_dp);

   for(; np != NULL; np = nxtnp){
      ASSERT(!(np->n_type & GDEL)); /* TODO legacy */
      nxtnp = np->n_flink;

      /* Only valid alias names may enter expansion; even so GFULL may cause
       * .n_fullname to be different memory, it will be bitwise equal) */
      if(is_fileorpipe_addr(np) || (np->n_name != np->n_fullname &&
               su_cs_cmp(np->n_name, np->n_fullname)) ||
            a_nm_alias_dp == NIL || !su_cs_dict_view_find(&dv, np->n_name)){
         np->n_blink = *nlist_tail;
         np->n_flink = NIL;
         *nlist_tail = np;
         nlist_tail = &np->n_flink;
      }else{
         nlist = a_nm_alias_expand(0, nlist, np->n_name, np->n_type, metoo,
               logname);
         if((np = nlist) == NIL)
            nlist_tail = &nlist;
         else for(;; np = np->n_flink)
            if(np->n_flink == NIL){
               nlist_tail = &np->n_flink;
               break;
            }
      }
   }

   NYD_OU;
   return nlist;
}

struct mx_name *
elide(struct mx_name *names)
{
   uz i, j, k;
   struct mx_name *nlist, *np, **nparr;
   NYD_IN;

   nlist = NULL;

   if(names == NULL)
      goto jleave;

   /* Throw away all deleted nodes */
   for(np = NULL, i = 0; names != NULL; names = names->n_flink)
      if(!(names->n_type & GDEL)){
         names->n_blink = np;
         if(np != NULL)
            np->n_flink = names;
         else
            nlist = names;
         np = names;
         ++i;
      }
   if(nlist == NULL || i == 1)
      goto jleave;
   np->n_flink = NULL;

   /* Create a temporary array and sort that */
   nparr = n_lofi_alloc(sizeof(*nparr) * i);

   for(i = 0, np = nlist; np != NULL; np = np->n_flink)
      nparr[i++] = np;

   su_sort_shell_vpp(su_S(void const**,nparr), i, &a_nm_elide_sort);

   /* Remove duplicates XXX speedup, or list_uniq()! */
   for(j = 0, --i; j < i;){
      if(su_cs_cmp_case(nparr[j]->n_name, nparr[k = j + 1]->n_name))
         ++j;
      else{
         for(; k < i; ++k)
            nparr[k] = nparr[k + 1];
         --i;
      }
   }

   /* Throw away all list members which are not part of the array.
    * Note this keeps the original, possibly carefully crafted, order of the
    * addressees, thus.. */
   for(np = nlist; np != NULL; np = np->n_flink){
      for(j = 0; j <= i; ++j)
         /* The order of pointers depends on the sorting algorithm, and
          * therefore our desire to keep the original order of addessees cannot
          * be guaranteed when there are multiple equal names (ham zebra ham)
          * of equal weight: we need to compare names _once_again_ :( */
         if(nparr[j] != NULL && !su_cs_cmp_case(np->n_name, nparr[j]->n_name)){
            nparr[j] = NULL;
            goto jiter;
         }
      /* Drop it */
      if(np == nlist){
         nlist = np->n_flink;
         np->n_blink = NULL;
      }else
         np->n_blink->n_flink = np->n_flink;
      if(np->n_flink != NULL)
         np->n_flink->n_blink = np->n_blink;
jiter:;
   }

   n_lofi_free(nparr);
jleave:
   NYD_OU;
   return nlist;
}

int
c_alias(void *vp){
   struct su_cs_dict_view dv;
   union {void const *cvp; boole haskey; struct n_strlist *slp;} dat;
   int rv;
   char const **argv, *key;
   NYD_IN;

   if((key = *(argv = S(char const**,vp))) == NIL){
      dat.slp = NIL;
      rv = !(mx_xy_dump_dict("alias", a_nm_alias_dp, &dat.slp, NIL,
               &a_nm_alias_dump) &&
            mx_page_or_print_strlist("alias", dat.slp, FAL0));
      goto jleave;
   }

   if(argv[1] != NIL && argv[2] == NIL && key[0] == '-' && key[1] == '\0')
      key = argv[1];

   if(!mx_alias_is_valid_name(key)){
      n_err(_("alias: not a valid name: %s\n"), n_shexp_quote_cp(key, FAL0));
      rv = 1;
      goto jleave;
   }

   if(a_nm_alias_dp != NIL && su_cs_dict_view_find(
            su_cs_dict_view_setup(&dv, a_nm_alias_dp), key))
      dat.cvp = su_cs_dict_view_data(&dv);
   else
      dat.cvp = NIL;

   if(argv[1] == NIL || key == argv[1]){
      if(dat.cvp != NIL){
         if(argv[1] == NIL){
            dat.slp = a_nm_alias_dump("alias", key, dat.cvp);
            rv = (fputs(dat.slp->sl_dat, n_stdout) == EOF);
            rv |= (putc('\n', n_stdout) == EOF);
         }else{
            struct mx_name *np;

            np = a_nm_alias_expand(0, NIL, key, 0, TRU1, ok_vlook(LOGNAME));
            np = elide(np);
            rv = (fprintf(n_stdout, "alias %s", key) < 0);
            if(!rv){
               for(; np != NIL; np = np->n_flink){
                  rv |= (putc(' ', n_stdout) == EOF);
                  rv |= (fputs(n_shexp_quote_cp(np->n_fullname, TRU1),
                        n_stdout) == EOF);
               }
               rv |= (putc('\n', n_stdout) == EOF);
            }
         }
      }else{
         n_err(_("No such alias: %s\n"), n_shexp_quote_cp(key, FAL0));
         rv = 1;
      }
   }else{
      struct n_strlist *head, **tailp;
      boole exists;
      char const *val1, *val2;

      if(a_nm_alias_dp == NIL)
         a_nm_alias_dp = su_cs_dict_set_treshold_shift(
               su_cs_dict_create(&a_nm_alias__d, a_NM_ALIAS_FLAGS, NIL),
               a_NM_ALIAS_TRESHOLD_SHIFT);

      if((exists = (head = dat.slp) != NIL)){
         while(dat.slp->sl_next != NIL)
            dat.slp = dat.slp->sl_next;
         tailp = &dat.slp->sl_next;
      }else
         head = NIL, tailp = &head;

      while((val1 = *++argv) != NIL){
         uz l1, l2;
         struct mx_name *np;
         boole norecur, name_eq_fullname;

         if((norecur = (*val1 == '\\')))
            ++val1;

         /* We need to allow empty targets */
         name_eq_fullname = TRU1;
         if(*val1 == '\0')
            val2 = val1;
         else if((np = n_extract_single(val1, GFULL)) != NIL){
            val1 = np->n_name;
            val2 = np->n_fullname;
            if((name_eq_fullname = !su_cs_cmp(val1, val2)))
               val2 = su_empty;
         }else{
            n_err(_("alias: %s: invalid argument: %s\n"),
               key, n_shexp_quote_cp(val1, FAL0));
            /*rv = 1;*/
            continue;
         }

         l1 = su_cs_len(val1) +1;
         l2 = su_cs_len(val2) +1;
         dat.slp = n_STRLIST_ALLOC(l1 + 1 + l2);
         *tailp = dat.slp;
         dat.slp->sl_next = NIL;
         tailp = &dat.slp->sl_next;
         dat.slp->sl_len = l1 -1;
         su_mem_copy(dat.slp->sl_dat, val1, l1);
         dat.slp->sl_dat[l1++] = (!norecur && name_eq_fullname &&
               mx_alias_is_valid_name(val1));
         su_mem_copy(&dat.slp->sl_dat[l1], val2, l2);
      }

      if(exists){
         su_cs_dict_view_set_data(&dv, head);
         rv = !TRU1;
      }else
         rv = !(su_cs_dict_insert(a_nm_alias_dp, key, head) == 0);
   }

jleave:
   NYD_OU;
   return rv;
}

int
c_unalias(void *vp){ /* XXX how about toolbox and generic unxy_dict()? */
   struct su_cs_dict_view dv;
   struct n_strlist *slp;
   char const **argv, *key;
   int rv;
   NYD_IN;

   rv = 0;
   key = (argv = vp)[0];

   if(a_nm_alias_dp != NIL)
      su_cs_dict_view_setup(&dv, a_nm_alias_dp);

   do{
      if(key[1] == '\0' && key[0] == '*'){
         if(a_nm_alias_dp != NIL){
            su_CS_DICT_VIEW_FOREACH(&dv){
               slp = S(struct n_strlist*,su_cs_dict_view_data(&dv));
               do{
                  vp = slp;
                  slp = slp->sl_next;
                  n_free(vp);
               }while(slp != NIL);
            }
            su_cs_dict_clear(a_nm_alias_dp);
         }
      }else if(!su_cs_dict_view_find(&dv, key)){
         n_err(_("No such `alias': %s\n"), n_shexp_quote_cp(key, FAL0));
         rv = 1;
      }else{
         slp = S(struct n_strlist*,su_cs_dict_view_data(&dv));
         do{
            vp = slp;
            slp = slp->sl_next;
            n_free(vp);
         }while(slp != NIL);
         su_cs_dict_view_remove(&dv);
      }
   }while((key = *++argv) != NIL);

   NYD_OU;
   return rv;
}

boole
mx_alias_is_valid_name(char const *name){
   char c;
   char const *cp;
   boole rv;
   NYD2_IN;

   for(rv = TRU1, cp = name; (c = *cp) != '\0'; ++cp){
      /* User names, plus things explicitly mentioned in Postfix aliases(5).
       * Plus extensions.  On change adjust *mta-aliases* and impl., too */
      /* TODO alias_is_valid_name(): locale dependent validity check,
       * TODO with Unicode prefix valid UTF-8! */
      if(!su_cs_is_alnum(c) && c != '_'){
         if(cp == name ||
               (c != '-' &&
               /* Extensions, but mentioned by Postfix */
               c != '#' && c != ':' && c != '@' &&
               /* Extensions */
               c != '!' && c != '.' && !(S(u8,c) & 0x80) &&
               !(c == '$' && cp[1] == '\0'))){
            rv = FAL0;
            break;
         }
      }
   }
   NYD2_OU;
   return rv;
}

int
c_alternates(void *vp){
   struct n_string s_b, *s;
   struct n_strlist *slp;
   int rv;
   boole cm_local;
   char const **argv, *varname, *key;
   NYD_IN;

   n_pstate_err_no = su_ERR_NONE;

   argv = S(char const**,vp);
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NIL;
   cm_local = ((n_pstate & n_PS_ARGMOD_LOCAL) != 0);

   if((key = *argv) == NIL){
      slp = NIL;
      rv = !mx_xy_dump_dict("alternates", a_nm_a8s_dp, &slp, NIL,
               &a_nm_a8s_dump);
      if(!rv){
         s = n_string_creat_auto(&s_b);
         s = n_string_book(s, 500); /* xxx */

         for(; slp != NIL; slp = slp->sl_next){
            if(s->s_len > 0)
               s = n_string_push_c(s, ' ');
            s = n_string_push_buf(s, slp->sl_dat, slp->sl_len);
         }
         key = n_string_cp(s);

         if(varname != NIL){
            if(!n_var_vset(varname, R(up,key), cm_local)){
               n_pstate_err_no = su_ERR_NOTSUP;
               rv = 1;
            }
         }else if(*key != '\0')
            rv = !(fprintf(n_stdout, "alternates %s\n", key) >= 0);
         else
            rv = !(fprintf(n_stdout, _("# no alternates registered\n")) >= 0);
      }
   }else{
      if(varname != NULL)
         n_err(_("alternates: `vput' only supported in \"show\" mode\n"));

      if(a_nm_a8s_dp == NIL)
         a_nm_a8s_dp = su_cs_dict_set_treshold_shift(
               su_cs_dict_create(&a_nm_a8s__d, a_NM_A8S_FLAGS, NIL),
               a_NM_A8S_TRESHOLD_SHIFT);
      /* In POSIX mode this command declares a, not appends to a list */
      else if(ok_blook(posix))
         su_cs_dict_clear_elems(a_nm_a8s_dp);

      for(rv = 0; (key = *argv++) != NIL;){
         struct mx_name *np;

         if((np = n_extract_single(key, 0)) == NIL ||
               (np = checkaddrs(np, EACM_STRICT, NIL)) == NIL){
            n_err(_("Invalid `alternates' argument: %s\n"),
               n_shexp_quote_cp(key, FAL0));
            n_pstate_err_no = su_ERR_INVAL;
            rv = 1;
            continue;
         }
         key = np->n_name;

         if(su_cs_dict_replace(a_nm_a8s_dp, key, NIL) > 0){
            n_err(_("Failed to create `alternates' storage: %s\n"),
               n_shexp_quote_cp(key, FAL0));
            n_pstate_err_no = su_ERR_INVAL;
            rv = 1;
         }
      }
   }

   NYD_OU;
   return rv;
}

int
c_unalternates(void *vp){
   int rv;
   NYD_IN;

   rv = !mx_unxy_dict("alternates", a_nm_a8s_dp, vp);
   NYD_OU;
   return rv;
}

struct mx_name *
mx_alternates_remove(struct mx_name *np, boole keep_single){
   /* XXX keep a single pointer, initial null, and immediate remove nodes
    * XXX on successful match unless keep single and that pointer null! */
   struct su_cs_dict_view dv;
   struct mx_name *xp, *newnp;
   NYD_IN;

   /* Delete the temporary bit from all */
   for(xp = np; xp != NULL; xp = xp->n_flink)
      xp->n_flags &= ~S(u32,S32_MIN);

   /* Mark all possible alternate names (xxx sic: instead walk over namelist
    * and hash-lookup alternate instead (unless *allnet*) */
   if(a_nm_a8s_dp != NIL)
      su_CS_DICT_FOREACH(a_nm_a8s_dp, &dv)
         np = a_nm_namelist_mark_name(np, su_cs_dict_view_key(&dv));

   np = a_nm_namelist_mark_name(np, ok_vlook(LOGNAME));

   if((xp = n_extract_single(ok_vlook(sender), GEXTRA)) != NIL)
      np = a_nm_namelist_mark_name(np, xp->n_name);
   else for(xp = lextract(ok_vlook(from), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      np = a_nm_namelist_mark_name(np, xp->n_name);

   /* C99 */{
      char const *v15compat;

      if((v15compat = ok_vlook(replyto)) != NULL){
         n_OBSOLETE(_("please use *reply-to*, not *replyto*"));
         for(xp = lextract(v15compat, GEXTRA | GSKIN); xp != NULL;
               xp = xp->n_flink)
            np = a_nm_namelist_mark_name(np, xp->n_name);
      }
   }

   for(xp = lextract(ok_vlook(reply_to), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      np = a_nm_namelist_mark_name(np, xp->n_name);

   /* Clean the list by throwing away all deleted or marked (but one) nodes */
   for(xp = newnp = NULL; np != NULL; np = np->n_flink){
      if(np->n_type & GDEL)
         continue;
      if(np->n_flags & S(u32,S32_MIN)){
         if(!keep_single)
            continue;
         keep_single = FAL0;
      }

      np->n_blink = xp;
      if(xp != NULL)
         xp->n_flink = np;
      else
         newnp = np;
      xp = np;
      xp->n_flags &= ~S(u32,S32_MIN);
   }
   if(xp != NULL)
      xp->n_flink = NULL;
   np = newnp;

   NYD_OU;
   return np;
}

boole
mx_name_is_mine(char const *name){
   struct su_cs_dict_view dv;
   struct mx_name *xp;
   NYD_IN;

   if(a_nm_is_same_name(ok_vlook(LOGNAME), name, NIL))
      goto jleave;

   if(a_nm_a8s_dp != NIL){
      if(!ok_blook(allnet)){
         if(su_cs_dict_has_key(a_nm_a8s_dp, name))
            goto jleave;
      }else su_CS_DICT_FOREACH(a_nm_a8s_dp, &dv)
         if(a_nm_is_same_name(name, su_cs_dict_view_key(&dv), NIL))
            goto jleave;
   }

   for(xp = lextract(ok_vlook(from), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      if(a_nm_is_same_name(xp->n_name, name, NIL))
         goto jleave;

   /* C99 */{
      char const *v15compat;

      if((v15compat = ok_vlook(replyto)) != NULL){
         n_OBSOLETE(_("please use *reply-to*, not *replyto*"));
         for(xp = lextract(v15compat, GEXTRA | GSKIN); xp != NULL;
               xp = xp->n_flink)
            if(a_nm_is_same_name(xp->n_name, name, NIL))
               goto jleave;
      }
   }

   for(xp = lextract(ok_vlook(reply_to), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      if(a_nm_is_same_name(xp->n_name, name, NIL))
         goto jleave;

   if((xp = n_extract_single(ok_vlook(sender), GEXTRA)) != NIL &&
         a_nm_is_same_name(xp->n_name, name, NIL))
      goto jleave;

   name = NIL;
jleave:
   NYD_OU;
   return (name != NIL);
}

#include "su/code-ou.h"
/* s-it-mode */
