/*	$Id: sendout.c,v 1.2 2000/04/16 23:05:28 gunnar Exp $	*/
/*	OpenBSD: send.c,v 1.6 1996/06/08 19:48:39 christos Exp 	*/
/*	NetBSD: send.c,v 1.6 1996/06/08 19:48:39 christos Exp 	*/

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
#if 0
static char sccsid[]  = "@(#)send.c	8.1 (Berkeley) 6/6/93";
static char rcsid[]  = "OpenBSD: send.c,v 1.6 1996/06/08 19:48:39 christos Exp";
#else
static char rcsid[]  = "@(#)$Id: sendout.c,v 1.2 2000/04/16 23:05:28 gunnar Exp $";
#endif
#endif /* not lint */

#include <sys/utsname.h>
#include "rcv.h"
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * Mail to others.
 */

static char *send_boundary;

static char *
makeboundary()
/* Generate a boundary for MIME multipart-messages */
{
	static unsigned msgcount;
	static pid_t pid;
	static char bound[73];
	time_t t;
	int r1, r2, r3, r4;
	char b1[sizeof(int) * 2 + 1],
		b2[sizeof(int) * 2 + 1],
		b3[sizeof(int) * 2 + 1],
		b4[sizeof(int) * 2 + 1];

	if (pid == 0) {
		pid = getpid();
		msgcount = pid;
	}
	msgcount *= 2053 * time(&t);
	srand(msgcount);
	r1 = rand(), r2 = rand(), r3 = rand(), r4 = rand();
	sprintf(bound, "%.8s-=-%.8s-CUT-HERE-%.8s-=-%.8s",
			itohex(r1, b1), itohex(r2, b2),
			itohex(r3, b3), itohex(r4, b4));
	send_boundary = bound;
	return send_boundary;
}

static char *
getcharset(convert)
{
	char *charset;

	if (convert != CONV_7BIT) {
		charset = value("charset");
		if (charset == NULL) {
			charset = defcharset;
			assign("charset", charset);
		}
	} else {
		charset = "US-ASCII";
	}
	return charset;
}

static char *
getencoding(convert)
{
	switch (convert) {
	case CONV_7BIT:
		return "7bit";
		break;
	case CONV_NONE:
		return "8bit";
		break;
	case CONV_TOQP:
		return "quoted-printable";
		break;
	case CONV_TOB64:
		return "base64";
		break;
	}
	abort();
}

static int
gettextconversion()
{
	char *p;
	int convert;

	p = value("encoding");
	if (p  == NULL)
		return CONV_NONE;
	if (equal(p, "quoted-printable")) {
		convert = CONV_TOQP;
	} else if (equal(p, "8bit")) {
		convert = CONV_NONE;
	} else {
		fprintf(stderr, "Warning: invalid encoding %s, using 8bit\n",
				p);
		convert = CONV_NONE;
	}
	return convert;
}

/*
 * Fix the header by glopping all of the expanded names from
 * the distribution list into the appropriate fields.
 */
static void
fixhead(hp, tolist)
	struct header *hp;
	struct name *tolist;
{
	struct name *np;

	hp->h_to = NIL;
	hp->h_cc = NIL;
	hp->h_bcc = NIL;
	for (np = tolist; np != NIL; np = np->n_flink)
		if ((np->n_type & GMASK) == GTO)
			hp->h_to =
				cat(hp->h_to, nalloc(np->n_name, np->n_type));
		else if ((np->n_type & GMASK) == GCC)
			hp->h_cc =
				cat(hp->h_cc, nalloc(np->n_name, np->n_type));
		else if ((np->n_type & GMASK) == GBCC)
			hp->h_bcc =
				cat(hp->h_bcc, nalloc(np->n_name, np->n_type));
}


#define	INFIX_BUF	972	/* Do not change
				 * you get incorrect base64 encodings else!
				 */
static int
put_signature(fo, convert)
FILE *fo;
{
	char *sig, buf[INFIX_BUF], c = '\n';
	FILE *fsig;
	size_t sz;

	sig = value("signature");
	if (sig == NULL || *sig == '\0') {
		return 0;
	} else {
		sig = expand(sig);
	}
	fsig = Fopen(sig, "r");
	if (fsig == NULL) {
		perror(sig);
		return -1;
	}
	while ((sz = fread(buf, sizeof(char), INFIX_BUF, fsig)) != 0) {
		c = buf[sz - 1];
		mime_write(buf, sizeof(char), sz, fo, convert, 0);
	}
	if (ferror(fsig)) {
		perror(sig);
		Fclose(fsig);
		return -1;
	}
	Fclose(fsig);
	if (c != '\n')
		fputc('\n', fo);
	return 0;
}

static int
attach_file(path, fo)
char *path;
FILE *fo;
{
	FILE *fi;
	char *charset = NULL, *contenttype, *basename, c = '\n';
	int convert = CONV_TOB64, typefound = 0, err = 0;
	size_t sz;
	char buf[INFIX_BUF];

	fi = Fopen(path, "r");
	if (fi == NULL) {
		perror(path);
		return -1;
	}
	basename = strrchr(path, '/');
	if (basename == NULL)
		basename = path;
	else
		basename++;
	if ((contenttype = mime_filecontent(basename)) != NULL) {
		typefound = 1;
	}
	switch (mime_isclean(fi)) {
	case 0:
		convert = CONV_7BIT;
		if (contenttype == NULL)
			contenttype = "text/plain";
		charset = getcharset(convert);
		break;
	case 1:
		convert = gettextconversion();
		if (contenttype == NULL)
			contenttype = "text/plain";
		charset = getcharset(convert);
		break;
	case 2:
		convert = CONV_TOB64;
		if (contenttype == NULL)
			contenttype = "application/octet-stream";
		charset = NULL;
		break;
	}
	if (convert == CONV_TOB64
			&& strncasecmp(contenttype, "text/", 5) == 0) {
		convert = CONV_TOQP;
	}
	fprintf(fo,
		"--%s\n"
		"Content-Type: %s",
		send_boundary, contenttype);
	if (charset == NULL)
		fputc('\n', fo);
	else
		fprintf(fo, ";\n charset=%s\n", charset);
	fprintf(fo, "Content-Transfer-Encoding: %s\n"
		"Content-Disposition: attachment;\n"
		" filename=\"",
		getencoding(convert));
	mime_write(basename, sizeof(char), strlen(basename), fo,
			CONV_TOHDR, 0);
	fwrite("\"\n\n", sizeof(char), 3, fo);
	if (typefound) free(contenttype);
	while ((sz = fread(buf, sizeof(char), INFIX_BUF, fi)) != 0) {
		c = buf[sz - 1];
		mime_write(buf, sizeof(char), sz, fo, convert, 0);
	}
	if (ferror(fi))
		err = -1;
	if (c != '\n')
		fputc('\n', fo);
	Fclose(fi);
	return err;
}

static int
make_multipart(hp, convert, fi, fo)
struct header *hp;
FILE *fi, *fo;
/* Generate the body of a MIME multipart message */
{
	char buf[INFIX_BUF], c = '\n';
	size_t sz;
	struct name *att;

	fprintf(fo,
		"This is a multi-part message in MIME format.\n"
		"--%s\n"
		"Content-Type: text/plain; charset=%s\n",
		send_boundary, getcharset(convert));
	fprintf(fo, "Content-Transfer-Encoding: %s\n\n", getencoding(convert));
	while ((sz = fread(buf, sizeof(char), INFIX_BUF, fi)) != 0) {
		c = buf[sz - 1];
		mime_write(buf, sizeof(char), sz, fo, convert, 0);
	}
	if (ferror(fi)) {
		return -1;
	}
	if (c != '\n')
		fputc('\n', fo);
	put_signature(fo, convert);
	for (att = hp->h_attach; att != NIL; att = att->n_flink) {
		if (attach_file(att->n_name, fo) != 0) {
			return -1;
		}
	}
	/* the final boundary with two attached dashes */
	fprintf(fo, "--%s--\n", send_boundary);
	return 0;
}

/*
 * Prepend a header in front of the collected stuff
 * and return the new file.
 */
static FILE *
infix(hp, fi, convert)
	struct header *hp;
	FILE *fi;
{
	extern char *tempMail;
	FILE *nfo, *nfi;
	size_t sz;
	char buf[INFIX_BUF];

	if ((nfo = Fopen(tempMail, "w")) == (FILE*)NULL) {
		perror(tempMail);
		return(NULL);
	}
	if ((nfi = Fopen(tempMail, "r")) == (FILE*)NULL) {
		perror(tempMail);
		(void) Fclose(nfo);
		return(NULL);
	}
	if (mime_isclean(fi) == 0) {
		convert = CONV_7BIT;
	}
	(void) rm(tempMail);
	(void) puthead(hp, nfo,
		   GTO|GSUBJECT|GCC|GBCC|GNL|GCOMMA|GXMAIL|GMIME
		   |GMSGID|GATTACH|GIDENT|GREF|GDATE,
		   convert);
	if (hp->h_attach != NIL) {
		if (make_multipart(hp, convert, fi, nfo) != 0) {
			(void) Fclose(nfo);
			(void) Fclose(nfi);
			return NULL;
		}
	} else {
		while ((sz = fread(buf, sizeof(char), INFIX_BUF, fi)) != 0) {
			mime_write(buf, sizeof(char), sz, nfo, convert, 0);
		}
		if (ferror(fi)) {
			(void) Fclose(nfo);
			(void) Fclose(nfi);
			perror("read");
			return NULL;
		}
		put_signature(nfo, convert);
	}
	(void) fflush(nfo);
	if (ferror(nfo)) {
		perror(tempMail);
		(void) Fclose(nfo);
		(void) Fclose(nfi);
		return NULL;
	}
	(void) Fclose(nfo);
	(void) Fclose(fi);
	rewind(nfi);
	return(nfi);
}

/*
 * Save the outgoing mail on the passed file.
 */

/*ARGSUSED*/
int
savemail(name, fi)
	char name[];
	FILE *fi;
{
	FILE *fo;
	char buf[BUFSIZ];
	char *p;
	time_t now;
	int newfile = 0;

	if (access(name, 0) < 0)
		newfile = 1;
	if ((fo = Fopen(name, "a")) == (FILE*)NULL) {
		perror(name);
		return (-1);
	}
	if (newfile == 0)
		fputc('\n', fo);
	(void) time(&now);
	fprintf(fo, "From %s %s", myname, ctime(&now));
	while (fgets(buf, sizeof(buf), fi) != NULL) {
		if (*buf == '>') {
			p = buf + 1;
			while (*p == '>') p++;
			if (strncmp(p, "From ", 5) == 0) {
				/* we got a masked From line */
				fputc('>', fo);
			}
		} else if (strncmp(buf, "From ", 5) == 0) {
			fputc('>', fo);
		}
		fputs(buf, fo);
	}
	(void) putc('\n', fo);
	/* Some programs (notably netscape) expect this */
	(void) putc('\n', fo);
	(void) fflush(fo);
	if (ferror(fo))
		perror(name);
	(void) Fclose(fo);
	rewind(fi);
	return (0);
}

/*
 * Interface between the argument list and the mail1 routine
 * which does all the dirty work.
 */
int
mail(to, cc, bcc, smopts, subject, attach)
	struct name *to, *cc, *bcc, *smopts, *attach;
	char *subject;
{
	struct header head;

	head.h_to = to;
	head.h_subject = subject;
	head.h_cc = cc;
	head.h_bcc = bcc;
	head.h_ref = NIL;
	head.h_attach = attach;
	head.h_smopts = smopts;
	mail1(&head, 0, NULL);
	return(0);
}

/*
 * Send mail to a bunch of user names.  The interface is through
 * the mail routine below.
 */
int
sendmail(v)
	void *v;
{
	char *str = v;
	struct header head;

	head.h_to = extract(str, GTO);
	head.h_subject = NOSTR;
	head.h_cc = NIL;
	head.h_bcc = NIL;
	head.h_ref = NIL;
	head.h_attach = NIL;
	head.h_smopts = NIL;
	mail1(&head, 0, NULL);
	return(0);
}

/*
 * Mail a message on standard input to the people indicated
 * in the passed header.  (Internal interface).
 */
void
mail1(hp, printheaders, quote)
	struct header *hp;
	int printheaders;
	struct message *quote;
{
	char *cp;
	int pid;
	char **namelist;
	struct name *to;
	FILE *mtf, *nmtf;
	int convert;

	convert = gettextconversion();
	/*
	 * Collect user's mail from standard input.
	 * Get the result as mtf.
	 */
	if ((mtf = collect(hp, printheaders, quote)) == (FILE*)NULL)
		return;
	if (value("interactive") != NOSTR) {
		if (value("askcc") != NOSTR || value("askbcc") != NOSTR
				|| value("askattach") != NOSTR) {
			if (value("askcc") != NOSTR)
				grabh(hp, GCC);
			if (value("askbcc") != NOSTR)
				grabh(hp, GBCC);
			if (value("askattach") != NOSTR) {
				do {
					grabh(hp, GATTACH);
				} while (mime_check_attach(hp->h_attach) != 0);
			}

		} else {
			printf("EOT\n");
			(void) fflush(stdout);
		}
	}
	if (fsize(mtf) == 0) {
		if (hp->h_subject == NOSTR)
			printf("No message, no subject; hope that's ok\n");
		else
			printf("Null message body; hope that's ok\n");
	}
	/*
	 * Now, take the user names from the combined
	 * to and cc lists and do all the alias
	 * processing.
	 */
	senderr = 0;
	to = usermap(cat(hp->h_bcc, cat(hp->h_to, hp->h_cc)));
	if (to == NIL) {
		printf("No recipients specified\n");
		senderr++;
	}
	fixhead(hp, to);
	if ((nmtf = infix(hp, mtf, convert)) == (FILE*)NULL) {
		/* fprintf(stderr, ". . . message lost, sorry.\n"); */
		rewind(mtf);
		savedeadletter(mtf);
		fputs(". . . message not sent.\n", stderr);
		return;
	}
	mtf = nmtf;
	/*
	 * Look through the recipient list for names with /'s
	 * in them which we write to as files directly.
	 */
	to = outof(to, mtf, hp);
	if (senderr)
		savedeadletter(mtf);
	to = elide(to);
	if (count(to) == 0)
		goto out;
	namelist = unpack(cat(hp->h_smopts, to));
	if (debug) {
		char **t;

		printf("Sendmail arguments:");
		for (t = namelist; *t != NOSTR; t++)
			printf(" \"%s\"", *t);
		printf("\n");
		goto out;
	}
	if ((cp = value("record")) != NOSTR)
		(void) savemail(expand(cp), mtf);
	/*
	 * Fork, set up the temporary mail file as standard
	 * input for "mail", and exec with the user list we generated
	 * far above.
	 */
	pid = fork();
	if (pid == -1) {
		perror("fork");
		savedeadletter(mtf);
		goto out;
	}
	if (pid == 0) {
		sigset_t nset;
		sigemptyset(&nset);
		sigaddset(&nset, SIGHUP);
		sigaddset(&nset, SIGINT);
		sigaddset(&nset, SIGQUIT);
		sigaddset(&nset, SIGTSTP);
		sigaddset(&nset, SIGTTIN);
		sigaddset(&nset, SIGTTOU);
		prepare_child(&nset, fileno(mtf), -1);
		if ((cp = value("sendmail")) != NOSTR)
			cp = expand(cp);
		else
			cp = _PATH_SENDMAIL;
		execv(cp, namelist);
		perror(cp);
		_exit(1);
	}
	if (value("verbose") != NOSTR)
		(void) wait_child(pid);
	else
		free_child(pid);
out:
	(void) Fclose(mtf);
}
/*
 * Create a Message-Id: header field.
 * Use either the host name or the host part of the from address.
 */
static void
message_id(fo)
FILE *fo;
{
	static unsigned msgcount;
	struct utsname ut;
	char datestr[sizeof(time_t) * 2 + 1];
	char pidstr[sizeof(pid_t) * 2 + 1];
	char countstr[sizeof(unsigned) * 2 + 1];
	time_t t;
	char *fromaddr;

	time(&t);
	itohex((unsigned int)t, datestr);
	itohex((unsigned int)getpid(), pidstr);
	itohex(++msgcount, countstr);
	fprintf(fo, "Message-ID: <%s.%s%s%s",
			datestr, progname, pidstr, countstr);
	if ((fromaddr = skin(value("from")))
			&& (fromaddr = strchr(fromaddr, '@'))) {
		fprintf(fo, "%s>\n", fromaddr);
	} else {
		uname(&ut);
		fprintf(fo, "@%s>\n", ut.nodename);
	}
}

const static char *weekday_names[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

const static char *month_names[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/*
 * Create a Date: header field.
 * We compare the localtime() and gmtime() results to get the timezone,
 * because numeric timezones are easier to read and because $TZ is
 * not set on most GNU systems.
 */
static void
date_field(fo)
FILE *fo;
{
	time_t t;
	struct tm *tmptr;
	int tzdiff_hour, tzdiff_min;

	time(&t);
	tzdiff_hour = ((int)((t - mktime(gmtime(&t)))) / 60);
	tzdiff_min = tzdiff_hour % 60;
	tzdiff_hour /= 60;
	tmptr = localtime(&t);
	if (tmptr->tm_isdst > 0)
		tzdiff_hour++;
	fprintf(fo, "Date: %s, %02d %s %04d %02d:%02d:%02d %+03d%02d\n",
			weekday_names[tmptr->tm_wday],
			tmptr->tm_mday, month_names[tmptr->tm_mon],
			tmptr->tm_year + 1900, tmptr->tm_hour,
			tmptr->tm_min, tmptr->tm_sec,
			tzdiff_hour, tzdiff_min);
}

/*
 * Dump the to, subject, cc header on the
 * passed file buffer.
 */
int
puthead(hp, fo, w, convert)
	struct header *hp;
	FILE *fo;
	int w;
{
	int gotcha;
	char *addr;
	int stealthmua;


	if (value("stealthmua"))
		stealthmua = 1;
	else
		stealthmua = 0;
	gotcha = 0;
	if (w & GDATE) {
		date_field(fo), gotcha++;
	}
	if (w & GIDENT) {
		addr = value("from");
		if (addr != NULL) {
			fprintf(fo, "Sender: %s\n", username());
			fwrite("From: ", sizeof(char), 6, fo);
			mime_write(addr, sizeof(char), strlen(addr), fo,
					convert == CONV_TODISP ?
					CONV_NONE:CONV_TOHDR,
					convert == CONV_TODISP), gotcha++;
			fputc('\n', fo);
		}
		addr = value("ORGANIZATION");
		if (addr != NULL) {
			fwrite("Organization: ", sizeof(char), 14, fo);
			mime_write(addr, sizeof(char), strlen(addr), fo,
					convert == CONV_TODISP ?
					CONV_NONE:CONV_TOHDR,
					convert == CONV_TODISP), gotcha++;
			fputc('\n', fo);
		}
		addr = value("replyto");
		if (addr != NULL) {
			fwrite("Reply-To: ", sizeof(char), 10, fo);
			mime_write(addr, sizeof(char), strlen(addr), fo,
					convert == CONV_TODISP ?
					CONV_NONE:CONV_TOHDR,
					convert == CONV_TODISP), gotcha++;
			fputc('\n', fo);
		}
	}
	if (hp->h_to != NIL && w & GTO)
		fmt("To:", hp->h_to, fo, w&GCOMMA), gotcha++;
	if (hp->h_subject != NOSTR && w & GSUBJECT) {
		fwrite("Subject: ", sizeof(char), 9, fo);
		mime_write(hp->h_subject, sizeof(char), strlen(hp->h_subject),
				fo, convert == CONV_TODISP ?
				CONV_NONE:CONV_TOHDR,
				convert == CONV_TODISP), gotcha++;
		fwrite("\n", sizeof(char), 1, fo);
	}
	if (hp->h_cc != NIL && w & GCC)
		fmt("Cc:", hp->h_cc, fo, w&GCOMMA), gotcha++;
	if (hp->h_bcc != NIL && w & GBCC)
		fmt("Bcc:", hp->h_bcc, fo, w&GCOMMA), gotcha++;
	if (w & GMSGID && stealthmua == 0) {
		message_id(fo), gotcha++;
	}
	if (hp->h_ref != NIL && w & GREF)
		fmt("References:", hp->h_ref, fo, 0), gotcha++;
	if (w & GXMAIL && stealthmua == 0) {
		fprintf(fo, "X-Mailer: %s\n", version), gotcha++;
	}
	if (w & GMIME) {
		fputs("MIME-Version: 1.0\n", fo), gotcha++;
		if (hp->h_attach != NIL && w & GATTACH) {
			makeboundary();
			fprintf(fo, "Content-Type: multipart/mixed;\n"
				" boundary=\"%s\"\n", send_boundary);
		} else {
			fprintf(fo,
				"Content-Type: text/plain; charset=%s\n"
				"Content-Transfer-Encoding: %s\n",
				getcharset(convert), getencoding(convert));
		}
	}
	if (gotcha && w & GNL)
		(void) putc('\n', fo);
	return(0);
}

/*
 * Format the given header line to not exceed 72 characters.
 */
void
fmt(str, np, fo, comma)
	char *str;
	struct name *np;
	FILE *fo;
	int comma;
{
	int col, len;
	int is_to = 0;

	comma = comma ? 1 : 0;
	col = strlen(str);
	if (col) {
		mime_write(str, sizeof(char), strlen(str), fo, CONV_TOHDR, 0);
		if (col == 3 && strcasecmp(str, "To:") == 0)
			is_to = 1;
	}
	for (; np != NIL; np = np->n_flink) {
		if (is_to && isfileaddr(np->n_name))
			continue;
		if (np->n_flink == NIL)
			comma = 0;
		len = strlen(np->n_name);
		col++;		/* for the space */
		if (col + len + comma > 72 && col > 4) {
			fputs("\n    ", fo);
			col = 4;
		} else
			putc(' ', fo);
		mime_write(np->n_name, sizeof(char), strlen(np->n_name),
				fo, CONV_TOHDR, 0);
		if (comma && !(is_to && isfileaddr(np->n_flink->n_name)))
			putc(',', fo);
		col += len + comma;
	}
	putc('\n', fo);
}
