/*	$Id: send.c,v 1.3 2000/03/24 23:01:39 gunnar Exp $	*/
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
#elif 0
static char rcsid[]  = "OpenBSD: send.c,v 1.6 1996/06/08 19:48:39 christos Exp";
#else
static char rcsid[]  = "@(#)$Id: send.c,v 1.3 2000/03/24 23:01:39 gunnar Exp $";
#endif
#endif /* not lint */

#include "rcv.h"
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * Mail to others.
 */

static char *send_boundary;

struct boundary {
	struct boundary *b_flink;	/* Link to previous boundary */
	struct boundary *b_nlink;	/* Link to next boundary */
	char *b_str;			/* Boundary string */
	size_t b_len;			/* Length of boundary string */
	unsigned b_count;		/* The number of the boundary */
};

static char *makeboundary()
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

static char *getcharset(convert)
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

static char *getencoding(convert)
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
}

int gettextconversion()
{
	char *p;
	int convert;

	p = value("encoding");
	if (p  == NULL)
		return CONV_NONE;
	if (equal(p, "quoted-printable"))
		convert = CONV_TOQP;
	else if (equal(p, "base64"))
		convert = CONV_TOB64;
	else
		convert = CONV_NONE;
	return convert;
}

void
print_partnumber(obuf, b, b0)
FILE *obuf;
struct boundary *b, *b0;
{
	struct boundary *bc;

	for (bc = b0; ; bc = bc->b_nlink) {
		if (bc != b0)
			fputc('.', obuf);
		fprintf(obuf, "%d", bc->b_count);
		if (bc == b)
			break;
	}
}

static char *newfilename(f, b, b0)
char *f;
struct boundary *b, *b0;
{
	char *p;
	struct str in, out;

	if (f != NULL && f != (char*)-1) {
		in.s = f;
		in.l = strlen(f);
		mime_fromhdr(&in, &out, 1);
		memcpy(f, out.s, out.l);
		*(f + out.l) = '\0';
		free(out.s);
	}
	p = (char*)smalloc(PATHSIZE + 1);
	if (value("interactive") != NOSTR) {
		fputs("Enter filename for part ", stdout);
		print_partnumber(stdout, b, b0);
		printf(" [%s]: ", f && f != (char*)-1 ? f : "(none)");
		if (readline(stdin, p, PATHSIZE) != 0) {
			if (f != NULL && f != (char*)-1)
				free(f);
			f = p;
		}
	}
	if (f == NULL || f == (char*)-1) {
		f =(char*)smalloc(10);
		strcpy(f, "/dev/null");
	}
	return f;
}

char *foldergets(s, size, stream)
/* This is fgets for mbox lines */
char *s;
FILE *stream;
{
	char *p;

	p = fgets(s, size, stream);
	if (p == NULL)
		return NULL;
	if (*p == '>') {
		p++;
		while (*p == '>') p++;
		if (strncmp(p, "From ", 5) == 0) {
			/* we got a masked From line */
			p = s;
			do {
				*p = *(p + 1);
			} while (*p++ != '\0');
		}
	}
	return s;
}

struct boundary *get_top_boundary(b)
struct boundary *b;
{
	while (b->b_nlink != NULL)
		b = b->b_nlink;
	return b;
}

struct boundary *bound_alloc(bprev)
struct boundary *bprev;
{
	struct boundary *b;

	b = (struct boundary*)smalloc(sizeof(struct boundary));
	b->b_str = NULL;
	b->b_count = 0;
	b->b_nlink = (struct boundary*)NULL;
	b->b_flink = bprev;
	bprev->b_nlink = b;
	return b;
}

void
bound_free(b)
struct boundary *b;
{
	struct boundary *bn;

	bn = b->b_nlink;
	b->b_nlink = NULL;
	while(bn != NULL) {
		b = bn->b_nlink;
		if (bn->b_str != NULL)
			free(bn->b_str);
		free(bn);
		bn = b;
	}
}

int
send_multipart(mp, ibuf, obuf, doign, prefix, prefixlen, count, 
		convert, action, b0)
register struct message *mp;
FILE *ibuf, *obuf;
struct ignoretab *doign;
char *prefix;
long count;
struct boundary *b0;
/* Send a MIME multipart message. */
{
	int mime_enc, mime_content = MIME_TEXT, new_content, i;
	register FILE *oldobuf = (FILE*)-1;
	char line[LINESIZE];
	register int c = 0;
	int length;
	char *filename = (char*)-1;
	int error_return = 0;
	char *boundend;
	struct boundary *b;
	char *(*f_gets) __P((char *s, int size, FILE *stream));

	if (action == CONV_NONE)
		f_gets = fgets;
	else
		f_gets = foldergets;
	if (b0->b_str == NULL) {
		error_return = 1;
		goto send_multi_end;
	}
	b0->b_count = 0;
	b0->b_len = strlen(b0->b_str);
	mime_content = MIME_DISCARD;
	while (count > 0 && 
			f_gets(line, LINESIZE, ibuf)
			!= NULL) {
		count -= c = strlen(line);
		if (line[0] == '-' && line[1] == '-') {
			boundend = NULL;
			for (b = b0; b != NULL; b = b->b_nlink) {
				if (strncmp(line, b->b_str, b->b_len)
					== 0) {
					boundend = line + b->b_len;
					bound_free(b);
					break;
				}
			}
			if (boundend != NULL) {
				if (*boundend == '\n') {
					mime_content = MIME_SUBHDR;
					b->b_count++;
					if (action == CONV_QUOTE
						&& b->b_count > 1) {
						goto send_multi_end;
					}
					if (action != CONV_TOFILE) {
						fputs("\nPart ", obuf);
						print_partnumber(obuf, b, b0);
						fputs(":\n", obuf);
					}
					new_content = MIME_TEXT;
				} else if (*boundend == '-') {
					if (b == b0)
						break;
					if (b != NULL) {
						b = b->b_flink;
						bound_free(b);
					}
					mime_content = MIME_DISCARD;
				} else { /* ignore */
					mime_content = MIME_DISCARD;
				}
				if (oldobuf != (FILE*)-1) {
					fclose(obuf);
					obuf = oldobuf;
					oldobuf = (FILE*)-1;
				}
				if (filename != (char*)-1
					&& filename != NULL) {
					free(filename);
				}
				filename = (char*)-1;
				continue;
			}
		}
		switch (mime_content) {
		case MIME_SUBHDR:
			if (*line == '\n') {
				/* end of subheader */
				mime_content = new_content;
				if (mime_content == MIME_MULTI) {
					if (b->b_str == NULL) {
						error_return = 1;
						goto send_multi_end;
					}
					b->b_len = strlen(b->b_str);
				}
				if (action == CONV_TOFILE) {
					filename =
					 newfilename(
						filename, b, b0);
					if (filename != NULL){
						oldobuf = obuf;
						obuf = fopen(
						     filename,
							"wb");
						if (obuf
						    == NULL) {
						fprintf(
			stderr, "Cannot open %s\n", filename);
						obuf = oldobuf;
						oldobuf =
						  (FILE*)-1;
						error_return=-1;
						goto
						   send_multi_end;

						}
					}
				}
			} else if (*line == 'C' || *line == 'c') {
				if (new_content != MIME_UNKNOWN
					&& strncasecmp(line,
					"content-type:", 13)
						== 0) {
					new_content =
					mime_getcontent(
						line);
					if (new_content == MIME_MULTI) {
						b = bound_alloc(
							get_top_boundary(b0));
				 		b->b_str =
						mime_getboundary(line);
					}
				}
				if ((action == CONV_TODISP
					|| action == CONV_QUOTE
					|| action == CONV_TOFILE)
					&& strncasecmp(line,
					"content-transfer-encoding:",
					26) == 0) {
					mime_enc = mime_getenc(
						line);
					switch (mime_enc) {
					case MIME_7B:
					case MIME_8B:
					case MIME_BIN:
					convert = CONV_NONE;
						break;
					case MIME_QP:
					convert = CONV_FROMQP;
						break;
					case MIME_B64:
					convert = CONV_FROMB64;
						break;
					default:
					convert = CONV_NONE;
					new_content
						= MIME_UNKNOWN;
					}
				}
				if (strncasecmp(line,
					"content-disposition:",
					20) == 0) {
					filename =
					mime_getfilename(line);
				}
			} else if (line[0] == ' ' || line[0] == '\t') {
				if (filename == NULL)
					filename =
					mime_getfilename(line);
			 		if (new_content == MIME_MULTI
							&& b->b_str == NULL) {
				 		b->b_str =
							mime_getboundary(line);
			 		}
			}
			if (doign && (isign(line, doign)
				|| doign == ignoreall))
				break;
			if (prefix != NOSTR) {
				if (length > 1)
					fputs(prefix, obuf);
				else
					(void)fwrite(prefix,
					  sizeof *prefix,
					  prefixlen, obuf);
			}
			(void) mime_write(line, sizeof *line,
				strlen(line), obuf, action == CONV_TODISP 
				|| action == CONV_QUOTE ?
				CONV_FROMHDR:CONV_NONE,
				action == CONV_TODISP);
			break;
		case MIME_MESSAGE:
		case MIME_TEXT:
			if (prefix != NOSTR) {
				if (c > 1)
					fputs(prefix, obuf);
				else
					(void) fwrite(prefix,
					      sizeof *prefix,
					prefixlen, obuf);
			}
			(void)mime_write(line, sizeof *line,
					 strlen(line), obuf,
					 convert,
					 action==CONV_TODISP);
			break;
		case MIME_DISCARD:
			/* unspecified part of a mp. msg. */
			break;
		default: /* We do not display this */
			if (action == CONV_TOFILE) {
				(void)mime_write(line,
					 sizeof *line,
					 strlen(line), obuf,
					 convert,
					 action==CONV_TODISP);
			}
		}
		if (ferror(obuf)) {
			error_return = -1;
			break;
		}
	}
send_multi_end:
	if (oldobuf != (FILE*)-1) {
		fclose(obuf);
		obuf = oldobuf;
		oldobuf = (FILE*)-1;
	}
	if (filename != (char*)-1
		&& filename != NULL) {
		free(filename);
	}
	bound_free(b0);
	return error_return;
}

/*
 * Send message described by the passed pointer to the
 * passed output buffer.  Return -1 on error.
 * Adjust the status: field if need be.
 * If doign is given, suppress ignored header fields.
 * prefix is a string to prepend to each output line.
 */
int
send(mp, obuf, doign, prefix, convert)
	register struct message *mp;
	FILE *obuf;
	struct ignoretab *doign;
	char *prefix;
{
	long count;
	register FILE *ibuf;
	char line[LINESIZE];
	int ishead, infld, ignoring = 0, dostat, firstline;
	register char *cp, *cp2;
	register int c = 0;
	int length;
	int prefixlen = 0;
	int mime_enc, mime_content = MIME_TEXT, action;
	char *mime_version = NULL;
	int error_return = 0;
	struct boundary b0;
	char *(*f_gets) __P((char *s, int size, FILE *stream));

	b0.b_str = NULL;
	b0.b_nlink = b0.b_flink = NULL;
	action = convert;
	if (action == CONV_NONE)
		f_gets = fgets;
	else
		f_gets = foldergets;
	/*
	 * Compute the prefix string, without trailing whitespace
	 */
	if (prefix != NOSTR) {
		cp2 = 0;
		for (cp = prefix; *cp; cp++)
			if (*cp != ' ' && *cp != '\t')
				cp2 = cp;
		prefixlen = cp2 == 0 ? 0 : cp2 - prefix + 1;
	}
	ibuf = setinput(mp);
	count = mp->m_size;
	ishead = 1;
	dostat = doign == 0 || !isign("status", doign);
	infld = 0;
	firstline = 1;
	/*
	 * Process headers first
	 */
	while (count > 0 && ishead) {
		if (f_gets(line, LINESIZE, ibuf) == NOSTR)
			break;
		count -= length = strlen(line);
		if (firstline) {
			/* 
			 * First line is the From line, so no headers
			 * there to worry about
			 */
			firstline = 0;
			ignoring = doign == ignoreall;
		} else if (line[0] == '\n') {
			/*
			 * If line is blank, we've reached end of
			 * headers, so force out status: field
			 * and note that we are no longer in header
			 * fields
			 */
			if (dostat) {
				statusput(mp, obuf, prefix);
				dostat = 0;
			}
			ishead = 0;
			ignoring = doign == ignoreall;
		} else if (infld && (line[0] == ' ' || line[0] == '\t')) {
			/*
			 * If this line is a continuation (via space or tab)
			 * of a previous header field, just echo it
			 * (unless the field should be ignored).
			 */
			 if (mime_content == MIME_MULTI && b0.b_str == NULL) {
				 b0.b_str = mime_getboundary(line);
			 }
		} else {
			/*
			 * Pick up the header field if we have one.
			 */
			for (cp = line; (c = *cp++) && c != ':' && !isspace(c);)
				;
			cp2 = --cp;
			while (isspace(*cp++))
				;
			if (cp[-1] != ':') {
				/*
				 * Not a header line, force out status:
				 * This happens in uucp style mail where
				 * there are no headers at all.
				 */
				if (dostat) {
					statusput(mp, obuf, prefix);
					dostat = 0;
				}
				if (doign != ignoreall)
					/* add blank line */
					(void) putc('\n', obuf);
				ishead = 0;
				ignoring = 0;
			} else {
				if (strncasecmp(line, "mime-version:", 13)
						== 0) {
					if (mime_version != NULL)
						free(mime_version);
					mime_version = mime_getparam(
						"mime-version:", line);
				}
				if (action == CONV_TODISP
					|| action == CONV_QUOTE
					|| action == CONV_TOFILE) {
					if (mime_content != MIME_UNKNOWN
						&& strncasecmp(line,
							"content-type:", 13)
							== 0) {
					mime_content = mime_getcontent(line);
					if (mime_content == MIME_MULTI)
				 		b0.b_str
						      = mime_getboundary(line);
				}
				if (strncasecmp(line,
					"content-transfer-encoding:",
					26) == 0) {
					mime_enc = mime_getenc(line);
					switch (mime_enc) {
					case MIME_7B:
					case MIME_8B:
					case MIME_BIN:
						convert = CONV_NONE;
						break;
					case MIME_QP:
						convert = CONV_FROMQP;
						break;
					case MIME_B64:
						convert = CONV_FROMB64;
						break;
					default:
						convert = CONV_NONE;
						mime_content = MIME_UNKNOWN;
					}
				}
				}
				/*
				 * If it is an ignored field and
				 * we care about such things, skip it.
				 */
				*cp2 = 0;	/* temporarily null terminate */
				if (doign && isign(line, doign))
					ignoring = 1;
				else if ((line[0] == 's' || line[0] == 'S') &&
					 strcasecmp(line, "status") == 0) {
					/*
					 * If the field is "status," go compute
					 * and print the real Status: field
					 */
					if (dostat) {
						statusput(mp, obuf, prefix);
						dostat = 0;
					}
					ignoring = 1;
				} else {
					ignoring = 0;
					*cp2 = c;	/* restore */
				}
				infld = 1;
			}
		}
		if (!ignoring) {
			/*
			 * Strip trailing whitespace from prefix
			 * if line is blank.
			 */
			if (prefix != NOSTR) {
				if (length > 1)
					fputs(prefix, obuf);
				else
					(void) fwrite(prefix,
							  sizeof *prefix,
							prefixlen, obuf);
			}
			(void) mime_write(line, sizeof *line,
					length, obuf,
						action == CONV_TODISP 
						|| action == CONV_QUOTE ?
						CONV_FROMHDR:CONV_NONE,
						action == CONV_TODISP);
			if (ferror(obuf)) {
				error_return = -1;
				goto send_end;
			}
		}
	}
	/*
	 * Copy out message body
	 */
#if 0	/* do not check the MIME version, discussion in comp.mail.mime */
	if (mime_version == NULL || strcmp(mime_version, "1.0")) {
		convert = CONV_NONE;
		mime_content = MIME_TEXT;
	}
#endif
	if (action == CONV_TODISP
			|| action == CONV_QUOTE
			|| action == CONV_TOFILE) {
		switch (mime_content) {
		case MIME_MESSAGE:
		case MIME_TEXT:
			break;
		case MIME_MULTI:
			error_return = send_multipart(mp, ibuf, obuf, doign,
					prefix, prefixlen, count,
					convert, action, &b0);
			goto send_end;
		default:
			if (action != CONV_TOFILE) {
				/* we do not display this */
				goto send_end;
			}
		}
	}
	if (doign == ignoreall)
		count--;		/* skip final blank line */
	while (count > 0) {
		if (f_gets(line, LINESIZE, ibuf) == NOSTR) {
			c = 0;
			break;
		}
		count -= c = strlen(line);
		if (prefix != NOSTR) {
			/*
			 * Strip trailing whitespace from prefix
			 * if line is blank.
			 */
			if (c > 1)
				fputs(prefix, obuf);
			else
				(void) fwrite(prefix, sizeof *prefix,
						prefixlen, obuf);
		}
		(void)mime_write(line, sizeof *line, c,
				 obuf, convert, action == CONV_TODISP);
		if (ferror(obuf)) {
			error_return = -1;
			goto send_end;
		}
	}
send_end:
#if 0
	if (doign == ignoreall && c > 0 && line[c - 1] != '\n')
		/* no final blank line */
		if ((c = getc(ibuf)) != EOF && putc(c, obuf) == EOF)
			return -1;
#endif
	if (b0.b_str != NULL)
		free(b0.b_str);
	if (mime_version != NULL)
		free(mime_version);
	return error_return;
}

/*
 * Output a reasonable looking status field.
 */
void
statusput(mp, obuf, prefix)
	register struct message *mp;
	FILE *obuf;
	char *prefix;
{
	char statout[3];
	register char *cp = statout;

	if (mp->m_flag & MREAD)
		*cp++ = 'R';
	if ((mp->m_flag & MNEW) == 0)
		*cp++ = 'O';
	*cp = 0;
	if (statout[0])
		fprintf(obuf, "%sStatus: %s\n",
			prefix == NOSTR ? "" : prefix, statout);
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
	FILE *mtf;
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
			if (value("askattach") != NOSTR)
				grabh(hp, GATTACH);
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
	if ((mtf = infix(hp, mtf, convert)) == (FILE*)NULL) {
		fprintf(stderr, ". . . message lost, sorry.\n");
		return;
	}
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
 * Fix the header by glopping all of the expanded names from
 * the distribution list into the appropriate fields.
 */
void
fixhead(hp, tolist)
	struct header *hp;
	struct name *tolist;
{
	register struct name *np;

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

static void attach_file(path, fo)
char *path;
FILE *fo;
{
	FILE *fi;
	char *charset, *contenttype, *basename, c;
	int convert, typefound = 0;
	size_t sz;
	char buf[INFIX_BUF];

	fi = fopen(path, "rb");
	if (fi == NULL) {
		perror(path);
		return;
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
		if (convert == CONV_NONE)
			convert = CONV_TOQP;
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
	fprintf(fo,
		"--%s\n"
		"Content-Type: %s",
		send_boundary, contenttype);
	if (typefound) free(contenttype);
	if (charset == NULL)
		fputc('\n', fo);
	else
		fprintf(fo, ";\n charset=%s\n", charset);
#if 0
	/* do not use raw 7bit */
	if (convert == CONV_7BIT)
		convert = CONV_TOQP;
#endif
	fprintf(fo, "Content-Transfer-Encoding: %s\n"
		"Content-Disposition: attachment;\n"
		" filename=\"",
		getencoding(convert));
	mime_write(basename, sizeof(char), strlen(basename), fo,
			CONV_TOHDR, 0);
	fwrite("\"\n\n", sizeof(char), 3, fo);
	while (sz = fread(buf, sizeof(char), INFIX_BUF, fi)) {
		c = buf[sz - 1];
		mime_write(buf, sizeof(char), sz, fo, convert, 0);
	}
	if (c != '\n')
		fputc('\n', fo);
	fclose(fi);
}

static void make_multipart(hp, convert, fi, fo)
struct header *hp;
FILE *fi, *fo;
/* Generate the body of a MIME multipart message */
{
	char buf[INFIX_BUF], c;
	size_t sz;
	struct name *att;

	fprintf(fo,
		"This is a multi-part message in MIME format.\n"
		"--%s\n"
		"Content-Type: text/plain; charset=%s\n",
		send_boundary, getcharset(convert));
#if 0
	/* do not use raw 7bit */
	if (convert == CONV_7BIT)
		convert = CONV_TOQP;
#endif
	fprintf(fo, "Content-Transfer-Encoding: %s\n\n", getencoding(convert));
	while (sz = fread(buf, sizeof(char), INFIX_BUF, fi)) {
		c = buf[sz - 1];
		mime_write(buf, sizeof(char), sz, fo, convert, 0);
	}
	if (c != '\n')
		fputc('\n', fo);
	for (att = hp->h_attach; att != NIL; att = att->n_flink) {
		attach_file(att->n_name, fo);
	}
	/* the final boundary with two attached dashes */
	fprintf(fo, "--%s--\n", send_boundary);
}

/*
 * Prepend a header in front of the collected stuff
 * and return the new file.
 */
FILE *
infix(hp, fi, convert)
	struct header *hp;
	FILE *fi;
{
	extern char *tempMail;
	register FILE *nfo, *nfi;
	register int c;
	size_t sz;
	char buf[INFIX_BUF];

	if ((nfo = Fopen(tempMail, "w")) == (FILE*)NULL) {
		perror(tempMail);
		return(fi);
	}
	if ((nfi = Fopen(tempMail, "r")) == (FILE*)NULL) {
		perror(tempMail);
		(void) Fclose(nfo);
		return(fi);
	}
	if (mime_isclean(fi) == 0) {
		convert = CONV_7BIT;
	}
	(void) rm(tempMail);
	(void) puthead(hp, nfo,
		   GTO|GSUBJECT|GCC|GBCC|GNL|GCOMMA|GXMAIL|GMIME
		   |GMSGID|GATTACH|GFROM|GORG|GREPLY|GREF,
		   convert);
	if (hp->h_attach != NIL) {
		make_multipart(hp, convert, fi, nfo);
	} else {
		while (sz = fread(buf, sizeof(char), INFIX_BUF, fi)) {
			mime_write(buf, sizeof(char), sz, nfo, convert, 0);
		}
	}
	if (ferror(fi)) {
		perror("read");
		rewind(fi);
		return(fi);
	}
	(void) fflush(nfo);
	if (ferror(nfo)) {
		perror(tempMail);
		(void) Fclose(nfo);
		(void) Fclose(nfi);
		rewind(fi);
		return(fi);
	}
	(void) Fclose(nfo);
	(void) Fclose(fi);
	rewind(nfi);
	return(nfi);
}

void message_id(fo)
FILE *fo;
{
	static unsigned msgcount;
	char hostname[MAXHOSTNAMELEN];
	char domainname[MAXHOSTNAMELEN];
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
#ifdef	__linux__
		gethostname(hostname, MAXHOSTNAMELEN);
		getdomainname(domainname, MAXHOSTNAMELEN);
		fprintf(fo, "@%s.%s>\n", hostname, domainname);
#else
		gethostname(hostname, MAXHOSTNAMELEN);
		fprintf(fo, "@%s>\n", hostname, domainname);
#endif
	}
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
	register int gotcha;
	char *addr;

	gotcha = 0;
	if (w & GFROM) {
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
	}
	if (w & GORG) {
		addr = value("ORGANIZATION");
		if (addr != NULL) {
			fwrite("Organization: ", sizeof(char), 14, fo);
			mime_write(addr, sizeof(char), strlen(addr), fo,
					convert == CONV_TODISP ?
					CONV_NONE:CONV_TOHDR,
					convert == CONV_TODISP), gotcha++;
			fputc('\n', fo);
		}
	}
	if (w & GREPLY) {
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
	if (w & GMSGID) {
		message_id(fo), gotcha++;
	}
	if (hp->h_ref != NIL && w & GREF)
		fmt("References:", hp->h_ref, fo, 0), gotcha++;
	if (w & GXMAIL) {
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
	register struct name *np;
	FILE *fo;
	int comma;
{
	register int col, len;
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

/*
 * Save the outgoing mail on the passed file.
 */

/*ARGSUSED*/
int
savemail(name, fi)
	char name[];
	register FILE *fi;
{
	register FILE *fo;
	char buf[BUFSIZ];
	char *p;
	register int i;
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
