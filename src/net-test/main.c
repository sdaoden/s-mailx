/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Simple optional pseudo server to test network I/O and protocols.
 *@ Synopsis: net-test [-S | -s || [-U]] [-v] TEST-SCRIPT
 *@ - TEST-SCRIPT must be an executable script, which is invoked with the port
 *@   number to connect to as an argument.
 *@ - net-test reads \001\n:SERVER DATA:\002\n:CLIENT DATA: data tuples from
 *@   STDIN, and uses that to "replay" I/O over the TCP connection the client
 *@   establishes via the provided port number.  Data mismatches cause errors.
 *@ - If -S is given a TLS handshake is immediately performed, with -s the
 *@   connection can later be upgraded (mx_HAVE_XTLS only).
 *@   This mode needs server-chain.pem (key+cert) and ca.pem (all certs) in ./.
 *@ . if -U is (also) given, request and verify client certificate.
 *@   the user matches.
 *@ - If -v is given, the prepared I/O as well as real I/O is logged on STDERR.
 *@ TODO - intermangled responses (expect tuple 2[lines follow] 3[..]),
 *@ TODO   store list unless fulfilled (or whatever).  E.g.,
 *@ TODO   "tuples 1/2 2/3" would read one line and save the next two as
 *@ TODO   expected answers, etc.  Then resort the collected answers and
 *@ TODO   write them in the given order to STDOUT for checksumming.  Etc.
 *
 * Copyright (c) 2019 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#define mx_EXT_SOURCE

#include <mx/gen-config.h>

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifdef mx_HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef mx_HAVE_XTLS /* Shorthand for mx_HAVE_TLS==mx_TLS_IMPL{...} */
# define a_HAVE_TLS
# include <openssl/ssl.h>
#endif

/* */
#define a_LINE_BUF 4096

/* SO_REUSEADDR, but sometimes buggy / not available */
#define a_PORT_FIRST 50000
#define a_PORT_LAST 50050

/* As this is a test, restrain it! */
#define a_TIMEOUT 7

/* These are generated by mx-test.sh, and are expected in CWD */
#define a_TLS_PEM_CHAIN "server-chain.pem"
#define a_TLS_PEM_KEY "server-key.pem"
#define a_TLS_PEM_CA "ca.pem"

/* Mode control in user input data */
#define a_MODE_CLI '\001'
#define a_MODE_SRV '\002'
#define a_MODE_TLS '\003'

/* */
#ifdef mx_HAVE_XTLS
# if mx_HAVE_XTLS >= 0x30000
#  define a_SSL_CTX_load_verify_file(CTXP,FILE) SSL_CTX_load_verify_file(CTXP, FILE)
# else
#  define a_SSL_CTX_load_verify_file(CTXP,FILE) SSL_CTX_load_verify_locations(CTXP, FILE, NULL)
# endif
#endif

#ifdef a_HAVE_TLS
enum a_tls_mode{
	a_TLS_NONE,
	/* Used as bits, tls_setup() sets NONE, then */
	a_TLS_ALWAYS = 1u<<0,
	a_TLS_UPGRADE = 1u<<1,
	a_TLS_CLIENT_CHECK = 1u<<2,
	/* When a_tls!=NULL, these may be set */
	a_TLS_NEED_ACCEPT = 1u<<3,
	a_TLS_NEED_READ = 1u<<4,
	a_TLS_NEED_WRITE = 1u<<5,
	a_TLS_NEED_IO_MASK = a_TLS_NEED_READ | a_TLS_NEED_WRITE
};
#endif

struct a_comm{
	struct a_comm *c_next;
	struct a_dat *c_lines;
	char c_mode;
};

struct a_dat{
	struct a_dat *d_next;
	char *d_dat;
	size_t d_len;
};

static int a_verbose;
static int a_sigchld_seen;
#ifdef a_HAVE_TLS
static SSL *a_tls;
static unsigned int/*enum a_tls_mode*/ a_tls_mode;
#endif

static void a_sigchld(int sig);

/* Convert all the input data to comm-unication */
static struct a_comm *a_prepare_comm(void);

/* Handle the incoming connection request */
static int a_connection(int sofd, struct a_comm *commp);

/* */
#ifndef a_HAVE_TLS
# define a_read(A,B,C) read(A, B, C)
# define a_write(A,B,C) write(A, B, C)
#elif defined mx_HAVE_XTLS
# define a_read(A,B,C) a_tls_read(A, B, C)
# define a_write(A,B,C) a_tls_write(A, B, C)
static int a_tls_setup(int sofd);
static int a_tls_accept(void);
static ssize_t a_tls_read(int sofd, void *buf, size_t len);
static ssize_t a_tls_write(int sofd, void const *buf, size_t len);
#else
# error This TLS implementation not supported
#endif

int
main(int argc, char **argv){
	char ibuf[16];
	struct sigaction siac;
	struct sockaddr_in soa4;
	socklen_t soal;
	struct a_comm *commp;
	int es, sofd, i;
	pid_t clipid;

	clipid = 0;
	es = 1;

	while(argc > 2){
		if(!strcmp(argv[1], "-v"))
			a_verbose = 1;
#ifdef a_HAVE_TLS
		else if(!strcmp(argv[1], "-S"))
			a_tls_mode |= a_TLS_ALWAYS;
		else if(!strcmp(argv[1], "-s"))
			a_tls_mode |= a_TLS_UPGRADE;
		else if(!strcmp(argv[1], "-U"))
			a_tls_mode |= a_TLS_CLIENT_CHECK;
#endif
		else
			break;
		--argc, ++argv;
	}
	if(argc != 2
#ifdef a_HAVE_TLS
			|| ((a_tls_mode & a_TLS_CLIENT_CHECK) && !(a_tls_mode & (a_TLS_ALWAYS | a_TLS_UPGRADE)))
#endif
	){
		errno = EINVAL;
		goto jex1;
	}
	++es;

	if((commp = a_prepare_comm()) == NULL)
		goto jex1;
	++es;

	if(access(argv[1], X_OK))
		goto jex1;
	++es;

	while((sofd = socket(AF_INET, SOCK_STREAM
#ifdef SOCK_CLOEXEC
			| SOCK_CLOEXEC
#endif
			, 0)) == -1){
		if(errno == EINTR)
			continue;
		goto jex1;
	}
#ifndef SOCK_CLOEXEC
	fcntl(sofd, F_SETFD, FD_CLOEXEC);
#endif
	i = 1;
	while(setsockopt(sofd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) == -1)
		if(errno != EINTR)
			goto jex1;
	++es;

	memset(&soa4, 0, soal = sizeof(soa4));
	soa4.sin_family = AF_INET;
	soa4.sin_port = htons(a_PORT_FIRST);
	soa4.sin_addr.s_addr = INADDR_ANY;

	while(bind(sofd, (struct sockaddr const*)&soa4, soal) == -1){
		switch(errno){
		case EINTR:
			break;
		case EADDRINUSE:
			soa4.sin_port = ntohs(soa4.sin_port);
			if(++soa4.sin_port >= a_PORT_LAST)
				goto jex1;
			soa4.sin_port = htons(soa4.sin_port);
			break;
		default:
			goto jex1;
		}
	}
	snprintf(ibuf, sizeof(ibuf), "%d", (int)ntohs(soa4.sin_port));
	++es;

	while(listen(sofd, 1) == -1){
		if(errno != EINTR)
			goto jex1;
	}
	++es;

	memset(&siac, 0, sizeof siac);
	siac.sa_handler = &a_sigchld;
	sigemptyset(&siac.sa_mask);
	sigaction(SIGCHLD, &siac, NULL);
	++es;

	switch((clipid = fork())){
	case -1:
		goto jex1;
	case 0:{
		char *cargv[3];

		cargv[0] = argv[1];
		cargv[1] = ibuf;
		cargv[2] = NULL;
		execv(argv[1], cargv);
		close(sofd);
		exit(100);
		}break;
	default:
		while((i = accept(sofd, (struct sockaddr*)&soa4, &soal)) == -1)
			if(errno != EINTR || a_sigchld_seen)
				goto jex1;
		close(sofd);
		++es;

		if(!a_connection(i, commp)){
			switch(errno){
			case 0:
				errno = ECANCELED;
				/* FALLTHRU */
			default:
				break;
			case ETIMEDOUT:
				kill(clipid, SIGKILL);
				break;
			}
			++es;
			goto jex1;
		}
		break;
	}

	es = 0;
	if(0){
jex1:
		perror("net-test");
	}

#ifdef a_HAVE_TLS
	if(a_tls != NULL)
		SSL_free(a_tls);
#endif

	if(clipid > 0){
		sofd = SIGTERM;
		while(waitpid(clipid, NULL, WNOHANG) == 0){
			sleep(1);
			kill(clipid, sofd);
			sofd = SIGKILL;
		}
	}

	return es;
}

static void
a_sigchld(int sig){
	(void)sig;
	a_sigchld_seen = 1;
}

static struct a_comm *
a_prepare_comm(void){
	char b[a_LINE_BUF];
	struct a_dat *datp;
	struct a_comm *rv, *commp;

	for(rv = commp = NULL;;){
		if(fgets(b, sizeof(b) - 2, stdin) == NULL){
			if(ferror(stdin))
				goto jerr;
			if(commp == NULL || commp->c_lines == NULL)
				goto jerr;
			break;
		}else{
			size_t i;

			if((i = strlen(b)) == 2 && (b[0] == a_MODE_CLI || b[0] == a_MODE_SRV || b[0] == a_MODE_TLS) &&
					b[1] == '\n'){
				struct a_comm *cp;

				if(b[0] == a_MODE_TLS){
#ifndef a_HAVE_TLS
					fprintf(stderr, "NET_TEST ERROR: no TLS (\\003) available\n");
					goto jerr;
#else
					if(!(a_tls_mode & a_TLS_UPGRADE)){
						fprintf(stderr, "NET_TEST ERROR: TLS upgrade (\\003) needs -s\n");
						goto jerr;
					}
#endif
				}

				if(commp != NULL){
					if(commp->c_mode == b[0]){
						fprintf(stderr, "NET_TEST WARN: mode yet active\n");
						continue; /* xxx */
					}
					if(commp->c_lines == NULL){
						fprintf(stderr, "NET_TEST WARN: mode change (%d -> %d), no data yet\n",
							commp->c_mode, b[0]);
#ifdef a_HAVE_TLS
						if(commp->c_mode == a_MODE_TLS){
							fprintf(stderr, "NET_TEST ERROR: and TLS needs data!\n");
							goto jerr;
						}
#endif
						commp->c_mode = b[0];
						continue;
					}
				}

				if(a_verbose)
					fprintf(stderr, "MODE %s\n", (b[0] == a_MODE_CLI ? "CLIENT"
							: (b[0] == a_MODE_SRV ? "SERVER" : "SERVER/TLS")));

				if((cp = (struct a_comm*)calloc(1, sizeof *cp)) == NULL)
					goto jerr;
				if(commp != NULL)
					commp->c_next = cp;
				else
					rv = cp;
				commp = cp;
				cp->c_mode = b[0];
				datp = NULL;
			}else if(i >= a_LINE_BUF - 2 -1){
				fprintf(stderr, "NET_TEST ERROR: line too long\n");
				goto jerr;
			}else if(commp == NULL){
				fprintf(stderr, "NET_TEST ERROR: no leading mode line\n");
				goto jerr;
			}else{
				struct a_dat *dp;

				while(i > 0 && b[i - 1] == '\n')
					--i;
				if(a_verbose){
					b[i] = '\0';
					fprintf(stderr, "  DAT %lu <%s>\n", (unsigned long)i + 2, b);
				}
				b[i++] = '\015';
				b[i++] = '\012';
				b[i] = '\0';

				if((dp = (struct a_dat*)malloc(sizeof(*dp) + i +1)) == NULL)
					goto jerr;
				if(datp != NULL)
					datp->d_next = dp;
				else
					commp->c_lines = dp;
				datp = dp;
				dp->d_next = NULL;
				dp->d_dat = (char*)&dp[1]; /* xxx vararray, sigh */
				dp->d_len = i;
				memcpy(dp->d_dat, b, i +1);
			}
		}
	}

jleave:
	return rv;
jerr:
	rv = NULL; /* exit(3)ing soon, no cleanup */
	goto jleave;
}

static int
a_connection(int sofd, struct a_comm *commp){
	char b[a_LINE_BUF], rb[a_LINE_BUF], *rbp, *bp;
	union {struct a_dat *d; struct a_comm *c;} p;
	fd_set fds;
	struct timeval tv;
	int rv, i, rnl_pend;

	rv = 0;

	while((i = fcntl(sofd, F_GETFL, 0)) == -1){
		if(errno != EINTR)
			goto jleave;
	}
	i |= O_NONBLOCK;
	while(fcntl(sofd, F_SETFL, i) == -1){
		if(errno != EINTR)
			goto jleave;
	}

#ifdef a_HAVE_TLS
	if(a_tls_mode & a_TLS_ALWAYS){
		a_tls_mode &= (a_TLS_ALWAYS | a_TLS_UPGRADE);
		if(!a_tls_setup(sofd))
			goto jleave;
	}
#endif

	for(rnl_pend = 0, rbp = rb; commp != NULL && !a_sigchld_seen;){
		fd_set *rfdp, *wfdp;

		FD_ZERO(&fds);
		FD_SET(sofd, &fds);
		tv.tv_sec = a_TIMEOUT;
		tv.tv_usec = 0;

		rfdp = wfdp = NULL;
#ifdef a_HAVE_TLS
		if(a_tls_mode & (a_TLS_NEED_ACCEPT | a_TLS_NEED_IO_MASK)){
			if(a_tls_mode & a_TLS_NEED_READ)
				rfdp = &fds;
			else if(a_tls_mode & a_TLS_NEED_WRITE)
				wfdp = &fds;
			else
				goto jaccept;
			a_tls_mode &= ~a_TLS_NEED_IO_MASK;
		}else
#endif
				if(commp->c_mode == a_MODE_CLI)
					rfdp = &fds; /* XXX always !! */
		else
			wfdp = &fds;

		switch((i = select(sofd + 1, rfdp, wfdp, NULL, &tv))){
		case 0:
			errno = ETIMEDOUT;
			goto jleave;
		case -1:
			if(((i = errno) != EINTR && i != EAGAIN) || a_sigchld_seen)
				goto jleave;
			break;
		default:{
			struct a_dat *dp;
			ssize_t ssz;

#ifdef a_HAVE_TLS
			if(a_tls_mode & a_TLS_NEED_ACCEPT){
jaccept:
				if(!a_tls_accept())
					goto jleave;
				continue;
			}
#endif

			dp = commp->c_lines;

			/* This test should work even if lines cannot be send / are not
			 * received as a whole, but splitted across several poll events */
			if(commp->c_mode != a_MODE_CLI){
				if(a_verbose)
					fprintf(stderr, "SERV WRITES\n");

				for(;;){
					if(a_verbose)
						fprintf(stderr, "  DAT %lu <%.*s>\n",
							(unsigned long)dp->d_len, (int)(dp->d_len - 2), dp->d_dat);

					if((ssz = a_write(sofd, dp->d_dat, dp->d_len)) == -1){
						if(a_sigchld_seen)
							goto jleave;
						if((i = errno) == EINTR)
							continue;
						if(i == EAGAIN)
							break;
						goto jleave;
					}

					dp->d_dat += ssz;
					if((dp->d_len -= ssz) == 0){
						p.d = dp;
						dp = dp->d_next;
						free(p.d);

						if((commp->c_lines = dp) == NULL){
#ifdef a_HAVE_TLS
							if(commp->c_mode == a_MODE_TLS){
								if(a_verbose)
									fprintf(stderr, "SERV UPGRADES TO TLS\n");
								if(!a_tls_setup(sofd))
									goto jleave;
							}
#endif
							p.c = commp;
							commp = commp->c_next;
							free(p.c);
							break;
						}
					}
				}
			}else{
				if(a_verbose)
					fprintf(stderr, "SERV READS\n");

				for(;;){
					if((ssz = a_read(sofd, b, sizeof(b) -1)) == -1){
						if(a_sigchld_seen)
							goto jleave;
						if((i = errno) == EINTR)
							continue;
						if(i == EAGAIN)
							break;
						goto jleave;
					}
					if(ssz == 0) /* xxx */
						break;
					b[(size_t)ssz] = '\0';

					if(a_verbose){
						size_t j;

						for(j = ssz; (j > 0 && (b[j - 1] == '\015' || b[j - 1] == '\012')); --j){
						}
						fprintf(stderr, "  DAT %lu <%.*s>\n", (unsigned long)ssz, (int)j, b);
					}

					bp = b;
					if(rnl_pend){
						if(*bp != '\012')
							goto jclerr_nl;
						rnl_pend = 0;
						++bp;
						goto jcln_lndone;
					}

					while(*bp != '\0'){
						if(*bp == '\015'){
							/* That it fits is verified below */
							*rbp++ = '\015';
							*rbp++ = '\012';
							*rbp = '\0';

							if(bp[1] != '\012'){
								if(bp[1] != '\0')
									goto jclerr_nl;
								rnl_pend = 1;
								break;
							}else{
								bp += 2;
jcln_lndone:
								if(*bp != '\0')
									memmove(b, bp, strlen(bp) +1);
								else
									b[0] = '\0';
								bp = b;
							}

							dp = commp->c_lines;
							if((size_t)(rbp - rb) != dp->d_len ||
									memcmp(rb, dp->d_dat, dp->d_len)){
								fprintf(stderr,
									"NET_TEST ERROR: client sent invalid data\n"
										"  Want %lu: %sGot %lu: %s",
									(unsigned long)dp->d_len, dp->d_dat,
									(unsigned long)(rbp - rb), rb);
								goto jleave;
							}

							p.d = dp;
							dp = dp->d_next;
							free(p.d);

							if((commp->c_lines = dp) == NULL){
								if(*bp != '\0'){
									fprintf(stderr, "NET_TEST ERROR: excess client data <%s>\n", bp);
									goto jleave;
								}

								p.c = commp;
								commp = commp->c_next;
								free(p.c);

								rbp = rb;
								goto jouter_loop;
							}
							rbp = rb;
						}else if(*bp == '\012'){
jclerr_nl:
							fprintf(stderr, "NET_TEST ERROR: client sent invalid LF <%s>\n", b);
							goto jleave;
						}else{
							*rbp++ = *bp++;
							if((size_t)(rbp - rb) >= a_LINE_BUF - 2 -1){
								fprintf(stderr, "NET_TEST ERROR: client line too long\n");
								goto jleave;
							}
						}
					}
				}
			}
			}break;
		}
jouter_loop:;
	}

	rv = 1;
jleave:
	close(sofd);

	if(commp != NULL){
		i = errno;
		if(rv != 0){
			rv = 0;
			i = 0;
		}

		do{
			while((p.d = commp->c_lines) != NULL){
				commp->c_lines = p.d->d_next;
				free(p.d);
			}

			p.c = commp;
			commp = p.c->c_next;
			free(p.c);
		}while(commp != NULL);

		errno = i;
	}

	return rv;
}

#ifdef a_HAVE_TLS
# ifdef mx_HAVE_XTLS
static int
a_tls_setup(int sofd){
	SSL_CTX *ctxp;
	int rv;

	rv = 1;

	/* */
#  if mx_HAVE_XTLS >= 0x10100
	OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
#  else
	SSL_library_init();
	SSL_load_error_strings();
#  endif

	/* */
	if((ctxp = SSL_CTX_new(mx_XTLS_SERVER_METHOD())) == NULL)
		goto jerr;
	++rv;

	if(SSL_CTX_use_certificate_chain_file(ctxp, a_TLS_PEM_CHAIN) != 1)
		goto jerr;
	++rv;

	if(SSL_CTX_use_PrivateKey_file(ctxp, a_TLS_PEM_KEY, SSL_FILETYPE_PEM) != 1)
		goto jerr;
	++rv;

	if(a_SSL_CTX_load_verify_file(ctxp, a_TLS_PEM_CA) != 1)
		goto jerr;
	++rv;

	SSL_CTX_set_verify(ctxp, (SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE |
		(a_tls_mode & a_TLS_CLIENT_CHECK ? SSL_VERIFY_FAIL_IF_NO_PEER_CERT : 0)), NULL);

	if((a_tls = SSL_new(ctxp)) == NULL || !SSL_set_fd(a_tls, sofd))
		goto jerr;

	a_tls_mode |= a_TLS_NEED_ACCEPT;
	rv = 1;
jleave:
	if(ctxp != NULL){
		SSL_CTX_free(ctxp);
		if(rv != 1 && a_tls != NULL){
			SSL_free(a_tls);
			a_tls = NULL;
		}
	}

	return rv;

jerr:
	fprintf(stderr, "NET_TEST ERROR: server TLS setup, %d\n", rv);
	rv = 0;
	goto jleave;
}

static int
a_tls_accept(void){
	int rv;

	if((rv = SSL_accept(a_tls)) > 0){
		rv = 1;
		a_tls_mode &= ~a_TLS_NEED_ACCEPT;
	}else switch(SSL_get_error(a_tls, rv)){
	case SSL_ERROR_WANT_READ:
		a_tls_mode |= a_TLS_NEED_READ;
		rv = 1;
		break;
	case SSL_ERROR_WANT_WRITE:
		a_tls_mode |= a_TLS_NEED_WRITE;
		rv = 1;
		break;
	default:
		fprintf(stderr, "NET_TEST ERROR: TLS accept failed\n");
		rv = 0;
		break;
	}

	return rv;
}

static ssize_t
a_tls_read(int sofd, void *buf, size_t len){
	ssize_t rv;

	if(a_tls == NULL)
		rv = read(sofd, buf, len);
	else{
		a_tls_mode &= ~a_TLS_NEED_IO_MASK;
		rv = SSL_read(a_tls, buf, len);
		if(rv <= 0){
			int e;

			e = EAGAIN;
			switch(SSL_get_error(a_tls, rv)){
			case SSL_ERROR_WANT_READ: a_tls_mode |= a_TLS_NEED_READ; break;
			case SSL_ERROR_WANT_WRITE: a_tls_mode |= a_TLS_NEED_WRITE; break;
			default: e = EIO; break;
			}

			errno = e;
			rv = -1;
		}
	}

	return rv;
}

static ssize_t
a_tls_write(int sofd, void const *buf, size_t len){
	ssize_t rv;

	if(a_tls == NULL)
		rv = write(sofd, buf, len);
	else{
		a_tls_mode &= ~a_TLS_NEED_IO_MASK;
		rv = SSL_write(a_tls, buf, len);
		if(rv < 0){
			int e;

			e = EAGAIN;
			switch(SSL_get_error(a_tls, rv)){
			case SSL_ERROR_WANT_READ: a_tls_mode = a_TLS_NEED_READ; break;
			case SSL_ERROR_WANT_WRITE: a_tls_mode = a_TLS_NEED_WRITE; break;
			default: e = EIO; break;
			}

			errno = e;
			rv = -1;
		}
	}

	return rv;
}
# endif /* mx_HAVE_XTLS */
#endif /* a_HAVE_TLS */

/* s-itt-mode */
