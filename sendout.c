/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Mail to others.
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

#include "nail.h"

#include <fcntl.h>

#define	INFIX_BUF	\
	((1024 / B64_ENCODE_INPUT_PER_LINE) * B64_ENCODE_INPUT_PER_LINE)

static char	*send_boundary;
static bool_t	_senderror;

static enum okay	_putname(char const *line, enum gfield w,
				enum sendaction action, int *gotcha,
				char const *prefix, FILE *fo, struct name **xp);

/* Get an encoding flag based on the given string */
static char const *	_get_encoding(const enum conversion convert);

/* Write an attachment to the file buffer, converting to MIME */
static int		_attach_file(struct attachment *ap, FILE *fo);
static int		__attach_file(struct attachment *ap, FILE *fo);

static char const **	_prepare_mta_args(struct name *to, struct header *hp);

static struct name *fixhead(struct header *hp, struct name *tolist);
static int put_signature(FILE *fo, int convert);
static int attach_message(struct attachment *ap, FILE *fo);
static int make_multipart(struct header *hp, int convert, FILE *fi, FILE *fo,
		const char *contenttype, const char *charset);
static FILE *infix(struct header *hp, FILE *fi);
static int savemail(char const *name, FILE *fi);
static int sendmail_internal(void *v, int recipient_record);
static enum okay transfer(struct name *to, FILE *input, struct header *hp);
static enum okay start_mta(struct name *to, FILE *input, struct header *hp);
static void message_id(FILE *fo, struct header *hp);
static int fmt(char const *str, struct name *np, FILE *fo, int comma,
		int dropinvalid, int domime);
static int infix_resend(FILE *fi, FILE *fo, struct message *mp,
		struct name *to, int add_resent);

static enum okay
_putname(char const *line, enum gfield w, enum sendaction action, int *gotcha,
	char const *prefix, FILE *fo, struct name **xp)
{
	enum okay ret = STOP;
	struct name *np;

	np = lextract(line, GEXTRA|GFULL);
	if (xp)
		*xp = np;
	if (np == NULL)
		;
	else if (fmt(prefix, np, fo, w & GCOMMA, 0, action != SEND_TODISP))
		ret = OKAY;
	else if (gotcha)
		++(*gotcha);
	return (ret);
}

static char const *
_get_encoding(enum conversion const convert)
{
	char const *ret;
	switch (convert) {
	case CONV_7BIT:		ret = "7bit";			break;
	case CONV_8BIT:		ret = "8bit";			break;
	case CONV_TOQP:		ret = "quoted-printable";	break;
	case CONV_TOB64:	ret = "base64";			break;
	default:		ret = NULL;			break;
	}
	return (ret);
}

static int
_attach_file(struct attachment *ap, FILE *fo)
{
	/* TODO of course, the MIME classification needs to performed once
	 * only, not for each and every charset anew ... ;-// */
	int err = 0;
	char *charset_iter_orig[2];
	long offs;

	/* Is this already in target charset? */
	if (ap->a_conv == AC_TMPFILE) {
		err = __attach_file(ap, fo);
		Fclose(ap->a_tmpf);
		goto jleave;
	}

	/* We "consume" *ap*, so directly adjust it as we need it */
	if (ap->a_conv == AC_FIX_INCS)
		ap->a_charset = ap->a_input_charset;

	if ((offs = ftell(fo)) < 0) {
		err = EIO;
		goto jleave;
	}

	charset_iter_recurse(charset_iter_orig);
	for (charset_iter_reset(NULL); charset_iter_next() != NULL;) {
		err = __attach_file(ap, fo);
		if (err == 0 || (err != EILSEQ && err != EINVAL))
			break;
		clearerr(fo);
		if (fseek(fo, offs, SEEK_SET) < 0) {
			err = EIO;
			break;
		}
		if (ap->a_conv != AC_DEFAULT) {
			err = EILSEQ;
			break;
		}
		ap->a_charset = NULL;
	}
	charset_iter_restore(charset_iter_orig);
jleave:
	return err;
}

static int
__attach_file(struct attachment *ap, FILE *fo) /* XXX linelength */
{
	int err = 0, do_iconv;
	FILE *fi;
	char const *charset;
	enum conversion convert;
	char *buf;
	size_t bufsize, lncnt, inlen;

	/* Either charset-converted temporary file, or plain path */
	if (ap->a_conv == AC_TMPFILE) {
		fi = ap->a_tmpf;
		assert(ftell(fi) == 0x0l);
	} else if ((fi = Fopen(ap->a_name, "r")) == NULL) {
		err = errno;
		perror(ap->a_name);
		goto jleave;
	}

	/* MIME part header for attachment */
	{	char const *bn = ap->a_name, *ct;

		if ((ct = strrchr(bn, '/')) != NULL)
			bn = ++ct;
		ct = ap->a_content_type;
		charset = ap->a_charset;
		convert = mime_classify_file(fi, (char const**)&ct, &charset,
				&do_iconv);
		if (charset == NULL || ap->a_conv == AC_FIX_INCS ||
				ap->a_conv == AC_TMPFILE)
			do_iconv = 0;

		if (fprintf(fo, "\n--%s\nContent-Type: %s", send_boundary, ct)
				< 0)
			goto jerr_header;

		if (charset == NULL) {
			if (putc('\n', fo) == EOF)
				goto jerr_header;
		} else if (fprintf(fo, "; charset=%s\n", charset) < 0)
			goto jerr_header;

		if (fprintf(fo, "Content-Transfer-Encoding: %s\n"
				"Content-Disposition: %s;\n filename=\"",
				_get_encoding(convert),
				ap->a_content_disposition) < 0)
			goto jerr_header;
		if (xmime_write(bn, strlen(bn), fo, CONV_TOHDR, TD_NONE, NULL)
				< 0)
			goto jerr_header;
		if (fwrite("\"\n", sizeof(char), 2, fo) != 2 * sizeof(char))
			goto jerr_header;

		if (ap->a_content_id != NULL && fprintf(fo, "Content-ID: %s\n",
				ap->a_content_id) < 0)
			goto jerr_header;

		if (ap->a_content_description != NULL && fprintf(fo,
				"Content-Description: %s\n",
				ap->a_content_description) < 0)
			goto jerr_header;

		if (putc('\n', fo) == EOF) {
jerr_header:		err = errno;
			goto jerr_fclose;
		}
	}

#ifdef HAVE_ICONV
	if (iconvd != (iconv_t)-1)
		n_iconv_close(iconvd);
	if (do_iconv) {
		char const *tcs = charset_get_lc();
		if (asccasecmp(charset, tcs) != 0 &&
				(iconvd = n_iconv_open(charset, tcs))
				== (iconv_t)-1 && (err = errno) != 0) {
			if (err == EINVAL)
				fprintf(stderr, tr(179,
					"Cannot convert from %s to %s\n"),
					tcs, charset);
			else
				perror("iconv_open");
			goto jerr_fclose;
		}
	}
#endif

	bufsize = INFIX_BUF;
	buf = smalloc(bufsize);
	if (convert == CONV_TOQP
#ifdef HAVE_ICONV
			|| iconvd != (iconv_t)-1
#endif
			)
		lncnt = fsize(fi);
	for (;;) {
		if (convert == CONV_TOQP
#ifdef HAVE_ICONV
				|| iconvd != (iconv_t)-1
#endif
				) {
			if (fgetline(&buf, &bufsize, &lncnt, &inlen, fi, 0)
					== NULL)
				break;
		} else if ((inlen = fread(buf, sizeof *buf, bufsize, fi)) == 0)
			break;
		if (xmime_write(buf, inlen, fo, convert, TD_ICONV, NULL) < 0) {
			err = errno;
			goto jerr;
		}
	}
	if (ferror(fi))
		err = EDOM;
jerr:
	free(buf);
jerr_fclose:
	if (ap->a_conv != AC_TMPFILE)
		Fclose(fi);
jleave:
	return (err);
}

static char const **
_prepare_mta_args(struct name *to, struct header *hp)
{
	size_t j, i = 4 + smopts_count + 2 + count(to) + 1;
	char const **args = salloc(i * sizeof(char*));

	args[0] = value("sendmail-progname");
	if (args[0] == NULL || *args[0] == '\0')
		args[0] = SENDMAIL_PROGNAME;

	args[1] = "-i";
	i = 2;
	if (value("metoo"))
		args[i++] = "-m";
	if (options & OPT_VERBOSE)
		args[i++] = "-v";

	for (j = 0; j < smopts_count; ++j, ++i)
		args[i] = smopts[j];

	/* -r option?  We may only pass skinned addresses, which is why we do
	 * not simply call myorigin() (TODO myorigin shouldn't fullname!) */
	if (options & OPT_r_FLAG) {
		char const *froma;

		if (option_r_arg[0] != '\0')
			froma = option_r_arg;
		else if (hp->h_from != NULL)
			froma = hp->h_from->n_name;
		else
			froma = myorigin(hp);
		if (froma != NULL) {
			args[i++] = "-r";
			args[i++] = froma;
		}
	}

	/* Receivers follow */
	for (; to != NULL; to = to->n_flink)
		if ((to->n_type & GDEL) == 0)
			args[i++] = to->n_name;
	args[i] = NULL;
	return args;
}

/*
 * Fix the header by glopping all of the expanded names from
 * the distribution list into the appropriate fields.
 */
static struct name *
fixhead(struct header *hp, struct name *tolist) /* TODO !HAVE_ASSERTS legacy*/
{
	struct name *np;

	tolist = elide(tolist);

	hp->h_to = hp->h_cc = hp->h_bcc = NULL;
	for (np = tolist; np != NULL; np = np->n_flink)
		if (np->n_type & GDEL) {
#ifdef HAVE_ASSERTS
			assert(0); /* Shouldn't happen here, but later on :)) */
#else
			continue;
#endif
		} else switch (np->n_type & GMASK) {
		case (GTO):
			hp->h_to = cat(hp->h_to, ndup(np, np->n_type|GFULL));
			break;
		case (GCC):
			hp->h_cc = cat(hp->h_cc, ndup(np, np->n_type|GFULL));
			break;
		case (GBCC):
			hp->h_bcc = cat(hp->h_bcc, ndup(np, np->n_type|GFULL));
			break;
		default:
			break;
		}
	return (tolist);
}

/*
 * Put the signature file at fo. TODO send layer rewrite: *integrate in body*!!
 */
static int
put_signature(FILE *fo, int convert)
{
	char *sig, buf[INFIX_BUF], c = '\n';
	FILE *fsig;
	size_t sz;

	sig = value("signature");
	if (sig == NULL || *sig == '\0')
		return 0;
	else if ((sig = file_expand(sig)) == NULL)
		return (-1);
	if ((fsig = Fopen(sig, "r")) == NULL) {
		perror(sig);
		return -1;
	}
	while ((sz = fread(buf, sizeof *buf, INFIX_BUF, fsig)) != 0) {
		c = buf[sz - 1];
		if (xmime_write(buf, sz, fo, convert, TD_NONE, NULL) < 0) {
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
		putc('\n', fo);
	return 0;
}

/*
 * Attach a message to the file buffer.
 */
static int
attach_message(struct attachment *ap, FILE *fo)
{
	struct message *mp;

	fprintf(fo, "\n--%s\n"
		    "Content-Type: message/rfc822\n"
		    "Content-Disposition: inline\n", send_boundary);
	if (ap->a_content_description != NULL)
		fprintf(fo, "Content-Description: %s\n",
			ap->a_content_description);
	fputc('\n', fo);

	mp = &message[ap->a_msgno - 1];
	touch(mp);
	if (sendmp(mp, fo, 0, NULL, SEND_RFC822, NULL) < 0)
		return -1;
	return 0;
}

/*
 * Generate the body of a MIME multipart message.
 */
static int
make_multipart(struct header *hp, int convert, FILE *fi, FILE *fo,
		const char *contenttype, const char *charset)
{
	struct attachment *att;

	fputs("This is a multi-part message in MIME format.\n", fo);
	if (fsize(fi) != 0) {
		char *buf;
		size_t sz, bufsize, cnt;

		fprintf(fo, "\n--%s\n", send_boundary);
		fprintf(fo, "Content-Type: %s", contenttype);
		if (charset)
			fprintf(fo, "; charset=%s", charset);
		fprintf(fo, "\nContent-Transfer-Encoding: %s\n"
				"Content-Disposition: inline\n\n",
				_get_encoding(convert));
		buf = smalloc(bufsize = INFIX_BUF);
		if (convert == CONV_TOQP
#ifdef HAVE_ICONV
				|| iconvd != (iconv_t)-1
#endif
				) {
			fflush(fi);
			cnt = fsize(fi);
		}

		for (;;) {
			if (convert == CONV_TOQP
#ifdef HAVE_ICONV
					|| iconvd != (iconv_t)-1
#endif
					) {
				if (fgetline(&buf, &bufsize, &cnt, &sz, fi, 0)
						== NULL)
					break;
			} else {
				sz = fread(buf, sizeof *buf, bufsize, fi);
				if (sz == 0)
					break;
			}

			if (xmime_write(buf, sz, fo, convert, TD_ICONV, NULL)
					< 0) {
				free(buf);
				return -1;
			}
		}
		free(buf);
		if (ferror(fi))
			return -1;
		if (charset != NULL)
			put_signature(fo, convert);
	}
	for (att = hp->h_attach; att != NULL; att = att->a_flink) {
		if (att->a_msgno) {
			if (attach_message(att, fo) != 0)
				return -1;
		} else {
			if (_attach_file(att, fo) != 0)
				return -1;
		}
	}
	/* the final boundary with two attached dashes */
	fprintf(fo, "\n--%s--\n", send_boundary);
	return 0;
}

/*
 * Prepend a header in front of the collected stuff
 * and return the new file.
 */
static FILE *
infix(struct header *hp, FILE *fi) /* TODO check */
{
	FILE *nfo, *nfi;
	char *tempMail;
#ifdef HAVE_ICONV
	char const *tcs, *convhdr = NULL;
#endif
	enum conversion convert;
	char const *contenttype, *charset = NULL;
	int do_iconv = 0;

	if ((nfo = Ftemp(&tempMail, "Rs", "w", 0600, 1)) == NULL) {
		perror(tr(178, "temporary mail file"));
		return(NULL);
	}
	if ((nfi = Fopen(tempMail, "r")) == NULL) {
		perror(tempMail);
		Fclose(nfo);
	}
	rm(tempMail);
	Ftfree(&tempMail);
	if (nfi == NULL)
		return NULL;

	contenttype = "text/plain"; /* XXX mail body - always text/plain */
	convert = mime_classify_file(fi, &contenttype, &charset, &do_iconv);

#ifdef HAVE_ICONV
	tcs = charset_get_lc();
	if ((convhdr = need_hdrconv(hp, GTO|GSUBJECT|GCC|GBCC|GIDENT)) != 0) {
		if (iconvd != (iconv_t)-1)
			n_iconv_close(iconvd);
		if (asccasecmp(convhdr, tcs) != 0 &&
				(iconvd = n_iconv_open(convhdr, tcs))
				== (iconv_t)-1 && errno != 0) {
			if (errno == EINVAL)
				fprintf(stderr, tr(179,
					"Cannot convert from %s to %s\n"),
					tcs, convhdr);
			else
				perror("iconv_open");
			Fclose(nfo);
			return NULL;
		}
	}
#endif
	if (puthead(hp, nfo,
		   GTO|GSUBJECT|GCC|GBCC|GNL|GCOMMA|GUA|GMIME
		   |GMSGID|GIDENT|GREF|GDATE,
		   SEND_MBOX, convert, contenttype, charset)) {
		Fclose(nfo);
		Fclose(nfi);
#ifdef HAVE_ICONV
		if (iconvd != (iconv_t)-1)
			n_iconv_close(iconvd);
#endif
		return NULL;
	}
#ifdef HAVE_ICONV
	if (iconvd != (iconv_t)-1)
		n_iconv_close(iconvd);
	if (do_iconv && charset != NULL) { /*TODO charset->mime_classify_file*/
		int err;
		if (asccasecmp(charset, tcs) != 0 &&
				(iconvd = n_iconv_open(charset, tcs))
				== (iconv_t)-1 && (err = errno) != 0) {
			if (err == EINVAL)
				fprintf(stderr, tr(179,
					"Cannot convert from %s to %s\n"),
					tcs, charset);
			else
				perror("iconv_open");
			Fclose(nfo);
			return (NULL);
		}
	}
#endif
	if (hp->h_attach != NULL) {
		if (make_multipart(hp, convert, fi, nfo, contenttype, charset)
				!= 0) {
			Fclose(nfo);
			Fclose(nfi);
#ifdef HAVE_ICONV
			if (iconvd != (iconv_t)-1)
				n_iconv_close(iconvd);
#endif
			return NULL;
		}
	} else {
		size_t sz, bufsize, cnt;
		char *buf;

		if (convert == CONV_TOQP
#ifdef HAVE_ICONV
				|| iconvd != (iconv_t)-1
#endif
				) {
			fflush(fi);
			cnt = fsize(fi);
		}
		buf = smalloc(bufsize = INFIX_BUF);
		for (;;) {
			if (convert == CONV_TOQP
#ifdef HAVE_ICONV
					|| iconvd != (iconv_t)-1
#endif
					) {
				if (fgetline(&buf, &bufsize, &cnt, &sz, fi, 0)
						== NULL)
					break;
			} else {
				sz = fread(buf, sizeof *buf, bufsize, fi);
				if (sz == 0)
					break;
			}
			if (xmime_write(buf, sz, nfo, convert, TD_ICONV, NULL)
					< 0) {
				Fclose(nfo);
				Fclose(nfi);
#ifdef HAVE_ICONV
				if (iconvd != (iconv_t)-1)
					n_iconv_close(iconvd);
#endif
				free(buf);
				return NULL;
			}
		}
		free(buf);
		if (ferror(fi)) {
			Fclose(nfo);
			Fclose(nfi);
#ifdef HAVE_ICONV
			if (iconvd != (iconv_t)-1)
				n_iconv_close(iconvd);
#endif
			return NULL;
		}
		if (charset != NULL)
			put_signature(nfo, convert); /* XXX if (text/) !! */
	}
#ifdef HAVE_ICONV
	if (iconvd != (iconv_t)-1)
		n_iconv_close(iconvd);
#endif
	fflush(nfo);
	if (ferror(nfo)) {
		perror(tr(180, "temporary mail file"));
		Fclose(nfo);
		Fclose(nfi);
		return NULL;
	}
	Fclose(nfo);
	Fclose(fi);
	fflush(nfi);
	rewind(nfi);
	return(nfi);
}

/*
 * Save the outgoing mail on the passed file.
 */

/*ARGSUSED*/
static int
savemail(char const *name, FILE *fi)
{
	FILE *fo;
	char *buf;
	size_t bufsize, buflen, cnt;
	int prependnl = 0, error = 0;

	buf = smalloc(bufsize = LINESIZE);
	if ((fo = Zopen(name, "a+", NULL)) == NULL) {
		if ((fo = Zopen(name, "wx", NULL)) == NULL) {
			perror(name);
			free(buf);
			return (-1);
		}
	} else {
		if (fseek(fo, -2L, SEEK_END) == 0) {
			switch (fread(buf, sizeof *buf, 2, fo)) {
			case 2:
				if (buf[1] != '\n') {
					prependnl = 1;
					break;
				}
				/*FALLTHRU*/
			case 1:
				if (buf[0] != '\n')
					prependnl = 1;
				break;
			default:
				if (ferror(fo)) {
					perror(name);
					free(buf);
					return -1;
				}
			}
			fflush(fo);
			if (prependnl) {
				putc('\n', fo);
				fflush(fo);
			}
		}
	}

	fprintf(fo, "From %s %s", myname, time_current.tc_ctime);
	buflen = 0;
	fflush(fi);
	rewind(fi);
	cnt = fsize(fi);
	while (fgetline(&buf, &bufsize, &cnt, &buflen, fi, 0) != NULL) {
#ifdef HAVE_ASSERTS /* TODO assert legacy */
		assert(! is_head(buf, buflen));
#else
		if (is_head(buf, buflen))
			putc('>', fo);
#endif
		fwrite(buf, sizeof *buf, buflen, fo);
	}
	if (buflen && *(buf + buflen - 1) != '\n')
		putc('\n', fo);
	putc('\n', fo);
	fflush(fo);
	if (ferror(fo)) {
		perror(name);
		error = -1;
	}
	if (Fclose(fo) != 0)
		error = -1;
	fflush(fi);
	rewind(fi);
	free(buf);
	return error;
}

/*
 * Interface between the argument list and the mail1 routine
 * which does all the dirty work.
 */
int 
mail(struct name *to, struct name *cc, struct name *bcc,
		char *subject, struct attachment *attach,
		char *quotefile, int recipient_record)
{
	struct header head;
	struct str in, out;

	memset(&head, 0, sizeof head);
	/* The given subject may be in RFC1522 format. */
	if (subject != NULL) {
		in.s = subject;
		in.l = strlen(subject);
		mime_fromhdr(&in, &out, /* TODO ??? TD_ISPR |*/ TD_ICONV);
		head.h_subject = out.s;
	}
	if (! (options & OPT_t_FLAG)) {
		head.h_to = to;
		head.h_cc = cc;
		head.h_bcc = bcc;
	}
	head.h_attach = attach;
	mail1(&head, 0, NULL, quotefile, recipient_record, 0);
	if (subject != NULL)
		free(out.s);
	return 0;
}

/*
 * Send mail to a bunch of user names.  The interface is through
 * the mail routine below.
 */
static int 
sendmail_internal(void *v, int recipient_record)
{
	char *str = v;
	struct header head;

	memset(&head, 0, sizeof head);
	head.h_to = lextract(str, GTO|GFULL);
	mail1(&head, 0, NULL, NULL, recipient_record, 0);
	return 0;
}

int 
csendmail(void *v)
{
	return sendmail_internal(v, 0);
}

int 
cSendmail(void *v)
{
	return sendmail_internal(v, 1);
}

static enum okay
transfer(struct name *to, FILE *input, struct header *hp)
{
	char o[LINESIZE], *cp;
	struct name *np;
	int cnt = 0;
	enum okay ok = OKAY;

	np = to;
	while (np) {
		snprintf(o, sizeof o, "smime-encrypt-%s", np->n_name);
		if ((cp = value(o)) != NULL) {
#ifdef HAVE_SSL
			struct name *nt;
			FILE *ef;
			if ((ef = smime_encrypt(input, cp, np->n_name)) != 0) {
				nt = ndup(np, np->n_type & ~(GFULL|GSKIN));
				if (start_mta(nt, ef, hp) != OKAY)
					ok = STOP;
				Fclose(ef);
			} else {
#else
				fprintf(stderr, tr(225,
					"No SSL support compiled in.\n"));
				ok = STOP;
#endif
				fprintf(stderr, tr(38,
					"Message not sent to <%s>\n"),
					np->n_name);
				_senderror = TRU1;
#ifdef HAVE_SSL
			}
#endif
			rewind(input);
			if (np->n_flink)
				np->n_flink->n_blink = np->n_blink;
			if (np->n_blink)
				np->n_blink->n_flink = np->n_flink;
			if (np == to)
				to = np->n_flink;
			np = np->n_flink;
		} else {
			cnt++;
			np = np->n_flink;
		}
	}
	if (cnt) {
		if (value("smime-force-encryption") ||
				start_mta(to, input, hp) != OKAY)
			ok = STOP;
	}
	return ok;
}

/*
 * Start the Mail Transfer Agent
 * mailing to namelist and stdin redirected to input.
 */
static enum okay
start_mta(struct name *to, FILE *input, struct header *hp)
{
#ifdef HAVE_SMTP
	char *user = NULL, *password = NULL, *skinned = NULL;
#endif
	char const **args = NULL, **t, *mta;
	char *smtp;
	enum okay ok = STOP;
	pid_t pid;
	sigset_t nset;
	(void)hp;

	if ((smtp = value("smtp")) == NULL) {
		if ((mta = value("sendmail")) != NULL) {
			if ((mta = file_expand(mta)) == NULL)
				goto jstop;
		} else
			mta = SENDMAIL;

		args = _prepare_mta_args(to, hp);
		if (options & OPT_DEBUG) {
			printf(tr(181, "Sendmail arguments:"));
			for (t = args; *t != NULL; t++)
				printf(" \"%s\"", *t);
			printf("\n");
			ok = OKAY;
			goto jleave;
		}
	} else {
		mta = NULL; /* Silence cc */
#ifndef HAVE_SMTP
		fputs(tr(194, "No SMTP support compiled in.\n"), stderr);
		goto jstop;
#else
		skinned = skin(myorigin(hp));
		if ((user = smtp_auth_var("-user", skinned)) != NULL &&
				(password = smtp_auth_var("-password",
					skinned)) == NULL)
			password = getpassword(NULL);
#endif
	}

	/*
	 * Fork, set up the temporary mail file as standard
	 * input for "mail", and exec with the user list we generated
	 * far above.
	 */
	if ((pid = fork()) == -1) {
		perror("fork");
jstop:		savedeadletter(input, 0);
		_senderror = TRU1;
		goto jleave;
	}
	if (pid == 0) {
		sigemptyset(&nset);
		sigaddset(&nset, SIGHUP);
		sigaddset(&nset, SIGINT);
		sigaddset(&nset, SIGQUIT);
		sigaddset(&nset, SIGTSTP);
		sigaddset(&nset, SIGTTIN);
		sigaddset(&nset, SIGTTOU);
		freopen("/dev/null", "r", stdin);
#ifdef HAVE_SMTP
		if (smtp != NULL) {
			prepare_child(&nset, 0, 1);
			if (smtp_mta(smtp, to, input, hp,
					user, password, skinned) == 0)
				_exit(0);
		} else {
#endif
			prepare_child(&nset, fileno(input), -1);
			/* If *record* is set then savemail() will move the
			 * file position; it'll call rewind(), but that may
			 * optimize away the systemcall if possible, and since
			 * dup2() shares the position with the original FD the
			 * MTA may end up reading nothing */
			lseek(0, 0, SEEK_SET);
			execv(mta, UNCONST(args));
			perror(mta);
#ifdef HAVE_SMTP
		}
#endif
		savedeadletter(input, 1);
		fputs(tr(182, ". . . message not sent.\n"), stderr);
		_exit(1);
	}
	if ((options & (OPT_DEBUG|OPT_VERBOSE|OPT_BATCH_FLAG)) ||
			value("sendwait")) {
		if (wait_child(pid) == 0)
			ok = OKAY;
		else
			_senderror = TRU1;
	} else {
		ok = OKAY;
		free_child(pid);
	}
jleave:
	return ok;
}

/*
 * Record outgoing mail if instructed to do so.
 */
static enum okay
mightrecord(FILE *fp, struct name *to, int recipient_record)
{
	char *cp, *cq;
	char const *ep;

	if (recipient_record) {
		size_t i = strlen(cq = skinned_name(to)) + 1;
		cp = salloc(i);
		memcpy(cp, cq, i);
		for (cq = cp; *cq && *cq != '@'; cq++)
			;
		*cq = '\0';
	} else
		cp = value("record");
	if (cp != NULL) {
		ep = expand(cp);
		if (ep == NULL) {
			ep = "NULL";
			goto jbail;
		}
		if (value("outfolder") && *ep != '/' && *ep != '+' &&
				which_protocol(ep) == PROTO_FILE) {
			size_t i = strlen(cp);
			cq = salloc(i + 2);
			cq[0] = '+';
			memcpy(cq + 1, cp, i + 1);
			cp = cq;
			ep = expand(cp); /* TODO file_expand() possible? */
			if (ep == NULL) {
				ep = "NULL";
				goto jbail;
			}
		}
		if (savemail(ep, fp) != 0) {
jbail:			fprintf(stderr, tr(285,
				"Failed to save message in %s - "
				"message not sent\n"), ep);
			exit_status |= 1;
			savedeadletter(fp, 1);
			return STOP;
		}
	}
	return OKAY;
}

/*
 * Mail a message on standard input to the people indicated
 * in the passed header.  (Internal interface).
 */
enum okay 
mail1(struct header *hp, int printheaders, struct message *quote,
	char *quotefile, int recipient_record, int doprefix)
{
	enum okay ok = STOP;
	struct name *to;
	FILE *mtf, *nmtf;
	int dosign = -1, err;
	char const *cp;

	_senderror = FAL0;

	/* Update some globals we likely need first */
	time_current_update(&time_current, TRU1);

	/*  */
	if ((cp = value("autocc")) != NULL && *cp)
		hp->h_cc = cat(hp->h_cc, checkaddrs(lextract(cp, GCC|GFULL)));
	if ((cp = value("autobcc")) != NULL && *cp)
		hp->h_bcc = cat(hp->h_bcc, checkaddrs(lextract(cp,GBCC|GFULL)));

	/*
	 * Collect user's mail from standard input.
	 * Get the result as mtf.
	 */
	mtf = collect(hp, printheaders, quote, quotefile, doprefix);
	if (mtf == NULL)
		goto j_leave;

	if (options & OPT_INTERACTIVE) {
		err = (value("bsdcompat") || value("askatend"));
		if (err == 0)
			goto jaskeot;
		if (value("askcc"))
			++err, grab_headers(hp, GCC, 1);
		if (value("askbcc"))
			++err, grab_headers(hp, GBCC, 1);
		if (value("askattach"))
			++err, hp->h_attach = edit_attachments(hp->h_attach);
		if (value("asksign"))
			++err, dosign = yorn(tr(35,
				"Sign this message (y/n)? "));
		if (err == 1) {
jaskeot:
			printf(tr(183, "EOT\n"));
			fflush(stdout);
		}
	}

	if (fsize(mtf) == 0) {
		if (options & OPT_E_FLAG)
			goto jleave;
		if (hp->h_subject == NULL)
			printf(tr(184,
				"No message, no subject; hope that's ok\n"));
		else if (value("bsdcompat") || value("bsdmsgs"))
			printf(tr(185, "Null message body; hope that's ok\n"));
	}

	if (dosign < 0)
		dosign = (value("smime-sign") != NULL);
#ifndef HAVE_SSL
	if (dosign) {
		fprintf(stderr, tr(225, "No SSL support compiled in.\n"));
		goto jleave;
	}
#endif

	/* XXX Update time_current again; once collect() offers editing of more
	 * XXX headers, including Date:, this must only happen if Date: is the
	 * XXX same that it was before collect() (e.g., postponing etc.).
	 * XXX But *do* update otherwise because the mail seems to be backdated
	 * XXX if the user edited some time, which looks odd and it happened
	 * XXX to me that i got mis-dated response mails due to that... */
	time_current_update(&time_current, TRU1);

	/* TODO hrmpf; the MIME/send layer rewrite MUST address the init crap:
	 * TODO setup the header ONCE; note this affects edit.c, collect.c ...,
	 * TODO but: offer a hook that rebuilds/expands/checks/fixates all
	 * TODO header fields ONCE, call that ONCE after user editing etc. has
	 * TODO completed (one edit cycle) */

	/*
	 * Take the user names from the combined to and cc lists and do all the
	 * alias processing.  The POSIX standard says:
	 *   The names shall be substituted when alias is used as a recipient
	 *   address specified by the user in an outgoing message (that is,
	 *   other recipients addressed indirectly through the reply command
	 *   shall not be substituted in this manner).
	 * S-nail thus violates POSIX, as has been pointed out correctly by
	 * Martin Neitzel, but logic, usability und intellectual penetration of
	 * POSIX standards is disputable anyway.  Go for user friendliness.
	 */

	/* Do alias expansion on Reply-To: members, too (Martin Neitzel) */
	/* TODO puthead() YET (!!! see ONCE note above) expands the value, but
	 * TODO doesn't perform alias expansion; encapsulate in the ONCE-o */
	if (hp->h_replyto == NULL && (cp = value("replyto")) != NULL)
		hp->h_replyto = checkaddrs(lextract(cp, GEXTRA|GFULL));
	if (hp->h_replyto != NULL)
		hp->h_replyto = elide(usermap(hp->h_replyto, TRU1));

	/*
	 * TODO what happens now is that all recipients are merged into
	 * TODO a duplicated list with expanded aliases, then this list is
	 * TODO splitted again into the three individual recipient lists (with
	 * TODO duplicates removed).
	 * TODO later on we use the merged list for outof() pipe/file saving,
	 * TODO then we eliminate duplicates (again) and then we use that one
	 * TODO for mightrecord() and transfer(), and again.  ... Please ...
	 */
	/*
	 * NOTE: Due to elide() in fixhead(), ENSURE to,cc,bcc order of to!,
	 * because otherwise the recipients will be "degraded" if they occur
	 * multiple times
	 */
	to = usermap(cat(hp->h_to, cat(hp->h_cc, hp->h_bcc)), FAL0);
	if (to == NULL) {
		fprintf(stderr, tr(186, "No recipients specified\n"));
		_senderror = TRU1;
	}
	to = fixhead(hp, to);

	/*
	 * 'Bit ugly kind of control flow until we find a charset that does it.
	 * XXX Can maybe be done nicer once we have a carrier struct instead
	 * XXX of globals
	 */
	for (charset_iter_reset(hp->h_charset);;) {
		if (charset_iter_next() == NULL)
			;
		else if ((nmtf = infix(hp, mtf)) != NULL)
			break;
		else if ((err = errno) == EILSEQ || err == EINVAL) {
			rewind(mtf);
			continue;
		}

		perror("");
#ifdef HAVE_SSL
jfail_dead:
#endif
		_senderror = TRU1;
		savedeadletter(mtf, 1);
		fputs(tr(187, ". . . message not sent.\n"), stderr);
		goto jleave;
	}

	mtf = nmtf;
#ifdef HAVE_SSL
	if (dosign) {
		if ((nmtf = smime_sign(mtf, hp)) == NULL)
			goto jfail_dead;
		Fclose(mtf);
		mtf = nmtf;
	}
#endif

	/*
	 * Look through the recipient list for names with /'s
	 * in them which we write to as files directly.
	 */
	to = outof(to, mtf, hp, &_senderror);
	if (_senderror)
		savedeadletter(mtf, 0);
	to = elide(to); /* XXX needed only to drop GDELs due to outof()! */
	if (count(to) == 0) {
		if (! _senderror)
			ok = OKAY;
		goto jleave;
	}

	if (mightrecord(mtf, to, recipient_record) != OKAY)
		goto jleave;
	ok = transfer(to, mtf, hp);

jleave:
	Fclose(mtf);
j_leave:
	if (_senderror)
		exit_status |= EXIT_SEND_ERROR;
	return ok;
}

/*
 * Create a Message-Id: header field.
 * Use either the host name or the from address.
 */
static void
message_id(FILE *fo, struct header *hp)
{
	char const *h;
	size_t rl;
	struct tm *tmp;

	if (boption("message-id-disable"))
		goto jleave;
	if ((h = voption("hostname")) != NULL)
		rl = 24;
	else if ((h = skin(myorigin(hp))) != NULL && strchr(h, '@') != NULL)
		rl = 16;
	else
		/* Up to MTA */
		goto jleave;

	tmp = &time_current.tc_gm;
	fprintf(fo, "Message-ID: <%04d%02d%02d%02d%02d%02d.%s%c%s>\n",
		tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday,
			tmp->tm_hour, tmp->tm_min, tmp->tm_sec,
		getrandstring(rl), (rl == 16 ? '%' : '@'), h);
jleave:
	;
}

/*
 * Create a Date: header field.
 * We compare the localtime() and gmtime() results to get the timezone,
 * because numeric timezones are easier to read and because $TZ is
 * not set on most GNU systems.
 */
int
mkdate(FILE *fo, const char *field)
{
	struct tm *tmptr;
	int tzdiff, tzdiff_hour, tzdiff_min;

	tzdiff = time_current.tc_time - mktime(&time_current.tc_gm);
	tzdiff_hour = (int)(tzdiff / 60);
	tzdiff_min = tzdiff_hour % 60;
	tzdiff_hour /= 60;
	tmptr = &time_current.tc_local;
	if (tmptr->tm_isdst > 0)
		tzdiff_hour++;
	return fprintf(fo, "%s: %s, %02d %s %04d %02d:%02d:%02d %+05d\n",
			field,
			weekday_names[tmptr->tm_wday],
			tmptr->tm_mday, month_names[tmptr->tm_mon],
			tmptr->tm_year + 1900, tmptr->tm_hour,
			tmptr->tm_min, tmptr->tm_sec,
			tzdiff_hour * 100 + tzdiff_min);
}

#define	FMT_CC_AND_BCC	{ \
				if (hp->h_cc != NULL && w & GCC) { \
					if (fmt("Cc:", hp->h_cc, fo, \
							w&(GCOMMA|GFILES), 0, \
							action!=SEND_TODISP)) \
						return 1; \
					gotcha++; \
				} \
				if (hp->h_bcc != NULL && w & GBCC) { \
					if (fmt("Bcc:", hp->h_bcc, fo, \
							w&(GCOMMA|GFILES), 0, \
							action!=SEND_TODISP)) \
						return 1; \
					gotcha++; \
				} \
			}
/*
 * Dump the to, subject, cc header on the
 * passed file buffer.
 */
int
puthead(struct header *hp, FILE *fo, enum gfield w,
		enum sendaction action, enum conversion convert,
		char const *contenttype, char const *charset)
{
	int gotcha;
	char const *addr;
	int stealthmua;
	size_t l;
	struct name *np, *fromfield = NULL, *senderfield = NULL;

	if ((addr = value("stealthmua")) != NULL) {
		stealthmua = (strcmp(addr, "noagent") == 0) ? -1 : 1;
	} else
		stealthmua = 0;
	gotcha = 0;
	if (w & GDATE) {
		mkdate(fo, "Date"), gotcha++;
	}
	if (w & GIDENT) {
		if (hp->h_from != NULL) {
			if (fmt("From:", hp->h_from, fo, w&(GCOMMA|GFILES), 0,
						action!=SEND_TODISP))
				return 1;
			gotcha++;
			fromfield = hp->h_from;
		} else if ((addr = myaddrs(hp)) != NULL) {
			if (_putname(addr, w, action, &gotcha, "From:", fo,
						&fromfield))
				return 1;
			hp->h_from = fromfield;
		}
		if (((addr = hp->h_organization) != NULL ||
				(addr = value("ORGANIZATION")) != NULL) &&
				(l = strlen(addr)) > 0) {
			fwrite("Organization: ", sizeof (char), 14, fo);
			if (xmime_write(addr, l, fo,
					action == SEND_TODISP ?
						CONV_NONE:CONV_TOHDR,
					action == SEND_TODISP ?
						TD_ISPR|TD_ICONV:TD_ICONV,
					NULL) < 0)
				return 1;
			gotcha++;
			putc('\n', fo);
		}
		/* TODO see the ONCE TODO note somewhere around this file;
		 * TODO but anyway, do NOT perform alias expansion UNLESS
		 * TODO we are actually sending out! */
		if (hp->h_replyto != NULL) {
			if (fmt("Reply-To:", hp->h_replyto, fo, w & GCOMMA, 0,
					action!=SEND_TODISP))
				return 1;
			gotcha++;
		} else if ((addr = value("replyto")) != NULL)
			if (_putname(addr, w, action, &gotcha, "Reply-To:", fo,
						NULL))
				return 1;
		if (hp->h_sender != NULL) {
			if (fmt("Sender:", hp->h_sender, fo, w & GCOMMA, 0,
					action!=SEND_TODISP))
				return 1;
			gotcha++;
			senderfield = hp->h_sender;
		} else if ((addr = value("sender")) != NULL)
			if (_putname(addr, w, action, &gotcha, "Sender:", fo,
						&senderfield))
				return 1;
		if (check_from_and_sender(fromfield, senderfield))
			return 1;
	}
	if (hp->h_to != NULL && w & GTO) {
		if (fmt("To:", hp->h_to, fo, w&(GCOMMA|GFILES), 0,
					action!=SEND_TODISP))
			return 1;
		gotcha++;
	}
	if (value("bsdcompat") == NULL && value("bsdorder") == NULL)
		FMT_CC_AND_BCC
	if (hp->h_subject != NULL && w & GSUBJECT) {
		fwrite("Subject: ", sizeof (char), 9, fo);
		if (ascncasecmp(hp->h_subject, "re: ", 4) == 0) {
			fwrite("Re: ", sizeof (char), 4, fo);
			if (strlen(hp->h_subject + 4) > 0 &&
				xmime_write(hp->h_subject + 4,
					strlen(hp->h_subject + 4), fo,
					action == SEND_TODISP ?
						CONV_NONE:CONV_TOHDR,
					action == SEND_TODISP ?
						TD_ISPR|TD_ICONV:TD_ICONV,
					NULL) < 0)
				return 1;
		} else if (*hp->h_subject) {
			if (xmime_write(hp->h_subject, strlen(hp->h_subject),
					fo, action == SEND_TODISP ?
						CONV_NONE:CONV_TOHDR,
					action == SEND_TODISP ?
						TD_ISPR|TD_ICONV:TD_ICONV,
					NULL) < 0)
				return 1;
		}
		gotcha++;
		fwrite("\n", sizeof (char), 1, fo);
	}
	if (value("bsdcompat") || value("bsdorder"))
		FMT_CC_AND_BCC
	if (w & GMSGID && stealthmua <= 0)
		message_id(fo, hp), gotcha++;
	if ((np = hp->h_ref) != NULL && w & GREF) {
		fmt("References:", np, fo, 0, 1, 0);
		if (np->n_name) {
			while (np->n_flink)
				np = np->n_flink;
			if (is_addr_invalid(np, 0) == 0) {
				fprintf(fo, "In-Reply-To: %s\n", np->n_name);
				gotcha++;
			}
		}
	}
	if (w & GUA && stealthmua == 0)
		fprintf(fo, "User-Agent: %s %s\n", uagent, version), gotcha++;
	if (w & GMIME) {
		fputs("MIME-Version: 1.0\n", fo), gotcha++;
		if (hp->h_attach != NULL) {
			send_boundary = mime_create_boundary();/*TODO carrier*/
			fprintf(fo, "Content-Type: multipart/mixed;\n"
				" boundary=\"%s\"\n", send_boundary);
		} else {
			fprintf(fo, "Content-Type: %s", contenttype);
			if (charset)
				fprintf(fo, "; charset=%s", charset);
			fprintf(fo, "\nContent-Transfer-Encoding: %s\n",
					_get_encoding(convert));
		}
	}
	if (gotcha && w & GNL)
		putc('\n', fo);
	return(0);
}

/*
 * Format the given header line to not exceed 72 characters.
 */
static int
fmt(char const *str, struct name *np, FILE *fo, int flags, int dropinvalid,
	int domime)
{
	enum {
		m_INIT	= 1<<0,
		m_COMMA	= 1<<1,
		m_NOPF	= 1<<2,
		m_CSEEN	= 1<<3
	} m = (flags & GCOMMA) ? m_COMMA : 0;
	ssize_t col, len;

	col = strlen(str);
	if (col) {
		fwrite(str, sizeof *str, col, fo);
		if (flags & GFILES)
			goto jstep;
		if (col == 9 && asccasecmp(str, "reply-to:") == 0) {
			m |= m_NOPF;
			goto jstep;
		}
		if (value("add-file-recipients"))
			goto jstep;
		if ((col == 3 && ((asccasecmp(str, "to:") == 0) ||
					asccasecmp(str, "cc:") == 0)) ||
				(col == 4 && asccasecmp(str, "bcc:") == 0) ||
				(col == 10 &&
					asccasecmp(str, "Resent-To:") == 0))
			m |= m_NOPF;
	}

jstep:
	for (; np != NULL; np = np->n_flink) {
		if ((m & m_NOPF) && is_fileorpipe_addr(np))
			continue;
		if (is_addr_invalid(np, ! dropinvalid)) {
			if (dropinvalid)
				continue;
			else
				return (1);
		}
		if ((m & (m_INIT | m_COMMA)) == (m_INIT | m_COMMA)) {
			putc(',', fo);
			m |= m_CSEEN;
			++col;
		}
		len = strlen(np->n_fullname);
		++col; /* The separating space */
		if ((m & m_INIT) && col > 1 && col + len > 72) {
			fputs("\n ", fo);
			col = 1;
			m &= ~m_CSEEN;
		} else
			putc(' ', fo);
		m = (m & ~m_CSEEN) | m_INIT;
		len = xmime_write(np->n_fullname, len, fo,
				domime?CONV_TOHDR_A:CONV_NONE,
				TD_ICONV, NULL);
		if (len < 0)
			return (1);
		col += len;
	}
	putc('\n', fo);
	return (0);
}

/*
 * Rewrite a message for resending, adding the Resent-Headers.
 */
static int
infix_resend(FILE *fi, FILE *fo, struct message *mp, struct name *to,
		int add_resent)
{
	size_t cnt, c, bufsize = 0;
	char *buf = NULL;
	char const *cp;
	struct name *fromfield = NULL, *senderfield = NULL;

	cnt = mp->m_size;
	/*
	 * Write the Resent-Fields.
	 */
	if (add_resent) {
		fputs("Resent-", fo);
		mkdate(fo, "Date");
		if ((cp = myaddrs(NULL)) != NULL) {
			if (_putname(cp, GCOMMA, SEND_MBOX, NULL,
					"Resent-From:", fo, &fromfield))
				return 1;
		}
		if ((cp = value("sender")) != NULL) {
			if (_putname(cp, GCOMMA, SEND_MBOX, NULL,
					"Resent-Sender:", fo, &senderfield))
				return 1;
		}
		if (fmt("Resent-To:", to, fo, 1, 1, 0))
			return 1;
		if ((cp = value("stealthmua")) == NULL ||
				strcmp(cp, "noagent") == 0) {
			fputs("Resent-", fo);
			message_id(fo, NULL);
		}
	}
	if (check_from_and_sender(fromfield, senderfield))
		return 1;
	/*
	 * Write the original headers.
	 */
	while (cnt > 0) {
		if (fgetline(&buf, &bufsize, &cnt, &c, fi, 0) == NULL)
			break;
		/* XXX more checks: The From_ line may be seen when resending */
		if (ascncasecmp("status: ", buf, 8) != 0 &&
				strncmp("From ", buf, 5) != 0
				/* In the headers, is_head() is actually
				 * overkill, so a simple ^From_ is sufficient.
				 * ! is_head(buf, c) */
				)
			fwrite(buf, sizeof *buf, c, fo);
		if (cnt > 0 && *buf == '\n')
			break;
	}
	/*
	 * Write the message body.
	 */
	while (cnt > 0) {
		if (fgetline(&buf, &bufsize, &cnt, &c, fi, 0) == NULL)
			break;
		if (cnt == 0 && *buf == '\n')
			break;
		fwrite(buf, sizeof *buf, c, fo);
	}
	if (buf)
		free(buf);
	if (ferror(fo)) {
		perror(tr(188, "temporary mail file"));
		return 1;
	}
	return 0;
}

enum okay 
resend_msg(struct message *mp, struct name *to, int add_resent) /* TODO check */
{
	enum okay ok = STOP;
	FILE *ibuf, *nfo, *nfi;
	char *tempMail;
	struct header head;

	_senderror = FAL0;

	/* Update some globals we likely need first */
	time_current_update(&time_current, TRU1);

	memset(&head, 0, sizeof head);

	if ((to = checkaddrs(to)) == NULL) {
		_senderror = TRU1;
		goto jleave;
	}

	if ((nfo = Ftemp(&tempMail, "Rs", "w", 0600, 1)) == NULL) {
		_senderror = TRU1;
		perror(tr(189, "temporary mail file"));
		goto jleave;
	}
	if ((nfi = Fopen(tempMail, "r")) == NULL) {
		_senderror = TRU1;
		perror(tempMail);
	}
	rm(tempMail);
	Ftfree(&tempMail);
	if (nfi == NULL)
		goto jerr_o;

	if ((ibuf = setinput(&mb, mp, NEED_BODY)) == NULL)
		goto jerr_all;
	head.h_to = to;
	to = fixhead(&head, to);
	if (infix_resend(ibuf, nfo, mp, head.h_to, add_resent) != 0) {
		savedeadletter(nfi, 1);
		fputs(tr(190, ". . . message not sent.\n"), stderr);
jerr_all:
		Fclose(nfi);
jerr_o:
		Fclose(nfo);
		_senderror = TRU1;
		goto jleave;
	}
	Fclose(nfo);
	rewind(nfi);
	to = outof(to, nfi, &head, &_senderror);
	if (_senderror)
		savedeadletter(nfi, 0);
	to = elide(to); /* TODO should have been done in fixhead()? */
	if (count(to) != 0) {
		if (value("record-resent") == NULL ||
				mightrecord(nfi, to, 0) == OKAY)
			ok = transfer(to, nfi, NULL);
	} else if (! _senderror)
		ok = OKAY;
	Fclose(nfi);
jleave:
	if (_senderror)
		exit_status |= EXIT_SEND_ERROR;
	return ok;
}
