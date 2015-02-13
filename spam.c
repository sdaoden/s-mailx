/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Spam related facilities.
 *
 * Copyright (c) 2013 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

EMPTY_FILE(spam)
#ifdef HAVE_SPAM
#ifdef HAVE_SPAM_SPAMD
# include <sys/socket.h>
# include <sys/un.h>
#endif

#ifdef HAVE_SPAM_SPAMD
  /* This is rather arbitrary chosen to swallow an entire CHECK/TELL response */
# if BUFFER_SIZE < 1024
#  error SpamAssassin interaction BUFFER_SIZE constraints are not matched
# endif

# define SPAMD_IDENT "SPAMC/1.5"

# ifndef SUN_LEN
#  define SUN_LEN(SUP) \
        (sizeof(*(SUP)) - sizeof((SUP)->sun_path) + strlen((SUP)->sun_path))
# endif
#endif

enum spam_action {
   _SPAM_RATE,
   _SPAM_HAM,
   _SPAM_SPAM,
   _SPAM_FORGET
};

#ifdef HAVE_SPAM_SPAMC
struct spam_spamc {
   char const        *spamc_cmd_s;
   char const        *spamc_cmd_a[16];
   /* TODO This codebase jumps around and uses "stacks" of signal handling;
    * TODO until some later time we have to play the same game */
   sighandler_type   spamc_otstp;
   sighandler_type   spamc_ottin;
   sighandler_type   spamc_ottou;
   sighandler_type   spamc_ohup;
   sighandler_type   spamc_opipe;
   sighandler_type   spamc_oint;
   sighandler_type   spamc_oquit;
};
#endif

#ifdef HAVE_SPAM_SPAMD
struct spam_spamd {
   struct str           spamd_user;
   sighandler_type      spamd_otstp;
   sighandler_type      spamd_ottin;
   sighandler_type      spamd_ottou;
   sighandler_type      spamd_ohup;
   sighandler_type      spamd_opipe;
   sighandler_type      spamd_oint;
   sighandler_type      spamd_oquit;
   struct sockaddr_un   spamd_sun;
};
#endif

struct spam_vc {
   enum spam_action     vc_action;
   bool_t               vc_verbose;    /* Verbose output */
   bool_t               vc_progress;   /* "Progress meter" (mutual verbose) */
   ui8_t                __pad[2];
   bool_t               (*vc_act)(struct spam_vc *);
   char                 *vc_buffer;    /* I/O buffer, BUFFER_SIZE bytes */
   size_t               vc_mno;        /* Current message number */
   struct message       *vc_mp;        /* Current message */
   FILE                 *vc_ifp;       /* Input stream on .vc_mp */
   union {
#ifdef HAVE_SPAM_SPAMC
      struct spam_spamc c;
#endif
#ifdef HAVE_SPAM_SPAMD
      struct spam_spamd d;
#endif
   }                    vc_type;
   char const           *vc_esep;      /* Error separator for progress mode */
};

/* Indices according to enum spam_action */
static char const _spam_comms[][16] = {
   "spamrate", "spamham", "spamspam", "spamforget"
};

/* Shared action setup */
static bool_t  _spam_action(enum spam_action sa, int *ip);

/* *spam-interface*=spamc: initialize, communicate */
#ifdef HAVE_SPAM_SPAMC
static bool_t  _spamc_setup(struct spam_vc *vcp);
static bool_t  _spamc_interact(struct spam_vc *vcp);
#endif

/* *spam-interface*=spamd: initialize, communicate */
#ifdef HAVE_SPAM_SPAMD
static bool_t  _spamd_setup(struct spam_vc *vcp);
static bool_t  _spamd_interact(struct spam_vc *vcp);
#endif

/* Convert a 2[.x]/whatever spam rate into message.m_spamscore */
static void    _spam_rate2score(struct spam_vc *vcp, char *buf);

static bool_t
_spam_action(enum spam_action sa, int *ip)
{
   struct spam_vc vc;
   size_t maxsize;
   char const *cp;
   bool_t ok = FAL0;
   NYD_ENTER;

   memset(&vc, 0, sizeof vc);
   vc.vc_action = sa;
   vc.vc_verbose = ((options & OPT_VERB) != 0);
   vc.vc_progress = (!vc.vc_verbose && ((options & OPT_INTERACTIVE) != 0));
   vc.vc_esep = vc.vc_progress ? "\n" : "";

   /* Check and setup the desired spam interface */
   if ((cp = ok_vlook(spam_interface)) == NULL) {
      fprintf(stderr, _("`%s': no *spam-interface* set\n"), _spam_comms[sa]);
      goto jleave;
#ifdef HAVE_SPAM_SPAMC
   } else if (!asccasecmp(cp, "spamc")) {
       if (!_spamc_setup(&vc))
         goto jleave;
#endif
#ifdef HAVE_SPAM_SPAMD
   } else if (!asccasecmp(cp, "spamd")) {
      if (!_spamd_setup(&vc))
         goto jleave;
#endif
   } else {
      fprintf(stderr, _("`%s': unknown / unsupported *spam-interface*: `%s'\n"),
         _spam_comms[sa], cp);
      goto jleave;
   }

   /* *spam-maxsize* we do handle ourselfs instead */
   maxsize = 0;
   if ((cp = ok_vlook(spam_maxsize)) != NULL)
      maxsize = (size_t)strtol(cp, NULL, 10); /* TODO strtol */
   if (maxsize <= 0)
      maxsize = SPAM_MAXSIZE;

   /* Finally get an I/O buffer */
   vc.vc_buffer = salloc(BUFFER_SIZE);

   if (vc.vc_progress) {
      fprintf(stdout, "%s: ", _spam_comms[sa]);
      fflush(stdout);
   }
   for (ok = TRU1; *ip != 0; ++ip) {
      vc.vc_mno = (size_t)*ip;
      vc.vc_mp = message + vc.vc_mno - 1;
      if (sa == _SPAM_RATE)
         vc.vc_mp->m_spamscore = 0;

      if (vc.vc_mp->m_size > maxsize) {
         if (vc.vc_verbose)
            fprintf(stderr,
               _("`%s': message %" PRIuZ " exceeds maxsize (%"
                  PRIuZ " > %" PRIuZ "), skip\n"),
               _spam_comms[sa], vc.vc_mno, (size_t)vc.vc_mp->m_size,
               maxsize);
         else if (vc.vc_progress) {
            putc('!', stdout);
            fflush(stdout);
         }
      } else {
         if (vc.vc_verbose)
            fprintf(stderr, _("`%s': message %" PRIuZ "\n"),
               _spam_comms[sa], vc.vc_mno);
         else if (vc.vc_progress) {
            putc('.', stdout);
            fflush(stdout);
         }

         setdot(vc.vc_mp);
         if ((vc.vc_ifp = setinput(&mb, vc.vc_mp, NEED_BODY)) == NULL) {
            fprintf(stderr,
               _("%s`%s': cannot load message %" PRIuZ ": %s\n"),
               vc.vc_esep, _spam_comms[sa], vc.vc_mno, strerror(errno));
            ok = FAL0;
            break;
         }

         if (!(ok = (*vc.vc_act)(&vc)))
            break;
      }
   }
   if (ok && vc.vc_progress) {
      fputs(" done\n", stdout);
      fflush(stdout);
   }

jleave:
   NYD_LEAVE;
   return !ok;
}

#ifdef HAVE_SPAM_SPAMC
static bool_t
_spamc_setup(struct spam_vc *vcp)
{
   struct str str;
   char const **args, *cp;
   bool_t rv = FAL0;
   NYD2_ENTER;

   args = vcp->vc_type.c.spamc_cmd_a;

   if ((cp = ok_vlook(spam_command)) == NULL) {
# ifdef SPAM_SPAMC_PATH
      cp = SPAM_SPAMC_PATH;
# else
      fprintf(stderr, _("`%s': *spam-command* is not set\n"),
         _spam_comms[vcp->vc_action]);
      goto jleave;
# endif
   }
   *args++ = cp;

   switch (vcp->vc_action) {
   case _SPAM_RATE:
      *args = "-c";
      break;
   case _SPAM_HAM:
      args[1] = "ham";
      goto jlearn;
   case _SPAM_SPAM:
      args[1] = "spam";
      goto jlearn;
   case _SPAM_FORGET:
      args[1] = "forget";
jlearn:
      *args = "-L";
      ++args;
      break;
   }
   ++args;

   if ((cp = ok_vlook(spam_socket)) != NULL) {
      *args++ = "-U";
      *args++ = cp;
   } else {
      if ((cp = ok_vlook(spam_host)) != NULL) {
         *args++ = "-d";
         *args++ = cp;
      }
      if ((cp = ok_vlook(spam_port)) != NULL) {
         *args++ = "-p";
         *args++ = cp;
      }
   }

   *args++ = "-l"; /* --log-to-stderr */

   if ((cp = ok_vlook(spam_user)) != NULL) {
      if (*cp == '\0')
         cp = myname;
      *args++ = "-u";
      *args++ = cp;
   }

   *args = NULL;
   vcp->vc_type.c.spamc_cmd_s = str_concat_cpa(&str,
         vcp->vc_type.c.spamc_cmd_a, " ")->s;
   if (vcp->vc_verbose)
      fprintf(stderr, "spamc(1) via <%s>\n", vcp->vc_type.c.spamc_cmd_s);

   vcp->vc_act = &_spamc_interact;
   rv = TRU1;
# ifndef SPAM_SPAMC_PATH
jleave:
# endif
   NYD2_LEAVE;
   return rv;
}

static sigjmp_buf __spamc_actjmp; /* TODO someday, we won't need it no more */
static int        __spamc_sig; /* TODO someday, we won't need it no more */
static void
__spamc_onsig(int sig) /* TODO someday, we won't need it no more */
{
   NYD_X; /* Signal handler */
   __spamc_sig = sig;
   siglongjmp(__spamc_actjmp, 1);
}

static bool_t
_spamc_interact(struct spam_vc *vcp)
{
   int p2c[2], c2p[2];
   sigset_t cset;
   size_t size;
   pid_t volatile pid;
   enum {
      _NONE    = 0,
      _SIGHOLD = 1<<0,
      _P2C_0   = 1<<1,
      _P2C_1   = 1<<2,
      _P2C     = _P2C_0 | _P2C_1,
      _C2P_0   = 1<<3,
      _C2P_1   = 1<<4,
      _C2P     = _C2P_0 | _C2P_1,
      _JUMPED  = 1<<5,
      _RUNNING = 1<<6,
      _GOODRUN = 1<<7,
      _ERRORS  = 1<<8
   } state = _NONE;
   NYD2_ENTER;

   /* TODO Avoid that we jump away; yet necessary signal mess */
   vcp->vc_type.c.spamc_otstp = safe_signal(SIGTSTP, SIG_DFL);
   vcp->vc_type.c.spamc_ottin = safe_signal(SIGTTIN, SIG_DFL);
   vcp->vc_type.c.spamc_ottou = safe_signal(SIGTTOU, SIG_DFL);
   vcp->vc_type.c.spamc_opipe = safe_signal(SIGPIPE, SIG_IGN);
   hold_sigs();
   state |= _SIGHOLD;
   vcp->vc_type.c.spamc_ohup = safe_signal(SIGHUP, &__spamc_onsig);
   vcp->vc_type.c.spamc_oint = safe_signal(SIGINT, &__spamc_onsig);
   vcp->vc_type.c.spamc_oquit = safe_signal(SIGQUIT, &__spamc_onsig);
   /* Keep sigs blocked */
   pid = 0; /* cc uninit */

   if (!pipe_cloexec(p2c)) {
      fprintf(stderr, _("%s`%s': cannot create parent pipe: %s\n"),
         vcp->vc_esep, _spam_comms[vcp->vc_action], strerror(errno));
      goto jtail;
   }
   state |= _P2C;

   if (!pipe_cloexec(c2p)) {
      fprintf(stderr, _("%s`%s': cannot create child pipe: %s\n"),
         vcp->vc_esep, _spam_comms[vcp->vc_action], strerror(errno));
      goto jtail;
   }
   state |= _C2P;

   if (sigsetjmp(__spamc_actjmp, 1)) {
      if (*vcp->vc_esep != '\0')
         fputs(vcp->vc_esep, stderr);
      state |= _JUMPED;
      goto jtail;
   }
   rele_sigs();
   state &= ~_SIGHOLD;

   sigemptyset(&cset);
   if ((pid = start_command(vcp->vc_type.c.spamc_cmd_s, &cset, p2c[0], c2p[1],
            NULL, NULL, NULL, NULL)) < 0) {
      state |= _ERRORS;
      goto jtail;
   }
   state |= _RUNNING;
   close(p2c[0]);
   state &= ~_P2C_0;

   /* Yes, we could sendmp(SEND_MBOX), but simply passing through the MBOX
    * content does the same in effect, but is much more efficient.
    * NOTE: this may mean we pass a message without From_ line! */
   for (size = vcp->vc_mp->m_size; size > 0;) {
      size_t i = fread(vcp->vc_buffer, 1, MIN(size, BUFFER_SIZE), vcp->vc_ifp);
      if (i == 0) {
         if (ferror(vcp->vc_ifp))
            state |= _ERRORS;
         break;
      }
      size -= i;
      if (i != (size_t)write(p2c[1], vcp->vc_buffer, i)) {
         state |= _ERRORS;
         break;
      }
   }

jtail:
   /* TODO In what follows you see a lot of races; these can't be helped without
    * TODO atomic compare-and-swap -- WE COULD ALSO BLOCK ANYTHING FOR A WHILE*/
   if (state & _SIGHOLD) {
      state &= ~_SIGHOLD;
      rele_sigs();
   }

   if (state & _P2C_0) {
      state &= ~_P2C_0;
      close(p2c[0]);
   }
   if (state & _C2P_1) {
      state &= ~_C2P_1;
      close(c2p[1]);
   }
   /* Close the write end, so that spamc(1) goes */
   if (state & _P2C_1) {
      state &= ~_P2C_1;
      close(p2c[1]);
   }

   if (state & _RUNNING) {
      state &= ~_RUNNING;
      if (wait_child(pid, NULL))
         state |= _GOODRUN;
   }

   /* XXX This only works because spamc(1) follows the clear protocol (1) read
    * XXX everything until EOF on input, then (2) work, then (3) output
    * XXX a single result line; otherwise we could deadlock here, but since
    * TODO this is rather intermediate, go with it */
   if (!(state & _ERRORS) &&
         vcp->vc_action == _SPAM_RATE && !(state & (_JUMPED | _ERRORS))) {
      ssize_t i = read(c2p[0], vcp->vc_buffer, BUFFER_SIZE - 1);
      if (i > 0) {
         vcp->vc_buffer[i] = '\0';
         _spam_rate2score(vcp, vcp->vc_buffer);
      } else if (i != 0)
         state |= _ERRORS;
   }

   if (state & _C2P_0) {
      state &= ~_C2P_0;
      close(c2p[0]);
   }

   if (vcp->vc_action == _SPAM_RATE) {
      switch (state & (_JUMPED | _GOODRUN | _ERRORS)) {
      case _GOODRUN:
         vcp->vc_mp->m_flag &= ~MSPAM;
         break;
      case 0:
         vcp->vc_mp->m_flag |= MSPAM;
      default:
         break;
      }
   } else {
      if (state & (_JUMPED | _ERRORS))
         /* xxx print message? */;
      else if (vcp->vc_action == _SPAM_SPAM)
         vcp->vc_mp->m_flag |= MSPAM;
      else if (vcp->vc_action == _SPAM_HAM)
         vcp->vc_mp->m_flag &= ~MSPAM;
   }

   safe_signal(SIGQUIT, vcp->vc_type.c.spamc_oquit);
   safe_signal(SIGINT, vcp->vc_type.c.spamc_oint);
   safe_signal(SIGHUP, vcp->vc_type.c.spamc_ohup);
   safe_signal(SIGPIPE, vcp->vc_type.c.spamc_opipe);
   safe_signal(SIGTSTP, vcp->vc_type.c.spamc_otstp);
   safe_signal(SIGTTIN, vcp->vc_type.c.spamc_ottin);
   safe_signal(SIGTTOU, vcp->vc_type.c.spamc_ottou);

   NYD2_LEAVE;
   if (state & _JUMPED) {
      sigemptyset(&cset);
      sigaddset(&cset, __spamc_sig);
      sigprocmask(SIG_UNBLOCK, &cset, NULL);
      kill(0, __spamc_sig);
   }
   return !(state & _ERRORS);
}
#endif /* HAVE_SPAM_SPAMC */

#ifdef HAVE_SPAM_SPAMD
static bool_t
_spamd_setup(struct spam_vc *vcp)
{
   char const *cp;
   size_t l;
   bool_t rv = FAL0;
   NYD2_ENTER;

   if ((cp = ok_vlook(spam_user)) != NULL) {
      if (*cp == '\0')
         cp = UNCONST(myname);
      vcp->vc_type.d.spamd_user.l = strlen(vcp->vc_type.d.spamd_user.s = cp);
   }

   if ((cp = ok_vlook(spam_socket)) == NULL) {
      fprintf(stderr, _("`%s': required *spam-socket* is not set\n"),
         _spam_comms[vcp->vc_action]);
      goto jleave;
   }

   if ((l = strlen(cp) +1) >= sizeof(vcp->vc_type.d.spamd_sun.sun_path)) {
      fprintf(stderr, _("`%s': *spam-socket* too long: `%s'\n"),
         _spam_comms[vcp->vc_action], cp);
      goto jleave;
   }
   vcp->vc_type.d.spamd_sun.sun_family = AF_UNIX;
   memcpy(vcp->vc_type.d.spamd_sun.sun_path, cp, l);

   vcp->vc_act = &_spamd_interact;
   rv = TRU1;
jleave:
   NYD2_LEAVE;
   return rv;
}

static sigjmp_buf __spamd_actjmp; /* TODO someday, we won't need it no more */
static int        __spamd_sig; /* TODO someday, we won't need it no more */
static void
__spamd_onsig(int sig) /* TODO someday, we won't need it no more */
{
   NYD_X; /* Signal handler */
   __spamd_sig = sig;
   siglongjmp(__spamd_actjmp, 1);
}

static bool_t
_spamd_interact(struct spam_vc *vcp)
{
   size_t size, i;
   char *lp, *cp, * volatile headbuf = NULL;
   int volatile dsfd = -1;
   bool_t volatile rv = FAL0;
   NYD2_ENTER;

   __spamd_sig = 0;
   hold_sigs();
   vcp->vc_type.d.spamd_otstp = safe_signal(SIGTSTP, SIG_DFL);
   vcp->vc_type.d.spamd_ottin = safe_signal(SIGTTIN, SIG_DFL);
   vcp->vc_type.d.spamd_ottou = safe_signal(SIGTTOU, SIG_DFL);
   vcp->vc_type.d.spamd_opipe = safe_signal(SIGPIPE, SIG_IGN);
   vcp->vc_type.d.spamd_ohup = safe_signal(SIGHUP, &__spamd_onsig);
   vcp->vc_type.d.spamd_oint = safe_signal(SIGINT, &__spamd_onsig);
   vcp->vc_type.d.spamd_oquit = safe_signal(SIGQUIT, &__spamd_onsig);
   if (sigsetjmp(__spamd_actjmp, 1)) {
      if (*vcp->vc_esep != '\0')
         fputs(vcp->vc_esep, stderr);
      goto jleave;
   }
   rele_sigs();

   if ((dsfd = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
      fprintf(stderr, _("%s`%s': can't create unix(4) socket: %s\n"),
         vcp->vc_esep, _spam_comms[vcp->vc_action], strerror(errno));
      goto jleave;
   }

   if (connect(dsfd, (struct sockaddr*)&vcp->vc_type.d.spamd_sun,
         SUN_LEN(&vcp->vc_type.d.spamd_sun)) == -1) {
      fprintf(stderr, _("%s`%s': can't connect to *spam-socket*: %s\n"),
         vcp->vc_esep, _spam_comms[vcp->vc_action], strerror(errno));
      close(dsfd);
      goto jleave;
   }

   /* The command header, finalized with an empty line.
    * This needs to be written in a single write(2)! */
# define _X(X) do {memcpy(lp, X, sizeof(X) -1); lp += sizeof(X) -1;} while (0)

   i = ((cp = vcp->vc_type.d.spamd_user.s) != NULL)
         ? vcp->vc_type.d.spamd_user.l : 0;
   lp = headbuf = ac_alloc(
         sizeof(NETLINE("A_VERY_LONG_COMMAND " SPAMD_IDENT)) +
         sizeof(NETLINE("Content-length: 9223372036854775807")) +
         ((cp != NULL) ? sizeof("User: ") + i + sizeof(NETNL) : 0) +
         sizeof(NETLINE("Message-class: spam")) +
         sizeof(NETLINE("Set: local")) +
         sizeof(NETLINE("Remove: local")) +
         sizeof(NETNL) /*+1*/);

   switch (vcp->vc_action) {
   case _SPAM_RATE:
      _X(NETLINE("CHECK " SPAMD_IDENT));
      break;
   case _SPAM_HAM:
   case _SPAM_SPAM:
   case _SPAM_FORGET:
      _X(NETLINE("TELL " SPAMD_IDENT));
      break;
   }

   lp += snprintf(lp, 0x7FFF, NETLINE("Content-length: %" PRIuZ),
         (size_t)vcp->vc_mp->m_size);

   if (cp != NULL) {
      _X("User: ");
      memcpy(lp, cp, i);
      lp += i;
      _X(NETNL);
   }

   switch (vcp->vc_action) {
   case _SPAM_RATE:
      _X(NETNL);
      break;
   case _SPAM_HAM:
      _X(NETLINE("Message-class: ham")
         NETLINE("Set: local")
         NETNL);
      break;
   case _SPAM_SPAM:
      _X(NETLINE("Message-class: spam")
         NETLINE("Set: local")
         NETNL);
      break;
   case _SPAM_FORGET:
      if (vcp->vc_mp->m_flag & MSPAM)
         _X(NETLINE("Message-class: spam"));
      else
         _X(NETLINE("Message-class: ham"));
      _X(NETLINE("Remove: local")
         NETNL);
      break;
   }
# undef _X

   i = PTR2SIZE(lp - headbuf);
   if (options & OPT_VERBVERB)
      fprintf(stderr, ">>> %.*s <<<\n", (int)i, headbuf);
   if (i != (size_t)write(dsfd, headbuf, i))
      goto jeso;

   /* Then simply pass through the message "as-is" */
   for (size = vcp->vc_mp->m_size; size > 0;) {
      i = fread(vcp->vc_buffer, sizeof *vcp->vc_buffer,
            MIN(size, BUFFER_SIZE), vcp->vc_ifp);
      if (i == 0) {
         if (ferror(vcp->vc_ifp))
            goto jeso;
         break;
      }
      size -= i;

      if (i != (size_t)write(dsfd, vcp->vc_buffer, i)) {
jeso:
         fprintf(stderr, _("%s`%s': I/O on *spam-socket* failed: %s\n"),
            vcp->vc_esep, _spam_comms[vcp->vc_action], strerror(errno));
         goto jleave;
      }
   }

   /* Shutdown our write end */
   shutdown(dsfd, SHUT_WR);

   /* Be aware on goto: i will be a line counter after this loop! */
   for (size = 0, i = BUFFER_SIZE -1;;) {
      ssize_t j = read(dsfd, vcp->vc_buffer + size, i);
      if (j == -1)
         goto jeso;
      if (j == 0)
         break;
      size += j;
      i -= j;
      /* For the current way of doing things a single read will suffice.
       * Note we'll be "penaltized" when awaiting EOF on the socket, at least
       * in blocking mode, so do avoid that and break off */
      break;
   }
   i = 0;
   vcp->vc_buffer[size] = '\0';

   if (size == 0 || size == BUFFER_SIZE) {
jebogus:
      fprintf(stderr,
         _("%s`%s': bogus spamd(1) I/O interaction (%" PRIuZ ")\n"),
         vcp->vc_esep, _spam_comms[vcp->vc_action], i);
# ifdef HAVE_DEVEL
      if (options & OPT_VERBVERB)
         fprintf(stderr, ">>> BUFFER: %s <<<\n", vcp->vc_buffer);
# endif
      goto jleave;
   }

   /* From the response, read those lines that interest us */
   for (lp = vcp->vc_buffer; size > 0; ++i) {
      cp = lp;
      lp = strchr(lp, NETNL[0]);
      if (lp == NULL)
         goto jebogus;
      lp[0] = '\0';
      if (lp[1] != NETNL[1])
         goto jebogus;
      lp += 2;
      size -= PTR2SIZE(lp - cp);

      if (i == 0) {
         if (!strncmp(cp, "SPAMD/1.1 0 EX_OK", sizeof("SPAMD/1.1 0 EX_OK") -1))
            continue;
         if (vcp->vc_action != _SPAM_RATE ||
               strstr(cp, "Service Unavailable") == NULL)
            goto jebogus;
         else {
            /* Unfortunately a missing --allow-tell drops connection.. */
            fprintf(stderr,
               _("%s`%s': service not available in spamd(1) instance\n"),
               vcp->vc_esep, _spam_comms[vcp->vc_action]);
            goto jleave;
         }
      } else if (i == 1) {
         switch (vcp->vc_action) {
         case _SPAM_RATE:
            if (strncmp(cp, "Spam: ", sizeof("Spam: ") -1))
               goto jebogus;
            cp += sizeof("Spam: ") -1;

            if (!strncmp(cp, "False", sizeof("False") -1)) {
               cp += sizeof("False") -1;
               vcp->vc_mp->m_flag &= ~MSPAM;
            } else if (!strncmp(cp, "True", sizeof("True") -1)) {
               cp += sizeof("True") -1;
               vcp->vc_mp->m_flag |= MSPAM;
            } else
               goto jebogus;

            while (blankspacechar(*cp))
               ++cp;

            if (*cp++ != ';')
               goto jebogus;
            _spam_rate2score(vcp, cp);
            goto jdone;

         case _SPAM_HAM:
         case _SPAM_SPAM:
            /* Empty response means ok but "did nothing" */
            if (*cp != '\0' &&
                  strncmp(cp, "DidSet: local", sizeof("DidSet: local") -1))
               goto jebogus;
            if (*cp == '\0' && vcp->vc_verbose)
               fputs(_("\tBut spamd(1) \"did nothing\" for message\n"), stderr);
            vcp->vc_mp->m_flag &= ~(MSPAM | MSPAMUNSURE);
            if (vcp->vc_action == _SPAM_SPAM)
               vcp->vc_mp->m_flag |= MSPAM;
            goto jdone;

         case _SPAM_FORGET:
            if (*cp != '\0' &&
                  strncmp(cp, "DidRemove: local", sizeof("DidSet: local") -1))
               goto jebogus;
            if (*cp == '\0' && vcp->vc_verbose)
               fputs(_("\tBut spamd(1) \"did nothing\" for message\n"), stderr);
            vcp->vc_mp->m_flag &= ~(MSPAM | MSPAMUNSURE);
            goto jdone;
         }
      }
   }

jdone:
   rv = TRU1;
jleave:
   if (headbuf != NULL)
      ac_free(headbuf);
   if (dsfd >= 0)
      close(dsfd);

   safe_signal(SIGQUIT, vcp->vc_type.d.spamd_oquit);
   safe_signal(SIGINT, vcp->vc_type.d.spamd_oint);
   safe_signal(SIGHUP, vcp->vc_type.d.spamd_ohup);
   safe_signal(SIGPIPE, vcp->vc_type.d.spamd_opipe);
   safe_signal(SIGTSTP, vcp->vc_type.d.spamd_otstp);
   safe_signal(SIGTTIN, vcp->vc_type.d.spamd_ottin);
   safe_signal(SIGTTOU, vcp->vc_type.d.spamd_ottou);

   NYD2_LEAVE;
   if (__spamd_sig != 0) {
      sigset_t cset;
      sigemptyset(&cset);
      sigaddset(&cset, __spamd_sig);
      sigprocmask(SIG_UNBLOCK, &cset, NULL);
      kill(0, __spamd_sig);
      assert(rv == FAL0);
   }
   return rv;
}
#endif /* HAVE_SPAM_SPAMD */

static void
_spam_rate2score(struct spam_vc *vcp, char *buf)
{
   char *cp;
   size_t size;
   ui32_t m, s;
   NYD2_ENTER;

   cp = strchr(buf, '/');
   if (cp == NULL)
      goto jleave;
   size = PTR2SIZE(cp - buf);
   buf[size] = '\0';

   m = (ui32_t)strtol(buf, &cp, 10);
   if (cp == buf)
      goto jleave;

   s = (*cp == '\0') ? 0 : (ui32_t)strtol(++cp, NULL, 10);

   vcp->vc_mp->m_spamscore = (m << 8) | (s & 0xFF);
jleave:
   NYD2_LEAVE;
}

FL int
c_spam_clear(void *v)
{
   int *ip;
   NYD_ENTER;

   for (ip = v; *ip != 0; ++ip)
      message[(size_t)*ip - 1].m_flag &= ~MSPAM;
   NYD_LEAVE;
   return 0;
}

FL int
c_spam_set(void *v)
{
   int *ip;
   NYD_ENTER;

   for (ip = v; *ip != 0; ++ip)
      message[(size_t)*ip - 1].m_flag |= MSPAM;
   NYD_LEAVE;
   return 0;
}

FL int
c_spam_forget(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _spam_action(_SPAM_FORGET, (int*)v) ? OKAY : STOP;
   NYD_LEAVE;
   return rv;
}

FL int
c_spam_ham(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _spam_action(_SPAM_HAM, (int*)v) ? OKAY : STOP;
   NYD_LEAVE;
   return rv;
}

FL int
c_spam_rate(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _spam_action(_SPAM_RATE, (int*)v) ? OKAY : STOP;
   NYD_LEAVE;
   return rv;
}

FL int
c_spam_spam(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _spam_action(_SPAM_SPAM, (int*)v) ? OKAY : STOP;
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_SPAM */

/* s-it-mode */
