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
static char sccsid[] = "@(#)cmd1.c	1.5 (gritter) 11/18/00";
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
		dot = mp;
	for (; mp < &message[msgcount]; mp++) {
		mesg++;
		if (mp->m_flag & MDELETED)
			continue;
		if (flag++ >= size)
			break;
		printhead(mesg);
	}
	if (flag == 0) {
		printf("No more mail.\n");
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
			printf("On last screenful of messages\n");
		}
		break;

	case '-':
		if (arg[1] == '\0')
			screen--;
		else
			screen -= atoi(arg + 1);
		if (screen < 0) {
			screen = 0;
			printf("On first screenful of messages\n");
		}
		break;

	default:
		printf("Unrecognized scrolling command \"%s\"\n", arg);
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

/*
 * Print out the headlines for each message
 * in the passed message list.
 */
int
from(v)
	void *v;
{
	int *msgvec = v;
	int *ip;

	for (ip = msgvec; *ip != 0; ip++)
		printhead(*ip);
	if (--ip >= msgvec)
		dot = &message[*ip - 1];
	return(0);
}

/*
 * Print out the header of a specific message.
 * This is a slight improvement to the standard one.
 */
void
printhead(mesg)
	int mesg;
{
	struct message *mp;
	char headline[LINESIZE], wcount[LINESIZE], *subjline, dispc, curind;
	char pbuf[BUFSIZ];
	struct headline hl;
	struct str in, out;
	int subjlen;
	char *name;

	mp = &message[mesg-1];
	(void) readline(setinput(mp), headline, LINESIZE);
	if ((subjline = hfield("subject", mp)) == NULL)
		subjline = hfield("subj", mp);
	if (subjline == NULL) {
		subjline = "";
		out.s = NULL;
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
	if (mp->m_flag & MSAVED)
		dispc = '*';
	if (mp->m_flag & MPRESERVE)
		dispc = 'P';
	if ((mp->m_flag & (MREAD|MNEW)) == MNEW)
		dispc = 'N';
	if ((mp->m_flag & (MREAD|MNEW)) == 0)
		dispc = 'U';
	if (mp->m_flag & MBOX)
		dispc = 'M';
	parse(headline, &hl, pbuf);
	snprintf(wcount, LINESIZE, "%3d/%-5u", mp->m_lines,
			(unsigned int)mp->m_size);
	subjlen = scrnwidth - 50 - strlen(wcount);
	if (subjlen > out.l)
		subjlen = out.l;
	name = value("show-rcpt") != NULL ?
		skin(hfield("to", mp)) : nameof(mp, 0);
	if (subjline == NULL || subjlen < 0) {         /* pretty pathetic */
		printf("%c%c%3d %-20.20s  %16.16s %s\n",
			curind, dispc, mesg, name, hl.l_date, wcount);
	} else {
		makeprint(subjline, subjlen);
		printf("%c%c%3d %-20.20s  %16.16s %s \"%.*s\"\n",
			curind, dispc, mesg, name, hl.l_date, wcount,
			subjlen, subjline);
	}
	if (out.s != NULL) free(out.s);
}

/*
 * Print out the value of dot.
 */
int
pdot(v)
	void *v;
{
	printf("%d\n", (int) (dot - &message[0] + 1));
	return(0);
}

/*
 * Print out all the possible commands.
 */
int
pcmdlist(v)
	void *v;
{
	extern const struct cmd cmdtab[];
	const struct cmd *cp;
	int cc;

	printf("Commands are:\n");
	for (cc = 0, cp = cmdtab; cp->c_name != NULL; cp++) {
		cc += strlen(cp->c_name) + 2;
		if (cc > 72) {
			printf("\n");
			cc = strlen(cp->c_name) + 2;
		}
		if ((cp+1)->c_name != NULL)
			printf("%s, ", cp->c_name);
		else
			printf("%s\n", cp->c_name);
	}
	return(0);
}

/*
 * Type out the messages requested.
 */
sigjmp_buf	pipestop;

static int
type1(msgvec, doign, page, pipe, cmd)
int *msgvec;
char *cmd;
{
	int *ip;
	struct message *mp;
	char *cp;
	int nlines;
	FILE *obuf;
#if __GNUC__
	/* Avoid longjmp clobbering */
	(void) &cp;
	(void) &cmd;
	(void) &obuf;
#endif

	obuf = stdout;
	if (sigsetjmp(pipestop, 1))
		goto close_pipe;
	if (pipe) {
		if (cmd == NULL) {
			cmd = value("cmd");
			if (cmd == NULL || *cmd == '\0') {
				fputs("variable cmd not set\n", stderr);
				return 1;
			}
		}
		cp = value("SHELL");
		if (cp == NULL)
			cp = PATH_CSHELL;
		obuf = Popen(cmd, "w", cp);
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
			for (ip = msgvec; *ip && ip-msgvec < msgcount; ip++)
				nlines += message[*ip - 1].m_lines;
		}
		if (page || nlines > (*cp ? atoi(cp) : realscreenheight)) {
			cp = value("PAGER");
			if (cp == NULL || *cp == '\0')
				cp = PATH_MORE;
			obuf = Popen(cp, "w", NULL);
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
		dot = mp;
		if (value("quiet") == NULL)
			fprintf(obuf, "Message %d:\n", *ip);
		(void) send_message(mp, obuf, doign ? ignore : 0,
				    NULL, CONV_TODISP);
		if (pipe && value("page")) {
			fputc('\f', obuf);
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
 * Get the shell command out of the pipe command's arguments.
 */
char *
getcmd(linebuf, flag)
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
	while (cp > linebuf && isspace(*cp))
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
		*cp-- = '\0';
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
		if (*cp == quoted)
			*cp++ = 0;
		else
			*flag = 0;
	} else {
		while (cp > linebuf && !isspace(*cp))
			cp--;
		if (isspace(*cp))
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
int
pipe1(str, doign)
char *str;
{
	char *cmd;
	int f, *msgvec;

	msgvec = (int*) salloc((msgcount + 2) * sizeof *msgvec);
	cmd = getcmd(str, &f);
	if (!f) {
		*msgvec = first(0, MMNORM);
		if (*msgvec == 0) {
			puts("No messages to pipe.");
			return 1;
		}
		msgvec[1] = 0;
	} else if (getmsglist(str, msgvec, 0) < 0)
		return 1;
	return type1(msgvec, doign, 0, 1, cmd);
}

/*
 * Paginate messages, honor ignored fields.
 */
int
more(v)
	void *v;
{
	int *msgvec = v;
	return (type1(msgvec, 1, 1, 0, NULL));
}

/*
 * Paginate messages, even printing ignored fields.
 */
int
More(v)
	void *v;
{
	int *msgvec = v;

	return (type1(msgvec, 0, 1, 0, NULL));
}

/*
 * Type out messages, honor ignored fields.
 */
int
type(v)
	void *v;
{
	int *msgvec = v;

	return(type1(msgvec, 1, 0, 0, NULL));
}

/*
 * Type out messages, even printing ignored fields.
 */
int
Type(v)
	void *v;
{
	int *msgvec = v;

	return(type1(msgvec, 0, 0, 0, NULL));
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
RETSIGTYPE
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
	char *valtop, linebuf[LINESIZE];
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
		dot = mp;
		if (value("quiet") == NULL)
			printf("Message %d:\n", *ip);
		ibuf = setinput(mp);
		c = mp->m_lines;
		if (!lineb)
			printf("\n");
		for (lines = 0; lines < c && lines <= topl; lines++) {
			if (readline(ibuf, linebuf, LINESIZE) < 0)
				break;
			puts(linebuf);
			lineb = blankline(linebuf);
		}
	}
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
		dot = &message[*ip-1];
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
		dot = &message[*ip-1];
		dot->m_flag |= MTOUCH|MBOX;
		dot->m_flag &= ~MPRESERVE;
	}
	return(0);
}

/*
 * List the folders the user currently has.
 */
int
folders(v)
	void *v;
{
	char dirname[BUFSIZ];
	char *cmd;

	if (getfold(dirname, BUFSIZ) < 0) {
		printf("No value set for \"folder\"\n");
		return 1;
	}
	if ((cmd = value("LISTER")) == NULL)
		cmd = "ls";
	(void) run_command(cmd, 0, -1, -1, dirname, NULL, NULL);
	return 0;
}
