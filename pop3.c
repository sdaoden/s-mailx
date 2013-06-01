/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ POP3 (RFCs 1939, 2595) client.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 2002
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

#include "config.h"

#ifndef USE_POP3
typedef int avoid_empty_file_compiler_warning;
#else
#include "rcv.h"

#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include "extern.h"
#ifdef USE_MD5
# include "md5.h"
#endif

#define POP3_ANSWER()	POP3_XANSWER(return STOP);
#define POP3_XANSWER(ACTIONSTOP) \
{\
	if (pop3_answer(mp) == STOP) {\
		ACTIONSTOP;\
	}\
}

#define POP3_OUT(X,Y)	POP3_XOUT(X, Y, return STOP);
#define POP3_XOUT(X,Y,ACTIONSTOP) \
{\
	if (pop3_finish(mp) == STOP) {\
		ACTIONSTOP;\
	}\
	if (options & OPT_VERBOSE)\
		fprintf(stderr, ">>> %s", X);\
	mp->mb_active |= Y;\
	if (swrite(&mp->mb_sock, X) == STOP) {\
		ACTIONSTOP;\
	}\
}

static char	*pop3buf;
static size_t	pop3bufsize;
static sigjmp_buf	pop3jmp;
static sighandler_type savealrm;
static int	pop3keepalive;
static volatile int	pop3lock;

/* Perform entire login handshake */
static enum okay	_pop3_login(struct mailbox *mp, char *xuser, char *pass,
				char const *uhp, char const *xserver);

/* APOP: get greeting credential or NULL */
#ifdef USE_MD5
static char *		_pop3_lookup_apop_timestamp(char const *bp);
#endif

static bool_t		_pop3_use_starttls(char const *uhp);

/* APOP: shall we use it (for *uhp*)? */
static bool_t		_pop3_no_apop(char const *uhp);

/* Several authentication methods */
static enum okay	_pop3_auth_plain(struct mailbox *mp, char *xuser,
				char *pass);
#ifdef USE_MD5
static enum okay	_pop3_auth_apop(struct mailbox *mp, char *xuser,
				char *pass, char const *ts);
#endif

static void pop3_timer_off(void);
static enum okay pop3_answer(struct mailbox *mp);
static enum okay pop3_finish(struct mailbox *mp);
static void pop3catch(int s);
static void maincatch(int s);
static enum okay pop3_noop1(struct mailbox *mp);
static void pop3alarm(int s);
static enum okay pop3_stat(struct mailbox *mp, off_t *size, int *count);
static enum okay pop3_list(struct mailbox *mp, int n, size_t *size);
static void pop3_init(struct mailbox *mp, int n);
static void pop3_dates(struct mailbox *mp);
static void pop3_setptr(struct mailbox *mp);
static enum okay pop3_get(struct mailbox *mp, struct message *m,
		enum needspec need);
static enum okay pop3_exit(struct mailbox *mp);
static enum okay pop3_delete(struct mailbox *mp, int n);
static enum okay pop3_update(struct mailbox *mp);

static enum okay
_pop3_login(struct mailbox *mp, char *xuser, char *pass, char const *uhp,
	char const *xserver)
{
#ifdef USE_MD5
	char *ts;
#endif
	char *cp;
	enum okay rv = STOP;

	/* Get the greeting, check wether APOP is advertised */
	POP3_XANSWER(goto jleave);
#ifdef USE_MD5
	ts = _pop3_lookup_apop_timestamp(pop3buf);
#endif

	if ((cp = strchr(xserver, ':')) != NULL) { /* TODO GENERIC URI PARSE! */
		size_t l = (size_t)(cp - xserver);
		char *x = salloc(l + 1);
		memcpy(x, xserver, l);
		x[l] = '\0';
		xserver = x;
	}

	/* If not yet secured, can we upgrade to TLS? */
#ifdef USE_SSL
	if (mp->mb_sock.s_use_ssl == 0 && _pop3_use_starttls(uhp)) {
		POP3_XOUT("STLS\r\n", MB_COMD, goto jleave);
		POP3_XANSWER(goto jleave);
		if (ssl_open(xserver, &mp->mb_sock, uhp) != OKAY)
			goto jleave;
	}
#else
	if (_pop3_use_starttls(uhp)) {
		fprintf(stderr, "No SSL support compiled in.\n");
		goto jleave;
	}
#endif

	/* Use the APOP single roundtrip? */
	if (! _pop3_no_apop(uhp)) {
#ifdef USE_MD5
		if (ts != NULL) {
			rv = _pop3_auth_apop(mp, xuser, pass, ts);
			if (rv != OKAY)
				fprintf(stderr, tr(276,
					"POP3 `APOP' authentication failed, "
					"maybe try setting *pop3-no-apop*\n"));
			goto jleave;
		} else
#endif
		if (options & OPT_VERBOSE)
			fprintf(stderr, tr(204, "No POP3 `APOP' support "
				"available, sending password in clear text\n"));
	}
	rv = _pop3_auth_plain(mp, xuser, pass);
jleave:
	return rv;
}

static char *
_pop3_lookup_apop_timestamp(char const *bp)
{
	/*
	 * RFC 1939:
	 * A POP3 server which implements the APOP command will include
	 * a timestamp in its banner greeting.  The syntax of the timestamp
	 * corresponds to the `msg-id' in [RFC822]
	 * RFC 822:
	 * msg-id	= "<" addr-spec ">"
	 * addr-spec	= local-part "@" domain
	 */
	char const *cp, *ep;
	size_t tl;
	char *rp = NULL;
	bool_t hadat = FAL0;

	if ((cp = strchr(bp, '<')) == NULL)
		goto jleave;

	/* xxx What about malformed APOP timestamp (<@>) here? */
	for (ep = cp; *ep; ep++) {
		if (spacechar(*ep))
			goto jleave;
		else if (*ep == '@')
			hadat = TRU1;
		else if (*ep == '>') {
			if (! hadat)
				goto jleave;
			break;
		}
	}
	if (*ep != '>')
		goto jleave;

	tl = (size_t)(++ep - cp);
	rp = salloc(tl + 1);
	memcpy(rp, cp, tl);
	rp[tl] = '\0';
jleave:
	return rp;
}

static bool_t
_pop3_use_starttls(char const *uhp)
{
	char *var;

	if (value("pop3-use-starttls"))
		return TRU1;
	var = savecat("pop3-use-starttls-", uhp);
	return value(var) != NULL;
}

static bool_t
_pop3_no_apop(char const *uhp)
{
	bool_t ret;

	if (! (ret = boption("pop3-no-apop"))) {
#define __S	"pop3-no-apop-"
#define __SL	sizeof(__S)
		size_t i = strlen(uhp);
		char *var = ac_alloc(i + __SL);
		memcpy(var, __S, __SL - 1);
		memcpy(var + __SL - 1, uhp, i + 1);
		ret = boption(var);
		ac_free(var);
#undef __SL
#undef __S
	}
	return ret;
}

#ifdef USE_MD5
static enum okay
_pop3_auth_apop(struct mailbox *mp, char *xuser, char *pass, char const *ts)
{
	enum okay rv = STOP;
	unsigned char digest[16];
	char hex[MD5TOHEX_SIZE];
	MD5_CTX	ctx;
	size_t tl, i;
	char *user, *cp;

	for (tl = strlen(ts);;) {
		user = xuser;
		if (! getcredentials(&user, &pass))
			break;

		MD5Init(&ctx);
		MD5Update(&ctx, (unsigned char*)UNCONST(ts), tl);
		MD5Update(&ctx, (unsigned char*)pass, strlen(pass));
		MD5Final(digest, &ctx);
		md5tohex(hex, digest);

		i = strlen(user);
		cp = ac_alloc(5 + i + 1 + MD5TOHEX_SIZE + 3);

		memcpy(cp, "APOP ", 5);
		memcpy(cp + 5, user, i);
		i += 5;
		cp[i++] = ' ';
		memcpy(cp + i, hex, MD5TOHEX_SIZE);
		i += MD5TOHEX_SIZE;
		memcpy(cp + i, "\r\n\0", 3);
		POP3_XOUT(cp, MB_COMD, goto jcont);
		POP3_XANSWER(goto jcont);
		rv = OKAY;
jcont:
		ac_free(cp);
		if (rv == OKAY)
			break;
		pass = NULL;
	}
	return rv;
}
#endif /* USE_MD5 */

static enum okay
_pop3_auth_plain(struct mailbox *mp, char *xuser, char *pass)
{
	enum okay rv = STOP;
	char *user, *cp;
	size_t ul, pl;

	/* The USER/PASS plain text version */
	for (;;) {
		user = xuser;
		if (! getcredentials(&user, &pass))
			break;

		ul = strlen(user);
		pl = strlen(pass);
		cp = ac_alloc(MAX(ul, pl) + 5 + 2 +1);

		memcpy(cp, "USER ", 5);
		memcpy(cp + 5, user, ul);
		memcpy(cp + 5 + ul, "\r\n\0", 3);
		POP3_XOUT(cp, MB_COMD, goto jcont);
		POP3_XANSWER(goto jcont);

		memcpy(cp, "PASS ", 5);
		memcpy(cp + 5, pass, pl);
		memcpy(cp + 5 + pl, "\r\n\0", 3);
		POP3_XOUT(cp, MB_COMD, goto jcont);
		POP3_XANSWER(goto jcont);
		rv = OKAY;
jcont:
		ac_free(cp);
		if (rv == OKAY)
			break;
		pass = NULL;
	}
	return rv;
}

static void
pop3_timer_off(void)
{
	if (pop3keepalive > 0) {
		alarm(0);
		safe_signal(SIGALRM, savealrm);
	}
}

static enum okay
pop3_answer(struct mailbox *mp)
{
	int sz;
	enum okay ok = STOP;

retry:	if ((sz = sgetline(&pop3buf, &pop3bufsize, NULL, &mp->mb_sock)) > 0) {
		if ((mp->mb_active & (MB_COMD|MB_MULT)) == MB_MULT)
			goto multiline;
		if (options & OPT_VERBOSE)
			fputs(pop3buf, stderr);
		switch (*pop3buf) {
		case '+':
			ok = OKAY;
			mp->mb_active &= ~MB_COMD;
			break;
		case '-':
			ok = STOP;
			mp->mb_active = MB_NONE;
			fprintf(stderr, catgets(catd, CATSET, 218,
					"POP3 error: %s"), pop3buf);
			break;
		default:
			/*
			 * If the answer starts neither with '+' nor with
			 * '-', it must be part of a multiline response,
			 * e. g. because the user interrupted a file
			 * download. Get lines until a single dot appears.
			 */
	multiline:	 while (pop3buf[0] != '.' || pop3buf[1] != '\r' ||
					pop3buf[2] != '\n' ||
					pop3buf[3] != '\0') {
				sz = sgetline(&pop3buf, &pop3bufsize,
						NULL, &mp->mb_sock);
				if (sz <= 0)
					goto eof;
			}
			mp->mb_active &= ~MB_MULT;
			if (mp->mb_active != MB_NONE)
				goto retry;
		}
	} else {
	eof: 	ok = STOP;
		mp->mb_active = MB_NONE;
	}
	return ok;
}

static enum okay
pop3_finish(struct mailbox *mp)
{
	while (mp->mb_sock.s_fd > 0 && mp->mb_active != MB_NONE)
		(void)pop3_answer(mp);
	return OKAY;
}

static void
pop3catch(int s)
{
	termios_state_reset();
	switch (s) {
	case SIGINT:
		fprintf(stderr, catgets(catd, CATSET, 102, "Interrupt\n"));
		siglongjmp(pop3jmp, 1);
		break;
	case SIGPIPE:
		fprintf(stderr, "Received SIGPIPE during POP3 operation\n");
		break;
	}
}

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
pop3_noop1(struct mailbox *mp)
{
	POP3_OUT("NOOP\r\n", MB_COMD)
	POP3_ANSWER()
	return OKAY;
}

enum okay
pop3_noop(void)
{
	enum okay	ok = STOP;
	sighandler_type	saveint, savepipe;

	(void)&saveint;
	(void)&savepipe;
	(void)&ok;
	pop3lock = 1;
	if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
		safe_signal(SIGINT, maincatch);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(pop3jmp, 1) == 0) {
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, pop3catch);
		ok = pop3_noop1(&mb);
	}
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	pop3lock = 0;
	return ok;
}

/*ARGSUSED*/
static void
pop3alarm(int s)
{
	sighandler_type	saveint;
	sighandler_type savepipe;
	(void)s;

	if (pop3lock++ == 0) {
		if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
			safe_signal(SIGINT, maincatch);
		savepipe = safe_signal(SIGPIPE, SIG_IGN);
		if (sigsetjmp(pop3jmp, 1)) {
			safe_signal(SIGINT, saveint);
			safe_signal(SIGPIPE, savepipe);
			goto brk;
		}
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, pop3catch);
		if (pop3_noop1(&mb) != OKAY) {
			safe_signal(SIGINT, saveint);
			safe_signal(SIGPIPE, savepipe);
			goto out;
		}
		safe_signal(SIGINT, saveint);
		safe_signal(SIGPIPE, savepipe);
	}
brk:	alarm(pop3keepalive);
out:	pop3lock--;
}

static enum okay
pop3_stat(struct mailbox *mp, off_t *size, int *count)
{
	char *cp;
	enum okay ok = OKAY;

	POP3_OUT("STAT\r\n", MB_COMD)
	POP3_ANSWER()
	for (cp = pop3buf; *cp && !spacechar(*cp & 0377); cp++);
	while (*cp && spacechar(*cp & 0377))
		cp++;
	if (*cp) {
		*count = (int)strtol(cp, NULL, 10);
		while (*cp && !spacechar(*cp & 0377))
			cp++;
		while (*cp && spacechar(*cp & 0377))
			cp++;
		if (*cp)
			*size = (int)strtol(cp, NULL, 10);
		else
			ok = STOP;
	} else
		ok = STOP;
	if (ok == STOP)
		fprintf(stderr, catgets(catd, CATSET, 260,
			"invalid POP3 STAT response: %s\n"), pop3buf);
	return ok;
}

static enum okay
pop3_list(struct mailbox *mp, int n, size_t *size)
{
	char o[LINESIZE], *cp;

	snprintf(o, sizeof o, "LIST %u\r\n", n);
	POP3_OUT(o, MB_COMD)
	POP3_ANSWER()
	for (cp = pop3buf; *cp && !spacechar(*cp & 0377); cp++);
	while (*cp && spacechar(*cp & 0377))
		cp++;
	while (*cp && !spacechar(*cp & 0377))
		cp++;
	while (*cp && spacechar(*cp & 0377))
		cp++;
	if (*cp)
		*size = (size_t)strtol(cp, NULL, 10);
	else
		*size = 0;
	return OKAY;
}

static void
pop3_init(struct mailbox *mp, int n)
{
	struct message *m = &message[n];
	char *cp;

	m->m_flag = MUSED|MNEW|MNOFROM|MNEWEST;
	m->m_block = 0;
	m->m_offset = 0;
	pop3_list(mp, m - message + 1, &m->m_xsize);
	if ((cp = hfield1("status", m)) != NULL) {
		while (*cp != '\0') {
			if (*cp == 'R')
				m->m_flag |= MREAD;
			else if (*cp == 'O')
				m->m_flag &= ~MNEW;
			cp++;
		}
	}
}

/*ARGSUSED*/
static void
pop3_dates(struct mailbox *mp)
{
	int	i;
	(void)mp;

	for (i = 0; i < msgCount; i++)
		substdate(&message[i]);
}

static void
pop3_setptr(struct mailbox *mp)
{
	int i;

	message = scalloc(msgCount + 1, sizeof *message);
	for (i = 0; i < msgCount; i++)
		pop3_init(mp, i);
	setdot(message);
	message[msgCount].m_size = 0;
	message[msgCount].m_lines = 0;
	pop3_dates(mp);
}

int
pop3_setfile(const char *server, int newmail, int isedit)
{
	struct sock	so;
	sighandler_type	saveint;
	sighandler_type savepipe;
	char *user, *pass;
	const char *cp, *uhp, *volatile sp = server;
	int use_ssl = 0;

	if (newmail)
		return 1;
	if (strncmp(sp, "pop3://", 7) == 0) {
		sp = &sp[7];
		use_ssl = 0;
#ifdef USE_SSL
	} else if (strncmp(sp, "pop3s://", 8) == 0) {
		sp = &sp[8];
		use_ssl = 1;
#endif
	}
	uhp = sp;
	pass = lookup_password_for_token(uhp);
	if ((cp = last_at_before_slash(sp)) != NULL) {
		user = salloc(cp - sp + 1);
		memcpy(user, sp, cp - sp);
		user[cp - sp] = '\0';
		sp = &cp[1];
		user = urlxdec(user);
	} else
		user = NULL;
	if (sopen(sp, &so, use_ssl, uhp, use_ssl ? "pop3s" : "pop3",
				(options & OPT_VERBOSE) != 0) != OKAY) {
		return -1;
	}
	quit();
	edit = (isedit != 0);
	if (mb.mb_sock.s_fd >= 0)
		sclose(&mb.mb_sock);
	if (mb.mb_itf) {
		fclose(mb.mb_itf);
		mb.mb_itf = NULL;
	}
	if (mb.mb_otf) {
		fclose(mb.mb_otf);
		mb.mb_otf = NULL;
	}
	initbox(server);
	mb.mb_type = MB_VOID;
	pop3lock = 1;
	mb.mb_sock = so;
	saveint = safe_signal(SIGINT, SIG_IGN);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(pop3jmp, 1)) {
		sclose(&mb.mb_sock);
		safe_signal(SIGINT, saveint);
		safe_signal(SIGPIPE, savepipe);
		pop3lock = 0;
		return 1;
	}
	if (saveint != SIG_IGN)
		safe_signal(SIGINT, pop3catch);
	if (savepipe != SIG_IGN)
		safe_signal(SIGPIPE, pop3catch);
	if ((cp = value("pop3-keepalive")) != NULL) {
		if ((pop3keepalive = strtol(cp, NULL, 10)) > 0) {
			savealrm = safe_signal(SIGALRM, pop3alarm);
			alarm(pop3keepalive);
		}
	}
	mb.mb_sock.s_desc = "POP3";
	mb.mb_sock.s_onclose = pop3_timer_off;
	if (_pop3_login(&mb, user, pass, uhp, sp) != OKAY ||
			pop3_stat(&mb, &mailsize, &msgCount) != OKAY) {
		sclose(&mb.mb_sock);
		pop3_timer_off();
		safe_signal(SIGINT, saveint);
		safe_signal(SIGPIPE, savepipe);
		pop3lock = 0;
		return 1;
	}
	mb.mb_type = MB_POP3;
	mb.mb_perm = (options & OPT_R_FLAG) ? 0 : MB_DELE;
	pop3_setptr(&mb);
	setmsize(msgCount);
	sawcom = FAL0;
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	pop3lock = 0;
	if (!edit && msgCount == 0) {
		if (mb.mb_type == MB_POP3 && value("emptystart") == NULL)
			fprintf(stderr, catgets(catd, CATSET, 258,
				"No mail at %s\n"), server);
		return 1;
	}
	return 0;
}

static enum okay
pop3_get(struct mailbox *mp, struct message *m, enum needspec volatile need)
{
	sighandler_type	volatile saveint = SIG_IGN, savepipe = SIG_IGN;
	off_t offset;
	char o[LINESIZE], *line = NULL, *lp;
	size_t linesize = 0, linelen, size;
	int number = m - message + 1, emptyline = 0, lines;

	(void)&saveint;
	(void)&savepipe;
	(void)&number;
	(void)&emptyline;
	(void)&need;
	if (mp->mb_sock.s_fd < 0) {
		fprintf(stderr, catgets(catd, CATSET, 219,
				"POP3 connection already closed.\n"));
		return STOP;
	}
	if (pop3lock++ == 0) {
		if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
			safe_signal(SIGINT, maincatch);
		savepipe = safe_signal(SIGPIPE, SIG_IGN);
		if (sigsetjmp(pop3jmp, 1)) {
			safe_signal(SIGINT, saveint);
			safe_signal(SIGPIPE, savepipe);
			pop3lock--;
			return STOP;
		}
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, pop3catch);
	}
	fseek(mp->mb_otf, 0L, SEEK_END);
	offset = ftell(mp->mb_otf);
retry:	switch (need) {
	case NEED_HEADER:
		snprintf(o, sizeof o, "TOP %u 0\r\n", number);
		break;
	case NEED_BODY:
		snprintf(o, sizeof o, "RETR %u\r\n", number);
		break;
	case NEED_UNSPEC:
		abort();
	}
	POP3_OUT(o, MB_COMD|MB_MULT)
	if (pop3_answer(mp) == STOP) {
		if (need == NEED_HEADER) {
			/*
			 * The TOP POP3 command is optional, so retry
			 * with the entire message.
			 */
			need = NEED_BODY;
			goto retry;
		}
		if (interrupts)
			onintr(0);
		return STOP;
	}
	size = 0;
	lines = 0;
	while (sgetline(&line, &linesize, &linelen, &mp->mb_sock) > 0) {
		if (line[0] == '.' && line[1] == '\r' && line[2] == '\n' &&
				line[3] == '\0') {
			mp->mb_active &= ~MB_MULT;
			break;
		}
		if (line[0] == '.') {
			lp = &line[1];
			linelen--;
		} else
			lp = line;
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
		 * TODO i have *never* seen the latter?!?!?
		 * TODO <<
		 */
		/*
		 * Since we simply copy over data without doing any transfer
		 * encoding reclassification/adjustment we *have* to perform
		 * RFC 4155 compliant From_ quoting here
		 */
		if (is_head(lp, linelen)) {
			if (lines == 0)
				continue;
			fputc('>', mp->mb_otf);
			++size;
		}
		lines++;
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
	}
	if (!emptyline) {
		/*
		 * This is very ugly; but some POP3 daemons don't end a
		 * message with \r\n\r\n, and we need \n\n for mbox format.
		 */
		fputc('\n', mp->mb_otf);
		lines++;
		size++;
	}
	m->m_size = size;
	m->m_lines = lines;
	m->m_block = mailx_blockof(offset);
	m->m_offset = mailx_offsetof(offset);
	fflush(mp->mb_otf);
	switch (need) {
	case NEED_HEADER:
		m->m_have |= HAVE_HEADER;
		break;
	case NEED_BODY:
		m->m_have |= HAVE_HEADER|HAVE_BODY;
		m->m_xlines = m->m_lines;
		m->m_xsize = m->m_size;
		break;
	case NEED_UNSPEC:
		break;
	}
	if (line)
		free(line);
	if (saveint != SIG_IGN)
		safe_signal(SIGINT, saveint);
	if (savepipe != SIG_IGN)
		safe_signal(SIGPIPE, savepipe);
	pop3lock--;
	if (interrupts)
		onintr(0);
	return OKAY;
}

enum okay
pop3_header(struct message *m)
{
	return pop3_get(&mb, m,
		boption("pop3-bulk-load") ? NEED_BODY : NEED_HEADER);
}

enum okay
pop3_body(struct message *m)
{
	return pop3_get(&mb, m, NEED_BODY);
}

static enum okay
pop3_exit(struct mailbox *mp)
{
	POP3_OUT("QUIT\r\n", MB_COMD)
	POP3_ANSWER()
	return OKAY;
}

static enum okay
pop3_delete(struct mailbox *mp, int n)
{
	char o[LINESIZE];

	snprintf(o, sizeof o, "DELE %u\r\n", n);
	POP3_OUT(o, MB_COMD)
	POP3_ANSWER()
	return OKAY;
}

static enum okay
pop3_update(struct mailbox *mp)
{
	struct message *m;
	int dodel, c, gotcha, held;

	if (!edit) {
		holdbits();
		for (m = &message[0], c = 0; m < &message[msgCount]; m++) {
			if (m->m_flag & MBOX)
				c++;
		}
		if (c > 0)
			makembox();
	}
	for (m = &message[0], gotcha=0, held=0; m < &message[msgCount]; m++) {
		if (edit) {
			dodel = m->m_flag & MDELETED;
		} else {
			dodel = !((m->m_flag&MPRESERVE) ||
					(m->m_flag&MTOUCH) == 0);
		}
		if (dodel) {
			pop3_delete(mp, m - message + 1);
			gotcha++;
		} else
			held++;
	}
	if (gotcha && edit) {
		printf(tr(168, "\"%s\" "), displayname);
		printf(value("bsdcompat") || value("bsdmsgs") ?
				catgets(catd, CATSET, 170, "complete\n") :
				catgets(catd, CATSET, 212, "updated.\n"));
	} else if (held && !edit) {
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
pop3_quit(void)
{
	sighandler_type	saveint;
	sighandler_type savepipe;

	if (mb.mb_sock.s_fd < 0) {
		fprintf(stderr, catgets(catd, CATSET, 219,
				"POP3 connection already closed.\n"));
		return;
	}
	pop3lock = 1;
	saveint = safe_signal(SIGINT, SIG_IGN);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(pop3jmp, 1)) {
		safe_signal(SIGINT, saveint);
		safe_signal(SIGPIPE, saveint);
		pop3lock = 0;
		return;
	}
	if (saveint != SIG_IGN)
		safe_signal(SIGINT, pop3catch);
	if (savepipe != SIG_IGN)
		safe_signal(SIGPIPE, pop3catch);
	pop3_update(&mb);
	pop3_exit(&mb);
	sclose(&mb.mb_sock);
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	pop3lock = 0;
}
#endif /* USE_POP3 */
