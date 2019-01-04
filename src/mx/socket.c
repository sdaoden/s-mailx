/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Socket operations.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef su_FILE
#define su_FILE socket
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_SOCKETS
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

/* */
static bool_t a_socket_open(struct sock *sp, struct url *urlp);

/* */
static int a_socket_connect(int fd, struct sockaddr *soap, size_t soapl);

/* Write to socket fd, restarting on EINTR, unless anything is written */
static long a_socket_xwrite(int fd, char const *data, size_t sz);

static sigjmp_buf __sopen_actjmp; /* TODO someday, we won't need it no more */
static int        __sopen_sig; /* TODO someday, we won't need it no more */
static void
__sopen_onsig(int sig) /* TODO someday, we won't need it no more */
{
   n_NYD_X; /* Signal handler */
   if (__sopen_sig == -1) {
      fprintf(n_stderr, _("\nInterrupting this operation may turn "
         "the DNS resolver unusable\n"));
      __sopen_sig = 0;
   } else {
      __sopen_sig = sig;
      siglongjmp(__sopen_actjmp, 1);
   }
}

static bool_t
a_socket_open(struct sock *sp, struct url *urlp) /* TODO sigstuff; refactor */
{
# ifdef mx_HAVE_SO_XTIMEO
   struct timeval tv;
# endif
# ifdef mx_HAVE_SO_LINGER
   struct linger li;
# endif
# ifdef mx_HAVE_GETADDRINFO
#  ifndef NI_MAXHOST
#   define NI_MAXHOST 1025
#  endif
   char hbuf[NI_MAXHOST];
   struct addrinfo hints, *res0 = NULL, *res;
# else
   struct sockaddr_in servaddr;
   struct in_addr **pptr;
   struct hostent *hp;
   struct servent *ep;
# endif
   sighandler_type volatile ohup, oint;
   char const * volatile serv;
   int volatile sofd = -1, errval;
   n_NYD2_IN;

   su_mem_set(sp, 0, sizeof *sp);
   n_UNINIT(errval, 0);

   serv = (urlp->url_port != NULL) ? urlp->url_port : urlp->url_proto;

   if (n_poption & n_PO_D_V)
      n_err(_("Resolving host %s:%s ... "), urlp->url_host.s, serv);

   /* Signal handling (in respect to __sopen_sig dealing) is heavy, but no
    * healing until v15.0 and i want to end up with that functionality */
   hold_sigs();
   __sopen_sig = 0;
   ohup = safe_signal(SIGHUP, &__sopen_onsig);
   oint = safe_signal(SIGINT, &__sopen_onsig);
   if (sigsetjmp(__sopen_actjmp, 0)) {
jpseudo_jump:
      n_err("%s\n",
         (__sopen_sig == SIGHUP ? _("Hangup") : _("Interrupted")));
      if (sofd >= 0) {
         close(sofd);
         sofd = -1;
      }
      goto jjumped;
   }
   rele_sigs();

# ifdef mx_HAVE_GETADDRINFO
   for (;;) {
      su_mem_set(&hints, 0, sizeof hints);
      hints.ai_socktype = SOCK_STREAM;
      __sopen_sig = -1;
      errval = getaddrinfo(urlp->url_host.s, serv, &hints, &res0);
      if (__sopen_sig != -1) {
         __sopen_sig = SIGINT;
         goto jpseudo_jump;
      }
      __sopen_sig = 0;
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
               (serv = n_servbyname(urlp->url_proto, NULL)) != NULL &&
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
      assert(sofd == -1);
      errval = 0;
      goto jjumped;
   }
   if (n_poption & n_PO_D_V)
      n_err(_("done\n"));

   for (res = res0; res != NULL && sofd < 0; res = res->ai_next) {
      if (n_poption & n_PO_D_V) {
         if (getnameinfo(res->ai_addr, res->ai_addrlen, hbuf, sizeof hbuf,
               NULL, 0, NI_NUMERICHOST))
            su_mem_copy(hbuf, "unknown host", sizeof("unknown host"));
         n_err(_("%sConnecting to %s:%s ... "),
               (res == res0 ? n_empty : "\n"), hbuf, serv);
      }

      sofd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
      if(sofd >= 0 &&
            (errval = a_socket_connect(sofd, res->ai_addr, res->ai_addrlen)
               ) != su_ERR_NONE)
         sofd = -1;
   }

jjumped:
   if (res0 != NULL) {
      freeaddrinfo(res0);
      res0 = NULL;
   }

# else /* mx_HAVE_GETADDRINFO */
   if (serv == urlp->url_proto) {
      if ((ep = getservbyname(n_UNCONST(serv), "tcp")) != NULL)
         urlp->url_portno = ntohs(ep->s_port);
      else {
         if (n_poption & n_PO_D_V)
            n_err(_("failed\n"));
         if ((serv = n_servbyname(urlp->url_proto, &urlp->url_portno)) != NULL)
            n_err(_("  Unknown service: %s\n"), urlp->url_proto);
            n_err(_("  Trying standard protocol port %s\n"), serv);
            n_err(_("  If that succeeds consider including the "
               "port in the URL!\n"));
         else {
            n_err(_("  Unknown service: %s\n"), urlp->url_proto);
            n_err(_("  Including a port number in the URL may "
               "circumvent this problem\n"));
            assert(sofd == -1 && errval == 0);
            goto jjumped;
         }
      }
   }

   __sopen_sig = -1;
   hp = gethostbyname(urlp->url_host.s);
   if (__sopen_sig != -1) {
      __sopen_sig = SIGINT;
      goto jpseudo_jump;
   }
   __sopen_sig = 0;

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
      assert(sofd == -1 && errval == 0);
      goto jjumped;
   }

   su_mem_set(&servaddr, 0, sizeof servaddr);
   servaddr.sin_family = AF_INET;
   servaddr.sin_port = htons(urlp->url_portno);
   su_mem_copy(&servaddr.sin_addr, *pptr, sizeof(struct in_addr));
   if (n_poption & n_PO_D_V)
      n_err(_("%sConnecting to %s:%d ... "),
         n_empty, inet_ntoa(**pptr), (int)urlp->url_portno);
   if((errval = a_socket_connect(sofd, (struct sockaddr*)&servaddr,
         sizeof servaddr)) != su_ERR_NONE)
      sofd = -1;
jjumped:
# endif /* !mx_HAVE_GETADDRINFO */

   hold_sigs();
   safe_signal(SIGINT, oint);
   safe_signal(SIGHUP, ohup);
   rele_sigs();

   if (sofd < 0) {
      if (errval != 0) {
         n_perr(_("Could not connect"), errval);
         su_err_set_no(errval);
      }
      goto jleave;
   }

   sp->s_fd = sofd;
   if (n_poption & n_PO_D_V)
      n_err(_("connected.\n"));

   /* And the regular timeouts XXX configurable */
# ifdef mx_HAVE_SO_XTIMEO
   tv.tv_sec = 42;
   tv.tv_usec = 0;
   (void)setsockopt(sofd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
   (void)setsockopt(sofd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
# endif
# ifdef mx_HAVE_SO_LINGER
   li.l_onoff = 1;
   li.l_linger = 42;
   (void)setsockopt(sofd, SOL_SOCKET, SO_LINGER, &li, sizeof li);
# endif

   /* SSL/TLS upgrade? */
# ifdef mx_HAVE_TLS
   hold_sigs();

#  if defined mx_HAVE_GETADDRINFO && defined SSL_CTRL_SET_TLSEXT_HOSTNAME
      /* TODO the SSL_ def check should NOT be here */
   if(urlp->url_flags & n_URL_TLS_MASK){
      su_mem_set(&hints, 0, sizeof hints);
      hints.ai_family = AF_UNSPEC;
      hints.ai_flags = AI_NUMERICHOST;
      res0 = NULL;
      if(getaddrinfo(urlp->url_host.s, NULL, &hints, &res0) == 0)
         freeaddrinfo(res0);
      else
         urlp->url_flags |= n_URL_HOST_IS_NAME;
   }
#  endif

   if (urlp->url_flags & n_URL_TLS_REQUIRED) {
      ohup = safe_signal(SIGHUP, &__sopen_onsig);
      oint = safe_signal(SIGINT, &__sopen_onsig);
      if (sigsetjmp(__sopen_actjmp, 0)) {
         n_err(_("%s during SSL/TLS handshake\n"),
            (__sopen_sig == SIGHUP ? _("Hangup") : _("Interrupted")));
         goto jsclose;
      }
      rele_sigs();

      if(!n_tls_open(urlp, sp)){
jsclose:
         sclose(sp);
         sofd = -1;
      }else if(urlp->url_cproto == CPROTO_CERTINFO)
         sclose(sp);

      hold_sigs();
      safe_signal(SIGINT, oint);
      safe_signal(SIGHUP, ohup);
   }

   rele_sigs();
# endif /* mx_HAVE_TLS */

jleave:
   /* May need to bounce the signal to the go.c trampoline (or wherever) */
   if (__sopen_sig != 0) {
      sigset_t cset;
      sigemptyset(&cset);
      sigaddset(&cset, __sopen_sig);
      sigprocmask(SIG_UNBLOCK, &cset, NULL);
      n_raise(__sopen_sig);
   }
   n_NYD2_OU;
   return (sofd >= 0);
}

static int
a_socket_connect(int fd, struct sockaddr *soap, size_t soapl){
   int rv;
   n_NYD_IN;

#ifdef mx_HAVE_NONBLOCKSOCK
   rv = fcntl(fd, F_GETFL, 0);
   if(rv != -1 && !fcntl(fd, F_SETFL, rv | O_NONBLOCK)){
      fd_set fdset;
      struct timeval tv; /* XXX configurable */
      socklen_t sol;
      bool_t show_progress;
      uiz_t cnt;
      int i, soe;

      if(connect(fd, soap, soapl) && (i = su_err_no()) != su_ERR_INPROGRESS){
         rv = i;
         goto jerr_noerrno;
      }

      show_progress = ((n_poption & n_PO_D_V) ||
               ((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT)));

      FD_ZERO(&fdset);
      FD_SET(fd, &fdset);
      /* C99 */{
         char const *cp;

         if((cp = ok_vlook(socket_connect_timeout)) == NULL ||
               (su_idec_uz_cp(&cnt, cp, 0, NULL), cnt < 2))
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
      if((soe = select(fd + 1, NULL, &fdset, NULL, &tv)) == 1){
         i = rv;
         sol = sizeof rv;
         getsockopt(fd, SOL_SOCKET, SO_ERROR, &rv, &sol);
         fcntl(fd, F_SETFL, i);
         if(show_progress)
            n_err(" ");
      }else if(soe == 0){
         if(show_progress && --cnt > 0){
            n_err(".");
            tv.tv_sec = 2;
            goto jrewait;
         }
         n_err(_(" timeout\n"));
         close(fd);
         rv = su_ERR_TIMEDOUT;
      }else
         goto jerr;
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
      n_perr(_("connect(2) failed:"), rv);
      close(fd);
   }
   n_NYD_OU;
   return rv;
}

static long
a_socket_xwrite(int fd, char const *data, size_t sz)
{
   long rv = -1, wo;
   size_t wt = 0;
   n_NYD_IN;

   do {
      if ((wo = write(fd, data + wt, sz - wt)) < 0) {
         if (su_err_no() == su_ERR_INTR)
            continue;
         else
            goto jleave;
      }
      wt += wo;
   } while (wt < sz);
   rv = (long)sz;
jleave:
   n_NYD_OU;
   return rv;
}

FL int
sclose(struct sock *sp)
{
   int i;
   n_NYD_IN;

   i = sp->s_fd;
   sp->s_fd = -1;
   /* TODO NOTE: we MUST NOT close the descriptor 0 here...
    * TODO of course this should be handled in a VMAILFS->open() .s_fd=-1,
    * TODO but unfortunately it isn't yet */
   if (i <= 0)
      i = 0;
   else {
      if (sp->s_onclose != NULL)
         (*sp->s_onclose)();
      if (sp->s_wbuf != NULL)
         n_free(sp->s_wbuf);
# ifdef mx_HAVE_XTLS
      if (sp->s_use_tls) {
         void *s_tls = sp->s_tls;

         sp->s_tls = NULL;
         sp->s_use_tls = 0;
         while (!SSL_shutdown(s_tls)) /* XXX proper error handling;signals! */
            ;
         SSL_free(s_tls);
      }
# endif
      i = close(i);
   }
   n_NYD_OU;
   return i;
}

FL enum okay
swrite(struct sock *sp, char const *data)
{
   enum okay rv;
   n_NYD2_IN;

   rv = swrite1(sp, data, su_cs_len(data), 0);
   n_NYD2_OU;
   return rv;
}

FL enum okay
swrite1(struct sock *sp, char const *data, int sz, int use_buffer)
{
   enum okay rv = STOP;
   int x;
   n_NYD2_IN;

   if (use_buffer > 0) {
      int di;

      if (sp->s_wbuf == NULL) {
         sp->s_wbufsize = 4096;
         sp->s_wbuf = n_alloc(sp->s_wbufsize);
         sp->s_wbufpos = 0;
      }
      while (sp->s_wbufpos + sz > sp->s_wbufsize) {
         di = sp->s_wbufsize - sp->s_wbufpos;
         sz -= di;
         if (sp->s_wbufpos > 0) {
            su_mem_copy(sp->s_wbuf + sp->s_wbufpos, data, di);
            rv = swrite1(sp, sp->s_wbuf, sp->s_wbufsize, -1);
         } else
            rv = swrite1(sp, data, sp->s_wbufsize, -1);
         if (rv != OKAY)
            goto jleave;
         data += di;
         sp->s_wbufpos = 0;
      }
      if (sz == sp->s_wbufsize) {
         rv = swrite1(sp, data, sp->s_wbufsize, -1);
         if (rv != OKAY)
            goto jleave;
      } else if (sz) {
         su_mem_copy(sp->s_wbuf+ sp->s_wbufpos, data, sz);
         sp->s_wbufpos += sz;
      }
      rv = OKAY;
      goto jleave;
   } else if (use_buffer == 0 && sp->s_wbuf != NULL && sp->s_wbufpos > 0) {
      x = sp->s_wbufpos;
      sp->s_wbufpos = 0;
      if ((rv = swrite1(sp, sp->s_wbuf, x, -1)) != OKAY)
         goto jleave;
   }
   if (sz == 0) {
      rv = OKAY;
      goto jleave;
   }

# ifdef mx_HAVE_XTLS
   if (sp->s_use_tls) {
jssl_retry:
      x = SSL_write(sp->s_tls, data, sz);
      if (x < 0) {
         switch (SSL_get_error(sp->s_tls, x)) {
         case SSL_ERROR_WANT_READ:
         case SSL_ERROR_WANT_WRITE:
            goto jssl_retry;
         }
      }
   } else
# endif
   {
      x = a_socket_xwrite(sp->s_fd, data, sz);
   }
   if (x != sz) {
      char o[512];

      snprintf(o, sizeof o, "%s write error",
         (sp->s_desc ? sp->s_desc : "socket"));
# ifdef mx_HAVE_XTLS
      if (sp->s_use_tls)
         ssl_gen_err("%s", o);
      else
# endif
         n_perr(o, 0);
      if (x < 0)
         sclose(sp);
      rv = STOP;
      goto jleave;
   }
   rv = OKAY;
jleave:
   n_NYD2_OU;
   return rv;
}

FL bool_t
sopen(struct sock *sp, struct url *urlp){
   char const *cp;
   bool_t rv;
   n_NYD_IN;

   rv = FAL0;

   /* We may have a proxy configured */
   if((cp = xok_vlook(socks_proxy, urlp, OXM_ALL)) == NULL)
      rv = a_socket_open(sp, urlp);
   else{
      ui8_t pbuf[4 + 1 + 255 + 2];
      size_t i;
      char const *emsg;
      struct url url2;

      if(!url_parse(&url2, CPROTO_SOCKS, cp)){
         n_err(_("Failed to parse *socks-proxy*: %s\n"), cp);
         goto jleave;
      }
      if(urlp->url_host.l > 255){
         n_err(_("*socks-proxy*: hostname too long: %s\n"),
            urlp->url_input);
         goto jleave;
      }

      if (n_poption & n_PO_D_V)
         n_err(_("Connecting via *socks-proxy* to %s:%s ...\n"),
            urlp->url_host.s,
            (urlp->url_port != NULL ? urlp->url_port : urlp->url_proto));

      if(!a_socket_open(sp, &url2)){
         n_err(_("Failed to connect to *socks-proxy*: %s\n"), cp);
         goto jleave;
      }

      /* RFC 1928: version identifier/method selection message */
      pbuf[0] = 0x05; /* VER: protocol version: X'05' */
      pbuf[1] = 0x01; /* NMETHODS: 1 */
      pbuf[2] = 0x00; /* METHOD: X'00' NO AUTHENTICATION REQUIRED */
      if(write(sp->s_fd, pbuf, 3) != 3){
jerrsocks:
         n_perr("*socks-proxy*", 0);
jesocks:
         sclose(sp);
         goto jleave;
      }

      /* Receive greeting */
      if(read(sp->s_fd, pbuf, 2) != 2)
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
      pbuf[0] = 0x05; /* VER: protocol version: X'05' */
      pbuf[1] = 0x01; /* CMD: CONNECT X'01' */
      pbuf[2] = 0x00; /* RESERVED */
      pbuf[3] = 0x03; /* ATYP: domain name */
      pbuf[4] = (ui8_t)urlp->url_host.l;
      su_mem_copy(&pbuf[i = 5], urlp->url_host.s, urlp->url_host.l);
      /* C99 */{
         ui16_t x;

         x = htons(urlp->url_portno);
         su_mem_copy(&pbuf[i += urlp->url_host.l], (ui8_t*)&x, sizeof x);
         i += sizeof x;
      }
      if(write(sp->s_fd, pbuf, i) != (ssize_t)i)
         goto jerrsocks;

      /* Connect result */
      if((i = read(sp->s_fd, pbuf, 4)) != 4)
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
      i += sizeof(ui16_t);
      if(read(sp->s_fd, pbuf, i) != (ssize_t)i)
         goto jerrsocks;
      if(i == 1 + sizeof(ui16_t)){
         i = pbuf[0];
         if(read(sp->s_fd, pbuf, i) != (ssize_t)i)
            goto jerrsocks;
      }
      rv = TRU1;
   }
jleave:
   n_NYD_OU;
   return rv;
}

FL int
(sgetline)(char **line, size_t *linesize, size_t *linelen, struct sock *sp
   su_DBG_LOC_ARGS_DECL)
{
   int rv;
   size_t lsize;
   char *lp_base, *lp;
   n_NYD2_IN;

   lsize = *linesize;
   lp_base = *line;
   lp = lp_base;

   if (sp->s_rsz < 0) {
      sclose(sp);
      rv = sp->s_rsz;
      goto jleave;
   }

   do {
      if (lp_base == NULL || PTRCMP(lp, >, lp_base + lsize - 128)) {
         size_t diff = PTR2SIZE(lp - lp_base);
         *linesize = (lsize += 256); /* XXX magic */
         *line = lp_base = su_MEM_REALLOC_LOCOR(lp_base, lsize,
               su_DBG_LOC_ARGS_ORUSE);
         lp = lp_base + diff;
      }

      if (sp->s_rbufptr == NULL ||
            PTRCMP(sp->s_rbufptr, >=, sp->s_rbuf + sp->s_rsz)) {
# ifdef mx_HAVE_XTLS
         if (sp->s_use_tls) {
jssl_retry:
            sp->s_rsz = SSL_read(sp->s_tls, sp->s_rbuf, sizeof sp->s_rbuf);
            if (sp->s_rsz <= 0) {
               if (sp->s_rsz < 0) {
                  char o[512];

                  switch(SSL_get_error(sp->s_tls, sp->s_rsz)) {
                  case SSL_ERROR_WANT_READ:
                  case SSL_ERROR_WANT_WRITE:
                     goto jssl_retry;
                  }
                  snprintf(o, sizeof o, "%s",
                     (sp->s_desc ?  sp->s_desc : "socket"));
                  ssl_gen_err("%s", o);
               }
               break;
            }
         } else
# endif
         {
jagain:
            sp->s_rsz = read(sp->s_fd, sp->s_rbuf, sizeof sp->s_rbuf);
            if (sp->s_rsz <= 0) {
               if (sp->s_rsz < 0) {
                  char o[512];

                  if (su_err_no() == su_ERR_INTR)
                     goto jagain;
                  snprintf(o, sizeof o, "%s",
                     (sp->s_desc ?  sp->s_desc : "socket"));
                  n_perr(o, 0);
               }
               break;
            }
         }
         sp->s_rbufptr = sp->s_rbuf;
      }
   } while ((*lp++ = *sp->s_rbufptr++) != '\n');
   *lp = '\0';
   lsize = PTR2SIZE(lp - lp_base);

   if (linelen)
      *linelen = lsize;
   rv = (int)lsize;
jleave:
   n_NYD2_OU;
   return rv;
}
#endif /* mx_HAVE_SOCKETS */

/* s-it-mode */
