/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Collect input from standard input, handling ~ escapes.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
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
 * The following hokiness with global variables is so that on
 * receipt of an interrupt signal, the partial message can be salted
 * away on dead.letter.
 */

static sighandler_type	_coll_saveint;	/* Previous SIGINT value */
static sighandler_type	_coll_savehup;	/* Previous SIGHUP value */
static sighandler_type	_coll_savetstp;	/* Previous SIGTSTP value */
static sighandler_type	_coll_savettou;	/* Previous SIGTTOU value */
static sighandler_type	_coll_savettin;	/* Previous SIGTTIN value */
static FILE		*_coll_fp;	/* File for saving away */
static int volatile	_coll_hadintr;	/* Have seen one SIGINT so far */
static sigjmp_buf	_coll_jmp;	/* To get back to work */
static int		_coll_jmp_p;	/* whether to long jump */
static sigjmp_buf	_coll_abort;	/* To end collection with error */
static sigjmp_buf	_coll_pipejmp;	/* On broken pipe */

/* Handle `~:', `~_' */
static void	_execute_command(struct header *hp, char *linebuf,
			size_t linesize);

/* If *interactive* is set and *doecho* is, too, also dump to *stdout* */
static int	_include_file(FILE *fbuf, char const *name, int *linecount,
			int *charcount, bool_t doecho);

static void _collect_onpipe(int signo);
static void insertcommand(FILE *fp, char const *cmd);
static void print_collf(FILE *collf, struct header *hp);
static int exwrite(char const *name, FILE *fp, int f);
static enum okay makeheader(FILE *fp, struct header *hp);
static void mesedit(int c, struct header *hp);
static void mespipe(char *cmd);
static int forward(char *ms, FILE *fp, int f);
static void collstop(int s);
static void collint(int s);
static void collhup(int s);
static int putesc(const char *s, FILE *stream);

static void
_execute_command(struct header *hp, char *linebuf, size_t linesize)
{
	/* The problem arises if there are rfc822 message attachments and the
	 * user uses `~:' to change the current file.  TODO Unfortunately we
	 * TODO cannot simply keep a pointer to, or increment a reference count
	 * TODO of the current `file' (mailbox that is) object, because the
	 * TODO codebase doesn't deal with that at all; so, until some far
	 * TODO later time, copy the name of the path, and warn the user if it
	 * TODO changed; we COULD use the AC_TMPFILE attachment type, i.e.,
	 * TODO copy the message attachments over to temporary files, but that
	 * TODO would require more changes so that the user still can recognize
	 * TODO in `~@' etc. that its a rfc822 message attachment; see below */
	char *mnbuf = NULL;
	size_t mnlen = 0 /* silence CC */;
	struct attachment *ap;

	/* If the above todo is worked, remove or outsource to attachments.c! */
	if ((ap = hp->h_attach) != NULL) do
		if (ap->a_msgno) {
			mnlen = strlen(mailname) + 1;
			mnbuf = ac_alloc(mnlen);
			memcpy(mnbuf, mailname, mnlen);
			break;
		}
	while ((ap = ap->a_flink) != NULL);

	inhook = 0;
	execute(linebuf, TRU1, linesize);

	if (mnbuf != NULL) {
		if (strncmp(mnbuf, mailname, mnlen))
			fputs(tr(237, "Mailbox changed: it seems existing "
				"rfc822 attachments became invalid!\n"),
				stderr);
		ac_free(mnbuf);
	}
}

static int
_include_file(FILE *fbuf, char const *name, int *linecount, int *charcount,
	bool_t doecho)
{
	int ret = -1;
	char *linebuf = NULL;
	size_t linesize = 0, linelen, cnt;

	if (fbuf == NULL) {
		if ((fbuf = Fopen(name, "r")) == NULL) {
			perror(name);
			goto jleave;
		}
	} else
		fflush_rewind(fbuf);

	*linecount = *charcount = 0;
	cnt = fsize(fbuf);
	while (fgetline(&linebuf, &linesize, &cnt, &linelen, fbuf, 0)
			!= NULL) {
		if (fwrite(linebuf, sizeof *linebuf, linelen, _coll_fp)
				!= linelen)
			goto jleave;
		if ((options & OPT_INTERACTIVE) && doecho)
			fwrite(linebuf, sizeof *linebuf, linelen, stdout);
		++(*linecount);
		(*charcount) += linelen;
	}
	if (fflush(_coll_fp))
		goto jleave;

	ret = 0;
jleave:
	if (linebuf != NULL)
		free(linebuf);
	if (fbuf != NULL)
		Fclose(fbuf);
	return (ret);
}

/*ARGSUSED*/
static void
_collect_onpipe(int signo)
{
	UNUSED(signo);
	siglongjmp(_coll_pipejmp, 1);
}

/*
 * Execute cmd and insert its standard output into fp.
 */
static void
insertcommand(FILE *fp, char const *cmd)
{
	FILE *ibuf = NULL;
	char const *cp;
	int c;

	cp = ok_vlook(SHELL);
	if (cp == NULL)
		cp = XSHELL;
	if ((ibuf = Popen(cmd, "r", cp, 0)) != NULL) {
		while ((c = getc(ibuf)) != EOF) /* XXX bytewise, yuck! */
			putc(c, fp);
		Pclose(ibuf, TRU1);
	} else
		perror(cmd);
}

/*
 * ~p command.
 */
static void
print_collf(FILE *cf, struct header *hp)
{
	char *lbuf = NULL;
	FILE *volatile obuf = stdout;
	struct attachment *ap;
	char const *cp;
	enum gfield gf;
	size_t linecnt, maxlines, linesize = 0, linelen, cnt, cnt2;

	fflush(cf);
	rewind(cf);
	cnt = cnt2 = fsize(cf);

	if (IS_TTY_SESSION() && (cp = ok_vlook(crt)) != NULL) {
		for (linecnt = 0;
			fgetline(&lbuf, &linesize, &cnt2, NULL, cf, 0);
			linecnt++);
		rewind(cf);
		maxlines = (*cp == '\0' ? screensize() : atoi(cp));
		maxlines -= 4;
		if (hp->h_to)
			maxlines--;
		if (hp->h_subject)
			maxlines--;
		if (hp->h_cc)
			maxlines--;
		if (hp->h_bcc)
			maxlines--;
		if (hp->h_attach)
			maxlines--;
		maxlines -= myaddrs(hp) != NULL || hp->h_from != NULL;
		maxlines -= ok_vlook(ORGANIZATION) != NULL ||
			hp->h_organization != NULL;
		maxlines -= ok_vlook(replyto) != NULL || hp->h_replyto != NULL;
		maxlines -= ok_vlook(sender) != NULL || hp->h_sender != NULL;
		if ((long)maxlines < 0 || linecnt > maxlines) {
			cp = get_pager();
			if (sigsetjmp(_coll_pipejmp, 1))
				goto endpipe;
			obuf = Popen(cp, "w", NULL, 1);
			if (obuf == NULL) {
				perror(cp);
				obuf = stdout;
			} else
				safe_signal(SIGPIPE, &_collect_onpipe);
		}
	}

	fprintf(obuf, tr(62, "-------\nMessage contains:\n"));
	gf = GIDENT|GTO|GSUBJECT|GCC|GBCC|GNL|GFILES;
	if (ok_blook(fullnames))
		gf |= GCOMMA;
	puthead(hp, obuf, gf, SEND_TODISP, CONV_NONE, NULL, NULL);
	while (fgetline(&lbuf, &linesize, &cnt, &linelen, cf, 1))
		prout(lbuf, linelen, obuf);
	if (hp->h_attach != NULL) {
		fputs(tr(63, "-------\nAttachments:\n"), obuf);
		for (ap = hp->h_attach; ap != NULL; ap = ap->a_flink) {
			if (ap->a_msgno)
				fprintf(obuf, " - message %u\n", ap->a_msgno);
			else {
				/* TODO after MIME/send layer rewrite we *know*
				 * TODO the details of the attachment here,
				 * TODO so adjust this again, then */
				char const *cs, *csi = "-> ";

				if ((cs = ap->a_charset) == NULL &&
						(csi = "<- ",
						cs = ap->a_input_charset)
						== NULL)
					cs = charset_get_lc();
				if ((cp = ap->a_content_type) == NULL)
					cp = "?";
				else if (ascncasecmp(cp, "text/", 5) != 0)
					csi = "";

				fprintf(obuf, " - [%s, %s%s] %s\n",
					cp, csi, cs, ap->a_name);
			}
		}
	}
endpipe:
	if (obuf != stdout) {
		safe_signal(SIGPIPE, SIG_IGN);
		Pclose(obuf, TRU1);
		safe_signal(SIGPIPE, dflpipe);
	}
	if (lbuf)
		free(lbuf);
}

FL FILE *
collect(struct header *hp, int printheaders, struct message *mp,
		char *quotefile, int doprefix)
{
	FILE *fbuf;
	struct ignoretab *quoteig;
	int lc, cc, eofcount, c, t;
	int volatile escape, getfields;
	char *linebuf = NULL, *quote = NULL, *tempMail = NULL;
	char const *cp;
	size_t linesize = 0;
	long cnt;
	enum sendaction	action;
	sigset_t oset, nset;
	sighandler_type	savedtop;

	_coll_fp = NULL;
	/*
	 * Start catching signals from here, but we're still die on interrupts
	 * until we're in the main loop.
	 */
	sigemptyset(&nset);
	sigaddset(&nset, SIGINT);
	sigaddset(&nset, SIGHUP);
	sigprocmask(SIG_BLOCK, &nset, &oset);
	handlerpush(collint);
	if ((_coll_saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGINT, collint);
	if ((_coll_savehup = safe_signal(SIGHUP, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGHUP, collhup);
	/* TODO We do a lot of redundant signal handling, especially
	 * TODO with the command line editor(s); try to merge this */
	_coll_savetstp = safe_signal(SIGTSTP, collstop);
	_coll_savettou = safe_signal(SIGTTOU, collstop);
	_coll_savettin = safe_signal(SIGTTIN, collstop);
	if (sigsetjmp(_coll_abort, 1))
		goto jerr;
	if (sigsetjmp(_coll_jmp, 1))
		goto jerr;
	sigprocmask(SIG_SETMASK, &oset, (sigset_t *)NULL);

	noreset++;
	if ((_coll_fp = Ftemp(&tempMail, "Rs", "w+", 0600, 1)) == NULL) {
		perror(tr(51, "temporary mail file"));
		goto jerr;
	}
	unlink(tempMail);
	Ftfree(&tempMail);

	if ((cp = ok_vlook(NAIL_HEAD)) != NULL && putesc(cp, _coll_fp) < 0)
		goto jerr;

	/*
	 * If we are going to prompt for a subject,
	 * refrain from printing a newline after
	 * the headers (since some people mind).
	 */
	getfields = 0;
	if (! (options & OPT_t_FLAG)) {
		t = GTO|GSUBJECT|GCC|GNL;
		if (ok_blook(fullnames))
			t |= GCOMMA;
		if (hp->h_subject == NULL && (options & OPT_INTERACTIVE) &&
			    (ok_blook(ask) || ok_blook(asksub)))
			t &= ~GNL, getfields |= GSUBJECT;
		if (hp->h_to == NULL && (options & OPT_INTERACTIVE))
			t &= ~GNL, getfields |= GTO;
		if (!ok_blook(bsdcompat) && !ok_blook(askatend) &&
				(options & OPT_INTERACTIVE)) {
			if (hp->h_bcc == NULL && ok_blook(askbcc))
				t &= ~GNL, getfields |= GBCC;
			if (hp->h_cc == NULL && ok_blook(askcc))
				t &= ~GNL, getfields |= GCC;
		}
		if (printheaders) {
			(void)puthead(hp, stdout, t, SEND_TODISP, CONV_NONE,
					NULL, NULL);
			(void)fflush(stdout);
		}
	}

	/*
	 * Quote an original message
	 */
	if (mp != NULL && (doprefix || (quote = ok_vlook(quote)) != NULL)) {
		quoteig = allignore;
		action = SEND_QUOTE;
		if (doprefix) {
			quoteig = fwdignore;
			if ((cp = ok_vlook(fwdheading)) == NULL)
				cp = "-------- Original Message --------";
			if (*cp && fprintf(_coll_fp, "%s\n", cp) < 0)
				goto jerr;
		} else if (strcmp(quote, "noheading") == 0) {
			/*EMPTY*/;
		} else if (strcmp(quote, "headers") == 0) {
			quoteig = ignore;
		} else if (strcmp(quote, "allheaders") == 0) {
			quoteig = NULL;
			action = SEND_QUOTE_ALL;
		} else {
			cp = hfield1("from", mp);
			if (cp != NULL && (cnt = (long)strlen(cp)) > 0) {
				if (xmime_write(cp, cnt, _coll_fp,
						CONV_FROMHDR, TD_NONE,
						NULL) < 0)
					goto jerr;
				if (fprintf(_coll_fp,
						tr(52, " wrote:\n\n")) < 0)
					goto jerr;
			}
		}
		if (fflush(_coll_fp))
			goto jerr;
		cp = ok_vlook(indentprefix);
		if (cp != NULL && *cp == '\0')
			cp = "\t";
		if (sendmp(mp, _coll_fp, quoteig, (doprefix ? NULL : cp),
				action, NULL) < 0)
			goto jerr;
	}

	/* Print what we have sofar also on the terminal */
	rewind(_coll_fp);
	while ((c = getc(_coll_fp)) != EOF) /* XXX bytewise, yuck! */
		(void)putc(c, stdout);
	if (fseek(_coll_fp, 0, SEEK_END))
		goto jerr;

	escape = ((cp = ok_vlook(escape)) != NULL) ? *cp : ESCAPE;
	eofcount = 0;
	_coll_hadintr = 0;

	if (!sigsetjmp(_coll_jmp, 1)) {
		if (getfields)
			grab_headers(hp, getfields, 1);
		if (quotefile != NULL) {
			if (_include_file(NULL, quotefile, &lc, &cc, TRU1) != 0)
				goto jerr;
		}
		if ((options & OPT_INTERACTIVE) && ok_blook(editalong)) {
			rewind(_coll_fp);
			mesedit('e', hp);
			goto jcont;
		}
	} else {
		/*
		 * Come here for printing the after-signal message.
		 * Duplicate messages won't be printed because
		 * the write is aborted if we get a SIGTTOU.
		 */
jcont:
		if (_coll_hadintr) {
			(void)fprintf(stderr, tr(53,
				"\n(Interrupt -- one more to kill letter)\n"));
		} else {
			printf(tr(54, "(continue)\n"));
			(void)fflush(stdout);
		}
	}

	/*
	 * No tilde escapes, interrupts not expected.  Simply copy STDIN
	 */
	if (! (options & (OPT_INTERACTIVE | OPT_t_FLAG|OPT_TILDE_FLAG))) {
		linebuf = srealloc(linebuf, linesize = LINESIZE);
		while ((cnt = fread(linebuf, sizeof *linebuf,
						linesize, stdin)) > 0) {
			if ((size_t)cnt != fwrite(linebuf, sizeof *linebuf,
						cnt, _coll_fp))
				goto jerr;
		}
		if (fflush(_coll_fp))
			goto jerr;
		goto jout;
	}

	/*
	 * The interactive collect loop
	 */
	for (;;) {
		_coll_jmp_p = 1;
		cnt = readline_input(LNED_NONE, "", &linebuf, &linesize);
		_coll_jmp_p = 0;

		if (cnt < 0) {
			if ((options & OPT_INTERACTIVE) &&
			    ok_blook(ignoreeof) && ++eofcount < 25) {
				printf(tr(55,
					"Use \".\" to terminate letter\n"));
				continue;
			}
			break;
		}
		if ((options & OPT_t_FLAG) && cnt == 0) {
			rewind(_coll_fp);
			if (makeheader(_coll_fp, hp) != OKAY)
				goto jerr;
			rewind(_coll_fp);
			options &= ~OPT_t_FLAG;
			continue;
		}

		eofcount = 0;
		_coll_hadintr = 0;
		if (linebuf[0] == '.' && linebuf[1] == '\0' &&
				(options & (OPT_INTERACTIVE|OPT_TILDE_FLAG)) &&
				(ok_blook(dot) || ok_blook(ignoreeof)))
			break;
		if (cnt == 0 || linebuf[0] != escape || ! (options &
				(OPT_INTERACTIVE | OPT_TILDE_FLAG))) {
			/* TODO calls putline(), which *always* appends LF;
			 * TODO thus, STDIN with -t will ALWAYS end with LF,
			 * TODO even if no trailing LF and QP CTE */
			if (putline(_coll_fp, linebuf, cnt) < 0)
				goto jerr;
			continue;
		}

		c = linebuf[1];
		switch (c) {
		default:
			/*
			 * On double escape, just send the single one.
			 * Otherwise, it's an error.
			 */
			if (c == escape) {
				if (putline(_coll_fp, &linebuf[1], cnt - 1) < 0)
					goto jerr;
				else
					break;
			}
			fputs(tr(56, "Unknown tilde escape.\n"), stderr);
			break;
#ifdef HAVE_DEBUG
		case 'C':
			/* Dump core */
			core(NULL);
			break;
#endif
		case '!':
			/* Shell escape, send the balance of line to sh -c */
			shell(&linebuf[2]);
			break;
		case ':':
			/* FALLTHRU */
		case '_':
			/* Escape to command mode, but be nice! */
			_execute_command(hp, linebuf + 2, cnt - 2);
			goto jcont;
		case '.':
			/* Simulate end of file on input */
			goto jout;
		case 'x':
			/* Same as 'q', but no dead.letter saving */
			/* FALLTHRU */
		case 'q':
			/* Force a quit, act like an interrupt had happened */
			++_coll_hadintr;
			collint((c == 'x') ? 0 : SIGINT);
			exit(1);
			/*NOTREACHED*/
		case 'h':
			/* Grab a bunch of headers */
			do
				grab_headers(hp, GTO|GSUBJECT|GCC|GBCC,
						(ok_blook(bsdcompat) &&
						ok_blook(bsdorder)));
			while (hp->h_to == NULL);
			goto jcont;
		case 'H':
			/* Grab extra headers */
			do
				grab_headers(hp, GEXTRA, 0);
			while (check_from_and_sender(hp->h_from, hp->h_sender));
			goto jcont;
		case 't':
			/* Add to the To list */
			while ((hp->h_to = cat(hp->h_to, checkaddrs(
					lextract(&linebuf[2], GTO|GFULL))))
					== NULL)
				;
			break;
		case 's':
			/* Set the Subject list */
			cp = &linebuf[2];
			while (whitechar(*cp))
				++cp;
			hp->h_subject = savestr(cp);
			break;
#ifdef HAVE_DEBUG
		case 'S':
			c_sstats(NULL);
			break;
#endif
		case '@':
			/* Edit the attachment list */
			if (linebuf[2] != '\0')
				hp->h_attach = append_attachments(hp->h_attach,
						&linebuf[2]);
			else
				hp->h_attach = edit_attachments(hp->h_attach);
			break;
		case 'c':
			/* Add to the CC list */
			hp->h_cc = cat(hp->h_cc, checkaddrs(
					lextract(&linebuf[2], GCC|GFULL)));
			break;
		case 'b':
			/* Add stuff to blind carbon copies list */
			hp->h_bcc = cat(hp->h_bcc, checkaddrs(
					lextract(&linebuf[2], GBCC|GFULL)));
			break;
		case 'd':
			strncpy(linebuf + 2, getdeadletter(), linesize - 2);
			linebuf[linesize-1]='\0';
			/*FALLTHRU*/
		case 'r':
		case '<':
			/*
			 * Invoke a file:
			 * Search for the file name,
			 * then open it and copy the contents to _coll_fp.
			 */
			cp = &linebuf[2];
			while (whitechar(*cp))
				cp++;
			if (*cp == '\0') {
				fputs(tr(57, "Interpolate what file?\n"),
					stderr);
				break;
			}
			if (*cp == '!') {
				insertcommand(_coll_fp, cp + 1);
				break;
			}
			if ((cp = file_expand(cp)) == NULL)
				break;
			if (is_dir(cp)) {
				fprintf(stderr, tr(58, "%s: Directory\n"), cp);
				break;
			}
			if ((fbuf = Fopen(cp, "r")) == NULL) {
				perror(cp);
				break;
			}
			printf(tr(59, "\"%s\" "), cp);
			fflush(stdout);
			if (_include_file(fbuf, cp, &lc, &cc, FAL0) != 0)
				goto jerr;
			printf(tr(60, "%d/%d\n"), lc, cc);
			break;
		case 'i':
			/* Insert a variable into the file */
			cp = &linebuf[2];
			while (whitechar(*cp))
				++cp;
			if ((cp = vok_vlook(cp)) == NULL || *cp == '\0')
				break;
			if (putesc(cp, _coll_fp) < 0)
				goto jerr;
			if ((options & OPT_INTERACTIVE) &&
					putesc(cp, stdout) < 0)
				goto jerr;
			break;
		case 'a':
		case 'A':
			/* Insert the contents of a signature variable */
			cp = (c == 'a') ? ok_vlook(sign) : ok_vlook(Sign);
			if (cp != NULL && *cp != '\0') {
				if (putesc(cp, _coll_fp) < 0)
					goto jerr;
				if ((options & OPT_INTERACTIVE) &&
						putesc(cp, stdout) < 0)
					goto jerr;
			}
			break;
		case 'w':
			/* Write the message on a file */
			cp = &linebuf[2];
			while (blankchar(*cp))
				++cp;
			if (*cp == '\0' || (cp = file_expand(cp)) == NULL) {
				fputs(tr(61, "Write what file!?\n"), stderr);
				break;
			}
			rewind(_coll_fp);
			if (exwrite(cp, _coll_fp, 1) < 0)
				goto jerr;
			break;
		case 'm':
		case 'M':
		case 'f':
		case 'F':
		case 'u':
		case 'U':
			/*
			 * Interpolate the named messages, if we
			 * are in receiving mail mode.  Does the
			 * standard list processing garbage.
			 * If ~f is given, we don't shift over.
			 */
			if (forward(linebuf + 2, _coll_fp, c) < 0)
				goto jerr;
			goto jcont;
		case 'p':
			/*
			 * Print out the current state of the
			 * message without altering anything.
			 */
			print_collf(_coll_fp, hp);
			goto jcont;
		case '|':
			/*
			 * Pipe message through command.
			 * Collect output as new message.
			 */
			rewind(_coll_fp);
			mespipe(&linebuf[2]);
			goto jcont;
		case 'v':
		case 'e':
			/*
			 * Edit the current message.
			 * 'e' means to use EDITOR
			 * 'v' means to use VISUAL
			 */
			rewind(_coll_fp);
			mesedit(c, ok_blook(editheaders) ? hp : NULL);
			goto jcont;
		case '?':
			/*
			 * Last the lengthy help string.
			 * (Very ugly, but take care for compiler supported
			 * string lengths :()
			 */
			(void)puts(tr(300,
"-------------------- ~ ESCAPES ----------------------------\n"
"~~             Quote a single tilde\n"
"~@ [file ...]  Edit attachment list\n"
"~b users       Add users to \"blind\" cc list\n"
"~c users       Add users to cc list\n"
"~d             Read in dead.letter\n"
"~e             Edit the message buffer\n"
"~f messages    Read in messages without indenting lines\n"
"~F messages    Same as ~f, but keep all header lines\n"
"~h             Prompt for to list, subject, cc, and \"blind\" cc list\n"));
			(void)puts(tr(301,
"~r file        Read a file into the message buffer\n"
"~p             Print the message buffer\n"
"~q             Abort message composition and save text to dead.letter\n"
"~m messages    Read in messages with each line indented\n"
"~M messages    Same as ~m, but keep all header lines\n"
"~s subject     Set subject\n"
"~t users       Add users to to list\n"
"~u messages    Same as ~f, but without any headers\n"
"~U messages    Same as ~m, but without any headers\n"));
			(void)puts(tr(302,
"~v             Invoke display editor on message\n"
"~w file        Write message onto file\n"
"~x             Abort message composition and discard text written so far\n"
"~!command      Invoke the shell\n"
"~:command      Execute a regular command\n"
"-----------------------------------------------------------\n"));
			break;
		}
	}

jout:
	if (_coll_fp != NULL) {
		if ((cp = ok_vlook(NAIL_TAIL)) != NULL) {
			if (putesc(cp, _coll_fp) < 0)
				goto jerr;
			if ((options & OPT_INTERACTIVE) &&
					putesc(cp, stdout) < 0)
				goto jerr;
		}
		rewind(_coll_fp);
	}
	if (linebuf != NULL)
		free(linebuf);
	handlerpop();
	noreset--;
	sigemptyset(&nset);
	sigaddset(&nset, SIGINT);
	sigaddset(&nset, SIGHUP);
	sigprocmask(SIG_BLOCK, &nset, (sigset_t*)NULL);
	safe_signal(SIGINT, _coll_saveint);
	safe_signal(SIGHUP, _coll_savehup);
	safe_signal(SIGTSTP, _coll_savetstp);
	safe_signal(SIGTTOU, _coll_savettou);
	safe_signal(SIGTTIN, _coll_savettin);
	sigprocmask(SIG_SETMASK, &oset, (sigset_t*)NULL);
	return _coll_fp;

jerr:
	if (tempMail != NULL) {
		rm(tempMail);
		Ftfree(&tempMail);
	}
	if (_coll_fp != NULL) {
		Fclose(_coll_fp);
		_coll_fp = NULL;
	}
	goto jout;
}

/*
 * Write a file, ex-like if f set.
 */
static int
exwrite(char const *name, FILE *fp, int f)
{
	FILE *of;
	int c;
	long cc;
	int lc;

	if (f) {
		printf("\"%s\" ", name);
		fflush(stdout);
	}
	if ((of = Fopen(name, "a")) == NULL) {
		perror(NULL);
		return(-1);
	}
	lc = 0;
	cc = 0;
	while ((c = getc(fp)) != EOF) {
		cc++;
		if (c == '\n')
			lc++;
		putc(c, of);
		if (ferror(of)) {
			perror(name);
			Fclose(of);
			return(-1);
		}
	}
	Fclose(of);
	printf(tr(65, "%d/%ld\n"), lc, cc);
	fflush(stdout);
	return(0);
}

static enum okay
makeheader(FILE *fp, struct header *hp)
{
	char *tempEdit;
	FILE *nf;
	int c;

	if ((nf = Ftemp(&tempEdit, "Re", "w+", 0600, 1)) == NULL) {
		perror(tr(66, "temporary mail edit file"));
		return STOP;
	}
	unlink(tempEdit);
	Ftfree(&tempEdit);

	extract_header(fp, hp);
	while ((c = getc(fp)) != EOF) /* XXX bytewise, yuck! */
		putc(c, nf);
	if (fp != _coll_fp)
		Fclose(_coll_fp);
	Fclose(fp);
	_coll_fp = nf;
	if (check_from_and_sender(hp->h_from, hp->h_sender))
		return STOP;
	return OKAY;
}

/*
 * Edit the message being collected on fp.
 * On return, make the edit file the new temp file.
 */
static void
mesedit(int c, struct header *hp)
{
	sighandler_type sigint = safe_signal(SIGINT, SIG_IGN);
	bool_t saved = ok_blook(add_file_recipients);
	FILE *nf;

	ok_bset(add_file_recipients, FAL0);
	nf = run_editor(_coll_fp, (off_t)-1, c, 0, hp, NULL, SEND_MBOX, sigint);

	if (nf != NULL) {
		if (hp) {
			rewind(nf);
			makeheader(nf, hp);
		} else {
			fseek(nf, 0L, SEEK_END);
			Fclose(_coll_fp);
			_coll_fp = nf;
		}
	}

	ok_bset(add_file_recipients, saved);
	safe_signal(SIGINT, sigint);
}

/*
 * Pipe the message through the command.
 * Old message is on stdin of command;
 * New message collected from stdout.
 * Sh -c must return 0 to accept the new message.
 */
static void
mespipe(char *cmd)
{
	FILE *nf;
	sighandler_type sigint = safe_signal(SIGINT, SIG_IGN);
	char *tempEdit;
	char const *sh;

	if ((nf = Ftemp(&tempEdit, "Re", "w+", 0600, 1)) == NULL) {
		perror(tr(66, "temporary mail edit file"));
		goto out;
	}
	fflush(_coll_fp);
	unlink(tempEdit);
	Ftfree(&tempEdit);
	/*
	 * stdin = current message.
	 * stdout = new message.
	 */
	if ((sh = ok_vlook(SHELL)) == NULL)
		sh = XSHELL;
	if (run_command(sh, 0, fileno(_coll_fp), fileno(nf), "-c", cmd, NULL)
			< 0) {
		Fclose(nf);
		goto out;
	}
	if (fsize(nf) == 0) {
		fprintf(stderr, tr(67, "No bytes from \"%s\" !?\n"), cmd);
		Fclose(nf);
		goto out;
	}
	/*
	 * Take new files.
	 */
	fseek(nf, 0L, SEEK_END);
	Fclose(_coll_fp);
	_coll_fp = nf;
out:
	safe_signal(SIGINT, sigint);
}

/*
 * Interpolate the named messages into the current
 * message, preceding each line with a tab.
 * Return a count of the number of characters now in
 * the message, or -1 if an error is encountered writing
 * the message temporary.  The flag argument is 'm' if we
 * should shift over and 'f' if not.
 */
static int
forward(char *ms, FILE *fp, int f)
{
	int *msgvec;
	struct ignoretab *ig;
	char const *tabst;
	enum sendaction	action;

	/*LINTED*/
	msgvec = (int *)salloc((msgCount+1) * sizeof *msgvec);
	if (msgvec == NULL)
		return(0);
	if (getmsglist(ms, msgvec, 0) < 0)
		return(0);
	if (*msgvec == 0) {
		*msgvec = first(0, MMNORM);
		if (*msgvec == 0) {
			fputs(tr(68, "No appropriate messages\n"), stderr);
			return 0;
		}
		msgvec[1] = 0;
	}

	if (f == 'f' || f == 'F' || f == 'u')
		tabst = NULL;
	else if ((tabst = ok_vlook(indentprefix)) == NULL)
		tabst = "\t";
	if (f == 'u' || f == 'U')
		ig = allignore;
	else
		ig = upperchar(f) ? (struct ignoretab*)NULL : ignore;
	action = (upperchar(f) && f != 'U') ? SEND_QUOTE_ALL : SEND_QUOTE;

	printf(tr(69, "Interpolating:"));
	for (; *msgvec != 0; msgvec++) {
		struct message *mp = message + *msgvec - 1;

		touch(mp);
		printf(" %d", *msgvec);
		if (sendmp(mp, fp, ig, tabst, action, NULL) < 0) {
			perror(tr(70, "temporary mail file"));
			return(-1);
		}
	}
	printf("\n");
	return(0);
}

/*
 * Print (continue) when continued after ^Z.
 */
/*ARGSUSED*/
static void
collstop(int s)
{
	sighandler_type old_action = safe_signal(s, SIG_DFL);
	sigset_t nset;

	sigemptyset(&nset);
	sigaddset(&nset, s);
	sigprocmask(SIG_UNBLOCK, &nset, (sigset_t *)NULL);
	kill(0, s);
	sigprocmask(SIG_BLOCK, &nset, (sigset_t *)NULL);
	safe_signal(s, old_action);
	if (_coll_jmp_p) {
		_coll_jmp_p = 0;
		_coll_hadintr = 0;
		siglongjmp(_coll_jmp, 1);
	}
}

/*
 * On interrupt, come here to save the partial message in ~/dead.letter.
 * Then jump out of the collection loop.
 */
/*ARGSUSED*/
static void
collint(int s)
{
	/* the control flow is subtle, because we can be called from ~q */
	if (_coll_hadintr == 0) {
		if (ok_blook(ignore)) {
			puts("@");
			fflush(stdout);
			clearerr(stdin);
			return;
		}
		_coll_hadintr = 1;
		siglongjmp(_coll_jmp, 1);
	}
	exit_status |= 04;
	if (ok_blook(save) && s != 0)
		savedeadletter(_coll_fp, 1);
	/* Aborting message, no need to fflush() .. */
	siglongjmp(_coll_abort, 1);
}

/*ARGSUSED*/
static void
collhup(int s)
{
	(void)s;
	savedeadletter(_coll_fp, 1);
	/*
	 * Let's pretend nobody else wants to clean up,
	 * a true statement at this time.
	 */
	exit(1);
}

FL void
savedeadletter(FILE *fp, int fflush_rewind_first)
{
	char const *cp;
	int c;
	FILE *dbuf;
	ul_it lines, bytes;

	if (fflush_rewind_first) {
		(void)fflush(fp);
		rewind(fp);
	}
	if (fsize(fp) == 0)
		goto jleave;

	cp = getdeadletter();
	c = umask(077);
	dbuf = Fopen(cp, "a");
	umask(c);
	if (dbuf == NULL)
		goto jleave;

	/*
	 * There are problems with dup()ing of file-descriptors for child
	 * processes.  As long as those are not fixed in equal spirit to
	 * (outof(): FIX and recode.., 2012-10-04), and to avoid reviving of
	 * bugs like (If *record* is set, avoid writing dead content twice..,
	 * 2012-09-14), we have to somehow accomplish that the FILE* fp
	 * makes itself comfortable with the *real* offset of the underlaying
	 * file descriptor.  Unfortunately Standard I/O and POSIX don't
	 * describe a way for that -- fflush();rewind(); won't do it.
	 * This fseek(END),rewind() pair works around the problem on *BSD.
	 */
	(void)fseek(fp, 0, SEEK_END);
	rewind(fp);

	printf("\"%s\" ", cp);
	for (lines = bytes = 0; (c = getc(fp)) != EOF; ++bytes) {
		putc(c, dbuf);
		if (c == '\n')
			++lines;
	}
	printf("%lu/%lu\n", lines, bytes);

	Fclose(dbuf);
	rewind(fp);
jleave:	;
}

static int
putesc(const char *s, FILE *stream)
{
	int	n = 0;

	while (s[0]) {
		if (s[0] == '\\') {
			if (s[1] == 't') {
				if (putc('\t', stream) == EOF)
					return (-1);
				n++;
				s += 2;
				continue;
			}
			if (s[1] == 'n') {
				if (putc('\n', stream) == EOF)
					return (-1);
				n++;
				s += 2;
				continue;
			}
		}
		if (putc(s[0], stream) == EOF)
			return (-1);
		n++;
		s++;
	}
	if (putc('\n', stream) == EOF)
		return (-1);
	return (++n);
}
