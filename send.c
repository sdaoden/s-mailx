/*	$Id: send.c,v 1.12 2000/08/08 20:10:39 gunnar Exp $	*/
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
static char rcsid[]  = "@(#)$Id: send.c,v 1.12 2000/08/08 20:10:39 gunnar Exp $";
#endif
#endif /* not lint */

#include "rcv.h"
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * Mail to mail folders and displays.
 */

struct boundary {
	struct boundary *b_flink;	/* Link to previous boundary */
	struct boundary *b_nlink;	/* Link to next boundary */
	char *b_str;			/* Boundary string */
	size_t b_len;			/* Length of boundary string */
	unsigned b_count;		/* The number of the boundary */
};

/*
 * Print the part number indicated by b0.
 */
static void
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

/*
 * Get a filename based on f.
 */
static char *
newfilename(f, b, b0)
char *f;
struct boundary *b, *b0;
{
	char *p;
	struct str in, out;

	if (f != NULL && f != (char*)-1) {
		in.s = f;
		in.l = strlen(f);
		mime_fromhdr(&in, &out, TD_ISPR);
		memcpy(f, out.s, out.l);
		*(f + out.l) = '\0';
		free(out.s);
	}
	p = (char*)smalloc(PATHSIZE + 1);
	if (value("interactive") != NOSTR) {
		fputs("Enter filename for part ", stdout);
		print_partnumber(stdout, b, b0);
		f = readtty(": ", f != (char*)-1 ? f : NULL);
	}
	if (f == NULL || f == (char*)-1) {
		f =(char*)smalloc(10);
		strcpy(f, "/dev/null");
	}
	return f;
}

/*
 * This is fgets for mbox lines.
 */
char *
foldergets(s, size, stream)
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

/*
 * Output a reasonable looking status field.
 */
static void
statusput(mp, obuf, prefix)
	struct message *mp;
	FILE *obuf;
	char *prefix;
{
	char statout[3];
	char *cp = statout;

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
 * Get the innermost multipart boundary.
 */
static struct boundary *
get_top_boundary(b)
struct boundary *b;
{
	while (b->b_nlink != NULL)
		b = b->b_nlink;
	return b;
}

/*
 * Allocate a multipart boundary.
 */
static struct boundary *
bound_alloc(bprev)
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

/*
 * Free a multipart boundary.
 */
static void
bound_free(b)
struct boundary *b;
{
	struct boundary *bn;

	bn = b->b_nlink;
	b->b_nlink = NULL;
	while (bn != NULL) {
		b = bn->b_nlink;
		if (bn->b_str != NULL)
			free(bn->b_str);
		free(bn);
		bn = b;
	}
}

/*
 * Send the body of a MIME multipart message.
 */
static int
send_multipart(mp, ibuf, obuf, doign, prefix, prefixlen, count, 
		convert, action, b0)
struct message *mp;
FILE *ibuf, *obuf;
struct ignoretab *doign;
char *prefix;
long count;
struct boundary *b0;
{
	int mime_enc, mime_content = MIME_TEXT, new_content = MIME_TEXT;
	FILE *oldobuf = (FILE*)-1, *origobuf = (FILE*)-1;
	char line[LINESIZE];
	char *filename = (char*)-1;
	int c = 0, part = 0, error_return = 0, is_cont = 0;
	char *boundend, *cs = us_ascii, *tcs;
	struct boundary *b = b0;
	char *(*f_gets) __P((char *s, int size, FILE *stream));
	long now;
	int linelen = -1, lineno = -1;

	if (action == CONV_NONE)
		f_gets = fgets;
	else
		f_gets = foldergets;
	tcs = gettcharset();
	if (b0->b_str == NULL) {
		error_return = 1;
		goto send_multi_end;
	}
	b0->b_count = 0;
	b0->b_len = strlen(b0->b_str);
	mime_content = MIME_DISCARD;
	origobuf = obuf;
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
					if (action == CONV_QUOTE) {
						if (b->b_count > 1)
							goto send_multi_end;
					} else if (action != CONV_TOFILE) {
						fputs("\nPart ", obuf);
						print_partnumber(obuf, b, b0);
						fputs(":\n", obuf);
					}
					new_content = MIME_TEXT;
					cs = us_ascii;
#ifdef	HAVE_ICONV
					if (iconvd != (iconv_t) -1) {
						iconv_close(iconvd);
						iconvd = (iconv_t) -1;
					}
#endif
				} else if (*boundend == '-') {
					if (b == b0)
						break;
					if (b != NULL) {
						b = b->b_flink;
						bound_free(b);
					}
					mime_content = MIME_DISCARD;
				} else {
					goto send_multi_nobound;
				}
				part++;
				if (oldobuf != (FILE*)-1 &&
						obuf != origobuf) {
					Fclose(obuf);
					obuf = oldobuf;
					oldobuf = (FILE*)-1;
				}
				filename = (char*)-1;
				continue;
			}
		}
send_multi_nobound:
		linelen = strlen(line) - 1;
		switch (mime_content) {
		case MIME_SUBHDR:
			if (*line == '\n') {
				if (new_content == MIME_822
					&& (action == CONV_TODISP
						|| action == CONV_QUOTE)) {
					fputc('\n', obuf);
					new_content = MIME_TEXT;
					continue;
				}
				/*
				 * End of subheader.
				 */
				lineno = -1;
				mime_content = new_content;
				if (mime_content == MIME_MULTI) {
					if (b->b_str == NULL) {
						error_return = 1;
						goto send_multi_end;
					}
					b->b_len = strlen(b->b_str);
				} else if (mime_content == MIME_TEXT) {
					if (cs == NULL)
						cs = us_ascii;
#ifdef	HAVE_ICONV
					if (iconvd != (iconv_t) -1)
						iconv_close(iconvd);
					if (action == CONV_TODISP
						|| action == CONV_QUOTE)
					if (iconvd != (iconv_t) -1)
						iconv_close(iconvd);
					if (strcasecmp(tcs, cs)
						&& strcasecmp(us_ascii, cs))
						iconvd = iconv_open(tcs, cs);
					else
						iconvd = (iconv_t) -1;
#endif
					if (convert == CONV_FROMB64) {
						convert = CONV_FROMB64_T;
					}
				}
				if (action == CONV_TOFILE && part != 1
						&& mime_content!=MIME_MULTI) {
					filename =
					 newfilename(
						filename, b, b0);
					if (filename != NULL){
						oldobuf = obuf;
						obuf = Fopen(
						     filename,
							"a");
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
				if (mime_content == MIME_822) {
					if (action == CONV_TOFILE) {
						time (&now);
						fprintf(obuf, "From %s %s",
								myname,
								ctime(&now));
					}
					mime_content = MIME_MESSAGE;
				}
			} else if (*line == 'C' || *line == 'c') {
				if (new_content != MIME_UNKNOWN
					&& strncasecmp(line,
					"content-type:", 13)
						== 0) {
					new_content =
					mime_getcontent(
						line);
					cs = mime_getparam("charset=", line);
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
			 		b->b_str = mime_getboundary(line);
				} else if (new_content == MIME_TEXT
							&& cs == NULL) {
						cs = mime_getparam("charset=",
								line);
						if (cs == NULL)
							cs = us_ascii;
				}
			}
			if (doign && (isign(line, doign)
				|| doign == allignore))
				break;
			if (prefix != NOSTR) {
				(void)fwrite(prefix, sizeof *prefix,
					  prefixlen, obuf);
			}
			(void) mime_write(line, sizeof *line,
				strlen(line), obuf, action == CONV_TODISP 
				|| action == CONV_QUOTE ?
				CONV_FROMHDR:CONV_NONE,
				action == CONV_TODISP ?
				TD_ISPR|TD_ICONV:TD_NONE);
			break;
		case MIME_TEXT:
		case MIME_MESSAGE:
			if (lineno > 0 && convert != CONV_FROMB64
					&& is_cont == 0)
				fputc('\n', obuf);
			if (prefix != NOSTR && is_cont == 0) {
				if (c > 1)
					fputs(prefix, obuf);
				else
					(void) fwrite(prefix,
					      sizeof *prefix,
					prefixlen, obuf);
			}
			if (convert == CONV_FROMQP && *(line + c - 2) == '=')
				is_cont = 1;
			else
				is_cont = 0;
			(void)mime_write(line, sizeof *line,
					 linelen, obuf,
					 convert,
					 action == CONV_TODISP
					 || action == CONV_QUOTE ?
					 TD_ISPR|TD_ICONV:TD_NONE);
			break;
		case MIME_DISCARD:
			/* unspecified part of a mp. msg. */
			break;
		default: /* We do not display this */
			if (action == CONV_TOFILE) {
				if (lineno > 0 && convert != CONV_FROMB64)
					fputc('\n', obuf);
				(void)mime_write(line,
					 sizeof *line,
					 linelen, obuf,
					 convert, TD_NONE);
			}
		}
		if (ferror(obuf)) {
			error_return = -1;
			break;
		}
		lineno++;
	}
send_multi_end:
#ifdef	HAVE_ICONV
	if (iconvd != (iconv_t) -1) {
		iconv_close(iconvd);
		iconvd = (iconv_t) -1;
	}
#endif
	if (oldobuf != (FILE*)-1 &&
			obuf != origobuf) {
		Fclose(obuf);
		obuf = oldobuf;
		oldobuf = (FILE*)-1;
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
send_message(mp, obuf, doign, prefix, convert)
	struct message *mp;
	FILE *obuf;
	struct ignoretab *doign;
	char *prefix;
{
	long count, now;
	FILE *ibuf;
	char line[LINESIZE];
	int ishead, infld, ignoring = 0, dostat, firstline;
	char *cp, *cp2;
	int c = 0, length, is_cont = 0, prefixlen = 0;
	int mime_enc, mime_content = MIME_TEXT, action;
	char *mime_version = NULL;
	int error_return = 0;
	struct boundary b0;
	char *(*f_gets) __P((char *s, int size, FILE *stream));
	char *cs = us_ascii, *tcs;

	b0.b_str = NULL;
	b0.b_nlink = b0.b_flink = NULL;
	action = convert;
	if (action == CONV_NONE)
		f_gets = fgets;
	else
		f_gets = foldergets;
	tcs = gettcharset();
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
			ignoring = doign == allignore;
		} else if (line[0] == '\n') {
			/*
			 * If line is blank, we've reached end of
			 * headers, so force out status: field
			 * and note that we are no longer in header
			 * fields
			 */
			if (mime_content == MIME_822) {
				switch (action) {
				case CONV_TODISP:
				case CONV_QUOTE:
					if (prefix != NOSTR)
						(void) fwrite(prefix,
							  sizeof *prefix,
							prefixlen, obuf);
					fputc('\n', obuf);
					continue;
				case CONV_TOFILE:
					if (action == CONV_TOFILE) {
						time (&now);
						fprintf(obuf, "From %s %s",
								myname,
								ctime(&now));
					}
					/*FALLTHROUGH*/
				default:
					mime_content = MIME_MESSAGE;
				}
			}
			if (dostat) {
				statusput(mp, obuf, prefix);
				dostat = 0;
			}
			ishead = 0;
			ignoring = doign == allignore;
		} else if (infld && (line[0] == ' ' || line[0] == '\t')) {
			/*
			 * If this line is a continuation (via space or tab)
			 * of a previous header field, just echo it
			 * (unless the field should be ignored).
			 */
			 if (mime_content == MIME_MULTI && b0.b_str == NULL) {
				 b0.b_str = mime_getboundary(line);
			 } else if (mime_content == MIME_TEXT && cs == NULL) {
				cs = mime_getparam("charset=", line);
				if (cs == NULL)
					cs = us_ascii;
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
				if (doign != allignore)
					/* add blank line */
					(void) putc('\n', obuf);
				ishead = 0;
				ignoring = 0;
			} else {
				if (strncasecmp(line, "mime-version:", 13)
						== 0) {
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
					cs = mime_getparam("charset=", line);
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
						action == CONV_TODISP ?
						TD_ISPR|TD_ICONV:TD_NONE);
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
		case MIME_TEXT:
			if (convert == CONV_FROMB64)
				convert = CONV_FROMB64_T;
			/*FALLTHROUGH*/
		case MIME_MESSAGE:
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
	if (cs == NULL)
		cs = us_ascii;
#ifdef	HAVE_ICONV
	if (action == CONV_TODISP || action == CONV_QUOTE) {
		if (iconvd != (iconv_t) -1)
				iconv_close(iconvd);
		if (strcasecmp(tcs, cs) && strcasecmp(us_ascii, cs))
			iconvd = iconv_open(tcs, cs);
		else
			iconvd = (iconv_t) -1;
	}
#endif
	if (doign == allignore)
		count--;		/* skip final blank line */
	while (count > 0) {
		if (f_gets(line, LINESIZE, ibuf) == NOSTR) {
			c = 0;
			break;
		}
		count -= c = strlen(line);
		if (prefix != NOSTR && is_cont == 0) {
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
		if (convert == CONV_FROMQP && *(line + c - 2) == '=')
			is_cont = 1;
		else
			is_cont = 0;
		(void)mime_write(line, sizeof *line, c,
				 obuf, convert, action == CONV_TODISP
				 	|| action == CONV_QUOTE ?
					TD_ISPR|TD_ICONV:TD_NONE);
		if (ferror(obuf)) {
			error_return = -1;
			goto send_end;
		}
	}
send_end:
#if 0
	if (doign == allignore && c > 0 && line[c - 1] != '\n')
		/* no final blank line */
		if ((c = getc(ibuf)) != EOF && putc(c, obuf) == EOF)
			return -1;
#endif
#ifdef	HAVE_ICONV
	if (iconvd != (iconv_t) -1) {
		iconv_close(iconvd);
		iconvd = (iconv_t) -1;
	}
#endif
	if (b0.b_str != NULL)
		free(b0.b_str);
	return error_return;
}
