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

#include <errno.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef HAVE_SOCKETS
# include <netdb.h>
# include <netinet/in.h>
# include <sys/socket.h>
# ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
# endif
#endif
#ifdef USE_OPENSSL
# include <openssl/ssl.h>
# include <openssl/err.h>
# include <openssl/x509v3.h>
# include <openssl/x509.h>
# include <openssl/rand.h>
#endif
#ifdef HAVE_WORDEXP
# include <wordexp.h>
#endif

#include "extern.h"

/*
 * Mail -- a mail program
 *
 * File I/O.
 */

/*
 * Evaluate the string given as a new mailbox name.
 * Supported meta characters:
 *	%	for my system mail box
 *	%user	for user's system mail box
 *	#	for previous file
 *	&	invoker's mbox file
 *	+file	file in folder directory
 *	any shell meta character
 * Return the file name as a dynamic string.
 */
static char *	_expand(char const *name, int only_local);
/* Perform shell meta character expansion */
static char *	_globname(char const *name);

static void makemessage(void);
static void append(struct message *mp);
static size_t length_of_line(const char *line, size_t linesize);
static char *fgetline_byone(char **line, size_t *linesize, size_t *llen,
		FILE *fp, int appendnl, size_t n);
static enum okay get_header(struct message *mp);

static char *
_expand(char const *name, int only_local)
{
	char cbuf[PATHSIZE], *res;
	struct str s;
	struct shortcut *sh;
	int dyn;

	/*
	 * The order of evaluation is "%" and "#" expand into constants.
	 * "&" can expand into "+".  "+" can expand into shell meta characters.
	 * Shell meta characters expand into constants.
	 * This way, we make no recursive expansion.
	 */
	res = (char*)name;
	if ((sh = get_shortcut(res)) != NULL)
		res = sh->sh_long;

jnext:	dyn = 0;
	switch (*res) {
	case '%':
		if (res[1] == ':' && res[2]) {
			res = &res[2];
			goto jnext;
		}
		findmail((res[1] ? res + 1 : myname),
			(res[1] != '\0' || uflag), cbuf, sizeof cbuf);
		res = cbuf;
		goto jislocal;
	case '#':
		if (res[1] != 0)
			break;
		if (prevfile[0] == 0) {
			fprintf(stderr, tr(80, "No previous file\n"));
			res = NULL;
			goto jleave;
		}
		res = prevfile;
		goto jislocal;
	case '&':
		if (res[1] == 0 && (res = value("MBOX")) == NULL)
			res = "~/mbox";
		break;
	}

	if (res[0] == '@' && which_protocol(mailname) == PROTO_IMAP) {
		res = str_concat_csvl(&s,
			protbase(mailname), "/", res + 1, NULL)->s;
		dyn = 1;
	}

	if (res[0] == '+' && getfold(cbuf, sizeof cbuf) >= 0) {
		res = str_concat_csvl(&s,
			cbuf, ((which_protocol(cbuf) == PROTO_IMAP &&
					strcmp(cbuf, protbase(cbuf))) /* XXX */
				? "" : "/"),
			res + 1, NULL)->s;
		dyn = 1;
		if (cbuf[0] == '%' && cbuf[1] == ':')
			goto jnext;
	}

	/* Catch the most common shell meta character */
	if (res[0] == '~' && (res[1] == '/' || res[1] == '\0')) {
		res = str_concat_csvl(&s, homedir, res + 1, NULL)->s;
		dyn = 1;
	}

	if (anyof(res, "|&;<>~{}()[]*?$`'\"\\") &&
			which_protocol(res) == PROTO_FILE) {
		res = _globname(res);
		dyn = 1;
		goto jleave;
	}

jislocal:
	if (only_local)
		switch (which_protocol(res)) {
		case PROTO_FILE:
		case PROTO_MAILDIR:	/* XXX Really? ok MAILDIR for local? */
			break;
		default:
			fprintf(stderr, tr(280,
				"\"%s\": only a local file or directory may "
				"be used\n"), name);
			res = NULL;
			break;
		}

jleave:
	if (res && ! dyn)
		res = savestr(res);
	return (res);
}

static char *
_globname(char const *name)
{
#ifdef HAVE_WORDEXP
	wordexp_t we;
	char *cp = NULL;
	sigset_t nset;
	int i;

	/*
	 * Some systems (notably Open UNIX 8.0.0) fork a shell for
	 * wordexp() and wait for it; waiting will fail if our SIGCHLD
	 * handler is active.
	 */
	sigemptyset(&nset);
	sigaddset(&nset, SIGCHLD);
	sigprocmask(SIG_BLOCK, &nset, NULL);
	i = wordexp(name, &we, 0);
	sigprocmask(SIG_UNBLOCK, &nset, NULL);

	switch (i) {
	case 0:
		break;
	case WRDE_NOSPACE:
		fprintf(stderr, tr(83, "\"%s\": Expansion buffer overflow.\n"),
			name);
		goto jleave;
	case WRDE_BADCHAR:
	case WRDE_SYNTAX:
	default:
		fprintf(stderr, tr(242, "Syntax error in \"%s\"\n"), name);
		goto jleave;
	}

	switch (we.we_wordc) {
	case 1:
		cp = savestr(we.we_wordv[0]);
		break;
	case 0:
		fprintf(stderr, tr(82, "\"%s\": No match.\n"), name);
		break;
	default:
		fprintf(stderr, tr(84, "\"%s\": Ambiguous.\n"), name);
		break;
	}
jleave:
	wordfree(&we);
	return (cp);

#else /* !HAVE_WORDEXP */
	extern int wait_status;

	struct stat sbuf;
	char xname[PATHSIZE], cmdbuf[PATHSIZE], /* also used for file names */
		*cp, *shell;
	int pid, l, pivec[2];

	if (pipe(pivec) < 0) {
		perror("pipe");
		return (NULL);
	}
	snprintf(cmdbuf, sizeof cmdbuf, "echo %s", name);
	if ((shell = value("SHELL")) == NULL)
		shell = SHELL;
	pid = start_command(shell, 0, -1, pivec[1], "-c", cmdbuf, NULL);
	if (pid < 0) {
		close(pivec[0]);
		close(pivec[1]);
		return (NULL);
	}
	close(pivec[1]);

again:
	l = read(pivec[0], xname, sizeof xname);
	if (l < 0) {
		if (errno == EINTR)
			goto again;
		perror("read");
		close(pivec[0]);
		return (NULL);
	}
	close(pivec[0]);
	if (wait_child(pid) < 0 && WTERMSIG(wait_status) != SIGPIPE) {
		fprintf(stderr, tr(81, "\"%s\": Expansion failed.\n"), name);
		return (NULL);
	}
	if (l == 0) {
		fprintf(stderr, tr(82, "\"%s\": No match.\n"), name);
		return (NULL);
	}
	if (l == sizeof xname) {
		fprintf(stderr, tr(83, "\"%s\": Expansion buffer overflow.\n"),
			name);
		return (NULL);
	}
	xname[l] = 0;
	for (cp = &xname[l-1]; *cp == '\n' && cp > xname; cp--)
		;
	cp[1] = '\0';
	if (strchr(xname, ' ') && stat(xname, &sbuf) < 0) {
		fprintf(stderr, tr(84, "\"%s\": Ambiguous.\n"), name);
		return NULL;
	}
	return (savestr(xname));
#endif /* !HAVE_WORDEXP */
}

/*
 * Set up the input pointers while copying the mail file into /tmp.
 */
void
setptr(FILE *ibuf, off_t offset)
{
	int c;
	size_t count;
	char *cp, *cp2;
	struct message this;
	int maybe, inhead, thiscnt;
	char *linebuf = NULL;
	size_t linesize = 0, filesize;

	maybe = 1;
	inhead = 0;
	thiscnt = 0;
	memset(&this, 0, sizeof this);
	this.m_flag = MUSED|MNEW|MNEWEST;
	filesize = mailsize - offset;
	offset = ftell(mb.mb_otf);
	for (;;) {
		if (fgetline(&linebuf, &linesize, &filesize, &count, ibuf, 0)
				== NULL) {
			this.m_xsize = this.m_size;
			this.m_xlines = this.m_lines;
			this.m_have = HAVE_HEADER|HAVE_BODY;
			if (thiscnt > 0)
				append(&this);
			makemessage();
			if (linebuf)
				free(linebuf);
			return;
		}
#ifdef	notdef
		if (linebuf[0] == '\0')
			linebuf[0] = '.';
#endif
		fwrite(linebuf, sizeof *linebuf, count, mb.mb_otf);
		if (ferror(mb.mb_otf)) {
			perror("/tmp");
			exit(1);
		}
		if (linebuf[count - 1] == '\n')
			linebuf[count - 1] = '\0';
		if (maybe && linebuf[0] == 'F' && is_head(linebuf, count)) {
			this.m_xsize = this.m_size;
			this.m_xlines = this.m_lines;
			this.m_have = HAVE_HEADER|HAVE_BODY;
			if (thiscnt++ > 0)
				append(&this);
			msgCount++;
			this.m_flag = MUSED|MNEW|MNEWEST;
			this.m_size = 0;
			this.m_lines = 0;
			this.m_block = mailx_blockof(offset);
			this.m_offset = mailx_offsetof(offset);
			inhead = 1;
		} else if (linebuf[0] == 0) {
			inhead = 0;
		} else if (inhead) {
			for (cp = linebuf, cp2 = "status";; cp++) {
				if ((c = *cp2++) == 0) {
					while (c = *cp++, whitechar(c));
					if (cp[-1] != ':')
						break;
					while ((c = *cp++) != '\0')
						if (c == 'R')
							this.m_flag |= MREAD;
						else if (c == 'O')
							this.m_flag &= ~MNEW;
					break;
				}
				if (*cp != c && *cp != upperconv(c))
					break;
			}
			for (cp = linebuf, cp2 = "x-status";; cp++) {
				if ((c = *cp2++) == 0) {
					while (c = *cp++, whitechar(c));
					if (cp[-1] != ':')
						break;
					while ((c = *cp++) != '\0')
						if (c == 'F')
							this.m_flag |= MFLAGGED;
						else if (c == 'A')
							this.m_flag|=MANSWERED;
						else if (c == 'T')
							this.m_flag|=MDRAFTED;
					break;
				}
				if (*cp != c && *cp != upperconv(c))
					break;
			}
		}
		offset += count;
		this.m_size += count;
		this.m_lines++;
		maybe = linebuf[0] == 0;
	}
	/*NOTREACHED*/
}

/*
 * Drop the passed line onto the passed output buffer.
 * If a write error occurs, return -1, else the count of
 * characters written, including the newline.
 */
int
putline(FILE *obuf, char *linebuf, size_t count)
{
	fwrite(linebuf, sizeof *linebuf, count, obuf);
	putc('\n', obuf);
	if (ferror(obuf))
		return (-1);
	return (count + 1);
}

/*
 * Read up a line from the specified input into the line
 * buffer.  Return the number of characters read.  Do not
 * include the newline at the end.
 *
 * n is the number of characters already read.
 */
int
readline_restart(FILE *ibuf, char **linebuf, size_t *linesize, size_t n)
{
	long sz;

	clearerr(ibuf);
	/*
	 * Interrupts will cause trouble if we are inside a stdio call. As
	 * this is only relevant if input comes from a terminal, we can simply
	 * bypass it by read() then.
	 */
	if (fileno(ibuf) == 0 && is_a_tty[0]) {
		if (*linebuf == NULL || *linesize < LINESIZE + n + 1)
			*linebuf = srealloc(*linebuf,
					*linesize = LINESIZE + n + 1);
		for (;;) {
			if (n >= *linesize - 128)
				*linebuf = srealloc(*linebuf, *linesize += 256);
again:
			sz = read(0, *linebuf + n, *linesize - n - 1);
			if (sz > 0) {
				n += sz;
				(*linebuf)[n] = '\0';
				if (n > 0 && (*linebuf)[n - 1] == '\n')
					break;
			} else {
				if (sz < 0 && errno == EINTR)
					goto again;
				if (n > 0) {
					if ((*linebuf)[n - 1] != '\n') {
						(*linebuf)[n++] = '\n';
						(*linebuf)[n] = '\0';
					}
					break;
				} else
					return -1;
			}
		}
	} else {
		/*
		 * Not reading from standard input or standard input not
		 * a terminal. We read one char at a time as it is the
		 * only way to get lines with embedded NUL characters in
		 * standard stdio.
		 */
		if (fgetline_byone(linebuf, linesize, &n, ibuf, 1, n) == NULL)
			return -1;
	}
	if (n > 0 && (*linebuf)[n - 1] == '\n')
		(*linebuf)[--n] = '\0';
	return n;
}

/*
 * Return a file buffer all ready to read up the
 * passed message pointer.
 */
FILE *
setinput(struct mailbox *mp, struct message *m, enum needspec need)
{
	enum okay ok = STOP;

	switch (need) {
	case NEED_HEADER:
		if (m->m_have & HAVE_HEADER)
			ok = OKAY;
		else
			ok = get_header(m);
		break;
	case NEED_BODY:
		if (m->m_have & HAVE_BODY)
			ok = OKAY;
		else
			ok = get_body(m);
		break;
	case NEED_UNSPEC:
		ok = OKAY;
		break;
	}
	if (ok != OKAY)
		return NULL;
	fflush(mp->mb_otf);
	if (fseek(mp->mb_itf, (long)mailx_positionof(m->m_block,
					m->m_offset), SEEK_SET) < 0) {
		perror("fseek");
		panic(catgets(catd, CATSET, 77, "temporary file seek"));
	}
	return (mp->mb_itf);
}

struct message *
setdot(struct message *mp)
{
	if (dot != mp) {
		prevdot = dot;
		did_print_dot = 0;
	}
	dot = mp;
	uncollapse1(dot, 0);
	return dot;
}

/*
 * Take the data out of the passed ghost file and toss it into
 * a dynamically allocated message structure.
 */
static void
makemessage(void)
{
	if (msgCount == 0)
		append(NULL);
	setdot(message);
	message[msgCount].m_size = 0;
	message[msgCount].m_lines = 0;
}

/*
 * Append the passed message descriptor onto the message structure.
 */
static void
append(struct message *mp)
{
	if (msgCount + 1 >= msgspace)
		message = srealloc(message, (msgspace += 64) * sizeof *message);
	if (msgCount > 0)
		message[msgCount - 1] = *mp;
}

/*
 * Delete a file, but only if the file is a plain file.
 */
int
rm(char *name)
{
	struct stat sb;

	if (stat(name, &sb) < 0)
		return(-1);
	if (!S_ISREG(sb.st_mode)) {
		errno = EISDIR;
		return(-1);
	}
	return(unlink(name));
}

static int sigdepth;		/* depth of holdsigs() */
static sigset_t nset, oset;
/*
 * Hold signals SIGHUP, SIGINT, and SIGQUIT.
 */
void
holdsigs(void)
{

	if (sigdepth++ == 0) {
		sigemptyset(&nset);
		sigaddset(&nset, SIGHUP);
		sigaddset(&nset, SIGINT);
		sigaddset(&nset, SIGQUIT);
		sigprocmask(SIG_BLOCK, &nset, &oset);
	}
}

/*
 * Release signals SIGHUP, SIGINT, and SIGQUIT.
 */
void
relsesigs(void)
{
	if (--sigdepth == 0)
		sigprocmask(SIG_SETMASK, &oset, (sigset_t *)NULL);
}

/*
 * Determine the size of the file possessed by
 * the passed buffer.
 */
off_t
fsize(FILE *iob)
{
	struct stat sbuf;

	if (fstat(fileno(iob), &sbuf) < 0)
		return 0;
	return sbuf.st_size;
}

/*
 * Just like expand(), but expanded file must be FILE or DIR
 */
char *
file_expand(char const *name)
{
	return (_expand(name, 1));
}

/*
 * Evaluate the string given as a new mailbox name.
 * Supported meta characters:
 *	%	for my system mail box
 *	%user	for user's system mail box
 *	#	for previous file
 *	&	invoker's mbox file
 *	+file	file in folder directory
 *	any shell meta character
 * Return the file name as a dynamic string.
 */
char *
expand(char const *name)
{
	return (_expand(name, 0));
}

/*
 * Determine the current folder directory name.
 */
int
getfold(char *name, int size)
{
	char *folder;
	enum protocol	p;

	if ((folder = value("folder")) == NULL)
		return (-1);
	if (*folder == '/' || ((p = which_protocol(folder)) != PROTO_FILE &&
			p != PROTO_MAILDIR)) {
		strncpy(name, folder, size);
		name[size-1]='\0';
	} else {
		snprintf(name, size, "%s/%s", homedir, folder);
	}
	return (0);
}

/*
 * Return the name of the dead.letter file.
 */
char *
getdeadletter(void)
{
	char *cp;

	if ((cp = value("DEAD")) == NULL || (cp = file_expand(cp)) == NULL)
		cp = file_expand("~/dead.letter");
	else if (*cp != '/') {
		size_t sz = strlen(cp) + 3;
		char *buf = ac_alloc(sz);

		snprintf(buf, sz, "~/%s", cp);
		cp = file_expand(buf);
		ac_free(buf);
	}
	if (cp == NULL)
		cp = "dead.letter";
	return (cp);
}

/*
 * line is a buffer with the result of fgets(). Returns the first
 * newline or the last character read.
 */
static size_t
length_of_line(const char *line, size_t linesize)
{
	register size_t i;

	/*
	 * Last character is always '\0' and was added by fgets.
	 */
	linesize--;
	for (i = 0; i < linesize; i++)
		if (line[i] == '\n')
			break;
	return i < linesize ? i + 1 : linesize;
}

/*
 * fgets replacement to handle lines of arbitrary size and with
 * embedded \0 characters.
 * line - line buffer. *line be NULL.
 * linesize - allocated size of line buffer.
 * count - maximum characters to read. May be NULL.
 * llen - length_of_line(*line).
 * fp - input FILE.
 * appendnl - always terminate line with \n, append if necessary.
 */
char *
fgetline(char **line, size_t *linesize, size_t *count, size_t *llen,
		FILE *fp, int appendnl)
{
	size_t i_llen, sz;

	if (count == NULL)
		/*
		 * If we have no count, we cannot determine where the
		 * characters returned by fgets() end if there was no
		 * newline. We have to read one character at one.
		 */
		return fgetline_byone(line, linesize, llen, fp, appendnl, 0);
	if (*line == NULL || *linesize < LINESIZE)
		*line = srealloc(*line, *linesize = LINESIZE);
	sz = *linesize <= *count ? *linesize : *count + 1;
	if (sz <= 1 || fgets(*line, sz, fp) == NULL)
		/*
		 * Leave llen untouched; it is used to determine whether
		 * the last line was \n-terminated in some callers.
		 */
		return NULL;
	i_llen = length_of_line(*line, sz);
	*count -= i_llen;
	while ((*line)[i_llen - 1] != '\n') {
		*line = srealloc(*line, *linesize += 256);
		sz = *linesize - i_llen;
		sz = (sz <= *count ? sz : *count + 1);
		if (sz <= 1 || fgets(&(*line)[i_llen], sz, fp) == NULL) {
			if (appendnl) {
				(*line)[i_llen++] = '\n';
				(*line)[i_llen] = '\0';
			}
			break;
		}
		sz = length_of_line(&(*line)[i_llen], sz);
		i_llen += sz;
		*count -= sz;
	}
	if (llen)
		*llen = i_llen;
	return *line;
}

/*
 * Read a line, one character at once.
 */
static char *
fgetline_byone(char **line, size_t *linesize, size_t *llen,
		FILE *fp, int appendnl, size_t n)
{
	int c;

	if (*line == NULL || *linesize < LINESIZE + n + 1)
		*line = srealloc(*line, *linesize = LINESIZE + n + 1);
	for (;;) {
		if (n >= *linesize - 128)
			*line = srealloc(*line, *linesize += 256);
		c = getc(fp);
		if (c != EOF) {
			(*line)[n++] = c;
			(*line)[n] = '\0';
			if (c == '\n')
				break;
		} else {
			if (n > 0) {
				if (appendnl) {
					(*line)[n++] = '\n';
					(*line)[n] = '\0';
				}
				break;
			} else
				return NULL;
		}
	}
	if (llen)
		*llen = n;
	return *line;
}

static enum okay
get_header(struct message *mp)
{
	(void)mp;
	switch (mb.mb_type) {
	case MB_FILE:
	case MB_MAILDIR:
		return (OKAY);
#ifdef USE_POP3
	case MB_POP3:
		return (pop3_header(mp));
#endif
#ifdef USE_IMAP
	case MB_IMAP:
	case MB_CACHE:
		return imap_header(mp);
#endif
	case MB_VOID:
	default:
		return (STOP);
	}
}

enum okay
get_body(struct message *mp)
{
	(void)mp;
	switch (mb.mb_type) {
	case MB_FILE:
	case MB_MAILDIR:
		return (OKAY);
#ifdef USE_POP3
	case MB_POP3:
		return (pop3_body(mp));
#endif
#ifdef USE_IMAP
	case MB_IMAP:
	case MB_CACHE:
		return imap_body(mp);
#endif
	case MB_VOID:
	default:
		return (STOP);
	}
}

#ifdef	HAVE_SOCKETS
static long xwrite(int fd, const char *data, size_t sz);

static long
xwrite(int fd, const char *data, size_t sz)
{
	long wo;
	size_t wt = 0;

	do {
		if ((wo = write(fd, data + wt, sz - wt)) < 0) {
			if (errno == EINTR)
				continue;
			else
				return -1;
		}
		wt += wo;
	} while (wt < sz);
	return sz;
}

int
sclose(struct sock *sp)
{
	int	i;

	if (sp->s_fd > 0) {
		if (sp->s_onclose != NULL)
			(*sp->s_onclose)();
#ifdef USE_OPENSSL
		if (sp->s_use_ssl) {
			sp->s_use_ssl = 0;
			SSL_shutdown(sp->s_ssl);
			SSL_free(sp->s_ssl);
			sp->s_ssl = NULL;
			SSL_CTX_free(sp->s_ctx);
			sp->s_ctx = NULL;
		}
#endif
		{
			i = close(sp->s_fd);
		}
		sp->s_fd = -1;
		return i;
	}
	sp->s_fd = -1;
	return 0;
}

enum okay
swrite(struct sock *sp, const char *data)
{
	return swrite1(sp, data, strlen(data), 0);
}

enum okay
swrite1(struct sock *sp, const char *data, int sz, int use_buffer)
{
	int	x;

	if (use_buffer > 0) {
		int	di;
		enum okay	ok;

		if (sp->s_wbuf == NULL) {
			sp->s_wbufsize = 4096;
			sp->s_wbuf = smalloc(sp->s_wbufsize);
			sp->s_wbufpos = 0;
		}
		while (sp->s_wbufpos + sz > sp->s_wbufsize) {
			di = sp->s_wbufsize - sp->s_wbufpos;
			sz -= di;
			if (sp->s_wbufpos > 0) {
				memcpy(&sp->s_wbuf[sp->s_wbufpos], data, di);
				ok = swrite1(sp, sp->s_wbuf,
						sp->s_wbufsize, -1);
			} else
				ok = swrite1(sp, data,
						sp->s_wbufsize, -1);
			if (ok != OKAY)
				return STOP;
			data += di;
			sp->s_wbufpos = 0;
		}
		if (sz == sp->s_wbufsize) {
			ok = swrite1(sp, data, sp->s_wbufsize, -1);
			if (ok != OKAY)
				return STOP;
		} else if (sz) {
			memcpy(&sp->s_wbuf[sp->s_wbufpos], data, sz);
			sp->s_wbufpos += sz;
		}
		return OKAY;
	} else if (use_buffer == 0 && sp->s_wbuf != NULL &&
			sp->s_wbufpos > 0) {
		x = sp->s_wbufpos;
		sp->s_wbufpos = 0;
		if (swrite1(sp, sp->s_wbuf, x, -1) != OKAY)
			return STOP;
	}
	if (sz == 0)
		return OKAY;
#ifdef USE_OPENSSL
	if (sp->s_use_ssl) {
ssl_retry:	x = SSL_write(sp->s_ssl, data, sz);
		if (x < 0) {
			switch (SSL_get_error(sp->s_ssl, x)) {
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				goto ssl_retry;
			}
		}
	} else
#endif
	{
		x = xwrite(sp->s_fd, data, sz);
	}
	if (x != sz) {
		char	o[512];
		snprintf(o, sizeof o, "%s write error",
				sp->s_desc ? sp->s_desc : "socket");
#ifdef USE_OPENSSL
		sp->s_use_ssl ? ssl_gen_err("%s", o) : perror(o);
#else
		perror(o);
#endif
		if (x < 0)
			sclose(sp);
		return STOP;
	}
	return OKAY;
}

int
sgetline(char **line, size_t *linesize, size_t *linelen, struct sock *sp)
{
	char	*lp = *line;

	if (sp->s_rsz < 0) {
		sclose(sp);
		return sp->s_rsz;
	}
	do {
		if (*line == NULL || lp > &(*line)[*linesize - 128]) {
			size_t diff = lp - *line;
			*line = srealloc(*line, *linesize += 256);
			lp = &(*line)[diff];
		}
		if (sp->s_rbufptr == NULL ||
				sp->s_rbufptr >= &sp->s_rbuf[sp->s_rsz]) {
#ifdef USE_OPENSSL
			if (sp->s_use_ssl) {
		ssl_retry:	if ((sp->s_rsz = SSL_read(sp->s_ssl,
						sp->s_rbuf,
						sizeof sp->s_rbuf)) <= 0) {
					if (sp->s_rsz < 0) {
						char	o[512];
						switch(SSL_get_error(sp->s_ssl,
							sp->s_rsz)) {
						case SSL_ERROR_WANT_READ:
						case SSL_ERROR_WANT_WRITE:
							goto ssl_retry;
						}
						snprintf(o, sizeof o, "%s",
							sp->s_desc ?
								sp->s_desc :
								"socket");
						ssl_gen_err("%s", o);

					}
					break;
				}
			} else
#endif
			{
			again:	if ((sp->s_rsz = read(sp->s_fd, sp->s_rbuf,
						sizeof sp->s_rbuf)) <= 0) {
					if (sp->s_rsz < 0) {
						char	o[512];
						if (errno == EINTR)
							goto again;
						snprintf(o, sizeof o, "%s",
							sp->s_desc ?
								sp->s_desc :
								"socket");
						perror(o);
					}
					break;
				}
			}
			sp->s_rbufptr = sp->s_rbuf;
		}
	} while ((*lp++ = *sp->s_rbufptr++) != '\n');
	*lp = '\0';
	if (linelen)
		*linelen = lp - *line;
	return lp - *line;
}

enum okay
sopen(const char *xserver, struct sock *sp, int use_ssl,
		const char *uhp, const char *portstr, int verbose)
{
#ifdef USE_IPV6
	char	hbuf[NI_MAXHOST];
	struct addrinfo	hints, *res0, *res;
#else
	struct sockaddr_in	servaddr;
	struct in_addr	**pptr;
	struct hostent	*hp;
	struct servent	*ep;
	unsigned short	port = 0;
#endif
	int	sockfd;
	char	*cp;
	char	*server = (char *)xserver;
	(void)use_ssl;
	(void)uhp;

	if ((cp = strchr(server, ':')) != NULL) {
		portstr = &cp[1];
#ifndef USE_IPV6
		port = strtol(portstr, NULL, 10);
#endif
		server = salloc(cp - xserver + 1);
		memcpy(server, xserver, cp - xserver);
		server[cp - xserver] = '\0';
	}
#ifdef USE_IPV6
	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_STREAM;
	if (verbose)
		fprintf(stderr, "Resolving host %s . . .", server);
	if (getaddrinfo(server, portstr, &hints, &res0) != 0) {
		fprintf(stderr, catgets(catd, CATSET, 252,
				"Could not resolve host: %s\n"), server);
		return STOP;
	} else if (verbose)
		fprintf(stderr, " done.\n");
	sockfd = -1;
	for (res = res0; res != NULL && sockfd < 0; res = res->ai_next) {
		if (verbose) {
			if (getnameinfo(res->ai_addr, res->ai_addrlen,
						hbuf, sizeof hbuf, NULL, 0,
						NI_NUMERICHOST) != 0)
				strcpy(hbuf, "unknown host");
			fprintf(stderr, catgets(catd, CATSET, 192,
					"Connecting to %s:%s . . ."),
					hbuf, portstr);
		}
		if ((sockfd = socket(res->ai_family, res->ai_socktype,
				res->ai_protocol)) >= 0) {
			if (connect(sockfd, res->ai_addr, res->ai_addrlen)!=0) {
				close(sockfd);
				sockfd = -1;
			}
		}
	}
	if (sockfd < 0) {
		perror(catgets(catd, CATSET, 254, "could not connect"));
		freeaddrinfo(res0);
		return STOP;
	}
	freeaddrinfo(res0);
#else /* USE_IPV6 */
	if (port == 0) {
		if (strcmp(portstr, "smtp") == 0)
			port = htons(25);
		else if (strcmp(portstr, "smtps") == 0)
			port = htons(465);
# ifdef USE_IMAP
		else if (strcmp(portstr, "imap") == 0)
			port = htons(143);
		else if (strcmp(portstr, "imaps") == 0)
			port = htons(993);
# endif
# ifdef USE_POP3
		else if (strcmp(portstr, "pop3") == 0)
			port = htons(110);
		else if (strcmp(portstr, "pop3s") == 0)
			port = htons(995);
# endif
		else if ((ep = getservbyname((char *)portstr, "tcp")) != NULL)
			port = ep->s_port;
		else {
			fprintf(stderr, tr(251, "Unknown service: %s\n"),
				portstr);
			return (STOP);
		}
	} else
		port = htons(port);
	if (verbose)
		fprintf(stderr, "Resolving host %s . . .", server);
	if ((hp = gethostbyname(server)) == NULL) {
		fprintf(stderr, catgets(catd, CATSET, 252,
				"Could not resolve host: %s\n"), server);
		return STOP;
	} else if (verbose)
		fprintf(stderr, " done.\n");
	pptr = (struct in_addr **)hp->h_addr_list;
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror(catgets(catd, CATSET, 253, "could not create socket"));
		return STOP;
	}
	memset(&servaddr, 0, sizeof servaddr);
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = port;
	memcpy(&servaddr.sin_addr, *pptr, sizeof(struct in_addr));
	if (verbose)
		fprintf(stderr, catgets(catd, CATSET, 192,
				"Connecting to %s:%d . . ."),
				inet_ntoa(**pptr), ntohs(port));
	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof servaddr)
			!= 0) {
		perror(catgets(catd, CATSET, 254, "could not connect"));
		return STOP;
	}
#endif /* USE_IPV6 */
	if (verbose)
		fputs(catgets(catd, CATSET, 193, " connected.\n"), stderr);
	memset(sp, 0, sizeof *sp);
	sp->s_fd = sockfd;
#ifdef USE_SSL
	if (use_ssl) {
		enum okay ok;

		if ((ok = ssl_open(server, sp, uhp)) != OKAY)
			sclose(sp);
		return ok;
	}
#endif
	return OKAY;
}
#endif	/* HAVE_SOCKETS */
