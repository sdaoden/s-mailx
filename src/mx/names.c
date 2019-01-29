/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of names.h.
 *@ FIXME Use the new GNOT_A_LIST
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

#include "mx/iconv.h"

#include "mx/names.h"
#include "su/code-in.h"

enum a_nm_type{
   /* Main types */
   a_NM_T_ALTERNATES = 1,
   a_NM_T_ALIAS,
   a_NM_T_MASK = 0x1F,

   /* Extended type mask to be able to reflect what we really have.
    * This is a leftover from when mlist/mlsubscribe were handled in here,
    * i.e., being able to differentiate in between several subtypes */
   a_NM_T_PRINT_MASK = a_NM_T_MASK
};
CTA(a_NM_T_MASK >= a_NM_T_ALIAS, "Mask does not cover necessary bits");

struct a_nm_group{
   struct a_nm_group *ng_next;
   u32 ng_subclass_off; /* of "subclass" in .ng_id (if any) */
   u16 ng_id_len_sub;   /* length of .ng_id: _subclass_off - this */
   u8 ng_type;          /* enum a_nm_type */
   /* Identifying name, of variable size.  Dependent on actual "subtype" more
    * data follows thereafter, but note this is always used (i.e., for regular
    * expression entries this is still set to the plain string) */
   char ng_id[VFIELD_SIZE(1)];
};
#define a_NM_GP_TO_SUBCLASS(X,G) \
do{\
   union a_nm_group_subclass {void *gs_vp; char *gs_cp;} a__gs__;\
   a__gs__.gs_cp = &UNCONST(char*,G)[(G)->ng_subclass_off];\
   (X) = a__gs__.gs_vp;\
}while(0)

struct a_nm_grp_names_head{
   struct a_nm_grp_names *ngnh_head;
};

struct a_nm_grp_names{
   struct a_nm_grp_names *ngn_next;
   char ngn_id[VFIELD_SIZE(0)];
};

struct a_nm_lookup{
   struct a_nm_group **ngl_htable;
   struct a_nm_group **ngl_slot;
   struct a_nm_group *ngl_slot_last;
   struct a_nm_group *ngl_group;
};

/* `alternates' */
static struct a_nm_group *a_nm_alternates_heads[HSHSIZE];

/* `alias' */
static struct a_nm_group *a_nm_alias_heads[HSHSIZE];

/* Same name, while taking care for *allnet*? */
static boole a_nm_is_same_name(char const *n1, char const *n2);

/* Mark all (!file, !pipe) nodes with the given name */
static struct mx_name *a_nm_namelist_mark_name(struct mx_name *np,
      char const *name);

/* Grab a single name (liberal name) */
static char const *a_nm_yankname(char const *ap, char *wbuf,
      char const *separators, int keepcomms);

/* Extraction multiplexer that splits an input line to names */
static struct mx_name *a_nm_extract1(char const *line, enum gfield ntype,
      char const *separators, boole keepcomms);

/* Recursively expand a alias name.  Limit expansion to some fixed level.
 * Direct recursion is not expanded for convenience */
static struct mx_name *a_nm_gexpand(uz level, struct mx_name *nlist,
      struct a_nm_group *ngp, boole metoo, int ntype, char const *logname);

/* elide() helper */
static int a_nm_elide_qsort(void const *s1, void const *s2);

/* Lookup a group, return it or NULL, fill in glp anyway */
static struct a_nm_group *a_nm_lookup(enum a_nm_type nt,
      struct a_nm_lookup *nglp, char const *id);

/* Easier-to-use wrapper around _group_lookup() */
static struct a_nm_group *a_nm_group_find(enum a_nm_type nt,
      char const *id);

/* Iteration: go to the first group, which also inits the iterator.  A valid
 * iterator can be stepped via _next().  A NULL return means no (more) groups
 * to be iterated exist, in which case only nglp->ngl_group is set (NULL) */
static struct a_nm_group *a_nm_group_go_first(enum a_nm_type nt,
      struct a_nm_lookup *nglp);
static struct a_nm_group *a_nm_group_go_next(struct a_nm_lookup *nglp);

/* Fetch the group id, create it as necessary, fail with NULL if impossible */
static struct a_nm_group *a_nm_group_fetch(enum a_nm_type nt,
      char const *id, uz addsz);

/* "Intelligent" delete which handles a "*" id, too;
 * returns a true boolean if a group was deleted, and always succeeds for "*" */
static boole a_nm_group_del(enum a_nm_type nt, char const *id);

static struct a_nm_group *a_nm__group_del(struct a_nm_lookup *nglp);
static void a_nm__names_del(struct a_nm_group *ngp);

/* Print all groups of the given type, alphasorted, or store in `vput' varname:
 * only in this mode it can return failure */
static boole a_nm_group_print_all(enum a_nm_type nt, char const *varname);

static int a_nm__group_print_qsorter(void const *a, void const *b);

/* Really print a group, actually.  Or store in vputsp, if set.
 * Return number of written lines */
static uz a_nm_group_print(struct a_nm_group const *ngp, FILE *fo,
      struct n_string *vputsp);

static boole
a_nm_is_same_name(char const *n1, char const *n2){
   boole rv;
   char c1, c2, c1r, c2r;
   NYD2_IN;

   if(ok_blook(allnet)){
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
   }else
      rv = !su_cs_cmp_case(n1, n2);
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
            a_nm_is_same_name(p->n_name, name))
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
   char const *cp;
   char *nbuf;
   struct mx_name *headp, *tailp, *np;
   NYD_IN;

   headp = NULL;

   if(line == NULL || *line == '\0')
      goto jleave;

   nbuf = n_alloc(su_cs_len(cp = line) +1);

   for(tailp = headp;
         ((cp = a_nm_yankname(cp, nbuf, seps, keepcomms)) != NULL);)
      /* TODO We cannot set GNULL_OK because otherwise this software blows up.
       * TODO We will need a completely new way of reporting the errors of
       * TODO is_addr_invalid() ... */
      if((np = nalloc(nbuf, ntype /*| GNULL_OK*/)) != NULL){
         if((np->n_blink = tailp) != NULL)
            tailp->n_flink = np;
         else
            headp = np;
         tailp = np;
      }

   n_free(nbuf);

jleave:
   NYD_OU;
   return headp;
}

static struct mx_name *
a_nm_gexpand(uz level, struct mx_name *nlist, struct a_nm_group *ngp,
      boole metoo, int ntype, char const *logname){
   struct a_nm_grp_names *ngnp;
   struct mx_name *nlist_tail;
   struct a_nm_grp_names_head *ngnhp;
   NYD2_IN;

   if(UCMP(z, level++, >, n_ALIAS_MAXEXP)){
      n_err(_("Expanding alias to depth larger than %d\n"), n_ALIAS_MAXEXP);
      goto jleave;
   }

   a_NM_GP_TO_SUBCLASS(ngnhp, ngp);

   for(ngnp = ngnhp->ngnh_head; ngnp != NULL; ngnp = ngnp->ngn_next){
      struct a_nm_group *xngp;
      char *cp;

      cp = ngnp->ngn_id;

      if(!su_cs_cmp(cp, ngp->ng_id))
         goto jas_is;

      if((xngp = a_nm_group_find(a_NM_T_ALIAS, cp)) != NULL){
         /* For S-nail(1), the "alias" may *be* the sender in that a name maps
          * to a full address specification; aliases cannot be empty */
         struct a_nm_grp_names_head *xngnhp;

         a_NM_GP_TO_SUBCLASS(xngnhp, xngp);

         ASSERT(xngnhp->ngnh_head != NULL);
         if(metoo || xngnhp->ngnh_head->ngn_next != NULL ||
               !a_nm_is_same_name(cp, logname))
            nlist = a_nm_gexpand(level, nlist, xngp, metoo, ntype, logname);
         continue;
      }

      /* Here we should allow to expand to itself if only person in alias */
jas_is:
      if(metoo || ngnhp->ngnh_head->ngn_next == NULL ||
            !a_nm_is_same_name(cp, logname)){
         struct mx_name *np;

         np = nalloc(cp, ntype | GFULL);
         if((nlist_tail = nlist) != NULL){
            while(nlist_tail->n_flink != NULL)
               nlist_tail = nlist_tail->n_flink;
            nlist_tail->n_flink = np;
            np->n_blink = nlist_tail;
         }else
            nlist = np;
      }
   }
jleave:
   NYD2_OU;
   return nlist;
}

static int
a_nm_elide_qsort(void const *s1, void const *s2){
   struct mx_name const * const *np1, * const *np2;
   int rv;
   NYD2_IN;

   np1 = s1;
   np2 = s2;
   if(!(rv = su_cs_cmp_case((*np1)->n_name, (*np2)->n_name))){
      LCTAV(GTO < GCC && GCC < GBCC);
      rv = ((*np1)->n_type & (GTO | GCC | GBCC)) -
            ((*np2)->n_type & (GTO | GCC | GBCC));
   }
   NYD2_OU;
   return rv;
}

static struct a_nm_group *
a_nm_lookup(enum a_nm_type nt, struct a_nm_lookup *nglp,
      char const *id){
   char c1;
   struct a_nm_group *lngp, *ngp;
   boole icase;
   NYD2_IN;

   icase = FAL0;

   /* C99 */{
      u32 h;
      struct a_nm_group **ngpa;

      switch((nt &= a_NM_T_MASK)){
      case a_NM_T_ALTERNATES:
         ngpa = a_nm_alternates_heads;
         icase = TRU1;
         break;
      default:
      case a_NM_T_ALIAS:
         ngpa = a_nm_alias_heads;
         break;
      }

      nglp->ngl_htable = ngpa;
      h = icase ? su_cs_hash_case(id) : su_cs_hash(id);
      ngp = *(nglp->ngl_slot = &ngpa[h % HSHSIZE]);
   }

   lngp = NULL;
   c1 = *id++;

   if(icase){
      c1 = su_cs_to_lower(c1);
      for(; ngp != NULL; lngp = ngp, ngp = ngp->ng_next)
         if((ngp->ng_type & a_NM_T_MASK) == nt && *ngp->ng_id == c1 &&
               !su_cs_cmp_case(&ngp->ng_id[1], id))
            break;
   }else{
      for(; ngp != NULL; lngp = ngp, ngp = ngp->ng_next)
         if((ngp->ng_type & a_NM_T_MASK) == nt && *ngp->ng_id == c1 &&
               !su_cs_cmp(&ngp->ng_id[1], id))
            break;
   }

   nglp->ngl_slot_last = lngp;
   nglp->ngl_group = ngp;
   NYD2_OU;
   return ngp;
}

static struct a_nm_group *
a_nm_group_find(enum a_nm_type nt, char const *id){
   struct a_nm_lookup ngl;
   struct a_nm_group *ngp;
   NYD2_IN;

   ngp = a_nm_lookup(nt, &ngl, id);
   NYD2_OU;
   return ngp;
}

static struct a_nm_group *
a_nm_group_go_first(enum a_nm_type nt, struct a_nm_lookup *nglp){
   uz i;
   struct a_nm_group **ngpa, *ngp;
   NYD2_IN;

   switch((nt &= a_NM_T_MASK)){
   case a_NM_T_ALTERNATES:
      ngpa = a_nm_alternates_heads;
      break;
   default:
   case a_NM_T_ALIAS:
      ngpa = a_nm_alias_heads;
      break;
   }

   nglp->ngl_htable = ngpa;

   for(i = 0; i < HSHSIZE; ++ngpa, ++i)
      if((ngp = *ngpa) != NULL){
         nglp->ngl_slot = ngpa;
         nglp->ngl_group = ngp;
         goto jleave;
      }

   nglp->ngl_group = ngp = NULL;
jleave:
   nglp->ngl_slot_last = NULL;
   NYD2_OU;
   return ngp;
}

static struct a_nm_group *
a_nm_group_go_next(struct a_nm_lookup *nglp){
   struct a_nm_group *ngp, **ngpa;
   NYD2_IN;

   if((ngp = nglp->ngl_group->ng_next) != NULL)
      nglp->ngl_slot_last = nglp->ngl_group;
   else{
      nglp->ngl_slot_last = NULL;
      for(ngpa = &nglp->ngl_htable[HSHSIZE]; ++nglp->ngl_slot < ngpa;)
         if((ngp = *nglp->ngl_slot) != NULL)
            break;
   }
   nglp->ngl_group = ngp;
   NYD2_OU;
   return ngp;
}

static struct a_nm_group *
a_nm_group_fetch(enum a_nm_type nt, char const *id, uz addsz){
   struct a_nm_lookup ngl;
   struct a_nm_group *ngp;
   uz l, i;
   NYD2_IN;

   if((ngp = a_nm_lookup(nt, &ngl, id)) != NULL)
      goto jleave;

   l = su_cs_len(id) +1;
   if(UZ_MAX - Z_ALIGN(l) <=
         Z_ALIGN(VSTRUCT_SIZEOF(struct a_nm_group, ng_id)))
      goto jleave;

   i = Z_ALIGN(VSTRUCT_SIZEOF(struct a_nm_group, ng_id) + l);
   switch(nt & a_NM_T_MASK){
   case a_NM_T_ALTERNATES:
   default:
      break;
   case a_NM_T_ALIAS:
      addsz += sizeof(struct a_nm_grp_names_head);
      break;
   }
   if(UZ_MAX - i < addsz || U32_MAX <= i || U16_MAX < i - l)
      goto jleave;

   ngp = n_alloc(i + addsz);
   su_mem_copy(ngp->ng_id, id, l);
   ngp->ng_subclass_off = S(u32,i);
   ngp->ng_id_len_sub = S(u16,i - --l);
   ngp->ng_type = nt;
   switch(nt & a_NM_T_MASK){
   case a_NM_T_ALTERNATES:{
      char *cp, c;

      for(cp = ngp->ng_id; (c = *cp) != '\0'; ++cp)
         *cp = su_cs_to_lower(c);
      }break;
   default:
      break;
   }

   if((nt & a_NM_T_MASK) == a_NM_T_ALIAS){
      struct a_nm_grp_names_head *ngnhp;

      a_NM_GP_TO_SUBCLASS(ngnhp, ngp);
      ngnhp->ngnh_head = NULL;
   }

   ngp->ng_next = *ngl.ngl_slot;
   *ngl.ngl_slot = ngp;
jleave:
   NYD2_OU;
   return ngp;
}

static boole
a_nm_group_del(enum a_nm_type nt, char const *id){
   struct a_nm_lookup ngl;
   struct a_nm_group *ngp;
   enum a_nm_type xnt;
   NYD2_IN;

   xnt = nt & a_NM_T_MASK;

   /* Delete 'em all? */
   if(id[0] == '*' && id[1] == '\0'){
      for(ngp = a_nm_group_go_first(nt, &ngl); ngp != NULL;)
         ngp = ((ngp->ng_type & a_NM_T_MASK) == xnt) ? a_nm__group_del(&ngl)
               : a_nm_group_go_next(&ngl);
      ngp = (struct a_nm_group*)TRU1;
   }else if((ngp = a_nm_lookup(nt, &ngl, id)) != NULL){
      if(ngp->ng_type & xnt)
         a_nm__group_del(&ngl);
      else
         ngp = NULL;
   }
   NYD2_OU;
   return (ngp != NULL);
}

static struct a_nm_group *
a_nm__group_del(struct a_nm_lookup *nglp){
   struct a_nm_group *x, *ngp;
   NYD2_IN;

   /* Overly complicated: link off this node, step ahead to next.. */
   x = nglp->ngl_group;
   if((ngp = nglp->ngl_slot_last) != NULL)
      ngp = (ngp->ng_next = x->ng_next);
   else{
      nglp->ngl_slot_last = NULL;
      ngp = (*nglp->ngl_slot = x->ng_next);

      if(ngp == NULL){
         struct a_nm_group **ngpa;

         for(ngpa = &nglp->ngl_htable[HSHSIZE]; ++nglp->ngl_slot < ngpa;)
            if((ngp = *nglp->ngl_slot) != NULL)
               break;
      }
   }
   nglp->ngl_group = ngp;

   if((x->ng_type & a_NM_T_MASK) == a_NM_T_ALIAS)
      a_nm__names_del(x);

   n_free(x);
   NYD2_OU;
   return ngp;
}

static void
a_nm__names_del(struct a_nm_group *ngp){
   struct a_nm_grp_names_head *ngnhp;
   struct a_nm_grp_names *ngnp;
   NYD2_IN;

   a_NM_GP_TO_SUBCLASS(ngnhp, ngp);

   for(ngnp = ngnhp->ngnh_head; ngnp != NULL;){
      struct a_nm_grp_names *x;

      x = ngnp;
      ngnp = ngnp->ngn_next;
      n_free(x);
   }
   NYD2_OU;
}

static boole
a_nm_group_print_all(enum a_nm_type nt, char const *varname){
   struct n_string s;
   uz lines;
   FILE *fp;
   char const **ida;
   struct a_nm_group const *ngp;
   u32 h, i;
   struct a_nm_group **ngpa;
   char const *tname;
   enum a_nm_type xnt;
   NYD_IN;

   if(varname != NULL)
      n_string_creat_auto(&s);

   xnt = nt & a_NM_T_PRINT_MASK;

   switch(xnt & a_NM_T_MASK){
   case a_NM_T_ALTERNATES:
      tname = "alternates";
      ngpa = a_nm_alternates_heads;
      break;
   default:
   case a_NM_T_ALIAS:
      tname = "alias";
      ngpa = a_nm_alias_heads;
      break;
   }

   /* Count entries */
   for(i = h = 0; h < HSHSIZE; ++h)
      for(ngp = ngpa[h]; ngp != NULL; ngp = ngp->ng_next)
         if((ngp->ng_type & a_NM_T_PRINT_MASK) == xnt)
            ++i;
   if(i == 0){
      if(varname == NULL)
         fprintf(n_stdout, _("# no %s registered\n"), tname);
      goto jleave;
   }
   ++i;
   ida = n_autorec_alloc(i * sizeof *ida);

   /* Create alpha sorted array of entries */
   for(i = h = 0; h < HSHSIZE; ++h)
      for(ngp = ngpa[h]; ngp != NULL; ngp = ngp->ng_next)
         if((ngp->ng_type & a_NM_T_PRINT_MASK) == xnt)
            ida[i++] = ngp->ng_id;
   if(i > 1)
      qsort(ida, i, sizeof *ida, &a_nm__group_print_qsorter);
   ida[i] = NULL;

   if(varname != NULL)
      fp = NULL;
   else if((fp = Ftmp(NULL, "nagprint", OF_RDWR | OF_UNLINK | OF_REGISTER)
         ) == NULL)
      fp = n_stdout;

   /* Create visual result */
   lines = 0;

   switch(xnt & a_NM_T_MASK){
   case a_NM_T_ALTERNATES:
      if(fp != NULL){
         fputs(tname, fp);
         lines = 1;
      }
      break;
   default:
      break;
   }

   for(i = 0; ida[i] != NULL; ++i)
      lines += a_nm_group_print(a_nm_group_find(nt, ida[i]), fp, &s);

   switch(xnt & a_NM_T_MASK){
   case a_NM_T_ALTERNATES:
      if(fp != NULL){
         putc('\n', fp);
         ASSERT(lines == 1);
      }
      break;
   default:
      break;
   }

   if(varname == NULL && fp != n_stdout){
      ASSERT(fp != NULL);
      page_or_print(fp, lines);
      Fclose(fp);
   }

jleave:
   if(varname != NULL){
      tname = n_string_cp(&s);
      if(n_var_vset(varname, (up)tname))
         varname = NULL;
      else
         n_pstate_err_no = su_ERR_NOTSUP;
   }
   NYD_OU;
   return (varname == NULL);
}

static int
a_nm__group_print_qsorter(void const *a, void const *b){
   int rv;
   NYD2_IN;

   rv = su_cs_cmp(*UNCONST(char**,a), *UNCONST(char**,b));
   NYD2_OU;
   return rv;
}

static uz
a_nm_group_print(struct a_nm_group const *ngp, FILE *fo,
      struct n_string *vputsp){
   uz rv;
   NYD2_IN;

   rv = 1;

   switch(ngp->ng_type & a_NM_T_MASK){
   case a_NM_T_ALTERNATES:{
      if(fo != NULL)
         fprintf(fo, " %s", ngp->ng_id);
      else{
         if(vputsp->s_len > 0)
            vputsp = n_string_push_c(vputsp, ' ');
         /*vputsp =*/ n_string_push_cp(vputsp, ngp->ng_id);
      }
      rv = 0;
      }break;
   case a_NM_T_ALIAS:{
      struct a_nm_grp_names_head *ngnhp;
      struct a_nm_grp_names *ngnp;

      ASSERT(fo != NULL); /* xxx no vput yet */
      fprintf(fo, "alias %s ", ngp->ng_id);

      a_NM_GP_TO_SUBCLASS(ngnhp, ngp);
      if((ngnp = ngnhp->ngnh_head) != NULL) { /* xxx always 1+ entries */
         do{
            struct a_nm_grp_names *x;

            x = ngnp;
            ngnp = ngnp->ngn_next;
            fprintf(fo, " \"%s\"", string_quote(x->ngn_id)); /* TODO shexp */
         }while(ngnp != NULL);
      }
      putc('\n', fo);
      }break;
   }
   NYD2_OU;
   return rv;
}

FL struct mx_name *
nalloc(char const *str, enum gfield ntype)
{
   struct n_addrguts ag;
   struct str in, out;
   struct mx_name *np;
   NYD_IN;
   ASSERT(!(ntype & GFULLEXTRA) || (ntype & GFULL) != 0);

   str = n_addrspec_with_guts(&ag, str,
         ((ntype & (GFULL | GSKIN | GREF)) != 0),
         ((ntype & GNOT_A_LIST) != 0));
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
         in.s = n_lofi_alloc(s + 1 + i +1);
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
         mime_fromhdr(&in, &out, /* TODO TD_ISPR |*/ TD_ICONV);

         for (cp = out.s, i = out.l; i > 0 && su_cs_is_space(*cp); --i, ++cp)
            ;
         while (i > 0 && su_cs_is_space(cp[i - 1]))
            --i;
         np->n_fullextra = savestrbuf(cp, i);

         n_lofi_free(in.s);
         n_free(out.s);
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
         /* TODO This definetely doesn't belong here! */
         uz l = ag.ag_iaddr_start,
            lsuff = ag.ag_ilen - ag.ag_iaddr_aend;
         in.s = n_lofi_alloc(l + ag.ag_slen + lsuff +1);
         su_mem_copy(in.s, str, l);
         su_mem_copy(in.s + l, ag.ag_skinned, ag.ag_slen);
         l += ag.ag_slen;
         su_mem_copy(in.s + l, str + ag.ag_iaddr_aend, lsuff);
         l += lsuff;
         in.s[l] = '\0';
         in.l = l;
      }
#endif
      mime_fromhdr(&in, &out, /* TODO TD_ISPR |*/ TD_ICONV);
      np->n_fullname = savestr(out.s);
      n_free(out.s);
#ifdef mx_HAVE_IDNA
      if (ag.ag_n_flags & mx_NAME_IDNA)
         n_lofi_free(in.s);
#endif
   }

jleave:
   NYD_OU;
   return np;
}

FL struct mx_name *
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

FL struct mx_name *
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
   nnp->n_type = ntype;
   nnp->n_flags = np->n_flags | mx_NAME_NAME_SALLOC;
   nnp->n_name = savestr(np->n_name);

   if (np->n_name == np->n_fullname || !(ntype & (GFULL | GSKIN))) {
      nnp->n_fullname = nnp->n_name;
      nnp->n_fullextra = NULL;
   } else {
      nnp->n_fullname = savestr(np->n_fullname);
      nnp->n_fullextra = (np->n_fullextra == NULL) ? NULL
            : savestr(np->n_fullextra);
   }

jleave:
   NYD_OU;
   return nnp;
}

FL struct mx_name *
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

FL struct mx_name *
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

FL u32
count(struct mx_name const *np){
   u32 c;
   NYD_IN;

   for(c = 0; np != NIL; np = np->n_flink)
      if(!(np->n_type & GDEL))
         ++c;
   NYD_OU;
   return c;
}

FL u32
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

FL struct mx_name *
extract(char const *line, enum gfield ntype)
{
   struct mx_name *rv;
   NYD_IN;

   rv = a_nm_extract1(line, ntype, " \t,", 0);
   NYD_OU;
   return rv;
}

FL struct mx_name *
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
      enum n_shexp_state shs;

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
         line = cp = NULL;
      n_autorec_relax_gut();
   }

   rv = ((line != NULL && strpbrk(line, ",\"\\(<|"))
         ? a_nm_extract1(line, ntype, ",", 1) : extract(line, ntype));

   if(cp != NULL)
      n_lofi_free(cp);
   NYD_OU;
   return rv;
}

FL struct mx_name *
n_extract_single(char const *line, enum gfield ntype){
   struct mx_name *rv;
   NYD_IN;

   rv = nalloc(line, ntype | GSKIN | GNOT_A_LIST | GNULL_OK);
   NYD_OU;
   return rv;
}

FL char *
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

FL struct mx_name *
grab_names(enum n_go_input_flags gif, char const *field, struct mx_name *np,
      int comma, enum gfield gflags)
{
   struct mx_name *nq;
   NYD_IN;

jloop:
   np = lextract(n_go_input_cp(gif, field, detract(np, comma)), gflags);
   for (nq = np; nq != NULL; nq = nq->n_flink)
      if (is_addr_invalid(nq, EACM_NONE))
         goto jloop;
   NYD_OU;
   return np;
}

FL boole
name_is_same_domain(struct mx_name const *n1, struct mx_name const *n2)
{
   char const *d1, *d2;
   boole rv;
   NYD_IN;

   d1 = su_cs_rfind_c(n1->n_name, '@');
   d2 = su_cs_rfind_c(n2->n_name, '@');

   rv = (d1 != NULL && d2 != NULL) ? !su_cs_cmp_case(++d1, ++d2) : FAL0;

   NYD_OU;
   return rv;
}

FL struct mx_name *
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
         else if (eacm & EAF_MAYKEEP) /* TODO HACK!  See definition! */
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

FL struct mx_name *
n_namelist_vaporise_head(boole strip_alternates, struct header *hp,
   enum expand_addr_check_mode eacm, s8 *set_on_error)
{
   /* TODO namelist_vaporise_head() is incredibly expensive and redundant */
   struct mx_name *tolist, *np, **npp;
   NYD_IN;

   tolist = cat(hp->h_to, cat(hp->h_cc, cat(hp->h_bcc, hp->h_fcc)));
   hp->h_to = hp->h_cc = hp->h_bcc = hp->h_fcc = NULL;

   tolist = usermap(tolist, strip_alternates/*metoo*/);
   if(strip_alternates)
      tolist = n_alternates_remove(tolist, TRU1);
   tolist = elide(checkaddrs(tolist, eacm, set_on_error));

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

FL struct mx_name *
usermap(struct mx_name *names, boole force_metoo){
   struct a_nm_group *ngp;
   struct mx_name *nlist, *nlist_tail, *np, *cp;
   int metoo;
   char const *logname;
   NYD_IN;

   logname = ok_vlook(LOGNAME);
   metoo = (force_metoo || ok_blook(metoo));
   nlist = nlist_tail = NULL;
   np = names;

   for(; np != NULL; np = cp){
      ASSERT(!(np->n_type & GDEL)); /* TODO legacy */
      cp = np->n_flink;

      if(is_fileorpipe_addr(np) ||
            (ngp = a_nm_group_find(a_NM_T_ALIAS, np->n_name)) == NULL){
         if((np->n_blink = nlist_tail) != NULL)
            nlist_tail->n_flink = np;
         else
            nlist = np;
         nlist_tail = np;
         np->n_flink = NULL;
      }else{
         nlist = a_nm_gexpand(0, nlist, ngp, metoo, np->n_type, logname);
         if((nlist_tail = nlist) != NULL)
            while(nlist_tail->n_flink != NULL)
               nlist_tail = nlist_tail->n_flink;
      }
   }
   NYD_OU;
   return nlist;
}

FL struct mx_name *
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

   /* Create a temporay array and sort that */
   nparr = n_lofi_alloc(sizeof(*nparr) * i);

   for(i = 0, np = nlist; np != NULL; np = np->n_flink)
      nparr[i++] = np;

   qsort(nparr, i, sizeof *nparr, &a_nm_elide_qsort);

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
    * addressees, thus */
   for(np = nlist; np != NULL; np = np->n_flink){
      for(j = 0; j <= i; ++j)
         if(np == nparr[j]){
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

FL int
c_alternates(void *vp){
   struct a_nm_group *ngp;
   char const *varname, *ccp;
   char **argv;
   NYD_IN;

   n_pstate_err_no = su_ERR_NONE;

   argv = vp;
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NULL;

   if(*argv == NULL){
      if(!a_nm_group_print_all(a_NM_T_ALTERNATES, varname))
         vp = NULL;
   }else{
      if(varname != NULL)
         n_err(_("`alternates': `vput' only supported for show mode\n"));

      /* Delete the old set to "declare a list", if *posix* */
      if(ok_blook(posix))
         a_nm_group_del(a_NM_T_ALTERNATES, n_star);

      while((ccp = *argv++) != NULL){
         uz l;
         struct mx_name *np;

         if((np = lextract(ccp, GSKIN)) == NULL || np->n_flink != NULL ||
               (np = checkaddrs(np, EACM_STRICT, NULL)) == NULL){
            n_err(_("Invalid `alternates' argument: %s\n"),
               n_shexp_quote_cp(ccp, FAL0));
            n_pstate_err_no = su_ERR_INVAL;
            vp = NULL;
            continue;
         }
         ccp = np->n_name;

         l = su_cs_len(ccp) +1;
         if((ngp = a_nm_group_fetch(a_NM_T_ALTERNATES, ccp, l)) == NULL){
            n_err(_("Failed to create storage for alternates: %s\n"),
               n_shexp_quote_cp(ccp, FAL0));
            n_pstate_err_no = su_ERR_NOMEM;
            vp = NULL;
         }
      }
   }
   NYD_OU;
   return (vp != NULL ? 0 : 1);
}

FL int
c_unalternates(void *vp){
   char **argv;
   int rv;
   NYD_IN;

   rv = 0;
   argv = vp;

   do if(!a_nm_group_del(a_NM_T_ALTERNATES, *argv)){
      n_err(_("No such `alternates': %s\n"), n_shexp_quote_cp(*argv, FAL0));
      rv = 1;
   }while(*++argv != NULL);
   NYD_OU;
   return rv;
}

FL struct mx_name *
n_alternates_remove(struct mx_name *np, boole keep_single){
   /* XXX keep a single pointer, initial null, and immediate remove nodes
    * XXX on successful match unless keep single and that pointer null! */
   struct a_nm_lookup ngl;
   struct a_nm_group *ngp;
   struct mx_name *xp, *newnp;
   NYD_IN;

   /* Delete the temporary bit from all */
   for(xp = np; xp != NULL; xp = xp->n_flink)
      xp->n_flags &= ~S(u32,S32_MIN);

   /* Mark all possible alternate names (xxx sic: instead walk over namelist
    * and hash-lookup alternate instead (unless *allnet*) */
   for(ngp = a_nm_group_go_first(a_NM_T_ALTERNATES, &ngl); ngp != NULL;
         ngp = a_nm_group_go_next(&ngl))
      np = a_nm_namelist_mark_name(np, ngp->ng_id);

   np = a_nm_namelist_mark_name(np, ok_vlook(LOGNAME));

   if((xp = extract(ok_vlook(sender), GEXTRA | GSKIN)) != NULL){
      /* TODO check_from_and_sender(): drop; *sender*: only one name!
       * TODO At assignment time, as VIP var? */
      do
         np = a_nm_namelist_mark_name(np, xp->n_name);
      while((xp = xp->n_flink) != NULL);
   }else for(xp = lextract(ok_vlook(from), GEXTRA | GSKIN); xp != NULL;
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

FL boole
n_is_myname(char const *name){
   struct a_nm_lookup ngl;
   struct a_nm_group *ngp;
   struct mx_name *xp;
   NYD_IN;

   if(a_nm_is_same_name(ok_vlook(LOGNAME), name))
      goto jleave;

   if(!ok_blook(allnet)){
      if(a_nm_lookup(a_NM_T_ALTERNATES, &ngl, name) != NULL)
         goto jleave;
   }else{
      for(ngp = a_nm_group_go_first(a_NM_T_ALTERNATES, &ngl); ngp != NULL;
            ngp = a_nm_group_go_next(&ngl))
         if(a_nm_is_same_name(ngp->ng_id, name))
            goto jleave;
   }

   for(xp = lextract(ok_vlook(from), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      if(a_nm_is_same_name(xp->n_name, name))
         goto jleave;

   /* C99 */{
      char const *v15compat;

      if((v15compat = ok_vlook(replyto)) != NULL){
         n_OBSOLETE(_("please use *reply-to*, not *replyto*"));
         for(xp = lextract(v15compat, GEXTRA | GSKIN); xp != NULL;
               xp = xp->n_flink)
            if(a_nm_is_same_name(xp->n_name, name))
               goto jleave;
      }
   }

   for(xp = lextract(ok_vlook(reply_to), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      if(a_nm_is_same_name(xp->n_name, name))
         goto jleave;

   for(xp = extract(ok_vlook(sender), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      if(a_nm_is_same_name(xp->n_name, name))
         goto jleave;

   name = NULL;
jleave:
   NYD_OU;
   return (name != NULL);
}

FL boole
n_alias_is_valid_name(char const *name){
   char c;
   char const *cp;
   boole rv;
   NYD2_IN;

   for(rv = TRU1, cp = name++; (c = *cp++) != '\0';)
      /* User names, plus things explicitly mentioned in Postfix aliases(5),
       * i.e., [[:alnum:]_#:@.-]+$?.
       * As extensions allow high-bit bytes, semicolon and period. */
      /* TODO n_alias_is_valid_name(): locale dependent validity check,
       * TODO with Unicode prefix valid UTF-8! */
      if(!su_cs_is_alnum(c) && c != '_' && c != '-' &&
            c != '#' && c != ':' && c != '@' &&
            !(S(u8,c) & 0x80) && c != '!' && c != '.'){
         if(c == '$' && cp != name && *cp == '\0')
            break;
         rv = FAL0;
         break;
      }
   NYD2_OU;
   return rv;
}

FL int
c_alias(void *v)
{
   char const *ecp;
   char **argv;
   struct a_nm_group *ngp;
   int rv;
   NYD_IN;

   rv = 0;
   argv = v;
   UNINIT(ecp, NULL);

   if(*argv == NULL)
      a_nm_group_print_all(a_NM_T_ALIAS, NULL);
   else if(!n_alias_is_valid_name(*argv)){
      ecp = N_("Not a valid alias name: %s\n");
      goto jerr;
   }else if(argv[1] == NULL){
      if((ngp = a_nm_group_find(a_NM_T_ALIAS, *argv)) != NULL)
         a_nm_group_print(ngp, n_stdout, NULL);
      else{
         ecp = N_("No such alias: %s\n");
         goto jerr;
      }
   }else if((ngp = a_nm_group_fetch(a_NM_T_ALIAS, *argv, 0)) == NULL){
      ecp = N_("Failed to create alias storage for: %s\n");
jerr:
      n_err(V_(ecp), n_shexp_quote_cp(*argv, FAL0));
      rv = 1;
   }else{
      struct a_nm_grp_names *ngnp_tail, *ngnp;
      struct a_nm_grp_names_head *ngnhp;

      a_NM_GP_TO_SUBCLASS(ngnhp, ngp);

      if((ngnp_tail = ngnhp->ngnh_head) != NULL)
         while((ngnp = ngnp_tail->ngn_next) != NULL)
            ngnp_tail = ngnp;

      for(++argv; *argv != NULL; ++argv){
         uz i;

         i = su_cs_len(*argv) +1;
         ngnp = n_alloc(VSTRUCT_SIZEOF(struct a_nm_grp_names, ngn_id) + i);
         if(ngnp_tail != NULL)
            ngnp_tail->ngn_next = ngnp;
         else
            ngnhp->ngnh_head = ngnp;
         ngnp_tail = ngnp;
         ngnp->ngn_next = NULL;
         su_mem_copy(ngnp->ngn_id, *argv, i);
      }
   }
   NYD_OU;
   return rv;
}

FL int
c_unalias(void *v){
   char **argv;
   int rv;
   NYD_IN;

   rv = 0;
   argv = v;

   do if(!a_nm_group_del(a_NM_T_ALIAS, *argv)){
      n_err(_("No such alias: %s\n"), *argv);
      rv = 1;
   }while(*++argv != NULL);
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
/* s-it-mode */
