/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Mail to mail folders and displays.
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

enum pipeflags {
	PIPE_NULL,	/* No pipe- mimetype handler */
	PIPE_COMM,	/* Normal command */
	PIPE_ASYNC,	/* Normal command, run asynchronous */
	PIPE_TEXT,	/* @ special command to force treatment as text */
	PIPE_MSG	/* Display message (returned as command string) */
};

enum parseflags {
	PARSE_DEFAULT	= 0,
	PARSE_DECRYPT	= 01,
	PARSE_PARTS	= 02
};

static void onpipe(int signo);

static void	_parsemultipart(struct message *zmp, struct mimepart *ip,
			enum parseflags pf, int level);

/* Going for user display, print Part: info string */
static void	_print_part_info(struct str *out, struct mimepart *mip,
			struct ignoretab *doign, int level);

/* Adjust output statistics */
SINLINE void	_addstats(off_t *stats, off_t lines, off_t bytes);

/* Call mime_write() as approbiate and adjust statistics */
SINLINE ssize_t	_out(char const *buf, size_t len, FILE *fp,
			enum conversion convert, enum sendaction action,
			struct quoteflt *qf, off_t *stats, struct str *rest);

/* Query possible pipe command for MIME type */
static enum pipeflags _pipecmd(char **result, char const *content_type);

/* Create a pipe */
static FILE *	_pipefile(char const *pipecomm, FILE **qbuf, bool_t quote,
			bool_t async);

static int sendpart(struct message *zmp, struct mimepart *ip, FILE *obuf,
		struct ignoretab *doign, struct quoteflt *qf,
		enum sendaction action, off_t *stats, int level);
static struct mimepart *parsemsg(struct message *mp, enum parseflags pf);
static enum okay parsepart(struct message *zmp, struct mimepart *ip,
		enum parseflags pf, int level);
static void newpart(struct mimepart *ip, struct mimepart **np, off_t offs,
		int *part);
static void endpart(struct mimepart **np, off_t xoffs, long lines);
static void parse822(struct message *zmp, struct mimepart *ip,
		enum parseflags pf, int level);
#ifdef HAVE_SSL
static void parsepkcs7(struct message *zmp, struct mimepart *ip,
		enum parseflags pf, int level);
#endif
static FILE *newfile(struct mimepart *ip, int *ispipe);
static void pipecpy(FILE *pipebuf, FILE *outbuf, FILE *origobuf,
		struct quoteflt *qf, off_t *stats);
static void statusput(const struct message *mp, FILE *obuf,
		struct quoteflt *qf, off_t *stats);
static void xstatusput(const struct message *mp, FILE *obuf,
		struct quoteflt *qf, off_t *stats);
static void put_from_(FILE *fp, struct mimepart *ip, off_t *stats);

static sigjmp_buf	pipejmp;

/*ARGSUSED*/
static void 
onpipe(int signo)
{
	(void)signo;
	siglongjmp(pipejmp, 1);
}

static void
_parsemultipart(struct message *zmp, struct mimepart *ip, enum parseflags pf,
	int level)
{
	/*
	 * TODO Instead of the recursive multiple run parse we have today,
	 * TODO the send/MIME layer rewrite must create a "tree" of parts with
	 * TODO a single-pass parse, then address each part directly as
	 * TODO necessary; since boundaries start with -- and the content
	 * TODO rather forms a stack this is pretty cheap indeed!
	 */
	struct mimepart	*np = NULL;
	char *boundary, *line = NULL;
	size_t linesize = 0, linelen, cnt, boundlen;
	FILE *ibuf;
	off_t offs;
	int part = 0;
	long lines = 0;

	if ((boundary = mime_get_boundary(ip->m_ct_type, &linelen)) == NULL)
		return;
	boundlen = linelen;
	if ((ibuf = setinput(&mb, (struct message*)ip, NEED_BODY)) == NULL)
		return;
	cnt = ip->m_size;
	while (fgetline(&line, &linesize, &cnt, &linelen, ibuf, 0))
		if (line[0] == '\n')
			break;
	offs = ftell(ibuf);
	newpart(ip, &np, offs, NULL);
	while (fgetline(&line, &linesize, &cnt, &linelen, ibuf, 0)) {
		/* XXX linelen includes LF */
		if (! ((lines > 0 || part == 0) && linelen > boundlen &&
				memcmp(line, boundary, boundlen) == 0)) {
			++lines;
			continue;
		}
		/* Subpart boundary? */
		if (line[boundlen] == '\n') {
			offs = ftell(ibuf);
			if (part > 0) {
				endpart(&np, offs - boundlen - 2, lines);
				newpart(ip, &np, offs - boundlen - 2, NULL);
			}
			endpart(&np, offs, 2);
			newpart(ip, &np, offs, &part);
			lines = 0;
			continue;
		}
		/*
		 * Final boundary?  Be aware of cases where there is no
		 * separating newline in between boundaries, as has been seen
		 * in a message with "Content-Type: multipart/appledouble;"
		 */
		if (linelen < boundlen + 2)
			continue;
		linelen -= boundlen + 2;
		if (line[boundlen] != '-' || line[boundlen + 1] != '-' ||
				(linelen > 0 && line[boundlen + 2] != '\n'))
			continue;
		offs = ftell(ibuf);
		if (part != 0) {
			endpart(&np, offs - boundlen - 4, lines);
			newpart(ip, &np, offs - boundlen - 4, NULL);
		}
		endpart(&np, offs + cnt, 2);
		break;
	}
	if (np) {
		offs = ftell(ibuf);
		endpart(&np, offs, lines);
	}
	for (np = ip->m_multipart; np; np = np->m_nextpart)
		if (np->m_mimecontent != MIME_DISCARD)
			parsepart(zmp, np, pf, level + 1);
	free(line);
}

static void
_print_part_info(struct str *out, struct mimepart *mip,
	struct ignoretab *doign, int level)
{
	struct str ct = {NULL, 0}, cd = {NULL, 0};
	char const *ps;

	/* Max. 24 */
	if (is_ign("content-type", 12, doign)) {
		out->s = mip->m_ct_type_plain;
		out->l = strlen(out->s);
		ct.s = ac_alloc(out->l + 2 +1);
		ct.s[0] = ',';
		ct.s[1] = ' ';
		ct.l = 2;
		if (is_prefix("application/", out->s)) {
			memcpy(ct.s + 2, "appl../", 7);
			ct.l += 7;
			out->l -= 12;
			out->s += 12;
			out->l = smin(out->l, 17);
		} else
			out->l = smin(out->l, 24);
		memcpy(ct.s + ct.l, out->s, out->l);
		ct.l += out->l;
		ct.s[ct.l] = '\0';
	}

	/* Max. 27 */
	if (is_ign("content-disposition", 19, doign) &&
			mip->m_filename != NULL) {
		struct str ti, to;

		ti.l = strlen(ti.s = mip->m_filename);
		mime_fromhdr(&ti, &to, TD_ISPR | TD_ICONV | TD_DELCTRL);
		to.l = MIN(to.l, 25);
		cd.s = ac_alloc(to.l + 3);
		cd.s[0] = ',';
		cd.s[1] = ' ';
		memcpy(cd.s + 2, to.s, to.l);
		to.l += 2;
		cd.s[to.l] = '\0';
		free(to.s);
	}

	/* Take care of "99.99", i.e., 5 */
	if ((ps = mip->m_partstring) == NULL || ps[0] == '\0')
		ps = "?";

	/*
	 * Assume maximum possible sizes for 64 bit integers here to avoid any
	 * buffer overflows just in case we have a bug somewhere and / or the
	 * snprintf() is our internal version that doesn't really provide hard
	 * buffer cuts
	 */
#define __msg	"%s[-- #%s : %lu/%lu%s%s --]\n"
	out->l = sizeof(__msg) + strlen(ps) + 2*21 + ct.l + cd.l + 1;
	out->s = salloc(out->l);
	out->l = snprintf(out->s, out->l, __msg,
			(level || (ps[0] != '1' && ps[1] == '\0')) ? "\n" : "",
			ps, (ul_it)mip->m_lines, (ul_it)mip->m_size,
			(ct.s != NULL ? ct.s : ""), (cd.s != NULL ? cd.s : ""));
	out->s[out->l] = '\0';
#undef __msg

	if (cd.s != NULL)
		ac_free(cd.s);
	if (ct.s != NULL)
		ac_free(ct.s);
}

SINLINE void
_addstats(off_t *stats, off_t lines, off_t bytes)
{
	if (stats != NULL) {
		if (stats[0] >= 0)
			stats[0] += lines;
		stats[1] += bytes;
	}
}

SINLINE ssize_t
_out(char const *buf, size_t len, FILE *fp, enum conversion convert, enum
	sendaction action, struct quoteflt *qf, off_t *stats, struct str *rest)
{
	ssize_t sz = 0, n;
	int flags;
	char const *cp;

#if 0
	Well ... it turns out to not work like that since of course a valid
	RFC 4155 compliant parser, like S-nail, takes care for From_ lines only
	after an empty line has been seen, which cannot be detected that easily
	right here!
ifdef HAVE_ASSERTS /* TODO assert legacy */
	/* TODO if at all, this CAN only happen for SEND_DECRYPT, since all
	 * TODO other input situations handle RFC 4155 OR, if newly generated,
	 * TODO enforce quoted-printable if there is From_, as "required" by
	 * TODO RFC 5751.  The SEND_DECRYPT case is not yet overhauled;
	 * TODO if it may happen in this path, we should just treat decryption
	 * TODO as we do for the other input paths; i.e., handle it in SSL!! */
	if (action == SEND_MBOX || action == SEND_DECRYPT)
		assert(! is_head(buf, len));
#else
	if ((/*action == SEND_MBOX ||*/ action == SEND_DECRYPT) &&
			is_head(buf, len)) {
		putc('>', fp);
		++sz;
	}
#endif

	flags = ((int)action & _TD_EOF);
	action &= ~_TD_EOF;
	n = mime_write(buf, len, fp,
			action == SEND_MBOX ? CONV_NONE : convert,
			flags |
			(action == SEND_TODISP || action == SEND_TODISP_ALL ||
					action == SEND_QUOTE ||
					action == SEND_QUOTE_ALL ?
				TD_ISPR|TD_ICONV :
				action == SEND_TOSRCH || action == SEND_TOPIPE ?
					TD_ICONV :
				action == SEND_TOFLTR ?
					TD_DELCTRL :
				action == SEND_SHOW ?
					TD_ISPR : TD_NONE),
			qf, rest);
	if (n < 0)
		sz = n;
	else if (n > 0) {
		sz = (ssize_t)((size_t)sz + n);
		n = 0;
		if (stats != NULL && stats[0] != -1)
			for (cp = buf; cp < &buf[sz]; ++cp)
				if (*cp == '\n')
					++n;
		_addstats(stats, n, sz);
	}
	return sz;
}

static enum pipeflags
_pipecmd(char **result, char const *content_type)
{
	enum pipeflags ret;
	char *s, *cp;
	char const *cq;

	ret = PIPE_NULL;
	*result = NULL;
	if (content_type == NULL)
		goto jleave;

	/* First check wether there is a special pipe-MIMETYPE handler */
	s = ac_alloc(strlen(content_type) + 6);
	memcpy(s, "pipe-", 5);
	cp = &s[5];
	cq = content_type;
	do
		*cp++ = lowerconv(*cq);
	while (*cq++ != '\0');
	cp = value(s);
	ac_free(s);

	if (cp == NULL)
		goto jleave;

	/* User specified a command, inspect for special cases */
	if (cp[0] != '@') {
		/* Normal command line */
		ret = PIPE_COMM;
		*result = cp;
	} else if (*++cp == '\0') {
		/* Treat as plain text */
		ret = PIPE_TEXT;
	} else if (! msglist_is_single) {
		/* Viewing multiple messages in one go, don't block system */
		ret = PIPE_MSG;
		*result = UNCONST(tr(86,
			"[Directly address message only to display this]\n"));
	} else {
		/* Viewing a single message only */
#if 0	/* TODO send/MIME layer rewrite: when we have a single-pass parser
	 * TODO then the parsing phase and the send phase will be separated;
	 * TODO that allows us to ask a user *before* we start the send, i.e.,
	 * TODO *before* a pager pipe is setup (which is the problem with
	 * TODO the '#if 0' code here) */
		size_t l = strlen(content_type);
		char const *x = tr(999, "Should i display a part `%s' (y/n)? ");
		s = ac_alloc(l += strlen(x) + 1);
		snprintf(s, l - 1, x, content_type);
		l = yorn(s);
			puts(""); /* .. we've hijacked a pipe 8-] ... */
		ac_free(s);
		if (! l) {
			x = tr(210, "[User skipped diplay]\n");
			ret = PIPE_MSG;
			*result = UNCONST(x);
		} else
#endif
		if (cp[0] == '&')
			/* Asynchronous command, normal command line */
			ret = PIPE_ASYNC, *result = ++cp;
		else
			ret = PIPE_COMM, *result = cp;
	}
jleave:
	return ret;
}

static FILE *
_pipefile(char const *pipecomm, FILE **qbuf, bool_t quote, bool_t async)
{
	char const *sh;
	FILE *rbuf = *qbuf;

	if (quote) {
		char *tempPipe;

		if ((*qbuf = Ftemp(&tempPipe, "Rp", "w+", 0600, 1)) == NULL) {
			perror(tr(173, "tmpfile"));
			*qbuf = rbuf;
		}
		unlink(tempPipe);
		Ftfree(&tempPipe);
		async = FAL0;
	}
	if ((sh = value("SHELL")) == NULL)
		sh = SHELL;
	if ((rbuf = Popen(pipecomm, "W", sh,
			async ? -1 : fileno(*qbuf))) == NULL)
		perror(pipecomm);
	else {
		fflush(*qbuf);
		if (*qbuf != stdout)
			fflush(stdout);
	}
	return rbuf;
}

/*
 * Send message described by the passed pointer to the
 * passed output buffer.  Return -1 on error.
 * Adjust the status: field if need be.
 * If doign is given, suppress ignored header fields.
 * prefix is a string to prepend to each output line.
 * action = data destination (SEND_MBOX,_TOFILE,_TODISP,_QUOTE,_DECRYPT).
 * stats[0] is line count, stats[1] is character count. stats may be NULL.
 * Note that stats[0] is valid for SEND_MBOX only.
 */
int
sendmp(struct message *mp, FILE *obuf, struct ignoretab *doign,
	char const *prefix, enum sendaction action, off_t *stats)
{
	int rv = -1, c;
	size_t cnt, sz, i;
	FILE *ibuf;
	enum parseflags pf;
	struct mimepart *ip;
	struct quoteflt qf;

	if (mp == dot && action != SEND_TOSRCH && action != SEND_TOFLTR)
		did_print_dot = 1;
	if (stats)
		stats[0] = stats[1] = 0;
	quoteflt_init(&qf, prefix);

	/*
	 * First line is the From_ line, so no headers there to worry about.
	 */
	if ((ibuf = setinput(&mb, mp, NEED_BODY)) == NULL)
		return -1;

	cnt = mp->m_size;
	sz = 0;
	if (mp->m_flag & MNOFROM) {
		if (doign != allignore && doign != fwdignore &&
				action != SEND_RFC822)
			sz = fprintf(obuf, "%.*sFrom %s %s\n",
					(int)qf.qf_pfix_len,
					(qf.qf_pfix_len != 0 ? qf.qf_pfix : ""),
					fakefrom(mp), fakedate(mp->m_time));
	} else {
		if (qf.qf_pfix_len > 0 && doign != allignore &&
				doign != fwdignore && action != SEND_RFC822) {
			i = fwrite(qf.qf_pfix, sizeof *qf.qf_pfix,
				qf.qf_pfix_len, obuf);
			if (i != qf.qf_pfix_len)
				goto jleave;
			sz += i;
		}
		while (cnt && (c = getc(ibuf)) != EOF) {
			if (doign != allignore && doign != fwdignore &&
					action != SEND_RFC822) {
				putc(c, obuf);
				sz++;
			}
			cnt--;
			if (c == '\n')
				break;
		}
	}
	if (sz)
		_addstats(stats, 1, sz);

	pf = 0;
	if (action != SEND_MBOX && action != SEND_RFC822 && action != SEND_SHOW)
		pf |= PARSE_DECRYPT|PARSE_PARTS;
	if ((ip = parsemsg(mp, pf)) == NULL)
		goto jleave;

	rv = sendpart(mp, ip, obuf, doign, &qf, action, stats, 0);
jleave:
	quoteflt_destroy(&qf);
	return rv;
}

static int
sendpart(struct message *zmp, struct mimepart *ip, FILE *obuf,
	struct ignoretab *doign, struct quoteflt *qf,
	enum sendaction volatile action, off_t *volatile stats, int level)
{
	int volatile ispipe, rt = 0;
	struct str rest;
	char *line = NULL, *cp, *cp2, *start, *pipecomm = NULL;
	size_t linesize = 0, linelen, cnt;
	int dostat, infld = 0, ignoring = 1, isenc, c;
	struct mimepart	*volatile np;
	FILE *volatile ibuf = NULL, *volatile pbuf = obuf,
		*volatile qbuf = obuf, *origobuf = obuf;
	enum conversion	volatile convert;
	sighandler_type	volatile oldpipe = SIG_DFL;
	long lineno = 0;

	if (ip->m_mimecontent == MIME_PKCS7 && ip->m_multipart &&
			action != SEND_MBOX && action != SEND_RFC822 &&
			action != SEND_SHOW)
		goto skip;
	dostat = 0;
	if (level == 0) {
		if (doign != NULL) {
			if (!is_ign("status", 6, doign))
				dostat |= 1;
			if (!is_ign("x-status", 8, doign))
				dostat |= 2;
		} else
			dostat = 3;
	}
	if ((ibuf = setinput(&mb, (struct message *)ip, NEED_BODY)) == NULL)
		return -1;
	cnt = ip->m_size;
	if (ip->m_mimecontent == MIME_DISCARD)
		goto skip;

	if ((ip->m_flag & MNOFROM) == 0)
		while (cnt && (c = getc(ibuf)) != EOF) {
			cnt--;
			if (c == '\n')
				break;
		}
	isenc = 0;
	convert = action == SEND_TODISP || action == SEND_TODISP_ALL ||
			action == SEND_QUOTE || action == SEND_QUOTE_ALL ||
			action == SEND_TOSRCH || action == SEND_TOFLTR ?
		CONV_FROMHDR : CONV_NONE;

	/* Work the headers */
	quoteflt_reset(qf, obuf);
	while (fgetline(&line, &linesize, &cnt, &linelen, ibuf, 0)) {
		lineno++;
		if (line[0] == '\n') {
			/*
			 * If line is blank, we've reached end of
			 * headers, so force out status: field
			 * and note that we are no longer in header
			 * fields
			 */
			if (dostat & 1)
				statusput(zmp, obuf, qf, stats);
			if (dostat & 2)
				xstatusput(zmp, obuf, qf, stats);
			if (doign != allignore)
				_out("\n", 1, obuf, CONV_NONE, SEND_MBOX,
					qf, stats, NULL);
			break;
		}
		isenc &= ~1;
		if (infld && blankchar(line[0])) {
			/*
			 * If this line is a continuation (via space or tab)
			 * of a previous header field, determine if the start
			 * of the line is a MIME encoded word.
			 */
			if (isenc & 2) {
				for (cp = line; blankchar(*cp); cp++);
				if (cp > line && linelen - (cp - line) > 8 &&
						cp[0] == '=' && cp[1] == '?')
					isenc |= 1;
			}
		} else {
			/*
			 * Pick up the header field if we have one.
			 */
			for (cp = line; (c = *cp & 0377) && c != ':' &&
					!spacechar(c); ++cp)
				;
			cp2 = cp;
			while (spacechar(*cp))
				++cp;
			if (cp[0] != ':' && level == 0 && lineno == 1) {
				/*
				 * Not a header line, force out status:
				 * This happens in uucp style mail where
				 * there are no headers at all.
				 */
				if (dostat & 1)
					statusput(zmp, obuf, qf, stats);
				if (dostat & 2)
					xstatusput(zmp, obuf, qf, stats);
				if (doign != allignore)
					_out("\n", 1, obuf, CONV_NONE,SEND_MBOX,
						qf, stats, NULL);
				break;
			}
			/*
			 * If it is an ignored field and
			 * we care about such things, skip it.
			 */
			c = *cp2;
			*cp2 = 0;	/* temporarily null terminate */
			if ((doign && is_ign(line, cp2 - line, doign)) ||
					(action == SEND_MBOX &&
					 !boption("keep-content-length") &&
					 (asccasecmp(line, "content-length")==0
					 || asccasecmp(line, "lines") == 0)))
				ignoring = 1;
			else if (asccasecmp(line, "status") == 0) {
				 /*
				  * If the field is "status," go compute
				  * and print the real Status: field
				  */
				if (dostat & 1) {
					statusput(zmp, obuf, qf, stats);
					dostat &= ~1;
					ignoring = 1;
				}
			} else if (asccasecmp(line, "x-status") == 0) {
				/*
				 * If the field is "status," go compute
				 * and print the real Status: field
				 */
				if (dostat & 2) {
					xstatusput(zmp, obuf, qf, stats);
					dostat &= ~2;
					ignoring = 1;
				}
			} else
				ignoring = 0;
			*cp2 = c;
			infld = 1;
		}
		/*
		 * Determine if the end of the line is a MIME encoded word.
		 */
		isenc &= ~2;
		if (cnt && (c = getc(ibuf)) != EOF) {
			if (blankchar(c)) {
				if (linelen > 0 && line[linelen - 1] == '\n')
					cp = &line[linelen - 2];
				else
					cp = &line[linelen - 1];
				while (cp >= line && whitechar(*cp))
					++cp;
				if (cp - line > 8 && cp[0] == '=' &&
						cp[-1] == '?')
					isenc |= 2;
			}
			ungetc(c, ibuf);
		}
		if (!ignoring) {
			size_t len = linelen;
			start = line;
			if (action == SEND_TODISP ||
					action == SEND_TODISP_ALL ||
					action == SEND_QUOTE ||
					action == SEND_QUOTE_ALL ||
					action == SEND_TOSRCH ||
					action == SEND_TOFLTR) {
				/*
				 * Strip blank characters if two MIME-encoded
				 * words follow on continuing lines.
				 */
				if (isenc & 1)
					while (len > 0 && blankchar(*start)) {
						++start;
						--len;
					}
				if (isenc & 2)
					if (len > 0 && start[len - 1] == '\n')
						--len;
				while (len > 0 && blankchar(start[len - 1]))
					--len;
			}
			_out(start, len, obuf, convert, action, qf, stats,
				NULL);
			if (ferror(obuf)) {
				free(line);
				return -1;
			}
		}
	}
	quoteflt_flush(qf);
	free(line);
	line = NULL;

skip:
	switch (ip->m_mimecontent) {
	case MIME_822:
		switch (action) {
		case SEND_TOFLTR:
			putc('\0', obuf);
			/*FALLTHRU*/
		case SEND_TODISP:
		case SEND_TODISP_ALL:
		case SEND_QUOTE:
		case SEND_QUOTE_ALL:
			if (value("rfc822-body-from_")) {
				if (qf->qf_pfix_len > 0) {
					size_t i = fwrite(qf->qf_pfix,
						sizeof *qf->qf_pfix,
						qf->qf_pfix_len, obuf);
					if (i == qf->qf_pfix_len)
						_addstats(stats, 0, i);
				}
				put_from_(obuf, ip->m_multipart, stats);
			}
			/*FALLTHRU*/
		case SEND_TOSRCH:
		case SEND_DECRYPT:
			goto multi;
		case SEND_TOFILE:
		case SEND_TOPIPE:
			if (value("rfc822-body-from_"))
				put_from_(obuf, ip->m_multipart, stats);
			/*FALLTHRU*/
		case SEND_MBOX:
		case SEND_RFC822:
		case SEND_SHOW:
			break;
		}
		break;
	case MIME_TEXT_HTML:
		if (action == SEND_TOFLTR)
			putc('\b', obuf);
		/*FALLTHRU*/
	case MIME_TEXT:
	case MIME_TEXT_PLAIN:
		switch (action) {
		case SEND_TODISP:
		case SEND_TODISP_ALL:
		case SEND_QUOTE:
		case SEND_QUOTE_ALL:
			ispipe = TRU1;
			switch (_pipecmd(&pipecomm, ip->m_ct_type_plain)) {
			case PIPE_MSG:
				_out(pipecomm, strlen(pipecomm), obuf,
					CONV_NONE, SEND_MBOX, qf, stats, NULL);
				pipecomm = NULL;
				/* FALLTRHU */
			case PIPE_TEXT:
			case PIPE_COMM:
			case PIPE_ASYNC:
			case PIPE_NULL:
				break;
			}
			/* FALLTRHU */
		default:
			break;
		}
		break;
	case MIME_DISCARD:
		if (action != SEND_DECRYPT)
			return rt;
		break;
	case MIME_PKCS7:
		if (action != SEND_MBOX && action != SEND_RFC822 &&
				action != SEND_SHOW && ip->m_multipart)
			goto multi;
		/*FALLTHRU*/
	default:
		switch (action) {
		case SEND_TODISP:
		case SEND_TODISP_ALL:
		case SEND_QUOTE:
		case SEND_QUOTE_ALL:
			ispipe = TRU1;
			switch (_pipecmd(&pipecomm, ip->m_ct_type_plain)) {
			case PIPE_MSG:
				_out(pipecomm, strlen(pipecomm), obuf,
					CONV_NONE, SEND_MBOX, qf, stats, NULL);
				pipecomm = NULL;
				break;
			case PIPE_ASYNC:
				ispipe = FAL0;
				/* FALLTHRU */
			case PIPE_COMM:
			case PIPE_NULL:
				break;
			case PIPE_TEXT:
				goto jcopyout; /* break; break; */
			}
			if (pipecomm != NULL)
				break;
			if (level == 0 && cnt) {
				char const *x = tr(210, "[Binary content]\n");
				_out(x, strlen(x), obuf, CONV_NONE, SEND_MBOX,
					qf, stats, NULL);
			}
			/*FALLTHRU*/
		case SEND_TOFLTR:
			return rt;
		case SEND_TOFILE:
		case SEND_TOPIPE:
		case SEND_TOSRCH:
		case SEND_DECRYPT:
		case SEND_MBOX:
		case SEND_RFC822:
		case SEND_SHOW:
			break;
		}
		break;
	case MIME_ALTERNATIVE:
		if ((action == SEND_TODISP || action == SEND_QUOTE) &&
				value("print-alternatives") == NULL) {
			bool_t doact = FAL0;
			for (np = ip->m_multipart; np; np = np->m_nextpart)
				if (np->m_mimecontent == MIME_TEXT_PLAIN)
					doact = TRU1;
			if (doact) {
				for (np = ip->m_multipart; np;
						np = np->m_nextpart) {
					if (np->m_ct_type_plain != NULL &&
							action != SEND_QUOTE) {
						_print_part_info(&rest, np,
							doign, level);
						_out(rest.s, rest.l, obuf,
							CONV_NONE, SEND_MBOX,
							qf, stats, NULL);
					}
					if (doact && np->m_mimecontent ==
							MIME_TEXT_PLAIN) {
						doact = FAL0;
						rt = sendpart(zmp, np, obuf,
							doign, qf, action,
							stats, level + 1);
						quoteflt_reset(qf, origobuf);
						if (rt < 0)
							break;
					}
				}
				return rt;
			}
		}
		/*FALLTHRU*/
	case MIME_MULTI:
	case MIME_DIGEST:
		switch (action) {
		case SEND_TODISP:
		case SEND_TODISP_ALL:
		case SEND_QUOTE:
		case SEND_QUOTE_ALL:
		case SEND_TOFILE:
		case SEND_TOPIPE:
		case SEND_TOSRCH:
		case SEND_TOFLTR:
		case SEND_DECRYPT:
		multi:
			if ((action == SEND_TODISP ||
					action == SEND_TODISP_ALL) &&
			    ip->m_multipart != NULL &&
			    ip->m_multipart->m_mimecontent == MIME_DISCARD &&
			    ip->m_multipart->m_nextpart == NULL) {
				char const *x = tr(85,
					"[Missing multipart boundary - "
					"use \"show\" to display "
					"the raw message]\n");
				_out(x, strlen(x), obuf, CONV_NONE, SEND_MBOX,
					qf, stats, NULL);
			}
			for (np = ip->m_multipart; np; np = np->m_nextpart) {
				if (np->m_mimecontent == MIME_DISCARD &&
						action != SEND_DECRYPT)
					continue;
				ispipe = FAL0;
				switch (action) {
				case SEND_TOFILE:
					if (np->m_partstring &&
							strcmp(np->m_partstring,
							"1") == 0)
						break;
					stats = NULL;
					if ((obuf = newfile(np,
							UNVOLATILE(&ispipe)))
							== NULL)
						continue;
					if (!ispipe)
						break;
					if (sigsetjmp(pipejmp, 1)) {
						rt = -1;
						goto jpipe_close;
					}
					oldpipe = safe_signal(SIGPIPE, onpipe);
					break;
				case SEND_TODISP:
				case SEND_TODISP_ALL:
				case SEND_QUOTE_ALL:
					if (ip->m_mimecontent != MIME_MULTI &&
							ip->m_mimecontent !=
							MIME_ALTERNATIVE &&
							ip->m_mimecontent !=
							MIME_DIGEST)
						break;
					_print_part_info(&rest, np, doign,
						level);
					_out(rest.s, rest.l, obuf,
						CONV_NONE, SEND_MBOX, qf,
						stats, NULL);
					break;
				case SEND_TOFLTR:
					putc('\0', obuf);
					/*FALLTHRU*/
				case SEND_MBOX:
				case SEND_RFC822:
				case SEND_SHOW:
				case SEND_TOSRCH:
				case SEND_QUOTE:
				case SEND_DECRYPT:
				case SEND_TOPIPE:
					break;
				}

				quoteflt_flush(qf);
				if (sendpart(zmp, np, obuf, doign, qf,
						action, stats, level+1) < 0)
					rt = -1;
				quoteflt_reset(qf, origobuf);
				if (action == SEND_QUOTE)
					break;
				if (action == SEND_TOFILE && obuf != origobuf) {
					if (!ispipe)
						Fclose(obuf);
					else {
jpipe_close:					safe_signal(SIGPIPE, SIG_IGN);
						Pclose(obuf, TRU1);
						safe_signal(SIGPIPE, oldpipe);
					}
				}
			}
			return rt;
		case SEND_MBOX:
		case SEND_RFC822:
		case SEND_SHOW:
			break;
		}
	}

	/*
	 * Copy out message body
	 */
jcopyout:
	if (doign == allignore && level == 0)	/* skip final blank line */
		cnt--;
	switch (ip->m_mimeenc) {
	case MIME_BIN:
		if (stats)
			stats[0] = -1;
		/*FALLTHRU*/
	case MIME_7B:
	case MIME_8B:
		convert = CONV_NONE;
		break;
	case MIME_QP:
		convert = CONV_FROMQP;
		break;
	case MIME_B64:
		switch (ip->m_mimecontent) {
		case MIME_TEXT:
		case MIME_TEXT_PLAIN:
		case MIME_TEXT_HTML:
			convert = CONV_FROMB64_T;
			break;
		default:
			convert = CONV_FROMB64;
		}
		break;
	default:
		convert = CONV_NONE;
	}
	if (action == SEND_DECRYPT || action == SEND_MBOX ||
			action == SEND_RFC822 || action == SEND_SHOW)
		convert = CONV_NONE;
#ifdef HAVE_ICONV
	if ((action == SEND_TODISP || action == SEND_TODISP_ALL ||
			action == SEND_QUOTE || action == SEND_QUOTE_ALL ||
			action == SEND_TOSRCH) &&
			(ip->m_mimecontent == MIME_TEXT_PLAIN ||
			 ip->m_mimecontent == MIME_TEXT_HTML ||
			 ip->m_mimecontent == MIME_TEXT)) {
		char const *tcs = charset_get_lc();

		if (iconvd != (iconv_t)-1)
			n_iconv_close(iconvd);
		/* TODO Since Base64 has an odd 4:3 relation in between input
		 * TODO and output an input line may end with a partial
		 * TODO multibyte character; this is no problem at all unless
		 * TODO we send to the display or whatever, i.e., ensure
		 * TODO makeprint() or something; to avoid this trap, *force*
		 * TODO iconv(), in which case this layer will handle leftovers
		 * TODO correctly */
		if (convert == CONV_FROMB64_T ||
				(asccasecmp(tcs, ip->m_charset) &&
				asccasecmp(charset_get_7bit(),
				ip->m_charset))) {
			iconvd = n_iconv_open(tcs, ip->m_charset);
			/* XXX Don't bail out if we cannot iconv(3) here;
			 * XXX alternatively we could avoid trying to open
			 * XXX if ip->m_charset is "unknown-8bit", which was
			 * XXX the one that has bitten me?? */
			/*
			 * TODO errors should DEFINETELY not be scrolled away!
			 * TODO what about an error buffer (think old shsp(1)),
			 * TODO re-dump errors since last snapshot when the
			 * TODO command loop enters again?  i.e., at least print
			 * TODO "There were errors ?" before the next prompt,
			 * TODO so that the user can look at the error buffer?
			 */
			if (iconvd == (iconv_t)-1 && errno == EINVAL) {
				fprintf(stderr, tr(179,
					"Cannot convert from %s to %s\n"),
					ip->m_charset, tcs);
				/*return -1;*/
			}
		}
	}
#endif
	if (pipecomm != NULL &&
			(action == SEND_TODISP || action == SEND_TODISP_ALL ||
			action == SEND_QUOTE || action == SEND_QUOTE_ALL)) {
		qbuf = obuf;
		pbuf = _pipefile(pipecomm, UNVOLATILE(&qbuf),
			action == SEND_QUOTE || action == SEND_QUOTE_ALL,
			!ispipe);
		action = SEND_TOPIPE;
		if (pbuf != qbuf) {
			oldpipe = safe_signal(SIGPIPE, onpipe);
			if (sigsetjmp(pipejmp, 1))
				goto end;
		}
	} else
		pbuf = qbuf = obuf;

	{
	bool_t eof;
	size_t save_qf_pfix_len = qf->qf_pfix_len;
	off_t *save_stats = stats;

	if (pbuf != origobuf) {
		qf->qf_pfix_len = 0; /* XXX legacy (remove filter instead) */
		stats = NULL;
	}
	eof = FAL0;
	rest.s = NULL;
	rest.l = 0;

	quoteflt_reset(qf, pbuf);
	while (!eof && fgetline(&line, &linesize, &cnt, &linelen, ibuf, 0)) {
joutln:
		if (_out(line, linelen, pbuf, convert, action, qf, stats,
				&rest) < 0 || ferror(pbuf)) {
			rt = -1; /* XXX Should bail away?! */
			break;
		}
	}
	if (!eof && rest.l != 0) {
		linelen = 0;
		eof = TRU1;
		action |= _TD_EOF;
		goto joutln;
	}
	quoteflt_flush(qf);
	if (rest.s != NULL)
		free(rest.s);

	if (pbuf != origobuf) {
		qf->qf_pfix_len = save_qf_pfix_len;
		stats = save_stats;
	}
	}

end:	free(line);
	if (pbuf != qbuf) {
		safe_signal(SIGPIPE, SIG_IGN);
		Pclose(pbuf, ispipe);
		safe_signal(SIGPIPE, oldpipe);
		if (qbuf != obuf)
			pipecpy(qbuf, obuf, origobuf, qf, stats);
	}
#ifdef HAVE_ICONV
	if (iconvd != (iconv_t)-1)
		n_iconv_close(iconvd);
#endif
	return rt;
}

static struct mimepart *
parsemsg(struct message *mp, enum parseflags pf)
{
	struct mimepart	*ip;

	ip = csalloc(1, sizeof *ip);
	ip->m_flag = mp->m_flag;
	ip->m_have = mp->m_have;
	ip->m_block = mp->m_block;
	ip->m_offset = mp->m_offset;
	ip->m_size = mp->m_size;
	ip->m_xsize = mp->m_xsize;
	ip->m_lines = mp->m_lines;
	ip->m_xlines = mp->m_lines;
	if (parsepart(mp, ip, pf, 0) != OKAY)
		return NULL;
	return ip;
}

static enum okay
parsepart(struct message *zmp, struct mimepart *ip, enum parseflags pf,
		int level)
{
	char	*cp;

	ip->m_ct_type = hfield1("content-type", (struct message *)ip);
	if (ip->m_ct_type != NULL) {
		ip->m_ct_type_plain = savestr(ip->m_ct_type);
		if ((cp = strchr(ip->m_ct_type_plain, ';')) != NULL)
			*cp = '\0';
	} else if (ip->m_parent && ip->m_parent->m_mimecontent == MIME_DIGEST)
		ip->m_ct_type_plain = UNCONST("message/rfc822");
	else
		ip->m_ct_type_plain = UNCONST("text/plain");

	if (ip->m_ct_type)
		ip->m_charset = mime_getparam("charset", ip->m_ct_type);
	if (ip->m_charset == NULL)
		ip->m_charset = charset_get_7bit();
	ip->m_ct_transfer_enc = hfield1("content-transfer-encoding",
			(struct message *)ip);
	ip->m_mimeenc = ip->m_ct_transfer_enc ?
		mime_getenc(ip->m_ct_transfer_enc) : MIME_7B;
	if ((cp = hfield1("content-disposition", (struct message *)ip)) == 0 ||
			(ip->m_filename = mime_getparam("filename", cp)) == 0)
		if (ip->m_ct_type != NULL)
			ip->m_filename = mime_getparam("name", ip->m_ct_type);
	ip->m_mimecontent = mime_classify_content_of_part(ip);

	if (pf & PARSE_PARTS) {
		if (level > 9999) {
			fprintf(stderr, tr(36,
				"MIME content too deeply nested\n"));
			return STOP;
		}
		switch (ip->m_mimecontent) {
		case MIME_PKCS7:
			if (pf & PARSE_DECRYPT) {
#ifdef HAVE_SSL
				parsepkcs7(zmp, ip, pf, level);
				break;
#else
				fprintf(stderr, tr(225,
					"No SSL support compiled in.\n"));
				return STOP;
#endif
			}
			/*FALLTHRU*/
		default:
			break;
		case MIME_MULTI:
		case MIME_ALTERNATIVE:
		case MIME_DIGEST:
			_parsemultipart(zmp, ip, pf, level);
			break;
		case MIME_822:
			parse822(zmp, ip, pf, level);
			break;
		}
	}
	return OKAY;
}

static void
newpart(struct mimepart *ip, struct mimepart **np, off_t offs, int *part)
{
	struct mimepart	*pp;
	size_t	sz;

	*np = csalloc(1, sizeof **np);
	(*np)->m_flag = MNOFROM;
	(*np)->m_have = HAVE_HEADER|HAVE_BODY;
	(*np)->m_block = mailx_blockof(offs);
	(*np)->m_offset = mailx_offsetof(offs);
	if (part) {
		(*part)++;
		sz = ip->m_partstring ? strlen(ip->m_partstring) : 0;
		sz += 20;
		(*np)->m_partstring = salloc(sz);
		if (ip->m_partstring)
			snprintf((*np)->m_partstring, sz, "%s.%u",
					ip->m_partstring, *part);
		else
			snprintf((*np)->m_partstring, sz, "%u", *part);
	} else
		(*np)->m_mimecontent = MIME_DISCARD;
	(*np)->m_parent = ip;
	if (ip->m_multipart) {
		for (pp = ip->m_multipart; pp->m_nextpart; pp = pp->m_nextpart);
		pp->m_nextpart = *np;
	} else
		ip->m_multipart = *np;
}

static void
endpart(struct mimepart **np, off_t xoffs, long lines)
{
	off_t	offs;

	offs = mailx_positionof((*np)->m_block, (*np)->m_offset);
	(*np)->m_size = (*np)->m_xsize = xoffs - offs;
	(*np)->m_lines = (*np)->m_xlines = lines;
	*np = NULL;
}

static void
parse822(struct message *zmp, struct mimepart *ip, enum parseflags pf,
		int level)
{
	int	c, lastc = '\n';
	size_t	cnt;
	FILE	*ibuf;
	off_t	offs;
	struct mimepart	*np;
	long	lines;

	if ((ibuf = setinput(&mb, (struct message *)ip, NEED_BODY)) == NULL)
		return;
	cnt = ip->m_size;
	lines = ip->m_lines;
	while (cnt && ((c = getc(ibuf)) != EOF)) {
		cnt--;
		if (c == '\n') {
			lines--;
			if (lastc == '\n')
				break;
		}
		lastc = c;
	}
	offs = ftell(ibuf);
	np = csalloc(1, sizeof *np);
	np->m_flag = MNOFROM;
	np->m_have = HAVE_HEADER|HAVE_BODY;
	np->m_block = mailx_blockof(offs);
	np->m_offset = mailx_offsetof(offs);
	np->m_size = np->m_xsize = cnt;
	np->m_lines = np->m_xlines = lines;
	np->m_partstring = ip->m_partstring;
	np->m_parent = ip;
	ip->m_multipart = np;
	if (value("rfc822-body-from_")) {
		substdate((struct message *)np);
		np->m_from = fakefrom((struct message *)np);
	}
	parsepart(zmp, np, pf, level+1);
}

#ifdef HAVE_SSL
static void
parsepkcs7(struct message *zmp, struct mimepart *ip, enum parseflags pf,
		int level)
{
	struct message	m, *xmp;
	struct mimepart	*np;
	char	*to, *cc;

	memcpy(&m, ip, sizeof m);
	to = hfield1("to", zmp);
	cc = hfield1("cc", zmp);
	if ((xmp = smime_decrypt(&m, to, cc, 0)) != NULL) {
		np = csalloc(1, sizeof *np);
		np->m_flag = xmp->m_flag;
		np->m_have = xmp->m_have;
		np->m_block = xmp->m_block;
		np->m_offset = xmp->m_offset;
		np->m_size = xmp->m_size;
		np->m_xsize = xmp->m_xsize;
		np->m_lines = xmp->m_lines;
		np->m_xlines = xmp->m_xlines;
		np->m_partstring = ip->m_partstring;
		if (parsepart(zmp, np, pf, level+1) == OKAY) {
			np->m_parent = ip;
			ip->m_multipart = np;
		}
	}
}
#endif

/*
 * Get a file for an attachment.
 */
static FILE *
newfile(struct mimepart *ip, int *ispipe)
{
	char *f = ip->m_filename;
	struct str in, out;
	FILE *fp;

	*ispipe = 0;
	if (f != NULL && f != (char *)-1) {
		in.s = f;
		in.l = strlen(f);
		mime_fromhdr(&in, &out, TD_ISPR);
		memcpy(f, out.s, out.l);
		*(f + out.l) = '\0';
		free(out.s);
	}

	if (options & OPT_INTERACTIVE) {
		char *f2, *f3;
jgetname:	(void)printf(tr(278, "Enter filename for part %s (%s)"),
			ip->m_partstring ? ip->m_partstring : "?",
			ip->m_ct_type_plain);
		f2 = readstr_input(": ", f != (char *)-1 ? f : NULL);
		if (f2 == NULL || *f2 == '\0') {
			fprintf(stderr, tr(279, "... skipping this\n"));
			return (NULL);
		} else if (*f2 == '|')
			/* Pipes are expanded by the shell */
			f = f2;
		else if ((f3 = file_expand(f2)) == NULL)
			/* (Error message written by file_expand()) */
			goto jgetname;
		else
			f = f3;
	}
	if (f == NULL || f == (char *)-1)
		return NULL;

	if (*f == '|') {
		char const *cp;
		cp = value("SHELL");
		if (cp == NULL)
			cp = SHELL;
		fp = Popen(f + 1, "w", cp, 1);
		if (! (*ispipe = (fp != NULL)))
			perror(f);
	} else {
		if ((fp = Fopen(f, "w")) == NULL)
			fprintf(stderr, tr(176, "Cannot open %s\n"), f);
	}
	return (fp);
}

static void
pipecpy(FILE *pipebuf, FILE *outbuf, FILE *origobuf, struct quoteflt *qf,
	off_t *stats)
{
	char *line = NULL;
	size_t linesize = 0, linelen, cnt;
	ssize_t all_sz, sz;

	fflush(pipebuf);
	rewind(pipebuf);
	cnt = fsize(pipebuf);
	all_sz = 0;

	quoteflt_reset(qf, outbuf);
	while (fgetline(&line, &linesize, &cnt, &linelen, pipebuf, 0)
			!= NULL) {
		if ((sz = quoteflt_push(qf, line, linelen)) < 0)
			break;
		all_sz += sz;
	}
	if ((sz = quoteflt_flush(qf)) > 0)
		all_sz += sz;
	if (line)
		free(line);

	if (all_sz > 0 && outbuf == origobuf)
		_addstats(stats, 1, all_sz);
	fclose(pipebuf);
}

/*
 * Output a reasonable looking status field.
 */
static void
statusput(const struct message *mp, FILE *obuf, struct quoteflt *qf,
	off_t *stats)
{
	char statout[3];
	char *cp = statout;

	if (mp->m_flag & MREAD)
		*cp++ = 'R';
	if ((mp->m_flag & MNEW) == 0)
		*cp++ = 'O';
	*cp = 0;
	if (statout[0]) {
		int i = fprintf(obuf, "%.*sStatus: %s\n",
			(int)qf->qf_pfix_len,
			(qf->qf_pfix_len > 0) ? qf->qf_pfix : 0,
			statout);
		if (i > 0)
			_addstats(stats, 1, i);
	}
}

static void
xstatusput(const struct message *mp, FILE *obuf, struct quoteflt *qf,
	off_t *stats)
{
	char xstatout[4];
	char *xp = xstatout;

	if (mp->m_flag & MFLAGGED)
		*xp++ = 'F';
	if (mp->m_flag & MANSWERED)
		*xp++ = 'A';
	if (mp->m_flag & MDRAFTED)
		*xp++ = 'T';
	*xp = 0;
	if (xstatout[0]) {
		int i = fprintf(obuf, "%.*sX-Status: %s\n",
			(int)qf->qf_pfix_len,
			(qf->qf_pfix_len > 0) ? qf->qf_pfix : 0,
			xstatout);
		if (i > 0)
			_addstats(stats, 1, i);
	}
}

static void
put_from_(FILE *fp, struct mimepart *ip, off_t *stats)
{
	char const *froma, *date, *nl;
	int i;

	if (ip && ip->m_from) {
		froma = ip->m_from;
		date = fakedate(ip->m_time);
		nl = "\n";
	} else {
		froma = myname;
		date = time_current.tc_ctime;
		nl = "";
	}

	i = fprintf(fp, "From %s %s%s", froma, date, nl);
	if (i > 0)
		_addstats(stats, (*nl != '\0'), i);
}
