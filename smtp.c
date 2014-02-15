/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ SMTP client.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 2000
 * Gunnar Ritter.  All rights reserved.
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
 *    This product includes software developed by Gunnar Ritter
 *    and his contributors.
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

#undef NL
#undef LINE
#define NL        "\015\012"
#define LINE(X)   X NL

static sigjmp_buf _smtp_jmp;

static void _smtp_onterm(int signo);

/* Get the SMTP server's answer, expecting val */
static int  _smtp_read(struct sock *sp, char **buf, size_t *bufsize, int val,
               int ign_eof);

/* Talk to a SMTP server */
static int  _smtp_talk(struct name *to, FILE *fi, struct sock *sp,
               char *server, char *uhp, struct header *hp, char const *user,
               char const *password, char const *skinned);

static void
_smtp_onterm(int signo)
{
   NYD_X; /* Signal handler */
   UNUSED(signo);
   siglongjmp(_smtp_jmp, 1);
}

static int
_smtp_read(struct sock *sp, char **buf, size_t *bufsize, int val, int ign_eof)
{
   int rv, len;
   NYD_ENTER;

   do {
      if ((len = sgetline(buf, bufsize, NULL, sp)) < 6) {
         if (len >= 0 && !ign_eof)
            fprintf(stderr, tr(241, "Unexpected EOF on SMTP connection\n"));
            rv = -1;
            goto jleave;
      }
      if (options & OPT_VERBOSE)
         fputs(*buf, stderr);
      switch (**buf) {
      case '1':   rv = 1; break;
      case '2':   rv = 2; break;
      case '3':   rv = 3; break;
      case '4':   rv = 4; break;
      default:    rv = 5; break;
      }
      if (val != rv)
         fprintf(stderr, tr(191, "smtp-server: %s"), *buf);
   } while ((*buf)[3] == '-');
jleave:
   NYD_LEAVE;
   return rv;
}

static int
_smtp_talk(struct name *to, FILE *fi, struct sock *sp, char *xserver,
   char *uhp, struct header *hp, char const *user, char const *password,
   char const *skinned)
{
#define _ANSWER(X, IGNEOF) \
do if (!(options & OPT_DEBUG)) {\
   int y;\
   if ((y = _smtp_read(sp, &buf, &bsize, X, IGNEOF)) != (X) &&\
         (!(IGNEOF) || y != -1))\
      goto jleave;\
} while (0)
#define _OUT(X) \
do {\
   if (options & OPT_VERBOSE)\
      fprintf(stderr, ">>> %s", X);\
   if (!(options & OPT_DEBUG))\
      swrite(sp, X);\
} while (0)

   char o[LINESIZE], *authstr, *cp, *buf = NULL;
   struct str b64;
   struct name *n;
   size_t blen, cnt, bsize = 0;
   enum {AUTH_NONE, AUTH_PLAIN, AUTH_LOGIN, AUTH_CRAM_MD5} auth;
   int inhdr = 1, inbcc = 0, rv = 1;
   NYD_ENTER;
   UNUSED(hp);
   UNUSED(xserver);
   UNUSED(uhp);

   if ((authstr = smtp_auth_var("", skinned)) == NULL)
      auth = (user != NULL && password != NULL) ? AUTH_LOGIN : AUTH_NONE;
   else if (!strcmp(authstr, "plain"))
      auth = AUTH_PLAIN;
   else if (!strcmp(authstr, "login"))
      auth = AUTH_LOGIN;
   else if (!strcmp(authstr, "cram-md5")) {
#ifdef HAVE_MD5
      auth = AUTH_CRAM_MD5;
#else
      fprintf(stderr, tr(277, "No CRAM-MD5 support compiled in.\n"));
      goto jleave;
#endif
   } else {
      fprintf(stderr, tr(274, "Unknown SMTP authentication method: %s\n"),
         authstr);
      goto jleave;
   }

   if (auth != AUTH_NONE && (user == NULL || password == NULL)) {
      fprintf(stderr, tr(275,
         "User and password are necessary for SMTP authentication.\n"));
      goto jleave;
   }
   _ANSWER(2, 0);

#ifdef HAVE_SSL
   if (!sp->s_use_ssl && ok_blook(smtp_use_starttls)) {
      char *server;

      if ((cp = strchr(xserver, ':')) != NULL)
         server = savestrbuf(xserver, PTR2SIZE(cp - xserver));
      else
         server = xserver;
      snprintf(o, sizeof o, LINE("EHLO %s"), nodename(1));
      _OUT(o);
      _ANSWER(2, 0);

      _OUT(LINE("STARTTLS"));
      _ANSWER(2, 0);

      if ((options & OPT_DEBUG) == 0 && ssl_open(server, sp, uhp) != OKAY)
         goto jleave;
   }
#else
   if (ok_blook(smtp_use_starttls)) {
      fprintf(stderr, tr(225, "No SSL support compiled in.\n"));
      goto jleave;
   }
#endif

   if (auth != AUTH_NONE) {
      snprintf(o, sizeof o, LINE("EHLO %s"), nodename(1));
      _OUT(o);
      _ANSWER(2, 0);

      switch (auth) {
      case AUTH_NONE:
#ifndef HAVE_MD5
      case AUTH_CRAM_MD5:
#endif
         /* FALLTRHU
          * Won't happen, but gcc(1) and clang(1) whine without
          * and Coverity whines with; that's a hard one.. */
      case AUTH_LOGIN:
         _OUT(LINE("AUTH LOGIN"));
         _ANSWER(3, 0);

         b64_encode_cp(&b64, user, B64_SALLOC | B64_CRLF);
         _OUT(b64.s);
         _ANSWER(3, 0);

         b64_encode_cp(&b64, password, B64_SALLOC | B64_CRLF);
         _OUT(b64.s);
         _ANSWER(2, 0);
         break;
      case AUTH_PLAIN:
         _OUT(LINE("AUTH PLAIN"));
         _ANSWER(3, 0);

         snprintf(o, sizeof o, "%c%s%c%s", '\0', user, '\0', password);
         b64_encode_buf(&b64, o, strlen(user) + strlen(password) + 2,
            B64_SALLOC | B64_CRLF);
         _OUT(b64.s);
         _ANSWER(2, 0);
         break;
#ifdef HAVE_MD5
      case AUTH_CRAM_MD5:
         _OUT(LINE("AUTH CRAM-MD5"));
         _ANSWER(3, 0);

         for (cp = buf; digitchar(*cp); ++cp)
            ;
         while (blankchar(*cp))
            ++cp;
         cp = cram_md5_string(user, password, cp);
         _OUT(cp);
         _ANSWER(2, 0);
         break;
#endif
      }
   } else {
      snprintf(o, sizeof o, LINE("HELO %s"), nodename(1));
      _OUT(o);
      _ANSWER(2, 0);
   }

   snprintf(o, sizeof o, LINE("MAIL FROM:<%s>"), skinned);
   _OUT(o);
   _ANSWER(2, 0);

   for (n = to; n != NULL; n = n->n_flink) {
      if (!(n->n_type & GDEL)) {
         snprintf(o, sizeof o, LINE("RCPT TO:<%s>"), skinned_name(n));
         _OUT(o);
         _ANSWER(2, 0);
      }
   }

   _OUT(LINE("DATA"));
   _ANSWER(3, 0);
   fflush_rewind(fi);

   cnt = fsize(fi);
   while (fgetline(&buf, &bsize, &cnt, &blen, fi, 1) != NULL) {
      if (inhdr) {
         if (*buf == '\n') {
            inhdr = 0;
            inbcc = 0;
         } else if (inbcc && blankchar(*buf))
            continue;
         /* We know what we have generated first, so do not look for whitespace
          * before the ':' */
         else if (!ascncasecmp(buf, "bcc: ", 5)) {
            inbcc = 1;
            continue;
         } else
            inbcc = 0;
      }

      if (*buf == '.') {
         if (options & OPT_DEBUG)
            putc('.', stderr);
         else
            swrite1(sp, ".", 1, 1);
      }
      if (options & OPT_DEBUG) {
         fprintf(stderr, ">>> %s", buf);
         continue;
      }
      buf[blen - 1] = NL[0];
      buf[blen] = NL[1];
      swrite1(sp, buf, blen + 1, 1);
   }
   _OUT(LINE("."));
   _ANSWER(2, 0);

   _OUT(LINE("QUIT"));
   _ANSWER(2, 1);
   rv = 0;
jleave:
   if (buf != NULL)
      free(buf);
   NYD_LEAVE;
   return rv;
#undef _OUT
#undef _ANSWER
#undef __ANSWER
}

FL char *
smtp_auth_var(char const *atype, char const *addr) /* FIXME GENERIC */
{
   size_t tl, al, len;
   char *var, *cp;
   NYD_ENTER;

   tl = strlen(atype);
   al = strlen(addr);
   len = tl + al + 10 +1;
   var = ac_alloc(len);

   /* Try a 'user@host', i.e., address specific version first */
   (void)snprintf(var, len, "smtp-auth%s-%s", atype, addr);
   if ((cp = vok_vlook(var)) == NULL) {
      snprintf(var, len, "smtp-auth%s", atype);
      cp = vok_vlook(var);
   }
   if (cp != NULL)
      cp = savestr(cp);

   ac_free(var);
   NYD_LEAVE;
   return cp;
}

FL int
smtp_mta(char *volatile server, struct name *volatile to, FILE *fi,
   struct header *hp, char const *user, char const *password,
   char const *skinned)
{
   struct sock so;
   sighandler_type volatile saveterm;
   int use_ssl, rv = 1;
   NYD_ENTER;

   memset(&so, 0, sizeof so);

   saveterm = safe_signal(SIGTERM, SIG_IGN);
   if (sigsetjmp(_smtp_jmp, 1))
      goto jleave;
   if (saveterm != SIG_IGN)
      safe_signal(SIGTERM, &_smtp_onterm);

   if (!strncmp(server, "smtp://", 7)) {
      use_ssl = 0;
      server += 7;
#ifdef HAVE_SSL
   } else if (!strncmp(server, "smtps://", 8)) {
      use_ssl = 1;
      server += 8;
#endif
   } else
      use_ssl = 0;

   if (!(options & OPT_DEBUG) && sopen(server, &so, use_ssl, server,
         (use_ssl ? "smtps" : "smtp")) != OKAY)
      goto jleave;

   so.s_desc = "SMTP";
   rv = _smtp_talk(to, fi, &so, server, server, hp, user, password, skinned);

   if (!(options & OPT_DEBUG))
      sclose(&so);
jleave:
   safe_signal(SIGTERM, saveterm);
   NYD_LEAVE;
   return rv;
}

#undef LINE
#undef NL
#endif /* HAVE_SMTP */

/* vim:set fenc=utf-8:s-it-mode */
