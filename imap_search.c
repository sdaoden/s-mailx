/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Client-side implementation of the IMAP SEARCH command. This is used
 *@ for folders not located on IMAP servers, or for IMAP servers that do
 *@ not implement the SEARCH command.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 2004
 * Gunnar Ritter.  All rights reserved.
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
 *    This product includes software developed by Gunnar Ritter
 *    and his contributors.
 * 4. Neither the name of Gunnar Ritter nor the names of his contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GUNNAR RITTER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL GUNNAR RITTER OR CONTRIBUTORS BE LIABLE
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

#ifdef HAVE_REGEX
# include <regex.h>
#endif

enum itoken {
   ITBAD, ITEOD, ITBOL, ITEOL, ITAND, ITSET, ITALL, ITANSWERED,
   ITBCC, ITBEFORE, ITBODY,
   ITCC,
   ITDELETED, ITDRAFT,
   ITFLAGGED, ITFROM,
   ITHEADER,
   ITKEYWORD,
   ITLARGER,
   ITNEW, ITNOT,
   ITOLD, ITON, ITOR,
   ITRECENT,
   ITSEEN, ITSENTBEFORE, ITSENTON, ITSENTSINCE, ITSINCE, ITSMALLER,
      ITSUBJECT,
   ITTEXT, ITTO,
   ITUID, ITUNANSWERED, ITUNDELETED, ITUNDRAFT, ITUNFLAGGED, ITUNKEYWORD,
      ITUNSEEN
};

struct itlex {
   char const     *s_string;
   enum itoken    s_token;
};

#ifdef HAVE_REGEX
struct itregex {
   struct itregex *re_next;
   regex_t        re_regex;
};
#endif

struct itnode {
   enum itoken    n_token;
   unsigned long  n_n;
   void           *n_v;
   void           *n_w;
   struct itnode  *n_x;
   struct itnode  *n_y;
};

static struct itlex const  _it_strings[] = {
   { "ALL",          ITALL },
   { "ANSWERED",     ITANSWERED },
   { "BCC",          ITBCC },
   { "BEFORE",       ITBEFORE },
   { "BODY",         ITBODY },
   { "CC",           ITCC },
   { "DELETED",      ITDELETED },
   { "DRAFT",        ITDRAFT },
   { "FLAGGED",      ITFLAGGED },
   { "FROM",         ITFROM },
   { "HEADER",       ITHEADER },
   { "KEYWORD",      ITKEYWORD },
   { "LARGER",       ITLARGER },
   { "NEW",          ITNEW },
   { "NOT",          ITNOT },
   { "OLD",          ITOLD },
   { "ON",           ITON },
   { "OR",           ITOR },
   { "RECENT",       ITRECENT },
   { "SEEN",         ITSEEN },
   { "SENTBEFORE",   ITSENTBEFORE },
   { "SENTON",       ITSENTON },
   { "SENTSINCE",    ITSENTSINCE },
   { "SINCE",        ITSINCE },
   { "SMALLER",      ITSMALLER },
   { "SUBJECT",      ITSUBJECT },
   { "TEXT",         ITTEXT },
   { "TO",           ITTO },
   { "UID",          ITUID },
   { "UNANSWERED",   ITUNANSWERED },
   { "UNDELETED",    ITUNDELETED },
   { "UNDRAFT",      ITUNDRAFT },
   { "UNFLAGGED",    ITUNFLAGGED },
   { "UNKEYWORD",    ITUNKEYWORD },
   { "UNSEEN",       ITUNSEEN },
   { NULL,           ITBAD }
};

static struct itnode    *_it_tree;
#ifdef HAVE_REGEX
static struct itregex   *_it_regex;
#endif
static char             *_it_begin;
static enum itoken      _it_token;
static unsigned long    _it_number;
static void             *_it_args[2];
static size_t           _it_need_headers;
static bool_t           _it_need_regex;

static enum okay     itparse(char const *spec, char const **xp, int sub);
static enum okay     itscan(char const *spec, char const **xp);
static enum okay     itsplit(char const *spec, char const **xp);
static enum okay     itstring(void **tp, char const *spec, char const **xp);
static int           itexecute(struct mailbox *mp, struct message *m,
                        size_t c, struct itnode *n);
static int           matchfield(struct message *m, char const *field,
                        const void *what);
static int           matchenvelope(struct message *m, char const *field,
                        const void *what);
static char *        mkenvelope(struct name *np);
static int           matchmsg(struct message *m, const void *what,
                        int withheader);
static char const *  around(char const *cp);

static enum okay
itparse(char const *spec, char const **xp, int sub)
{
   int level = 0;
   struct itnode n, *z, *ittree;
   enum okay rv;
   NYD_ENTER;

   _it_tree = NULL;
   while ((rv = itscan(spec, xp)) == OKAY && _it_token != ITBAD &&
         _it_token != ITEOD) {
      ittree = _it_tree;
      memset(&n, 0, sizeof n);
      spec = *xp;
      switch (_it_token) {
      case ITBOL:
         level++;
         continue;
      case ITEOL:
         if (--level == 0)
            goto jleave;
         if (level < 0) {
            if (sub > 0) {
               (*xp)--;
               goto jleave;
            }
            fprintf(stderr, "Excess in \")\".\n");
            rv = STOP;
            goto jleave;
         }
         continue;
      case ITNOT:
         /* <search-key> */
         n.n_token = ITNOT;
         if ((rv = itparse(spec, xp, sub + 1)) == STOP)
            goto jleave;
         spec = *xp;
         if ((n.n_x = _it_tree) == NULL) {
            fprintf(stderr, "Criterion for NOT missing: >>> %s <<<\n",
               around(*xp));
            rv = STOP;
            goto jleave;
         }
         _it_token = ITNOT;
         break;
      case ITOR:
         /* <search-key1> <search-key2> */
         n.n_token = ITOR;
         if ((rv = itparse(spec, xp, sub + 1)) == STOP)
            goto jleave;
         if ((n.n_x = _it_tree) == NULL) {
            fprintf(stderr, "First criterion for OR missing: >>> %s <<<\n",
               around(*xp));
            rv = STOP;
            goto jleave;
         }
         spec = *xp;
         if ((rv = itparse(spec, xp, sub + 1)) == STOP)
            goto jleave;
         spec = *xp;
         if ((n.n_y = _it_tree) == NULL) {
            fprintf(stderr, "Second criterion for OR missing: >>> %s <<<\n",
               around(*xp));
            rv = STOP;
            goto jleave;
         }
         break;
      default:
         n.n_token = _it_token;
         n.n_n = _it_number;
         n.n_v = _it_args[0];
         n.n_w = _it_args[1];
      }

      _it_tree = ittree;
      if (_it_tree == NULL) {
         _it_tree = salloc(sizeof *_it_tree);
         *_it_tree = n;
      } else {
         z = _it_tree;
         _it_tree = salloc(sizeof *_it_tree);
         _it_tree->n_token = ITAND;
         _it_tree->n_x = z;
         _it_tree->n_y = salloc(sizeof *_it_tree->n_y);
         *_it_tree->n_y = n;
      }
      if (sub && level == 0)
         break;
   }
jleave:
   NYD_LEAVE;
   return rv;
}

static enum okay
itscan(char const *spec, char const **xp)
{
   int i, n;
   enum okay rv = OKAY;
   NYD_ENTER;

   while (spacechar(*spec))
      ++spec;
   if (*spec == '(') {
      *xp = &spec[1];
      _it_token = ITBOL;
      goto jleave;
   }
   if (*spec == ')') {
      *xp = &spec[1];
      _it_token = ITEOL;
      goto jleave;
   }
   while (spacechar(*spec))
      ++spec;
   if (*spec == '\0') {
      _it_token = ITEOD;
      goto jleave;
   }

   for (i = 0; _it_strings[i].s_string; i++) {
      n = strlen(_it_strings[i].s_string);
      if (ascncasecmp(spec, _it_strings[i].s_string, n) == 0 &&
            (spacechar(spec[n]) || spec[n] == '\0' ||
             spec[n] == '(' || spec[n] == ')')) {
         _it_token = _it_strings[i].s_token;
         spec += n;
         while (spacechar(*spec))
            ++spec;
         rv = itsplit(spec, xp);
         goto jleave;
      }
   }
   if (digitchar(*spec)) {
      _it_number = strtoul(spec, UNCONST(xp), 10);
      if (spacechar(**xp) || **xp == '\0' || **xp == '(' || **xp == ')') {
         _it_token = ITSET;
         goto jleave;
      }
   }
   fprintf(stderr, "Bad SEARCH criterion \"");
   while (*spec && !spacechar(*spec) && *spec != '(' && *spec != ')') {
      putc(*spec & 0377, stderr);
      ++spec;
   }
   fprintf(stderr, "\": >>> %s <<<\n", around(*xp));
   _it_token = ITBAD;
   rv = STOP;
jleave:
   NYD_LEAVE;
   return rv;
}

static enum okay
itsplit(char const *spec, char const **xp)
{
   char *cp;
   time_t t;
   enum okay rv;
   NYD_ENTER;

   switch (_it_token) {
   case ITBCC:
   case ITBODY:
   case ITCC:
   case ITFROM:
   case ITSUBJECT:
   case ITTEXT:
   case ITTO:
      /* <string> */
      _it_need_headers++;
      rv = itstring(&_it_args[0], spec, xp);
#ifdef HAVE_REGEX
      if (rv == OKAY && _it_need_regex) {
         _it_number = 0;
         goto jregcomp;
      }
#endif
      break;
   case ITSENTBEFORE:
   case ITSENTON:
   case ITSENTSINCE:
      _it_need_headers++;
      /*FALLTHRU*/
   case ITBEFORE:
   case ITON:
   case ITSINCE:
      /* <date> */
      if ((rv = itstring(&_it_args[0], spec, xp)) != OKAY)
         break;
      if ((t = imap_read_date(_it_args[0])) == (time_t)-1) {
         fprintf(stderr, "Invalid date \"%s\": >>> %s <<<\n",
               (char*)_it_args[0], around(*xp));
         rv = STOP;
         break;
      }
      _it_number = t;
      rv = OKAY;
      break;
   case ITHEADER:
      /* <field-name> <string> */
      _it_need_headers++;
      if ((rv = itstring(&_it_args[0], spec, xp)) != OKAY)
         break;
      spec = *xp;
      if ((rv = itstring(&_it_args[1], spec, xp)) != OKAY)
         break;
#ifdef HAVE_REGEX
      _it_number = 1;
jregcomp:
      if (_it_need_regex) {
         struct itregex *itre = salloc(sizeof *_it_regex);
         itre->re_next = _it_regex;
         _it_regex = itre;

         cp = _it_args[_it_number];
         _it_args[_it_number] = &itre->re_regex;
         if (regcomp(&itre->re_regex, cp, REG_EXTENDED | REG_ICASE | REG_NOSUB)
               != 0) {
            fprintf(stderr, tr(526,
               "Invalid regular expression \"%s\": >>> %s <<<\n"),
               cp, around(*xp));
            rv = STOP;
            break;
         }
      }
#endif
      break;
   case ITKEYWORD:
   case ITUNKEYWORD:
      /* <flag> */
      if ((rv = itstring(&_it_args[0], spec, xp)) != OKAY)
         break;
      if (asccasecmp(_it_args[0], "\\Seen") == 0)
         _it_number = MREAD;
      else if (asccasecmp(_it_args[0], "\\Deleted") == 0)
         _it_number = MDELETED;
      else if (asccasecmp(_it_args[0], "\\Recent") == 0)
         _it_number = MNEW;
      else if (asccasecmp(_it_args[0], "\\Flagged") == 0)
         _it_number = MFLAGGED;
      else if (asccasecmp(_it_args[0], "\\Answered") == 0)
         _it_number = MANSWERED;
      else if (asccasecmp(_it_args[0], "\\Draft") == 0)
         _it_number = MDRAFT;
      else
         _it_number = 0;
      break;
   case ITLARGER:
   case ITSMALLER:
      /* <n> */
      if ((rv = itstring(&_it_args[0], spec, xp)) != OKAY)
         break;
      _it_number = strtoul(_it_args[0], &cp, 10);
      if (spacechar(*cp) || *cp == '\0')
         break;
      fprintf(stderr, "Invalid size: >>> %s <<<\n", around(*xp));
      rv = STOP;
      break;
   case ITUID:
      /* <message set> */
      fprintf(stderr,
         "Searching for UIDs is not supported: >>> %s <<<\n", around(*xp));
      rv = STOP;
      break;
   default:
      *xp = spec;
      rv = OKAY;
      break;
   }
   NYD_LEAVE;
   return rv;
}

static enum okay
itstring(void **tp, char const *spec, char const **xp)
{
   int inquote = 0;
   char *ap;
   enum okay rv = STOP;
   NYD_ENTER;

   while (spacechar(*spec))
      spec++;
   if (*spec == '\0' || *spec == '(' || *spec == ')') {
      fprintf(stderr, "Missing string argument: >>> %s <<<\n",
         around(&(*xp)[spec - *xp]));
      goto jleave;
   }
   ap = *tp = salloc(strlen(spec) + 1);
   *xp = spec;
    do {
      if (inquote && **xp == '\\')
         *ap++ = *(*xp)++;
      else if (**xp == '"')
         inquote = !inquote;
      else if (!inquote && (spacechar(**xp) || **xp == '(' || **xp == ')')) {
         *ap++ = '\0';
         break;
      }
      *ap++ = **xp;
   } while (*(*xp)++);

   *tp = imap_unquotestr(*tp);
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

static int
itexecute(struct mailbox *mp, struct message *m, size_t c, struct itnode *n)
{
   char *cp, *line = NULL;
   size_t linesize = 0;
   FILE *ibuf;
   int rv;
   NYD_ENTER;

   if (n == NULL) {
      fprintf(stderr, "Internal error: Empty node in SEARCH tree.\n");
      rv = 0;
      goto jleave;
   }

   switch (n->n_token) {
   case ITBEFORE:
   case ITON:
   case ITSINCE:
      if (m->m_time == 0 && !(m->m_flag & MNOFROM) &&
            (ibuf = setinput(mp, m, NEED_HEADER)) != NULL) {
         if (readline_restart(ibuf, &line, &linesize, 0) > 0)
            m->m_time = unixtime(line);
         free(line);
      }
      break;
   case ITSENTBEFORE:
   case ITSENTON:
   case ITSENTSINCE:
      if (m->m_date == 0)
         if ((cp = hfield1("date", m)) != NULL)
            m->m_date = rfctime(cp);
      break;
   default:
      break;
   }

   switch (n->n_token) {
   default:
      fprintf(stderr, "Internal SEARCH error: Lost token %d\n", n->n_token);
      rv = 0;
      break;
   case ITAND:
      rv = itexecute(mp, m, c, n->n_x) & itexecute(mp, m, c, n->n_y);
      break;
   case ITSET:
      rv = UICMP(z, c, ==, n->n_n);
      break;
   case ITALL:
      rv = 1;
      break;
   case ITANSWERED:
      rv = ((m->m_flag & MANSWERED) != 0);
      break;
   case ITBCC:
      rv = matchenvelope(m, "bcc", n->n_v);
      break;
   case ITBEFORE:
      rv = UICMP(z, m->m_time, <, n->n_n);
      break;
   case ITBODY:
      rv = matchmsg(m, n->n_v, 0);
      break;
   case ITCC:
      rv = matchenvelope(m, "cc", n->n_v);
      break;
   case ITDELETED:
      rv = ((m->m_flag & MDELETED) != 0);
      break;
   case ITDRAFT:
      rv = ((m->m_flag & MDRAFTED) != 0);
      break;
   case ITFLAGGED:
      rv = ((m->m_flag & MFLAGGED) != 0);
      break;
   case ITFROM:
      rv = matchenvelope(m, "from", n->n_v);
      break;
   case ITHEADER:
      rv = matchfield(m, n->n_v, n->n_w);
      break;
   case ITKEYWORD:
      rv = ((m->m_flag & n->n_n) != 0);
      break;
   case ITLARGER:
      rv = (m->m_xsize > n->n_n);
      break;
   case ITNEW:
      rv = ((m->m_flag & (MNEW | MREAD)) == MNEW);
      break;
   case ITNOT:
      rv = !itexecute(mp, m, c, n->n_x);
      break;
   case ITOLD:
      rv = !(m->m_flag & MNEW);
      break;
   case ITON:
      rv = (UICMP(z, m->m_time, >=, n->n_n) &&
            UICMP(z, m->m_time, <, n->n_n + 86400));
      break;
   case ITOR:
      rv = itexecute(mp, m, c, n->n_x) | itexecute(mp, m, c, n->n_y);
      break;
   case ITRECENT:
      rv = ((m->m_flag & MNEW) != 0);
      break;
   case ITSEEN:
      rv = ((m->m_flag & MREAD) != 0);
      break;
   case ITSENTBEFORE:
      rv = UICMP(z, m->m_date, <, n->n_n);
      break;
   case ITSENTON:
      rv = (UICMP(z, m->m_date, >=, n->n_n) &&
            UICMP(z, m->m_date, <, n->n_n + 86400));
      break;
   case ITSENTSINCE:
      rv = UICMP(z, m->m_date, >=, n->n_n);
      break;
   case ITSINCE:
      rv = UICMP(z, m->m_time, >=, n->n_n);
      break;
   case ITSMALLER:
      rv = UICMP(z, m->m_xsize, <, n->n_n);
      break;
   case ITSUBJECT:
      rv = matchfield(m, "subject", n->n_v);
      break;
   case ITTEXT:
      rv = matchmsg(m, n->n_v, 1);
      break;
   case ITTO:
      rv = matchenvelope(m, "to", n->n_v);
      break;
   case ITUNANSWERED:
      rv = !(m->m_flag & MANSWERED);
      break;
   case ITUNDELETED:
      rv = !(m->m_flag & MDELETED);
      break;
   case ITUNDRAFT:
      rv = !(m->m_flag & MDRAFTED);
      break;
   case ITUNFLAGGED:
      rv = !(m->m_flag & MFLAGGED);
      break;
   case ITUNKEYWORD:
      rv = !(m->m_flag & n->n_n);
      break;
   case ITUNSEEN:
      rv = !(m->m_flag & MREAD);
      break;
   }
jleave:
   NYD_LEAVE;
   return rv;
}

static int
matchfield(struct message *m, char const *field, const void *what)
{
   struct str in, out;
   int i = 0;
   NYD_ENTER;

   if ((in.s = hfieldX(field, m)) == NULL)
      goto jleave;

   in.l = strlen(in.s);
   mime_fromhdr(&in, &out, TD_ICONV);

#ifdef HAVE_REGEX
   if (_it_need_regex)
      i = (regexec(what, out.s, 0,NULL, 0) != REG_NOMATCH);
   else
#endif
      i = substr(out.s, what);

   free(out.s);
jleave:
   NYD_LEAVE;
   return i;
}

static int
matchenvelope(struct message *m, char const *field, const void *what)
{
   struct name *np;
   char *cp;
   int rv = 0;
   NYD_ENTER;

   if ((cp = hfieldX(field, m)) == NULL)
      goto jleave;

   for (np = lextract(cp, GFULL); np != NULL; np = np->n_flink) {
#ifdef HAVE_REGEX
      if (_it_need_regex) {
         if (regexec(what, np->n_name, 0,NULL, 0) == REG_NOMATCH &&
               regexec(what, mkenvelope(np), 0,NULL, 0) == REG_NOMATCH)
            continue;
      } else
#endif
      if (!substr(np->n_name, what) && !substr(mkenvelope(np), what))
         continue;
      rv = 1;
      break;
   }

jleave:
   NYD_LEAVE;
   return rv;
}

static char *
mkenvelope(struct name *np)
{
   size_t epsize;
   char *ep, *realnam = NULL, *sourceaddr = NULL, *localpart = NULL,
      *domainpart = NULL, *cp, *rp, *xp, *ip;
   struct str in, out;
   int level = 0, hadphrase = 0;
   NYD_ENTER;

   in.s = np->n_fullname;
   in.l = strlen(in.s);
   mime_fromhdr(&in, &out, TD_ICONV);
   rp = ip = ac_alloc(strlen(out.s) + 1);
   for (cp = out.s; *cp; cp++) {
      switch (*cp) {
      case '"':
         while (*cp) {
            if (*++cp == '"')
               break;
            if (*cp == '\\' && cp[1])
               cp++;
            *rp++ = *cp;
         }
         break;
      case '<':
         while (cp > out.s && blankchar(cp[-1]))
            cp--;
         rp = ip;
         xp = out.s;
         if (xp < &cp[-1] && *xp == '"' && cp[-1] == '"') {
            xp++;
            cp--;
         }
         while (xp < cp)
            *rp++ = *xp++;
         hadphrase = 1;
         goto jdone;
      case '(':
         if (level++)
            goto jdfl;
         if (hadphrase++ == 0)
            rp = ip;
         break;
      case ')':
         if (--level)
            goto jdfl;
         break;
      case '\\':
         if (level && cp[1])
            cp++;
         goto jdfl;
      default:
jdfl:
         *rp++ = *cp;
      }
   }
jdone:
   *rp = '\0';
   if (hadphrase)
      realnam = ip;
   free(out.s);
   localpart = savestr(np->n_name);
   if ((cp = strrchr(localpart, '@')) != NULL) {
      *cp = '\0';
      domainpart = &cp[1];
   }

   ep = salloc(epsize = strlen(np->n_fullname) * 2 + 40);
   snprintf(ep, epsize, "(%s %s %s %s)",
         realnam ? imap_quotestr(realnam) : "NIL",
         sourceaddr ? imap_quotestr(sourceaddr) : "NIL",
         localpart ? imap_quotestr(localpart) : "NIL",
         domainpart ? imap_quotestr(domainpart) : "NIL");
   ac_free(ip);
   NYD_LEAVE;
   return ep;
}

static int
matchmsg(struct message *m, const void *what, int withheader)
{
   char *line = NULL;
   size_t linesize, linelen, cnt;
   FILE *fp;
   int yes = 0;
   NYD_ENTER;

   if ((fp = Ftmp(NULL, "imasrch", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL)
      goto j_leave;
   if (sendmp(m, fp, NULL, NULL, SEND_TOSRCH, NULL) < 0)
      goto jleave;
   fflush(fp);
   rewind(fp);

   cnt = fsize(fp);
   line = smalloc(linesize = LINESIZE);
   linelen = 0;

   if (!withheader)
      while (fgetline(&line, &linesize, &cnt, &linelen, fp, 0))
         if (*line == '\n')
            break;

   while (fgetline(&line, &linesize, &cnt, &linelen, fp, 0)) {
#ifdef HAVE_REGEX
      if (_it_need_regex) {
         if (regexec(what, line, 0,NULL, 0) == REG_NOMATCH)
            continue;
      } else
#endif
      if (!substr(line, what))
         continue;
      yes = 1;
      break;
   }

jleave:
   free(line);
   Fclose(fp);
j_leave:
   NYD_LEAVE;
   return yes;
}

#define SURROUNDING 16
static char const *
around(char const *cp)
{
   static char ab[2 * SURROUNDING +1];

   size_t i;
   NYD_ENTER;

   for (i = 0; i < SURROUNDING && cp > _it_begin; ++i)
      --cp;
   for (i = 0; i < sizeof(ab) - 1; ++i)
      ab[i] = *cp++;
   ab[i] = '\0';
   NYD_LEAVE;
   return ab;
}

FL enum okay
imap_search(char const *spec, int f)
{
   static char *lastspec;

   char const *xp;
   size_t i;
   enum okay rv = STOP;
   NYD_ENTER;

   if (strcmp(spec, "()")) {
      if (lastspec != NULL)
         free(lastspec);
      _it_need_regex = (spec[0] == '(' && spec[1] == '/');
      i = strlen(spec);
      lastspec = sbufdup(spec + _it_need_regex, i - _it_need_regex);
      if (_it_need_regex)
         lastspec[0] = '(';
   } else if (lastspec == NULL) {
      fprintf(stderr, tr(524, "No last SEARCH criteria available.\n"));
      goto jleave;
   }
   spec =
   _it_begin = lastspec;

   /* Regular expression searches are always local */
   _it_need_headers = FAL0;
   if (!_it_need_regex) {
#ifdef HAVE_IMAP
      if ((rv = imap_search1(spec, f) == OKAY))
         goto jleave;
#endif
   }
#ifndef HAVE_REGEX
   else {
      fprintf(stderr, tr(525, "No regular expression support for SEARCHes.\n"));
      goto jleave;
   }
#endif

   if (itparse(spec, &xp, 0) == STOP)
      goto jleave;
   if (_it_tree == NULL) {
      rv = OKAY;
      goto jleave;
   }

#ifdef HAVE_IMAP
   if (mb.mb_type == MB_IMAP && _it_need_headers)
      imap_getheaders(1, msgCount);
#endif
   srelax_hold();
   for (i = 0; UICMP(z, i, <, msgCount); ++i) {
      if (message[i].m_flag & MHIDDEN)
         continue;
      if (f == MDELETED || !(message[i].m_flag & MDELETED)) {
         size_t j = (int)(i + 1);
         if (itexecute(&mb, &message[i], j, _it_tree))
            mark((int)j, f);
         srelax();
      }
   }
   srelax_rele();

   rv = OKAY;
jleave:
#ifdef HAVE_REGEX
   for (; _it_regex != NULL; _it_regex = _it_regex->re_next)
      regfree(&_it_regex->re_regex);
   _it_regex = NULL;
#endif
   NYD_LEAVE;
   return rv;
}

/* vim:set fenc=utf-8:s-it-mode */
