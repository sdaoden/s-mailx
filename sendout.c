/*	$Id: sendout.c,v 1.15 2000/08/20 22:33:41 gunnar Exp $	*/
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
static char rcsid[]  = "@(#)$Id: sendout.c,v 1.15 2000/08/20 22:33:41 gunnar Exp $";
#endif
#endif /* not lint */

#include "rcv.h"
#include "extern.h"
#include <errno.h>

/*
 * Mail -- a mail program
 *
 * Mail to others.
 */

const char randfile[] = "/dev/urandom";
extern char *tempMail;
static char *send_boundary;

#define	BOUNDARRAY	8

/*
 * Generate a boundary for MIME multipart messages.
 */
static char *
makeboundary()
{
	int i, bd;
	static unsigned msgc;
	static pid_t pid;
	static char bound[73];
	time_t t;
	unsigned long r[BOUNDARRAY];
	char b[BOUNDARRAY][sizeof(r[0]) * 2 + 1];

	if (pid == 0) {
		pid = getpid();
		msgc = (unsigned)pid;
	}
	msgc *= 2053 * (unsigned)time(&t);
	bd = open(randfile, O_RDONLY);
	if (bd != -1) {
		for (i = 0; i < BOUNDARRAY; i++) {
			if (read(bd, &r[i], sizeof(r[0])) != sizeof(r[0])) {
				r[0] = 0L;
				break;
			}
		}
		close(bd);
	} else {
		r[0] = 0L;
	}
	if (r[0] == 0L) {
		srand((unsigned)msgc);
		for (i = 0; i < BOUNDARRAY; i++) {
			r[i] = 1L;
			while (r[i] < 60466176L)
				r[i] *= (unsigned long)rand();
		}
	}
	snprintf(bound, 73,
			"%.5s%.5s-=-%.5s%.5s-CUT-HERE-%.5s%.5s-=-%.5s%.5s",
			itostr(36, (unsigned)r[0], b[0]),
			itostr(36, (unsigned)r[1], b[1]),
			itostr(36, (unsigned)r[2], b[2]),
			itostr(36, (unsigned)r[3], b[3]),
			itostr(36, (unsigned)r[4], b[4]),
			itostr(36, (unsigned)r[5], b[5]),
			itostr(36, (unsigned)r[6], b[6]),
			itostr(36, (unsigned)r[7], b[7]));
	send_boundary = bound;
	return send_boundary;
}

/*
 * Get an encoding flag based on the given string.
 */
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

/*
 * Get the conversion that matches the encoding specified in the environment.
 */
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


/*
 * Do not change, you get incorrect base64 encodings else!
 */
#define	INFIX_BUF	972

/*
 * Put the signature file at fo.
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
		if (mime_write(buf, sizeof(char), sz, fo, convert, TD_NONE)
				== 0) {
			perror(sig);
			Fclose(fsig);
			return -1;
		}
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

/*
 * Write an attachment to the file buffer, converting to MIME.
 */
static int
attach_file(path, fo)
char *path;
FILE *fo;
{
	FILE *fi;
	char *charset = NULL, *contenttype, *basename;
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
	case MIME_7BITTEXT:
		convert = CONV_7BIT;
		if (contenttype == NULL)
			contenttype = "text/plain";
		charset = getcharset(convert);
		break;
	case MIME_INTERTEXT:
		convert = gettextconversion();
		if (contenttype == NULL)
			contenttype = "text/plain";
		charset = getcharset(convert);
		break;
	case MIME_BINARY:
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
			CONV_TOHDR, TD_NONE);
	fwrite("\"\n\n", sizeof(char), 3, fo);
	if (typefound) free(contenttype);
	for (;;) {
		if (convert == CONV_TOQP) {
			if (fgets(buf, INFIX_BUF, fi) == NULL)
				break;
			sz = strlen(buf);
		} else {
			sz = fread(buf, sizeof(char), INFIX_BUF, fi);
			if (sz == 0)
				break;
		}
		if (mime_write(buf, sizeof(char), sz, fo, convert, TD_NONE)
				== 0)
			err = -1;
	}
	if (ferror(fi))
		err = -1;
	fputc('\n', fo);
	Fclose(fi);
	return err;
}

/*
 * Generate the body of a MIME multipart message.
 */
static int
make_multipart(hp, convert, fi, fo)
struct header *hp;
FILE *fi, *fo;
{
	char buf[INFIX_BUF], c = '\n';
	size_t sz;
	struct name *att;

	fputs("This is a multi-part message in MIME format.\n", fo);
	if (fsize(fi) != 0) {
		fprintf(fo, "--%s\n"
				"Content-Type: text/plain; charset=%s\n"
				"Content-Transfer-Encoding: %s\n\n",
				send_boundary,
				getcharset(convert),
				getencoding(convert));
		for (;;) {
			if (convert == CONV_TOQP) {
				if (fgets(buf, INFIX_BUF, fi) == NULL)
					break;
				sz = strlen(buf);
			} else {
				sz = fread(buf, sizeof(char), INFIX_BUF, fi);
				if (sz == 0)
					break;
			}
			c = buf[sz - 1];
			if (mime_write(buf, sizeof(char), sz, fo, convert,
					TD_ICONV) == 0)
				return -1;
		}
		if (ferror(fi)) {
			return -1;
		}
		if (c != '\n')
			fputc('\n', fo);
		put_signature(fo, convert);
	}
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
#ifdef	HAVE_ICONV
	char *cs, *tcs;
#endif

	if ((nfo = Fopen(tempMail, "w")) == (FILE*)NULL) {
		perror(tempMail);
		return(NULL);
	}
	if ((nfi = Fopen(tempMail, "r")) == (FILE*)NULL) {
		perror(tempMail);
		(void) Fclose(nfo);
		return(NULL);
	}
	(void) rm(tempMail);
#ifdef	HAVE_ICONV
	cs = getcharset(convert);
	if (mime_isclean(fi) == MIME_7BITTEXT)
		convert = CONV_7BIT;
	tcs = gettcharset();
	if (strcmp(cs, tcs)) {
		if (iconvd != (iconv_t) -1)
			iconv_close(iconvd);
		iconvd = iconv_open_ft(cs, tcs);
		if (iconvd == (iconv_t) -1) {
			if (errno == EINVAL)
				fprintf(stderr,
			"Cannot convert from %s to %s\n", tcs, cs);
			else
				perror("iconv_open");
			Fclose(nfo);
			return NULL;
		}
	}
#else	/* !HAVE_ICONV */
	if (mime_isclean(fi) == MIME_7BITTEXT) {
		convert = CONV_7BIT;
	}
#endif	/* !HAVE_ICONV */
	if (puthead(hp, nfo,
		   GTO|GSUBJECT|GCC|GBCC|GNL|GCOMMA|GUA|GMIME
		   |GMSGID|GATTACH|GIDENT|GREF|GDATE,
		   convert)) {
		(void) Fclose(nfo);
		(void) Fclose(nfi);
#ifdef	HAVE_ICONV
		if (iconvd != (iconv_t) -1) {
			iconv_close(iconvd);
			iconvd = (iconv_t) -1;
		}
#endif
		return NULL;
	}
	if (hp->h_attach != NIL) {
		if (make_multipart(hp, convert, fi, nfo) != 0) {
			(void) Fclose(nfo);
			(void) Fclose(nfi);
#ifdef	HAVE_ICONV
			if (iconvd != (iconv_t) -1) {
				iconv_close(iconvd);
				iconvd = (iconv_t) -1;
			}
#endif
			return NULL;
		}
	} else {
		for (;;) {
			if (convert == CONV_TOQP) {
				if (fgets(buf, INFIX_BUF, fi) == NULL)
					break;
				sz = strlen(buf);
			} else {
				sz = fread(buf, sizeof(char), INFIX_BUF, fi);
				if (sz == 0)
					break;
			}
			if (mime_write(buf, sizeof(char), sz,
						nfo, convert,
					TD_ICONV) == 0) {
				(void) Fclose(nfo);
				(void) Fclose(nfi);
				perror("read");
#ifdef	HAVE_ICONV
				if (iconvd != (iconv_t) -1) {
					iconv_close(iconvd);
					iconvd = (iconv_t) -1;
				}
#endif
				return NULL;
			}
		}
		if (ferror(fi)) {
			(void) Fclose(nfo);
			(void) Fclose(nfi);
			perror("read");
#ifdef	HAVE_ICONV
			if (iconvd != (iconv_t) -1) {
				iconv_close(iconvd);
				iconvd = (iconv_t) -1;
			}
#endif
			return NULL;
		}
		put_signature(nfo, convert);
	}
#ifdef	HAVE_ICONV
	if (iconvd != (iconv_t) -1) {
		iconv_close(iconvd);
		iconvd = (iconv_t) -1;
	}
#endif
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
mail(to, cc, bcc, smopts, subject, attach, quotefile)
	struct name *to, *cc, *bcc, *smopts, *attach;
	char *subject, *quotefile;
{
	struct header head;
	struct str in, out;

	/* The given subject may be in RFC1522 format. */
	if (subject != NULL) {
		in.s = subject;
		in.l = strlen(subject);
		mime_fromhdr(&in, &out, TD_ISPR | TD_ICONV);
		head.h_subject = out.s;
	} else {
		head.h_subject = NULL;
	}
	head.h_to = to;
	head.h_cc = cc;
	head.h_bcc = bcc;
	head.h_ref = NIL;
	head.h_attach = attach;
	head.h_smopts = smopts;
	mail1(&head, 0, NULL, quotefile);
	if (subject != NULL)
		free(out.s);
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
	mail1(&head, 0, NULL, NULL);
	return(0);
}

/*
 * Start the Mail Transfer Agent
 * mailing to namelist and stdin redirected to input.
 */
int
start_mta(to, mailargs, input)
struct name *to, *mailargs;
FILE* input;
{
	char **args = NULL, **t;
	pid_t pid;
	sigset_t nset;
	char *cp, *smtp;

	smtp = value("smtp");
	if (smtp == NULL) {
		args = unpack(cat(mailargs, to));
		if (debug) {
			printf("Sendmail arguments:");
			for (t = args; *t != NOSTR; t++)
				printf(" \"%s\"", *t);
			printf("\n");
			return 0;
		}
	}
	/*
	 * Fork, set up the temporary mail file as standard
	 * input for "mail", and exec with the user list we generated
	 * far above.
	 */
	pid = fork();
	if (pid == -1) {
		perror("fork");
		savedeadletter(input);
		return 1;
	}
	if (pid == 0) {
		sigemptyset(&nset);
		sigaddset(&nset, SIGHUP);
		sigaddset(&nset, SIGINT);
		sigaddset(&nset, SIGQUIT);
		sigaddset(&nset, SIGTSTP);
		sigaddset(&nset, SIGTTIN);
		sigaddset(&nset, SIGTTOU);
		if (smtp != NULL) {
			prepare_child(&nset, 0, 1);
			if (smtp_mta(smtp, to, input) == 0)
				_exit(0);
		} else {
			prepare_child(&nset, fileno(input), -1);
			if ((cp = value("sendmail")) != NOSTR)
				cp = expand(cp);
			else
				cp = PATH_SENDMAIL;
			execv(cp, args);
			perror(cp);
		}
		savedeadletter(input);
		fputs(". . . message not sent.\n", stderr);
		_exit(1);
	}
	if (value("verbose") != NOSTR)
		(void) wait_child(pid);
	else
		free_child(pid);
	return 0;
}

/*
 * Mail a message on standard input to the people indicated
 * in the passed header.  (Internal interface).
 */
void
mail1(hp, printheaders, quote, quotefile)
	struct header *hp;
	int printheaders;
	struct message *quote;
	char *quotefile;
{
	char *cp;
	char **namelist;
	struct name *to;
	FILE *mtf, *nmtf;
	int convert;

	convert = gettextconversion();
	/*
	 * Collect user's mail from standard input.
	 * Get the result as mtf.
	 */
	if ((mtf = collect(hp, printheaders, quote, quotefile)) == (FILE*)NULL)
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
	if ((cp = value("record")) != NOSTR)
		(void) savemail(expand(cp), mtf);
	start_mta(to, hp->h_smopts, mtf);
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
	static unsigned long msgc;
	time_t t;
	char *domainpart;
	int rd;
	long randbuf;
	char datestr[sizeof(time_t) * 2 + 1];
	char pidstr[sizeof(pid_t) * 2+ 1];
	char countstr[sizeof(msgc) * 2 + 1];
	char randstr[sizeof(randbuf) * 2+ 1];


	msgc++;
	time(&t);
	rd = open(randfile, O_RDONLY);
	if (rd != -1) {
		if (read(rd, &randbuf, sizeof(randbuf)) != sizeof(randbuf)) {
			srand((unsigned)(msgc * t));
			randbuf = (long)rand();
		}
		close(rd);
	} else {
		srand((unsigned)(msgc * t));
		randbuf = (long)rand();
	}
	itostr(16, (unsigned)t, datestr);
	itostr(36, (unsigned)getpid(), pidstr);
	itostr(36, (unsigned)msgc, countstr);
	itostr(36, (unsigned)randbuf, randstr);
	if ((domainpart = skin(value("from")))
			&& (domainpart = strchr(domainpart, '@'))) {
		domainpart++;
	} else {
		domainpart = nodename();
	}
	fprintf(fo, "Message-ID: <%s.%s%.5s%s%.5s@%s>\n",
			datestr, progname, pidstr, countstr, randstr,
			domainpart);
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
	char *addr, *cp;
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
		addr = myaddr();
		if (addr != NULL) {
			if (mime_name_invalid(addr))
				return 1;
			if ((cp = value("smtp")) == NULL
					|| strcmp(cp, "localhost") == 0)
				fprintf(fo, "Sender: %s\n", username());
			fwrite("From: ", sizeof(char), 6, fo);
			if (mime_write(addr, sizeof(char), strlen(addr), fo,
					convert == CONV_TODISP ?
					CONV_NONE:CONV_TOHDR_A,
					convert == CONV_TODISP ?
					TD_ISPR|TD_ICONV:TD_ICONV) == 0)
				return 1;
			gotcha++;
			fputc('\n', fo);
		}
		addr = value("ORGANIZATION");
		if (addr != NULL) {
			fwrite("Organization: ", sizeof(char), 14, fo);
			if (mime_write(addr, sizeof(char), strlen(addr), fo,
					convert == CONV_TODISP ?
					CONV_NONE:CONV_TOHDR,
					convert == CONV_TODISP ?
					TD_ISPR|TD_ICONV:TD_ICONV) == 0)
				return 1;
			gotcha++;
			fputc('\n', fo);
		}
		addr = value("replyto");
		if (addr != NULL) {
			if (mime_name_invalid(addr))
				return 1;
			fwrite("Reply-To: ", sizeof(char), 10, fo);
			if (mime_write(addr, sizeof(char), strlen(addr), fo,
					convert == CONV_TODISP ?
					CONV_NONE:CONV_TOHDR_A,
					convert == CONV_TODISP ?
					TD_ISPR|TD_ICONV:TD_ICONV) == 0)
				return 1;
			gotcha++;
			fputc('\n', fo);
		}
	}
	if (hp->h_to != NIL && w & GTO) {
		if (fmt("To:", hp->h_to, fo, w&GCOMMA))
			return 1;
		gotcha++;
	}
	if (hp->h_subject != NOSTR && w & GSUBJECT) {
		fwrite("Subject: ", sizeof(char), 9, fo);
		if (mime_write(hp->h_subject, sizeof(char),
				strlen(hp->h_subject),
				fo, convert == CONV_TODISP ?
				CONV_NONE:CONV_TOHDR,
				convert == CONV_TODISP ?
				TD_ISPR|TD_ICONV:TD_ICONV) == 0)
			return 1;
		gotcha++;
		fwrite("\n", sizeof(char), 1, fo);
	}
	if (hp->h_cc != NIL && w & GCC) {
		if (fmt("Cc:", hp->h_cc, fo, w&GCOMMA))
			return 1;
		gotcha++;
	}
	if (hp->h_bcc != NIL && w & GBCC) {
		if (fmt("Bcc:", hp->h_bcc, fo, w&GCOMMA))
			return 1;
		gotcha++;
	}
	if (w & GMSGID && stealthmua == 0) {
		message_id(fo), gotcha++;
	}
	if (hp->h_ref != NIL && w & GREF) {
		if (fmt("References:", hp->h_ref, fo, 0))
			return 1;
		gotcha++;
	}
	if (w & GUA && stealthmua == 0) {
		fprintf(fo, "User-Agent: %s\n", version), gotcha++;
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
				getcharset(convert),
				getencoding(convert));
		}
	}
	if (gotcha && w & GNL)
		(void) putc('\n', fo);
	return(0);
}

/*
 * Format the given header line to not exceed 72 characters.
 */
int
fmt(str, np, fo, comma)
	char *str;
	struct name *np;
	FILE *fo;
	int comma;
{
	int col, len, count = 0;
	int is_to = 0;

	comma = comma ? 1 : 0;
	col = strlen(str);
	if (col) {
		fwrite(str, sizeof(char), strlen(str), fo);
		if ((col == 3 && strcasecmp(str, "to:") == 0) ||
			(col == 10 && strcasecmp(str, "Resent-To:") == 0))
			is_to = 1;
	}
	for (; np != NIL; np = np->n_flink) {
		if (is_to && isfileaddr(np->n_name))
			continue;
		if (np->n_flink == NIL)
			comma = 0;
		if (mime_name_invalid(np->n_name))
			return 1;
		len = strlen(np->n_name);
		col++;		/* for the space */
		if (count && col + len + comma > 72 && col > 1) {
			fputs("\n ", fo);
			col = 1;
		} else
			putc(' ', fo);
		len = mime_write(np->n_name, sizeof(char), len, fo,
				CONV_TOHDR_A, TD_ICONV);
		if (comma && !(is_to && isfileaddr(np->n_flink->n_name)))
			putc(',', fo);
		col += len + comma;
		count++;
	}
	putc('\n', fo);
	return 0;
}

/*
 * Rewrite a message for forwarding, adding the Resent-Headers.
 */
static int
infix_fw(fi, fo, mp, to, add_resent)
FILE *fi, *fo;
struct message *mp;
struct name *to;
{
	long count;
	char buf[LINESIZE], *cp, *cp2;
	size_t c;

	count = mp->m_size;
	/*
	 * Write the original headers first.
	 */
	while (count > 0) {
		if (foldergets(buf, LINESIZE, fi) == NULL)
			break;
		count -= c = strlen(buf);
		if (count > 0 && *buf == '\n')
			break;
		if (strncasecmp("status: ", buf, 8) != 0
				&& strncmp("From ", buf, 5) != 0) {
			if (strncasecmp("resent-", buf, 7) == 0)
				fputs("X-Old-", fo);
			fwrite(buf, sizeof *buf, c, fo);
		}
	}
	/*
	 * Write the Resent-Headers, but only if the message
	 * has headers at all.
	 */
	if (count > 0) {
		if (add_resent) {
				fputs("Resent-", fo);
			date_field(fo);
			cp = myaddr();
			if (cp != NULL) {
				if (mime_name_invalid(cp))
					return 1;
				if ((cp2 = value("smtp")) == NULL
					|| strcmp(cp2, "localhost") == 0)
					fprintf(fo, "Resent-Sender: %s\n",
							username());
				fwrite("Resent-From: ", sizeof(char), 13, fo);
				mime_write(cp, sizeof *cp, strlen(cp), fo,
						CONV_TOHDR_A, TD_ICONV);
				fputc('\n', fo);
			}
			cp = value("replyto");
			if (cp != NULL) {
				if (mime_name_invalid(cp))
					return 1;
				fwrite("Resent-Reply-To: ", sizeof(char),
						10, fo);
				mime_write(cp, sizeof(char), strlen(cp), fo,
						CONV_TOHDR_A, TD_ICONV);
				fputc('\n', fo);
			}
			if (fmt("Resent-To:", to, fo, 1))
				return 1;
			if (value("stealthmua") == NULL) {
				fputs("Resent-", fo);
				message_id(fo);
			}
		}
		fputc('\n', fo);
		/*
		 * Write the message body.
		 */
		while (count > 0) {
			if (foldergets(buf, LINESIZE, fi) == NULL)
				break;
			count -= c = strlen(buf);
			fwrite(buf, sizeof *buf, c, fo);
		}
	}
	if (ferror(fo)) {
		perror(tempMail);
		return 1;
	}
	return 0;
}

int
forward_msg(mp, to, add_resent)
struct message *mp;
struct name *to;
{
	FILE *ibuf, *nfo, *nfi;
	struct header head;

	if ((nfo = Fopen(tempMail, "w")) == (FILE*)NULL) {
		perror(tempMail);
		return 1;
	}
	if ((nfi = Fopen(tempMail, "r")) == (FILE*)NULL) {
		perror(tempMail);
		return 1;
	}
	rm(tempMail);
	ibuf = setinput(mp);
	head.h_to = to;
	head.h_cc = head.h_bcc = head.h_ref = head.h_attach = head.h_smopts
		= NULL;
	fixhead(&head, to);
	if (infix_fw(ibuf, nfo, mp, head.h_to, add_resent) != 0) {
		rewind(nfo);
		savedeadletter(nfi);
		fputs(". . . message not sent.\n", stderr);
		Fclose(nfo);
		Fclose(nfi);
		return 1;
	}
	fflush(nfo);
	rewind(nfo);
	Fclose(nfo);
	to = outof(to, nfi, &head);
	if (senderr)
		savedeadletter(nfi);
	to = elide(to);
	if (count(to) != 0)
		start_mta(to, head.h_smopts, nfi);
	Fclose(nfi);
	return 0;
}
