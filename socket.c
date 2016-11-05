/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Socket operations.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2016 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef n_FILE
#define n_FILE socket

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

EMPTY_FILE()
#ifdef HAVE_SOCKETS
#include <sys/socket.h>

#include <netdb.h>

#include <netinet/in.h>

#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

#ifdef HAVE_SSL_TLS
# include <openssl/err.h>
# include <openssl/rand.h>
# include <openssl/ssl.h>
# include <openssl/x509v3.h>
# include <openssl/x509.h>
#endif

/* Write to socket fd, restarting on EINTR, unless anything is written */
static long a_sock_xwrite(int fd, char const *data, size_t sz);

static long
a_sock_xwrite(int fd, char const *data, size_t sz)
{
   long rv = -1, wo;
   size_t wt = 0;
   NYD_ENTER;

   do {
      if ((wo = write(fd, data + wt, sz - wt)) < 0) {
         if (errno == EINTR)
            continue;
         else
            goto jleave;
      }
      wt += wo;
   } while (wt < sz);
   rv = (long)sz;
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
sclose(struct sock *sp)
{
   int i;
   NYD_ENTER;

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
         free(sp->s_wbuf);
# ifdef HAVE_SSL_TLS
      if (sp->s_use_ssl) {
         void *s_ssl = sp->s_ssl;

         sp->s_ssl = NULL;
         sp->s_use_ssl = 0;
         while (!SSL_shutdown(s_ssl)) /* XXX proper error handling;signals! */
            ;
         SSL_free(s_ssl);
      }
# endif
      i = close(i);
   }
   NYD_LEAVE;
   return i;
}

FL enum okay
swrite(struct sock *sp, char const *data)
{
   enum okay rv;
   NYD2_ENTER;

   rv = swrite1(sp, data, strlen(data), 0);
   NYD2_LEAVE;
   return rv;
}

FL enum okay
swrite1(struct sock *sp, char const *data, int sz, int use_buffer)
{
   enum okay rv = STOP;
   int x;
   NYD2_ENTER;

   if (use_buffer > 0) {
      int di;

      if (sp->s_wbuf == NULL) {
         sp->s_wbufsize = 4096;
         sp->s_wbuf = smalloc(sp->s_wbufsize);
         sp->s_wbufpos = 0;
      }
      while (sp->s_wbufpos + sz > sp->s_wbufsize) {
         di = sp->s_wbufsize - sp->s_wbufpos;
         sz -= di;
         if (sp->s_wbufpos > 0) {
            memcpy(sp->s_wbuf + sp->s_wbufpos, data, di);
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
         memcpy(sp->s_wbuf+ sp->s_wbufpos, data, sz);
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

# ifdef HAVE_SSL_TLS
   if (sp->s_use_ssl) {
jssl_retry:
      x = SSL_write(sp->s_ssl, data, sz);
      if (x < 0) {
         switch (SSL_get_error(sp->s_ssl, x)) {
         case SSL_ERROR_WANT_READ:
         case SSL_ERROR_WANT_WRITE:
            goto jssl_retry;
         }
      }
   } else
# endif
   {
      x = a_sock_xwrite(sp->s_fd, data, sz);
   }
   if (x != sz) {
      char o[512];
      snprintf(o, sizeof o, "%s write error",
         (sp->s_desc ? sp->s_desc : "socket"));
# ifdef HAVE_SSL_TLS
      if (sp->s_use_ssl)
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
   NYD2_LEAVE;
   return rv;
}

static sigjmp_buf __sopen_actjmp; /* TODO someday, we won't need it no more */
static int        __sopen_sig; /* TODO someday, we won't need it no more */
static void
__sopen_onsig(int sig) /* TODO someday, we won't need it no more */
{
   NYD_X; /* Signal handler */
   if (__sopen_sig == -1) {
      fprintf(stderr, _("\nInterrupting this operation may turn "
         "the DNS resolver unusable\n"));
      __sopen_sig = 0;
   } else {
      __sopen_sig = sig;
      siglongjmp(__sopen_actjmp, 1);
   }
}

FL bool_t
sopen(struct sock *sp, struct url *urlp) /* TODO sighandling; refactor */
{
# ifdef HAVE_SO_SNDTIMEO
   struct timeval tv;
# endif
# ifdef HAVE_SO_LINGER
   struct linger li;
# endif
# ifdef HAVE_GETADDRINFO
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
   NYD_ENTER;

   UNINIT(errval, 0);

   /* Connect timeouts after 30 seconds XXX configurable */
# ifdef HAVE_SO_SNDTIMEO
   tv.tv_sec = 30;
   tv.tv_usec = 0;
# endif
   serv = (urlp->url_port != NULL) ? urlp->url_port : urlp->url_proto;

   if (options & OPT_VERB)
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

# ifdef HAVE_GETADDRINFO
   for (;;) {
      memset(&hints, 0, sizeof hints);
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

      if (options & OPT_VERB)
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
   if (options & OPT_VERB)
      n_err(_("done\n"));

   for (res = res0; res != NULL && sofd < 0; res = res->ai_next) {
      if (options & OPT_VERB) {
         if (getnameinfo(res->ai_addr, res->ai_addrlen, hbuf, sizeof hbuf,
               NULL, 0, NI_NUMERICHOST))
            memcpy(hbuf, "unknown host", sizeof("unknown host"));
         n_err(_("%sConnecting to %s:%s ..."),
               (res == res0 ? "" : "\n"), hbuf, serv);
      }

      sofd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
      if (sofd >= 0) {
#  ifdef HAVE_SO_SNDTIMEO
         (void)setsockopt(sofd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#  endif
         if (connect(sofd, res->ai_addr, res->ai_addrlen)) {
            errval = errno;
            close(sofd);
            sofd = -1;
         }
      }
   }

jjumped:
   if (res0 != NULL) {
      freeaddrinfo(res0);
      res0 = NULL;
   }

# else /* HAVE_GETADDRINFO */
   if (serv == urlp->url_proto) {
      if ((ep = getservbyname(UNCONST(serv), "tcp")) != NULL)
         urlp->url_portno = ntohs(ep->s_port);
      else {
         if (options & OPT_VERB)
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

      if (options & OPT_VERB)
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
   } else if (options & OPT_VERB)
      n_err(_("done\n"));

   pptr = (struct in_addr**)hp->h_addr_list;
   if ((sofd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      n_perr(_("could not create socket"), 0);
      assert(sofd == -1 && errval == 0);
      goto jjumped;
   }

   memset(&servaddr, 0, sizeof servaddr);
   servaddr.sin_family = AF_INET;
   servaddr.sin_port = htons(urlp->url_portno);
   memcpy(&servaddr.sin_addr, *pptr, sizeof(struct in_addr));
   if (options & OPT_VERB)
      n_err(_("%sConnecting to %s:%d ... "),
         "", inet_ntoa(**pptr), (int)urlp->url_portno);
#  ifdef HAVE_SO_SNDTIMEO
   (void)setsockopt(sofd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#  endif
   if (connect(sofd, (struct sockaddr*)&servaddr, sizeof servaddr)) {
      errval = errno;
      close(sofd);
      sofd = -1;
   }
jjumped:
# endif /* !HAVE_GETADDRINFO */

   hold_sigs();
   safe_signal(SIGINT, oint);
   safe_signal(SIGHUP, ohup);
   rele_sigs();

   if (sofd < 0) {
      if (errval != 0) {
         errno = errval;
         n_perr(_("Could not connect"), 0);
      }
      goto jleave;
   }

   if (options & OPT_VERB)
      n_err(_("connected.\n"));

   /* And the regular timeouts XXX configurable */
# ifdef HAVE_SO_SNDTIMEO
   tv.tv_sec = 42;
   tv.tv_usec = 0;
   (void)setsockopt(sofd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
   (void)setsockopt(sofd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
# endif
# ifdef HAVE_SO_LINGER
   li.l_onoff = 1;
   li.l_linger = 42;
   (void)setsockopt(sofd, SOL_SOCKET, SO_LINGER, &li, sizeof li);
# endif

   memset(sp, 0, sizeof *sp);
   sp->s_fd = sofd;

   /* SSL/TLS upgrade? */
# ifdef HAVE_SSL
   if (urlp->url_needs_tls) {
      hold_sigs();
      ohup = safe_signal(SIGHUP, &__sopen_onsig);
      oint = safe_signal(SIGINT, &__sopen_onsig);
      if (sigsetjmp(__sopen_actjmp, 0)) {
         n_err(_("%s during SSL/TLS handshake\n"),
            (__sopen_sig == SIGHUP ? _("Hangup") : _("Interrupted")));
         goto jsclose;
      }
      rele_sigs();

      if (ssl_open(urlp, sp) != OKAY) {
jsclose:
         sclose(sp);
         sofd = -1;
      }

      hold_sigs();
      safe_signal(SIGINT, oint);
      safe_signal(SIGHUP, ohup);
      rele_sigs();
   }
# endif /* HAVE_SSL */

jleave:
   /* May need to bounce the signal to the lex.c trampoline (or wherever) */
   if (__sopen_sig != 0) {
      sigset_t cset;
      sigemptyset(&cset);
      sigaddset(&cset, __sopen_sig);
      sigprocmask(SIG_UNBLOCK, &cset, NULL);
      n_raise(__sopen_sig);
   }
   NYD_LEAVE;
   return (sofd >= 0);
}

FL int
(sgetline)(char **line, size_t *linesize, size_t *linelen, struct sock *sp
   SMALLOC_DEBUG_ARGS)
{
   int rv;
   size_t lsize;
   char *lp_base, *lp;
   NYD2_ENTER;

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
         *line = lp_base = (srealloc)(lp_base, lsize SMALLOC_DEBUG_ARGSCALL);
         lp = lp_base + diff;
      }

      if (sp->s_rbufptr == NULL ||
            PTRCMP(sp->s_rbufptr, >=, sp->s_rbuf + sp->s_rsz)) {
# ifdef HAVE_SSL_TLS
         if (sp->s_use_ssl) {
jssl_retry:
            sp->s_rsz = SSL_read(sp->s_ssl, sp->s_rbuf, sizeof sp->s_rbuf);
            if (sp->s_rsz <= 0) {
               if (sp->s_rsz < 0) {
                  char o[512];
                  switch(SSL_get_error(sp->s_ssl, sp->s_rsz)) {
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
                  if (errno == EINTR)
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
   NYD2_LEAVE;
   return rv;
}
#endif /* HAVE_SOCKETS */

/* s-it-mode */