/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Client-side implementation of the IMAP SEARCH command. This is used
 *@ for folders not located on IMAP servers, or for IMAP servers that do
 *@ not implement the SEARCH command.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 2004
 *	Gunnar Ritter.  All rights reserved.
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
 *	This product includes software developed by Gunnar Ritter
 *	and his contributors.
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
   const char  *s_string;
   enum itoken s_token;
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

static enum okay itparse(char const *spec, char const **xp, int sub);
static enum okay itscan(char const *spec, char const **xp);
static enum okay itsplit(char const *spec, char const **xp);
static enum okay itstring(void **tp, char const *spec, char const **xp);
static int itexecute(struct mailbox *mp, struct message *m,
		size_t c, struct itnode *n);
static int matchfield(struct message *m, const char *field, const void *what);
static int matchenvelope(struct message *m, const char *field,
		const void *what);
static char *mkenvelope(struct name *np);
static int matchmsg(struct message *m, const void *what, int withheader);
static const char *around(const char *cp);

FL enum okay
imap_search(const char *spec, int f)
{
   static char *lastspec;

   enum okay rv = STOP;
   char const *xp;
   size_t i;

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
   return rv;
}

static enum okay
itparse(char const *spec, char const **xp, int sub)
{
	int	level = 0;
	struct itnode	n, *z, *ittree;
	enum okay	ok;

	_it_tree = NULL;
	while ((ok = itscan(spec, xp)) == OKAY && _it_token != ITBAD &&
			_it_token != ITEOD) {
		ittree = _it_tree;
		memset(&n, 0, sizeof n);
		spec = *xp;
		switch (_it_token) {
		case ITBOL:
			level++;
			continue;
		case ITEOL:
			if (--level == 0) {
				return OKAY;
			}
			if (level < 0) {
				if (sub > 0) {
					(*xp)--;
					return OKAY;
				}
				fprintf(stderr, "Excess in \")\".\n");
				return STOP;
			}
			continue;
		case ITNOT:
			/* <search-key> */
			n.n_token = ITNOT;
			if (itparse(spec, xp, sub+1) == STOP)
				return STOP;
			spec = *xp;
			if ((n.n_x = _it_tree) == NULL) {
				fprintf(stderr,
				"Criterion for NOT missing: >>> %s <<<\n",
					around(*xp));
				return STOP;
			}
			_it_token = ITNOT;
			break;
		case ITOR:
			/* <search-key1> <search-key2> */
			n.n_token = ITOR;
			if (itparse(spec, xp, sub+1) == STOP)
				return STOP;
			if ((n.n_x = _it_tree) == NULL) {
				fprintf(stderr, "First criterion for OR "
						"missing: >>> %s <<<\n",
						around(*xp));
				return STOP;
			}
			spec = *xp;
			if (itparse(spec, xp, sub+1) == STOP)
				return STOP;
			spec = *xp;
			if ((n.n_y = _it_tree) == NULL) {
				fprintf(stderr, "Second criterion for OR "
						"missing: >>> %s <<<\n",
						around(*xp));
				return STOP;
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
	return ok;
}

static enum okay
itscan(char const *spec, char const **xp)
{
	int	i, n;

	while (spacechar(*spec))
		spec++;
	if (*spec == '(') {
		*xp = &spec[1];
		_it_token = ITBOL;
		return OKAY;
	}
	if (*spec == ')') {
		*xp = &spec[1];
		_it_token = ITEOL;
		return OKAY;
	}
	while (spacechar(*spec))
		spec++;
	if (*spec == '\0') {
		_it_token = ITEOD;
		return OKAY;
	}
	for (i = 0; _it_strings[i].s_string; i++) {
		n = strlen(_it_strings[i].s_string);
		if (ascncasecmp(spec, _it_strings[i].s_string, n) == 0 &&
				(spacechar(spec[n]) || spec[n] == '\0' ||
				 spec[n] == '(' || spec[n] == ')')) {
			_it_token = _it_strings[i].s_token;
			spec += n;
			while (spacechar(*spec))
				spec++;
			return itsplit(spec, xp);
		}
	}
	if (digitchar(*spec)) {
		_it_number = strtoul(spec, UNCONST(xp), 10);
		if (spacechar(**xp) || **xp == '\0' ||
				**xp == '(' || **xp == ')') {
			_it_token = ITSET;
			return OKAY;
		}
	}
	fprintf(stderr, "Bad SEARCH criterion \"");
	while (*spec && !spacechar(*spec) && *spec != '(' && *spec != ')') {
		putc(*spec&0377, stderr);
		spec++;
	}
	fprintf(stderr, "\": >>> %s <<<\n", around(*xp));
	_it_token = ITBAD;
	return STOP;
}

static enum okay
itsplit(char const *spec, char const **xp)
{
	enum okay rv;
	char *cp;
	time_t t;

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
		fprintf(stderr, "Invalid size: >>> %s <<<\n",
				around(*xp));
		rv = STOP;
		break;
	case ITUID:
		/* <message set> */
		fprintf(stderr,
			"Searching for UIDs is not supported: >>> %s <<<\n",
			around(*xp));
		rv = STOP;
		break;
	default:
		*xp = spec;
		rv = OKAY;
		break;
	}
	return rv;
}

static enum okay
itstring(void **tp, char const *spec, char const **xp)
{
	int	inquote = 0;
	char	*ap;

	while (spacechar(*spec))
		spec++;
	if (*spec == '\0' || *spec == '(' || *spec == ')') {
		fprintf(stderr, "Missing string argument: >>> %s <<<\n",
				around(&(*xp)[spec - *xp]));
		return STOP;
	}
	ap = *tp = salloc(strlen(spec) + 1);
	*xp = spec;
	 do {
		if (inquote && **xp == '\\')
			*ap++ = *(*xp)++;
		else if (**xp == '"')
			inquote = !inquote;
		else if (!inquote && (spacechar(**xp) ||
				**xp == '(' || **xp == ')')) {
			*ap++ = '\0';
			break;
		}
		*ap++ = **xp;
	} while (*(*xp)++);

	*tp = imap_unquotestr(*tp);
	return OKAY;
}

static int
itexecute(struct mailbox *mp, struct message *m, size_t c, struct itnode *n)
{
	char	*cp, *line = NULL;
	size_t	linesize = 0;
	FILE	*ibuf;

	if (n == NULL) {
		fprintf(stderr, "Internal error: Empty node in SEARCH tree.\n");
		return 0;
	}
	switch (n->n_token) {
	case ITBEFORE:
	case ITON:
	case ITSINCE:
		if (m->m_time == 0 && (m->m_flag&MNOFROM) == 0 &&
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
		fprintf(stderr, "Internal SEARCH error: Lost token %d\n",
				n->n_token);
		return 0;
	case ITAND:
		return itexecute(mp, m, c, n->n_x) &
			itexecute(mp, m, c, n->n_y);
	case ITSET:
		return (unsigned long)c == n->n_n;
	case ITALL:
		return 1;
	case ITANSWERED:
		return (m->m_flag&MANSWERED) != 0;
	case ITBCC:
		return matchenvelope(m, "bcc", n->n_v);
	case ITBEFORE:
		return (unsigned long)m->m_time < n->n_n;
	case ITBODY:
		return matchmsg(m, n->n_v, 0);
	case ITCC:
		return matchenvelope(m, "cc", n->n_v);
	case ITDELETED:
		return (m->m_flag&MDELETED) != 0;
	case ITDRAFT:
		return (m->m_flag&MDRAFTED) != 0;
	case ITFLAGGED:
		return (m->m_flag&MFLAGGED) != 0;
	case ITFROM:
		return matchenvelope(m, "from", n->n_v);
	case ITHEADER:
		return matchfield(m, n->n_v, n->n_w);
	case ITKEYWORD:
		return (m->m_flag & n->n_n) != 0;
	case ITLARGER:
		return m->m_xsize > n->n_n;
	case ITNEW:
		return (m->m_flag&(MNEW|MREAD)) == MNEW;
	case ITNOT:
		return !itexecute(mp, m, c, n->n_x);
	case ITOLD:
		return (m->m_flag&MNEW) == 0;
	case ITON:
		return ((unsigned long)m->m_time >= n->n_n &&
			(unsigned long)m->m_time < n->n_n + 86400);
	case ITOR:
		return itexecute(mp, m, c, n->n_x) |
			itexecute(mp, m, c, n->n_y);
	case ITRECENT:
		return (m->m_flag&MNEW) != 0;
	case ITSEEN:
		return (m->m_flag&MREAD) != 0;
	case ITSENTBEFORE:
		return (unsigned long)m->m_date < n->n_n;
	case ITSENTON:
		return ((unsigned long)m->m_date >= n->n_n &&
			(unsigned long)m->m_date < n->n_n + 86400);
	case ITSENTSINCE:
		return (unsigned long)m->m_date >= n->n_n;
	case ITSINCE:
		return (unsigned long)m->m_time >= n->n_n;
	case ITSMALLER:
		return (unsigned long)m->m_xsize < n->n_n;
	case ITSUBJECT:
		return matchfield(m, "subject", n->n_v);
	case ITTEXT:
		return matchmsg(m, n->n_v, 1);
	case ITTO:
		return matchenvelope(m, "to", n->n_v);
	case ITUNANSWERED:
		return (m->m_flag&MANSWERED) == 0;
	case ITUNDELETED:
		return (m->m_flag&MDELETED) == 0;
	case ITUNDRAFT:
		return (m->m_flag&MDRAFTED) == 0;
	case ITUNFLAGGED:
		return (m->m_flag&MFLAGGED) == 0;
	case ITUNKEYWORD:
		return (m->m_flag & n->n_n) == 0;
	case ITUNSEEN:
		return (m->m_flag&MREAD) == 0;
	}
}

static int
matchfield(struct message *m, const char *field, const void *what)
{
   struct str in, out;
   int i = 0;

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
   return i;
}

static int
matchenvelope(struct message *m, const char *field, const void *what)
{
   struct name *np;
   char *cp;
   int rv = 0;

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
   return rv;
}

static char *
mkenvelope(struct name *np)
{
	size_t	epsize;
	char	*ep;
	char	*realnam = NULL, *sourceaddr = NULL,
		*localpart = NULL, *domainpart = NULL,
		*cp, *rp, *xp, *ip;
	struct str	in, out;
	int	level = 0, hadphrase = 0;

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
			while (cp > out.s && blankchar(cp[-1]&0377))
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
			goto done;
		case '(':
			if (level++)
				goto dfl;
			if (hadphrase++ == 0)
				rp = ip;
			break;
		case ')':
			if (--level)
				goto dfl;
			break;
		case '\\':
			if (level && cp[1])
				cp++;
			goto dfl;
		default:
		dfl:
			*rp++ = *cp;
		}
	}
done:	*rp = '\0';
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
	return ep;
}

static int
matchmsg(struct message *m, const void *what, int withheader)
{
   char *tempFile, *line = NULL;
   size_t linesize, linelen, cnt;
   FILE *fp;
   int yes = 0;

   if ((fp = Ftemp(&tempFile, "Ra", "w+", 0600, 1)) == NULL)
      goto j_leave;
   rm(tempFile);
   Ftfree(&tempFile);
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
   return yes;
}

#define SURROUNDING 16
static const char *
around(const char *cp)
{
   static char ab[2 * SURROUNDING +1];

   size_t i;

   for (i = 0; i < SURROUNDING && cp > _it_begin; ++i)
      --cp;
   for (i = 0; i < sizeof(ab) - 1; ++i)
      ab[i] = *cp++;
   ab[i] = '\0';
   return ab;
}

/* vim:set fenc=utf-8:s-it-mode (TODO only partial true) */
