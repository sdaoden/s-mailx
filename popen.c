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
static char sccsid[] = "@(#)popen.c	1.7 (gritter) 11/17/01";
#endif
#endif /* not lint */

#include "rcv.h"
#ifdef	HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#include <errno.h>
#include "extern.h"

#define READ 0
#define WRITE 1

struct fp {
	FILE *fp;
	int pipe;
	int pid;
	struct fp *link;
};
static struct fp *fp_head;

struct child {
	int pid;
	char done;
	char free;
	int status;
	struct child *link;
};
static struct child *child;
static struct child *findchild __P((int));
static void delchild __P((struct child *));
static int file_pid __P((FILE *));

/*
 * Provide BSD-like signal() on all systems.
 */
signal_handler_t
safe_signal(signum, handler)
signal_handler_t handler;
{
	struct sigaction nact, oact;

	nact.sa_handler = handler;
	sigemptyset(&nact.sa_mask);
	nact.sa_flags = 0;
#ifdef	SA_RESTART
	nact.sa_flags |= SA_RESTART;
#endif
	if (sigaction(signum, &nact, &oact) != 0)
		return SIG_ERR;
	return oact.sa_handler;
}

FILE *
safe_fopen(file, mode)
	char *file, *mode;
{
	int  omode, fd;

	if (!strcmp(mode, "r")) {
		omode = O_RDONLY;
	} else if (!strcmp(mode, "w")) {
		omode = O_WRONLY | O_CREAT | O_EXCL;
	} else if (!strcmp(mode, "a")) {
		omode = O_WRONLY | O_APPEND | O_CREAT;
	} else if (!strcmp(mode, "a+")) {
		omode = O_RDWR | O_APPEND;
	} else if (!strcmp(mode, "r+")) {
		omode = O_RDWR;
	} else if (!strcmp(mode, "w+")) {
		omode = O_RDWR   | O_CREAT | O_EXCL;
	} else {
		fprintf(stderr,
			"Internal error: bad stdio open mode %s\n", mode);
		errno = EINVAL;
		return (FILE *)NULL;
	}

	if ((fd = open(file, omode, 0666)) < 0)
		return (FILE *)NULL;
	return fdopen(fd, mode);
}

FILE *
Fopen(file, mode)
	char *file, *mode;
{
	FILE *fp;

	if ((fp = safe_fopen(file, mode)) != (FILE *)NULL) {
		register_file(fp, 0, 0);
		(void) fcntl(fileno(fp), F_SETFD, 1);
	}
	return fp;
}

FILE *
Fdopen(fd, mode)
	int fd;
	char *mode;
{
	FILE *fp;

	if ((fp = fdopen(fd, mode)) != (FILE *)NULL) {
		register_file(fp, 0, 0);
		(void) fcntl(fileno(fp), F_SETFD, 1);
	}
	return fp;
}

int
Fclose(fp)
	FILE *fp;
{
	unregister_file(fp);
	return fclose(fp);
}

FILE *
Popen(cmd, mode, shell, newfd1)
char *cmd, *mode, *shell;
{
	int p[2];
	int myside, hisside, fd0, fd1;
	int pid;
	char mod[2] = { '0', '\0' };
	sigset_t nset;
	FILE *fp;

	if (pipe(p) < 0)
		return (FILE *)NULL;
	(void) fcntl(p[READ], F_SETFD, 1);
	(void) fcntl(p[WRITE], F_SETFD, 1);
	if (*mode == 'r') {
		myside = p[READ];
		fd0 = -1;
		hisside = fd1 = p[WRITE];
		mod[0] = *mode;
	} else if (*mode == 'W') {
		myside = p[WRITE];
		hisside = fd0 = p[READ];
		fd1 = newfd1;
		mod[0] = 'w';
	} else {
		myside = p[WRITE];
		hisside = fd0 = p[READ];
		fd1 = -1;
		mod[0] = 'w';
	}
	sigemptyset(&nset);
	if (shell == NULL) {
		pid = start_command(cmd, &nset, fd0, fd1, NULL, NULL, NULL);
	} else {
		pid = start_command(shell, &nset, fd0, fd1, "-c", cmd, NULL);
	}
	if (pid < 0) {
		close(p[READ]);
		close(p[WRITE]);
		return (FILE *)NULL;
	}
	(void) close(hisside);
	if ((fp = fdopen(myside, mod)) != (FILE *)NULL)
		register_file(fp, 1, pid);
	return fp;
}

int
Pclose(ptr)
	FILE *ptr;
{
	int i;
	sigset_t nset, oset;

	i = file_pid(ptr);
	if (i < 0)
		return 0;
	unregister_file(ptr);
	(void) fclose(ptr);
	sigemptyset(&nset);
	sigaddset(&nset, SIGINT);
	sigaddset(&nset, SIGHUP);
	sigprocmask(SIG_BLOCK, &nset, &oset);
	i = wait_child(i);
	sigprocmask(SIG_SETMASK, &oset, (sigset_t *)NULL);
	return i;
}

void
close_all_files()
{

	while (fp_head)
		if (fp_head->pipe)
			(void) Pclose(fp_head->fp);
		else
			(void) Fclose(fp_head->fp);
}

void
register_file(fp, pipe, pid)
	FILE *fp;
	int pipe, pid;
{
	struct fp *fpp;

	fpp = (struct fp*)smalloc(sizeof *fpp);
	fpp->fp = fp;
	fpp->pipe = pipe;
	fpp->pid = pid;
	fpp->link = fp_head;
	fp_head = fpp;
}

void
unregister_file(fp)
	FILE *fp;
{
	struct fp **pp, *p;

	for (pp = &fp_head; (p = *pp) != (struct fp *)NULL; pp = &p->link)
		if (p->fp == fp) {
			*pp = p->link;
			free((char *) p);
			return;
		}
	panic("Invalid file pointer");
}

static int
file_pid(fp)
	FILE *fp;
{
	struct fp *p;

	for (p = fp_head; p; p = p->link)
		if (p->fp == fp)
			return (p->pid);
	return -1;
}

/*
 * Run a command without a shell, with optional arguments and splicing
 * of stdin and stdout.  The command name can be a sequence of words.
 * Signals must be handled by the caller.
 * "Mask" contains the signals to ignore in the new process.
 * SIGINT is enabled unless it's in the mask.
 */
/*VARARGS4*/
int
run_command(cmd, mask, infd, outfd, a0, a1, a2)
	char *cmd;
	sigset_t *mask;
	int infd, outfd;
	char *a0, *a1, *a2;
{
	int pid;

	if ((pid = start_command(cmd, mask, infd, outfd, a0, a1, a2)) < 0)
		return -1;
	return wait_command(pid);
}

/*VARARGS4*/
int
start_command(cmd, mask, infd, outfd, a0, a1, a2)
	char *cmd;
	sigset_t *mask;
	int infd, outfd;
	char *a0, *a1, *a2;
{
	int pid;

	if ((pid = fork()) < 0) {
		perror("fork");
		return -1;
	}
	if (pid == 0) {
		char *argv[100];
		int i = getrawlist(cmd, argv, sizeof argv / sizeof *argv);

		if ((argv[i++] = a0) != NULL &&
		    (argv[i++] = a1) != NULL &&
		    (argv[i++] = a2) != NULL)
			argv[i] = NULL;
		prepare_child(mask, infd, outfd);
		execvp(argv[0], argv);
		perror(argv[0]);
		_exit(1);
	}
	return pid;
}

void
prepare_child(nset, infd, outfd)
	sigset_t *nset;
	int infd, outfd;
{
	int i;
	sigset_t fset;

	/*
	 * All file descriptors other than 0, 1, and 2 are supposed to be
	 * close-on-exec.
	 */
	if (infd >= 0)
		dup2(infd, 0);
	if (outfd >= 0)
		dup2(outfd, 1);
	if (nset) {
		for (i = 1; i <= NSIG; i++)
			if (sigismember(nset, i))
				(void) safe_signal(i, SIG_IGN);
		if (!sigismember(nset, SIGINT))
			(void) safe_signal(SIGINT, SIG_DFL);
	}
	sigfillset(&fset);
	(void) sigprocmask(SIG_UNBLOCK, &fset, (sigset_t *)NULL);
}

int
wait_command(pid)
	int pid;
{

	if (wait_child(pid) < 0) {
		printf("Fatal error in process.\n");
		return -1;
	}
	return 0;
}

static struct child *
findchild(pid)
	int pid;
{
	struct child **cpp;

	for (cpp = &child; *cpp != (struct child *)NULL && (*cpp)->pid != pid;
	     cpp = &(*cpp)->link)
			;
	if (*cpp == (struct child *)NULL) {
		*cpp = (struct child *) smalloc(sizeof (struct child));
		(*cpp)->pid = pid;
		(*cpp)->done = (*cpp)->free = 0;
		(*cpp)->link = (struct child *)NULL;
	}
	return *cpp;
}

static void
delchild(cp)
	struct child *cp;
{
	struct child **cpp;

	for (cpp = &child; *cpp != cp; cpp = &(*cpp)->link)
		;
	*cpp = cp->link;
	free((char *) cp);
}

/*ARGSUSED*/
RETSIGTYPE
sigchild(signo)
	int signo;
{
	int pid;
	int status;
	struct child *cp;

	while ((pid = waitpid(-1, (int*)&status, WNOHANG)) > 0) {
		cp = findchild(pid);
		if (cp->free)
			delchild(cp);
		else {
			cp->done = 1;
			cp->status = status;
		}
	}
}

int wait_status;

/*
 * Mark a child as don't care.
 */
void
free_child(pid)
	int pid;
{
	sigset_t nset, oset;
	struct child *cp = findchild(pid);
	sigemptyset(&nset);
	sigaddset(&nset, SIGCHLD);
	sigprocmask(SIG_BLOCK, &nset, &oset);

	if (cp->done)
		delchild(cp);
	else
		cp->free = 1;
	sigprocmask(SIG_SETMASK, &oset, (sigset_t *)NULL);
}

/*
 * Wait for a specific child to die.
 */
#if 0
/*
 * This version is correct code, but causes harm on some loosing
 * systems. So we use the second one instead.
 */
int
wait_child(pid)
	int pid;
{
	sigset_t nset, oset;
	struct child *cp = findchild(pid);
	sigemptyset(&nset);
	sigaddset(&nset, SIGCHLD);
	sigprocmask(SIG_BLOCK, &nset, &oset);

	while (!cp->done)
		sigsuspend(&oset);
	wait_status = cp->status;
	delchild(cp);
	sigprocmask(SIG_SETMASK, &oset, (sigset_t *)NULL);

	if (WIFEXITED(wait_status) && (WEXITSTATUS(wait_status) == 0))
		return 0;
	return -1;
}
#endif
int
wait_child(pid)
int pid;
{
	pid_t term;
	struct child *cp;
	struct sigaction nact, oact;
	
	nact.sa_handler = SIG_DFL;
	sigemptyset(&nact.sa_mask);
	nact.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &nact, &oact);
	
	cp = findchild(pid);
	if (!cp->done) {
		do {
			term = wait(&wait_status);
			if (term == 0 || term == -1)
			break;
			cp = findchild(term);
			if (cp->free || term == pid) {
				delchild(cp);
			} else {
				cp->done = 1;
				cp->status = wait_status;
			}
		} while (term != pid);
	} else {
		wait_status = cp->status;
		delchild(cp);
	}
	
	sigaction(SIGCHLD, &oact, NULL);
	/*
	 * Make sure no zombies are left.
	 */
	sigchild(SIGCHLD);
	
	if (WIFEXITED(wait_status) && (WEXITSTATUS(wait_status) == 0))
		return 0;
	return -1;
}
