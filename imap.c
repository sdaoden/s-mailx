/*
 * Nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2002 Gunnar Ritter, Freiburg i. Br., Germany.
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

#ifndef lint
#ifdef	DOSCCS
static char sccsid[] = "@(#)imap.c	1.122 (gritter) 8/14/04";
#endif
#endif /* not lint */

#include "config.h"

#include "rcv.h"
#include "extern.h"
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "md5.h"

/*
 * Mail -- a mail program
 *
 * IMAP v4r1 client following RFC 2060.
 */

#ifdef	HAVE_SOCKETS
static int	verbose;

#define	IMAP_ANSWER()	{ \
				if (mp->mb_type != MB_CACHE) { \
					enum okay ok = OKAY; \
					while (mp->mb_active & MB_COMD) \
						ok = imap_answer(mp, 1); \
					if (ok == STOP) \
						return STOP; \
				} \
			}
#define	IMAP_OUT(x, y, action)	\
			{ \
				if (mp->mb_type != MB_CACHE) { \
					if (imap_finish(mp) == STOP) \
						return STOP; \
					if (verbose) \
						fprintf(stderr, ">>> %s", x); \
					mp->mb_active |= (y); \
					if (swrite(&mp->mb_sock, x) == STOP) \
						action; \
				} else { \
					if (queuefp != NULL) \
						fputs(x, queuefp); \
				} \
			}

static char	*imapbuf;
static size_t	imapbufsize;
static sigjmp_buf	imapjmp;
static sighandler_type savealrm;
static int	reset_tio;
static struct termios	otio;
static int	imapkeepalive;
static long	had_exists = -1;
static long	had_expunge = -1;
static long	expunged_messages;
static volatile int	imaplock;

static int	same_imap_account;

static void	imap_timer_off __P((void));
static void	imap_other_get __P((char *));
static void	imap_response_get __P((const char **));
static void	imap_response_parse __P((void));
static enum okay	imap_answer __P((struct mailbox *, int));
static enum okay	imap_finish __P((struct mailbox *));
static void	imapcatch __P((int));
static enum okay	imap_noop __P((struct mailbox *));
static void	imapalarm __P((int));
static enum okay	imap_preauth __P((struct mailbox *, const char *,
				const char *));
static enum okay	imap_auth __P((struct mailbox *, const char *, char *,
				const char *));
static enum okay	imap_login __P((struct mailbox *, char *,
				const char *));
static enum okay	imap_cram_md5 __P((struct mailbox *, char *,
				const char *));
#ifdef	USE_GSSAPI
static enum okay	imap_gss __P((struct mailbox *, char *));
#endif	/* USE_GSSAPI */
static enum okay	imap_flags __P((struct mailbox *));
static void	imap_init __P((struct mailbox *, int));
static void	imap_setptr __P((struct mailbox *, int, int));
static char	*imap_have_password __P((const char *));
static void	imap_split __P((char **, const char **, int *, const char **,
			char **, char **, const char **, char **));
static int	imap_fetchdata __P((struct mailbox *, struct message *, size_t,
			int));
static enum okay	imap_get __P((struct mailbox *, struct message *,
				enum needspec));
static void	commithdr __P((struct mailbox *, struct message *,
				struct message));
static enum okay	imap_fetchheaders __P((struct mailbox *,
				struct message *, int, int));
static int	imap_setfile1 __P((const char *, int, int, int));
static enum okay	imap_exit __P((struct mailbox *));
static enum okay	imap_delete __P((struct mailbox *, int,
				struct message *));
static enum okay	imap_expunge __P((struct mailbox *));
static enum okay	imap_update __P((struct mailbox *));
static enum okay	imap_undelete1 __P((struct mailbox *,
				struct message *, int));
static const char	*tag __P((int));
static time_t	imap_read_date_time __P((const char *));
static const char	*imap_make_date_time __P((time_t));
static const char	*imap_quotestr __P((const char *));
static const char	*imap_unquotestr __P((const char *));
static enum okay	imap_append1 __P((struct mailbox *, const char *,
				FILE *, off_t, long, int, time_t));
static enum okay	imap_append0 __P((struct mailbox *, const char *,
				FILE *));
static enum okay	imap_parse_list __P((void));
static enum okay	imap_list __P((struct mailbox *, const char *, FILE *));
static enum okay	imap_copy1 __P((struct mailbox *, struct message *,
				int, const char *));
static char	*imap_strex __P((const char *));

static void
imap_timer_off()
{
	if (imapkeepalive > 0) {
		alarm(0);
		safe_signal(SIGALRM, savealrm);
	}
}

static enum {
	RESPONSE_TAGGED,
	RESPONSE_DATA,
	RESPONSE_FATAL,
	RESPONSE_CONT
} response_type;

static enum {
	RESPONSE_OK,
	RESPONSE_NO,
	RESPONSE_BAD,
	RESPONSE_PREAUTH,
	RESPONSE_BYE,
	RESPONSE_OTHER
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

static void
imap_other_get(pp)
	char *pp;
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
imap_response_get(cp)
	const char **cp;
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
imap_response_parse()
{
	static char	*parsebuf;
	static size_t	parsebufsize;
	const char	*ip = imapbuf;
	char	*pp;

	if (parsebufsize < imapbufsize) {
		free(parsebuf);
		parsebuf = smalloc(parsebufsize = imapbufsize);
	}
	strcpy(parsebuf, imapbuf);
	pp = parsebuf;
	switch (*ip) {
	case '+':
		response_type = RESPONSE_CONT;
		ip += 2;
		pp += 2;
		break;
	case '*':
		ip += 2;
		pp += 2;
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
		while (*pp != ' ')
			pp++;
		*pp++ = '\0';
		while (*pp == ' ')
			pp++;
		ip = &imapbuf[pp - parsebuf];
		response_type = RESPONSE_TAGGED;
		imap_response_get(&ip);
		pp = &parsebuf[ip - imapbuf];
	}
	responded_text = pp;
	if (response_type != RESPONSE_CONT &&
			response_status == RESPONSE_OTHER)
		imap_other_get(pp);
}

static enum okay
imap_answer(mp, errprnt)
	struct mailbox *mp;
	int errprnt;
{
	int sz, i, complete;
	enum okay ok = STOP;

	if (mp->mb_type == MB_CACHE)
		return OKAY;
again:	if ((sz = sgetline(&imapbuf, &imapbufsize, NULL, &mp->mb_sock)) > 0) {
		if (verbose)
			fputs(imapbuf, stderr);
		imap_response_parse();
		if (response_type == RESPONSE_CONT)
			return OKAY;
		if (response_status == RESPONSE_OTHER) {
			if (response_other == MAILBOX_DATA_EXISTS) {
				had_exists = responded_other_number;
				if (had_expunge > 0)
					had_expunge = 0;
			} else if (response_other == MESSAGE_DATA_EXPUNGE) {
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
				fprintf(stderr, catgets(catd, CATSET, 218,
					"IMAP error: %s"), responded_text);
			break;
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
imap_finish(mp)
	struct mailbox *mp;
{
	while (mp->mb_sock.s_fd >= 0 && mp->mb_active & MB_COMD)
		imap_answer(mp, 1);
	return OKAY;
}

static void
imapcatch(s)
	int s;
{
	if (reset_tio)
		tcsetattr(0, TCSADRAIN, &otio);
	switch (s) {
	case SIGINT:
		fprintf(stderr, catgets(catd, CATSET, 102, "Interrupt\n"));
		break;
	case SIGPIPE:
		fprintf(stderr, "Received SIGPIPE during IMAP operation\n");
		sclose(&mb.mb_sock);
		break;
	}
	siglongjmp(imapjmp, 1);
}

static enum okay
imap_noop(mp)
	struct mailbox *mp;
{
	char	o[LINESIZE];
	FILE	*queuefp = NULL;

	snprintf(o, sizeof o, "%s NOOP\r\n", tag(1));
	IMAP_OUT(o, MB_COMD, return STOP)
	IMAP_ANSWER()
	return OKAY;
}

/*ARGSUSED*/
static void
imapalarm(s)
	int s;
{
	sighandler_type	saveint;
	sighandler_type savepipe;

	if (imaplock++ == 0) {
		saveint = safe_signal(SIGINT, SIG_IGN);
		savepipe = safe_signal(SIGPIPE, SIG_IGN);
		if (sigsetjmp(imapjmp, 1)) {
			safe_signal(SIGINT, saveint);
			safe_signal(SIGPIPE, savepipe);
			goto brk;
		}
		if (saveint != SIG_IGN)
			safe_signal(SIGINT, imapcatch);
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
		if (imap_noop(&mb) != OKAY) {
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
imap_use_starttls(uhp)
	const char	*uhp;
{
	char	*var;

	if (value("imap-use-starttls"))
		return 1;
	var = savecat("imap-use-starttls-", uhp);
	return value(var) != NULL;
}

static enum okay
imap_preauth(mp, xserver, uhp)
	struct mailbox	*mp;
	const char	*xserver, *uhp;
{
	char	*server, *cp;

	mp->mb_active |= MB_PREAUTH;
	imap_answer(mp, 1);
	if ((cp = strchr(xserver, ':')) != NULL) {
		server = salloc(cp - xserver + 1);
		memcpy(server, xserver, cp - xserver);
		server[cp - xserver] = '\0';
	} else
		server = (char *)xserver;
#ifdef	USE_SSL
	if (mp->mb_sock.s_ssl == NULL && imap_use_starttls(uhp)) {
		FILE	*queuefp = NULL;
		char	o[LINESIZE];

		snprintf(o, sizeof o, "%s STARTTLS\r\n", tag(1));
		IMAP_OUT(o, MB_COMD, return STOP);
		IMAP_ANSWER()
		if (ssl_open(server, &mp->mb_sock, uhp) != OKAY)
			return STOP;
	}
#else	/* !USE_SSL */
	if (imap_use_starttls(uhp)) {
		fprintf(stderr, "No SSL support compiled in.\n");
		return STOP;
	}
#endif	/* !USE_SSL */
	return OKAY;
}

static enum okay
imap_auth(mp, uhp, xuser, pass)
	struct mailbox *mp;
	char *xuser;
	const char *uhp, *pass;
{
	char	*var;
	char	*auth;

	if (!(mp->mb_active & MB_PREAUTH))
		return OKAY;
	if ((auth = value("imap-auth")) == NULL) {
		var = ac_alloc(strlen(uhp) + 11);
		strcpy(var, "imap-auth-");
		strcpy(&var[10], uhp);
		auth = value(var);
		ac_free(var);
	}
	if (auth == NULL || strcmp(auth, "login") == 0)
		return imap_login(mp, xuser, pass);
	if (strcmp(auth, "cram-md5") == 0)
		return imap_cram_md5(mp, xuser, pass);
	if (strcmp(auth, "gssapi") == 0) {
#ifdef	USE_GSSAPI
		return imap_gss(mp, xuser);
#else	/* !USE_GSSAPI */
		fprintf(stderr, "No GSSAPI support compiled in.\n");
		return STOP;
#endif	/* !USE_GSSAPI */
	}
	fprintf(stderr, "Unknown IMAP authentication method: \"%s\"\n", auth);
	return STOP;
}

/*
 * Implementation of RFC 2194.
 */
static enum okay
imap_cram_md5(mp, xuser, xpass)
	struct mailbox *mp;
	char *xuser;
	const char *xpass;
{
	char o[LINESIZE];
	const char	*user, *pass;
	char	*cp;
	FILE	*queuefp = NULL;
	enum okay	ok = STOP;

retry:	if (xuser == NULL) {
		if ((user = getuser()) == NULL)
			return STOP;
	} else
		user = xuser;
	if (xpass == NULL) {
		if ((pass = getpassword(&otio, &reset_tio)) == NULL)
			return STOP;
	} else
		pass = xpass;
	snprintf(o, sizeof o, "%s AUTHENTICATE CRAM-MD5\r\n", tag(1));
	IMAP_OUT(o, 0, return STOP)
	imap_answer(mp, 1);
	if (response_type != RESPONSE_CONT)
		return STOP;
	cp = cram_md5_string(user, pass, responded_text);
	IMAP_OUT(cp, MB_COMD, return STOP)
	while (mp->mb_active & MB_COMD)
		ok = imap_answer(mp, 1);
	if (ok == STOP) {
		xpass = NULL;
		goto retry;
	}
	return ok;
}

static enum okay
imap_login(mp, xuser, xpass)
	struct mailbox *mp;
	char *xuser;
	const char *xpass;
{
	char o[LINESIZE];
	const char	*user, *pass;
	FILE	*queuefp = NULL;
	enum okay	ok = STOP;

retry:	if (xuser == NULL) {
		if ((user = getuser()) == NULL)
			return STOP;
	} else
		user = xuser;
	if (xpass == NULL) {
		if ((pass = getpassword(&otio, &reset_tio)) == NULL)
			return STOP;
	} else
		pass = xpass;
	snprintf(o, sizeof o, "%s LOGIN %s %s\r\n",
			tag(1), imap_quotestr(user), imap_quotestr(pass));
	IMAP_OUT(o, MB_COMD, return STOP)
	while (mp->mb_active & MB_COMD)
		ok = imap_answer(mp, 1);
	if (ok == STOP) {
		xpass = NULL;
		goto retry;
	}
	return OKAY;
}

#ifdef	USE_GSSAPI
#include "imap_gssapi.c"
#endif	/* USE_GSSAPI */

enum okay
imap_select(mp, size, count, mbx)
	struct mailbox *mp;
	off_t *size;
	int *count;
	const char *mbx;
{
	enum okay ok = OKAY;
	char	*cp;
	char o[LINESIZE];
	FILE	*queuefp = NULL;

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
imap_flags(mp)
	struct mailbox *mp;
{
	char o[LINESIZE];
	FILE	*queuefp = NULL;
	const char	*cp;
	struct message *m;
	int n;

	if (msgcount == 0)
		return OKAY;
	snprintf(o, sizeof o,
		"%s FETCH 1:%u (RFC822.SIZE FLAGS UID INTERNALDATE)\r\n",
		tag(1), msgcount);
	IMAP_OUT(o, MB_COMD, return STOP)
	while (mp->mb_active & MB_COMD) {
		imap_answer(mp, 1);
		if (response_status == RESPONSE_OTHER &&
				response_other == MESSAGE_DATA_FETCH) {
			n = responded_other_number;
			if (n < 0 || n > msgcount)
				continue;
			m = &message[n-1];
			m->m_xsize = 0;
		} else
			continue;
		if ((cp = asccasestr(responded_other_text, "FLAGS ")) != NULL) {
			cp += 6;
			if (*cp == '(') {
				while (*cp != ')') {
					if (*cp == '\\') {
						if (ascncasecmp(cp, "\\Seen", 5)
								== 0)
							m->m_flag |= MREAD;
						else if (ascncasecmp(cp,
								"\\Recent", 7)
								== 0)
							m->m_flag |= MNEW;
						else if (ascncasecmp(cp,
								"\\Deleted", 8)
								== 0)
							m->m_flag |= MDELETED;
					}
					cp++;
				}
			}
		}
		if ((cp = asccasestr(responded_other_text, "RFC822.SIZE "))
				!= NULL)
			m->m_xsize = strtol(&cp[12], NULL, 10);
		if ((cp = asccasestr(responded_other_text, "UID ")) != NULL)
			m->m_uid = strtoul(&cp[4], NULL, 10);
		if ((cp = asccasestr(responded_other_text, "INTERNALDATE "))
				!= NULL)
			m->m_time = imap_read_date_time(&cp[13]);
		putcache(mp, m);
	}
	return OKAY;
}

static void
imap_init(mp, n)
	struct mailbox *mp;
	int n;
{
	struct message *m = &message[n];

	m->m_flag = MUSED|MNOFROM;
	m->m_block = 0;
	m->m_offset = 0;
}

static void
imap_setptr(mp, newmail, transparent)
	struct mailbox *mp;
	int newmail, transparent;
{
	struct message	*omessage = 0;
	int i, omsgcount = 0;

	if (newmail || transparent) {
		omessage = message;
		omsgcount = msgcount;
	}
	if (had_exists >= 0) {
		msgcount = had_exists;
		had_exists = -1;
	}
	if (had_expunge >= 0) {
		msgcount -= had_expunge;
		had_expunge = -1;
	}
	if (newmail && expunged_messages > 0) {
		printf("Expunged %ld messages.\n", expunged_messages);
		expunged_messages = 0;
	}
	if (msgcount < 0) {
		fputs("IMAP error: Negative message count\n", stderr);
		msgcount = 0;
	}
	message = scalloc(msgcount + 1, sizeof *message);
	for (i = 0; i < msgcount; i++)
		imap_init(mp, i);
	if (!newmail && mp->mb_type == MB_IMAP)
		initcache(mp);
	imap_flags(mp);
	message[msgcount].m_size = 0;
	message[msgcount].m_lines = 0;
	if (newmail || transparent)
		transflags(omessage, omsgcount, transparent);
	else
		setdot(message);
}

static char *
imap_have_password(server)
	const char *server;
{
	char *var, *cp;

	var = ac_alloc(strlen(server) + 10);
	strcpy(var, "password-");
	strcpy(&var[9], server);
	if ((cp = value(var)) != NULL)
		cp = savestr(cp);
	ac_free(var);
	return cp;
}

static void
imap_split(char **server, const char **sp, int *use_ssl, const char **cp,
		char **uhp, char **mbx, const char **pass, char **user)
{
	*sp = *server;
	if (strncmp(*sp, "imap://", 7) == 0) {
		*sp = &(*sp)[7];
		*use_ssl = 0;
#ifdef	USE_SSL
	} else if (strncmp(*sp, "imaps://", 8) == 0) {
		*sp = &(*sp)[8];
		*use_ssl = 1;
#endif	/* USE_SSL */
	}
	if ((*cp = strchr(*sp, '/')) != NULL && (*cp)[1] != '\0') {
		*uhp = savestr((char *)(*sp));
		(*uhp)[*cp - *sp] = '\0';
		*mbx = (char *)&(*cp)[1];
	} else {
		if (*cp)
			(*server)[*cp - *server] = '\0';
		*uhp = (char *)(*sp);
		*mbx = "INBOX";
	}
	*pass = imap_have_password(*uhp);
	if ((*cp = strchr(*uhp, '@')) != NULL) {
		*user = salloc(*cp - *uhp + 1);
		memcpy(*user, *uhp, *cp - *uhp);
		(*user)[*cp - *uhp] = '\0';
		*sp = &(*cp)[1];
	} else {
		*user = NULL;
		*sp = *uhp;
	}
}

int
imap_setfile(xserver, newmail, isedit)
	const char *xserver;
	int newmail, isedit;
{
	return imap_setfile1(xserver, newmail, isedit, 0);
}

static int
imap_setfile1(xserver, newmail, isedit, transparent)
	const char *xserver;
	int newmail, isedit, transparent;
{
	struct sock	so;
	sighandler_type	saveint;
	sighandler_type savepipe;
	char *server, *user, *account;
	const char *cp, *sp, *pass;
	char	*uhp, *mbx;
	int use_ssl = 0;

	(void)&sp;
	(void)&use_ssl;
	(void)&saveint;
	(void)&savepipe;
	server = savestr((char *)xserver);
	verbose = value("verbose") != NULL;
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
	same_imap_account = 0;
	sp = protbase(server);
	if (mb.mb_imap_account) {
		if (mb.mb_sock.s_fd >= 0 &&
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
				use_ssl ? "imaps" : "imap", verbose) != OKAY)
		return -1;
	} else
		so = mb.mb_sock;
	if (!transparent)
		quit();
	edit = isedit;
	free(mb.mb_imap_account);
	mb.mb_imap_account = account;
	if (!same_imap_account)
		mb.mb_sock.s_fd = -1;
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
		free(mb.mb_imap_mailbox);
		mb.mb_imap_mailbox = sstrdup(mbx);
		initbox(server);
	}
	mb.mb_type = MB_VOID;
	mb.mb_active = MB_NONE;;
	imaplock = 1;
	saveint = safe_signal(SIGINT, SIG_IGN);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1)) {
		sclose(&so);
		safe_signal(SIGINT, saveint);
		safe_signal(SIGPIPE, savepipe);
		imaplock = 0;
		return 1;
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
			return 1;
		}
	}
	mb.mb_perm = MB_DELE;
	mb.mb_type = MB_IMAP;
	cache_dequeue(&mb);
	if (imap_select(&mb, &mailsize, &msgcount, mbx) != OKAY) {
		/*sclose(&mb.mb_sock);
		imap_timer_off();*/
		safe_signal(SIGINT, saveint);
		safe_signal(SIGPIPE, savepipe);
		imaplock = 0;
		mb.mb_type = MB_VOID;
		return -1;
	}
newmail:
	imap_setptr(&mb, newmail, transparent);
done:	setmsize(msgcount);
	sawcom = 0;
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
	if (!newmail && mb.mb_type == MB_IMAP)
		purgecache(&mb, message, msgcount);
	if (transparent && mb.mb_sorted) {
		char	*args[2];
		mb.mb_threaded = 0;
		args[0] = mb.mb_sorted;
		args[1] = NULL;
		sort(args);
	}
	if (!newmail && !edit && msgcount == 0) {
		if ((mb.mb_type == MB_IMAP || mb.mb_type == MB_CACHE) &&
				value("emptystart") == NULL)
			fprintf(stderr, catgets(catd, CATSET, 258,
				"No mail at %s\n"), server);
		return 1;
	}
	return 0;
}

static int
imap_fetchdata(mp, m, expected, need)
	struct	mailbox *mp;
	struct	message *m;
	size_t	expected;
	int	need;
{
	char	*line = NULL, *lp;
	size_t	linesize = 0, linelen, size = 0;
	int	emptyline = 0, lines = 0, excess = 0;
	off_t	offset;

	fseek(mp->mb_otf, 0L, SEEK_END);
	offset = ftell(mp->mb_otf);
	while (sgetline(&line, &linesize, &linelen, &mp->mb_sock) > 0) {
		lp = line;
		/*
		 * Need to mask 'From ' lines. This cannot be done properly
		 * since some servers pass them as 'From ' and others as
		 * '>From '. Although one could identify the first kind of
		 * server in principle, it is not possible to identify the
		 * second as '>From ' may also come from a server of the
		 * first type as actual data. So do what is absolutely
		 * necessary only - mask 'From '.
		 */
		if (lp[0] == 'F' && lp[1] == 'r' && lp[2] == 'o' &&
				lp[3] == 'm' && lp[4] == ' ') {
			fputc('>', mp->mb_otf);
			size++;
		}
		if (linelen > expected) {
			excess = linelen - expected;
			linelen = expected;
		}
		if (lp[linelen-1] == '\n' && (linelen == 1 ||
					lp[linelen-2] == '\r')) {
			emptyline = linelen <= 2;
			if (linelen > 2)
				fwrite(lp, 1, linelen - 2, mp->mb_otf);
			fputc('\n', mp->mb_otf);
			size += linelen - 1;
		} else {
			emptyline = 0;
			fwrite(lp, 1, linelen, mp->mb_otf);
			size += linelen;
		}
		lines++;
		if ((expected -= linelen) == 0)
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
		m->m_size = size;
		m->m_lines = lines;
		m->m_block = nail_blockof(offset);
		m->m_offset = nail_offsetof(offset);
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

static enum okay
imap_get(mp, m, need)
	struct mailbox *mp;
	struct message *m;
	enum needspec need;
{
	sighandler_type	saveint = SIG_IGN;
	sighandler_type savepipe = SIG_IGN;
	char o[LINESIZE], *cp = NULL;
	size_t expected;
	int number = m - message + 1;
	enum okay ok = STOP;
	FILE	*queuefp = NULL;

	(void)&saveint;
	(void)&savepipe;
	(void)&number;
	(void)&need;
	(void)&cp;
	(void)&ok;
	verbose = value("verbose") != NULL;
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
		cp = "RFC822.HEADER";
		break;
	case NEED_BODY:
		cp = "RFC822";
		break;
	case NEED_UNSPEC:
		return STOP;
	}
	if (imaplock++ == 0) {
		saveint = safe_signal(SIGINT, SIG_IGN);
		savepipe = safe_signal(SIGPIPE, SIG_IGN);
		if (sigsetjmp(imapjmp, 1)) {
			safe_signal(SIGINT, saveint);
			safe_signal(SIGPIPE, savepipe);
			imaplock--;
			return STOP;
		}
		if (saveint != SIG_IGN)
			safe_signal(SIGINT, imapcatch);
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
	}
	if (m->m_uid)
		snprintf(o, sizeof o,
				"%s UID FETCH %lu (%s)\r\n",
				tag(1), m->m_uid, cp);
	else
		snprintf(o, sizeof o,
				"%s FETCH %u (%s)\r\n",
				tag(1), number, cp);
	IMAP_OUT(o, MB_COMD, goto out)
	do
		ok = imap_answer(mp, 1);
	while (response_status != RESPONSE_OTHER ||
			response_other != MESSAGE_DATA_FETCH);
	if (ok == STOP || (cp = strchr(responded_other_text, '{')) == NULL)
		goto out;
	expected = atol(&cp[1]);
	imap_fetchdata(mp, m, expected, need);
out:	while (mp->mb_active & MB_COMD)
		ok = imap_answer(mp, 1);
	if (saveint != SIG_IGN)
		safe_signal(SIGINT, saveint);
	if (savepipe != SIG_IGN)
		safe_signal(SIGPIPE, savepipe);
	imaplock--;
	if (ok == OKAY)
		putcache(mp, m);
	return ok;
}

enum okay
imap_header(m)
	struct message *m;
{
	return imap_get(&mb, m, NEED_HEADER);
}


enum okay
imap_body(m)
	struct message *m;
{
	return imap_get(&mb, m, NEED_BODY);
}

static void
commithdr(mp, to, from)
	struct mailbox *mp;
	struct message *to, from;
{
	to->m_size = from.m_size;
	to->m_lines = from.m_lines;
	to->m_block = from.m_block;
	to->m_offset = from.m_offset;
	to->m_have = HAVE_HEADER;
	putcache(mp, to);
}

static enum okay
imap_fetchheaders(mp, m, bot, top)
	struct mailbox	*mp;
	struct message	*m;
	int	bot, top;	/* bot > top */
{
	char	o[LINESIZE], *cp;
	struct message	mt;
	size_t	expected;
	enum okay	ok;
	int	n = 0, u, sz;
	FILE	*queuefp = NULL;

	if (m[bot].m_uid)
		snprintf(o, sizeof o,
			"%s UID FETCH %lu:%lu (RFC822.HEADER)\r\n",
			tag(1), m[bot-1].m_uid, m[top-1].m_uid);
	else
		snprintf(o, sizeof o,
			"%s FETCH %u:%u (RFC822.HEADER)\r\n",
			tag(1), bot, top);
	IMAP_OUT(o, MB_COMD, return STOP)
	for (;;) {
		ok = imap_answer(mp, 1);
		if (response_status != RESPONSE_OTHER)
			break;
		if (response_other != MESSAGE_DATA_FETCH)
			continue;
		if (ok == STOP || (cp = strchr(responded_other_text, '{')) == 0)
			return STOP;
		expected = atol(&cp[1]);
		if (m[bot-1].m_uid) {
			if ((cp=asccasestr(responded_other_text, "UID "))) {
				u = atol(&cp[4]);
				for (n = bot; n <= top; n++)
					if (m[n-1].m_uid == u)
						break;
				if (n > top) {
					imap_fetchdata(mp, NULL, expected,
							NEED_HEADER);
					continue;
				}
			} else
				n = -1;
		} else {
			n = responded_other_number;
			if (n <= 0 || n > msgcount) {
				imap_fetchdata(mp, NULL, expected, NEED_HEADER);
				continue;
			}
		}
		imap_fetchdata(mp, &mt, expected, NEED_HEADER);
		if (n > 0 && !(m[n-1].m_have & HAVE_HEADER))
			commithdr(mp, &m[n-1], mt);
		if (n == -1 && (sz = sgetline(&imapbuf, &imapbufsize, NULL,
					&mp->mb_sock)) > 0) {
			if (verbose)
				fputs(imapbuf, stderr);
			if ((cp = asccasestr(imapbuf, "UID ")) != NULL) {
				u = atol(&cp[4]);
				for (n = bot; n <= top; n++)
					if (m[n-1].m_uid == u)
						break;
				if (n <= top && !(m[n-1].m_have & HAVE_HEADER))
					commithdr(mp, &m[n-1], mt);
				n = 0;
			}
		}
	}
	while (mp->mb_active & MB_COMD)
		ok = imap_answer(mp, 1);
	return ok;
}

void
imap_getheaders(bot, top)
	int bot, top;
{
	sighandler_type	saveint, savepipe;
	enum okay	ok = STOP;
	int	i;

	(void)&saveint;
	(void)&savepipe;
	(void)&ok;
	(void)&bot;
	(void)&top;
	verbose = value("verbose") != NULL;
	if (mb.mb_type == MB_CACHE)
		return;
	if (bot < 1)
		bot = 1;
	if (top > msgcount)
		top = msgcount;
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
	saveint = safe_signal(SIGINT, SIG_IGN);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1) == 0) {
		if (saveint != SIG_IGN)
			safe_signal(SIGINT, imapcatch);
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
		ok = imap_fetchheaders(&mb, message, bot, top);
	}
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
}

static enum okay
imap_exit(mp)
	struct mailbox *mp;
{
	char	o[LINESIZE];
	FILE	*queuefp = NULL;

	verbose = value("verbose") != NULL;
	mp->mb_active |= MB_BYE;
	snprintf(o, sizeof o, "%s LOGOUT\r\n", tag(1));
	IMAP_OUT(o, MB_COMD, return STOP)
	IMAP_ANSWER()
	return OKAY;
}

static enum okay
imap_delete(mp, n, m)
	struct mailbox *mp;
	int n;
	struct message *m;
{
	char o[LINESIZE];
	FILE	*queuefp = NULL;

	verbose = value("verbose") != NULL;
	if (mp->mb_type == MB_CACHE && (queuefp = cache_queue(mp)) == NULL)
		return STOP;
	if (m->m_uid)
		snprintf(o, sizeof o,
				"%s UID STORE %lu +FLAGS (\\Deleted)\r\n",
				tag(1), m->m_uid);
	else
		snprintf(o, sizeof o,
				"%s STORE %u +FLAGS (\\Deleted)\r\n",
				tag(1), n);
	IMAP_OUT(o, MB_COMD, return STOP)
	IMAP_ANSWER()
	delcache(mp, m);
	if (queuefp != NULL)
		Fclose(queuefp);
	return OKAY;
}

static enum okay
imap_expunge(mp)
	struct mailbox *mp;
{
	char	o[LINESIZE];
	FILE	*queuefp = NULL;

	snprintf(o, sizeof o, "%s EXPUNGE\r\n", tag(1));
	IMAP_OUT(o, MB_COMD, return STOP)
	IMAP_ANSWER()
	return OKAY;
}

static enum okay
imap_update(mp)
	struct mailbox *mp;
{
	FILE *readstat = NULL;
	struct message *m;
	int dodel, c, gotcha = 0, held = 0;

	verbose = value("verbose") != NULL;
	if (Tflag != NULL) {
		if ((readstat = Zopen(Tflag, "w", NULL)) == NULL)
			Tflag = NULL;
	}
	for (m = &message[0]; m < &message[msgcount]; m++)
		putcache(mp, m);
	if (!edit) {
		holdbits();
		for (m = &message[0], c = 0; m < &message[msgcount]; m++) {
			if (m->m_flag & MBOX)
				c++;
		}
		if (c > 0)
			if (makembox() == STOP)
				goto bypass;
	}
	for (m = &message[0], gotcha=0, held=0; m < &message[msgcount]; m++) {
		if (readstat != NULL && (m->m_flag & (MREAD|MDELETED)) != 0) {
			char *id;

			if ((id = hfield("message-id", m)) != NULL ||
					(id = hfield("article-id", m)) != NULL)
				fprintf(readstat, "%s\n", id);
		}
		if (edit) {
			dodel = m->m_flag & MDELETED;
		} else {
			dodel = !((m->m_flag&MPRESERVE) ||
					(m->m_flag&MTOUCH) == 0);
		}
		if (dodel) {
			imap_delete(mp, m - message + 1, m);
			gotcha++;
		} else
			held++;
	}
bypass:	if (readstat != NULL)
		Fclose(readstat);
	if (gotcha)
		imap_expunge(mp);
	if (gotcha && edit) {
		printf(catgets(catd, CATSET, 168, "\"%s\" "), mailname);
		printf(value("bsdcompat") || value("bsdmsgs") ?
				catgets(catd, CATSET, 170, "complete\n") :
				catgets(catd, CATSET, 212, "updated.\n"));
	} else if (held && !edit) {
		if (held == 1)
			printf(catgets(catd, CATSET, 155,
				"Held 1 message in %s\n"), mailname);
		else if (held > 1)
			printf(catgets(catd, CATSET, 156,
				"Held %d messages in %s\n"), held, mailname);
	}
	fflush(stdout);
	return OKAY;
}

void
imap_quit()
{
	sighandler_type	saveint;
	sighandler_type savepipe;

	verbose = value("verbose") != NULL;
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
		sclose(&mb.mb_sock);
	}
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
}

static enum okay
imap_undelete1(mp, m, n)
	struct mailbox *mp;
	struct message *m;
	int n;
{
	char	o[LINESIZE];
	FILE	*queuefp = NULL;

	if (mp->mb_type == MB_CACHE && (queuefp = cache_queue(mp)) == NULL)
		return STOP;
	if (m->m_uid)
		snprintf(o, sizeof o,
				"%s UID STORE %lu -FLAGS (\\Deleted)\r\n",
				tag(1), m->m_uid);
	else
		snprintf(o, sizeof o,
				"%s STORE %u +FLAGS (\\Deleted)\r\n",
				tag(1), n);
	IMAP_OUT(o, MB_COMD, return STOP)
	IMAP_ANSWER()
	if (queuefp != NULL)
		Fclose(queuefp);
	return OKAY;
}

enum okay
imap_undelete(m, n)
	struct message *m;
	int n;
{
	sighandler_type	saveint, savepipe;
	enum okay	ok = STOP;

	(void)&saveint;
	(void)&savepipe;
	(void)&ok;
	verbose = value("verbose") != NULL;
	imaplock = 1;
	saveint = safe_signal(SIGINT, SIG_IGN);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1) == 0) {
		if (saveint != SIG_IGN)
			safe_signal(SIGINT, imapcatch);
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
		ok = imap_undelete1(&mb, m, n);
	}
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
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

static time_t
imap_read_date_time(cp)
	const char *cp;
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

static const char *
imap_make_date_time(t)
	time_t t;
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

static const char *
imap_quotestr(s)
	const char *s;
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

static const char *
imap_unquotestr(s)
	const char *s;
{
	char	*n, *np;

	if (*s != '"')
		return s;
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

int
imap_imap(vp)
	void *vp;
{
	sighandler_type	saveint, savepipe;
	char	o[LINESIZE];
	enum okay	ok = STOP;
	struct mailbox	*mp = &mb;
	FILE	*queuefp = NULL;

	(void)&saveint;
	(void)&savepipe;
	(void)&ok;
	verbose = value("verbose") != NULL;
	if (mp->mb_type != MB_IMAP) {
		printf("Not operating on an IMAP mailbox.\n");
		return 1;
	}
	imaplock = 1;
	saveint = safe_signal(SIGINT, SIG_IGN);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1) == 0) {
		if (saveint != SIG_IGN)
			safe_signal(SIGINT, imapcatch);
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
	return ok != OKAY;
}

int
imap_newmail(autoinc)
	int	autoinc;
{
	if (autoinc && had_exists < 0 && had_expunge < 0) {
		verbose = value("verbose") != NULL;
		imaplock = 1;
		imap_noop(&mb);
		imaplock = 0;
	}
	if (had_exists == msgcount && had_expunge < 0)
		/*
		 * Some servers always respond with EXISTS to NOOP. If
		 * the mailbox has been changed but the number of messages
		 * has not, an EXPUNGE must also had been sent; otherwise,
		 * nothing has changed.
		 */
		had_exists = -1;
	return had_exists >= 0 || had_expunge >= 0;
}

static enum okay
imap_append1(mp, name, fp, off1, xsize, flag, t)
	struct mailbox *mp;
	const char *name;
	FILE *fp;
	off_t off1;
	long xsize;
	int flag;
	time_t t;
{
	char	o[LINESIZE];
	char	*buf;
	size_t	bufsize, buflen, count;
	enum okay	ok = STOP;
	long	size;
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
	fseek(fp, off1, SEEK_SET);
	snprintf(o, sizeof o, "%s APPEND %s %s%s {%ld}\r\n",
			tag(1), imap_quotestr(name),
			flag & MREAD ? "(\\Seen) " : "",
			imap_make_date_time(t),
			size);
	IMAP_OUT(o, MB_COMD, goto out)
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
	while (size > 0) {
		fgetline(&buf, &bufsize, &count, &buflen, fp, 1);
		buf[buflen-1] = '\r';
		buf[buflen] = '\n';
		if (mp->mb_type == MB_IMAP)
			swrite1(&mp->mb_sock, buf, buflen+1, 1);
		else if (queuefp)
			fwrite(buf, 1, buflen+1, queuefp);
		size -= buflen+1;
	}
	if (mp->mb_type == MB_IMAP)
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
			IMAP_OUT(o, MB_COMD, goto out);
			while (mp->mb_active & MB_COMD)
				ok = imap_answer(mp, 1);
			if (ok == STOP)
				goto out;
			imap_created_mailbox++;
			goto again;
		} else if (ok != OKAY)
			fprintf(stderr, "IMAP error: %s", responded_text);
	}
	if (queuefp != NULL)
		Fclose(queuefp);
out:	free(buf);
	return ok;
}

static enum okay
imap_append0(mp, name, fp)
	struct mailbox *mp;
	const char *name;
	FILE *fp;
{
	char	*buf, *bp, *lp;
	size_t	bufsize, buflen, count;
	off_t	off1 = -1, offs;
	int	inhead = 1;
	int	flag = MNEW;
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
			tim = unixtime(buf);
		} else
			size += buflen+1;
		offs += buflen;
		if (buf[0] == '\n')
			inhead = 0;
		else if (inhead && ascncasecmp(buf, "status", 6) == 0) {
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
						flag |= ~MNEW;
						break;
					}
		}
	} while (bp != NULL);
	free(buf);
	return OKAY;
}

enum okay
imap_append(xserver, fp)
	const char *xserver;
	FILE *fp;
{
	sighandler_type	saveint, savepipe;
	char	*server, *uhp, *mbx, *user;
	const char	*sp, *cp, *pass;
	int	use_ssl;
	enum okay	ok = STOP;

	(void)&saveint;
	(void)&savepipe;
	(void)&ok;
	verbose = value("verbose") != NULL;
	server = savestr((char *)xserver);
	imap_split(&server, &sp, &use_ssl, &cp, &uhp, &mbx, &pass, &user);
	imaplock = 1;
	saveint = safe_signal(SIGINT, SIG_IGN);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1))
		goto out;
	if (saveint != SIG_IGN)
		safe_signal(SIGINT, imapcatch);
	if (savepipe != SIG_IGN)
		safe_signal(SIGPIPE, imapcatch);
	if ((mb.mb_type == MB_CACHE || mb.mb_sock.s_fd >= 0) &&
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
					verbose) != OKAY)
				goto fail;
			mx.mb_sock.s_desc = "IMAP";
			mx.mb_type = MB_IMAP;
			mx.mb_imap_account = (char *)protbase(server);
			mx.mb_imap_mailbox = mbx;
			if (imap_preauth(&mx, sp, uhp) != OKAY ||
					imap_auth(&mx, uhp, user, pass)!=OKAY) {
				sclose(&mx.mb_sock);
				goto fail;
			}
			ok = imap_append0(&mx, mbx, fp);
			imap_exit(&mx);
			sclose(&mx.mb_sock);
		} else {
			mx.mb_imap_account = (char *)protbase(server);
			mx.mb_imap_mailbox = mbx;
			mx.mb_type = MB_CACHE;
			ok = imap_append0(&mx, mbx, fp);
		}
	fail:;
	}
out:	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
	return ok;
}

static enum {
	LIST_NONE		= 000,
	LIST_NOINFERIORS	= 001,
	LIST_NOSELECT		= 002,
	LIST_MARKED		= 004,
	LIST_UNMARKED		= 010
} list_attributes;
static int	list_hierarchy_delimiter;
static char	*list_name;

static enum okay
imap_parse_list()
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
imap_list(mp, base, fp)
	struct mailbox *mp;
	const char *base;
	FILE *fp;
{
	char	o[LINESIZE];
	enum okay	ok = STOP;
	const char	*cp, *bp;
	FILE	*queuefp = NULL;

	snprintf(o, sizeof o, "%s LIST %s %%\r\n",
			tag(1), imap_quotestr(base));
	IMAP_OUT(o, MB_COMD, return STOP);
	while (mp->mb_active & MB_COMD) {
		ok = imap_answer(mp, 1);
		if (response_status == RESPONSE_OTHER &&
				response_other == MAILBOX_DATA_LIST &&
				imap_parse_list() == OKAY) {
			cp = imap_unquotestr(list_name);
			for (bp = base; *bp && *bp == *cp; bp++)
				cp++;
			fprintf(fp, "%s\n", *cp ? cp : "INBOX");
		}
	}
	return ok;
}

void
imap_folders()
{
	sighandler_type	saveint, savepipe;
	char	*fold, *tempfn;
	FILE	*fp;

	(void)&saveint;
	(void)&savepipe;
	(void)&fp;
	(void)&fold;
	if ((fold = value("folder")) == NULL)
		fold = "";
	if ((fp = Ftemp(&tempfn, "Ri", "w+", 0600, 1)) == NULL) {
		perror("tmpfile");
		return;
	}
	unlink(tempfn);
	imaplock = 1;
	saveint = safe_signal(SIGINT, SIG_IGN);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1))
		goto out;
	if (saveint != SIG_IGN)
		safe_signal(SIGINT, imapcatch);
	if (savepipe != SIG_IGN)
		safe_signal(SIGPIPE, imapcatch);
	if (mb.mb_type == MB_CACHE)
		cache_list(&mb, protfile(fold), fp);
	else
		imap_list(&mb, protfile(fold), fp);
	imaplock = 0;
	fflush(fp);
	rewind(fp);
	if (fsize(fp) > 0)
		/*
		 * This is not optimal. Unless we get a System V version
		 * of pr, -F will not do what we want and names will be
		 * truncated.
		 */
		run_command("pr -4 -F -t", 0, fileno(fp), -1, NULL, NULL, NULL);
out:
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	Fclose(fp);
}

static enum okay
imap_copy1(mp, m, n, name)
	struct mailbox *mp;
	struct message *m;
	int n;
	const char *name;
{
	char	o[LINESIZE];
	enum okay	ok = STOP;
	int	twice = 0;
	FILE	*queuefp = NULL;

	if (mp->mb_type == MB_CACHE) {
		if ((queuefp = cache_queue(mp)) == NULL)
			return STOP;
		ok = OKAY;
	}
	name = imap_quotestr(protfile(name));
again:	if (m->m_uid)
		snprintf(o, sizeof o, "%s UID COPY %lu %s\r\n",
				tag(1), m->m_uid, name);
	else
		snprintf(o, sizeof o, "%s COPY %u %s\r\n",
				tag(1), n, name);
	IMAP_OUT(o, MB_COMD, return STOP)
	while (mp->mb_active & MB_COMD)
		ok = imap_answer(mp, twice);
	if (response_status == RESPONSE_NO && twice++ == 0) {
		snprintf(o, sizeof o, "%s CREATE %s\r\n", tag(1), name);
		IMAP_OUT(o, MB_COMD, return STOP)
		while (mp->mb_active & MB_COMD)
			ok = imap_answer(mp, 1);
		if (ok == OKAY) {
			imap_created_mailbox++;
			goto again;
		}
	}
	if (queuefp != NULL)
		Fclose(queuefp);
	return ok;
}

enum okay
imap_copy(m, n, name)
	struct message *m;
	int n;
	const char *name;
{
	sighandler_type	saveint, savepipe;
	enum okay	ok = STOP;

	(void)&saveint;
	(void)&savepipe;
	(void)&ok;
	verbose = value("verbose") != NULL;
	imaplock = 1;
	saveint = safe_signal(SIGINT, SIG_IGN);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(imapjmp, 1) == 0) {
		if (saveint != SIG_IGN)
			safe_signal(SIGINT, imapcatch);
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, imapcatch);
		ok = imap_copy1(&mb, m, n, name);
	}
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	imaplock = 0;
	return ok;
}

int
imap_thisaccount(cp)
	const char *cp;
{
	if ((mb.mb_type != MB_CACHE && mb.mb_sock.s_fd < 0) ||
			mb.mb_imap_account == NULL)
		return 0;
	return strcmp(protbase(cp), mb.mb_imap_account) == 0;
}

enum okay
imap_dequeue(mp, fp)
	struct mailbox	*mp;
	FILE	*fp;
{
	FILE	*queuefp = NULL;
	char	o[LINESIZE], *newname;
	char	*buf, *bp, *cp, iob[4096];
	size_t	bufsize, buflen, count;
	enum okay	ok = OKAY, rok = OKAY;
	long	offs, octets;
	int	n, twice, gotcha = 0;

	buf = smalloc(bufsize = LINESIZE);
	buflen = 0;
	count = fsize(fp);
	while (fgetline(&buf, &bufsize, &count, &buflen, fp, 0) != NULL) {
		for (bp = buf; *bp != ' '; bp++);	/* strip old tag */
		while (*bp == ' ')
			bp++;
		twice = 0;
		offs = ftell(fp);
	again:	snprintf(o, sizeof o, "%s %s", tag(1), bp);
		if (ascncasecmp(bp, "UID COPY ", 9) == 0) {
			cp = &bp[9];
			while (digitchar(*cp&0377))
				cp++;
			if (*cp != ' ')
				goto fail;
			while (*cp == ' ')
				cp++;
			if ((newname = imap_strex(cp)) == NULL)
				goto fail;
			IMAP_OUT(o, MB_COMD, continue)
			while (mp->mb_active & MB_COMD)
				ok = imap_answer(mp, twice);
			if (response_status == RESPONSE_NO && twice++ == 0)
				goto trycreate;
		} else if (ascncasecmp(bp, "UID STORE ", 10) == 0) {
			IMAP_OUT(o, MB_COMD, continue)
			while (mp->mb_active & MB_COMD)
				ok = imap_answer(mp, 1);
			if (ok == OKAY)
				gotcha++;
		} else if (ascncasecmp(bp, "APPEND ", 7) == 0) {
			if ((cp = strchr(bp, '{')) == NULL)
				goto fail;
			octets = atol(&cp[1]) + 2;
			if ((newname = imap_strex(&bp[7])) == NULL)
				goto fail;
			IMAP_OUT(o, MB_COMD, continue)
			while (mp->mb_active & MB_COMD) {
				ok = imap_answer(mp, twice);
				if (response_type == RESPONSE_CONT)
					break;
			}
			if (ok == STOP) {
				if (twice++ == 0) {
					fseek(fp, offs, SEEK_SET);
					goto trycreate;
				}
				goto fail;
			}
			while (octets > 0) {
				n = octets > sizeof iob ? sizeof iob : octets;
				octets -= n;
				if (fread(iob, 1, n, fp) != n)
					goto fail;
				swrite1(&mp->mb_sock, iob, n, 1);
			}
			swrite(&mp->mb_sock, "");
			while (mp->mb_active & MB_COMD) {
				ok = imap_answer(mp, 0);
				if (response_status == RESPONSE_NO &&
						twice++ == 0) {
					fseek(fp, offs, SEEK_SET);
					goto trycreate;
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
		imap_expunge(mp);
	return rok;
}

static char *
imap_strex(cp)
	const char	*cp;
{
	const char	*cq;
	char	*n;

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
	return n;
}

/*ARGSUSED*/
int
cconnect(vp)
	void	*vp;
{
	char	*cp, *cq;

	if (mb.mb_type == MB_IMAP && mb.mb_sock.s_fd >= 0) {
		fprintf(stderr, "Already connected.\n");
		return 1;
	}
	unset_allow_undefined = 1;
	unset_internal("disconnected");
	cp = protbase(mailname);
	if (strncmp(cp, "imap://", 7) == 0)
		cp += 7;
	else if (strncmp(cp, "imaps://", 8) == 0)
		cp += 8;
	if ((cq = strchr(cp, ':')) != NULL)
		*cq = '\0';
	unset_internal(savecat("disconnected-", cp));
	unset_allow_undefined = 0;
	if (mb.mb_type == MB_CACHE)
		imap_setfile1(mailname, 0, edit, 1);
	return 0;
}

/*ARGSUSED*/
int
cdisconnect(vp)
	void	*vp;
{
	if (mb.mb_type == MB_CACHE) {
		fprintf(stderr, "Not connected.\n");
		return 1;
	} else if (mb.mb_type == MB_IMAP) {
		if (cached_uidvalidity(&mb) == 0) {
			fprintf(stderr, "The current mailbox is not cached.\n");
			return 1;
		}
	}
	assign("disconnected", "");
	if (mb.mb_type == MB_IMAP) {
		sclose(&mb.mb_sock);
		imap_setfile1(mailname, 0, edit, 1);
	}
	return 0;
}
#else	/* !HAVE_SOCKETS */
static void
noimap()
{
	fprintf(stderr, catgets(catd, CATSET, 216,
				"No IMAP support compiled in.\n"));
}

int
imap_setfile(server, newmail, isedit)
	const char *server;
{
	noimap();
	return -1;
}

enum okay
imap_header(mp)
	struct message *mp;
{
	noimap();
	return STOP;
}

enum okay
imap_body(mp)
	struct message *mp;
{
	noimap();
	return STOP;
}

void
imap_getheaders(bot, top)
	int bot, top;
{
}

void
imap_quit()
{
	noimap();
}

/*ARGSUSED*/
int
imap_imap(vp)
	void *vp;
{
	noimap();
	return 1;
}

/*ARGSUSED*/
int
imap_newmail(dummy)
	int	dummy;
{
	return 0;
}

/*ARGSUSED*/
enum okay
imap_undelete(m, n)
	struct message *m;
	int n;
{
	return STOP;
}

enum okay
imap_append(server, fp)
	const char *server;
	FILE *fp;
{
	noimap();
	return STOP;
}

void
imap_folders()
{
	noimap();
}

enum okay
imap_copy(m, n, name)
	struct message *m;
	int n;
	const char *name;
{
	noimap();
	return STOP;
}

int
imap_thisaccount(cp)
	const char *cp;
{
	return 0;
}

/*ARGSUSED*/
int
cconnect(vp)
	void	*vp;
{
	noimap();
	return 1;
}

/*ARGSUSED*/
int
cdisconnect(vp)
	void	*vp;
{
	noimap();
	return 1;
}
#endif	/* HAVE_SOCKETS */
