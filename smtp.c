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
static char sccsid[] = "@(#)smtp.c	2.8 (gritter) 1/8/04";
#endif
#endif /* not lint */

#include "rcv.h"
#include "extern.h"

#if defined (HAVE_UNAME)
#include <sys/utsname.h>
#endif

/*
 * Mail -- a mail program
 *
 * SMTP client and other internet related functions.
 */

static int verbose;

/*
 * Return our hostname.
 */
char *
nodename()
{
	static char *hostname;
	char *hn;
#if defined (HAVE_UNAME)
        struct utsname ut;
#elif defined (HAVE_GETHOSTNAME)
        char host__name[MAXHOSTNAMELEN];
#endif
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
#if defined (HAVE_UNAME)
		uname(&ut);
		hn = ut.nodename;
#elif defined (HAVE_GETHOSTNAME)
		gethostname(host__name, MAXHOSTNAMELEN);
		hn = host__name;
#else
		hn = "unknown";
#endif
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

static char	*smtpbuf;
static size_t	smtpbufsize;

/*
 * Get the SMTP server's answer, expecting value.
 */
static int
read_smtp(f, value)
FILE *f;
{
	int ret;
	size_t len;

	do {
		if (fgetline(&smtpbuf, &smtpbufsize, NULL, &len, f, 0) == NULL
				|| len < 6) {
			if (ferror(f))
				perror(catgets(catd, CATSET, 255,
						"SMTP read failed"));
			else
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
#define	SMTP_ANSWER(x)	if (ferror(fsi)) { \
				perror(catgets(catd, CATSET, 256, \
					"SMTP write error")); \
				if (b != NULL) \
					free(b); \
				return 1; \
			} \
			if (read_smtp(fso, x) != (x)) { \
				fputs("QUIT\r\n", fsi); \
				if (b != NULL) \
					free(b); \
				return 1; \
			}

#define	SMTP_OUT(x)	if (verbose) \
				fprintf(stderr, ">>> %s", x); \
			fputs(x, fsi); \
			fflush(fsi);

/*
 * Talk to a SMTP server.
 */
static int
talk_smtp(to, fi, fsi, fso)
struct name *to;
FILE *fi, *fsi, *fso;
{
	struct name *n;
	char *b = NULL, o[LINESIZE];
	size_t blen, bsize = 0, count;

	SMTP_ANSWER(2);
	snprintf(o, sizeof o, "HELO %s\r\n", nodename());
	SMTP_OUT(o);
	SMTP_ANSWER(2);
	snprintf(o, sizeof o, "MAIL FROM: <%s>\r\n", skin(myaddr()));
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
			sputc('.', fsi);
		crlfputs(b, fsi);
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
	int sockfd;
#ifdef	HAVE_IPv6_FUNCS
	struct addrinfo hints, *res, *res0;
	char hbuf[NI_MAXHOST];
#else	/* !HAVE_IPv6_FUNCS */
	struct sockaddr_in servaddr;
	struct in_addr **pptr;
	struct hostent *hp;
	struct servent *sp;
	unsigned short port = 0;
#endif	/* !HAVE_IPv6_FUNCS */
	FILE *fsi, *fso;
	int ret;
	char *portstr;

	verbose = value("verbose") != NULL;
	portstr = strchr(server, ':');
	if (portstr == NULL)
		portstr = "smtp";
	else {
		*portstr++ = '\0';
		if (*portstr == '\0')
			portstr = "smtp";
#ifndef	HAVE_IPv6_FUNCS
		else
			port = (unsigned short)strtol(portstr, NULL, 10);
#endif
	}
#ifdef	HAVE_IPv6_FUNCS
	memset(&hints, 0, sizeof hints);
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(server, portstr, &hints, &res0) != 0) {
		fprintf(stderr, catgets(catd, CATSET, 252,
				"could not resolve host: %s\n"));
		return 1;
	}
	sockfd = -1;
	for (res = res0; res != NULL && sockfd < 0; res = res->ai_next) {
		if (verbose) {
			if (getnameinfo(res->ai_addr, res->ai_addrlen,
						hbuf, sizeof hbuf, NULL, 0,
						NI_NUMERICHOST) != 0)
				strcpy(hbuf, "unknown host");
			fprintf(stderr, catgets(catd, CATSET, 192,
					"Connecting to %s . . ."), hbuf);
		}
		if ((sockfd = socket(res->ai_family, res->ai_socktype,
						res->ai_protocol)) >= 0) {
			if (connect(sockfd, res->ai_addr, res->ai_addrlen)!=0) {
				close(sockfd);
				sockfd = -1;
			}
		}
	}
	if (sockfd < 0) {
		freeaddrinfo(res0);
		perror(catgets(catd, CATSET, 254, "could not connect"));
		return 1;
	}
	freeaddrinfo(res0);
#else	/* !HAVE_IPv6_FUNCS */
	if (port == 0) {
		if ((sp = getservbyname(portstr, "tcp")) == NULL) {
			if (equal(portstr, "smtp"))
				port = htons(25);
			else {
				fprintf(stderr, catgets(catd, CATSET, 251,
					"unknown service: %s\n"), portstr);
				return 1;
			}
		}
		port = sp->s_port;
	} else
		port = htons(port);
	if ((hp = gethostbyname(server)) == NULL) {
		fprintf(stderr, catgets(catd, CATSET, 252,
				"could not resolve host: %s\n"));
		return 1;
	}
	pptr = (struct in_addr **) hp->h_addr_list;
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror(catgets(catd, CATSET, 253, "could not create socket"));
		return 1;
	}
	memset(&servaddr, 0, sizeof servaddr);
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = port;
	memcpy(&servaddr.sin_addr, *pptr, sizeof(struct in_addr));
	if (verbose)
		fprintf(stderr, catgets(catd, CATSET, 192,
				"Connecting to %s . . ."), inet_ntoa(**pptr));
	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof servaddr)
			!= 0) {
		perror(catgets(catd, CATSET, 254, "could not connect"));
		return 1;
	}
#endif	/* !HAVE_IPv6_FUNCS */
	if (verbose)
		fputs(catgets(catd, CATSET, 193, " connected.\n"), stderr);
	fsi = (FILE *)Fdopen(sockfd, "w");
	fso = (FILE *)Fdopen(sockfd, "r");
	ret = talk_smtp(to, fi, fsi, fso);
	Fclose(fsi);
	Fclose(fso);
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
