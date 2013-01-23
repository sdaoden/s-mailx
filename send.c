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

#include "rcv.h"

#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "extern.h"

/*
 * Mail -- a mail program
 *
 * Mail to mail folders and displays.
 */

enum parseflags {
	PARSE_DEFAULT	= 0,
	PARSE_DECRYPT	= 01,
	PARSE_PARTS	= 02
};

extern void brokpipe(int signo);
static void onpipe(int signo);

static void	_parsemultipart(struct message *zmp, struct mimepart *ip,
			enum parseflags pf, int level);

/* Going for user display, print Part: info string */
static void	_print_part_info(struct str *out, struct mimepart *mip,
			struct ignoretab *doign, int level);

static int sendpart(struct message *zmp, struct mimepart *ip, FILE *obuf,
		struct ignoretab *doign, char *prefix, size_t prefixlen,
		enum sendaction action, off_t *stats, int level);
static struct mimepart *parsemsg(struct message *mp, enum parseflags pf);
static enum okay parsepart(struct message *zmp, struct mimepart *ip,
		enum parseflags pf, int level);
static void newpart(struct mimepart *ip, struct mimepart **np, off_t offs,
		int *part);
static void endpart(struct mimepart **np, off_t xoffs, long lines);
static void parse822(struct message *zmp, struct mimepart *ip,
		enum parseflags pf, int level);
#ifdef USE_SSL
static void parsepkcs7(struct message *zmp, struct mimepart *ip,
		enum parseflags pf, int level);
#endif
static ssize_t	out(char *buf, size_t len, FILE *fp,
			enum conversion convert, enum sendaction action,
			char *prefix, size_t prefixlen, off_t *stats,
			struct str *rest);
static void addstats(off_t *stats, off_t lines, off_t bytes);
static FILE *newfile(struct mimepart *ip, int *ispipe,
		sighandler_type volatile*oldpipe);
static char *getpipecmd(char *content);
static FILE *getpipefile(char *cmd, FILE **qbuf, int quote);
static void pipecpy(FILE *pipebuf, FILE *outbuf, FILE *origobuf,
		char *prefix, size_t prefixlen, off_t *stats);
static void statusput(const struct message *mp, FILE *obuf,
		char *prefix, size_t prefixlen, off_t *stats);
static void xstatusput(const struct message *mp, FILE *obuf,
		char *prefix, size_t prefixlen, off_t *stats);
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
	struct mimepart	*np = NULL;
	char *boundary, *line = NULL;
	size_t linesize = 0, linelen, count, boundlen;
	FILE *ibuf;
	off_t offs;
	int part = 0;
	long lines = 0;

	if ((boundary = mime_get_boundary(ip->m_ct_type, &linelen)) == NULL)
		return;
	boundlen = linelen;
	if ((ibuf = setinput(&mb, (struct message*)ip, NEED_BODY)) == NULL)
		return;
	count = ip->m_size;
	while (foldergets(&line, &linesize, &count, &linelen, ibuf))
		if (line[0] == '\n')
			break;
	offs = ftell(ibuf);
	newpart(ip, &np, offs, NULL);
	while (foldergets(&line, &linesize, &count, &linelen, ibuf)) {
		if ((lines == 0 || part > 0) && (linelen <= boundlen ||
				strncmp(line, boundary, boundlen) != 0)) {
			++lines;
			continue;
		}
		/* Subpart boundary? */
		if (line[boundlen] == '\n') {
			offs = ftell(ibuf);
			if (part != 0) {
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
			/* XXX This is a mysterious message - ??? */
			continue;
		offs = ftell(ibuf);
		if (part != 0) {
			endpart(&np, offs - boundlen - 4, lines);
			newpart(ip, &np, offs - boundlen - 4, NULL);
		}
		endpart(&np, offs + count, 2);
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
		out->l = strlen(out->s) + 1;
		ct.s = ac_alloc(2 + out->l);
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
		ct.s[ct.l] = 0;
	}

	/* Max. 27 */
	if (is_ign("content-disposition", 19, doign) &&
			mip->m_filename != NULL) {
		cd.s = ac_alloc(2 + 25 + 1);
		cd.l = snprintf(cd.s, 2 + 25 + 1, ", %.25s", mip->m_filename);
	}

	/* Take care of "99.99", i.e., 5 */
	if ((ps = mip->m_partstring) == NULL || ps[0] == '\0')
		ps = "?";

#define __msg	"%s[-- #%s : %lu/%lu%s%s --]\n"
	out->l = sizeof(__msg) + strlen(ps) + ct.l + cd.l + 1;
	out->s = salloc(out->l);
	out->l = snprintf(out->s, out->l, __msg,
			(level || (ps[0] != '1' && ps[1] == '\0')) ? "\n" : "",
			ps, (ul_it)mip->m_lines, (ul_it)mip->m_size,
			(ct.s != NULL ? ct.s : ""), (cd.s != NULL ? cd.s : ""));
#undef __msg

	if (cd.s != NULL)
		ac_free(cd.s);
	if (ct.s != NULL)
		ac_free(ct.s);
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
send(struct message *mp, FILE *obuf, struct ignoretab *doign,
		char *prefix, enum sendaction action, off_t *stats)
{
	size_t prefixlen, count, sz, i;
	FILE *ibuf;
	int c;
	enum parseflags pf;
	struct mimepart *ip;

	if (mp == dot && action != SEND_TOSRCH && action != SEND_TOFLTR)
		did_print_dot = 1;
	if (stats)
		stats[0] = stats[1] = 0;
	prefixlen = (prefix != NULL) ? strlen(prefix) : 0;

	/*
	 * First line is the From_ line, so no headers there to worry about.
	 */
	if ((ibuf = setinput(&mb, mp, NEED_BODY)) == NULL)
		return (-1);
	count = mp->m_size;
	sz = 0;
	if (mp->m_flag & MNOFROM) {
		if (doign != allignore && doign != fwdignore &&
				action != SEND_RFC822)
			sz = fprintf(obuf, "%.*sFrom %s %s\n",
					(int)prefixlen, prefixlen ? prefix :"",
					fakefrom(mp), fakedate(mp->m_time));
	} else {
		if (prefixlen && doign != allignore && doign != fwdignore &&
				action != SEND_RFC822) {
			i = fwrite(prefix, sizeof *prefix, prefixlen, obuf);
			if (i != prefixlen)
				return (-1);
			sz += i;
		}
		while (count && (c = getc(ibuf)) != EOF) {
			if (doign != allignore && doign != fwdignore &&
					action != SEND_RFC822) {
				putc(c, obuf);
				sz++;
			}
			count--;
			if (c == '\n')
				break;
		}
	}
	if (sz)
		addstats(stats, 1, sz);
	pf = 0;
	if (action != SEND_MBOX && action != SEND_RFC822 && action != SEND_SHOW)
		pf |= PARSE_DECRYPT|PARSE_PARTS;
	if ((ip = parsemsg(mp, pf)) == NULL)
		return (-1);
	return (sendpart(mp, ip, obuf, doign, prefix, prefixlen, action, stats,
			0));
}

static int
sendpart(struct message *zmp, struct mimepart *ip, FILE *obuf,
		struct ignoretab *doign, char *prefix, size_t prefixlen,
		enum sendaction action, off_t *volatile stats, int level)
{
	struct str rest;
	char *line = NULL, *cp, *cp2, *start, *pipecmd = NULL;
	char const *tcs;
	size_t linesize = 0, linelen, count, len;
	int dostat, infld = 0, ignoring = 1, isenc, c, rt = 0, eof, ispipe = 0;
	struct mimepart	*np;
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
	count = ip->m_size;
	if (ip->m_mimecontent == MIME_DISCARD)
		goto skip;
	if ((ip->m_flag&MNOFROM) == 0)
		while (count && (c = getc(ibuf)) != EOF) {
			count--;
			if (c == '\n')
				break;
		}
	isenc = 0;
	convert = action == SEND_TODISP || action == SEND_TODISP_ALL ||
			action == SEND_QUOTE || action == SEND_QUOTE_ALL ||
			action == SEND_TOSRCH || action == SEND_TOFLTR ?
		CONV_FROMHDR : CONV_NONE;

	/*
	 * Normally headers included in "Content-Type: message/rfc822" messages
	 * will not show up in replies to the encapsulating envelope.
	 * This is nail(1) specific and thus may be configured differently.
	 */
	if (ip->m_mimecontent == MIME_TEXT_PLAIN && ip->m_parent != NULL &&
			ip->m_parent->m_mimecontent == MIME_822 &&
			value("rfc822-show-all"))
		goto skip;

	/* Work the headers */
	while (foldergets(&line, &linesize, &count, &linelen, ibuf)) {
		lineno++;
		if (line[0] == '\n') {
			/*
			 * If line is blank, we've reached end of
			 * headers, so force out status: field
			 * and note that we are no longer in header
			 * fields
			 */
			if (dostat & 1)
				statusput(zmp, obuf, prefix, prefixlen, stats);
			if (dostat & 2)
				xstatusput(zmp, obuf, prefix, prefixlen, stats);
			if (doign != allignore)
				out("\n", 1, obuf, CONV_NONE, SEND_MBOX,
					prefix, prefixlen, stats, NULL);
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
			for (cp = line; (c = *cp&0377) && c != ':' &&
					! spacechar(c); cp++);
			cp2 = cp;
			while (spacechar(*cp))
				cp++;
			if (cp[0] != ':' && level == 0 && lineno == 1) {
				/*
				 * Not a header line, force out status:
				 * This happens in uucp style mail where
				 * there are no headers at all.
				 */
				if (dostat & 1)
					statusput(zmp, obuf, prefix, prefixlen,
						stats);
				if (dostat & 2)
					xstatusput(zmp, obuf, prefix,
						prefixlen, stats);
				if (doign != allignore)
					out("\n", 1, obuf, CONV_NONE, SEND_MBOX,
						prefix, prefixlen, stats, NULL);
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
					 ! value("keep-content-length") &&
					 (asccasecmp(line, "content-length")==0
					 || asccasecmp(line, "lines") == 0)))
				ignoring = 1;
			else if (asccasecmp(line, "status") == 0) {
				 /*
				  * If the field is "status," go compute
				  * and print the real Status: field
				  */
				if (dostat & 1) {
					statusput(zmp, obuf, prefix, prefixlen,
						stats);
					dostat &= ~1;
					ignoring = 1;
				}
			} else if (asccasecmp(line, "x-status") == 0) {
				/*
				 * If the field is "status," go compute
				 * and print the real Status: field
				 */
				if (dostat & 2) {
					xstatusput(zmp, obuf, prefix,
						prefixlen, stats);
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
		if (count && (c = getc(ibuf)) != EOF) {
			if (blankchar(c)) {
				if (linelen > 0 && line[linelen - 1] == '\n')
					cp = &line[linelen - 2];
				else
					cp = &line[linelen - 1];
				while (cp >= line && whitechar(*cp))
					cp++;
				if (cp - line > 8 && cp[0] == '=' &&
						cp[-1] == '?')
					isenc |= 2;
			}
			ungetc(c, ibuf);
		}
		if (! ignoring) {
			start = line;
			len = linelen;
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
					while (len > 0&& blankchar(*start)) {
						start++;
						len--;
					}
				if (isenc & 2)
					if (len > 0 && start[len - 1] == '\n')
						len--;
				while (len > 0 && blankchar(start[len - 1]))
					len--;
			}
			out(start, len, obuf, convert,
				action, prefix, prefixlen, stats, NULL);
			if (ferror(obuf)) {
				free(line);
				return -1;
			}
		}
	}
	free(line);
	line = NULL;

skip:	switch (ip->m_mimecontent) {
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
				if (prefixlen > 0) {
					size_t i = fwrite(prefix,
						sizeof *prefix, prefixlen,
						obuf);
					if (i == prefixlen)
						addstats(stats, 0, i);
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
			pipecmd = getpipecmd(ip->m_ct_type_plain);
			if (MIME_CONTENT_PIPECMD_FORCE_TEXT(pipecmd))
				pipecmd = NULL;
			/*FALLTHRU*/
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
			pipecmd = getpipecmd(ip->m_ct_type_plain);
			if (MIME_CONTENT_PIPECMD_FORCE_TEXT(pipecmd)) {
				pipecmd = NULL;
				goto jcopyout; /* break; break; :*/
			}
			if (pipecmd != NULL)
				break;
			if (level == 0 && count) {
				cp = tr(210, "[Binary content]\n\n");
				out(cp, strlen(cp), obuf, CONV_NONE, SEND_MBOX,
					prefix, prefixlen, stats, NULL);
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
			for (np = ip->m_multipart; np; np = np->m_nextpart) {
				if (np->m_ct_type_plain != NULL && /* XXX */
						action != SEND_QUOTE) {
					_print_part_info(&rest, np, doign,
						level);
					out(rest.s, rest.l, obuf,
						CONV_NONE, SEND_MBOX,
						prefix, prefixlen,
						stats, NULL);
				}
				if (np->m_mimecontent == MIME_TEXT_PLAIN) {
					if (sendpart(zmp, np, obuf,
							doign, prefix,
							prefixlen,
							action, stats,
							level+1) < 0)
						return -1;
				}
			}
			return rt;
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
				cp = "[Missing multipart boundary - "
				     "use \"show\" to display "
				     "the raw message]\n\n";
				out(cp, strlen(cp), obuf, CONV_NONE, SEND_MBOX,
					prefix, prefixlen, stats, NULL);
			}
			for (np = ip->m_multipart; np; np = np->m_nextpart) {
				if (np->m_mimecontent == MIME_DISCARD &&
						action != SEND_DECRYPT)
					continue;
				switch (action) {
				case SEND_TOFILE:
					if (np->m_partstring &&
							strcmp(np->m_partstring,
								"1") == 0)
						break;
					stats = NULL;
					if ((obuf = newfile(np, &ispipe,
								&oldpipe))
							== NULL)
						continue;
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
					out(rest.s, rest.l, obuf,
						CONV_NONE, SEND_MBOX,
						prefix, prefixlen,
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
				if (sendpart(zmp, np, obuf,
						doign, prefix, prefixlen,
						action, stats, level+1) < 0)
					rt = -1;
				else if (action == SEND_QUOTE)
					break;
				if (action == SEND_TOFILE && obuf != origobuf) {
					if (ispipe == 0)
						Fclose(obuf);
					else {
						safe_signal(SIGPIPE, SIG_IGN);
						Pclose(obuf);
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
		count--;
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
	tcs = charset_get_lc();
#ifdef	HAVE_ICONV
	if ((action == SEND_TODISP || action == SEND_TODISP_ALL ||
			action == SEND_QUOTE || action == SEND_QUOTE_ALL ||
			action == SEND_TOSRCH) &&
			(ip->m_mimecontent == MIME_TEXT_PLAIN ||
			 ip->m_mimecontent == MIME_TEXT_HTML ||
			 ip->m_mimecontent == MIME_TEXT)) {
		if (iconvd != (iconv_t)-1)
			iconv_close(iconvd);
		if (asccasecmp(tcs, ip->m_charset) &&
				asccasecmp(charset_get_7bit(), ip->m_charset))
			iconvd = iconv_open_ft(tcs, ip->m_charset);
		else
			iconvd = (iconv_t)-1;
	}
#endif	/* HAVE_ICONV */
	if ((action == SEND_TODISP || action == SEND_TODISP_ALL ||
			action == SEND_QUOTE || action == SEND_QUOTE_ALL) &&
			pipecmd != NULL) {
		qbuf = obuf;
		pbuf = getpipefile(pipecmd, (FILE**)&qbuf,
			action == SEND_QUOTE || action == SEND_QUOTE_ALL);
		action = SEND_TOPIPE;
		if (pbuf != qbuf) {
			oldpipe = safe_signal(SIGPIPE, onpipe);
			if (sigsetjmp(pipejmp, 1))
				goto end;
		}
	} else
		pbuf = qbuf = obuf;

	eof = 0;
	rest.s = NULL;
	rest.l = 0;
	while (! eof && foldergets(&line, &linesize, &count, &linelen, ibuf)) {
		++lineno;

		while (convert == CONV_FROMQP && linelen >= 2 &&
				line[linelen-2] == '=') {
			char	*line2;
			size_t	linesize2, linelen2;
			line2 = NULL;
			linesize2 = 0;
			if (foldergets(&line2, &linesize2, &count, &linelen2,
						ibuf) == NULL) {
				if (line2 != NULL)
					free(line2);
				eof = 1;
				break;
			}
			if (linelen + linelen2 + 1 > linesize)
				line = srealloc(line, linesize = linelen +
						linelen2 + 1);
			memcpy(&line[linelen], line2, linelen2+1);
			linelen += linelen2;
			free(line2);
		}
joutln:
		len = (size_t)out(line, linelen, pbuf, convert, action,
				pbuf == origobuf ? prefix : NULL,
				pbuf == origobuf ? prefixlen : 0,
				pbuf == origobuf ? stats : NULL, &rest);
		if ((ssize_t)len < 0 || (len == 0 && rest.l == 0) ||
				ferror(pbuf)) {
			rt = -1;
			break;
		}
	}
	if (rest.l != 0) {
		linelen = 0;
		eof = 1;
		goto joutln;
	}
	if (rest.s != NULL)
		free(rest.s);

end:	free(line);
	if (pbuf != qbuf) {
		safe_signal(SIGPIPE, SIG_IGN);
		Pclose(pbuf);
		safe_signal(SIGPIPE, oldpipe);
		if (qbuf != obuf)
			pipecpy(qbuf, obuf, origobuf, prefix, prefixlen, stats);
	}
#ifdef	HAVE_ICONV
	if (iconvd != (iconv_t)-1) {
		iconv_close(iconvd);
		iconvd = (iconv_t)-1;
	}
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
		ip->m_ct_type_plain = "message/rfc822";
	else
		ip->m_ct_type_plain = "text/plain";

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
#ifdef USE_SSL
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
	size_t	count;
	FILE	*ibuf;
	off_t	offs;
	struct mimepart	*np;
	long	lines;

	if ((ibuf = setinput(&mb, (struct message *)ip, NEED_BODY)) == NULL)
		return;
	count = ip->m_size;
	lines = ip->m_lines;
	while (count && ((c = getc(ibuf)) != EOF)) {
		count--;
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
	np->m_size = np->m_xsize = count;
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

#ifdef USE_SSL
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

static ssize_t
out(char *buf, size_t len, FILE *fp,
	enum conversion convert, enum sendaction action,
	char *prefix, size_t prefixlen, off_t *stats, struct str *rest)
{
	ssize_t sz = 0, n;
	char *cp;

	if (action != SEND_MBOX && action != SEND_DECRYPT)
		goto jmw;

	cp = buf;
	n = len;
	while (n && cp[0] == '>')
		++cp, --n;
	/* Primitive, rather POSIX-compliant From_ quoting? */
	if (value("posix-mbox")) {
		if (n >= 5 && cp[0] == 'F' && cp[1] == 'r' && cp[2] == 'o' &&
				cp[3] == 'm' && cp[4] == ' ')
			goto jquote;
	}
	/* We however *have* to perform RFC 4155 compliant From_ quoting, or
	 * we end up like Mutt 1.5.21 (2010-09-15) */
	else if (cp != buf && is_head(cp, n)) { /* TODO recheck: cp==buf only */
jquote:		putc('>', fp);
		++sz;
	}

jmw:	n = mime_write(buf, len, fp,
			action == SEND_MBOX ? CONV_NONE : convert,
			action == SEND_TODISP || action == SEND_TODISP_ALL ||
					action == SEND_QUOTE ||
					action == SEND_QUOTE_ALL ?
				TD_ISPR|TD_ICONV :
				action == SEND_TOSRCH || action == SEND_TOPIPE ?
					TD_ICONV :
				action == SEND_TOFLTR ?
					TD_DELCTRL :
				action == SEND_SHOW ?
					TD_ISPR : TD_NONE,
			prefix, prefixlen, rest);
	if (n < 0)
		sz = n;
	else if (n > 0) {
		sz += n;
		n = 0;
		if (stats && stats[0] != -1) {
			for (cp = buf; cp < &buf[sz]; cp++)
				if (*cp == '\n')
					++n;
		}
		addstats(stats, n, sz);
	}
	return sz;
}

static void
addstats(off_t *stats, off_t lines, off_t bytes)
{
	if (stats) {
		if (stats[0] >= 0)
			stats[0] += lines;
		stats[1] += bytes;
	}
}

/*
 * Get a file for an attachment.
 */
static FILE *
newfile(struct mimepart *ip, int *ispipe, sighandler_type volatile*oldpipe)
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

	if (value("interactive") != NULL) {
		char *f2, *f3;
jgetname:	(void)printf(tr(278, "Enter filename for part %s (%s)"),
			ip->m_partstring ? ip->m_partstring : "?",
			ip->m_ct_type_plain);
		f2 = readtty(": ", f != (char *)-1 ? f : NULL);
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
		char *cp;
		cp = value("SHELL");
		if (cp == NULL)
			cp = SHELL;
		fp = Popen(f + 1, "w", cp, 1);
		if (fp == NULL) {
			perror(f);
			fp = stdout;
		} else {
			*oldpipe = safe_signal(SIGPIPE, brokpipe);
			*ispipe = 1;
		}
	} else {
		if ((fp = Fopen(f, "w")) == NULL)
			fprintf(stderr, tr(176, "Cannot open %s\n"), f);
	}
	return (fp);
}

static char *
getpipecmd(char *content)
{
	char	*penv, *cp, *cq, *pipecmd;

	if (content == NULL)
		return NULL;
	penv = ac_alloc(strlen(content) + 6);
	strcpy(penv, "pipe-");
	cp = &penv[5];
	cq = content;
	do
		*cp++ = lowerconv(*cq & 0377);
	while (*cq++);
	pipecmd = value(penv);
	ac_free(penv);
	return pipecmd;
}

static FILE *
getpipefile(char *pipecmd, FILE **qbuf, int quote)
{
	char	*shell;
	FILE	*rbuf = *qbuf;

	if (pipecmd != NULL) {
		if (quote) {
			char *tempPipe;

			if ((*qbuf = Ftemp(&tempPipe, "Rp", "w+", 0600, 1))
					== NULL) {
				perror(catgets(catd, CATSET, 173, "tmpfile"));
				*qbuf = rbuf;
			}
			unlink(tempPipe);
			Ftfree(&tempPipe);
		}
		if ((shell = value("SHELL")) == NULL)
			shell = SHELL;
		if ((rbuf = Popen(pipecmd, "W", shell, fileno(*qbuf)))
				== NULL) {
			perror(pipecmd);
		} else {
			fflush(*qbuf);
			if (*qbuf != stdout)
				fflush(stdout);
		}
	}
	return rbuf;
}

static void
pipecpy(FILE *pipebuf, FILE *outbuf, FILE *origobuf,
		char *prefix, size_t prefixlen, off_t *stats)
{
	char *line = NULL;
	size_t linesize = 0, linelen, sz, count;

	fflush(pipebuf);
	rewind(pipebuf);
	count = fsize(pipebuf);
	while (fgetline(&line, &linesize, &count, &linelen, pipebuf, 0)
			!= NULL) {
		sz = prefixwrite(line, sizeof *line, linelen, outbuf,
			prefix, prefixlen);
		if (outbuf == origobuf)
			addstats(stats, 1, sz);
	}
	if (line)
		free(line);
	fclose(pipebuf);
}

/*
 * Output a reasonable looking status field.
 */
static void
statusput(const struct message *mp, FILE *obuf, char *prefix, size_t prefixlen,
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
			(int)prefixlen, (prefixlen ? prefix : ""), statout);
		if (i > 0)
			addstats(stats, 1, i);
	}
}

static void
xstatusput(const struct message *mp, FILE *obuf, char *prefix,
	size_t prefixlen, off_t *stats)
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
			(int)prefixlen, (prefixlen ? prefix : ""), xstatout);
		if (i > 0)
			addstats(stats, 1, i);
	}
}

static void
put_from_(FILE *fp, struct mimepart *ip, off_t *stats)
{
	time_t now;
	char const *from, *date, *nl;
	int i;

	if (ip && ip->m_from) {
		from = ip->m_from;
		date = fakedate(ip->m_time);
		nl = "\n";
	} else {
		time(&now);
		from = myname;
		date = ctime(&now);
		nl = "";
	}

	i = fprintf(fp, "From %s %s%s", from, date, nl);
	if (i > 0)
		addstats(stats, (*nl != '\0'), i);
}

/*
 * This is fgetline for mbox lines.
 */
char *
foldergets(char **s, size_t *size, size_t *count, size_t *llen, FILE *stream)
{
	char *p;

	if ((p = fgetline(s, size, count, llen, stream, 0)) == NULL)
		return (NULL);
	if (*p != '>')
		goto jleave;

	while (*++p == '>')
		;
	if (value("posix-mbox")) {
		if (strncmp(p, "From ", 5) != 0)
			goto jleave;
	}
	/* Since we actually *have* to perform RFC 4155 compliant From_ quoting
	 * we should of course undo that */
	else if (p == *s || ! is_head(p, *llen - (p - *s)))
		goto jleave;

	/* We got a masked From line */
	memmove(*s, *s + 1, --*llen);
jleave:
	return (*s);
}
