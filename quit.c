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
static char sccsid[] = "@(#)quit.c	2.2 (gritter) 9/7/02";
#endif
#endif /* not lint */

#include "rcv.h"
#include <stdio.h>
#include <errno.h>
#include "extern.h"
#ifdef	HAVE_SYS_FILE_H
#include <sys/file.h>
#endif

/*
 * Rcv -- receive mail rationally.
 *
 * Termination processing.
 */
static void	edstop __P((void));

/*
 * The "quit" command.
 */
/*ARGSUSED*/
int
quitcmd(v)
	void *v;
{
	/*
	 * If we are sourcing, then return 1 so execute() can handle it.
	 * Otherwise, return -1 to abort command loop.
	 */
	if (sourcing)
		return 1;
	return -1;
}

/*
 * Preserve all the appropriate messages back in the system
 * mailbox, and print a nice message indicated how many were
 * saved.  On any error, just return -1.  Else return 0.
 * Incorporate the any new mail that we found.
 */
static int
#ifndef	F_SETLKW
writeback(res)
	FILE *res;
#else
writeback(res, obuf)
	FILE *res, *obuf;
#endif
{
	struct message *mp;
	int p, c;
#ifndef	F_SETLKW
	FILE *obuf;
#endif

	p = 0;
#ifndef	F_SETLKW
	if ((obuf = Fopen(mailname, "r+")) == (FILE*)NULL) {
		perror(mailname);
		return(-1);
	}
#else
	fseek(obuf, 0L, SEEK_SET);
#endif
#ifndef APPEND
	if (res != (FILE*)NULL)
		while ((c = sgetc(res)) != EOF)
			(void) sputc(c, obuf);
#endif
	for (mp = &message[0]; mp < &message[msgcount]; mp++)
		if ((mp->m_flag&MPRESERVE)||(mp->m_flag&MTOUCH)==0) {
			p++;
			if (send_message(mp, obuf, (struct ignoretab *)0,
						NULL, CONV_NONE, NULL) < 0) {
				perror(mailname);
#ifndef	F_SETLKW
				Fclose(obuf);
#else
				fseek(obuf, 0L, SEEK_SET);
#endif
				return(-1);
			}
		}
#ifdef APPEND
	if (res != (FILE*)NULL)
		while ((c = sgetc(res)) != EOF)
			(void) sputc(c, obuf);
#endif
	fflush(obuf);
	trunc(obuf);
	if (ferror(obuf)) {
		perror(mailname);
#ifndef	F_SETLKW
		Fclose(obuf);
#else
		fseek(obuf, 0L, SEEK_SET);
#endif
		return(-1);
	}
	if (res != (FILE*)NULL)
		Fclose(res);
#ifndef	F_SETLKW
	Fclose(obuf);
#else
	fseek(obuf, 0L, SEEK_SET);
#endif
	alter(mailname);
	if (p == 1)
		printf(catgets(catd, CATSET, 155,
				"Held 1 message in %s\n"), mailname);
	else
		printf(catgets(catd, CATSET, 156,
				"Held %d messages in %s\n"), p, mailname);
	return(0);
}

/*
 * Save all of the undetermined messages at the top of "mbox"
 * Save all untouched messages back in the system mailbox.
 * Remove the system mailbox, if none saved there.
 */
void
quit()
{
	int mcount, p, modify, autohold, anystat, holdbit, nohold;
	FILE *ibuf = (FILE*)NULL, *obuf, *fbuf, *rbuf, *readstat = (FILE*)NULL, *abuf;
	struct message *mp;
	int c;
	char *tempQuit, *tempResid;
	struct stat minfo;
	char *mbox;

	/*
	 * If we are read only, we can't do anything,
	 * so just return quickly.
	 */
	if (readonly)
		return;
	/*
	 * If editing (not reading system mail box), then do the work
	 * in edstop()
	 */
	if (edit) {
		edstop();
		return;
	}

	/*
	 * See if there any messages to save in mbox.  If no, we
	 * can save copying mbox to /tmp and back.
	 *
	 * Check also to see if any files need to be preserved.
	 * Delete all untouched messages to keep them out of mbox.
	 * If all the messages are to be preserved, just exit with
	 * a message.
	 */

#ifndef	F_SETLKW
	fbuf = Fopen(mailname, "r");
#else
	fbuf = Fopen(mailname, "r+");
#endif
	if (fbuf == (FILE*)NULL) {
		if (errno == ENOENT)
			return;
		goto newmail;
	}
#ifndef	F_SETLKW
	if (fcntl_lock(fileno(fbuf), LOCK_EX) == -1) {
#else
	if (fcntl_lock(fileno(fbuf), F_WRLCK) == -1) {
#endif
nolock:
		perror(catgets(catd, CATSET, 157, "Unable to lock mailbox"));
		Fclose(fbuf);
		return;
	}
	if (dot_lock(mailname, fileno(fbuf), 1, stdout, ".") == -1)
		goto nolock;
	rbuf = (FILE *) NULL;
	if (fstat(fileno(fbuf), &minfo) >= 0 && minfo.st_size > mailsize) {
		printf(catgets(catd, CATSET, 158, "New mail has arrived.\n"));
		rbuf = Ftemp(&tempResid, "Rq", "w", 0600, 1);
		if (rbuf == (FILE*)NULL || fbuf == (FILE*)NULL)
			goto newmail;
#ifdef APPEND
		fseek(fbuf, (long)mailsize, SEEK_SET);
		while ((c = sgetc(fbuf)) != EOF)
			(void) sputc(c, rbuf);
#else
		p = minfo.st_size - mailsize;
		while (p-- > 0) {
			c = sgetc(fbuf);
			if (c == EOF)
				goto newmail;
			(void) sputc(c, rbuf);
		}
#endif
		Fclose(rbuf);
		if ((rbuf = Fopen(tempResid, "r")) == (FILE*)NULL)
			goto newmail;
		rm(tempResid);
		Ftfree(&tempResid);
	}

	/*
	 * Adjust the message flags in each message.
	 */

	anystat = 0;
	autohold = value("hold") != NULL;
	holdbit = autohold ? MPRESERVE : MBOX;
	nohold = MBOX|MSAVED|MDELETED|MPRESERVE;
	if (value("keepsave") != NULL)
		nohold &= ~MSAVED;
	for (mp = &message[0]; mp < &message[msgcount]; mp++) {
		if (mp->m_flag & MNEW) {
			mp->m_flag &= ~MNEW;
			mp->m_flag |= MSTATUS;
		}
		if (mp->m_flag & MSTATUS)
			anystat++;
		if ((mp->m_flag & MTOUCH) == 0)
			mp->m_flag |= MPRESERVE;
		if ((mp->m_flag & nohold) == 0)
			mp->m_flag |= holdbit;
	}
	modify = 0;
	if (Tflag != NULL) {
		if ((readstat = Fopen(Tflag, "w")) == (FILE*)NULL)
			Tflag = NULL;
	}
	for (c = 0, p = 0, mp = &message[0]; mp < &message[msgcount]; mp++) {
		if (mp->m_flag & MBOX)
			c++;
		if (mp->m_flag & MPRESERVE)
			p++;
		if (mp->m_flag & MODIFY)
			modify++;
		if (Tflag != NULL && (mp->m_flag & (MREAD|MDELETED)) != 0) {
			char *id;

			if ((id = hfield("message-id", mp)) != NULL ||
					(id = hfield("article-id", mp)) != NULL)
				fprintf(readstat, "%s\n", id);
		}
	}
	if (Tflag != NULL)
		Fclose(readstat);
	if (p == msgcount && !modify && !anystat) {
		printf(catgets(catd, CATSET, 159, "Held %d message%s in %s\n"),
			p, p == 1 ? catgets(catd, CATSET, 160, "")
			: catgets(catd, CATSET, 161, "s"),
			mailname);
		Fclose(fbuf);
		dot_unlock(mailname);
		return;
	}
	if (c == 0) {
		if (p != 0) {
#ifndef	F_SETLKW
			writeback(rbuf);
#else
			writeback(rbuf, fbuf);
#endif
			Fclose(fbuf);
			dot_unlock(mailname);
			return;
		}
		goto cream;
	}

	/*
	 * Create another temporary file and copy user's mbox file
	 * darin.  If there is no mbox, copy nothing.
	 * If he has specified "append" don't copy his mailbox,
	 * just copy saveable entries at the end.
	 */

	mbox = expand("&");
	mcount = c;
	if (value("append") == NULL) {
		if ((obuf = Ftemp(&tempQuit, "Rm", "w", 0600, 1)) == NULL) {
			perror(catgets(catd, CATSET, 162,
					"temporary mail quit file"));
			Fclose(fbuf);
			dot_unlock(mailname);
			return;
		}
		if ((ibuf = Fopen(tempQuit, "r")) == NULL) {
			perror(tempQuit);
			rm(tempQuit);
			Ftfree(&tempQuit);
			Fclose(obuf);
			Fclose(fbuf);
			dot_unlock(mailname);
			return;
		}
		rm(tempQuit);
		Ftfree(&tempQuit);
		if ((abuf = Fopen(mbox, "r")) != (FILE*)NULL) {
			while ((c = sgetc(abuf)) != EOF)
				(void) sputc(c, obuf);
			Fclose(abuf);
		}
		if (ferror(obuf)) {
			perror(catgets(catd, CATSET, 163,
					"temporary mail quit file"));
			Fclose(ibuf);
			Fclose(obuf);
			Fclose(fbuf);
			dot_unlock(mailname);
			return;
		}
		Fclose(obuf);
		close(creat(mbox, 0600));
		if ((obuf = Fopen(mbox, "r+")) == (FILE*)NULL) {
			perror(mbox);
			Fclose(ibuf);
			Fclose(fbuf);
			dot_unlock(mailname);
			return;
		}
	}
	else {
		if ((obuf = Fopen(mbox, "a")) == (FILE*)NULL) {
			perror(mbox);
			Fclose(fbuf);
			dot_unlock(mailname);
			return;
		}
		fchmod(fileno(obuf), 0600);
	}
	for (mp = &message[0]; mp < &message[msgcount]; mp++)
		if (mp->m_flag & MBOX)
			if (send_message(mp, obuf, saveignore,
						NULL, CONV_NONE, NULL) < 0) {
				perror(mbox);
				if (ibuf)
					Fclose(ibuf);
				Fclose(obuf);
				Fclose(fbuf);
				dot_unlock(mailname);
				return;
			}

	/*
	 * Copy the user's old mbox contents back
	 * to the end of the stuff we just saved.
	 * If we are appending, this is unnecessary.
	 */

	if (value("append") == NULL) {
		rewind(ibuf);
		c = sgetc(ibuf);
		while (c != EOF) {
			(void) sputc(c, obuf);
			if (ferror(obuf))
				break;
			c = sgetc(ibuf);
		}
		Fclose(ibuf);
		fflush(obuf);
	}
	trunc(obuf);
	if (ferror(obuf)) {
		perror(mbox);
		Fclose(obuf);
		Fclose(fbuf);
		dot_unlock(mailname);
		return;
	}
	Fclose(obuf);
	if (mcount == 1)
		printf(catgets(catd, CATSET, 164, "Saved 1 message in mbox\n"));
	else
		printf(catgets(catd, CATSET, 165,
				"Saved %d messages in mbox\n"), mcount);

	/*
	 * Now we are ready to copy back preserved files to
	 * the system mailbox, if any were requested.
	 */

	if (p != 0) {
#ifndef	F_SETLKW
		writeback(rbuf);
#else
		writeback(rbuf, fbuf);
#endif
		Fclose(fbuf);
		dot_unlock(mailname);
		return;
	}

	/*
	 * Finally, remove his /usr/mail file.
	 * If new mail has arrived, copy it back.
	 */

cream:
	if (rbuf != (FILE*)NULL) {
#ifndef	F_SETLKW
		abuf = Fopen(mailname, "r+");
		if (abuf == (FILE*)NULL)
			goto newmail;
#else
		abuf = fbuf;
		fseek(abuf, 0L, SEEK_SET);
#endif
		while ((c = sgetc(rbuf)) != EOF)
			(void) sputc(c, abuf);
		Fclose(rbuf);
		trunc(abuf);
#ifndef	F_SETLKW
		Fclose(abuf);
#endif
		alter(mailname);
		Fclose(fbuf);
		dot_unlock(mailname);
		return;
	}
	demail();
	Fclose(fbuf);
	dot_unlock(mailname);
	return;

newmail:
	printf(catgets(catd, CATSET, 166, "Thou hast new mail.\n"));
	if (fbuf != (FILE*)NULL) {
		Fclose(fbuf);
		dot_unlock(mailname);
	}
}

/*
 * Terminate an editing session by attempting to write out the user's
 * file from the temporary.  Save any new stuff appended to the file.
 */
static void
edstop()
{
	int gotcha, c;
	struct message *mp;
	FILE *obuf, *ibuf, *readstat = (FILE*)NULL;
	struct stat statb;

	if (readonly)
		return;
	holdsigs();
	if (Tflag != NULL) {
		if ((readstat = Fopen(Tflag, "w")) == (FILE*)NULL)
			Tflag = NULL;
	}
	for (mp = &message[0], gotcha = 0; mp < &message[msgcount]; mp++) {
		if (mp->m_flag & MNEW) {
			mp->m_flag &= ~MNEW;
			mp->m_flag |= MSTATUS;
		}
		if (mp->m_flag & (MODIFY|MDELETED|MSTATUS))
			gotcha++;
		if (Tflag != NULL && (mp->m_flag & (MREAD|MDELETED)) != 0) {
			char *id;

			if ((id = hfield("message-id", mp)) != NULL ||
					(id = hfield("article-id", mp)) != NULL)
				fprintf(readstat, "%s\n", id);
		}
	}
	if (Tflag != NULL)
		Fclose(readstat);
	if (!gotcha || Tflag != NULL)
		goto done;
	ibuf = (FILE *)NULL;
	if (stat(mailname, &statb) >= 0 && statb.st_size > mailsize) {
		char *tempname;

		if ((obuf = Ftemp(&tempname, "mbox.", "w", 0600, 1)) == NULL) {
			perror(catgets(catd, CATSET, 167, "tmpfile"));
			relsesigs();
			reset(0);
		}
		if ((ibuf = Fopen(mailname, "r")) == (FILE *)NULL) {
			perror(mailname);
			Fclose(obuf);
			rm(tempname);
			Ftfree(&tempname);
			relsesigs();
			reset(0);
		}
		fseek(ibuf, (long)mailsize, SEEK_SET);
		while ((c = sgetc(ibuf)) != EOF)
			(void) sputc(c, obuf);
		Fclose(ibuf);
		Fclose(obuf);
		if ((ibuf = Fopen(tempname, "r")) == (FILE*)NULL) {
			perror(tempname);
			rm(tempname);
			Ftfree(&tempname);
			relsesigs();
			reset(0);
		}
		rm(tempname);
		Ftfree(&tempname);
	}
	printf(catgets(catd, CATSET, 168, "\"%s\" "), mailname);
	fflush(stdout);
	if ((obuf = Fopen(mailname, "r+")) == (FILE*)NULL) {
		perror(mailname);
		relsesigs();
		reset(0);
	}
	trunc(obuf);
	c = 0;
	for (mp = &message[0]; mp < &message[msgcount]; mp++) {
		if ((mp->m_flag & MDELETED) != 0)
			continue;
		c++;
		if (send_message(mp, obuf, (struct ignoretab *) NULL,
					NULL, CONV_NONE, NULL) < 0) {
			perror(mailname);
			relsesigs();
			reset(0);
		}
	}
	gotcha = (c == 0 && ibuf == (FILE*)NULL);
	if (ibuf != (FILE*)NULL) {
		while ((c = sgetc(ibuf)) != EOF)
			(void) sputc(c, obuf);
		Fclose(ibuf);
	}
	fflush(obuf);
	if (ferror(obuf)) {
		perror(mailname);
		relsesigs();
		reset(0);
	}
	Fclose(obuf);
	if (gotcha && value("emptybox") == NULL) {
		rm(mailname);
		printf(value("bsdcompat") ?
				catgets(catd, CATSET, 169, "removed\n") :
				catgets(catd, CATSET, 211, "removed.\n"));
	} else
		printf(value("bsdcompat") ?
				catgets(catd, CATSET, 170, "complete\n") :
				catgets(catd, CATSET, 212, "updated.\n"));
	fflush(stdout);

done:
	relsesigs();
}
