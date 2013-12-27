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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/*
 * Print the current active headings.
 * Don't change dot if invoker didn't give an argument.
 */

static int screen;

/* Prepare and print "[Message: xy]:" intro */
static void	_show_msg_overview(struct message *mp, int msg_no, FILE *obuf);

/* ... And place the extracted date in `date' */
static void	_parse_from_(struct message *mp, char date[FROM_DATEBUF]);

/* Print out the header of a specific message
 * __hprf: handle *headline*
 * __subject: Subject:, but return NULL if threaded and Subject: yet seen
 * __putindent: print out the indenting in threaded display */
static void	_print_head(size_t yetprinted, int msgno, FILE *f,
			bool_t threaded);
static void	__hprf(size_t yetprinted, const char *fmt, int mesg, FILE *f,
			bool_t threaded, const char *attrlist);
static char *	__subject(struct message *mp, bool_t threaded,
			size_t yetprinted);
static char *	__subject_trim(char *s);
static int	__putindent(FILE *fp, struct message *mp, int maxwidth);

static void _cmd1_onpipe(int signo);
static int _dispc(struct message *mp, const char *a);
static int scroll1(char *arg, int onlynew);

static int	_type1(int *msgvec, bool_t doign, bool_t dopage, bool_t dopipe,
			bool_t dodecode, char *cmd, off_t *tstats);
static int pipe1(char *str, int doign);

static void
_show_msg_overview(struct message *mp, int msg_no, FILE *obuf)
{
	fprintf(obuf, tr(17, "[-- Message %2d -- %lu lines, %lu bytes --]:\n"),
		msg_no, (ul_it)mp->m_lines, (ul_it)mp->m_size);
}

static void
_parse_from_(struct message *mp, char date[FROM_DATEBUF])
{
	FILE *ibuf;
	int hlen;
	char *hline = NULL;
	size_t hsize = 0;

	if ((ibuf = setinput(&mb, mp, NEED_HEADER)) != NULL &&
			(hlen = readline_restart(ibuf, &hline, &hsize, 0)) > 0)
		(void)extract_date_from_from_(hline, hlen, date);
	if (hline != NULL)
		free(hline);
}

static void
_print_head(size_t yetprinted, int msgno, FILE *f, bool_t threaded)
{
	char attrlist[30], *cp;
	char const *fmt;

	if ((cp = voption("attrlist")) != NULL) {
		size_t i = strlen(cp);
		if (UICMP(32, i, >, sizeof attrlist - 1))
			i = (int)sizeof attrlist - 1;
		memcpy(attrlist, cp, i);
	} else if (boption("bsdcompat") || boption("bsdflags") ||
			getenv("SYSV3") != NULL) {
		char const bsdattr[] = "NU  *HMFAT+-$";
		memcpy(attrlist, bsdattr, sizeof bsdattr - 1);
	} else {
		char const pattr[] = "NUROSPMFAT+-$";
		memcpy(attrlist, pattr, sizeof pattr - 1);
	}

	if ((fmt = voption("headline")) == NULL) {
		fmt = ((boption("bsdcompat") || boption("bsdheadline"))
			? "%>%a%m %-20f  %16d %3l/%-5o %i%-S"
			: "%>%a%m %-18f %16d %4l/%-5o %i%-s");
	}

	__hprf(yetprinted, fmt, msgno, f, threaded, attrlist);
}

static void
__hprf(size_t yetprinted, char const *fmt, int msgno, FILE *f, bool_t threaded,
	char const *attrlist)
{
	char datebuf[FROM_DATEBUF], *cp, *subjline;
	char const *datefmt, *date, *name, *fp;
	int B, c, i, n, s, wleft, subjlen, isto = 0, isaddr = 0;
	struct message *mp = &message[msgno - 1];
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
		if ((i & 2) &&
				/* TODO *datefield-markout-older* we accept
				 * TODO one day in the future, should be UTC
				 * TODO offset only?  and Stephen Isard had
				 * TODO one week once he proposed the patch! */
				(datet > time_current.tc_time + DATE_SECSDAY ||
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
		_parse_from_(mp, datebuf);
		date = datebuf;
	} else {
jdate_set:
		date = fakedate(datet);
	}

	isaddr = 1;
	name = name1(mp, 0);
	if (name != NULL && value("showto") && is_myname(skin(name))) {
		if ((cp = hfield1("to", mp)) != NULL) {
			name = cp;
			isto = 1;
		}
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

	subjline = NULL;

	/* Detect the width of the non-format characters in *headline*;
	 * like that we can simply use putc() in the next loop, since we have
	 * already calculated their column widths (TODO it's sick) */
	wleft =
	subjlen = scrnwidth;

	for (fp = fmt; *fp; ++fp) {
		if (*fp == '%') {
			if (*++fp == '-') {
				++fp;
			} else if (*fp == '+')
				++fp;
			if (digitchar(*fp)) {
				n = 0;
				do
					n = 10*n + *fp - '0';
				while (++fp, digitchar(*fp));
				subjlen -= n;
			}

			if (*fp == '\0')
				break;
		} else {
#ifdef HAVE_WCWIDTH
			if (mb_cur_max > 1) {
				wchar_t	wc;
				if ((s = mbtowc(&wc, fp, mb_cur_max)) < 0)
					n = s = 1;
				else if ((n = wcwidth(wc)) < 0)
					n = 1;
			} else
#endif
				n = s = 1;
			subjlen -= n;
			wleft -= n;
			while (--s > 0)
				++fp;
		}
	}

	/* Walk *headline*, producing output */
	for (fp = fmt; *fp; ++fp) {
		if ((c = *fp & 0xFF) == '%') {
			B = 0;
			n = 0;
			s = 1;
			if (*++fp == '-') {
				s = -1;
				++fp;
			} else if (*fp == '+')
				++fp;
			if (digitchar(*fp)) {
				do
					n = 10*n + *fp - '0';
				while (++fp, digitchar(*fp));
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
				c = _dispc(mp, attrlist);
jputc:
				if (UICMP(32, ABS(n), >, wleft))
					n = (n < 0) ? -wleft : wleft;
				n = fprintf(f, "%*c", n, c);
				wleft = (n >= 0) ? wleft - n : 0;
				break;
			case 'm':
				if (n == 0) {
					n = 3;
					if (threaded)
						for (i=msgCount; i>999; i/=10)
							n++;
				}
				if (UICMP(32, ABS(n), >, wleft))
					n = (n < 0) ? -wleft : wleft;
				n = fprintf(f, "%*d", n, msgno);
				wleft = (n >= 0) ? wleft - n : 0;
				break;
			case 'f':
				if (n == 0) {
					n = 18;
					if (s < 0)
						n = -n;
				}
				i = ABS(n);
				if (i > wleft) {
					i = wleft;
					n = (n < 0) ? -wleft : wleft;
				}
				if (isto) /* XXX tr()! */
					i -= 3;
				n = fprintf(f, "%s%s", (isto ? "To " : ""),
					colalign(name, i, n, &wleft));
				if (n < 0)
					wleft = 0;
				else if (isto)
					wleft -= 3;
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
				if (UICMP(32, ABS(n), >, wleft))
					n = (n < 0) ? -wleft : wleft;
				n = fprintf(f, "%*.*s", n, n, date);
				wleft = (n >= 0) ? wleft - n : 0;
				break;
			case 'l':
				if (n == 0)
					n = 4;
				if (UICMP(32, ABS(n), >, wleft))
					n = (n < 0) ? -wleft : wleft;
				if (mp->m_xlines) {
					n = fprintf(f, "%*ld", n, mp->m_xlines);
					wleft = (n >= 0) ? wleft - n : 0;
				} else {
					n = ABS(n);
					wleft -= n;
					while (n-- != 0)
						putc(' ', f);
				}
				break;
			case 'o':
				if (n == 0)
					n = -5;
				if (UICMP(32, ABS(n), >, wleft))
					n = (n < 0) ? -wleft : wleft;
				n = fprintf(f, "%*lu", n, (long)mp->m_xsize);
				wleft = (n >= 0) ? wleft - n : 0;
				break;
			case 'i':
				if (threaded) {
					n = __putindent(f, mp, MIN(wleft,
						scrnwidth - 60));
					wleft = (n >= 0) ? wleft - n : 0;
				}
				break;
			case 'S':
				B = 1;
				/*FALLTHRU*/
			case 's':
				if (n == 0)
					n = subjlen - 2;
				if (n > 0 && s < 0)
					n = -n;
				if (subjlen > wleft)
					subjlen = wleft;
				if (UICMP(32, ABS(n), >, subjlen))
					n = (n < 0) ? -subjlen : subjlen;
				if (B)
					n -= (n < 0) ? -2 : 2;
				if (n == 0)
					break;
				if (subjline == NULL)
					subjline = __subject(mp, threaded,
							yetprinted);
				if (subjline == (char*)-1) {
					n = fprintf(f, "%*s", n, "");
					wleft = (n >= 0) ? wleft-n : 0;
				} else {
					n = fprintf(f, (B ? "\"%s\"" : "%s"),
						colalign(subjline, ABS(n), n,
						&wleft));
					if (n < 0)
						wleft = 0;
				}
				break;
			case 'U':
#ifdef HAVE_IMAP
				if (n == 0)
					n = 9;
				if (UICMP(32, ABS(n), >, wleft))
					n = (n < 0) ? -wleft : wleft;
				n = fprintf(f, "%*lu", n, mp->m_uid);
				wleft = (n >= 0) ? wleft - n : 0;
				break;
#else
				c = '?';
				goto jputc;
#endif
			case 'e':
				if (n == 0)
					n = 2;
				if (UICMP(32, ABS(n), >, wleft))
					n = (n < 0) ? -wleft : wleft;
				n = fprintf(f, "%*u", n,
					threaded == 1 ? mp->m_level : 0);
				wleft = (n >= 0) ? wleft - n : 0;
				break;
			case 't':
				if (n == 0) {
					n = 3;
					if (threaded)
						for (i=msgCount; i>999; i/=10)
							n++;
				}
				if (UICMP(32, ABS(n), >, wleft))
					n = (n < 0) ? -wleft : wleft;
				n = fprintf(f, "%*ld", n,
					threaded ? mp->m_threadpos : msgno);
				wleft = (n >= 0) ? wleft - n : 0;
				break;
			case '$':
#ifdef HAVE_SPAM
				if (n == 0)
					n = 4;
				if (UICMP(32, ABS(n), >, wleft))
					n = (n < 0) ? -wleft : wleft;
				{	char buf[16];
					snprintf(buf, sizeof buf, "%u.%u",
						(mp->m_spamscore >> 8),
						(mp->m_spamscore & 0xFF));
					n = fprintf(f, "%*s", n, buf);
					wleft = (n >= 0) ? wleft - n : 0;
				}
#else
				c = '?';
				goto jputc;
#endif
			}

			if (wleft <= 0)
				break;
		} else
			putc(c, f);
	}
	putc('\n', f);

	if (subjline != NULL && subjline != (char*)-1)
		free(subjline);
}

static char *
__subject_trim(char *s)
{
	struct {
		ui8_t	len;
		char	dat[7];
	} const *pp, ignored[] = { /* TODO make ignore list configurable */
		{ 3, "re:" }, { 4, "fwd:" },
		{ 3, "aw:" }, { 5, "antw:" },
		{ 0, "" }
	};
jouter:
	while (*s != '\0') {
		while (spacechar(*s))
			++s;
		/* TODO While it is maybe ok not to MIME decode these, we
		 * TODO should skip =?..?= at the beginning? */
		for (pp = ignored; pp->len > 0; ++pp)
			if (is_asccaseprefix(pp->dat, s)) {
				s += pp->len;
				goto jouter;
			}
		break;
	}
	return s;
}

static char *
__subject(struct message *mp, bool_t threaded, size_t yetprinted)
{
	/* XXX NOTE: because of efficiency reasons we simply ignore any encoded
	 * XXX parts and use ASCII case-insensitive comparison */
	struct str in, out;
	struct message *xmp;
	char *rv = (char*)-1, *ms, *mso, *os;

	if ((ms = hfield1("subject", mp)) == NULL)
		goto jleave;

	if (!threaded || mp->m_level == 0)
		goto jconv;

	/* In a display thread - check wether this message uses the same
	 * Subject: as it's parent or elder neighbour, suppress printing it if
	 * this is the case.  To extend this a bit, ignore any leading Re: or
	 * Fwd: plus follow-up WS.  Ignore invisible messages along the way */
	mso = __subject_trim(ms);
	for (xmp = mp; (xmp = prev_in_thread(xmp)) != NULL && yetprinted-- > 0;)
		if (visible(xmp) && (os = hfield1("subject", xmp)) != NULL &&
				asccasecmp(mso, __subject_trim(os)) == 0)
			goto jleave;
jconv:
	in.s = ms;
	in.l = strlen(ms);
	mime_fromhdr(&in, &out, TD_ICONV | TD_ISPR);
	rv = out.s;
jleave:
	return rv;
}

static int
__putindent(FILE *fp, struct message *mp, int maxwidth)/* XXX no magic consts */
{
	struct message *mq;
	int *us, indlvl, indw, i, important = MNEW|MFLAGGED;
	char *cs;

	if (mp->m_level == 0 || maxwidth == 0)
		return 0;
	cs = ac_alloc(mp->m_level);
	us = ac_alloc(mp->m_level * sizeof *us);

	i = mp->m_level - 1;
	if (mp->m_younger && UICMP(32, i + 1, ==, mp->m_younger->m_level)) {
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
			if (UICMP(32, i, >, mq->m_level - 1)) {
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

	--maxwidth;
	for (indlvl = indw = 0; (uc_it)indlvl < mp->m_level &&
			indw < maxwidth; ++indlvl) {
		if (indw < maxwidth - 1)
			indw += (int)putuc(us[indlvl], cs[indlvl] & 0377, fp);
		else
			indw += (int)putuc(0x21B8, '^', fp);
	}
	indw += /*putuc(0x261E, fp)*/putc('>', fp) != EOF;

	ac_free(us);
	ac_free(cs);
	return indw;
}

FL int
ccmdnotsupp(void *v) /* TODO -> lex.c */
{
	(void)v;
	fprintf(stderr, tr(10, "The requested feature is not compiled in\n"));
	return (1);
}

FL char const *
get_pager(void)
{
	char const *cp;

	cp = value("PAGER");
	if (cp == NULL || *cp == '\0')
		cp = PAGER;
	return cp;
}

FL int
headers(void *v)
{
	ui32_t flag;
	int *msgvec = v, g, k, n, mesg, size, lastg = 1;
	struct message *mp, *mq, *lastmq = NULL;
	enum mflag	fl = MNEW|MFLAGGED;

	time_current_update(&time_current, FAL0);

	flag = 0;
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
#ifdef HAVE_IMAP
		if (mb.mb_type == MB_IMAP)
			imap_getheaders(mesg+1, mesg + size);
#endif
		srelax_hold();
		for (; mp < &message[msgCount]; mp++) {
			mesg++;
			if (!visible(mp))
				continue;
			if (UICMP(32, flag++, >=, size))
				break;
			_print_head(0, mesg, stdout, 0);
			srelax();
		}
		srelax_rele();
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
		srelax_hold();
		while (mp) {
			if (visible(mp) && (mp->m_collapsed <= 0 ||
					 mp == &message[n-1])) {
				if (UICMP(32, flag++, >=, size))
					break;
				_print_head(flag - 1, mp - &message[0] + 1,
					stdout, mb.mb_threaded);
				srelax();
			}
			mp = next_in_thread(mp);
		}
		srelax_rele();
	}
	if (!flag)
		printf(tr(6, "No more mail.\n"));
	return !flag;
}

/*
 * Scroll to the next/previous screen
 */
FL int
scroll(void *v)
{
	return scroll1(v, 0);
}

FL int
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
			printf(tr(7, "On last screenful of messages\n"));
		}
		break;

	case '-':
		if (arg[1] == '\0')
			screen--;
		else
			screen -= atoi(arg + 1);
		if (screen < 0) {
			screen = 0;
			printf(tr(8, "On first screenful of messages\n"));
		}
		if (cur[0] == -1)
			cur[0] = -2;
		break;

	default:
		printf(tr(9, "Unrecognized scrolling command \"%s\"\n"), arg);
		return(1);
	}
	return(headers(cur));
}

/*
 * Compute screen size.
 */
FL int
screensize(void)
{
	int s;
	char *cp;

	if ((cp = value("screen")) != NULL && (s = atoi(cp)) > 0)
		return s;
	return scrnheight - 4;
}

static sigjmp_buf	_cmd1_pipejmp;

/*ARGSUSED*/
static void
_cmd1_onpipe(int signo)
{
	UNUSED(signo);
	siglongjmp(_cmd1_pipejmp, 1);
}

/*
 * Print out the headlines for each message
 * in the passed message list.
 */
FL int
from(void *v)
{
	int *msgvec = v, *ip, n;
	char *cp;
	FILE *volatile obuf = stdout;

	time_current_update(&time_current, FAL0);

	/* TODO unfixable memory leaks still */
	if (IS_TTY_SESSION() && (cp = value("crt")) != NULL) {
		for (n = 0, ip = msgvec; *ip; ip++)
			n++;
		if (n > (*cp == '\0' ? screensize() : atoi((char*)cp)) + 3) {
			char const *p;
			if (sigsetjmp(_cmd1_pipejmp, 1))
				goto endpipe;
			p = get_pager();
			if ((obuf = Popen(p, "w", NULL, 1)) == NULL) {
				perror(p);
				obuf = stdout;
				cp=NULL;
			} else
				safe_signal(SIGPIPE, _cmd1_onpipe);
		}
	}
	for (n = 0, ip = msgvec; *ip != 0; ip++)
		_print_head((size_t)n++, *ip, obuf, mb.mb_threaded);
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
_dispc(struct message *mp, const char *a)
{
	int i = ' ';

	/*
	 * Bletch!
	 */
	if ((mp->m_flag & (MREAD|MNEW)) == MREAD)
		i = a[3];
	if ((mp->m_flag & (MREAD|MNEW)) == (MREAD|MNEW))
		i = a[2];
	if (mp->m_flag & MANSWERED)
		i = a[8];
	if (mp->m_flag & MDRAFTED)
		i = a[9];
	if ((mp->m_flag & (MREAD|MNEW)) == MNEW)
		i = a[0];
	if ((mp->m_flag & (MREAD|MNEW)) == 0)
		i = a[1];
	if (mp->m_flag & MSPAM)
		i = a[12];
	if (mp->m_flag & MSAVED)
		i = a[4];
	if (mp->m_flag & MPRESERVE)
		i = a[5];
	if (mp->m_flag & (MBOX|MBOXED))
		i = a[6];
	if (mp->m_flag & MFLAGGED)
		i = a[7];
	if (mb.mb_threaded == 1 && mp->m_collapsed > 0)
		i = a[11];
	if (mb.mb_threaded == 1 && mp->m_collapsed < 0)
		i = a[10];
	return i;
}

FL void
print_headers(size_t bottom, size_t topx)
{
	size_t printed;

#ifdef HAVE_IMAP
	if (mb.mb_type == MB_IMAP)
		imap_getheaders(bottom, topx);
#endif
	time_current_update(&time_current, FAL0);

	for (printed = 0; bottom <= topx; ++bottom)
		if (visible(&message[bottom - 1]))
			_print_head(printed++, bottom, stdout, 0);
}

/*
 * Print out the value of dot.
 */
/*ARGSUSED*/
FL int
pdot(void *v)
{
	(void)v;
	printf("%d\n", (int)(dot - &message[0] + 1));
	return(0);
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
_type1(int *msgvec, bool_t doign, bool_t dopage, bool_t dopipe,
	bool_t dodecode, char *cmd, off_t *tstats)
{
	off_t mstats[2];
	int *ip;
	struct message *mp;
	char const *cp;
	FILE * volatile obuf;

	obuf = stdout;
	if (sigsetjmp(pipestop, 1))
		goto close_pipe;
	if (dopipe) {
		if ((cp = value("SHELL")) == NULL)
			cp = SHELL;
		if ((obuf = Popen(cmd, "w", cp, 1)) == NULL) {
			perror(cmd);
			obuf = stdout;
		} else
			safe_signal(SIGPIPE, brokpipe);
	} else if ((options & OPT_TTYOUT) &&
			(dopage || (cp = value("crt")) != NULL)) {
		long nlines = 0;
		if (!dopage) {
			for (ip = msgvec; *ip &&
					PTRCMP(ip - msgvec, <, msgCount);
					++ip) {
				if (!(message[*ip - 1].m_have & HAVE_BODY)) {
					if ((get_body(&message[*ip - 1])) !=
							OKAY)
						return 1;
				}
				nlines += message[*ip - 1].m_lines;
			}
		}
		if (dopage || nlines > (*cp ? atoi(cp) : realscreenheight)) {
			char const *p = get_pager();
			if ((obuf = Popen(p, "w", NULL, 1)) == NULL) {
				perror(p);
				obuf = stdout;
			} else
				safe_signal(SIGPIPE, brokpipe);
		}
	}

	/* This may jump, in which case srelax_rele() wouldn't be called, but
	 * it shouldn't matter, because we -- then -- directly reenter the
	 * lex.c:commands() loop, which sreset()s */
	srelax_hold();
	for (ip = msgvec; *ip && PTRCMP(ip - msgvec, <, msgCount); ++ip) {
		mp = &message[*ip - 1];
		touch(mp);
		setdot(mp);
		uncollapse1(mp, 1);
		if (!dopipe && ip != msgvec)
			fprintf(obuf, "\n");
		_show_msg_overview(mp, *ip, obuf);
		sendmp(mp, obuf, (doign ? ignore : 0), NULL,
			((dopipe && boption("piperaw"))
				? SEND_MBOX : dodecode
				? SEND_SHOW : doign
				? SEND_TODISP : SEND_TODISP_ALL),
			mstats);
		srelax();
		if (dopipe && boption("page"))
			putc('\f', obuf);
		if (tstats) {
			tstats[0] += mstats[0];
			tstats[1] += mstats[1];
		}
	}
	srelax_rele();
close_pipe:
	if (obuf != stdout) {
		/* Ignore SIGPIPE so it can't cause a duplicate close */
		safe_signal(SIGPIPE, SIG_IGN);
		Pclose(obuf, TRU1);
		safe_signal(SIGPIPE, dflpipe);
	}
	return 0;
}

/*
 * Pipe the messages requested.
 */
static int
pipe1(char *str, int doign)
{
	char *cmd;
	int *msgvec, ret;
	off_t stats[2];
	bool_t f;

	/*LINTED*/
	msgvec = (int *)salloc((msgCount + 2) * sizeof *msgvec);
	if ((cmd = laststring(str, &f, 1)) == NULL) {
		cmd = value("cmd");
		if (cmd == NULL || *cmd == '\0') {
			fputs(tr(16, "variable cmd not set\n"), stderr);
			return 1;
		}
	}
	if (!f) {
		*msgvec = first(0, MMNORM);
		if (*msgvec == 0) {
			if (inhook)
				return 0;
			puts(tr(18, "No messages to pipe."));
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
	printf(tr(268, "Pipe to: \"%s\"\n"), cmd);
	stats[0] = stats[1] = 0;
	if ((ret = _type1(msgvec, doign, FAL0, TRU1, FAL0, cmd, stats)) == 0) {
		printf("\"%s\" ", cmd);
		if (stats[0] >= 0)
			printf("%lu", (long)stats[0]);
		else
			printf(tr(27, "binary"));
		printf("/%lu\n", (long)stats[1]);
	}
	return ret;
}

/*
 * Paginate messages, honor ignored fields.
 */
FL int
more(void *v)
{
	int *msgvec = v;

	return _type1(msgvec, TRU1, TRU1, FAL0, FAL0, NULL, NULL);
}

/*
 * Paginate messages, even printing ignored fields.
 */
FL int
More(void *v)
{
	int *msgvec = v;

	return _type1(msgvec, FAL0, TRU1, FAL0, FAL0, NULL, NULL);
}

/*
 * Type out messages, honor ignored fields.
 */
FL int
type(void *v)
{
	int *msgvec = v;

	return _type1(msgvec, TRU1, FAL0, FAL0, FAL0, NULL, NULL);
}

/*
 * Type out messages, even printing ignored fields.
 */
FL int
Type(void *v)
{
	int *msgvec = v;

	return _type1(msgvec, FAL0, FAL0, FAL0, FAL0, NULL, NULL);
}

/*
 * Show MIME-encoded message text, including all fields.
 */
FL int
show(void *v)
{
	int *msgvec = v;

	return _type1(msgvec, FAL0, FAL0, FAL0, TRU1, NULL, NULL);
}

/*
 * Pipe messages, honor ignored fields.
 */
FL int
pipecmd(void *v)
{
	char *str = v;
	return(pipe1(str, 1));
}
/*
 * Pipe messages, not respecting ignored fields.
 */
FL int
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
FL int
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
		for (lines = 0; lines < c && UICMP(32, lines, <=, topl);
				++lines) {
			if (readline_restart(ibuf, &linebuf, &linesize, 0) < 0)
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
FL int
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
FL int
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
FL int
folders(void *v)
{
	char dirname[MAXPATHLEN], *name, **argv = v;
	char const *cmd;

	if (*argv) {
		name = expand(*argv);
		if (name == NULL)
			return 1;
	} else if (! getfold(dirname, sizeof dirname)) {
		fprintf(stderr, tr(20, "No value set for \"folder\"\n"));
		return 1;
	} else
		name = dirname;

	if (which_protocol(name) == PROTO_IMAP) {
#ifdef HAVE_IMAP
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
