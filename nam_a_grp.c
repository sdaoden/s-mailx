/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Name lists, alternates and groups: aliases, mailing lists, shortcuts.
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
   GT_ALIAS       = 1<< 0,
   GT_MLIST       = 1<< 1,
   GT_SHORTCUT    = 1<< 2,
   GT_MASK        = GT_ALIAS | GT_MLIST | GT_SHORTCUT,

   /* Subtype bits and flags */
   GT_SUBSCRIBE   = 1<< 4,
   GT_REGEX       = 1<< 5,

   /* Extended type mask to be able to reflect what we really have; i.e., mlist
    * can have GT_REGEX if they are subscribed or not, but `mlsubscribe' should
    * print out only GT_MLIST which have the GT_SUBSCRIBE attribute set */
   GT_PRINT_MASK  = GT_MASK | GT_SUBSCRIBE
};

struct group {
   struct group   *g_next;
   size_t         g_subclass_off;   /* of "subclass" in .g_id (if any) */
   ui8_t          g_type;           /* enum group_type */
   /* Identifying name, of variable size.  Dependent on actual "subtype" more
    * data follows thereafter, but note this is always used (i.e., for regular
    * expression entries this is still set to the plain string) */
   char           g_id[n_VFIELD_SIZE(-1)];
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

struct group_lookup {
   struct group   **gl_htable;
   struct group   **gl_slot;
   struct group   *gl_slot_last;
   struct group   *gl_group;
};

/* List of alternate names of user */
static char             **_altnames;

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
static struct group     *_shortcut_heads[HSHSIZE]; /* TODO dynamic hash */

/* Same name, while taking care for *allnet*? */
static bool_t        _same_name(char const *n1, char const *n2);

/* Delete the given name from a namelist */
static struct name * delname(struct name *np, char const *name);

/* Put another node onto a list of names and return the list */
static struct name * put(struct name *list, struct name *node);

/* Grab a single name (liberal name) */
static char const *  yankname(char const *ap, char *wbuf,
                        char const *separators, int keepcomms);

/* Extraction multiplexer that splits an input line to names */
static struct name * _extract1(char const *line, enum gfield ntype,
                        char const *separators, bool_t keepcomms);

/* Recursively expand a alias name.  Limit expansion to some fixed level.
 * Direct recursion is not expanded for convenience */
static struct name * _gexpand(size_t level, struct name *nlist,
                        struct group *gp, bool_t metoo, int ntype);

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

/* Fetch the group id, create it as necessary */
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
_same_name(char const *n1, char const *n2)
{
   bool_t rv = FAL0;
   char c1, c2;
   NYD_ENTER;

   if (ok_blook(allnet)) {
      do {
         c1 = *n1++;
         c2 = *n2++;
         c1 = lowerconv(c1);
         c2 = lowerconv(c2);
         if (c1 != c2)
            goto jleave;
      } while (c1 != '\0' && c2 != '\0' && c1 != '@' && c2 != '@');
      rv = 1;
   } else
      rv = !asccasecmp(n1, n2);
jleave:
   NYD_LEAVE;
   return rv;
}

static struct name *
delname(struct name *np, char const *name)
{
   struct name *p;
   NYD_ENTER;

   for (p = np; p != NULL; p = p->n_flink)
      if (_same_name(p->n_name, name)) {
         if (p->n_blink == NULL) {
            if (p->n_flink != NULL)
               p->n_flink->n_blink = NULL;
            np = p->n_flink;
            continue;
         }
         if (p->n_flink == NULL) {
            if (p->n_blink != NULL)
               p->n_blink->n_flink = NULL;
            continue;
         }
         p->n_blink->n_flink = p->n_flink;
         p->n_flink->n_blink = p->n_blink;
      }
   NYD_LEAVE;
   return np;
}

static struct name *
put(struct name *list, struct name *node)
{
   NYD_ENTER;
   node->n_flink = list;
   node->n_blink = NULL;
   if (list != NULL)
      list->n_blink = node;
   NYD_LEAVE;
   return node;
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
_gexpand(size_t level, struct name *nlist, struct group *gp, bool_t metoo,
   int ntype)
{
   struct grp_names_head *gnhp;
   struct grp_names *gnp;
   NYD_ENTER;

   if (UICMP(z, level++, >, MAXEXP)) {
      n_err(_("Expanding alias to depth larger than %d\n"), MAXEXP);
      goto jleave;
   }

   GP_TO_SUBCLASS(gnhp, gp);
   for (gnp = gnhp->gnh_head; gnp != NULL; gnp = gnp->gn_next) {
      char *cp;
      struct group *ngp;

      /* FIXME we do not really support leading backslash quoting do we??? */
      if (*(cp = gnp->gn_id) == '\\' || !strcmp(cp, gp->g_id))
         goto jquote;

      if ((ngp = _group_find(GT_ALIAS, cp)) != NULL) {
         /* For S-nail(1), the "alias" may *be* the sender in that a name maps
          * to a full address specification; aliases cannot be empty */
         struct grp_names_head *ngnhp;
         GP_TO_SUBCLASS(ngnhp, ngp);

         assert(ngnhp->gnh_head != NULL);
         if (metoo || ngnhp->gnh_head->gn_next != NULL ||
               !_same_name(cp, myname))
            nlist = _gexpand(level, nlist, ngp, metoo, ntype);
         continue;
      }

      /* Here we should allow to expand to itself if only person in alias */
jquote:
      if (metoo || gnhp->gnh_head->gn_next == NULL || !_same_name(cp, myname))
         nlist = put(nlist, nalloc(cp, ntype | GFULL));
   }
jleave:
   NYD_LEAVE;
   return nlist;
}

static struct group *
_group_lookup(enum group_type gt, struct group_lookup *glp, char const *id)
{
   struct group *lgp, *gp;
   NYD_ENTER;

   gt &= GT_MASK;
   lgp = NULL;
   gp = *(glp->gl_htable = glp->gl_slot =
          ((gt & GT_ALIAS ? _alias_heads :
            (gt & GT_MLIST ? _mlist_heads :
            (/*gt & GT_SHORTCUT ?*/ _shortcut_heads /*: NULL */))) +
           torek_hash(id) % HSHSIZE));

   for (; gp != NULL; lgp = gp, gp = gp->g_next)
      if ((gp->g_type & gt) && *gp->g_id == *id && !strcmp(gp->g_id, id))
         break;

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

   for (glp->gl_htable = gpa = (gt & GT_ALIAS ? _alias_heads :
            (gt & GT_MLIST ? _mlist_heads :
            (/*gt & GT_SHORTCUT ?*/ _shortcut_heads /*: NULL */))), i = 0;
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
   i = n_ALIGN(n_VSTRUCT_SIZEOF(struct group, g_id) + l);
   switch (gt & GT_MASK) {
   case GT_ALIAS:
      addsz = sizeof(struct grp_names_head);
      break;
   case GT_MLIST:
#ifdef HAVE_REGEX
      if (n_is_maybe_regex(id)) {
         addsz = sizeof(struct grp_regex);
         gt |= GT_REGEX;
      }
#endif
   case GT_SHORTCUT:
   default:
      break;
   }

   gp = smalloc(i + addsz);
   gp->g_subclass_off = i;
   gp->g_type = gt;
   memcpy(gp->g_id, id, l);

   if (gt & GT_ALIAS) {
      struct grp_names_head *gnhp;

      GP_TO_SUBCLASS(gnhp, gp);
      gnhp->gnh_head = NULL;
   }
#ifdef HAVE_REGEX
   else if (/*(gt & GT_MLIST) &&*/ gt & GT_REGEX) {
      struct grp_regex *grp;
      GP_TO_SUBCLASS(grp, gp);

      if (regcomp(&grp->gr_regex, id, REG_EXTENDED | REG_ICASE | REG_NOSUB)) {
         n_err(_("Invalid regular expression: %s\n"), id);
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
   if ((gp = glp->gl_slot_last) != NULL) {
      gp = (gp->g_next = x->g_next);
   } else {
      glp->gl_slot_last = NULL;
      gp = (*glp->gl_slot = x->g_next);

      if (gp == NULL) {
         struct group **gpa = glp->gl_htable + HSHSIZE;

         while (++glp->gl_slot < gpa)
            if ((gp = *glp->gl_slot) != NULL)
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
   gpa = (xgt & GT_ALIAS ? _alias_heads
         : (xgt & GT_MLIST ? _mlist_heads
         : (/*xgt & GT_SHORTCUT ?*/ _shortcut_heads
         /*: NULL */)));

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
      fp = stdout;
   lines = 0;

   for (i = 0; ida[i] != NULL; ++i)
      lines += _group_print(_group_find(gt, ida[i]), fp);
#ifdef HAVE_REGEX
   if (gt & GT_MLIST) {
      if (gt & GT_SUBSCRIBE)
         i = (ui32_t)_mlsub_size, h = (ui32_t)_mlsub_hits;
      else
         i = (ui32_t)_mlist_size, h = (ui32_t)_mlist_hits;
      if (i > 0 && (options & OPT_D_V)){
         fprintf(fp, _("# %s list regex(7) total: %u entries, %u hits\n"),
            (gt & GT_SUBSCRIBE ? _("Subscribed") : _("Non-subscribed")),
            i, h);
         ++lines;
      }
   }
#endif

   if (fp != stdout) {
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

   if (gp->g_type & GT_ALIAS) {
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
      if ((gp->g_type & GT_REGEX) && (options & OPT_D_V)){
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
   }

   NYD_LEAVE;
   return rv;
}

static int
_mlmux(enum group_type gt, char **argv)
{
   struct group *gp;
   int rv = 0;
   NYD_ENTER;

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
               n_err(_("Mailing-list already `mlsubscribe'd: %s\n"),
                  *argv);
               rv = 1;
            }
         } else {
            n_err(_("Mailing-list already `mlist'ed: %s\n"), *argv);
            rv = 1;
         }
      } else
         _group_fetch(gt, *argv, 0);
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
         ((ntype & (GFULL | GSKIN | GREF)) != 0));
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
namelist_dup(struct name const *np, enum gfield ntype)
{
   struct name *nnp;
   NYD_ENTER;

   for (nnp = NULL; np != NULL; np = np->n_flink) {
      struct name *x = ndup(n_UNCONST(np), (np->n_type & ~GMASK) | ntype);
      x->n_flink = nnp;
      nnp = x;
   }
   NYD_LEAVE;
   return nnp;
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
grab_names(enum n_lexinput_flags lif, char const *field, struct name *np,
      int comma, enum gfield gflags)
{
   struct name *nq;
   NYD_ENTER;

jloop:
   np = lextract(n_lex_input_cp(lif, field, detract(np, comma)), gflags);
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

   for (n = np; n != NULL;) {
      si8_t rv;

      if ((rv = is_addr_invalid(n, eacm)) != 0) {
         if (set_on_error != NULL)
            *set_on_error |= rv; /* don't loose -1! */
         if (n->n_blink)
            n->n_blink->n_flink = n->n_flink;
         if (n->n_flink)
            n->n_flink->n_blink = n->n_blink;
         if (n == np)
            np = n->n_flink;
      }
      n = n->n_flink;
   }
   NYD_LEAVE;
   return np;
}

FL struct name *
namelist_vaporise_head(struct header *hp, enum expand_addr_check_mode eacm,
   bool_t metoo, si8_t *set_on_error)
{
   struct name *tolist, *np, **npp;
   NYD_ENTER;

   tolist = usermap(cat(hp->h_to, cat(hp->h_cc, hp->h_bcc)), metoo);
   hp->h_to = hp->h_cc = hp->h_bcc = NULL;

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
usermap(struct name *names, bool_t force_metoo)
{
   struct name *new, *np, *cp;
   struct group *gp;
   int metoo;
   NYD_ENTER;

   new = NULL;
   np = names;
   metoo = (force_metoo || ok_blook(metoo));
   while (np != NULL) {
      assert(!(np->n_type & GDEL)); /* TODO legacy */
      if (is_fileorpipe_addr(np) || np->n_name[0] == '\\') {
         cp = np->n_flink;
         new = put(new, np);
         np = cp;
         continue;
      }
      gp = _group_find(GT_ALIAS, np->n_name);
      cp = np->n_flink;
      if (gp != NULL)
         new = _gexpand(0, new, gp, metoo, np->n_type);
      else
         new = put(new, np);
      np = cp;
   }
   NYD_LEAVE;
   return new;
}

FL struct name *
elide(struct name *names)
{
   struct name *np, *t, *newn, *x;
   NYD_ENTER;

   newn = NULL;
   if (names == NULL)
      goto jleave;

   /* Throw away all deleted nodes (XXX merge with plain sort below?) */
   for (np = NULL; names != NULL; names = names->n_flink)
      if  (!(names->n_type & GDEL)) {
         names->n_blink = np;
         if (np)
            np->n_flink = names;
         else
            newn = names;
         np = names;
      }
   if (newn == NULL)
      goto jleave;

   np = newn->n_flink;
   if (np != NULL)
      np->n_blink = NULL;
   newn->n_flink = NULL;

   while (np != NULL) {
      int cmpres;

      t = newn;
      while ((cmpres = asccasecmp(t->n_name, np->n_name)) < 0) {
         if (t->n_flink == NULL)
            break;
         t = t->n_flink;
      }

      /* If we ran out of t's, put new entry after the current value of t */
      if (cmpres < 0) {
         t->n_flink = np;
         np->n_blink = t;
         t = np;
         np = np->n_flink;
         t->n_flink = NULL;
         continue;
      }

      /* Otherwise, put the new entry in front of the current t.  If at the
       * front of the list, the new guy becomes the new head of the list */
      if (t == newn) {
         t = np;
         np = np->n_flink;
         t->n_flink = newn;
         newn->n_blink = t;
         t->n_blink = NULL;
         newn = t;
         continue;
      }

      /* The normal case -- we are inserting into the middle of the list */
      x = np;
      np = np->n_flink;
      x->n_flink = t;
      x->n_blink = t->n_blink;
      t->n_blink->n_flink = x;
      t->n_blink = x;
   }

   /* Now the list headed up by new is sorted.  Remove duplicates */
   np = newn;
   while (np != NULL) {
      t = np;
      while (t->n_flink != NULL && !asccasecmp(np->n_name, t->n_flink->n_name))
         t = t->n_flink;
      if (t == np) {
         np = np->n_flink;
         continue;
      }

      /* Now t points to the last entry with the same name as np.
       * Make np point beyond t */
      np->n_flink = t->n_flink;
      if (t->n_flink != NULL)
         t->n_flink->n_blink = np;
      np = np->n_flink;
   }
jleave:
   NYD_LEAVE;
   return newn;
}

FL int
c_alternates(void *v)
{
   size_t l;
   char **namelist = v, **ap, **ap2, *cp;
   NYD_ENTER;

   for (namelist = v, l = 0; namelist[l] != NULL; ++l)
      ;

   if (l == 0) {
      if (_altnames != NULL) {
         printf("alternates ");
         for (ap = _altnames; *ap != NULL; ++ap)
            printf("%s ", *ap);
         printf("\n");
      }
      goto jleave;
   }

   if (_altnames != NULL) {
      for (ap = _altnames; *ap != NULL; ++ap)
         free(*ap);
      free(_altnames);
   }

   ++l;
   _altnames = smalloc(l * sizeof(*_altnames));
   for (ap = namelist, ap2 = _altnames; *ap != NULL; ++ap, ++ap2) {
      l = strlen(*ap) +1;
      cp = smalloc(l);
      memcpy(cp, *ap, l);
      *ap2 = cp;
   }
   *ap2 = NULL;
jleave:
   NYD_LEAVE;
   return 0;
}

FL struct name *
delete_alternates(struct name *np)
{
   struct name *xp;
   char **ap;
   NYD_ENTER;

   np = delname(np, myname);
   if (_altnames != NULL)
      for (ap = _altnames; *ap != '\0'; ++ap)
         np = delname(np, *ap);

   if ((xp = lextract(ok_vlook(from), GEXTRA | GSKIN)) != NULL)
      while (xp != NULL) {
         np = delname(np, xp->n_name);
         xp = xp->n_flink;
      }

   if ((xp = lextract(ok_vlook(replyto), GEXTRA | GSKIN)) != NULL)
      while (xp != NULL) {
         np = delname(np, xp->n_name);
         xp = xp->n_flink;
      }

   if ((xp = extract(ok_vlook(sender), GEXTRA | GSKIN)) != NULL)
      while (xp != NULL) {
         np = delname(np, xp->n_name);
         xp = xp->n_flink;
      }
   NYD_LEAVE;
   return np;
}

FL int
is_myname(char const *name)
{
   int rv = 1;
   struct name *xp;
   char **ap;
   NYD_ENTER;

   if (_same_name(myname, name))
      goto jleave;
   if (_altnames != NULL)
      for (ap = _altnames; *ap != NULL; ++ap)
         if (_same_name(*ap, name))
            goto jleave;

   if ((xp = lextract(ok_vlook(from), GEXTRA | GSKIN)) != NULL)
      while (xp != NULL) {
         if (_same_name(xp->n_name, name))
            goto jleave;
         xp = xp->n_flink;
      }

   if ((xp = lextract(ok_vlook(replyto), GEXTRA | GSKIN)) != NULL)
      while (xp != NULL) {
         if (_same_name(xp->n_name, name))
            goto jleave;
         xp = xp->n_flink;
      }

   if ((xp = extract(ok_vlook(sender), GEXTRA | GSKIN)) != NULL)
      while (xp != NULL) {
         if (_same_name(xp->n_name, name))
            goto jleave;
         xp = xp->n_flink;
      }
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_alias(void *v)
{
   char **argv = v;
   struct group *gp;
   int rv = 0;
   NYD_ENTER;

   if (*argv == NULL)
      _group_print_all(GT_ALIAS);
   else if (argv[1] == NULL) {
      if ((gp = _group_find(GT_ALIAS, *argv)) != NULL)
         _group_print(gp, stdout);
      else {
         n_err(_("No such alias: %s\n"), *argv);
         rv = 1;
      }
   } else {
      struct grp_names_head *gnhp;

      gp = _group_fetch(GT_ALIAS, *argv, 0);
      GP_TO_SUBCLASS(gnhp, gp);

      for (++argv; *argv != NULL; ++argv) {
         size_t l = strlen(*argv) +1;
         struct grp_names *gnp = smalloc(n_VSTRUCT_SIZEOF(struct grp_names,
               gn_id) + l);
         gnp->gn_next = gnhp->gnh_head;
         gnhp->gnh_head = gnp;
         memcpy(gnp->gn_id, *argv, l);
      }
      assert(gnhp->gnh_head != NULL);
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
   char **argv = v;
   int rv = 0;
   NYD_ENTER;

   if (*argv == NULL)
      _group_print_all(GT_SHORTCUT);
   else for (; *argv != NULL; argv += 2) {
      /* Because one hardly ever redefines, anything is stored in one chunk */
      size_t l;
      struct group *gp;
      char *cp;

      if (argv[1] == NULL) {
         n_err(_("Shortcut expansion is missing: %s\n"), *argv);
         rv = 1;
         break;
      }
      if (_group_find(GT_SHORTCUT, *argv) != NULL)
         _group_del(GT_SHORTCUT, *argv);

      l = strlen(argv[1]) +1;
      gp = _group_fetch(GT_SHORTCUT, *argv, l);
      GP_TO_SUBCLASS(cp, gp);
      memcpy(cp, argv[1], l);
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
shortcut_expand(char const *str)
{
   struct group *gp;
   NYD_ENTER;

   if ((gp = _group_find(GT_SHORTCUT, str)) != NULL)
      GP_TO_SUBCLASS(str, gp);
   else
      str = NULL;
   NYD_LEAVE;
   return str;
}

/* s-it-mode */
