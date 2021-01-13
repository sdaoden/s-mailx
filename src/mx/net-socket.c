/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Socket operations.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
#define su_FILE net_socket
#define mx_SOURCE
#define mx_SOURCE_NET_SOCKET

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_NET
# ifdef mx_HAVE_NONBLOCKSOCK
/*#  include <sys/types.h>*/
#  include <sys/select.h>
/*#  include <sys/time.h>*/
#  include <arpa/inet.h>
/*#  include <netinet/in.h>*/
/*#  include <errno.h>*/
/*#  include <fcntl.h>*/
/*#  include <stdlib.h>*/
/*#  include <unistd.h>*/

#  include <su/icodec.h>
# endif

#include <sys/socket.h>

#include <netdb.h>
#ifdef mx_HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#include <netinet/in.h>

#ifdef mx_HAVE_XTLS
# include <openssl/err.h>
# include <openssl/rand.h>
# include <openssl/ssl.h>
# include <openssl/x509v3.h>
# include <openssl/x509.h>
#endif

#include <su/cs.h>
#include <su/mem.h>

#include "mx/sigs.h"
#include "mx/url.h"

#include "mx/net-socket.h"
/* TODO fake */
#include "su/code-in.h"

/* If a_netso_sig was zeroed .. test if anything happened */
static void a_netso_onsig(int sig); /* TODO someday, need no more */
static void a_netso_test_sig(void);

/* */
static boole a_netso_open(struct mx_socket *sop, struct mx_url *urlp);

/* .url_flags&URL_TLS_MASK, do what is to be done; closes socket on error! */
#ifdef mx_HAVE_TLS
static int a_netso_open_tls_maybe(struct mx_socket *sop, struct mx_url *urlp);
#endif

/* This closes the socket in case of errors */
static int a_netso_connect(int fd, struct sockaddr *soap, uz soapl);

/* Write to socket fd, restarting on EINTR, unless anything is written */
static long a_netso_xwrite(int fd, char const *data, uz size);

static sigjmp_buf a_netso_actjmp; /* TODO someday, we won't need it no more */
static int a_netso_sig; /* TODO someday, we won't need it no more */

static void
a_netso_onsig(int sig) /* TODO someday, we won't need it no more */
{
   NYD; /* Signal handler */
   if (a_netso_sig == -1) {
      fprintf(n_stderr, _("\nInterrupting this operation may turn "
         "the DNS resolver unusable\n"));
      a_netso_sig = 0;
   } else {
      a_netso_sig = sig;
      siglongjmp(a_netso_actjmp, 1);
   }
}

static void
a_netso_test_sig(void){
   /* May need to bounce the signal to the go.c trampoline (or wherever) */
   if(a_netso_sig != 0){
      sigset_t cset;

      sigemptyset(&cset);
      sigaddset(&cset, a_netso_sig);
      sigprocmask(SIG_UNBLOCK, &cset, NIL);
      n_raise(a_netso_sig);
   }
}

static boole
a_netso_open(struct mx_socket *sop, struct mx_url *urlp) /*TODO sigs;refactor*/
{
#ifdef mx_HAVE_SO_XTIMEO
   struct timeval tv;
#endif
#ifdef mx_HAVE_SO_LINGER
   struct linger li;
#endif
#ifdef mx_HAVE_GETADDRINFO
# ifndef NI_MAXHOST
#  define NI_MAXHOST 1025
# endif
   char hbuf[NI_MAXHOST];
   struct addrinfo hints, *res0 = NULL, *res;
#else
   struct sockaddr_in servaddr;
   struct in_addr **pptr;
   struct hostent *hp;
   struct servent *ep;
#endif
   n_sighdl_t volatile ohup, oint;
   char const * volatile serv;
   int volatile sofd = -1, errval;
   NYD2_IN;

   su_mem_set(sop, 0, sizeof *sop);
   UNINIT(errval, 0);

   serv = (urlp->url_port != NULL) ? urlp->url_port : urlp->url_proto;

   if (n_poption & n_PO_D_V)
      n_err(_("Resolving host %s:%s ... "), urlp->url_host.s, serv);

   /* Signal handling (in respect to a_netso_sig dealing) is heavy, but no
    * healing until v15.0 and i want to end up with that functionality */
   hold_sigs();
   a_netso_sig = 0;
   ohup = safe_signal(SIGHUP, &a_netso_onsig);
   oint = safe_signal(SIGINT, &a_netso_onsig);
   if (sigsetjmp(a_netso_actjmp, 0)) {
jpseudo_jump:
      n_err("%s\n",
         (a_netso_sig == SIGHUP ? _("Hangup") : _("Interrupted")));
      if (sofd >= 0) {
         close(sofd);
         sofd = -1;
      }
      goto jjumped;
   }
   rele_sigs();

#ifdef mx_HAVE_GETADDRINFO
   for (;;) {
      su_mem_set(&hints, 0, sizeof hints);
      hints.ai_socktype = SOCK_STREAM;
      a_netso_sig = -1;
      errval = getaddrinfo(urlp->url_host.s, serv, &hints, &res0);
      if (a_netso_sig != -1) {
         a_netso_sig = SIGINT;
         goto jpseudo_jump;
      }
      a_netso_sig = 0;
      if (errval == 0)
         break;

      if (n_poption & n_PO_D_V)
         n_err(_("failed\n"));
      n_err(_("Lookup of %s:%s failed: %s\n"),
         urlp->url_host.s, serv, gai_strerror(errval));

      /* Error seems to depend on how "smart" the /etc/service code is: is it
       * "able" to state whether the service as such is NONAME or does it only
       * check for the given ai_socktype.. */
      if (errval == EAI_NONAME || errval == EAI_SERVICE) {
         if (serv == urlp->url_proto &&
               (serv = mx_url_servbyname(urlp->url_proto, NIL, NIL)) != NIL &&
               *serv != '\0') {
            n_err(_("  Trying standard protocol port %s\n"), serv);
            n_err(_("  If that succeeds consider including the "
               "port in the URL!\n"));
            continue;
         }
         if (serv != urlp->url_port)
            n_err(_("  Including a port number in the URL may "
               "circumvent this problem\n"));
      }
      ASSERT(sofd == -1);
      errval = 0;
      goto jjumped;
   }
   if(n_poption & n_PO_D_V)
      n_err(_("done\n"));

   for(res = res0; res != NULL && sofd < 0; res = res->ai_next){
      if(n_poption & n_PO_D_V){
         if(getnameinfo(res->ai_addr, res->ai_addrlen, hbuf, sizeof hbuf,
               NIL, 0, NI_NUMERICHOST))
            su_mem_copy(hbuf, "unknown host", sizeof("unknown host"));
         n_err(_("%sConnecting to %s:%s ... "),
               (res == res0 ? n_empty : "\n"), hbuf, serv);
      }

      sofd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
      if(sofd >= 0){
         if((errval = a_netso_connect(sofd, res->ai_addr, res->ai_addrlen)
               ) == su_ERR_NONE){
            u16 p;
            void *vp;

            vp = res->ai_addr;
# ifdef INET6_ADDRSTRLEN
            if(res->ai_family == AF_INET6)
               p = S(struct sockaddr_in6*,vp)->sin6_port;
            else
# endif
                 if(res->ai_family == AF_INET)
               p = R(struct sockaddr_in*,vp)->sin_port;
            else
               p = 0;
            p = ntohs(p);
            urlp->url_portno = p;
            break;
         }
         sofd = -1; /* (FD was closed) */
      }
   }

jjumped:
   if(res0 != NIL){
      freeaddrinfo(res0);
      res0 = NULL;
   }

#else /* mx_HAVE_GETADDRINFO */
   if(serv == urlp->url_proto){
      if((ep = getservbyname(UNCONST(char*,serv), "tcp")) != NULL)
         urlp->url_portno = ntohs(ep->s_port);
      else{
         if(n_poption & n_PO_D_V)
            n_err(_("failed\n"));
         if((serv = mx_url_servbyname(urlp->url_proto, &urlp->url_portno, NIL)
               ) != NIL && *serv != '\0')
            n_err(_("  Unknown service %s, trying protocol standard port %s\n"
                  "  Upon success consider including that port in the URL!\n"),
               urlp->url_proto, serv);
         else{
            n_err(_("  Unknown service: %s, including a port number may "
                  "succeed\n"), urlp->url_proto);
            ASSERT(sofd == -1 && errval == 0);
            goto jjumped;
         }
      }
   }

   a_netso_sig = -1;
   hp = gethostbyname(urlp->url_host.s);
   if (a_netso_sig != -1) {
      a_netso_sig = SIGINT;
      goto jpseudo_jump;
   }
   a_netso_sig = 0;

   if (hp == NULL) {
      char const *emsg;

      if (n_poption & n_PO_D_V)
         n_err(_("failed\n"));
      switch (h_errno) {
      case HOST_NOT_FOUND: emsg = N_("host not found"); break;
      default:
      case TRY_AGAIN:      emsg = N_("(maybe) try again later"); break;
      case NO_RECOVERY:    emsg = N_("non-recoverable server error"); break;
      case NO_DATA:        emsg = N_("valid name without IP address"); break;
      }
      n_err(_("Lookup of %s:%s failed: %s\n"),
         urlp->url_host.s, serv, V_(emsg));
      goto jjumped;
   } else if (n_poption & n_PO_D_V)
      n_err(_("done\n"));

   pptr = (struct in_addr**)hp->h_addr_list;
   if ((sofd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      n_perr(_("could not create socket"), 0);
      ASSERT(sofd == -1 && errval == 0);
      goto jjumped;
   }

   su_mem_set(&servaddr, 0, sizeof servaddr);
   servaddr.sin_family = AF_INET;
   servaddr.sin_port = htons(urlp->url_portno);
   su_mem_copy(&servaddr.sin_addr, *pptr, sizeof(struct in_addr));
   if (n_poption & n_PO_D_V)
      n_err(_("%sConnecting to %s:%d ... "),
         n_empty, inet_ntoa(**pptr), (int)urlp->url_portno);
   if((errval = a_netso_connect(sofd, (struct sockaddr*)&servaddr,
         sizeof servaddr)) != su_ERR_NONE)
      sofd = -1;
jjumped:
#endif /* !mx_HAVE_GETADDRINFO */

   hold_sigs();
   safe_signal(SIGINT, oint);
   safe_signal(SIGHUP, ohup);
   rele_sigs();

   if(sofd < 0){
      if(errval != 0){
         if(!(n_poption & n_PO_D_V))
            n_perr(_("Could not connect(2)"), errval);
         su_err_set_no(errval);
      }
      goto jleave;
   }

   sop->s_fd = sofd;
   if (n_poption & n_PO_D_V)
      n_err(_("connected.\n"));

   /* And the regular timeouts XXX configurable */
#ifdef mx_HAVE_SO_XTIMEO
   tv.tv_sec = 42;
   tv.tv_usec = 0;
   (void)setsockopt(sofd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
   (void)setsockopt(sofd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
#ifdef mx_HAVE_SO_LINGER
   li.l_onoff = 1;
   li.l_linger = 42;
   (void)setsockopt(sofd, SOL_SOCKET, SO_LINGER, &li, sizeof li);
#endif

   /* SSL/TLS upgrade? */
#ifdef mx_HAVE_TLS
   if(urlp->url_flags & mx_URL_TLS_MASK)
      sofd = a_netso_open_tls_maybe(sop, urlp);
#endif

jleave:
   a_netso_test_sig();
   NYD2_OU;
   return (sofd >= 0);
}

#ifdef mx_HAVE_TLS
static int
a_netso_open_tls_maybe(struct mx_socket *sop, struct mx_url *urlp){
   n_sighdl_t volatile ohup, oint;
   NYD2_IN;

   hold_sigs();
   a_netso_sig = 0;

# if defined mx_HAVE_GETADDRINFO && defined SSL_CTRL_SET_TLSEXT_HOSTNAME
      /* TODO the SSL_ def check should NOT be here */
   if(urlp->url_flags & mx_URL_TLS_MASK){
      struct {struct addrinfo hints; struct addrinfo *res0;} x;

      su_mem_set(&x, 0, sizeof x);
      x.hints.ai_family = AF_UNSPEC;
      x.hints.ai_flags = AI_NUMERICHOST;
      if(getaddrinfo(urlp->url_host.s, NIL, &x.hints, &x.res0) == 0)
         freeaddrinfo(x.res0);
      else
         urlp->url_flags |= mx_URL_HOST_IS_NAME;
   }
# endif

   if(urlp->url_flags & mx_URL_TLS_REQUIRED){
      ohup = safe_signal(SIGHUP, &a_netso_onsig);
      oint = safe_signal(SIGINT, &a_netso_onsig);
      if(sigsetjmp(a_netso_actjmp, 0)){
         n_err(_("%s during SSL/TLS handshake\n"),
            (a_netso_sig == SIGHUP ? _("Hangup") : _("Interrupted")));
         goto jsclose;
      }
      rele_sigs();

      if(!n_tls_open(urlp, sop)){
jsclose:
         mx_socket_close(sop);
         ASSERT(sop->s_fd == -1);
      }

      hold_sigs();
      safe_signal(SIGINT, oint);
      safe_signal(SIGHUP, ohup);
   }

   rele_sigs();

   NYD2_OU;
   return sop->s_fd;
}
#endif /* mx_HAVE_TLS */

static int
a_netso_connect(int fd, struct sockaddr *soap, uz soapl){
   int rv;
   NYD_IN;

#ifdef mx_HAVE_NONBLOCKSOCK
   rv = fcntl(fd, F_GETFL, 0);
   if(rv != -1 && !fcntl(fd, F_SETFL, rv | O_NONBLOCK)){
      fd_set fdset;
      struct timeval tv; /* XXX configurable */
      socklen_t sol;
      boole show_progress;
      uz cnt;
      int i, soe;

      /* Always select(2) even if it succeeds right away, since on at least
       * SunOS/Solaris 5.9 SPARC it will cause failures (busy resources) */
      if(connect(fd, soap, soapl) && (i = su_err_no()) != su_ERR_INPROGRESS){
         rv = i;
         goto jerr_noerrno;
      }else{
         show_progress = ((n_poption & n_PO_D_V) ||
               ((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT)));

         FD_ZERO(&fdset);
         FD_SET(fd, &fdset);
         /* C99 */{
            char const *cp;

            if((cp = ok_vlook(socket_connect_timeout)) == NIL ||
                  (su_idec_uz_cp(&cnt, cp, 0, NIL), cnt < 2))
               cnt = 42; /* XXX mx-config.h */

            if(show_progress){
               tv.tv_sec = 2;
               cnt >>= 1;
            }else{
               tv.tv_sec = (long)cnt; /* XXX */
               cnt = 1;
            }
         }

jrewait:
         tv.tv_usec = 0;
         if((soe = select(fd + 1, NIL, &fdset, NIL, &tv)) == 1){
            i = rv;
            sol = sizeof rv;
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &rv, &sol);
            fcntl(fd, F_SETFL, i);
            if(show_progress == TRUM1)
               n_err(" ");
         }else if(soe == 0){
            if(show_progress && --cnt > 0){
               show_progress = TRUM1;
               n_err(".");
               tv.tv_sec = 2;
               goto jrewait;
            }
            n_err(_(" timeout\n"));
            close(fd);
            rv = su_ERR_TIMEDOUT;
         }else
            goto jerr;
      }
   }else
#endif /* mx_HAVE_NONBLOCKSOCK */
         if(!connect(fd, soap, soapl))
      rv = su_ERR_NONE;
   else{
#ifdef mx_HAVE_NONBLOCKSOCK
jerr:
#endif
      rv = su_err_no();
#ifdef mx_HAVE_NONBLOCKSOCK
jerr_noerrno:
#endif
      if(n_poption & n_PO_D_V)
         n_perr(_("connect(2) failed:"), rv);
      close(fd);
   }

   NYD_OU;
   return rv;
}

static long
a_netso_xwrite(int fd, char const *data, uz size)
{
   long rv = -1, wo;
   uz wt = 0;
   NYD_IN;

   do {
      if ((wo = write(fd, data + wt, size - wt)) < 0) {
         if (su_err_no() == su_ERR_INTR)
            continue;
         else
            goto jleave;
      }
      wt += wo;
   } while (wt < size);
   rv = (long)size;
jleave:
   NYD_OU;
   return rv;
}

boole
mx_socket_open(struct mx_socket *sop, struct mx_url *urlp){
   char const *cp;
   boole rv;
   NYD_IN;

   rv = FAL0;

   /* We may have a proxy configured */
   if((cp = xok_vlook(socks_proxy, urlp, OXM_ALL)) == NULL)
      rv = a_netso_open(sop, urlp);
   else{
      u8 pbuf[4 + 1 + 255 + 2];
      uz i;
      char const *emsg;
      struct mx_url url2;

      if(!mx_url_parse(&url2, CPROTO_SOCKS, cp)){
         n_err(_("Failed to parse *socks-proxy*: %s\n"), cp);
         goto jleave;
      }
      if(urlp->url_host.l > 255){
         n_err(_("*socks-proxy*: hostname too long: %s\n"),
            urlp->url_input);
         goto jleave;
      }

      if(n_poption & n_PO_D_V)
         n_err(_("Connecting to *socks-proxy*=%s\n"), cp);
      if(!a_netso_open(sop, &url2)){
         n_err(_("Failed to connect to *socks-proxy*: %s\n"), cp);
         goto jleave;
      }

      /* RFC 1928: version identifier/method selection message */
      pbuf[0] = 0x05; /* VER: protocol version: X'05' */
      pbuf[1] = 0x01; /* NMETHODS: 1 */
      pbuf[2] = 0x00; /* METHOD: X'00' NO AUTHENTICATION REQUIRED */
      if(write(sop->s_fd, pbuf, 3) != 3){
jerrsocks:
         n_perr("*socks-proxy*", 0);
jesocks:
         mx_socket_close(sop);
         goto jleave;
      }

      /* Receive greeting */
      if(read(sop->s_fd, pbuf, 2) != 2)
         goto jerrsocks;
      if(pbuf[0] != 0x05 || pbuf[1] != 0x00){
jesocksreply:
         emsg = N_("unexpected reply\n");
jesocksreplymsg:
         /* I18N: error message and failing URL */
         n_err(_("*socks-proxy*: %s: %s\n"), V_(emsg), cp);
         goto jesocks;
      }

      /* RFC 1928: CONNECT request */
      if(n_poption & n_PO_D_V)
         n_err(_("Through *socks-proxy*, connecting to %s:%s ...\n"),
            urlp->url_host.s,
            (urlp->url_port != NULL ? urlp->url_port : urlp->url_proto));
      pbuf[0] = 0x05; /* VER: protocol version: X'05' */
      pbuf[1] = 0x01; /* CMD: CONNECT X'01' */
      pbuf[2] = 0x00; /* RESERVED */
      pbuf[3] = 0x03; /* ATYP: domain name */
      pbuf[4] = (u8)urlp->url_host.l;
      su_mem_copy(&pbuf[i = 5], urlp->url_host.s, urlp->url_host.l);
      /* C99 */{
         u16 x;

         x = htons(urlp->url_portno);
         su_mem_copy(&pbuf[i += urlp->url_host.l], (u8*)&x, sizeof x);
         i += sizeof x;
      }
      if(write(sop->s_fd, pbuf, i) != (sz)i)
         goto jerrsocks;

      /* Connect result */
      if((i = read(sop->s_fd, pbuf, 4)) != 4)
         goto jerrsocks;
      /* Version 5, reserved must be 0 */
      if(pbuf[0] != 0x05 || pbuf[2] != 0x00)
         goto jesocksreply;
      /* Result */
      switch(pbuf[1]){
      case 0x00: emsg = NULL; break;
      case 0x01: emsg = N_("SOCKS server failure"); break;
      case 0x02: emsg = N_("connection not allowed by ruleset"); break;
      case 0x03: emsg = N_("network unreachable"); break;
      case 0x04: emsg = N_("host unreachable"); break;
      case 0x05: emsg = N_("connection refused"); break;
      case 0x06: emsg = N_("TTL expired"); break;
      case 0x07: emsg = N_("command not supported"); break;
      case 0x08: emsg = N_("address type not supported"); break;
      default: emsg = N_("unknown SOCKS error code"); break;
      }
      if(emsg != NULL)
         goto jesocksreplymsg;

      /* Address type variable; read the BND.PORT with it.
       * This is actually false since RFC 1928 says that the BND.ADDR reply to
       * CONNECT contains the IP address, so only 0x01 and 0x04 are allowed */
      switch(pbuf[3]){
      case 0x01: i = 4; break;
      case 0x03: i = 1; break;
      case 0x04: i = 16; break;
      default: goto jesocksreply;
      }
      i += sizeof(u16);
      if(read(sop->s_fd, pbuf, i) != (sz)i)
         goto jerrsocks;
      if(i == 1 + sizeof(u16)){
         i = pbuf[0];
         if(read(sop->s_fd, pbuf, i) != (sz)i)
            goto jerrsocks;
      }

      /* SSL/TLS upgrade? */
#ifdef mx_HAVE_TLS
      if(urlp->url_flags & mx_URL_TLS_MASK)
         rv = (a_netso_open_tls_maybe(sop, urlp) >= 0);
      else
#endif
         rv = TRU1;
   }
jleave:
   a_netso_test_sig();
   NYD_OU;
   return rv;
}

int
mx_socket_close(struct mx_socket *sop)
{
   int i;
   NYD_IN;

   i = sop->s_fd;
   sop->s_fd = -1;
   /* TODO NOTE: we MUST NOT close the descriptor 0 here...
    * TODO of course this should be handled in a VMAILFS->open() .s_fd=-1,
    * TODO but unfortunately it isn't yet */
   if (i <= 0)
      i = 0;
   else {
      if (sop->s_onclose != NIL)
         (*sop->s_onclose)();
      if (sop->s_wbuf != NIL)
         su_FREE(sop->s_wbuf);
# ifdef mx_HAVE_XTLS
      if (sop->s_use_tls) {
         void *s_tls = sop->s_tls;

         sop->s_tls = NIL;
         sop->s_use_tls = 0;
         if(SSL_shutdown(s_tls) == 0) /* XXX proper error handling;signals! */
            SSL_shutdown(s_tls);
         SSL_free(s_tls);
      }
# endif
      i = close(i);
   }

   NYD_OU;
   return i;
}

enum okay
mx_socket_write(struct mx_socket *sop, char const *data) /* XXX INLINE */
{
   enum okay rv;
   NYD2_IN;

   rv = mx_socket_write1(sop, data, su_cs_len(data), 0);
   NYD2_OU;
   return rv;
}

enum okay
mx_socket_write1(struct mx_socket *sop, char const *data, int size,
   int use_buffer)
{
   enum okay rv = STOP;
   int x;
   NYD2_IN;

   if (use_buffer > 0) {
      int di;

      if (sop->s_wbuf == NULL) {
         sop->s_wbufsize = 4096;
         sop->s_wbuf = su_ALLOC(sop->s_wbufsize);
         sop->s_wbufpos = 0;
      }
      while (sop->s_wbufpos + size > sop->s_wbufsize) {
         di = sop->s_wbufsize - sop->s_wbufpos;
         size -= di;
         if (sop->s_wbufpos > 0) {
            su_mem_copy(&sop->s_wbuf[sop->s_wbufpos], data, di);
            rv = mx_socket_write1(sop, sop->s_wbuf, sop->s_wbufsize, -1);
         } else
            rv = mx_socket_write1(sop, data, sop->s_wbufsize, -1);
         if (rv != OKAY)
            goto jleave;
         data += di;
         sop->s_wbufpos = 0;
      }
      if (size == sop->s_wbufsize) {
         rv = mx_socket_write1(sop, data, sop->s_wbufsize, -1);
         if (rv != OKAY)
            goto jleave;
      } else if (size > 0) {
         su_mem_copy(&sop->s_wbuf[sop->s_wbufpos], data, size);
         sop->s_wbufpos += size;
      }
      rv = OKAY;
      goto jleave;
   } else if (use_buffer == 0 && sop->s_wbuf != NULL && sop->s_wbufpos > 0) {
      x = sop->s_wbufpos;
      sop->s_wbufpos = 0;
      if ((rv = mx_socket_write1(sop, sop->s_wbuf, x, -1)) != OKAY)
         goto jleave;
   }
   if (size == 0) {
      rv = OKAY;
      goto jleave;
   }

# ifdef mx_HAVE_XTLS
   if(sop->s_use_tls){
      int errcnt, err;

      errcnt = 0;
jssl_retry:
      x = SSL_write(sop->s_tls, data, size);
      if(x < 0){
         if((err = su_err_no()) == su_ERR_INTR)
            goto jssl_retry;

         if(++errcnt < 3 && err != su_ERR_WOULDBLOCK){
            switch(SSL_get_error(sop->s_tls, x)){
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
               n_err(_("TLS socket write error, retrying: %s\n"),
                  su_err_doc(err));
               goto jssl_retry;
            }
         }
      }
   } else
# endif /* mx_HAVE_XTLS */
   {
      x = a_netso_xwrite(sop->s_fd, data, size);
   }
   if (x != size) {
      char o[512];

      snprintf(o, sizeof o, "%s write error",
         (sop->s_desc ? sop->s_desc : "socket"));
# ifdef mx_HAVE_XTLS
      if (sop->s_use_tls)
         ssl_gen_err("%s", o);
      else
# endif
         n_perr(o, 0);
      if (x < 0)
         mx_socket_close(sop);
      rv = STOP;
      goto jleave;
   }
   rv = OKAY;
jleave:
   NYD2_OU;
   return rv;
}

int
(mx_socket_getline)(char **line, uz *linesize, uz *linelen,
   struct mx_socket *sop  su_DBG_LOC_ARGS_DECL)
{
   int rv;
   uz lsize;
   char *lp_base, *lp;
   NYD2_IN;

   lsize = *linesize;
   lp_base = *line;
   lp = lp_base;

   if (sop->s_rsz < 0) {
      mx_socket_close(sop);
      rv = sop->s_rsz;
      goto jleave;
   }

   do {
      if (lp_base == NULL || PCMP(lp, >, lp_base + lsize - 128)) {
         uz diff = P2UZ(lp - lp_base);
         *linesize = (lsize += 256); /* XXX magic */
         *line = lp_base = su_MEM_REALLOC_LOCOR(lp_base, lsize,
               su_DBG_LOC_ARGS_ORUSE);
         lp = lp_base + diff;
      }

      if (sop->s_rbufptr == NULL ||
            PCMP(sop->s_rbufptr, >=, sop->s_rbuf + sop->s_rsz)) {
# ifdef mx_HAVE_XTLS
         if(sop->s_use_tls){
            int errcnt, err;

            errcnt = 0;
jssl_retry:
            sop->s_rsz = SSL_read(sop->s_tls, sop->s_rbuf, sizeof sop->s_rbuf);
            if (sop->s_rsz <= 0) {
               if (sop->s_rsz < 0) {
                  char o[512];

                  if((err = su_err_no()) == su_ERR_INTR)
                     goto jssl_retry;

                  if(++errcnt < 3 && err != su_ERR_WOULDBLOCK){
                     switch(SSL_get_error(sop->s_tls, sop->s_rsz)){
                     case SSL_ERROR_WANT_READ:
                     case SSL_ERROR_WANT_WRITE:
                        n_err(_("TLS socket read error, retrying: %s\n"),
                           su_err_doc(err));
                        goto jssl_retry;
                     }
                  }
                  snprintf(o, sizeof o, "%s",
                     (sop->s_desc ?  sop->s_desc : "socket"));
                  ssl_gen_err("%s", o);
               }
               break;
            }
         } else
# endif /* mx_HAVE_XTLS */
         {
jagain:
            sop->s_rsz = read(sop->s_fd, sop->s_rbuf, sizeof sop->s_rbuf);
            if (sop->s_rsz <= 0) {
               if (sop->s_rsz < 0) {
                  char o[512];

                  if (su_err_no() == su_ERR_INTR)
                     goto jagain;
                  snprintf(o, sizeof o, "%s",
                     (sop->s_desc ?  sop->s_desc : "socket"));
                  n_perr(o, 0);
               }
               break;
            }
         }
         sop->s_rbufptr = sop->s_rbuf;
      }
   } while ((*lp++ = *sop->s_rbufptr++) != '\n');
   *lp = '\0';
   lsize = P2UZ(lp - lp_base);

   if (linelen)
      *linelen = lsize;
   rv = (int)lsize;
jleave:
   NYD2_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_NET */
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_NET_SOCKET
/* s-it-mode */
