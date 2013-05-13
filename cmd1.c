/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ User commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
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

#include "rcv.h"

#ifdef HAVE_WCWIDTH
# include <wchar.h>
#endif

#include "extern.h"

/*
 * Print the current active headings.
 * Don't change dot if invoker didn't give an argument.
 */

static int screen;

/* Prepare and print "[Message: xy]:" intro */
static void	_show_msg_overview(struct message *mp, int msg_no, FILE *obuf);

static void onpipe(int signo);
static int dispc(struct message *mp, const char *a);
static int scroll1(char *arg, int onlynew);

static void	_parse_head(struct message *mp, char date[FROM_DATEBUF]);
static void hprf(const char *fmt, int mesg, FILE *f, int threaded,
		const char *attrlist);
static int putindent(FILE *fp, struct message *mp, int maxwidth);
static int type1(int *msgvec, int doign, int page, int pipe, int decode,
		char *cmd, off_t *tstats);
static int pipe1(char *str, int doign);

static void
_show_msg_overview(struct message *mp, int msg_no, FILE *obuf)
{
	fprintf(obuf, tr(17, "[-- Message %2d -- %lu lines, %lu bytes --]:\n"),
		msg_no, (ul_it)mp->m_lines, (ul_it)mp->m_size);
}

int
ccmdnotsupp(void *v)
{
	(void)v;
	fprintf(stderr, tr(10, "The requested feature is not compiled in\n"));
	return (1);
}

char const *
get_pager(void)
{
	char const *cp;

	cp = value("PAGER");
	if (cp == NULL || *cp == '\0')
		cp = value("bsdcompat") ? PAGER_BSD : PAGER_SYSV;
	return cp;
}

int 
headers(void *v)
{
	int *msgvec = v;
	int g, k, n, mesg, flag = 0, lastg = 1;
	struct message *mp, *mq, *lastmq = NULL;
	int size;
	enum mflag	fl = MNEW|MFLAGGED;

	time_current_update(&time_current, FAL0);

	size = screensize();
	n = msgvec[0];	/* n == {-2, -1, 0}: called from scroll() */
	if (screen < 0)
		screen = 0;
	k = screen * size;
	if (k >= msgCount)
		k = msgCount - size;
	if (k < 0)
		k = 0;
	if (mb.mb_threaded == 0) {
		g = 0;
		mq = &message[0];
		for (mp = &message[0]; mp < &message[msgCount]; mp++)
			if (visible(mp)) {
				if (g % size == 0)
					mq = mp;
				if (mp->m_flag&fl) {
					lastg = g;
					lastmq = mq;
				}
				if ((n > 0 && mp == &message[n-1]) ||
						(n == 0 && g == k) ||
						(n == -2 && g == k + size &&
						 lastmq) ||
						(n < 0 && g >= k &&
						 (mp->m_flag & fl) != 0))
					break;
				g++;
			}
		if (lastmq && (n==-2 || (n==-1 && mp == &message[msgCount]))) {
			g = lastg;
			mq = lastmq;
		}
		screen = g / size;
		mp = mq;
		mesg = mp - &message[0];
		if (dot != &message[n-1]) {
			for (mq = mp; mq < &message[msgCount]; mq++)
				if (visible(mq)) {
					setdot(mq);
					break;
				}
		}
#ifdef USE_IMAP
		if (mb.mb_type == MB_IMAP)
			imap_getheaders(mesg+1, mesg + size);
#endif
		for (; mp < &message[msgCount]; mp++) {
			mesg++;
			if (!visible(mp))
				continue;
			if (flag++ >= size)
				break;
			printhead(mesg, stdout, 0);
		}
	} else {	/* threaded */
		g = 0;
		mq = threadroot;
		for (mp = threadroot; mp; mp = next_in_thread(mp))
			if (visible(mp) && (mp->m_collapsed <= 0 ||
					 mp == &message[n-1])) {
				if (g % size == 0)
					mq = mp;
				if (mp->m_flag&fl) {
					lastg = g;
					lastmq = mq;
				}
				if ((n > 0 && mp == &message[n-1]) ||
						(n == 0 && g == k) ||
						(n == -2 && g == k + size &&
						 lastmq) ||
						(n < 0 && g >= k &&
						 (mp->m_flag & fl) != 0))
					break;
				g++;
			}
		if (lastmq && (n==-2 || (n==-1 && mp==&message[msgCount]))) {
			g = lastg;
			mq = lastmq;
		}
		screen = g / size;
		mp = mq;
		if (dot != &message[n-1]) {
			for (mq = mp; mq; mq = next_in_thread(mq))
				if (visible(mq) && mq->m_collapsed <= 0) {
					setdot(mq);
					break;
				}
		}
		while (mp) {
			if (visible(mp) && (mp->m_collapsed <= 0 ||
					 mp == &message[n-1])) {
				if (flag++ >= size)
					break;
				printhead(mp - &message[0] + 1, stdout,
						mb.mb_threaded);
			}
			mp = next_in_thread(mp);
		}
	}
	if (flag == 0) {
		printf(tr(6, "No more mail.\n"));
		return(1);
	}
	return(0);
}

/*
 * Scroll to the next/previous screen
 */
int
scroll(void *v)
{
	return scroll1(v, 0);
}

int
Scroll(void *v)
{
	return scroll1(v, 1);
}

static int
scroll1(char *arg, int onlynew)
{
	int size;
	int cur[1];

	cur[0] = onlynew ? -1 : 0;
	size = screensize();
	switch (*arg) {
	case '1': case '2': case '3': case '4': case '5':
	case '6': case '7': case '8': case '9': case '0':
		screen = atoi(arg);
		goto scroll_forward;
	case '\0':
		screen++;
		goto scroll_forward;
	case '$':
		screen = msgCount / size;
		goto scroll_forward;
	case '+':
		if (arg[1] == '\0')
			screen++;
		else
			screen += atoi(arg + 1);
scroll_forward:
		if (screen * size > msgCount) {
			screen = msgCount / size;
			printf(catgets(catd, CATSET, 7,
					"On last screenful of messages\n"));
		}
		break;

	case '-':
		if (arg[1] == '\0')
			screen--;
		else
			screen -= atoi(arg + 1);
		if (screen < 0) {
			screen = 0;
			printf(catgets(catd, CATSET, 8,
					"On first screenful of messages\n"));
		}
		if (cur[0] == -1)
			cur[0] = -2;
		break;

	default:
		printf(catgets(catd, CATSET, 9,
			"Unrecognized scrolling command \"%s\"\n"), arg);
		return(1);
	}
	return(headers(cur));
}

/*
 * Compute screen size.
 */
int 
screensize(void)
{
	int s;
	char *cp;

	if ((cp = value("screen")) != NULL && (s = atoi(cp)) > 0)
		return s;
	return scrnheight - 4;
}

static sigjmp_buf	pipejmp;

/*ARGSUSED*/
static void 
onpipe(int signo)
{
	(void)signo;
	siglongjmp(pipejmp, 1);
}

/*
 * Print out the headlines for each message
 * in the passed message list.
 */
int 
from(void *v)
{
	int *msgvec = v, *ip, n;
	char *cp;
	FILE *volatile obuf = stdout;

	time_current_update(&time_current, FAL0);

	/* TODO unfixable memory leaks still */
	if (is_a_tty[0] && is_a_tty[1] && (cp = value("crt")) != NULL) {
		for (n = 0, ip = msgvec; *ip; ip++)
			n++;
		if (n > (*cp == '\0' ? screensize() : atoi((char*)cp)) + 3) {
			char const *p;
			if (sigsetjmp(pipejmp, 1))
				goto endpipe;
			p = get_pager();
			if ((obuf = Popen(p, "w", NULL, 1)) == NULL) {
				perror(p);
				obuf = stdout;
				cp=NULL;
			} else
				safe_signal(SIGPIPE, onpipe);
		}
	}
	for (ip = msgvec; *ip != 0; ip++)
		printhead(*ip, obuf, mb.mb_threaded);
	if (--ip >= msgvec)
		setdot(&message[*ip - 1]);
endpipe:
	if (obuf != stdout) {
		safe_signal(SIGPIPE, SIG_IGN);
		Pclose(obuf, TRU1);
		safe_signal(SIGPIPE, dflpipe);
	}
	return(0);
}

static int 
dispc(struct message *mp, const char *a)
{
	int	dispc = ' ';

	/*
	 * Bletch!
	 */
	if ((mp->m_flag & (MREAD|MNEW)) == MREAD)
		dispc = a[3];
	if ((mp->m_flag & (MREAD|MNEW)) == (MREAD|MNEW))
		dispc = a[2];
	if (mp->m_flag & MANSWERED)
		dispc = a[8];
	if (mp->m_flag & MDRAFTED)
		dispc = a[9];
	if ((mp->m_flag & (MREAD|MNEW)) == MNEW)
		dispc = a[0];
	if ((mp->m_flag & (MREAD|MNEW)) == 0)
		dispc = a[1];
	if (mp->m_flag & MJUNK)
		dispc = a[13];
	if (mp->m_flag & MSAVED)
		dispc = a[4];
	if (mp->m_flag & MPRESERVE)
		dispc = a[5];
	if (mp->m_flag & (MBOX|MBOXED))
		dispc = a[6];
	if (mp->m_flag & MFLAGGED)
		dispc = a[7];
	if (mp->m_flag & MKILL)
		dispc = a[10];
	if (mb.mb_threaded == 1 && mp->m_collapsed > 0)
		dispc = a[12];
	if (mb.mb_threaded == 1 && mp->m_collapsed < 0)
		dispc = a[11];
	return dispc;
}

static void
_parse_head(struct message *mp, char date[FROM_DATEBUF])
{
	FILE *ibuf;
	int hlen;
	char *hline = NULL;
	size_t hsize = 0;

	if ((ibuf = setinput(&mb, mp, NEED_HEADER)) != NULL &&
			(hlen = readline(ibuf, &hline, &hsize)) > 0)
		(void)extract_date_from_from_(hline, hlen, date);
	if (hline != NULL)
		free(hline);
}

static void
hprf(const char *fmt, int mesg, FILE *f, int threaded, const char *attrlist)
{
	char datebuf[FROM_DATEBUF], *subjline, *cp;
	struct str in, out;
	char const *datefmt, *date, *name, *fp;
	int B, c, i, n, s, fromlen, subjlen = scrnwidth, isto = 0, isaddr = 0;
	struct message *mp = &message[mesg - 1];
	time_t datet = mp->m_time;

	date = NULL;
	if ((datefmt = value("datefield")) != NULL) {
		fp = hfield1("date", mp);/* TODO use m_date field! */
		if (fp == NULL) {
			datefmt = NULL;
			goto jdate_set;
		}
		datet = rfctime(fp);
		date = fakedate(datet);
		fp = value("datefield-markout-older");
		i = (*datefmt != '\0');
		if (fp != NULL)
			i |= (*fp != '\0') ? 2 | 4 : 2;
		/* May we strftime(3)? */
		if (i & (1 | 4))
			memcpy(&time_current.tc_local, localtime(&datet),
				sizeof time_current.tc_local);
		if ((i & 2) && (datet > time_current.tc_time ||
#define _6M	((DATE_DAYSYEAR / 2) * DATE_SECSDAY)
				(datet + _6M < time_current.tc_time))) {
#undef _6M
			if ((datefmt = (i & 4) ? fp : NULL) == NULL) {
				memset(datebuf, ' ', FROM_DATEBUF); /* xxx ur */
				memcpy(datebuf + 4, date + 4, 7);
				datebuf[4 + 7] = ' ';
				memcpy(datebuf + 4 + 7 + 1, date + 20, 4);
				datebuf[4 + 7 + 1 + 4] = '\0';
				date = datebuf;
			}
		} else if ((i & 1) == 0)
			datefmt = NULL;
	} else if (datet == (time_t)0 && (mp->m_flag & MNOFROM) == 0) {
		/* TODO eliminate this path, query the FROM_ date in setptr(),
		 * TODO all other codepaths do so by themselves ALREADY ?????
		 * TODO assert(mp->m_time != 0);, then
		 * TODO ALSO changes behaviour of markout-non-current */
		_parse_head(mp, datebuf);
		date = datebuf;
	} else {
jdate_set:
		date = fakedate(datet);
	}

	out.s = NULL;
	out.l = 0;
	if ((subjline = hfield1("subject", mp)) != NULL) {
		in.s = subjline;
		in.l = strlen(subjline);
		mime_fromhdr(&in, &out, TD_ICONV | TD_ISPR);
		subjline = out.s;
	}

	if (options & OPT_I_FLAG) {
		if ((name = hfieldX("newsgroups", mp)) == NULL)
			if ((name = hfieldX("article-id", mp)) == NULL)
				name = "<>";
		name = prstr(name);
	} else if (value("show-rcpt") == NULL) {
		name = name1(mp, 0);
		isaddr = 1;
		if (value("showto") && name && is_myname(skin(name))) {
			if ((cp = hfield1("to", mp)) != NULL) {
				name = cp;
				isto = 1;
			}
		}
	} else {
		isaddr = 1;
		if ((name = hfield1("to", mp)) != NULL)
			isto = 1;
	}
	if (name == NULL) {
		name = "";
		isaddr = 0;
	}
	if (isaddr) {
		if (value("showname"))
			name = realname(name);
		else {
			name = prstr(skin(name));
		}
	}

	for (fp = fmt; *fp; fp++) {
		if (*fp == '%') {
			if (*++fp == '-') {
				fp++;
			} else if (*fp == '+')
				fp++;
			while (digitchar(*fp))
				fp++;
			if (*fp == '\0')
				break;
		} else {
#if defined (HAVE_MBTOWC) && defined (HAVE_WCWIDTH)
			if (mb_cur_max > 1) {
				wchar_t	wc;
				if ((s = mbtowc(&wc, fp, mb_cur_max)) < 0)
					n = s = 1;
				else {
					if ((n = wcwidth(wc)) < 0)
						n = 1;
				}
			} else
#endif  /* HAVE_MBTOWC && HAVE_WCWIDTH */
			{
				n = s = 1;
			}
			subjlen -= n;
			while (--s > 0)
				fp++;
		}
	}

	for (fp = fmt; *fp; fp++) {
		if ((c = *fp & 0xFF) == '%') {
			B = 0;
			n = 0;
			s = 1;
			if (*++fp == '-') {
				s = -1;
				fp++;
			} else if (*fp == '+')
				fp++;
			if (digitchar(*fp)) {
				do
					n = 10*n + *fp - '0';
				while (fp++, digitchar(*fp));
			}
			if (*fp == '\0')
				break;
			n *= s;
			switch ((c = *fp & 0xFF)) {
			case '%':
				goto jputc;
			case '>':
			case '<':
				if (dot != mp)
					c = ' ';
				goto jputc;
			case 'a':
				c = dispc(mp, attrlist);
jputc:
				n = fprintf(f, "%*c", n, c);
				if (n >= 0)
					subjlen -= n;
				break;
			case 'm':
				if (n == 0) {
					n = 3;
					if (threaded)
						for (i=msgCount; i>999; i/=10)
							n++;
				}
				subjlen -= fprintf(f, "%*d", n, mesg);
				break;
			case 'f':
				if (n == 0) {
					n = 18;
					if (s < 0)
						n = -n;
				}
				fromlen = ABS(n);
				if (isto) /* XXX tr()! */
					fromlen -= 3;
				subjlen -= fprintf(f, "%s%s", isto ? "To " : "",
						colalign(name, fromlen, n));
				break;
			case 'd':
				if (datefmt != NULL) {
					i = strftime(datebuf, sizeof datebuf,
						datefmt,
						&time_current.tc_local);
					if (i != 0)
						date = datebuf;
					else
						fprintf(stderr, tr(174,
							"Ignored date format, "
							"it excesses the "
							"target buffer "
							"(%lu bytes)\n"),
							(ul_it)sizeof datebuf);
					datefmt = NULL;
				}
				if (n == 0)
					n = 16;
				subjlen -= fprintf(f, "%*.*s", n, n, date);
				break;
			case 'l':
				if (n == 0)
					n = 4;
				if (mp->m_xlines)
					subjlen -= fprintf(f, "%*ld", n,
							mp->m_xlines);
				else {
					n = ABS(n);
					subjlen -= n;
					while (n--)
						putc(' ', f);
				}
				break;
			case 'o':
				if (n == 0)
					n = -5;
				subjlen -= fprintf(f, "%*lu", n,
						(long)mp->m_xsize);
				break;
			case 'i':
				if (threaded)
					subjlen -= putindent(f, mp,
							scrnwidth - 60);
				break;
			case 'S':
				B = 1;
				/*FALLTHRU*/
			case 's':
				if (n == 0)
					n = subjlen - 2;
				if (n > 0 && s < 0)
					n = -n;
				if (B)
					n -= (n < 0) ? -2 : 2;
				if (subjline != NULL && n != 0) {
					/* pretty pathetic */
					fprintf(f, B ? "\"%s\"" : "%s",
						colalign(subjline, ABS(n), n));
				}
				break;
			case 'U':
#ifdef USE_IMAP
				if (n == 0)
					n = 9;
				subjlen -= fprintf(f, "%*lu", n, mp->m_uid);
				break;
#else
				c = '?';
				goto jputc;
#endif
			case 'e':
				if (n == 0)
					n = 2;
				subjlen -= fprintf(f, "%*u", n, threaded == 1 ?
						mp->m_level : 0);
				break;
			case 't':
				if (n == 0) {
					n = 3;
					if (threaded)
						for (i=msgCount; i>999; i/=10)
							n++;
				}
				subjlen -= fprintf(f, "%*ld", n, threaded ?
						mp->m_threadpos : mesg);
				break;
			case 'c':
#ifdef USE_SCORE
				if (n == 0)
					n = 6;
				subjlen -= fprintf(f, "%*g", n, mp->m_score);
				break;
#else
				c = '?';
				goto jputc;
#endif
			}
		} else
			putc(c, f);
	}
	putc('\n', f);

	if (out.s)
		free(out.s);
}

/*
 * Print out the indenting in threaded display.
 */
static int
putindent(FILE *fp, struct message *mp, int maxwidth)
{
	struct message	*mq;
	int	indent, i;
	int	*us;
	char	*cs;
	int	important = MNEW|MFLAGGED;

	if (mp->m_level == 0)
		return 0;
	cs = ac_alloc(mp->m_level);
	us = ac_alloc(mp->m_level * sizeof *us);
	i = mp->m_level - 1;
	if (mp->m_younger && (unsigned)i + 1 == mp->m_younger->m_level) {
		if (mp->m_parent && mp->m_parent->m_flag & important)
			us[i] = mp->m_flag & important ? 0x2523 : 0x2520;
		else
			us[i] = mp->m_flag & important ? 0x251D : 0x251C;
		cs[i] = '+';
	} else {
		if (mp->m_parent && mp->m_parent->m_flag & important)
			us[i] = mp->m_flag & important ? 0x2517 : 0x2516;
		else
			us[i] = mp->m_flag & important ? 0x2515 : 0x2514;
		cs[i] = '\\';
	}
	mq = mp->m_parent;
	for (i = mp->m_level - 2; i >= 0; i--) {
		if (mq) {
			if ((unsigned)i > mq->m_level - 1) {
				us[i] = cs[i] = ' ';
				continue;
			}
			if (mq->m_younger) {
				if (mq->m_parent &&
						mq->m_parent->m_flag&important)
					us[i] = 0x2503;
				else
					us[i] = 0x2502;
				cs[i] = '|';
			} else
				us[i] = cs[i] = ' ';
			mq = mq->m_parent;
		} else
			us[i] = cs[i] = ' ';
	}
	for (indent = 0; (unsigned)indent < mp->m_level && indent < maxwidth;
			++indent) {
		if (indent < maxwidth - 1)
			putuc(us[indent], cs[indent] & 0377, fp);
		else
			putuc(0x21B8, '^', fp);
	}
	ac_free(us);
	ac_free(cs);
	return indent;
}

void
printhead(int mesg, FILE *f, int threaded)
{
	int bsdflags, bsdheadline, sz;
	char attrlist[30], *cp;
	char const *fmt;

	bsdflags = value("bsdcompat") != NULL || value("bsdflags") != NULL ||
		getenv("SYSV3") != NULL;
	strcpy(attrlist, bsdflags ? "NU  *HMFATK+-J" : "NUROSPMFATK+-J");
	if ((cp = value("attrlist")) != NULL) {
		sz = strlen(cp);
		if (sz > (int)sizeof attrlist - 1)
			sz = (int)sizeof attrlist - 1;
		memcpy(attrlist, cp, sz);
	}
	bsdheadline = value("bsdcompat") != NULL ||
		value("bsdheadline") != NULL;
	if ((fmt = value("headline")) == NULL)
		fmt = bsdheadline ?
			"%>%a%m %-20f  %16d %3l/%-5o %i%-S" :
			"%>%a%m %-18f %16d %4l/%-5o %i%-s";
	hprf(fmt, mesg, f, threaded, attrlist);
}

/*
 * Print out the value of dot.
 */
/*ARGSUSED*/
int 
pdot(void *v)
{
	(void)v;
	printf(catgets(catd, CATSET, 13, "%d\n"),
			(int)(dot - &message[0] + 1));
	return(0);
}

/*
 * Print out all the possible commands.
 */

static int
_pcmd_cmp(void const *s1, void const *s2)
{
	struct cmd const *const*c1 = s1, *const*c2 = s2;
	return (strcmp((*c1)->c_name, (*c2)->c_name));
}

/*ARGSUSED*/
int 
pcmdlist(void *v)
{
	extern struct cmd const cmdtab[];
	struct cmd const **cpa, *cp, **cursor;
	size_t i;
	(void)v;

	for (i = 0; cmdtab[i].c_name != NULL; ++i)
		;
	++i;
	cpa = ac_alloc(sizeof(cp) * i);

	for (i = 0; (cp = cmdtab + i)->c_name != NULL; ++i)
		cpa[i] = cp;
	cpa[i] = NULL;

	qsort(cpa, i, sizeof(cp), &_pcmd_cmp);

	printf(tr(14, "Commands are:\n"));
	for (i = 0, cursor = cpa; (cp = *cursor++) != NULL;) {
		size_t j;
		if (cp->c_func == &ccmdnotsupp)
			continue;
		j = strlen(cp->c_name) + 2;
		if ((i += j) > 72) {
			i = j;
			printf("\n");
		}
		printf((*cursor != NULL ? "%s, " : "%s\n"), cp->c_name);
	}

	ac_free(cpa);
	return (0);
}

/*
 * Type out the messages requested.
 */
static sigjmp_buf	pipestop;

/*ARGSUSED*/
static void
brokpipe(int signo)
{
	(void)signo;
	siglongjmp(pipestop, 1);
}

static int
type1(int *msgvec, int doign, int page, int pipe, int decode,
		char *cmd, off_t *tstats)
{
	int *ip;
	struct message *mp;
	char const *cp;
	int nlines;
	off_t mstats[2];
	FILE *volatile obuf;

	obuf = stdout;
	if (sigsetjmp(pipestop, 1))
		goto close_pipe;
	if (pipe) {
		cp = value("SHELL");
		if (cp == NULL)
			cp = SHELL;
		obuf = Popen(cmd, "w", cp, 1);
		if (obuf == NULL) {
			perror(cmd);
			obuf = stdout;
		} else {
			safe_signal(SIGPIPE, brokpipe);
		}
	} else if (value("interactive") != NULL &&
	    (page || (cp = value("crt")) != NULL)) {
		nlines = 0;
		if (!page) {
			for (ip = msgvec; *ip && ip-msgvec < msgCount; ip++) {
				if ((message[*ip-1].m_have & HAVE_BODY) == 0) {
					if ((get_body(&message[*ip - 1])) !=
							OKAY)
						return 1;
				}
				nlines += message[*ip - 1].m_lines;
			}
		}
		if (page || nlines > (*cp ? atoi(cp) : realscreenheight)) {
			char const *p = get_pager();
			obuf = Popen(p, "w", NULL, 1);
			if (obuf == NULL) {
				perror(p);
				obuf = stdout;
			} else
				safe_signal(SIGPIPE, brokpipe);
		}
	}
	for (ip = msgvec; *ip && ip - msgvec < msgCount; ip++) {
		mp = &message[*ip - 1];
		touch(mp);
		setdot(mp);
		uncollapse1(mp, 1);
		if (! pipe && ip != msgvec)
			fprintf(obuf, "\n");
		_show_msg_overview(mp, *ip, obuf);
		send(mp, obuf, doign ? ignore : 0, NULL,
			pipe && value("piperaw") ? SEND_MBOX :
				decode ? SEND_SHOW :
				doign ? SEND_TODISP : SEND_TODISP_ALL,
			mstats);
		if (pipe && value("page")) {
			putc('\f', obuf);
		}
		if (tstats) {
			tstats[0] += mstats[0];
			tstats[1] += mstats[1];
		}
	}
close_pipe:
	if (obuf != stdout) {
		/*
		 * Ignore SIGPIPE so it can't cause a duplicate close.
		 */
		safe_signal(SIGPIPE, SIG_IGN);
		Pclose(obuf, TRU1);
		safe_signal(SIGPIPE, dflpipe);
	}
	return(0);
}

/*
 * Get the last, possibly quoted part of linebuf.
 */
char *
laststring(char *linebuf, int *flag, int strip)
{
	char *cp, *p;
	char quoted;

	*flag = 1;
	cp = strlen(linebuf) + linebuf - 1;

	/*
	 * Strip away trailing blanks.
	 */
	while (cp > linebuf && whitechar(*cp & 0377))
		cp--;
	*++cp = 0;
	if (cp == linebuf) {
		*flag = 0;
		return NULL;
	}

	/*
	 * Now search for the beginning of the command name.
	 */
	quoted = *(cp - 1);
	if (quoted == '\'' || quoted == '\"') {
		cp--;
		if (strip)
			*cp = '\0';
		cp--;
		while (cp > linebuf) {
			if (*cp != quoted) {
				cp--;
			} else if (*(cp - 1) != '\\') {
				break;
			} else {
				p = --cp;
				do {
					*p = *(p + 1);
				} while (*p++);
				cp--;
			}
		}
		if (cp == linebuf)
			*flag = 0;
		if (*cp == quoted) {
			if (strip)
				*cp++ = 0;
		} else
			*flag = 0;
	} else {
		while (cp > linebuf && !whitechar(*cp & 0377))
			cp--;
		if (whitechar(*cp & 0377))
			*cp++ = 0;
		else
			*flag = 0;
	}
	if (*cp == '\0') {
		return(NULL);
	}
	return(cp);
}

/*
 * Pipe the messages requested.
 */
static int 
pipe1(char *str, int doign)
{
	char *cmd;
	int f, *msgvec, ret;
	off_t stats[2];

	/*LINTED*/
	msgvec = (int *)salloc((msgCount + 2) * sizeof *msgvec);
	if ((cmd = laststring(str, &f, 1)) == NULL) {
		cmd = value("cmd");
		if (cmd == NULL || *cmd == '\0') {
			fputs(catgets(catd, CATSET, 16,
				"variable cmd not set\n"), stderr);
			return 1;
		}
	}
	if (!f) {
		*msgvec = first(0, MMNORM);
		if (*msgvec == 0) {
			if (inhook)
				return 0;
			puts(catgets(catd, CATSET, 18, "No messages to pipe."));
			return 1;
		}
		msgvec[1] = 0;
	} else if (getmsglist(str, msgvec, 0) < 0)
		return 1;
	if (*msgvec == 0) {
		if (inhook)
			return 0;
		printf("No applicable messages.\n");
		return 1;
	}
	printf(catgets(catd, CATSET, 268, "Pipe to: \"%s\"\n"), cmd);
	stats[0] = stats[1] = 0;
	if ((ret = type1(msgvec, doign, 0, 1, 0, cmd, stats)) == 0) {
		printf("\"%s\" ", cmd);
		if (stats[0] >= 0)
			printf("%lu", (long)stats[0]);
		else
			printf(catgets(catd, CATSET, 27, "binary"));
		printf("/%lu\n", (long)stats[1]);
	}
	return ret;
}

/*
 * Paginate messages, honor ignored fields.
 */
int 
more(void *v)
{
	int *msgvec = v;
	return (type1(msgvec, 1, 1, 0, 0, NULL, NULL));
}

/*
 * Paginate messages, even printing ignored fields.
 */
int 
More(void *v)
{
	int *msgvec = v;

	return (type1(msgvec, 0, 1, 0, 0, NULL, NULL));
}

/*
 * Type out messages, honor ignored fields.
 */
int 
type(void *v)
{
	int *msgvec = v;

	return(type1(msgvec, 1, 0, 0, 0, NULL, NULL));
}

/*
 * Type out messages, even printing ignored fields.
 */
int 
Type(void *v)
{
	int *msgvec = v;

	return(type1(msgvec, 0, 0, 0, 0, NULL, NULL));
}

/*
 * Show MIME-encoded message text, including all fields.
 */
int
show(void *v)
{
	int *msgvec = v;

	return(type1(msgvec, 0, 0, 0, 1, NULL, NULL));
}

/*
 * Pipe messages, honor ignored fields.
 */
int 
pipecmd(void *v)
{
	char *str = v;
	return(pipe1(str, 1));
}
/*
 * Pipe messages, not respecting ignored fields.
 */
int 
Pipecmd(void *v)
{
	char *str = v;
	return(pipe1(str, 0));
}

/*
 * Print the top so many lines of each desired message.
 * The number of lines is taken from the variable "toplines"
 * and defaults to 5.
 */
int 
top(void *v)
{
	int *msgvec = v, *ip, c, topl, lines, empty_last;
	struct message *mp;
	char *cp, *linebuf = NULL;
	size_t linesize;
	FILE *ibuf;

	topl = 5;
	cp = value("toplines");
	if (cp != NULL) {
		topl = atoi(cp);
		if (topl < 0 || topl > 10000)
			topl = 5;
	}
	empty_last = 1;
	for (ip = msgvec; *ip && ip-msgvec < msgCount; ip++) {
		mp = &message[*ip - 1];
		touch(mp);
		setdot(mp);
		did_print_dot = TRU1;
		if (! empty_last)
			printf("\n");
		_show_msg_overview(mp, *ip, stdout);
		if (mp->m_flag & MNOFROM)
			printf("From %s %s\n", fakefrom(mp),
				fakedate(mp->m_time));
		if ((ibuf = setinput(&mb, mp, NEED_BODY)) == NULL) {	/* XXX could use TOP */
			v = NULL;
			break;
		}
		c = mp->m_lines;
		for (lines = 0; lines < c && lines <= topl; lines++) {
			if (readline(ibuf, &linebuf, &linesize) < 0)
				break;
			puts(linebuf);

			for (cp = linebuf; *cp && blankchar(*cp); ++cp)
				;
			empty_last = (*cp == '\0');
		}
	}

	if (linebuf != NULL)
		free(linebuf);
	return (v != NULL);
}

/*
 * Touch all the given messages so that they will
 * get mboxed.
 */
int 
stouch(void *v)
{
	int *msgvec = v;
	int *ip;

	for (ip = msgvec; *ip != 0; ip++) {
		setdot(&message[*ip-1]);
		dot->m_flag |= MTOUCH;
		dot->m_flag &= ~MPRESERVE;
		/*
		 * POSIX interpretation necessary.
		 */
		did_print_dot = TRU1;
	}
	return(0);
}

/*
 * Make sure all passed messages get mboxed.
 */
int 
mboxit(void *v)
{
	int *msgvec = v;
	int *ip;

	for (ip = msgvec; *ip != 0; ip++) {
		setdot(&message[*ip-1]);
		dot->m_flag |= MTOUCH|MBOX;
		dot->m_flag &= ~MPRESERVE;
		/*
		 * POSIX interpretation necessary.
		 */
		did_print_dot = TRU1;
	}
	return(0);
}

/*
 * List the folders the user currently has.
 */
int 
folders(void *v)
{
	char dirname[MAXPATHLEN], *name, **argv = v;
	char const *cmd;

	if (*argv) {
		name = expand(*argv);
		if (name == NULL)
			return 1;
	} else if (getfold(dirname, sizeof dirname) < 0) {
		fprintf(stderr, tr(20, "No value set for \"folder\"\n"));
		return 1;
	} else
		name = dirname;

	if (which_protocol(name) == PROTO_IMAP) {
#ifdef USE_IMAP
		imap_folders(name, *argv == NULL);
#else
		return ccmdnotsupp(NULL);
#endif
	} else {
		if ((cmd = value("LISTER")) == NULL)
			cmd = LISTER;
		run_command(cmd, 0, -1, -1, name, NULL, NULL);
	}
	return 0;
}
