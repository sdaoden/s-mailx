/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ SMTP client and other internet related functions.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 2000
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

EMPTY_FILE(smtp)
#ifdef HAVE_SMTP
#ifdef HAVE_SOCKETS
# include <sys/socket.h>

# include <netdb.h>

# include <netinet/in.h>

# ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
# endif
#endif

#ifdef HAVE_MD5
# include "md5.h"
#endif

static char		*smtpbuf;
static size_t		smtpbufsize;
static sigjmp_buf	smtpjmp;

static void	onterm(int signo);
static int	read_smtp(struct sock *sp, int val, int ign_eof);
static int	talk_smtp(struct name *to, FILE *fi, struct sock *sp,
			char *server, char *uhp, struct header *hp,
			const char *user, const char *password,
			const char *skinned);

static void
onterm(int signo)
{
	(void)signo;
	siglongjmp(smtpjmp, 1);
}

/*
 * Get the SMTP server's answer, expecting val.
 */
static int
read_smtp(struct sock *sp, int val, int ign_eof)
{
	int ret;
	int len;

	do {
		if ((len = sgetline(&smtpbuf, &smtpbufsize, NULL, sp)) < 6) {
			if (len >= 0 && !ign_eof)
				fprintf(stderr, tr(241,
					"Unexpected EOF on SMTP connection\n"));
			return -1;
		}
		if (options & OPT_VERBOSE)
			fputs(smtpbuf, stderr);
		switch (*smtpbuf) {
		case '1': ret = 1; break;
		case '2': ret = 2; break;
		case '3': ret = 3; break;
		case '4': ret = 4; break;
		default: ret = 5;
		}
		if (val != ret)
			fprintf(stderr, tr(191, "smtp-server: %s"), smtpbuf);
	} while (smtpbuf[3] == '-');
	return ret;
}

/*
 * Macros for talk_smtp.
 */
#define	_SMTP_ANSWER(x, ign_eof)	\
			if ((options & OPT_DEBUG) == 0) { \
				int	y; \
				if ((y = read_smtp(sp, x, ign_eof)) != (x) && \
					(!(ign_eof) || y != -1)) { \
					if (b != NULL) \
						free(b); \
					return 1; \
				} \
			}

#define	SMTP_ANSWER(x)	_SMTP_ANSWER(x, 0)

#define	SMTP_OUT(x)	if (options & OPT_VERBOSE) \
				fprintf(stderr, ">>> %s", x); \
			if ((options & OPT_DEBUG) == 0) \
				swrite(sp, x);

/*
 * Talk to a SMTP server.
 */
static int
talk_smtp(struct name *to, FILE *fi, struct sock *sp,
		char *xserver, char *uhp, struct header *hp,
		const char *user, const char *password, const char *skinned)
{
	char o[LINESIZE], *authstr, *cp, *b = NULL;
	struct str b64;
	struct name *n;
	size_t blen, cnt, bsize = 0;
	enum { AUTH_NONE, AUTH_PLAIN, AUTH_LOGIN, AUTH_CRAM_MD5 } auth;
	int inhdr = 1, inbcc = 0;
	(void)hp;
	(void)xserver;
	(void)uhp;

	if ((authstr = smtp_auth_var("", skinned)) == NULL)
		auth = user && password ? AUTH_LOGIN : AUTH_NONE;
	else if (strcmp(authstr, "plain") == 0)
		auth = AUTH_PLAIN;
	else if (strcmp(authstr, "login") == 0)
		auth = AUTH_LOGIN;
	else if (strcmp(authstr, "cram-md5") == 0) {
#ifdef HAVE_MD5
		auth = AUTH_CRAM_MD5;
#else
		fprintf(stderr, tr(277, "No CRAM-MD5 support compiled in.\n"));
		return (1);
#endif
	} else {
		fprintf(stderr, tr(274,
			"Unknown SMTP authentication method: %s\n"), authstr);
		return 1;
	}
	if (auth != AUTH_NONE && (user == NULL || password == NULL)) {
		fprintf(stderr, tr(275,
			"User and password are necessary "
			"for SMTP authentication.\n"));
		return 1;
	}
	SMTP_ANSWER(2);
#ifdef HAVE_SSL
	if (! sp->s_use_ssl && value("smtp-use-starttls")) {
		char *server;
		if ((cp = strchr(xserver, ':')) != NULL) {
			server = salloc(cp - xserver + 1);
			memcpy(server, xserver, cp - xserver);
			server[cp - xserver] = '\0';
		} else
			server = xserver;
		snprintf(o, sizeof o, "EHLO %s\r\n", nodename(1));
		SMTP_OUT(o);
		SMTP_ANSWER(2);
		SMTP_OUT("STARTTLS\r\n");
		SMTP_ANSWER(2);
		if ((options & OPT_DEBUG) == 0 &&
				ssl_open(server, sp, uhp) != OKAY)
			return 1;
	}
#else
	if (value("smtp-use-starttls")) {
		fprintf(stderr, tr(225, "No SSL support compiled in.\n"));
		return 1;
	}
#endif
	if (auth != AUTH_NONE) {
		snprintf(o, sizeof o, "EHLO %s\r\n", nodename(1));
		SMTP_OUT(o);
		SMTP_ANSWER(2);
		switch (auth) {
		case AUTH_NONE:
#ifndef HAVE_MD5
		case AUTH_CRAM_MD5:
#endif
			/* FALLTRHU
			 * Won't happen, but gcc(1) and clang(1) whine without
			 * and Coverity whines with; that's a hard one.. */
		case AUTH_LOGIN:
			SMTP_OUT("AUTH LOGIN\r\n");
			SMTP_ANSWER(3);
			(void)b64_encode_cp(&b64, user, B64_SALLOC|B64_CRLF);
			SMTP_OUT(b64.s);
			SMTP_ANSWER(3);
			(void)b64_encode_cp(&b64, password,
				B64_SALLOC|B64_CRLF);
			SMTP_OUT(b64.s);
			SMTP_ANSWER(2);
			break;
		case AUTH_PLAIN:
			SMTP_OUT("AUTH PLAIN\r\n");
			SMTP_ANSWER(3);
			(void)snprintf(o, sizeof o, "%c%s%c%s",
				'\0', user, '\0', password);
			(void)b64_encode_buf(&b64, o, strlen(user) +
				strlen(password) + 2, B64_SALLOC|B64_CRLF);
			SMTP_OUT(b64.s);
			SMTP_ANSWER(2);
			break;
#ifdef HAVE_MD5
		case AUTH_CRAM_MD5:
			SMTP_OUT("AUTH CRAM-MD5\r\n");
			SMTP_ANSWER(3);
			for (cp = smtpbuf; digitchar(*cp); ++cp)
				;
			while (blankchar(*cp))
				++cp;
			cp = cram_md5_string(user, password, cp);
			SMTP_OUT(cp);
			SMTP_ANSWER(2);
			break;
#endif
		}
	} else {
		snprintf(o, sizeof o, "HELO %s\r\n", nodename(1));
		SMTP_OUT(o);
		SMTP_ANSWER(2);
	}
	snprintf(o, sizeof o, "MAIL FROM:<%s>\r\n", skinned);
	SMTP_OUT(o);
	SMTP_ANSWER(2);
	for (n = to; n != NULL; n = n->n_flink) {
		if ((n->n_type & GDEL) == 0) {
			snprintf(o, sizeof o, "RCPT TO:<%s>\r\n",
				skinned_name(n));
			SMTP_OUT(o);
			SMTP_ANSWER(2);
		}
	}
	SMTP_OUT("DATA\r\n");
	SMTP_ANSWER(3);
	fflush(fi);
	rewind(fi);
	cnt = fsize(fi);
	while (fgetline(&b, &bsize, &cnt, &blen, fi, 1) != NULL) {
		if (inhdr) {
			if (*b == '\n') {
				inhdr = 0;
				inbcc = 0;
			} else if (inbcc && blankchar(*b & 0377))
				continue;
			/*
			 * We know what we have generated first, so
			 * do not look for whitespace before the ':'.
			 */
			else if (ascncasecmp(b, "bcc: ", 5) == 0) {
				inbcc = 1;
				continue;
			} else
				inbcc = 0;
		}
		if (*b == '.') {
			if (options & OPT_DEBUG)
				putc('.', stderr);
			else
				swrite1(sp, ".", 1, 1);
		}
		if (options & OPT_DEBUG) {
			fprintf(stderr, ">>> %s", b);
			continue;
		}
		b[blen-1] = '\r';
		b[blen] = '\n';
		swrite1(sp, b, blen+1, 1);
	}
	SMTP_OUT(".\r\n");
	SMTP_ANSWER(2);
	SMTP_OUT("QUIT\r\n");
	_SMTP_ANSWER(2, 1);
	if (b != NULL)
		free(b);
	return 0;
}

FL char *
smtp_auth_var(char const *atype, char const *addr)
{
	size_t tl, al, len;
	char *var, *cp;

	tl = strlen(atype);
	al = strlen(addr);
	len = tl + al + 10 + 1;
	var = ac_alloc(len);

	/* Try a 'user@host', i.e., address specific version first */
	(void)snprintf(var, len, "smtp-auth%s-%s", atype, addr);
	if ((cp = value(var)) == NULL) {
		snprintf(var, len, "smtp-auth%s", atype);
		cp = value(var);
	}
	if (cp != NULL)
		cp = savestr(cp);

	ac_free(var);
	return cp;
}

/*
 * Connect to a SMTP server.
 */
FL int
smtp_mta(char *volatile server, struct name *volatile to, FILE *fi,
		struct header *hp, const char *user, const char *password,
		const char *skinned)
{
	struct sock	so;
	int	use_ssl, ret;
	sighandler_type	saveterm;

	memset(&so, 0, sizeof so);
	saveterm = safe_signal(SIGTERM, SIG_IGN);
	if (sigsetjmp(smtpjmp, 1)) {
		safe_signal(SIGTERM, saveterm);
		return 1;
	}
	if (saveterm != SIG_IGN)
		safe_signal(SIGTERM, onterm);
	if (strncmp(server, "smtp://", 7) == 0) {
		use_ssl = 0;
		server += 7;
#ifdef HAVE_SSL
	} else if (strncmp(server, "smtps://", 8) == 0) {
		use_ssl = 1;
		server += 8;
#endif
	} else
		use_ssl = 0;
	if ((options & OPT_DEBUG) == 0 && sopen(server, &so, use_ssl, server,
			use_ssl ? "smtps" : "smtp",
			(options & OPT_VERBOSE) != 0) != OKAY) {
		safe_signal(SIGTERM, saveterm);
		return 1;
	}
	so.s_desc = "SMTP";
	ret = talk_smtp(to, fi, &so, server, server, hp,
			user, password, skinned);
	if ((options & OPT_DEBUG) == 0)
		sclose(&so);
	if (smtpbuf) {
		free(smtpbuf);
		smtpbuf = NULL;
		smtpbufsize = 0;
	}
	safe_signal(SIGTERM, saveterm);
	return ret;
}
#endif /* HAVE_SMTP */
