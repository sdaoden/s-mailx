/*
 * Nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2002 Gunnar Ritter, Freiburg i. Br., Germany.
 */
/*-
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
static char sccsid[] = "@(#)cmd1.c	2.18 (gritter) 11/22/02";
#endif
#endif /* not lint */

#include "rcv.h"
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * User commands.
 */

/*
 * Print the current active headings.
 * Don't change dot if invoker didn't give an argument.
 */

static int screen;
static RETSIGTYPE brokpipe __P((int));
static int	pipe1 __P((char *, int));
static int	type1 __P((int *, int, int, int, char *, off_t *));

char *
get_pager()
{
	char *cp;

	cp = value("PAGER");
	if (cp == NULL || *cp == '\0')
		cp = value("bsdcompat") ? PATH_MORE : PATH_PG;
	return cp;
}
int
headers(v)
	void *v;
{
	int *msgvec = v;
	int n, mesg, flag;
	struct message *mp;
	int size;

	size = screensize();
	n = msgvec[0];
	if (n != 0)
		screen = (n-1)/size;
	if (screen < 0)
		screen = 0;
	mp = &message[screen * size];
	if (mp >= &message[msgcount])
		mp = &message[msgcount - size];
	if (mp < &message[0])
		mp = &message[0];
	flag = 0;
	mesg = mp - &message[0];
	if (dot != &message[n-1])
		setdot(mp);
	for (; mp < &message[msgcount]; mp++) {
		mesg++;
		if (mp->m_flag & MDELETED)
			continue;
		if (flag++ >= size)
			break;
		printhead(mesg, stdout);
	}
	if (flag == 0) {
		printf(catgets(catd, CATSET, 6, "No more mail.\n"));
		return(1);
	}
	return(0);
}

/*
 * Scroll to the next/previous screen
 */
int
scroll(v)
	void *v;
{
	char *arg = v;
	int size;
	int cur[1];

	cur[0] = 0;
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
		screen = msgcount / size;
		goto scroll_forward;
	case '+':
		if (arg[1] == '\0')
			screen++;
		else
			screen += atoi(arg + 1);
scroll_forward:
		if (screen * size > msgcount) {
			screen = msgcount / size;
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
screensize()
{
	int s;
	char *cp;

	if ((cp = value("screen")) != NULL && (s = atoi(cp)) > 0)
		return s;
	return scrnheight - 4;
}

static sigjmp_buf	pipejmp;

/*ARGSUSED*/
static RETSIGTYPE
onpipe(signo)
{
	siglongjmp(pipejmp, 1);
}

/*
 * Print out the headlines for each message
 * in the passed message list.
 */
int
from(v)
	void *v;
{
	int *msgvec = v;
	int *ip, n;
	FILE *obuf = stdout;
	char *cp;

	(void)&obuf;
	(void)&cp;
	if (is_a_tty[0] && is_a_tty[1] && (cp = value("crt")) != NULL) {
		for (n = 0, ip = msgvec; *ip; ip++)
			n++;
		if (n > (*cp == '\0' ? screensize() : atoi(cp)) + 3) {
			cp = get_pager();
			if (sigsetjmp(pipejmp, 1))
				goto endpipe;
			if ((obuf = Popen(cp, "w", NULL, 1)) == NULL) {
				perror(cp);
				obuf = stdout;
			} else
				safe_signal(SIGPIPE, onpipe);
		}
	}
	for (ip = msgvec; *ip != 0; ip++)
		printhead(*ip, obuf);
	if (--ip >= msgvec)
		setdot(&message[*ip - 1]);
endpipe:
	if (obuf != stdout) {
		safe_signal(SIGPIPE, SIG_IGN);
		Pclose(obuf);
		safe_signal(SIGPIPE, SIG_DFL);
	}
	return(0);
}

/*
 * Print out the header of a specific message.
 * This is a slight improvement to the standard one.
 */
void
printhead(mesg, f)
	int mesg;
	FILE *f;
{
	struct message *mp;
	char *headline = NULL, lcount[64], ccount[64], *subjline, dispc, curind;
	size_t headsize = 0;
	int headlen = 0;
	char *pbuf = NULL;
	struct headline hl;
	struct str in, out;
	int subjlen, fromlen, isto = 0;
	int bsdcompat = (value("bsdcompat") != NULL);
	char *name, *cp;
	FILE *ibuf;

	mp = &message[mesg-1];
	if ((mp->m_flag & MNOFROM) == 0) {
		if ((ibuf = setinput(mp, NEED_HEADER)) == NULL)
			return;
		if ((headlen = readline(ibuf, &headline, &headsize)) < 0)
			return;
	}
	if ((subjline = hfield("subject", mp)) == NULL)
		subjline = hfield("subj", mp);
	if (subjline == NULL) {
		subjline = "";
		out.s = NULL;
		out.l = 0;
	} else {
		in.s = subjline;
		in.l = strlen(subjline);
		mime_fromhdr(&in, &out, TD_ICONV | TD_ISPR);
		subjline = out.s;
	}
	/*
	 * Bletch!
	 */
	curind = dot == mp ? '>' : ' ';
	dispc = ' ';
	if (value("bsdcompat") == NULL) {
		if (mp->m_flag & (MREAD|MNEW))
			dispc = 'R';
		if ((mp->m_flag & (MREAD|MNEW)) == MREAD)
			dispc = 'O';
	}
	if (mp->m_flag & MSAVED)
		dispc = bsdcompat ? '*' : 'S';
	if (mp->m_flag & MPRESERVE)
		dispc = bsdcompat ? 'P' : 'H';
	if ((mp->m_flag & (MREAD|MNEW)) == MNEW)
		dispc = 'N';
	if ((mp->m_flag & (MREAD|MNEW)) == 0)
		dispc = 'U';
	if (mp->m_flag & MBOX)
		dispc = 'M';
	if ((mp->m_flag & MNOFROM) == 0) {
		pbuf = ac_alloc(headlen + 1);
		parse(headline, headlen, &hl, pbuf);
	} else {
		hl.l_from = /*fakefrom(mp);*/NULL;
		hl.l_tty = NULL;
		hl.l_date = fakedate(mp->m_time);
	}
	if (value("datefield") && (cp = hfield("date", mp)) != NULL)
		hl.l_date = fakedate(rfctime(cp));
	if (mp->m_xlines > 0)
		snprintf(lcount, sizeof lcount, "%3d", mp->m_xlines);
	else
		strcpy(lcount, "   ");
	snprintf(ccount, sizeof ccount, "%-5u", (unsigned)mp->m_xsize);
	subjlen = scrnwidth - (bsdcompat ? 49 : 45) - strlen(ccount) -
		strlen(ccount);
	if (subjlen > out.l)
		subjlen = out.l;
	if (Iflag) {
		if ((name = hfield("newsgroups", mp)) == NULL)
			if ((name = hfield("article-id", mp)) == NULL)
				name = "<>";
	} else if (value("show-rcpt") == NULL) {
		name = nameof(mp, 0);
		if (value("showto") && name && is_myname(name)) {
			if ((cp = skin(hfield("to", mp))) != NULL) {
				name = cp;
				isto = 1;
			}
		}
	} else {
		if ((name = skin(hfield("to", mp))) != NULL)
			isto = 1;
	}
	if (name == NULL)
		name = "";
	if (bsdcompat)
		fromlen = isto ? 17 : 20;
	else
		fromlen = isto ? 15 : 18;
	if (subjline == NULL || subjlen < 0) {         /* pretty pathetic */
		fprintf(f, bsdcompat ?  catgets(catd, CATSET, 206,
				"%c%c%3d %s%-*.*s  %16.16s %s/%s\n") :
			catgets(catd, CATSET, 11,
				"%c%c%3d %s%-*.*s  %16.16s %s/%s\n"),
			curind, dispc, mesg, isto ? "To " : "",
			fromlen, fromlen, name, hl.l_date, lcount, ccount);
	} else {
		makeprint(subjline, subjlen);
		fprintf(f, bsdcompat ? catgets(catd, CATSET, 207,
				"%c%c%3d %s%-*.*s  %16.16s %s/%s \"%.*s\"\n") :
			catgets(catd, CATSET, 12,
				"%c%c%3d %s%-*.*s  %16.16s %s/%s %.*s\n"),
			curind, dispc, mesg, isto ? "To " : "",
			fromlen, fromlen, name, hl.l_date, lcount, ccount,
			subjlen, subjline);
	}
	if (out.s)
		free(out.s);
	if (headline)
		free(headline);
	if (pbuf)
		ac_free(pbuf);
}

/*
 * Print out the value of dot.
 */
/*ARGSUSED*/
int
pdot(v)
	void *v;
{
	printf(catgets(catd, CATSET, 13, "%d\n"),
			(int)(dot - &message[0] + 1));
	return(0);
}

/*
 * Print out all the possible commands.
 */
/*ARGSUSED*/
int
pcmdlist(v)
	void *v;
{
	extern const struct cmd cmdtab[];
	const struct cmd *cp;
	int cc;

	printf(catgets(catd, CATSET, 14, "Commands are:\n"));
	for (cc = 0, cp = cmdtab; cp->c_name != NULL; cp++) {
		cc += strlen(cp->c_name) + 2;
		if (cc > 72) {
			printf("\n");
			cc = strlen(cp->c_name) + 2;
		}
		if ((cp+1)->c_name != NULL)
			printf(catgets(catd, CATSET, 15, "%s, "), cp->c_name);
		else
			printf("%s\n", cp->c_name);
	}
	return(0);
}

/*
 * Type out the messages requested.
 */
static sigjmp_buf	pipestop;

static int
type1(msgvec, doign, page, pipe, cmd, tstats)
int *msgvec;
char *cmd;
off_t *tstats;
{
	int *ip;
	struct message *mp;
	char *cp;
	int nlines;
	off_t mstats[2];
	/*
	 * Must be static to become excluded from sigsetjmp().
	 */
	static FILE *obuf;
#ifdef __GNUC__
	/* Avoid longjmp clobbering */
	(void) &cp;
	(void) &cmd;
	(void) &obuf;
#endif

	obuf = stdout;
	if (sigsetjmp(pipestop, 1))
		goto close_pipe;
	if (pipe) {
		cp = value("SHELL");
		if (cp == NULL)
			cp = PATH_CSHELL;
		obuf = Popen(cmd, "w", cp, 1);
		if (obuf == (FILE*)NULL) {
			perror(cmd);
			obuf = stdout;
		} else {
			safe_signal(SIGPIPE, brokpipe);
		}
	} else if (value("interactive") != NULL &&
	    (page || (cp = value("crt")) != NULL)) {
		nlines = 0;
		if (!page) {
			for (ip = msgvec; *ip && ip-msgvec < msgcount; ip++) {
				if ((message[*ip-1].m_have & HAVE_BODY) == 0) {
					if ((get_body(&message[*ip - 1])) !=
							OKAY)
						return 1;
				}
				nlines += message[*ip - 1].m_lines;
			}
		}
		if (page || nlines > (*cp ? atoi(cp) : realscreenheight)) {
			cp = get_pager();
			obuf = Popen(cp, "w", NULL, 1);
			if (obuf == (FILE*)NULL) {
				perror(cp);
				obuf = stdout;
			} else
				safe_signal(SIGPIPE, brokpipe);
		}
	}
	for (ip = msgvec; *ip && ip - msgvec < msgcount; ip++) {
		mp = &message[*ip - 1];
		touch(mp);
		setdot(mp);
		if (value("quiet") == NULL)
			fprintf(obuf, catgets(catd, CATSET, 17,
				"Message %2d:\n"), *ip);
		(void) send_message(mp, obuf, doign ? ignore : 0, NULL,
				    pipe && value("piperaw") ?
				    	CONV_NONE : CONV_TODISP,
				    mstats);
		if (pipe && value("page")) {
			sputc('\f', obuf);
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
		Pclose(obuf);
		safe_signal(SIGPIPE, SIG_DFL);
	}
	return(0);
}

/*
 * Get the last, possibly quoted part of linebuf.
 */
char *
laststring(linebuf, flag, strip)
char *linebuf;
int *flag;
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
pipe1(str, doign)
char *str;
{
	char *cmd;
	int f, *msgvec, ret;
	off_t stats[2];

	/*LINTED*/
	msgvec = (int *)salloc((msgcount + 2) * sizeof *msgvec);
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
			puts(catgets(catd, CATSET, 18, "No messages to pipe."));
			return 1;
		}
		msgvec[1] = 0;
	} else if (getmsglist(str, msgvec, 0) < 0)
		return 1;
	printf(catgets(catd, CATSET, 268, "Pipe to: \"%s\"\n"), cmd);
	stats[0] = stats[1] = 0;
	if ((ret = type1(msgvec, doign, 0, 1, cmd, stats)) == 0) {
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
more(v)
	void *v;
{
	int *msgvec = v;
	return (type1(msgvec, 1, 1, 0, NULL, NULL));
}

/*
 * Paginate messages, even printing ignored fields.
 */
int
More(v)
	void *v;
{
	int *msgvec = v;

	return (type1(msgvec, 0, 1, 0, NULL, NULL));
}

/*
 * Type out messages, honor ignored fields.
 */
int
type(v)
	void *v;
{
	int *msgvec = v;

	return(type1(msgvec, 1, 0, 0, NULL, NULL));
}

/*
 * Type out messages, even printing ignored fields.
 */
int
Type(v)
	void *v;
{
	int *msgvec = v;

	return(type1(msgvec, 0, 0, 0, NULL, NULL));
}

/*
 * Pipe messages, honor ignored fields.
 */
int
pipecmd(v)
void *v;
{
	char *str = v;
	return(pipe1(str, 1));
}
/*
 * Pipe messages, not respecting ignored fields.
 */
int
Pipecmd(v)
void *v;
{
	char *str = v;
	return(pipe1(str, 0));
}

/*
 * Respond to a broken pipe signal --
 * probably caused by quitting more.
 */
/*ARGSUSED*/
static RETSIGTYPE
brokpipe(signo)
	int signo;
{
	siglongjmp(pipestop, 1);
}

/*
 * Print the top so many lines of each desired message.
 * The number of lines is taken from the variable "toplines"
 * and defaults to 5.
 */
int
top(v)
	void *v;
{
	int *msgvec = v;
	int *ip;
	struct message *mp;
	int c, topl, lines, lineb;
	char *valtop, *linebuf = NULL;
	size_t linesize;
	FILE *ibuf;

	topl = 5;
	valtop = value("toplines");
	if (valtop != NULL) {
		topl = atoi(valtop);
		if (topl < 0 || topl > 10000)
			topl = 5;
	}
	lineb = 1;
	for (ip = msgvec; *ip && ip-msgvec < msgcount; ip++) {
		mp = &message[*ip - 1];
		touch(mp);
		setdot(mp);
		did_print_dot = 1;
		if (value("quiet") == NULL)
			printf(catgets(catd, CATSET, 19,
					"Message %2d:\n"), *ip);
		if (mp->m_flag & MNOFROM)
			printf("From %s %s\n", fakefrom(mp),
					fakedate(mp->m_time));
		if ((ibuf = setinput(mp, NEED_BODY)) == NULL)	/* XXX could use TOP */
			return 1;
		c = mp->m_lines;
		if (!lineb)
			printf("\n");
		for (lines = 0; lines < c && lines <= topl; lines++) {
			if (readline(ibuf, &linebuf, &linesize) < 0)
				break;
			puts(linebuf);
			lineb = blankline(linebuf);
		}
	}
	if (linebuf)
		free(linebuf);
	return(0);
}

/*
 * Touch all the given messages so that they will
 * get mboxed.
 */
int
stouch(v)
	void *v;
{
	int *msgvec = v;
	int *ip;

	for (ip = msgvec; *ip != 0; ip++) {
		setdot(&message[*ip-1]);
		dot->m_flag |= MTOUCH;
		dot->m_flag &= ~MPRESERVE;
	}
	return(0);
}

/*
 * Make sure all passed messages get mboxed.
 */
int
mboxit(v)
	void *v;
{
	int *msgvec = v;
	int *ip;

	for (ip = msgvec; *ip != 0; ip++) {
		setdot(&message[*ip-1]);
		dot->m_flag |= MTOUCH|MBOX;
		dot->m_flag &= ~MPRESERVE;
	}
	return(0);
}

/*
 * List the folders the user currently has.
 */
/*ARGSUSED*/
int
folders(v)
	void *v;
{
	char dirname[PATHSIZE];
	char *cmd;

	if (getfold(dirname, sizeof dirname) < 0) {
		printf(catgets(catd, CATSET, 20,
				"No value set for \"folder\"\n"));
		return 1;
	}
	if ((cmd = value("LISTER")) == NULL)
		cmd = "ls";
	(void) run_command(cmd, 0, -1, -1, dirname, NULL, NULL);
	return 0;
}
