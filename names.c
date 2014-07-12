/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Handle name lists, alias expansion; outof(): serve file / pipe addresses.
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

#include <fcntl.h>

#ifndef O_CLOEXEC
# define _OUR_CLOEXEC
# define O_CLOEXEC         0
# define _SET_CLOEXEC(FD)  fcntl((FD), F_SETFD, FD_CLOEXEC)
#else
# define _SET_CLOEXEC(FD)
#endif

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

/* Recursively expand a group name.  Limit expansion to some fixed level.
 * Direct recursion is not expanded for convenience */
static struct name * _gexpand(size_t level, struct name *nlist,
                        struct grouphead *gh, bool_t metoo, int ntype);

static void          _remove_grouplist(struct grouphead *gh);

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
_gexpand(size_t level, struct name *nlist, struct grouphead *gh, bool_t metoo,
   int ntype)
{
   struct group *gp;
   struct grouphead *ngh;
   struct name *np;
   char *cp;
   NYD_ENTER;

   if (UICMP(z, level++, >, MAXEXP)) {
      printf(_("Expanding alias to depth larger than %d\n"), MAXEXP);
      goto jleave;
   }

   for (gp = gh->g_list; gp != NULL; gp = gp->ge_link) {
      cp = gp->ge_name;
      if (*cp == '\\')
         goto jquote;
      if (!strcmp(cp, gh->g_name))
         goto jquote;
      if ((ngh = findgroup(cp)) != NULL) {
         /* For S-nail(1), the "group" may *be* the sender in that a name maps
          * to a full address specification */
         if (!metoo && ngh->g_list->ge_link == NULL && _same_name(cp, myname))
            continue;
         nlist = _gexpand(level, nlist, ngh, metoo, ntype);
         continue;
      }
jquote:
      np = nalloc(cp, ntype | GFULL);
      /* At this point should allow to expand itself if only person in group */
      if (gp == gh->g_list && gp->ge_link == NULL)
         goto jskip;
      if (!metoo && _same_name(cp, myname))
         np->n_type |= GDEL;
jskip:
      nlist = put(nlist, np);
   }
jleave:
   NYD_LEAVE;
   return nlist;
}

static void
_remove_grouplist(struct grouphead *gh)
{
   struct group *gp, *gq;
   NYD_ENTER;

   if ((gp = gh->g_list) != NULL) {
      for (; gp; gp = gq) {
         gq = gp->ge_link;
         free(gp->ge_name);
         free(gp);
      }
   }
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
   struct grouphead *gh;
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
      gh = findgroup(np->n_name);
      cp = np->n_flink;
      if (gh != NULL)
         new = _gexpand(0, new, gh, metoo, np->n_type);
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
   if (altnames)
      for (ap = altnames; *ap != '\0'; ++ap)
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
   if (altnames)
      for (ap = altnames; *ap != NULL; ++ap)
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

FL struct name *
outof(struct name *names, FILE *fo, bool_t *senderror)
{
   ui32_t pipecnt, xcnt, i;
   int *fda;
   char const *sh;
   struct name *np;
   FILE *fin = NULL, *fout;
   NYD_ENTER;

   /* Look through all recipients and do a quick return if no file or pipe
    * addressee is found */
   fda = NULL; /* Silence cc */
   for (pipecnt = xcnt = 0, np = names; np != NULL; np = np->n_flink)
      switch (np->n_flags & NAME_ADDRSPEC_ISFILEORPIPE) {
      case NAME_ADDRSPEC_ISFILE:
         ++xcnt;
         break;
      case NAME_ADDRSPEC_ISPIPE:
         ++pipecnt;
         break;
      }
   if (pipecnt == 0 && xcnt == 0)
      goto jleave;

   /* Otherwise create an array of file descriptors for each found pipe
    * addressee to get around the dup(2)-shared-file-offset problem, i.e.,
    * each pipe subprocess needs its very own file descriptor, and we need
    * to deal with that.
    * To make our life a bit easier let's just use the auto-reclaimed
    * string storage */
   if (pipecnt == 0) {
      fda = NULL;
      sh = NULL;
   } else {
      fda = salloc(sizeof(int) * pipecnt);
      for (i = 0; i < pipecnt; ++i)
         fda[i] = -1;
      if ((sh = ok_vlook(SHELL)) == NULL)
         sh = XSHELL;
   }

   for (np = names; np != NULL;) {
      if (!(np->n_flags & NAME_ADDRSPEC_ISFILEORPIPE)) {
         np = np->n_flink;
         continue;
      }

      /* See if we have copied the complete message out yet.  If not, do so */
      if (image < 0) {
         int c;
         char *tempEdit;

         if ((fout = Ftmp(&tempEdit, "outof",
               OF_WRONLY | OF_HOLDSIGS | OF_REGISTER, 0600)) == NULL) {
            perror(_("Creation of temporary image"));
            *senderror = TRU1;
            goto jcant;
         }
         if ((image = open(tempEdit, O_RDWR | O_CLOEXEC)) >= 0) {
            _SET_CLOEXEC(image);
            for (i = 0; i < pipecnt; ++i) {
               int fd = open(tempEdit, O_RDONLY | O_CLOEXEC);
               if (fd < 0) {
                  close(image);
                  image = -1;
                  pipecnt = i;
                  break;
               }
               fda[i] = fd;
               _SET_CLOEXEC(fd);
            }
         }
         Ftmp_release(&tempEdit);

         if (image < 0) {
            perror(_("Creating descriptor duplicate of temporary image"));
            *senderror = TRU1;
            Fclose(fout);
            goto jcant;
         }

         fprintf(fout, "From %s %s", myname, time_current.tc_ctime);
         c = EOF;
         while (i = c, (c = getc(fo)) != EOF)
            putc(c, fout);
         rewind(fo);
         if ((int)i != '\n')
            putc('\n', fout);
         putc('\n', fout);
         fflush(fout);
         if (ferror(fout)) {
            perror(_("Finalizing write of temporary image"));
            Fclose(fout);
            goto jcantfout;
         }
         Fclose(fout);

         /* If we have to serve file addressees, open reader */
         if (xcnt != 0 && (fin = Fdopen(image, "r")) == NULL) {
            perror(_(
               "Failed to open a duplicate of the temporary image"));
jcantfout:
            *senderror = TRU1;
            close(image);
            image = -1;
            goto jcant;
         }

         /* From now on use xcnt as a counter for pipecnt */
         xcnt = 0;
      }

      /* Now either copy "image" to the desired file or give it as the standard
       * input to the desired program as appropriate */
      if (np->n_flags & NAME_ADDRSPEC_ISPIPE) {
         int pid;
         sigset_t nset;

         sigemptyset(&nset);
         sigaddset(&nset, SIGHUP);
         sigaddset(&nset, SIGINT);
         sigaddset(&nset, SIGQUIT);
         pid = start_command(sh, &nset, fda[xcnt++], -1, "-c",
               np->n_name + 1, NULL, NULL);
         if (pid < 0) {
            fprintf(stderr, _("Message piping to <%s> failed\n"),
               np->n_name);
            *senderror = TRU1;
            goto jcant;
         }
         free_child(pid);
      } else {
         char c, *fname = file_expand(np->n_name);
         if (fname == NULL) {
            *senderror = TRU1;
            goto jcant;
         }

         if ((fout = Zopen(fname, "a", NULL)) == NULL) {
            fprintf(stderr, _("Message writing to <%s> failed: %s\n"),
               fname, strerror(errno));
            *senderror = TRU1;
            goto jcant;
         }
         rewind(fin);
         while ((c = getc(fin)) != EOF)
            putc(c, fout);
         if (ferror(fout)) {
            fprintf(stderr, _("Message writing to <%s> failed: %s\n"),
               fname, _("write error"));
            *senderror = TRU1;
         }
         Fclose(fout);
      }
jcant:
      /* In days of old we removed the entry from the the list; now for sake of
       * header expansion we leave it in and mark it as deleted */
      np->n_type |= GDEL;
      np = np->n_flink;
      if (image < 0)
         goto jdelall;
   }
jleave:
   if (fin != NULL)
      Fclose(fin);
   for (i = 0; i < pipecnt; ++i)
      close(fda[i]);
   if (image >= 0) {
      close(image);
      image = -1;
   }
   NYD_LEAVE;
   return names;

jdelall:
   while (np != NULL) {
      if (np->n_flags & NAME_ADDRSPEC_ISFILEORPIPE)
         np->n_type |= GDEL;
      np = np->n_flink;
   }
   goto jleave;
}

FL struct grouphead *
findgroup(char *name)
{
   struct grouphead *gh;
   NYD_ENTER;

   for (gh = groups[hash(name)]; gh != NULL; gh = gh->g_link)
      if (*gh->g_name == *name && !strcmp(gh->g_name, name))
         break;
   NYD_LEAVE;
   return gh;
}

FL void
printgroup(char *name)
{
   struct grouphead *gh;
   struct group *gp;
   NYD_ENTER;

   if ((gh = findgroup(name)) == NULL) {
      fprintf(stderr, _("\"%s\": no such alias\n"), name);
      goto jleave;
   }

   printf("%s\t", gh->g_name);
   for (gp = gh->g_list; gp != NULL; gp = gp->ge_link)
      printf(" %s", gp->ge_name);
   putchar('\n');
jleave:
   NYD_LEAVE;
}

FL void
remove_group(char const *name)
{
   ui32_t h;
   struct grouphead *gh, *gp;
   NYD_ENTER;

   h = hash(name);

   for (gp = NULL, gh = groups[h]; gh != NULL; gh = gh->g_link) {
      if (*gh->g_name == *name && !strcmp(gh->g_name, name)) {
         _remove_grouplist(gh);
         free(gh->g_name);
         if (gp != NULL)
            gp->g_link = gh->g_link;
         else
            groups[h] = NULL;
         free(gh);
         break;
      }
      gp = gh;
   }
   NYD_LEAVE;
}

#ifdef _OUR_CLOEXEC
# undef O_CLOEXEC
# undef _OUR_CLOEXEC
#endif
#undef _SET_CLOEXEC

/* s-it-mode */
