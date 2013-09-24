/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Perform message editing functions.
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

static int edit1(int *msgvec, int viored);

/*
 * Edit a message list.
 */
int
editor(void *v)
{
	int *msgvec = v;

	return edit1(msgvec, 'e');
}

/*
 * Invoke the visual editor on a message list.
 */
int 
visual(void *v)
{
	int *msgvec = v;

	return edit1(msgvec, 'v');
}

/*
 * Edit a message by writing the message into a funnily-named file
 * (which should not exist) and forking an editor on it.
 * We get the editor from the stuff above.
 */
static int 
edit1(int *msgvec, int viored)
{
	int c, i, wb, lastnl;
	FILE *fp = NULL;
	struct message *mp;
	off_t size;
	char *line = NULL;
	size_t linesize;

	/*
	 * Deal with each message to be edited . . .
	 */
	wb = value("writebackedited") != NULL;
	for (i = 0; msgvec[i] && i < msgCount; i++) {
		sighandler_type sigint;

		if (i > 0) {
			char *p;

			printf(tr(72, "Edit message %d [ynq]? "), msgvec[i]);
			fflush(stdout);
			if (readline_restart(stdin, &line, &linesize, 0) < 0)
				break;
			for (p = line; blankchar(*p); p++)
				;
			if (*p == 'q')
				break;
			if (*p == 'n')
				continue;
		}
		setdot(mp = &message[msgvec[i] - 1]);
		did_print_dot = TRU1;
		touch(mp);

		sigint = safe_signal(SIGINT, SIG_IGN);
		--mp->m_size; /* Strip final NL.. */
		fp = run_editor(fp, -1/*mp->m_size*/, viored,
				(mb.mb_perm & MB_EDIT) == 0 || ! wb,
				NULL, mp, wb ? SEND_MBOX : SEND_TODISP_ALL,
				sigint);
		++mp->m_size; /* And readd it */
		if (fp != NULL) {
			fseek(mb.mb_otf, 0L, SEEK_END);
			size = ftell(mb.mb_otf);
			mp->m_block = mailx_blockof(size);
			mp->m_offset = mailx_offsetof(size);
			mp->m_lines = 0;
			mp->m_flag |= MODIFY;
			rewind(fp);
			lastnl = 0;
			size = 0;
			while ((c = getc(fp)) != EOF) {
				if ((lastnl = c == '\n'))
					mp->m_lines++;
				if (putc(c, mb.mb_otf) == EOF)
					break;
				++size;
			}
			if (! lastnl && putc('\n', mb.mb_otf) != EOF)
				++size;
			if (putc('\n', mb.mb_otf) != EOF)
				++size;
			mp->m_size = (size_t)size;
			if (ferror(mb.mb_otf))
				perror("/tmp");
			Fclose(fp);
		}
		safe_signal(SIGINT, sigint);
	}
	if (line)
		free(line);
	return 0;
}

/*
 * Run an editor on the file at "fpp" of "size" bytes,
 * and return a new file pointer.
 * Signals must be handled by the caller.
 * "viored" is 'e' for ed, 'v' for vi.
 */
FILE *
run_editor(FILE *fp, off_t size, int viored, int readonly,
		struct header *hp, struct message *mp, enum sendaction action,
		sighandler_type oldint)
{
	FILE *nf = NULL;
	int t;
	time_t modtime;
	char const *ed;
	struct stat statb;
	char *tempEdit;
	sigset_t cset;

	if ((nf = Ftemp(&tempEdit, "Re", "w", readonly ? 0400 : 0600, 1))
			== NULL) {
		perror(catgets(catd, CATSET, 73, "temporary mail edit file"));
		goto out;
	}
	if (hp) {
		t = GTO|GSUBJECT|GCC|GBCC|GNL|GCOMMA;
		if (hp->h_from || hp->h_replyto || hp->h_sender ||
				hp->h_organization)
			t |= GIDENT;
		puthead(hp, nf, t, SEND_TODISP, CONV_NONE, NULL, NULL);
	}
	if (mp) {
		sendmp(mp, nf, 0, NULL, action, NULL);
	} else {
		if (size >= 0)
			while (--size >= 0 && (t = getc(fp)) != EOF)
				putc(t, nf);
		else
			while ((t = getc(fp)) != EOF)
				putc(t, nf);
	}
	fflush(nf);
	if (fstat(fileno(nf), &statb) < 0)
		modtime = 0;
	else
		modtime = statb.st_mtime;
	if (ferror(nf)) {
		Fclose(nf);
		perror(tempEdit);
		nf = NULL;
		goto out;
	}
	if (Fclose(nf) < 0) {
		perror(tempEdit);
		nf = NULL;
		goto out;
	}
	nf = NULL;

	if ((ed = value(viored == 'e' ? "EDITOR" : "VISUAL")) == NULL)
		ed = (viored == 'e') ? "ed" : "vi";
	sigemptyset(&cset);
	if (run_command(ed, oldint != SIG_IGN ? &cset : NULL, -1, -1,
				tempEdit, NULL, NULL) < 0)
		goto out;

	/*
	 * If in read only mode or file unchanged, just remove the editor
	 * temporary and return.  Otherwise switch to new file.
	 */
	if (readonly)
		goto out;
	if (stat(tempEdit, &statb) < 0) {
		perror(tempEdit);
		Ftfree(&tempEdit);
		goto out;
	}
	if (modtime != statb.st_mtime && (nf = Fopen(tempEdit, "a+")) == NULL)
		perror(tempEdit);
out:
	if (tempEdit != NULL) {
		unlink(tempEdit);
		Ftfree(&tempEdit);
	}
	return nf;
}
