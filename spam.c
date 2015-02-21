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
/*
 * TODO - We cannot use the spamc library because of our jumping behaviour.
 * TODO   We could nonetheless if we'd start a fork(2)ed child which would
 * TODO   use the spamc library.
 * TODO -- In fact using a child process that is immune from the terrible
 * TODO    signal and jumping mess, and that controls further childs, and
 * TODO    gains file descriptors via sendmsg(2), and is started once it is
 * TODO    needed first, i have in mind for quite some time, for the transition
 * TODO    to a select(2) based implementation: we could slowly convert SMTP,
 * TODO    etc., finally IMAP, at which case we could rejoin back into a single
 * TODO    process program (unless we want to isolate the UI at that time, to
 * TODO    allow for some xUI protocol).
 * TODO    :: That is to say -- it's a horrible signal and jump mess ::
 * TODO - We do not yet handle direct communication with spamd(1).
 * TODO   I.e., this could be a lean alternative to the first item;
 * TODO   the protocol is easy and we could support ALL operations easily.
 * TODO - We do not yet update mails in place, i.e., replace the original
 * TODO   message with the updated one; we could easily do it as if `edit' has
 * TODO   been used, but the nail codebase doesn't truly support that for IMAP.
 * TODO   (And it seems a bit grazy to download a message, update it and upload
 * TODO   it again.)
 */

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
            fprintf(stderr, _("`%s': checking message %" PRIuZ "\n"),
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
