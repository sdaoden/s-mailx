/*
 * Nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2002 Gunnar Ritter, Freiburg i. Br., Germany.
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

#ifndef lint
#ifdef	DOSCCS
static char sccsid[] = "@(#)smtp.c	2.15 (gritter) 7/29/04";
#endif
#endif /* not lint */

#include "rcv.h"
#include "extern.h"

#include <sys/utsname.h>
#ifdef	HAVE_SOCKETS
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#ifdef	HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif	/* HAVE_ARPA_INET_H */
#endif	/* HAVE_SOCKETS */
#include <unistd.h>

/*
 * Mail -- a mail program
 *
 * SMTP client and other internet related functions.
 */

#ifdef	HAVE_SOCKETS
static int verbose;
#endif

/*
 * Return our hostname.
 */
char *
nodename()
{
	static char *hostname;
	char *hn;
        struct utsname ut;
#ifdef	HAVE_SOCKETS
#ifdef	HAVE_IPv6_FUNCS
	struct addrinfo hints, *res;
#else	/* !HAVE_IPv6_FUNCS */
        struct hostent *hent;
#endif	/* !HAVE_IPv6_FUNCS */
#endif	/* HAVE_SOCKETS */

	if ((hn = value("hostname")) != NULL && *hn)
		hostname = sstrdup(hn);
	if (hostname == NULL) {
		uname(&ut);
		hn = ut.nodename;
#ifdef	HAVE_SOCKETS
#ifdef	HAVE_IPv6_FUNCS
		memset(&hints, 0, sizeof hints);
		hints.ai_socktype = SOCK_DGRAM;	/* dummy */
		hints.ai_flags = AI_CANONNAME;
		if (getaddrinfo(hn, "0", &hints, &res) == 0) {
			if (res->ai_canonname) {
				hn = salloc(strlen(res->ai_canonname) + 1);
				strcpy(hn, res->ai_canonname);
			}
			freeaddrinfo(res);
		}
#else	/* !HAVE_IPv6_FUNCS */
		hent = gethostbyname(hn);
		if (hent != NULL) {
			hn = hent->h_name;
		}
#endif	/* !HAVE_IPv6_FUNCS */
#endif	/* HAVE_SOCKETS */
		hostname = (char *)smalloc(strlen(hn) + 1);
		strcpy(hostname, hn);
	}
	return hostname;
}

/*
 * Return the user's From: address.
 */
char *
myaddr()
{
	char *cp, *hn;
	static char *addr;
	size_t sz;

	if ((cp = value("from")) != NULL)
		return cp;
	/*
	 * When invoking sendmail directly, it's its task
	 * to generate a From: address.
	 */
	if (value("smtp") == NULL)
		return NULL;
	if (addr == NULL) {
		hn = nodename();
		sz = strlen(myname) + strlen(hn) + 2;
		addr = (char *)smalloc(sz);
		snprintf(addr, sz, "%s@%s", myname, hn);
	}
	return addr;
}

#ifdef	HAVE_SOCKETS

static char *
auth_var(type, addr)
	const char *type, *addr;
{
	char	*var, *cp;
	int	len;

	var = ac_alloc(len = strlen(type) + strlen(addr) + 7);
	snprintf(var, len, "smtp-auth-%s-%s", type, addr);
	if ((cp = value(var)) != NULL)
		cp = savestr(cp);
	else {
		snprintf(var, len, "smtp-auth-%s", type);
		if ((cp = value(var)) != NULL)
			cp = savestr(cp);
	}
	ac_free(var);
	return cp;
}

static char	*smtpbuf;
static size_t	smtpbufsize;

/*
 * Get the SMTP server's answer, expecting value.
 */
static int
read_smtp(sp, value)
struct sock *sp;
int value;
{
	int ret;
	int len;

	do {
		if ((len = sgetline(&smtpbuf, &smtpbufsize, NULL, sp)) < 6) {
			if (len >= 0)
				fprintf(stderr, catgets(catd, CATSET, 241,
					"Unexpected EOF on SMTP connection\n"));
			return 5;
		}
		if (verbose)
			fputs(smtpbuf, stderr);
		switch (*smtpbuf) {
		case '1': ret = 1; break;
		case '2': ret = 2; break;
		case '3': ret = 3; break;
		case '4': ret = 4; break;
		default: ret = 5;
		}
		if (value != ret)
			fprintf(stderr, catgets(catd, CATSET, 191,
					"smtp-server: %s"), smtpbuf);
	} while (smtpbuf[3] == '-');
	return ret;
}

/*
 * Macros for talk_smtp.
 */
#define	SMTP_ANSWER(x)	if (read_smtp(sp, x) != (x)) { \
				swrite(sp, "QUIT\r\n"); \
				if (b != NULL) \
					free(b); \
				return 1; \
			}

#define	SMTP_OUT(x)	if (verbose) \
				fprintf(stderr, ">>> %s", x); \
			swrite(sp, x);

/*
 * Talk to a SMTP server.
 */
static int
talk_smtp(to, fi, sp, server, uhp)
struct name *to;
FILE *fi;
struct sock *sp;
char *server, *uhp;
{
	struct name *n;
	char *b = NULL, o[LINESIZE];
	size_t blen, bsize = 0, count;
	char	*user, *password, *b64, *skinned;

	skinned = skin(myaddr());
	SMTP_ANSWER(2);
	if (value("smtp-use-tls")) {
		snprintf(o, sizeof o, "EHLO %s\r\n", nodename());
		SMTP_OUT(o);
		SMTP_ANSWER(2);
		SMTP_OUT("STARTTLS\r\n");
		SMTP_ANSWER(2);
		if (ssl_open(server, sp, uhp) != OKAY)
			return 1;
	}
	user = auth_var("user", skinned);
	password = auth_var("password", skinned);
	if (user && password) {
		snprintf(o, sizeof o, "EHLO %s\r\n", nodename());
		SMTP_OUT(o);
		SMTP_ANSWER(2);
		snprintf(o, sizeof o, "AUTH LOGIN\r\n");
		SMTP_OUT(o);
		SMTP_ANSWER(3);
		b64 = strtob64(user);
		snprintf(o, sizeof o, "%s\r\n", b64);
		free(b64);
		SMTP_OUT(o);
		SMTP_ANSWER(3);
		b64 = strtob64(password);
		snprintf(o, sizeof o, "%s\r\n", b64);
		free(b64);
		SMTP_OUT(o);
		SMTP_ANSWER(2);
	} else {
		snprintf(o, sizeof o, "HELO %s\r\n", nodename());
		SMTP_OUT(o);
		SMTP_ANSWER(2);
	}
	snprintf(o, sizeof o, "MAIL FROM: <%s>\r\n", skinned);
	SMTP_OUT(o);
	SMTP_ANSWER(2);
	for (n = to; n != NULL; n = n->n_flink) {
		if ((n->n_type & GDEL) == 0) {
			snprintf(o, sizeof o, "RCPT TO: <%s>\r\n", n->n_name);
			SMTP_OUT(o);
			SMTP_ANSWER(2);
		}
	}
	SMTP_OUT("DATA\r\n");
	SMTP_ANSWER(3);
	fflush(fi);
	rewind(fi);
	count = fsize(fi);
	while (fgetline(&b, &bsize, &count, &blen, fi, 1) != NULL) {
		if (*b == '.')
			swrite1(sp, ".", 1, 1);
		b[blen-1] = '\r';
		b[blen] = '\n';
		swrite1(sp, b, blen+1, 1);
	}
	SMTP_OUT(".\r\n");
	SMTP_ANSWER(2);
	SMTP_OUT("QUIT\r\n");
	SMTP_ANSWER(2);
	if (b != NULL)
		free(b);
	return 0;
}

/*
 * Connect to a SMTP server.
 */
int
smtp_mta(server, to, fi)
char *server;
struct name *to;
FILE *fi;
{
	struct sock	so;
	int	use_ssl, ret;

	memset(&so, 0, sizeof so);
	verbose = value("verbose") != NULL;
	if (strncmp(server, "smtp://", 7) == 0) {
		use_ssl = 0;
		server += 7;
#ifdef	USE_SSL
	} else if (strncmp(server, "smtps://", 8) == 0) {
		use_ssl = 1;
		server += 8;
#endif
	} else
		use_ssl = 0;
	if (sopen(server, &so, use_ssl, server, use_ssl ? "smtps" : "smtp",
				verbose) != OKAY)
		return 1;
	ret = talk_smtp(to, fi, &so, server, server);
	sclose(&so);
	if (smtpbuf) {
		free(smtpbuf);
		smtpbuf = NULL;
		smtpbufsize = 0;
	}
	return ret;
}
#else	/* !HAVE_SOCKETS */
int
smtp_mta(server, to, fi)
char *server;
struct name *to;
FILE *fi;
{
	fputs(catgets(catd, CATSET, 194,
			"No SMTP support compiled in.\n"), stderr);
	return 1;
}
#endif	/* !HAVE_SOCKETS */
