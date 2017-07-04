/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Name lists, alternates and groups: aliases, mailing lists, shortcuts.
 *@ TODO Dynamic hashmaps; names and (these) groups have _nothing_ in common!
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

enum group_type {
   /* Main types (bits not values for easier testing only) */
   GT_COMMANDALIAS = 1u<<0,
   GT_ALIAS = 1u<<1,
   GT_MLIST = 1u<<2,
   GT_SHORTCUT = 1u<<3,
   GT_CHARSETALIAS = 1u<<4,
   GT_FILETYPE = 1u<<5,
   GT_MASK = GT_COMMANDALIAS | GT_ALIAS | GT_MLIST | GT_SHORTCUT |
         GT_CHARSETALIAS | GT_FILETYPE,

   /* Subtype bits and flags */
   GT_SUBSCRIBE = 1u<<6,
   GT_REGEX = 1u<<7,

   /* Extended type mask to be able to reflect what we really have; i.e., mlist
    * can have GT_REGEX if they are subscribed or not, but `mlsubscribe' should
    * print out only GT_MLIST which have the GT_SUBSCRIBE attribute set */
   GT_PRINT_MASK = GT_MASK | GT_SUBSCRIBE
};

struct group {
   struct group   *g_next;
   ui32_t         g_subclass_off;   /* of "subclass" in .g_id (if any) */
   ui16_t         g_id_len_sub;     /* length of .g_id: _subclass_off - this */
   ui8_t          g_type;           /* enum group_type */
   /* Identifying name, of variable size.  Dependent on actual "subtype" more
    * data follows thereafter, but note this is always used (i.e., for regular
    * expression entries this is still set to the plain string) */
   char           g_id[n_VFIELD_SIZE(1)];
};
#define GP_TO_SUBCLASS(X,G) \
do {\
   union __group_subclass {void *gs_vp; char *gs_cp;} __gs__;\
   __gs__.gs_cp = (char*)n_UNCONST(G) + (G)->g_subclass_off;\
   (X) = __gs__.gs_vp;\
} while (0)

struct grp_names_head {
   struct grp_names  *gnh_head;
};

struct grp_names {
   struct grp_names  *gn_next;
   char              gn_id[n_VFIELD_SIZE(0)];
};

#ifdef HAVE_REGEX
struct grp_regex {
   struct grp_regex  *gr_last;
   struct grp_regex  *gr_next;
   struct group      *gr_mygroup;   /* xxx because lists use grp_regex*! ?? */
   size_t            gr_hits;       /* Number of times this group matched */
   regex_t           gr_regex;
};
#endif

struct a_nag_cmd_alias{
   struct str ca_expand;
};

struct a_nag_file_type{
   struct str nft_load;
   struct str nft_save;
};

struct group_lookup {
   struct group   **gl_htable;
   struct group   **gl_slot;
   struct group   *gl_slot_last;
   struct group   *gl_group;
};

static struct n_file_type const a_nag_OBSOLETE_xz = { /* TODO v15 compat */
   "xz", 2, "xz -cd", sizeof("xz -cd") -1, "xz -cz", sizeof("xz -cz") -1
}, a_nag_OBSOLETE_gz = {
   "gz", 2, "gzip -cd", sizeof("gzip -cd") -1, "gzip -cz", sizeof("gzip -cz") -1
}, a_nag_OBSOLETE_bz2 = {
   "bz2", 3, "bzip2 -cd", sizeof("bzip2 -cd") -1,
   "bzip2 -cz", sizeof("bzip2 -cz") -1
};

/* List of alternate names of user */
struct n_strlist *a_nag_altnames;

/* `commandalias' */
static struct group     *_commandalias_heads[HSHSIZE]; /* TODO dynamic hash */

/* `alias' */
static struct group     *_alias_heads[HSHSIZE]; /* TODO dynamic hash */

/* `mlist', `mlsubscribe'.  Anything is stored in the hashmap.. */
static struct group     *_mlist_heads[HSHSIZE]; /* TODO dynamic hash */

/* ..but entries which have GT_REGEX set are false lookups and will really be
 * accessed via sequential lists instead, which are type-specific for better
 * performance, but also to make it possible to have ".*@xy.org" as a mlist
 * and "(one|two)@xy.org" as a mlsubscription.
 * These lists use a bit of QOS optimization in that a matching group will
 * become relinked as the new list head if its hit count is
 *    (>= ((xy_hits / _xy_size) >> 2))
 * Note that the hit counts only count currently linked in nodes.. */
#ifdef HAVE_REGEX
static struct grp_regex *_mlist_regex, *_mlsub_regex;
static size_t           _mlist_size, _mlist_hits, _mlsub_size, _mlsub_hits;
#endif

/* `shortcut' */
static struct group     *_shortcut_heads[HSHSIZE]; /* TODO dynamic hashmaps! */

/* `charsetalias' */
static struct group     *_charsetalias_heads[HSHSIZE];

/* `filetype' */
static struct group     *_filetype_heads[HSHSIZE];

/* Same name, while taking care for *allnet*? */
static bool_t a_nag_is_same_name(char const *n1, char const *n2);

/* Mark all nodes with the given name */
static struct name *a_nag_namelist_mark_name(struct name *np, char const *name);

/* Grab a single name (liberal name) */
static char const *  yankname(char const *ap, char *wbuf,
                        char const *separators, int keepcomms);

/* Extraction multiplexer that splits an input line to names */
static struct name * _extract1(char const *line, enum gfield ntype,
                        char const *separators, bool_t keepcomms);

/* Recursively expand a alias name.  Limit expansion to some fixed level.
 * Direct recursion is not expanded for convenience */
static struct name *a_nag_gexpand(size_t level, struct name *nlist,
                        struct group *gp, bool_t metoo, int ntype);

/* elide() helper */
static int a_nag_elide_qsort(void const *s1, void const *s2);

/* Lookup a group, return it or NULL, fill in glp anyway */
static struct group * _group_lookup(enum group_type gt,
                        struct group_lookup *glp, char const *id);

/* Easier-to-use wrapper around _group_lookup() */
static struct group * _group_find(enum group_type gt, char const *id);

/* Iteration: go to the first group, which also inits the iterator.  A valid
 * iterator can be stepped via _next().  A NULL return means no (more) groups
 * to be iterated exist, in which case only glp->gl_group is set (NULL) */
static struct group * _group_go_first(enum group_type gt,
                        struct group_lookup *glp);
static struct group * _group_go_next(struct group_lookup *glp);

/* Fetch the group id, create it as necessary, fail with NULL if impossible */
static struct group * _group_fetch(enum group_type gt, char const *id,
                        size_t addsz);

/* "Intelligent" delete which handles a "*" id, too;
 * returns a true boolean if a group was deleted, and always succeeds for "*" */
static bool_t        _group_del(enum group_type gt, char const *id);

static struct group * __group_del(struct group_lookup *glp);
static void          __names_del(struct group *gp);

/* Print all groups of the given type, alphasorted */
static void          _group_print_all(enum group_type gt);

static int           __group_print_qsorter(void const *a, void const *b);

/* Really print a group, actually.  Return number of written lines */
static size_t        _group_print(struct group const *gp, FILE *fo);

/* Multiplexers for list and subscribe commands */
static int           _mlmux(enum group_type gt, char **argv);
static int           _unmlmux(enum group_type gt, char **argv);

/* Relinkers for the sequential match lists */
#ifdef HAVE_REGEX
static void          _mlmux_linkin(struct group *gp);
static void          _mlmux_linkout(struct group *gp);
# define _MLMUX_LINKIN(GP) \
   do if ((GP)->g_type & GT_REGEX) _mlmux_linkin(GP); while (0)
# define _MLMUX_LINKOUT(GP) \
   do if ((GP)->g_type & GT_REGEX) _mlmux_linkout(GP); while (0)
#else
# define _MLMUX_LINKIN(GP)
# define _MLMUX_LINKOUT(GP)
#endif

static bool_t
a_nag_is_same_name(char const *n1, char const *n2){
   bool_t rv;
   char c1, c2, c1r, c2r;
   NYD2_ENTER;

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
   NYD2_LEAVE;
   return rv;
}

static struct name *
a_nag_namelist_mark_name(struct name *np, char const *name){
   struct name *p;
   NYD2_ENTER;

   for(p = np; p != NULL; p = p->n_flink)
      if(!(p->n_type & GDEL) && !(p->n_flags & (ui32_t)SI32_MIN) &&
            a_nag_is_same_name(p->n_name, name))
         p->n_flags |= (ui32_t)SI32_MIN;
   NYD2_LEAVE;
   return np;
}

static char const *
yankname(char const *ap, char *wbuf, char const *separators, int keepcomms)
{
   char const *cp;
   char *wp, c, inquote, lc, lastsp;
   NYD_ENTER;

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
   NYD_LEAVE;
   return cp;
}

static struct name *
_extract1(char const *line, enum gfield ntype, char const *separators,
   bool_t keepcomms)
{
   struct name *topp, *np, *t;
   char const *cp;
   char *nbuf;
   NYD_ENTER;

   topp = NULL;
   if (line == NULL || *line == '\0')
      goto jleave;

   np = NULL;
   cp = line;
   nbuf = smalloc(strlen(line) +1);
   while ((cp = yankname(cp, nbuf, separators, keepcomms)) != NULL) {
      t = nalloc(nbuf, ntype);
      if (topp == NULL)
         topp = t;
      else
         np->n_flink = t;
      t->n_blink = np;
      np = t;
   }
   free(nbuf);
jleave:
   NYD_LEAVE;
   return topp;
}

static struct name *
a_nag_gexpand(size_t level, struct name *nlist, struct group *gp, bool_t metoo,
      int ntype){
   struct grp_names *gnp;
   struct name *nlist_tail;
   char const *logname;
   struct grp_names_head *gnhp;
   NYD2_ENTER;

   if(UICMP(z, level++, >, n_ALIAS_MAXEXP)){
      n_err(_("Expanding alias to depth larger than %d\n"), n_ALIAS_MAXEXP);
      goto jleave;
   }

   GP_TO_SUBCLASS(gnhp, gp);
   logname = ok_vlook(LOGNAME);

   for(gnp = gnhp->gnh_head; gnp != NULL; gnp = gnp->gn_next){
      struct group *ngp;
      char *cp;

      cp = gnp->gn_id;

      if(!strcmp(cp, gp->g_id))
         goto jas_is;

      if((ngp = _group_find(GT_ALIAS, cp)) != NULL){
         /* For S-nail(1), the "alias" may *be* the sender in that a name maps
          * to a full address specification; aliases cannot be empty */
         struct grp_names_head *ngnhp;
         GP_TO_SUBCLASS(ngnhp, ngp);

         assert(ngnhp->gnh_head != NULL);
         if(metoo || ngnhp->gnh_head->gn_next != NULL ||
               !a_nag_is_same_name(cp, logname))
            nlist = a_nag_gexpand(level, nlist, ngp, metoo, ntype);
         continue;
      }

      /* Here we should allow to expand to itself if only person in alias */
jas_is:
      if(metoo || gnhp->gnh_head->gn_next == NULL ||
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
   NYD2_LEAVE;
   return nlist;
}

static int
a_nag_elide_qsort(void const *s1, void const *s2){
   struct name const * const *np1, * const *np2;
   int rv;
   NYD2_ENTER;

   np1 = s1;
   np2 = s2;
   rv = asccasecmp((*np1)->n_name, (*np2)->n_name);
   NYD2_LEAVE;
   return rv;
}

static struct group *
_group_lookup(enum group_type gt, struct group_lookup *glp, char const *id){
   char c1;
   struct group *lgp, *gp;
   NYD_ENTER;

   gt &= GT_MASK;
   lgp = NULL;
   glp->gl_htable =
          ( gt & GT_COMMANDALIAS ? _commandalias_heads
         : (gt & GT_ALIAS ? _alias_heads
         : (gt & GT_MLIST ? _mlist_heads
         : (gt & GT_SHORTCUT ? _shortcut_heads
         : (gt & GT_CHARSETALIAS ? _charsetalias_heads
         : (/*gt & GT_FILETYPE ?*/ _filetype_heads
         ))))));
   gp = *(glp->gl_slot = &glp->gl_htable[
         ((gt & (GT_MLIST | GT_CHARSETALIAS | GT_FILETYPE))
         ? n_torek_ihash(id) : n_torek_hash(id)) % HSHSIZE]);
   c1 = *id++;

   if(gt & (GT_MLIST | GT_CHARSETALIAS | GT_FILETYPE)){
      c1 = lowerconv(c1);
      for(; gp != NULL; lgp = gp, gp = gp->g_next)
         if((gp->g_type & gt) && *gp->g_id == c1 &&
               !asccasecmp(&gp->g_id[1], id))
            break;
   }else{
      for(; gp != NULL; lgp = gp, gp = gp->g_next)
         if((gp->g_type & gt) && *gp->g_id == c1 && !strcmp(&gp->g_id[1], id))
            break;
   }

   glp->gl_slot_last = lgp;
   glp->gl_group = gp;
   NYD_LEAVE;
   return gp;
}

static struct group *
_group_find(enum group_type gt, char const *id)
{
   struct group_lookup gl;
   struct group *gp;
   NYD_ENTER;

   gp = _group_lookup(gt, &gl, id);
   NYD_LEAVE;
   return gp;
}

static struct group *
_group_go_first(enum group_type gt, struct group_lookup *glp)
{
   struct group **gpa, *gp;
   size_t i;
   NYD_ENTER;

   for (glp->gl_htable = gpa = (
               gt & GT_COMMANDALIAS ? _commandalias_heads
            : (gt & GT_ALIAS ? _alias_heads
            : (gt & GT_MLIST ? _mlist_heads
            : (gt & GT_SHORTCUT ? _shortcut_heads
            : (gt & GT_CHARSETALIAS ? _charsetalias_heads
            : (gt & GT_FILETYPE ? _filetype_heads
            : NULL)))))
            ), i = 0;
         i < HSHSIZE; ++gpa, ++i)
      if ((gp = *gpa) != NULL) {
         glp->gl_slot = gpa;
         glp->gl_group = gp;
         goto jleave;
      }

   glp->gl_group = gp = NULL;
jleave:
   glp->gl_slot_last = NULL;
   NYD_LEAVE;
   return gp;
}

static struct group *
_group_go_next(struct group_lookup *glp)
{
   struct group *gp, **gpa;
   NYD_ENTER;

   if ((gp = glp->gl_group->g_next) != NULL)
      glp->gl_slot_last = glp->gl_group;
   else {
      glp->gl_slot_last = NULL;
      for (gpa = glp->gl_htable + HSHSIZE; ++glp->gl_slot < gpa;)
         if ((gp = *glp->gl_slot) != NULL)
            break;
   }
   glp->gl_group = gp;
   NYD_LEAVE;
   return gp;
}

static struct group *
_group_fetch(enum group_type gt, char const *id, size_t addsz)
{
   struct group_lookup gl;
   struct group *gp;
   size_t l, i;
   NYD_ENTER;

   if ((gp = _group_lookup(gt, &gl, id)) != NULL)
      goto jleave;

   l = strlen(id) +1;
   if (UIZ_MAX - n_ALIGN(l) <= n_ALIGN(n_VSTRUCT_SIZEOF(struct group, g_id)))
      goto jleave;

   i = n_ALIGN(n_VSTRUCT_SIZEOF(struct group, g_id) + l);
   switch (gt & GT_MASK) {
   case GT_COMMANDALIAS:
      addsz += sizeof(struct a_nag_cmd_alias);
      break;
   case GT_ALIAS:
      addsz += sizeof(struct grp_names_head);
      break;
   case GT_FILETYPE:
      addsz += sizeof(struct a_nag_file_type);
      break;
   case GT_MLIST:
#ifdef HAVE_REGEX
      if (n_is_maybe_regex(id)) {
         addsz = sizeof(struct grp_regex);
         gt |= GT_REGEX;
      }
#endif
      /* FALLTHRU */
   case GT_SHORTCUT:
   case GT_CHARSETALIAS:
   default:
      break;
   }
   if (UIZ_MAX - i < addsz || UI32_MAX <= i || UI16_MAX < i - l)
      goto jleave;

   gp = smalloc(i + addsz);
   memcpy(gp->g_id, id, l);
   gp->g_subclass_off = (ui32_t)i;
   gp->g_id_len_sub = (ui16_t)(i - --l);
   gp->g_type = gt;
   if(gt & (GT_MLIST | GT_CHARSETALIAS | GT_FILETYPE)){
      char *cp, c;

      for(cp = gp->g_id; (c = *cp) != '\0'; ++cp)
         *cp = lowerconv(c);
   }

   if (gt & GT_ALIAS) {
      struct grp_names_head *gnhp;

      GP_TO_SUBCLASS(gnhp, gp);
      gnhp->gnh_head = NULL;
   }
#ifdef HAVE_REGEX
   else if (/*(gt & GT_MLIST) &&*/ gt & GT_REGEX) {
      int s;
      struct grp_regex *grp;
      GP_TO_SUBCLASS(grp, gp);

      if((s = regcomp(&grp->gr_regex, id,
            REG_EXTENDED | REG_ICASE | REG_NOSUB)) != 0){
         n_err(_("Invalid regular expression: %s: %s\n"),
            n_shexp_quote_cp(id, FAL0), n_regex_err_to_doc(&grp->gr_regex, s));
         free(gp);
         gp = NULL;
         goto jleave;
      }
      grp->gr_mygroup = gp;
      _mlmux_linkin(gp);
   }
#endif

   gp->g_next = *gl.gl_slot;
   *gl.gl_slot = gp;
jleave:
   NYD_LEAVE;
   return gp;
}

static bool_t
_group_del(enum group_type gt, char const *id)
{
   enum group_type xgt = gt & GT_MASK;
   struct group_lookup gl;
   struct group *gp;
   NYD_ENTER;

   /* Delete 'em all? */
   if (id[0] == '*' && id[1] == '\0') {
      for (gp = _group_go_first(gt, &gl); gp != NULL;)
         gp = (gp->g_type & xgt) ? __group_del(&gl) : _group_go_next(&gl);
      gp = (struct group*)TRU1;
   } else if ((gp = _group_lookup(gt, &gl, id)) != NULL) {
      if (gp->g_type & xgt)
         __group_del(&gl);
      else
         gp = NULL;
   }
   NYD_LEAVE;
   return (gp != NULL);
}

static struct group *
__group_del(struct group_lookup *glp)
{
   struct group *x, *gp;
   NYD_ENTER;

   /* Overly complicated: link off this node, step ahead to next.. */
   x = glp->gl_group;
   if((gp = glp->gl_slot_last) != NULL)
      gp = (gp->g_next = x->g_next);
   else{
      glp->gl_slot_last = NULL;
      gp = (*glp->gl_slot = x->g_next);

      if(gp == NULL){
         struct group **gpa;

         for(gpa = &glp->gl_htable[HSHSIZE]; ++glp->gl_slot < gpa;)
            if((gp = *glp->gl_slot) != NULL)
               break;
      }
   }
   glp->gl_group = gp;

   if (x->g_type & GT_ALIAS)
      __names_del(x);
#ifdef HAVE_REGEX
   else if (/*(x->g_type & GT_MLIST) &&*/ x->g_type & GT_REGEX) {
      struct grp_regex *grp;
      GP_TO_SUBCLASS(grp, x);

      regfree(&grp->gr_regex);
      _mlmux_linkout(x);
   }
#endif

   free(x);
   NYD_LEAVE;
   return gp;
}

static void
__names_del(struct group *gp)
{
   struct grp_names_head *gnhp;
   struct grp_names *gnp;
   NYD_ENTER;

   GP_TO_SUBCLASS(gnhp, gp);
   for (gnp = gnhp->gnh_head; gnp != NULL;) {
      struct grp_names *x = gnp;
      gnp = gnp->gn_next;
      free(x);
   }
   NYD_LEAVE;
}

static void
_group_print_all(enum group_type gt)
{
   enum group_type xgt;
   struct group **gpa;
   struct group const *gp;
   ui32_t h, i;
   char const **ida;
   FILE *fp;
   size_t lines;
   NYD_ENTER;

   xgt = gt & GT_PRINT_MASK;
   gpa = (  xgt & GT_COMMANDALIAS ? _commandalias_heads
         : (xgt & GT_ALIAS ? _alias_heads
         : (xgt & GT_MLIST ? _mlist_heads
         : (xgt & GT_SHORTCUT ? _shortcut_heads
         : (xgt & GT_CHARSETALIAS ? _charsetalias_heads
         : (xgt & GT_FILETYPE ? _filetype_heads
         : NULL))))));

   for (h = 0, i = 1; h < HSHSIZE; ++h)
      for (gp = gpa[h]; gp != NULL; gp = gp->g_next)
         if ((gp->g_type & xgt) == xgt)
            ++i;
   ida = salloc(i * sizeof *ida);

   for (i = h = 0; h < HSHSIZE; ++h)
      for (gp = gpa[h]; gp != NULL; gp = gp->g_next)
         if ((gp->g_type & xgt) == xgt)
            ida[i++] = gp->g_id;
   ida[i] = NULL;

   if (i > 1)
      qsort(ida, i, sizeof *ida, &__group_print_qsorter);

   if ((fp = Ftmp(NULL, "prgroup", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL)
      fp = n_stdout;
   lines = 0;

   for (i = 0; ida[i] != NULL; ++i)
      lines += _group_print(_group_find(gt, ida[i]), fp);
#ifdef HAVE_REGEX
   if (gt & GT_MLIST) {
      if (gt & GT_SUBSCRIBE)
         i = (ui32_t)_mlsub_size, h = (ui32_t)_mlsub_hits;
      else
         i = (ui32_t)_mlist_size, h = (ui32_t)_mlist_hits;
      if (i > 0 && (n_poption & n_PO_D_V)){
         fprintf(fp, _("# %s list regex(7) total: %u entries, %u hits\n"),
            (gt & GT_SUBSCRIBE ? _("Subscribed") : _("Non-subscribed")),
            i, h);
         ++lines;
      }
   }
#endif

   if (fp != n_stdout) {
      page_or_print(fp, lines);
      Fclose(fp);
   }
   NYD_LEAVE;
}

static int
__group_print_qsorter(void const *a, void const *b)
{
   int rv;
   NYD_ENTER;

   rv = strcmp(*(char**)n_UNCONST(a), *(char**)n_UNCONST(b));
   NYD_LEAVE;
   return rv;
}

static size_t
_group_print(struct group const *gp, FILE *fo)
{
   char const *cp;
   size_t rv;
   NYD_ENTER;

   rv = 1;

   if(gp->g_type & GT_COMMANDALIAS){
      struct a_nag_cmd_alias *ncap;

      GP_TO_SUBCLASS(ncap, gp);
      fprintf(fo, "commandalias %s %s\n",
         n_shexp_quote_cp(gp->g_id, TRU1),
         n_shexp_quote_cp(ncap->ca_expand.s, TRU1));
   } else if (gp->g_type & GT_ALIAS) {
      struct grp_names_head *gnhp;
      struct grp_names *gnp;

      fprintf(fo, "alias %s ", gp->g_id);

      GP_TO_SUBCLASS(gnhp, gp);
      if ((gnp = gnhp->gnh_head) != NULL) { /* xxx always 1+ entries */
         do {
            struct grp_names *x = gnp;
            gnp = gnp->gn_next;
            fprintf(fo, " \"%s\"", string_quote(x->gn_id)); /* TODO shexp */
         } while (gnp != NULL);
      }
      putc('\n', fo);
   } else if (gp->g_type & GT_MLIST) {
#ifdef HAVE_REGEX
      if ((gp->g_type & GT_REGEX) && (n_poption & n_PO_D_V)){
         size_t i;
         struct grp_regex *grp,
            *lp = (gp->g_type & GT_SUBSCRIBE ? _mlsub_regex : _mlist_regex);

         GP_TO_SUBCLASS(grp, gp);
         for (i = 1; lp != grp; lp = lp->gr_next)
            ++i;
         fprintf(fo, "# regex(7): hits %" PRIuZ ", sort %" PRIuZ ".\n  ",
            grp->gr_hits, i);
         ++rv;
      }
#endif

      fprintf(fo, "wysh %s %s\n",
         (gp->g_type & GT_SUBSCRIBE ? "mlsubscribe" : "mlist"),
         n_shexp_quote_cp(gp->g_id, TRU1));
   } else if (gp->g_type & GT_SHORTCUT) {
      GP_TO_SUBCLASS(cp, gp);
      fprintf(fo, "wysh shortcut %s %s\n",
         gp->g_id, n_shexp_quote_cp(cp, TRU1));
   } else if (gp->g_type & GT_CHARSETALIAS) {
      GP_TO_SUBCLASS(cp, gp);
      fprintf(fo, "charsetalias %s %s\n",
         n_shexp_quote_cp(gp->g_id, TRU1), n_shexp_quote_cp(cp, TRU1));
   } else if (gp->g_type & GT_FILETYPE) {
      struct a_nag_file_type *nftp;

      GP_TO_SUBCLASS(nftp, gp);
      fprintf(fo, "filetype %s %s %s\n",
         n_shexp_quote_cp(gp->g_id, TRU1),
         n_shexp_quote_cp(nftp->nft_load.s, TRU1),
         n_shexp_quote_cp(nftp->nft_save.s, TRU1));
   }

   NYD_LEAVE;
   return rv;
}

static int
_mlmux(enum group_type gt, char **argv)
{
   char const *ecp;
   struct group *gp;
   int rv = 0;
   NYD_ENTER;

   rv = 0;
   n_UNINIT(ecp, NULL);

   if (*argv == NULL)
      _group_print_all(gt);
   else do {
      if ((gp = _group_find(gt, *argv)) != NULL) {
         if (gt & GT_SUBSCRIBE) {
            if (!(gp->g_type & GT_SUBSCRIBE)) {
               _MLMUX_LINKOUT(gp);
               gp->g_type |= GT_SUBSCRIBE;
               _MLMUX_LINKIN(gp);
            } else {
               ecp = N_("Mailing-list already `mlsubscribe'd: %s\n");
               goto jerr;
            }
         } else {
            ecp = N_("Mailing-list already `mlist'ed: %s\n");
            goto jerr;
         }
      } else if(_group_fetch(gt, *argv, 0) == NULL) {
         ecp = N_("Failed to create storage for mailing-list: %s\n");
jerr:
         n_err(V_(ecp), n_shexp_quote_cp(*argv, FAL0));
         rv = 1;
      }
   } while (*++argv != NULL);

   NYD_LEAVE;
   return rv;
}

static int
_unmlmux(enum group_type gt, char **argv)
{
   struct group *gp;
   int rv = 0;
   NYD_ENTER;

   for (; *argv != NULL; ++argv) {
      if (gt & GT_SUBSCRIBE) {
         struct group_lookup gl;
         bool_t isaster;

         if (!(isaster = (**argv == '*')))
            gp = _group_find(gt, *argv);
         else if ((gp = _group_go_first(gt, &gl)) == NULL)
            continue;
         else if (gp != NULL && !(gp->g_type & GT_SUBSCRIBE))
            goto jaster_entry;

         if (gp != NULL) {
jaster_redo:
            if (gp->g_type & GT_SUBSCRIBE) {
               _MLMUX_LINKOUT(gp);
               gp->g_type &= ~GT_SUBSCRIBE;
               _MLMUX_LINKIN(gp);
               if (isaster) {
jaster_entry:
                  while ((gp = _group_go_next(&gl)) != NULL &&
                        !(gp->g_type & GT_SUBSCRIBE))
                     ;
                  if (gp != NULL)
                     goto jaster_redo;
               }
            } else {
               n_err(_("Mailing-list not `mlsubscribe'd: %s\n"),
                  n_shexp_quote_cp(*argv, FAL0));
               rv = 1;
            }
            continue;
         }
      } else if (_group_del(gt, *argv))
         continue;
      n_err(_("No such mailing-list: %s\n"), n_shexp_quote_cp(*argv, FAL0));
      rv = 1;
   }
   NYD_LEAVE;
   return rv;
}

#ifdef HAVE_REGEX
static void
_mlmux_linkin(struct group *gp)
{
   struct grp_regex **lpp, *grp, *lhp;
   NYD_ENTER;

   if (gp->g_type & GT_SUBSCRIBE) {
      lpp = &_mlsub_regex;
      ++_mlsub_size;
   } else {
      lpp = &_mlist_regex;
      ++_mlist_size;
   }

   GP_TO_SUBCLASS(grp, gp);
   if ((lhp = *lpp) != NULL) {
      (grp->gr_last = lhp->gr_last)->gr_next = grp;
      (grp->gr_next = lhp)->gr_last = grp;
   } else
      *lpp = grp->gr_last = grp->gr_next = grp;
   grp->gr_hits = 0;
   NYD_LEAVE;
}

static void
_mlmux_linkout(struct group *gp)
{
   struct grp_regex *grp, **lpp;
   NYD_ENTER;

   GP_TO_SUBCLASS(grp, gp);

   if (gp->g_type & GT_SUBSCRIBE) {
      lpp = &_mlsub_regex;
      --_mlsub_size;
      _mlsub_hits -= grp->gr_hits;
   } else {
      lpp = &_mlist_regex;
      --_mlist_size;
      _mlist_hits -= grp->gr_hits;
   }

   if (grp->gr_next == grp)
      *lpp = NULL;
   else {
      (grp->gr_last->gr_next = grp->gr_next)->gr_last = grp->gr_last;
      if (*lpp == grp)
         *lpp = grp->gr_next;
   }
   NYD_LEAVE;
}
#endif /* HAVE_REGEX */

FL struct name *
nalloc(char const *str, enum gfield ntype)
{
   struct n_addrguts ag;
   struct str in, out;
   struct name *np;
   NYD_ENTER;
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
      np = salloc(sizeof(*np) + ag.ag_slen +1);
      memcpy(np + 1, ag.ag_skinned, ag.ag_slen +1);
      ag.ag_skinned = (char*)(np + 1);
   } else
      np = salloc(sizeof *np);

   np->n_flink = NULL;
   np->n_blink = NULL;
   np->n_type = ntype;
   np->n_flags = 0;

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
         mime_fromhdr(&in, &out, TD_ISPR | TD_ICONV);

         for (cp = out.s, i = out.l; i > 0 && spacechar(*cp); --i, ++cp)
            ;
         while (i > 0 && spacechar(cp[i - 1]))
            --i;
         np->n_fullextra = savestrbuf(cp, i);

         n_lofi_free(in.s);
         free(out.s);
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
         in.s = ac_alloc(l + ag.ag_slen + lsuff +1);
         memcpy(in.s, str, l);
         memcpy(in.s + l, ag.ag_skinned, ag.ag_slen);
         l += ag.ag_slen;
         memcpy(in.s + l, str + ag.ag_iaddr_aend, lsuff);
         l += lsuff;
         in.s[l] = '\0';
         in.l = l;
      }
#endif
      mime_fromhdr(&in, &out, TD_ISPR | TD_ICONV);
      np->n_fullname = savestr(out.s);
      free(out.s);
#ifdef HAVE_IDNA
      if (ag.ag_n_flags & NAME_IDNA)
         ac_free(in.s);
#endif
      np->n_flags |= NAME_FULLNAME_SALLOC;
   }
jleave:
   NYD_LEAVE;
   return np;
}

FL struct name *
ndup(struct name *np, enum gfield ntype)
{
   struct name *nnp;
   NYD_ENTER;

   if ((ntype & (GFULL | GSKIN)) && !(np->n_flags & NAME_SKINNED)) {
      nnp = nalloc(np->n_name, ntype);
      goto jleave;
   }

   nnp = salloc(sizeof *np);
   nnp->n_flink = nnp->n_blink = NULL;
   nnp->n_type = ntype;
   nnp->n_flags = (np->n_flags & ~(NAME_NAME_SALLOC | NAME_FULLNAME_SALLOC)) |
         NAME_NAME_SALLOC;
   nnp->n_name = savestr(np->n_name);
   if (np->n_name == np->n_fullname || !(ntype & (GFULL | GSKIN))) {
      nnp->n_fullname = nnp->n_name;
      nnp->n_fullextra = NULL;
   } else {
      nnp->n_flags |= NAME_FULLNAME_SALLOC;
      nnp->n_fullname = savestr(np->n_fullname);
      nnp->n_fullextra = (np->n_fullextra == NULL) ? NULL
            : savestr(np->n_fullextra);
   }
jleave:
   NYD_LEAVE;
   return nnp;
}

FL struct name *
cat(struct name *n1, struct name *n2)
{
   struct name *tail;
   NYD_ENTER;

   tail = n2;
   if (n1 == NULL)
      goto jleave;
   tail = n1;
   if (n2 == NULL)
      goto jleave;

   while (tail->n_flink != NULL)
      tail = tail->n_flink;
   tail->n_flink = n2;
   n2->n_blink = tail;
   tail = n1;
jleave:
   NYD_LEAVE;
   return tail;
}

FL struct name *
namelist_dup(struct name const *np, enum gfield ntype){
   struct name *nlist, *xnp;
   NYD2_ENTER;

   for(nlist = xnp = NULL; np != NULL; np = np->n_flink){
      struct name *x;

      x = ndup(n_UNCONST(np), (np->n_type & ~GMASK) | ntype);
      if((x->n_blink = xnp) == NULL)
         nlist = x;
      else
         xnp->n_flink = x;
      xnp = x;
   }
   NYD2_LEAVE;
   return nlist;
}

FL ui32_t
count(struct name const *np)
{
   ui32_t c;
   NYD_ENTER;

   for (c = 0; np != NULL; np = np->n_flink)
      if (!(np->n_type & GDEL))
         ++c;
   NYD_LEAVE;
   return c;
}

FL ui32_t
count_nonlocal(struct name const *np)
{
   ui32_t c;
   NYD_ENTER;

   for (c = 0; np != NULL; np = np->n_flink)
      if (!(np->n_type & GDEL) && !(np->n_flags & NAME_ADDRSPEC_ISFILEORPIPE))
         ++c;
   NYD_LEAVE;
   return c;
}

FL struct name *
extract(char const *line, enum gfield ntype)
{
   struct name *rv;
   NYD_ENTER;

   rv = _extract1(line, ntype, " \t,", 0);
   NYD_LEAVE;
   return rv;
}

FL struct name *
lextract(char const *line, enum gfield ntype)
{
   struct name *rv;
   NYD_ENTER;

   rv = ((line != NULL && strpbrk(line, ",\"\\(<|"))
         ? _extract1(line, ntype, ",", 1) : extract(line, ntype));
   NYD_LEAVE;
   return rv;
}

FL char *
detract(struct name *np, enum gfield ntype)
{
   char *topp, *cp;
   struct name *p;
   int flags, s;
   NYD_ENTER;

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
   topp = salloc(s);
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
   NYD_LEAVE;
   return topp;
}

FL struct name *
grab_names(enum n_go_input_flags gif, char const *field, struct name *np,
      int comma, enum gfield gflags)
{
   struct name *nq;
   NYD_ENTER;

jloop:
   np = lextract(n_go_input_cp(gif, field, detract(np, comma)), gflags);
   for (nq = np; nq != NULL; nq = nq->n_flink)
      if (is_addr_invalid(nq, EACM_NONE))
         goto jloop;
   NYD_LEAVE;
   return np;
}

FL bool_t
name_is_same_domain(struct name const *n1, struct name const *n2)
{
   char const *d1, *d2;
   bool_t rv;
   NYD_ENTER;

   d1 = strrchr(n1->n_name, '@');
   d2 = strrchr(n2->n_name, '@');

   rv = (d1 != NULL && d2 != NULL) ? !asccasecmp(++d1, ++d2) : FAL0;

   NYD_LEAVE;
   return rv;
}

FL struct name *
checkaddrs(struct name *np, enum expand_addr_check_mode eacm,
   si8_t *set_on_error)
{
   struct name *n;
   NYD_ENTER;

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
   NYD_LEAVE;
   return np;
}

FL struct name *
namelist_vaporise_head(struct header *hp, enum expand_addr_check_mode eacm,
   bool_t metoo, si8_t *set_on_error)
{
   /* TODO namelist_vaporise_head() is incredibly expensive and redundant */
   struct name *tolist, *np, **npp;
   NYD_ENTER;

   tolist = cat(hp->h_to, cat(hp->h_cc, hp->h_bcc));
   hp->h_to = hp->h_cc = hp->h_bcc = NULL;

   tolist = usermap(tolist, metoo);
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
   NYD_LEAVE;
   return tolist;
}

FL struct name *
usermap(struct name *names, bool_t force_metoo){
   struct group *gp;
   struct name *nlist, *nlist_tail, *np, *cp;
   int metoo;
   NYD_ENTER;

   metoo = (force_metoo || ok_blook(metoo));
   nlist = nlist_tail = NULL;
   np = names;

   for(; np != NULL; np = cp){
      assert(!(np->n_type & GDEL)); /* TODO legacy */
      cp = np->n_flink;

      if(is_fileorpipe_addr(np) ||
            (gp = _group_find(GT_ALIAS, np->n_name)) == NULL){
         if((np->n_blink = nlist_tail) != NULL)
            nlist_tail->n_flink = np;
         else
            nlist = np;
         nlist_tail = np;
         np->n_flink = NULL;
      }else{
         nlist = a_nag_gexpand(0, nlist, gp, metoo, np->n_type);
         if((nlist_tail = nlist) != NULL)
            while(nlist_tail->n_flink != NULL)
               nlist_tail = nlist_tail->n_flink;
      }
   }
   NYD_LEAVE;
   return nlist;
}

FL struct name *
elide(struct name *names)
{
   size_t i, j, k;
   struct name *nlist, *np, **nparr;
   NYD_ENTER;

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
   NYD_LEAVE;
   return nlist;
}

FL int
c_alternates(void *v){ /* TODO use a hashmap!! */
   char **argv;
   int rv;
   NYD_ENTER;

   rv = 0;

   if(*(argv = v) == NULL){
      char const *ccp;

      if((ccp = ok_vlook(alternates)) != NULL)
         fprintf(n_stdout, "alternates %s\n", ccp);
      else
         fputs(_("# no alternates registered\n"), n_stdout);
   }else{
      char *cp;
      size_t l, vl;
      struct n_strlist *slp, **slpa;

      while((slp = a_nag_altnames) != NULL){
         a_nag_altnames = slp->sl_next;
         n_free(slp);
      }
      vl = 0;

      /* Extension: only clearance? */
      if(argv[1] == NULL && argv[0][0] == '-' && argv[0][1] == '\0')
         n_UNINIT(cp, NULL);
      else for(slpa = &a_nag_altnames; *argv != NULL; ++argv){
         if(**argv != '\0'){
            struct name *np;

            if((np = lextract(*argv, GSKIN)) == NULL || np->n_flink != NULL ||
                  (np = checkaddrs(np, EACM_STRICT, NULL)) == NULL){
               n_err(_("Invalid `alternates' argument: %s\n"),
                  n_shexp_quote_cp(*argv, FAL0));
               rv = 1;
               continue;
            }

            l = strlen(np->n_name);
            if(UIZ_MAX - l <= vl){
               n_err(_("Failed to create storage for alternate: %s\n"),
                  n_shexp_quote_cp(*argv, FAL0));
               rv = 1;
               continue;
            }

            slp = n_STRLIST_ALLOC(l);
            slp->sl_next = NULL;
            slp->sl_len = l;
            memcpy(slp->sl_dat, np->n_name, ++l);
            *slpa = slp;
            slpa = &slp->sl_next;
            vl += l;
         }
      }

      /* And put it into *alternates* */
      if(vl > 0){
         cp = n_autorec_alloc(vl);
         for(vl = 0, slp = a_nag_altnames; slp != NULL; slp = slp->sl_next){
            memcpy(&cp[vl], slp->sl_dat, slp->sl_len);
            cp[vl += slp->sl_len] = ' ';
            ++vl;
         }
         cp[vl - 1] = '\0';
      }

      n_PS_ROOT_BLOCK(vl > 0 ? ok_vset(alternates, cp) : ok_vclear(alternates));
   }
   NYD_LEAVE;
   return rv;
}

FL struct name *
n_alternates_remove(struct name *np, bool_t keep_single){
   struct name *xp, *newnp;
   NYD_ENTER;

   /* Delete the temporary bit from all */
   for(xp = np; xp != NULL; xp = xp->n_flink)
      xp->n_flags &= ~(ui32_t)SI32_MIN;

   /* Mark all possible alternate names (xxx sic) */

   if(a_nag_altnames != NULL){
      struct n_strlist *slp;

      for(slp = a_nag_altnames; slp != NULL; slp = slp->sl_next)
         np = a_nag_namelist_mark_name(np, slp->sl_dat);
   }

   np = a_nag_namelist_mark_name(np, ok_vlook(LOGNAME));

   for(xp = lextract(ok_vlook(from), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      np = a_nag_namelist_mark_name(np, xp->n_name);

   for(xp = extract(ok_vlook(sender), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      np = a_nag_namelist_mark_name(np, xp->n_name);

   for(xp = lextract(ok_vlook(replyto), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      np = a_nag_namelist_mark_name(np, xp->n_name);

   /* GDEL all (but a single) marked node(s) */
   for(xp = np; xp != NULL; xp = xp->n_flink)
      if(xp->n_flags & (ui32_t)SI32_MIN){
         if(!keep_single)
            xp->n_type |= GDEL;
         keep_single = FAL0;
      }

   /* Clean the list by throwing away all deleted nodes */
   for(xp = newnp = NULL; np != NULL; np = np->n_flink)
      if(!(np->n_type & GDEL)){
         np->n_blink = xp;
         if(xp != NULL)
            xp->n_flink = np;
         else
            newnp = np;
         xp = np;
      }
   np = newnp;

   /* Delete the temporary bit from all remaining (again) */
   for(xp = np; xp != NULL; xp = xp->n_flink)
      xp->n_flags &= ~(ui32_t)SI32_MIN;

   NYD_LEAVE;
   return np;
}

FL bool_t
n_is_myname(char const *name){
   struct name *xp;
   NYD_ENTER;

   if(a_nag_is_same_name(ok_vlook(LOGNAME), name))
      goto jleave;

   if(a_nag_altnames != NULL){
      struct n_strlist *slp;

      for(slp = a_nag_altnames; slp != NULL; slp = slp->sl_next)
         if(a_nag_is_same_name(slp->sl_dat, name))
            goto jleave;
   }

   for(xp = lextract(ok_vlook(from), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      if(a_nag_is_same_name(xp->n_name, name))
         goto jleave;

   for(xp = lextract(ok_vlook(replyto), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      if(a_nag_is_same_name(xp->n_name, name))
         goto jleave;

   for(xp = extract(ok_vlook(sender), GEXTRA | GSKIN); xp != NULL;
         xp = xp->n_flink)
      if(a_nag_is_same_name(xp->n_name, name))
         goto jleave;

   name = NULL;
jleave:
   NYD_LEAVE;
   return (name != NULL);
}

FL int
c_addrcodec(void *vp){
   struct n_addrguts ag;
   struct n_string s_b, *sp;
   size_t alen;
   int mode;
   char const **argv, *varname, *act, *cp;
   NYD_ENTER;

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

   /* C99 */{
      size_t i;

      i = strlen(cp);
      if(i <= UIZ_MAX / 4)
         i <<= 1;
      sp = n_string_reserve(sp, i);
   }

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
      }else if(is_ascncaseprefix(act, "skin", alen)){
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
   NYD_LEAVE;
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
   struct group *gp;
   char const **argv, *ccp;
   int rv;
   NYD_ENTER;

   rv = 0;
   argv = vp;

   if((ccp = *argv) == NULL){
      _group_print_all(GT_COMMANDALIAS);
      goto jleave;
   }

   /* Verify the name is a valid one, and not a command modifier */
   if(*ccp == '\0' || *n_cmd_isolate(ccp) != '\0' ||
         !asccasecmp(ccp, "ignerr") || !asccasecmp(ccp, "wysh") ||
         !asccasecmp(ccp, "vput")){
      n_err(_("`commandalias': not a valid command name: %s\n"),
         n_shexp_quote_cp(ccp, FAL0));
      rv = 1;
      goto jleave;
   }

   if(argv[1] == NULL){
      if((gp = _group_find(GT_COMMANDALIAS, ccp)) != NULL)
         _group_print(gp, n_stdout);
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
         _group_del(GT_COMMANDALIAS, ccp);

      for(i = len = 0, ++argv; argv[i] != NULL; ++i)
         len += strlen(argv[i]) + 1;
      if(len == 0)
         len = 1;

      if((gp = _group_fetch(GT_COMMANDALIAS, ccp, len)) == NULL){
         n_err(_("Failed to create storage for commandalias: %s\n"),
            n_shexp_quote_cp(ccp, FAL0));
         rv = 1;
      }else{
         struct a_nag_cmd_alias *ncap;

         GP_TO_SUBCLASS(ncap, gp);
         GP_TO_SUBCLASS(cp, gp);
         cp += sizeof *ncap;
         ncap->ca_expand.s = cp;
         ncap->ca_expand.l = len - 1;

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
   NYD_LEAVE;
   return rv;
}

FL int
c_uncommandalias(void *vp){
   char **argv;
   int rv;
   NYD_ENTER;

   rv = 0;
   argv = vp;

   do if(!_group_del(GT_COMMANDALIAS, *argv)){
      n_err(_("No such `commandalias': %s\n"), n_shexp_quote_cp(*argv, FAL0));
      rv = 1;
   }while(*++argv != NULL);
   NYD_LEAVE;
   return rv;
}

FL char const *
n_commandalias_exists(char const *name, struct str const **expansion_or_null){
   struct group *gp;
   NYD_ENTER;

   if((gp = _group_find(GT_COMMANDALIAS, name)) != NULL){
      name = gp->g_id;

      if(expansion_or_null != NULL){
         struct a_nag_cmd_alias *ncap;

         GP_TO_SUBCLASS(ncap, gp);
         *expansion_or_null = &ncap->ca_expand;
      }
   }else
      name = NULL;
   NYD_LEAVE;
   return name;
}

FL bool_t
n_alias_is_valid_name(char const *name){
   char c;
   char const *cp;
   bool_t rv;
   NYD2_ENTER;

   for(rv = TRU1, cp = name++; (c = *cp++) != '\0';)
      /* User names, plus things explicitly mentioned in Postfix aliases(5).
       * As an extension, allow period: [[:alnum:]_#:@.-]+$? */
      if(!alnumchar(c) && c != '_' && c != '-' &&
            c != '#' && c != ':' && c != '@' &&
            c != '.'){
         if(c == '$' && cp != name && *cp == '\0')
            break;
         rv = FAL0;
         break;
      }
   NYD2_LEAVE;
   return rv;
}

FL int
c_alias(void *v)
{
   char const *ecp;
   char **argv;
   struct group *gp;
   int rv;
   NYD_ENTER;

   rv = 0;
   argv = v;
   n_UNINIT(ecp, NULL);

   if (*argv == NULL)
      _group_print_all(GT_ALIAS);
   else if (!n_alias_is_valid_name(*argv)) {
      ecp = N_("Not a valid alias name: %s\n");
      goto jerr;
   } else if (argv[1] == NULL) {
      if ((gp = _group_find(GT_ALIAS, *argv)) != NULL)
         _group_print(gp, n_stdout);
      else {
         ecp = N_("No such alias: %s\n");
         goto jerr;
      }
   } else if ((gp = _group_fetch(GT_ALIAS, *argv, 0)) == NULL) {
      ecp = N_("Failed to create alias storage for: %s\n");
jerr:
      n_err(V_(ecp), n_shexp_quote_cp(*argv, FAL0));
      rv = 1;
   } else {
      struct grp_names *gnp_tail, *gnp;
      struct grp_names_head *gnhp;

      GP_TO_SUBCLASS(gnhp, gp);

      if((gnp_tail = gnhp->gnh_head) != NULL)
         while((gnp = gnp_tail->gn_next) != NULL)
            gnp_tail = gnp;

      for(++argv; *argv != NULL; ++argv){
         size_t i;

         i = strlen(*argv) +1;
         gnp = smalloc(n_VSTRUCT_SIZEOF(struct grp_names, gn_id) + i);
         if(gnp_tail != NULL)
            gnp_tail->gn_next = gnp;
         else
            gnhp->gnh_head = gnp;
         gnp_tail = gnp;
         gnp->gn_next = NULL;
         memcpy(gnp->gn_id, *argv, i);
      }
   }
   NYD_LEAVE;
   return rv;
}

FL int
c_unalias(void *v)
{
   char **argv = v;
   int rv = 0;
   NYD_ENTER;

   do if (!_group_del(GT_ALIAS, *argv)) {
      n_err(_("No such alias: %s\n"), *argv);
      rv = 1;
   } while (*++argv != NULL);
   NYD_LEAVE;
   return rv;
}

FL int
c_mlist(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _mlmux(GT_MLIST, v);
   NYD_LEAVE;
   return rv;
}

FL int
c_unmlist(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _unmlmux(GT_MLIST, v);
   NYD_LEAVE;
   return rv;
}

FL int
c_mlsubscribe(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _mlmux(GT_MLIST | GT_SUBSCRIBE, v);
   NYD_LEAVE;
   return rv;
}

FL int
c_unmlsubscribe(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _unmlmux(GT_MLIST | GT_SUBSCRIBE, v);
   NYD_LEAVE;
   return rv;
}

FL enum mlist_state
is_mlist(char const *name, bool_t subscribed_only)
{
   struct group *gp;
#ifdef HAVE_REGEX
   struct grp_regex **lpp, *grp;
   bool_t re2;
#endif
   enum mlist_state rv;
   NYD_ENTER;

   gp = _group_find(GT_MLIST, name);
   rv = (gp != NULL) ? MLIST_KNOWN : MLIST_OTHER;
   if (rv == MLIST_KNOWN) {
      if (gp->g_type & GT_SUBSCRIBE)
         rv = MLIST_SUBSCRIBED;
      else if (subscribed_only)
         rv = MLIST_OTHER;
      /* Of course, if that is a regular expression it doesn't mean a thing */
#ifdef HAVE_REGEX
      if (gp->g_type & GT_REGEX)
         rv = MLIST_OTHER;
      else
#endif
         goto jleave;
   }

   /* Not in the hashmap (as something matchable), walk the lists */
#ifdef HAVE_REGEX
   re2 = FAL0;
   lpp = &_mlsub_regex;
jregex_redo:
   if ((grp = *lpp) != NULL) {
      do if (regexec(&grp->gr_regex, name, 0,NULL, 0) != REG_NOMATCH) {
         /* Relink as the head of this list if the hit count of this group is
          * >= 25% of the average hit count */
         size_t i;
         if (!re2)
            i = ++_mlsub_hits / _mlsub_size;
         else
            i = ++_mlist_hits / _mlist_size;
         i >>= 2;

         if (++grp->gr_hits >= i && *lpp != grp && grp->gr_next != grp) {
            grp->gr_last->gr_next = grp->gr_next;
            grp->gr_next->gr_last = grp->gr_last;
            (grp->gr_last = (*lpp)->gr_last)->gr_next = grp;
            (grp->gr_next = *lpp)->gr_last = grp;
            *lpp = grp;
         }
         rv = !re2 ? MLIST_SUBSCRIBED : MLIST_KNOWN;
         goto jleave;
      } while ((grp = grp->gr_next) != *lpp);
   }
   if (!re2 && !subscribed_only) {
      re2 = TRU1;
      lpp = &_mlist_regex;
      goto jregex_redo;
   }
   assert(rv == MLIST_OTHER);
#endif

jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_shortcut(void *v)
{
   struct group *gp;
   char **argv;
   int rv;
   NYD_ENTER;

   rv = 0;
   argv = v;

   if(*argv == NULL)
      _group_print_all(GT_SHORTCUT);
   else if(argv[1] == NULL){
      if((gp = _group_find(GT_SHORTCUT, *argv)) != NULL)
         _group_print(gp, n_stdout);
      else{
         n_err(_("No such shortcut: %s\n"), n_shexp_quote_cp(*argv, FAL0));
         rv = 1;
      }
   }else for (; *argv != NULL; argv += 2) {
      /* Because one hardly ever redefines, anything is stored in one chunk */
      size_t l;
      char *cp;

      if (argv[1] == NULL) {
         n_err(_("Synopsis: shortcut: <shortcut> <expansion>\n"));
         rv = 1;
         break;
      }
      if (_group_find(GT_SHORTCUT, *argv) != NULL)
         _group_del(GT_SHORTCUT, *argv);

      l = strlen(argv[1]) +1;
      if ((gp = _group_fetch(GT_SHORTCUT, *argv, l)) == NULL) {
         n_err(_("Failed to create storage for shortcut: %s\n"),
            n_shexp_quote_cp(*argv, FAL0));
         rv = 1;
      } else {
         GP_TO_SUBCLASS(cp, gp);
         memcpy(cp, argv[1], l);
      }
   }
   NYD_LEAVE;
   return rv;
}

FL int
c_unshortcut(void *v)
{
   char **argv = v;
   int rv = 0;
   NYD_ENTER;

   do if (!_group_del(GT_SHORTCUT, *argv)) {
      n_err(_("No such shortcut: %s\n"), *argv);
      rv = 1;
   } while (*++argv != NULL);
   NYD_LEAVE;
   return rv;
}

FL char const *
shortcut_expand(char const *str){
   struct group *gp;
   NYD_ENTER;

   if((gp = _group_find(GT_SHORTCUT, str)) != NULL)
      GP_TO_SUBCLASS(str, gp);
   else
      str = NULL;
   NYD_LEAVE;
   return str;
}

FL int
c_charsetalias(void *vp){
   struct group *gp;
   char **argv;
   int rv;
   NYD_ENTER;

   rv = 0;
   argv = vp;

   if(*argv == NULL)
      _group_print_all(GT_CHARSETALIAS);
   else if(argv[1] == NULL){
      if((gp = _group_find(GT_CHARSETALIAS, *argv)) != NULL)
         _group_print(gp, n_stdout);
      else{
         n_err(_("No such charsetalias: %s\n"), n_shexp_quote_cp(*argv, FAL0));
         rv = 1;
      }
   }else for(; *argv != NULL; argv += 2){
      /* Because one hardly ever redefines, anything is stored in one chunk */
      char const *ccp;
      char *cp, c;
      size_t l;

      if(argv[1] == NULL){
         n_err(_("Synopsis: charsetalias: <charset> <charset-alias>\n"));
         rv = 1;
         break;
      }

      /* Delete the old one, if any; don't get fooled to remove them all */
      ccp = argv[0];
      if(ccp[0] != '*' || ccp[1] != '\0')
         _group_del(GT_CHARSETALIAS, ccp);

      l = strlen(argv[1]) +1;
      if ((gp = _group_fetch(GT_CHARSETALIAS, ccp, l)) == NULL) {
         n_err(_("Failed to create storage for charsetalias: %s\n"),
            n_shexp_quote_cp(ccp, FAL0));
         rv = 1;
      } else {
         GP_TO_SUBCLASS(cp, gp);
         for(ccp = argv[1]; (c = *ccp++) != '\0';)
            *cp++ = lowerconv(c);
         *cp = '\0';
      }
   }
   NYD_LEAVE;
   return rv;
}

FL int
c_uncharsetalias(void *vp){
   char **argv;
   int rv;
   NYD_ENTER;

   rv = 0;
   argv = vp;

   do if(!_group_del(GT_CHARSETALIAS, *argv)){
      n_err(_("No such `charsetalias': %s\n"), n_shexp_quote_cp(*argv, FAL0));
      rv = 1;
   }while(*++argv != NULL);
   NYD_LEAVE;
   return rv;
}

FL char const *
n_charsetalias_expand(char const *cp){
   struct group *gp;
   size_t i;
   char const *cp_orig;
   NYD_ENTER;

   cp_orig = cp;

   for(i = 0; (gp = _group_find(GT_CHARSETALIAS, cp)) != NULL;){
      GP_TO_SUBCLASS(cp, gp);
      if(++i == 8) /* XXX Magic (same as for `ghost' expansion) */
         break;
   }

   if(cp != cp_orig)
      cp = savestr(cp);
   NYD_LEAVE;
   return cp;
}

FL int
c_filetype(void *vp){ /* TODO support automatic chains: .tar.gz -> .gz + .tar */
   struct group *gp;
   char **argv; /* TODO While there: let ! prefix mean: direct execlp(2) */
   int rv;
   NYD_ENTER;

   rv = 0;
   argv = vp;

   if(*argv == NULL)
      _group_print_all(GT_FILETYPE);
   else if(argv[1] == NULL){
      if((gp = _group_find(GT_FILETYPE, *argv)) != NULL)
         _group_print(gp, n_stdout);
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
         _group_del(GT_FILETYPE, ccp);

      /* Lowercase it all (for display purposes) */
      cp = savestr(ccp);
      ccp = cp;
      while((c = *cp) != '\0')
         *cp++ = lowerconv(c);

      llc = strlen(argv[1]) +1;
      lsc = strlen(argv[2]) +1;
      if(UIZ_MAX - llc <= lsc)
         goto jenomem;

      if((gp = _group_fetch(GT_FILETYPE, ccp, llc + lsc)) == NULL){
jenomem:
         n_err(_("Failed to create storage for filetype: %s\n"),
            n_shexp_quote_cp(argv[0], FAL0));
         rv = 1;
      }else{
         struct a_nag_file_type *nftp;

         GP_TO_SUBCLASS(nftp, gp);
         GP_TO_SUBCLASS(cp, gp);
         cp += sizeof *nftp;
         memcpy(nftp->nft_load.s = cp, argv[1], llc);
            cp += llc;
            nftp->nft_load.l = --llc;
         memcpy(nftp->nft_save.s = cp, argv[2], lsc);
            /*cp += lsc;*/
            nftp->nft_save.l = --lsc;
      }
   }
   NYD_LEAVE;
   return rv;
}

FL int
c_unfiletype(void *vp){
   char **argv;
   int rv;
   NYD_ENTER;

   rv = 0;
   argv = vp;

   do if(!_group_del(GT_FILETYPE, *argv)){
      n_err(_("No such `filetype': %s\n"), n_shexp_quote_cp(*argv, FAL0));
      rv = 1;
   }while(*++argv != NULL);
   NYD_LEAVE;
   return rv;
}

FL bool_t
n_filetype_trial(struct n_file_type *res_or_null, char const *file){
   struct stat stb;
   struct group_lookup gl;
   struct n_string s, *sp;
   struct group const *gp;
   ui32_t l;
   NYD2_ENTER;

   sp = n_string_creat_auto(&s);
   sp = n_string_assign_cp(sp, file);
   sp = n_string_push_c(sp, '.');
   l = sp->s_len;

   for(gp = _group_go_first(GT_FILETYPE, &gl); gp != NULL;
         gp = _group_go_next(&gl)){
      sp = n_string_trunc(sp, l);
      sp = n_string_push_buf(sp, gp->g_id,
            gp->g_subclass_off - gp->g_id_len_sub);

      if(!stat(n_string_cp(sp), &stb) && S_ISREG(stb.st_mode)){
         if(res_or_null != NULL){
            struct a_nag_file_type *nftp;

            GP_TO_SUBCLASS(nftp, gp);
            res_or_null->ft_ext_dat = gp->g_id;
            res_or_null->ft_ext_len = gp->g_subclass_off - gp->g_id_len_sub;
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
   gp = (struct group*)0x1;

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

   gp = NULL;

jleave:
   NYD2_LEAVE;
   return (gp != NULL);
}

FL bool_t
n_filetype_exists(struct n_file_type *res_or_null, char const *file){
   char const *ext, *lext;
   NYD2_ENTER;

   if((ext = strrchr(file, '/')) != NULL)
      file = ++ext;

   for(lext = NULL; (ext = strchr(file, '.')) != NULL; lext = file = ext){
      struct group const *gp;

      if((gp = _group_find(GT_FILETYPE, ++ext)) != NULL){
         lext = ext;
         if(res_or_null != NULL){
            struct a_nag_file_type *nftp;

            GP_TO_SUBCLASS(nftp, gp);
            res_or_null->ft_ext_dat = gp->g_id;
            res_or_null->ft_ext_len = gp->g_subclass_off - gp->g_id_len_sub;
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
   NYD2_LEAVE;
   return (lext != NULL);
}

/* s-it-mode */
