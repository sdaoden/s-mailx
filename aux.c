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
static char sccsid[] = "@(#)aux.c	2.19 (gritter) 12/7/02";
#endif
#endif /* not lint */

#include "rcv.h"
#include "extern.h"
#include <utime.h>

/*
 * Mail -- a mail program
 *
 * Auxiliary functions.
 */
static char	*save2str __P((char *, char *));
static int	gethfield __P((FILE *, char **, size_t *, int, char **));
static char	*is_hfield __P((char [], char[], char *));
static char	*skip_comment __P((char *));
static char	*name1 __P((struct message *, int));
static int	charcount __P((char *, int));
static time_t	combinetime __P((int, int, int, int, int, int));

/*
 * Return a pointer to a dynamic copy of the argument.
 */
char *
savestr(str)
	char *str;
{
	char *new;
	int size = strlen(str) + 1;

	if ((new = salloc(size)) != NULL)
		memcpy(new, str, size);
	return new;
}

/*
 * Make a copy of new argument incorporating old one.
 */
static char *
save2str(str, old)
	char *str, *old;
{
	char *new;
	int newsize = strlen(str) + 1;
	int oldsize = old ? strlen(old) + 1 : 0;

	if ((new = salloc(newsize + oldsize)) != NULL) {
		if (oldsize) {
			memcpy(new, old, oldsize);
			new[oldsize - 1] = ' ';
		}
		memcpy(new + oldsize, str, newsize);
	}
	return new;
}

#ifdef	__STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

#ifdef	HAVE_STRINGS_H
#include <strings.h>
#endif

#ifndef	HAVE_SNPRINTF
/*
 * Lazy vsprintf wrapper.
 */
int
#ifdef	__STDC__
snprintf(char *str, size_t size, const char *format, ...)
#else
snprintf(str, size, format, va_alist)
char *str;
size_t size;
const char *format;
va_dcl
#endif
{
	va_list ap;
	int ret;
#ifdef	__STDC__
	va_start(ap, format);
#else
	va_start(ap);
#endif
	ret = vsprintf(str, format, ap);
	va_end(ap);
	return ret;
}
#endif	/* !HAVE_SNPRINTF */

/*
 * Announce a fatal error and die.
 */
void
#ifdef	__STDC__
panic(const char *format, ...)
#else
panic(format, va_alist)
	char *format;
        va_dcl
#endif
{
	va_list ap;
#ifdef	__STDC__
	va_start(ap, format);
#else
	va_start(ap);
#endif
	(void)fprintf(stderr, catgets(catd, CATSET, 1, "panic: "));
	vfprintf(stderr, format, ap);
	va_end(ap);
	(void)fprintf(stderr, catgets(catd, CATSET, 2, "\n"));
	fflush(stderr);
	abort();
}

/*
 * Touch the named message by setting its MTOUCH flag.
 * Touched messages have the effect of not being sent
 * back to the system mailbox on exit.
 */
void
touch(mp)
	struct message *mp;
{

	mp->m_flag |= MTOUCH;
	if ((mp->m_flag & MREAD) == 0)
		mp->m_flag |= MREAD|MSTATUS;
}

/*
 * Test to see if the passed file name is a directory.
 * Return true if it is.
 */
int
is_dir(name)
	char name[];
{
	struct stat sbuf;

	if (stat(name, &sbuf) < 0)
		return(0);
	return(S_ISDIR(sbuf.st_mode));
}

/*
 * Count the number of arguments in the given string raw list.
 */
int
argcount(argv)
	char **argv;
{
	char **ap;

	for (ap = argv; *ap++ != NULL;)
		;	
	return ap - argv - 1;
}

void
extract_header(fp, hp)
	FILE *fp;
	struct header *hp;
{
	char *linebuf = NULL;
	size_t linesize = 0;
	int seenfields = 0;
	char *colon, *cp;
	struct header nh;
	struct header *hq = &nh;
	int lc, c;

	hq->h_to = NULL;
	hq->h_cc = NULL;
	hq->h_bcc = NULL;
	hq->h_subject = NULL;
	for (lc = 0; readline(fp, &linebuf, &linesize) > 0; lc++);
	rewind(fp);
	while ((lc = gethfield(fp, &linebuf, &linesize, lc, &colon)) >= 0) {
		if (is_hfield(linebuf, colon, "to") != NULL) {
			seenfields++;
			hq->h_to = checkaddrs(cat(hq->h_to,
						extract(&colon[1], GTO)));
		} else if (is_hfield(linebuf, colon, "cc") != NULL) {
			seenfields++;
			hq->h_cc = checkaddrs(cat(hq->h_cc,
						extract(&colon[1], GCC)));
		} else if (is_hfield(linebuf, colon, "bcc") != NULL) {
			seenfields++;
			hq->h_bcc = checkaddrs(cat(hq->h_bcc,
						extract(&colon[1], GBCC)));
		} else if (is_hfield(linebuf, colon, "subject") != NULL ||
				is_hfield(linebuf, colon, "subj") != NULL) {
			seenfields++;
			for (cp = &colon[1]; blankchar(*cp & 0377); cp++);
			hq->h_subject = hq->h_subject ?
				save2str(hq->h_subject, cp) :
				savestr(cp);
		} else
			fprintf(stderr, catgets(catd, CATSET, 266,
					"Ignoring header field \"%s\"\n"),
					linebuf);
	}
	/*
	 * In case the blank line after the header has been edited out.
	 * Otherwise, fetch the header separator.
	 */
	if (linebuf) {
		if (linebuf[0] != '\0') {
			for (cp = linebuf; *(++cp) != '\0'; );
			fseek(fp, (long)-(1 + cp - linebuf), SEEK_CUR);
		} else {
			if ((c = getc(fp)) != '\n' && c != EOF)
				ungetc(c, fp);
		}
	}
	if (seenfields) {
		hp->h_to = hq->h_to;
		hp->h_cc = hq->h_cc;
		hp->h_bcc = hq->h_bcc;
		hp->h_subject = hq->h_subject;
	} else
		fprintf(stderr, catgets(catd, CATSET, 267,
				"Restoring deleted header lines\n"));
	if (linebuf)
		free(linebuf);
}

/*
 * Return the desired header line from the passed message
 * pointer (or NULL if the desired header field is not available).
 * If mult is zero, return the content of the first matching header
 * field only, the content of all matching header fields else.
 */
char *
hfield_mult(field, mp, mult)
	char field[];
	struct message *mp;
{
	FILE *ibuf;
	char *linebuf = NULL;
	size_t linesize = 0;
	int lc;
	char *hfield;
	char *colon, *oldhfield = NULL;

	if ((ibuf = setinput(mp, NEED_HEADER)) == NULL)
		return NULL;
	if ((lc = mp->m_lines - 1) < 0)
		return NULL;
	if ((mp->m_flag & MNOFROM) == 0) {
		if (readline(ibuf, &linebuf, &linesize) < 0) {
			if (linebuf)
				free(linebuf);
			return NULL;
		}
	}
	while (lc > 0) {
		if ((lc = gethfield(ibuf, &linebuf, &linesize, lc, &colon))
				< 0) {
			if (linebuf)
				free(linebuf);
			return oldhfield;
		}
		if ((hfield = is_hfield(linebuf, colon, field)) != NULL) {
			oldhfield = save2str(hfield, oldhfield);
			if (mult == 0)
				break;
		}
	}
	if (linebuf)
		free(linebuf);
	return oldhfield;
}

/*
 * Return the next header field found in the given message.
 * Return >= 0 if something found, < 0 elsewise.
 * "colon" is set to point to the colon in the header.
 * Must deal with \ continuations & other such fraud.
 */
static int
gethfield(f, linebuf, linesize, rem, colon)
	FILE *f;
	char **linebuf;
	size_t *linesize;
	int rem;
	char **colon;
{
	char *line2 = NULL;
	size_t line2size = 0;
	char *cp, *cp2;
	int c;

	if (*linebuf == NULL)
		*linebuf = srealloc(*linebuf, *linesize = 1);
	**linebuf = '\0';
	for (;;) {
		if (--rem < 0)
			return -1;
		if ((c = readline(f, linebuf, linesize)) <= 0)
			return -1;
		for (cp = *linebuf; fieldnamechar(*cp & 0377); cp++);
		if (cp > *linebuf)
			while (blankchar(*cp & 0377))
				cp++;
		if (*cp != ':' || cp == *linebuf)
			continue;
		/*
		 * I guess we got a headline.
		 * Handle wraparounding
		 */
		*colon = cp;
		cp = *linebuf + c;
		for (;;) {
			while (--cp >= *linebuf && blankchar(*cp & 0377));
			cp++;
			if (rem <= 0)
				break;
			ungetc(c = sgetc(f), f);
			if (!blankchar(c))
				break;
			if ((c = readline(f, &line2, &line2size)) < 0)
				break;
			rem--;
			for (cp2 = line2; blankchar(*cp2 & 0377); cp2++);
			c -= cp2 - line2;
			if (cp + c >= *linebuf + *linesize - 2) {
				size_t diff = cp - *linebuf;
				size_t colondiff = *colon - *linebuf;
				*linebuf = srealloc(*linebuf,
						*linesize += c + 2);
				cp = &(*linebuf)[diff];
				*colon = &(*linebuf)[colondiff];
			}
			*cp++ = ' ';
			memcpy(cp, cp2, c);
			cp += c;
		}
		*cp = 0;
		if (line2)
			free(line2);
		return rem;
	}
	/* NOTREACHED */
}

/*
 * Check whether the passed line is a header line of
 * the desired breed.  Return the field body, or 0.
 */
static char *
is_hfield(linebuf, colon, field)
	char linebuf[], field[];
	char *colon;
{
	char *cp = colon;

	*cp = 0;
	if (asccasecmp(linebuf, field) != 0) {
		*cp = ':';
		return 0;
	}
	*cp = ':';
	for (cp++; blankchar(*cp & 0377); cp++);
	return cp;
}

/*
 * Copy a string, lowercasing it as we go.
 */
void
i_strcpy(dest, src, size)
	char *dest, *src;
	int size;
{
	char *max;

	max=dest+size-1;
	while (dest<=max) {
		*dest++ = lowerconv(*src & 0377);
		if (*src++ == '\0')
			break;
	}
}

/*
 * The following code deals with input stacking to do source
 * commands.  All but the current file pointer are saved on
 * the stack.
 */

static	int	ssp;			/* Top of file stack */
struct sstack {
	FILE	*s_file;		/* File we were in. */
	enum condition	s_cond;		/* Saved state of conditionals */
	int	s_loading;		/* Loading .mailrc, etc. */
} sstack[NOFILE];

/*
 * Pushdown current input file and switch to a new one.
 * Set the global flag "sourcing" so that others will realize
 * that they are no longer reading from a tty (in all probability).
 */
int
source(v)
	void *v;
{
	char **arglist = v;
	FILE *fi;
	char *cp;

	if ((cp = expand(*arglist)) == NULL)
		return(1);
	if ((fi = Fopen(cp, "r")) == (FILE *)NULL) {
		perror(cp);
		return(1);
	}
	if (ssp >= NOFILE - 1) {
		printf(catgets(catd, CATSET, 3,
					"Too much \"sourcing\" going on.\n"));
		Fclose(fi);
		return(1);
	}
	sstack[ssp].s_file = input;
	sstack[ssp].s_cond = cond;
	sstack[ssp].s_loading = loading;
	ssp++;
	loading = 0;
	cond = CANY;
	input = fi;
	sourcing++;
	return(0);
}

/*
 * Pop the current input back to the previous level.
 * Update the "sourcing" flag as appropriate.
 */
int
unstack()
{
	if (ssp <= 0) {
		printf(catgets(catd, CATSET, 4,
					"\"Source\" stack over-pop.\n"));
		sourcing = 0;
		return(1);
	}
	Fclose(input);
	if (cond != CANY)
		printf(catgets(catd, CATSET, 5, "Unmatched \"if\"\n"));
	ssp--;
	cond = sstack[ssp].s_cond;
	loading = sstack[ssp].s_loading;
	input = sstack[ssp].s_file;
	if (ssp == 0)
		sourcing = loading;
	return(0);
}

/*
 * Touch the indicated file.
 * This is nifty for the shell.
 */
void
alter(name)
	char *name;
{
	struct stat sb;
	struct utimbuf utb;

	if (stat(name, &sb))
		return;
	utb.actime = time((time_t *)0) + 1;
	utb.modtime = sb.st_mtime;
	(void)utime(name, &utb);
}

/*
 * Examine the passed line buffer and
 * return true if it is all blanks and tabs.
 */
int
blankline(linebuf)
	char linebuf[];
{
	char *cp;

	for (cp = linebuf; *cp; cp++)
		if (!blankchar(*cp & 0377))
			return(0);
	return(1);
}

/*
 * Get sender's name from this message.  If the message has
 * a bunch of arpanet stuff in it, we may have to skin the name
 * before returning it.
 */
char *
nameof(mp, reptype)
	struct message *mp;
	int reptype;
{
	char *cp, *cp2;

	cp = skin(name1(mp, reptype));
	if (reptype != 0 || charcount(cp, '!') < 2)
		return(cp);
	cp2 = strrchr(cp, '!');
	cp2--;
	while (cp2 > cp && *cp2 != '!')
		cp2--;
	if (*cp2 == '!')
		return(cp2 + 1);
	return(cp);
}

/*
 * Start of a "comment".
 * Ignore it.
 */
static char *
skip_comment(cp)
	char *cp;
{
	int nesting = 1;

	for (; nesting > 0 && *cp; cp++) {
		switch (*cp) {
		case '\\':
			if (cp[1])
				cp++;
			break;
		case '(':
			nesting++;
			break;
		case ')':
			nesting--;
			break;
		}
	}
	return cp;
}

/*
 * Skin an arpa net address according to the RFC 822 interpretation
 * of "host-phrase."
 */
char *
skin(name)
	char *name;
{
	int c;
	char *cp, *cp2;
	char *bufend;
	int gotlt, lastsp;
	char *nbuf;

	if (name == NULL)
		return(NULL);
	if (strchr(name, '(') == NULL && strchr(name, '<') == NULL
	    && strchr(name, ' ') == NULL)
		return(name);
	gotlt = 0;
	lastsp = 0;
	nbuf = ac_alloc(strlen(name) + 1);
	bufend = nbuf;
	for (cp = name, cp2 = bufend; (c = *cp++) != '\0'; ) {
		switch (c) {
		case '(':
			cp = skip_comment(cp);
			lastsp = 0;
			break;

		case '"':
			/*
			 * Start of a "quoted-string".
			 * Copy it in its entirety.
			 */
			*cp2++ = c;
			while ((c = *cp) != '\0') {
				cp++;
				if (c == '"') {
					*cp2++ = c;
					break;
				}
				if (c != '\\')
					*cp2++ = c;
				else if ((c = *cp) != '\0') {
					*cp2++ = c;
					cp++;
				}
			}
			lastsp = 0;
			break;

		case ' ':
			if (cp[0] == 'a' && cp[1] == 't' && cp[2] == ' ')
				cp += 3, *cp2++ = '@';
			else
			if (cp[0] == '@' && cp[1] == ' ')
				cp += 2, *cp2++ = '@';
#if 0
			/*
			 * RFC 822 specifies spaces are STRIPPED when
			 * in an adress specifier.
			 */
			else
				lastsp = 1;
#endif
			break;

		case '<':
			cp2 = bufend;
			gotlt++;
			lastsp = 0;
			break;

		case '>':
			if (gotlt) {
				gotlt = 0;
				while ((c = *cp) != '\0' && c != ',') {
					cp++;
					if (c == '(')
						cp = skip_comment(cp);
					else if (c == '"')
						while ((c = *cp) != '\0') {
							cp++;
							if (c == '"')
								break;
							if (c == '\\' && *cp)
								cp++;
						}
				}
				lastsp = 0;
				break;
			}
			/* Fall into . . . */

		default:
			if (lastsp) {
				lastsp = 0;
				*cp2++ = ' ';
			}
			*cp2++ = c;
			if (c == ',' && !gotlt) {
				*cp2++ = ' ';
				for (; *cp == ' '; cp++)
					;
				lastsp = 0;
				bufend = cp2;
			}
		}
	}
	*cp2 = 0;
	cp = savestr(nbuf);
	ac_free(nbuf);
	return cp;
}

/*
 * Fetch the sender's name from the passed message.
 * Reptype can be
 *	0 -- get sender's name for display purposes
 *	1 -- get sender's name for reply
 *	2 -- get sender's name for Reply
 */
static char *
name1(mp, reptype)
	struct message *mp;
	int reptype;
{
	char *namebuf;
	size_t namesize;
	char *linebuf = NULL;
	size_t linesize = 0;
	char *cp, *cp2;
	FILE *ibuf;
	int first = 1;

	if ((cp = hfield("from", mp)) != NULL && *cp != '\0')
		return cp;
	if (reptype == 0 && (cp = hfield("sender", mp)) != NULL &&
			*cp != '\0')
		return cp;
	namebuf = smalloc(namesize = 1);
	namebuf[0] = 0;
	if (mp->m_flag & MNOFROM)
		goto out;
	if ((ibuf = setinput(mp, NEED_HEADER)) == NULL)
		goto out;
	if (readline(ibuf, &linebuf, &linesize) < 0)
		goto out;
newname:
	if (namesize <= linesize)
		namebuf = srealloc(namebuf, namesize = linesize + 1);
	for (cp = linebuf; *cp && *cp != ' '; cp++)
		;
	for (; blankchar(*cp & 0377); cp++);
	for (cp2 = &namebuf[strlen(namebuf)];
	     *cp && !blankchar(*cp & 0377) && cp2 < namebuf + namesize - 1;)
		*cp2++ = *cp++;
	*cp2 = '\0';
	if (readline(ibuf, &linebuf, &linesize) < 0)
		goto out;
	if ((cp = strchr(linebuf, 'F')) == NULL)
		goto out;
	if (strncmp(cp, "From", 4) != 0)
		goto out;
	if (namesize <= linesize)
		namebuf = srealloc(namebuf, namesize = linesize + 1);
	while ((cp = strchr(cp, 'r')) != NULL) {
		if (strncmp(cp, "remote", 6) == 0) {
			if ((cp = strchr(cp, 'f')) == NULL)
				break;
			if (strncmp(cp, "from", 4) != 0)
				break;
			if ((cp = strchr(cp, ' ')) == NULL)
				break;
			cp++;
			if (first) {
				strncpy(namebuf, cp, namesize);
				first = 0;
			} else {
				cp2=strrchr(namebuf, '!')+1;
				strncpy(cp2, cp, (namebuf+namesize)-cp2);
			}
			namebuf[namesize-2]='\0';
			strcat(namebuf, "!");
			goto newname;
		}
		cp++;
	}
out:
	if (*namebuf != '\0' || ((cp = hfield("return-path", mp))) == NULL ||
			*cp == '\0')
		cp = savestr(namebuf);
	if (linebuf)
		free(linebuf);
	free(namebuf);
	return cp;
}

/*
 * Count the occurances of c in str
 */
static int
charcount(str, c)
	char *str;
	int c;
{
	char *cp;
	int i;

	for (i = 0, cp = str; *cp; cp++)
		if (*cp == c)
			i++;
	return(i);
}

/*
 * Are any of the characters in the two strings the same?
 */
int
anyof(s1, s2)
	char *s1, *s2;
{

	while (*s1)
		if (strchr(s2, *s1++))
			return 1;
	return 0;
}

/*
 * See if the given header field is supposed to be ignored.
 */
int
is_ign(field, fieldlen, ignore)
	char *field;
	size_t fieldlen;
	struct ignoretab ignore[2];
{
	char *realfld;
	int ret;

	if (ignore == NULL)
		return 0;
	if (ignore == allignore)
		return 1;
	/*
	 * Lower-case the string, so that "Status" and "status"
	 * will hash to the same place.
	 */
	realfld = ac_alloc(fieldlen + 1);
	i_strcpy(realfld, field, fieldlen + 1);
	if (ignore[1].i_count > 0)
		ret = !member(realfld, ignore + 1);
	else
		ret = member(realfld, ignore);
	ac_free(realfld);
	return ret;
}

int
member(realfield, table)
	char *realfield;
	struct ignoretab *table;
{
	struct ignore *igp;

	for (igp = table->i_head[hash(realfield)]; igp != 0; igp = igp->i_link)
		if (*igp->i_field == *realfield &&
		    equal(igp->i_field, realfield))
			return (1);
	return (0);
}

/*
 * Fake Sender for From_ lines if missing, e. g. with POP3.
 */
char *
fakefrom(mp)
	struct message *mp;
{
	char *name;

	if (((name = skin(hfield("return-path", mp))) == NULL ||
				*name == '\0' ) &&
			((name = skin(hfield("from", mp))) == NULL ||
				*name == '\0'))
		name = "-";
	return name;
}

char *
fakedate(t)
	time_t t;
{
	char *cp, *cq;

	cp = ctime(&t);
	for (cq = cp; *cq && *cq != '\n'; cq++);
	*cq = '\0';
	return savestr(cp);
}

char *
nexttoken(cp)
	char *cp;
{
	for (;;) {
		if (*cp == '\0')
			return NULL;
		if (*cp == '(') {
			int nesting = 0;

			while (*cp != '\0') {
				switch (*cp++) {
				case '(':
					nesting++;
					break;
				case ')':
					nesting--;
					break;
				}
				if (nesting <= 0)
					break;
			}
		} else if (blankchar(*cp & 0377) || *cp == ',')
			cp++;
		else
			break;
	}
	return cp;
}

time_t
rfctime(date)
	char *date;
{
	char *cp = date, *x;
	time_t t;
	int i, year, month, day, hour, minute, second;

	if ((cp = nexttoken(cp)) == NULL)
		goto invalid;
	if (alphachar(cp[0] & 0377) && alphachar(cp[1] & 0377) &&
				alphachar(cp[2] & 0377) && cp[3] == ',') {
		if ((cp = nexttoken(&cp[4])) == NULL)
			goto invalid;
	}
	day = strtol(cp, &x, 10);
	if ((cp = nexttoken(x)) == NULL)
		goto invalid;
	for (i = 0; month_names[i]; i++) {
		if (strncmp(cp, month_names[i], 3) == 0)
			break;
	}
	if (month_names[i] == NULL)
		goto invalid;
	month = i + 1;
	if ((cp = nexttoken(&cp[3])) == NULL)
		goto invalid;
	year = strtol(cp, &x, 10);
	if ((cp = nexttoken(x)) == NULL)
		goto invalid;
	hour = strtol(cp, &x, 10);
	if (*x != ':')
		goto invalid;
	cp = &x[1];
	minute = strtol(cp, &x, 10);
	if (*x == ':') {
		cp = &x[1];
		second = strtol(cp, &x, 10);
	} else
		second = 0;
	if ((t = combinetime(year, month, day, hour, minute, second)) ==
			(time_t)-1)
		goto invalid;
	if ((cp = nexttoken(x)) != NULL) {
		int sign = -1;
		char buf[3];

		switch (*cp) {
		case '-':
			sign = 1;
			/*FALLTHRU*/
		case '+':
			cp++;
		}
		if (digitchar(cp[0] & 0377) && digitchar(cp[1] & 0377) &&
				digitchar(cp[2] & 0377) &&
				digitchar(cp[3] & 0377)) {
			buf[2] = '\0';
			buf[0] = cp[0];
			buf[1] = cp[1];
			t += strtol(buf, NULL, 10) * sign * 3600;
			buf[0] = cp[2];
			buf[1] = cp[3];
			t += strtol(buf, NULL, 10) * sign * 60;
		}
	}
	return t;
invalid:
	return 0;
}

#define	leapyear(year)	((year % 100 ? year : year / 100) % 4 == 0)

static time_t
combinetime(year, month, day, hour, minute, second)
{
	time_t t;

	if (second < 0 || minute < 0 || hour < 0 || day < 1)
		return -1;
	t = second + minute * 60 + hour * 3600 + (day - 1) * 86400;
	if (year < 70)
		year += 2000;
	else if (year < 1900)
		year += 1900;
	if (month > 1)
		t += 86400 * 31;
	if (month > 2)
		t += 86400 * (leapyear(year) ? 29 : 28);
	if (month > 3)
		t += 86400 * 31;
	if (month > 4)
		t += 86400 * 30;
	if (month > 5)
		t += 86400 * 31;
	if (month > 6)
		t += 86400 * 30;
	if (month > 7)
		t += 86400 * 31;
	if (month > 8)
		t += 86400 * 31;
	if (month > 9)
		t += 86400 * 30;
	if (month > 10)
		t += 86400 * 31;
	if (month > 11)
		t += 86400 * 30;
	year -= 1900;
	t += (year - 70) * 31536000 + ((year - 69) / 4) * 86400 -
		((year - 1) / 100) * 86400 + ((year + 299) / 400) * 86400;
	return t;
}

enum protocol
which_protocol(name)
	const char *name;
{
	register const char *cp;

	for (cp = name; *cp && *cp != ':'; cp++);
	if (cp[0] == ':' && cp[1] == '/' && cp[2] == '/') {
		if (strncmp(name, "pop3://", 7) == 0)
			return PROTO_POP3;
		if (strncmp(name, "pop3s://", 8) == 0)
#ifdef	USE_SSL
			return PROTO_POP3;
#else	/* !USE_SSL */
			fprintf(stderr, catgets(catd, CATSET, 225,
					"No SSL support compiled in.\n"));
#endif	/* !USE_SSL */
		return PROTO_UNKNOWN;
	} else
		return PROTO_FILE;
}

void
out_of_memory()
{
	panic("no memory");
}

void *
smalloc(s)
size_t s;
{
	void *p;

	if (s == 0)
		s = 1;
	if ((p = malloc(s)) == NULL)
		out_of_memory();
	return p;
}

void *
srealloc(v, s)
void *v;
size_t s;
{
	void *r;

	if (s == 0)
		s = 1;
	if (v == NULL)
		return smalloc(s);
	if ((r = realloc(v, s)) == NULL)
		out_of_memory();
	return r;
}

void *
scalloc(nmemb, size)
size_t nmemb, size;
{
	void *vp;

	if (size == 0)
		size = 1;
	if ((vp = calloc(nmemb, size)) == NULL)
		out_of_memory();
	return vp;
}

char *
sstpcpy(dst, src)
char *dst;
const char *src;
{
	while ((*dst = *src++) != '\0')
		dst++;
	return dst;
}

char *
sstrdup(cp)
const char *cp;
{
	char	*dp = smalloc(strlen(cp) + 1);

	strcpy(dp, cp);
	return dp;
}

/*
 * Locale-independent character class functions.
 */
int
asccasecmp(s1, s2)
const char *s1, *s2;
{
	register int cmp;

	do
		if ((cmp = lowerconv(*s1 & 0377) - lowerconv(*s2 & 0377)) != 0)
			return cmp;
	while (*s1++ != '\0' && *s2++ != '\0');
	return 0;
}

int
ascncasecmp(s1, s2, sz)
const char *s1, *s2;
size_t sz;
{
	register int cmp;
	size_t i = 1;

	if (sz == 0)
		return 0;
	do
		if ((cmp = lowerconv(*s1 & 0377) - lowerconv(*s2 & 0377)) != 0)
			return cmp;
	while (i++ < sz && *s1++ != '\0' && *s2++ != '\0');
	return 0;
}

const unsigned char class_char[] = {
/*	000 nul	001 soh	002 stx	003 etx	004 eot	005 enq	006 ack	007 bel	*/
	C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,
/*	010 bs 	011 ht 	012 nl 	013 vt 	014 np 	015 cr 	016 so 	017 si 	*/
	C_CNTRL,C_BLANK,C_WHITE,C_SPACE,C_SPACE,C_SPACE,C_CNTRL,C_CNTRL,
/*	020 dle	021 dc1	022 dc2	023 dc3	024 dc4	025 nak	026 syn	027 etb	*/
	C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,
/*	030 can	031 em 	032 sub	033 esc	034 fs 	035 gs 	036 rs 	037 us 	*/
	C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,
/*	040 sp 	041  ! 	042  " 	043  # 	044  $ 	045  % 	046  & 	047  ' 	*/
	C_BLANK,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,
/*	050  ( 	051  ) 	052  * 	053  + 	054  , 	055  - 	056  . 	057  / 	*/
	C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,
/*	060  0 	061  1 	062  2 	063  3 	064  4 	065  5 	066  6 	067  7 	*/
	C_DIGIT,C_DIGIT,C_DIGIT,C_DIGIT,C_DIGIT,C_DIGIT,C_DIGIT,C_DIGIT,
/*	070  8 	071  9 	072  : 	073  ; 	074  < 	075  = 	076  > 	077  ? 	*/
	C_DIGIT,C_DIGIT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,
/*	100  @ 	101  A 	102  B 	103  C 	104  D 	105  E 	106  F 	107  G 	*/
	C_PUNCT,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,
/*	110  H 	111  I 	112  J 	113  K 	114  L 	115  M 	116  N 	117  O 	*/
	C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,
/*	120  P 	121  Q 	122  R 	123  S 	124  T 	125  U 	126  V 	127  W 	*/
	C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,
/*	130  X 	131  Y 	132  Z 	133  [ 	134  \ 	135  ] 	136  ^ 	137  _ 	*/
	C_UPPER,C_UPPER,C_UPPER,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,
/*	140  ` 	141  a 	142  b 	143  c 	144  d 	145  e 	146  f 	147  g 	*/
	C_PUNCT,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,
/*	150  h 	151  i 	152  j 	153  k 	154  l 	155  m 	156  n 	157  o 	*/
	C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,
/*	160  p 	161  q 	162  r 	163  s 	164  t 	165  u 	166  v 	167  w 	*/
	C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,
/*	170  x 	171  y 	172  z 	173  { 	174  | 	175  } 	176  ~ 	177 del	*/
	C_LOWER,C_LOWER,C_LOWER,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_CNTRL
};
