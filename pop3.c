/*
 * Nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2002 Gunnar Ritter, Freiburg i. Br., Germany.
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

#ifndef lint
#ifdef	DOSCCS
static char sccsid[] = "@(#)pop3.c	2.2 (gritter) 11/24/02";
#endif
#endif /* not lint */

#include "config.h"

#ifdef	USE_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/x509.h>
#include <openssl/rand.h>
#endif	/* USE_SSL */

#include "rcv.h"
#include "extern.h"
#include <errno.h>

/*
 * Mail -- a mail program
 *
 * POP3 client.
 */

#ifdef	HAVE_SOCKETS
static int	verbose;

#define	POP3_ANSWER()	if (pop3_answer(mp) == STOP) \
				return STOP;
#define	POP3_OUT(x, y)	if (pop3_finish(mp) == STOP) \
				return STOP; \
			if (verbose) \
				fputs(x, stderr); \
			mp->mb_active |= (y); \
			if (pop3_write(mp, x) == STOP) \
				return STOP;

static char	*pop3buf;
static size_t	pop3bufsize;
static sigjmp_buf	pop3jmp;
static sighandler_type savealrm;
static int	reset_tio;
static struct termios	otio;
static int	pop3keepalive;
static volatile int	pop3lock;

static void pop3_timer_off __P((void));
static int pop3_close __P((struct mailbox *));
static ssize_t xwrite __P((int, const char *, size_t));
static enum okay pop3_write __P((struct mailbox *, char *));
static int pop3_getline __P((char **, size_t *, size_t *, struct mailbox *));
static enum okay pop3_answer __P((struct mailbox *));
static enum okay pop3_finish __P((struct mailbox *));
static void pop3catch __P((int));
static enum okay pop3_noop __P((struct mailbox *));
static void pop3alarm __P((int));
static enum okay pop3_open __P((const char *, struct mailbox *, int,
			const char *));
static enum okay pop3_pass __P((struct mailbox *, const char *));
static enum okay pop3_user __P((struct mailbox *, char *, const char *));
static enum okay pop3_stat __P((struct mailbox *, off_t *, int *));
static enum okay pop3_list __P((struct mailbox *, int, size_t *));
static void pop3_init __P((struct mailbox *, int));
static void pop3_dates __P ((struct mailbox *));
static void pop3_setptr __P((struct mailbox *));
static char *pop3_have_password __P((const char *));
static enum okay pop3_get __P((struct mailbox *, struct message *,
			enum needspec));
static enum okay pop3_exit __P((struct mailbox *));
static enum okay pop3_delete __P((struct mailbox *, int));
static enum okay pop3_update __P((struct mailbox *));

#ifdef	USE_SSL
/*
 * OpenSSL client implementation according to: John Viega, Matt Messier,
 * Pravir Chandra: Network Security with OpenSSL. Sebastopol, CA 2002.
 */

enum {
	VRFY_IGNORE,
	VRFY_WARN,
	VRFY_ASK,
	VRFY_STRICT
} ssl_vrfy_level;

static void ssl_set_vrfy_level __P((void));
static enum okay ssl_vrfy_decide __P((void));
static int ssl_rand_init __P((void));
static SSL_METHOD *ssl_select_method __P((void));
static int ssl_verify_cb __P((int, X509_STORE_CTX *));
static void ssl_load_verifications __P((struct mailbox *));
static void ssl_certificate __P((struct mailbox *, const char *));
static enum okay ssl_check_host __P((const char *,
			struct mailbox *));
static enum okay ssl_open __P((const char *, struct mailbox *, const char *));
static void ssl_gen_err __P((const char *));

static void
ssl_set_vrfy_level()
{
	char *cp;

	ssl_vrfy_level = VRFY_ASK;
	if ((cp = value("ssl-verify")) != NULL) {
		if (equal(cp, "strict"))
			ssl_vrfy_level = VRFY_STRICT;
		else if (equal(cp, "ask"))
			ssl_vrfy_level = VRFY_ASK;
		else if (equal(cp, "warn"))
			ssl_vrfy_level = VRFY_WARN;
		else if (equal(cp, "ignore"))
			ssl_vrfy_level = VRFY_IGNORE;
		else
			fprintf(stderr, catgets(catd, CATSET, 265,
					"invalid value of ssl-verify: %s\n"),
					cp);
	}
}

static enum okay
ssl_vrfy_decide()
{
	enum okay ok = STOP;

	switch (ssl_vrfy_level) {
	case VRFY_STRICT:
		ok = STOP;
		break;
	case VRFY_ASK:
		{
			char *line = NULL;
			size_t linesize = 0;

			fprintf(stderr, catgets(catd, CATSET, 264,
					"Continue (y/n)? "));
			if (readline(stdin, &line, &linesize) > 0 &&
					*line == 'y')
				ok = OKAY;
			else
				ok = STOP;
			if (line)
				free(line);
		}
		break;
	case VRFY_WARN:
	case VRFY_IGNORE:
		ok = OKAY;
	}
	return ok;
}

static int
ssl_rand_init()
{
	char *cp;
	int state = 0;

	if ((cp = value("ssl-rand-egd")) != NULL) {
		cp = expand(cp);
		if (RAND_egd(cp) == -1) {
			fprintf(stderr, catgets(catd, CATSET, 245,
				"entropy daemon at \"%s\" not available\n"),
					cp);
		} else
			state = 1;
	} else if ((cp = value("ssl-rand-file")) != NULL) {
		cp = expand(cp);
		if (RAND_load_file(cp, 1024) == -1) {
			fprintf(stderr, catgets(catd, CATSET, 246,
				"entropy file at \"%s\" not available\n"), cp);
		} else {
			struct stat st;

			if (stat(cp, &st) == 0 && S_ISREG(st.st_mode) &&
					access(cp, W_OK) == 0) {
				if (RAND_write_file(cp) == -1) {
					fprintf(stderr, catgets(catd, CATSET,
								247,
				"writing entropy data to \"%s\" failed\n"), cp);
				}
			}
			state = 1;
		}
	}
	return state;
}

static SSL_METHOD *
ssl_select_method()
{
	SSL_METHOD *method;
	char *cp;

	if ((cp = value("ssl-method")) != NULL) {
		if (equal(cp, "ssl2"))
			method = SSLv2_client_method();
		else if (equal(cp, "ssl3"))
			method = SSLv3_client_method();
		else if (equal(cp, "tls1"))
			method = TLSv1_client_method();
		else {
			fprintf(stderr, catgets(catd, CATSET, 244,
					"Invalid SSL method \"%s\"\n"), cp);
			method = SSLv23_client_method();
		}
	} else
		method = SSLv23_client_method();
	return method;
}

static int
ssl_verify_cb(int success, X509_STORE_CTX *store)
{
	if (success == 0) {
		char data[256];
		X509 *cert = X509_STORE_CTX_get_current_cert(store);
		int depth = X509_STORE_CTX_get_error_depth(store);
		int err = X509_STORE_CTX_get_error(store);

		fprintf(stderr, catgets(catd, CATSET, 229,
				"Error with certificate at depth: %i\n"),
				depth);
		X509_NAME_oneline(X509_get_issuer_name(cert), data,
				sizeof data);
		fprintf(stderr, catgets(catd, CATSET, 230, " issuer = %s\n"),
				data);
		X509_NAME_oneline(X509_get_subject_name(cert), data,
				sizeof data);
		fprintf(stderr, catgets(catd, CATSET, 231, " subject = %s\n"),
				data);
		fprintf(stderr, catgets(catd, CATSET, 232, " err %i: %s\n"),
				err, X509_verify_cert_error_string(err));
		if (ssl_vrfy_decide() != OKAY)
			return 0;
	}
	return 1;
}

static void
ssl_load_verifications(mp)
	struct mailbox *mp;
{
	char *ca_dir, *ca_file;

	if (ssl_vrfy_level == VRFY_IGNORE)
		return;
	if ((ca_dir = value("ssl-ca-dir")) != NULL)
		ca_dir = expand(ca_dir);
	if ((ca_file = value("ssl-ca-file")) != NULL)
		ca_file = expand(ca_file);
	if (ca_dir || ca_file) {
		if (SSL_CTX_load_verify_locations(mp->mb_ctx,
					ca_file, ca_dir) != 1) {
			fprintf(stderr, catgets(catd, CATSET, 233,
						"Error loading"));
			if (ca_dir) {
				fprintf(stderr, catgets(catd, CATSET, 234,
							" %s"), ca_dir);
				if (ca_file)
					fprintf(stderr, catgets(catd, CATSET,
							235, " or"));
			}
			if (ca_file)
				fprintf(stderr, catgets(catd, CATSET, 236,
						" %s"), ca_file);
			fprintf(stderr, catgets(catd, CATSET, 237, "\n"));
		}
	}
	if (value("ssl-no-default-ca") == NULL) {
		if (SSL_CTX_set_default_verify_paths(mp->mb_ctx) != 1)
			fprintf(stderr, catgets(catd, CATSET, 243,
				"Error loading default CA locations\n"));
	}
	SSL_CTX_set_verify(mp->mb_ctx, SSL_VERIFY_PEER, ssl_verify_cb);
}

static void
ssl_certificate(mp, uhp)
	struct mailbox *mp;
	const char *uhp;
{
	char *certvar, *keyvar, *cert, *key;

	certvar = ac_alloc(strlen(uhp) + 10);
	strcpy(certvar, "ssl-cert-");
	strcpy(&certvar[9], uhp);
	if ((cert = value(certvar)) != NULL ||
			(cert = value("ssl-cert")) != NULL) {
		cert = expand(cert);
		if (SSL_CTX_use_certificate_chain_file(mp->mb_ctx, cert) == 1) {
			keyvar = ac_alloc(strlen(uhp) + 9);
			strcpy(keyvar, "ssl-key-");
			if ((key = value(keyvar)) == NULL &&
					(key = value("ssl-key")) == NULL)
				key = cert;
			else
				key = expand(key);
			if (SSL_CTX_use_PrivateKey_file(mp->mb_ctx, key,
						SSL_FILETYPE_PEM) != 1)
				fprintf(stderr, catgets(catd, CATSET, 238,
				"cannot load private key from file %s\n"),
						key);
			ac_free(keyvar);
		} else
			fprintf(stderr, catgets(catd, CATSET, 239,
				"cannot load certificate from file %s\n"),
					cert);
	}
	ac_free(certvar);
}

static enum okay
ssl_check_host(server, mp)
	const char *server;
	struct mailbox *mp;
{
	X509 *cert;
	X509_NAME *subj;
	char data[256];
	int i, extcount;

	if ((cert = SSL_get_peer_certificate(mp->mb_ssl)) == NULL) {
		fprintf(stderr, catgets(catd, CATSET, 248,
				"no certificate from \"%s\"\n"), server);
		return STOP;
	}
	extcount = X509_get_ext_count(cert);
	for (i = 0; i < extcount; i++) {
		const char *extstr;
		X509_EXTENSION *ext;

		ext = X509_get_ext(cert, i);
		extstr= OBJ_nid2sn(OBJ_obj2nid(X509_EXTENSION_get_object(ext)));
		if (equal(extstr, "subjectAltName")) {
			int j;
			unsigned char *data;
			STACK_OF(CONF_VALUE) *val;
			CONF_VALUE *nval;
			X509V3_EXT_METHOD *meth;

			if ((meth = X509V3_EXT_get(ext)) == NULL)
				break;
			data = ext->value->data;
			val = meth->i2v(meth,
					meth->d2i(NULL, &data,
						ext->value->length),
					NULL);
			for (j = 0; j < sk_CONF_VALUE_num(val); j++) {
				/*LINTED*/
				nval = sk_CONF_VALUE_value(val, j);
				if (equal(nval->name, "DNS") &&
						asccasecmp(nval->value, server)
						== 0)
					goto found;
			}
		}
	}
	if ((subj = X509_get_subject_name(cert)) != NULL &&
			X509_NAME_get_text_by_NID(subj, NID_commonName,
				data, sizeof data) > 0) {
		data[sizeof data - 1] = 0;
		if (asccasecmp(data, server) == 0)
			goto found;
	}
	X509_free(cert);
	return STOP;
found:	X509_free(cert);
	return OKAY;
}

static enum okay
ssl_open(server, mp, uhp)
	const char *server;
	struct mailbox *mp;
	const char *uhp;
{
	static int initialized, rand_init;
	char *cp;
	long options;

	if (initialized == 0) {
		SSL_library_init();
		initialized = 1;
	}
	if (rand_init == 0)
		rand_init = ssl_rand_init();
	ssl_set_vrfy_level();
	if ((mp->mb_ctx = SSL_CTX_new(ssl_select_method())) == NULL) {
		ssl_gen_err(catgets(catd, CATSET, 261, "SSL_CTX_new() failed"));
		return STOP;
	}
#ifdef	SSL_MODE_AUTO_RETRY
	/* available with OpenSSL 0.9.6 or later */
	SSL_CTX_set_mode(mp->mb_ctx, SSL_MODE_AUTO_RETRY);
#endif	/* SSL_MODE_AUTO_RETRY */
	options = SSL_OP_ALL;
	if (value("ssl-v2-allow") == NULL)
		options |= SSL_OP_NO_SSLv2;
	SSL_CTX_set_options(mp->mb_ctx, options);
	ssl_load_verifications(mp);
	ssl_certificate(mp, uhp);
	if ((cp = value("ssl-cipher-list")) != NULL) {
		if (SSL_CTX_set_cipher_list(mp->mb_ctx, cp) != 1)
			fprintf(stderr, catgets(catd, CATSET, 240,
					"invalid ciphers: %s\n"), cp);
	}
	if ((mp->mb_ssl = SSL_new(mp->mb_ctx)) == NULL) {
		ssl_gen_err(catgets(catd, CATSET, 262, "SSL_new() failed"));
		return STOP;
	}
	SSL_set_fd(mp->mb_ssl, mp->mb_sock);
	if (SSL_connect(mp->mb_ssl) < 0) {
		ssl_gen_err(catgets(catd, CATSET, 263,
				"could not initiate SSL/TLS connection"));
		return STOP;
	}
	if (ssl_vrfy_level != VRFY_IGNORE) {
		if (ssl_check_host(server, mp) != OKAY) {
			fprintf(stderr, catgets(catd, CATSET, 249,
				"host certificate does not match \"%s\"\n"),
				server);
			if (ssl_vrfy_decide() != OKAY)
				return STOP;
		}
	}
	return OKAY;
}

static void
ssl_gen_err(msg)
	const char *msg;
{
	SSL_load_error_strings();
	fprintf(stderr, "%s: %s\n", msg,
		ERR_error_string(ERR_get_error(), NULL));
}
#endif	/* USE_SSL */

static void
pop3_timer_off()
{
	if (pop3keepalive > 0) {
		alarm(0);
		safe_signal(SIGALRM, savealrm);
	}
}

static int
pop3_close(mp)
	struct mailbox *mp;
{
	int i;

	if (mp->mb_sock >= 0) {
		pop3_timer_off();
#ifdef	USE_SSL
		if (mp->mb_ssl) {
			SSL_shutdown(mp->mb_ssl);
			SSL_free(mp->mb_ssl);
			mp->mb_ssl = NULL;
			SSL_CTX_free(mp->mb_ctx);
			mp->mb_ctx = NULL;
		}
#endif	/* USE_SSL */
		i = close(mp->mb_sock);
		mp->mb_sock = -1;
		return i;
	}
	return 0;
}

static ssize_t
xwrite(fd, data, sz)
	const char *data;
	size_t sz;
{
	ssize_t wo, wt = 0;

	do {
		if ((wo = write(fd, data + wt, sz - wt)) < 0) {
			if (errno == EINTR)
				continue;
			else
				return -1;
		}
		wt += wo;
	} while (wt < sz);
	return sz;
}

static enum okay
pop3_write(mp, data)
	struct mailbox *mp;
	char *data;
{
	int sz = strlen(data), x;

#ifdef	USE_SSL
	if (mp->mb_ssl) {
ssl_retry:	x = SSL_write(mp->mb_ssl, data, sz);
		if (x < 0) {
			switch(SSL_get_error(mp->mb_ssl, x)) {
			case SSL_ERROR_WANT_READ:
			case SSL_ERROR_WANT_WRITE:
				goto ssl_retry;
			}
		}
	} else
#endif	/* USE_SSL */
	{
		x = xwrite(mp->mb_sock, data, sz);
	}
	if (x != sz) {
#ifdef	USE_SSL
		(mp->mb_ssl ? ssl_gen_err : perror)
#else	/* !USE_SSL */
			perror
#endif	/* !USE_SSL */
				(catgets(catd, CATSET, 259,
					"POP3 write error"));
		if (x < 0)
			pop3_close(mp);
		return STOP;
	}
	return OKAY;
}

static int
pop3_getline(line, linesize, linelen, mp)
	char **line;
	size_t *linesize, *linelen;
	struct mailbox *mp;
{
	char *lp = *line;

	if (mp->mb_sz < 0) {
		pop3_close(mp);
		return mp->mb_sz;
	}
	do {
		if (*line == NULL || lp > &(*line)[*linesize - 128]) {
			size_t diff = lp - *line;
			*line = srealloc(*line, *linesize += 256);
			lp = &(*line)[diff];
		}
		if (mp->mb_ptr == NULL ||
				mp->mb_ptr >= &mp->mb_buf[mp->mb_sz]) {
#ifdef	USE_SSL
			if (mp->mb_ssl) {
		ssl_retry:	if ((mp->mb_sz = SSL_read(mp->mb_ssl,
						mp->mb_buf,
						sizeof mp->mb_buf)) <= 0) {
					if (mp->mb_sz < 0) {
						switch(SSL_get_error(mp->mb_ssl,
							mp->mb_sz)) {
						case SSL_ERROR_WANT_READ:
						case SSL_ERROR_WANT_WRITE:
							goto ssl_retry;
						}
						ssl_gen_err(catgets(catd,
							CATSET, 250,
							"POP3 read failed"));

					}
					break;
				}
			} else
#endif	/* USE_SSL */
			{
			again:	if ((mp->mb_sz = read(mp->mb_sock, mp->mb_buf,
						sizeof mp->mb_buf)) <= 0) {
					if (mp->mb_sz < 0) {
						if (errno == EINTR)
							goto again;
						perror(catgets(catd, CATSET,
							250,
							"POP3 read failed"));
					}
					break;
				}
			}
			mp->mb_ptr = mp->mb_buf;
		}
	} while ((*lp++ = *mp->mb_ptr++) != '\n');
	*lp = '\0';
	if (linelen)
		*linelen = lp - *line;
	return lp - *line;
}

static enum okay
pop3_answer(mp)
	struct mailbox *mp;
{
	int sz;
	enum okay ok = STOP;

retry:	if ((sz = pop3_getline(&pop3buf, &pop3bufsize, NULL, mp)) > 0) {
		if ((mp->mb_active & (MB_COMD|MB_MULT)) == MB_MULT)
			goto multiline;
		if (verbose)
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
				sz = pop3_getline(&pop3buf, &pop3bufsize,
						NULL, mp);
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
pop3_finish(mp)
	struct mailbox *mp;
{
	while (mp->mb_active != MB_NONE)
		pop3_answer(mp);
	return OKAY;
}

static RETSIGTYPE
pop3catch(s)
	int s;
{
	if (reset_tio)
		tcsetattr(0, TCSADRAIN, &otio);
	if (s == SIGPIPE)
		pop3_close(&mb);
	siglongjmp(pop3jmp, 1);
}

static enum okay
pop3_noop(mp)
	struct mailbox *mp;
{
	POP3_OUT("NOOP\r\n", MB_COMD)
	POP3_ANSWER()
	return OKAY;
}

/*ARGSUSED*/
static RETSIGTYPE
pop3alarm(s)
	int s;
{
	sighandler_type	saveint;
	sighandler_type savepipe;

	if (pop3lock++ == 0) {
		saveint = safe_signal(SIGINT, SIG_IGN);
		savepipe = safe_signal(SIGPIPE, SIG_IGN);
		if (sigsetjmp(pop3jmp, 1)) {
			safe_signal(SIGINT, saveint);
			safe_signal(SIGPIPE, savepipe);
			goto brk;
		}
		if (saveint != SIG_IGN)
			safe_signal(SIGINT, pop3catch);
		if (savepipe != SIG_IGN)
			safe_signal(SIGPIPE, pop3catch);
		if (pop3_noop(&mb) != OKAY) {
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
pop3_open(xserver, mp, use_ssl, uhp)
	const char *xserver;
	struct mailbox *mp;
	const char *uhp;
{
	int sockfd;
	struct sockaddr_in servaddr;
	struct in_addr **pptr;
	struct hostent *hp;
	struct servent *sp;
	unsigned short port = 0;
	char *portstr = use_ssl ? "pop3s" : "pop3", *cp;
	char *server = (char *)xserver;

	if ((cp = strchr(server, ':')) != NULL) {
		portstr = &cp[1];
		port = (unsigned short)strtol(portstr, NULL, 10);
		server = salloc(cp - xserver + 1);
		memcpy(server, xserver, cp - xserver);
		server[cp - xserver] = '\0';
	}
	if (port == 0) {
		if ((sp = getservbyname(portstr, "tcp")) == NULL) {
			if (equal(portstr, "pop3"))
				port = htons(110);
			else if (equal(portstr, "pop3s"))
				port = htons(995);
			else {
				fprintf(stderr, catgets(catd, CATSET, 251,
					"unknown service: %s\n"), portstr);
				return STOP;
			}
		} else
			port = sp->s_port;
	} else
		port = htons(port);
	if ((hp = gethostbyname(server)) == NULL) {
		fprintf(stderr, catgets(catd, CATSET, 252,
				"could not resolve host: %s\n"), server);
		return STOP;
	}
	pptr = (struct in_addr **)hp->h_addr_list;
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror(catgets(catd, CATSET, 253, "could not create socket"));
		return STOP;
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
		return STOP;
	}
	if (verbose)
		fputs(catgets(catd, CATSET, 193, " connected.\n"), stderr);
	mp->mb_sock = sockfd;
#ifdef	USE_SSL
	if (use_ssl) {
		enum okay ok;

		if ((ok = ssl_open(server, mp, uhp)) != OKAY)
			pop3_close(mp);
		return ok;
	}
#endif	/* USE_SSL */
	return OKAY;
}

static enum okay
pop3_pass(mp, pass)
	struct mailbox *mp;
	const char *pass;
{
	char o[LINESIZE];

	snprintf(o, sizeof o, "PASS %s\r\n", pass);
	POP3_OUT(o, MB_COMD)
	POP3_ANSWER()
	return OKAY;
}

static enum okay
pop3_user(mp, xuser, pass)
	struct mailbox *mp;
	char *xuser;
	const char *pass;
{
	char *line = NULL, o[LINESIZE], *user;
	size_t linesize = 0;
	struct termios tio;
	int i;

	POP3_ANSWER()
retry:	if (xuser == NULL) {
		if (is_a_tty[0]) {
			fputs("User: ", stdout);
			fflush(stdout);
		}
		if (readline(stdin, &line, &linesize) == 0) {
			if (line)
				free(line);
			return STOP;
		}
		user = line;
	} else
		user = xuser;
	snprintf(o, sizeof o, "USER %s\r\n", user);
	POP3_OUT(o, MB_COMD)
	POP3_ANSWER()
	if (pass == NULL) {
		if (is_a_tty[0]) {
			fputs("Password: ", stdout);
			fflush(stdout);
			tcgetattr(0, &tio);
			otio = tio;
			tio.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
			reset_tio = 1;
			tcsetattr(0, TCSAFLUSH, &tio);
		}
		i = readline(stdin, &line, &linesize);
		if (is_a_tty[0]) {
			fputc('\n', stdout);
			tcsetattr(0, TCSADRAIN, &otio);
		}
		reset_tio = 0;
		if (i < 0) {
			if (line)
				free(line);
			return STOP;
		}
	}
	if (pop3_pass(mp, pass ? pass : line) == STOP) {
		pass = NULL;
		goto retry;
	}
	if (line)
		free(line);
	return OKAY;
}

static enum okay
pop3_stat(mp, size, count)
	struct mailbox *mp;
	off_t *size;
	int *count;
{
	char *cp;
	enum okay ok = OKAY;

	POP3_OUT("STAT\r\n", MB_COMD);
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
pop3_list(mp, n, size)
	struct mailbox *mp;
	int n;
	size_t *size;
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
pop3_init(mp, n)
	struct mailbox *mp;
{
	struct message *m = &message[n];
	char *cp;

	m->m_flag = MUSED|MNEW|MNOFROM;
	m->m_block = 0;
	m->m_offset = 0;
	pop3_list(mp, m - message + 1, &m->m_xsize);
	if ((cp = hfield("status", m)) != NULL) {
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
pop3_dates(mp)
	struct mailbox *mp;
{
	char *cp;
	struct message *m;
	time_t now;
	int i;

	/*
	 * Determine the date to print in faked 'From ' lines. This is
	 * traditionally the date the message was written to the mail
	 * file. Try to determine this using RFC message header fields,
	 * or fall back to current time.
	 */
	time(&now);
	for (i = 0; i < msgcount; i++) {
		m = &message[i];
		if ((cp = hfield_mult("received", m, 0)) != NULL) {
			while ((cp = nexttoken(cp)) != NULL && *cp != ';') {
				do
					cp++;
				while (alnumchar(*cp & 0377));
			}
			if (cp && *++cp)
				m->m_time = rfctime(cp);
		}
		if (m->m_time == 0 || m->m_time > now)
			if ((cp = hfield("date", m)) != NULL)
				m->m_time = rfctime(cp);
		if (m->m_time == 0 || m->m_time > now)
			m->m_time = now;
	}
}

static void
pop3_setptr(mp)
	struct mailbox *mp;
{
	int i;

	message = scalloc(msgcount + 1, sizeof *message);
	for (i = 0; i < msgcount; i++)
		pop3_init(mp, i);
	setdot(message);
	message[msgcount].m_size = 0;
	message[msgcount].m_lines = 0;
	pop3_dates(mp);
}

static char *
pop3_have_password(server)
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

int
pop3_setfile(server, newmail, isedit)
	const char *server;
{
	sighandler_type	saveint;
	sighandler_type savepipe;
	char *user;
	const char *cp, *sp = server, *pass, *uhp;
	int use_ssl = 0;

	(void)&sp;
	(void)&use_ssl;
	if (newmail)
		return 1;
	quit();
	edit = isedit;
	mb.mb_sock = -1;
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
	saveint = safe_signal(SIGINT, SIG_IGN);
	savepipe = safe_signal(SIGPIPE, SIG_IGN);
	if (sigsetjmp(pop3jmp, 1)) {
		pop3_close(&mb);
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
	if (strncmp(sp, "pop3://", 7) == 0) {
		sp = &sp[7];
		use_ssl = 0;
#ifdef	USE_SSL
	} else if (strncmp(sp, "pop3s://", 8) == 0) {
		sp = &sp[8];
		use_ssl = 1;
#endif	/* USE_SSL */
	}
	uhp = sp;
	pass = pop3_have_password(uhp);
	if ((cp = strchr(sp, '@')) != NULL) {
		user = salloc(cp - sp + 1);
		memcpy(user, sp, cp - sp);
		user[cp - sp] = '\0';
		sp = &cp[1];
	} else
		user = NULL;
	verbose = value("verbose") != NULL;
	if (pop3_open(sp, &mb, use_ssl, uhp) != OKAY) {
		pop3_timer_off();
		safe_signal(SIGINT, saveint);
		safe_signal(SIGPIPE, savepipe);
		pop3lock = 0;
		return 1;
	}
	if (pop3_user(&mb, user, pass) != OKAY ||
			pop3_stat(&mb, &mailsize, &msgcount) != OKAY) {
		pop3_close(&mb);
		pop3_timer_off();
		safe_signal(SIGINT, saveint);
		safe_signal(SIGPIPE, savepipe);
		pop3lock = 0;
		return 1;
	}
	mb.mb_type = MB_POP3;
	mb.mb_perm = MB_DELE;
	pop3_setptr(&mb);
	setmsize(msgcount);
	sawcom = 0;
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	pop3lock = 0;
	if (!edit && msgcount == 0) {
		if (mb.mb_type == MB_POP3 && value("emptystart") == NULL)
			fprintf(stderr, catgets(catd, CATSET, 258,
				"No mail at %s\n"), server);
		return 1;
	}
	return 0;
}

static enum okay
pop3_get(mp, m, need)
	struct mailbox *mp;
	struct message *m;
	enum needspec need;
{
	sighandler_type	saveint = SIG_IGN;
	sighandler_type savepipe = SIG_IGN;
	off_t offset;
	char o[LINESIZE], *line = NULL, *lp;
	size_t linesize = 0, linelen, size;
	int number = m - message + 1;
	int emptyline = 0, lines;

	(void)&saveint;
	(void)&savepipe;
	(void)&number;
	(void)&emptyline;
	(void)&need;
	verbose = value("verbose") != NULL;
	if (mp->mb_sock < 0) {
		fprintf(stderr, catgets(catd, CATSET, 219,
				"POP3 connection already closed.\n"));
		return STOP;
	}
	if (pop3lock++ == 0) {
		saveint = safe_signal(SIGINT, SIG_IGN);
		savepipe = safe_signal(SIGPIPE, SIG_IGN);
		if (sigsetjmp(pop3jmp, 1)) {
			safe_signal(SIGINT, saveint);
			safe_signal(SIGPIPE, savepipe);
			pop3lock--;
			return STOP;
		}
		if (saveint != SIG_IGN)
			safe_signal(SIGINT, pop3catch);
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
		return STOP;
	}
	size = 0;
	lines = 0;
	while (pop3_getline(&line, &linesize, &linelen, mp) > 0) {
		if (line[0] == '.' && line[1] == '\r' && line[2] == '\n' &&
				line[3] == '\0') {
			mp->mb_active &= ~MB_MULT;
			break;
		}
		lines++;
		if (line[0] == '.') {
			lp = &line[1];
			linelen--;
		} else
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
	m->m_block = nail_blockof(offset);
	m->m_offset = nail_offsetof(offset);
	fflush(mp->mb_otf);
	switch (need) {
	case NEED_HEADER:
		m->m_have |= HAVE_HEADER;
		break;
	case NEED_BODY:
		m->m_have |= HAVE_BODY;
		m->m_xlines = m->m_lines;
		m->m_xsize = m->m_size;
		break;
	}
	if (line)
		free(line);
	if (saveint != SIG_IGN)
		safe_signal(SIGINT, saveint);
	if (savepipe != SIG_IGN)
		safe_signal(SIGPIPE, savepipe);
	pop3lock--;
	return OKAY;
}

enum okay
pop3_header(m)
	struct message *m;
{
	return pop3_get(&mb, m, NEED_HEADER);
}


enum okay
pop3_body(m)
	struct message *m;
{
	return pop3_get(&mb, m, NEED_BODY);
}

static enum okay
pop3_exit(mp)
	struct mailbox *mp;
{
	POP3_OUT("QUIT\r\n", MB_COMD)
	POP3_ANSWER()
	return OKAY;
}

static enum okay
pop3_delete(mp, n)
	struct mailbox *mp;
{
	char o[LINESIZE];

	snprintf(o, sizeof o, "DELE %u\r\n", n);
	POP3_OUT(o, MB_COMD)
	POP3_ANSWER()
	return OKAY;
}

static enum okay
pop3_update(mp)
	struct mailbox *mp;
{
	FILE *readstat = NULL;
	struct message *m;
	int dodel, c, gotcha, held;

	if (Tflag != NULL) {
		if ((readstat = Fopen(Tflag, "w")) == NULL)
			Tflag = NULL;
	}
	if (!edit) {
		holdbits();
		for (m = &message[0], c = 0; m < &message[msgcount]; m++) {
			if (m->m_flag & MBOX)
				c++;
		}
		if (c > 0)
			makembox();
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
			pop3_delete(mp, m - message + 1);
			gotcha++;
		} else
			held++;
	}
	if (readstat != NULL)
		Fclose(readstat);
	if (gotcha && edit) {
		printf(catgets(catd, CATSET, 168, "\"%s\" "), mailname);
		printf(value("bsdcompat") ?
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
pop3_quit()
{
	sighandler_type	saveint;
	sighandler_type savepipe;

	verbose = value("verbose") != NULL;
	if (mb.mb_sock < 0) {
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
	pop3_close(&mb);
	safe_signal(SIGINT, saveint);
	safe_signal(SIGPIPE, savepipe);
	pop3lock = 0;
}
#else	/* !HAVE_SOCKETS */
static void
nopop3()
{
	fprintf(stderr, catgets(catd, CATSET, 216,
				"No POP3 support compiled in.\n"));
}

int
pop3_setfile(server, newmail, isedit)
	const char *server;
{
	nopop3();
	return -1;
}

enum okay
pop3_header(mp)
	struct message *mp;
{
	nopop3();
	return STOP;
}

enum okay
pop3_body(mp)
	struct message *mp;
{
	nopop3();
	return STOP;
}

void
pop3_quit()
{
	nopop3();
}
#endif	/* HAVE_SOCKETS */
