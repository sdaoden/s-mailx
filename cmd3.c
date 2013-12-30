/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Still more user commands.
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/* Modify subject we reply to to begin with Re: if it does not already */
static char *	_reedit(char *subj);

static int	bangexp(char **str, size_t *size);
static void	make_ref_and_cs(struct message *mp, struct header *head);
static int (*	respond_or_Respond(int c))(int *, int);
static int	respond_internal(int *msgvec, int recipient_record);
static char *	fwdedit(char *subj);
static void	asort(char **list);
static int	diction(const void *a, const void *b);
static int	Respond_internal(int *msgvec, int recipient_record);
static int	resend1(void *v, int add_resent);
static void	list_shortcuts(void);
static enum okay delete_shortcut(const char *str);

static char *
_reedit(char *subj)
{
	struct str in, out;
	char *newsubj = NULL;

	if (subj == NULL || *subj == '\0')
		goto j_leave;

	in.s = subj;
	in.l = strlen(subj);
	mime_fromhdr(&in, &out, TD_ISPR|TD_ICONV);

	if ((out.s[0] == 'r' || out.s[0] == 'R') &&
			(out.s[1] == 'e' || out.s[1] == 'E') &&
			out.s[2] == ':') {
		newsubj = savestr(out.s);
		goto jleave;
	}
	newsubj = salloc(out.l + 5);
	sstpcpy(sstpcpy(newsubj, "Re: "), out.s);
jleave:
	free(out.s);
j_leave:
	return (newsubj);
}

/*
 * Process a shell escape by saving signals, ignoring signals,
 * and forking a sh -c
 */
FL int
shell(void *v)
{
	char *str = v, *cmd;
	char const *sh;
	size_t cmdsize;
	sighandler_type sigint = safe_signal(SIGINT, SIG_IGN);

	cmd = smalloc(cmdsize = strlen(str) + 1);
	memcpy(cmd, str, cmdsize);
	if (bangexp(&cmd, &cmdsize) < 0)
		return 1;
	if ((sh = value("SHELL")) == NULL)
		sh = SHELL;
	run_command(sh, 0, -1, -1, "-c", cmd, NULL);
	safe_signal(SIGINT, sigint);
	printf("!\n");
	free(cmd);
	return 0;
}

/*
 * Fork an interactive shell.
 */
/*ARGSUSED*/
FL int
dosh(void *v)
{
	sighandler_type sigint = safe_signal(SIGINT, SIG_IGN);
	char const *sh;
	(void)v;

	if ((sh = value("SHELL")) == NULL)
		sh = SHELL;
	run_command(sh, 0, -1, -1, NULL, NULL, NULL);
	safe_signal(SIGINT, sigint);
	putchar('\n');
	return 0;
}

/*
 * Expand the shell escape by expanding unescaped !'s into the
 * last issued command where possible.
 */

static char	*lastbang;
static size_t	lastbangsize;

static int
bangexp(char **str, size_t *size)
{
	char *bangbuf;
	int changed = 0;
	int dobang = ok_blook(bang);
	size_t sz, i, j, bangbufsize;

	bangbuf = smalloc(bangbufsize = *size);
	i = j = 0;
	while ((*str)[i]) {
		if (dobang) {
			if ((*str)[i] == '!') {
				sz = strlen(lastbang);
				bangbuf = srealloc(bangbuf, bangbufsize += sz);
				changed++;
				memcpy(bangbuf + j, lastbang, sz + 1);
				j += sz;
				i++;
				continue;
			}
		}
		if ((*str)[i] == '\\' && (*str)[i + 1] == '!') {
			bangbuf[j++] = '!';
			i += 2;
			changed++;
		}
		bangbuf[j++] = (*str)[i++];
	}
	bangbuf[j] = '\0';
	if (changed) {
		printf("!%s\n", bangbuf);
		fflush(stdout);
	}
	sz = j + 1;
	if (sz > *size)
		*str = srealloc(*str, *size = sz);
	memcpy(*str, bangbuf, sz);
	if (sz > lastbangsize)
		lastbang = srealloc(lastbang, lastbangsize = sz);
	memcpy(lastbang, bangbuf, sz);
	free(bangbuf);
	return 0;
}

/*ARGSUSED*/
FL int
help(void *v)
{
	int ret = 0;
	char *arg = *(char**)v;

	if (arg != NULL) {
#ifdef HAVE_DOCSTRINGS
		ret = ! print_comm_docstr(arg);
		if (ret)
			fprintf(stderr, tr(91, "Unknown command: `%s'\n"), arg);
#else
		ret = ccmdnotsupp(NULL);
#endif
		goto jleave;
	}

	/* Very ugly, but take care for compiler supported string lengths :( */
	printf(tr(295, "%s commands:\n"), progname);
	puts(tr(296,
"type <message list>         type messages\n"
"next                        goto and type next message\n"
"from <message list>         give head lines of messages\n"
"headers                     print out active message headers\n"
"delete <message list>       delete messages\n"
"undelete <message list>     undelete messages\n"));
	puts(tr(297,
"save <message list> folder  append messages to folder and mark as saved\n"
"copy <message list> folder  append messages to folder without marking them\n"
"write <message list> file   append message texts to file, save attachments\n"
"preserve <message list>     keep incoming messages in mailbox even if saved\n"
"Reply <message list>        reply to message senders\n"
"reply <message list>        reply to message senders and all recipients\n"));
	puts(tr(298,
"mail addresses              mail to specific recipients\n"
"file folder                 change to another folder\n"
"quit                        quit and apply changes to folder\n"
"xit                         quit and discard changes made to folder\n"
"!                           shell escape\n"
"cd <directory>              chdir to directory or home if none given\n"
"list                        list names of all available commands\n"));
	printf(tr(299,
"\nA <message list> consists of integers, ranges of same, or other criteria\n"
"separated by spaces.  If omitted, %s uses the last message typed.\n"),
		progname);

jleave:
	return ret;
}

FL int
c_cwd(void *v)
{
	char buf[MAXPATHLEN]; /* TODO getcwd(3) may return a larger value */

	if (getcwd(buf, sizeof buf) != NULL) {
		puts(buf);
		v = (void*)0x1;
	} else {
		perror("getcwd");
		v = NULL;
	}
	return (v == NULL);
}

FL int
c_chdir(void *v)
{
	char **arglist = v;
	char const *cp;

	if (*arglist == NULL)
		cp = homedir;
	else if ((cp = file_expand(*arglist)) == NULL)
		goto jleave;
	if (chdir(cp) < 0) {
		perror(cp);
		cp = NULL;
	}
jleave:
	return (cp == NULL);
}

static void
make_ref_and_cs(struct message *mp, struct header *head)
{
	char *oldref, *oldmsgid, *newref, *cp;
	size_t oldreflen = 0, oldmsgidlen = 0, reflen;
	unsigned i;
	struct name *n;

	oldref = hfield1("references", mp);
	oldmsgid = hfield1("message-id", mp);
	if (oldmsgid == NULL || *oldmsgid == '\0') {
		head->h_ref = NULL;
		return;
	}
	reflen = 1;
	if (oldref) {
		oldreflen = strlen(oldref);
		reflen += oldreflen + 2;
	}
	if (oldmsgid) {
		oldmsgidlen = strlen(oldmsgid);
		reflen += oldmsgidlen;
	}

	newref = ac_alloc(reflen);
	if (oldref != NULL) {
		memcpy(newref, oldref, oldreflen + 1);
		if (oldmsgid != NULL) {
			newref[oldreflen++] = ',';
			newref[oldreflen++] = ' ';
			memcpy(newref + oldreflen, oldmsgid, oldmsgidlen + 1);
		}
	} else if (oldmsgid)
		memcpy(newref, oldmsgid, oldmsgidlen + 1);
	n = extract(newref, GREF);
	ac_free(newref);

	/*
	 * Limit the references to 21 entries.
	 */
	while (n->n_flink != NULL)
		n = n->n_flink;
	for (i = 1; i < 21; i++) {
		if (n->n_blink != NULL)
			n = n->n_blink;
		else
			break;
	}
	n->n_blink = NULL;
	head->h_ref = n;
	if (ok_blook(reply_in_same_charset) &&
			(cp = hfield1("content-type", mp)) != NULL)
		head->h_charset = mime_getparam("charset", cp);
}

static int
(*respond_or_Respond(int c))(int *, int)
{
	int opt = 0;

	opt += ok_blook(Replyall);
	opt += ok_blook(flipr);
	return ((opt == 1) ^ (c == 'R')) ? Respond_internal : respond_internal;
}

FL int
respond(void *v)
{
	return (respond_or_Respond('r'))((int *)v, 0);
}

FL int
respondall(void *v)
{
	return respond_internal((int *)v, 0);
}

FL int
respondsender(void *v)
{
	return Respond_internal((int *)v, 0);
}

FL int
followup(void *v)
{
	return (respond_or_Respond('r'))((int *)v, 1);
}

FL int
followupall(void *v)
{
	return respond_internal((int *)v, 1);
}

FL int
followupsender(void *v)
{
	return Respond_internal((int *)v, 1);
}

/*
 * Reply to a single message.  Extract each name from the
 * message header and send them off to mail1()
 */
static int
respond_internal(int *msgvec, int recipient_record)
{
	struct header head;
	struct message *mp;
	char *cp, *rcv;
	struct name *np = NULL;
	enum gfield gf = ok_blook(fullnames) ? GFULL : GSKIN;

	if (msgvec[1] != 0) {
		fprintf(stderr, tr(37,
			"Sorry, can't reply to multiple messages at once\n"));
		return 1;
	}
	mp = &message[msgvec[0] - 1];
	touch(mp);
	setdot(mp);

	if ((rcv = hfield1("reply-to", mp)) == NULL)
		if ((rcv = hfield1("from", mp)) == NULL)
			rcv = nameof(mp, 1);
	if (rcv != NULL)
		np = lextract(rcv, GTO|gf);
	if (!ok_blook(recipients_in_cc) && (cp = hfield1("to", mp)) != NULL)
		np = cat(np, lextract(cp, GTO | gf));
	/*
	 * Delete my name from the reply list,
	 * and with it, all my alternate names.
	 */
	np = elide(delete_alternates(np));
	if (np == NULL)
		np = lextract(rcv, GTO | gf);

	memset(&head, 0, sizeof head);
	head.h_to = np;
	head.h_subject = hfield1("subject", mp);
	head.h_subject = _reedit(head.h_subject);
	/* Cc: */
	np = NULL;
	if (ok_blook(recipients_in_cc) && (cp = hfield1("to", mp)) != NULL)
		np = lextract(cp, GCC | gf);
	if ((cp = hfield1("cc", mp)) != NULL)
		np = cat(np, lextract(cp, GCC | gf));
	if (np != NULL)
		head.h_cc = elide(delete_alternates(np));
	make_ref_and_cs(mp, &head);

	if (ok_blook(quote_as_attachment)) {
		head.h_attach = csalloc(1, sizeof *head.h_attach);
		head.h_attach->a_msgno = *msgvec;
		head.h_attach->a_content_description = tr(512,
			"Original message content");
	}

	if (mail1(&head, 1, mp, NULL, recipient_record, 0) == OKAY &&
			ok_blook(markanswered) &&
			(mp->m_flag & MANSWERED) == 0)
		mp->m_flag |= MANSWER | MANSWERED;
	return 0;
}

/*
 * Forward a message to a new recipient, in the sense of RFC 2822.
 */
static int
forward1(char *str, int recipient_record)
{
	int	*msgvec;
	char	*recipient;
	struct message	*mp;
	struct header	head;
	bool_t f, forward_as_attachment;

	forward_as_attachment = ok_blook(forward_as_attachment);
	msgvec = salloc((msgCount + 2) * sizeof *msgvec);
	if ((recipient = laststring(str, &f, 0)) == NULL) {
		puts(tr(47, "No recipient specified."));
		return 1;
	}
	if (!f) {
		*msgvec = first(0, MMNORM);
		if (*msgvec == 0) {
			if (inhook)
				return 0;
			printf("No messages to forward.\n");
			return 1;
		}
		msgvec[1] = 0;
	} else if (getmsglist(str, msgvec, 0) < 0)
		return 1;
	if (*msgvec == 0) {
		if (inhook)
			return 0;
		printf("No applicable messages.\n");
		return 1;
	}
	if (msgvec[1] != 0) {
		printf("Cannot forward multiple messages at once\n");
		return 1;
	}
	memset(&head, 0, sizeof head);
	if ((head.h_to = lextract(recipient,
			GTO | (ok_blook(fullnames) ? GFULL : GSKIN))) == NULL)
		return 1;
	mp = &message[*msgvec - 1];
	if (forward_as_attachment) {
		head.h_attach = csalloc(1, sizeof *head.h_attach);
		head.h_attach->a_msgno = *msgvec;
		head.h_attach->a_content_description = "Forwarded message";
	} else {
		touch(mp);
		setdot(mp);
	}
	head.h_subject = hfield1("subject", mp);
	head.h_subject = fwdedit(head.h_subject);
	mail1(&head, 1, (forward_as_attachment ? NULL : mp),
		NULL, recipient_record, 1);
	return 0;
}

/*
 * Modify the subject we are replying to to begin with Fwd:.
 */
static char *
fwdedit(char *subj)
{
	struct str in, out;
	char *newsubj;

	if (subj == NULL || *subj == '\0')
		return NULL;
	in.s = subj;
	in.l = strlen(subj);
	mime_fromhdr(&in, &out, TD_ISPR|TD_ICONV);

	newsubj = salloc(out.l + 6);
	memcpy(newsubj, "Fwd: ", 5);
	memcpy(newsubj + 5, out.s, out.l + 1);
	free(out.s);
	return newsubj;
}

/*
 * The 'forward' command.
 */
FL int
forwardcmd(void *v)
{
	return forward1(v, 0);
}

/*
 * Similar to forward, saving the message in a file named after the
 * first recipient.
 */
FL int
Forwardcmd(void *v)
{
	return forward1(v, 1);
}

/*
 * Preserve the named messages, so that they will be sent
 * back to the system mailbox.
 */
FL int
preserve(void *v)
{
	int *msgvec = v;
	struct message *mp;
	int *ip, mesg;

	if (edit) {
		printf(tr(39, "Cannot \"preserve\" in edit mode\n"));
		return(1);
	}
	for (ip = msgvec; *ip != 0; ip++) {
		mesg = *ip;
		mp = &message[mesg-1];
		mp->m_flag |= MPRESERVE;
		mp->m_flag &= ~MBOX;
		setdot(mp);
		/*
		 * This is now Austin Group Request XCU #20.
		 */
		did_print_dot = TRU1;
	}
	return(0);
}

/*
 * Mark all given messages as unread.
 */
FL int
unread(void *v)
{
	int	*msgvec = v;
	int *ip;

	for (ip = msgvec; *ip != 0; ip++) {
		setdot(&message[*ip-1]);
		dot->m_flag &= ~(MREAD|MTOUCH);
		dot->m_flag |= MSTATUS;
#ifdef HAVE_IMAP
		if (mb.mb_type == MB_IMAP || mb.mb_type == MB_CACHE)
			imap_unread(&message[*ip-1], *ip); /* TODO return? */
#endif
		/*
		 * The "unread" command is not part of POSIX mailx.
		 */
		did_print_dot = TRU1;
	}
	return(0);
}

/*
 * Mark all given messages as read.
 */
FL int
seen(void *v)
{
	int	*msgvec = v;
	int	*ip;

	for (ip = msgvec; *ip; ip++) {
		setdot(&message[*ip-1]);
		touch(&message[*ip-1]);
	}
	return 0;
}

/*
 * Print the size of each message.
 */
FL int
messize(void *v)
{
	int *msgvec = v;
	struct message *mp;
	int *ip, mesg;

	for (ip = msgvec; *ip != 0; ip++) {
		mesg = *ip;
		mp = &message[mesg-1];
		printf("%d: ", mesg);
		if (mp->m_xlines > 0)
			printf("%ld", mp->m_xlines);
		else
			putchar(' ');
		printf("/%lu\n", (unsigned long)mp->m_xsize);
	}
	return(0);
}

/*
 * Quit quickly.  If we are sourcing, just pop the input level
 * by returning an error.
 */
/*ARGSUSED*/
FL int
rexit(void *v)
{
	(void)v;
	if (sourcing)
		return(1);
	exit(0);
	/*NOTREACHED*/
}

FL int
set(void *v)
{
	char **ap = v, *cp, *cp2, *varbuf, c;
	int errs = 0;

	if (*ap == NULL) {
		var_list_all();
		goto jleave;
	}

	for (; *ap != NULL; ++ap) {
		cp = *ap;
		cp2 = varbuf = ac_alloc(strlen(cp) + 1);
		for (; (c = *cp) != '=' && c != '\0'; ++cp)
			*cp2++ = c;
		*cp2 = '\0';
		if (c == '\0')
			cp = UNCONST("");
		else
			++cp;
		if (varbuf == cp2) {
			fprintf(stderr,
				tr(41, "Non-null variable name required\n"));
			++errs;
			goto jnext;
		}
		if (varbuf[0] == 'n' && varbuf[1] == 'o')
			errs += var_unset(&varbuf[2]);
		else
			errs += var_assign(varbuf, cp);
jnext:		ac_free(varbuf);
	}
jleave:
	return (errs);
}

/*
 * Unset a bunch of variable values.
 */
FL int
unset(void *v)
{
	int errs;
	char **ap;

	errs = 0;
	for (ap = (char**)v; *ap != NULL; ap++)
		errs += var_unset(*ap);
	return errs;
}

/*
 * Put add users to a group.
 */
FL int
group(void *v)
{
	char **argv = v;
	struct grouphead *gh;
	struct group *gp;
	int h;
	int s;
	char **ap, *gname, **p;

	if (*argv == NULL) {
		for (h = 0, s = 1; h < HSHSIZE; h++)
			for (gh = groups[h]; gh != NULL; gh = gh->g_link)
				s++;
		/*LINTED*/
		ap = (char **)salloc(s * sizeof *ap);
		for (h = 0, p = ap; h < HSHSIZE; h++)
			for (gh = groups[h]; gh != NULL; gh = gh->g_link)
				*p++ = gh->g_name;
		*p = NULL;
		asort(ap);
		for (p = ap; *p != NULL; p++)
			printgroup(*p);
		return(0);
	}
	if (argv[1] == NULL) {
		printgroup(*argv);
		return(0);
	}
	gname = *argv;
	h = hash(gname);
	if ((gh = findgroup(gname)) == NULL) {
		gh = (struct grouphead *)scalloc(1, sizeof *gh);
		gh->g_name = sstrdup(gname);
		gh->g_list = NULL;
		gh->g_link = groups[h];
		groups[h] = gh;
	}

	/*
	 * Insert names from the command list into the group.
	 * Who cares if there are duplicates?  They get tossed
	 * later anyway.
	 */

	for (ap = argv+1; *ap != NULL; ap++) {
		gp = (struct group *)scalloc(1, sizeof *gp);
		gp->ge_name = sstrdup(*ap);
		gp->ge_link = gh->g_list;
		gh->g_list = gp;
	}
	return(0);
}

/*
 * Delete the passed groups.
 */
FL int
ungroup(void *v)
{
	char **argv = v;

	if (*argv == NULL) {
		fprintf(stderr, tr(209, "Must specify alias to remove\n"));
		return 1;
	}
	do
		remove_group(*argv);
	while (*++argv != NULL);
	return 0;
}

/*
 * Sort the passed string vecotor into ascending dictionary
 * order.
 */
static void
asort(char **list)
{
	char **ap;

	for (ap = list; *ap != NULL; ap++)
		;
	if (ap-list < 2)
		return;
	qsort(list, ap-list, sizeof(*list), diction);
}

/*
 * Do a dictionary order comparison of the arguments from
 * qsort.
 */
static int
diction(const void *a, const void *b)
{
	return(strcmp(*(char**)UNCONST(a), *(char**)UNCONST(b)));
}

/*
 * Change to another file.  With no argument, print information about
 * the current file.
 */
FL int
cfile(void *v)
{
	char **argv = v;
	int i;

	if (*argv == NULL) {
		newfileinfo();
		return 0;
	}

	if (inhook) {
		fprintf(stderr, tr(516,
			"Cannot change folder from within a hook.\n"));
		return 1;
	}

	save_mbox_for_possible_quitstuff();

	i = setfile(*argv, 0);
	if (i < 0)
		return 1;
	callhook(mailname, 0);
	if (i > 0 && !ok_blook(emptystart))
		return 1;
	announce(ok_blook(bsdcompat) || ok_blook(bsdannounce));
	return 0;
}

/*
 * Expand file names like echo
 */
FL int
echo(void *v)
{
	char const **argv = v, **ap, *cp;
	int c;

	for (ap = argv; *ap != NULL; ++ap) {
		cp = *ap;
		if ((cp = fexpand(cp, FEXP_NSHORTCUT)) != NULL) {
			if (ap != argv)
				putchar(' ');
			c = 0;
			while (*cp != '\0' &&
					(c = expand_shell_escape(&cp, FAL0))
					> 0)
				putchar(c);
			/* \c ends overall processing */
			if (c < 0)
				goto jleave;
		}
	}
	putchar('\n');
jleave:
	return 0;
}

FL int
Respond(void *v)
{
	return (respond_or_Respond('R'))((int *)v, 0);
}

FL int
Followup(void *v)
{
	return (respond_or_Respond('R'))((int *)v, 1);
}

/*
 * Reply to a series of messages by simply mailing to the senders
 * and not messing around with the To: and Cc: lists as in normal
 * reply.
 */
static int
Respond_internal(int *msgvec, int recipient_record)
{
	struct header head;
	struct message *mp;
	int *ap;
	char *cp;
	enum gfield gf = ok_blook(fullnames) ? GFULL : GSKIN;

	memset(&head, 0, sizeof head);

	for (ap = msgvec; *ap != 0; ap++) {
		mp = &message[*ap - 1];
		touch(mp);
		setdot(mp);
		if ((cp = hfield1("reply-to", mp)) == NULL)
			if ((cp = hfield1("from", mp)) == NULL)
				cp = nameof(mp, 2);
		head.h_to = cat(head.h_to, lextract(cp, GTO | gf));
	}
	if (head.h_to == NULL)
		return 0;

	mp = &message[msgvec[0] - 1];
	head.h_subject = hfield1("subject", mp);
	head.h_subject = _reedit(head.h_subject);
	make_ref_and_cs(mp, &head);

	if (ok_blook(quote_as_attachment)) {
		head.h_attach = csalloc(1, sizeof *head.h_attach);
		head.h_attach->a_msgno = *msgvec;
		head.h_attach->a_content_description = tr(512,
			"Original message content");
	}

	if (mail1(&head, 1, mp, NULL, recipient_record, 0) == OKAY &&
			ok_blook(markanswered) && (mp->m_flag & MANSWERED) == 0)
		mp->m_flag |= MANSWER | MANSWERED;
	return 0;
}

/*
 * Conditional commands.  These allow one to parameterize one's
 * .mailrc and do some things if sending, others if receiving.
 */
FL int
ifcmd(void *v)
{
	char **argv = v;
	char *cp;

	if (cond != CANY) {
		printf(tr(42, "Illegal nested \"if\"\n"));
		return(1);
	}
	cond = CANY;
	cp = argv[0];
	switch (*cp) {
	case 'r': case 'R':
		cond = CRCV;
		break;

	case 's': case 'S':
		cond = CSEND;
		break;

	case 't': case 'T':
		cond = CTERM;
		break;

	default:
		printf(tr(43, "Unrecognized if-keyword: \"%s\"\n"), cp);
		return(1);
	}
	return(0);
}

/*
 * Implement 'else'.  This is pretty simple -- we just
 * flip over the conditional flag.
 */
/*ARGSUSED*/
FL int
elsecmd(void *v)
{
	(void)v;

	switch (cond) {
	case CANY:
		printf(tr(44, "\"Else\" without matching \"if\"\n"));
		return(1);

	case CSEND:
		cond = CRCV;
		break;

	case CRCV:
		cond = CSEND;
		break;

	case CTERM:
		cond = CNONTERM;
		break;

	default:
		printf(tr(45, "Mail's idea of conditions is screwed up\n"));
		cond = CANY;
		break;
	}
	return(0);
}

/*
 * End of if statement.  Just set cond back to anything.
 */
/*ARGSUSED*/
FL int
endifcmd(void *v)
{
	(void)v;

	if (cond == CANY) {
		printf(tr(46, "\"Endif\" without matching \"if\"\n"));
		return(1);
	}
	cond = CANY;
	return(0);
}

/*
 * Set the list of alternate names.
 */
FL int
alternates(void *v)
{
	size_t l;
	char **namelist = v, **ap, **ap2, *cp;

	l = argcount(namelist) + 1;

	if (l == 1) {
		if (altnames == NULL)
			goto jleave;
		for (ap = altnames; *ap != NULL; ++ap)
			printf("%s ", *ap);
		printf("\n");
		goto jleave;
	}

	if (altnames != NULL) {
		for (ap = altnames; *ap != NULL; ++ap)
			free(*ap);
		free(altnames);
	}
	altnames = smalloc(l * sizeof(char*));
	for (ap = namelist, ap2 = altnames; *ap; ++ap, ++ap2) {
		l = strlen(*ap) + 1;
		cp = smalloc(l);
		memcpy(cp, *ap, l);
		*ap2 = cp;
	}
	*ap2 = NULL;
jleave:
	return (0);
}

/*
 * Do the real work of resending.
 */
static int
resend1(void *v, int add_resent)
{
	char *name, *str;
	struct name *to;
	struct name *sn;
	int *ip, *msgvec;
	bool_t f;

	str = (char *)v;
	/*LINTED*/
	msgvec = (int *)salloc((msgCount + 2) * sizeof *msgvec);
	name = laststring(str, &f, 1);
	if (name == NULL) {
		puts(tr(47, "No recipient specified."));
		return 1;
	}
	if (!f) {
		*msgvec = first(0, MMNORM);
		if (*msgvec == 0) {
			if (inhook)
				return 0;
			puts(tr(48, "No applicable messages."));
			return 1;
		}
		msgvec[1] = 0;
	} else if (getmsglist(str, msgvec, 0) < 0)
		return 1;
	if (*msgvec == 0) {
		if (inhook)
			return 0;
		printf("No applicable messages.\n");
		return 1;
	}
	sn = nalloc(name, GTO);
	to = usermap(sn, FAL0);
	for (ip = msgvec; *ip && ip - msgvec < msgCount; ip++) {
		if (resend_msg(&message[*ip - 1], to, add_resent) != OKAY)
			return 1;
	}
	return 0;
}

/*
 * Resend a message list to a third person.
 */
FL int
resendcmd(void *v)
{
	return resend1(v, 1);
}

/*
 * Resend a message list to a third person without adding headers.
 */
FL int
Resendcmd(void *v)
{
	return resend1(v, 0);
}

/*
 * 'newmail' or 'inc' command: Check for new mail without writing old
 * mail back.
 */
/*ARGSUSED*/
FL int
newmail(void *v)
{
	int val = 1, mdot;
	(void)v;

	if (
#ifdef HAVE_IMAP
	    (mb.mb_type != MB_IMAP || imap_newmail(1)) &&
#endif
	    (val = setfile(mailname, 1)) == 0) {
		mdot = getmdot(1);
		setdot(&message[mdot - 1]);
	}
	return val;
}

static void
list_shortcuts(void)
{
	struct shortcut *s;

	for (s = shortcuts; s; s = s->sh_next)
		printf("%s=%s\n", s->sh_short, s->sh_long);
}

FL int
shortcut(void *v)
{
	char **args = (char **)v;
	struct shortcut *s;

	if (args[0] == NULL) {
		list_shortcuts();
		return 0;
	}
	if (args[1] == NULL) {
		fprintf(stderr, tr(220,
			"expansion name for shortcut missing\n"));
		return 1;
	}
	if (args[2] != NULL) {
		fprintf(stderr, tr(221, "too many arguments\n"));
		return 1;
	}
	if ((s = get_shortcut(args[0])) != NULL) {
		free(s->sh_long);
		s->sh_long = sstrdup(args[1]);
	} else {
		s = scalloc(1, sizeof *s);
		s->sh_short = sstrdup(args[0]);
		s->sh_long = sstrdup(args[1]);
		s->sh_next = shortcuts;
		shortcuts = s;
	}
	return 0;
}

FL struct shortcut *
get_shortcut(const char *str)
{
	struct shortcut *s;

	for (s = shortcuts; s; s = s->sh_next)
		if (strcmp(str, s->sh_short) == 0)
			break;
	return s;
}

static enum okay
delete_shortcut(const char *str)
{
	struct shortcut *sp, *sq;

	for (sp = shortcuts, sq = NULL; sp; sq = sp, sp = sp->sh_next) {
		if (strcmp(sp->sh_short, str) == 0) {
			free(sp->sh_short);
			free(sp->sh_long);
			if (sq)
				sq->sh_next = sp->sh_next;
			if (sp == shortcuts)
				shortcuts = sp->sh_next;
			free(sp);
			return OKAY;
		}
	}
	return STOP;
}

FL int
unshortcut(void *v)
{
	char **args = (char **)v;
	int errs = 0;

	if (args[0] == NULL) {
		fprintf(stderr, tr(222, "need shortcut names to remove\n"));
		return 1;
	}
	while (*args != NULL) {
		if (delete_shortcut(*args) != OKAY) {
			errs = 1;
			fprintf(stderr, tr(223, "%s: no such shortcut\n"),
				*args);
		}
		args++;
	}
	return errs;
}

FL int
cflag(void *v)
{
	struct message	*m;
	int	*msgvec = v;
	int	*ip;

	for (ip = msgvec; *ip != 0; ip++) {
		m = &message[*ip-1];
		setdot(m);
		if ((m->m_flag & (MFLAG|MFLAGGED)) == 0)
			m->m_flag |= MFLAG|MFLAGGED;
	}
	return 0;
}

FL int
cunflag(void *v)
{
	struct message	*m;
	int	*msgvec = v;
	int	*ip;

	for (ip = msgvec; *ip != 0; ip++) {
		m = &message[*ip-1];
		setdot(m);
		if (m->m_flag & (MFLAG|MFLAGGED)) {
			m->m_flag &= ~(MFLAG|MFLAGGED);
			m->m_flag |= MUNFLAG;
		}
	}
	return 0;
}

FL int
canswered(void *v)
{
	struct message	*m;
	int	*msgvec = v;
	int	*ip;

	for (ip = msgvec; *ip != 0; ip++) {
		m = &message[*ip-1];
		setdot(m);
		if ((m->m_flag & (MANSWER|MANSWERED)) == 0)
			m->m_flag |= MANSWER|MANSWERED;
	}
	return 0;
}

FL int
cunanswered(void *v)
{
	struct message	*m;
	int	*msgvec = v;
	int	*ip;

	for (ip = msgvec; *ip != 0; ip++) {
		m = &message[*ip-1];
		setdot(m);
		if (m->m_flag & (MANSWER|MANSWERED)) {
			m->m_flag &= ~(MANSWER|MANSWERED);
			m->m_flag |= MUNANSWER;
		}
	}
	return 0;
}

FL int
cdraft(void *v)
{
	struct message	*m;
	int	*msgvec = v;
	int	*ip;

	for (ip = msgvec; *ip != 0; ip++) {
		m = &message[*ip-1];
		setdot(m);
		if ((m->m_flag & (MDRAFT|MDRAFTED)) == 0)
			m->m_flag |= MDRAFT|MDRAFTED;
	}
	return 0;
}

FL int
cundraft(void *v)
{
	struct message	*m;
	int	*msgvec = v;
	int	*ip;

	for (ip = msgvec; *ip != 0; ip++) {
		m = &message[*ip-1];
		setdot(m);
		if (m->m_flag & (MDRAFT|MDRAFTED)) {
			m->m_flag &= ~(MDRAFT|MDRAFTED);
			m->m_flag |= MUNDRAFT;
		}
	}
	return 0;
}

/*ARGSUSED*/
FL int
cnoop(void *v)
{
	(void)v;

	switch (mb.mb_type) {
	case MB_IMAP:
#ifdef HAVE_IMAP
		imap_noop();
		break;
#else
		return (ccmdnotsupp(NULL));
#endif
	case MB_POP3:
#ifdef HAVE_POP3
		pop3_noop();
		break;
#else
		return (ccmdnotsupp(NULL));
#endif
	default:
		break;
	}
	return 0;
}

FL int
cremove(void *v)
{
	char	vb[LINESIZE];
	char	**args = v;
	char	*name;
	int	ec = 0;

	if (*args == NULL) {
		fprintf(stderr, tr(290, "Syntax is: remove mailbox ...\n"));
		return (1);
	}
	do {
		if ((name = expand(*args)) == NULL)
			continue;
		if (strcmp(name, mailname) == 0) {
			fprintf(stderr, tr(286,
				"Cannot remove current mailbox \"%s\".\n"),
				name);
			ec |= 1;
			continue;
		}
		snprintf(vb, sizeof vb, tr(287, "Remove \"%s\" (y/n) ? "),
			name);
		if (yorn(vb) == 0)
			continue;
		switch (which_protocol(name)) {
		case PROTO_FILE:
			if (unlink(name) < 0) {	/* do not handle .gz .bz2 */
				perror(name);
				ec |= 1;
			}
			break;
		case PROTO_POP3:
			fprintf(stderr, tr(288,
				"Cannot remove POP3 mailbox \"%s\".\n"),
					name);
			ec |= 1;
			break;
		case PROTO_IMAP:
#ifdef HAVE_IMAP
			if (imap_remove(name) != OKAY)
#endif
				ec |= 1;
			break;
		case PROTO_MAILDIR:
			if (maildir_remove(name) != OKAY)
				ec |= 1;
			break;
		case PROTO_UNKNOWN:
			fprintf(stderr, tr(289,
				"Unknown protocol in \"%s\". Not removed.\n"),
				name);
			ec |= 1;
		}
	} while (*++args);
	return ec;
}

FL int
crename(void *v)
{
	char	**args = v, *old, *new;
	enum protocol	oldp, newp;
	int	ec = 0;

	if (args[0] == NULL || args[1] == NULL || args[2] != NULL) {
		fprintf(stderr, "Syntax: rename old new\n");
		return (1);
	}

	if ((old = expand(args[0])) == NULL)
		return (1);
	oldp = which_protocol(old);
	if ((new = expand(args[1])) == NULL)
		return (1);
	newp = which_protocol(new);

	if (strcmp(old, mailname) == 0 || strcmp(new, mailname) == 0) {
		fprintf(stderr, tr(291,
		"Cannot rename current mailbox \"%s\".\n"), old);
		return 1;
	}
	if ((oldp == PROTO_IMAP || newp == PROTO_IMAP) && oldp != newp) {
		fprintf(stderr, tr(292,
			"Can only rename folders of same type.\n"));
		return 1;
	}
	if (newp == PROTO_POP3)
		goto nopop3;
	switch (oldp) {
	case PROTO_FILE:
		if (link(old, new) < 0) {
			switch (errno) {
			case EACCES:
			case EEXIST:
			case ENAMETOOLONG:
			case ENOENT:
			case ENOSPC:
			case EXDEV:
				perror(new);
				break;
			default:
				perror(old);
			}
			ec |= 1;
		} else if (unlink(old) < 0) {
			perror(old);
			ec |= 1;
		}
		break;
	case PROTO_MAILDIR:
		if (rename(old, new) < 0) {
			perror(old);
			ec |= 1;
		}
		break;
	case PROTO_POP3:
	nopop3:	fprintf(stderr, tr(293, "Cannot rename POP3 mailboxes.\n"));
		ec |= 1;
		break;
#ifdef HAVE_IMAP
	case PROTO_IMAP:
		if (imap_rename(old, new) != OKAY)
			ec |= 1;
		break;
#endif
	case PROTO_UNKNOWN:
	default:
		fprintf(stderr, tr(294,
			"Unknown protocol in \"%s\" and \"%s\".  "
			"Not renamed.\n"), old, new);
		ec |= 1;
	}
	return ec;
}
