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
static char sccsid[] = "@(#)smtp.c	1.2 (gritter) 9/29/00";
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
 * This is fputs with conversion to \r\n format.
 * Note that the string's terminating \0 may be overwritten.
 */
int
crlfputs(s, stream)
char *s;
FILE *stream;
{
	size_t l;

	l = strlen(s);

	if (*(s + l - 1) == '\n' && *(s + l - 2) != '\r') {
		*(s + l - 1) = '\r';
		*(s + l) = '\n';
		l = fwrite(s, sizeof(char), l + 1, stream);
	} else {
		l = fwrite(s, sizeof(char), l, stream);
	}
	if (l <= 0)
		return EOF;
	return l;
}

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
        struct hostent *hent;
#endif

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
		hent = gethostbyname(hn);
		if (hent != NULL) {
			hn = hent->h_name;
		}
#endif
		hostname = (char*)smalloc(strlen(hn) + 1);
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

	cp = value("from");
	if (cp != NULL)
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
		addr = (char*)smalloc(sz);
		snprintf(addr, sz, "%s@%s", myname, hn);
	}
	return addr;
}

#ifdef	HAVE_SOCKETS

/*
 * Get the SMTP server's answer, expecting value.
 */
static int
read_smtp(f, value)
FILE *f;
{
	int oldfl, ret = 5;
	char b[LINESIZE];

	if (fgets(b, LINESIZE, f) != NULL) {
		if (verbose)
			fputs(b, stderr);
		switch (*b) {
		case '1': ret = 1; break;
		case '2': ret = 2; break;
		case '3': ret = 3; break;
		case '4': ret = 4; break;
		default: ret = 5;
		}
		if (value != ret) {
			fprintf(stderr, "smtp-server: %s", b + 4);
		}
		/*
		 * Maybe the server has said too much.
		 */
		oldfl = fcntl(fileno(f), F_GETFL);
		fcntl(fileno(f), F_SETFL, oldfl | O_NONBLOCK);
		while (fgets(b, LINESIZE, f) != NULL);
		fcntl(fileno(f), F_SETFL, oldfl);
	} else if (ferror(f)) {
		perror("smtp-read");
		return 5;
	}
	return ret;
}

/*
 * Macros for talk_smtp.
 */
#define	SMTP_ANSWER(x)	fflush(fsi); \
			if (ferror(fsi)) { \
				perror("smtp-write"); \
				return 1; \
			} \
			if (read_smtp(fso, x) != (x)) { \
				fputs("QUIT\r\n", fsi); \
				return 1; \
			}

#define	SMTP_OUT(x)	fputs(x, fsi); \
			if (verbose) \
				fprintf(stderr, ">>> %s", x);

/*
 * Talk to a SMTP server.
 */
static int
talk_smtp(to, fi, fsi, fso)
struct name *to;
FILE *fi, *fsi, *fso;
{
	struct name *n;
	char b[LINESIZE], o[LINESIZE];

	SMTP_ANSWER(2);
	snprintf(o, LINESIZE, "HELO %s\r\n", nodename());
	SMTP_OUT(o);
	SMTP_ANSWER(2);
	snprintf(o, LINESIZE, "MAIL FROM: <%s>\r\n", skin(myaddr()));
	SMTP_OUT(o);
	SMTP_ANSWER(2);
	for (n = to; n != NULL; n = n->n_flink) {
		snprintf(o, LINESIZE, "RCPT TO: <%s>\r\n", n->n_name);
		SMTP_OUT(o);
		SMTP_ANSWER(2);
	}
	SMTP_OUT("DATA\r\n");
	SMTP_ANSWER(3);
	while (fgets(b, LINESIZE, fi) != NULL) {
		if (*b == '.')
			fputc('.', fsi);
		crlfputs(b, fsi);
	}
	if (*(b + strlen(b) - 1) != '\n')
		fputs("\r\n", fsi);
	SMTP_OUT(".\r\n");
	SMTP_ANSWER(2);
	SMTP_OUT("QUIT\r\n");
	SMTP_ANSWER(2);
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
	struct sockaddr_in servaddr;
	struct in_addr **pptr;
	struct hostent *hp;
	struct servent *sp;
	FILE *fsi, *fso;
	int ret;
	unsigned short port = 0;
	char *portstr;

	if (value("verbose") != NULL)
		verbose = 1;
	else
		verbose = 0;
	portstr = strchr(server, ':');
	if (portstr == NULL)
		portstr = "smtp";
	else {
		*portstr++ = '\0';
		if (*portstr == '\0')
			portstr = "smtp";
		else {
			port = (unsigned short)strtol(portstr, NULL, 10);
		}
	}
	if (port == 0) {
		sp = getservbyname(portstr, "tcp");
		if (sp == NULL) {
			perror("getservbyname");
			return 1;
		}
		port = sp->s_port;
	} else {
		port = htons(port);
	}
	hp = gethostbyname(server);
	if (hp == NULL) {
		perror("gethostbyname");
		return 1;
	}
	pptr = (struct in_addr **) hp->h_addr_list;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		perror("socket");
		return 1;
	}
	memset(&servaddr, 0, sizeof servaddr);
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = port;
	memcpy(&servaddr.sin_addr, *pptr, sizeof(struct in_addr));
	if (verbose)
		fprintf(stderr, "Connecting to %s . . .", inet_ntoa(**pptr));
	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof servaddr)
			!= 0) {
		perror("connect");
		return 1;
	}
	if (verbose)
		fputs(" connected.\n", stderr);
	fsi = (FILE*)Fdopen(sockfd, "w");
	fso = (FILE*)Fdopen(sockfd, "r");
	ret = talk_smtp(to, fi, fsi, fso);
	Fclose(fsi);
	Fclose(fso);
	return ret;
}
#else	/* !HAVE_SOCKETS */
int
smtp_mta(server, to, fi)
char *server;
struct name *to;
FILE *fi;
{
	fputs("No SMTP support compiled in.\n", stderr);
	return 1;
}
#endif	/* !HAVE_SOCKETS */
