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
static char sccsid[] = "@(#)temp.c	1.5 (gritter) 10/19/00";
#endif
#endif /* not lint */

#include "rcv.h"
#include <errno.h>
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * Give names to all the temporary files that we will need.
 */

char	*tempMail;
char	*tempQuit;
char	*tempEdit;
char	*tempResid;
char	*tempMesg;
char	*tmpdir;

#ifdef	HAVE_MKSTEMP
/*
 * This is mainly done because of glibc 2.2's fascist warnings.
 */
char *
maket(prefix)
char *prefix;
{
	int fd;
	char *fn = (char *)smalloc(strlen(tmpdir) + strlen(prefix) + 8);
	strcpy(fn, tmpdir);
	strcat(fn, "/");
	strcat(fn, prefix);
	strcat(fn, "XXXXXX");
	if ((fd = mkstemp(fn)) < 0) {
		perror(fn);
		exit(1);
	}
	close(fd);
	unlink(fn);
	return fn;
}
#endif	/* HAVE_MKSTEMP */

void
tinit()
{
	char *cp;

	if ((tmpdir = getenv("TMPDIR")) == NULL) {
		tmpdir = PATH_TMP;
	}

#ifdef	HAVE_MKSTEMP
	tempMail  = maket("Rs");
	tempResid = maket("Rq");
	tempQuit  = maket("Rm");
	tempEdit  = maket("Re");
	tempMesg  = maket("Rx");
#else	/* !HAVE_MKSTEMP */
	tempMail  = tempnam (tmpdir, "Rs");
	tempResid = tempnam (tmpdir, "Rq");
	tempQuit  = tempnam (tmpdir, "Rm");
	tempEdit  = tempnam (tmpdir, "Re");
	tempMesg  = tempnam (tmpdir, "Rx");
#endif	/* !HAVE_MKSTEMP */

	/*
	 * It's okay to call savestr in here because main will
	 * do a spreserve() after us.
	 */
	if (myname != NOSTR) {
		if (getuserid(myname) < 0) {
			printf("\"%s\" is not a user of this system\n",
			    myname);
			exit(1);
		}
	} else {
		if ((cp = username()) == NOSTR) {
			myname = "nobody";
			if (rcvmode)
				exit(1);
		} else
			myname = savestr(cp);
	}
	if ((cp = getenv("HOME")) == NOSTR)
		cp = ".";
	homedir = savestr(cp);
	if (debug)
		printf("user = %s, homedir = %s\n", myname, homedir);
}
