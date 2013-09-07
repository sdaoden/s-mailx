/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ IMAP v4r1 client following RFC 2060.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 2004
 *	Gunnar Ritter.  All rights reserved.
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
 *	This product includes software developed by Gunnar Ritter
 *	and his contributors.
 * 4. Neither the name of Gunnar Ritter nor the names of his contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GUNNAR RITTER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL GUNNAR RITTER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "rcv.h"

#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#ifndef HAVE_IMAP
# include "extern.h"
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

#include "extern.h"
#ifdef HAVE_MD5
# include "md5.h"
#endif

#define	IMAP_ANSWER()	{ \
				if (mp->mb_type != MB_CACHE) { \
					enum okay ok = OKAY; \
					while (mp->mb_active & MB_COMD) \
						ok = imap_answer(mp, 1); \
					if (ok == STOP) \
						return STOP; \
				} \
			}
/* TODO IMAP_OUT() simply returns instead of doing "actioN" if imap_finish()
 * TODO fails, which leaves behind leaks in, e.g., imap_append1()!
 * TODO IMAP_XOUT() was added due to this, but (1) needs to be used everywhere
 * TODO and (2) doesn't handle all I/O errors itself, yet, too.
 * TODO I.e., that should be a function, not a macro ... or so.
 * TODO This entire module needs MASSIVE work! */
#define IMAP_OUT(X,Y,ACTION)	IMAP_XOUT(X, Y, ACTION, return STOP)
#define	IMAP_XOUT(X,Y,ACTIONERR,ACTIONBAIL)\
do {\
	if (mp->mb_type != MB_CACHE) {\
		if (imap_finish(mp) == STOP) {\
			ACTIONBAIL;\
		}\
		if (options & OPT_VERBOSE)\
			fprintf(stderr, ">>> %s", X);\
		mp->mb_active |= Y;\
		if (swrite(&mp->mb_sock, X) == STOP) {\
			ACTIONERR;\
		}\
	} else {\
		if (queuefp != NULL)\
			fputs(X, queuefp);\
	}\
} while (0);

static struct	record {
	struct record	*rec_next;
	unsigned long	rec_count;
	enum rec_type {
		REC_EXISTS,
		REC_EXPUNGE
	} rec_type;
} *record, *recend;

static enum {
	RESPONSE_TAGGED,
	RESPONSE_DATA,
	RESPONSE_FATAL,
	RESPONSE_CONT,
	RESPONSE_ILLEGAL
} response_type;

static enum {
	RESPONSE_OK,
	RESPONSE_NO,
	RESPONSE_BAD,
	RESPONSE_PREAUTH,
	RESPONSE_BYE,
	RESPONSE_OTHER,
	RESPONSE_UNKNOWN
} response_status;

static char	*responded_tag;
static char	*responded_text;
static char	*responded_other_text;
static long	responded_other_number;

static enum {
	MAILBOX_DATA_FLAGS,
	MAILBOX_DATA_LIST,
	MAILBOX_DATA_LSUB,
	MAILBOX_DATA_MAILBOX,
	MAILBOX_DATA_SEARCH,
	MAILBOX_DATA_STATUS,
	MAILBOX_DATA_EXISTS,
	MAILBOX_DATA_RECENT,
	MESSAGE_DATA_EXPUNGE,
	MESSAGE_DATA_FETCH,
	CAPABILITY_DATA,
	RESPONSE_OTHER_UNKNOWN
} response_other;

static enum list_attributes {
	LIST_NONE		= 000,
	LIST_NOINFERIORS	= 001,
	LIST_NOSELECT		= 002,
	LIST_MARKED		= 004,
	LIST_UNMARKED		= 010
} list_attributes;

static int	list_hierarchy_delimiter;
static char	*list_name;

struct list_item {
	struct list_item	*l_next;
	char	*l_name;
	char	*l_base;
	enum list_attributes	l_attr;
	int	l_delim;
	int	l_level;
	int	l_has_children;
};

static char	*imapbuf;	/* TODO not static, use pool */
static size_t	imapbufsize;
static sigjmp_buf	imapjmp;
static sighandler_type savealrm;
static int	imapkeepalive;
static long	had_exists = -1;
static long	had_expunge = -1;
static long	expunged_messages;
static volatile int	imaplock;

static int	same_imap_account;

static void imap_other_get(char *pp);
static void imap_response_get(const char **cp);
static void imap_response_parse(void);
static enum okay imap_answer(struct mailbox *mp, int errprnt);
static enum okay imap_parse_list(void);
static enum okay imap_finish(struct mailbox *mp);
static void imap_timer_off(void);
static void imapcatch(int s);
static void maincatch(int s);
static enum okay imap_noop1(struct mailbox *mp);
static void rec_queue(enum rec_type type, unsigned long count);
static enum okay rec_dequeue(void);
static void rec_rmqueue(void);
static void imapalarm(int s);
static int imap_use_starttls(const char *uhp);
static enum okay imap_preauth(struct mailbox *mp, const char *xserver,
		const char *uhp);
static enum okay imap_capability(struct mailbox *mp);
static enum okay imap_auth(struct mailbox *mp, const char *uhp,
		char *xuser, char *pass);
#ifdef HAVE_MD5
static enum okay imap_cram_md5(struct mailbox *mp,
			char *xuser, char *xpass);
#endif
static enum okay imap_login(struct mailbox *mp, char *xuser, char *xpass);
#ifdef HAVE_GSSAPI
static enum okay imap_gss(struct mailbox *mp, char *user);
#endif
static enum okay imap_flags(struct mailbox *mp, unsigned X, unsigned Y);
static void imap_init(struct mailbox *mp, int n);
static void imap_setptr(struct mailbox *mp, int newmail, int transparent,
		int *prevcount);
static void imap_split(char **server, const char **sp, int *use_ssl,
		const char **cp, char const **uhp, char const **mbx,
		char **pass, char **user);
static int imap_setfile1(const char *xserver, int newmail, int isedit,
		int transparent);
static int imap_fetchdata(struct mailbox *mp, struct message *m,
		size_t expected, int need,
		const char *head, size_t headsize, long headlines);
static void imap_putstr(struct mailbox *mp, struct message *m,
		const char *str,
		const char *head, size_t headsize, long headlines);
static enum okay imap_get(struct mailbox *mp, struct message *m,
		enum needspec need);
static void commitmsg(struct mailbox *mp, struct message *to,
		struct message *from, enum havespec have);
static enum okay imap_fetchheaders(struct mailbox *mp, struct message *m,
		int bot, int top);
static enum okay imap_exit(struct mailbox *mp);
static enum okay imap_delete(struct mailbox *mp, int n, struct message *m, int
		needstat);
static enum okay imap_close(struct mailbox *mp);
static enum okay imap_update(struct mailbox *mp);
static enum okay imap_store(struct mailbox *mp, struct message *m,
		int n, int c, const char *sp, int needstat);
static enum okay imap_unstore(struct message *m, int n, const char *flag);
static const char *tag(int new);
static char *imap_putflags(int f);
static void imap_getflags(const char *cp, char const **xp, enum mflag *f);
static enum okay imap_append1(struct mailbox *mp, const char *name, FILE *fp,
		off_t off1, long xsize, enum mflag flag, time_t t);
static enum okay imap_append0(struct mailbox *mp, const char *name, FILE *fp);
static enum okay imap_list1(struct mailbox *mp, const char *base,
		struct list_item **list, struct list_item **lend, int level);
static enum okay imap_list(struct mailbox *mp, const char *base,
		int strip, FILE *fp);
static void dopr(FILE *fp);
static enum okay imap_copy1(struct mailbox *mp, struct message *m, int n,
		const char *name);
static enum okay imap_copyuid_parse(const char *cp, unsigned long *uidvalidity,
		unsigned long *olduid, unsigned long *newuid);
static enum okay imap_appenduid_parse(const char *cp,
		unsigned long *uidvalidity, unsigned long *uid);
static enum okay imap_copyuid(struct mailbox *mp, struct message *m,
		const char *name);
static enum okay imap_appenduid(struct mailbox *mp, FILE *fp, time_t t,
		long off1, long xsize, long size, long lines,
		int flag, const char *name);
static enum okay imap_appenduid_cached(struct mailbox *mp, FILE *fp);
static enum okay imap_search2(struct mailbox *mp, struct message *m,
		int count, const char *spec, int f);
static enum okay imap_remove1(struct mailbox *mp, const char *name);
static enum okay imap_rename1(struct mailbox *mp, const char *old,
		const char *new);
static char *imap_strex(char const *cp, char const **xp);
static enum okay check_expunged(void);

static void 
imap_other_get(char *pp)
{
	char	*xp;

	if (ascncasecmp(pp, "FLAGS ", 6) == 0) {
		pp += 6;
		response_other = MAILBOX_DATA_FLAGS;
	} else if (ascncasecmp(pp, "LIST ", 5) == 0) {
		pp += 5;
		response_other = MAILBOX_DATA_LIST;
	} else if (ascncasecmp(pp, "LSUB ", 5) == 0) {
		pp += 5;
		response_other = MAILBOX_DATA_LSUB;
	} else if (ascncasecmp(pp, "MAILBOX ", 8) == 0) {
		pp += 8;
		response_other = MAILBOX_DATA_MAILBOX;
	} else if (ascncasecmp(pp, "SEARCH ", 7) == 0) {
		pp += 7;
		response_other = MAILBOX_DATA_SEARCH;
	} else if (ascncasecmp(pp, "STATUS ", 7) == 0) {
		pp += 7;
		response_other = MAILBOX_DATA_STATUS;
	} else if (ascncasecmp(pp, "CAPABILITY ", 11) == 0) {
		pp += 11;
		response_other = CAPABILITY_DATA;
	} else {
		responded_other_number = strtol(pp, &xp, 10);
		while (*xp == ' ')
			xp++;
		if (ascncasecmp(xp, "EXISTS\r\n", 8) == 0) {
			response_other = MAILBOX_DATA_EXISTS;
		} else if (ascncasecmp(xp, "RECENT\r\n", 8) == 0) {
			response_other = MAILBOX_DATA_RECENT;
		} else if (ascncasecmp(xp, "EXPUNGE\r\n", 9) == 0) {
			response_other = MESSAGE_DATA_EXPUNGE;
		} else if (ascncasecmp(xp, "FETCH ", 6) == 0) {
			pp = &xp[6];
			response_other = MESSAGE_DATA_FETCH;
		} else
			response_other = RESPONSE_OTHER_UNKNOWN;
	}
	responded_other_text = pp;
}

static void 
imap_response_get(const char **cp)
{
	if (ascncasecmp(*cp, "OK ", 3) == 0) {
		*cp += 3;
		response_status = RESPONSE_OK;
	} else if (ascncasecmp(*cp, "NO ", 3) == 0) {
		*cp += 3;
		response_status = RESPONSE_NO;
	} else if (ascncasecmp(*cp, "BAD ", 4) == 0) {
		*cp += 4;
		response_status = RESPONSE_BAD;
	} else if (ascncasecmp(*cp, "PREAUTH ", 8) == 0) {
		*cp += 8;
		response_status = RESPONSE_PREAUTH;
	} else if (ascncasecmp(*cp, "BYE ", 4) == 0) {
		*cp += 4;
		response_status = RESPONSE_BYE;
	} else
		response_status = RESPONSE_OTHER;
}

static void 
imap_response_parse(void)
{
	static char	*parsebuf; /* TODO Use pool */
	static size_t	parsebufsize;
	const char	*ip = imapbuf;
	char	*pp;

	if (parsebufsize < imapbufsize)
		parsebuf = srealloc(parsebuf, parsebufsize = imapbufsize);
	memcpy(parsebuf, imapbuf, strlen(imapbuf) + 1);
	pp = parsebuf;
	switch (*ip) {
	case '+':
		response_type = RESPONSE_CONT;
		ip++;
		pp++;
		while (*ip == ' ') {
			ip++;
			pp++;
		}
		break;
	case '*':
		ip++;
		pp++;
		while (*ip == ' ') {
			ip++;
			pp++;
		}
		imap_response_get(&ip);
		pp = &parsebuf[ip - imapbuf];
		switch (response_status) {
		case RESPONSE_BYE:
			response_type = RESPONSE_FATAL;
			break;
		default:
			response_type = RESPONSE_DATA;
		}
		break;
	default:
		responded_tag = parsebuf;
		while (*pp && *pp != ' ')
			pp++;
		if (*pp == '\0') {
			response_type = RESPONSE_ILLEGAL;
			break;
		}
		*pp++ = '\0';
		while (*pp && *pp == ' ')
			pp++;
		if (*pp == '\0') {
			response_type = RESPONSE_ILLEGAL;
			break;
		}
		ip = &imapbuf[pp - parsebuf];
		response_type = RESPONSE_TAGGED;
		imap_response_get(&ip);
		pp = &parsebuf[ip - imapbuf];
	}
	responded_text = pp;
	if (response_type != RESPONSE_CONT &&
			response_type != RESPONSE_ILLEGAL &&
			response_status == RESPONSE_OTHER)
		imap_other_get(pp);
}

static enum okay 
imap_answer(struct mailbox *mp, int errprnt)
{
	int	i, complete;
	enum okay ok = STOP;

	if (mp->mb_type == MB_CACHE)
		return OKAY;
again:	if (sgetline(&imapbuf, &imapbufsize, NULL, &mp->mb_sock) > 0) {
		if (options & OPT_VERBOSE)
			fputs(imapbuf, stderr);
		imap_response_parse();
		if (response_type == RESPONSE_ILLEGAL)
			goto again;
		if (response_type == RESPONSE_CONT)
			return OKAY;
		if (response_status == RESPONSE_OTHER) {
			if (response_other == MAILBOX_DATA_EXISTS) {
				had_exists = responded_other_number;
				rec_queue(REC_EXISTS, responded_other_number);
				if (had_expunge > 0)
					had_expunge = 0;
			} else if (response_other == MESSAGE_DATA_EXPUNGE) {
				rec_queue(REC_EXPUNGE, responded_other_number);
				if (had_expunge < 0)
					had_expunge = 0;
				had_expunge++;
				expunged_messages++;
			}
		}
		complete = 0;
		if (response_type == RESPONSE_TAGGED) {
			if (asccasecmp(responded_tag, tag(0)) == 0)
				complete |= 1;
			else
				goto again;
		}
		switch (response_status) {
		case RESPONSE_PREAUTH:
			mp->mb_active &= ~MB_PREAUTH;
			/*FALLTHRU*/
		case RESPONSE_OK:
		okay:	ok = OKAY;
			complete |= 2;
			break;
		case RESPONSE_NO:
		case RESPONSE_BAD:
		stop:	ok = STOP;
			complete |= 2;
			if (errprnt)
				fprintf(stderr, tr(270, "IMAP error: %s"),
					responded_text);
			break;
		case RESPONSE_UNKNOWN:	/* does not happen */
		case RESPONSE_BYE:
			i = mp->mb_active;
			mp->mb_active = MB_NONE;
			if (i & MB_BYE)
				goto okay;
			else
				goto stop;
		case RESPONSE_OTHER:
			ok = OKAY;
		}
		if (response_status != RESPONSE_OTHER &&
				ascncasecmp(responded_text, "[ALERT] ", 8) == 0)
			fprintf(stderr, "IMAP alert: %s", &responded_text[8]);
		if (complete == 3)
			mp->mb_active &= ~MB_COMD;
	} else {
		ok = STOP;
		mp->mb_active = MB_NONE;
	}
	return ok;
}

static enum okay 
imap_parse_list(void)
{
	char	*cp;

	cp = responded_other_text;
	list_attributes = LIST_NONE;
	if (*cp == '(') {
		while (*cp && *cp != ')') {
			if (*cp == '\\') {
				if (ascncasecmp(&cp[1], "Noinferiors ", 12)
						== 0) {
					list_attributes |= LIST_NOINFERIORS;
					cp += 12;
				} else if (ascncasecmp(&cp[1], "Noselect ", 9)
						== 0) {
					list_attributes |= LIST_NOSELECT;
					cp += 9;
				} else if (ascncasecmp(&cp[1], "Marked ", 7)
						== 0) {
					list_attributes |= LIST_MARKED;
					cp += 7;
				} else if (ascncasecmp(&cp[1], "Unmarked ", 9)
						== 0) {
					list_attributes |= LIST_UNMARKED;
					cp += 9;
				}
			}
			cp++;
		}
		if (*++cp != ' ')
			return STOP;
		while (*cp == ' ')
			cp++;
	}
	list_hierarchy_delimiter = EOF;
	if (*cp == '"') {
		if (*++cp == '\\')
			cp++;
		list_hierarchy_delimiter = *cp++ & 0377;
		if (cp[0] != '"' || cp[1] != ' ')
			return STOP;
		cp++;
	} else if (cp[0] == 'N' && cp[1] == 'I' && cp[2] == 'L' &&
			cp[3] == ' ') {
		list_hierarchy_delimiter = EOF;
		cp += 3;
	}
	while (*cp == ' ')
		cp++;
	list_name = cp;
	while (*cp && *cp != '\r')
		cp++;
	*cp = '\0';
	return OKAY;
}

static enum okay 
imap_finish(struct mailbox *mp)
{
	while (mp->mb_sock.s_fd > 0 && mp->mb_active & MB_COMD)
		imap_answer(mp, 1);
	return OKAY;
}

static void 
imap_timer_off(void)
{
	if (imapkeepalive > 0) {
		alarm(0);
		safe_signal(SIGALRM, savealrm);
	}
}

/* TODO OOH MY GOOOOOOOOD WE SIGLONGJMP() FROM WITHIN SIGNAL HANDLERS!!! */
static void 
imapcatch(int s)
{
	termios_state_reset();
	switch (s) {
	case SIGINT:
		fprintf(stderr, catgets(catd, CATSET, 102, "Interrupt\n"));
		siglongjmp(imapjmp, 1);
		/*NOTREACHED*/
	case SIGPIPE:
		fprintf(stderr, "Received SIGPIPE during IMAP operation\n");
		break;
	}
}

/* TODO OOOOOHH MYYYY GOOOOOD WE DO ALL SORTS OF UNSAFE STUFF IN SIGHDLSS! */
static void
maincatch(int s)
{
	(void)s;
	if (interrupts++ == 0) {
		fprintf(stderr, catgets(catd, CATSET, 102, "Interrupt\n"));
		return;
	}
	onintr(0);
}

static enum okay 
imap_noop1(struct mailbox *mp)
{
	char	o[LINESIZE];
	FILE	*queuefp = NULL;

	snprintf(o, sizeof o, "%s NOOP\r\n", tag(1));
	IMAP_OUT(o, MB_COMD, return STOP)
	IMAP_ANSWER()
	return OKAY;
}

char const *
imap_fileof(char const *xcp)
{
	char const *cp = xcp;
	int state = 0;

	while (*cp) {
		if (cp[0] == ':' && cp[1] == '/' && cp[2] == '/') {
			cp += 3;
			state = 1;
		}
		if (cp[0] == '/' && state == 1)
			return (cp + 1);
		if (cp[0] == '/')
			return (xcp);
		++cp;
	}
	return (cp);
}

enum okay 
imap_noop(void)
{
	sighandler_type	saveint, savepipe;
	enum okay	ok = STOP;

	(void)&saveint;
	(void)&savepipe;
	(void)&ok;
	if (mb.mb_type != MB_IMAP)
		return STOP;
	imaplock = 1;
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGINT, maincatch);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1) == 0) {
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
		ok = imap_noop1(&mb);
	}
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
	if (interrupts)
		onintr(0);
	return ok;
}

static void 
rec_queue(enum rec_type type, unsigned long count)
{
	struct record	*rp;

	rp = scalloc(1, sizeof *rp);
	rp->rec_type = type;
	rp->rec_count = count;
	if (record && recend) {
		recend->rec_next = rp;
		recend = rp;
	} else
		record = recend = rp;
}

static enum okay 
rec_dequeue(void)
{
	struct message	*omessage;
	enum okay	ok = OKAY;
	struct record	*rp = record, *rq = NULL;
	unsigned long	exists = 0, i;

	if (record == NULL)
		return STOP;
	omessage = message;
	message = smalloc((msgCount+1) * sizeof *message);
	if (msgCount)
		memcpy(message, omessage, msgCount * sizeof *message);
	memset(&message[msgCount], 0, sizeof *message);
	while (rp) {
		switch (rp->rec_type) {
		case REC_EXISTS:
			exists = rp->rec_count;
			break;
		case REC_EXPUNGE:
			if (rp->rec_count == 0) {
				ok = STOP;
				break;
			}
			if (rp->rec_count > (unsigned long)msgCount) {
				if (exists == 0 || rp->rec_count > exists--)
					ok = STOP;
				break;
			}
			if (exists > 0)
				exists--;
			delcache(&mb, &message[rp->rec_count-1]);
			memmove(&message[rp->rec_count-1],
				&message[rp->rec_count],
				(msgCount - rp->rec_count + 1) *
					sizeof *message);
			msgCount--;
			/*
			 * If the message was part of a collapsed thread,
			 * the m_collapsed field of one of its ancestors
			 * should be incremented. It seems hardly possible
			 * to do this with the current message structure,
			 * though. The result is that a '+' may be shown
			 * in the header summary even if no collapsed
			 * children exists.
			 */
			break;
		}
		if (rq != NULL)
			free(rq);
		rq = rp;
		rp = rp->rec_next;
	}
	if (rq != NULL)
		free(rq);
	record = recend = NULL;
	if (ok == OKAY && exists > (unsigned long)msgCount) {
		message = srealloc(message,
				(exists + 1) * sizeof *message);
		memset(&message[msgCount], 0,
				(exists - msgCount + 1) * sizeof *message);
		for (i = msgCount; i < exists; i++)
			imap_init(&mb, i);
		imap_flags(&mb, msgCount+1, exists);
		msgCount = exists;
	}
	if (ok == STOP) {
		free(message);
		message = omessage;
	}
	return ok;
}

static void 
rec_rmqueue(void)
{
	struct record *rp;

	for (rp = record; rp != NULL;) {
		struct record *tmp = rp;
		rp = rp->rec_next;
		free(tmp);
	}
	record = recend = NULL;
}

/*ARGSUSED*/
static void 
imapalarm(int s)
{
	sighandler_type	saveint;
	sighandler_type savepipe;
	(void)s;

	if (imaplock++ == 0) {
		if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
			safe_signal(SIGINT, maincatch);
		savepipe = safe_signal(SIGPIPE, SIG_IGN);
		if (sigsetjmp(imapjmp, 1)) {
			safe_signal(SIGINT, saveint);
			safe_signal(SIGPIPE, savepipe);
			goto brk;
		}
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
		if (imap_noop1(&mb) != OKAY) {
			safe_signal(SIGINT, saveint);
			safe_signal(SIGPIPE, savepipe);
			goto out;
		}
		safe_signal(SIGINT, saveint);
		safe_signal(SIGPIPE, savepipe);
	}
brk:	alarm(imapkeepalive);
out:	imaplock--;
}

static int 
imap_use_starttls(const char *uhp)
{
	char	*var;

	if (value("imap-use-starttls"))
		return 1;
	var = savecat("imap-use-starttls-", uhp);
	return value(var) != NULL;
}

static enum okay 
imap_preauth(struct mailbox *mp, const char *xserver, const char *uhp)
{
	char *cp;

	mp->mb_active |= MB_PREAUTH;
	imap_answer(mp, 1);
	if ((cp = strchr(xserver, ':')) != NULL) {
		char *x = salloc(cp - xserver + 1);
		memcpy(x, xserver, cp - xserver);
		x[cp - xserver] = '\0';
		xserver = x;
	}
#ifdef HAVE_SSL
	if (mp->mb_sock.s_use_ssl == 0 && imap_use_starttls(uhp)) {
		FILE	*queuefp = NULL;
		char	o[LINESIZE];

		snprintf(o, sizeof o, "%s STARTTLS\r\n", tag(1));
		IMAP_OUT(o, MB_COMD, return STOP)
		IMAP_ANSWER()
		if (ssl_open(xserver, &mp->mb_sock, uhp) != OKAY)
			return STOP;
	}
#else
	if (imap_use_starttls(uhp)) {
		fprintf(stderr, "No SSL support compiled in.\n");
		return STOP;
	}
#endif
	imap_capability(mp);
	return OKAY;
}

static enum okay 
imap_capability(struct mailbox *mp)
{
	char	o[LINESIZE];
	FILE	*queuefp = NULL;
	enum okay	ok = STOP;
	const char	*cp;

	snprintf(o, sizeof o, "%s CAPABILITY\r\n", tag(1));
	IMAP_OUT(o, MB_COMD, return STOP)
	while (mp->mb_active & MB_COMD) {
		ok = imap_answer(mp, 0);
		if (response_status == RESPONSE_OTHER &&
				response_other == CAPABILITY_DATA) {
			cp = responded_other_text;
			while (*cp) {
				while (spacechar(*cp&0377))
					cp++;
				if (strncmp(cp, "UIDPLUS", 7) == 0 &&
						spacechar(cp[7]&0377))
					/* RFC 2359 */
					mp->mb_flags |= MB_UIDPLUS;
				while (*cp && !spacechar(*cp&0377))
					cp++;
			}
		}
	}
	return ok;
}

static enum okay 
imap_auth(struct mailbox *mp, const char *uhp, char *xuser, char *pass)
{
	char	*var;
	char	*auth;

	if (!(mp->mb_active & MB_PREAUTH))
		return OKAY;
	if ((auth = value("imap-auth")) == NULL) {
		size_t i = strlen(uhp) + 1;
		var = ac_alloc(i + 10);
		memcpy(var, "imap-auth-", 10);
		memcpy(var + 10, uhp, i);
		auth = value(var);
		ac_free(var);
	}
	if (auth == NULL || strcmp(auth, "login") == 0)
		return imap_login(mp, xuser, pass);
	if (strcmp(auth, "cram-md5") == 0) {
#ifdef HAVE_MD5
		return imap_cram_md5(mp, xuser, pass);
#else
		fprintf(stderr, tr(277, "No CRAM-MD5 support compiled in.\n"));
		return (STOP);
#endif
	}
	if (strcmp(auth, "gssapi") == 0) {
#ifdef HAVE_GSSAPI
		return imap_gss(mp, xuser);
#else
		fprintf(stderr, tr(272, "No GSSAPI support compiled in.\n"));
		return STOP;
#endif
	}
	fprintf(stderr, tr(273, "Unknown IMAP authentication method: %s\n"),
		auth);
	return STOP;
}

/*
 * Implementation of RFC 2194.
 */
#ifdef HAVE_MD5
static enum okay 
imap_cram_md5(struct mailbox *mp, char *xuser, char *xpass)
{
	char o[LINESIZE], *user, *pass, *cp;
	FILE *queuefp = NULL;
	enum okay ok = STOP;

jretry:
	user = xuser;
	pass = xpass;
	if (! getcredentials(&user, &pass))
		goto jleave;

	snprintf(o, sizeof o, "%s AUTHENTICATE CRAM-MD5\r\n", tag(1));
	IMAP_XOUT(o, 0, goto jleave, goto jleave);
	imap_answer(mp, 1);
	if (response_type != RESPONSE_CONT)
		goto jleave;

	cp = cram_md5_string(user, pass, responded_text);
	IMAP_XOUT(cp, MB_COMD, goto jleave, goto jleave);
	while (mp->mb_active & MB_COMD)
		ok = imap_answer(mp, 1);
	if (ok == STOP) {
		xpass = NULL;
		goto jretry;
	}
jleave:
	return ok;
}
#endif /* HAVE_MD5 */

static enum okay 
imap_login(struct mailbox *mp, char *xuser, char *xpass)
{
	char o[LINESIZE];
	char *user, *pass;
	FILE *queuefp = NULL;
	enum okay ok = STOP;

jretry:
	user = xuser;
	pass = xpass;
	if (! getcredentials(&user, &pass))
		goto jleave;

	snprintf(o, sizeof o, "%s LOGIN %s %s\r\n",
		tag(1), imap_quotestr(user), imap_quotestr(pass));
	IMAP_XOUT(o, MB_COMD, goto jleave, goto jleave);
	while (mp->mb_active & MB_COMD)
		ok = imap_answer(mp, 1);
	if (ok == STOP) {
		xpass = NULL;
		goto jretry;
	}
jleave:
	return ok;
}

#ifdef HAVE_GSSAPI
# include "imap_gssapi.c"
#endif

enum okay
imap_select(struct mailbox *mp, off_t *size, int *count, const char *mbx)
{
	enum okay ok = OKAY;
	char const*cp;
	char o[LINESIZE];
	FILE	*queuefp = NULL;
	(void)size;

	mp->mb_uidvalidity = 0;
	snprintf(o, sizeof o, "%s SELECT %s\r\n", tag(1), imap_quotestr(mbx));
	IMAP_OUT(o, MB_COMD, return STOP)
	while (mp->mb_active & MB_COMD) {
		ok = imap_answer(mp, 1);
		if (response_status != RESPONSE_OTHER &&
				(cp = asccasestr(responded_text,
						 "[UIDVALIDITY ")) != NULL)
			mp->mb_uidvalidity = atol(&cp[13]);
	}
	*count = had_exists > 0 ? had_exists : 0;
	if (response_status != RESPONSE_OTHER &&
			ascncasecmp(responded_text, "[READ-ONLY] ", 12)
			== 0)
		mp->mb_perm = 0;
	return ok;
}

static enum okay 
imap_flags(struct mailbox *mp, unsigned X, unsigned Y)
{
	char o[LINESIZE];
	FILE	*queuefp = NULL;
	char const *cp;
	struct message *m;
	unsigned 	x = X, y = Y, n;

	snprintf(o, sizeof o, "%s FETCH %u:%u (FLAGS UID)\r\n", tag(1), x, y);
	IMAP_OUT(o, MB_COMD, return STOP)
	while (mp->mb_active & MB_COMD) {
		imap_answer(mp, 1);
		if (response_status == RESPONSE_OTHER &&
				response_other == MESSAGE_DATA_FETCH) {
			n = responded_other_number;
			if (n < x || n > y)
				continue;
			m = &message[n-1];
			m->m_xsize = 0;
		} else
			continue;
		if ((cp = asccasestr(responded_other_text, "FLAGS ")) != NULL) {
			cp += 5;
			while (*cp == ' ')
				cp++;
			if (*cp == '(')
				imap_getflags(cp, &cp, &m->m_flag);
		}
		if ((cp = asccasestr(responded_other_text, "UID ")) != NULL)
			m->m_uid = strtoul(&cp[4], NULL, 10);
		getcache1(mp, m, NEED_UNSPEC, 1);
		m->m_flag &= ~MHIDDEN;
	}
	while (x <= y && message[x-1].m_xsize && message[x-1].m_time)
		x++;
	while (y > x && message[y-1].m_xsize && message[y-1].m_time)
		y--;
	if (x <= y) {
		snprintf(o, sizeof o,
			"%s FETCH %u:%u (RFC822.SIZE INTERNALDATE)\r\n",
			tag(1), x, y);
		IMAP_OUT(o, MB_COMD, return STOP)
		while (mp->mb_active & MB_COMD) {
			imap_answer(mp, 1);
			if (response_status == RESPONSE_OTHER &&
					response_other == MESSAGE_DATA_FETCH) {
				n = responded_other_number;
				if (n < x || n > y)
					continue;
				m = &message[n-1];
			} else
				continue;
			if ((cp = asccasestr(responded_other_text,
						"RFC822.SIZE ")) != NULL)
				m->m_xsize = strtol(&cp[12], NULL, 10);
			if ((cp = asccasestr(responded_other_text,
						"INTERNALDATE ")) != NULL)
				m->m_time = imap_read_date_time(&cp[13]);
		}
	}
	for (n = X; n <= Y; n++)
		putcache(mp, &message[n-1]);
	return OKAY;
}

static void 
imap_init(struct mailbox *mp, int n)
{
	struct message *m = &message[n];
	(void)mp;

	m->m_flag = MUSED|MNOFROM;
	m->m_block = 0;
	m->m_offset = 0;
}

static void 
imap_setptr(struct mailbox *mp, int newmail, int transparent, int *prevcount)
{
	struct message	*omessage = 0;
	int i, omsgCount = 0;
	enum okay	dequeued = STOP;

	if (newmail || transparent) {
		omessage = message;
		omsgCount = msgCount;
	}
	if (newmail)
		dequeued = rec_dequeue();
	if (had_exists >= 0) {
		if (dequeued != OKAY)
			msgCount = had_exists;
		had_exists = -1;
	}
	if (had_expunge >= 0) {
		if (dequeued != OKAY)
			msgCount -= had_expunge;
		had_expunge = -1;
	}
	if (newmail && expunged_messages)
		printf("Expunged %ld message%s.\n",
				expunged_messages,
				expunged_messages != 1 ? "s" : "");
	*prevcount = omsgCount - expunged_messages;
	expunged_messages = 0;
	if (msgCount < 0) {
		fputs("IMAP error: Negative message count\n", stderr);
		msgCount = 0;
	}
	if (dequeued != OKAY) {
		message = scalloc(msgCount + 1, sizeof *message);
		for (i = 0; i < msgCount; i++)
			imap_init(mp, i);
		if (!newmail && mp->mb_type == MB_IMAP)
			initcache(mp);
		if (msgCount > 0)
			imap_flags(mp, 1, msgCount);
		message[msgCount].m_size = 0;
		message[msgCount].m_lines = 0;
		rec_rmqueue();
	}
	if (newmail || transparent)
		transflags(omessage, omsgCount, transparent);
	else
		setdot(message);
}

static void
imap_split(char **server, const char **sp, int *use_ssl, const char **cp,
	char const **uhp, char const **mbx, char **pass, char **user)
{
	*sp = *server;
	if (strncmp(*sp, "imap://", 7) == 0) {
		*sp = &(*sp)[7];
		*use_ssl = 0;
#ifdef HAVE_SSL
	} else if (strncmp(*sp, "imaps://", 8) == 0) {
		*sp = &(*sp)[8];
		*use_ssl = 1;
#endif
	}
	if ((*cp = strchr(*sp, '/')) != NULL && (*cp)[1] != '\0') {
		char *x = savestr(*sp);
		x[*cp - *sp] = '\0';
		*uhp = x;
		*mbx = &(*cp)[1];
	} else {
		if (*cp)
			(*server)[*cp - *server] = '\0';
		*uhp = *sp;
		*mbx = "INBOX";
	}
	*pass = lookup_password_for_token(*uhp);
	if ((*cp = last_at_before_slash(*uhp)) != NULL) {
		*user = salloc(*cp - *uhp + 1);
		memcpy(*user, *uhp, *cp - *uhp);
		(*user)[*cp - *uhp] = '\0';
		*sp = &(*cp)[1];
		*user = urlxdec(*user);
	} else {
		*user = NULL;
		*sp = *uhp;
	}
}

int 
imap_setfile(const char *xserver, int newmail, int isedit)
{
	return imap_setfile1(xserver, newmail, isedit, 0);
}

static int 
imap_setfile1(const char *xserver, int newmail, int isedit,
	int volatile transparent)
{
	struct sock so;
	sighandler_type volatile saveint, savepipe;
	char *server, *user, *pass, *account;
	char const *cp, *sp, *mbx, *uhp;
	int use_ssl = 0, prevcount = 0;
	enum mbflags same_flags;

	server = savestr(xserver);
	if (newmail) {
		saveint = safe_signal(SIGINT, SIG_IGN);
		savepipe = safe_signal(SIGPIPE, SIG_IGN);
		if (saveint != SIG_IGN)
			safe_signal(SIGINT, imapcatch);
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
		imaplock = 1;
		goto newmail;
	}
	same_flags = mb.mb_flags;
	same_imap_account = 0;
	sp = protbase(server);
	if (mb.mb_imap_account && mb.mb_type == MB_IMAP) {
		if (mb.mb_sock.s_fd > 0 &&
				strcmp(mb.mb_imap_account, sp) == 0 &&
				disconnected(mb.mb_imap_account) == 0)
			same_imap_account = 1;
	}
	account = sstrdup(sp);
	imap_split(&server, &sp, &use_ssl, &cp, &uhp, &mbx, &pass, &user);
	so.s_fd = -1;
	if (!same_imap_account) {
		if (!disconnected(account) &&
				sopen(sp, &so, use_ssl, uhp,
					use_ssl ? "imaps" : "imap",
					(options & OPT_VERBOSE) != 0) != OKAY) {
			free(account);
			return -1;
		}
	} else
		so = mb.mb_sock;
	if (!transparent)
		quit();
	edit = (isedit != 0);
	if (mb.mb_imap_account != NULL)
		free(mb.mb_imap_account);
	mb.mb_imap_account = account;
	if (!same_imap_account) {
		if (mb.mb_sock.s_fd >= 0)
			sclose(&mb.mb_sock);
	}
	same_imap_account = 0;
	if (!transparent) {
		if (mb.mb_itf) {
			fclose(mb.mb_itf);
			mb.mb_itf = NULL;
		}
		if (mb.mb_otf) {
			fclose(mb.mb_otf);
			mb.mb_otf = NULL;
		}
		if (mb.mb_imap_mailbox != NULL)
			free(mb.mb_imap_mailbox);
		mb.mb_imap_mailbox = sstrdup(mbx);
		initbox(server);
	}
	mb.mb_type = MB_VOID;
	mb.mb_active = MB_NONE;
	imaplock = 1;
	saveint = safe_signal(SIGINT, SIG_IGN);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1)) {
		/* Not safe to use &so; save to use mb.mb_sock?? :-( TODO */
		sclose(&mb.mb_sock);
		safe_signal(SIGINT, saveint);
		safe_signal(SIGPIPE, savepipe);
		imaplock = 0;
		mb.mb_type = MB_VOID;
		mb.mb_active = MB_NONE;
		return -1;
	}
	if (saveint != SIG_IGN)
		safe_signal(SIGINT, imapcatch);
	if (savepipe != SIG_IGN)
		safe_signal(SIGPIPE, imapcatch);
	if (mb.mb_sock.s_fd < 0) {
		if (disconnected(mb.mb_imap_account)) {
			if (cache_setptr(transparent) == STOP)
				fprintf(stderr,
					"Mailbox \"%s\" is not cached.\n",
					server);
			goto done;
		}
		if ((cp = value("imap-keepalive")) != NULL) {
			if ((imapkeepalive = strtol(cp, NULL, 10)) > 0) {
				savealrm = safe_signal(SIGALRM, imapalarm);
				alarm(imapkeepalive);
			}
		}
		mb.mb_sock = so;
		mb.mb_sock.s_desc = "IMAP";
		mb.mb_sock.s_onclose = imap_timer_off;
		if (imap_preauth(&mb, sp, uhp) != OKAY ||
				imap_auth(&mb, uhp, user, pass) != OKAY) {
			sclose(&mb.mb_sock);
			imap_timer_off();
			safe_signal(SIGINT, saveint);
			safe_signal(SIGPIPE, savepipe);
			imaplock = 0;
			return -1;
		}
	} else	/* same account */
		mb.mb_flags |= same_flags;
	mb.mb_perm = (options & OPT_R_FLAG) ? 0 : MB_DELE;
	mb.mb_type = MB_IMAP;
	cache_dequeue(&mb);
	if (imap_select(&mb, &mailsize, &msgCount, mbx) != OKAY) {
		/*sclose(&mb.mb_sock);
		imap_timer_off();*/
		safe_signal(SIGINT, saveint);
		safe_signal(SIGPIPE, savepipe);
		imaplock = 0;
		mb.mb_type = MB_VOID;
		return -1;
	}
newmail:
	imap_setptr(&mb, newmail, transparent, &prevcount);
done:	setmsize(msgCount);
	if (!newmail && !transparent)
		sawcom = FAL0;
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
	if (!newmail && mb.mb_type == MB_IMAP)
		purgecache(&mb, message, msgCount);
	if ((newmail || transparent) && mb.mb_sorted) {
		mb.mb_threaded = 0;
		sort((void *)-1);
	}
	if (!newmail && !edit && msgCount == 0) {
		if ((mb.mb_type == MB_IMAP || mb.mb_type == MB_CACHE) &&
				value("emptystart") == NULL)
			fprintf(stderr, catgets(catd, CATSET, 258,
				"No mail at %s\n"), server);
		return 1;
	}
	if (newmail)
		newmailinfo(prevcount);
	return 0;
}

static int
imap_fetchdata(struct mailbox *mp, struct message *m, size_t expected,
		int need,
		const char *head, size_t headsize, long headlines)
{
	char	*line = NULL, *lp;
	size_t	linesize = 0, linelen, size = 0;
	int	emptyline = 0, lines = 0, excess = 0;
	off_t	offset;

	fseek(mp->mb_otf, 0L, SEEK_END);
	offset = ftell(mp->mb_otf);
	if (head)
		fwrite(head, 1, headsize, mp->mb_otf);
	while (sgetline(&line, &linesize, &linelen, &mp->mb_sock) > 0) {
		lp = line;
		if (linelen > expected) {
			excess = linelen - expected;
			linelen = expected;
		}
		/* TODO >>
		 * Need to mask 'From ' lines. This cannot be done properly
		 * since some servers pass them as 'From ' and others as
		 * '>From '. Although one could identify the first kind of
		 * server in principle, it is not possible to identify the
		 * second as '>From ' may also come from a server of the
		 * first type as actual data. So do what is absolutely
		 * necessary only - mask 'From '.
		 *
		 * If the line is the first line of the message header, it
		 * is likely a real 'From ' line. In this case, it is just
		 * ignored since it violates all standards.
		 * TODO can the latter *really* happen??
		 * TODO <<
		 */
		/*
		 * Since we simply copy over data without doing any transfer
		 * encoding reclassification/adjustment we *have* to perform
		 * RFC 4155 compliant From_ quoting here
		 */
		if (is_head(lp, linelen)) {
			if (lines + headlines == 0)
				goto skip;
			fputc('>', mp->mb_otf);
			++size;
		}
		if (lp[linelen-1] == '\n' && (linelen == 1 ||
					lp[linelen-2] == '\r')) {
			emptyline = linelen <= 2;
			if (linelen > 2) {
				fwrite(lp, 1, linelen - 2, mp->mb_otf);
				size += linelen - 1;
			} else
				size++;
			fputc('\n', mp->mb_otf);
		} else {
			emptyline = 0;
			fwrite(lp, 1, linelen, mp->mb_otf);
			size += linelen;
		}
		lines++;
	skip:	if ((expected -= linelen) <= 0)
			break;
	}
	if (!emptyline) {
		/*
		 * This is very ugly; but some IMAP daemons don't end a
		 * message with \r\n\r\n, and we need \n\n for mbox format.
		 */
		fputc('\n', mp->mb_otf);
		lines++;
		size++;
	}
	fflush(mp->mb_otf);
	if (m != NULL) {
		m->m_size = size + headsize;
		m->m_lines = lines + headlines;
		m->m_block = mailx_blockof(offset);
		m->m_offset = mailx_offsetof(offset);
		switch (need) {
		case NEED_HEADER:
			m->m_have |= HAVE_HEADER;
			break;
		case NEED_BODY:
			m->m_have |= HAVE_HEADER|HAVE_BODY;
			m->m_xlines = m->m_lines;
			m->m_xsize = m->m_size;
			break;
		}
	}
	free(line);
	return excess;
}

static void
imap_putstr(struct mailbox *mp, struct message *m, const char *str,
		const char *head, size_t headsize, long headlines)
{
	off_t	offset;
	size_t	len;

	len = strlen(str);
	fseek(mp->mb_otf, 0L, SEEK_END);
	offset = ftell(mp->mb_otf);
	if (head)
		fwrite(head, 1, headsize, mp->mb_otf);
	if (len > 0) {
		fwrite(str, 1, len, mp->mb_otf);
		fputc('\n', mp->mb_otf);
		len++;
	}
	fflush(mp->mb_otf);
	if (m != NULL) {
		m->m_size = headsize + len;
		m->m_lines = headlines + 1;
		m->m_block = mailx_blockof(offset);
		m->m_offset = mailx_offsetof(offset);
		m->m_have |= HAVE_HEADER|HAVE_BODY;
		m->m_xlines = m->m_lines;
		m->m_xsize = m->m_size;
	}
}

static enum okay 
imap_get(struct mailbox *mp, struct message *m, enum needspec need)
{
	char o[LINESIZE];
	struct message mt;
	sighandler_type	saveint = SIG_IGN, savepipe = SIG_IGN;
	char *volatile head = NULL;
	char const *cp = NULL, *loc = NULL,
		*volatile item = NULL, *volatile resp = NULL;
	size_t expected;
	size_t volatile headsize = 0;
	int number = m - message + 1;
	enum okay ok = STOP;
	FILE *queuefp = NULL;
	long volatile headlines = 0;
	long n = -1;
	unsigned long	u = 0;

	if (getcache(mp, m, need) == OKAY)
		return OKAY;
	if (mp->mb_type == MB_CACHE) {
		fprintf(stderr, "Message %u not available.\n", number);
		return STOP;
	}
	if (mp->mb_sock.s_fd < 0) {
		fprintf(stderr, "IMAP connection closed.\n");
		return STOP;
	}
	switch (need) {
	case NEED_HEADER:
		resp = item = "RFC822.HEADER";
		break;
	case NEED_BODY:
		item = "BODY.PEEK[]";
		resp = "BODY[]";
		if (m->m_flag & HAVE_HEADER && m->m_size) {
			char	*hdr = smalloc(m->m_size);
			fflush(mp->mb_otf);
			if (fseek(mp->mb_itf, (long)mailx_positionof(m->m_block,
						m->m_offset), SEEK_SET) < 0 ||
					fread(hdr, 1, m->m_size, mp->mb_itf)
						!= m->m_size) {
				free(hdr);
				break;
			}
			head = hdr;
			headsize = m->m_size;
			headlines = m->m_lines;
			item = "BODY.PEEK[TEXT]";
			resp = "BODY[TEXT]";
		}
		break;
	case NEED_UNSPEC:
		return STOP;
	}
	imaplock = 1;
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1)) {
		safe_signal(SIGINT, saveint);
		safe_signal(SIGPIPE, savepipe);
		imaplock = 0;
		return STOP;
	}
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGINT, maincatch);
	if (savepipe != SIG_IGN)
		safe_signal(SIGPIPE, imapcatch);
	if (m->m_uid)
		snprintf(o, sizeof o,
				"%s UID FETCH %lu (%s)\r\n",
				tag(1), m->m_uid, item);
	else {
		if (check_expunged() == STOP)
			goto out;
		snprintf(o, sizeof o,
				"%s FETCH %u (%s)\r\n",
				tag(1), number, item);
	}
	IMAP_OUT(o, MB_COMD, goto out)
	for (;;) {
		ok = imap_answer(mp, 1);
		if (ok == STOP)
			break;
		if (response_status != RESPONSE_OTHER ||
				response_other != MESSAGE_DATA_FETCH)
			continue;
		if ((loc = asccasestr(responded_other_text, resp)) == NULL)
			continue;
		if (m->m_uid) {
			if ((cp = asccasestr(responded_other_text, "UID "))) {
				u = atol(&cp[4]);
				n = 0;
			} else {
				n = -1;
				u = 0;
			}
		} else
			n = responded_other_number;
		if ((cp = strrchr(responded_other_text, '{')) == NULL) {
			if (m->m_uid ? m->m_uid != u : n != number)
				continue;
			if ((cp = strchr(loc, '"')) != NULL) {
				cp = imap_unquotestr(cp);
				imap_putstr(mp, m, cp,
						head, headsize, headlines);
			} else {
				m->m_have |= HAVE_HEADER|HAVE_BODY;
				m->m_xlines = m->m_lines;
				m->m_xsize = m->m_size;
			}
			goto out;
		}
		expected = atol(&cp[1]);
		if (m->m_uid ? n == 0 && m->m_uid != u : n != number) {
			imap_fetchdata(mp, NULL, expected, need, NULL, 0, 0);
			continue;
		}
		mt = *m;
		imap_fetchdata(mp, &mt, expected, need,
				head, headsize, headlines);
		if (n >= 0) {
			commitmsg(mp, m, &mt, mt.m_have);
			break;
		}
		if (n == -1 && sgetline(&imapbuf, &imapbufsize, NULL,
						&mp->mb_sock) > 0) {
			if (options & OPT_VERBOSE)
				fputs(imapbuf, stderr);
			if ((cp = asccasestr(imapbuf, "UID ")) != NULL) {
				u = atol(&cp[4]);
				if (u == m->m_uid) {
					commitmsg(mp, m, &mt, mt.m_have);
					break;
				}
			}
		}
	}
out:	while (mp->mb_active & MB_COMD)
		ok = imap_answer(mp, 1);
	if (saveint != SIG_IGN)
		safe_signal(SIGINT, saveint);
	if (savepipe != SIG_IGN)
		safe_signal(SIGPIPE, savepipe);
	imaplock--;
	if (ok == OKAY)
		putcache(mp, m);
	if (head != NULL)
		free(head);
	if (interrupts)
		onintr(0);
	return ok;
}

enum okay 
imap_header(struct message *m)
{
	return imap_get(&mb, m, NEED_HEADER);
}


enum okay 
imap_body(struct message *m)
{
	return imap_get(&mb, m, NEED_BODY);
}

static void 
commitmsg(struct mailbox *mp, struct message *to, struct message *from,
	enum havespec have)
{
	to->m_size = from->m_size;
	to->m_lines = from->m_lines;
	to->m_block = from->m_block;
	to->m_offset = from->m_offset;
	to->m_have = have;
	if (have & HAVE_BODY) {
		to->m_xlines = from->m_lines;
		to->m_xsize = from->m_size;
	}
	putcache(mp, to);
}

static enum okay 
imap_fetchheaders (
    struct mailbox *mp,
    struct message *m,
    int bot,
    int top	/* bot > top */
)
{
	char	o[LINESIZE];
	char const *cp;
	struct message	mt;
	size_t	expected;
	enum okay	ok;
	int	n = 0, u;
	FILE	*queuefp = NULL;

	if (m[bot].m_uid)
		snprintf(o, sizeof o,
			"%s UID FETCH %lu:%lu (RFC822.HEADER)\r\n",
			tag(1), m[bot-1].m_uid, m[top-1].m_uid);
	else {
		if (check_expunged() == STOP)
			return STOP;
		snprintf(o, sizeof o,
			"%s FETCH %u:%u (RFC822.HEADER)\r\n",
			tag(1), bot, top);
	}
	IMAP_OUT(o, MB_COMD, return STOP)
	for (;;) {
		ok = imap_answer(mp, 1);
		if (response_status != RESPONSE_OTHER)
			break;
		if (response_other != MESSAGE_DATA_FETCH)
			continue;
		if (ok == STOP || (cp=strrchr(responded_other_text, '{')) == 0)
			return STOP;
		if (asccasestr(responded_other_text, "RFC822.HEADER") == NULL)
			continue;
		expected = atol(&cp[1]);
		if (m[bot-1].m_uid) {
			if ((cp=asccasestr(responded_other_text, "UID "))) {
				u = atoi(&cp[4]);
				for (n = bot; n <= top; n++)
					if ((unsigned long)u == m[n-1].m_uid)
						break;
				if (n > top) {
					imap_fetchdata(mp, NULL, expected,
							NEED_HEADER,
							NULL, 0, 0);
					continue;
				}
			} else
				n = -1;
		} else {
			n = responded_other_number;
			if (n <= 0 || n > msgCount) {
				imap_fetchdata(mp, NULL, expected, NEED_HEADER,
						NULL, 0, 0);
				continue;
			}
		}
		imap_fetchdata(mp, &mt, expected, NEED_HEADER, NULL, 0, 0);
		if (n >= 0 && !(m[n-1].m_have & HAVE_HEADER))
			commitmsg(mp, &m[n-1], &mt, HAVE_HEADER);
		if (n == -1 && sgetline(&imapbuf, &imapbufsize, NULL,
					&mp->mb_sock) > 0) {
			if (options & OPT_VERBOSE)
				fputs(imapbuf, stderr);
			if ((cp = asccasestr(imapbuf, "UID ")) != NULL) {
				u = atoi(&cp[4]);
				for (n = bot; n <= top; n++)
					if ((unsigned long)u == m[n-1].m_uid)
						break;
				if (n <= top && !(m[n-1].m_have & HAVE_HEADER))
					commitmsg(mp, &m[n-1], &mt,HAVE_HEADER);
				n = 0;
			}
		}
	}
	while (mp->mb_active & MB_COMD)
		ok = imap_answer(mp, 1);
	return ok;
}

void 
imap_getheaders(int volatile bot, int top)
{
	sighandler_type	saveint, savepipe;
	/* XXX enum okay ok = STOP;*/
	int i, chunk = 256;

	if (mb.mb_type == MB_CACHE)
		return;
	if (bot < 1)
		bot = 1;
	if (top > msgCount)
		top = msgCount;
	for (i = bot; i < top; i++) {
		if (message[i-1].m_have & HAVE_HEADER ||
				getcache(&mb, &message[i-1], NEED_HEADER)
				== OKAY)
			bot = i+1;
		else
			break;
	}
	for (i = top; i > bot; i--) {
		if (message[i-1].m_have & HAVE_HEADER ||
				getcache(&mb, &message[i-1], NEED_HEADER)
				== OKAY)
			top = i-1;
		else
			break;
	}
	if (bot >= top)
		return;
	imaplock = 1;
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGINT, maincatch);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1) == 0) {
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
		for (i = bot; i <= top; i += chunk) {
			/*ok = */imap_fetchheaders(&mb, message, i,
					i+chunk-1 < top ? i+chunk-1 : top);
			if (interrupts)
				onintr(0);
		}
	}
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
}

static enum okay 
__imap_exit(struct mailbox *mp)
{
	char	o[LINESIZE];
	FILE	*queuefp = NULL;

	mp->mb_active |= MB_BYE;
	snprintf(o, sizeof o, "%s LOGOUT\r\n", tag(1));
	IMAP_OUT(o, MB_COMD, return STOP)
	IMAP_ANSWER()
	return OKAY;
}

static enum okay 
imap_exit(struct mailbox *mp)
{
	enum okay ret = __imap_exit(mp);
#if 0 /* TODO the option today: memory leak(s) and halfway reuse or nottin */
	free(mp->mb_imap_account);
	free(mp->mb_imap_mailbox);
	if (mp->mb_cache_directory != NULL)
		free(mp->mb_cache_directory);
#ifndef HAVE_ASSERTS /* TODO ASSERT LEGACY */
	mp->mb_imap_account =
	mp->mb_imap_mailbox =
	mp->mb_cache_directory = "";
#else
	mp->mb_imap_account = NULL; /* for assert legacy time.. */
	mp->mb_imap_mailbox = NULL;
	mp->mb_cache_directory = NULL;
#endif
#endif
	sclose(&mp->mb_sock);
	return ret;
}

static enum okay
imap_delete(struct mailbox *mp, int n, struct message *m, int needstat)
{
	imap_store(mp, m, n, '+', "\\Deleted", needstat);
	if (mp->mb_type == MB_IMAP)
		delcache(mp, m);
	return OKAY;
}

static enum okay 
imap_close(struct mailbox *mp)
{
	char	o[LINESIZE];
	FILE	*queuefp = NULL;

	snprintf(o, sizeof o, "%s CLOSE\r\n", tag(1));
	IMAP_OUT(o, MB_COMD, return STOP)
	IMAP_ANSWER()
	return OKAY;
}

static enum okay 
imap_update(struct mailbox *mp)
{
	struct message *m;
	int dodel, c, gotcha = 0, held = 0, modflags = 0, needstat, stored = 0;

	if (!edit && mp->mb_perm != 0) {
		holdbits();
		for (m = &message[0], c = 0; m < &message[msgCount]; m++) {
			if (m->m_flag & MBOX)
				c++;
		}
		if (c > 0)
			if (makembox() == STOP)
				goto bypass;
	}
	for (m = &message[0], gotcha=0, held=0; m < &message[msgCount]; m++) {
		if (mp->mb_perm == 0) {
			dodel = 0;
		} else if (edit) {
			dodel = m->m_flag & MDELETED;
		} else {
			dodel = !((m->m_flag&MPRESERVE) ||
					(m->m_flag&MTOUCH) == 0);
		}
		/*
		 * Fetch the result after around each 800 STORE commands
		 * sent (approx. 32k data sent). Otherwise, servers will
		 * try to flush the return queue at some point, leading
		 * to a deadlock if we are still writing commands but not
		 * reading their results.
		 */
		needstat = stored > 0 && stored % 800 == 0;
		/*
		 * Even if this message has been deleted, continue
		 * to set further flags. This is necessary to support
		 * Gmail semantics, where "delete" actually means
		 * "archive", and the flags are applied to the copy
		 * in "All Mail".
		 */
		if ((m->m_flag&(MREAD|MSTATUS)) == (MREAD|MSTATUS)) {
			imap_store(mp, m, m-message+1,
					'+', "\\Seen", needstat);
			stored++;
		}
		if (m->m_flag & MFLAG) {
			imap_store(mp, m, m-message+1,
					'+', "\\Flagged", needstat);
			stored++;
		}
		if (m->m_flag & MUNFLAG) {
			imap_store(mp, m, m-message+1,
					'-', "\\Flagged", needstat);
			stored++;
		}
		if (m->m_flag & MANSWER) {
			imap_store(mp, m, m-message+1,
					'+', "\\Answered", needstat);
			stored++;
		}
		if (m->m_flag & MUNANSWER) {
			imap_store(mp, m, m-message+1,
					'-', "\\Answered", needstat);
			stored++;
		}
		if (m->m_flag & MDRAFT) {
			imap_store(mp, m, m-message+1,
					'+', "\\Draft", needstat);
			stored++;
		}
		if (m->m_flag & MUNDRAFT) {
			imap_store(mp, m, m-message+1,
					'-', "\\Draft", needstat);
			stored++;
		}
		if (dodel) {
			imap_delete(mp, m-message+1, m, needstat);
			stored++;
			gotcha++;
		} else if (mp->mb_type != MB_CACHE ||
			(! edit && ! (m->m_flag&(MBOXED|MSAVED|MDELETED))) ||
				(m->m_flag & (MBOXED|MPRESERVE|MTOUCH)) ==
					(MPRESERVE|MTOUCH) ||
				(edit && ! (m->m_flag & MDELETED)))
			held++;
		if (m->m_flag & MNEW) {
			m->m_flag &= ~MNEW;
			m->m_flag |= MSTATUS;
		}
	}
bypass:
	if (gotcha)
		imap_close(mp);
	for (m = &message[0]; m < &message[msgCount]; m++)
		if (!(m->m_flag&MUNLINKED) &&
				m->m_flag&(MBOXED|MDELETED|MSAVED|MSTATUS|
					MFLAG|MUNFLAG|MANSWER|MUNANSWER|
					MDRAFT|MUNDRAFT)) {
			putcache(mp, m);
			modflags++;
		}
	if ((gotcha || modflags) && edit) {
		printf(tr(168, "\"%s\" "), displayname);
		printf(value("bsdcompat") || value("bsdmsgs") ?
				catgets(catd, CATSET, 170, "complete\n") :
				catgets(catd, CATSET, 212, "updated.\n"));
	} else if (held && !edit && mp->mb_perm != 0) {
		if (held == 1)
			printf(tr(155, "Held 1 message in %s\n"), displayname);
		else if (held > 1)
			printf(tr(156, "Held %d messages in %s\n"), held,
				displayname);
	}
	fflush(stdout);
	return OKAY;
}

void 
imap_quit(void)
{
	sighandler_type	saveint;
	sighandler_type savepipe;

	if (mb.mb_type == MB_CACHE) {
		imap_update(&mb);
		return;
	}
	if (mb.mb_sock.s_fd < 0) {
		fprintf(stderr, "IMAP connection closed.\n");
		return;
	}
	imaplock = 1;
	saveint = safe_signal(SIGINT, SIG_IGN);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1)) {
		safe_signal(SIGINT, saveint);
		safe_signal(SIGPIPE, saveint);
		imaplock = 0;
		return;
	}
	if (saveint != SIG_IGN)
		safe_signal(SIGINT, imapcatch);
	if (savepipe != SIG_IGN)
		safe_signal(SIGPIPE, imapcatch);
	imap_update(&mb);
	if (!same_imap_account) {
		imap_exit(&mb);
	}
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
}

static enum okay 
imap_store(struct mailbox *mp, struct message *m, int n,
		int c, const char *sp, int needstat)
{
	char	o[LINESIZE];
	FILE	*queuefp = NULL;

	if (mp->mb_type == MB_CACHE && (queuefp = cache_queue(mp)) == NULL)
		return STOP;
	if (m->m_uid)
		snprintf(o, sizeof o,
				"%s UID STORE %lu %cFLAGS (%s)\r\n",
				tag(1), m->m_uid, c, sp);
	else {
		if (check_expunged() == STOP)
			return STOP;
		snprintf(o, sizeof o,
				"%s STORE %u %cFLAGS (%s)\r\n",
				tag(1), n, c, sp);
	}
	IMAP_OUT(o, MB_COMD, return STOP)
	if (needstat)
		IMAP_ANSWER()
	else
		mb.mb_active &= ~MB_COMD;
	if (queuefp != NULL)
		Fclose(queuefp);
	return OKAY;
}

enum okay 
imap_undelete(struct message *m, int n)
{
	return imap_unstore(m, n, "\\Deleted");
}

enum okay 
imap_unread(struct message *m, int n)
{
	return imap_unstore(m, n, "\\Seen");
}

static enum okay 
imap_unstore(struct message *m, int n, const char *flag)
{
	sighandler_type	saveint, savepipe;
	enum okay	ok = STOP;

	(void)&saveint;
	(void)&savepipe;
	(void)&ok;
	imaplock = 1;
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGINT, maincatch);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1) == 0) {
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
		ok = imap_store(&mb, m, n, '-', flag, 1);
	}
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
	if (interrupts)
		onintr(0);
	return ok;
}

static const char *
tag(int new)
{
	static char	ts[20];
	static long	n;

	if (new)
		n++;
	snprintf(ts, sizeof ts, "T%lu", n);
	return ts;
}

int 
imap_imap(void *vp)
{
	sighandler_type	saveint, savepipe;
	char	o[LINESIZE];
	enum okay	ok = STOP;
	struct mailbox	*mp = &mb;
	FILE	*queuefp = NULL;

	(void)&saveint;
	(void)&savepipe;
	(void)&ok;
	if (mp->mb_type != MB_IMAP) {
		printf("Not operating on an IMAP mailbox.\n");
		return 1;
	}
	imaplock = 1;
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGINT, maincatch);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1) == 0) {
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
		snprintf(o, sizeof o, "%s %s\r\n", tag(1), (char *)vp);
		IMAP_OUT(o, MB_COMD, goto out)
		while (mp->mb_active & MB_COMD) {
			ok = imap_answer(mp, 0);
			fputs(responded_text, stdout);
		}
	}
out:	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
	if (interrupts)
		onintr(0);
	return ok != OKAY;
}

int 
imap_newmail(int autoinc)
{
	if (autoinc && had_exists < 0 && had_expunge < 0) {
		imaplock = 1;
		imap_noop();
		imaplock = 0;
	}
	if (had_exists == msgCount && had_expunge < 0)
		/*
		 * Some servers always respond with EXISTS to NOOP. If
		 * the mailbox has been changed but the number of messages
		 * has not, an EXPUNGE must also had been sent; otherwise,
		 * nothing has changed.
		 */
		had_exists = -1;
	return had_expunge >= 0 ? 2 : had_exists >= 0 ? 1 : 0;
}

static char *
imap_putflags(int f)
{
	const char	*cp;
	char	*buf, *bp;

	bp = buf = salloc(100);
	if (f & (MREAD|MFLAGGED|MANSWERED|MDRAFTED)) {
		*bp++ = '(';
		if (f & MREAD) {
			if (bp[-1] != '(')
				*bp++ = ' ';
			for (cp = "\\Seen"; *cp; cp++)
				*bp++ = *cp;
		}
		if (f & MFLAGGED) {
			if (bp[-1] != '(')
				*bp++ = ' ';
			for (cp = "\\Flagged"; *cp; cp++)
				*bp++ = *cp;
		}
		if (f & MANSWERED) {
			if (bp[-1] != '(')
				*bp++ = ' ';
			for (cp = "\\Answered"; *cp; cp++)
				*bp++ = *cp;
		}
		if (f & MDRAFT) {
			if (bp[-1] != '(')
				*bp++ = ' ';
			for (cp = "\\Draft"; *cp; cp++)
				*bp++ = *cp;
		}
		*bp++ = ')';
		*bp++ = ' ';
	}
	*bp = '\0';
	return buf;
}

static void 
imap_getflags(const char *cp, char const **xp, enum mflag *f)
{
	while (*cp != ')') {
		if (*cp == '\\') {
			if (ascncasecmp(cp, "\\Seen", 5) == 0)
				*f |= MREAD;
			else if (ascncasecmp(cp, "\\Recent", 7) == 0)
				*f |= MNEW;
			else if (ascncasecmp(cp, "\\Deleted", 8) == 0)
				*f |= MDELETED;
			else if (ascncasecmp(cp, "\\Flagged", 8) == 0)
				*f |= MFLAGGED;
			else if (ascncasecmp(cp, "\\Answered", 9) == 0)
				*f |= MANSWERED;
			else if (ascncasecmp(cp, "\\Draft", 6) == 0)
				*f |= MDRAFTED;
		}
		cp++;
	}
	if (xp)
		*xp = cp;
}

static enum okay
imap_append1(struct mailbox *mp, const char *name, FILE *fp,
		off_t off1, long xsize, enum mflag flag, time_t t)
{
	char	o[LINESIZE];
	char	*buf;
	size_t	bufsize, buflen, count;
	enum okay	ok = STOP;
	long	size, lines, ysize;
	int	twice = 0;
	FILE	*queuefp = NULL;

	if (mp->mb_type == MB_CACHE) {
		queuefp = cache_queue(mp);
		if (queuefp == NULL)
			return STOP;
		ok = OKAY;
	}
	buf = smalloc(bufsize = LINESIZE);
	buflen = 0;
again:	size = xsize;
	count = fsize(fp);
	if (fseek(fp, off1, SEEK_SET) < 0) {
		ok = STOP;
		goto out;
	}
	snprintf(o, sizeof o, "%s APPEND %s %s%s {%ld}\r\n",
			tag(1), imap_quotestr(name),
			imap_putflags(flag),
			imap_make_date_time(t),
			size);
	IMAP_XOUT(o, MB_COMD, goto out, ok = STOP;goto out)
	while (mp->mb_active & MB_COMD) {
		ok = imap_answer(mp, twice);
		if (response_type == RESPONSE_CONT)
			break;
	}
	if (mp->mb_type != MB_CACHE && ok == STOP) {
		if (twice == 0)
			goto trycreate;
		else
			goto out;
	}
	lines = ysize = 0;
	while (size > 0) {
		fgetline(&buf, &bufsize, &count, &buflen, fp, 1);
		lines++;
		ysize += buflen;
		buf[buflen-1] = '\r';
		buf[buflen] = '\n';
		if (mp->mb_type != MB_CACHE)
			swrite1(&mp->mb_sock, buf, buflen+1, 1);
		else if (queuefp)
			fwrite(buf, 1, buflen+1, queuefp);
		size -= buflen+1;
	}
	if (mp->mb_type != MB_CACHE)
		swrite(&mp->mb_sock, "\r\n");
	else if (queuefp)
		fputs("\r\n", queuefp);
	while (mp->mb_active & MB_COMD) {
		ok = imap_answer(mp, 0);
		if (response_status == RESPONSE_NO /*&&
				ascncasecmp(responded_text,
					"[TRYCREATE] ", 12) == 0*/) {
	trycreate:	if (twice++) {
				ok = STOP;
				goto out;
			}
			snprintf(o, sizeof o, "%s CREATE %s\r\n",
					tag(1),
					imap_quotestr(name));
			IMAP_XOUT(o, MB_COMD, goto out, ok = STOP;goto out)
			while (mp->mb_active & MB_COMD)
				ok = imap_answer(mp, 1);
			if (ok == STOP)
				goto out;
			imap_created_mailbox++;
			goto again;
		} else if (ok != OKAY)
			fprintf(stderr, tr(270, "IMAP error: %s"),
				responded_text);
		else if (response_status == RESPONSE_OK &&
				mp->mb_flags & MB_UIDPLUS)
			imap_appenduid(mp, fp, t, off1, xsize, ysize, lines,
					flag, name);
	}
out:	if (queuefp != NULL)
		Fclose(queuefp);
	free(buf);
	return ok;
}

static enum okay
imap_append0(struct mailbox *mp, const char *name, FILE *fp)
{
	char	*buf, *bp, *lp;
	size_t	bufsize, buflen, count;
	off_t	off1 = -1, offs;
	int	inhead = 1;
	int	flag = MNEW|MNEWEST;
	long	size = 0;
	time_t	tim;
	enum okay	ok;

	buf = smalloc(bufsize = LINESIZE);
	buflen = 0;
	count = fsize(fp);
	offs = ftell(fp);
	time(&tim);
	do {
		bp = fgetline(&buf, &bufsize, &count, &buflen, fp, 1);
		if (bp == NULL || strncmp(buf, "From ", 5) == 0) {
			if (off1 != (off_t)-1) {
				ok=imap_append1(mp, name, fp, off1,
						size, flag, tim);
				if (ok == STOP)
					return STOP;
				fseek(fp, offs+buflen, SEEK_SET);
			}
			off1 = offs + buflen;
			size = 0;
			inhead = 1;
			flag = MNEW;
			if (bp != NULL)
				tim = unixtime(buf);
		} else
			size += buflen+1;
		offs += buflen;
		if (bp && buf[0] == '\n')
			inhead = 0;
		else if (bp && inhead && ascncasecmp(buf, "status", 6) == 0) {
			lp = &buf[6];
			while (whitechar(*lp&0377))
				lp++;
			if (*lp == ':')
				while (*++lp != '\0')
					switch (*lp) {
					case 'R':
						flag |= MREAD;
						break;
					case 'O':
						flag &= ~MNEW;
						break;
					}
		} else if (bp && inhead &&
				ascncasecmp(buf, "x-status", 8) == 0) {
			lp = &buf[8];
			while (whitechar(*lp&0377))
				lp++;
			if (*lp == ':')
				while (*++lp != '\0')
					switch (*lp) {
					case 'F':
						flag |= MFLAGGED;
						break;
					case 'A':
						flag |= MANSWERED;
						break;
					case 'T':
						flag |= MDRAFTED;
						break;
					}
		}
	} while (bp != NULL);
	free(buf);
	return OKAY;
}

enum okay
imap_append(const char *xserver, FILE *fp)
{
	sighandler_type	saveint, savepipe;
	char *server, *user, *pass;
	char const *sp, *cp, *mbx, *uhp;
	int use_ssl;
	enum okay ok = STOP;

	server = savestr(xserver);
	imap_split(&server, &sp, &use_ssl, &cp, &uhp, &mbx, &pass, &user);
	imaplock = 1;
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGINT, maincatch);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1))
		goto out;
	if (savepipe != SIG_IGN)
		safe_signal(SIGPIPE, imapcatch);
	if ((mb.mb_type == MB_CACHE || mb.mb_sock.s_fd > 0) &&
			mb.mb_imap_account &&
			strcmp(protbase(server), mb.mb_imap_account) == 0) {
		ok = imap_append0(&mb, mbx, fp);
	}
	else {
		struct mailbox	mx;

		memset(&mx, 0, sizeof mx);
		if (disconnected(server) == 0) {
			if (sopen(sp, &mx.mb_sock, use_ssl, uhp,
					use_ssl ? "imaps" : "imap",
					(options & OPT_VERBOSE) != 0) != OKAY)
				goto fail;
			mx.mb_sock.s_desc = "IMAP";
			mx.mb_type = MB_IMAP;
			mx.mb_imap_account = (char *)protbase(server);
			/* TODO the code now did
			 * TODO mx.mb_imap_mailbox = mbx;
			 * TODO though imap_mailbox is sfree()d and mbx
			 * TODO is possibly even a constant
			 * TODO i changed this to sstrdup() sofar, as is used
			 * TODO somewhere else in this file for this! */
			mx.mb_imap_mailbox = sstrdup(mbx);
			if (imap_preauth(&mx, sp, uhp) != OKAY ||
					imap_auth(&mx, uhp, user, pass)!=OKAY) {
				sclose(&mx.mb_sock);
				goto fail;
			}
			ok = imap_append0(&mx, mbx, fp);
			imap_exit(&mx);
		} else {
			mx.mb_imap_account = (char *)protbase(server);
			mx.mb_imap_mailbox = sstrdup(mbx); /* TODO as above */
			mx.mb_type = MB_CACHE;
			ok = imap_append0(&mx, mbx, fp);
		}
	fail:;
	}
out:	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
	if (interrupts)
		onintr(0);
	return ok;
}

static enum okay 
imap_list1(struct mailbox *mp, const char *base, struct list_item **list,
		struct list_item **lend, int level)
{
	char	o[LINESIZE];
	enum okay	ok = STOP;
	char	*cp;
	const char	*bp;
	FILE	*queuefp = NULL;
	struct list_item	*lp;

	*list = *lend = NULL;
	snprintf(o, sizeof o, "%s LIST %s %%\r\n",
			tag(1), imap_quotestr(base));
	IMAP_OUT(o, MB_COMD, return STOP)
	while (mp->mb_active & MB_COMD) {
		ok = imap_answer(mp, 1);
		if (response_status == RESPONSE_OTHER &&
				response_other == MAILBOX_DATA_LIST &&
				imap_parse_list() == OKAY) {
			cp = imap_unquotestr(list_name);
			lp = csalloc(1, sizeof *lp);
			lp->l_name = cp;
			for (bp = base; *bp && *bp == *cp; bp++)
				cp++;
			lp->l_base = *cp ? cp : savestr(base);
			lp->l_attr = list_attributes;
			lp->l_level = level+1;
			lp->l_delim = list_hierarchy_delimiter;
			if (*list && *lend) {
				(*lend)->l_next = lp;
				*lend = lp;
			} else
				*list = *lend = lp;
		}
	}
	return ok;
}

static enum okay
imap_list(struct mailbox *mp, const char *base, int strip, FILE *fp)
{
	struct list_item	*list, *lend, *lp, *lx, *ly;
	int	n;
	const char	*bp;
	char	*cp;
	int	depth;

	depth = (cp = value("imap-list-depth")) != NULL ? atoi(cp) : 2;
	if (imap_list1(mp, base, &list, &lend, 0) == STOP)
		return STOP;
	if (list == NULL || lend == NULL)
		return OKAY;
	for (lp = list; lp; lp = lp->l_next)
		if (lp->l_delim != '/' && lp->l_delim != EOF &&
				lp->l_level < depth &&
				(lp->l_attr&LIST_NOINFERIORS) == 0) {
			cp = salloc((n = strlen(lp->l_name)) + 2);
			memcpy(cp, lp->l_name, n);
			cp[n] = lp->l_delim;
			cp[n+1] = '\0';
			if (imap_list1(mp, cp, &lx, &ly, lp->l_level) == OKAY &&
					lx && ly) {
				lp->l_has_children = 1;
				if (strcmp(cp, lx->l_name) == 0)
					lx = lx->l_next;
				if (lx) {
					lend->l_next = lx;
					lend = ly;
				}
			}
		}
	for (lp = list; lp; lp = lp->l_next) {
		if (strip) {
			cp = lp->l_name;
			for (bp = base; *bp && *bp == *cp; bp++)
				cp++;
		} else
			cp = lp->l_name;
		if ((lp->l_attr&LIST_NOSELECT) == 0)
			fprintf(fp, "%s\n", *cp ? cp : base);
		else if (lp->l_has_children == 0)
			fprintf(fp, "%s%c\n", *cp ? cp : base,
				lp->l_delim != EOF ? lp->l_delim : '\n');
	}
	return OKAY;
}

void 
imap_folders(const char *name, int strip)
{
	sighandler_type	saveint, savepipe;
	const char *fold, *cp, *sp;
	char *tempfn;
	FILE *volatile fp;

	cp = protbase(name);
	sp = mb.mb_imap_account;
	if (sp == NULL || strcmp(cp, sp)) {
		fprintf(stderr, tr(502,
			"Cannot perform `folders' but when on the very IMAP "
			"account; the current one is\n  `%s' -- "
			"try `folders @'.\n"),
			(sp != NULL) ? sp : tr(503, "[NONE]"));
		return;
	}
	fold = imap_fileof(name);
	if (options & OPT_TTYOUT) {
		if ((fp = Ftemp(&tempfn, "Ri", "w+", 0600, 1)) == NULL) {
			perror("tmpfile");
			return;
		}
		rm(tempfn);
		Ftfree(&tempfn);
	} else
		fp = stdout;
	imaplock = 1;
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGINT, maincatch);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1))
		goto out;
	if (savepipe != SIG_IGN)
		safe_signal(SIGPIPE, imapcatch);
	if (mb.mb_type == MB_CACHE)
		cache_list(&mb, fold, strip, fp);
	else
		imap_list(&mb, fold, strip, fp);
	imaplock = 0;
	if (interrupts) {
		if (options & OPT_TTYOUT)
			Fclose(fp);
		onintr(0);
	}
	fflush(fp);
	if (options & OPT_TTYOUT) {
		rewind(fp);
		if (fsize(fp) > 0)
			dopr(fp);
		else
			fprintf(stderr, "Folder not found.\n");
	}
out:
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	if (options & OPT_TTYOUT)
		Fclose(fp);
}

static void
dopr(FILE *fp)
{
	char	o[LINESIZE], *tempfn;
	int	c;
	long	n = 0, mx = 0, columns, width;
	FILE	*out;

	if ((out = Ftemp(&tempfn, "Ro", "w+", 0600, 1)) == NULL) {
		perror("tmpfile");
		return;
	}
	rm(tempfn);
	Ftfree(&tempfn);
	while ((c = getc(fp)) != EOF) {
		if (c == '\n') {
			if (n > mx)
				mx = n;
			n = 0;
		} else
			n++;
	}
	rewind(fp);
	width = scrnwidth;
	if (mx < width / 2) {
		columns = width / (mx+2);
		snprintf(o, sizeof o,
				"sort | pr -%lu -w%lu -t",
				columns, width);
	} else
		strncpy(o, "sort", sizeof o)[sizeof o - 1] = '\0';
	run_command(SHELL, 0, fileno(fp), fileno(out), "-c", o, NULL);
	try_pager(out);
	Fclose(out);
}

static enum okay 
imap_copy1(struct mailbox *mp, struct message *m, int n, const char *name)
{
	char	o[LINESIZE];
	const char	*qname;
	enum okay	ok = STOP;
	int	twice = 0;
	int	stored = 0;
	FILE	*queuefp = NULL;

	if (mp->mb_type == MB_CACHE) {
		if ((queuefp = cache_queue(mp)) == NULL)
			return STOP;
		ok = OKAY;
	}
	qname = imap_quotestr(name = imap_fileof(name));
	/*
	 * Since it is not possible to set flags on the copy, recently
	 * set flags must be set on the original to include it in the copy.
	 */
	if ((m->m_flag&(MREAD|MSTATUS)) == (MREAD|MSTATUS))
		imap_store(mp, m, n, '+', "\\Seen", 0);
	if (m->m_flag&MFLAG)
		imap_store(mp, m, n, '+', "\\Flagged", 0);
	if (m->m_flag&MUNFLAG)
		imap_store(mp, m, n, '-', "\\Flagged", 0);
	if (m->m_flag&MANSWER)
		imap_store(mp, m, n, '+', "\\Answered", 0);
	if (m->m_flag&MUNANSWER)
		imap_store(mp, m, n, '-', "\\Flagged", 0);
	if (m->m_flag&MDRAFT)
		imap_store(mp, m, n, '+', "\\Draft", 0);
	if (m->m_flag&MUNDRAFT)
		imap_store(mp, m, n, '-', "\\Draft", 0);
again:	if (m->m_uid)
		snprintf(o, sizeof o, "%s UID COPY %lu %s\r\n",
				tag(1), m->m_uid, qname);
	else {
		if (check_expunged() == STOP)
			goto out;
		snprintf(o, sizeof o, "%s COPY %u %s\r\n",
				tag(1), n, qname);
	}
	IMAP_OUT(o, MB_COMD, goto out)
	while (mp->mb_active & MB_COMD)
		ok = imap_answer(mp, twice);
	if (mp->mb_type == MB_IMAP &&
			mp->mb_flags & MB_UIDPLUS &&
			response_status == RESPONSE_OK)
		imap_copyuid(mp, m, name);
	if (response_status == RESPONSE_NO && twice++ == 0) {
		snprintf(o, sizeof o, "%s CREATE %s\r\n", tag(1), qname);
		IMAP_OUT(o, MB_COMD, goto out)
		while (mp->mb_active & MB_COMD)
			ok = imap_answer(mp, 1);
		if (ok == OKAY) {
			imap_created_mailbox++;
			goto again;
		}
	}
	if (queuefp != NULL)
		Fclose(queuefp);
	/*
	 * ... and reset the flag to its initial value so that
	 * the 'exit' command still leaves the message unread.
	 */
out:	if ((m->m_flag&(MREAD|MSTATUS)) == (MREAD|MSTATUS)) {
		imap_store(mp, m, n, '-', "\\Seen", 0);
		stored++;
	}
	if (m->m_flag&MFLAG) {
		imap_store(mp, m, n, '-', "\\Flagged", 0);
		stored++;
	}
	if (m->m_flag&MUNFLAG) {
		imap_store(mp, m, n, '+', "\\Flagged", 0);
		stored++;
	}
	if (m->m_flag&MANSWER) {
		imap_store(mp, m, n, '-', "\\Answered", 0);
		stored++;
	}
	if (m->m_flag&MUNANSWER) {
		imap_store(mp, m, n, '+', "\\Answered", 0);
		stored++;
	}
	if (m->m_flag&MDRAFT) {
		imap_store(mp, m, n, '-', "\\Draft", 0);
		stored++;
	}
	if (m->m_flag&MUNDRAFT) {
		imap_store(mp, m, n, '+', "\\Draft", 0);
		stored++;
	}
	if (stored) {
		mp->mb_active |= MB_COMD;
		(void)imap_finish(mp);
	}
	return ok;
}

enum okay 
imap_copy(struct message *m, int n, const char *name)
{
	sighandler_type	saveint, savepipe;
	enum okay	ok = STOP;

	(void)&saveint;
	(void)&savepipe;
	(void)&ok;
	imaplock = 1;
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGINT, maincatch);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1) == 0) {
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
		ok = imap_copy1(&mb, m, n, name);
	}
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
	if (interrupts)
		onintr(0);
	return ok;
}

static enum okay 
imap_copyuid_parse(const char *cp, unsigned long *uidvalidity,
		unsigned long *olduid, unsigned long *newuid)
{
	char	*xp, *yp, *zp;

	*uidvalidity = strtoul(cp, &xp, 10);
	*olduid = strtoul(xp, &yp, 10);
	*newuid = strtoul(yp, &zp, 10);
	return *uidvalidity && *olduid && *newuid && xp > cp && *xp == ' ' &&
		yp > xp && *yp == ' ' && zp > yp && *zp == ']';
}

static enum okay 
imap_appenduid_parse(const char *cp, unsigned long *uidvalidity,
		unsigned long *uid)
{
	char	*xp, *yp;

	*uidvalidity = strtoul(cp, &xp, 10);
	*uid = strtoul(xp, &yp, 10);
	return *uidvalidity && *uid && xp > cp && *xp == ' ' &&
		yp > xp && *yp == ']';
}

static enum okay 
imap_copyuid(struct mailbox *mp, struct message *m, const char *name)
{
	const char	*cp;
	unsigned long	uidvalidity, olduid, newuid;
	struct mailbox	xmb;
	struct message	xm;

	if ((cp = asccasestr(responded_text, "[COPYUID ")) == NULL ||
			imap_copyuid_parse(&cp[9], &uidvalidity,
				&olduid, &newuid) == STOP)
		return STOP;
	xmb = *mp;
	xmb.mb_cache_directory = NULL;
	xmb.mb_imap_mailbox = savestr(name);
	xmb.mb_uidvalidity = uidvalidity;
	initcache(&xmb);
	if (m == NULL) {
		memset(&xm, 0, sizeof xm);
		xm.m_uid = olduid;
		if (getcache1(mp, &xm, NEED_UNSPEC, 3) != OKAY)
			return STOP;
		getcache(mp, &xm, NEED_HEADER);
		getcache(mp, &xm, NEED_BODY);
	} else {
		if ((m->m_flag & HAVE_HEADER) == 0)
			getcache(mp, m, NEED_HEADER);
		if ((m->m_flag & HAVE_BODY) == 0)
			getcache(mp, m, NEED_BODY);
		xm = *m;
	}
	xm.m_uid = newuid;
	xm.m_flag &= ~MFULLYCACHED;
	putcache(&xmb, &xm);
	return OKAY;
}

static enum okay
imap_appenduid(struct mailbox *mp, FILE *fp, time_t t, long off1,
		long xsize, long size, long lines, int flag, const char *name)
{
	const char	*cp;
	unsigned long	uidvalidity, uid;
	struct mailbox	xmb;
	struct message	xm;

	if ((cp = asccasestr(responded_text, "[APPENDUID ")) == NULL ||
			imap_appenduid_parse(&cp[11], &uidvalidity,
				&uid) == STOP)
		return STOP;
	xmb = *mp;
	xmb.mb_cache_directory = NULL;
	xmb.mb_imap_mailbox = savestr(name);
	xmb.mb_uidvalidity = uidvalidity;
	xmb.mb_otf = xmb.mb_itf = fp;
	initcache(&xmb);
	memset(&xm, 0, sizeof xm);
	xm.m_flag = (flag & MREAD) | MNEW;
	xm.m_time = t;
	xm.m_block = mailx_blockof(off1);
	xm.m_offset = mailx_offsetof(off1);
	xm.m_size = size;
	xm.m_xsize = xsize;
	xm.m_lines = xm.m_xlines = lines;
	xm.m_uid = uid;
	xm.m_have = HAVE_HEADER|HAVE_BODY;
	putcache(&xmb, &xm);
	return OKAY;
}

static enum okay
imap_appenduid_cached(struct mailbox *mp, FILE *fp)
{
	FILE	*tp = NULL;
	time_t	t;
	long	size, xsize, ysize, lines;
	enum mflag	flag = MNEW;
	char *name, *buf, *bp, *tempCopy;
	char const *cp;
	size_t	bufsize, buflen, count;
	enum okay	ok = STOP;

	buf = smalloc(bufsize = LINESIZE);
	buflen = 0;
	count = fsize(fp);
	if (fgetline(&buf, &bufsize, &count, &buflen, fp, 0) == NULL)
		goto stop;
	for (bp = buf; *bp != ' '; bp++);	/* strip old tag */
	while (*bp == ' ')
		bp++;
	if ((cp = strrchr(bp, '{')) == NULL)
		goto stop;
	xsize = atol(&cp[1]) + 2;
	if ((name = imap_strex(&bp[7], &cp)) == NULL)
		goto stop;
	while (*cp == ' ')
		cp++;
	if (*cp == '(') {
		imap_getflags(cp, &cp, &flag);
		while (*++cp == ' ');
	}
	t = imap_read_date_time(cp);
	if ((tp = Ftemp(&tempCopy, "Rc", "w+", 0600, 1)) == NULL)
		goto stop;
	rm(tempCopy);
	Ftfree(&tempCopy);
	size = xsize;
	ysize = lines = 0;
	while (size > 0) {
		if (fgetline(&buf, &bufsize, &count, &buflen, fp, 0) == NULL)
			goto stop;
		size -= buflen;
		buf[--buflen] = '\0';
		buf[buflen-1] = '\n';
		fwrite(buf, 1, buflen, tp);
		ysize += buflen;
		lines++;
	}
	fflush(tp);
	rewind(tp);
	imap_appenduid(mp, tp, t, 0, xsize-2, ysize-1, lines-1, flag,
			imap_unquotestr(name));
	ok = OKAY;
stop:	free(buf);
	if (tp)
		Fclose(tp);
	return ok;
}

static enum okay 
imap_search2(struct mailbox *mp, struct message *m, int count,
		const char *spec, int f)
{
	char	*o;
	size_t	osize;
	FILE	*queuefp = NULL;
	enum okay	ok = STOP;
	int	i;
	unsigned long	n;
	const char	*cp;
	char	*xp, *cs, c;

	c = 0;
	for (cp = spec; *cp; cp++)
		c |= *cp;
	if (c & 0200) {
		cp = charset_get_lc();
#ifdef HAVE_ICONV
		if (asccasecmp(cp, "utf-8")) {
			iconv_t	it;
			char *nsp, *nspec;
			size_t sz, nsz;
			if ((it = n_iconv_open("utf-8", cp)) != (iconv_t)-1) {
				sz = strlen(spec) + 1;
				nsp = nspec = salloc(nsz = 6*strlen(spec) + 1);
				if (n_iconv_buf(it, &spec, &sz, &nsp, &nsz,
						FAL0) == 0 && sz == 0) {
					spec = nspec;
					cp = "utf-8";
				}
				n_iconv_close(it);
			}
		}
#endif
		cp = imap_quotestr(cp);
		cs = salloc(n = strlen(cp) + 10);
		snprintf(cs, n, "CHARSET %s ", cp);
	} else
		cs = UNCONST("");
	o = ac_alloc(osize = strlen(spec) + 60);
	snprintf(o, osize, "%s UID SEARCH %s%s\r\n", tag(1), cs, spec);
	IMAP_OUT(o, MB_COMD, goto out)
	while (mp->mb_active & MB_COMD) {
		ok = imap_answer(mp, 0);
		if (response_status == RESPONSE_OTHER &&
				response_other == MAILBOX_DATA_SEARCH) {
			xp = responded_other_text;
			while (*xp && *xp != '\r') {
				n = strtoul(xp, &xp, 10);
				for (i = 0; i < count; i++)
					if (m[i].m_uid == n &&
							(m[i].m_flag&MHIDDEN) 
							== 0 &&
							(f == MDELETED ||
							 (m[i].m_flag&MDELETED)
							 == 0))
						mark(i+1, f);
			}
		}
	}
out:	ac_free(o);
	return ok;
}

enum okay 
imap_search1(const char *volatile spec, int f)
{
	sighandler_type	saveint, savepipe;
	enum okay	ok = STOP;

	(void)&saveint;
	(void)&savepipe;
	(void)&ok;
	if (mb.mb_type != MB_IMAP)
		return STOP;
	imaplock = 1;
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGINT, maincatch);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1) == 0) {
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
		ok = imap_search2(&mb, message, msgCount, spec, f);
	}
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
	if (interrupts)
		onintr(0);
	return ok;
}

int 
imap_thisaccount(const char *cp)
{
	if (mb.mb_type != MB_CACHE && mb.mb_type != MB_IMAP)
		return 0;
	if ((mb.mb_type != MB_CACHE && mb.mb_sock.s_fd < 0) ||
			mb.mb_imap_account == NULL)
		return 0;
	return strcmp(protbase(cp), mb.mb_imap_account) == 0;
}

enum okay 
imap_remove(const char *name)
{
	sighandler_type saveint, savepipe;
	enum okay	ok = STOP;

	(void)&saveint;
	(void)&savepipe;
	(void)&ok;
	if (mb.mb_type != MB_IMAP) {
		fprintf(stderr, "Refusing to remove \"%s\" "
				"in disconnected mode.\n", name);
		return STOP;
	}
	if (!imap_thisaccount(name)) {
		fprintf(stderr, "Can only remove mailboxes on current IMAP "
				"server: \"%s\" not removed.\n", name);
		return STOP;
	}
	imaplock = 1;
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGINT, maincatch);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1) == 0) {
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
		ok = imap_remove1(&mb, imap_fileof(name));
	}
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
	if (ok == OKAY)
		ok = cache_remove(name);
	if (interrupts)
		onintr(0);
	return ok;
}

static enum okay 
imap_remove1(struct mailbox *mp, const char *name)
{
	FILE	*queuefp = NULL;
	char	*o;
	int	os;
	enum okay	ok = STOP;

	o = ac_alloc(os = 2*strlen(name) + 100);
	snprintf(o, os, "%s DELETE %s\r\n", tag(1), imap_quotestr(name));
	IMAP_OUT(o, MB_COMD, goto out)
	while (mp->mb_active & MB_COMD)
		ok = imap_answer(mp, 1);
out:	ac_free(o);
	return ok;
}

enum okay 
imap_rename(const char *old, const char *new)
{
	sighandler_type saveint, savepipe;
	enum okay	ok = STOP;

	(void)&saveint;
	(void)&savepipe;
	(void)&ok;
	if (mb.mb_type != MB_IMAP) {
		fprintf(stderr, "Refusing to rename mailboxes "
				"in disconnected mode.\n");
		return STOP;
	}
	if (!imap_thisaccount(old) || !imap_thisaccount(new)) {
		fprintf(stderr, "Can only rename mailboxes on current IMAP "
				"server: \"%s\" not renamed to \"%s\".\n",
				old, new);
		return STOP;
	}
	imaplock = 1;
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGINT, maincatch);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1) == 0) {
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
		ok = imap_rename1(&mb, imap_fileof(old), imap_fileof(new));
	}
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
	if (ok == OKAY)
		ok = cache_rename(old, new);
	if (interrupts)
		onintr(0);
	return ok;
}

static enum okay 
imap_rename1(struct mailbox *mp, const char *old, const char *new)
{
	FILE	*queuefp = NULL;
	char	*o;
	int	os;
	enum okay	ok = STOP;

	o = ac_alloc(os = 2*strlen(old) + 2*strlen(new) + 100);
	snprintf(o, os, "%s RENAME %s %s\r\n", tag(1),
			imap_quotestr(old), imap_quotestr(new));
	IMAP_OUT(o, MB_COMD, goto out)
	while (mp->mb_active & MB_COMD)
		ok = imap_answer(mp, 1);
out:	ac_free(o);
	return ok;
}

enum okay
imap_dequeue(struct mailbox *mp, FILE *fp)
{
	FILE	*queuefp = NULL;
	char	o[LINESIZE], *newname;
	char	*buf, *bp, *cp, iob[4096];
	size_t	bufsize, buflen, count;
	enum okay	ok = OKAY, rok = OKAY;
	long	offs, offs1, offs2, octets;
	int	twice, gotcha = 0;

	buf = smalloc(bufsize = LINESIZE);
	buflen = 0;
	count = fsize(fp);
	while ((offs1 = ftell(fp)) >= 0 &&
			fgetline(&buf, &bufsize, &count, &buflen, fp, 0)
			!= NULL) {
		for (bp = buf; *bp != ' '; bp++);	/* strip old tag */
		while (*bp == ' ')
			bp++;
		twice = 0;
		if ((offs = ftell(fp)) < 0)
			goto fail;
	again:	snprintf(o, sizeof o, "%s %s", tag(1), bp);
		if (ascncasecmp(bp, "UID COPY ", 9) == 0) {
			cp = &bp[9];
			while (digitchar(*cp&0377))
				cp++;
			if (*cp != ' ')
				goto fail;
			while (*cp == ' ')
				cp++;
			if ((newname = imap_strex(cp, NULL)) == NULL)
				goto fail;
			IMAP_OUT(o, MB_COMD, continue)
			while (mp->mb_active & MB_COMD)
				ok = imap_answer(mp, twice);
			if (response_status == RESPONSE_NO && twice++ == 0)
				goto trycreate;
			if (response_status == RESPONSE_OK &&
					mp->mb_flags & MB_UIDPLUS) {
				imap_copyuid(mp, NULL,
						imap_unquotestr(newname));
			}
		} else if (ascncasecmp(bp, "UID STORE ", 10) == 0) {
			IMAP_OUT(o, MB_COMD, continue)
			while (mp->mb_active & MB_COMD)
				ok = imap_answer(mp, 1);
			if (ok == OKAY)
				gotcha++;
		} else if (ascncasecmp(bp, "APPEND ", 7) == 0) {
			if ((cp = strrchr(bp, '{')) == NULL)
				goto fail;
			octets = atol(&cp[1]) + 2;
			if ((newname = imap_strex(&bp[7], NULL)) == NULL)
				goto fail;
			IMAP_OUT(o, MB_COMD, continue)
			while (mp->mb_active & MB_COMD) {
				ok = imap_answer(mp, twice);
				if (response_type == RESPONSE_CONT)
					break;
			}
			if (ok == STOP) {
				if (twice++ == 0 &&
						fseek(fp, offs, SEEK_SET) >= 0)
					goto trycreate;
				goto fail;
			}
			while (octets > 0) {
				size_t n = (size_t)octets > sizeof iob
					? sizeof iob : (size_t)octets;
				octets -= n;
				if (n != fread(iob, 1, n, fp))
					goto fail;
				swrite1(&mp->mb_sock, iob, n, 1);
			}
			swrite(&mp->mb_sock, "");
			while (mp->mb_active & MB_COMD) {
				ok = imap_answer(mp, 0);
				if (response_status == RESPONSE_NO &&
						twice++ == 0) {
					if (fseek(fp, offs, SEEK_SET) < 0)
						goto fail;
					goto trycreate;
				}
			}
			if (response_status == RESPONSE_OK &&
					mp->mb_flags & MB_UIDPLUS) {
				if ((offs2 = ftell(fp)) < 0)
					goto fail;
				fseek(fp, offs1, SEEK_SET);
				if (imap_appenduid_cached(mp, fp) == STOP) {
					(void)fseek(fp, offs2, SEEK_SET);
					goto fail;
				}
			}
		} else {
		fail:	fprintf(stderr,
				"Invalid command in IMAP cache queue: \"%s\"\n",
				bp);
			rok = STOP;
		}
		continue;
	trycreate:
		snprintf(o, sizeof o, "%s CREATE %s\r\n",
				tag(1), newname);
		IMAP_OUT(o, MB_COMD, continue)
		while (mp->mb_active & MB_COMD)
			ok = imap_answer(mp, 1);
		if (ok == OKAY)
			goto again;
	}
	fflush(fp);
	rewind(fp);
	ftruncate(fileno(fp), 0);
	if (gotcha)
		imap_close(mp);
	free(buf);
	return rok;
}

static char *
imap_strex(char const *cp, char const **xp)
{
	char const *cq;
	char *n;

	if (*cp != '"')
		return NULL;
	for (cq = &cp[1]; *cq; cq++) {
		if (*cq == '\\')
			cq++;
		else if (*cq == '"')
			break;
	}
	if (*cq != '"')
		return NULL;
	n = salloc(cq - cp + 2);
	memcpy(n, cp, cq - cp + 1);
	n[cq - cp + 1] = '\0';
	if (xp)
		*xp = &cq[1];
	return n;
}

static enum okay 
check_expunged(void)
{
	if (expunged_messages > 0) {
		fprintf(stderr,
			"Command not executed - messages have been expunged\n");
		return STOP;
	}
	return OKAY;
}

/*ARGSUSED*/
int 
cconnect(void *vp)
{
	char	*cp, *cq;
	int	omsgCount = msgCount;
	(void)vp;

	if (mb.mb_type == MB_IMAP && mb.mb_sock.s_fd > 0) {
		fprintf(stderr, "Already connected.\n");
		return 1;
	}
	unset_allow_undefined = TRU1;
	unset_internal("disconnected");
	cp = protbase(mailname);
	if (strncmp(cp, "imap://", 7) == 0)
		cp += 7;
	else if (strncmp(cp, "imaps://", 8) == 0)
		cp += 8;
	if ((cq = strchr(cp, ':')) != NULL)
		*cq = '\0';
	unset_internal(savecat("disconnected-", cp));
	unset_allow_undefined = FAL0;
	if (mb.mb_type == MB_CACHE) {
		imap_setfile1(mailname, 0, edit, 1);
		if (msgCount > omsgCount)
			newmailinfo(omsgCount);
	}
	return 0;
}

int 
cdisconnect(void *vp)
{
	int	*msgvec = vp;

	if (mb.mb_type == MB_CACHE) {
		fprintf(stderr, "Not connected.\n");
		return 1;
	} else if (mb.mb_type == MB_IMAP) {
		if (cached_uidvalidity(&mb) == 0) {
			fprintf(stderr, "The current mailbox is not cached.\n");
			return 1;
		}
	}
	if (*msgvec)
		ccache(vp);
	assign("disconnected", "");
	if (mb.mb_type == MB_IMAP) {
		sclose(&mb.mb_sock);
		imap_setfile1(mailname, 0, edit, 1);
	}
	return 0;
}

int 
ccache(void *vp)
{
	int	*msgvec = vp, *ip;
	struct message	*mp;

	if (mb.mb_type != MB_IMAP) {
		fprintf(stderr, "Not connected to an IMAP server.\n");
		return 1;
	}
	if (cached_uidvalidity(&mb) == 0) {
		fprintf(stderr, "The current mailbox is not cached.\n");
		return 1;
	}
	for (ip = msgvec; *ip; ip++) {
		mp = &message[*ip-1];
		if (!(mp->m_have & HAVE_BODY))
			get_body(mp);
	}
	return 0;
}

int
disconnected(const char *file)
{
	char	*cp, *cq, *vp;
	int	vs, r;

	if (value("disconnected"))
		return 1;
	cp = protbase(file);
	if (strncmp(cp, "imap://", 7) == 0)
		cp += 7;
	else if (strncmp(cp, "imaps://", 8) == 0)
		cp += 8;
	else
		return 0;
	if ((cq = strchr(cp, ':')) != NULL)
		*cq = '\0';
	vp = ac_alloc(vs = strlen(cp) + 14);
	snprintf(vp, vs, "disconnected-%s", cp);
	r = value(vp) != NULL;
	ac_free(vp);
	return r;
}

void
transflags(struct message *omessage, long omsgCount, int transparent)
{
	struct message	*omp, *nmp, *newdot, *newprevdot;
	int	hf;

	omp = omessage;
	nmp = message;
	newdot = message;
	newprevdot = NULL;
	while (omp < &omessage[omsgCount] &&
			nmp < &message[msgCount]) {
		if (dot && nmp->m_uid == dot->m_uid)
			newdot = nmp;
		if (prevdot && nmp->m_uid == prevdot->m_uid)
			newprevdot = nmp;
		if (omp->m_uid == nmp->m_uid) {
			hf = nmp->m_flag & MHIDDEN;
			if (transparent && mb.mb_type == MB_IMAP)
				omp->m_flag &= ~MHIDDEN;
			*nmp++ = *omp++;
			if (transparent && mb.mb_type == MB_CACHE)
				nmp[-1].m_flag |= hf;
		} else if (omp->m_uid < nmp->m_uid)
			omp++;
		else
			nmp++;
	}
	dot = newdot;
	setdot(newdot);
	prevdot = newprevdot;
	free(omessage);
}
#endif	/* HAVE_IMAP */

time_t
imap_read_date_time(const char *cp)
{
	time_t	t;
	int	i, year, month, day, hour, minute, second;
	int	sign = -1;
	char	buf[3];

	/*
	 * "25-Jul-2004 15:33:44 +0200"
	 * |    |    |    |    |    |  
	 * 0    5   10   15   20   25  
	 */
	if (cp[0] != '"' || strlen(cp) < 28 || cp[27] != '"')
		goto invalid;
	day = strtol(&cp[1], NULL, 10);
	for (i = 0; month_names[i]; i++)
		if (ascncasecmp(&cp[4], month_names[i], 3) == 0)
			break;
	if (month_names[i] == NULL)
		goto invalid;
	month = i + 1;
	year = strtol(&cp[8], NULL, 10);
	hour = strtol(&cp[13], NULL, 10);
	minute = strtol(&cp[16], NULL, 10);
	second = strtol(&cp[19], NULL, 10);
	if ((t = combinetime(year, month, day, hour, minute, second)) ==
			(time_t)-1)
		goto invalid;
	switch (cp[22]) {
	case '-':
		sign = 1;
		break;
	case '+':
		break;
	default:
		goto invalid;
	}
	buf[2] = '\0';
	buf[0] = cp[23];
	buf[1] = cp[24];
	t += strtol(buf, NULL, 10) * sign * 3600;
	buf[0] = cp[25];
	buf[1] = cp[26];
	t += strtol(buf, NULL, 10) * sign * 60;
	return t;
invalid:
	time(&t);
	return t;
}

time_t
imap_read_date(const char *cp)
{
	time_t	t;
	int	year, month, day, i, tzdiff;
	struct tm	*tmptr;
	char	*xp, *yp;

	if (*cp == '"')
		cp++;
	day = strtol(cp, &xp, 10);
	if (day <= 0 || day > 31 || *xp++ != '-')
		return -1;
	for (i = 0; month_names[i]; i++)
		if (ascncasecmp(xp, month_names[i], 3) == 0)
			break;
	if (month_names[i] == NULL)
		return -1;
	month = i+1;
	if (xp[3] != '-')
		return -1;
	year = strtol(&xp[4], &yp, 10);
	if (year < 1970 || year > 2037 || yp != &xp[8])
		return -1;
	if (yp[0] != '\0' && (yp[1] != '"' || yp[2] != '\0'))
		return -1;
	if ((t = combinetime(year, month, day, 0, 0, 0)) == (time_t)-1)
		return -1;
	tzdiff = t - mktime(gmtime(&t));
	tmptr = localtime(&t);
	if (tmptr->tm_isdst > 0)
		tzdiff += 3600;
	t -= tzdiff;
	return t;
}

const char *
imap_make_date_time(time_t t)
{
	static char	s[30];
	struct tm	*tmptr;
	int	tzdiff, tzdiff_hour, tzdiff_min;

	tzdiff = t - mktime(gmtime(&t));
	tzdiff_hour = (int)(tzdiff / 60);
	tzdiff_min = tzdiff_hour % 60;
	tzdiff_hour /= 60;
	tmptr = localtime(&t);
	if (tmptr->tm_isdst > 0)
		tzdiff_hour++;
	snprintf(s, sizeof s, "\"%02d-%s-%04d %02d:%02d:%02d %+03d%02d\"",
			tmptr->tm_mday,
			month_names[tmptr->tm_mon],
			tmptr->tm_year + 1900,
			tmptr->tm_hour,
			tmptr->tm_min,
			tmptr->tm_sec,
			tzdiff_hour,
			tzdiff_min);
	return s;
}

char *
imap_quotestr(const char *s)
{
	char	*n, *np;

	np = n = salloc(2 * strlen(s) + 3);
	*np++ = '"';
	while (*s) {
		if (*s == '"' || *s == '\\')
			*np++ = '\\';
		*np++ = *s++;
	}
	*np++ = '"';
	*np = '\0';
	return n;
}

char *
imap_unquotestr(const char *s)
{
	char	*n, *np;

	if (*s != '"')
		return savestr(s);
	np = n = salloc(strlen(s) + 1);
	while (*++s) {
		if (*s == '\\')
			s++;
		else if (*s == '"')
			break;
		*np++ = *s;
	}
	*np = '\0';
	return n;
}
