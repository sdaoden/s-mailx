/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ More user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <sys/wait.h>

static int	save1(char *str, int domark, char const *cmd,
			struct ignoretab *ignore, int convert,
			int sender_record, int domove);
static char *	snarf(char *linebuf, bool_t *flag, bool_t usembox);
static int	delm(int *msgvec);
#ifdef HAVE_DEBUG
static void	clob1(int n);
#endif
static int	ignore1(char **list, struct ignoretab *tab, char const *which);
static int	igshow(struct ignoretab *tab, char const *which);
static int	igcomp(const void *l, const void *r);
static void	unignore_one(const char *name, struct ignoretab *tab);
static int	unignore1(char **list, struct ignoretab *tab,
			char const *which);

/*
 * If any arguments were given, go to the next applicable argument
 * following dot, otherwise, go to the next applicable message.
 * If given as first command with no arguments, print first message.
 */
FL int
next(void *v)
{
	int *msgvec = v;
	struct message *mp;
	int *ip, *ip2;
	int list[2], mdot;

	if (*msgvec != 0) {

		/*
		 * If some messages were supplied, find the
		 * first applicable one following dot using
		 * wrap around.
		 */

		mdot = dot - &message[0] + 1;

		/*
		 * Find the first message in the supplied
		 * message list which follows dot.
		 */

		for (ip = msgvec; *ip != 0; ip++) {
#ifdef	_CRAY
/*
 * Work around an optimizer bug in Cray Standard C Version 4.0.3  (057126).
 * Otherwise, SIGFPE is received when mb.mb_threaded != 0.
 */
#pragma _CRI suppress ip
#endif	/* _CRAY */
			if (mb.mb_threaded ? message[*ip-1].m_threadpos >
						dot->m_threadpos :
					*ip > mdot)
				break;
		}
		if (*ip == 0)
			ip = msgvec;
		ip2 = ip;
		do {
			mp = &message[*ip2 - 1];
			if ((mp->m_flag & (MDELETED|MHIDDEN)) == 0) {
				setdot(mp);
				goto hitit;
			}
			if (*ip2 != 0)
				ip2++;
			if (*ip2 == 0)
				ip2 = msgvec;
		} while (ip2 != ip);
		printf(tr(21, "No messages applicable\n"));
		return(1);
	}

	/*
	 * If this is the first command, select message 1.
	 * Note that this must exist for us to get here at all.
	 */

	if (!sawcom) {
		if (msgCount == 0)
			goto ateof;
		goto hitit;
	}

	/*
	 * Just find the next good message after dot, no
	 * wraparound.
	 */

	if (mb.mb_threaded == 0) {
		for (mp = dot + did_print_dot; mp < &message[msgCount]; mp++)
			if ((mp->m_flag & (MDELETED|MSAVED|MHIDDEN)) == 0)
				break;
	} else {
		mp = dot;
		if (did_print_dot)
			mp = next_in_thread(mp);
		while (mp && mp->m_flag & (MDELETED|MSAVED|MHIDDEN))
			mp = next_in_thread(mp);
	}
	if (mp == NULL || mp >= &message[msgCount]) {
ateof:
		printf(tr(22, "At EOF\n"));
		return(0);
	}
	setdot(mp);
hitit:
	/*
	 * Print dot.
	 */

	list[0] = dot - &message[0] + 1;
	list[1] = 0;
	return(type(list));
}

/*
 * Save a message in a file.  Mark the message as saved
 * so we can discard when the user quits.
 */
FL int
save(void *v)
{
	char *str = v;

	return save1(str, 1, "save", saveignore, SEND_MBOX, 0, 0);
}

FL int
Save(void *v)
{
	char *str = v;

	return save1(str, 1, "save", saveignore, SEND_MBOX, 1, 0);
}

/*
 * Copy a message to a file without affected its saved-ness
 */
FL int
copycmd(void *v)
{
	char *str = v;

	return save1(str, 0, "copy", saveignore, SEND_MBOX, 0, 0);
}

FL int
Copycmd(void *v)
{
	char *str = v;

	return save1(str, 0, "copy", saveignore, SEND_MBOX, 1, 0);
}

/*
 * Move a message to a file.
 */
FL int
cmove(void *v)
{
	char *str = v;

	return save1(str, 0, "move", saveignore, SEND_MBOX, 0, 1);
}

FL int
cMove(void *v)
{
	char *str = v;

	return save1(str, 0, "move", saveignore, SEND_MBOX, 1, 1);
}

/*
 * Decrypt and copy a message to a file.
 */
FL int
cdecrypt(void *v)
{
	char *str = v;

	return save1(str, 0, "decrypt", saveignore, SEND_DECRYPT, 0, 0);
}

FL int
cDecrypt(void *v)
{
	char *str = v;

	return save1(str, 0, "decrypt", saveignore, SEND_DECRYPT, 1, 0);
}

/*
 * Save/copy the indicated messages at the end of the passed file name.
 * If mark is true, mark the message "saved."
 */
static int
save1(char *str, int domark, char const *cmd, struct ignoretab *ignoret,
	int convert, int sender_record, int domove)
{
	off_t mstats[2], tstats[2];
	struct stat st;
	int newfile = 0, compressed = 0, success = 1, last = 0, *msgvec, *ip;
	struct message *mp;
	char *file = NULL, *cp, *cq;
	char const *disp = "";
	FILE *obuf;
	enum protocol prot;
	bool_t f;

	/*LINTED*/
	msgvec = (int *)salloc((msgCount + 2) * sizeof *msgvec);
	if (sender_record) {
		for (cp = str; *cp && blankchar(*cp); cp++)
			;
		f = (*cp != '\0');
	} else {
		if ((file = snarf(str, &f, convert != SEND_TOFILE)) == NULL)
			return(1);
	}
	if (!f) {
		*msgvec = first(0, MMNORM);
		if (*msgvec == 0) {
			if (inhook)
				return 0;
			printf(tr(23, "No messages to %s.\n"), cmd);
			return(1);
		}
		msgvec[1] = 0;
	} else if (getmsglist(str, msgvec, 0) < 0)
		return(1);
	if (*msgvec == 0) {
		if (inhook)
			return 0;
		printf("No applicable messages.\n");
		return 1;
	}
	if (sender_record) {
		if ((cp = nameof(&message[*msgvec - 1], 0)) == NULL) {
			printf(tr(24,
				"Cannot determine message sender to %s.\n"),
				cmd);
			return 1;
		}
		for (cq = cp; *cq && *cq != '@'; cq++);
		*cq = '\0';
		if (ok_blook(outfolder)) {
			size_t sz = strlen(cp) + 1;
			file = salloc(sz + 1);
			file[0] = '+';
			memcpy(file + 1, cp, sz);
		} else
			file = cp;
	}
	if ((file = expand(file)) == NULL)
		return (1);
	prot = which_protocol(file);
	if (prot != PROTO_IMAP) {
		if (access(file, 0) >= 0) {
			newfile = 0;
			disp = tr(25, "[Appended]");
		} else {
			newfile = 1;
			disp = tr(26, "[New file]");
		}
	}
	if ((obuf = convert == SEND_TOFILE ? Fopen(file, "a+") :
			Zopen(file, "a+", &compressed)) == NULL) {
		if ((obuf = convert == SEND_TOFILE ? Fopen(file, "wx") :
				Zopen(file, "wx", &compressed)) == NULL) {
			perror(file);
			return(1);
		}
	} else {
		if (compressed) {
			newfile = 0;
			disp = tr(25, "[Appended]");
		}
		if (!newfile && fstat(fileno(obuf), &st) &&
				S_ISREG(st.st_mode) &&
				fseek(obuf, -2L, SEEK_END) == 0) {
			char buf[2];
			int prependnl = 0;

			switch (fread(buf, sizeof *buf, 2, obuf)) {
			case 2:
				if (buf[1] != '\n') {
					prependnl = 1;
					break;
				}
				/*FALLTHRU*/
			case 1:
				if (buf[0] != '\n')
					prependnl = 1;
				break;
			default:
				if (ferror(obuf)) {
					perror(file);
					return(1);
				}
				prependnl = 0;
			}
			fflush(obuf);
			if (prependnl) {
				putc('\n', obuf);
				fflush(obuf);
			}
		}
	}

	tstats[0] = tstats[1] = 0;
	imap_created_mailbox = 0;
	srelax_hold();
	for (ip = msgvec; *ip && ip-msgvec < msgCount; ip++) {
		mp = &message[*ip - 1];
		if (prot == PROTO_IMAP &&
				ignoret[0].i_count == 0 &&
				ignoret[1].i_count == 0
#ifdef HAVE_IMAP /* TODO revisit */
				&& imap_thisaccount(file)
#endif
		) {
#ifdef HAVE_IMAP
			if (imap_copy(mp, *ip, file) == STOP)
#endif
				goto jferr;
#ifdef HAVE_IMAP
			mstats[0] = -1;
			mstats[1] = mp->m_xsize;
#endif
		} else if (sendmp(mp, obuf, ignoret, NULL,
					convert, mstats) < 0) {
			perror(file);
			goto jferr;
		}
		srelax();
		touch(mp);
		if (domark)
			mp->m_flag |= MSAVED;
		if (domove) {
			mp->m_flag |= MDELETED|MSAVED;
			last = *ip;
		}
		tstats[0] += mstats[0];
		tstats[1] += mstats[1];
	}
	fflush(obuf);
	if (ferror(obuf)) {
		perror(file);
jferr:
		success = 0;
	}
	if (Fclose(obuf) != 0)
		success = 0;
	srelax_rele();

	if (success) {
		if (prot == PROTO_IMAP || prot == PROTO_MAILDIR) {
			disp = (
#ifdef HAVE_IMAP
				((prot == PROTO_IMAP) && disconnected(file))
				? "[Queued]" :
#endif
				(imap_created_mailbox ? "[New file]"
					: "[Appended]"));
		}
		printf("\"%s\" %s ", file, disp);
		if (tstats[0] >= 0)
			printf("%lu", (long)tstats[0]);
		else
			printf(tr(27, "binary"));
		printf("/%lu\n", (long)tstats[1]);
	} else if (domark) {
		for (ip = msgvec; *ip && ip-msgvec < msgCount; ip++) {
			mp = &message[*ip - 1];
			mp->m_flag &= ~MSAVED;
		}
	} else if (domove) {
		for (ip = msgvec; *ip && ip-msgvec < msgCount; ip++) {
			mp = &message[*ip - 1];
			mp->m_flag &= ~(MSAVED|MDELETED);
		}
	}
	if (domove && last && success) {
		setdot(&message[last-1]);
		last = first(0, MDELETED);
		setdot(&message[last ? last-1 : 0]);
	}
	return(success == 0);
}

/*
 * Write the indicated messages at the end of the passed
 * file name, minus header and trailing blank line.
 * This is the MIME save function.
 */
FL int
cwrite(void *v)
{
	char *str = v;

	if (str == NULL || *str == '\0')
		str = savestr("/dev/null");
	return (save1(str, 0, "write", allignore, SEND_TOFILE, 0, 0));
}

/*
 * Snarf the file from the end of the command line and
 * return a pointer to it.  If there is no file attached,
 * return the mbox file.  Put a null in front of the file
 * name so that the message list processing won't see it,
 * unless the file name is the only thing on the line, in
 * which case, return 0 in the reference flag variable.
 */
static char *
snarf(char *linebuf, bool_t *flag, bool_t usembox)
{
	char *cp;

	if ((cp = laststring(linebuf, flag, 0)) == NULL) {
		if (usembox) {
			*flag = FAL0;
			cp = expand("&");
		} else
			fprintf(stderr, tr(28, "No file specified.\n"));
	}
	return (cp);
}

/*
 * Delete messages.
 */
FL int
delete(void *v)
{
	int *msgvec = v;
	delm(msgvec);
	return 0;
}

/*
 * Delete messages, then type the new dot.
 */
FL int
deltype(void *v)
{
	int *msgvec = v;
	int list[2];
	int lastdot;

	lastdot = dot - &message[0] + 1;
	if (delm(msgvec) >= 0) {
		list[0] = dot - &message[0] + 1;
		if (list[0] > lastdot) {
			touch(dot);
			list[1] = 0;
			return(type(list));
		}
		printf(tr(29, "At EOF\n"));
	} else
		printf(tr(30, "No more messages\n"));
	return(0);
}

/*
 * Delete the indicated messages.
 * Set dot to some nice place afterwards.
 * Internal interface.
 */
static int
delm(int *msgvec)
{
	struct message *mp;
	int *ip;
	int last;

	last = 0;
	for (ip = msgvec; *ip != 0; ip++) {
		mp = &message[*ip - 1];
		touch(mp);
		mp->m_flag |= MDELETED|MTOUCH;
		mp->m_flag &= ~(MPRESERVE|MSAVED|MBOX);
		last = *ip;
	}
	if (last != 0) {
		setdot(&message[last-1]);
		last = first(0, MDELETED);
		if (last != 0) {
			setdot(&message[last-1]);
			return(0);
		}
		else {
			setdot(&message[0]);
			return(-1);
		}
	}

	/*
	 * Following can't happen -- it keeps lint happy
	 */

	return(-1);
}

/*
 * Undelete the indicated messages.
 */
FL int
undeletecmd(void *v)
{
	int *msgvec = v;
	struct message *mp;
	int *ip;

	for (ip = msgvec; *ip && ip-msgvec < msgCount; ip++) {
		mp = &message[*ip - 1];
		touch(mp);
		setdot(mp);
		if (mp->m_flag & (MDELETED|MSAVED))
			mp->m_flag &= ~(MDELETED|MSAVED);
		else
			mp->m_flag &= ~MDELETED;
#ifdef HAVE_IMAP
		if (mb.mb_type == MB_IMAP || mb.mb_type == MB_CACHE)
			imap_undelete(mp, *ip);
#endif
	}
	return 0;
}

#ifdef HAVE_DEBUG
/* Interactively dump core on "core" */
FL int
core(void *v)
{
	int pid, waits;
	UNUSED(v);

	switch (pid = fork()) {
	case -1:
		perror("fork");
		return (1);
	case 0:
		abort();
		_exit(1);
	}
	printf(tr(31, "Okie dokie"));
	fflush(stdout);
	wait_child(pid, &waits);
# ifdef	WCOREDUMP
	if (WCOREDUMP(waits))
		printf(tr(32, " -- Core dumped.\n"));
	else
		printf(tr(33, " -- Can't dump core.\n"));
# endif
	return 0;
}

static void
clob1(int n)
{
	char buf[512], *cp;

	if (n <= 0)
		return;
	for (cp = buf; PTRCMP(cp, <, buf + 512); ++cp)
		*cp = (char)0xFF;
	clob1(n - 1);
}

/*
 * Clobber as many bytes of stack as the user requests.
 */
FL int
clobber(void *v)
{
	char **argv = v;
	int times;

	if (argv[0] == 0)
		times = 1;
	else
		times = (atoi(argv[0]) + 511) / 512;
	clob1(times);
	return (0);
}
#endif /* HAVE_DEBUG */

/*
 * Add the given header fields to the retained list.
 * If no arguments, print the current list of retained fields.
 */
FL int
retfield(void *v)
{
	char **list = v;

	return ignore1(list, ignore + 1, "retained");
}

/*
 * Add the given header fields to the ignored list.
 * If no arguments, print the current list of ignored fields.
 */
FL int
igfield(void *v)
{
	char **list = v;

	return ignore1(list, ignore, "ignored");
}

FL int
saveretfield(void *v)
{
	char **list = v;

	return ignore1(list, saveignore + 1, "retained");
}

FL int
saveigfield(void *v)
{
	char **list = v;

	return ignore1(list, saveignore, "ignored");
}

FL int
fwdretfield(void *v)
{
	char **list = v;

	return ignore1(list, fwdignore + 1, "retained");
}

FL int
fwdigfield(void *v)
{
	char **list = v;

	return ignore1(list, fwdignore, "ignored");
}

static int
ignore1(char **list, struct ignoretab *tab, char const *which)
{
	int h;
	struct ignore *igp;
	char **ap;

	if (*list == NULL)
		return igshow(tab, which);
	for (ap = list; *ap != 0; ap++) {
		char *field;
		size_t sz;

		sz = strlen(*ap);
		field = ac_alloc(sz + 1);
		i_strcpy(field, *ap, sz + 1);
		field[sz]='\0';
		if (member(field, tab)) {
			ac_free(field);
			continue;
		}
		h = hash(field);
		igp = (struct ignore *)scalloc(1, sizeof (struct ignore));
		sz = strlen(field) + 1;
		igp->i_field = smalloc(sz);
		memcpy(igp->i_field, field, sz);
		igp->i_link = tab->i_head[h];
		tab->i_head[h] = igp;
		tab->i_count++;
		ac_free(field);
	}
	return 0;
}

/*
 * Print out all currently retained fields.
 */
static int
igshow(struct ignoretab *tab, char const *which)
{
	int h;
	struct ignore *igp;
	char **ap, **ring;

	if (tab->i_count == 0) {
		printf(tr(34, "No fields currently being %s.\n"), which);
		return 0;
	}
	/*LINTED*/
	ring = (char **)salloc((tab->i_count + 1) * sizeof (char *));
	ap = ring;
	for (h = 0; h < HSHSIZE; h++)
		for (igp = tab->i_head[h]; igp != 0; igp = igp->i_link)
			*ap++ = igp->i_field;
	*ap = 0;
	qsort(ring, tab->i_count, sizeof (char *), igcomp);
	for (ap = ring; *ap != 0; ap++)
		printf("%s\n", *ap);
	return 0;
}

/*
 * Compare two names for sorting ignored field list.
 */
static int
igcomp(const void *l, const void *r)
{
	return (strcmp(*(char**)UNCONST(l), *(char**)UNCONST(r)));
}

FL int
unignore(void *v)
{
	return unignore1((char **)v, ignore, "ignored");
}

FL int
unretain(void *v)
{
	return unignore1((char **)v, ignore + 1, "retained");
}

FL int
unsaveignore(void *v)
{
	return unignore1((char **)v, saveignore, "ignored");
}

FL int
unsaveretain(void *v)
{
	return unignore1((char **)v, saveignore + 1, "retained");
}

FL int
unfwdignore(void *v)
{
	return unignore1((char **)v, fwdignore, "ignored");
}

FL int
unfwdretain(void *v)
{
	return unignore1((char **)v, fwdignore + 1, "retained");
}

static void
unignore_one(const char *name, struct ignoretab *tab)
{
	struct ignore *ip, *iq = NULL;
	int h = hash(name);

	for (ip = tab->i_head[h]; ip; ip = ip->i_link) {
		if (asccasecmp(ip->i_field, name) == 0) {
			free(ip->i_field);
			if (iq != NULL)
				iq->i_link = ip->i_link;
			else
				tab->i_head[h] = ip->i_link;
			free(ip);
			tab->i_count--;
			break;
		}
		iq = ip;
	}
}

static int
unignore1(char **list, struct ignoretab *tab, char const *which)
{
	if (tab->i_count == 0) {
		printf(tr(34, "No fields currently being %s.\n"), which);
		return 0;
	}
	while (*list)
		unignore_one(*list++, tab);
	return 0;
}
