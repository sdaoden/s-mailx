/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Name lists, alternates and groups: aliases, mailing lists, shortcuts.
 *@ TODO Dynamic hashmaps; names and (these) groups have _nothing_ in common!
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause TODO ISC
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
#define n_FILE nam_a_grp

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

enum a_nag_type{
   /* Main types */
   a_NAG_T_ALTERNATES = 1,
   a_NAG_T_COMMANDALIAS,
   a_NAG_T_ALIAS,
   a_NAG_T_MLIST,
   a_NAG_T_SHORTCUT,
   a_NAG_T_CHARSETALIAS,
   a_NAG_T_FILETYPE,
   a_NAG_T_MASK = 0x1F,

   /* Subtype bits and flags */
   a_NAG_T_SUBSCRIBE = 1u<<6,
   a_NAG_T_REGEX = 1u<<7,

   /* Extended type mask to be able to reflect what we really have; i.e., mlist
    * can have a_NAG_T_REGEX if they are subscribed or not, but `mlsubscribe'
    * should print out only a_NAG_T_MLIST which have the a_NAG_T_SUBSCRIBE
    * attribute set */
   a_NAG_T_PRINT_MASK = a_NAG_T_MASK | a_NAG_T_SUBSCRIBE
};
n_CTA(a_NAG_T_MASK >= a_NAG_T_FILETYPE, "Mask does not cover necessary bits");

struct a_nag_group{
   struct a_nag_group *ng_next;
   ui32_t ng_subclass_off; /* of "subclass" in .ng_id (if any) */
   ui16_t ng_id_len_sub;   /* length of .ng_id: _subclass_off - this */
   ui8_t ng_type;          /* enum a_nag_type */
   /* Identifying name, of variable size.  Dependent on actual "subtype" more
    * data follows thereafter, but note this is always used (i.e., for regular
    * expression entries this is still set to the plain string) */
   char ng_id[n_VFIELD_SIZE(1)];
};
#define a_NAG_GP_TO_SUBCLASS(X,G) \
do{\
   union a_nag_group_subclass {void *gs_vp; char *gs_cp;} a__gs__;\
   a__gs__.gs_cp = &((char*)n_UNCONST(G))[(G)->ng_subclass_off];\
   (X) = a__gs__.gs_vp;\
}while(0)

struct a_nag_grp_names_head{
   struct a_nag_grp_names *ngnh_head;
};

struct a_nag_grp_names{
   struct a_nag_grp_names *ngn_next;
   char ngn_id[n_VFIELD_SIZE(0)];
};

#ifdef HAVE_REGEX
struct a_nag_grp_regex{
   struct a_nag_grp_regex *ngr_last;
   struct a_nag_grp_regex *ngr_next;
   struct a_nag_group *ngr_mygroup; /* xxx because lists use grp_regex*! ?? */
   size_t ngr_hits;                 /* Number of times this group matched */
   regex_t ngr_regex;
};
#endif

struct a_nag_cmd_alias{
   struct str nca_expand;
};

struct a_nag_file_type{
   struct str nft_load;
   struct str nft_save;
};

struct a_nag_group_lookup{
   struct a_nag_group **ngl_htable;
   struct a_nag_group **ngl_slot;
   struct a_nag_group *ngl_slot_last;
   struct a_nag_group *ngl_group;
};

static struct n_file_type const a_nag_OBSOLETE_xz = { /* TODO v15 compat */
   "xz", 2, "xz -cd", sizeof("xz -cd") -1, "xz -cz", sizeof("xz -cz") -1
}, a_nag_OBSOLETE_gz = {
   "gz", 2, "gzip -cd", sizeof("gzip -cd") -1, "gzip -cz", sizeof("gzip -cz") -1
}, a_nag_OBSOLETE_bz2 = {
   "bz2", 3, "bzip2 -cd", sizeof("bzip2 -cd") -1,
   "bzip2 -cz", sizeof("bzip2 -cz") -1
};

/* `alternates' */
static struct a_nag_group *a_nag_alternates_heads[HSHSIZE];

/* `commandalias' */
static struct a_nag_group *a_nag_commandalias_heads[HSHSIZE];

/* `alias' */
static struct a_nag_group *a_nag_alias_heads[HSHSIZE];

/* `mlist', `mlsubscribe'.  Anything is stored in the hashmap.. */
static struct a_nag_group *a_nag_mlist_heads[HSHSIZE];

/* ..but entries which have a_NAG_T_REGEX set are false lookups and will really
 * be accessed via sequential lists instead, which are type-specific for better
 * performance, but also to make it possible to have ".*@xy.org" as a mlist
 * and "(one|two)@xy.org" as a mlsubscription.
 * These lists use a bit of QOS optimization in that a matching group will
 * become relinked as the new list head if its hit count is
 *    (>= ((xy_hits / _xy_size) >> 2))
 * Note that the hit counts only count currently linked in nodes.. */
#ifdef HAVE_REGEX
static struct a_nag_grp_regex *a_nag_mlist_regex;
static struct a_nag_grp_regex *a_nag_mlsub_regex;
static size_t a_nag_mlist_size;
static size_t a_nag_mlist_hits;
static size_t a_nag_mlsub_size;
static size_t a_nag_mlsub_hits;
#endif

/* `shortcut' */
static struct a_nag_group *a_nag_shortcut_heads[HSHSIZE];

/* `charsetalias' */
static struct a_nag_group *a_nag_charsetalias_heads[HSHSIZE];

/* `filetype' */
static struct a_nag_group *a_nag_filetype_heads[HSHSIZE];

/* Same name, while taking care for *allnet*? */
static bool_t a_nag_is_same_name(char const *n1, char const *n2);

/* Mark all (!file, !pipe) nodes with the given name */
static struct name *a_nag_namelist_mark_name(struct name *np, char const *name);

/* Grab a single name (liberal name) */
static char const *a_nag_yankname(char const *ap, char *wbuf,
                        char const *separators, int keepcomms);

/* Extraction multiplexer that splits an input line to names */
static struct name *a_nag_extract1(char const *line, enum gfield ntype,
                        char const *separators, bool_t keepcomms);

/* Recursively expand a alias name.  Limit expansion to some fixed level.
 * Direct recursion is not expanded for convenience */
static struct name *a_nag_gexpand(size_t level, struct name *nlist,
                        struct a_nag_group *ngp, bool_t metoo, int ntype);

/* elide() helper */
static int a_nag_elide_qsort(void const *s1, void const *s2);

/* Lookup a group, return it or NULL, fill in glp anyway */
static struct a_nag_group *a_nag_group_lookup(enum a_nag_type nt,
                           struct a_nag_group_lookup *nglp, char const *id);

/* Easier-to-use wrapper around _group_lookup() */
static struct a_nag_group *a_nag_group_find(enum a_nag_type nt, char const *id);

/* Iteration: go to the first group, which also inits the iterator.  A valid
 * iterator can be stepped via _next().  A NULL return means no (more) groups
 * to be iterated exist, in which case only nglp->ngl_group is set (NULL) */
static struct a_nag_group *a_nag_group_go_first(enum a_nag_type nt,
                        struct a_nag_group_lookup *nglp);
static struct a_nag_group *a_nag_group_go_next(struct a_nag_group_lookup *nglp);

/* Fetch the group id, create it as necessary, fail with NULL if impossible */
static struct a_nag_group *a_nag_group_fetch(enum a_nag_type nt, char const *id,
                        size_t addsz);

/* "Intelligent" delete which handles a "*" id, too;
 * returns a true boolean if a group was deleted, and always succeeds for "*" */
static bool_t a_nag_group_del(enum a_nag_type nt, char const *id);

static struct a_nag_group *a_nag__group_del(struct a_nag_group_lookup *nglp);
static void a_nag__names_del(struct a_nag_group *ngp);

/* Print all groups of the given type, alphasorted, or store in `vput' varname:
 * only in this mode it can return failure */
static bool_t a_nag_group_print_all(enum a_nag_type nt,
               char const *varname);

static int a_nag__group_print_qsorter(void const *a, void const *b);

/* Really print a group, actually.  Or store in vputsp, if set.
 * Return number of written lines */
static size_t a_nag_group_print(struct a_nag_group const *ngp, FILE *fo,
               struct n_string *vputsp);

/* Multiplexers for list and subscribe commands */
static int a_nag_mlmux(enum a_nag_type nt, char const **argv);
static int a_nag_unmlmux(enum a_nag_type nt, char const **argv);

/* Relinkers for the sequential match lists */
#ifdef HAVE_REGEX
static void a_nag_mlmux_linkin(struct a_nag_group *ngp);
static void a_nag_mlmux_linkout(struct a_nag_group *ngp);
# define a_NAG_MLMUX_LINKIN(GP) \
   do if((GP)->ng_type & a_NAG_T_REGEX) a_nag_mlmux_linkin(GP); while(0)
# define a_NAG_MLMUX_LINKOUT(GP) \
   do if((GP)->ng_type & a_NAG_T_REGEX) a_nag_mlmux_linkout(GP); while(0)
#else
# define a_NAG_MLMUX_LINKIN(GP)
# define a_NAG_MLMUX_LINKOUT(GP)
#endif

static bool_t
a_nag_is_same_name(char const *n1, char const *n2){
   bool_t rv;
   char c1, c2, c1r, c2r;
   NYD2_IN;

   if(ok_blook(allnet)){
      for(;; ++n1, ++n2){
         c1 = *n1;
         c1 = lowerconv(c1);
         c1r = (c1 == '\0' || c1 == '@');
         c2 = *n2;
         c2 = lowerconv(c2);
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
      rv = !asccasecmp(n1, n2);
   NYD2_OU;
   return rv;
}

static struct name *
a_nag_namelist_mark_name(struct name *np, char const *name){
   struct name *p;
   NYD2_IN;

   for(p = np; p != NULL; p = p->n_flink)
      if(!(p->n_type & GDEL) &&
            !(p->n_flags & (((ui32_t)SI32_MIN) | NAME_ADDRSPEC_ISFILE |
               NAME_ADDRSPEC_ISPIPE)) &&
            a_nag_is_same_name(p->n_name, name))
         p->n_flags |= (ui32_t)SI32_MIN;
   NYD2_OU;
   return np;
}

static char const *
a_nag_yankname(char const *ap, char *wbuf, char const *separators,
   int keepcomms)
{
   char const *cp;
   char *wp, c, inquote, lc, lastsp;
   NYD_IN;

   *(wp = wbuf) = '\0';

   /* Skip over intermediate list trash, as in ".org>  ,  <xy@zz.org>" */
   for (c = *ap; blankchar(c) || c == ','; c = *++ap)
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
      if (strchr(separators, c) != NULL)
         break;

      lc = lastsp;
      lastsp = blankchar(c);
      if (!lastsp || !lc)
         *wp++ = c;
   }
   if (blankchar(lc))
      --wp;

   *wp = '\0';
jleave:
   NYD_OU;
   return cp;
}

static struct name *
a_nag_extract1(char const *line, enum gfield ntype, char const *separators,
   bool_t keepcomms)
{
   struct name *topp, *np, *t;
   char const *cp;
   char *nbuf;
   NYD_IN;

   topp = NULL;
   if (line == NULL || *line == '\0')
      goto jleave;

   np = NULL;
   cp = line;
   nbuf = n_alloc(strlen(line) +1);
   while ((cp = a_nag_yankname(cp, nbuf, separators, keepcomms)) != NULL) {
      t = nalloc(nbuf, ntype);
      if (topp == NULL)
         topp = t;
      else
         np->n_flink = t;
      t->n_blink = np;
      np = t;
   }
   n_free(nbuf);
jleave:
   NYD_OU;
   return topp;
}

static struct name *
a_nag_gexpand(size_t level, struct name *nlist, struct a_nag_group *ngp,
      bool_t metoo, int ntype){
   struct a_nag_grp_names *ngnp;
   struct name *nlist_tail;
   char const *logname;
   struct a_nag_grp_names_head *ngnhp;
   NYD2_IN;

   if(UICMP(z, level++, >, n_ALIAS_MAXEXP)){
      n_err(_("Expanding alias to depth larger than %d\n"), n_ALIAS_MAXEXP);
      goto jleave;
   }

   a_NAG_GP_TO_SUBCLASS(ngnhp, ngp);
   logname = ok_vlook(LOGNAME);

   for(ngnp = ngnhp->ngnh_head; ngnp != NULL; ngnp = ngnp->ngn_next){
      struct a_nag_group *xngp;
      char *cp;

      cp = ngnp->ngn_id;

      if(!strcmp(cp, ngp->ng_id))
         goto jas_is;

      if((xngp = a_nag_group_find(a_NAG_T_ALIAS, cp)) != NULL){
         /* For S-nail(1), the "alias" may *be* the sender in that a name maps
          * to a full address specification; aliases cannot be empty */
         struct a_nag_grp_names_head *xngnhp;

         a_NAG_GP_TO_SUBCLASS(xngnhp, xngp);

         assert(xngnhp->ngnh_head != NULL);
         if(metoo || xngnhp->ngnh_head->ngn_next != NULL ||
               !a_nag_is_same_name(cp, logname))
            nlist = a_nag_gexpand(level, nlist, xngp, metoo, ntype);
         continue;
      }

      /* Here we should allow to expand to itself if only person in alias */
jas_is:
      if(metoo || ngnhp->ngnh_head->ngn_next == NULL ||
            !a_nag_is_same_name(cp, logname)){
         struct name *np;

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
a_nag_elide_qsort(void const *s1, void const *s2){
   struct name const * const *np1, * const *np2;
   int rv;
   NYD2_IN;

   np1 = s1;
   np2 = s2;
   if(!(rv = asccasecmp((*np1)->n_name, (*np2)->n_name))){
      n_LCTAV(GTO < GCC && GCC < GBCC);
      rv = ((*np1)->n_type & (GTO | GCC | GBCC)) -
            ((*np2)->n_type & (GTO | GCC | GBCC));
   }
   NYD2_OU;
   return rv;
}

static struct a_nag_group *
a_nag_group_lookup(enum a_nag_type nt, struct a_nag_group_lookup *nglp,
      char const *id){
   char c1;
   struct a_nag_group *lngp, *ngp;
   bool_t icase;
   NYD2_IN;

   icase = FAL0;

   /* C99 */{
      ui32_t h;
      struct a_nag_group **ngpa;

      switch((nt &= a_NAG_T_MASK)){
      case a_NAG_T_ALTERNATES:
         ngpa = a_nag_alternates_heads;
         icase = TRU1;
         break;
      default:
      case a_NAG_T_COMMANDALIAS:
         ngpa = a_nag_commandalias_heads;
         break;
      case a_NAG_T_ALIAS:
         ngpa = a_nag_alias_heads;
         break;
      case a_NAG_T_MLIST:
         ngpa = a_nag_mlist_heads;
         icase = TRU1;
         break;
      case a_NAG_T_SHORTCUT:
         ngpa = a_nag_shortcut_heads;
         break;
      case a_NAG_T_CHARSETALIAS:
         ngpa = a_nag_charsetalias_heads;
         icase = TRU1;
         break;
      case a_NAG_T_FILETYPE:
         ngpa = a_nag_filetype_heads;
         icase = TRU1;
         break;
      }

      nglp->ngl_htable = ngpa;
      h = icase ? n_torek_ihash(id) : n_torek_hash(id);
      ngp = *(nglp->ngl_slot = &ngpa[h % HSHSIZE]);
   }

   lngp = NULL;
   c1 = *id++;

   if(icase){
      c1 = lowerconv(c1);
      for(; ngp != NULL; lngp = ngp, ngp = ngp->ng_next)
         if((ngp->ng_type & a_NAG_T_MASK) == nt && *ngp->ng_id == c1 &&
               !asccasecmp(&ngp->ng_id[1], id))
            break;
   }else{
      for(; ngp != NULL; lngp = ngp, ngp = ngp->ng_next)
         if((ngp->ng_type & a_NAG_T_MASK) == nt && *ngp->ng_id == c1 &&
               !strcmp(&ngp->ng_id[1], id))
            break;
   }

   nglp->ngl_slot_last = lngp;
   nglp->ngl_group = ngp;
   NYD2_OU;
   return ngp;
}

static struct a_nag_group *
a_nag_group_find(enum a_nag_type nt, char const *id){
   struct a_nag_group_lookup ngl;
   struct a_nag_group *ngp;
   NYD2_IN;

   ngp = a_nag_group_lookup(nt, &ngl, id);
   NYD2_OU;
   return ngp;
}

static struct a_nag_group *
a_nag_group_go_first(enum a_nag_type nt, struct a_nag_group_lookup *nglp){
   size_t i;
   struct a_nag_group **ngpa, *ngp;
   NYD2_IN;

   switch((nt &= a_NAG_T_MASK)){
   case a_NAG_T_ALTERNATES:
      ngpa = a_nag_alternates_heads;
      break;
   default:
   case a_NAG_T_COMMANDALIAS:
      ngpa = a_nag_commandalias_heads;
      break;
   case a_NAG_T_ALIAS:
      ngpa = a_nag_alias_heads;
      break;
   case a_NAG_T_MLIST:
      ngpa = a_nag_mlist_heads;
      break;
   case a_NAG_T_SHORTCUT:
      ngpa = a_nag_shortcut_heads;
      break;
   case a_NAG_T_CHARSETALIAS:
      ngpa = a_nag_charsetalias_heads;
      break;
   case a_NAG_T_FILETYPE:
      ngpa = a_nag_filetype_heads;
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

static struct a_nag_group *
a_nag_group_go_next(struct a_nag_group_lookup *nglp){
   struct a_nag_group *ngp, **ngpa;
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

static struct a_nag_group *
a_nag_group_fetch(enum a_nag_type nt, char const *id, size_t addsz){
   struct a_nag_group_lookup ngl;
   struct a_nag_group *ngp;
   size_t l, i;
   NYD2_IN;

   if((ngp = a_nag_group_lookup(nt, &ngl, id)) != NULL)
      goto jleave;

   l = strlen(id) +1;
   if(UIZ_MAX - n_ALIGN(l) <=
         n_ALIGN(n_VSTRUCT_SIZEOF(struct a_nag_group, ng_id)))
      goto jleave;

   i = n_ALIGN(n_VSTRUCT_SIZEOF(struct a_nag_group, ng_id) + l);
   switch(nt & a_NAG_T_MASK){
   case a_NAG_T_ALTERNATES:
   case a_NAG_T_SHORTCUT:
   case a_NAG_T_CHARSETALIAS:
   default:
      break;
   case a_NAG_T_COMMANDALIAS:
      addsz += sizeof(struct a_nag_cmd_alias);
      break;
   case a_NAG_T_ALIAS:
      addsz += sizeof(struct a_nag_grp_names_head);
      break;
   case a_NAG_T_MLIST:
#ifdef HAVE_REGEX
      if(n_is_maybe_regex(id)){
         addsz = sizeof(struct a_nag_grp_regex);
         nt |= a_NAG_T_REGEX;
      }
#endif
      break;
   case a_NAG_T_FILETYPE:
      addsz += sizeof(struct a_nag_file_type);
      break;
   }
   if(UIZ_MAX - i < addsz || UI32_MAX <= i || UI16_MAX < i - l)
      goto jleave;

   ngp = n_alloc(i + addsz);
   memcpy(ngp->ng_id, id, l);
   ngp->ng_subclass_off = (ui32_t)i;
   ngp->ng_id_len_sub = (ui16_t)(i - --l);
   ngp->ng_type = nt;
   switch(nt & a_NAG_T_MASK){
   case a_NAG_T_ALTERNATES:
   case a_NAG_T_MLIST:
   case a_NAG_T_CHARSETALIAS:
   case a_NAG_T_FILETYPE:{
      char *cp, c;

      for(cp = ngp->ng_id; (c = *cp) != '\0'; ++cp)
         *cp = lowerconv(c);
      }break;
   default:
      break;
   }

   if((nt & a_NAG_T_MASK) == a_NAG_T_ALIAS){
      struct a_nag_grp_names_head *ngnhp;

      a_NAG_GP_TO_SUBCLASS(ngnhp, ngp);
      ngnhp->ngnh_head = NULL;
   }
#ifdef HAVE_REGEX
   else if(nt & a_NAG_T_REGEX){
      int s;
      struct a_nag_grp_regex *ngrp;

      a_NAG_GP_TO_SUBCLASS(ngrp, ngp);

      if((s = regcomp(&ngrp->ngr_regex, id,
            REG_EXTENDED | REG_ICASE | REG_NOSUB)) != 0){
         n_err(_("Invalid regular expression: %s: %s\n"),
            n_shexp_quote_cp(id, FAL0), n_regex_err_to_doc(NULL, s));
         n_free(ngp);
         ngp = NULL;
         goto jleave;
      }
      ngrp->ngr_mygroup = ngp;
      a_nag_mlmux_linkin(ngp);
   }
#endif /* HAVE_REGEX */

   ngp->ng_next = *ngl.ngl_slot;
   *ngl.ngl_slot = ngp;
jleave:
   NYD2_OU;
   return ngp;
}

static bool_t
a_nag_group_del(enum a_nag_type nt, char const *id){
   struct a_nag_group_lookup ngl;
   struct a_nag_group *ngp;
   enum a_nag_type xnt;
   NYD2_IN;

   xnt = nt & a_NAG_T_MASK;

   /* Delete 'em all? */
   if(id[0] == '*' && id[1] == '\0'){
      for(ngp = a_nag_group_go_first(nt, &ngl); ngp != NULL;)
         ngp = ((ngp->ng_type & a_NAG_T_MASK) == xnt) ? a_nag__group_del(&ngl)
               : a_nag_group_go_next(&ngl);
      ngp = (struct a_nag_group*)TRU1;
   }else if((ngp = a_nag_group_lookup(nt, &ngl, id)) != NULL){
      if(ngp->ng_type & xnt)
         a_nag__group_del(&ngl);
      else
         ngp = NULL;
   }
   NYD2_OU;
   return (ngp != NULL);
}

static struct a_nag_group *
a_nag__group_del(struct a_nag_group_lookup *nglp){
   struct a_nag_group *x, *ngp;
   NYD2_IN;

   /* Overly complicated: link off this node, step ahead to next.. */
   x = nglp->ngl_group;
   if((ngp = nglp->ngl_slot_last) != NULL)
      ngp = (ngp->ng_next = x->ng_next);
   else{
      nglp->ngl_slot_last = NULL;
      ngp = (*nglp->ngl_slot = x->ng_next);

      if(ngp == NULL){
         struct a_nag_group **ngpa;

         for(ngpa = &nglp->ngl_htable[HSHSIZE]; ++nglp->ngl_slot < ngpa;)
            if((ngp = *nglp->ngl_slot) != NULL)
               break;
      }
   }
   nglp->ngl_group = ngp;

   if((x->ng_type & a_NAG_T_MASK) == a_NAG_T_ALIAS)
      a_nag__names_del(x);
#ifdef HAVE_REGEX
   else if(x->ng_type & a_NAG_T_REGEX){
      struct a_nag_grp_regex *ngrp;

      a_NAG_GP_TO_SUBCLASS(ngrp, x);

      regfree(&ngrp->ngr_regex);
      a_nag_mlmux_linkout(x);
   }
#endif

   n_free(x);
   NYD2_OU;
   return ngp;
}

static void
a_nag__names_del(struct a_nag_group *ngp){
   struct a_nag_grp_names_head *ngnhp;
   struct a_nag_grp_names *ngnp;
   NYD2_IN;

   a_NAG_GP_TO_SUBCLASS(ngnhp, ngp);

   for(ngnp = ngnhp->ngnh_head; ngnp != NULL;){
      struct a_nag_grp_names *x;

      x = ngnp;
      ngnp = ngnp->ngn_next;
      n_free(x);
   }
   NYD2_OU;
}

static bool_t
a_nag_group_print_all(enum a_nag_type nt, char const *varname){
   struct n_string s;
   size_t lines;
   FILE *fp;
   char const **ida;
   struct a_nag_group const *ngp;
   ui32_t h, i;
   struct a_nag_group **ngpa;
   char const *tname;
   enum a_nag_type xnt;
   NYD_IN;

   if(varname != NULL)
      n_string_creat_auto(&s);

   xnt = nt & a_NAG_T_PRINT_MASK;

   switch(xnt & a_NAG_T_MASK){
   case a_NAG_T_ALTERNATES:
      tname = "alternates";
      ngpa = a_nag_alternates_heads;
      break;
   default:
   case a_NAG_T_COMMANDALIAS:
      tname = "commandalias";
      ngpa = a_nag_commandalias_heads;
      break;
   case a_NAG_T_ALIAS:
      tname = "alias";
      ngpa = a_nag_alias_heads;
      break;
   case a_NAG_T_MLIST:
      tname = "mlist";
      ngpa = a_nag_mlist_heads;
      break;
   case a_NAG_T_SHORTCUT:
      tname = "shortcut";
      ngpa = a_nag_shortcut_heads;
      break;
   case a_NAG_T_CHARSETALIAS:
      tname = "charsetalias";
      ngpa = a_nag_charsetalias_heads;
      break;
   case a_NAG_T_FILETYPE:
      tname = "filetype";
      ngpa = a_nag_filetype_heads;
      break;
   }

   /* Count entries */
   for(i = h = 0; h < HSHSIZE; ++h)
      for(ngp = ngpa[h]; ngp != NULL; ngp = ngp->ng_next)
         if((ngp->ng_type & a_NAG_T_PRINT_MASK) == xnt)
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
         if((ngp->ng_type & a_NAG_T_PRINT_MASK) == xnt)
            ida[i++] = ngp->ng_id;
   if(i > 1)
      qsort(ida, i, sizeof *ida, &a_nag__group_print_qsorter);
   ida[i] = NULL;

   if(varname != NULL)
      fp = NULL;
   else if((fp = Ftmp(NULL, "nagprint", OF_RDWR | OF_UNLINK | OF_REGISTER)
         ) == NULL)
      fp = n_stdout;

   /* Create visual result */
   lines = 0;

   switch(xnt & a_NAG_T_MASK){
   case a_NAG_T_ALTERNATES:
      if(fp != NULL){
         fputs(tname, fp);
         lines = 1;
      }
      break;
   default:
      break;
   }

   for(i = 0; ida[i] != NULL; ++i)
      lines += a_nag_group_print(a_nag_group_find(nt, ida[i]), fp, &s);

#ifdef HAVE_REGEX
   if(varname == NULL && (nt & a_NAG_T_MASK) == a_NAG_T_MLIST){
      if(nt & a_NAG_T_SUBSCRIBE)
         i = (ui32_t)a_nag_mlsub_size, h = (ui32_t)a_nag_mlsub_hits;
      else
         i = (ui32_t)a_nag_mlist_size, h = (ui32_t)a_nag_mlist_hits;

      if(i > 0 && (n_poption & n_PO_D_V)){
         assert(fp != NULL);
         fprintf(fp, _("# %s list regex(7) total: %u entries, %u hits\n"),
            (nt & a_NAG_T_SUBSCRIBE ? _("Subscribed") : _("Non-subscribed")),
            i, h);
         ++lines;
      }
   }
#endif

   switch(xnt & a_NAG_T_MASK){
   case a_NAG_T_ALTERNATES:
      if(fp != NULL){
         putc('\n', fp);
         assert(lines == 1);
      }
      break;
   default:
      break;
   }

   if(varname == NULL && fp != n_stdout){
      assert(fp != NULL);
      page_or_print(fp, lines);
      Fclose(fp);
   }

jleave:
   if(varname != NULL){
      tname = n_string_cp(&s);
      if(n_var_vset(varname, (uintptr_t)tname))
         varname = NULL;
      else
         n_pstate_err_no = n_ERR_NOTSUP;
   }
   NYD_OU;
   return (varname == NULL);
}

static int
a_nag__group_print_qsorter(void const *a, void const *b){
   int rv;
   NYD2_IN;

   rv = strcmp(*(char**)n_UNCONST(a), *(char**)n_UNCONST(b));
   NYD2_OU;
   return rv;
}

static size_t
a_nag_group_print(struct a_nag_group const *ngp, FILE *fo,
      struct n_string *vputsp){
   char const *cp;
   size_t rv;
   NYD2_IN;

   rv = 1;

   switch(ngp->ng_type & a_NAG_T_MASK){
   case a_NAG_T_ALTERNATES:{
      if(fo != NULL)
         fprintf(fo, " %s", ngp->ng_id);
      else{
         if(vputsp->s_len > 0)
            vputsp = n_string_push_c(vputsp, ' ');
         /*vputsp =*/ n_string_push_cp(vputsp, ngp->ng_id);
      }
      rv = 0;
      }break;
   case a_NAG_T_COMMANDALIAS:{
      struct a_nag_cmd_alias *ncap;

      assert(fo != NULL); /* xxx no vput yet */
      a_NAG_GP_TO_SUBCLASS(ncap, ngp);
      fprintf(fo, "commandalias %s %s\n",
         n_shexp_quote_cp(ngp->ng_id, TRU1),
         n_shexp_quote_cp(ncap->nca_expand.s, TRU1));
      }break;
   case a_NAG_T_ALIAS:{
      struct a_nag_grp_names_head *ngnhp;
      struct a_nag_grp_names *ngnp;

      assert(fo != NULL); /* xxx no vput yet */
      fprintf(fo, "alias %s ", ngp->ng_id);

      a_NAG_GP_TO_SUBCLASS(ngnhp, ngp);
      if((ngnp = ngnhp->ngnh_head) != NULL) { /* xxx always 1+ entries */
         do{
            struct a_nag_grp_names *x;

            x = ngnp;
            ngnp = ngnp->ngn_next;
            fprintf(fo, " \"%s\"", string_quote(x->ngn_id)); /* TODO shexp */
         }while(ngnp != NULL);
      }
      putc('\n', fo);
      }break;
   case a_NAG_T_MLIST:
      assert(fo != NULL); /* xxx no vput yet */
#ifdef HAVE_REGEX
      if((ngp->ng_type & a_NAG_T_REGEX) && (n_poption & n_PO_D_V)){
         size_t i;
         struct a_nag_grp_regex *lp, *ngrp;

         lp = (ngp->ng_type & a_NAG_T_SUBSCRIBE ? a_nag_mlsub_regex
               : a_nag_mlist_regex);
         a_NAG_GP_TO_SUBCLASS(ngrp, ngp);
         for(i = 1; lp != ngrp; lp = lp->ngr_next)
            ++i;
         fprintf(fo, "# regex(7): hits %" PRIuZ ", sort %" PRIuZ ".\n  ",
            ngrp->ngr_hits, i);
         ++rv;
      }
#endif
      fprintf(fo, "wysh %s %s\n",
         (ngp->ng_type & a_NAG_T_SUBSCRIBE ? "mlsubscribe" : "mlist"),
         n_shexp_quote_cp(ngp->ng_id, TRU1));
      break;
   case a_NAG_T_SHORTCUT:
      assert(fo != NULL); /* xxx no vput yet */
      a_NAG_GP_TO_SUBCLASS(cp, ngp);
      fprintf(fo, "wysh shortcut %s %s\n",
         ngp->ng_id, n_shexp_quote_cp(cp, TRU1));
      break;
   case a_NAG_T_CHARSETALIAS:
      assert(fo != NULL); /* xxx no vput yet */
      a_NAG_GP_TO_SUBCLASS(cp, ngp);
      fprintf(fo, "charsetalias %s %s\n",
         n_shexp_quote_cp(ngp->ng_id, TRU1), n_shexp_quote_cp(cp, TRU1));
      break;
   case a_NAG_T_FILETYPE:{
      struct a_nag_file_type *nftp;

      assert(fo != NULL); /* xxx no vput yet */
      a_NAG_GP_TO_SUBCLASS(nftp, ngp);
      fprintf(fo, "filetype %s %s %s\n",
         n_shexp_quote_cp(ngp->ng_id, TRU1),
         n_shexp_quote_cp(nftp->nft_load.s, TRU1),
         n_shexp_quote_cp(nftp->nft_save.s, TRU1));
      }break;
   }
   NYD2_OU;
   return rv;
}

static int
a_nag_mlmux(enum a_nag_type nt, char const **argv){
   struct a_nag_group *ngp;
   char const *ecp;
   int rv;
   NYD2_IN;

   rv = 0;
   n_UNINIT(ecp, NULL);

   if(*argv == NULL)
      a_nag_group_print_all(nt, NULL);
   else do{
      if((ngp = a_nag_group_find(nt, *argv)) != NULL){
         if(nt & a_NAG_T_SUBSCRIBE){
            if(!(ngp->ng_type & a_NAG_T_SUBSCRIBE)){
               a_NAG_MLMUX_LINKOUT(ngp);
               ngp->ng_type |= a_NAG_T_SUBSCRIBE;
               a_NAG_MLMUX_LINKIN(ngp);
            }else{
               ecp = N_("Mailing-list already `mlsubscribe'd: %s\n");
               goto jerr;
            }
         }else{
            ecp = N_("Mailing-list already `mlist'ed: %s\n");
            goto jerr;
         }
      }else if(a_nag_group_fetch(nt, *argv, 0) == NULL){
         ecp = N_("Failed to create storage for mailing-list: %s\n");
jerr:
         n_err(V_(ecp), n_shexp_quote_cp(*argv, FAL0));
         rv = 1;
      }
   }while(*++argv != NULL);

   NYD2_OU;
   return rv;
}

static int
a_nag_unmlmux(enum a_nag_type nt, char const **argv){
   struct a_nag_group *ngp;
   int rv;
   NYD2_IN;

   rv = 0;

   for(; *argv != NULL; ++argv){
      if(nt & a_NAG_T_SUBSCRIBE){
         struct a_nag_group_lookup ngl;
         bool_t isaster;

         if(!(isaster = (**argv == '*')))
            ngp = a_nag_group_find(nt, *argv);
         else if((ngp = a_nag_group_go_first(nt, &ngl)) == NULL)
            continue;
         else if(ngp != NULL && !(ngp->ng_type & a_NAG_T_SUBSCRIBE))
            goto jaster_entry;

         if(ngp != NULL){
jaster_redo:
            if(ngp->ng_type & a_NAG_T_SUBSCRIBE){
               a_NAG_MLMUX_LINKOUT(ngp);
               ngp->ng_type &= ~a_NAG_T_SUBSCRIBE;
               a_NAG_MLMUX_LINKIN(ngp);

               if(isaster){
jaster_entry:
                  while((ngp = a_nag_group_go_next(&ngl)) != NULL &&
                        !(ngp->ng_type & a_NAG_T_SUBSCRIBE))
                     ;
                  if(ngp != NULL)
                     goto jaster_redo;
               }
            }else{
               n_err(_("Mailing-list not `mlsubscribe'd: %s\n"),
                  n_shexp_quote_cp(*argv, FAL0));
               rv = 1;
            }
            continue;
         }
      }else if(a_nag_group_del(nt, *argv))
         continue;
      n_err(_("No such mailing-list: %s\n"), n_shexp_quote_cp(*argv, FAL0));
      rv = 1;
   }
   NYD2_OU;
   return rv;
}

#ifdef HAVE_REGEX
static void
a_nag_mlmux_linkin(struct a_nag_group *ngp){
   struct a_nag_grp_regex **lpp, *ngrp, *lhp;
   NYD2_IN;

   if(ngp->ng_type & a_NAG_T_SUBSCRIBE){
      lpp = &a_nag_mlsub_regex;
      ++a_nag_mlsub_size;
   }else{
      lpp = &a_nag_mlist_regex;
      ++a_nag_mlist_size;
   }

   a_NAG_GP_TO_SUBCLASS(ngrp, ngp);

   if((lhp = *lpp) != NULL){
      (ngrp->ngr_last = lhp->ngr_last)->ngr_next = ngrp;
      (ngrp->ngr_next = lhp)->ngr_last = ngrp;
   }else
      *lpp = ngrp->ngr_last = ngrp->ngr_next = ngrp;
   ngrp->ngr_hits = 0;
   NYD2_OU;
}

static void
a_nag_mlmux_linkout(struct a_nag_group *ngp){
   struct a_nag_grp_regex *ngrp, **lpp;
   NYD2_IN;

   a_NAG_GP_TO_SUBCLASS(ngrp, ngp);

   if(ngp->ng_type & a_NAG_T_SUBSCRIBE){
      lpp = &a_nag_mlsub_regex;
      --a_nag_mlsub_size;
      a_nag_mlsub_hits -= ngrp->ngr_hits;
   }else{
      lpp = &a_nag_mlist_regex;
      --a_nag_mlist_size;
      a_nag_mlist_hits -= ngrp->ngr_hits;
   }

   if(ngrp->ngr_next == ngrp)
      *lpp = NULL;
   else{
      (ngrp->ngr_last->ngr_next = ngrp->ngr_next)->ngr_last = ngrp->ngr_last;
      if(*lpp == ngrp)
         *lpp = ngrp->ngr_next;
   }
   NYD2_OU;
}
#endif /* HAVE_REGEX */

FL struct name *
nalloc(char const *str, enum gfield ntype)
{
   struct n_addrguts ag;
   struct str in, out;
   struct name *np;
   NYD_IN;
   assert(!(ntype & GFULLEXTRA) || (ntype & GFULL) != 0);

   str = n_addrspec_with_guts(&ag, str,
         ((ntype & (GFULL | GSKIN | GREF)) != 0), FAL0);
   if(str == NULL){
      /*
      np = NULL; TODO We cannot return NULL,
      goto jleave; TODO thus handle failures in here!
      */
      str = ag.ag_input;
   }

   if (!(ag.ag_n_flags & NAME_NAME_SALLOC)) {
      ag.ag_n_flags |= NAME_NAME_SALLOC;
      np = n_autorec_alloc(sizeof(*np) + ag.ag_slen +1);
      memcpy(np + 1, ag.ag_skinned, ag.ag_slen +1);
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
#ifdef HAVE_IDNA
            && !(ag.ag_n_flags & NAME_IDNA)
#endif
      )
         goto jleave;
      if (ag.ag_n_flags & NAME_ADDRSPEC_ISFILEORPIPE)
         goto jleave;

      /* n_fullextra is only the complete name part without address.
       * Beware of "-r '<abc@def>'", don't treat that as FULLEXTRA */
      if ((ntype & GFULLEXTRA) && ag.ag_ilen > ag.ag_slen + 2) {
         size_t s = ag.ag_iaddr_start, e = ag.ag_iaddr_aend, i;
         char const *cp;

         if (s == 0 || str[--s] != '<' || str[e++] != '>')
            goto jskipfullextra;
         i = ag.ag_ilen - e;
         in.s = n_lofi_alloc(s + 1 + i +1);
         while(s > 0 && blankchar(str[s - 1]))
            --s;
         memcpy(in.s, str, s);
         if (i > 0) {
            in.s[s++] = ' ';
            while (blankchar(str[e])) {
               ++e;
               if (--i == 0)
                  break;
            }
            if (i > 0)
               memcpy(&in.s[s], &str[e], i);
         }
         s += i;
         in.s[in.l = s] = '\0';
         mime_fromhdr(&in, &out, /* TODO TD_ISPR |*/ TD_ICONV);

         for (cp = out.s, i = out.l; i > 0 && spacechar(*cp); --i, ++cp)
            ;
         while (i > 0 && spacechar(cp[i - 1]))
            --i;
         np->n_fullextra = savestrbuf(cp, i);

         n_lofi_free(in.s);
         n_free(out.s);
      }
jskipfullextra:

      /* n_fullname depends on IDNA conversion */
#ifdef HAVE_IDNA
      if (!(ag.ag_n_flags & NAME_IDNA)) {
#endif
         in.s = n_UNCONST(str);
         in.l = ag.ag_ilen;
#ifdef HAVE_IDNA
      } else {
         /* The domain name was IDNA and has been converted.  We also have to
          * ensure that the domain name in .n_fullname is replaced with the
          * converted version, since MIME doesn't perform encoding of addrs */
         /* TODO This definetely doesn't belong here! */
         size_t l = ag.ag_iaddr_start,
            lsuff = ag.ag_ilen - ag.ag_iaddr_aend;
         in.s = n_lofi_alloc(l + ag.ag_slen + lsuff +1);
         memcpy(in.s, str, l);
         memcpy(in.s + l, ag.ag_skinned, ag.ag_slen);
         l += ag.ag_slen;
         memcpy(in.s + l, str + ag.ag_iaddr_aend, lsuff);
         l += lsuff;
         in.s[l] = '\0';
         in.l = l;
      }
#endif
      mime_fromhdr(&in, &out, /* TODO TD_ISPR |*/ TD_ICONV);
      np->n_fullname = savestr(out.s);
      n_free(out.s);
#ifdef HAVE_IDNA
      if (ag.ag_n_flags & NAME_IDNA)
         n_lofi_free(in.s);
#endif
   }
jleave:
   NYD_OU;
   return np;
}

FL struct name *
nalloc_fcc(char const *file){
   struct name *nnp;
   NYD_IN;

   nnp = n_autorec_alloc(sizeof *nnp);
   nnp->n_flink = nnp->n_blink = NULL;
   nnp->n_type = GBCC | GBCC_IS_FCC; /* xxx Bcc: <- namelist_vaporise_head */
   nnp->n_flags = NAME_NAME_SALLOC | NAME_SKINNED | NAME_ADDRSPEC_ISFILE;
   nnp->n_fullname = nnp->n_name = savestr(file);
   nnp->n_fullextra = NULL;
   NYD_OU;
   return nnp;
}

FL struct name *
ndup(struct name *np, enum gfield ntype)
{
   struct name *nnp;
   NYD_IN;

   if ((ntype & (GFULL | GSKIN)) && !(np->n_flags & NAME_SKINNED)) {
      nnp = nalloc(np->n_name, ntype);
      goto jleave;
   }

   nnp = n_autorec_alloc(sizeof *np);
   nnp->n_flink = nnp->n_blink = NULL;
   nnp->n_type = ntype;
   nnp->n_flags = np->n_flags | NAME_NAME_SALLOC;
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

FL struct name *
cat(struct name *n1, struct name *n2){
   struct name *tail;
   NYD2_IN;

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
   NYD2_OU;
   return tail;
}

FL struct name *
n_namelist_dup(struct name const *np, enum gfield ntype){
   struct name *nlist, *xnp;
   NYD2_IN;

   for(nlist = xnp = NULL; np != NULL; np = np->n_flink){
      struct name *x;

      if(!(np->n_type & GDEL)){
         x = ndup(n_UNCONST(np), (np->n_type & ~GMASK) | ntype);
         if((x->n_blink = xnp) == NULL)
            nlist = x;
         else
            xnp->n_flink = x;
         xnp = x;
      }
   }
   NYD2_OU;
   return nlist;
}

FL ui32_t
count(struct name const *np)
{
   ui32_t c;
   NYD_IN;

   for (c = 0; np != NULL; np = np->n_flink)
      if (!(np->n_type & GDEL))
         ++c;
   NYD_OU;
   return c;
}

FL ui32_t
count_nonlocal(struct name const *np)
{
   ui32_t c;
   NYD_IN;

   for (c = 0; np != NULL; np = np->n_flink)
      if (!(np->n_type & GDEL) && !(np->n_flags & NAME_ADDRSPEC_ISFILEORPIPE))
         ++c;
   NYD_OU;
   return c;
}

FL struct name *
extract(char const *line, enum gfield ntype)
{
   struct name *rv;
   NYD_IN;

   rv = a_nag_extract1(line, ntype, " \t,", 0);
   NYD_OU;
   return rv;
}

FL struct name *
lextract(char const *line, enum gfield ntype)
{
   char *cp;
   struct name *rv;
   NYD_IN;

   if(!(ntype & GSHEXP_PARSE_HACK) || !(expandaddr_to_eaf() & EAF_SHEXP_PARSE))
      cp = NULL;
   else{
      struct str sin;
      struct n_string s_b, *sp;
      enum n_shexp_state shs;

      n_autorec_relax_create();
      sp = n_string_creat_auto(&s_b);
      sin.s = n_UNCONST(line); /* logical */
      sin.l = UIZ_MAX;
      shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG |
            n_SHEXP_PARSE_IGNORE_EMPTY | n_SHEXP_PARSE_QUOTE_AUTO_FIXED |
            n_SHEXP_PARSE_QUOTE_AUTO_DSQ), sp, &sin, NULL);
      if(!(shs & n_SHEXP_STATE_ERR_MASK) && (shs & n_SHEXP_STATE_STOP)){
         line = cp = n_lofi_alloc(sp->s_len +1);
         memcpy(cp, n_string_cp(sp), sp->s_len +1);
      }else
         line = cp = NULL;
      n_autorec_relax_gut();
   }

   rv = ((line != NULL && strpbrk(line, ",\"\\(<|"))
         ? a_nag_extract1(line, ntype, ",", 1) : extract(line, ntype));

   if(cp != NULL)
      n_lofi_free(cp);
   NYD_OU;
   return rv;
}

FL char *
detract(struct name *np, enum gfield ntype)
{
   char *topp, *cp;
   struct name *p;
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
      s += strlen(flags & GNAMEONLY ? p->n_name : p->n_fullname) +1;
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
      cp = sstpcpy(cp, (flags & GNAMEONLY ? p->n_name : p->n_fullname));
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

FL struct name *
grab_names(enum n_go_input_flags gif, char const *field, struct name *np,
      int comma, enum gfield gflags)
{
   struct name *nq;
   NYD_IN;

jloop:
   np = lextract(n_go_input_cp(gif, field, detract(np, comma)), gflags);
   for (nq = np; nq != NULL; nq = nq->n_flink)
      if (is_addr_invalid(nq, EACM_NONE))
         goto jloop;
   NYD_OU;
   return np;
}

FL bool_t
name_is_same_domain(struct name const *n1, struct name const *n2)
{
   char const *d1, *d2;
   bool_t rv;
   NYD_IN;

   d1 = strrchr(n1->n_name, '@');
   d2 = strrchr(n2->n_name, '@');

   rv = (d1 != NULL && d2 != NULL) ? !asccasecmp(++d1, ++d2) : FAL0;

   NYD_OU;
   return rv;
}

FL struct name *
checkaddrs(struct name *np, enum expand_addr_check_mode eacm,
   si8_t *set_on_error)
{
   struct name *n;
   NYD_IN;

   for (n = np; n != NULL; n = n->n_flink) {
      si8_t rv;

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

FL struct name *
n_namelist_vaporise_head(bool_t strip_alternates, struct header *hp,
   enum expand_addr_check_mode eacm, si8_t *set_on_error)
{
   /* TODO namelist_vaporise_head() is incredibly expensive and redundant */
   struct name *tolist, *np, **npp;
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

FL struct name *
usermap(struct name *names, bool_t force_metoo){
   struct a_nag_group *ngp;
   struct name *nlist, *nlist_tail, *np, *cp;
   int metoo;
   NYD_IN;

   metoo = (force_metoo || ok_blook(metoo));
   nlist = nlist_tail = NULL;
   np = names;

   for(; np != NULL; np = cp){
      assert(!(np->n_type & GDEL)); /* TODO legacy */
      cp = np->n_flink;

      if(is_fileorpipe_addr(np) ||
            (ngp = a_nag_group_find(a_NAG_T_ALIAS, np->n_name)) == NULL){
         if((np->n_blink = nlist_tail) != NULL)
            nlist_tail->n_flink = np;
         else
            nlist = np;
         nlist_tail = np;
         np->n_flink = NULL;
      }else{
         nlist = a_nag_gexpand(0, nlist, ngp, metoo, np->n_type);
         if((nlist_tail = nlist) != NULL)
            while(nlist_tail->n_flink != NULL)
               nlist_tail = nlist_tail->n_flink;
      }
   }
   NYD_OU;
   return nlist;
}

FL struct name *
elide(struct name *names)
{
   size_t i, j, k;
   struct name *nlist, *np, **nparr;
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

   qsort(nparr, i, sizeof *nparr, &a_nag_elide_qsort);

   /* Remove duplicates XXX speedup, or list_uniq()! */
   for(j = 0, --i; j < i;){
      if(asccasecmp(nparr[j]->n_name, nparr[k = j + 1]->n_name))
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
   struct a_nag_group *ngp;
   char const *varname, *ccp;
   char **argv;
   NYD_IN;

   n_pstate_err_no = n_ERR_NONE;

   argv = vp;
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NULL;

   if(*argv == NULL){
      if(!a_nag_group_print_all(a_NAG_T_ALTERNATES, varname))
         vp = NULL;
   }else{
      if(varname != NULL)
         n_err(_("`alternates': `vput' only supported for show mode\n"));

      /* Delete the old set to "declare a list", if *posix* */
      if(ok_blook(posix))
         a_nag_group_del(a_NAG_T_ALTERNATES, n_star);

      while((ccp = *argv++) != NULL){
         size_t l;
         struct name *np;

         if((np = lextract(ccp, GSKIN)) == NULL || np->n_flink != NULL ||
               (np = checkaddrs(np, EACM_STRICT, NULL)) == NULL){
            n_err(_("Invalid `alternates' argument: %s\n"),
               n_shexp_quote_cp(ccp, FAL0));
            n_pstate_err_no = n_ERR_INVAL;
            vp = NULL;
            continue;
         }
         ccp = np->n_name;

         l = strlen(ccp) +1;
         if((ngp = a_nag_group_fetch(a_NAG_T_ALTERNATES, ccp, l)) == NULL){
            n_err(_("Failed to create storage for alternates: %s\n"),
               n_shexp_quote_cp(ccp, FAL0));
            n_pstate_err_no = n_ERR_NOMEM;
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

   do if(!a_nag_group_del(a_NAG_T_ALTERNATES, *argv)){
      n_err(_("No such `alternates': %s\n"), n_shexp_quote_cp(*argv, FAL0));
      rv = 1;
   }while(*++argv != NULL);
   NYD_OU;
   return rv;
}

FL struct name *
n_alternates_remove(struct name *np, bool_t keep_single){
   /* XXX keep a single pointer, initial null, and immediate remove nodes
    * XXX on successful match unless keep single and that pointer null! */
   struct a_nag_group_lookup ngl;
   struct a_nag_group *ngp;
   struct name *xp, *newnp;
   NYD_IN;

   /* Delete the temporary bit from all */
   for(xp = np; xp != NULL; xp = xp->n_flink)
      xp->n_flags &= ~(ui32_t)SI32_MIN;

   /* Mark all possible alternate names (xxx sic: instead walk over namelist
    * and hash-lookup alternate instead (unless *allnet*) */
   for(ngp = a_nag_group_go_first(a_NAG_T_ALTERNATES, &ngl); ngp != NULL;
         ngp = a_nag_group_go_next(&ngl))
      np = a_nag_namelist_mark_name(np, ngp->ng_id);

   np = a_nag_namelist_mark_name(np, ok_vlook(LOGNAME));

   if((xp = extract(ok_vlook(sender), GEXTRA | GSKIN)) != NULL){
      /* TODO check_from_and_sender(): drop; *sender*: only one name!
       * TODO At assignment time, as VIP var? */
      do
         np = a_nag_namelist_mark_name(np, xp->n_name);
      while((xp = xp->n_flink) != NULL);
   }else for(xp = lextract(ok_vlook(from), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      np = a_nag_namelist_mark_name(np, xp->n_name);

   /* C99 */{
      char const *v15compat;

      if((v15compat = ok_vlook(replyto)) != NULL){
         n_OBSOLETE(_("please use *reply-to*, not *replyto*"));
         for(xp = lextract(v15compat, GEXTRA | GSKIN); xp != NULL;
               xp = xp->n_flink)
            np = a_nag_namelist_mark_name(np, xp->n_name);
      }
   }

   for(xp = lextract(ok_vlook(reply_to), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      np = a_nag_namelist_mark_name(np, xp->n_name);

   /* Clean the list by throwing away all deleted or marked (but one) nodes */
   for(xp = newnp = NULL; np != NULL; np = np->n_flink){
      if(np->n_type & GDEL)
         continue;
      if(np->n_flags & (ui32_t)SI32_MIN){
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
      xp->n_flags &= ~(ui32_t)SI32_MIN;
   }
   if(xp != NULL)
      xp->n_flink = NULL;
   np = newnp;

   NYD_OU;
   return np;
}

FL bool_t
n_is_myname(char const *name){
   struct a_nag_group_lookup ngl;
   struct a_nag_group *ngp;
   struct name *xp;
   NYD_IN;

   if(a_nag_is_same_name(ok_vlook(LOGNAME), name))
      goto jleave;

   if(!ok_blook(allnet)){
      if(a_nag_group_lookup(a_NAG_T_ALTERNATES, &ngl, name) != NULL)
         goto jleave;
   }else{
      for(ngp = a_nag_group_go_first(a_NAG_T_ALTERNATES, &ngl); ngp != NULL;
            ngp = a_nag_group_go_next(&ngl))
         if(a_nag_is_same_name(ngp->ng_id, name))
            goto jleave;
   }

   for(xp = lextract(ok_vlook(from), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      if(a_nag_is_same_name(xp->n_name, name))
         goto jleave;

   /* C99 */{
      char const *v15compat;

      if((v15compat = ok_vlook(replyto)) != NULL){
         n_OBSOLETE(_("please use *reply-to*, not *replyto*"));
         for(xp = lextract(v15compat, GEXTRA | GSKIN); xp != NULL;
               xp = xp->n_flink)
            if(a_nag_is_same_name(xp->n_name, name))
               goto jleave;
      }
   }

   for(xp = lextract(ok_vlook(reply_to), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      if(a_nag_is_same_name(xp->n_name, name))
         goto jleave;

   for(xp = extract(ok_vlook(sender), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      if(a_nag_is_same_name(xp->n_name, name))
         goto jleave;

   name = NULL;
jleave:
   NYD_OU;
   return (name != NULL);
}

FL int
c_addrcodec(void *vp){
   struct n_addrguts ag;
   struct str trims;
   struct n_string s_b, *sp;
   size_t alen;
   int mode;
   char const **argv, *varname, *act, *cp;
   NYD_IN;

   sp = n_string_creat_auto(&s_b);
   argv = vp;
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NULL;

   act = *argv;
   for(cp = act; *cp != '\0' && !blankspacechar(*cp); ++cp)
      ;
   mode = 0;
   if(*act == '+')
      mode = 1, ++act;
   if(*act == '+')
      mode = 2, ++act;
   if(*act == '+')
      mode = 3, ++act;
   if(act >= cp)
      goto jesynopsis;
   alen = PTR2SIZE(cp - act);
   if(*cp != '\0')
      ++cp;

   trims.l = strlen(trims.s = n_UNCONST(cp));
   cp = savestrbuf(n_str_trim(&trims, n_STR_TRIM_BOTH)->s, trims.l);
   if(trims.l <= UIZ_MAX / 4)
         trims.l <<= 1;
   sp = n_string_reserve(sp, trims.l);

   n_pstate_err_no = n_ERR_NONE;

   if(is_ascncaseprefix(act, "encode", alen)){
      /* This function cannot be a simple nalloc() wrapper even later on, since
       * we may need to turn any ", () or \ into quoted-pairs */
      char c;

      while((c = *cp++) != '\0'){
         if(((c == '(' || c == ')') && mode < 1) || (c == '"' && mode < 2) ||
               (c == '\\' && mode < 3))
            sp = n_string_push_c(sp, '\\');
         sp = n_string_push_c(sp, c);
      }

      if(n_addrspec_with_guts(&ag, n_string_cp(sp), TRU1, TRU1) == NULL ||
            (ag.ag_n_flags & (NAME_ADDRSPEC_ISADDR | NAME_ADDRSPEC_INVALID)
               ) != NAME_ADDRSPEC_ISADDR){
         cp = sp->s_dat;
         n_pstate_err_no = n_ERR_INVAL;
         vp = NULL;
      }else{
         struct name *np;

         np = nalloc(ag.ag_input, GTO | GFULL | GSKIN);
         cp = np->n_fullname;
      }
   }else if(mode == 0){
      if(is_ascncaseprefix(act, "decode", alen)){
         char c;

         while((c = *cp++) != '\0'){
            switch(c){
            case '(':
               sp = n_string_push_c(sp, '(');
               act = skip_comment(cp);
               if(--act > cp)
                  sp = n_string_push_buf(sp, cp, PTR2SIZE(act - cp));
               sp = n_string_push_c(sp, ')');
               cp = ++act;
               break;
            case '"':
               while(*cp != '\0'){
                  if((c = *cp++) == '"')
                     break;
                  if(c == '\\' && (c = *cp) != '\0')
                     ++cp;
                  sp = n_string_push_c(sp, c);
               }
               break;
            default:
               if(c == '\\' && (c = *cp++) == '\0')
                  break;
               sp = n_string_push_c(sp, c);
               break;
            }
         }
         cp = n_string_cp(sp);
      }else if(is_ascncaseprefix(act, "skin", alen) ||
            (mode = 1, is_ascncaseprefix(act, "skinlist", alen))){
         /* Let's just use the is-single-address hack for this one, too.. */
         if(n_addrspec_with_guts(&ag, cp, TRU1, TRU1) == NULL ||
               (ag.ag_n_flags & (NAME_ADDRSPEC_ISADDR | NAME_ADDRSPEC_INVALID)
                  ) != NAME_ADDRSPEC_ISADDR){
            n_pstate_err_no = n_ERR_INVAL;
            vp = NULL;
         }else{
            struct name *np;

            np = nalloc(ag.ag_input, GTO | GFULL | GSKIN);
            cp = np->n_name;

            if(mode == 1 && is_mlist(cp, FAL0) != MLIST_OTHER)
               n_pstate_err_no = n_ERR_EXIST;
         }
      }else
         goto jesynopsis;
   }else
      goto jesynopsis;

   if(varname == NULL){
      if(fprintf(n_stdout, "%s\n", cp) < 0){
         n_pstate_err_no = n_err_no;
         vp = NULL;
      }
   }else if(!n_var_vset(varname, (uintptr_t)cp)){
      n_pstate_err_no = n_ERR_NOTSUP;
      vp = NULL;
   }

jleave:
   NYD_OU;
   return (vp != NULL ? 0 : 1);
jesynopsis:
   n_err(_("Synopsis: addrcodec: <[+[+[+]]]e[ncode]|d[ecode]|s[kin]> "
      "<rest-of-line>\n"));
   n_pstate_err_no = n_ERR_INVAL;
   vp = NULL;
   goto jleave;
}

FL int
c_commandalias(void *vp){
   struct a_nag_group *ngp;
   char const **argv, *ccp;
   int rv;
   NYD_IN;

   rv = 0;
   argv = vp;

   if((ccp = *argv) == NULL){
      a_nag_group_print_all(a_NAG_T_COMMANDALIAS, NULL);
      goto jleave;
   }

   /* Verify the name is a valid one, and not a command modifier.
    * NOTE: this list duplicates settings isolated somewhere else (go.c) */
   if(*ccp == '\0' || *n_cmd_isolate(ccp) != '\0' ||
         !asccasecmp(ccp, "ignerr") || !asccasecmp(ccp, "local") ||
         !asccasecmp(ccp, "wysh") || !asccasecmp(ccp, "vput") ||
         !asccasecmp(ccp, "scope") || !asccasecmp(ccp, "u")){
      n_err(_("`commandalias': not a valid command name: %s\n"),
         n_shexp_quote_cp(ccp, FAL0));
      rv = 1;
      goto jleave;
   }

   if(argv[1] == NULL){
      if((ngp = a_nag_group_find(a_NAG_T_COMMANDALIAS, ccp)) != NULL)
         a_nag_group_print(ngp, n_stdout, NULL);
      else{
         n_err(_("No such commandalias: %s\n"), n_shexp_quote_cp(ccp, FAL0));
         rv = 1;
      }
   }else{
      /* Because one hardly ever redefines, anything is stored in one chunk */
      char *cp;
      size_t i, len;

      /* Delete the old one, if any; don't get fooled to remove them all */
      if(ccp[0] != '*' || ccp[1] != '\0')
         a_nag_group_del(a_NAG_T_COMMANDALIAS, ccp);

      for(i = len = 0, ++argv; argv[i] != NULL; ++i)
         len += strlen(argv[i]) + 1;
      if(len == 0)
         len = 1;

      if((ngp = a_nag_group_fetch(a_NAG_T_COMMANDALIAS, ccp, len)) == NULL){
         n_err(_("Failed to create storage for commandalias: %s\n"),
            n_shexp_quote_cp(ccp, FAL0));
         rv = 1;
      }else{
         struct a_nag_cmd_alias *ncap;

         a_NAG_GP_TO_SUBCLASS(ncap, ngp);
         a_NAG_GP_TO_SUBCLASS(cp, ngp);
         cp += sizeof *ncap;
         ncap->nca_expand.s = cp;
         ncap->nca_expand.l = len - 1;

         for(len = 0; (ccp = *argv++) != NULL;)
            if((i = strlen(ccp)) > 0){
               if(len++ != 0)
                  *cp++ = ' ';
               memcpy(cp, ccp, i);
               cp += i;
            }
         *cp = '\0';
      }
   }
jleave:
   NYD_OU;
   return rv;
}

FL int
c_uncommandalias(void *vp){
   char **argv;
   int rv;
   NYD_IN;

   rv = 0;
   argv = vp;

   do if(!a_nag_group_del(a_NAG_T_COMMANDALIAS, *argv)){
      n_err(_("No such `commandalias': %s\n"), n_shexp_quote_cp(*argv, FAL0));
      rv = 1;
   }while(*++argv != NULL);
   NYD_OU;
   return rv;
}

FL char const *
n_commandalias_exists(char const *name, struct str const **expansion_or_null){
   struct a_nag_group *ngp;
   NYD_IN;

   if((ngp = a_nag_group_find(a_NAG_T_COMMANDALIAS, name)) != NULL){
      name = ngp->ng_id;

      if(expansion_or_null != NULL){
         struct a_nag_cmd_alias *ncap;

         a_NAG_GP_TO_SUBCLASS(ncap, ngp);
         *expansion_or_null = &ncap->nca_expand;
      }
   }else
      name = NULL;
   NYD_OU;
   return name;
}

FL bool_t
n_alias_is_valid_name(char const *name){
   char c;
   char const *cp;
   bool_t rv;
   NYD2_IN;

   for(rv = TRU1, cp = name++; (c = *cp++) != '\0';)
      /* User names, plus things explicitly mentioned in Postfix aliases(5),
       * i.e., [[:alnum:]_#:@.-]+$?.
       * As extensions allow high-bit bytes, semicolon and period. */
      /* TODO n_alias_is_valid_name(): locale dependent validity check,
       * TODO with Unicode prefix valid UTF-8! */
      if(!alnumchar(c) && c != '_' && c != '-' &&
            c != '#' && c != ':' && c != '@' &&
            !((ui8_t)c & 0x80) && c != '!' && c != '.'){
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
   struct a_nag_group *ngp;
   int rv;
   NYD_IN;

   rv = 0;
   argv = v;
   n_UNINIT(ecp, NULL);

   if(*argv == NULL)
      a_nag_group_print_all(a_NAG_T_ALIAS, NULL);
   else if(!n_alias_is_valid_name(*argv)){
      ecp = N_("Not a valid alias name: %s\n");
      goto jerr;
   }else if(argv[1] == NULL){
      if((ngp = a_nag_group_find(a_NAG_T_ALIAS, *argv)) != NULL)
         a_nag_group_print(ngp, n_stdout, NULL);
      else{
         ecp = N_("No such alias: %s\n");
         goto jerr;
      }
   }else if((ngp = a_nag_group_fetch(a_NAG_T_ALIAS, *argv, 0)) == NULL){
      ecp = N_("Failed to create alias storage for: %s\n");
jerr:
      n_err(V_(ecp), n_shexp_quote_cp(*argv, FAL0));
      rv = 1;
   }else{
      struct a_nag_grp_names *ngnp_tail, *ngnp;
      struct a_nag_grp_names_head *ngnhp;

      a_NAG_GP_TO_SUBCLASS(ngnhp, ngp);

      if((ngnp_tail = ngnhp->ngnh_head) != NULL)
         while((ngnp = ngnp_tail->ngn_next) != NULL)
            ngnp_tail = ngnp;

      for(++argv; *argv != NULL; ++argv){
         size_t i;

         i = strlen(*argv) +1;
         ngnp = n_alloc(n_VSTRUCT_SIZEOF(struct a_nag_grp_names, ngn_id) + i);
         if(ngnp_tail != NULL)
            ngnp_tail->ngn_next = ngnp;
         else
            ngnhp->ngnh_head = ngnp;
         ngnp_tail = ngnp;
         ngnp->ngn_next = NULL;
         memcpy(ngnp->ngn_id, *argv, i);
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

   do if(!a_nag_group_del(a_NAG_T_ALIAS, *argv)){
      n_err(_("No such alias: %s\n"), *argv);
      rv = 1;
   }while(*++argv != NULL);
   NYD_OU;
   return rv;
}

FL int
c_mlist(void *v){
   int rv;
   NYD_IN;

   rv = a_nag_mlmux(a_NAG_T_MLIST, v);
   NYD_OU;
   return rv;
}

FL int
c_unmlist(void *v){
   int rv;
   NYD_IN;

   rv = a_nag_unmlmux(a_NAG_T_MLIST, v);
   NYD_OU;
   return rv;
}

FL int
c_mlsubscribe(void *v){
   int rv;
   NYD_IN;

   rv = a_nag_mlmux(a_NAG_T_MLIST | a_NAG_T_SUBSCRIBE, v);
   NYD_OU;
   return rv;
}

FL int
c_unmlsubscribe(void *v){
   int rv;
   NYD_IN;

   rv = a_nag_unmlmux(a_NAG_T_MLIST | a_NAG_T_SUBSCRIBE, v);
   NYD_OU;
   return rv;
}

FL enum mlist_state
is_mlist(char const *name, bool_t subscribed_only){
   struct a_nag_group *ngp;
#ifdef HAVE_REGEX
   struct a_nag_grp_regex **lpp, *ngrp;
   bool_t re2;
#endif
   enum mlist_state rv;
   NYD_IN;

   ngp = a_nag_group_find(a_NAG_T_MLIST, name);
   rv = (ngp != NULL) ? MLIST_KNOWN : MLIST_OTHER;

   if(rv == MLIST_KNOWN){
      if(ngp->ng_type & a_NAG_T_SUBSCRIBE)
         rv = MLIST_SUBSCRIBED;
      else if(subscribed_only)
         rv = MLIST_OTHER;
      /* Of course, if that is a regular expression it doesn't mean a thing */
#ifdef HAVE_REGEX
      if(ngp->ng_type & a_NAG_T_REGEX)
         rv = MLIST_OTHER;
      else
#endif
         goto jleave;
   }

   /* Not in the hashmap (as something matchable), walk the lists */
#ifdef HAVE_REGEX
   re2 = FAL0;
   lpp = &a_nag_mlsub_regex;

jregex_redo:
   if((ngrp = *lpp) != NULL){
      do if(regexec(&ngrp->ngr_regex, name, 0,NULL, 0) != REG_NOMATCH){
         /* Relink as the head of this list if the hit count of this group is
          * >= 25% of the average hit count */
         size_t i;

         if(!re2)
            i = ++a_nag_mlsub_hits / a_nag_mlsub_size;
         else
            i = ++a_nag_mlist_hits / a_nag_mlist_size;
         i >>= 2;

         if(++ngrp->ngr_hits >= i && *lpp != ngrp && ngrp->ngr_next != ngrp){
            ngrp->ngr_last->ngr_next = ngrp->ngr_next;
            ngrp->ngr_next->ngr_last = ngrp->ngr_last;
            (ngrp->ngr_last = (*lpp)->ngr_last)->ngr_next = ngrp;
            (ngrp->ngr_next = *lpp)->ngr_last = ngrp;
            *lpp = ngrp;
         }
         rv = !re2 ? MLIST_SUBSCRIBED : MLIST_KNOWN;
         goto jleave;
      }while((ngrp = ngrp->ngr_next) != *lpp);
   }

   if(!re2 && !subscribed_only){
      re2 = TRU1;
      lpp = &a_nag_mlist_regex;
      goto jregex_redo;
   }
   assert(rv == MLIST_OTHER);
#endif /* HAVE_REGEX */

jleave:
   NYD_OU;
   return rv;
}

FL enum mlist_state
is_mlist_mp(struct message *mp, enum mlist_state what){
   struct name *np;
   bool_t cc;
   enum mlist_state rv;
   NYD_IN;

   rv = MLIST_OTHER;

   cc = FAL0;
   np = lextract(hfield1("to", mp), GTO | GSKIN);
jredo:
   for(; np != NULL; np = np->n_flink){
      switch(is_mlist(np->n_name, FAL0)){
      case MLIST_OTHER:
         break;
      case MLIST_KNOWN:
         if(what == MLIST_KNOWN || what == MLIST_OTHER){
            if(rv == MLIST_OTHER)
               rv = MLIST_KNOWN;
            if(what == MLIST_KNOWN)
               goto jleave;
         }
         break;
      case MLIST_SUBSCRIBED:
         if(what == MLIST_SUBSCRIBED || what == MLIST_OTHER){
            if(rv != MLIST_SUBSCRIBED)
               rv = MLIST_SUBSCRIBED;
            goto jleave;
         }
         break;
      }
   }

   if(!cc){
      cc = TRU1;
      np = lextract(hfield1("cc", mp), GCC | GSKIN);
      goto jredo;
   }
jleave:
   NYD_OU;
   return rv;
}

FL int
c_shortcut(void *vp){
   struct a_nag_group *ngp;
   char **argv;
   int rv;
   NYD_IN;

   rv = 0;
   argv = vp;

   if(*argv == NULL)
      a_nag_group_print_all(a_NAG_T_SHORTCUT, NULL);
   else if(argv[1] == NULL){
      if((ngp = a_nag_group_find(a_NAG_T_SHORTCUT, *argv)) != NULL)
         a_nag_group_print(ngp, n_stdout, NULL);
      else{
         n_err(_("No such shortcut: %s\n"), n_shexp_quote_cp(*argv, FAL0));
         rv = 1;
      }
   }else for(; *argv != NULL; argv += 2){
      /* Because one hardly ever redefines, anything is stored in one chunk */
      size_t l;
      char *cp;

      if(argv[1] == NULL){
         n_err(_("Synopsis: shortcut: <shortcut> <expansion>\n"));
         rv = 1;
         break;
      }
      if(a_nag_group_find(a_NAG_T_SHORTCUT, *argv) != NULL)
         a_nag_group_del(a_NAG_T_SHORTCUT, *argv);

      l = strlen(argv[1]) +1;
      if((ngp = a_nag_group_fetch(a_NAG_T_SHORTCUT, *argv, l)) == NULL){
         n_err(_("Failed to create storage for shortcut: %s\n"),
            n_shexp_quote_cp(*argv, FAL0));
         rv = 1;
      }else{
         a_NAG_GP_TO_SUBCLASS(cp, ngp);
         memcpy(cp, argv[1], l);
      }
   }
   NYD_OU;
   return rv;
}

FL int
c_unshortcut(void *vp){
   char **argv;
   int rv;
   NYD_IN;

   rv = 0;
   argv = vp;

   do if(!a_nag_group_del(a_NAG_T_SHORTCUT, *argv)){
      n_err(_("No such shortcut: %s\n"), *argv);
      rv = 1;
   }while(*++argv != NULL);
   NYD_OU;
   return rv;
}

FL char const *
shortcut_expand(char const *str){
   struct a_nag_group *ngp;
   NYD_IN;

   if((ngp = a_nag_group_find(a_NAG_T_SHORTCUT, str)) != NULL)
      a_NAG_GP_TO_SUBCLASS(str, ngp);
   else
      str = NULL;
   NYD_OU;
   return str;
}

FL int
c_charsetalias(void *vp){
   struct a_nag_group *ngp;
   char **argv;
   int rv;
   NYD_IN;

   rv = 0;
   argv = vp;

   if(*argv == NULL)
      a_nag_group_print_all(a_NAG_T_CHARSETALIAS, NULL);
   else if(argv[1] == NULL){
      if((ngp = a_nag_group_find(a_NAG_T_CHARSETALIAS, *argv)) != NULL)
         a_nag_group_print(ngp, n_stdout, NULL);
      else{
         n_err(_("No such charsetalias: %s\n"), n_shexp_quote_cp(*argv, FAL0));
         rv = 1;
      }
   }else for(; *argv != NULL; argv += 2){
      /* Because one hardly ever redefines, anything is stored in one chunk */
      char *cp;
      size_t dstl;
      char const *dst, *src;

      if((dst = argv[1]) == NULL){
         n_err(_("Synopsis: charsetalias: <charset> <charset-alias>\n"));
         rv = 1;
         break;
      }else if((dst = n_iconv_normalize_name(dst)) == NULL){
         n_err(_("charsetalias: invalid target charset %s\n"),
            n_shexp_quote_cp(argv[1], FAL0));
         rv = 1;
         continue;
      }else if((src = n_iconv_normalize_name(argv[0])) == NULL){
         n_err(_("charsetalias: invalid source charset %s\n"),
            n_shexp_quote_cp(argv[0], FAL0));
         rv = 1;
         continue;
      }

      /* Delete the old one, if any; don't get fooled to remove them all */
      if(src[0] != '*' || src[1] != '\0')
         a_nag_group_del(a_NAG_T_CHARSETALIAS, src);

      dstl = strlen(dst) +1;
      if((ngp = a_nag_group_fetch(a_NAG_T_CHARSETALIAS, src, dstl)) == NULL){
         n_err(_("Failed to create storage for charsetalias: %s\n"),
            n_shexp_quote_cp(src, FAL0));
         rv = 1;
      }else{
         a_NAG_GP_TO_SUBCLASS(cp, ngp);
         memcpy(cp, dst, dstl);
      }
   }
   NYD_OU;
   return rv;
}

FL int
c_uncharsetalias(void *vp){
   char **argv, *cp;
   int rv;
   NYD_IN;

   rv = 0;
   argv = vp;

   do{
      if((cp = n_iconv_normalize_name(*argv)) == NULL ||
            !a_nag_group_del(a_NAG_T_CHARSETALIAS, cp)){
         n_err(_("No such `charsetalias': %s\n"),
            n_shexp_quote_cp(*argv, FAL0));
         rv = 1;
      }
   }while(*++argv != NULL);
   NYD_OU;
   return rv;
}

FL char const *
n_charsetalias_expand(char const *cp){
   struct a_nag_group *ngp;
   size_t i;
   char const *cp_orig;
   NYD_IN;

   cp_orig = cp;

   for(i = 0; (ngp = a_nag_group_find(a_NAG_T_CHARSETALIAS, cp)) != NULL;){
      a_NAG_GP_TO_SUBCLASS(cp, ngp);
      if(++i == 8) /* XXX Magic (same as for `ghost' expansion) */
         break;
   }

   if(cp != cp_orig)
      cp = savestr(cp);
   NYD_OU;
   return cp;
}

FL int
c_filetype(void *vp){ /* TODO support automatic chains: .tar.gz -> .gz + .tar */
   struct a_nag_group *ngp;
   char **argv; /* TODO While there: let ! prefix mean: direct execlp(2) */
   int rv;
   NYD_IN;

   rv = 0;
   argv = vp;

   if(*argv == NULL)
      a_nag_group_print_all(a_NAG_T_FILETYPE, NULL);
   else if(argv[1] == NULL){
      if((ngp = a_nag_group_find(a_NAG_T_FILETYPE, *argv)) != NULL)
         a_nag_group_print(ngp, n_stdout, NULL);
      else{
         n_err(_("No such filetype: %s\n"), n_shexp_quote_cp(*argv, FAL0));
         rv = 1;
      }
   }else for(; *argv != NULL; argv += 3){
      /* Because one hardly ever redefines, anything is stored in one chunk */
      char const *ccp;
      char *cp, c;
      size_t llc, lsc;

      if(argv[1] == NULL || argv[2] == NULL){
         n_err(_("Synopsis: filetype: <extension> <load-cmd> <save-cmd>\n"));
         rv = 1;
         break;
      }

      /* Delete the old one, if any; don't get fooled to remove them all */
      ccp = argv[0];
      if(ccp[0] != '*' || ccp[1] != '\0')
         a_nag_group_del(a_NAG_T_FILETYPE, ccp);

      /* Lowercase it all (for display purposes) */
      cp = savestr(ccp);
      ccp = cp;
      while((c = *cp) != '\0')
         *cp++ = lowerconv(c);

      llc = strlen(argv[1]) +1;
      lsc = strlen(argv[2]) +1;
      if(UIZ_MAX - llc <= lsc)
         goto jenomem;

      if((ngp = a_nag_group_fetch(a_NAG_T_FILETYPE, ccp, llc + lsc)) == NULL){
jenomem:
         n_err(_("Failed to create storage for filetype: %s\n"),
            n_shexp_quote_cp(argv[0], FAL0));
         rv = 1;
      }else{
         struct a_nag_file_type *nftp;

         a_NAG_GP_TO_SUBCLASS(nftp, ngp);
         a_NAG_GP_TO_SUBCLASS(cp, ngp);
         cp += sizeof *nftp;
         memcpy(nftp->nft_load.s = cp, argv[1], llc);
            cp += llc;
            nftp->nft_load.l = --llc;
         memcpy(nftp->nft_save.s = cp, argv[2], lsc);
            /*cp += lsc;*/
            nftp->nft_save.l = --lsc;
      }
   }
   NYD_OU;
   return rv;
}

FL int
c_unfiletype(void *vp){
   char **argv;
   int rv;
   NYD_IN;

   rv = 0;
   argv = vp;

   do if(!a_nag_group_del(a_NAG_T_FILETYPE, *argv)){
      n_err(_("No such `filetype': %s\n"), n_shexp_quote_cp(*argv, FAL0));
      rv = 1;
   }while(*++argv != NULL);
   NYD_OU;
   return rv;
}

FL bool_t
n_filetype_trial(struct n_file_type *res_or_null, char const *file){
   struct stat stb;
   struct a_nag_group_lookup ngl;
   struct n_string s, *sp;
   struct a_nag_group const *ngp;
   ui32_t l;
   NYD2_IN;

   sp = n_string_creat_auto(&s);
   sp = n_string_assign_cp(sp, file);
   sp = n_string_push_c(sp, '.');
   l = sp->s_len;

   for(ngp = a_nag_group_go_first(a_NAG_T_FILETYPE, &ngl); ngp != NULL;
         ngp = a_nag_group_go_next(&ngl)){
      sp = n_string_trunc(sp, l);
      sp = n_string_push_buf(sp, ngp->ng_id,
            ngp->ng_subclass_off - ngp->ng_id_len_sub);

      if(!stat(n_string_cp(sp), &stb) && S_ISREG(stb.st_mode)){
         if(res_or_null != NULL){
            struct a_nag_file_type *nftp;

            a_NAG_GP_TO_SUBCLASS(nftp, ngp);
            res_or_null->ft_ext_dat = ngp->ng_id;
            res_or_null->ft_ext_len = ngp->ng_subclass_off - ngp->ng_id_len_sub;
            res_or_null->ft_load_dat = nftp->nft_load.s;
            res_or_null->ft_load_len = nftp->nft_load.l;
            res_or_null->ft_save_dat = nftp->nft_save.s;
            res_or_null->ft_save_len = nftp->nft_save.l;
         }
         goto jleave; /* TODO after v15 legacy drop: break; */
      }
   }

   /* TODO v15 legacy code: automatic file hooks for .{bz2,gz,xz},
    * TODO but NOT supporting *file-hook-{load,save}-EXTENSION* */
   ngp = (struct a_nag_group*)0x1;

   sp = n_string_trunc(sp, l);
   sp = n_string_push_buf(sp, a_nag_OBSOLETE_xz.ft_ext_dat,
         a_nag_OBSOLETE_xz.ft_ext_len);
   if(!stat(n_string_cp(sp), &stb) && S_ISREG(stb.st_mode)){
      n_OBSOLETE(".xz support will vanish, please use the `filetype' command");
      if(res_or_null != NULL)
         *res_or_null = a_nag_OBSOLETE_xz;
      goto jleave;
   }

   sp = n_string_trunc(sp, l);
   sp = n_string_push_buf(sp, a_nag_OBSOLETE_gz.ft_ext_dat,
         a_nag_OBSOLETE_gz.ft_ext_len);
   if(!stat(n_string_cp(sp), &stb) && S_ISREG(stb.st_mode)){
      n_OBSOLETE(".gz support will vanish, please use the `filetype' command");
      if(res_or_null != NULL)
         *res_or_null = a_nag_OBSOLETE_gz;
      goto jleave;
   }

   sp = n_string_trunc(sp, l);
   sp = n_string_push_buf(sp, a_nag_OBSOLETE_bz2.ft_ext_dat,
         a_nag_OBSOLETE_bz2.ft_ext_len);
   if(!stat(n_string_cp(sp), &stb) && S_ISREG(stb.st_mode)){
      n_OBSOLETE(".bz2 support will vanish, please use the `filetype' command");
      if(res_or_null != NULL)
         *res_or_null = a_nag_OBSOLETE_bz2;
      goto jleave;
   }

   ngp = NULL;

jleave:
   NYD2_OU;
   return (ngp != NULL);
}

FL bool_t
n_filetype_exists(struct n_file_type *res_or_null, char const *file){
   char const *ext, *lext;
   NYD2_IN;

   if((ext = strrchr(file, '/')) != NULL)
      file = ++ext;

   for(lext = NULL; (ext = strchr(file, '.')) != NULL; lext = file = ext){
      struct a_nag_group const *ngp;

      if((ngp = a_nag_group_find(a_NAG_T_FILETYPE, ++ext)) != NULL){
         lext = ext;
         if(res_or_null != NULL){
            struct a_nag_file_type *nftp;

            a_NAG_GP_TO_SUBCLASS(nftp, ngp);
            res_or_null->ft_ext_dat = ngp->ng_id;
            res_or_null->ft_ext_len = ngp->ng_subclass_off - ngp->ng_id_len_sub;
            res_or_null->ft_load_dat = nftp->nft_load.s;
            res_or_null->ft_load_len = nftp->nft_load.l;
            res_or_null->ft_save_dat = nftp->nft_save.s;
            res_or_null->ft_save_len = nftp->nft_save.l;
         }
         goto jleave; /* TODO after v15 legacy drop: break; */
      }
   }

   /* TODO v15 legacy code: automatic file hooks for .{bz2,gz,xz},
    * TODO as well as supporting *file-hook-{load,save}-EXTENSION* */
   if(lext == NULL)
      goto jleave;

   if(!asccasecmp(lext, "xz")){
      n_OBSOLETE(".xz support will vanish, please use the `filetype' command");
      if(res_or_null != NULL)
         *res_or_null = a_nag_OBSOLETE_xz;
      goto jleave;
   }else if(!asccasecmp(lext, "gz")){
      n_OBSOLETE(".gz support will vanish, please use the `filetype' command");
      if(res_or_null != NULL)
         *res_or_null = a_nag_OBSOLETE_gz;
      goto jleave;
   }else if(!asccasecmp(lext, "bz2")){
      n_OBSOLETE(".bz2 support will vanish, please use the `filetype' command");
      if(res_or_null != NULL)
         *res_or_null = a_nag_OBSOLETE_bz2;
      goto jleave;
   }else{
      char const *cload, *csave;
      char *vbuf;
      size_t l; 

#undef a_X1
#define a_X1 "file-hook-load-"
#undef a_X2
#define a_X2 "file-hook-save-"
      l = strlen(lext);
      vbuf = n_lofi_alloc(l + n_MAX(sizeof(a_X1), sizeof(a_X2)));

      memcpy(vbuf, a_X1, sizeof(a_X1) -1);
      memcpy(&vbuf[sizeof(a_X1) -1], lext, l);
      vbuf[sizeof(a_X1) -1 + l] = '\0';
      cload = n_var_vlook(vbuf, FAL0);

      memcpy(vbuf, a_X2, sizeof(a_X2) -1);
      memcpy(&vbuf[sizeof(a_X2) -1], lext, l);
      vbuf[sizeof(a_X2) -1 + l] = '\0';
      csave = n_var_vlook(vbuf, FAL0);

#undef a_X2
#undef a_X1
      n_lofi_free(vbuf);

      if((csave != NULL) | (cload != NULL)){
         n_OBSOLETE("*file-hook-{load,save}-EXTENSION* will vanish, "
            "please use the `filetype' command");

         if(((csave != NULL) ^ (cload != NULL)) == 0){
            if(res_or_null != NULL){
               res_or_null->ft_ext_dat = lext;
               res_or_null->ft_ext_len = l;
               res_or_null->ft_load_dat = cload;
               res_or_null->ft_load_len = strlen(cload);
               res_or_null->ft_save_dat = csave;
               res_or_null->ft_save_len = strlen(csave);
            }
            goto jleave;
         }else
            n_alert(_("Incomplete *file-hook-{load,save}-EXTENSION* for: .%s"),
               lext);
      }
   }

   lext = NULL;

jleave:
   NYD2_OU;
   return (lext != NULL);
}

/* s-it-mode */
