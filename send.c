/*
 * Nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2002 Gunnar Ritter, Freiburg i. Br., Germany.
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

#ifndef lint
#ifdef	DOSCCS
static char sccsid[] = "@(#)send.c	2.6 (gritter) 9/15/02";
#endif
#endif /* not lint */

#include "rcv.h"
#include "extern.h"

#ifdef	HAVE_STRINGS_H
#include <strings.h>
#endif

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
 * Code around struct sendmsg and struct hdrline was derived from
 * a contribution by Stan Tobias, August 2002.
 */

struct sendmsg {
	FILE *sm_ibuf;			/* input buffer */
	char *sm_line;			/* line buffer for input */
	size_t sm_count;		/* character count */
	size_t sm_linesize;		/* buffer length */
	size_t sm_llen;			/* == strlen(sm_line) */
	int sm_unread;			/* like ungetc() but for lines */
	int sm_unmask_from;		/* remove >From_ */
};

struct hdrline {
	struct hdrline *hd_next;	/* next header line in list */
	char *hd_line;			/* buffer containing the line */
	size_t hd_llen;			/* line length */
};

enum {
	HDR_OKAY = 0,
	HDR_NOHDR = 1	/* first line was not a header line;
			   unread_sendmsg called */
};

static void	addstats __P((off_t *, off_t, off_t));
static void	print_partnumber __P((FILE *, struct boundary *,
			struct boundary *, off_t *));
static char	*newfilename __P((char *, struct boundary *,
			struct boundary *, char *));
static void	statusput __P((const struct message *, FILE *,
			char *, off_t *));
static struct boundary	*get_top_boundary __P((struct boundary *));
static struct boundary	*bound_alloc __P((struct boundary *));
static void	bound_free __P((struct boundary *));
static FILE	*getpipetype __P((char *, FILE **, int));
static void	pipecpy __P((FILE *, FILE *, FILE *, char *, size_t, off_t *));
static void	put_from_ __P((FILE *));
static struct sendmsg	*open_sendmsg __P((FILE *, size_t, int));
static void	close_sendmsg __P((struct sendmsg *));
static char	*read_sendmsg __P((struct sendmsg *, char **, size_t *,
			size_t *, long *, int));
static void	unread_sendmsg __P((struct sendmsg *, long *));
static void	del_hdr __P((struct hdrline *));
static int	read_hdr __P((struct hdrline **ph, struct sendmsg *pm,
			int allowempty));
static int	write_hdr __P((const struct hdrline *ph, FILE *obuf, int action,
			struct ignoretab *doign, char *prefix, int prefixlen,
			int rewritestatus, const struct message *mp,
			off_t *stats));
static void	addline_hdr __P((struct hdrline **, char *, size_t));
static const struct hdrline	*start_of_field_of_hdr __P((const struct
			hdrline *, const char *));
static char	*unfold_hdr __P((const struct hdrline *, const char *));
static char	*field_of_hdr __P((const struct hdrline *, char *));
static char	*param_of_hdr __P((const struct hdrline *, char *, char *));
static void	onpipe __P((int));
static void	exchange __P((char **, size_t *, size_t *,
			char **, size_t *, size_t *));
static int	send_multipart __P((struct sendmsg *, FILE *,
			struct ignoretab *, char *, int, int, int,
			struct boundary *, off_t *));


static void
addstats(stats, lines, bytes)
off_t *stats, lines, bytes;
{
	if (stats) {
		if (stats[0] >= 0)
			stats[0] += lines;
		stats[1] += bytes;
	}
}

/*
 * Print the part number indicated by b0.
 */
static void
print_partnumber(obuf, b, b0, stats)
FILE *obuf;
struct boundary *b, *b0;
off_t *stats;
{
	struct boundary *bc;
	char buf[20];
	size_t sz;

	for (bc = b0; ; bc = bc->b_nlink) {
		if (bc != b0) {
			sputc('.', obuf);
			if (stats)
				stats[1]++;
		}
		snprintf(buf, sizeof buf, "%d", bc->b_count);
		sz = strlen(buf);
		fwrite(buf, 1, sz, obuf);
		if (stats)
			stats[1] += sz;
		if (bc == b)
			break;
	}
}

/*
 * Get a filename based on f.
 */
static char *
newfilename(f, b, b0, content_type)
char *f, *content_type;
struct boundary *b, *b0;
{
	struct str in, out;

	if (f != NULL && f != (char *)-1) {
		in.s = f;
		in.l = strlen(f);
		mime_fromhdr(&in, &out, TD_ISPR);
		memcpy(f, out.s, out.l);
		*(f + out.l) = '\0';
		free(out.s);
	}
	if (value("interactive") != NULL) {
		fputs(catgets(catd, CATSET, 171,
					"Enter filename for part "), stdout);
		print_partnumber(stdout, b, b0, NULL);
		if (content_type)
			printf(" (%s)", content_type);
		f = readtty(catgets(catd, CATSET, 173, ": "),
				f != (char *)-1 ? f : NULL);
	}
	if (f == NULL || f == (char *)-1) {
		f = (char *)smalloc(10);
		strcpy(f, "/dev/null");
	}
	return f;
}

/*
 * This is fgetline for mbox lines.
 */
char *
foldergets(s, size, count, llen, stream)
char **s;
size_t *size, *count, *llen;
FILE *stream;
{
	char *p, *top;

	if ((p = fgetline(s, size, count, llen, stream, 0)) == NULL)
		return NULL;
	if (*p == '>') {
		p++;
		while (*p == '>') p++;
		if (strncmp(p, "From ", 5) == 0) {
			/* we got a masked From line */
			top = &(*s)[*llen];
			p = *s;
			do
				p[0] = p[1];
			while (++p < top);
		}
		(*llen)--;
	}
	return *s;
}

/*
 * Output a reasonable looking status field.
 */
static void
statusput(mp, obuf, prefix, stats)
	const struct message *mp;
	FILE *obuf;
	char *prefix;
	off_t *stats;
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
			prefix == NULL ? "" : prefix, statout);
	addstats(stats, 1, (prefix ? strlen(prefix) : 0) + 9 + cp - statout);
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

	b = (struct boundary *)smalloc(sizeof *b);
	b->b_str = NULL;
	b->b_count = 0;
	b->b_nlink = (struct boundary *)NULL;
	b->b_flink = bprev;
	bprev->b_nlink = b;
	return b;
}

/*
 * Delete all sub-boundaries below the given boundary.
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

static FILE *
getpipetype(content, qbuf, quote)
char *content;
FILE **qbuf;
{
	char *penv, *pipecmd, *shell, *cp, *cq;
	FILE *rbuf = *qbuf;

	if (content == NULL)
		return *qbuf;
	penv = ac_alloc(strlen(content) + 6);
	strcpy(penv, "pipe-");
	cp = &penv[5];
	cq = content;
	do
		*cp++ = lowerconv(*cq & 0377);
	while (*cq++);
	if ((pipecmd = value(penv)) != NULL) {
		if (quote) {
			char *tempPipe;

			if ((*qbuf = Ftemp(&tempPipe, "Rp", "w+", 0600, 1))
					== (FILE *)NULL) {
				perror(catgets(catd, CATSET, 173, "tmpfile"));
				*qbuf = rbuf;
			}
			unlink(tempPipe);
			Ftfree(&tempPipe);
		}
		if ((shell = value("SHELL")) == NULL)
			shell = PATH_CSHELL;
		if ((rbuf = Popen(pipecmd, "W", shell, fileno(*qbuf)))
				== NULL) {
			perror(pipecmd);
		} else {
			fflush(*qbuf);
			if (*qbuf != stdout)
				fflush(stdout);
		}
	}
	ac_free(penv);
	return rbuf;
}

static void
pipecpy(pipebuf, outbuf, origobuf, prefix, prefixlen, stats)
FILE *pipebuf, *outbuf, *origobuf;
char *prefix;
size_t prefixlen;
off_t *stats;
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

static void
put_from_(FILE *fp)
{
	time_t now;

	time(&now);
	fprintf(fp, "From %s %s", myname, ctime(&now));
}

static struct sendmsg *
open_sendmsg(ibuf, count, unmask_from)
FILE *ibuf;
size_t count;
{
	struct sendmsg *pm;

	pm = smalloc(sizeof *pm);
	pm->sm_ibuf = ibuf;
	pm->sm_unread = 0;
	pm->sm_unmask_from = unmask_from;
	pm->sm_count = count;
	pm->sm_llen = 0;
	pm->sm_linesize = LINESIZE;
	pm->sm_line = smalloc(pm->sm_linesize);
	return pm;
}

static void
close_sendmsg(pm)
struct sendmsg *pm;
{
	free(pm->sm_line);
	free(pm);
}

static char *
read_sendmsg(pm, s, size, ex_llen, ex_count, appendnl)
	struct sendmsg *pm;
	char **s;
	size_t *size;
	size_t *ex_llen;
	long *ex_count;
{
	size_t in_llen;
	char *stripped;

	/*
	 * pm->sm_count is kept in synch with file stream, whereas
	 * *ex_count is kept together with sendmsg stream (when a
	 * line is unread, *ex_count is put back, i. e. augmented).
	 */
	if (pm->sm_unread) {
		pm->sm_unread = 0;
		if (appendnl && pm->sm_line[pm->sm_llen - 1] != '\n') {
			newline_appended();
			if (pm->sm_llen + 2 > pm->sm_linesize)
				pm->sm_line = srealloc(pm->sm_line,
					pm->sm_linesize = pm->sm_llen + 2);
			pm->sm_line[pm->sm_llen++] = '\n';
			pm->sm_line[pm->sm_llen] = '\0';
		}
	} else {
		if (fgetline(&pm->sm_line, &pm->sm_linesize, &pm->sm_count,
				&pm->sm_llen, pm->sm_ibuf, appendnl) == NULL)
			/*
			 * XXX Mark pm somehow?
			 */
			return NULL;
	}
	/*
	 * Copy the line to the passed buffer, possibly stripping
	 * the >From_.
	 */
	if (pm->sm_unmask_from) {
		char *cp;

		for (cp = pm->sm_line; *cp == '>'; cp++);
		if (cp > pm->sm_line && strncmp(cp, "From ", 5) == 0)
			stripped = &pm->sm_line[1];
		else
			stripped = pm->sm_line;
	} else
		stripped = pm->sm_line;
	in_llen = pm->sm_llen - (stripped - pm->sm_line);
	if (s == NULL || *size < in_llen + 1)
		*s = srealloc(*s, *size = in_llen + 1);
	memcpy(*s, stripped, in_llen);
	(*s)[in_llen] = '\0';
	if (ex_llen)
		*ex_llen = in_llen;
	if (ex_count)
		*ex_count -= pm->sm_llen;
	return *s;
}

static void
unread_sendmsg(pm, ex_count)
struct sendmsg *pm;
long *ex_count;
{
	pm->sm_unread = 1;
	if (ex_count)
		*ex_count += pm->sm_llen;
}

static void
del_hdr(ph)
struct hdrline *ph;
{
	struct hdrline *pn;

	if (ph != NULL) {
		for ( ; ph; ph = pn) {
			pn = ph->hd_next;
			if (ph->hd_line)
				free(ph->hd_line);
			free(ph);
		}
	}
}

static void
addline_hdr(p0, line, linelen)
struct hdrline **p0;
char *line;
size_t linelen;
{
	struct hdrline *ph, *pp;

	ph = smalloc(sizeof *ph);
	ph->hd_line = smalloc(linelen + 1);
	memcpy(ph->hd_line, line, linelen);
	ph->hd_line[linelen] = '\0';
	ph->hd_llen = linelen;
	ph->hd_next = NULL;
	if (*p0) {
		for (pp = *p0; pp->hd_next; pp = pp->hd_next);
		pp->hd_next = ph;
	} else
		*p0 = ph;
}

static int
read_hdr(ph, pm, allowempty)
struct hdrline **ph;
struct sendmsg *pm;
{
	char *line = NULL, *cp;
	size_t lineno = 0, linesize = 0, linelen;

	while (read_sendmsg(pm, &line, &linesize, &linelen, NULL, 1) != NULL) {
		if (++lineno == 1) {
			for (cp = line; fieldnamechar(*cp & 0377); cp++);
			if (cp > line)
				while (blankchar(*cp & 0377))
					cp++;
			if (cp != line && *cp == ':')
				/*
				 * Plausible header line.
				 */
				/*EMPTY*/;
			else if (*line == '\n' && allowempty)
				/*
				 * Header lines may be empty e. g. in
				 * multipart headers.
				 */
				/*EMPTY*/;
			else {
				unread_sendmsg(pm, NULL);
				if (line)
					free(line);
				return HDR_NOHDR;
			}
		}
		addline_hdr(ph, line, linelen);
		if (*line == '\n')
			break;
	}
	if (line)
		free(line);
	return HDR_OKAY;
}

static int
write_hdr(ph, obuf, action, doign, prefix, prefixlen, rewritestatus, mp, stats)
const struct hdrline *ph;
FILE *obuf;
int action;
struct ignoretab *doign;
char *prefix;
int prefixlen;
int rewritestatus;	/* == 1 in main header only */
const struct message *mp;	/* can be NULL if rewritestatus == 0 */
off_t *stats;
{
	int ignoring = 0;
	size_t sz;

	if (doign == allignore)
		return 0;
	if (ph == NULL && rewritestatus) {
		/*
		 * uucp mail has no headers. This may ocurr in the main
		 * header only. This extra condition is for malformed
		 * "message/rfc822" contents, where the first line is
		 * not a header line at all -- then we should not
		 * "correct" it.
		 */
		if (rewritestatus && !is_ign("status", 6, doign))
			statusput(mp, obuf, prefix, stats);
		/*
		 * Add blank line.
		 */
		sz = mime_write("\n", 1, 1, obuf,
				action == CONV_TODISP || action == CONV_QUOTE ?
					CONV_FROMHDR : CONV_NONE,
				action == CONV_TODISP ?
					TD_ISPR|TD_ICONV : TD_NONE,
				prefix, prefixlen);
		addstats(stats, 1, sz);
	}
	for ( ; ph; ph = ph->hd_next) {
		switch (*ph->hd_line) {
			char *fieldname, *cp;

		case '\n':
			if (rewritestatus && !is_ign("status", 6, doign))
				statusput(mp, obuf, prefix, stats);
			break;
		case ' ':
		case '\t':
			if (ignoring)
				continue;
			break;
		default:
			/*
			 * Extract the field name and check it. XXX This
			 * might be done in in separate function.
			 */
			fieldname = sstrdup(ph->hd_line);
			cp = strchr(fieldname, ':');
			if (*cp == '\0') {
				/*
				 * Bad header.
				 */
				ignoring = 0;
				break;
			}
			cp--;
			while (blankchar(*cp & 0377))
				cp--;
			cp[1] = '\0';
			ignoring = is_ign(fieldname, &cp[1] - fieldname, doign);
			if (rewritestatus && !ignoring &&
					asccasecmp(fieldname, "status") == 0) {
				free(fieldname);
				statusput(mp, obuf, prefix, stats);
				rewritestatus = 0;	/* note */
				continue;
			}
			free(fieldname);
			if (ignoring)
				continue;
		}
		sz = mime_write(ph->hd_line, sizeof *ph->hd_line,
				ph->hd_llen, obuf,
				action == CONV_TODISP || action == CONV_QUOTE ?
					CONV_FROMHDR : CONV_NONE,
				action == CONV_TODISP ?
					TD_ISPR|TD_ICONV : TD_NONE,
				prefix, prefixlen);
		if (ferror(obuf))
			return 1;
		addstats(stats, 1, sz);
	}
	return 0;
}

static const struct hdrline *
start_of_field_of_hdr(ph, field)
const struct hdrline *ph;
const char *field;
{
	char *cp;
	size_t sz = strlen(field);

	for ( ; ph; ph = ph->hd_next) {
		cp = ph->hd_line;
		if (ascncasecmp(cp, field, sz) != 0)
			continue;
		cp += sz;
		for ( ; blankchar(*cp & 0377); cp++);
		if (*cp == ':')
			break;
	}
	return ph;
}

static char *
unfold_hdr(ph, field)
const struct hdrline *ph;
const char *field;
{
	const struct hdrline *pp, *pq;
	size_t totlen = 0;
	char *ret, *cp;

	if ((pp = start_of_field_of_hdr(ph, field)) == NULL)
		return NULL;
	for (pq = pp; pq == pp || (pq && blankchar(*pq->hd_line & 0377));
			pq = pq->hd_next)
		totlen += pq->hd_llen;
	ret = salloc(totlen + 1);
	cp = sstpcpy(ret, "\n");
	for (pq = pp; pq == pp || (pq && blankchar(*pq->hd_line & 0377));
			pq = pq->hd_next) {
		/*
		 * XXX remove initial LWSP for quoted-strings
		 * (RFC 822, 3.4.5)
		 */
		cp = sstpcpy(&cp[-1], pq->hd_line);
	}
	return ret;
}

static char *
field_of_hdr(ph, field)
const struct hdrline *ph;
char *field;
{
	const char *unfolded_field;
	char *unfolded_fake, *cp;

	if ((unfolded_field = unfold_hdr(ph, field)) == NULL)
		return NULL;
	/* XXX */
	unfolded_fake = salloc(strlen(unfolded_field) + 2);
	*unfolded_fake = ';';
	strcpy(&unfolded_fake[1], unfolded_field);
	cp = strchr(unfolded_fake, ':');
	*cp = '=';
	return mime_getparam(field, unfolded_fake);
}

static char *
param_of_hdr(ph, field, param)
const struct hdrline *ph;
char *field;
char *param;
{
	char *unfolded_field;

	if ((unfolded_field = unfold_hdr(ph, field)) == NULL)
		return NULL;
	return mime_getparam(param, unfolded_field);
}

static sigjmp_buf	pipejmp;

/*ARGSUSED*/
static RETSIGTYPE
onpipe(signo)
int signo;
{
	siglongjmp(pipejmp, 1);
}

static void
exchange(line1, linesize1, linelen1, line2, linesize2, linelen2)
char **line1, **line2;
size_t *linesize1, *linelen1, *linesize2, *linelen2;
{
	char *tmp_line;
	size_t tmp_size, tmp_len;

	tmp_line = *line1, tmp_size = *linesize1, tmp_len = *linelen1;
	*line1 = *line2, *linesize1 = *linesize2, *linelen1 = *linelen2;
	*line2 = tmp_line, *linesize2 = tmp_size, *linelen2 = tmp_len;
}

/*
 * Send the body of a MIME multipart message.
 */
static int
send_multipart(pm, obuf, doign, prefix, prefixlen,
		convert, action, b0, stats)
struct sendmsg *pm;
FILE *obuf;
struct ignoretab *doign;
char *prefix;
struct boundary *b0;
off_t *stats;
{
	int mime_content = MIME_TEXT, new_content = MIME_TEXT;
	FILE *oldobuf = (FILE *)-1, *origobuf = (FILE *)-1, *pbuf = obuf,
		*qbuf = obuf;
	char *line = NULL, *oline = NULL;
	char *filename;
	char *scontent;
	int part = 0, error_return = 0;
	char *cs = us_ascii, *tcs;
	struct boundary *b = b0;
	int lineno = -1, jump_to_bound = 0;
	size_t sz, linesize = 0, linelen, olinesize = 0, olinelen;

	tcs = gettcharset();
	if (b0->b_str == NULL) {
		error_return = 1;
		goto send_multi_end;
	}
	b0->b_count = 0;
	b0->b_len = strlen(b0->b_str);
	mime_content = MIME_DISCARD;
	origobuf = obuf;
	while (read_sendmsg(pm, &line, &linesize, &linelen, NULL, 0) != NULL) {
		if (line[0] == '-' && line[1] == '-') {
			/*
			 * This line could be a multipart boundary.
			 */
			scontent = NULL;
			for (b = b0; b != NULL; b = b->b_nlink)
				if (strncmp(line, b->b_str, b->b_len) == 0)
					break;
			if (b != NULL) {
				char *boundend = &line[b->b_len];

				if (boundend[0] == '-' && boundend[1] == '-') {
					boundend += 2;
					while (blankchar(*boundend & 0377))
						boundend++;
					if (*boundend != '\n')
						goto send_multi_nobound;
					/*
					 * Found a terminating boundary.
					 */
					bound_free(b);
					/*
					 * The last line of the part has not
					 * yet been written.
					 */
					jump_to_bound = 1;
					if (--olinelen > 0)
						goto send_multi_nobound;
send_multi_lastbound:
					jump_to_bound = 0;
					if (b == b0)
						break;
					if (b != NULL) {
						b = b->b_flink;
						bound_free(b);
					}
					mime_content = MIME_DISCARD;
					scontent = NULL;
				} else {
					while (blankchar(*boundend & 0377))
						boundend++;
					if (*boundend != '\n')
						goto send_multi_nobound;
					/*
					 * Found an intermediate boundary.
					 */
					bound_free(b);
					jump_to_bound = 2;
					if (--olinelen > 0)
						goto send_multi_nobound;
send_multi_midbound:
					jump_to_bound = 0;
					if (pbuf != qbuf) {
						safe_signal(SIGPIPE, SIG_IGN);
						Pclose(pbuf);
						safe_signal(SIGPIPE, SIG_DFL);
						if (qbuf != obuf)
							pipecpy(qbuf, obuf,
								origobuf,
								prefix,
								prefixlen,
								stats);
						pbuf = qbuf = obuf;
					}
					mime_content = MIME_SUBHDR;
					b->b_count++;
					if (action == CONV_QUOTE) {
						if (b->b_count > 1)
							goto send_multi_end;
					} else if (action != CONV_TOFILE) {
						fputs(catgets(catd, CATSET, 174,
							"\nPart "), obuf);
						print_partnumber(obuf, b, b0,
							obuf == origobuf ?
							stats : NULL);
						fputs(catgets(catd, CATSET, 175,
							":\n"), obuf);
						if (obuf == origobuf)
							addstats(stats, 2, 8);
					}
					new_content = MIME_TEXT;
					cs = us_ascii;
					convert = CONV_NONE;
#ifdef	HAVE_ICONV
					if (iconvd != (iconv_t)-1) {
						iconv_close(iconvd);
						iconvd = (iconv_t)-1;
					}
#endif
				}
				part++;
				if (oldobuf != (FILE *)-1 && obuf != origobuf) {
					Fclose(obuf);
					pbuf = qbuf = obuf = oldobuf;
					oldobuf = (FILE *)-1;
				}
				continue;
			}
		}
send_multi_nobound:
		switch (mime_content) {
			struct hdrline *ph;
			char *unfolded_ct, *unfolded_cte;

		case MIME_SUBHDR:
			/*
			 * First line is read within the loop. XXX Should
			 * some variables be zeroed?
			 */
			unread_sendmsg(pm, NULL);
send_multi_parseheader:
			ph = NULL;
			read_hdr(&ph, pm, 1);
			write_hdr(ph, obuf, action, doign, prefix, prefixlen, 0,
					NULL, obuf == origobuf ? stats : NULL);
			unfolded_ct = unfold_hdr(ph, "content-type");
			if ((scontent = field_of_hdr(ph, "content-type")) !=
					NULL)
				new_content = mime_getcontent(unfolded_ct);
			if (new_content == MIME_MULTI) {
				b = bound_alloc(get_top_boundary(b0));
				b->b_str = mime_getboundary(unfolded_ct);
			}
			if ((cs = param_of_hdr(ph, "content-type", "charset"))
					== NULL)
				cs = us_ascii;
			if ((unfolded_cte = unfold_hdr(ph,
					"content-transfer-encoding")) != NULL) {
				switch (mime_getenc(unfolded_cte)) {
				case MIME_BIN:
				case MIME_7B:
				case MIME_8B:
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
					new_content = MIME_UNKNOWN;
				}
			}
			filename = param_of_hdr(ph, "content-disposition",
					"filename");
			if (filename == NULL && scontent && asccasecmp(scontent,
					"message/external-body") != 0)
				filename = param_of_hdr(ph, "content-type",
						"name");
			del_hdr(ph);
			if (new_content == MIME_822 && (action == CONV_TODISP ||
						action == CONV_QUOTE)) {
				new_content = MIME_TEXT;
				/*
				 * See comment in send_message().
				 */
				goto send_multi_parseheader;
			}
			lineno = -1;
			if ((mime_content = new_content) == MIME_MULTI) {
				if (b->b_str == NULL) {
					error_return = 1;
					goto send_multi_end;
				}
				b->b_len = strlen(b->b_str);
			} else if (mime_content == MIME_TEXT) {
				if (cs == NULL)
					cs = us_ascii;
#ifdef	HAVE_ICONV
				if (iconvd != (iconv_t)-1)
					iconv_close(iconvd);
				if (action == CONV_TODISP
					|| action == CONV_QUOTE)
				if (iconvd != (iconv_t)-1)
					iconv_close(iconvd);
				if (asccasecmp(tcs, cs)
					&& asccasecmp(us_ascii, cs))
					iconvd = iconv_open_ft(tcs, cs);
				else
					iconvd = (iconv_t)-1;
#endif
			if (convert == CONV_FROMB64)
				convert = CONV_FROMB64_T;
			}
			if (action == CONV_TODISP || action == CONV_QUOTE) {
				qbuf = obuf;
				pbuf = getpipetype(scontent, &qbuf,
						action == CONV_QUOTE);
				if (pbuf != qbuf) {
					safe_signal(SIGPIPE, onpipe);
					if (sigsetjmp(pipejmp, 1))
						mime_content = MIME_DISCARD;
				}
			} else
				pbuf = qbuf = obuf;
			if (action == CONV_TOFILE && part != 1 &&
					mime_content != MIME_MULTI) {
				if (filename && strchr(filename, '/'))
					filename = strrchr(filename, '/') + 1;
				if (filename != NULL && *filename == '\0')
					filename = NULL;
				if ((filename = newfilename(filename, b, b0,
						scontent)) != NULL) {
					oldobuf = obuf;
					if ((obuf = Fopen(filename, "w"))
							== NULL) {
						fprintf(stderr, catgets(catd,
							CATSET, 176,
							"Cannot open %s\n"),
								filename);
						obuf = Fopen("/dev/null", "w");
					}
					if (obuf == NULL) {
						fprintf(stderr, catgets(catd,
							CATSET, 176,
							"Cannot open %s\n"),
							"/dev/null");
						obuf = oldobuf;
						oldobuf = (FILE *)-1;
						error_return = -1;
						goto send_multi_end;
					} else
						pbuf = qbuf = obuf;
				}
			}
			if (mime_content == MIME_822) {
				put_from_(pbuf);
				mime_content = MIME_MESSAGE;
			}
			break;
		case MIME_TEXT:
		case MIME_MESSAGE:
			if (lineno > 0) {
				sz = mime_write(oline, sizeof *oline,
					 olinelen, pbuf,
					 convert,
					 action == CONV_TODISP
					 || action == CONV_QUOTE ?
					 TD_ISPR|TD_ICONV:TD_NONE,
					pbuf == qbuf ? prefix : NULL,
					pbuf == qbuf ? prefixlen : 0);
				if (pbuf == origobuf)
					addstats(stats, 1, sz);
			}
			break;
		case MIME_DISCARD:
			/* unspecified part of a mp. msg. */
			break;
		default: /* We do not display this */
			if (lineno > 0 && (action == CONV_TOFILE ||
						pbuf != obuf)) {
				sz = mime_write(oline,
					 sizeof *oline,
					 olinelen, pbuf,
					 convert, TD_NONE,
					pbuf == qbuf ? prefix : NULL,
					pbuf == qbuf ? prefixlen : 0);
				if (pbuf == origobuf)
					addstats(stats, 1, sz);
			}
		}
		if (ferror(pbuf)) {
			error_return = -1;
			break;
		}
		lineno++;
		/*
		 * Writing part data is delayed by one loop run because
		 * the '\n' immediatly before the terminating delimiter
		 * does not belong to the part. Thus we must know the
		 * next line before we can write this one. jump_to_bound
		 * is set when this was the line preceding the delimiter.
		 */
		if (jump_to_bound == 1)
			goto send_multi_lastbound;
		else if (jump_to_bound == 2)
			goto send_multi_midbound;
		exchange(&line, &linesize, &linelen,
				&oline, &olinesize, &olinelen);
	}
send_multi_end:
#ifdef	HAVE_ICONV
	if (iconvd != (iconv_t)-1) {
		iconv_close(iconvd);
		iconvd = (iconv_t)-1;
	}
#endif
	if (oldobuf != (FILE *)-1 &&
			obuf != origobuf) {
		Fclose(obuf);
		qbuf = pbuf = obuf = oldobuf;
		oldobuf = (FILE *)-1;
	}
	if (pbuf != qbuf) {
		safe_signal(SIGPIPE, SIG_IGN);
		Pclose(pbuf);
		safe_signal(SIGPIPE, SIG_DFL);
		if (qbuf != obuf)
			pipecpy(qbuf, obuf, origobuf, prefix, prefixlen, stats);
	}
	bound_free(b0);
	if (line)
		free(line);
	if (oline)
		free(oline);
	return error_return;
}

/*
 * Send message described by the passed pointer to the
 * passed output buffer.  Return -1 on error.
 * Adjust the status: field if need be.
 * If doign is given, suppress ignored header fields.
 * prefix is a string to prepend to each output line.
 * action = data destination (CONV_NONE,_TOFILE,_TODISP,_QUOTE).
 * stats[0] is line count, stats[1] is character count. stats may be NULL.
 * Note that stats[0] is valid for CONV_NONE only.
 */
int
send_message(mp, obuf, doign, prefix, action, stats)
	struct message *mp;
	FILE *obuf;
	struct ignoretab *doign;
	char *prefix;
	off_t *stats;
{
	FILE *ibuf, *pbuf = obuf, *qbuf = obuf, *origobuf = obuf;
	struct sendmsg *pm;
	char *line = NULL;
	char *cp, *cp2;
	char *scontent = NULL;
	int prefixlen = 0;
	size_t length, count;
	int mime_content = MIME_TEXT, convert = CONV_NONE;
	int error_return = 0;
	struct boundary b0;
	char *cs = us_ascii, *tcs;
	int mainhdr = 1;
	struct hdrline *ph;
	size_t sz, linesize = 0;

	if (mp == dot)
		did_print_dot = 1;
	b0.b_str = NULL;
	b0.b_nlink = b0.b_flink = NULL;
	if (stats)
		stats[0] = stats[1] = 0;
	tcs = gettcharset();
	/*
	 * Compute the prefix string, without trailing whitespace
	 */
	if (prefix != NULL) {
		cp2 = 0;
		for (cp = prefix; *cp; cp++)
			if (!blankchar(*cp & 0377))
				cp2 = cp;
		prefixlen = cp2 == 0 ? 0 : cp2 - prefix + 1;
	} else
		prefixlen = 0;
	ibuf = setinput(mp);
	pm = open_sendmsg(ibuf, mp->m_size, action != CONV_NONE);
	/*
	 * Process headers first. First line is the From_ line, so no
	 * headers there to worry about.
	 */
	if ((cp = read_sendmsg(pm, &line, &linesize, &length, NULL, 0))
			!= NULL && doign != allignore) {
		sz = mime_write(line, sizeof *line, length, obuf,
				action == CONV_TODISP || action == CONV_QUOTE ?
					CONV_FROMHDR : CONV_NONE,
				action == CONV_TODISP ?
					TD_ISPR|TD_ICONV : TD_NONE,
				prefix, prefixlen);
		if (obuf == origobuf)
			addstats(stats, 1, sz);
	}
send_parseheader:
	ph = NULL;
	read_hdr(&ph, pm, 0);
	write_hdr(ph, obuf, action, doign, prefix, prefixlen, mainhdr, mp,
			obuf == origobuf ? stats : NULL);
	if (action != CONV_NONE) {
		char *unfolded_ct, *unfolded_cte;

		unfolded_ct = unfold_hdr(ph, "content-type");
		if ((scontent = field_of_hdr(ph, "content-type")) != NULL)
			mime_content = mime_getcontent(unfolded_ct);
		if (mime_content == MIME_MULTI)
			b0.b_str = mime_getboundary(unfolded_ct);
		if ((cs = param_of_hdr(ph, "content-type", "charset")) == NULL)
			cs = us_ascii;
		unfolded_cte = unfold_hdr(ph, "content-transfer-encoding");
		if (unfolded_cte) {
			switch (mime_getenc(unfolded_cte)) {
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
				convert = CONV_FROMB64;
				break;
			default:
				convert = CONV_NONE;
				mime_content = MIME_UNKNOWN;
			}
		}
	}
	del_hdr(ph);
	if (mime_content == MIME_822) {
		switch (action) {
		case CONV_TODISP:
		case CONV_QUOTE:
			mime_content = MIME_TEXT;
			mainhdr = 0;
			goto send_parseheader;
		case CONV_TOFILE:
			put_from_(obuf);
			/*FALLTHRU*/
		default:
			mime_content = MIME_MESSAGE;
		}
	}
	if (ferror(obuf)) {
		error_return = -1;
		goto send_end;
	}
	/*
	 * Copy out message body
	 */
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
			error_return = send_multipart(pm, obuf, doign,
					prefix, prefixlen,
					convert, action, &b0, stats);
			goto send_end;
		default:
			if (action != CONV_TOFILE) {
				/* we do not display this */
				if (action == CONV_TODISP)
					fputs(catgets(catd, CATSET, 210,
						"[Binary content]\n\n"), obuf);
				goto send_end;
			}
		}
	}
	if (cs == NULL)
		cs = us_ascii;
#ifdef	HAVE_ICONV
	if (action == CONV_TODISP || action == CONV_QUOTE) {
		if (iconvd != (iconv_t)-1)
				iconv_close(iconvd);
		if (asccasecmp(tcs, cs) && asccasecmp(us_ascii, cs))
			iconvd = iconv_open_ft(tcs, cs);
		else
			iconvd = (iconv_t)-1;
	}
#endif
	if (doign == allignore)
		pm->sm_count--;	/* skip final blank line XXX forbidden ? */
	if (action == CONV_TODISP || action == CONV_QUOTE) {
		qbuf = obuf;
		pbuf = getpipetype(scontent, &qbuf, action == CONV_QUOTE);
		if (pbuf != qbuf) {
			safe_signal(SIGPIPE, onpipe);
			if (sigsetjmp(pipejmp, 1))
				goto send_end;
		}
	} else
		pbuf = qbuf = obuf;
	if (stats && convert != CONV_NONE)
		stats[0] = -1;
	for (;;) {
		cp = read_sendmsg(pm, &line, &linesize, &count, NULL, 0);
		if (cp == NULL)
			break;
		sz = mime_write(line, sizeof *line, count,
				 pbuf, convert, action == CONV_TODISP
				 	|| action == CONV_QUOTE ?
					TD_ISPR|TD_ICONV:TD_NONE,
					pbuf == qbuf ? prefix : NULL,
					pbuf == qbuf ? prefixlen : 0);
		if (ferror(pbuf)) {
			error_return = -1;
			goto send_end;
		}
		if (pbuf == origobuf)
			addstats(stats, 1, sz);
	}
send_end:
	close_sendmsg(pm);
#if 0
	if (doign == allignore && count > 0 && line[count - 1] != '\n')
		/* no final blank line */
		if ((c = sgetc(ibuf)) != EOF && sputc(c, pbuf) == EOF)
			return -1;
#endif
	if (pbuf != qbuf) {
		safe_signal(SIGPIPE, SIG_IGN);
		Pclose(pbuf);
		safe_signal(SIGPIPE, SIG_DFL);
		if (qbuf != obuf)
			pipecpy(qbuf, obuf, origobuf, prefix, prefixlen, stats);
	}
#ifdef	HAVE_ICONV
	if (iconvd != (iconv_t)-1) {
		iconv_close(iconvd);
		iconvd = (iconv_t)-1;
	}
#endif
	if (b0.b_str != NULL)
		free(b0.b_str);
	if (line)
		free(line);
	return error_return;
}
