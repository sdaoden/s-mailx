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
static char sccsid[] = "@(#)head.c	2.4 (gritter) 10/27/02";
#endif
#endif /* not lint */

#include "rcv.h"
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * Routines for processing and detecting headlines.
 */
static char	*copyin __P((char *, char **));
static char	*nextword __P((char *, char *));

/*
 * See if the passed line buffer is a mail header.
 * Return true if yes.  POSIX.2 leaves the content
 * following 'From ' unspecified, so don't care about
 * it.
 */
/*ARGSUSED 2*/
int
is_head(linebuf, linelen)
	char *linebuf;
	size_t linelen;
{
	char *cp;

	cp = linebuf;
	if (*cp++ != 'F' || *cp++ != 'r' || *cp++ != 'o' || *cp++ != 'm' ||
	    *cp++ != ' ')
		return (0);
	return(1);
}

/*
 * Split a headline into its useful components.
 * Copy the line into dynamic string space, then set
 * pointers into the copied line in the passed headline
 * structure.  Actually, it scans.
 */
void
parse(line, linelen, hl, pbuf)
	char *line, *pbuf;
	size_t linelen;
	struct headline *hl;
{
	char *cp;
	char *sp;
	char *word;

	hl->l_from = NULL;
	hl->l_tty = NULL;
	hl->l_date = NULL;
	cp = line;
	sp = pbuf;
	word = ac_alloc(linelen + 1);
	/*
	 * Skip over "From" first.
	 */
	cp = nextword(cp, word);
	cp = nextword(cp, word);
	if (*word)
		hl->l_from = copyin(word, &sp);
	if (cp != NULL && cp[0] == 't' && cp[1] == 't' && cp[2] == 'y') {
		cp = nextword(cp, word);
		hl->l_tty = copyin(word, &sp);
	}
	if (cp != NULL)
		hl->l_date = copyin(cp, &sp);
	else
		hl->l_date = catgets(catd, CATSET, 213, "<Unknown date>");
	ac_free(word);
}

/*
 * Copy the string on the left into the string on the right
 * and bump the right (reference) string pointer by the length.
 * Thus, dynamically allocate space in the right string, copying
 * the left string into it.
 */
static char *
copyin(src, space)
	char *src;
	char **space;
{
	char *cp;
	char *top;

	top = cp = *space;
	while ((*cp++ = *src++) != '\0')
		;
	*space = cp;
	return (top);
}

#ifdef	notdef
static int	cmatch __P((char *, char *));
/*
 * Test to see if the passed string is a ctime(3) generated
 * date string as documented in the manual.  The template
 * below is used as the criterion of correctness.
 * Also, we check for a possible trailing time zone using
 * the tmztype template.
 */

/*
 * 'A'	An upper case char
 * 'a'	A lower case char
 * ' '	A space
 * '0'	A digit
 * 'O'	An optional digit or space
 * ':'	A colon
 * '+'	A sign
 * 'N'	A new line
 */
static char  *tmztype[] = {
	"Aaa Aaa O0 00:00:00 0000",
	"Aaa Aaa O0 00:00 0000",
	"Aaa Aaa O0 00:00:00 AAA 0000",
	"Aaa Aaa O0 00:00 AAA 0000",
	/*
	 * Sommer time, e.g. MET DST
	 */
	"Aaa Aaa O0 00:00:00 AAA AAA 0000",
	"Aaa Aaa O0 00:00 AAA AAA 0000",
	/*
	 * time zone offset, e.g.
	 * +0200 or +0200 MET or +0200 MET DST
	 */
	"Aaa Aaa O0 00:00:00 +0000 0000",
	"Aaa Aaa O0 00:00 +0000 0000",
	"Aaa Aaa O0 00:00:00 +0000 AAA 0000",
	"Aaa Aaa O0 00:00 +0000 AAA 0000",
	"Aaa Aaa O0 00:00:00 +0000 AAA AAA 0000",
	"Aaa Aaa O0 00:00 +0000 AAA AAA 0000",
	/*
	 * time zone offset without time zone specification (pine)
	 */
	"Aaa Aaa O0 00:00:00 0000 +0000",
	NULL,
};

static int
is_date(date)
	char date[];
{
	int ret = 0, form = 0;

	while (tmztype[form]) {
		if ( (ret = cmatch(date, tmztype[form])) == 1 )
			break;
		form++;
	}

	return ret;
}

/*
 * Match the given string (cp) against the given template (tp).
 * Return 1 if they match, 0 if they don't
 */
static int
cmatch(cp, tp)
	char *cp, *tp;
{
	int c;

	while (*cp && *tp)
		switch (*tp++) {
		case 'a':
			if (c = *cp++, !lowerchar(c))
				return 0;
			break;
		case 'A':
			if (c = *cp++, !upperchar(c))
				return 0;
			break;
		case ' ':
			if (*cp++ != ' ')
				return 0;
			break;
		case '0':
			if (c = *cp++, !digitchar(c))
				return 0;
			break;
		case 'O':
			if (c = *cp, c != ' ' && !digitchar(c))
				return 0;
			cp++;
			break;
		case ':':
			if (*cp++ != ':')
				return 0;
			break;
		case '+':
			if (*cp != '+' && *cp != '-')
				return 0;
			cp++;
			break;
		case 'N':
			if (*cp++ != '\n')
				return 0;
			break;
		}
	if (*cp || *tp)
		return 0;
	return (1);
}
#endif	/* notdef */

/*
 * Collect a liberal (space, tab delimited) word into the word buffer
 * passed.  Also, return a pointer to the next word following that,
 * or NULL if none follow.
 */
static char *
nextword(wp, wbuf)
	char *wp, *wbuf;
{
	int c;

	if (wp == NULL) {
		*wbuf = 0;
		return (NULL);
	}
	while ((c = *wp++) != '\0' && !blankchar(c)) {
		*wbuf++ = c;
		if (c == '"') {
 			while ((c = *wp++) != '\0' && c != '"')
 				*wbuf++ = c;
 			if (c == '"')
 				*wbuf++ = c;
			else
				wp--;
 		}
	}
	*wbuf = '\0';
	for (; blankchar(c); c = *wp++)
		;
	if (c == 0)
		return (NULL);
	return (wp - 1);
}
