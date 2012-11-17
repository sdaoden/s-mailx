/*
 * S-nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 Steffen "Daode" Nurpmeso.
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

/*
 * Mail -- a mail program
 *
 * Collect input from standard input, handling
 * ~ escapes.
 */

#include "rcv.h"

#include <sys/stat.h>
#include <unistd.h>

#include "extern.h"

/*
 * Read a message from standard output and return a read file to it
 * or NULL on error.
 */

/*
 * The following hokiness with global variables is so that on
 * receipt of an interrupt signal, the partial message can be salted
 * away on dead.letter.
 */

static	sighandler_type		saveint;	/* Previous SIGINT value */
static	sighandler_type		savehup;	/* Previous SIGHUP value */
static	sighandler_type		savetstp;	/* Previous SIGTSTP value */
static	sighandler_type		savettou;	/* Previous SIGTTOU value */
static	sighandler_type		savettin;	/* Previous SIGTTIN value */
static	FILE	*collf;			/* File for saving away */
static	int	hadintr;		/* Have seen one SIGINT so far */

static	sigjmp_buf	colljmp;	/* To get back to work */
static	int		colljmp_p;	/* whether to long jump */
static	sigjmp_buf	collabort;	/* To end collection with error */

static	sigjmp_buf pipejmp;		/* On broken pipe */

static void onpipe(int signo);
static void insertcommand(FILE *fp, char *cmd);
static void print_collf(FILE *collf, struct header *hp);
static int include_file(FILE *fbuf, char *name, int *linecount,
		int *charcount, int echo);
static struct attachment *read_attachment_data(struct attachment *ap,
		unsigned number);
static struct attachment *append_attachments(struct attachment *attach,
		char *names);
static int exwrite(char *name, FILE *fp, int f);
static enum okay makeheader(FILE *fp, struct header *hp);
static void mesedit(int c, struct header *hp);
static void mespipe(char *cmd);
static int forward(char *ms, FILE *fp, int f);
static void collstop(int s);
static void collint(int s);
static void collhup(int s);
static int putesc(const char *s, FILE *stream);

/*ARGSUSED*/
static void 
onpipe(int signo)
{
	(void)signo;
	siglongjmp(pipejmp, 1);
}

/*
 * Execute cmd and insert its standard output into fp.
 */
static void
insertcommand(FILE *fp, char *cmd)
{
	FILE *obuf = NULL;
	char *volatile cp;
	int c;

	cp = value("SHELL");
	if (sigsetjmp(pipejmp, 1))
		goto endpipe;
	if (cp == NULL)
		cp = SHELL;
	if ((obuf = Popen(cmd, "r", cp, 0)) == NULL) {
		perror(cmd);
		return;
	}
	safe_signal(SIGPIPE, onpipe);
	while ((c = getc(obuf)) != EOF)
		putc(c, fp);
endpipe:
	safe_signal(SIGPIPE, SIG_IGN);
	Pclose(obuf);
	safe_signal(SIGPIPE, dflpipe);
}

/*
 * ~p command.
 */
static void
print_collf(FILE *collf, struct header *hp)
{
	char *lbuf = NULL;
	FILE *volatile obuf = stdout;
	struct attachment *ap;
	char *cp;
	enum gfield	gf;
	size_t	linecnt, maxlines, linesize = 0, linelen, count, count2;

	(void)&obuf;
	(void)&cp;
	fflush(collf);
	rewind(collf);
	count = count2 = fsize(collf);
	if (is_a_tty[0] && is_a_tty[1] && (cp = value("crt")) != NULL) {
		for (linecnt = 0;
			fgetline(&lbuf, &linesize, &count2, NULL, collf, 0);
			linecnt++);
		rewind(collf);
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
		maxlines -= value("ORGANIZATION") != NULL ||
			hp->h_organization != NULL;
		maxlines -= value("replyto") != NULL || hp->h_replyto != NULL;
		maxlines -= value("sender") != NULL || hp->h_sender != NULL;
		if ((long)maxlines < 0 || linecnt > maxlines) {
			cp = get_pager();
			if (sigsetjmp(pipejmp, 1))
				goto endpipe;
			obuf = Popen(cp, "w", NULL, 1);
			if (obuf == NULL) {
				perror(cp);
				obuf = stdout;
			} else
				safe_signal(SIGPIPE, onpipe);
		}
	}
	fprintf(obuf, catgets(catd, CATSET, 62,
				"-------\nMessage contains:\n"));
	gf = GIDENT|GTO|GSUBJECT|GCC|GBCC|GNL|GFILES;
	if (value("fullnames"))
		gf |= GCOMMA;
	puthead(hp, obuf, gf, SEND_TODISP, CONV_NONE, NULL, NULL);
	while (fgetline(&lbuf, &linesize, &count, &linelen, collf, 1))
		prout(lbuf, linelen, obuf);
	if (hp->h_attach != NULL) {
		fputs(catgets(catd, CATSET, 63, "Attachments:"), obuf);
		for (ap = hp->h_attach; ap != NULL; ap = ap->a_flink) {
			if (ap->a_msgno)
				fprintf(obuf, " message %u", ap->a_msgno);
			else
				fprintf(obuf, " %s", ap->a_name);
			if (ap->a_flink)
				putc(',', obuf);
		}
		putc('\n', obuf);
	}
endpipe:
	if (obuf != stdout) {
		safe_signal(SIGPIPE, SIG_IGN);
		Pclose(obuf);
		safe_signal(SIGPIPE, dflpipe);
	}
	if (lbuf)
		free(lbuf);
}

static int
include_file(FILE *fbuf, char *name, int *linecount, int *charcount, int echo)
{
	char *interactive, *linebuf = NULL;
	size_t linesize = 0, linelen, count;

	if (fbuf == NULL)
		fbuf = Fopen(name, "r");
	if (fbuf == NULL) {
		perror(name);
		return (-1);
	}
	interactive = value("interactive");
	*linecount = 0;
	*charcount = 0;
	fflush(fbuf);
	rewind(fbuf);
	count = fsize(fbuf);
	while (fgetline(&linebuf, &linesize, &count, &linelen, fbuf, 0)
			!= NULL) {
		if (fwrite(linebuf, sizeof *linebuf, linelen, collf)
				!= linelen) {
			Fclose(fbuf);
			return (-1);
		}
		if (interactive != NULL && echo)
			fwrite(linebuf, sizeof *linebuf, linelen, stdout);
		(*linecount)++;
		(*charcount) += linelen;
	}
	if (linebuf)
		free(linebuf);
	Fclose(fbuf);
	if (fflush(collf))
		return (-1);
	return (0);
}

/*
 * Ask the user to edit file names and other data for the given
 * attachment. NULL is returned if no file name is given.
 */
static struct attachment *
read_attachment_data(struct attachment *ap, unsigned number)
{
	char prefix[80], *cp;

	if (ap == NULL)
		ap = csalloc(1, sizeof *ap);
	if (ap->a_msgno) {
		printf("#%u\tmessage %u\n", number, ap->a_msgno);
		return ap;
	}
	snprintf(prefix, sizeof prefix, catgets(catd, CATSET, 50,
			"#%u\tfilename: "), number);
	for (;;) {
		char *exf;
		if ((ap->a_name = readtty(prefix, ap->a_name)) == NULL)
			break;
		if ((exf = file_expand(ap->a_name)) == NULL)
			continue;
		ap->a_name = exf;
		if (access(ap->a_name, R_OK) == 0)
			break;
		perror(ap->a_name);
	}
	if ((ap->a_name && (value("attachment-ask-charset"))) ||
			((cp = value("sendcharsets")) != NULL &&
			 strchr(cp, ',') != NULL)) {
		snprintf(prefix, sizeof prefix, "#%u\tcharset: ", number);
		ap->a_charset = readtty(prefix, ap->a_charset);
	}
	/*
	 * The "attachment-ask-content-*" variables are left undocumented
	 * since they are for RFC connoisseurs only.
	 */
	if (ap->a_name && value("attachment-ask-content-type")) {
		if (ap->a_content_type == NULL)
			ap->a_content_type = mime_filecontent(ap->a_name);
		snprintf(prefix, sizeof prefix, "#%u\tContent-Type: ", number);
		ap->a_content_type = readtty(prefix, ap->a_content_type);
	}
	if (ap->a_name && value("attachment-ask-content-disposition")) {
		snprintf(prefix, sizeof prefix,
				"#%u\tContent-Disposition: ", number);
		ap->a_content_disposition = readtty(prefix,
				ap->a_content_disposition);
	}
	if (ap->a_name && value("attachment-ask-content-id")) {
		snprintf(prefix, sizeof prefix, "#%u\tContent-ID: ", number);
		ap->a_content_id = readtty(prefix, ap->a_content_id);
	}
	if (ap->a_name && value("attachment-ask-content-description")) {
		snprintf(prefix, sizeof prefix,
				"#%u\tContent-Description: ", number);
		ap->a_content_description = readtty(prefix,
				ap->a_content_description);
	}
	return ap->a_name ? ap : NULL;
}

/*
 * Interactively edit the attachment list.
 */
struct attachment *
edit_attachments(struct attachment *attach)
{
	struct attachment *ap, *nap;
	unsigned attno = 1;

	for (ap = attach; ap; ap = ap->a_flink) {
		if ((nap = read_attachment_data(ap, attno)) == NULL) {
			if (ap->a_blink)
				ap->a_blink->a_flink = ap->a_flink;
			else
				attach = ap->a_flink;
			if (ap->a_flink)
				ap->a_flink->a_blink = ap->a_blink;
			else
				return attach;
		} else
			attno++;
	}
	while ((nap = read_attachment_data(NULL, attno)) != NULL) {
		for (ap = attach; ap && ap->a_flink; ap = ap->a_flink);
		if (ap)
			ap->a_flink = nap;
		nap->a_blink = ap;
		nap->a_flink = NULL;
		if (attach == NULL)
			attach = nap;
		attno++;
	}
	return attach;
}

/*
 * Put the given file to the end of the attachment list.
 */
struct attachment *
add_attachment(struct attachment *attach, char *file, int expand_file)
{
	struct attachment *ap, *nap;

	if (expand_file) {
		if ((file = file_expand(file)) == NULL)
			return (NULL);
	} else
		file = savestr(file);
	if (access(file, R_OK) != 0)
		return NULL;
	/*LINTED*/
	nap = csalloc(1, sizeof *nap);
	nap->a_name = file;
	if (attach != NULL) {
		for (ap = attach; ap->a_flink != NULL; ap = ap->a_flink);
		ap->a_flink = nap;
		nap->a_blink = ap;
	} else {
		nap->a_blink = NULL;
		attach = nap;
	}
	return attach;
}

/*
 * Append the whitespace-separated file names to the end of
 * the attachment list.
 */
static struct attachment *
append_attachments(struct attachment *attach, char *names)
{
	char *cp;
	int c;
	struct attachment *ap;

	cp = names;
	while (*cp != '\0' && blankchar(*cp & 0377))
		cp++;
	while (*cp != '\0') {
		names = cp;
		while (*cp != '\0' && !blankchar(*cp & 0377))
			cp++;
		c = *cp;
		*cp++ = '\0';
		if (*names != '\0') {
			if ((ap = add_attachment(attach, names, 1)) == NULL)
				perror(names);
			else
				attach = ap;
		}
		if (c == '\0')
			break;
		while (*cp != '\0' && blankchar(*cp & 0377))
			cp++;
	}
	return attach;
}

FILE *
collect(struct header *hp, int printheaders, struct message *mp,
		char *quotefile, int doprefix, int volatile tflag)
{
	enum {
		val_INTERACT	= 1
	};

	FILE *fbuf;
	struct ignoretab *quoteig;
	int lc, cc, eofcount, val, c, t;
	int volatile escape, getfields;
	char *linebuf = NULL, *cp, *quote = NULL, *tempMail = NULL;
	size_t linesize;
	long count;
	enum sendaction	action;
	sigset_t oset, nset;
	sighandler_type	savedtop;

	val = 0;
	if (value("interactive") != NULL)
		val |= val_INTERACT;

	collf = NULL;
	/*
	 * Start catching signals from here, but we're still die on interrupts
	 * until we're in the main loop.
	 */
	sigemptyset(&nset);
	sigaddset(&nset, SIGINT);
	sigaddset(&nset, SIGHUP);
	sigprocmask(SIG_BLOCK, &nset, &oset);
	handlerpush(collint);
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGINT, collint);
	if ((savehup = safe_signal(SIGHUP, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGHUP, collhup);
	savetstp = safe_signal(SIGTSTP, collstop);
	savettou = safe_signal(SIGTTOU, collstop);
	savettin = safe_signal(SIGTTIN, collstop);
	if (sigsetjmp(collabort, 1))
		goto jerr;
	if (sigsetjmp(colljmp, 1))
		goto jerr;
	sigprocmask(SIG_SETMASK, &oset, (sigset_t *)NULL);

	noreset++;
	if ((collf = Ftemp(&tempMail, "Rs", "w+", 0600, 1)) == NULL) {
		perror(tr(51, "temporary mail file"));
		goto jerr;
	}
	unlink(tempMail);
	Ftfree(&tempMail);

	if ((cp = value("NAIL_HEAD")) != NULL && putesc(cp, collf) < 0)
		goto jerr;

	/*
	 * If we are going to prompt for a subject,
	 * refrain from printing a newline after
	 * the headers (since some people mind).
	 */
	getfields = 0;
	if (! tflag) {
		t = GTO|GSUBJECT|GCC|GNL;
		if (value("fullnames"))
			t |= GCOMMA;
		if (hp->h_subject == NULL && (val & val_INTERACT) &&
			    (value("ask") != NULL || value("asksub") != NULL))
			t &= ~GNL, getfields |= GSUBJECT;
		if (hp->h_to == NULL && (val & val_INTERACT))
			t &= ~GNL, getfields |= GTO;
		if (value("bsdcompat") == NULL && value("askatend") == NULL &&
				(val & val_INTERACT)) {
			if (hp->h_bcc == NULL && value("askbcc"))
				t &= ~GNL, getfields |= GBCC;
			if (hp->h_cc == NULL && value("askcc"))
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
	if (mp != NULL && (doprefix || (quote = value("quote")) != NULL)) {
		quoteig = allignore;
		action = SEND_QUOTE;
		if (doprefix) {
			quoteig = fwdignore;
			if ((cp = value("fwdheading")) == NULL)
				cp = "-------- Original Message --------";
			if (*cp && fprintf(collf, "%s\n", cp) < 0)
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
			if (cp != NULL && (count = (long)strlen(cp)) > 0) {
				if (mime_write(cp, count,
						collf, CONV_FROMHDR, TD_NONE,
						NULL, (size_t) 0, NULL) < 0)
					goto jerr;
				if (fprintf(collf, tr(52, " wrote:\n\n")) < 0)
					goto jerr;
			}
		}
		if (fflush(collf))
			goto jerr;
		cp = value("indentprefix");
		if (cp != NULL && *cp == '\0')
			cp = "\t";
		if (send(mp, collf, quoteig, (doprefix ? NULL : cp), action,
				NULL) < 0)
			goto jerr;
	}

	/* Print what we have sofar also on the terminal */
	(void)rewind(collf);
	while ((c = getc(collf)) != EOF)
		(void)putc(c, stdout);
	if (fseek(collf, 0, SEEK_END))
		goto jerr;

	escape = ((cp = value("escape")) != NULL) ? *cp : ESCAPE;
	eofcount = 0;
	hadintr = 0;

	if (! sigsetjmp(colljmp, 1)) {
		if (getfields)
			grabh(hp, getfields, 1);
		if (quotefile != NULL) {
			if (include_file(NULL, quotefile, &lc, &cc, 1) != 0)
				goto jerr;
		}
	} else {
		/*
		 * Come here for printing the after-signal message.
		 * Duplicate messages won't be printed because
		 * the write is aborted if we get a SIGTTOU.
		 */
jcont:
		if (hadintr) {
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
	if ((val & val_INTERACT) == 0 && ! tildeflag && ! tflag) {
		linebuf = srealloc(linebuf, linesize = LINESIZE);
		while ((count = fread(linebuf, sizeof *linebuf,
						linesize, stdin)) > 0) {
			if ((size_t)count != fwrite(linebuf, sizeof *linebuf,
						count, collf))
				goto jerr;
		}
		if (fflush(collf))
			goto jerr;
		goto jout;
	}

	/*
	 * The interactive collect loop
	 */
	for (;;) {
		colljmp_p = 1;
		count = readline(stdin, &linebuf, &linesize);
		colljmp_p = 0;
		if (count < 0) {
			if ((val & val_INTERACT) &&
			    value("ignoreeof") != NULL && ++eofcount < 25) {
				printf(tr(55,
					"Use \".\" to terminate letter\n"));
				continue;
			}
			break;
		}
		if (tflag && count == 0) {
			rewind(collf);
			if (makeheader(collf, hp) != OKAY)
				goto jerr;
			rewind(collf);
			tflag = 0;
			continue;
		}
		eofcount = 0;
		hadintr = 0;
		if (linebuf[0] == '.' && linebuf[1] == '\0' &&
				(val & val_INTERACT) &&
				(value("dot") != NULL ||
					value("ignoreeof") != NULL))
			break;
		if (linebuf[0] != escape ||
				(! (val & val_INTERACT) && tildeflag == 0)) {
			if (putline(collf, linebuf, count) < 0)
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
				if (putline(collf, &linebuf[1], count - 1) < 0)
					goto jerr;
				else
					break;
			}
			printf(tr(56, "Unknown tilde escape.\n"));
			break;
#ifdef HAVE_ASSERTS
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
		case '_':
			/* Escape to command mode, but be nice! */
			inhook = 0;
			execute(&linebuf[2], 1, count - 2);
			goto jcont;
		case '.':
			/* Simulate end of file on input */
			goto jout;
		case 'x':
			/* Same as 'q', but no dead.letter saving */
			hadintr++;
			collint(0);
			exit(1);
			/*NOTREACHED*/
		case 'q':
			/*
			 * Force a quit of sending mail.
			 * Act like an interrupt happened.
			 */
			hadintr++;
			collint(SIGINT);
			exit(1);
			/*NOTREACHED*/
		case 'h':
			/* Grab a bunch of headers */
			do
				grabh(hp, GTO|GSUBJECT|GCC|GBCC,
						value("bsdcompat") != NULL &&
						value("bsdorder") != NULL);
			while (hp->h_to == NULL);
			goto jcont;
		case 'H':
			/* Grab extra headers */
			do
				grabh(hp, GEXTRA, 0);
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
#ifdef HAVE_ASSERTS
		case 'S':
			sstats(NULL);
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
			 * then open it and copy the contents to collf.
			 */
			cp = &linebuf[2];
			while (whitechar(*cp))
				cp++;
			if (*cp == '\0') {
				fprintf(stderr, tr(57,
					"Interpolate what file?\n"));
				break;
			}
			if (*cp == '!') {
				insertcommand(collf, cp + 1);
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
			if (include_file(fbuf, cp, &lc, &cc, 0) != 0)
				goto jerr;
			printf(tr(60, "%d/%d\n"), lc, cc);
			break;
		case 'i':
			/* Insert an environment variable into the file */
			cp = &linebuf[2];
			while (whitechar(*cp))
				++cp;
			if ((cp = value(cp)) == NULL || *cp == '\0')
				break;
			if (putesc(cp, collf) < 0)
				goto jerr;
			if ((val & val_INTERACT) && putesc(cp, stdout) < 0)
				goto jerr;
			break;
		case 'a':
		case 'A':
			/* Insert the contents of a signature variable */
			if ((cp = value(c == 'a' ? "sign" : "Sign")) != NULL &&
					*cp != '\0') {
				if (putesc(cp, collf) < 0)
					goto jerr;
				if ((val & val_INTERACT) &&
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
				fprintf(stderr, tr(61, "Write what file!?\n"));
				break;
			}
			rewind(collf);
			if (exwrite(cp, collf, 1) < 0)
				goto jerr;
			break;
		case 'm':
		case 'M':
		case 'f':
		case 'F':
			/*
			 * Interpolate the named messages, if we
			 * are in receiving mail mode.  Does the
			 * standard list processing garbage.
			 * If ~f is given, we don't shift over.
			 */
			if (forward(linebuf + 2, collf, c) < 0)
				goto jerr;
			goto jcont;
		case 'p':
			/*
			 * Print out the current state of the
			 * message without altering anything.
			 */
			print_collf(collf, hp);
			goto jcont;
		case '|':
			/*
			 * Pipe message through command.
			 * Collect output as new message.
			 */
			rewind(collf);
			mespipe(&linebuf[2]);
			goto jcont;
		case 'v':
		case 'e':
			/*
			 * Edit the current message.
			 * 'e' means to use EDITOR
			 * 'v' means to use VISUAL
			 */
			rewind(collf);
			mesedit(c, value("editheaders") ? hp : NULL);
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
"~v             Invoke display editor on message\n"
"~w file        Write message onto file\n"
"~x             Abort message composition and discard text written so far\n"));
			(void)puts(tr(302,
"~!command      Invoke the shell\n"
"~:command      Execute a regular command\n"
"-----------------------------------------------------------\n"));
			break;

		}
	}
jout:
	if (collf != NULL) {
		if ((cp = value("NAIL_TAIL")) != NULL) {
			if (putesc(cp, collf) < 0)
				goto jerr;
			if ((val & val_INTERACT) && putesc(cp, stdout) < 0)
				goto jerr;
		}
		rewind(collf);
	}
	handlerpop();
	noreset--;
	sigemptyset(&nset);
	sigaddset(&nset, SIGINT);
	sigaddset(&nset, SIGHUP);
	sigprocmask(SIG_BLOCK, &nset, (sigset_t*)NULL);
	safe_signal(SIGINT, saveint);
	safe_signal(SIGHUP, savehup);
	safe_signal(SIGTSTP, savetstp);
	safe_signal(SIGTTOU, savettou);
	safe_signal(SIGTTIN, savettin);
	sigprocmask(SIG_SETMASK, &oset, (sigset_t*)NULL);
	return (collf);
jerr:
	if (tempMail != NULL) {
		rm(tempMail);
		Ftfree(&tempMail);
	}
	if (collf != NULL) {
		Fclose(collf);
		collf = NULL;
	}
	goto jout;
}

/*
 * Write a file, ex-like if f set.
 */
static int
exwrite(char *name, FILE *fp, int f)
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
	printf(catgets(catd, CATSET, 65, "%d/%ld\n"), lc, cc);
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
		perror(catgets(catd, CATSET, 66, "temporary mail edit file"));
		Fclose(nf);
		unlink(tempEdit);
		Ftfree(&tempEdit);
		return STOP;
	}
	unlink(tempEdit);
	Ftfree(&tempEdit);
	extract_header(fp, hp);
	while ((c = getc(fp)) != EOF)
		putc(c, nf);
	if (fp != collf)
		Fclose(collf);
	Fclose(fp);
	collf = nf;
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
	char *saved = value("add-file-recipients");
	FILE *nf;

	assign("add-file-recipients", "");
	nf = run_editor(collf, (off_t)-1, c, 0, hp, NULL, SEND_MBOX, sigint);

	if (nf != NULL) {
		if (hp) {
			rewind(nf);
			makeheader(nf, hp);
		} else {
			fseek(nf, 0L, SEEK_END);
			Fclose(collf);
			collf = nf;
		}
	}

	assign("add-file-recipients", saved);
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
	char *shell;

	if ((nf = Ftemp(&tempEdit, "Re", "w+", 0600, 1)) == NULL) {
		perror(catgets(catd, CATSET, 66, "temporary mail edit file"));
		goto out;
	}
	fflush(collf);
	unlink(tempEdit);
	Ftfree(&tempEdit);
	/*
	 * stdin = current message.
	 * stdout = new message.
	 */
	if ((shell = value("SHELL")) == NULL)
		shell = SHELL;
	if (run_command(shell,
	    0, fileno(collf), fileno(nf), "-c", cmd, NULL) < 0) {
		Fclose(nf);
		goto out;
	}
	if (fsize(nf) == 0) {
		fprintf(stderr, catgets(catd, CATSET, 67,
				"No bytes from \"%s\" !?\n"), cmd);
		Fclose(nf);
		goto out;
	}
	/*
	 * Take new files.
	 */
	fseek(nf, 0L, SEEK_END);
	Fclose(collf);
	collf = nf;
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
	char *tabst;
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
			printf(catgets(catd, CATSET, 68,
					"No appropriate messages\n"));
			return(0);
		}
		msgvec[1] = 0;
	}
	if (f == 'f' || f == 'F')
		tabst = NULL;
	else if ((tabst = value("indentprefix")) == NULL)
		tabst = "\t";
	ig = upperchar(f) ? (struct ignoretab *)NULL : ignore;
	action = upperchar(f) ? SEND_QUOTE_ALL : SEND_QUOTE;
	printf(catgets(catd, CATSET, 69, "Interpolating:"));
	for (; *msgvec != 0; msgvec++) {
		struct message *mp = message + *msgvec - 1;

		touch(mp);
		printf(" %d", *msgvec);
		if (send(mp, fp, ig, tabst, action, NULL) < 0) {
			perror(catgets(catd, CATSET, 70,
					"temporary mail file"));
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
	if (colljmp_p) {
		colljmp_p = 0;
		hadintr = 0;
		siglongjmp(colljmp, 1);
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
	/*
	 * the control flow is subtle, because we can be called from ~q.
	 */
	if (!hadintr) {
		if (value("ignore") != NULL) {
			puts("@");
			fflush(stdout);
			clearerr(stdin);
			return;
		}
		hadintr = 1;
		siglongjmp(colljmp, 1);
	}
	exit_status |= 04;
	if (value("save") != NULL && s != 0)
		savedeadletter(collf, 1);
	/* Aborting message, no need to fflush() .. */
	siglongjmp(collabort, 1);
}

/*ARGSUSED*/
static void 
collhup(int s)
{
	(void)s;
	savedeadletter(collf, 1);
	/*
	 * Let's pretend nobody else wants to clean up,
	 * a true statement at this time.
	 */
	exit(1);
}

void
savedeadletter(FILE *fp, int fflush_rewind_first)
{
	char *cp;
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
