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
static char sccsid[] = "@(#)fio.c	2.7 (gritter) 10/11/02";
#endif
#endif /* not lint */

#include "rcv.h"
#include <sys/file.h>
#ifdef	HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include <errno.h>
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * File I/O.
 */
static void	makemessage __P((void));
static void	append __P((struct message *));
static size_t	length_of_line __P((const char *, size_t));
static char	*fgetline_byone __P((char **, size_t *, size_t *,
			FILE *, int, size_t));

/*
 * Set up the input pointers while copying the mail file into /tmp.
 */
void
setptr(ibuf, offset)
	FILE *ibuf;
	off_t offset;
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
	this.m_flag = MUSED|MNEW;
	this.m_size = 0;
	this.m_lines = 0;
	this.m_block = 0;
	this.m_offset = 0;
	filesize = mailsize - offset;
	for (;;) {
		if (fgetline(&linebuf, &linesize, &filesize, &count, ibuf, 0)
				== NULL) {
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
		(void) fwrite(linebuf, sizeof *linebuf, count, otf);
		if (ferror(otf)) {
			perror("/tmp");
			exit(1);
		}
		if (linebuf[count - 1] == '\n')
			linebuf[count - 1] = '\0';
		if (maybe && linebuf[0] == 'F' && is_head(linebuf, count)) {
			if (thiscnt++ > 0)
				append(&this);
			msgcount++;
			this.m_flag = MUSED|MNEW;
			this.m_size = 0;
			this.m_lines = 0;
			this.m_block = nail_blockof(offset);
			this.m_offset = nail_offsetof(offset);
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
					inhead = 0;
					break;
				}
				if (*cp != c && *cp != toupper(c))
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
putline(obuf, linebuf, count)
	FILE *obuf;
	char *linebuf;
	size_t count;
{
	(void) fwrite(linebuf, sizeof *linebuf, count, obuf);
	(void) sputc('\n', obuf);
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
readline_restart(ibuf, linebuf, linesize, n)
	FILE *ibuf;
	char **linebuf;
	size_t *linesize;
	size_t n;
{
	ssize_t sz;

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
				if (errno == EINTR)
					goto again;
				if (n > 0) {
					if ((*linebuf)[n - 1] != '\n') {
						newline_appended();
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
setinput(mp)
	struct message *mp;
{

	fflush(otf);
	if (fseek(itf, (long)nail_positionof(mp->m_block,
					mp->m_offset), 0) < 0) {
		perror("fseek");
		panic(catgets(catd, CATSET, 77, "temporary file seek"));
	}
	return (itf);
}

struct message *
setdot(mp)
	struct message *mp;
{
	dot = mp;
	did_print_dot = 0;
	return dot;
}

/*
 * Take the data out of the passed ghost file and toss it into
 * a dynamically allocated message structure.
 */
static void
makemessage()
{
	setdot(message);
	if (msgcount == 0)
		append(NULL);
	message[msgcount].m_size = 0;
	message[msgcount].m_lines = 0;
}

/*
 * Append the passed message descriptor onto the message structure.
 */
static void
append(mp)
	struct message *mp;
{
	if (msgcount + 1 >= msgspace)
		message = srealloc(message, (msgspace += 64) * sizeof *message);
	if (msgcount > 0)
		message[msgcount - 1] = *mp;
}

/*
 * Delete a file, but only if the file is a plain file.
 */
int
rm(name)
	char *name;
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
holdsigs()
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
relsesigs()
{

	if (--sigdepth == 0)
		sigprocmask(SIG_SETMASK, &oset, (sigset_t *)NULL);
}

/*
 * Determine the size of the file possessed by
 * the passed buffer.
 */
off_t
fsize(iob)
	FILE *iob;
{
	struct stat sbuf;

	if (fstat(fileno(iob), &sbuf) < 0)
		return 0;
	return sbuf.st_size;
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
expand(name)
	char *name;
{
	char xname[PATHSIZE];
	char cmdbuf[PATHSIZE];		/* also used for file names */
	int pid, l;
	char *cp, *shell;
	int pivec[2];
	struct stat sbuf;
	extern int wait_status;

	/*
	 * The order of evaluation is "%" and "#" expand into constants.
	 * "&" can expand into "+".  "+" can expand into shell meta characters.
	 * Shell meta characters expand into constants.
	 * This way, we make no recursive expansion.
	 */
	switch (*name) {
	case '%':
		findmail(name[1] ? name + 1 : myname, name[1] != '\0' || uflag,
				xname, sizeof xname);
		return savestr(xname);
	case '#':
		if (name[1] != 0)
			break;
		if (prevfile[0] == 0) {
			printf(catgets(catd, CATSET, 80, "No previous file\n"));
			return NULL;
		}
		return savestr(prevfile);
	case '&':
		if (name[1] == 0 && (name = value("MBOX")) == NULL)
			name = "~/mbox";
		/* fall through */
	}
	if (name[0] == '+' && getfold(cmdbuf, sizeof cmdbuf) >= 0) {
		snprintf(xname, sizeof xname, "%s/%s", cmdbuf, name + 1);
		name = savestr(xname);
	}
	/* catch the most common shell meta character */
	if (name[0] == '~' && (name[1] == '/' || name[1] == '\0')) {
		snprintf(xname, sizeof xname, "%s%s", homedir, name + 1);
		name = savestr(xname);
	}
	if (!anyof(name, "~{[*?$`'\"\\"))
		return name;
	if (pipe(pivec) < 0) {
		perror("pipe");
		return name;
	}
	snprintf(cmdbuf, sizeof cmdbuf, "echo %s", name);
	if ((shell = value("SHELL")) == NULL)
		shell = PATH_CSHELL;
	pid = start_command(shell, 0, -1, pivec[1], "-c", cmdbuf, NULL);
	if (pid < 0) {
		close(pivec[0]);
		close(pivec[1]);
		return NULL;
	}
	close(pivec[1]);
again:
	l = read(pivec[0], xname, sizeof xname);
	if (l < 0) {
		if (errno == EINTR)
			goto again;
		perror("read");
		close(pivec[0]);
		return NULL;
	}
	close(pivec[0]);
	if (wait_child(pid) < 0 && WTERMSIG(wait_status) != SIGPIPE) {
		fprintf(stderr, catgets(catd, CATSET, 81,
				"\"%s\": Expansion failed.\n"), name);
		return NULL;
	}
	if (l == 0) {
		fprintf(stderr, catgets(catd, CATSET, 82,
					"\"%s\": No match.\n"), name);
		return NULL;
	}
	if (l == sizeof xname) {
		fprintf(stderr, catgets(catd, CATSET, 83,
				"\"%s\": Expansion buffer overflow.\n"), name);
		return NULL;
	}
	xname[l] = 0;
	for (cp = &xname[l-1]; *cp == '\n' && cp > xname; cp--)
		;
	cp[1] = '\0';
	if (strchr(xname, ' ') && stat(xname, &sbuf) < 0) {
		fprintf(stderr, catgets(catd, CATSET, 84,
				"\"%s\": Ambiguous.\n"), name);
		return NULL;
	}
	return savestr(xname);
}

/*
 * Determine the current folder directory name.
 */
int
getfold(name, size)
	char *name;
	int size;
{
	char *folder;

	if ((folder = value("folder")) == NULL)
		return (-1);
	if (*folder == '/') {
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
getdeadletter()
{
	char *cp;

	if ((cp = value("DEAD")) == NULL || (cp = expand(cp)) == NULL)
		cp = expand("~/dead.letter");
	else if (*cp != '/') {
		char *buf;
		size_t sz;

		buf = ac_alloc(sz = strlen(cp) + 3);
		snprintf(buf, sz, "~/%s", cp);
		snprintf(buf, sz, "~/%s", cp);
		cp = expand(buf);
		ac_free(buf);
	}
	return cp;
}

/*
 * line is a buffer with the result of fgets(). Returns the first
 * newline or the last character read.
 */
static size_t
length_of_line(line, linesize)
const char *line;
size_t linesize;
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
fgetline(line, linesize, count, llen, fp, appendnl)
char **line;
size_t *linesize, *count, *llen;
FILE *fp;
{
	ssize_t i_llen, sz;

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
				newline_appended();
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
fgetline_byone(line, linesize, llen, fp, appendnl, n)
char **line;
size_t *linesize, *llen;
FILE *fp;
size_t n;
{
	int c;

	if (*line == NULL || *linesize < LINESIZE + n + 1)
		*line = srealloc(*line, *linesize = LINESIZE + n + 1);
	for (;;) {
		if (n >= *linesize - 128)
			*line = srealloc(*line, *linesize += 256);
		c = sgetc(fp);
		if (c != EOF) {
			(*line)[n++] = c;
			(*line)[n] = '\0';
			if (c == '\n')
				break;
		} else {
			if (n > 0) {
				if (appendnl) {
					newline_appended();
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

void
newline_appended()
{
	fprintf(stderr, catgets(catd, CATSET, 208,
			"warning: incomplete line - newline appended\n"));
	exit_status |= 010;
}
