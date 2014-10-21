/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Handle name lists, alias expansion, group handling.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 * The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

struct group {
   struct group   *g_next;
   size_t         g_subclass_off;   /* of "subclass" in .g_id */
   /* Identifying name, of variable size.  Dependent on actual "subtype" more
    * data follows thereafter */
   char           g_id[VFIELD_SIZE(8)];
};
#define GP_TO_SUBCLASS(X,G) \
do {\
   union __group_subclass {void *gs_vp; char *gs_cp;} __gs__;\
   __gs__.gs_cp = (char*)UNCONST(G) + (G)->g_subclass_off;\
   (X) = __gs__.gs_vp;\
} while (0)

struct grp_names_head {
   struct grp_names  *gnh_head;
};

struct grp_names {
   struct grp_names  *gn_next;
   char              gn_id[VFIELD_SIZE(8)];
};

struct group_lookup {
   struct group   **gl_slot;
   struct group   *gl_slot_last;
   struct group   *gl_group;
};

/* `alias' */
static struct group  *_alias_heads[HSHSIZE]; /* TODO dynamic hash */

/* List of alternate names of user */
static char          **_altnames;

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

/* Locate a group fast, return it or NULL */
static struct group * _group_find(struct group * const *gpa, char const *name);

/* Lookup a group, return it or NULL, fill in glp anyway */
static struct group * _group_lookup(struct group_lookup *glp,
                        struct group **gpa, char const *name);

/* Iteration: go to the first group, which also inits the iterator.  A valid
 * iterator can be stepped via _next().  A NULL return means no (more) groups
 * to be iterated exist, in which case only glp->gl_group is set (NULL) */
static struct group * _group_go_first(struct group_lookup *glp,
                        struct group **gpa);
static struct group * _group_go_next(struct group_lookup *glp,
                        struct group **gpa);

/* Fetch the group id, create it as necessary, reserving add_size additional
 * bytes for possible subclass space */
static struct group * _group_fetch(struct group **gpa, ui32_t add_size,
                        char const *id);

/* Delete a group superclass */
static void          _group_del(struct group_lookup *glp);

/* "Intelligent" delete which handles a "*" id and knows how to delete kids;
 * returns a true boolean if a group was deleted, and always succeeds for "*" */
static bool_t        _group_dispatch_del(struct group **gpa, char const *id);

static void          __names_del(struct group *gp);

/* Print all groups in gpa, alphasorted */
static void          _group_print_all(struct group * const *gpa);

static int           __group_print_qsorter(void const *a, void const *b);

/* Print group "intelligently" */
static void          _group_dispatch_print(struct group const *gp);

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
      if (c == '\\') {
         lastsp = 0;
         continue;
      }
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
   nbuf = ac_alloc(strlen(line) +1);
   while ((cp = yankname(cp, nbuf, separators, keepcomms)) != NULL) {
      t = nalloc(nbuf, ntype);
      if (topp == NULL)
         topp = t;
      else
         np->n_flink = t;
      t->n_blink = np;
      np = t;
   }
   ac_free(nbuf);
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
      printf(_("Expanding alias to depth larger than %d\n"), MAXEXP);
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
_group_find(struct group * const *gpa, char const *name)
{
   struct group *gp;
   NYD_ENTER;

   for (gp = gpa[hash(name)]; gp != NULL; gp = gp->g_next)
      if (*gp->g_id == *name && !strcmp(gp->g_id, name))
         break;
   NYD_LEAVE;
   return gp;
}

static struct group *
_group_lookup(struct group_lookup *glp, struct group **gpa, char const *name)
{
   struct group *lgp, *gp;
   NYD_ENTER;

   lgp = NULL;
   gp = *(glp->gl_slot = (gpa + torek_hash(name) % HSHSIZE));

   while (gp != NULL) {
      if (*gp->g_id == *name && !strcmp(gp->g_id, name))
         break;
      lgp = gp;
      gp = gp->g_next;
   }
   glp->gl_slot_last = lgp;
   glp->gl_group = gp;
   NYD_LEAVE;
   return gp;
}

static struct group *
_group_go_first(struct group_lookup *glp, struct group **gpa)
{
   struct group *gp;
   size_t i;
   NYD_ENTER;

   for (i = 0; i < HSHSIZE; ++gpa, ++i)
      if ((gp = *gpa) != NULL) {
         glp->gl_slot = gpa;
         glp->gl_slot_last = NULL;
         glp->gl_group = gp;
         goto jleave;
      }
   glp->gl_group = gp = NULL;
jleave:
   NYD_LEAVE;
   return gp;
}

static struct group *
_group_go_next(struct group_lookup *glp, struct group **gpa)
{
   struct group *gp;
   NYD_ENTER;

   if ((gp = glp->gl_group->g_next) != NULL)
      glp->gl_group = gp;
   else {
      for (gpa += HSHSIZE; glp->gl_slot < gpa; ++glp->gl_slot)
         if ((gp = *glp->gl_slot) != NULL)
            break;
      glp->gl_group = gp;
   }
   NYD_LEAVE;
   return gp;
}

static struct group *
_group_fetch(struct group **gpa, ui32_t add_size, char const *id)
{
   struct group_lookup gl;
   struct group *gp;
   size_t l, i;
   NYD_ENTER;

   if ((gp = _group_lookup(&gl, gpa, id)) != NULL)
      goto jleave;

   l = strlen(id) +1;
   i = ALIGN(sizeof(*gp) - VFIELD_SIZEOF(struct group, g_id) + l);
   gp = smalloc(i + add_size);
   gp->g_next = *gl.gl_slot;
   *gl.gl_slot = gp;
   gp->g_subclass_off = i;
   memcpy(gp->g_id, id, l);
   if (add_size > 0)
      memset(gp->g_id + i, 0, add_size);
jleave:
   NYD_LEAVE;
   return gp;
}

static void
_group_del(struct group_lookup *glp)
{
   struct group *gp, *x;
   NYD_ENTER;

   gp = glp->gl_group;

   if ((x = glp->gl_slot_last) != NULL)
      x->g_next = gp->g_next;
   else
      *glp->gl_slot = gp->g_next;
   free(gp);
   NYD_LEAVE;
}

static bool_t
_group_dispatch_del(struct group **gpa, char const *id)
{
   struct group_lookup gl;
   struct group *gp;
   NYD_ENTER;

   if (id[0] == '*' && id[1] == '\0') {
      for (gp = _group_go_first(&gl, gpa); gp != NULL;
            gp = _group_go_next(&gl, gpa)) {
         __names_del(gp);
         _group_del(&gl);
      }
      gp = (struct group*)TRU1;
   } else if ((gp = _group_lookup(&gl, gpa, id)) != NULL) {
      __names_del(gp);
      _group_del(&gl);
   }
   NYD_LEAVE;
   return (gp != NULL);
}

static void
__names_del(struct group *gp)
{
   struct grp_names_head *gnhp;
   struct grp_names *gnp, *x;
   NYD_ENTER;

   GP_TO_SUBCLASS(gnhp, gp);

   for (gnp = gnhp->gnh_head; gnp != NULL;) {
      x = gnp;
      gnp = gnp->gn_next;
      free(x);
   }
   NYD_LEAVE;
}

static void
_group_print_all(struct group * const *gpa)
{
   char const **ida;
   struct group const *gp;
   ui32_t h, i;
   NYD_ENTER;

   for (h = 0, i = 1; h < HSHSIZE; ++h)
      for (gp = gpa[h]; gp != NULL; gp = gp->g_next)
         ++i;
   ida = salloc(i * sizeof *ida);

   for (i = h = 0; h < HSHSIZE; ++h)
      for (gp = gpa[h]; gp != NULL; gp = gp->g_next)
         ida[i++] = gp->g_id;
   ida[i] = NULL;

   if (i > 1)
      qsort(ida, i, sizeof *ida, &__group_print_qsorter);

   for (i = 0; ida[i] != NULL; ++i)
      _group_dispatch_print(_group_find(gpa, ida[i]));
   NYD_LEAVE;
}

static int
__group_print_qsorter(void const *a, void const *b)
{
   int rv;
   NYD_ENTER;

   rv = strcmp(*(char**)UNCONST(a), *(char**)UNCONST(b));
   NYD_LEAVE;
   return rv;
}

static void
_group_dispatch_print(struct group const *gp)
{
   struct grp_names_head *gnhp;
   struct grp_names *gnp;
   NYD_ENTER;

   printf("%s", gp->g_id);

   GP_TO_SUBCLASS(gnhp, gp);

   if ((gnp = gnhp->gnh_head) != NULL) {
      putc('\t', stdout);
      do {
         struct grp_names *x = gnp;
         gnp = gnp->gn_next;
         printf("%s%s", x->gn_id, (gnp != NULL ? " " : ""));
      } while (gnp != NULL);
   }

   putc('\n', stdout);
   NYD_LEAVE;
}

FL struct name *
nalloc(char *str, enum gfield ntype)
{
   struct addrguts ag;
   struct str in, out;
   struct name *np;
   NYD_ENTER;

   np = salloc(sizeof *np);
   np->n_flink = NULL;
   np->n_blink = NULL;
   np->n_type = ntype;
   np->n_flags = 0;

   addrspec_with_guts(((ntype & (GFULL | GSKIN | GREF)) != 0), str, &ag);
   if (!(ag.ag_n_flags & NAME_NAME_SALLOC)) {
      ag.ag_n_flags |= NAME_NAME_SALLOC;
      ag.ag_skinned = savestrbuf(ag.ag_skinned, ag.ag_slen);
   }
   np->n_fullname = np->n_name = ag.ag_skinned;
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
#ifdef HAVE_IDNA
      if (!(ag.ag_n_flags & NAME_IDNA)) {
#endif
         in.s = str;
         in.l = ag.ag_ilen;
#ifdef HAVE_IDNA
      } else {
         /* The domain name was IDNA and has been converted.  We also have to
          * ensure that the domain name in .n_fullname is replaced with the
          * converted version, since MIME doesn't perform encoding of addrs */
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
   } else if (ntype & GREF) { /* TODO LEGACY */
      /* TODO Unfortunately we had to skin GREFerences i.e. the
       * TODO surrounding angle brackets have been stripped away.
       * TODO Necessarily since otherwise the plain address check
       * TODO fails due to them; insert them back so that valid
       * TODO headers will be created */
      np->n_fullname = np->n_name = str = salloc(ag.ag_slen + 2 +1);
      *(str++) = '<';
      memcpy(str, ag.ag_skinned, ag.ag_slen);
      str += ag.ag_slen;
      *(str++) = '>';
      *str = '\0';
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
   if (np->n_name == np->n_fullname || !(ntype & (GFULL | GSKIN)))
      nnp->n_fullname = nnp->n_name;
   else {
      nnp->n_flags |= NAME_FULLNAME_SALLOC;
      nnp->n_fullname = savestr(np->n_fullname);
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
   int comma, s;
   NYD_ENTER;

   topp = NULL;
   if (np == NULL)
      goto jleave;

   comma = ntype & GCOMMA;
   ntype &= ~GCOMMA;
   s = 0;
   if ((options & OPT_DEBUG) && comma)
      fprintf(stderr, _("detract asked to insert commas\n"));
   for (p = np; p != NULL; p = p->n_flink) {
      if (ntype && (p->n_type & GMASK) != ntype)
         continue;
      s += strlen(p->n_fullname) +1;
      if (comma)
         s++;
   }
   if (s == 0)
      goto jleave;

   s += 2;
   topp = salloc(s);
   cp = topp;
   for (p = np; p != NULL; p = p->n_flink) {
      if (ntype && (p->n_type & GMASK) != ntype)
         continue;
      cp = sstpcpy(cp, p->n_fullname);
      if (comma && p->n_flink != NULL)
         *cp++ = ',';
      *cp++ = ' ';
   }
   *--cp = 0;
   if (comma && *--cp == ',')
      *cp = 0;
jleave:
   NYD_LEAVE;
   return topp;
}

FL struct name *
grab_names(char const *field, struct name *np, int comma, enum gfield gflags)
{
   struct name *nq;
   NYD_ENTER;

jloop:
   np = lextract(readstr_input(field, detract(np, comma)), gflags);
   for (nq = np; nq != NULL; nq = nq->n_flink)
      if (is_addr_invalid(nq, 1))
         goto jloop;
   NYD_LEAVE;
   return np;
}

FL struct name *
checkaddrs(struct name *np)
{
   struct name *n;
   NYD_ENTER;

   for (n = np; n != NULL;) {
      if (is_addr_invalid(n, 1)) {
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
      gp = _group_find(_alias_heads, np->n_name);
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
   NYD_ENTER;

   if (*argv == NULL) {
      _group_print_all(_alias_heads);
      gp = (struct group*)TRU1;
   } else if (argv[1] == NULL) {
      if ((gp = _group_find(_alias_heads, *argv)) != NULL)
      else
         fprintf(stderr, _("No such alias: `%s'\n"), *argv);
   } else {
      struct grp_names_head *gnhp;

      gp = _group_fetch(_alias_heads, sizeof(struct grp_names_head), *argv);
      GP_TO_SUBCLASS(gnhp, gp);

      for (++argv; *argv != NULL; ++argv) {
         size_t l = strlen(*argv) +1;
         struct grp_names *gnp = smalloc(sizeof(*gnp) -
               VFIELD_SIZEOF(struct grp_names, gn_id) + l);
         gnp->gn_next = gnhp->gnh_head;
         gnhp->gnh_head = gnp;
         memcpy(gnp->gn_id, *argv, l);
      }
   }
   NYD_LEAVE;
   return !(gp != NULL);
}

FL int
c_unalias(void *v)
{
   char **argv = v;
   NYD_ENTER;

   if (*argv != NULL) {
      bool_t errors = FAL0;

      do if (!_group_del(_alias_heads, *argv)) {
         errors = TRU1;
         fprintf(stderr, _("No such alias: `%s'\n"), *argv);
      } while (*++argv != NULL);
      if (errors)
         argv = NULL;
   } else
      fprintf(stderr, _("Must specify an alias to remove\n"));
   NYD_LEAVE;
   return !(argv != NULL);
}

FL int
c_alternates(void *v)
{
   size_t l;
   char **namelist = v, **ap, **ap2, *cp;
   NYD_ENTER;

   l = argcount(namelist) + 1;
   if (l == 1) {
      if (_altnames == NULL)
         goto jleave;
      for (ap = _altnames; *ap != NULL; ++ap)
         printf("%s ", *ap);
      printf("\n");
      goto jleave;
   }

   if (_altnames != NULL) {
      for (ap = _altnames; *ap != NULL; ++ap)
         free(*ap);
      free(_altnames);
   }

   _altnames = smalloc(l * sizeof *_altnames);
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

/* s-it-mode */
