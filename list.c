/*
 * Nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2002 Gunnar Ritter, Freiburg i. Br., Germany.
 */
/*
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
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

#ifndef lint
#ifdef	DOSCCS
static char sccsid[] = "@(#)list.c	2.9 (gritter) 10/27/02";
#endif
#endif /* not lint */

#include "rcv.h"
#include <ctype.h>
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * Message list handling.
 */
static char	**add_to_namelist __P((char ***, size_t *, char **, char *));
static int	markall __P((char [], int));
static int	evalcol __P((int));
static int	check __P((int, int));
static int	scan __P((char **));
static void	regret __P((int));
static void	scaninit __P((void));
static int	matchsender __P((char *, int));
static int	matchmid __P((char *, int));
static int	matchsubj __P((char *, int));
static void	mark __P((int));
static void	unmark __P((int));
static int	metamess __P((int, int));

static int	lexnumber;		/* Number of TNUMBER from scan() */
static char	lexstring[STRINGLEN];	/* String from TSTRING, scan() */
static int	regretp;		/* Pointer to TOS of regret tokens */
static int	regretstack[REGDEP];	/* Stack of regretted tokens */
static char	*string_stack[REGDEP];	/* Stack of regretted strings */
static int	numberstack[REGDEP];	/* Stack of regretted numbers */

/*
 * Convert the user string of message numbers and
 * store the numbers into vector.
 *
 * Returns the count of messages picked up or -1 on error.
 */
int
getmsglist(buf, vector, flags)
	char *buf;
	int *vector, flags;
{
	int *ip;
	struct message *mp;

	if (msgcount == 0) {
		*vector = 0;
		return 0;
	}
	if (markall(buf, flags) < 0)
		return(-1);
	ip = vector;
	for (mp = &message[0]; mp < &message[msgcount]; mp++)
		if (mp->m_flag & MMARK)
			*ip++ = mp - &message[0] + 1;
	*ip = 0;
	return(ip - vector);
}

/*
 * Mark all messages that the user wanted from the command
 * line in the message structure.  Return 0 on success, -1
 * on error.
 */

/*
 * Bit values for colon modifiers.
 */

#define	CMNEW		01		/* New messages */
#define	CMOLD		02		/* Old messages */
#define	CMUNREAD	04		/* Unread messages */
#define	CMDELETED	010		/* Deleted messages */
#define	CMREAD		020		/* Read messages */

/*
 * The following table describes the letters which can follow
 * the colon and gives the corresponding modifier bit.
 */

static struct coltab {
	char	co_char;		/* What to find past : */
	int	co_bit;			/* Associated modifier bit */
	int	co_mask;		/* m_status bits to mask */
	int	co_equal;		/* ... must equal this */
} coltab[] = {
	{ 'n',		CMNEW,		MNEW,		MNEW },
	{ 'o',		CMOLD,		MNEW,		0 },
	{ 'u',		CMUNREAD,	MREAD,		0 },
	{ 'd',		CMDELETED,	MDELETED,	MDELETED },
	{ 'r',		CMREAD,		MREAD,		MREAD },
	{ 0,		0,		0,		0 }
};

static	int	lastcolmod;

static char **
add_to_namelist(namelist, nmlsize, np, string)
	char ***namelist;
	size_t *nmlsize;
	char **np;
	char *string;
{
	size_t idx;

	if ((idx = np - *namelist) >= *nmlsize) {
		*namelist = srealloc(*namelist, (*nmlsize += 8) * sizeof *np);
		np = &(*namelist)[idx];
	}
	*np++ = string;
	return np;
}

#define	markall_ret(i)		{ \
					retval = i; \
					goto out; \
				}

static int
markall(buf, f)
	char buf[];
	int f;
{
	char **np, **nq;
	int i, retval;
	struct message *mp;
	char **namelist, *bufp, *id = NULL, *cp;
	int tok, beg, mc, star, other, valdot, colmod, colresult;
	size_t nmlsize;

	valdot = dot - &message[0] + 1;
	colmod = 0;
	for (i = 1; i <= msgcount; i++)
		unmark(i);
	bufp = buf;
	mc = 0;
	namelist = smalloc((nmlsize = 8) * sizeof *namelist);
	np = &namelist[0];
	scaninit();
	tok = scan(&bufp);
	star = 0;
	other = 0;
	beg = 0;
	while (tok != TEOL) {
		switch (tok) {
		case TNUMBER:
number:
			if (star) {
				printf(catgets(catd, CATSET, 112,
					"No numbers mixed with *\n"));
				markall_ret(-1)
			}
			mc++;
			other++;
			if (beg != 0) {
				if (check(lexnumber, f))
					markall_ret(-1)
				for (i = beg; i <= lexnumber; i++)
					if (f == MDELETED || (message[i - 1].m_flag & MDELETED) == 0)
						mark(i);
				beg = 0;
				break;
			}
			beg = lexnumber;
			if (check(beg, f))
				markall_ret(-1)
			tok = scan(&bufp);
			regret(tok);
			if (tok != TDASH) {
				mark(beg);
				beg = 0;
			}
			break;

		case TPLUS:
			if (beg != 0) {
				printf(catgets(catd, CATSET, 113,
					"Non-numeric second argument\n"));
				markall_ret(-1)
			}
			i = valdot;
			do {
				i++;
				if (i > msgcount) {
					printf(catgets(catd, CATSET, 114,
						"Referencing beyond EOF\n"));
					markall_ret(-1)
				}
			} while ((message[i - 1].m_flag & MDELETED) != f);
			mark(i);
			break;

		case TDASH:
			if (beg == 0) {
				i = valdot;
				do {
					i--;
					if (i <= 0) {
						printf(catgets(catd, CATSET,
							115,
						"Referencing before 1\n"));
						markall_ret(-1)
					}
				} while ((message[i - 1].m_flag & MDELETED) != f);
				mark(i);
			}
			break;

		case TSTRING:
			if (beg != 0) {
				printf(catgets(catd, CATSET, 116,
					"Non-numeric second argument\n"));
				markall_ret(-1)
			}
			other++;
			if (lexstring[0] == ':') {
				colresult = evalcol(lexstring[1]);
				if (colresult == 0) {
					printf(catgets(catd, CATSET, 117,
					"Unknown colon modifier \"%s\"\n"),
					    lexstring);
					markall_ret(-1)
				}
				colmod |= colresult;
			}
			else
				np = add_to_namelist(&namelist, &nmlsize,
						np, savestr(lexstring));
			break;

		case TDOLLAR:
		case TUP:
		case TDOT:
		case TSEMI:
			lexnumber = metamess(lexstring[0], f);
			if (lexnumber == -1)
				markall_ret(-1)
			goto number;

		case TSTAR:
			if (other) {
				printf(catgets(catd, CATSET, 118,
					"Can't mix \"*\" with anything\n"));
				markall_ret(-1)
			}
			star++;
			break;

		case TCOMMA:
			if ((cp = hfield("in-reply-to", dot)) != NULL)
				id = savestr(cp);
			else if ((cp = hfield("references", dot)) != NULL) {
				struct name *n;

				if ((n = extract(cp, GREF)) != NULL) {
					while (n->n_flink != NULL)
						n = n->n_flink;
					id = savestr(n->n_name);
				}
			}
			if (id == NULL) {
				printf(catgets(catd, CATSET, 227,
		"Cannot determine parent Message-ID of the current message\n"));
				markall_ret(-1)
			}
			break;

		case TERROR:
			markall_ret(-1)
		}
		tok = scan(&bufp);
	}
	lastcolmod = colmod;
	np = add_to_namelist(&namelist, &nmlsize, np, NULL);
	np--;
	mc = 0;
	if (star) {
		for (i = 0; i < msgcount; i++)
			if ((message[i].m_flag & MDELETED) == f) {
				mark(i+1);
				mc++;
			}
		if (mc == 0) {
			printf(catgets(catd, CATSET, 119,
					"No applicable messages.\n"));
			markall_ret(-1)
		}
		markall_ret(0)
	}

	/*
	 * If no numbers were given, mark all of the messages,
	 * so that we can unmark any whose sender was not selected
	 * if any user names were given.
	 */

	if ((np > namelist || colmod != 0 || id) && mc == 0)
		for (i = 1; i <= msgcount; i++)
			if ((message[i-1].m_flag & MDELETED) == f)
				mark(i);

	/*
	 * If any names were given, go through and eliminate any
	 * messages whose senders were not requested.
	 */

	if (np > namelist || id) {
		for (i = 1; i <= msgcount; i++) {
			mc = 0;
			if (np > namelist) {
				for (nq = &namelist[0]; *nq != NULL; nq++) {
					if (**nq == '/') {
						if (matchsubj(*nq, i)) {
							mc++;
							break;
						}
					}
					else {
						if (matchsender(*nq, i)) {
							mc++;
							break;
						}
					}
				}
			}
			if (mc == 0 && id && matchmid(id, i))
				mc++;
			if (mc == 0)
				unmark(i);
		}

		/*
		 * Make sure we got some decent messages.
		 */

		mc = 0;
		for (i = 1; i <= msgcount; i++)
			if (message[i-1].m_flag & MMARK) {
				mc++;
				break;
			}
		if (mc == 0) {
			if (np > namelist) {
				printf(catgets(catd, CATSET, 120,
					"No applicable messages from {%s"),
					namelist[0]);
				for (nq = &namelist[1]; *nq != NULL; nq++)
					printf(catgets(catd, CATSET, 121,
								", %s"), *nq);
				printf(catgets(catd, CATSET, 122, "}\n"));
			} else if (id) {
				printf(catgets(catd, CATSET, 227,
					"Parent message not found\n"));
			}
			markall_ret(-1)
		}
	}

	/*
	 * If any colon modifiers were given, go through and
	 * unmark any messages which do not satisfy the modifiers.
	 */

	if (colmod != 0) {
		for (i = 1; i <= msgcount; i++) {
			struct coltab *colp;

			mp = &message[i - 1];
			for (colp = &coltab[0]; colp->co_char; colp++)
				if (colp->co_bit & colmod)
					if ((mp->m_flag & colp->co_mask)
					    != colp->co_equal)
						unmark(i);
			
		}
		for (mp = &message[0]; mp < &message[msgcount]; mp++)
			if (mp->m_flag & MMARK)
				break;
		if (mp >= &message[msgcount]) {
			struct coltab *colp;

			printf(catgets(catd, CATSET, 123,
						"No messages satisfy"));
			for (colp = &coltab[0]; colp->co_char; colp++)
				if (colp->co_bit & colmod)
					printf(" :%c", colp->co_char);
			printf("\n");
			markall_ret(-1)
		}
	}
	markall_ret(0)
out:
	free(namelist);
	return retval;
}

/*
 * Turn the character after a colon modifier into a bit
 * value.
 */
static int
evalcol(col)
	int col;
{
	struct coltab *colp;

	if (col == 0)
		return(lastcolmod);
	for (colp = &coltab[0]; colp->co_char; colp++)
		if (colp->co_char == col)
			return(colp->co_bit);
	return(0);
}

/*
 * Check the passed message number for legality and proper flags.
 * If f is MDELETED, then either kind will do.  Otherwise, the message
 * has to be undeleted.
 */
static int
check(mesg, f)
	int mesg, f;
{
	struct message *mp;

	if (mesg < 1 || mesg > msgcount) {
		printf(catgets(catd, CATSET, 124,
			"%d: Invalid message number\n"), mesg);
		return(-1);
	}
	mp = &message[mesg-1];
	if (f != MDELETED && (mp->m_flag & MDELETED) != 0) {
		printf(catgets(catd, CATSET, 125,
			"%d: Inappropriate message\n"), mesg);
		return(-1);
	}
	return(0);
}

/*
 * Scan out the list of string arguments, shell style
 * for a RAWLIST.
 */
int
getrawlist(line, linesize, argv, argc)
	char line[];
	size_t linesize;
	char **argv;
	int  argc;
{
	char c, *cp, *cp2, quotec;
	int argn;
	char *linebuf;

	argn = 0;
	cp = line;
	linebuf = ac_alloc(linesize + 1);
	for (;;) {
		for (; blankchar(*cp & 0377); cp++);
		if (*cp == '\0')
			break;
		if (argn >= argc - 1) {
			printf(catgets(catd, CATSET, 126,
			"Too many elements in the list; excess discarded.\n"));
			break;
		}
		cp2 = linebuf;
		quotec = '\0';
		while ((c = *cp) != '\0') {
			cp++;
			if (quotec != '\0') {
				if (c == quotec)
					quotec = '\0';
				else if (c == '\\')
					switch (c = *cp++) {
					case '\0':
						*cp2++ = '\\';
						cp--;
						break;
					case '0': case '1': case '2': case '3':
					case '4': case '5': case '6': case '7':
						c -= '0';
						if (*cp >= '0' && *cp <= '7')
							c = c * 8 + *cp++ - '0';
						if (*cp >= '0' && *cp <= '7')
							c = c * 8 + *cp++ - '0';
						*cp2++ = c;
						break;
					case 'b':
						*cp2++ = '\b';
						break;
					case 'f':
						*cp2++ = '\f';
						break;
					case 'n':
						*cp2++ = '\n';
						break;
					case 'r':
						*cp2++ = '\r';
						break;
					case 't':
						*cp2++ = '\t';
						break;
					case 'v':
						*cp2++ = '\v';
						break;
					default:
						*cp2++ = c;
					}
				else if (c == '^') {
					c = *cp++;
					if (c == '?')
						*cp2++ = '\177';
					/* null doesn't show up anyway */
					else if ((c >= 'A' && c <= '_') ||
						 (c >= 'a' && c <= 'z'))
						*cp2++ = c & 037;
					else {
						*cp2++ = '^';
						cp--;
					}
				} else
					*cp2++ = c;
			} else if (c == '"' || c == '\'')
				quotec = c;
			else if (blankchar(c & 0377))
				break;
			else
				*cp2++ = c;
		}
		*cp2 = '\0';
		argv[argn++] = savestr(linebuf);
	}
	argv[argn] = NULL;
	ac_free(linebuf);
	return argn;
}

/*
 * scan out a single lexical item and return its token number,
 * updating the string pointer passed **p.  Also, store the value
 * of the number or string scanned in lexnumber or lexstring as
 * appropriate.  In any event, store the scanned `thing' in lexstring.
 */

static struct lex {
	char	l_char;
	enum ltoken	l_token;
} singles[] = {
	{ '$',	TDOLLAR },
	{ '.',	TDOT },
	{ '^',	TUP },
	{ '*',	TSTAR },
	{ '-',	TDASH },
	{ '+',	TPLUS },
	{ '(',	TOPEN },
	{ ')',	TCLOSE },
	{ ',',	TCOMMA },
	{ ';',	TSEMI },
	{ 0,	0 }
};

static int
scan(sp)
	char **sp;
{
	char *cp, *cp2;
	int c;
	struct lex *lp;
	int quotec;

	if (regretp >= 0) {
		strncpy(lexstring, string_stack[regretp], STRINGLEN);
		lexstring[STRINGLEN-1]='\0';
		lexnumber = numberstack[regretp];
		return(regretstack[regretp--]);
	}
	cp = *sp;
	cp2 = lexstring;
	c = *cp++;

	/*
	 * strip away leading white space.
	 */

	while (blankchar(c))
		c = *cp++;

	/*
	 * If no characters remain, we are at end of line,
	 * so report that.
	 */

	if (c == '\0') {
		*sp = --cp;
		return(TEOL);
	}

	/*
	 * If the leading character is a digit, scan
	 * the number and convert it on the fly.
	 * Return TNUMBER when done.
	 */

	if (digitchar(c)) {
		lexnumber = 0;
		while (digitchar(c)) {
			lexnumber = lexnumber*10 + c - '0';
			*cp2++ = c;
			c = *cp++;
		}
		*cp2 = '\0';
		*sp = --cp;
		return(TNUMBER);
	}

	/*
	 * Check for single character tokens; return such
	 * if found.
	 */

	for (lp = &singles[0]; lp->l_char != 0; lp++)
		if (c == lp->l_char) {
			lexstring[0] = c;
			lexstring[1] = '\0';
			*sp = cp;
			return(lp->l_token);
		}

	/*
	 * We've got a string!  Copy all the characters
	 * of the string into lexstring, until we see
	 * a null, space, or tab.
	 * If the lead character is a " or ', save it
	 * and scan until you get another.
	 */

	quotec = 0;
	if (c == '\'' || c == '"') {
		quotec = c;
		c = *cp++;
	}
	while (c != '\0') {
		if (c == quotec) {
			cp++;
			break;
		}
		if (quotec == 0 && blankchar(c))
			break;
		if (cp2 - lexstring < STRINGLEN-1)
			*cp2++ = c;
		c = *cp++;
	}
	if (quotec && c == 0) {
		fprintf(stderr, catgets(catd, CATSET, 127,
				"Missing %c\n"), quotec);
		return TERROR;
	}
	*sp = --cp;
	*cp2 = '\0';
	return(TSTRING);
}

/*
 * Unscan the named token by pushing it onto the regret stack.
 */
static void
regret(token)
	int token;
{
	if (++regretp >= REGDEP)
		panic(catgets(catd, CATSET, 128, "Too many regrets"));
	regretstack[regretp] = token;
	lexstring[STRINGLEN-1] = '\0';
	string_stack[regretp] = savestr(lexstring);
	numberstack[regretp] = lexnumber;
}

/*
 * Reset all the scanner global variables.
 */
static void
scaninit()
{
	regretp = -1;
}

/*
 * Find the first message whose flags & m == f  and return
 * its message number.
 */
int
first(f, m)
	int f, m;
{
	struct message *mp;

	if (msgcount == 0)
		return 0;
	f &= MDELETED;
	m &= MDELETED;
	for (mp = dot; mp < &message[msgcount]; mp++)
		if ((mp->m_flag & m) == f)
			return mp - message + 1;
	for (mp = dot-1; mp >= &message[0]; mp--)
		if ((mp->m_flag & m) == f)
			return mp - message + 1;
	return 0;
}

/*
 * See if the passed name sent the passed message number.  Return true
 * if so.
 */
static int
matchsender(str, mesg)
	char *str;
	int mesg;
{
	return !strcmp(str, nameof(&message[mesg - 1], 0));
}

static int
matchmid(id, mesg)
	char *id;
	int mesg;
{
	char *cp;

	if ((cp = hfield("message-id", &message[mesg - 1])) != NULL &&
			strcmp(cp, id) == 0)
		return 1;
	return 0;
}

/*
 * See if the given string matches inside the subject field of the
 * given message.  For the purpose of the scan, we ignore case differences.
 * If it does, return true.  The string search argument is assumed to
 * have the form "/search-string."  If it is of the form "/," we use the
 * previous search string.
 */

static char lastscan[128];

static int
matchsubj(str, mesg)
	char *str;
	int mesg;
{
	struct message *mp;
	char *cp, *cp2, *backup;
	struct str in, out;

	str++;
	if (strlen(str) == 0) {
		str = lastscan;
	} else {
		strncpy(lastscan, str, sizeof lastscan);
		lastscan[sizeof lastscan - 1]='\0';
	}
	mp = &message[mesg-1];
	
	/*
	 * Now look, ignoring case, for the word in the string.
	 */

	if (value("searchheaders") && (cp = strchr(str, ':'))) {
		*cp++ = '\0';
		cp2 = hfield(str, mp);
		cp[-1] = ':';
		str = cp;
	} else {
		cp = str;
		cp2 = hfield("subject", mp);
	}
	if (cp2 == NULL)
		return(0);
	in.s = cp2;
	in.l = strlen(cp2);
	mime_fromhdr(&in, &out, TD_ICONV);
	cp2 = out.s;
	backup = cp2;
	while (*cp2) {
		if (*cp == 0) {
			free(out.s);
			return(1);
		}
#ifdef	HAVE_MBTOWC
		if (mb_cur_max > 1) {
			wchar_t c, c2;
			int sz;

			if ((sz = mbtowc(&c, cp, mb_cur_max)) < 0)
				goto singlebyte;
			cp += sz;
			if ((sz = mbtowc(&c2, cp2, mb_cur_max)) < 0)
				goto singlebyte;
			cp2 += sz;
			c = towupper(c);
			c2 = towupper(c2);
			if (c != c2) {
				if ((sz = mbtowc(&c, backup, mb_cur_max)) > 0) {
					backup += sz;
					cp2 = backup;
				} else
					cp2 = ++backup;
				cp = str;
			}
		} else
#endif	/* HAVE_MBTOWC */
		{
			int c, c2;

	singlebyte:	c = *cp++ & 0377;
			if (islower(c))
				c = toupper(c);
			c2 = *cp2++ & 0377;
			if (islower(c2))
				c2 = toupper(c2);
			if (c != c2) {
				cp2 = ++backup;
				cp = str;
			}
		}
	}
	free(out.s);
	return(*cp == 0);
}

/*
 * Mark the named message by setting its mark bit.
 */
static void
mark(mesg)
	int mesg;
{
	int i;

	i = mesg;
	if (i < 1 || i > msgcount)
		panic(catgets(catd, CATSET, 129, "Bad message number to mark"));
	message[i-1].m_flag |= MMARK;
}

/*
 * Unmark the named message.
 */
static void
unmark(mesg)
	int mesg;
{
	int i;

	i = mesg;
	if (i < 1 || i > msgcount)
		panic(catgets(catd, CATSET, 130,
					"Bad message number to unmark"));
	message[i-1].m_flag &= ~MMARK;
}

/*
 * Return the message number corresponding to the passed meta character.
 */
static int
metamess(meta, f)
	int meta, f;
{
	int c, m;
	struct message *mp;

	c = meta;
	switch (c) {
	case '^':
		/*
		 * First 'good' message left.
		 */
		for (mp = &message[0]; mp < &message[msgcount]; mp++)
			if ((mp->m_flag & MDELETED) == f)
				return(mp - &message[0] + 1);
		printf(catgets(catd, CATSET, 131, "No applicable messages\n"));
		return(-1);

	case '$':
		/*
		 * Last 'good message left.
		 */
		for (mp = &message[msgcount-1]; mp >= &message[0]; mp--)
			if ((mp->m_flag & MDELETED) == f)
				return(mp - &message[0] + 1);
		printf(catgets(catd, CATSET, 132, "No applicable messages\n"));
		return(-1);

	case '.':
		/* 
		 * Current message.
		 */
		m = dot - &message[0] + 1;
		if ((dot->m_flag & MDELETED) != f) {
			printf(catgets(catd, CATSET, 133,
				"%d: Inappropriate message\n"), m);
			return(-1);
		}
		return(m);

	case ';':
		/*
		 * Previously current message.
		 */
		if (prevdot == NULL) {
			printf(catgets(catd, CATSET, 228,
				"No previously current message\n"));
			return(-1);
		}
		m = prevdot - &message[0] + 1;
		if ((prevdot->m_flag & MDELETED) != f) {
			printf(catgets(catd, CATSET, 133,
				"%d: Inappropriate message\n"), m);
			return(-1);
		}
		return(m);

	default:
		printf(catgets(catd, CATSET, 134,
				"Unknown metachar (%c)\n"), c);
		return(-1);
	}
}
