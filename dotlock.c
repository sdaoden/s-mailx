/*	$Id: dotlock.c,v 1.8 2000/05/29 00:29:22 gunnar Exp $	*/
/*	OpenBSD: dotlock.c,v 1.1 1996/06/08 19:48:19 christos Exp 	*/
/*	NetBSD: dotlock.c,v 1.1 1996/06/08 19:48:19 christos Exp 	*/

/*
 * Copyright (c) 1996 Christos Zoulas.  All rights reserved.
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
 *	This product includes software developed by Christos Zoulas.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lint
#if 0
static char rcsid[]  = "OpenBSD: dotlock.c,v 1.1 1996/06/08 19:48:19 christos Exp";
#else
static char rcsid[]  = "@(#)$Id: dotlock.c,v 1.8 2000/05/29 00:29:22 gunnar Exp $";
#endif
#endif

#include "rcv.h"
#include <sys/types.h>
#ifdef	HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif

#include <errno.h>

#include "extern.h"

#ifndef O_SYNC
#define O_SYNC	0
#endif

/* Check if we can write a lock file at all */
int
maildir_access(fname)
char *fname;
{
	char path[MAXPATHLEN];
	char *p;

	strcpy(path, fname);
	p = strrchr(path, '/');
	if (p != NULL)
		*p = '\0';
	if (p == NULL || *path == '\0')
		strcpy(path, ".");
	return access(path, R_OK|W_OK|X_OK);
}

/*
 * Set the gid if the path is in the normal mail spool
 */
static int perhaps_setgid (name, gid)
char *name;
gid_t gid;
{
	char safepath[]= _PATH_MAILDIR;

	if (strncmp (name, safepath, sizeof (safepath)-1) ||
	    strchr (name + sizeof (safepath), '/'))
	{
		return 0;
	}
	return (setgid (gid));
}


static int create_exclusive __P((const char *));

#define	APID_SZ	40	/* sufficient for storign 128 bits pids */
/*
 * Create a unique file. O_EXCL does not really work over NFS so we follow
 * the following trick: [Inspired by  S.R. van den Berg]
 *
 * - make a mostly unique filename and try to create it.
 * - link the unique filename to our target
 * - get the link count of the target
 * - unlink the mostly unique filename
 * - if the link count was 2, then we are ok; else we've failed.
 */
static int
create_exclusive(fname)
	const char *fname;
{
	char path[MAXPATHLEN];
	char *hostname;
	char apid[APID_SZ];
	const char *ptr;
	time_t t;
	pid_t pid;
	size_t ntries, cookie;
	int fd, serrno, cc;
	struct stat st;
#if defined (HAVE_UNAME)
	struct utsname ut;
#elif defined (HAVE_GETHOSTNAME)
	char host__name[MAXHOSTNAMELEN];
#endif

	time(&t);
#if defined (HAVE_UNAME)
	uname(&ut);
	hostname = ut.nodename;
#elif defined (HAVE_GETHOSTNAME)
	gethostname(host__name, MAXHOSTNAMELEN);
	hostname = host__name;
#else
	hostname = "unknown";
#endif
	pid = getpid();

	cookie = (int)pid ^ (int)t;

	/*
	 * We generate a semi-unique filename, from hostname.(pid ^ usec)
	 */
	if ((ptr = strrchr(fname, '/')) == NULL)
		ptr = fname;
	else
		ptr++;

	(void) snprintf(path, sizeof path, "%.*s.%s.%x", 
	    ptr - fname, fname, hostname, cookie);

	/*
	 * We try to create the unique filename.
	 */
	for (ntries = 0; ntries < 5; ntries++) {
		perhaps_setgid(path, effectivegid);
		fd = open(path, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL|O_SYNC, 0);
		setgid(realgid);
		if (fd != -1) {
			snprintf(apid, APID_SZ, "%d", (int)getpid());
			write(fd, apid, strlen(apid));
			(void) close(fd);
			break;
		}
		else if (errno == EEXIST)
			continue;
		else
			return -1;
	}
	/*
	 * We link the path to the name
	 */
	perhaps_setgid(fname, effectivegid);
	cc = link(path, fname);
	setgid(realgid);
   
	if (cc == -1)
		goto bad;

	/*
	 * Note that we stat our own exclusively created name, not the
	 * destination, since the destination can be affected by others.
	 */
	if (stat(path, &st) == -1)
		goto bad;

	perhaps_setgid(fname, effectivegid);
	(void) unlink(path);
	setgid(realgid);

	/*
	 * If the number of links was two (one for the unique file and one
	 * for the lock), we've won the race
	 */
	if (st.st_nlink != 2) {
		errno = EEXIST;
		return -1;
	}
	return 0;

bad:
	serrno = errno;
	(void) unlink(path);
	errno = serrno;
	return -1;
}

int
dot_lock(fname, pollinterval, fp, msg)
	const char *fname;	/* Pathname to lock */
	int pollinterval;	/* Interval to check for lock, -1 return */
	FILE *fp;		/* File to print message */
	const char *msg;	/* Message to print */
{
	char path[MAXPATHLEN];
	sigset_t nset, oset;
	int i;

	if (maildir_access(fname) != 0)
		return 0;

	sigemptyset(&nset);
	sigaddset(&nset, SIGHUP);
	sigaddset(&nset, SIGINT);
	sigaddset(&nset, SIGQUIT);
	sigaddset(&nset, SIGTERM);
	sigaddset(&nset, SIGTTIN);
	sigaddset(&nset, SIGTTOU);
	sigaddset(&nset, SIGTSTP);
	sigaddset(&nset, SIGCHLD);

	(void) snprintf(path, sizeof(path), "%s.lock", fname);

	for (i=0;i<15;i++) {
		(void) sigprocmask(SIG_BLOCK, &nset, &oset);
		if (create_exclusive(path) != -1) {
			(void) sigprocmask(SIG_SETMASK, &oset, NULL);
			return 0;
		}
		else
			(void) sigprocmask(SIG_SETMASK, &oset, NULL);

		if (errno != EEXIST)
			return -1;

		if (fp && msg)
		    (void) fputs(msg, fp);

		if (pollinterval) {
			if (pollinterval == -1) {
				errno = EEXIST;
				return -1;
			}
			sleep(pollinterval);
		}
	}
        fprintf(stderr,"%s seems a stale lock? Need to be removed by hand?\n",path);
        return -1;
}

void
dot_unlock(fname)
	const char *fname;
{
	char path[MAXPATHLEN];

	if (maildir_access(fname) != 0)
		return;

	(void) snprintf(path, sizeof(path), "%s.lock", fname);
	perhaps_setgid(path, effectivegid);
	(void) unlink(path);
	setgid(realgid);
}
